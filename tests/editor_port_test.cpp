// SPDX-License-Identifier: GPL-3.0-only
// "Show to editors" (plugin/EditorPort.h), headless.
//   editor_port_test            (FM1EMU_EDITOR_FAKE=1: no CoreMIDI) the port names and the USB-name table; on the
//                               echo core (tests/echo_core.c): a request through the port is answered through the port
//                               while the host plays, and none of it is in the host's MIDI out; a 300-byte request
//                               whole; the M-UPGRADE keys dropped; the core's channel messages still reach the host;
//                               with the port off, the host's SysEx is answered to the host as before; the idle clock
//                               answers while processBlock is not called, and stops when blocks come again
//   editor_port_test coremidi   a CoreMIDI client (JUCE's devices, in this process) finds the virtual port by name and
//                               does an INFO round trip against FM-106 (its Librarian's dump request, F0 41 34 ..) and
//                               against the echo core, idle and while playing; two instances get distinct names; no
//                               SysEx in the host's MIDI out. Exit 77 (skipped) without CoreMIDI or without FM-106.
#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
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
    explicit Host(FM1Processor &proc) : p(proc)
    {
        p.setRateAndBufferSizeDetails(sr, bs);
        p.prepareToPlay(sr, bs);
    }
    void block(juce::MidiBuffer &m)
    {
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
                 {"sloop", "Felucca"},              {"melodee", "Melodee"},              {"jiant", "JIANT FM"},
                 {"duell", "DUELL"},                {"x0x", "X0X FM-1"},                 {"fm106", "FM-106"},
                 {"coralflower", "CoralFlower FM-1"}, {"fart", "FoMni FM-1"},           {"unknown-fw", "FM-1"}};
    bool all = true;
    for (auto &t : table)
        all = all && EditorPort::usbProductName(t.id) == t.usb;
    check(all, "USB names by core / Hub id (usb.c STR2 / USB_STRS), unknown: \"FM-1\"");
    check(EditorPort::managerUrl("felucca").startsWith("https://hugelton.github.io/Felucca/"), "Felucca's editor URL");
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
    const double dt = nowMs() - t0;
    const uint32_t tk1 = got.size() == 1 ? ticksOf(got[0]) : 0;
    check(got.size() == 1 && tk1 > 0 && dt < 50, "answered with processBlock idle, in " + std::to_string((int)dt) + " ms");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ep.receiveForTest(msgOf(ask));
    got = waitSent(ep, 1, 500);
    const uint32_t tk2 = got.size() == 1 ? ticksOf(got[0]) : 0;
    check(tk2 > tk1 && tk2 - tk1 >= 150 && tk2 - tk1 <= 300,
          "the idle clock keeps real time: " + std::to_string(tk2 - tk1) + " device ms in ~200 ms");

    h.play(60);                                  // (the host plays again)
    const uint64_t idle0 = ep.idleMs();
    h.play(150);
    check(!ep.idling() && ep.idleMs() == idle0, "blocks again: the idle clock stopped (" +
                                                    std::to_string(ep.idleMs() - idle0) + " idle ms while playing)");
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
    if (!built_cores().count("fm106")) {
        std::printf("SKIP: this build has no FM-106 core (a public build)\n");
        return 77;
    }
    FM1Processor a;
    if (!a.switchCore("fm106")) {
        std::printf("FAIL: FM-106 does not load\n");
        return 1;
    }
    Host ha(a);
    ha.blocks(200);                              // (booted, its librarian up)
    a.setEditorPortEnabled(true);
    const auto sa = a.editorPortStatus();
    if (!sa.open) {
        std::printf("SKIP: no virtual CoreMIDI port could be made here (%s)\n", sa.error.toRawUTF8());
        return 77;
    }
    std::printf("[CoreMIDI: FM-106]\n");
    check(sa.name == "FM-106 (FM1VST)", "the port: \"" + sa.name.toStdString() + "\"");
    check(waitListed(sa.name, true), "CoreMIDI lists a source and a destination by that name");

    FM1Processor b;
    b.switchCore("fm106");
    Host hb(b);
    hb.blocks(4);
    b.setEditorPortEnabled(true);
    const auto sb = b.editorPortStatus();
    check(sb.open && sb.name == "FM-106 (FM1VST 2)" && sb.name != sa.name,
          "a second instance: \"" + sb.name.toStdString() + "\" (the first name is taken)");
    check(waitListed(sa.name, true) && waitListed(sb.name, true), "both listed");

    Client c;
    check(c.connect(sa.name), "a client finds the port by name and opens both directions");
    if (!c.in || !c.out)
        return 1;
    const Bytes dump = sx({0xF0, 0x41, 0x34, 0x00, 0x00, 0xF7});   // (the Librarian: dump request, patch 0)
    Bytes reply;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    double t = c.roundTrip(dump, sx({0xF0, 0x41, 0x30}), nullptr, &reply);
    check(t >= 0 && t < 300, "FM-106 answers with the host idle (the idle clock): " + std::to_string((int)t) + " ms, " +
                                 hex(reply, 12));
    ha.sysexToHost = 0;
    t = c.roundTrip(dump, sx({0xF0, 0x41, 0x30}), &ha, &reply);
    check(t >= 0 && t < 300, "and while the host plays: " + std::to_string((int)t) + " ms");
    ha.play(50);
    check(ha.sysexToHost == 0, "no SysEx in the host's MIDI out (" + std::to_string(ha.sysexToHost) + ")");

    // the second instance answers its own port
    Client c2;
    if (c2.connect(sb.name)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        t = c2.roundTrip(dump, sx({0xF0, 0x41, 0x30}), nullptr);
        check(t >= 0 && t < 300, "the second instance answers on its own port: " + std::to_string((int)t) + " ms");
    } else {
        check(false, "a client opens the second port");
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

int main(int argc, char **argv)
{
    const bool coremidi = argc > 1 && std::strcmp(argv[1], "coremidi") == 0;
    const juce::File scratch = juce::File(FM1_SCRATCH_DIR).getChildFile(coremidi ? "coremidi" : "fake");
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    if (coremidi)
        unsetenv("FM1EMU_EDITOR_FAKE");
    else
        setenv("FM1EMU_EDITOR_FAKE", "1", 1);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("editor_port_test (%s): FM1EMU_HOME=%s\n", coremidi ? "CoreMIDI" : "fake port", scratch.getFullPathName().toRawUTF8());
    const int rc = coremidi ? runCoreMidi() : runFake();
    if (rc == 77)
        return 77;
    std::printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
