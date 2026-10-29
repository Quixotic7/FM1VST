// SPDX-License-Identifier: GPL-3.0-only
// "Show to editors" (plugin/EditorPort.h), headless.
//   editor_port_test            (FM1EMU_EDITOR_FAKE=1: no CoreMIDI) the port names and the USB-name table; on the
//                               echo core (tests/echo_core.c): a request through the port is answered through the port
//                               while the host plays, and none of it is in the host's MIDI out; a 300-byte request
//                               whole; the M-UPGRADE keys dropped; the core's channel messages still reach the host;
//                               with the port off, the host's SysEx is answered to the host as before; the idle clock
//                               answers while processBlock is not called, and stops when blocks come again
//   editor_port_test coremidi   a CoreMIDI client (JUCE's devices, in this process) finds the virtual port by name and
//                               does an INFO round trip against FM-106 (its Librarian's dump request, F0 41 34 ..; a
//                               public build: Felucca's INFO) and
//                               against the echo core, idle and while playing; ChoralRoot's INFO, LIST and RESTART (its
//                               SysEx glue; the reboot powered on again with the host idle); two instances get distinct
//                               names; no SysEx in the host's MIDI out. Exit 77 (skipped) without CoreMIDI.
//   editor_port_test managers   the managers end to end through the CoreMIDI port (runManagers): INFO, LIST, GET, a
//                               PUT read back, for every firmware with a backup manager, playing and idle; a table of
//                               the latencies
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Processor.h"
#include "built_cores.h"

static int fails = 0;
static void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    fails += !c;
}

using Bytes = std::vector<uint8_t>;
static std::string hex(const Bytes &b, size_t max = 24)
{
    std::string s;
    char t[4];
    for (size_t i = 0; i < b.size() && i < max; i++)
        std::snprintf(t, sizeof t, "%02X ", b[i]), s += t;
    if (b.size() > max)
        s += "..";
    return s;
}

static void pump(double ms)
{
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, ms / 1000.0, false);
}
static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

// one processor driven block by block; what it gives the host is kept
struct Host {
    FM1Processor &p;
    int bs = 256;
    double sr = 48000;
    juce::AudioBuffer<float> buf{2, 256};
    int sysexToHost = 0, channelToHost = 0;
    std::vector<Bytes> hostSysex;
    double lastBlock = 0;                        // (wall ms) when the last block started
    double idleRoom = 0;                         // the most device ms the idle clock may have run in the gaps between
                                                 // blocks (a gap longer than idleAfterMs: the host was late)
    explicit Host(FM1Processor &proc) : p(proc)
    {
        p.setRateAndBufferSizeDetails(sr, bs);
        p.prepareToPlay(sr, bs);
    }
    void block(juce::MidiBuffer &m)
    {
        const double now = nowMs(), after = p.editorPortForTest()->idleAfterMs();
        if (lastBlock > 0 && now - lastBlock > after)
            idleRoom += now - lastBlock - after + EditorPort::kIdleMaxStep;
        lastBlock = now;
        buf.clear();
        p.processBlock(buf, m);
        for (const auto e : m) {
            if (e.data[0] == 0xF0)
                sysexToHost++, hostSysex.emplace_back(e.data, e.data + e.numBytes);
            else if (e.data[0] < 0xF0)
                channelToHost++;
        }
    }
    void block()
    {
        juce::MidiBuffer m;
        block(m);
    }
    void blocks(int n)
    {
        for (int i = 0; i < n; i++)
            block();
    }
    // blocks at the host's real pace for ms (a playing host)
    void play(double ms)
    {
        const double end = nowMs() + ms;
        while (nowMs() < end) {
            block();
            std::this_thread::sleep_for(std::chrono::microseconds((int)(1e6 * bs / sr)));
        }
    }
};

static Bytes sx(std::initializer_list<int> b)
{
    Bytes o;
    for (int x : b)
        o.push_back((uint8_t)x);
    return o;
}
static juce::MidiMessage msgOf(const Bytes &b) { return juce::MidiMessage(b.data(), (int)b.size()); }

// the echo core in the user cores folder (it is not one of the build's cores)
static bool installEcho()
{
    const juce::File src(FM1_ECHO_CORE);
    const juce::File dst = FM1Processor::userCoresDir().getChildFile("echo" FM1CORE_SUFFIX);
    FM1Processor::userCoresDir().createDirectory();
    return src.existsAsFile() && src.copyFileTo(dst);
}

// the fake port's replies, waited for (the service thread sends them)
static std::vector<Bytes> waitSent(EditorPort &ep, size_t want, double ms, Host *playing = nullptr)
{
    std::vector<Bytes> got;
    const double end = nowMs() + ms;
    while (got.size() < want && nowMs() < end) {
        if (playing)
            playing->play(5);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (auto &b : ep.takeSentForTest())
            got.push_back(std::move(b));
    }
    return got;
}

static uint32_t ticksOf(const Bytes &r)   // the echo core's tick count reply: F0 7D 45 43 01 t0 t1 t2 t3 F7
{
    return r.size() == 10 ? (uint32_t)r[5] | (uint32_t)r[6] << 7 | (uint32_t)r[7] << 14 | (uint32_t)r[8] << 21 : 0u;
}

// ============================================================== fake mode ===
static int runFake()
{
    std::printf("[names]\n");
    check(EditorPort::nameFor("Felucca", 1) == "Felucca (FM1VST)", "first port: \"Felucca (FM1VST)\"");
    check(EditorPort::nameFor("ChoralRoot FM-1", 2) == "ChoralRoot FM-1 (FM1VST 2)",
          "second: \"ChoralRoot FM-1 (FM1VST 2)\" (ChoralRoot's case-sensitive includes still matches)");
    struct {
        const char *id, *usb;
    } table[] = {{"choralroot", "ChoralRoot FM-1"}, {"purplemonkey", "ChoralRoot FM-1"}, {"felucca", "Felucca"},
                 {"sloop", "Felucca"},              {"melodee", "Melodee FM-1"},              {"jiant", "JIANT FM"},
                 {"duell", "DUELL"},                {"x0x", "X0X FM-1"},                 {"fm106", "FM-106"},
                 {"coralflower", "CoralFlower FM-1"}, {"fart", "FoMni FM-1"},           {"amb", "AMB-1 FM-1"},
                 {"fimba", "FiMba FM-1"},           {"fomni", "FoMni FM-1"},             {"obervave", "Felucca"},
                 {"loom", "LOOM-8 FM-1"},           {"minitone", "Felucca"},             {"fmini", "FM-1 Midi"},
                 {"skiff", "Felucca"},              {"grimm", "Felucca"},                {"pigeon", "PIGEON"},
                 {"tonitor", "FM-1 Tonitor"},       {"speak37", "Speak37"},              {"s1vave", "Felucca"},
                 {"unknown-fw", "FM-1"}};
    bool all = true;
    for (auto &t : table)
        all = all && EditorPort::usbProductName(t.id) == t.usb;
    check(all, "USB names by core / Hub id (usb.c STR2 / USB_STRS), unknown: \"FM-1\"");
    check(EditorPort::managerUrl("felucca").startsWith("https://hugelton.github.io/Felucca/"), "Felucca's editor URL");
    check(EditorPort::managerUrl("pigeon").startsWith("https://theshrimpblimp.com/pigeon/webapp/pigeon/"),
          "PIGEON's sample loader URL");
    check(EditorPort::managerUrl("speak37") == "https://genkigenki.github.io/smk37/", "Speak37's installer URL");
    check(EditorPort::managerUrl("x", "https://example.org/ed/") == "https://example.org/ed/", "the catalog's editor wins");
    check(EditorPort::isUpdateKey(sx({0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7}).data(), 6) &&
              EditorPort::isUpdateKey(sx({0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7}).data(), 6),
          "UBOOT and OTA start are recognised");

    if (!installEcho()) {
        check(false, "the echo core is built: " FM1_ECHO_CORE);
        return 1;
    }
    FM1Processor p;
    check(p.switchCore("echo"), "the echo core loads");
    Host h(p);
    h.blocks(4);
    check(!p.getEditorPortEnabled() && !p.editorPortStatus().open, "off by default: no port");

    std::printf("[the port on, the host playing]\n");
    p.setEditorPortEnabled(true);
    const auto st = p.editorPortStatus();
    check(st.open && st.name == "FM-1 (FM1VST)", "open as \"" + st.name.toStdString() + "\" (an unknown firmware: FM-1)");
    EditorPort &ep = *p.editorPortForTest();
    h.sysexToHost = 0, h.channelToHost = 0;
    const Bytes info = sx({0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7});
    ep.receiveForTest(msgOf(info));
    auto got = waitSent(ep, 1, 500, &h);
    check(got.size() == 1 && got[0] == sx({0xF0, 0x7D, 0x45, 0x43, 0x7D, 0x46, 0x4C, 0x01, 0xF7}),
          "INFO through the port answered through the port: " + (got.empty() ? std::string("nothing") : hex(got[0])));
    check(h.sysexToHost == 0, "nothing of it in the host's MIDI out (" + std::to_string(h.sysexToHost) + " SysEx)");

    Bytes big = {0xF0, 0x7D, 0x46, 0x4C, 0x43};  // (a BACKUP_PUT-sized frame: 256 bytes pack7 ~ 300)
    for (int i = 0; i < 300; i++)
        big.push_back((uint8_t)(i & 0x7F));
    big.push_back(0xF7);
    ep.receiveForTest(msgOf(big));
    got = waitSent(ep, 1, 500, &h);
    check(got.size() == 1 && got[0].size() == big.size() + 3 && std::memcmp(got[0].data() + 4, big.data() + 1, big.size() - 2) == 0,
          "a 306-byte request whole, its reply whole (" + std::to_string(got.empty() ? 0 : got[0].size()) + " bytes)");

    const uint32_t drop0 = ep.dropped();
    ep.receiveForTest(msgOf(sx({0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7})));
    ep.receiveForTest(msgOf(sx({0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7})));
    got = waitSent(ep, 1, 100, &h);
    check(ep.dropped() == drop0 + 2 && got.empty(), "UBOOT and OTA start never reach the core");

    // the host's MIDI as before: a note in comes back out to the host (the core's own MIDI out)
    {
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::controllerEvent(1, 74, 10), 0);
        h.channelToHost = 0;
        h.block(m);
        h.blocks(4);
        check(h.channelToHost == 1, "the core's channel messages still go to the host");
    }
    // while the port is open, the core's SysEx (whoever asked) goes to the port
    {
        juce::MidiBuffer m;
        m.addEvent(msgOf(sx({0xF0, 0x7D, 0x01, 0x02, 0xF7})), 0);
        h.sysexToHost = 0;
        h.block(m);
        got = waitSent(ep, 1, 300, &h);
        check(got.size() == 1 && h.sysexToHost == 0, "port open: the core's SysEx to the port, none to the host");
    }

    std::printf("[the idle clock]\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(120));   // (no blocks: the host stopped)
    check(ep.idling() && ep.idleMs() > 0, "no block for 50 ms: the core runs from the port's thread (" +
                                              std::to_string(ep.idleMs()) + " device ms)");
    const Bytes ask = sx({0xF0, 0x7D, 0x45, 0x43, 0x01, 0xF7});
    const double t0 = nowMs();
    ep.receiveForTest(msgOf(ask));
    got = waitSent(ep, 1, 500);
    const double t1 = nowMs(), dt = t1 - t0;
    const uint32_t tk1 = got.size() == 1 ? ticksOf(got[0]) : 0;
    check(got.size() == 1 && tk1 > 0 && dt < 50, "answered with processBlock idle, in " + std::to_string((int)dt) + " ms");
    // the device ms between two answers against the wall ms between them, as measured (a busy machine oversleeps):
    // never more (the catch-up and one step of tolerance: the interval is measured from before the first answer to
    // after the second)
    const double s0 = nowMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const double overslept = nowMs() - s0 - 200;
    ep.receiveForTest(msgOf(ask));
    got = waitSent(ep, 1, 500);
    const double t2 = nowMs();
    const uint32_t tk2 = got.size() == 1 ? ticksOf(got[0]) : 0;
    const double wall = t2 - t0, slack = EditorPort::kIdleCatchUp + EditorPort::kIdleMaxStep;
    check(tk2 > tk1 && tk2 - tk1 <= wall + slack,
          "the idle clock never runs ahead of real time: " + std::to_string(tk2 - tk1) + " device ms in " +
              std::to_string((int)wall) + " wall ms (+ " + std::to_string((int)slack) + " tolerance)");
    // (a starved machine, this test's own 200 ms sleep 50 ms late or more: the idle clock drops what it cannot catch
    // up, by design, so it is not held to keeping up there)
    if (overslept < 50)
        check(tk2 - tk1 >= wall / 2, "  and keeps up with it (at least half)");
    else
        std::printf("  (this machine is starved: a 200 ms sleep took %d ms; keeping up not checked)\n", (int)(200 + overslept));

    h.play(60);                                  // (the host plays again)
    const uint64_t idle0 = ep.idleMs();
    h.idleRoom = 0;
    h.play(150);
    const uint64_t ran = ep.idleMs() - idle0;
    check((h.idleRoom > 0 || !ep.idling()) && ran <= (uint64_t)h.idleRoom,
          "blocks again: the idle clock stopped (" + std::to_string(ran) + " idle ms while playing; " +
              std::to_string((int)h.idleRoom) + " allowed by the host's late blocks)");
    h.sysexToHost = 0;
    ep.receiveForTest(msgOf(info));
    got = waitSent(ep, 1, 500, &h);
    check(got.size() == 1 && h.sysexToHost == 0, "and the host's clock answers again");

    std::printf("[the port off]\n");
    p.setEditorPortEnabled(false);
    check(!p.editorPortStatus().open, "off: the port is gone");
    {
        juce::MidiBuffer m;
        m.addEvent(msgOf(sx({0xF0, 0x7D, 0x01, 0x02, 0xF7})), 0);
        h.sysexToHost = 0, h.hostSysex.clear();
        h.block(m);
        h.blocks(4);
        check(h.sysexToHost == 1 && h.hostSysex[0] == sx({0xF0, 0x7D, 0x45, 0x43, 0x7D, 0x01, 0x02, 0xF7}),
              "the host's SysEx is answered to the host, as before");
    }
    const uint64_t idleOff = ep.idleMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    check(ep.idleMs() == idleOff, "no idle clock without the port");

    // the state
    p.setEditorPortEnabled(true);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    {
        FM1Processor q;
        q.setStateInformation(state.getData(), (int)state.getSize());
        q.syncEditorPort();
        check(q.getEditorPortEnabled() && q.editorPortStatus().open, "the switch is in the state");
    }
    p.setEditorPortEnabled(false);
    return fails ? 1 : 0;
}

// =========================================================== CoreMIDI mode ===
struct Client : juce::MidiInputCallback {
    std::mutex m;
    std::vector<Bytes> got;
    std::unique_ptr<juce::MidiInput> in;
    std::unique_ptr<juce::MidiOutput> out;
    void handleIncomingMidiMessage(juce::MidiInput *, const juce::MidiMessage &msg) override
    {
        std::lock_guard<std::mutex> l(m);
        got.emplace_back(msg.getRawData(), msg.getRawData() + msg.getRawDataSize());
    }
    // the editors' way: an input and an output with the port's name
    bool connect(const juce::String &name)
    {
        for (const auto &d : juce::MidiInput::getAvailableDevices())
            if (d.name == name)
                in = juce::MidiInput::openDevice(d.identifier, this);
        for (const auto &d : juce::MidiOutput::getAvailableDevices())
            if (d.name == name)
                out = juce::MidiOutput::openDevice(d.identifier);
        if (in)
            in->start();
        return in && out;
    }
    std::vector<Bytes> take()
    {
        std::lock_guard<std::mutex> l(m);
        std::vector<Bytes> o;
        o.swap(got);
        return o;
    }
    // send, then wait for a SysEx reply whose first bytes are `prefix`; the time it took, or -1
    double roundTrip(const Bytes &req, const Bytes &prefix, Host *playing, Bytes *reply = nullptr)
    {
        take();
        const double t0 = nowMs();
        out->sendMessageNow(msgOf(req));
        while (nowMs() - t0 < 1000) {
            if (playing)
                playing->play(3);
            else
                pump(2);
            for (auto &b : take())
                if (b.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), b.begin())) {
                    if (reply)
                        *reply = b;
                    return nowMs() - t0;
                }
        }
        return -1;
    }
};

static bool listed(const juce::String &name)
{
    bool i = false, o = false;
    for (const auto &d : juce::MidiInput::getAvailableDevices())
        i = i || d.name == name;
    for (const auto &d : juce::MidiOutput::getAvailableDevices())
        o = o || d.name == name;
    return i && o;
}
// CoreMIDI tells its clients of new and gone endpoints through their run loop: wait (pumping) until the list agrees
static bool waitListed(const juce::String &name, bool want, double ms = 2000)
{
    const double end = nowMs() + ms;
    while (listed(name) != want && nowMs() < end)
        pump(10);
    return listed(name) == want;
}

static int runCoreMidi()
{
    // the first firmware: FM-106 (its Librarian's dump request), or in a public build (FM-106 gated) Felucca (INFO)
    const bool fm106 = built_cores().count("fm106") > 0;
    if (!fm106 && !built_cores().count("felucca")) {
        std::printf("SKIP: this build has neither FM-106 nor Felucca\n");
        return 77;
    }
    const char *firstId = fm106 ? "fm106" : "felucca";
    const std::string firstUsb = fm106 ? "FM-106" : "Felucca";
    FM1Processor a;
    if (!a.switchCore(firstId)) {
        std::printf("FAIL: %s does not load\n", firstId);
        return 1;
    }
    Host ha(a);
    ha.blocks(fm106 ? 200 : 600);                // (booted, its librarian / editor service up)
    a.setEditorPortEnabled(true);
    const auto sa = a.editorPortStatus();
    if (!sa.open) {
        std::printf("SKIP: no virtual CoreMIDI port could be made here (%s)\n", sa.error.toRawUTF8());
        return 77;
    }
    std::printf("[CoreMIDI: %s]\n", firstUsb.c_str());
    check(sa.name.toStdString() == firstUsb + " (FM1VST)", "the port: \"" + sa.name.toStdString() + "\"");
    check(waitListed(sa.name, true), "CoreMIDI lists a source and a destination by that name");

    FM1Processor b;
    b.switchCore(firstId);
    Host hb(b);
    hb.blocks(4);
    b.setEditorPortEnabled(true);
    const auto sb = b.editorPortStatus();
    check(sb.open && sb.name.toStdString() == firstUsb + " (FM1VST 2)" && sb.name != sa.name,
          "a second instance: \"" + sb.name.toStdString() + "\" (the first name is taken)");
    check(waitListed(sa.name, true) && waitListed(sb.name, true), "both listed");

    Client c;
    check(c.connect(sa.name), "a client finds the port by name and opens both directions");
    if (!c.in || !c.out)
        return 1;
    // FM-106's Librarian: a dump request (patch 0), answered F0 41 30 ..; Felucca's editor: INFO
    const Bytes dump = fm106 ? sx({0xF0, 0x41, 0x34, 0x00, 0x00, 0xF7}) : sx({0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7});
    const Bytes dumpReply = fm106 ? sx({0xF0, 0x41, 0x30}) : sx({0xF0, 0x7D, 0x46, 0x4C, 0x01});
    Bytes reply;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    double t = c.roundTrip(dump, dumpReply, nullptr, &reply);
    check(t >= 0 && t < 300, firstUsb + " answers with the host idle (the idle clock): " + std::to_string((int)t) + " ms, " +
                                 hex(reply, 12));
    ha.sysexToHost = 0;
    t = c.roundTrip(dump, dumpReply, &ha, &reply);
    check(t >= 0 && t < 300, "and while the host plays: " + std::to_string((int)t) + " ms");
    ha.play(50);
    check(ha.sysexToHost == 0, "no SysEx in the host's MIDI out (" + std::to_string(ha.sysexToHost) + ")");

    // the second instance answers its own port
    Client c2;
    if (c2.connect(sb.name)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        t = c2.roundTrip(dump, dumpReply, nullptr);
        check(t >= 0 && t < 300, "the second instance answers on its own port: " + std::to_string((int)t) + " ms");
    } else {
        check(false, "a client opens the second port");
    }

    // ChoralRoot (its SysEx glue: the managers' FL protocol): INFO, LIST and RESTART as its device manager sends them
    if (built_cores().count("choralroot")) {
        std::printf("[CoreMIDI: ChoralRoot]\n");
        FM1Processor cr;
        check(cr.switchCore("choralroot"), "ChoralRoot loads");
        Host hc(cr);
        hc.blocks(400);                          // (booted)
        cr.setEditorPortEnabled(true);
        const auto sc = cr.editorPortStatus();
        Client cc;
        check(sc.name == "ChoralRoot FM-1 (FM1VST)" && waitListed(sc.name, true) && cc.connect(sc.name),
              "\"" + sc.name.toStdString() + "\" found (its manager: name.includes(\"ChoralRoot FM-1\"), in and out alike)");
        if (cc.out) {
            const Bytes info = sx({0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7});
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            t = cc.roundTrip(info, sx({0xF0, 0x7D, 0x46, 0x4C, 0x01}), nullptr, &reply);
            const std::string text(reply.begin(), reply.end());
            check(t >= 0 && t < 300 && text.find("ChoralRoot") != std::string::npos,
                  "INFO answered, host idle: " + std::to_string((int)t) + " ms (the version text names ChoralRoot)");
            hc.sysexToHost = 0;
            t = cc.roundTrip(info, sx({0xF0, 0x7D, 0x46, 0x4C, 0x01}), &hc);
            check(t >= 0 && t < 300, "INFO answered, host playing: " + std::to_string((int)t) + " ms");
            t = cc.roundTrip(sx({0xF0, 0x7D, 0x46, 0x4C, 0x41, 0xF7}), sx({0xF0, 0x7D, 0x46, 0x4C, 0x41}), nullptr, &reply);
            check(t >= 0 && t < 1500 && reply.size() > 8, "LIST (65) answered: " + std::to_string(reply.size()) + " bytes");
            hc.play(30);
            check(hc.sysexToHost == 0, "none of it in the host's MIDI out");
            // RESTART (72): the reply, then the firmware's reboot (a halt the plugin powers on again, idle too)
            t = cc.roundTrip(sx({0xF0, 0x7D, 0x46, 0x4C, 0x48, 0xF7}), sx({0xF0, 0x7D, 0x46, 0x4C, 0x48}), nullptr);
            check(t >= 0 && t < 1500, "RESTART (72) answered: " + std::to_string((int)t) + " ms");
            const double end = nowMs() + 3000;
            double back = -1;
            while (nowMs() < end && back < 0) {
                pump(20);                        // (the halt's async power cycle runs on the message thread)
                back = cc.roundTrip(info, sx({0xF0, 0x7D, 0x46, 0x4C, 0x01}), nullptr);
            }
            check(back >= 0, "after the reboot INFO is answered again (host idle)");
        }
        cr.setEditorPortEnabled(false);
    }

    std::printf("[CoreMIDI: the echo core]\n");
    if (installEcho()) {
        FM1Processor e;
        e.switchCore("echo");
        Host he(e);
        he.blocks(4);
        e.setEditorPortEnabled(true);
        const auto se = e.editorPortStatus();
        Client ce;
        waitListed(se.name, true);
        check(se.open && ce.connect(se.name), "\"" + se.name.toStdString() + "\" found");
        if (ce.out) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            t = ce.roundTrip(sx({0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7}), sx({0xF0, 0x7D, 0x45, 0x43, 0x7D, 0x46, 0x4C, 0x01, 0xF7}),
                             nullptr, &reply);
            check(t >= 0 && t < 300, "INFO echoed through CoreMIDI, idle: " + std::to_string((int)t) + " ms");
            he.sysexToHost = 0;
            t = ce.roundTrip(sx({0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7}), sx({0xF0, 0x7D, 0x45, 0x43}), &he);
            he.play(30);
            check(t >= 0 && he.sysexToHost == 0, "and playing, with nothing in the host's MIDI out");
            // a client's own request never comes back to it (no echo of the request: CoreMIDI is not a loop bus)
            bool echoed = false;
            ce.out->sendMessageNow(msgOf(sx({0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7})));
            for (int i = 0; i < 50; i++)
                pump(2);
            for (auto &m : ce.take())
                echoed = echoed || m == sx({0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7});
            check(!echoed, "the request itself never comes back on the port");
        }
        e.setEditorPortEnabled(false);
    }
    b.setEditorPortEnabled(false);
    a.setEditorPortEnabled(false);
    check(waitListed(sa.name, false), "switched off: the port is gone from CoreMIDI");
    return fails ? 1 : 0;
}

// ========================================================= managers mode ===
// The firmwares' managers end to end through the real CoreMIDI port: a client finds "<USB name> (FM1VST)" and sends
// the managers' own requests (MANAGERS.md section 2; the same bytes as tests/sysex_test.cpp): INFO, the backup's LIST,
// a GET of one object, a PUT of a changed preset read back (X0X: its backup page's 0x42 backup and 0x41 restore, SEL
// pressed on the panel). Each firmware twice: the host playing (processBlock at its pace), then idle (no block: the
// port's idle clock). The latencies are printed as a table.
static uint32_t crc32Of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = c >> 1 ^ (c & 1u ? 0xEDB88320u : 0u);
    }
    return ~c;
}
static Bytes fl(uint8_t cmd, const Bytes &args = {})
{
    Bytes f = {0xF0, 0x7D, 0x46, 0x4C, cmd};
    f.insert(f.end(), args.begin(), args.end());
    f.push_back(0xF7);
    return f;
}
static std::function<bool(const Bytes &)> flReply(uint8_t cmd)
{
    return [cmd](const Bytes &f) { return f.size() >= 6 && f[1] == 0x7D && f[2] == 0x46 && f[3] == 0x4C && f[4] == cmd; };
}
static Bytes flArgs(const Bytes &f) { return f.size() >= 6 ? Bytes(f.begin() + 5, f.end() - 1) : Bytes(); }
static void u32(Bytes &a, uint32_t v)
{
    for (int i = 0; i < 5; i++)
        a.push_back((uint8_t)(v >> (7 * i) & 127u));
}
static uint32_t r32(const Bytes &a, size_t p)
{
    return (uint32_t)a[p] | (uint32_t)a[p + 1] << 7 | (uint32_t)a[p + 2] << 14 | (uint32_t)a[p + 3] << 21 |
           (uint32_t)a[p + 4] << 28;
}
static Bytes pack7(const uint8_t *p, size_t n)                  // bkPack
{
    Bytes a;
    for (size_t off = 0; off < n; off += 7) {
        const size_t k = std::min<size_t>(7, n - off);
        uint8_t m = 0;
        for (size_t i = 0; i < k; i++)
            m |= (uint8_t)(p[off + i] >> 7 << i);
        a.push_back(m);
        for (size_t i = 0; i < k; i++)
            a.push_back(p[off + i] & 127u);
    }
    return a;
}
static bool unpack7(const Bytes &a, size_t from, size_t size, Bytes &out)   // bkUnpack
{
    out.assign(size, 0);
    size_t i = from, j = 0;
    while (j < size) {
        const size_t n = std::min<size_t>(7, size - j);
        if (i >= a.size())
            return false;
        const uint8_t m = a[i++];
        for (size_t k = 0; k < n; k++) {
            if (i >= a.size() || a[i] > 127)
                return false;
            out[j++] = (uint8_t)(a[i++] | (m >> k & 1u) << 7);
        }
    }
    return i == a.size();
}
// the update framing (X0X web/backup/index.html PB.frame / parseFrame): F0 pack7(00 59 cmd len24 body ~sum) F7
static Bytes updUnpack(const Bytes &f)
{
    Bytes out;
    uint32_t acc = 0, nb = 0;
    for (size_t i = 1; i + 1 < f.size(); i++) {
        acc |= (uint32_t)(f[i] & 127u) << nb;
        nb += 7;
        if (nb >= 8) {
            out.push_back((uint8_t)acc);
            acc >>= 8;
            nb -= 8;
        }
    }
    return out;
}
static Bytes updWire(const Bytes &m)
{
    Bytes f = {0xF0};
    uint32_t acc = 0, nb = 0;
    for (uint8_t b : m) {
        acc |= (uint32_t)b << nb;
        nb += 8;
        while (nb >= 7) {
            f.push_back(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7;
        }
    }
    if (nb)
        f.push_back(acc & 0x7Fu);
    f.push_back(0xF7);
    return f;
}
static Bytes updFrame(uint8_t cmd, const Bytes &body)
{
    const size_t n = body.size();
    Bytes m = {0x00, 0x59, cmd, (uint8_t)(n & 0xFF), (uint8_t)(n >> 8 & 0xFF), 0};
    uint32_t sum = 0;
    for (uint8_t b : body)
        m.push_back(b), sum += b;
    m.push_back((uint8_t)~sum);
    return updWire(m);
}
static bool updParse(const Bytes &f, uint8_t &cmd, Bytes &body)
{
    if (f.size() < 2 || f.front() != 0xF0 || f.back() != 0xF7)
        return false;
    const Bytes u = updUnpack(f);
    if (u.size() < 7 || u[0] != 0 || u[1] != 0x59)
        return false;
    const size_t n = (size_t)u[3] | (size_t)u[4] << 8 | (size_t)u[5] << 16;
    if (n + 7 != u.size())
        return false;
    uint32_t sum = 0;
    for (size_t i = 6; i < 6 + n; i++)
        sum += u[i];
    if ((uint8_t)~sum != u[6 + n])
        return false;
    cmd = u[2];
    body.assign(u.begin() + 6, u.begin() + 6 + (std::ptrdiff_t)n);
    return true;
}
static Bytes le32(uint32_t v) { return {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; }
static uint32_t rd32le(const Bytes &b, size_t o)
{
    return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24;
}

// a DX7 32-voice VMEM bulk dump (fm1-editor.com, Dexed), voices named "SYSEX Vnn"
static Bytes dx7Bulk()
{
    Bytes d(4096, 0);
    for (int v = 0; v < 32; v++) {
        uint8_t *o = &d[(size_t)v * 128];
        for (int op = 0; op < 6; op++) {
            uint8_t *x = &o[op * 17];
            x[0] = x[1] = x[2] = x[3] = 99;
            x[4] = 99;
            x[14] = op == 5 ? 99 : 70;
            x[15] = 2;
        }
        o[102] = o[103] = o[104] = o[105] = 99;
        o[106] = o[107] = o[108] = o[109] = 50;
        o[110] = (uint8_t)(v % 32);
        o[111] = 7;
        o[117] = 24;
        char name[11];
        std::snprintf(name, sizeof name, "SYSEX V%02d ", v + 1);
        std::memcpy(&o[118], name, 10);
    }
    Bytes f = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    uint32_t sum = 0;
    for (uint8_t b : d)
        f.push_back(b), sum += b;
    f.push_back((uint8_t)(-(int)sum & 127));
    f.push_back(0xF7);
    return f;
}

struct MObj {
    uint32_t id, size, crc;
};
static const MObj *findObj(const std::vector<MObj> &m, uint32_t id)
{
    for (const MObj &o : m)
        if (o.id == id)
            return &o;
    return nullptr;
}

// one manager on the port: one request in flight, as every manager sends them
struct Manager {
    Client &c;
    Host &h;
    bool playing = true;
    std::string why;
    uint8_t bk = 65;                             // FL 65 / 66 / 67; SLOOP's editor.c v6: 34 / 35 / 36
    size_t listHdr = 2;
    // DUELL 1.0's patch records (EDITOR_PROTOCOL.md "Patch records in the archive"): LIST page 1..recPages after the
    // plain LIST; GET / PUT of the wire id 40 with the slot as one more byte. A record is the object kRec + slot
    static constexpr uint32_t kRec = 1000;
    uint8_t recPages = 0;
    static uint8_t wire(uint32_t id) { return (uint8_t)(id >= kRec ? 40u : id); }
    double worst = 0;                            // the longest single request
    void step()
    {
        if (playing)
            h.play(3);
        else
            pump(2);
    }
    void settle(double ms)                       // time passing with nothing sent (the host as the mode says)
    {
        const double end = nowMs() + ms;
        while (nowMs() < end)
            step();
    }
    double request(const Bytes &q, const std::function<bool(const Bytes &)> &match, Bytes &reply, double limit)
    {
        c.take();
        const double t0 = nowMs();
        c.out->sendMessageNow(msgOf(q));
        while (nowMs() - t0 < limit) {
            step();
            for (auto &b : c.take())
                if (match(b)) {
                    reply = std::move(b);
                    const double t = nowMs() - t0;
                    worst = std::max(worst, t);
                    return t;
                }
        }
        return -1;
    }
    double info(std::string &version, double limit = 1500)
    {
        Bytes r;
        const double t = request(fl(1), flReply(1), r, limit);
        version.clear();
        for (uint8_t b : flArgs(r)) {
            if (!b)
                break;
            version += (char)b;
        }
        return t;
    }
    double list(std::vector<MObj> &m)
    {
        Bytes r;
        const double t = request(fl(bk), flReply(bk), r, 3000);
        if (t < 0)
            return why = "LIST: no reply", -1;
        const Bytes a = flArgs(r);
        const size_t hd = listHdr;
        if (a.size() < hd + 1 || (hd == 2 && a[0] != 1) || a[hd - 1] != 0)
            return why = "LIST: rc", -1;
        const size_t n = a[hd];
        if (a.size() != hd + 1 + n * 11)
            return why = "LIST: length", -1;
        m.clear();
        for (size_t i = 0; i < n; i++)
            m.push_back({a[hd + 1 + i * 11], r32(a, hd + 1 + i * 11 + 1), r32(a, hd + 1 + i * 11 + 6)});
        for (uint8_t pg = 1; pg <= recPages; pg++) {   // (the saved patches' records: slot, size, crc each)
            if (request(fl(bk, {pg}), flReply(bk), r, 3000) < 0)
                return why = "LIST page " + std::to_string(pg) + ": no reply", -1;
            const Bytes p = flArgs(r);
            if (p.size() < 3 || p[0] != 1 || p[1] != 0 || p.size() != 3 + (size_t)p[2] * 11)
                return why = "LIST page " + std::to_string(pg) + ": rc or length", -1;
            for (size_t i = 0; i < p[2]; i++)
                m.push_back({kRec + p[3 + i * 11], r32(p, 3 + i * 11 + 1), r32(p, 3 + i * 11 + 6)});
        }
        return t;
    }
    double get(const MObj &o, Bytes &bytes)      // captureBackup for one object: 256-byte chunks
    {
        const double t0 = nowMs();
        bytes.assign(o.size, 0);
        for (uint32_t off = 0; off < o.size; off += 256) {
            const uint32_t size = std::min<uint32_t>(256, o.size - off);
            Bytes args = {wire(o.id)};
            u32(args, off);
            args.push_back(size & 127u);
            args.push_back((uint8_t)(size >> 7));
            if (o.id >= kRec)
                args.push_back((uint8_t)(o.id - kRec));
            Bytes r, chunk;
            if (request(fl(bk + 1, args), flReply(bk + 1), r, 1000) < 0)
                return why = "GET " + std::to_string(o.id) + " @" + std::to_string(off) + ": no reply", -1;
            const Bytes a = flArgs(r);
            if (a.size() < 9 || a[1] || a[0] != wire(o.id) || r32(a, 2) != off || (a[7] | a[8] << 7) != (int)size ||
                !unpack7(a, 9, size, chunk))
                return why = "GET " + std::to_string(o.id) + ": unexpected reply", -1;
            std::copy(chunk.begin(), chunk.end(), bytes.begin() + off);
        }
        if (crc32Of(bytes.data(), bytes.size()) != o.crc)
            return why = "GET " + std::to_string(o.id) + ": CRC differs from LIST's", -1;
        return nowMs() - t0;
    }
    int putOp(const Bytes &args, double limit = 4000)
    {
        Bytes r;
        if (request(fl(bk + 2, args), flReply(bk + 2), r, limit) < 0)
            return -1;
        const Bytes a = flArgs(r);
        return a.size() >= 3 ? a[2] : -1;
    }
    double put(uint32_t id, const Bytes &b)      // restoreBackup: begin / data / commit
    {
        const double t0 = nowMs();
        const uint8_t w = wire(id);
        Bytes begin = {0, w};
        u32(begin, (uint32_t)b.size());
        u32(begin, crc32Of(b.data(), b.size()));
        if (id >= kRec)
            begin.push_back((uint8_t)(id - kRec));
        int rc = putOp(begin);
        for (uint32_t off = 0; !rc && off < b.size(); off += 256) {
            Bytes d = {1, w};
            u32(d, off);
            const size_t n = std::min<size_t>(256, b.size() - off);
            const Bytes pk = pack7(b.data() + off, n);
            d.insert(d.end(), pk.begin(), pk.end());
            if ((rc = putOp(d)))
                putOp({3, w});
        }
        if (!rc)
            rc = putOp({2, w}, 5000);
        if (rc)
            return why = "PUT " + std::to_string(id) + ": rc " + std::to_string(rc), -1;
        return nowMs() - t0;
    }

    // ---- X0X's backup page
    double x0xBackup(Bytes &image)
    {
        c.take();
        image.clear();
        const double t0 = nowMs();
        c.out->sendMessageNow(msgOf(updFrame(0x42, {0x58, 0x30, 0x58, 0x42})));
        while (nowMs() - t0 < 20000) {
            step();
            for (auto &f : c.take()) {
                uint8_t cmd;
                Bytes b;
                if (!updParse(f, cmd, b))
                    continue;
                if (cmd == 0x42 && b.size() >= 4) {
                    if (rd32le(b, 0) != image.size())
                        return why = "backup: a frame's offset skips", -1;
                    image.insert(image.end(), b.begin() + 4, b.end());
                } else if (cmd == 0x43 && b.size() >= 10) {
                    if (b[8] || rd32le(b, 0) != image.size() || rd32le(b, 4) != crc32Of(image.data(), image.size()))
                        return why = "backup: the end frame's status, length or CRC-32", -1;
                    return nowMs() - t0;
                }
            }
        }
        return why = "backup: no end frame", -1;
    }
    // 0x41 (length, CRC-32), the device's reads answered from the image, SEL pressed on the panel when it asks
    double x0xRestore(FM1Processor &p, const Bytes &image)
    {
        c.take();
        const double t0 = nowMs();
        Bytes q = le32((uint32_t)image.size()), cr = le32(crc32Of(image.data(), image.size()));
        q.insert(q.end(), cr.begin(), cr.end());
        c.out->sendMessageNow(msgOf(updFrame(0x41, q)));
        double selAt = 0;
        while (nowMs() - t0 < 30000) {
            step();
            if (selAt > 0 && nowMs() >= selAt) {
                p.panelButton(EMU_B_SEL, true);  // (a person's press on the panel, after the question is up)
                p.panelButton(EMU_B_SEL, false);
                selAt = 0;
            }
            for (auto &f : c.take()) {
                const Bytes u = f.size() >= 3 ? updUnpack(f) : Bytes();
                if (u.size() != 15 || u[0] != 0 || u[1] != 0x59 || u[2] != 0x30)
                    continue;
                const uint8_t flg = u[6];
                const uint32_t addr = rd32le(u, 7), len = (uint32_t)u[11] | (uint32_t)u[12] << 8 | (uint32_t)u[13] << 16;
                const uint32_t top = addr & 0xFF000000u;
                Bytes data;
                if (top == 0xC0000000u || top == 0xC1000000u || top == 0xCF000000u)
                    data = {0x73, 0x75, 0x63, 0x63, 0x65, 0x73, 0x73, 0};
                else if (len > 512 || addr >= image.size())
                    return why = "restore: a read outside the image", -1;
                else {
                    data.assign(image.begin() + (std::ptrdiff_t)addr,
                                image.begin() + (std::ptrdiff_t)std::min<size_t>(image.size(), (size_t)addr + len));
                    data.resize(len, 0);
                }
                const uint32_t n = (uint32_t)data.size();
                Bytes m = {0x00, 0x59, 0x30, (uint8_t)((n + 8) & 0xFF), (uint8_t)((n + 8) >> 8), 0, flg};
                const Bytes a = le32(addr);
                m.insert(m.end(), a.begin(), a.end());
                m.push_back(n & 0xFF);
                m.push_back(n >> 8 & 0xFF);
                m.push_back(0);
                m.insert(m.end(), data.begin(), data.end());
                uint32_t s2 = 0;
                for (size_t i = 6; i < m.size(); i++)
                    s2 += m[i];
                m.push_back((uint8_t)~s2);
                c.out->sendMessageNow(msgOf(updWire(m)));
                if (addr == 0xC1000000u)
                    selAt = nowMs() + 100;
                else if (top == 0xC0000000u)
                    return nowMs() - t0;
                else if (top == 0xCF000000u)
                    return why = "restore: the device's code " + std::to_string(addr & 0xFFFF), -1;
            }
        }
        return why = "restore: no result", -1;
    }
};

// a firmware's manager: how a preset is made to change (the store its editor sends), and which object holds it
struct MProfile {
    const char *id;
    const char *info;                            // INFO's version starts with this
    enum { DX7, STORE19, LIB80, SLOOP, X0X } kind;
    uint8_t recPages = 0;                        // DUELL 1.0: the patch records' LIST pages (Manager::recPages)
};
static const MProfile kManagers[] = {
    {"choralroot", "ChoralRoot ", MProfile::DX7}, {"felucca", "FELUCCA v", MProfile::STORE19},
    {"jiant", "JIANT ", MProfile::STORE19},       {"ghoulbox", "FELUCCA ", MProfile::STORE19},
    {"rainbow", "FELUCCA ", MProfile::STORE19},   {"duell", "FELUCCA DUELL", MProfile::STORE19, 4},
    {"melodee", "MELODEE ", MProfile::LIB80},     {"sloop", "FELUCCA SLOOP", MProfile::SLOOP},
    {"sloopdx", "FELUCCA sloopDX", MProfile::SLOOP}, {"orbit", "FELUCCA ORBIT", MProfile::SLOOP},
    {"nomansky", "FELUCCA NMS", MProfile::SLOOP}, {"x0x", "X0X ", MProfile::X0X},
};

struct MRow {
    std::string id, port;
    double info[2] = {-1, -1}, list[2] = {-1, -1}, get[2] = {-1, -1}, put[2] = {-1, -1}, worst[2] = {0, 0};
    uint32_t getBytes = 0, putId = 0;
    bool ok[2] = {false, false};
};

static size_t findText(const Bytes &b, const char *t)
{
    const size_t n = std::strlen(t);
    for (size_t i = 0; i + n <= b.size(); i++)
        if (!std::memcmp(&b[i], t, n))
            return i;
    return std::string::npos;
}

static void runManager(const MProfile &pr, MRow &row)
{
    FM1Processor p;
    if (!p.switchCore(pr.id)) {
        check(false, std::string(pr.id) + " loads");
        return;
    }
    Host h(p);
    h.play(3000);                                // (booted: the splash showing or past)
    p.setEditorPortEnabled(true);
    const auto st = p.editorPortStatus();
    row.port = st.name.toStdString();
    Client c;
    if (!(st.open && waitListed(st.name, true) && c.connect(st.name))) {
        check(false, std::string(pr.id) + ": a client finds \"" + row.port + "\"");
        return;
    }
    Manager m{c, h, true, {}};
    if (pr.kind == MProfile::SLOOP)
        m.bk = 34, m.listHdr = 1;
    m.recPages = pr.recPages;
    m.settle(100);
    // the editors' connect: INFO, 500 ms for the answer, three tries (Felucca's, JIANT's, Melodee's editor.html; SLOOP's
    // two of 1000 ms), and a person pressing Connect again until it answers (20 s at most). A firmware answers only from
    // its main loop: not under its boot splash (JIANT's ~2 s, 600 frames), which on a busy machine, the host's blocks
    // late, takes longer in wall time
    int tries = 0;
    double hs = -1;
    std::string hv;
    for (const double t0 = nowMs(); hs < 0 && nowMs() - t0 < 20000;) {
        tries++;
        hs = m.info(hv, 500);
    }
    check(hs >= 0, std::string(pr.id) + ": the editor's connect: INFO answered at try " + std::to_string(tries) + " (\"" +
                       hv + "\")");
    if (hs < 0) {
        const auto cs = p.getCoreInfo();
        const auto ps = p.editorPortStatus();
        std::printf("    (core %s, halted %d code 0x%X; device %u ms; port in %u out %u dropped %u, idle %llu ms)\n",
                    cs.info.id.c_str(), (int)cs.halted, (unsigned)cs.haltCode,
                    p.deviceForTest() ? p.deviceForTest()->ms() : 0u, ps.in, ps.out, ps.dropped,
                    (unsigned long long)ps.idleMs);
        return;
    }
    m.settle(100);                               // (any late answer to an earlier try: gone before the next request)

    // the preset the manager will change: made once, the host playing (the editor's own store / a DX7 bulk dump)
    uint32_t target = 0;
    size_t letter = 0;                           // the byte of the target object that is a preset name's letter
    Bytes x0xA, x0xB;                            // X0X: the fresh project's backup, and one after a panel change
    std::vector<MObj> was, man;
    Bytes r, obj;
    bool prep = true;
    if (pr.kind == MProfile::DX7) {
        c.out->sendMessageNow(msgOf(dx7Bulk()));
        m.settle(1500);
        const MObj *o = m.list(man) >= 0 ? findObj(man, 8) : nullptr;
        prep = o && o->size >= 32 * 112 && m.get(*o, obj) >= 0;
        target = 8;
        letter = prep ? o->size - 32 * 112 + 104 : 0;
        prep = prep && (obj[letter] & 127u) == 'S';
    } else if (pr.kind == MProfile::STORE19 || pr.kind == MProfile::LIB80 || pr.kind == MProfile::SLOOP) {
        const bool lib = pr.kind == MProfile::LIB80;
        const Bytes args = lib ? Bytes{3, 0, 0, 0, 'S', 'Y', 'S', 'E', 'X'} : Bytes{0, 'S', 'Y', 'S', 'E', 'X', 0};
        const size_t rcAt = lib ? 3 : 1;
        prep = m.list(was) >= 0;
        const uint8_t cmd = lib ? 80 : 19;
        prep = prep && m.request(fl(cmd, args), flReply(cmd), r, 5000) >= 0 && flArgs(r).size() > rcAt &&
               flArgs(r)[rcAt] == 0;
        prep = prep && m.list(man) >= 0;
        const MObj *o = nullptr;
        if (prep && pr.kind == MProfile::SLOOP) {
            o = findObj(man, 6);
        } else if (prep) {
            for (const MObj &n : man) {
                const MObj *w = findObj(was, n.id);
                if (n.size && (!w || w->size != n.size || w->crc != n.crc)) {
                    o = &n;
                    break;
                }
            }
        }
        prep = o && m.get(*o, obj) >= 0;
        if (prep) {
            target = o->id;
            letter = findText(obj, "SYSEX");
            prep = letter != std::string::npos;
        }
    } else {                                     // X0X: a backup, a step written on the panel and saved, a backup
        prep = m.x0xBackup(x0xA) >= 0;
        p.panelKey(2, true);
        p.panelKey(2, false);
        m.settle(400);
        p.deviceForTest()->flash_sync();         // (AUTOSAVE's write; between blocks, on the thread that runs them)
        m.settle(100);
        prep = prep && m.x0xBackup(x0xB) >= 0 && x0xB != x0xA;
    }
    check(prep, std::string(pr.id) + ": a preset to change (" + m.why + ")");
    if (!prep) {
        const auto cs = p.getCoreInfo();
        const auto ps = p.editorPortStatus();
        std::printf("    (core %s, halted %d code 0x%X; port in %u out %u dropped %u, idle %llu ms)\n",
                    cs.info.id.c_str(), (int)cs.halted, (unsigned)cs.haltCode, ps.in, ps.out, ps.dropped,
                    (unsigned long long)ps.idleMs);
        return;
    }
    row.putId = target;

    for (int mode = 0; mode < 2; mode++) {      // 0: the host playing, 1: idle (no processBlock at all)
        m.playing = mode == 0;
        m.worst = 0;
        m.why.clear();
        if (!m.playing)
            m.settle(200);                       // (no block for 50 ms: the idle clock has the core)
        const std::string tag = std::string(pr.id) + (m.playing ? " (playing)" : " (idle)");
        std::string v;
        row.info[mode] = m.info(v);
        const bool infoOk = row.info[mode] >= 0 && row.info[mode] < 300 && v.rfind(pr.info, 0) == 0;
        check(infoOk, tag + ": INFO \"" + v + "\" in " + std::to_string((int)row.info[mode]) + " ms (< 300)");
        bool ok = infoOk;
        if (pr.kind == MProfile::X0X) {
            // LIST: the page's handshake, the M-UPGRADE identity (34 bytes decoded, FM-1_9..)
            auto idMatch = [](const Bytes &f) {
                const Bytes d = f.size() >= 4 && f[1] == 0x00 && f[2] == 0x32 ? updUnpack(f) : Bytes();
                return d.size() == 34 && d[0] == 0 && d[1] == 0x59 && d[2] == 0x11;
            };
            row.list[mode] = m.request(sx({0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7}), idMatch, r, 1000);
            check(row.list[mode] >= 0, tag + ": the page's identity query answered in " +
                                           std::to_string((int)row.list[mode]) + " ms");
            Bytes cur, back;
            row.get[mode] = m.x0xBackup(cur);
            row.getBytes = (uint32_t)cur.size();
            const Bytes &want = mode == 0 ? x0xB : x0xA;   // (playing: B is current, A restored; idle: the reverse)
            const Bytes &to = mode == 0 ? x0xA : x0xB;
            check(row.get[mode] >= 0 && cur == want, tag + ": backup (0x42) " + std::to_string(cur.size()) + " bytes in " +
                                                         std::to_string((int)row.get[mode]) + " ms " + m.why);
            row.put[mode] = m.x0xRestore(p, to);
            const bool got = row.put[mode] >= 0 && m.x0xBackup(back) >= 0;
            check(got && back == to, tag + ": restore (0x41, SEL on the panel) of the other project in " +
                                         std::to_string((int)row.put[mode]) + " ms, read back by a backup " + m.why);
            ok = ok && row.list[mode] >= 0 && row.get[mode] >= 0 && cur == want && got && back == to;
        } else {
            row.list[mode] = m.list(man);
            check(row.list[mode] >= 0, tag + ": LIST " + std::to_string(man.size()) + " objects in " +
                                           std::to_string((int)row.list[mode]) + " ms " + m.why);
            const MObj *o = row.list[mode] >= 0 ? findObj(man, target) : nullptr;
            Bytes before, back;
            row.get[mode] = o ? m.get(*o, before) : -1;
            row.getBytes = o ? o->size : 0;
            check(row.get[mode] >= 0, tag + ": GET object " + std::to_string(target) + " (" + std::to_string(row.getBytes) +
                                          " bytes) in " + std::to_string((int)row.get[mode]) + " ms " + m.why);
            Bytes after = before;
            if (letter < after.size())
                after[letter] = (uint8_t)((after[letter] & 0x80u) | (m.playing ? 'Z' : 'Y'));
            row.put[mode] = row.get[mode] >= 0 ? m.put(target, after) : -1;
            std::vector<MObj> m2;
            const bool got = row.put[mode] >= 0 && m.list(m2) >= 0 && findObj(m2, target) &&
                             m.get(*findObj(m2, target), back) >= 0;
            check(got && back == after && after != before,
                  tag + ": PUT object " + std::to_string(target) + " with a preset renamed (\"" +
                      std::string(1, (char)(after[letter] & 127u)) + "YSEX\") in " + std::to_string((int)row.put[mode]) +
                      " ms, read back " + m.why);
            ok = ok && row.list[mode] >= 0 && row.get[mode] >= 0 && got && back == after;
        }
        row.worst[mode] = m.worst;
        row.ok[mode] = ok;
        if (!m.playing)
            check(p.editorPortStatus().idling, tag + ": answered by the idle clock (no block ran)");
    }
    p.setEditorPortEnabled(false);
    waitListed(st.name, false);
}

static int runManagers(const char *only)
{
    std::vector<MRow> rows;
    for (const MProfile &pr : kManagers) {
        if (only && std::strcmp(only, pr.id) != 0)
            continue;
        if (!built_cores().count(pr.id)) {
            std::printf("[%s: not in this build]\n", pr.id);
            continue;
        }
        std::printf("[CoreMIDI managers: %s]\n", pr.id);
        rows.push_back({});
        rows.back().id = pr.id;
        runManager(pr, rows.back());
    }
    auto ms = [](double t) { return t < 0 ? std::string("FAIL") : std::to_string((int)(t + 0.5)); };
    std::printf("\n%-11s %-28s | %-24s | %-24s | %-6s\n", "firmware", "port", "playing: INFO LIST GET PUT",
                "idle: INFO LIST GET PUT", "GET B");
    for (const MRow &r : rows) {
        std::string pl, id;
        for (int k = 0; k < 2; k++) {
            std::string &s = k ? id : pl;
            s = ms(r.info[k]) + " " + ms(r.list[k]) + " " + ms(r.get[k]) + " " + ms(r.put[k]) + (r.ok[k] ? "" : " !");
        }
        std::printf("%-11s %-28s | %-24s | %-24s | %u\n", r.id.c_str(), r.port.c_str(), pl.c_str(), id.c_str(),
                    r.getBytes);
    }
    std::printf("(ms; GET: the object's chunks in all; PUT: begin, data, commit; X0X: LIST is its identity query, GET "
                "its 0x42 backup, PUT its 0x41 restore with SEL pressed)\n");
    if (rows.empty()) {
        std::printf("SKIP: none of the managers' firmwares is in this build\n");
        return 77;
    }
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    const bool managers = argc > 1 && std::strcmp(argv[1], "managers") == 0;
    const bool coremidi = managers || (argc > 1 && std::strcmp(argv[1], "coremidi") == 0);
    const juce::File scratch = juce::File(FM1_SCRATCH_DIR).getChildFile(managers ? "managers" : coremidi ? "coremidi" : "fake");
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    if (coremidi)
        unsetenv("FM1EMU_EDITOR_FAKE");
    else
        setenv("FM1EMU_EDITOR_FAKE", "1", 1);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("editor_port_test (%s): FM1EMU_HOME=%s\n", coremidi ? "CoreMIDI" : "fake port", scratch.getFullPathName().toRawUTF8());
    const int rc = managers ? runManagers(argc > 2 ? argv[2] : nullptr) : coremidi ? runCoreMidi() : runFake();
    if (rc == 77)
        return 77;
    std::printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
