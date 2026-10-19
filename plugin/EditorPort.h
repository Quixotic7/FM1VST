// SPDX-License-Identifier: GPL-3.0-only
// "Show to editors" (research/managers-2026-10-19/MANAGERS.md): a virtual MIDI port pair named like the loaded
// firmware's real USB port, so a firmware's own preset manager (a web page in Chrome, Web MIDI with SysEx) finds this
// FM1VST as if it were an FM-1 and backs up / restores presets through the firmware's own SysEx service.
//
// THE NAME  the firmware's USB product string (usbProductName: "Felucca", "ChoralRoot FM-1", "FM-106" ..) plus
//           " (FM1VST)". The managers match by substring or regex (/felucca/i, includes("ChoralRoot FM-1"), ..), so
//           a suffix keeps every match; a prefix would break ChoralRoot's. One source and one destination with the
//           SAME name (ChoralRoot's manager, SLOOP and X0X pair them by name). JUCE gives a virtual port a unique ID
//           from the bundle ID and the name, so a second instance (AU + VST3, the Standalone beside a plugin) cannot
//           take the same name: the next free one is "<USB name> (FM1VST 2)", " 3" .. (nameFor).
// THE DATA  port in (a CoreMIDI thread) -> USB-MIDI packets (CIN 4-7 SysEx, channel and system messages, as the
//           processor packs the DAW's MIDI: toUsbPackets) -> a single-producer single-consumer ring of whole messages
//           -> the processor's core input (takeMessage, on whichever thread runs the core, devLock_ held).
//           The core's SysEx (while the port is open: all of it) -> sysexFromCore (devLock_ held, lock-free, no
//           allocation) -> a byte FIFO -> the service thread -> the port's source. Nothing of the port's traffic
//           reaches the DAW's MIDI buffers, and the DAW's MIDI never reaches the port.
//           Dropped on the way in: M-UPGRADE's UBOOT (F0 22 24 35 7D F7) and OTA start (F0 22 24 35 7F F7): an
//           installer page pointed at the port would halt the firmware.
// THE IDLE CLOCK  the core runs only inside processBlock; a host that stops calling it (a stopped transport in some
//           hosts, a disabled track, the Standalone without audio) would leave a manager's INFO unanswered. When no
//           block has started for kIdleAfterMs and the port is open, the service thread runs the core's clock in real
//           time through the processor's tick function (which tries devLock_, the lock processBlock tries and the
//           message thread takes: the two never run the core at once; a tick that finds it taken is skipped), with its
//           audio discarded, in steps of at most kIdleMaxStep device ms. The first processBlock stops it. Not for
//           fm1emu (no MIDI out to answer with).
// THREADS  message thread: open / close / name / status. CoreMIDI's thread: the port's input. The clock thread
//           (audio, or the service thread when idle; devLock_ held): takeMessage, sysexFromCore. The service thread:
//           the port's output and the idle clock; it exists while the port is open.
// FAKE  FM1EMU_EDITOR_FAKE=1 (tests): no CoreMIDI port is made; receiveForTest stands in for the port's input and the
//           replies are kept for takeSentForTest. Everything else (the rings, the service thread, the idle clock) runs.
// macOS only (JUCE's virtual ports: CoreMIDI).
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// one MIDI message -> USB-MIDI event packets (cable 0): CIN | b0 << 8 | b1 << 16 | b2 << 24 (tools/emu/emu_midi.c)
template <typename Put>
inline void toUsbPackets(const uint8_t *d, int n, Put &&put)
{
    auto pkt = [](uint32_t cin, uint32_t a, uint32_t b, uint32_t c) { return (cin & 15u) | a << 8 | b << 16 | c << 24; };
    if (n <= 0)
        return;
    const uint8_t st = d[0];
    if (st == 0xF0) {                            // SysEx: 3-byte packets, the last one CIN 5 / 6 / 7
        int i = 0;
        while (n - i > 3) {
            put(pkt(4, d[i], d[i + 1], d[i + 2]));
            i += 3;
        }
        const int r = n - i;
        put(pkt(r == 1 ? 5u : r == 2 ? 6u : 7u, d[i], r > 1 ? d[i + 1] : 0u, r > 2 ? d[i + 2] : 0u));
    } else if (st >= 0xF8) {
        put(pkt(0xF, st, 0, 0));
    } else if (st >= 0xF0) {                     // system common
        if (st == 0xF1 || st == 0xF3)
            put(pkt(2, st, n > 1 ? d[1] : 0u, 0));
        else if (st == 0xF2)
            put(pkt(3, st, n > 1 ? d[1] : 0u, n > 2 ? d[2] : 0u));
        else if (st == 0xF6)
            put(pkt(5, st, 0, 0));
    } else if (st >= 0x80) {
        put(pkt(st >> 4, st, n > 1 ? d[1] : 0u, n > 2 ? d[2] : 0u));
    }
}

class EditorPort : private juce::MidiInputCallback, private juce::Thread {
public:
    // runs the core's clock for ms device milliseconds with its audio discarded (the processor's; takes devLock_)
    using IdleTick = std::function<void(uint32_t ms)>;
    explicit EditorPort(IdleTick tick);
    ~EditorPort() override;

    // ---- message thread
    // the port pair for this USB name: "<usb> (FM1VST)", else the next free number; false (and error()) when none
    bool open(const juce::String &usbName);
    void close();
    bool isOpen() const { return open_.load(std::memory_order_acquire); }
    juce::String name() const;                   // the port's name as the editors see it ("" closed)
    juce::String usbName() const;                // what it was opened for
    juce::String error() const;                  // why the last open failed ("" none)
    static juce::String nameFor(const juce::String &usbName, int n);   // n 1: "X (FM1VST)", n > 1: "X (FM1VST n)"
    static bool available();                     // the platform has virtual ports (macOS)
    static bool fake();                          // FM1EMU_EDITOR_FAKE=1

    // the loaded firmware's USB product string (the cores' usb.c STR2 / USB_STRS, MANAGERS.md section 3) by FM1VST
    // core id, or by Hub id for an emulated firmware; "FM-1" when unknown (fm1-editor.com, the FM-106 Librarian and the
    // installers' lists match it)
    static juce::String usbProductName(const juce::String &coreOrHubId);
    // the firmware's manager page known to work over Web MIDI (MANAGERS.md section 2), "" none. catalogEditor: the Hub
    // catalog's `editor` field for the firmware, preferred when given
    static juce::String managerUrl(const juce::String &coreOrHubId, const juce::String &catalogEditor = {});

    // ---- the clock thread (devLock_ held)
    // the next whole message from the port, when it fits in `room` packets: put(pkt) for each, true. false: none, or
    // the next one does not fit yet (it waits; nothing is ever split)
    template <typename Put>
    bool takeMessage(uint32_t room, Put &&put)
    {
        const uint32_t r = inR_.load(std::memory_order_relaxed), w = inW_.load(std::memory_order_acquire);
        if (r == w)
            return false;
        const uint32_t n = in_[r % kIn];
        if (n > room)
            return false;
        for (uint32_t i = 0; i < n; i++)
            put(in_[(r + 1 + i) % kIn]);
        inR_.store(r + 1 + n, std::memory_order_release);
        msgsIn_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    // one complete SysEx from the core (F0 .. F7) for the port; dropped whole when the FIFO is full
    void sysexFromCore(const uint8_t *d, int n);
    // processBlock started (any thread): the idle clock stops
    void blockRan() { lastBlockMs_.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed); }

    // ---- status (any thread)
    uint32_t messagesIn() const { return msgsIn_.load(); }         // taken by the core
    uint32_t messagesOut() const { return msgsOut_.load(); }       // sent to the port
    uint32_t dropped() const { return dropped_.load(); }           // refused (a full ring, UBOOT / OTA start)
    uint64_t idleMs() const { return idleMs_.load(); }             // device ms the idle clock has run
    bool idling() const { return idling_.load(); }

    // ---- tests
    void receiveForTest(const juce::MidiMessage &m) { handleIncomingMidiMessage(nullptr, m); }
    std::vector<std::vector<uint8_t>> takeSentForTest();

    static constexpr double kIdleAfterMs = 50.0;     // no block for this long: the idle clock runs
    static constexpr uint32_t kIdleMaxStep = 5;      // device ms per idle tick at most (a block that comes meanwhile waits
                                                     // at most this long for devLock_: an emulated core runs ~3x real time)
    static constexpr uint32_t kMaxPackets = 4000;    // a message's packets at most (12 KB of SysEx)
    // the M-UPGRADE keys dropped on the way in (MANAGERS.md section 3)
    static bool isUpdateKey(const uint8_t *d, int n);

private:
    void handleIncomingMidiMessage(juce::MidiInput *, const juce::MidiMessage &m) override;
    void run() override;
    void sendPending();
    void idleClock();

    IdleTick tick_;
    mutable std::mutex m_;                       // name_, usb_, error_ (message thread vs status readers)
    juce::String name_, usb_, error_;
    std::unique_ptr<juce::MidiInput> in_dev_;
    std::unique_ptr<juce::MidiOutput> out_dev_;
    std::atomic<bool> open_{false};
    bool fake_ = false;                          // (fake() when opened)

    // port -> core: [count][count packets] per message, single producer (the port's thread), single consumer (the
    // clock thread)
    static constexpr uint32_t kIn = 16384;
    std::array<uint32_t, kIn> in_{};
    std::atomic<uint32_t> inW_{0}, inR_{0};
    // core -> port: whole SysEx messages, bytes
    static constexpr int kOut = 65536;
    juce::AbstractFifo outFifo_{kOut};
    std::array<uint8_t, kOut> out_{};
    std::vector<uint8_t> sending_;               // (the service thread's reassembly)
    std::mutex sentLock_;
    std::vector<std::vector<uint8_t>> sent_;     // (fake: what would have been sent)

    std::atomic<double> lastBlockMs_{0.0};
    std::atomic<bool> idling_{false};
    double idleBase_ = 0;                        // (service thread) when the idle clock started
    uint64_t idleRan_ = 0;                       // device ms it has run since
    std::atomic<uint64_t> idleMs_{0};
    std::atomic<uint32_t> msgsIn_{0}, msgsOut_{0}, dropped_{0};
};
