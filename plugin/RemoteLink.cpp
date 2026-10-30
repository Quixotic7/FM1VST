// SPDX-License-Identifier: GPL-3.0-only
// Remote FM-1: the session thread, the unit lock, the back ends (see RemoteLink.h).
#include "RemoteLink.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "rfm/fake.h"
#include "rfm/macos.h"
#include "rfm/screen.h"
#include "rfm/session.h"

RemoteLink::Backend RemoteLink::defaultBackend()
{
    const char *e = std::getenv("FM1EMU_REMOTE_FAKE");
    return e && *e && std::strcmp(e, "0") != 0 ? Backend::Fake : Backend::Device;
}

bool RemoteLink::hasBulk()
{
#ifdef FM1_REMOTE_NO_BULK
    return false;
#else
    return true;
#endif
}

const char *RemoteLink::stateName(State s)
{
    switch (s) {
    case kOff: return "off";
    case kInUse: return "in use";
    case kSearching: return "searching";
    case kConnected: return "connected";
    case kLost: return "unit lost";
    case kError: return "error";
    case kStopping: return "stopping";
    }
    return "?";
}

RemoteLink::RemoteLink(const Options &o) : opt_(o), t0_(std::chrono::steady_clock::now())
{
    if (defaultBackend() == Backend::Fake)
        opt_.backend = Backend::Fake;            // (the environment wins: a test never reaches a real unit)
    if (opt_.fps < 1 || opt_.fps > 60)
        opt_.fps = 20;
    if (opt_.backend == Backend::Fake) {
        rfm_fake_cfg_t fc;
        rfm_fake_cfg_default(&fc);
        fc.sx_rate_bps = 300000;                 // the M0 numbers (RemoteFM1 PLAN.md section 10), as fm1remote's
        fc.bk_rate_bps = 830000;
        fake_ = rfm_fake_new(&fc);
        loopBuf_.resize(1u << 18);
    }
    st_.text = "off";
}

RemoteLink::~RemoteLink()
{
    {
        std::lock_guard<std::mutex> g(mu_);
        quit_ = true;
        want_ = false;
    }
    cv_.notify_all();
    if (th_.joinable())
        th_.join();
    rfm_fake_free(fake_);
}

void RemoteLink::start()
{
    {
        std::lock_guard<std::mutex> g(mu_);
        if (quit_)
            return;
        want_ = true;
        if (!th_.joinable())
            th_ = std::thread([this] { threadMain(); });
    }
    cv_.notify_all();
}

void RemoteLink::stop()
{
    {
        std::lock_guard<std::mutex> g(mu_);
        want_ = false;
    }
    cv_.notify_all();
}

void RemoteLink::invalidateScreen() { invalidate_.store(true, std::memory_order_release); }

RemoteLink::Status RemoteLink::status() const
{
    Status s;
    {
        std::lock_guard<std::mutex> g(stMu_);
        s = st_;
    }
    s.eventsIn = eventsIn_.load();
    s.eventsDropped = eventsDropped_.load();
    s.midiIn = midiIn_.load();
    s.midiDropped = midiDropped_.load();
    s.snapshotsTaken = snapshotsTaken_.load();
    s.fullResends = fullResends_.load();
    s.runs = runs_.load();
    return s;
}

uint32_t RemoteLink::now() const
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_)
        .count();
}

void RemoteLink::setStatus(State s, const std::string &text)
{
    std::lock_guard<std::mutex> g(stMu_);
    st_.state = s;
    st_.text = text;
    if (s != kConnected) {
        st_.bulk = false;
        st_.fps = 0.0f;
    }
}

// ------------------------------------------------------------------ the thread ---
void RemoteLink::threadMain()
{
    for (;;) {
        {
            std::unique_lock<std::mutex> l(mu_);
            cv_.wait(l, [this] { return quit_.load() || want_.load(); });
            if (quit_)
                break;
        }
        runOnce();
    }
    setStatus(kOff, "off");
}

int RemoteLink::takeLock()
{
    lockError_.clear();
    std::error_code ec;
    const std::filesystem::path p(opt_.lockPath);
    if (opt_.lockPath.empty()) {
        lockError_ = "no lock file (HOME unknown)";
        return -1;
    }
    std::filesystem::create_directories(p.parent_path(), ec);
    const int fd = ::open(opt_.lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        lockError_ = "cannot open " + opt_.lockPath + ": " + std::strerror(errno);
        return -1;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int e = errno;
        if (e != EWOULDBLOCK)
            lockError_ = "cannot lock " + opt_.lockPath + ": " + std::strerror(e);
        ::close(fd);
        return -1;
    }
    // who holds it (read by a link that finds it taken)
    char who[160];
    const int n = std::snprintf(who, sizeof who, "%s (pid %d)\n", getprogname(), (int)getpid());
    if (::ftruncate(fd, 0) == 0 && n > 0)
        (void)!::pwrite(fd, who, (size_t)n, 0);
    return fd;
}

static std::string lockHolder(const std::string &path)
{
    char b[160] = {};
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return "";
    const ssize_t n = ::pread(fd, b, sizeof b - 1, 0);
    ::close(fd);
    std::string s(b, n > 0 ? (size_t)n : 0u);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

void RemoteLink::runOnce()
{
    runs_.fetch_add(1);
    // ---- 1. the unit lock (one owner on this Mac)
    int fd = -1;
    while (want_ && !quit_) {
        fd = takeLock();
        if (fd >= 0)
            break;
        if (!lockError_.empty()) {
            setStatus(kError, lockError_);
        } else {
            const std::string who = lockHolder(opt_.lockPath);
            setStatus(kInUse, "in use by another instance" + (who.empty() ? std::string() : ": " + who));
        }
        std::unique_lock<std::mutex> l(mu_);
        cv_.wait_for(l, std::chrono::milliseconds(kLockRetryMs), [this] { return !want_.load() || quit_.load(); });
    }
    if (fd < 0) {
        setStatus(kOff, "off");
        return;
    }

    // ---- 2. the session and its links
    rfm_session_cfg_t sc;
    rfm_session_cfg_default(&sc);
    sc.fps = opt_.fps;
    sc.host_name = opt_.hostName.c_str();
    sc.transport = RFM_T_AUTO;
    const rfm_session_cb_t cb{this, cbEvent, cbState, cbRescan, cbLog, cbMidiIn};
    lastState_ = -1;
    wasConnected_ = false;
    statFrames_ = 0;
    statT_ = now();
    {
        std::lock_guard<std::mutex> g(fakeMu_);
        s_ = rfm_session_new(&sc, &cb);
        if (s_ && fake_) {
            loops_[0] = {this, RFM_LINK_SX, rfm_session_add_link(s_, RFM_LINK_SX, loopWrite, &loops_[0], "fake midi")};
            loops_[1] = {this, RFM_LINK_BK, rfm_session_add_link(s_, RFM_LINK_BK, loopWrite, &loops_[1], "fake bulk")};
        }
    }
    bool ok = s_ != nullptr;
    if (!ok)
        setStatus(kError, "out of memory");
    if (ok && !fake_) {
        rfm_mac_cfg_t mc{};
        char err[200] = "";
        mc.allow_devices = 1;                    // (the real unit: CoreMIDI + the vendor bulk interface)
        mc.use_midi = 1;
        mc.use_bulk = hasBulk() ? 1 : 0;
        mc.midi_restart = 0;                     // (never MIDIRestart() inside a host: the DAW's ports would drop)
        mc.log = [](void *ctx, const char *m) { cbLog(ctx, m); };
        mc.log_ctx = this;
        mac_ = rfm_mac_new(&mc, err, sizeof err);
        if (!mac_) {
            setStatus(kError, err[0] ? err : "cannot open CoreMIDI");
            ok = false;
        } else {
            rfm_mac_attach(mac_, s_);
        }
    }
    if (!ok) {                                   // (nothing to run: wait for the toggle, keep the lock meanwhile)
        std::unique_lock<std::mutex> l(mu_);
        cv_.wait(l, [this] { return !want_.load() || quit_.load(); });
    } else {
        fullReq_.store(true, std::memory_order_release);
        engaged_.store(true, std::memory_order_release);
        setStatus(kSearching, "searching for the FM-1 (RemoteFM1 firmware)");
    }

    // ---- 3. the loop (fm1remote's session_thread)
    bool stopping = false;
    uint32_t tStop = 0;
    while (ok) {
        const uint32_t t = now();
        std::unique_lock<std::mutex> fg(fakeMu_, std::defer_lock);
        if (fake_)
            fg.lock();
        if (!want_ && !stopping) {
            rfm_session_stop(s_, t);             // SESSION start=0: the unit goes back to its waiting screen
            stopping = true;
            tStop = t;
            setStatus(kStopping, "stopping");
        }
        if (stopping && (rfm_session_state(s_) == RFM_S_STOPPED || t - tStop > kStopMs))
            break;
        if (fake_)
            pumpFake(t);
        else
            rfm_mac_pump(mac_, t);
        if (const RemoteSnapshot *sn = tb_.take()) {
            rfm_session_set_frame(s_, sn->lcd);
            rfm_session_set_leds(s_, &sn->leds);
            snapshotsTaken_.fetch_add(1);
            // a power cycle / core switch: the unit's screen is resent whole (after the new core's first frame)
            if (invalidate_.exchange(false, std::memory_order_acq_rel)) {
                rfm_screen_invalidate(const_cast<rfm_screen_t *>(rfm_session_screen(s_)));
                fullResends_.fetch_add(1);
            }
        }
        rfm_session_poll(s_, t);
        if (fake_)
            pumpFake(t);
        if (!stopping)
            updateStatus(t);
        if (fake_) {
            fg.unlock();
            std::this_thread::sleep_for(std::chrono::microseconds(700));
        } else {
            rfm_mac_wait(mac_, 2);
        }
    }

    // ---- 4. the end: everything the unit held let go, the back ends closed, the lock released
    engaged_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> g(fakeMu_);
        if (s_)
            releasePanel();
        rfm_mac_free(mac_);                      // (after the session stopped: closes the ports and the pipe)
        mac_ = nullptr;
        rfm_session_free(s_);
        s_ = nullptr;
        loops_[0].id = loops_[1].id = -1;
    }
    ::close(fd);
    setStatus(kOff, "off");
}

void RemoteLink::updateStatus(uint32_t t)
{
    rfm_session_stats_t ss;
    rfm_session_stats(s_, &ss);
    const int state = rfm_session_state(s_);
    std::lock_guard<std::mutex> g(stMu_);
    if (t - statT_ >= 1000u) {
        st_.fps = (float)((double)(ss.frames - statFrames_) * 1000.0 / (double)(t - statT_));
        statFrames_ = ss.frames;
        statT_ = t;
    }
    st_.sessions = ss.sessions;
    st_.frames = ss.frames;
    st_.acks = ss.acks;
    st_.pending = ss.pending;
    st_.inFlight = ss.in_flight;
    const char *noBulk = hasBulk() ? "" : " (SysEx only: built without libusb)";
    char b[160];
    switch (state) {
    case RFM_S_ACTIVE: {
        const int fl = rfm_session_frame_link(s_);
        st_.state = kConnected;
        st_.bulk = rfm_session_link_kind(s_, fl) == RFM_LINK_BK;
        std::snprintf(b, sizeof b, "connected (%s, %.0f fps)%s", st_.bulk ? "bulk" : "MIDI", (double)st_.fps, noBulk);
        break;
    }
    case RFM_S_STARTING:
        st_.state = wasConnected_ ? kLost : kSearching;
        std::snprintf(b, sizeof b, "found the FM-1: starting the session%s", noBulk);
        break;
    case RFM_S_DISCOVER:
        st_.state = wasConnected_ ? kLost : kSearching;
        std::snprintf(b, sizeof b, "%s%s",
                      wasConnected_ ? "unit lost: searching" : "searching for the FM-1 (RemoteFM1 firmware)", noBulk);
        break;
    default:
        st_.state = kStopping;
        std::snprintf(b, sizeof b, "stopping");
        break;
    }
    if (st_.state != kConnected) {
        st_.bulk = false;
        st_.fps = 0.0f;
    }
    st_.text = b;
}

void RemoteLink::pumpFake(uint32_t t)
{
    rfm_fake_poll(fake_, t);
    for (const Loop &l : loops_) {
        if (l.id < 0)
            continue;
        const uint32_t n = l.kind == RFM_LINK_SX ? rfm_fake_take_sx(fake_, loopBuf_.data(), (uint32_t)loopBuf_.size())
                                                 : rfm_fake_take_bk(fake_, loopBuf_.data(), (uint32_t)loopBuf_.size());
        if (n)
            rfm_session_rx(s_, l.id, loopBuf_.data(), n, t);
    }
}

int RemoteLink::loopWrite(void *ctx, const uint8_t *p, uint32_t n)
{
    const Loop *l = static_cast<const Loop *>(ctx);
    if (l->kind == RFM_LINK_SX)
        rfm_fake_rx_sx(l->self->fake_, p, n, l->self->now());
    else
        rfm_fake_rx_bk(l->self->fake_, p, n, l->self->now());
    return 0;
}

// the unit's held keys and buttons are up for the core; the panel decoder forgets them too, so the next session's
// PANEL_R reports whatever is still held as a fresh press
void RemoteLink::releasePanel()
{
    rfm_panel_t *p = const_cast<rfm_panel_t *>(rfm_session_panel(s_));
    p->keys = 0;
    p->buttons = 0;
    rfm_event_t e{};
    e.type = kEvRelease;
    if (!q_.push(e))
        eventsDropped_.fetch_add(1);
    if (!mq_.push(MidiMsg{}))                    // (n == 0: the TRS jack's held notes are up too)
        midiDropped_.fetch_add(1);
}

// ------------------------------------------------------------------ callbacks (the session thread) ---
void RemoteLink::cbEvent(void *ctx, const rfm_event_t *e)
{
    RemoteLink *self = static_cast<RemoteLink *>(ctx);
    self->eventsIn_.fetch_add(1);
    if (!self->q_.push(*e))
        self->eventsDropped_.fetch_add(1);
}

// the unit's TRS MIDI in (MIDI_IN): rfm_session checked it (rfm_parse_midi_in: whole messages, 1..64 bytes); split
// here, one MidiMsg each, for the audio thread's next block
void RemoteLink::cbMidiIn(void *ctx, const uint8_t *msgs, uint32_t n)
{
    RemoteLink *self = static_cast<RemoteLink *>(ctx);
    for (uint32_t k = 0; k < n;) {
        const uint32_t len = rfm_midi_msg_len(msgs[k]);
        if (!len || k + len > n)
            break;
        MidiMsg m{};
        for (uint32_t i = 0; i < len; i++)
            m.b[i] = msgs[k + i];
        m.n = (uint8_t)len;
        self->midiIn_.fetch_add(1);
        if (!self->mq_.push(m))
            self->midiDropped_.fetch_add(1);
        k += len;
    }
}

void RemoteLink::cbState(void *ctx, int st, const char *why)
{
    RemoteLink *self = static_cast<RemoteLink *>(ctx);
    if (self->lastState_ == RFM_S_ACTIVE && st != RFM_S_ACTIVE)
        self->releasePanel();                    // (the session ended or the unit went away)
    if (st == RFM_S_ACTIVE)
        self->wasConnected_ = true;
    self->lastState_ = st;
    char b[200];
    std::snprintf(b, sizeof b, "session: %s (%s)", rfm_session_state_name(st), why ? why : "");
    cbLog(ctx, b);
}

void RemoteLink::cbRescan(void *ctx)
{
    RemoteLink *self = static_cast<RemoteLink *>(ctx);
    if (self->mac_)
        rfm_mac_rescan(self->mac_);
}

void RemoteLink::cbLog(void *ctx, const char *m)
{
    RemoteLink *self = static_cast<RemoteLink *>(ctx);
    std::lock_guard<std::mutex> g(self->stMu_);
    self->st_.lastLog = m ? m : "";
}
