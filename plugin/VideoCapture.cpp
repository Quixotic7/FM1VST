// SPDX-License-Identifier: GPL-3.0-only
// Video recording (see VideoCapture.h).
#include "VideoCapture.h"

#include "Editor.h"
#include "PanelComponent.h"
#include "Processor.h"
#include "ScreenCapture.h"
#include "StandaloneHooks.h"

#include <algorithm>
#include <cmath>

namespace fm1video {

// ============================================================ settings ===
juce::Point<int> Settings::frameSize() const
{
    const bool big = size >= 1080;
    if (view == kViewLcd)
        return big ? juce::Point<int>{1080, 1080} : juce::Point<int>{720, 720};
    return big ? juce::Point<int>{1920, 1080} : juce::Point<int>{1280, 720};
}

Settings Settings::normalised() const
{
    Settings s = *this;
    s.view = juce::jlimit(0, 2, s.view);
    s.size = s.size >= 1080 ? 1080 : 720;
    s.fps = s.fps >= 60 ? 60 : 30;
    s.inChannel = juce::jlimit(0, 255, s.inChannel);
    s.inChannels = s.inChannels == 2 ? 2 : 1;
    s.micGainDb = juce::jlimit(-24, 24, s.micGainDb);
    return s;
}

juce::String Settings::toJson() const
{
    auto *o = new juce::DynamicObject();
    o->setProperty("view", view);
    o->setProperty("size", size);
    o->setProperty("fps", fps);
    o->setProperty("format", mov ? "mov" : "mp4");
    o->setProperty("mic", mic);
    o->setProperty("inputDevice", inputDevice);
    o->setProperty("inChannel", inChannel);
    o->setProperty("inChannels", inChannels);
    o->setProperty("micGainDb", micGainDb);
    o->setProperty("micMode", micMix ? "mix" : "separate");
    o->setProperty("monitor", monitor);
    return juce::JSON::toString(juce::var(o), true);
}

Settings Settings::fromJson(const juce::String &json)
{
    Settings s;
    const juce::var v = juce::JSON::parse(json);
    if (!v.isObject())
        return s;
    auto num = [&](const char *k, int d) { return v.hasProperty(k) ? (int)v[k] : d; };
    s.view = num("view", s.view);
    s.size = num("size", s.size);
    s.fps = num("fps", s.fps);
    s.mov = v["format"].toString() == "mov";
    s.mic = v.hasProperty("mic") && (bool)v["mic"];
    s.inputDevice = v["inputDevice"].toString();
    s.inChannel = num("inChannel", 0);
    s.inChannels = num("inChannels", 1);
    s.micGainDb = num("micGainDb", 0);
    s.micMix = v["micMode"].toString() == "mix";
    s.monitor = v.hasProperty("monitor") && (bool)v["monitor"];
    return s.normalised();
}

juce::String formatTime(double seconds)
{
    const int t = juce::jmax(0, (int)std::floor(seconds));
    const int h = t / 3600, m = (t / 60) % 60, s = t % 60;
    return h > 0 ? juce::String::formatted("%d:%02d:%02d", h, m, s) : juce::String::formatted("%d:%02d", m, s);
}

// ======================================================== view sources ===
void scaleLcd(const uint16_t *lcd, juce::Image &frame)
{
    const int w = frame.getWidth(), h = frame.getHeight();
    std::vector<int> xs((size_t)w);
    for (int x = 0; x < w; x++)
        xs[(size_t)x] = juce::jmin(fm1screen::kW - 1, x * fm1screen::kW / w);
    juce::Image::BitmapData d(frame, juce::Image::BitmapData::writeOnly);
    int lastSy = -1;
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(void *)d.getLinePointer(y);
        const int sy = juce::jmin(fm1screen::kH - 1, y * fm1screen::kH / h);
        if (sy == lastSy) {                      // (the row above, again)
            std::memcpy(row, d.getLinePointer(y - 1), (size_t)w * 4);
            continue;
        }
        lastSy = sy;
        const uint16_t *src = lcd + (size_t)sy * fm1screen::kW;
        for (int x = 0; x < w; x++)
            row[x] = fm1screen::argbOf(src[xs[(size_t)x]]);
    }
}

namespace {
class LcdSource : public FrameSource {
public:
    explicit LcdSource(FM1Processor &p) : p_(p) {}
    void render(juce::Image &frame, int64_t) override
    {
        p_.getPanelView(view_);
        scaleLcd(view_.lcd.data(), frame);
    }

private:
    FM1Processor &p_;
    PanelView view_;
};

// a PanelComponent of its own: never on the desktop, no timer, no input; refreshed and painted on the recorder's thread
class MirrorBase : public FrameSource {
public:
    MirrorBase(FM1Processor &p) : p_(p), panel_(std::make_unique<PanelComponent>(p, true))
    {
        panel_->setButtonNames(p.buttonNames());
        panel_->setTheme(p.getTheme());
        serial_ = p.themeSerial();
        panel_->setVisible(true);
    }
    void follow()
    {
        if (p_.themeSerial() != serial_) {       // (a theme picked while recording)
            serial_ = p_.themeSerial();
            panel_->setTheme(p_.getTheme());
        }
        panel_->refresh();
    }

protected:
    FM1Processor &p_;
    std::unique_ptr<PanelComponent> panel_;
    uint32_t serial_ = 0;
};

class DeviceSource : public MirrorBase {
public:
    DeviceSource(FM1Processor &p, juce::Point<int> size) : MirrorBase(p) { panel_->setBounds(0, 0, size.x, size.y); }
    void render(juce::Image &frame, int64_t) override
    {
        follow();
        juce::Graphics g(frame);
        panel_->paintEntireComponent(g, true);
    }
};

class ScreenSource : public MirrorBase {
public:
    ScreenSource(FM1Processor &p, juce::Point<int> size) : MirrorBase(p), view_(std::make_unique<ScreenView>(*panel_))
    {
        const int side = juce::jmin(size.x, size.y);
        view_->setBounds(0, 0, side, side);
        view_->setVisible(true);
        x_ = (size.x - side) / 2, y_ = (size.y - side) / 2;
    }
    void render(juce::Image &frame, int64_t) override
    {
        follow();
        if (!body_.isValid() || bodySerial_ != serial_) {   // (the body and the bezel, drawn once per theme: the
            bodySerial_ = serial_;                          // bezel's shadow is a blur)
            body_ = juce::Image(juce::Image::ARGB, view_->getWidth(), view_->getHeight(), true, juce::SoftwareImageType());
            juce::Graphics bg(body_);
            view_->paintBody(bg);
        }
        view_->lcdChanged();                     // (its scaled copy of the LCD made again)
        juce::Graphics g(frame);
        g.fillAll(panel_->palette().backdrop);
        g.drawImageAt(body_, x_, y_);
        g.setOrigin(x_, y_);
        view_->paintLcd(g);
    }

private:
    std::unique_ptr<ScreenView> view_;
    juce::Image body_;
    uint32_t bodySerial_ = 0;
    int x_ = 0, y_ = 0;
};
}   // namespace

std::unique_ptr<FrameSource> makeViewSource(FM1Processor &p, int view, juce::Point<int> size)
{
    switch (view) {
    case kViewLcd: return std::make_unique<LcdSource>(p);
    case kViewScreen: return std::make_unique<ScreenSource>(p, size);
    default: return std::make_unique<DeviceSource>(p, size);
    }
}

// ============================================================ the mic ===
void MicInput::configure(int firstChannel, int channels, float gainDb, bool monitor)
{
    first_.store(juce::jmax(0, firstChannel));
    count_.store(channels == 2 ? 2 : 1);
    setGainDb(gainDb);
    monitor_.store(monitor);
}

void MicInput::audioDeviceAboutToStart(juce::AudioIODevice *d)
{
    stopped_.store(false);
    if (!d) {                                    // (no device: the inputs as given, every channel active)
        idxA_.store(first_.load());
        idxB_.store(first_.load() + (count_.load() == 2 ? 1 : 0));
        return;
    }
    // JUCE passes the active inputs compacted, in channel order: the chosen channels' places among them
    const juce::BigInteger active = d->getActiveInputChannels();
    auto place = [&](int ch) {
        if (ch < 0 || !active[ch])
            return -1;
        int i = 0;
        for (int c = 0; c < ch; c++)
            i += active[c] ? 1 : 0;
        return i;
    };
    const int f = first_.load();
    idxA_.store(place(f));
    idxB_.store(count_.load() == 2 ? place(f + 1) : place(f));
}

void MicInput::audioDeviceIOCallbackWithContext(const float *const *in, int nIn, float *const *out, int nOut, int n,
                                                const juce::AudioIODeviceCallbackContext &)
{
    for (int c = 0; c < nOut; c++)
        if (out[c])
            juce::FloatVectorOperations::clear(out[c], n);
    if (n <= 0)
        return;
    const int ia = idxA_.load(std::memory_order_relaxed), ib = idxB_.load(std::memory_order_relaxed);
    const float *A = ia >= 0 && ia < nIn ? in[ia] : nullptr;
    const float *B = ib >= 0 && ib < nIn ? in[ib] : A;
    const float g = gain_.load(std::memory_order_relaxed);
    if (A) {
        const auto r = juce::FloatVectorOperations::findMinAndMax(A, n);
        const float pk = juce::jmax(std::abs(r.getStart()), std::abs(r.getEnd())) * g;
        if (pk > peak_.load(std::memory_order_relaxed))
            peak_.store(pk, std::memory_order_relaxed);
        if (monitor_.load(std::memory_order_relaxed)) {
            if (nOut > 0 && out[0])
                juce::FloatVectorOperations::addWithMultiply(out[0], A, g, n);
            if (nOut > 1 && out[1])
                juce::FloatVectorOperations::addWithMultiply(out[1], B, g, n);
        }
    }
    // the block is the main tap's last n frames (the processor ran just before, in this device cycle)
    const int64_t rel = main_.running() ? main_.produced() : -1;   // (not the last take's length)
    if (rel >= 0 && mic_.armed())
        mic_.pushAt(rel - n, A, B, n, g);
}

// ========================================================== the recorder ===
static constexpr int kChunk = 4096;

Recorder::Recorder() : juce::Thread("FM1VST video") {}

Recorder::~Recorder()
{
    if (started_ && !collected_)
        stop();
}

bool Recorder::start(Config c, juce::String &error)
{
    if (started_ || !c.main || !c.source) {
        error = "the recorder is busy";
        return false;
    }
    c_ = std::move(c);
    sr_ = juce::roundToInt(c_.spec.inputRate);
    c_.main->clear();                            // (the clock reads nothing of the last take)
    if (c_.mic)
        c_.mic->clear();
    c_.spec.audioTracks = c_.mic && !c_.mix ? 2 : 1;
    if (!enc_.open(c_.spec, error))
        return false;
    frame_ = juce::Image(juce::Image::ARGB, c_.spec.width, c_.spec.height, true, juce::SoftwareImageType());
    a_.assign((size_t)kChunk * 2, 0.0f);
    b_.assign((size_t)kChunk * 2, 0.0f);
    res_ = {};
    res_.file = c_.spec.file;
    started_ = true;
    startThread(juce::Thread::Priority::high);
    return true;
}

double Recorder::seconds() const
{
    if (collected_)
        return res_.seconds;
    return c_.main ? (double)juce::jmax<int64_t>(0, c_.main->produced()) / juce::jmax(1, sr_) : 0.0;
}

juce::Image Recorder::firstFrame() const
{
    const juce::SpinLock::ScopedLockType l(firstLock_);
    return first_.isValid() ? first_.createCopy() : juce::Image();
}

int64_t Recorder::framesBefore(int64_t t) const
{
    // frames k with k * sr / fps < t, i.e. ceil(t * fps / sr)
    if (t <= 0)
        return 0;
    const int64_t fps = c_.spec.fps;
    return (t * fps + sr_ - 1) / sr_;
}

bool Recorder::emitFrames(int64_t due, int64_t now)
{
    if (next_ >= due)
        return true;
    c_.source->render(frame_, next_ * sr_ / c_.spec.fps);
    res_.rendered++;
    if (!first_.isValid()) {
        const juce::SpinLock::ScopedLockType l(firstLock_);
        first_ = frame_.createCopy();
    }
    // (how late the picture is against the instant of the frame it is shown at, at worst)
    const int64_t lag = now - next_ * sr_ / c_.spec.fps;
    res_.maxLagMs = juce::jmax(res_.maxLagMs, (int)(lag * 1000 / juce::jmax(1, sr_)));
    const juce::Image::BitmapData d(frame_, juce::Image::BitmapData::readOnly);
    const int64_t from = next_;
    while (next_ < due) {
        if (!enc_.addVideo(next_, d.getLinePointer(0), d.lineStride))
            return !enc_.failed();               // (the input busy: the rest next time, as repeats)
        if (next_ > from)
            res_.duplicated++;
        next_++;
        res_.videoFrames++;
    }
    return true;
}

bool Recorder::drainAudio(int64_t upTo, bool final)
{
    AudioTap &m = *c_.main;
    AudioTap *mic = c_.mic;
    if (c_.mix && mic && !micAbsent_) {
        // (the mic a second behind, or never started: it is left out rather than hold the main track up)
        const int64_t mp = mic->produced();
        if (!final && upTo - juce::jmax<int64_t>(0, mp) > sr_)
            micAbsent_ = true;
    }
    // track 0: the main tap (+ the mic when mixed)
    while (true) {
        const int64_t pos = m.consumed();
        int64_t n = upTo - pos;
        if (c_.mix && mic && !micAbsent_ && !final)
            n = juce::jmin(n, juce::jmax<int64_t>(0, mic->produced()) - mic->consumed());
        if (n <= 0)
            break;
        if (!final && !enc_.audioReady(0))
            return true;
        const int k = m.read(a_.data(), (int)juce::jmin<int64_t>(n, kChunk));
        if (k <= 0)
            break;
        if (c_.mix && mic && !micAbsent_) {
            const int mk = mic->read(b_.data(), k);
            for (int i = 0; i < mk * 2; i++)
                a_[(size_t)i] += b_[(size_t)i];
        }
        if (!enc_.addAudio(0, pos, a_.data(), k, final ? 2000 : 250))
            return !enc_.failed() && !final;
        res_.endFrame = juce::jmax(res_.endFrame, pos + k);
    }
    // track 1: the mic, separate
    if (mic && !c_.mix) {
        // (micPos_: what track 1 holds; the tap's frames while it has them, then, at the end, silence)
        while (micPos_ < upTo) {
            const int64_t avail = juce::jmin(upTo, juce::jmax<int64_t>(0, mic->produced()));
            int k = 0;
            if (avail > micPos_ && mic->consumed() == micPos_) {
                if (!final && !enc_.audioReady(1))
                    return true;
                k = mic->read(b_.data(), (int)juce::jmin<int64_t>(avail - micPos_, kChunk));
            } else if (final) {                  // (the end: silence where the mic gave nothing)
                k = (int)juce::jmin<int64_t>(upTo - micPos_, kChunk);
                std::fill(b_.begin(), b_.begin() + k * 2, 0.0f);
            }
            if (k <= 0)
                break;
            if (!enc_.addAudio(1, micPos_, b_.data(), k, final ? 2000 : 250))
                return !enc_.failed() && !final;
            micPos_ += k;
        }
    }
    return true;
}

void Recorder::run()
{
    AudioTap &m = *c_.main;
    juce::String why;
    while (!threadShouldExit()) {
        const bool frozen = m.frozen();          // (read first: produced() after it is the take's end)
        const int64_t now = m.produced();
        if (now > 0) {
            if (!drainAudio(now, false) || !emitFrames(framesBefore(now), now)) {
                why = "Recording stopped: " + enc_.error() + ". What was written is kept.";
                break;
            }
        }
        if (frozen) {
            why = m.overflowed() ? "The take ended: the recorder fell behind"
                  : m.reason() == AudioTap::kPowerOn ? "The take ended when the firmware restarted"
                  : m.reason() == AudioTap::kDevice  ? "The take ended when the audio device changed"
                                                      : "The take ended";
            break;
        }
        // until the next frame is due (or 4 ms: the audio keeps moving)
        const int64_t dueAt = next_ * sr_ / c_.spec.fps + 1;
        const int ms = now < 0 ? 4 : (int)juce::jlimit<int64_t>(1, 4, (dueAt - now) * 1000 / juce::jmax(1, sr_));
        wait(ms);
    }
    // the end: everything up to the take's last frame
    int64_t end = end_.load();
    if (end < 0)
        end = juce::jmax<int64_t>(0, m.produced());
    if (c_.mic && !enc_.failed()) {              // (the mic's last block lands right after the processor's)
        for (int i = 0; i < 100 && c_.mic->produced() < end && c_.mic->armed(); i++)
            juce::Thread::sleep(2);
    }
    bool ok = !enc_.failed();
    if (ok)
        ok = drainAudio(end, true) && emitFrames(framesBefore(end), end);
    juce::String err;
    const bool fin = enc_.finish(end, err);
    res_.ok = fin && ok && why.isEmpty();
    if (!fin || !ok)
        res_.error = why.isNotEmpty() ? why : "Recording stopped: " + err + ". What was written is kept.";
    else
        res_.error = why;
    res_.endFrame = end;
    res_.seconds = (double)end / juce::jmax(1, sr_);
}

Recorder::Result Recorder::stop()
{
    if (!started_ || collected_)
        return res_;
    if (isThreadRunning()) {
        c_.main->disarm();
        end_.store(juce::jmax<int64_t>(0, c_.main->produced()));
        signalThreadShouldExit();
        notify();
        waitForThreadToExit(-1);
    }
    c_.main->disarm();
    if (c_.mic)
        c_.mic->disarm();
    c_.source.reset();                           // (the mirrors, on the message thread)
    collected_ = true;
    return res_;
}

// =========================================================== the session ===
static bool micPermissionOk()
{
    const MicAccess a = micAccess();
    return a == MicAccess::granted || a == MicAccess::unsupported;
}

static Settings memorySettings;                  // (no app settings: tests)

bool Session::available(const FM1Processor &p) { return p.wrapperType == juce::AudioProcessor::wrapperType_Standalone; }

Session::Session(FM1Processor &p) : p_(p)
{
    if (juce::PropertySet *ps = fm1standalone::appSettings())
        s_ = Settings::fromJson(ps->getValue("videoRecorder"));
    else
        s_ = memorySettings;
    s_.monitor = false;                          // (never on by itself at launch: feedback)
    mic_ = std::make_unique<MicInput>(p_.videoTap(), micTap_);
}

Session::~Session()
{
    stopTimer();
    if (rec_)
        endTake();
    if (micAttached_)
        if (auto *dm = fm1standalone::deviceManager())
            dm->removeAudioCallback(mic_.get());
    micAttached_ = false;
    if (inputOpen_)
        openInput(false);
}

void Session::save() const
{
    if (juce::PropertySet *ps = fm1standalone::appSettings())
        ps->setValue("videoRecorder", s_.toJson());
    else
        memorySettings = s_;
}

void Session::setSettings(const Settings &s)
{
    const Settings was = s_;
    s_ = s.normalised();
    save();
    mic_->setGainDb((float)s_.micGainDb);
    mic_->setMonitor(s_.monitor);
    if (!rec_ && (was.monitor != s_.monitor || was.mic != s_.mic || was.inputDevice != s_.inputDevice ||
                  was.inChannel != s_.inChannel || was.inChannels != s_.inChannels)) {
        if (inputOpen_ && s_.monitor && s_.mic) {   // (another input: open it again)
            if (micAttached_)
                if (auto *dm = fm1standalone::deviceManager())
                    dm->removeAudioCallback(mic_.get());
            micAttached_ = false;
            openInput(false);
        }
        applyMonitor();
    }
}

juce::File Session::folder() const
{
    if (folder_ != juce::File())
        return folder_;
    return juce::File::getSpecialLocation(juce::File::userMoviesDirectory).getChildFile("FM1VST");
}

juce::File Session::nextFile() const
{
    const juce::String ext = s_.mov ? ".mov" : ".mp4";
    juce::File f = folder().getChildFile(FM1Editor::fileNameFor(p_, ext));
    for (int i = 2; f.exists() && i < 1000; i++)
        f = folder().getChildFile(FM1Editor::fileNameFor(p_, "-" + juce::String(i) + ext));
    return f;
}

bool Session::hasAudioDevice() const { return fm1standalone::deviceManager() != nullptr; }

juce::StringArray Session::inputDevices() const
{
    if (auto *dm = fm1standalone::deviceManager())
        if (auto *t = dm->getCurrentDeviceTypeObject())
            return t->getDeviceNames(true);
    return {};
}

juce::String Session::inputDevice() const
{
    const juce::StringArray all = inputDevices();
    if (s_.inputDevice.isNotEmpty() && all.contains(s_.inputDevice))
        return s_.inputDevice;
    if (auto *dm = fm1standalone::deviceManager()) {
        const juce::String out = dm->getAudioDeviceSetup().outputDeviceName;   // (an interface with both: its inputs)
        if (all.contains(out) && inputChannelNames(out).size() > 0)
            return out;
        if (auto *t = dm->getCurrentDeviceTypeObject()) {
            const int i = t->getDefaultDeviceIndex(true);
            if (i >= 0 && i < all.size())
                return all[i];
        }
    }
    return all.isEmpty() ? juce::String() : all[0];
}

juce::StringArray Session::inputChannelNames(const juce::String &device) const
{
    if (device.isEmpty())
        return {};
    auto it = channelNames_.find(device);
    if (it != channelNames_.end())
        return it->second;
    juce::StringArray names;
    if (auto *dm = fm1standalone::deviceManager())
        if (auto *t = dm->getCurrentDeviceTypeObject())
            if (std::unique_ptr<juce::AudioIODevice> d{t->createDevice({}, device)})   // (made, never opened)
                names = d->getInputChannelNames();
    channelNames_[device] = names;
    return names;
}

void Session::setMic(bool on, std::function<void(bool, juce::String)> done)
{
    auto apply = [this, done](bool ok, const juce::String &why) {
        Settings s = s_;
        s.mic = ok;
        setSettings(s);
        if (done)
            done(ok, why);
    };
    if (!on) {
        apply(false, {});
        return;
    }
    const MicAccess a = micAccess();
    if (a == MicAccess::granted || a == MicAccess::unsupported) {
        apply(true, {});
    } else if (a == MicAccess::undetermined) {
        requestMicAccess([apply](bool granted) {
            apply(granted, granted ? juce::String() : juce::String("Microphone access was not allowed: System Settings > "
                                                                    "Privacy & Security > Microphone > FM1VST"));
        });
    } else {
        apply(false, "Microphone access is off for FM1VST: System Settings > Privacy & Security > Microphone");
    }
}

juce::String Session::openInput(bool on)
{
    auto *dm = fm1standalone::deviceManager();
    if (!dm || on == inputOpen_)
        return {};
    if (on) {
        saved_ = dm->getAudioDeviceSetup();
        juce::AudioDeviceManager::AudioDeviceSetup st = saved_;
        st.inputDeviceName = inputDevice();
        st.useDefaultInputChannels = false;
        st.inputChannels.clear();
        st.inputChannels.setRange(s_.inChannel, s_.inChannels, true);
        // (not the chosen setup: the app's saved audio state keeps no input, so a launch never opens the mic)
        const juce::String err = dm->setAudioDeviceSetup(st, false);
        if (err.isNotEmpty())
            return err;
        inputOpen_ = true;
        mic_->configure(s_.inChannel, s_.inChannels, (float)s_.micGainDb, s_.monitor);
        return {};
    }
    juce::AudioDeviceManager::AudioDeviceSetup st = dm->getAudioDeviceSetup();
    st.inputDeviceName = saved_.inputDeviceName;
    st.inputChannels = saved_.inputChannels;
    st.useDefaultInputChannels = saved_.useDefaultInputChannels;
    inputOpen_ = false;
    return dm->setAudioDeviceSetup(st, false);
}

void Session::applyMonitor()
{
    auto *dm = fm1standalone::deviceManager();
    if (!dm || rec_)
        return;
    const bool want = s_.mic && s_.monitor && micPermissionOk();
    if (want) {
        if (openInput(true).isEmpty() && !micAttached_) {
            dm->addAudioCallback(mic_.get());
            micAttached_ = true;
        }
    } else {
        if (micAttached_)
            dm->removeAudioCallback(mic_.get());
        micAttached_ = false;
        openInput(false);
    }
}

bool Session::start()
{
    if (rec_)
        return true;
    error_.clear();
    // the folder
    const juce::File dir = folder();
    const juce::Result made = dir.createDirectory();
    if (made.failed() || !dir.isDirectory() || !dir.hasWriteAccess()) {
        error_ = "Can't record into " + dir.getFullPathName() + ": " +
                 (made.failed() ? made.getErrorMessage() : juce::String("the folder is not writable"));
        return false;
    }
    // the audio
    auto *dm = fm1standalone::deviceManager();
    double sr = p_.getSampleRate();
    if (dm && dm->getCurrentAudioDevice())
        sr = dm->getCurrentAudioDevice()->getCurrentSampleRate();
    if (!(sr > 0)) {
        error_ = "Can't record: the audio isn't running";
        return false;
    }
    const bool mic = s_.mic && dm != nullptr;
    if (mic) {
        if (!micPermissionOk()) {
            error_ = "Microphone access is off for FM1VST: System Settings > Privacy & Security > Microphone";
            return false;
        }
        const juce::String err = openInput(true);
        if (err.isNotEmpty()) {
            error_ = "Can't open the input: " + err;
            return false;
        }
        if (dm->getCurrentAudioDevice())
            sr = dm->getCurrentAudioDevice()->getCurrentSampleRate();
    }
    // the file, the view, the writer
    file_ = nextFile();
    Recorder::Config c;
    c.spec.file = file_;
    c.spec.mov = s_.mov;
    const juce::Point<int> size = s_.frameSize();
    c.spec.width = size.x;
    c.spec.height = size.y;
    c.spec.fps = s_.fps;
    c.spec.inputRate = sr;
    c.main = &p_.videoTap();
    c.mic = mic ? &micTap_ : nullptr;
    c.mix = s_.micMix;
    c.source = makeViewSource(p_, s_.view, size);
    p_.videoTap().allocate();
    micTap_.allocate();
    rec_ = std::make_unique<Recorder>();
    juce::String err;
    if (!rec_->start(std::move(c), err)) {
        rec_.reset();
        error_ = "Can't record to " + file_.getFileName() + ": " + err;
        file_.deleteFile();
        file_ = juce::File();
        if (mic && !s_.monitor)
            openInput(false);
        return false;
    }
    if (mic) {
        micTap_.arm();
        mic_->configure(s_.inChannel, s_.inChannels, (float)s_.micGainDb, s_.monitor);
        mic_->clearStopped();
        if (!micAttached_) {
            dm->addAudioCallback(mic_.get());
            micAttached_ = true;
        }
    }
    p_.videoTap().arm();
    serial_++;
    startTimerHz(10);
    if (onChange)
        onChange();
    return true;
}

void Session::stop()
{
    if (rec_)
        endTake();
}

void Session::endTake()
{
    stopTimer();
    last_ = rec_->stop();
    rec_.reset();
    error_ = last_.error;
    file_ = juce::File();
    if (micAttached_ && !(s_.mic && s_.monitor)) {
        if (auto *dm = fm1standalone::deviceManager())
            dm->removeAudioCallback(mic_.get());
        micAttached_ = false;
    }
    if (!(s_.mic && s_.monitor))
        openInput(false);
    serial_++;
    if (onChange)
        onChange();
}

void Session::timerCallback()
{
    if (!rec_)
        return stopTimer();
    if (mic_->deviceStopped() && micAttached_)  // (the device went away: the tap is frozen by prepareToPlay too)
        p_.videoTap().freeze(AudioTap::kDevice);
    if (rec_->ended())
        endTake();
}

}   // namespace fm1video
