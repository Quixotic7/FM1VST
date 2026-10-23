// SPDX-License-Identifier: GPL-3.0-only
// Video recording (plugin/VideoCapture.h, plugin/VideoRecorder.h), headless: no window, no real audio device, never
// the real microphone. A simulated device thread runs in real time (48 kHz, 480-frame blocks): each cycle the processor
// (ChoralRoot, a key held) renders a block (the video tap captures it in processBlock), then the mic callback gets a
// synthetic input, a 1 kHz tone at 0.25 on channel 1, as the device's second callback would. Every file is read back
// with AVAssetReader.
//   (t) AudioTap: a take starts at the first block after arm; pushAt fills a gap with silence and drops an overlap; a
//       full ring ends the take (overflowed); freeze ends it with a reason; MicInput's channels, gain and monitor;
//   (a) 5 s, the LCD view at 720 x 720, 30 fps, .mp4, the mic Separate at -6 dB: the duration within a frame, 1 video
//       + 2 audio tracks (H.264, AAC 48 kHz stereo), 150 frames, the first frame against the offscreen render (region
//       means), the main track's RMS against what the processor rendered, the mic track's tone at -6 dB, no tone in
//       the main track (the file kept: build/video_rec_test_home/take-lcd30.mp4);
//   (b) 5 s, the device view at 1280 x 720, 60 fps, .mov, the mic Mixed at 0 dB: 1 video + 1 audio track, 300 frames,
//       the first frame, the tone in the main track;
//   (s) 2 s, the screen view at 1920 x 1080, 30 fps: 60 frames, the first frame;
//   (d) the device view with the big LCD above the panel: the sizes (720 x 1162, 1080 x 1744: portrait, the layout's
//       aspect, even; Screen and LCD unchanged; the session reads the big LCD when a take starts and keeps it for the
//       take); 2 s of the processor at 1080 x 1744, 30 fps; a synthetic snapshot (an LCD pattern, a key held): the
//       recorder's first frame is exactly the offscreen render of that layout (a PanelComponent of its own, the big
//       LCD on) and the decoded one matches it; the computer-key hints off: the recorder's frame follows (no hints);
//   (y) A/V sync, synthetic: a click in the audio and a white flash in the panel snapshot of the same block, every second
//       for 30 s (30 fps): each click's and flash's times decoded; every offset under a frame, no drift; again with a
//       renderer slower than the frame rate (50 ms a frame, 12 s): frames are dropped (counted), and every flash that
//       remains is still on its click. Not on GitHub's runners (GITHUB_ACTIONS: timing on shared runners); it is meant to
//       pass on any Mac, loaded or not (taskpolicy -b and every core busy: checked); and with the device view and the
//       big LCD (12 s, 720 x 1162): the flash is the big LCD's, the panel and the LCD drawn from one snapshot;
//   (e) errors: an unwritable folder (the session) and an unwritable file (the recorder): a clean message, no file;
//   (w) the session: a take into a folder of its own (<id>-<version>-<date>.mp4), a power cycle ends it cleanly with
//       "the firmware restarted", the file finished and readable;
//   (k) a recording process killed mid-take (SIGKILL, no finish): the .mov and the .mp4 stay readable up to their last
//       movie fragment (2 s).
// FM1EMU_HOME points at a scratch folder under the build.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <sys/stat.h>

#include "built_cores.h"
#include "AudioTap.h"
#include "PanelComponent.h"
#include "Processor.h"
#include "ScreenCapture.h"
#include "VideoCapture.h"
#include "VideoRecorder.h"

using namespace fm1video;

static int fails = 0;
static void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    std::fflush(stdout);
    fails += !c;
}
static std::string f2(double v, int prec = 3)
{
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 480;
constexpr double kTone = 0.25;

// ------------------------------------------------- the simulated device ---
// a device cycle: the main render (the processor, or a synthetic one), then the mic callback; paced in real time
struct SimDevice : juce::Thread {
    std::function<void(juce::AudioBuffer<float> &)> render;
    MicInput *mic = nullptr;
    AudioTap *main = nullptr;
    std::atomic<int64_t> stopAt{-1};             // pause once the main tap's take is this long
    std::atomic<bool> paused{false};
    std::vector<double> energy;                  // what the main render made during the take: per frame, L^2 + R^2
    double phase = 0;
    SimDevice() : juce::Thread("sim device") {}
    ~SimDevice() override { stopThread(-1); }   // (no timeout: a starved device thread must never outlive its take)
    void run() override
    {
        juce::AudioBuffer<float> buf(2, kBlock), in(2, kBlock), out(2, kBlock);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int64_t cycle = 0; !threadShouldExit(); cycle++) {
            if (!paused.load()) {
                buf.clear();
                render(buf);
                const int64_t have = main && main->running() ? main->produced() : -1;   // (not the last take's length)
                if (have > 0 && (int64_t)energy.size() < have)   // (this block is on the take's timeline)
                    for (int i = 0; i < kBlock; i++)
                        energy.push_back((double)buf.getSample(0, i) * buf.getSample(0, i) + (double)buf.getSample(1, i) * buf.getSample(1, i));
                if (mic) {
                    for (int i = 0; i < kBlock; i++) {
                        in.setSample(0, i, (float)(kTone * std::sin(phase)));
                        in.setSample(1, i, 0.0f);
                        phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / kRate;
                    }
                    phase = std::fmod(phase, 2.0 * juce::MathConstants<double>::pi);
                    juce::AudioIODeviceCallbackContext ctx;
                    mic->audioDeviceIOCallbackWithContext(in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock, ctx);
                }
                if (main && stopAt.load() >= 0 && main->produced() >= stopAt.load())
                    paused.store(true);
            }
            const double next = t0 + (double)(cycle + 1) * kBlock * 1000.0 / kRate;
            const double wait = next - juce::Time::getMillisecondCounterHiRes();
            if (wait > 0)
                juce::Thread::sleep((int)wait);
        }
    }
};

static void waitPaused(SimDevice &d, double maxMs)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (!d.paused.load() && juce::Time::getMillisecondCounterHiRes() - t0 < maxMs)
        juce::Thread::sleep(5);
}

// until the tap's take is this long (at most a minute)
static void waitTake(AudioTap &t, double seconds)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (t.produced() < (int64_t)(seconds * kRate) && juce::Time::getMillisecondCounterHiRes() - t0 < 60000)
        juce::Thread::sleep(5);
}

// ------------------------------------------------------------ analysis ---
static double rms(const std::vector<float> &x, int rate, double skipS, int channel)
{
    const size_t frames = x.size() / 2, skip = (size_t)(skipS * rate);
    double s = 0;
    size_t n = 0;
    for (size_t i = skip; i + skip < frames; i++, n++)
        s += (double)x[i * 2 + (size_t)channel] * x[i * 2 + (size_t)channel];
    return n ? std::sqrt(s / (double)n) : 0.0;
}
static double rmsBoth(const std::vector<float> &x, int rate, double skipS)
{
    const double l = rms(x, rate, skipS, 0), r = rms(x, rate, skipS, 1);
    return std::sqrt((l * l + r * r) / 2);
}
// the amplitude of a sine at f in channel 0 (Goertzel over the middle of the track)
static double toneAmp(const std::vector<float> &x, int rate, double f)
{
    const size_t frames = x.size() / 2, skip = (size_t)(0.25 * rate);
    if (frames <= 2 * skip)
        return 0;
    const double w = 2.0 * juce::MathConstants<double>::pi * f / rate, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    size_t n = 0;
    for (size_t i = skip; i + skip < frames; i++, n++) {
        const double s0 = x[i * 2] + c * s1 - s2;
        s2 = s1, s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / (double)n;
}
static double db(double r, double ref) { return 20.0 * std::log10(juce::jmax(1e-9, r) / juce::jmax(1e-9, ref)); }

// the first decoded frame against an image: per-region mean colour (cols x rows), the worst and the average channel
// difference
struct Cmp {
    double worst = 999, mean = 999;
};
static Cmp compareFirst(const juce::File &f, const juce::Image &ref, int cols, int rows)
{
    Cmp out;
    juce::String err;
    bool got = false;
    decodeVideo(
        f,
        [&](double, const uint8_t *px, int w, int h, int stride) {
            if (w != ref.getWidth() || h != ref.getHeight())
                return false;
            const juce::Image::BitmapData rd(ref, juce::Image::BitmapData::readOnly);
            double worst = 0, sum = 0;
            int cells = 0;
            for (int cy = 0; cy < rows; cy++)
                for (int cx = 0; cx < cols; cx++) {
                    double a[3] = {}, b[3] = {};
                    int n = 0;
                    for (int y = cy * h / rows; y < (cy + 1) * h / rows; y++)
                        for (int x = cx * w / cols; x < (cx + 1) * w / cols; x++, n++) {
                            const uint8_t *p = px + (size_t)y * (size_t)stride + (size_t)x * 4;   // BGRA
                            const uint8_t *q = rd.getPixelPointer(x, y);                         // ARGB: BGRA in memory
                            for (int c = 0; c < 3; c++)
                                a[c] += p[c], b[c] += q[c];
                        }
                    for (int c = 0; c < 3; c++) {
                        const double d = std::abs(a[c] - b[c]) / juce::jmax(1, n);
                        worst = juce::jmax(worst, d);
                        sum += d;
                    }
                    cells++;
                }
            out.worst = worst;
            out.mean = sum / (cells * 3);
            got = true;
            return false;                        // (the first frame only)
        },
        err);
    if (!got)
        std::printf("    (no first frame: %s)\n", err.toRawUTF8());
    return out;
}

// --------------------------------------------------------- a real take ---
struct TakeSpec {
    const char *name;
    int view, w, h, fps;
    bool mov, mic, mix;
    float gainDb;
    double seconds;
    bool bigLcd = false;                         // (the device view: the big LCD above the panel)
};

static juce::File scratchDir() { return juce::File(FM1_SCRATCH_DIR); }

static void processorTake(FM1Processor &p, const TakeSpec &t)
{
    std::printf("\n(%s) %d x %d, %d fps, %s, %s%s\n", t.name, t.w, t.h, t.fps, t.mov ? ".mov" : ".mp4",
                !t.mic ? "no mic" : t.mix ? "mic mixed" : "mic separate", t.bigLcd ? ", the big LCD above the panel" : "");
    std::fflush(stdout);
    {                                            // (the chord struck again: it decays)
        juce::AudioBuffer<float> b(2, kBlock);
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::noteOff(1, 62), 0);
        m.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), kBlock / 2);
        p.processBlock(b, m);
    }
    AudioTap micTap;
    MicInput mic(p.videoTap(), micTap);
    mic.configure(0, 1, t.gainDb, false);
    mic.audioDeviceAboutToStart(nullptr);
    SimDevice dev;
    dev.main = &p.videoTap();
    dev.mic = t.mic ? &mic : nullptr;
    dev.render = [&p](juce::AudioBuffer<float> &b) {
        juce::MidiBuffer m;
        p.processBlock(b, m);
    };
    const int64_t total = (int64_t)(t.seconds * kRate);
    dev.startThread();
    juce::Thread::sleep(100);

    const juce::File file = scratchDir().getChildFile(juce::String("take-") + t.name + (t.mov ? ".mov" : ".mp4"));
    Recorder::Config c;
    c.spec.file = file;
    c.spec.mov = t.mov;
    c.spec.width = t.w;
    c.spec.height = t.h;
    c.spec.fps = t.fps;
    c.spec.inputRate = kRate;
    c.main = &p.videoTap();
    c.frames = &p.videoFrames();
    c.mic = t.mic ? &micTap : nullptr;
    c.mix = t.mix;
    c.source = makeViewSource(p, t.view, {t.w, t.h}, t.bigLcd);
    p.videoTap().allocate();
    p.videoFrames().allocate(kFrameSlots);
    micTap.allocate();
    Recorder rec;
    juce::String err;
    const bool started = rec.start(std::move(c), err);
    check(started, "the recorder starts" + (err.isEmpty() ? std::string() : ": " + err.toStdString()));
    if (!started)
        return;
    if (t.mic)
        micTap.arm();
    p.videoTap().arm();
    dev.stopAt.store(total);                     // (after the arm: the tap still says how long the last take was)
    waitPaused(dev, t.seconds * 1000 + 60000);
    const Recorder::Result r = rec.stop();
    dev.stopThread(-1);
    const juce::Image first = rec.firstFrame();
    check(r.ok && r.endFrame == total, "the take ends at " + std::to_string(r.endFrame) + " frames (" + f2(r.seconds) +
                                           " s), rendered " + std::to_string(r.rendered) + ", repeated " +
                                           std::to_string(r.duplicated) + ", at most " + std::to_string(r.maxLagMs) +
                                           " ms after its instant, " + std::to_string(r.dropped) + " dropped; drawing " + f2(r.rendered ? r.renderMsTotal / (double)r.rendered : 0.0, 1) + " ms a frame (" + f2(r.renderMsMax, 1) + " at most)" + (r.error.isEmpty() ? "" : ": " + r.error.toStdString()));

    const Probe pr = probe(file);
    const double frameS = 1.0 / t.fps;
    check(pr.ok && std::abs(pr.duration - t.seconds) <= frameS,
          "duration " + f2(pr.duration) + " s (within a frame of " + f2(t.seconds) + "); video " + f2(pr.videoDuration) + " s");
    const int wantAudio = t.mic && !t.mix ? 2 : 1;
    check(pr.videoTracks == 1 && pr.audioTracks == wantAudio, "tracks: " + std::to_string(pr.videoTracks) + " video + " +
                                                                  std::to_string(pr.audioTracks) + " audio (" +
                                                                  std::to_string(wantAudio) + " expected)");
    const int64_t wantFrames = (int64_t)std::llround(t.seconds * t.fps);
    check(pr.videoFrames == wantFrames && pr.width == t.w && pr.height == t.h && pr.videoCodec == "avc1",
          std::to_string(pr.videoFrames) + " frames (" + std::to_string(wantFrames) + "), " + std::to_string(pr.width) + " x " +
              std::to_string(pr.height) + ", " + pr.videoCodec.toStdString() + ", nominal " + f2(pr.nominalFps, 2) + " fps");
    bool aac = pr.audioTracks == wantAudio;
    for (int i = 0; i < pr.audioTracks; i++)
        aac = aac && pr.audioCodecs[(size_t)i] == "aac " && pr.audioRates[(size_t)i] == 48000 && pr.audioChannels[(size_t)i] == 2 &&
              std::abs(pr.audioDurations[(size_t)i] - t.seconds) <= frameS;
    check(aac, "every audio track AAC, 48 kHz, stereo, " + (pr.audioDurations.empty() ? std::string("?") : f2(pr.audioDurations[0])) + " s");

    const Cmp cmp = compareFirst(file, first, t.w > t.h ? 16 : 12, t.w > t.h ? 9 : 12);
    check(cmp.worst <= 8.0 && cmp.mean <= 2.5, "the first frame is the offscreen render (region means: worst " + f2(cmp.worst, 2) +
                                                   ", average " + f2(cmp.mean, 2) + " of 255)");

    std::vector<float> main;
    int rate = 0;
    double start = 0;
    decodeAudio(file, 0, main, rate, start, err);
    double eSum = 0;                             // (over the frames the decoded RMS is measured on: 0.1 s in from each end)
    const size_t e0 = (size_t)(0.1 * kRate), e1 = (size_t)juce::jmin<int64_t>((int64_t)dev.energy.size(), total) - e0;
    for (size_t i = e0; i < e1; i++)
        eSum += dev.energy[i];
    const double expect = e1 > e0 ? std::sqrt(eSum / (2.0 * (double)(e1 - e0))) : 0.0;
    const double got = rmsBoth(main, rate, 0.1);
    const double mainTone = toneAmp(main, rate, 1000.0);
    if (!t.mix) {
        check(expect > 0.0001 && std::abs(db(got, expect)) < 0.5,
              "the main track's RMS " + f2(got, 4) + " is what the processor rendered, " + f2(expect, 4) + " (" +
                  f2(db(got, expect), 2) + " dB), from " + f2(start, 4) + " s");
        if (t.mic)
            check(mainTone < 0.02, "  ... with no 1 kHz tone in it (" + f2(mainTone, 4) + ")");
    } else {
        check(std::abs(mainTone - kTone * juce::Decibels::decibelsToGain(t.gainDb)) < 0.03,
              "Mix: the tone is in the main track at " + f2(mainTone, 4) + " (" + f2(kTone, 2) + " in)");
    }
    if (t.mic && !t.mix) {
        std::vector<float> mt;
        decodeAudio(file, 1, mt, rate, start, err);
        const double want = kTone * juce::Decibels::decibelsToGain(t.gainDb);
        const double a = toneAmp(mt, rate, 1000.0), mr = rms(mt, rate, 0.1, 0), mr1 = rms(mt, rate, 0.1, 1);
        check(std::abs(db(a, want)) < 0.5 && std::abs(db(mr, want / std::sqrt(2.0))) < 0.5 && std::abs(db(mr1, mr)) < 0.1,
              "the mic track: the 1 kHz tone at " + f2(a, 4) + " (" + f2(want, 4) + " expected: " + f2(t.gainDb, 1) +
                  " dB), RMS " + f2(mr, 4) + ", both sides (mono input)");
    }
}

// --------------------------------------------------------- A/V sync ---
// the synthetic state: white in the block holding a whole second's instant (1 s, 2 s, ..), else black, captured into
// the snapshot ring after each block as the processor captures its panel; the source draws the snapshot it is given
// (slowly if asked: a loaded machine's renderer)
struct FlashSource : FrameSource {
    int delayMs;
    explicit FlashSource(int d = 0) : delayMs(d) {}
    void render(juce::Image &frame, const PanelView &state) override
    {
        if (delayMs > 0)
            juce::Thread::sleep(delayMs);
        frame.clear(frame.getBounds(), state.lcd[0] ? juce::Colours::white : juce::Colours::black);
    }
};

// a synthetic device: the click at each whole second in the audio, the flash in the snapshot of the same block
static void flashDevice(SimDevice &dev, AudioTap &tap, SnapshotRing<PanelView> &ring, PanelView &view)
{
    dev.main = &tap;
    dev.render = [&tap, &ring, &view](juce::AudioBuffer<float> &b) {
        const int64_t base = tap.produced();
        bool second = false;
        for (int i = 0; i < kBlock; i++) {
            float v = 0;
            if (base >= 0) {                     // (the block will land at `base`: the click at whole seconds)
                const int64_t t = base + i, inSec = t % (int64_t)kRate;
                second = second || (t >= (int64_t)kRate && inSec == 0);
                if (t >= (int64_t)kRate && inSec < (int64_t)(0.004 * kRate))
                    v = (float)(0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * 2000.0 * (double)inSec / kRate));
            }
            b.setSample(0, i, v);
            b.setSample(1, i, v);
        }
        tap.push(b.getReadPointer(0), b.getReadPointer(1), kBlock);
        view.lcd.fill(second ? 0xFFFF : 0);
        view.lcdSeq++;                           // (a new copy of the LCD: the device view's mirror converts it again)
        if (tap.running())
            ring.capture(view, tap.produced());
    };
}

// a take of `seconds` with a flash and a click every second; every flash found must sit on its click (pairing by the
// second, so a flash lost to a dropped frame cannot shift the others), all 30 found when nothing was dropped
using MakeSource = std::function<std::unique_ptr<FrameSource>()>;
// (make: the view drawing the snapshots, else FlashSource; size, and where the flash is looked for: the frame's middle
// by default)
static void syncTest(const char *name, double seconds, int fps, int delayMs, const MakeSource &make = nullptr,
                     juce::Point<int> size = {640, 360}, juce::Rectangle<int> where = {})
{
    const int n = (int)seconds;
    std::printf("\n(y) A/V sync, %s: a click and a flash every second for %d s, %d fps, %d x %d%s\n", name, n, fps, size.x, size.y,
                delayMs ? (", the renderer taking " + std::to_string(delayMs) + " ms a frame").c_str() : "");
    std::fflush(stdout);
    AudioTap tap;
    tap.allocate();
    SnapshotRing<PanelView> ring;
    ring.allocate(kFrameSlots);
    PanelView view;
    SimDevice dev;
    flashDevice(dev, tap, ring, view);
    dev.startThread();
    const juce::File file = scratchDir().getChildFile(juce::String("take-sync-") + name + ".mp4");
    Recorder::Config c;
    c.spec.file = file;
    c.spec.width = size.x;
    c.spec.height = size.y;
    c.spec.fps = fps;
    c.spec.inputRate = kRate;
    c.main = &tap;
    c.frames = &ring;
    c.source = make ? make() : std::make_unique<FlashSource>(delayMs);
    if (where.isEmpty())
        where = {size.x / 4, size.y / 4, size.x / 2, size.y / 2};
    Recorder rec;
    juce::String err;
    check(rec.start(std::move(c), err), "the recorder starts " + err.toStdString());
    tap.arm();
    dev.stopAt.store((int64_t)((seconds + 0.5) * kRate));
    waitPaused(dev, (seconds + 0.5) * 1000 + 60000);
    const Recorder::Result r = rec.stop();
    dev.stopThread(-1);
    check(r.ok && r.videoFrames == (int64_t)std::llround((seconds + 0.5) * fps),
          "the take: " + f2(r.seconds) + " s, " + std::to_string(r.videoFrames) + " frames (" + std::to_string(r.rendered) +
              " drawn, " + std::to_string(r.dropped) + " dropped), drawn at most " + std::to_string(r.maxLagMs) +
              " ms after a frame's instant " + r.error.toStdString());

    // the flashes: the first white frame of each run (a dropped frame repeats the last picture, a flash too: only a run's
    // start says when; one off a whole second is a misplaced picture)
    std::vector<double> flashes;
    int misplaced = 0;
    bool was = false;
    decodeVideo(
        file,
        [&](double t, const uint8_t *px, int w, int h, int stride) {
            double s = 0;
            int cnt = 0;
            const juce::Rectangle<int> box = where.getIntersection({0, 0, w, h});
            for (int y = box.getY(); y < box.getBottom(); y += 8)
                for (int x = box.getX(); x < box.getRight(); x += 8, cnt++)
                    s += px[(size_t)y * (size_t)stride + (size_t)x * 4 + 1];
            const bool white = s / (double)juce::jmax(1, cnt) > 128;
            if (white && !was) {
                flashes.push_back(t);
                misplaced += std::abs(t - std::round(t)) > 0.25 / fps ? 1 : 0;
            }
            was = white;
            return true;
        },
        err);
    // the clicks: an onset after half a second of quiet that peaks within 5 ms
    std::vector<float> a;
    int rate = 0;
    double start = 0;
    decodeAudio(file, 0, a, rate, start, err);
    std::vector<double> clicks;
    int64_t quiet = 0;
    for (size_t i = 0; i < a.size() / 2; i++) {
        const float v = std::abs(a[i * 2]);
        if (v > 0.05f && quiet > rate / 2) {
            float peak = 0;
            for (size_t j = i; j < std::min(a.size() / 2, i + (size_t)(0.005 * rate)); j++)
                peak = std::max(peak, std::abs(a[j * 2]));
            if (peak > 0.4f)
                clicks.push_back(start + (double)i / rate);
        }
        quiet = v > 0.05f ? 0 : quiet + 1;
    }
    bool timesOk = clicks.size() == (size_t)n;
    for (size_t i = 0; i < clicks.size(); i++)
        timesOk = timesOk && std::abs(clicks[i] - (double)(i + 1)) < 0.005;
    check(timesOk, std::to_string(clicks.size()) + " clicks, each at its whole second (within 5 ms): the audio's start is trimmed exactly");
    double worst = 0, lo = 1e9, hi = -1e9, sum = 0, sx = 0, sxy = 0, sxx = 0;
    size_t paired = 0;
    for (double f : flashes) {
        const long sec = std::lround(f);
        if (sec < 1 || sec > (long)clicks.size())
            continue;
        const double ck = clicks[(size_t)sec - 1], off = f - ck;
        worst = std::max(worst, std::abs(off));
        lo = std::min(lo, off), hi = std::max(hi, off), sum += off;
        sx += ck, sxy += ck * off, sxx += ck * ck;
        paired++;
    }
    const double mean = paired ? sum / (double)paired : 0;
    const double slope = paired > 1 ? (sxy - sx * mean) / (sxx - sx * sx / (double)paired) : 0;
    check(paired == flashes.size() && misplaced == 0 && (r.dropped > 0 ? paired >= 1 : paired == (size_t)n),
          std::to_string(flashes.size()) + " flashes, every one on its second (" + std::to_string(misplaced) +
              " starting elsewhere)" + (r.dropped > 0 ? "; the others fell in dropped frames" : ""));
    check(paired > 0 && worst < 1.0 / fps, "the flash - click offsets: mean " + f2(mean * 1000, 2) + " ms, worst " +
                                               f2(worst * 1000, 2) + " ms (a frame is " + f2(1000.0 / fps, 1) + " ms)");
    check(paired > 0 && hi - lo < 1.0 / fps && std::abs(slope * seconds) < 0.5 / fps,
          "no drift: the offsets span " + f2((hi - lo) * 1000, 2) + " ms over " + std::to_string(n) + " s, slope " + f2(slope * 1e6, 2) + " us/s");
    std::printf("SYNC %s flashes=%zu/%d dropped=%lld mean_ms=%.2f worst_ms=%.2f span_ms=%.2f slope_us_per_s=%.2f max_lag_ms=%d\n", name,
                flashes.size(), n, (long long)r.dropped, mean * 1000, worst * 1000, (hi - lo) * 1000, slope * 1e6, r.maxLagMs);
}

// ------------------------------------------- the device view, the big LCD ---
// the device view's layout drawn by a PanelComponent of its own (the recorder's mirror is another one): the big LCD on,
// the processor's theme, names and hints, the given snapshot
static juce::Image referenceDevice(FM1Processor &p, juce::Point<int> size, const PanelView &v, PanelComponent::Layout *lay = nullptr)
{
    PanelComponent panel(p, true);
    panel.setButtonNames(p.buttonNames());
    panel.setTheme(p.getTheme());
    panel.setKeyHints(p.getKeyHints());
    panel.setVisible(true);
    panel.setBigLcd(true);
    panel.setBounds(0, 0, size.x, size.y);
    panel.showView(v);
    juce::Image img(juce::Image::ARGB, size.x, size.y, true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        panel.paintEntireComponent(g, true);
    }
    if (lay)
        *lay = panel.layout();
    return img;
}

static int imageDiff(const juce::Image &a, const juce::Image &b)   // the largest channel difference (999: sizes differ)
{
    if (!a.isValid() || a.getBounds() != b.getBounds())
        return 999;
    const juce::Image::BitmapData da(a, juce::Image::BitmapData::readOnly), db(b, juce::Image::BitmapData::readOnly);
    int worst = 0;
    for (int y = 0; y < a.getHeight(); y++)
        for (int x = 0; x < a.getWidth(); x++) {
            const uint8_t *pa = da.getPixelPointer(x, y), *pb = db.getPixelPointer(x, y);
            for (int c = 0; c < 3; c++)
                worst = juce::jmax(worst, std::abs((int)pa[c] - (int)pb[c]));
        }
    return worst;
}

// a synthetic snapshot: an LCD pattern (bands of colour), key 7 held and lit, PLAY lit
static PanelView patternView()
{
    PanelView v;
    v.running = true;
    v.lcdSeq = 1;
    const uint16_t band[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};   // (RGB565: red, green, blue, white)
    for (int y = 0; y < EMU_LCD_H; y++)
        for (int x = 0; x < EMU_LCD_W; x++) {
            const uint16_t c = band[(y / 60) % 4];
            v.lcd[(size_t)(y * EMU_LCD_W + x)] = (uint16_t)((c >> 8) | (c << 8));   // (big-endian, as sent)
        }
    v.keys = 1u << 7;
    v.keyLed[7] = 255;
    v.btnLed[EMU_B_PLAY] = 255;
    return v;
}

// a take of the synthetic snapshot in the device view with the big LCD: its first frame against the reference render
static void bigLcdLayoutTake(FM1Processor &p, const char *name, juce::Point<int> size)
{
    std::printf("\n(d) %s: the device view with the big LCD, a fixed snapshot, %d x %d, 30 fps, hints %s\n", name, size.x, size.y,
                p.getKeyHints() ? "on" : "off");
    std::fflush(stdout);
    AudioTap tap;
    tap.allocate();
    SnapshotRing<PanelView> ring;
    ring.allocate(kFrameSlots);
    const PanelView view = patternView();
    SimDevice dev;
    dev.main = &tap;
    dev.render = [&tap, &ring, &view](juce::AudioBuffer<float> &b) {
        tap.push(b.getReadPointer(0), b.getReadPointer(1), kBlock);
        if (tap.running())
            ring.capture(view, tap.produced());
    };
    dev.startThread();
    const juce::File file = scratchDir().getChildFile(juce::String("take-") + name + ".mp4");
    Recorder::Config c;
    c.spec.file = file;
    c.spec.width = size.x;
    c.spec.height = size.y;
    c.spec.fps = 30;
    c.spec.inputRate = kRate;
    c.main = &tap;
    c.frames = &ring;
    c.source = makeViewSource(p, kViewDevice, size, true);
    Recorder rec;
    juce::String err;
    check(rec.start(std::move(c), err), "the recorder starts " + err.toStdString());
    tap.arm();
    dev.stopAt.store((int64_t)kRate);
    waitPaused(dev, 60000);
    const Recorder::Result r = rec.stop();
    dev.stopThread(-1);
    const Probe pr = probe(file);
    check(r.ok && pr.ok && pr.width == size.x && pr.height == size.y && pr.videoFrames == 30,
          "the take: " + std::to_string(pr.width) + " x " + std::to_string(pr.height) + ", " + std::to_string(pr.videoFrames) +
              " frames " + r.error.toStdString());
    PanelComponent::Layout lay;
    const juce::Image ref = referenceDevice(p, size, view, &lay);
    const juce::Image first = rec.firstFrame();
    const int d = imageDiff(first, ref);
    check(d == 0 && lay.lcdPx > 0 && lay.lcdBig.getWidth() >= size.x * 9 / 10 && lay.lcdBig.getBottom() <= lay.frY + lay.lcdPx,
          "the recorder's first frame is the offscreen render of the layout (the big LCD " + std::to_string(lay.lcdBig.getWidth()) +
              " px square above the panel), pixel for pixel (largest difference " + std::to_string(d) + ")");
    const Cmp cmp = compareFirst(file, ref, 9, 15);
    check(cmp.worst <= 8.0 && cmp.mean <= 2.5, "the decoded first frame matches it (region means: worst " + f2(cmp.worst, 2) +
                                                   ", average " + f2(cmp.mean, 2) + " of 255)");
    {
        const juce::File f = scratchDir().getChildFile(juce::String("frame-") + name + ".png");
        f.deleteFile();
        juce::FileOutputStream out(f);
        juce::PNGImageFormat png;
        if (first.isValid() && out.openedOk())
            png.writeImageToStream(first, out);
    }
}

// ------------------------------------------------------------ the child ---
// (k) a process that records the synthetic source until it is killed
static int childMain(const juce::String &path, bool mov)
{
    juce::ScopedJuceInitialiser_GUI gui;
    AudioTap tap;
    tap.allocate();
    SnapshotRing<PanelView> ring;
    ring.allocate(kFrameSlots);
    PanelView view;
    SimDevice dev;
    flashDevice(dev, tap, ring, view);
    dev.startThread();
    Recorder::Config c;
    c.spec.file = juce::File(path);
    c.spec.mov = mov;
    c.spec.width = 640;
    c.spec.height = 360;
    c.spec.inputRate = kRate;
    c.main = &tap;
    c.frames = &ring;
    c.source = std::make_unique<FlashSource>();
    Recorder rec;
    juce::String err;
    if (!rec.start(std::move(c), err))
        return 2;
    tap.arm();
    std::printf("recording\n");
    std::fflush(stdout);
    juce::Thread::sleep(300000);                 // (killed long before)
    return 0;
}

static void crashTest(bool mov)
{
    const juce::File f = scratchDir().getChildFile(mov ? "killed.mov" : "killed.mp4");
    f.deleteFile();
    juce::ChildProcess cp;
    const juce::String exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
    if (!cp.start(juce::StringArray{exe, "child", f.getFullPathName(), mov ? "mov" : "mp4"}, juce::ChildProcess::wantStdOut)) {
        check(false, "the recording child process starts");
        return;
    }
    // (killed once the file being written already reads as 2 s or more: its first movie fragment is out; a loaded
    // machine takes longer to get there)
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < 60000 && !(probe(f).duration >= 1.9))
        juce::Thread::sleep(250);
    const double after = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    cp.kill();                                   // (SIGKILL: no finishWriting, no destructor)
    cp.waitForProcessToFinish(5000);
    const Probe pr = probe(f);
    check(pr.ok && pr.videoTracks == 1 && pr.audioTracks == 1 && pr.duration >= 1.9,
          std::string(mov ? ".mov" : ".mp4") + " killed mid-take after " + f2(after, 1) + " s: readable, " + f2(pr.duration) + " s (" +
              std::to_string(pr.videoFrames) + " frames) kept " + pr.error.toStdString());
}

int main(int argc, char **argv)
{
    if (argc >= 4 && std::string(argv[1]) == "child")
        return childMain(argv[2], std::string(argv[3]) == "mov");
    if (argc >= 2 && std::string(argv[1]) == "sync") {
        juce::ScopedJuceInitialiser_GUI gui;
        scratchDir().createDirectory();
        syncTest("plain", 30, 30, 0);
        syncTest("slow", 12, 30, 50);
        return fails ? 1 : 0;
    }
    require_cores("video_rec_test", {"choralroot"});
    const juce::File scratch = scratchDir();
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getChildFile("home").getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("video_rec_test: files in %s\n", scratch.getFullPathName().toRawUTF8());

    // ---- (t) the tap
    {
        std::printf("\n(t) AudioTap, MicInput\n");
        AudioTap t;
        t.allocate(4096);
        float one[100], two[100];
        for (int i = 0; i < 100; i++)
            one[i] = 1.0f, two[i] = 2.0f;
        t.push(one, one, 100);
        check(t.produced() == -1, "nothing before arm");
        t.arm();
        check(t.produced() == -1, "armed, the take begins with the next block");
        t.push(one, nullptr, 100);
        t.pushAt(150, two, two, 100);            // (a gap of 50: silence)
        t.pushAt(200, one, one, 100);            // (an overlap of 50: dropped)
        std::vector<float> got(600);
        const int k = t.read(got.data(), 300);
        check(t.produced() == 300 && k == 300 && got[2 * 99] == 1.0f && got[2 * 99 + 1] == 1.0f && got[2 * 100] == 0.0f &&
                  got[2 * 149] == 0.0f && got[2 * 150] == 2.0f && got[2 * 249] == 2.0f && got[2 * 250] == 1.0f,
              "push, then pushAt: a gap filled with silence, an overlap dropped (300 frames)");
        t.freeze(AudioTap::kPowerOn);
        t.push(one, one, 100);
        check(t.frozen() && t.reason() == AudioTap::kPowerOn && t.produced() == 300, "freeze ends the take (no more frames)");
        t.arm();
        for (int i = 0; i < 50; i++)
            t.push(one, one, 100);
        check(t.overflowed() && t.frozen() && t.produced() <= 4096, "a full ring ends the take (overflowed, " +
                                                                        std::to_string(t.produced()) + " frames)");
        // the mic: channel 2 of 2 at -6 dB, then a stereo pair; the monitor
        AudioTap mainT, micT;
        mainT.allocate(4096), micT.allocate(4096);
        MicInput mic(mainT, micT);
        mic.configure(1, 1, -6.0f, false);
        mic.audioDeviceAboutToStart(nullptr);
        mainT.arm(), micT.arm();
        mainT.push(one, one, 100);
        const float *in[2] = {one, two};
        float o0[100], o1[100];
        float *out[2] = {o0, o1};
        juce::AudioIODeviceCallbackContext ctx;
        mic.audioDeviceIOCallbackWithContext(in, 2, out, 2, 100, ctx);
        micT.read(got.data(), 100);
        check(micT.produced() == 100 && std::abs(got[0] - 2.0f * 0.5012f) < 1e-3f && got[0] == got[1] && o0[0] == 0.0f,
              "the mic: input 2 at -6 dB on both sides, aligned with the main block; no monitor (" + f2(got[0], 4) + ")");
        mic.configure(0, 2, 0.0f, true);
        mic.audioDeviceAboutToStart(nullptr);
        mainT.push(one, one, 100);
        mic.audioDeviceIOCallbackWithContext(in, 2, out, 2, 100, ctx);
        micT.read(got.data(), 100);
        check(got[0] == 1.0f && got[1] == 2.0f && o0[5] == 1.0f && o1[5] == 2.0f, "a stereo pair (1+2); the monitor plays it");
        mic.audioDeviceIOCallbackWithContext(in, 2, out, 2, 100, ctx);   // (the processor skipped this cycle)
        check(micT.produced() == 200, "a cycle the processor skipped is dropped from the mic");
    }

    // ---- the processor: ChoralRoot booted, a key held
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    FM1Processor p;
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    p.setRateAndBufferSizeDetails(kRate, kBlock);
    p.prepareToPlay(kRate, kBlock);
    {
        juce::AudioBuffer<float> b(2, kBlock);
        for (int i = 0; i < (int)(1.5 * kRate / kBlock); i++) {
            juce::MidiBuffer m;
            b.clear();
            p.processBlock(b, m);
        }
    }
    {                                            // (a chord held: the note on, never off)
        juce::AudioBuffer<float> b(2, kBlock);
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 0);
        p.processBlock(b, m);
        for (int i = 0; i < 30; i++) {
            juce::MidiBuffer none;
            p.processBlock(b, none);
        }
        std::printf("(a note held: peak %.3f)\n", (double)b.getMagnitude(0, kBlock));
    }

    // ---- (d) the device view with the big LCD: the sizes
    {
        std::printf("\n(d) the big LCD's sizes\n");
        Settings st;
        st.view = kViewDevice;
        st.size = 720;
        const juce::Point<int> d720 = st.frameSize(true);
        st.size = 1080;
        const juce::Point<int> d1080 = st.frameSize(true), plain = st.frameSize(false);
        const float aspect = PanelComponent::frameAspect(true);
        check(d720 == juce::Point<int>(720, 1162) && d1080 == juce::Point<int>(1080, 1744) && plain == juce::Point<int>(1920, 1080) &&
                  std::abs((float)d1080.x / (float)d1080.y - aspect) < 0.002f && (d720.y & 1) == 0 && (d1080.y & 1) == 0,
              "Device with the big LCD: 720 x 1162 and 1080 x 1744 (the layout's aspect " + f2(aspect, 4) + ", even); without it 1920 x 1080");
        st.view = kViewScreen;
        const juce::Point<int> scr = st.frameSize(true);
        st.view = kViewLcd;
        const juce::Point<int> lcd = st.frameSize(true);
        check(scr == juce::Point<int>(1920, 1080) && lcd == juce::Point<int>(1080, 1080), "Screen and LCD: unchanged by the big LCD");
        Session s(p);
        st.view = kViewDevice;
        s.setSettings(st);
        p.setBigLcd(true);
        const bool on = s.bigLcd() && s.frameSize() == d1080;
        p.setBigLcd(false);
        check(on && !s.bigLcd() && s.frameSize() == plain, "the session's next take follows the processor's big LCD");
    }

    const std::string only = argc >= 3 && std::string(argv[1]) == "only" ? argv[2] : "";
    for (const TakeSpec &t : {TakeSpec{"lcd30", kViewLcd, 720, 720, 30, false, true, false, -6.0f, 5.0},
                              TakeSpec{"device60", kViewDevice, 1280, 720, 60, true, true, true, 0.0f, 5.0},
                              TakeSpec{"screen1080", kViewScreen, 1920, 1080, 30, false, false, false, 0.0f, 2.0},
                              TakeSpec{"device1080", kViewDevice, 1920, 1080, 60, false, false, false, 0.0f, 2.0},
                              TakeSpec{"devicebig", kViewDevice, 1080, 1744, 30, false, false, false, 0.0f, 2.0, true}})
        if (only.empty() || ("," + only + ",").find("," + std::string(t.name) + ",") != std::string::npos)
            processorTake(p, t);
    if (only.empty() || only.find("layout") != std::string::npos) {
        bigLcdLayoutTake(p, "biglayout", {1080, 1744});
        p.setKeyHints(false);                    // (the editor's setting: the recorder's view follows)
        bigLcdLayoutTake(p, "biglayout-nohints", {720, 1162});
        p.setKeyHints(true);
    }
    if (!only.empty())
        return fails ? 1 : 0;
    // (not on GitHub's runners, the owner's call: timing on shared runners; it runs on every Mac, loaded or not)
    if (getenv("GITHUB_ACTIONS") != nullptr) {
        std::printf("\n(y) A/V sync: skipped on CI: timing on shared runners (GITHUB_ACTIONS is set)\n");
    } else {
        syncTest("plain", 30, 30, 0);
        syncTest("slow", 12, 30, 50);            // (a renderer slower than the frame rate: frames dropped, none misplaced)
        // the device view with the big LCD: the flash is the big LCD's (the panel and the LCD from one snapshot)
        const juce::Point<int> big{720, 1162};
        PanelComponent::Layout lay;
        referenceDevice(p, big, PanelView{}, &lay);
        syncTest("biglcd", 12, 30, 0, [&p, big] { return makeViewSource(p, kViewDevice, big, true); }, big,
                 lay.lcdBig.reduced(lay.lcdBig.getWidth() / 10));
    }

    // ---- (e) errors
    {
        std::printf("\n(e) errors\n");
        const juce::File ro = scratch.getChildFile("read-only");
        ro.createDirectory();
        chmod(ro.getFullPathName().toRawUTF8(), 0555);
        Session s(p);
        s.setFolder(ro.getChildFile("FM1VST"));
        const bool started = s.start();
        check(!started && !s.recording() && s.lastError().startsWith("Can't record into"),
              "the session into an unwritable folder: \"" + s.lastError().toStdString() + "\"");
        AudioTap t;
        Recorder rec;
        Recorder::Config c;
        c.spec.file = ro.getChildFile("x.mp4");
        c.spec.inputRate = kRate;
        SnapshotRing<PanelView> ring;
        c.main = &t;
        c.frames = &ring;
        c.source = makeViewSource(p, kViewLcd, {720, 720});
        juce::String err;
        const bool ok = rec.start(std::move(c), err);
        check(!ok && err.isNotEmpty() && !ro.getChildFile("x.mp4").exists(), "the recorder into an unwritable file: \"" +
                                                                                  err.toStdString() + "\"");
        chmod(ro.getFullPathName().toRawUTF8(), 0755);
    }

    // ---- (w) the session: a take, then one a power cycle ends
    {
        std::printf("\n(w) the session\n");
        Session s(p);
        s.setFolder(scratch.getChildFile("Movies"));
        Settings st;
        st.view = kViewLcd;
        st.size = 720;
        s.setSettings(st);
        SimDevice dev;
        dev.main = &p.videoTap();
        dev.render = [&p](juce::AudioBuffer<float> &b) {
            juce::MidiBuffer m;
            if (!p.isSuspended())
                p.processBlock(b, m);
        };
        dev.startThread();
        check(s.start() && s.recording(), "a take starts: " + s.currentFile().getFileName().toStdString());
        const juce::String name = s.currentFile().getFileName();
        check(name.startsWith("choralroot-") && name.endsWith(".mp4") && s.currentFile().getParentDirectory() == s.folder(),
              "  ... named <id>-<version>-<date>.mp4 in its folder");
        waitTake(p.videoTap(), 1.2);             // (the take's own length, not the wall clock: a loaded machine is slower)
        s.stop();
        const Probe pr = probe(s.lastFile());
        check(!s.recording() && s.lastResult().ok && pr.ok && pr.duration > 1.0, "Stop: the file is finished, " + f2(pr.duration) + " s");
        check(s.start(), "another take");
        waitTake(p.videoTap(), 1.0);
        p.powerCycle();
        for (int i = 0; i < 3000 && !s.recorderForTest()->ended(); i++)
            juce::Thread::sleep(10);
        const bool ended = s.recorderForTest()->ended();
        s.stop();                                // (the session's timer would: no message loop here)
        const Probe p2 = probe(s.lastFile());
        check(ended && s.lastError() == "The take ended when the firmware restarted" && p2.ok && p2.duration > 0.8,
              "a power cycle ends the take by itself: \"" + s.lastError().toStdString() + "\", the file readable, " +
                  f2(p2.duration) + " s");
        dev.stopThread(-1);
    }

    // ---- (k) killed mid-take
    std::printf("\n(k) a recording process killed mid-take\n");
    crashTest(true);
    crashTest(false);

    std::printf("\nvideo_rec_test: %s (%d failed)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
