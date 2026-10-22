// SPDX-License-Identifier: GPL-3.0-only
// "Show to editors": the virtual port pair, its rings, the service thread and the idle clock (EditorPort.h).
#include "EditorPort.h"

#include <cstdlib>
#include <cstring>

EditorPort::EditorPort(IdleTick tick) : juce::Thread("FM1VST editor port"), tick_(std::move(tick))
{
    sending_.reserve(kOut);
    lastBlockMs_.store(juce::Time::getMillisecondCounterHiRes());
}

EditorPort::~EditorPort() { close(); }

bool EditorPort::available()
{
#if JUCE_MAC
    return true;
#else
    return fake();
#endif
}

bool EditorPort::fake()
{
    const char *e = std::getenv("FM1EMU_EDITOR_FAKE");
    return e && e[0] == '1';
}

juce::String EditorPort::nameFor(const juce::String &usbName, int n)
{
    return usbName + (n <= 1 ? juce::String(" (FM1VST)") : " (FM1VST " + juce::String(n) + ")");
}

juce::String EditorPort::name() const
{
    std::lock_guard<std::mutex> l(m_);
    return name_;
}
juce::String EditorPort::usbName() const
{
    std::lock_guard<std::mutex> l(m_);
    return usb_;
}
juce::String EditorPort::error() const
{
    std::lock_guard<std::mutex> l(m_);
    return error_;
}

bool EditorPort::open(const juce::String &usbName)
{
    close();
    juce::String name, err;
    fake_ = fake();
    if (fake_) {
        name = nameFor(usbName, 1);
    } else if (!available()) {
        err = "virtual MIDI ports need macOS";
    } else {
        // the first free name: JUCE's unique ID is the bundle ID + the name, so a taken name gives nullptr
        for (int n = 1; n <= 16 && name.isEmpty(); n++) {
            const juce::String cand = nameFor(usbName, n);
            auto out = juce::MidiOutput::createNewDevice(cand);       // a source: the editors' input
            auto in = out ? juce::MidiInput::createNewDevice(cand, this) : nullptr;   // a destination: their output
            if (out && in) {
                out_dev_ = std::move(out);
                in_dev_ = std::move(in);
                name = cand;
            }
        }
        if (name.isEmpty())
            err = "no virtual MIDI port could be made (" + nameFor(usbName, 1) + " .. 16 taken, or CoreMIDI unavailable)";
    }
    {
        std::lock_guard<std::mutex> l(m_);
        name_ = name, usb_ = usbName, error_ = err;
    }
    if (name.isEmpty())
        return false;
    lastBlockMs_.store(juce::Time::getMillisecondCounterHiRes());   // (a block may come: wait for it first)
    idling_.store(false);
    open_.store(true, std::memory_order_release);
    if (in_dev_)
        in_dev_->start();
    startThread(juce::Thread::Priority::high);
    return true;
}

void EditorPort::close()
{
    if (in_dev_)
        in_dev_->stop();                         // (no callback after this)
    open_.store(false, std::memory_order_release);
    stopThread(-1);                              // (prompt: the idle clock only tries devLock_, a step is a few ms)
    in_dev_.reset();
    out_dev_.reset();
    idling_.store(false);
    std::lock_guard<std::mutex> l(m_);
    name_.clear(), usb_.clear(), error_.clear();
}

bool EditorPort::isUpdateKey(const uint8_t *d, int n)
{
    return n == 6 && d[0] == 0xF0 && d[1] == 0x22 && d[2] == 0x24 && d[3] == 0x35 && (d[4] == 0x7D || d[4] == 0x7F) &&
           d[5] == 0xF7;
}

// the port's input (CoreMIDI's thread; the test's in fake mode): one whole message into the ring, or nothing
void EditorPort::handleIncomingMidiMessage(juce::MidiInput *, const juce::MidiMessage &m)
{
    const uint8_t *d = m.getRawData();
    const int n = m.getRawDataSize();
    if (n <= 0 || !open_.load(std::memory_order_acquire))
        return;
    if (isUpdateKey(d, n) || (d[0] == 0xF0 && d[n - 1] != 0xF7)) {   // (and a SysEx JUCE gave up on: no F7)
        dropped_.fetch_add(1);
        return;
    }
    uint32_t pk[kMaxPackets];
    uint32_t count = 0;
    bool over = false;
    toUsbPackets(d, n, [&](uint32_t p) {
        if (count < kMaxPackets)
            pk[count++] = p;
        else
            over = true;
    });
    const uint32_t w = inW_.load(std::memory_order_relaxed), r = inR_.load(std::memory_order_acquire);
    if (over || count == 0 || kIn - (w - r) < count + 1) {
        dropped_.fetch_add(1);
        return;
    }
    in_[w % kIn] = count;
    for (uint32_t i = 0; i < count; i++)
        in_[(w + 1 + i) % kIn] = pk[i];
    inW_.store(w + 1 + count, std::memory_order_release);
}

void EditorPort::sysexFromCore(const uint8_t *d, int n)
{
    if (n <= 0)
        return;
    if (outFifo_.getFreeSpace() < n) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto w = outFifo_.write(n);
    if (w.blockSize1 > 0)
        std::memcpy(out_.data() + w.startIndex1, d, (size_t)w.blockSize1);
    if (w.blockSize2 > 0)
        std::memcpy(out_.data() + w.startIndex2, d + w.blockSize1, (size_t)w.blockSize2);
}

void EditorPort::sendPending()
{
    const int avail = outFifo_.getNumReady();
    if (avail <= 0)
        return;
    const auto rd = outFifo_.read(avail);
    auto take = [this](int start, int size) {
        for (int i = 0; i < size; i++) {
            const uint8_t b = out_[(size_t)(start + i)];
            if (b == 0xF0)
                sending_.clear();
            sending_.push_back(b);
            if (b != 0xF7)
                continue;
            if (sending_.size() >= 2 && sending_.front() == 0xF0) {
                if (fake_) {
                    std::lock_guard<std::mutex> l(sentLock_);
                    sent_.push_back(sending_);
                } else if (out_dev_) {
                    out_dev_->sendMessageNow(juce::MidiMessage::createSysExMessage(sending_.data() + 1, (int)sending_.size() - 2));
                }
                msgsOut_.fetch_add(1, std::memory_order_relaxed);
            }
            sending_.clear();
        }
    };
    take(rd.startIndex1, rd.blockSize1);
    take(rd.startIndex2, rd.blockSize2);
}

std::vector<std::vector<uint8_t>> EditorPort::takeSentForTest()
{
    std::lock_guard<std::mutex> l(sentLock_);
    std::vector<std::vector<uint8_t>> out;
    out.swap(sent_);
    return out;
}

// no block for idleAfterMs: the core's clock by the wall clock, in steps of at most kIdleMaxStep device ms, never ahead
// of it: the device ms run since the idle clock started never exceed the wall ms since then, and a backlog (this thread
// woke late) is run only up to kIdleCatchUp ms, the rest dropped
void EditorPort::idleClock()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double after = idleAfterMs();
    if (now - lastBlockMs_.load(std::memory_order_relaxed) < after) {
        idling_.store(false, std::memory_order_relaxed);
        return;
    }
    if (!idling_.load(std::memory_order_relaxed)) {
        idling_.store(true, std::memory_order_relaxed);
        idleBase_ = now;
        idleRan_ = 0;
    }
    uint64_t due = (uint64_t)(now - idleBase_);
    if (due > idleRan_ + kIdleCatchUp) {         // (behind: a late wake-up; the time beyond the catch-up is dropped)
        idleBase_ = now - (double)(idleRan_ + kIdleCatchUp);
        due = idleRan_ + kIdleCatchUp;
    }
    while (due > idleRan_ && !threadShouldExit() && idling_.load(std::memory_order_relaxed)) {
        const uint32_t step = (uint32_t)std::min<uint64_t>(due - idleRan_, kIdleMaxStep);
        if (tick_)
            tick_(step);
        idleRan_ += step;
        idleMs_.fetch_add(step, std::memory_order_relaxed);
        sendPending();
        if (juce::Time::getMillisecondCounterHiRes() - lastBlockMs_.load(std::memory_order_relaxed) < after)
            idling_.store(false, std::memory_order_relaxed);   // (a block came meanwhile: the host has the clock)
    }
}

void EditorPort::run()
{
    while (!threadShouldExit()) {
        sendPending();
        idleClock();
        sendPending();
        wait(1);
    }
}

// ------------------------------------------------------------ the tables ---
// core id or Hub id -> the firmware's USB product string: its usb.c STR2 / USB_STRS, or (fm1-base apps: AMB-1) its
// fm1.toml usb_product, which build.py passes as FELUCCA_USB_PRODUCT
juce::String EditorPort::usbProductName(const juce::String &idIn)
{
    const juce::String id = idIn.toLowerCase();
    static const char *const kTable[][2] = {
        {"choralroot", "ChoralRoot FM-1"}, {"purplemonkey", "ChoralRoot FM-1"}, {"coralflower", "CoralFlower FM-1"},
        {"melodee", "Melodee FM-1"},       {"jiant", "JIANT FM"},               {"duell", "DUELL"},
        {"x0x", "X0X FM-1"},               {"fomni", "FoMni FM-1"},             {"fartmachine", "FoMni FM-1"},
        {"fart", "FoMni FM-1"},            {"fimba", "FiMba FM-1"},             {"fm106", "FM-106"},
        {"fm-106", "FM-106"},
        // the Felucca family and its forks keep Felucca's string
        {"felucca", "Felucca"},            {"felucca8", "Felucca"},             {"sloop", "Felucca"},
        {"sloopdx", "Felucca"},            {"floyd", "Felucca"},                {"sloopbanks", "Felucca"},
        {"orbit", "Felucca"},              {"ghoulbox", "Felucca"},             {"rainbow", "Felucca"},
        {"nomansky", "Felucca"},           {"nmsfm", "Felucca"},                {"obervave", "Felucca"},
        {"frodhes", "Felucca"},            {"dinghy", "Felucca"},               {"amb", "AMB-1 FM-1"},
        {"salt", "Felucca"},               {"jangada", "Felucca"},              {"waveloop", "Felucca"},
        {"gammawave", "Felucca"},          {"fm1quest", "Felucca"},             {"grimm", "Felucca"},
        {"zp12", "Felucca"},
        // fm1-base apps: their fm1.toml usb_product
        {"loom", "LOOM-8 FM-1"},
        // Tonitor's usb.c STR2 (Felucca's, renamed)
        {"tonitor", "FM-1 Tonitor"},
    };
    for (const auto &e : kTable)
        if (id == e[0])
            return e[1];
    return "FM-1";
}

juce::String EditorPort::managerUrl(const juce::String &idIn, const juce::String &catalogEditor)
{
    if (catalogEditor.startsWith("https://"))
        return catalogEditor;
    const juce::String id = idIn.toLowerCase();
    static const char *const kTable[][2] = {
        {"felucca", "https://hugelton.github.io/Felucca/webapp/editor/"},
        {"melodee", "https://keremimo.github.io/melodee/webapp/editor/"},
        {"jiant", "https://juanjiant-bit.github.io/JIANT/webapp/editor/"},
        {"ghoulbox", "https://jasonpersinger.me/ghoulbox-fm1-dungeon-synth/webapp/editor/"},
        {"sloop", "https://isod89.github.io/sloop-fm1/webapp/editor/"},
        {"sloopdx", "https://dx7.designburgapps.com/webapp/editor/"},
        {"choralroot", "https://quixotic7.github.io/ChoralRootFM1/"},
        {"fm106", "https://patttf.github.io/fm-106/Librarian.html"},
        {"loom", "https://charlesvestal.github.io/fm1-loom/samples/"},
        {"tonitor", "https://nzimas.github.io/tonitor/backup.html"},   // (no editor: the sessions' backup page)
    };
    for (const auto &e : kTable)
        if (id == e[0])
            return e[1];
    return {};
}
