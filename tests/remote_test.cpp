// SPDX-License-Identifier: GPL-3.0-only
// Remote FM-1 (plugin/RemoteLink), headless, against libremotefm1's in-process fake unit (host/src/fake.c behind a
// loopback at the M0 rates): FM1EMU_REMOTE_FAKE=1 is set here before anything else, so nothing on USB or CoreMIDI is
// ever opened (every link is checked to be the fake before it is started).
// (m) the host MIDI filter: the unit's own panel events and RemoteFM1 SysEx, nothing else;
// (c) discovery's candidates (plugin/RemoteMidi.c): only what can be an FM-1 ever gets HELLO (its USB IDs; a name only
//     without the USB property; never an updater port);
// (l) the unit lock: two links on one lock file, the second "in use" (touching nothing) until the first lets go, then
//     it takes over;
// (p) the processor with Remote on, ChoralRoot booted, paced in real time (the session runs on the wall clock): the
//     session starts, the unit's shadow equals panel->lcd once settled, the LEDs equal panel->led / led_dim; a key, a
//     button, an encoder (a device turn: Knob Presets reported inside a gesture, no host detents) and MASTER (the
//     parameter follows) injected on the fake reach the core; the host MIDI filter on and off; a core switch resends
//     the screen whole (Felucca's screen on the unit); a second processor restored from the state is "in use"; the
//     toggle off ends the session (SESSION 0: the fake back on its waiting screen).
// The message loop is not running: the test calls drainTier2 itself. FM1EMU_HOME points under the build.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>

#include "Processor.h"
#include "RemoteCandidates.h"
#include "rfm/fake.h"

static int fails = 0;
static void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    std::fflush(stdout);
    fails += !c;
}

using Clock = std::chrono::steady_clock;
static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// waits (wall clock) until cond, calling step between tries; true if it came within timeoutMs
static bool waitFor(const std::function<bool()> &cond, int timeoutMs, const std::function<void()> &step = nullptr)
{
    const auto end = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < end) {
        if (cond())
            return true;
        if (step)
            step();
        else
            sleepMs(5);
    }
    return cond();
}

// one processor driven in real time: 10 ms blocks at 48 kHz, one per ~10 ms of wall clock
struct Host {
    FM1Processor &p;
    juce::AudioBuffer<float> buf;
    static constexpr int kBs = 480;
    Host(FM1Processor &proc) : p(proc), buf(2, kBs)
    {
        p.setRateAndBufferSizeDetails(48000.0, kBs);
        p.prepareToPlay(48000.0, kBs);
    }
    void block(juce::MidiBuffer &m)
    {
        buf.clear();
        p.processBlock(buf, m);
    }
    void block()
    {
        juce::MidiBuffer m;
        block(m);
    }
    void tick()                                  // a block, then the rest of its 10 ms
    {
        const auto t = Clock::now();
        block();
        std::this_thread::sleep_until(t + std::chrono::milliseconds(10));
    }
    void runMs(int ms)
    {
        const auto end = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < end)
            tick();
    }
    bool until(const std::function<bool()> &cond, int timeoutMs) { return waitFor(cond, timeoutMs, [this] { tick(); }); }
};

static const fm1panel_t *hal(FM1Processor &p) { return p.deviceForTest()->panel(); }

// the device's screen as of one more block goes to the unit (a full publish); then no block runs (panel->lcd stays) and
// the unit must catch up exactly
static bool shadowSettles(FM1Processor &p, Host &h, int timeoutMs, size_t *diffPx = nullptr)
{
    p.remoteForTest()->requestFull();
    h.block();
    const uint8_t *lcd = reinterpret_cast<const uint8_t *>(hal(p)->lcd);
    bool same = false;
    waitFor(
        [&] {
            p.remoteForTest()->withFake([&](rfm_fake_t *f) { same = std::memcmp(rfm_fake_shadow(f), lcd, RFM_W * RFM_H * 2) == 0; });
            return same;
        },
        timeoutMs, [] { sleepMs(20); });
    if (diffPx) {
        size_t n = 0;
        p.remoteForTest()->withFake([&](rfm_fake_t *f) {
            const uint8_t *s = rfm_fake_shadow(f);
            for (size_t i = 0; i < RFM_W * RFM_H; i++)
                n += s[2 * i] != lcd[2 * i] || s[2 * i + 1] != lcd[2 * i + 1];
        });
        *diffPx = n;
    }
    return same;
}

static size_t litPixels(const fm1panel_t *h)
{
    size_t n = 0;
    for (uint16_t px : h->lcd)
        n += px != 0;
    return n;
}

struct Watch : juce::AudioProcessorParameter::Listener {
    std::map<int, int> changes, begins;
    void parameterValueChanged(int i, float) override { changes[i]++; }
    void parameterGestureChanged(int i, bool starting) override
    {
        if (starting)
            begins[i]++;
    }
};

static juce::MidiBuffer one(const juce::MidiMessage &m)
{
    juce::MidiBuffer b;
    b.addEvent(m, 0);
    return b;
}

int main()
{
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    setenv("FM1EMU_REMOTE_FAKE", "1", 1);        // (every RemoteLink: the in-process fake unit, nothing opened)
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("remote_test: FM1EMU_HOME=%s\n", scratch.getFullPathName().toRawUTF8());
    if (RemoteLink::defaultBackend() != RemoteLink::Backend::Fake) {
        std::printf("FAIL: the fake backend is not selected: not starting any link\n");
        return 1;
    }

    // ---- (m) the host MIDI filter
    {
        std::printf("(m) the host MIDI filter\n");
        struct Case { juce::MidiMessage m; bool drop; const char *what; };
        const uint8_t sxOurs[] = {0xF0, 0x7D, 0x52, 0x46, 0x53, 0x00, 0xF7}, sxOther[] = {0xF0, 0x7D, 0x00, 0x01, 0xF7};
        const Case cases[] = {
            {juce::MidiMessage::noteOn(16, 53, (juce::uint8)100), true, "note 53 on ch 16 (key 0)"},
            {juce::MidiMessage::noteOn(16, 79, (juce::uint8)100), true, "note 79 on ch 16 (key 26)"},
            {juce::MidiMessage::noteOff(16, 60), true, "note off 60 on ch 16"},
            {juce::MidiMessage::noteOn(16, 60, (juce::uint8)0), true, "note on vel 0 (off) 60 on ch 16"},
            {juce::MidiMessage::noteOn(16, 52, (juce::uint8)100), false, "note 52 on ch 16 (below the keys)"},
            {juce::MidiMessage::noteOn(16, 80, (juce::uint8)100), false, "note 80 on ch 16 (above the keys)"},
            {juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), false, "note 60 on ch 1"},
            {juce::MidiMessage::noteOn(15, 0, (juce::uint8)127), true, "note 0 on ch 15 (button OCT-)"},
            {juce::MidiMessage::noteOff(15, 13), true, "note off 13 on ch 15 (button REC)"},
            {juce::MidiMessage::noteOn(15, 14, (juce::uint8)127), false, "note 14 on ch 15 (no button)"},
            {juce::MidiMessage::noteOn(16, 5, (juce::uint8)127), false, "note 5 on ch 16 (no button there)"},
            {juce::MidiMessage::controllerEvent(15, 7, 64), true, "CC 7 on ch 15 (MASTER MSB)"},
            {juce::MidiMessage::controllerEvent(15, 39, 3), true, "CC 39 on ch 15 (MASTER LSB)"},
            {juce::MidiMessage::controllerEvent(15, 16, 65), true, "CC 16 on ch 15 (encoder 0)"},
            {juce::MidiMessage::controllerEvent(15, 22, 63), true, "CC 22 on ch 15 (encoder 6)"},
            {juce::MidiMessage::controllerEvent(15, 23, 65), false, "CC 23 on ch 15"},
            {juce::MidiMessage::controllerEvent(15, 1, 65), false, "CC 1 on ch 15"},
            {juce::MidiMessage::controllerEvent(1, 7, 100), false, "CC 7 on ch 1"},
            {juce::MidiMessage::pitchWheel(15, 9000), false, "pitch bend on ch 15"},
            {juce::MidiMessage(sxOurs, (int)sizeof sxOurs), true, "RemoteFM1 SysEx F0 7D 52 46"},
            {juce::MidiMessage(sxOther, (int)sizeof sxOther), false, "other SysEx F0 7D 00"},
            {juce::MidiMessage::midiClock(), false, "clock"},
        };
        for (const Case &c : cases)
            check(FM1Processor::isRemotePanelMidi(c.m) == c.drop,
                  std::string(c.what) + (c.drop ? ": dropped" : ": passed"));
    }

    // ---- (c) discovery: which CoreMIDI endpoints get RemoteFM1's HELLO
    {
        std::printf("(c) discovery's candidates\n");
        const int32_t fm1 = FM1_REMOTE_USB_ID, onyx = (int32_t)0x0A73001F, stock = (int32_t)0x4C4AC755, roto = (int32_t)(0x1D50u << 16 | 0x6189u);
        struct Ep { int hasUsb; int32_t usb; const char *dev, *ep; bool want; const char *what; };
        const Ep eps[] = {
            {1, fm1, "RemoteFM1", "RemoteFM1", true, "USB 1209:0001 \"RemoteFM1\""},
            {1, fm1, "ChoralRoot FM-1", "ChoralRoot FM-1", true, "USB 1209:0001 with the stale ChoralRoot name"},
            {1, fm1, "FM-1_015", "FM-1_015", true, "USB 1209:0001 \"FM-1_015\""},
            {1, fm1, "Mystery", "Port 1", true, "USB 1209:0001 with any name"},
            {1, fm1, "FM-1", "FM-1 Update", false, "USB 1209:0001's updater port"},
            {1, onyx, "Onyx Producer 2x2", "Onyx Producer 2x2", false, "an Onyx Producer (its USB IDs)"},
            {1, stock, "FM-1", "FM-1", false, "an FM-1 on the stock firmware (M-VAVE's IDs 4c4a:c755)"},
            {1, (int32_t)0x12090106, "FM-106", "FM-106", false, "1209:0106 \"FM-106\""},
            {1, (int32_t)0x12090002, "Felucca Update", "Felucca Update", false, "1209:0002 \"Felucca Update\""},
            {1, roto, "Roto-Control", "Roto-Control", false, "a Roto-Control"},
            {1, onyx, "My FM-1 rack", "FM-1 thru", false, "another USB device named FM-1: its IDs win"},
            {1, (int32_t)(0x1209u << 16 | 0x0002u), "FM-1", "FM-1", false, "1209:0002 (another pid.codes device)"},
            {1, (int32_t)(0x1208u << 16 | 0x0001u), "FM-1", "FM-1", false, "1208:0001"},
            {0, 0, "ChoralRoot FM-1", "ChoralRoot FM-1", true, "no USB property, \"ChoralRoot FM-1\""},
            {0, 0, "M-VAVE fm-1", "Port 1", true, "no USB property, \"fm-1\" in the device's name"},
            {0, 0, "Device", "FM-1 MIDI", true, "no USB property, \"FM-1\" in the endpoint's name"},
            {0, 0, "Onyx Producer 2x2", "Onyx Producer 2x2", false, "no USB property, an Onyx"},
            {0, 0, "FM1VST", "FM1", false, "no USB property, \"FM1\" (no dash)"},
            {0, 0, "FM-1", "FM-1 Update", false, "no USB property, an FM-1 updater port"},
            {0, 0, nullptr, nullptr, false, "no USB property, no names"},
            {0, 0, "", "", false, "no USB property, empty names"},
        };
        for (const Ep &e : eps)
            check((fm1_remote_midi_candidate(e.hasUsb, e.usb, e.dev, e.ep) != 0) == e.want,
                  std::string(e.what) + (e.want ? ": a candidate" : ": never touched"));
    }

    // ---- (l) the unit lock
    {
        std::printf("(l) the unit lock: two links, one lock file\n");
        RemoteLink::Options o;
        o.backend = RemoteLink::Backend::Fake;
        o.lockPath = scratch.getChildFile("locktest/remote.lock").getFullPathName().toStdString();
        RemoteLink a(o), b(o);
        check(a.backend() == RemoteLink::Backend::Fake && b.backend() == RemoteLink::Backend::Fake, "both links on the fake");
        a.start();
        check(waitFor([&] { return a.status().state == RemoteLink::kConnected; }, 5000),
              "A: connected (" + a.status().text + ")");
        b.start();
        check(waitFor([&] { return b.status().state == RemoteLink::kInUse; }, 3000), "B: \"" + b.status().text + "\"");
        sleepMs(300);
        uint32_t bHellos = 99;
        b.withFake([&](rfm_fake_t *f) {
            rfm_fake_stats_t st;
            rfm_fake_stats(f, &st);
            bHellos = st.hellos;
        });
        check(!b.engaged() && bHellos == 0, "B touches nothing while in use (engaged 0, its unit saw " +
                                                std::to_string(bHellos) + " HELLO)");
        check(a.status().state == RemoteLink::kConnected, "A stays connected");
        a.stop();
        check(waitFor([&] { return a.status().state == RemoteLink::kOff; }, 3000), "A off after stop()");
        int aSession = -1;
        a.withFake([&](rfm_fake_t *f) { aSession = rfm_fake_session(f); });
        check(aSession == 0, "A's unit: the session ended (SESSION 0)");
        check(waitFor([&] { return b.status().state == RemoteLink::kConnected; }, 4000),
              "B takes over once A let go (" + b.status().text + ")");
        b.stop();
        check(waitFor([&] { return b.status().state == RemoteLink::kOff; }, 3000), "B off");
    }

    // ---- (p) the processor with Remote on
    {
        std::printf("(p) the processor with Remote on (ChoralRoot)\n");
        FM1Processor p;
        RemoteLink *rl = p.remoteForTest();
        if (!rl || rl->backend() != RemoteLink::Backend::Fake) {
            std::printf("FAIL: the processor's link is not the fake: stopping here\n");
            return 1;
        }
        Host h(p);
        h.runMs(300);
        check(!p.getRemoteEnabled() && rl->status().state == RemoteLink::kOff, "off by default");
        p.setRemoteEnabled(true);
        check(h.until([&] { return rl->status().state == RemoteLink::kConnected; }, 5000),
              "connected: \"" + rl->status().text + "\"");
        check(rl->status().bulk, "frames over the (fake) bulk pipe");
        h.runMs(1500);                           // (the boot screens)
        const fm1panel_t *H = hal(p);
        size_t diff = 0;
        check(shadowSettles(p, h, 3000, &diff), "the unit's shadow equals panel->lcd once settled (" +
                                                    std::to_string(diff) + " pixels differ, " +
                                                    std::to_string(litPixels(H)) + " lit on the LCD)");
        {
            const RemoteLink::Status st = rl->status();
            check(st.frames >= 3 && st.snapshotsTaken >= 3 && p.remotePublished() >= 3,
                  "frames " + std::to_string(st.frames) + ", acknowledged " + std::to_string(st.acks) +
                      ", snapshots published " + std::to_string(p.remotePublished()) + " / taken " +
                      std::to_string(st.snapshotsTaken));
        }
        {
            h.runMs(1100);                       // (LEDS: on a change, and at least once a second)
            bool same = false;
            uint32_t leds = 0;
            rl->withFake([&](rfm_fake_t *f) {
                const rfm_leds_t *l = rfm_fake_leds(f);
                // each LED of the panel (a level 0..255) in the plane of its class, and in no other
                same = ((l->flags & RFM_LEDS_F_PLAY_GREEN) != 0) == (p.deviceForTest()->led_play_green() != 0);
                for (int c = 0; c < FM1P_NCOL; c++)
                    for (int r = 0; r < FM1P_NLINE; r++) {
                        const int cls = fm1p_led_class(H->led[c][r]), b = 1 << (r + 1);
                        same = same && ((l->lit[c] & b) != 0) == (cls == FM1P_LC_LIT) &&
                               ((l->mid[c] & b) != 0) == (cls == FM1P_LC_MID) &&
                               ((l->dim[c] & b) != 0) == (cls == FM1P_LC_DIM) &&
                               ((l->faint[c] & b) != 0) == (cls == FM1P_LC_FAINT);
                    }
                rfm_fake_stats_t st;
                rfm_fake_stats(f, &st);
                leds = st.leds;
            });
            int lit = 0;
            for (int c = 0; c < FM1P_NCOL; c++)
                for (int r = 0; r < FM1P_NLINE; r++)
                    lit += H->led[c][r] != 0;
            check(same && leds >= 1, "the unit's LEDs equal the panel's levels / PLAY green (" + std::to_string(leds) +
                                         " LEDS messages, " + std::to_string(lit) + " LEDs on)");
        }

        // input: a key
        rl->withFake([](rfm_fake_t *f) { rfm_fake_key(f, 9, 1); });
        check(h.until([&] { return (H->keys >> 9) & 1u; }, 1000), "a key held on the unit (key 9, D4): panel->keys bit 9");
        rl->withFake([](rfm_fake_t *f) { rfm_fake_key(f, 9, 0); });
        check(h.until([&] { return !((H->keys >> 9) & 1u); }, 1000), "  ... released: bit 9 clear");
        // a button (HOME): matrix id via panel->btn_id
        {
            const uint32_t id = H->btn_id[EMU_B_HOME];
            rl->withFake([id](rfm_fake_t *f) { rfm_fake_button(f, id, 1); });
            const bool down = h.until([&] { return (H->buttons >> id) & 1u; }, 1000);
            PanelView v;
            p.getPanelView(v);
            check(down && ((v.buttons >> EMU_B_HOME) & 1u), "HOME held on the unit (matrix " + std::to_string(id) +
                                                                 "): panel->buttons, and the panel shows it held");
            rl->withFake([id](rfm_fake_t *f) { rfm_fake_button(f, id, 0); });
            check(h.until([&] { return !((H->buttons >> id) & 1u); }, 1000), "  ... released");
            h.runMs(300);
        }
        // an encoder (PRESETS): a device turn, reported on Knob Presets inside a gesture, no host detents
        {
            Watch w;
            for (int r = 0; r < EMU_NE - 1; r++)
                p.knobParam(r)->addListener(&w);
            p.masterParam()->addListener(&w);
            p.drainTier2();
            w.changes.clear();
            w.begins.clear();
            Device *d = p.deviceForTest();
            const int role = EMU_E_PRESETS, e = H->enc_id[role], dir = H->enc_dir[role] < 0 ? -1 : 1;
            const int32_t before = d->enc_total(role);
            rl->withFake([e](rfm_fake_t *f) { rfm_fake_enc(f, (uint32_t)e, 1); });
            h.until([&] { return d->enc_total(role) != before; }, 1000);
            h.runMs(200);
            p.drainTier2();
            const int idx = p.knobParam(role)->getParameterIndex();
            check(d->enc_total(role) - before == dir && p.knobDetentsSent(role) == 0,
                  "PRESETS turned a detent on the unit (matrix encoder " + std::to_string(e) + "): Device::enc " +
                      std::to_string(d->enc_total(role) - before) + " (dir " + std::to_string(dir) +
                      "), host detents " + std::to_string(p.knobDetentsSent(role)));
            check(w.begins[idx] >= 1, "  ... reported on Knob Presets inside a gesture (as a panel turn): " +
                                          std::to_string(w.begins[idx]) + " gesture(s), \"" +
                                          p.knobParam(role)->getCurrentValueAsText().toStdString() + "\"");
            rl->withFake([e](rfm_fake_t *f) { rfm_fake_enc(f, (uint32_t)e, -1); });   // (the sound back)
            h.runMs(300);
            p.drainTier2();
            // MASTER: the pot, then the parameter follows (a gesture); the parameter's older value is not applied back
            w.begins.clear();
            rl->withFake([](rfm_fake_t *f) { rfm_fake_master(f, 300); });
            const bool m300 = h.until([&] { return H->master == 300; }, 1000);
            check(m300, "MASTER 300 on the unit: panel->master " + std::to_string(H->master) + " (the parameter still " +
                            std::to_string(p.masterParam()->get()) + ")");
            h.runMs(100);
            check(H->master == 300, "  ... not undone by the parameter's older value before it follows");
            p.drainTier2();
            h.runMs(100);
            check(p.masterParam()->get() == 300 && H->master == 300 && w.begins[p.masterParam()->getParameterIndex()] >= 1,
                  "  ... Knob Master follows (inside a gesture): " + std::to_string(p.masterParam()->get()));
            *p.masterParam() = 640;              // a host change after it drives the device again
            h.runMs(50);
            check(H->master == 640, "  ... then a host change of Knob Master drives it again: " + std::to_string(H->master));
            for (int r = 0; r < EMU_NE - 1; r++)
                p.knobParam(r)->removeListener(&w);
            p.masterParam()->removeListener(&w);
        }
        // the host MIDI filter on the processor
        {
            juce::MidiBuffer m = one(juce::MidiMessage::noteOn(16, 62, (juce::uint8)100));
            h.block(m);
            check(!((H->keys >> 9) & 1u), "host MIDI: note 62 on ch 16 (the unit's own) dropped while Remote is on");
            m = one(juce::MidiMessage::noteOff(16, 62));
            h.block(m);
            m = one(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100));
            h.block(m);
            check((H->keys >> 9) & 1u, "  ... note 62 on ch 1 plays key 9");
            m = one(juce::MidiMessage::noteOff(1, 62));
            h.block(m);
            p.setRemoteIgnoreMidi(false);
            m = one(juce::MidiMessage::noteOn(16, 62, (juce::uint8)100));
            h.block(m);
            check((H->keys >> 9) & 1u, "  ... \"Ignore the FM-1's own MIDI\" off: note 62 on ch 16 plays key 9");
            m = one(juce::MidiMessage::noteOff(16, 62));
            h.block(m);
            p.setRemoteIgnoreMidi(true);
            h.runMs(100);
        }
        // a core switch: the unit's screen resent whole, Felucca on the unit
        {
            const uint32_t before = rl->status().fullResends;
            check(p.switchCore("felucca"), "switched to Felucca");
            h.runMs(1500);
            const RemoteLink::Status st = rl->status();
            size_t d2 = 0;
            const bool settled = shadowSettles(p, h, 3000, &d2);
            check(st.fullResends > before, "the switch resent the unit's screen whole (full resends " +
                                               std::to_string(before) + " -> " + std::to_string(st.fullResends) + ")");
            check(settled && litPixels(hal(p)) > 0, "the unit shows Felucca's screen: shadow == panel->lcd (" +
                                                        std::to_string(d2) + " pixels differ)");
            check(st.state == RemoteLink::kConnected && st.sessions == 1, "the session kept running (" + st.text + ")");
            check(p.switchCore("choralroot"), "back to ChoralRoot");
            h.runMs(800);
            size_t d3 = 0;
            check(shadowSettles(p, h, 3000, &d3), "  ... its screen on the unit (" + std::to_string(d3) + " differ)");
        }
        // the state: Remote on is saved; a second processor restored from it is "in use" (the lock arbitrates)
        {
            juce::MemoryBlock mb;
            p.getStateInformation(mb);
            FM1Processor q;
            q.setStateInformation(mb.getData(), (int)mb.getSize());
            check(q.getRemoteEnabled(), "the state keeps Remote on: a restored instance has it on");
            check(waitFor([&] { return q.remoteStatus().state == RemoteLink::kInUse; }, 3000),
                  "  ... and says \"" + q.remoteStatus().text + "\" (this one holds the unit)");
            check(rl->status().state == RemoteLink::kConnected, "  ... the first one stays connected");
        }
        // the toggle off: SESSION 0, the unit back on its waiting screen
        {
            p.setRemoteEnabled(false);
            check(h.until([&] { return rl->status().state == RemoteLink::kOff; }, 3000), "toggle off: the link off");
            int sess = -1;
            uint32_t ended = 0;
            rl->withFake([&](rfm_fake_t *f) {
                sess = rfm_fake_session(f);
                rfm_fake_stats_t st;
                rfm_fake_stats(f, &st);
                ended = st.sessions_ended;
            });
            check(sess == 0 && ended >= 1 && !rl->engaged(),
                  "  ... the unit got SESSION 0 (sessions ended " + std::to_string(ended) + ")");
            juce::MidiBuffer m = one(juce::MidiMessage::noteOn(16, 62, (juce::uint8)100));
            h.block(m);
            check((H->keys >> 9) & 1u, "  ... and ch 16 notes from the host play again");
            m = one(juce::MidiMessage::noteOff(16, 62));
            h.block(m);
        }
        p.releaseResources();
    }
    std::printf("remote_test: %s (%d failure%s)\n", fails ? "FAIL" : "PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
