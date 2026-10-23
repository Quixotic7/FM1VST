// SPDX-License-Identifier: GPL-3.0-only
// Video recording (the Standalone app; plugins leave it out: the DAW records): the firmware playing as a movie file,
// its picture rendered by FM1VST itself and its sound FM1VST's own output, plus an optional mic / line input.
//
// THE PICTURE is never a screen capture (no Screen Recording permission; covering or moving the window changes
// nothing): a view source renders it offscreen with the editor's own paint code, on the recorder's thread:
//   Device   a PanelComponent of its own (a mirror: no timer, no input, never on the desktop) painted at the frame's
//            size, letterboxed on the theme's backdrop, as the device view draws it (held keys and buttons lit; the
//            computer-key hints as the editor's setting, followed live). With the big LCD above the panel on (the
//            sheet's View, the ` key; read when the take starts, kept for the take) the frame is that layout: the LCD
//            square above the panel, both from the frame's one snapshot, in a portrait frame of the layout's aspect
//            (876 x 1414 units) with the short side kept: 720 x 1162 or 1080 x 1744 (even, about the pixels of the 16:9
//            sizes: the same drawing and encoding load, within H.264's limits; no bars around a letterboxed panel);
//   Screen   a ScreenView on such a mirror, square, centred on the backdrop;
//   LCD      the firmware's frame buffer (the processor's PanelView), nearest-neighbour: 240 -> 720 (3x) or 1080
//            (4.5x: each pixel a 4 or 5 pixel block, every output pixel exactly one LCD pixel's colour, as argbOf).
// Sizes: 1280 x 720 or 1920 x 1080 (Device, Screen), 720 x 1162 or 1080 x 1744 (Device with the big LCD), 720 x 720
// or 1080 x 1080 (LCD); 30 or 60 fps.
//
// THE SOUND is FM1VST's output captured in processBlock after the core rendered it (the Standalone has no output gain
// after it): AudioTap (lock-free, AudioTap.h), the processor's videoTap(). The mic / line input: the Standalone's audio
// device with input channels opened for the take (MicInput, a second callback on the holder's AudioDeviceManager),
// a mono channel (on both sides) or a stereo pair, a gain, "Separate" (a second audio track, the default) or "Mix"
// (summed into the main track), and an optional monitor through the outputs (off by default: feedback).
//
// SYNC: one clock, the main tap's sample counter. A take's timeline starts at the first sample of the first block
// after arming; audio track times are timeline frames / the device's rate, exactly. Frame k is shown from k / fps,
// exactly, and its CONTENT is tied to the same instant: after each block the audio thread copies the panel's snapshot
// (the LCD frame buffer, the LEDs, the held keys and buttons, the knobs: PanelView) into a ring for every frame whose
// instant k * rate / fps the block passed (SnapshotRing, lock-free; at most one block after the instant). The
// recorder's thread draws each frame from its own snapshot however late it gets to it, so a slow or loaded machine
// delays the drawing, never the picture against the sound. Behind by more than half the ring (64 of its kFrameSlots:
// about 1 s at 60 fps, 2 s at 30) it drops the oldest snapshots, and a full ring drops the newest: those frames
// repeat the last picture drawn (counted: Result::dropped, the sheet, the take's toast), never the state of a later
// moment. No drift is possible: video and audio count the same samples, and a device whose clock runs fast or slow
// against the computer's changes both together. The mic's blocks go on the same timeline: its callback runs right after
// the processor's in the same device cycle, so a block of n frames there is the main tap's last n (pushAt; a cycle the
// processor skipped is dropped from the mic, a gap filled with silence).
//
// A TAKE ENDS by Stop (the button, Cmd+Shift+R), and cleanly by itself at a firmware switch or reboot (any power-on:
// the processor freezes the tap before the new firmware's first sample, like the site's recorder ending a take before
// an engine load: one firmware per take, named after it), at an audio device change, when the recorder falls behind by
// the ring's 10 s, or on a write error (a full disk): the file is then finished with what was written (the movie
// fragments keep even a crashed take readable).
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <map>
#include <memory>

#include "AudioTap.h"
#include "Processor.h"
#include "VideoRecorder.h"

namespace fm1video {

enum View { kViewDevice = 0, kViewScreen = 1, kViewLcd = 2 };
constexpr int kFrameSlots = 128;               // the snapshot ring (115 KB each: the LCD)

struct Settings {
    int view = kViewDevice;
    int size = 1080;                             // 720 or 1080
    int fps = 30;                                // 30 or 60
    bool mov = false;                            // .mov; else .mp4
    bool mic = false;
    juce::String inputDevice;                    // "" the system's default input
    int inChannel = 0;                           // the first input channel (0-based)
    int inChannels = 1;                          // 1: mono (both sides), 2: a stereo pair
    int micGainDb = 0;                           // -24 .. +24
    bool micMix = false;                         // false: "Separate", a track of its own
    bool monitor = false;
    juce::Point<int> frameSize(bool bigLcd = false) const;   // bigLcd: the big LCD shown (the Device view's portrait size)
    Settings normalised() const;
    juce::String toJson() const;
    static Settings fromJson(const juce::String &json);
    bool operator==(const Settings &o) const { return toJson() == o.toJson(); }
};

// a view rendered into a frame (ARGB, the frame's size) from the panel's snapshot taken at the frame's instant (the
// processor's videoFrames()): called on the recorder's thread only
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual void render(juce::Image &frame, const PanelView &state) = 0;
};
// the processor's view (made on the message thread; then the recorder's thread's alone); bigLcd: the Device view with
// the big LCD above the panel
std::unique_ptr<FrameSource> makeViewSource(FM1Processor &p, int view, juce::Point<int> size, bool bigLcd = false);
// the LCD alone, nearest-neighbour from 240 x 240 RGB565 big-endian (as sent) into a side x side ARGB image
void scaleLcd(const uint16_t *lcdBe, juce::Image &frame);

// the mic / line input: a callback on the audio device, after the processor's player
class MicInput : public juce::AudioIODeviceCallback {
public:
    MicInput(AudioTap &main, AudioTap &mic) : main_(main), mic_(mic) {}
    void configure(int firstChannel, int channels, float gainDb, bool monitor);
    void setGainDb(float db) { gain_.store(juce::Decibels::decibelsToGain(db, -60.0f)); }
    void setMonitor(bool on) { monitor_.store(on); }
    bool deviceStopped() const { return stopped_.load(); }
    void clearStopped() { stopped_.store(false); }
    float takePeak() { return peak_.exchange(0.0f); }
    // (tests drive it as a device would: the active inputs compacted, as JUCE passes them)
    void audioDeviceIOCallbackWithContext(const float *const *in, int nIn, float *const *out, int nOut, int n,
                                          const juce::AudioIODeviceCallbackContext &) override;
    void audioDeviceAboutToStart(juce::AudioIODevice *d) override;
    void audioDeviceStopped() override { stopped_.store(true); }

private:
    AudioTap &main_, &mic_;
    std::atomic<int> first_{0}, count_{1}, idxA_{0}, idxB_{1};
    std::atomic<float> gain_{1.0f}, peak_{0.0f};
    std::atomic<bool> monitor_{false}, stopped_{false};
};

// the frame clock and the writer (see SYNC above)
class Recorder : private juce::Thread {
public:
    struct Config {
        EncoderSpec spec;
        AudioTap *main = nullptr;                // the master clock
        SnapshotRing<PanelView> *frames = nullptr;   // what each frame shows, captured at its instant
        AudioTap *mic = nullptr;                 // optional
        bool mix = false;                        // the mic summed into track 0 (else track 1)
        std::unique_ptr<FrameSource> source;
    };
    struct Result {
        bool ok = false;
        juce::String error;                      // why it failed or ended early ("" a clean Stop)
        juce::File file;
        double seconds = 0;
        int64_t endFrame = 0, videoFrames = 0, duplicated = 0, rendered = 0;
        int maxLagMs = 0;                        // the latest a frame was rendered after its instant (timeline ms)
        int64_t dropped = 0;                     // frames that repeat an earlier picture: no room in the ring, or drawing fell
                                                 // more than half a ring behind
        double renderMsMax = 0, renderMsTotal = 0;   // the view's drawing time (rendered: how many)
    };
    Recorder();
    ~Recorder() override;
    // (message thread) opens the file and starts the clock; the caller arms the taps after it
    bool start(Config c, juce::String &error);
    // (message thread) the take ends at the main tap's current end; the file finished; blocks
    Result stop();
    bool active() const { return started_ && !collected_; }
    bool ended() const { return started_ && !isThreadRunning(); }   // ended by itself: stop() collects the result
    double seconds() const;
    int64_t droppedFrames() const { return dropped_.load() + (c_.frames ? c_.frames->dropped() : 0); }
    juce::Image firstFrame() const;              // (tests) the first frame rendered

private:
    void run() override;
    bool drainAudio(int64_t upTo, bool final);
    struct Pending {
        std::vector<float> data;
        int64_t pos = 0;
        int k = 0;
    };
    Pending pend_[2];
    bool flushPending(int track, bool final);
    bool drainFrames(int64_t limit, int64_t now);
    int64_t framesBefore(int64_t timelineFrame) const;   // frames whose instant is before it

    Config c_;
    Encoder enc_;
    juce::Image frame_, first_;
    mutable juce::SpinLock firstLock_;
    std::vector<float> b_;
    int64_t next_ = 0, micPos_ = 0;
    int sr_ = 48000;
    bool started_ = false, collected_ = false, micAbsent_ = false, drawn_ = false;
    std::atomic<int64_t> dropped_{0};
    std::atomic<int64_t> end_{-1};
    Result res_;
};

// the Standalone's recorder, owned by the processor (FM1Processor::videoSession): the settings (in the app's settings),
// the take (its file, its view, the mic input), its end at a power-on or a device change. Message thread only.
class Session : private juce::Timer {
public:
    explicit Session(FM1Processor &p);
    ~Session() override;                         // a take in progress is finished
    static bool available(const FM1Processor &p);   // the Standalone app (not a plugin)

    const Settings &settings() const { return s_; }
    // the Device view with the big LCD above the panel, and the frame's size: a take's own while recording, else what
    // a take started now would be (the setting, the processor's big LCD)
    bool bigLcd() const { return rec_ ? takeBig_ : s_.view == kViewDevice && p_.getBigLcd(); }
    juce::Point<int> frameSize() const { return rec_ ? takeSize_ : s_.frameSize(bigLcd()); }
    void setSettings(const Settings &s);         // saved; the monitor follows
    bool start();                                // false: lastError() says why
    void stop();
    bool recording() const { return rec_ != nullptr; }
    double seconds() const { return rec_ ? rec_->seconds() : 0.0; }
    int64_t droppedFrames() const { return rec_ ? rec_->droppedFrames() : 0; }   // (this take so far)
    juce::File currentFile() const { return file_; }
    juce::File lastFile() const { return last_.file; }
    const Recorder::Result &lastResult() const { return last_; }
    juce::String lastError() const { return error_; }
    uint32_t serial() const { return serial_; }  // moves at every start and end
    std::function<void()> onChange;              // a take started or ended (the editor: its button, a toast)

    // where takes go: ~/Movies/FM1VST (tests: another folder)
    juce::File folder() const;
    void setFolder(const juce::File &f) { folder_ = f; }
    juce::File nextFile() const;                 // <firmware id>-<version>-<YYYYMMDD-HHMMSS>.mp4 / .mov, unique

    // the mic: turning it on asks for the microphone permission first (the system's prompt, once); done(ok, why)
    void setMic(bool on, std::function<void(bool, juce::String)> done = nullptr);
    bool hasAudioDevice() const;
    juce::StringArray inputDevices() const;
    juce::String inputDevice() const;            // the setting, or the default input when it is ""
    juce::StringArray inputChannelNames(const juce::String &device) const;
    float takeMicPeak() { return mic_ ? mic_->takePeak() : 0.0f; }

    // (tests) the taps and the recorder
    AudioTap &micTap() { return micTap_; }
    Recorder *recorderForTest() { return rec_.get(); }

private:
    void timerCallback() override;
    void endTake();
    juce::String openInput(bool on);             // "" ok, else why
    void applyMonitor();
    void save() const;

    FM1Processor &p_;
    Settings s_;
    juce::File folder_, file_;
    AudioTap micTap_;
    std::unique_ptr<MicInput> mic_;
    bool micAttached_ = false, inputOpen_ = false;
    juce::AudioDeviceManager::AudioDeviceSetup saved_;
    std::unique_ptr<Recorder> rec_;
    bool takeBig_ = false;
    juce::Point<int> takeSize_;
    Recorder::Result last_;
    juce::String error_;
    uint32_t serial_ = 0;
    mutable std::map<juce::String, juce::StringArray> channelNames_;
};

juce::String formatTime(double seconds);         // "0:07", "12:34", "1:02:03"

}   // namespace fm1video
