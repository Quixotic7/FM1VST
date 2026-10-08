// SPDX-License-Identifier: GPL-3.0-only
// The plugin's AudioProcessor: see Processor.h for the threads, the Tier 2 seam, the files and the preset format.
#include "Processor.h"
#include "Editor.h"

#include <algorithm>
#include <cstring>

// ============================================================ parameters ===
PanelBoolParameter::PanelBoolParameter(const juce::String &id, const juce::String &initialName)
    : juce::AudioParameterBool(juce::ParameterID{id, 1}, initialName, false), displayName_(initialName)
{
}
juce::String PanelBoolParameter::getName(int maximumStringLength) const
{
    const juce::SpinLock::ScopedLockType l(nameLock_);
    return maximumStringLength > 0 ? displayName_.substring(0, maximumStringLength) : displayName_;
}
void PanelBoolParameter::setDisplayName(const juce::String &n)
{
    const juce::SpinLock::ScopedLockType l(nameLock_);
    displayName_ = n;
}

const char *const FM1Processor::kPanelLabels[EMU_NB] = {"FX",   "SEL", "ENV", "LFO",  "EDIT", "GLO",   "HOME",
                                                        "SAVE", "ARP", "SEQ", "PLAY", "REC",  "OCT-", "OCT+"};
const char *const FM1Processor::kButtonIds[EMU_NB] = {"btn_fx",   "btn_sel", "btn_env", "btn_lfo",  "btn_edit",
                                                      "btn_glo",  "btn_home", "btn_save", "btn_arp", "btn_seq",
                                                      "btn_play", "btn_rec", "btn_octdn", "btn_octup"};

static juce::String noteName(int midiNote) { return juce::MidiMessage::getMidiNoteName(midiNote, true, true, 4); }

// ======================================================== files, presets ===
static constexpr int kKeepBackups = 50;
static const char *const kPresetExt = ".fm1preset";

static bool readFileBytes(const juce::File &f, std::vector<uint8_t> &out)
{
    juce::MemoryBlock mb;
    if (!f.existsAsFile() || !f.loadFileAsData(mb))
        return false;
    out.assign((const uint8_t *)mb.getData(), (const uint8_t *)mb.getData() + mb.getSize());
    return true;
}

static bool writeFileAtomic(const juce::File &f, const void *data, size_t n)
{
    if (!f.getParentDirectory().createDirectory())
        return false;
    juce::TemporaryFile tmp(f);
    {
        juce::FileOutputStream out(tmp.getFile());
        if (!out.openedOk() || (n && !out.write(data, n)))
            return false;
        out.flush();
        if (out.getStatus().failed())
            return false;
    }
    return tmp.overwriteTargetFileWithTemporary();
}

static juce::MemoryBlock gzip(const std::vector<uint8_t> &bytes)
{
    juce::MemoryOutputStream mo;
    {
        juce::GZIPCompressorOutputStream gz(mo, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        if (!bytes.empty())
            gz.write(bytes.data(), bytes.size());
        gz.flush();
    }
    return mo.getMemoryBlock();
}

static bool gunzip(const void *data, size_t n, size_t maxBytes, std::vector<uint8_t> &out)
{
    juce::MemoryInputStream mi(data, n, false);
    juce::GZIPDecompressorInputStream gz(&mi, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock mb;
    juce::MemoryOutputStream mo(mb, false);
    char buf[16384];
    for (;;) {
        const int got = gz.read(buf, sizeof buf);
        if (got <= 0)
            break;
        mo.write(buf, (size_t)got);
        if (mo.getDataSize() > maxBytes)
            return false;
    }
    mo.flush();
    out.assign((const uint8_t *)mb.getData(), (const uint8_t *)mb.getData() + mo.getDataSize());
    return true;
}

struct PresetHeader {
    int format = 0;
    juce::String core, version, name, date;
    int size = 0;
};

static juce::MemoryBlock encodePreset(const PresetHeader &h, const std::vector<uint8_t> &flash)
{
    auto *o = new juce::DynamicObject();
    o->setProperty("fm1preset", 1);
    o->setProperty("core", h.core);
    o->setProperty("version", h.version);
    o->setProperty("name", h.name);
    o->setProperty("date", h.date);
    o->setProperty("size", (int)flash.size());
    const juce::String json = juce::JSON::toString(juce::var(o), juce::JSON::FormatOptions{}.withSpacing(juce::JSON::Spacing::none));
    const juce::String line = json.removeCharacters("\r\n");
    juce::MemoryBlock mb(line.toRawUTF8(), line.getNumBytesAsUTF8());
    mb.append("\n", 1);
    const juce::MemoryBlock z = gzip(flash);
    mb.append(z.getData(), z.getSize());
    return mb;
}

static bool decodePreset(const void *data, size_t n, size_t maxBytes, PresetHeader &h, std::vector<uint8_t> &flash)
{
    const char *p = (const char *)data;
    const void *nl = std::memchr(p, '\n', std::min<size_t>(n, 4096));
    if (!nl)
        return false;
    const size_t hl = (size_t)((const char *)nl - p);
    const juce::var v = juce::JSON::parse(juce::String::fromUTF8(p, (int)hl));
    if (!v.isObject() || (int)v.getProperty("fm1preset", 0) < 1)
        return false;
    h.format = (int)v.getProperty("fm1preset", 0);
    h.core = v.getProperty("core", "").toString();
    h.version = v.getProperty("version", "").toString();
    h.name = v.getProperty("name", "").toString();
    h.date = v.getProperty("date", "").toString();
    h.size = (int)v.getProperty("size", 0);
    return gunzip(p + hl + 1, n - hl - 1, maxBytes, flash);
}

static bool readPresetFile(const juce::File &f, size_t maxBytes, PresetHeader &h, std::vector<uint8_t> &flash)
{
    juce::MemoryBlock mb;
    return f.existsAsFile() && f.loadFileAsData(mb) && decodePreset(mb.getData(), mb.getSize(), maxBytes, h, flash);
}

static juce::String legalName(const juce::String &name)
{
    return juce::File::createLegalFileName(name.trim()).trim();
}

// ============================================================ processor ===
FM1Processor::FM1Processor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (auto &ch : noteKey_)
        ch.fill(-1);
    sysexOut_.reserve(4096);
    {
        const juce::ScopedLock l(devLock_);
        loadCoreLocked(coreId_);                 // (named buttons for the parameters; booted in prepareToPlay)
    }
    createKnobParameters();                      // first: the Roto-Control's first page is the eight physical knobs
    createTier2Parameters();
    createTier1Parameters();
    {
        const juce::ScopedLock l(devLock_);
        bindTier2Locked();
    }
    theme_ = ThemeStore(home()).defaultTheme();  // (a state restore replaces it)
    startTimerHz(30);                            // the Tier 2 feedback drain
}

FM1Processor::~FM1Processor()
{
    stopTimer();
    cancelPendingUpdate();
    const juce::ScopedLock l(devLock_);
    saveWorkingFlashLocked();
    teardownLocked();
}

juce::AudioProcessorEditor *FM1Processor::createEditor() { return new FM1Editor(*this); }

void FM1Processor::createTier1Parameters()
{
    for (int i = 0; i < EMU_NB; i++) {
        auto *p = new PanelBoolParameter(kButtonIds[i], buttonName(i));
        btn_[(size_t)i] = p;
        addParameter(p);
    }
    for (int k = 0; k < EMU_NKEY; k++) {
        auto *p = new PanelBoolParameter(juce::String::formatted("key_%02d", k), noteName(kNoteBase + k));
        key_[(size_t)k] = p;
        addParameter(p);
    }
}

// the eight physical knobs in panel order: MASTER (the pot) then SELECT PRESETS ALGORITHM KNOB1..4
void FM1Processor::createKnobParameters()
{
    // the emulator's power-on value: emu_fw_init sets emu_hal.master = 724 when the host left it 0
    // (the value text carries the function, as the other knobs': "Master: 724")
    master_ = new juce::AudioParameterInt(
        juce::ParameterID{"knob_master", 1}, "Knob Master", 0, 1023, 724,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([](int v, int maxLen) {
                const juce::String t = "Master: " + juce::String(v);
                return maxLen > 0 ? t.substring(0, maxLen) : t;
            })
            .withValueFromStringFunction([](const juce::String &t) {
                return (t.contains(": ") ? t.fromFirstOccurrenceOf(": ", false, false) : t).trim().getIntValue();
            }));
    addParameter(master_);
    static const struct { int role; const char *id, *name; } K[] = {
        {EMU_E_SELECT, "knob_select", "Knob Select"}, {EMU_E_PRESETS, "knob_presets", "Knob Presets"},
        {EMU_E_ALGO, "knob_algo", "Knob Algo"},       {EMU_E_K1, "knob_1", "Knob 1"},
        {EMU_E_K2, "knob_2", "Knob 2"},               {EMU_E_K3, "knob_3", "Knob 3"},
        {EMU_E_K4, "knob_4", "Knob 4"}};
    for (const auto &k : K) {
        auto *p = new KnobParameter(k.role, k.id, k.name);
        knob_[(size_t)k.role] = p;
        addParameter(p);
    }
}

// ============================================================== Tier 2 ===
void FM1Processor::createTier2Parameters()
{
    for (int i = 0; i < kTier2Slots; i++) {
        auto *p = new Tier2Parameter(i);
        t2_[(size_t)i] = p;
        addParameter(p);
    }
    t2Created_ = true;
}

void FM1Processor::bindTier2Locked()
{
    if (!t2Created_)
        return;                                  // (the constructor: the slots come after the first load)
    const fm1core_t *c = loaded_ ? loaded_->core() : nullptr;
    const uint32_t n = c && c->params ? c->nparams : 0u;
    uint32_t e = 0, visible = 0;
    int meta = 0;
    t2Bound_ = 0;
    for (uint32_t i = 0; i < n; i++)
        visible += !(c->params[i].flags & FM1P_HIDDEN);
    for (int i = 0; i < kTier2Slots; i++) {          // the visible entries in map order (FM1P_HIDDEN: knob targets)
        while (e < n && (c->params[e].flags & FM1P_HIDDEN))
            e++;
        const fm1param_t *p = e < n ? &c->params[e++] : nullptr;
        t2_[(size_t)i]->bind(p, p && (p->flags & FM1P_META) ? meta++ : -1);
        if (p)
            t2Bound_ = i + 1;
    }
    for (KnobParameter *k : knob_)                   // (relative until the first read-back reads knob_target)
        k->unbindKnob();
    t2Gen_.fetch_add(1);
    t2Reported_.fill(0);
    t2Valid_.fill(false);
    t2Holdoff_.fill(0);
    knobValid_.fill(false);
    knobHoldoff_.fill(0);
    knobSigValid_.fill(false);
    resetKnobTurns();
    t2EpochValid_ = false;
    t2Sweep_.store(true);
    if (kTier2Slots > 0 && visible > (uint32_t)kTier2Slots)
        message_ = juce::String(c->name) + ": " + juce::String(visible) + " firmware parameters, " +
                   juce::String(kTier2Slots) + " host slots (build with -DFM1_TIER2_SLOTS)";
}

void FM1Processor::unbindTier2Locked()
{
    for (Tier2Parameter *p : t2_)
        if (p)
            p->unbind();
    for (KnobParameter *k : knob_)
        if (k)
            k->unbindKnob();
    t2Bound_ = 0;
    t2Gen_.fetch_add(1);
}

bool FM1Processor::pushTier2(int slot, int32_t v, uint8_t kind, int32_t aux)
{
    int s1, n1, s2, n2;
    t2Fifo_.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 + n2 < 1)
        return false;
    t2Buf_[(size_t)(n1 ? s1 : s2)] = T2Msg{(int16_t)slot, kind, t2Gen_.load(std::memory_order_relaxed), v, aux};
    t2Fifo_.finishedWrite(1);
    return true;
}

void FM1Processor::applyHostParameters()
{
    for (int i = 0; i < t2Bound_; i++) {
        Tier2Parameter *p = t2_[(size_t)i];
        int32_t v;
        if (!p->takePending(v))
            continue;
        if (const fm1param_t *e = p->entry())
            e->set(v);
        t2Reported_[(size_t)i] = v;              // the host's own value: not echoed
        t2Valid_[(size_t)i] = true;
        t2Holdoff_[(size_t)i] = (uint8_t)kHoldoffFrames;
    }
    for (int r = 0; r < kKnobs; r++) {           // the knobs: their target's set(), or detents (relative)
        KnobParameter *k = knob_[(size_t)r];
        int32_t v;
        // just retargeted: what the host sends now was meant for the old target (its motor, its value: the push has
        // not reached it yet). Dropped; the knob is told its value again.
        const bool settling = knobSettling_[(size_t)r] && dev_->ms() - knobRetargetMs_[(size_t)r] < kSettleMs;
        if (!settling)
            knobSettling_[(size_t)r] = false;
        if (k->isRelative()) {
            if (settling) {
                if (k->takeDetents(v, knobDetents_.load()) || k->relativeOffCentre()) {
                    k->recentre();
                    pushTier2(kTier2Slots + r, (int32_t)k->hostSeq(), kKnobCentre);
                    knobDropped_[(size_t)r].fetch_add(1);
                }
                continue;
            }
            if (k->takeDetents(v, knobDetents_.load())) {
                dev_->enc(r, v);                 // (as a panel turn: the firmware's own knob code)
                hostEnc_[(size_t)r] += v;        // (the host's own detents: not a device turn to report)
                knobSent_[(size_t)r].fetch_add(v);
                knobLastTurn_[(size_t)r] = dev_->ms();
            }
            continue;
        }
        if (!k->takePending(v))
            continue;
        if (settling) {
            knobValid_[(size_t)r] = false;       // (the next read-back tells the host the target's value)
            knobHoldoff_[(size_t)r] = 0;
            knobDropped_[(size_t)r].fetch_add(1);
            continue;
        }
        if (const fm1param_t *e = k->entry())
            e->set(v);
        knobReported_[(size_t)r] = v;
        knobValid_[(size_t)r] = true;
        knobHoldoff_[(size_t)r] = (uint8_t)kHoldoffFrames;
    }
}

// the seven knobs' targets (the epoch moved, or the first frame after a bind): retarget the ones that changed (which
// entry, or its range, flags or name: a rewritten cell) and report their values (relabel: no gesture)
static uint32_t knobSignature(int32_t t, const fm1param_t *e)
{
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)t);
    if (e) {
        mix((uint32_t)e->min);
        mix((uint32_t)e->max);
        mix(e->flags);
        mix((uint32_t)(uintptr_t)e->names);
        for (const char *s = e->name; s && *s; s++)
            mix((uint8_t)*s);
    }
    return h;
}

void FM1Processor::retargetKnobs()
{
    const fm1core_t *c = dev_->core();
    for (int r = 0; r < kKnobs; r++) {
        KnobParameter *k = knob_[(size_t)r];
        const int32_t t = c->knob_target ? c->knob_target(r) : -1;
        const fm1param_t *e = t >= 0 && (uint32_t)t < c->nparams ? &c->params[t] : nullptr;
        const uint32_t sig = knobSignature(e ? t : -1, e);
        if (knobSigValid_[(size_t)r] && sig == knobSig_[(size_t)r])
            continue;
        k->retargetKnob(e);
        knobHoldoff_[(size_t)r] = 0;
        nudgeAcc_[(size_t)r] = 0;
        knobRetargetMs_[(size_t)r] = dev_->ms();
        knobSettling_[(size_t)r] = true;
        if (e) {
            const int32_t v = e->get();
            if (!pushTier2(kTier2Slots + r, v, kT2Relabel))
                continue;                        // (the FIFO full: retried next frame)
            knobReported_[(size_t)r] = v;
            knobValid_[(size_t)r] = true;
        } else {
            if (!pushTier2(kTier2Slots + r, (int32_t)k->hostSeq(), kKnobNorm))
                continue;
            knobValid_[(size_t)r] = false;
            knobLastTurn_[(size_t)r] = dev_->ms();
        }
        knobSig_[(size_t)r] = sig;
        knobSigValid_[(size_t)r] = true;
    }
}

void FM1Processor::resetKnobTurns()
{
    for (int r = 0; r < kKnobs; r++) {
        encSeen_[(size_t)r] = dev_ ? dev_->enc_total(r) : 0;
        hostEnc_[(size_t)r] = 0;
        nudgeAcc_[(size_t)r] = 0;
        ownTurnMs_[(size_t)r] = 0;
        ownTurnValid_[(size_t)r] = false;
    }
}

// the detents each knob was turned on the device since the last frame (the panel, its keys, a fine turn, a test's
// Device::enc), the host's own (a relative knob's host change) left out
void FM1Processor::takeKnobTurns(std::array<int32_t, EMU_NE - 1> &own)
{
    for (int r = 0; r < kKnobs; r++) {
        const int32_t total = dev_->enc_total(r);
        own[(size_t)r] = total - encSeen_[(size_t)r] - hostEnc_[(size_t)r];
        encSeen_[(size_t)r] = total;
        hostEnc_[(size_t)r] = 0;
        if (own[(size_t)r]) {
            ownTurnMs_[(size_t)r] = dev_->ms();
            ownTurnValid_[(size_t)r] = true;
        }
    }
}

bool FM1Processor::ownTurnRecent(int r) const
{
    return ownTurnValid_[(size_t)r] && dev_->ms() - ownTurnMs_[(size_t)r] <= kOwnTurnMs;
}

void FM1Processor::readBackParameters()
{
    if (!dev_ || dev_->halted())
        return;
    const fm1core_t *c = dev_->core();
    std::array<int32_t, EMU_NE - 1> own{};
    takeKnobTurns(own);
    // the epoch: a screen, layer, mode, part or engine changed (or the first frame after a bind): the knobs' targets,
    // the meta slots' targets
    {
        const uint32_t e = c->param_epoch ? c->param_epoch() : 0u;
        if (!t2EpochValid_ || e != t2Epoch_) {
            bool ok = true;
            for (int i = 0; i < t2Bound_; i++) {
                Tier2Parameter *p = t2_[(size_t)i];
                const fm1param_t *o = p->entry();
                if (!p->isMeta() || !o || !o->target)
                    continue;
                const int32_t t = o->target();
                p->retarget(t >= 0 && (uint32_t)t < c->nparams ? &c->params[t] : o);
                const int32_t v = o->get();
                if (pushTier2(i, v, kT2Relabel)) {
                    t2Reported_[(size_t)i] = v;
                    t2Valid_[(size_t)i] = true;
                } else {
                    ok = false;
                }
            }
            if (!t2EpochValid_)
                knobSigValid_.fill(false);       // (after a bind: every knob announced)
            retargetKnobs();
            for (int r = 0; r < kKnobs; r++)
                ok = ok && knobSigValid_[(size_t)r];
            t2Epoch_ = e;
            t2EpochValid_ = ok;                  // (the FIFO full: retried next frame)
        }
    }
    const bool sweep = t2Sweep_.load();
    bool swept = true;
    for (int i = 0; i < t2Bound_; i++) {
        const fm1param_t *o = t2_[(size_t)i]->entry();
        if (!o)
            continue;
        if (!sweep && t2Holdoff_[(size_t)i]) {
            t2Holdoff_[(size_t)i]--;
            continue;
        }
        const int32_t v = o->get();
        if (!sweep && t2Valid_[(size_t)i] && v == t2Reported_[(size_t)i])
            continue;
        if (pushTier2(i, v, sweep ? kT2Sweep : kT2Change, 1)) {
            t2Reported_[(size_t)i] = v;
            t2Valid_[(size_t)i] = true;
        } else {
            swept = false;                       // (full: what was not reported is retried next frame)
        }
    }
    for (int r = 0; r < kKnobs; r++) {           // the knobs: a bound one as a slot; a relative one springs back
        KnobParameter *k = knob_[(size_t)r];
        if (k->isRelative()) {
            // a device turn: shown to the host as a nudge off the centre (inside a gesture), then the spring back
            if (own[(size_t)r]) {
                const int32_t acc = juce::jlimit(-KnobParameter::kDefaultDetents / 2, KnobParameter::kDefaultDetents / 2,
                                                 nudgeAcc_[(size_t)r] + own[(size_t)r]);
                if (pushTier2(kTier2Slots + r, (int32_t)k->hostSeq(), kKnobNudge, acc)) {
                    nudgeAcc_[(size_t)r] = acc;
                    knobLastTurn_[(size_t)r] = dev_->ms();
                }
                continue;
            }
            if ((k->relativeOffCentre() || nudgeAcc_[(size_t)r]) &&
                dev_->ms() - knobLastTurn_[(size_t)r] >= KnobParameter::kRecentreMs) {
                const uint32_t seq = k->hostSeq();
                if (pushTier2(kTier2Slots + r, (int32_t)seq, kKnobCentre)) {
                    k->recentre();               // (no detents: the base moves with the value)
                    nudgeAcc_[(size_t)r] = 0;
                }
            }
            continue;
        }
        const fm1param_t *e = k->entry();
        if (!e)
            continue;
        if (!sweep && knobHoldoff_[(size_t)r]) {
            knobHoldoff_[(size_t)r]--;
            continue;
        }
        const int32_t v = e->get();
        if (!sweep && knobValid_[(size_t)r] && v == knobReported_[(size_t)r])
            continue;
        // a gesture only when this knob was turned on the device just now: its own change is a touch (Live's
        // Configure, the Roto-Control's touch, automation); the target moved by anything else (a sound loaded by
        // PRESETS changing the send KNOB4 shows, a MIDI CC) is a plain value update
        if (pushTier2(kTier2Slots + r, v, sweep ? kT2Sweep : kT2Change, !sweep && ownTurnRecent(r) ? 1 : 0)) {
            knobReported_[(size_t)r] = v;
            knobValid_[(size_t)r] = true;
        } else {
            swept = false;
        }
    }
    if (sweep && swept)
        t2Sweep_.store(false);
}

// the message thread: what the read-back pushed, coalesced (one update per parameter per drain, the latest), told
// to the host. A Tier 2 slot's firmware change: inside a gesture (as before the knobs). A knob: inside a gesture only
// when it was turned on the device (kT2Change with aux 1, kKnobNudge); those first, so a host collecting touched
// parameters sees the turned knob before any knob its turn moved. Nothing is told that the host already has (the
// same firmware value, a relative knob already at 0.5). parameterInfoChanged only for a Tier 2 meta slot's relabel
// (the knobs' names, ranges and steps never change).
void FM1Processor::drainTier2()
{
    struct Acc {
        bool any = false, gesture = false, allChange = true;
        uint8_t kind = kT2Change;
        int32_t v = 0, aux = 0;
    };
    constexpr int kSlots = FM1_TIER2_SLOTS + EMU_NE - 1;
    std::array<Acc, kSlots> acc{};
    const uint32_t gen = t2Gen_.load();
    bool any = false, relabel = false;
    for (;;) {
        int s1, n1, s2, n2;
        t2Fifo_.prepareToRead(kT2Fifo, s1, n1, s2, n2);
        if (n1 + n2 == 0)
            break;
        auto take = [&](int start, int cnt) {
            for (int k = 0; k < cnt; k++) {
                const T2Msg &m = t2Buf_[(size_t)(start + k)];
                if (m.gen != gen || m.slot < 0 || m.slot >= kSlots)
                    continue;                    // (from before a rebind)
                Acc &a = acc[(size_t)m.slot];
                a.any = any = true;
                a.v = m.value;                   // coalesced: the latest
                a.aux = m.aux;
                a.kind = m.kind;
                if (m.kind != kT2Change)
                    a.allChange = false;
                if ((m.kind == kT2Change && m.aux) || m.kind == kKnobNudge)
                    a.gesture = true;
                relabel = relabel || (m.kind == kT2Relabel && m.slot < kTier2Slots);
            }
        };
        take(s1, n1);
        take(s2, n2);
        t2Fifo_.finishedRead(n1 + n2);
    }
    if (!any)
        return;
    if (relabel)
        updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
    for (int i = 0; i < kTier2Slots; i++) {      // the Tier 2 slots (opt-in): as before the knobs
        const Acc &a = acc[(size_t)i];
        if (a.any)
            t2_[(size_t)i]->setFromFirmware(a.v, a.allChange && a.gesture);
    }
    bool quiet = false;                          // a knob told without a gesture: ask a VST3 host to re-read
    for (int pass = 0; pass < 2; pass++)         // the knobs: the touched ones first
        for (int r = 0; r < kKnobs; r++) {
            const Acc &a = acc[(size_t)(kTier2Slots + r)];
            if (!a.any || a.gesture != (pass == 0))
                continue;
            KnobParameter *k = knob_[(size_t)r];
            if (a.kind == kKnobNudge) {          // relative, turned on the device: unless the host turned it since
                if (k->hostSeq() == (uint32_t)a.v && k->isRelative())
                    k->nudge((float)(0.5 + (double)a.aux / KnobParameter::kDefaultDetents));
            } else if (a.kind == kKnobNorm || a.kind == kKnobCentre) {   // relative: 0.5, unless the host turned it
                if (k->hostSeq() == (uint32_t)a.v && k->isRelative() && !juce::exactlyEqual(k->hostValue(), 0.5f)) {
                    k->setNormFromFirmware(0.5f);
                    quiet = true;
                }
            } else if (!k->isRelative()) {
                // a change: unless the host's value is that step already; a relabel or the sweep: unless the host has
                // exactly that value (the old one means another range)
                const bool same = a.kind == kT2Change ? k->toPlain(k->hostValue()) == a.v : juce::exactlyEqual(k->toNorm(a.v), k->hostValue());
                if (!same) {
                    k->setFromFirmware(a.v, a.gesture);
                    quiet = quiet || !a.gesture;
                }
            }
        }
    // (performEdit outside a gesture is outside the VST3 contract: Live ignored it. kParamValuesChanged makes the
    // host read back what the wrapper's controller now holds, without a touch)
    if (quiet)
        vst3_.refresh();
}

juce::String FM1Processor::buttonName(int i) const
{
    const char *n = loaded_ && loaded_->core()->button_names[i] ? loaded_->core()->button_names[i] : kPanelLabels[i];
    return juce::String(n) + " (" + kPanelLabels[i] + ")";
}

void FM1Processor::relabelParameters()
{
    {
        const juce::ScopedLock l(devLock_);
        for (int i = 0; i < EMU_NB; i++)
            btn_[(size_t)i]->setDisplayName(buttonName(i));
    }
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
}

// ------------------------------------------------------------- paths ---
juce::File FM1Processor::home() { return juce::File(CoreLoader::app_support_dir()).getChildFile("fm1emu"); }
juce::File FM1Processor::userCoresDir() { return home().getChildFile("cores"); }

juce::File FM1Processor::bundledCoresDirFor(const juce::File &exe)
{
    const juce::File macos = exe.getParentDirectory(), contents = macos.getParentDirectory();
    if (macos.getFileName() == "MacOS" && contents.getFileName() == "Contents")
        return contents.getChildFile("Resources").getChildFile("cores");
    return {};
}

juce::File FM1Processor::bundledCoresDir()
{
    const juce::File d = bundledCoresDirFor(juce::File::getSpecialLocation(juce::File::currentExecutableFile));
    if (d != juce::File() && d.isDirectory())
        return d;
#ifdef FM1_CORES_DIR
    const juce::File dev(FM1_CORES_DIR);
    if (dev.isDirectory())
        return dev;
#endif
    return d;
}

juce::File FM1Processor::coreDir() const { return home().getChildFile(coreId_); }

std::vector<CoreInfo> FM1Processor::listCores() const
{
    const juce::File user = userCoresDir();
    user.createDirectory();
    std::vector<CoreInfo> all = CoreLoader::scan({bundledCoresDir().getFullPathName().toStdString(),
                                                  user.getFullPathName().toStdString()});
    std::vector<CoreInfo> out;
    for (const CoreInfo &c : all) {          // in scan order (bundled first); a later one with the same id replaces
        auto it = std::find_if(out.begin(), out.end(), [&](const CoreInfo &o) { return o.id == c.id; });
        if (it != out.end())
            *it = c;
        else
            out.push_back(c);
    }
    return out;
}

// ------------------------------------------------- the core, the device ---
bool FM1Processor::loadCoreLocked(const juce::String &id)
{
    for (const CoreInfo &c : listCores()) {
        if (juce::String(c.id) != id)
            continue;
        std::string err;
        std::unique_ptr<LoadedCore> lc = CoreLoader::load(c.path, &err);
        if (!lc) {
            message_ = juce::String(err);
            return false;
        }
        loaded_ = std::move(lc);
        coreInfo_ = c;
        coreId_ = id;
        message_.clear();
        bindTier2Locked();
        return true;
    }
    message_ = "firmware \"" + id + "\" is not installed";
    return false;
}

void FM1Processor::bootLocked()
{
    if (!loaded_ || dev_)
        return;
    std::vector<uint8_t> bytes;
    if (pendingFlash_)
        bytes = *pendingFlash_;
    else
        readFileBytes(workingFlashFile(), bytes);
    pendingFlash_.reset();
    const uint32_t size = loaded_->core()->flash_size;
    if (bytes.size() > size)
        bytes.resize(size);
    dev_ = std::make_unique<Device>(loaded_->core());
    dev_->set_between([this](uint32_t, bool frame) {
        if (frame) {
            fineFrame();                         // (emu.c ui_frame: fine_frame before the frame)
            readBackParameters();
        }
    });
    dev_->boot_from(bytes.empty() ? nullptr : bytes.data(), (uint32_t)bytes.size());
    resetAudioState();
    resetKnobTurns();                            // (a new device: its detent counters start at 0)
    t2Sweep_.store(true);                        // the first read-back reports every Tier 2 value
    t2EpochValid_ = false;
    halted_.store(dev_->halted());
    haltCode_.store(loaded_->core()->halted ? loaded_->core()->halted() : 0);
}

void FM1Processor::teardownLocked()
{
    unbindTier2Locked();                         // (before the module goes: no slot may call into it)
    dev_.reset();
    loaded_.reset();                             // shutdown, unload, the per-instance copy deleted
}

void FM1Processor::resetAudioState()            // (devLock_ held, processing stopped)
{
    appliedKeys_ = appliedButtons_ = 0xFFFFFFFFu;
    appliedMaster_ = -1;
    midiKeys_ = paramKeys_ = 0;
    paramButtons_ = panelKeysCur_ = panelButtonsCur_ = 0;
    fineStage_ = 0;
    fineSteps_ = 0;
    fineGlo_ = false;
    for (auto &e : panelEnc_)
        e.store(0);
    viewDirty_ = true;                           // (a new device: a fresh snapshot, the LCD included)
    for (auto &ch : noteKey_)
        ch.fill(-1);
    keyCount_.fill(0);
    inW_ = inR_ = 0;
    sysexOut_.clear();
    sysexOutOverflow_ = false;
}

std::vector<uint8_t> FM1Processor::currentFlashLocked()
{
    std::vector<uint8_t> out;
    if (dev_ && dev_->booted() && dev_->flash()) {
        dev_->flash_sync();
        out.assign(dev_->flash(), dev_->flash() + dev_->flash_size());
    } else if (pendingFlash_) {
        out = *pendingFlash_;
    } else if (loaded_) {
        readFileBytes(workingFlashFile(), out);
    }
    return out;
}

std::vector<uint8_t> FM1Processor::currentFlash()
{
    const juce::ScopedLock l(devLock_);
    return currentFlashLocked();
}

bool FM1Processor::saveWorkingFlashLocked()
{
    if (!loaded_ || !(dev_ && dev_->booted()))
        return false;                            // (never powered on: nothing new to keep)
    const std::vector<uint8_t> f = currentFlashLocked();
    return !f.empty() && writeFileAtomic(workingFlashFile(), f.data(), f.size());
}

bool FM1Processor::saveWorkingFlash()
{
    const juce::ScopedLock l(devLock_);
    return saveWorkingFlashLocked();
}

bool FM1Processor::powerCycleLocked(std::optional<std::vector<uint8_t>> bytes)
{
    if (coreInfo_.path.empty())
        return false;
    if (!bytes)
        bytes = currentFlashLocked();
    teardownLocked();
    std::string err;
    loaded_ = CoreLoader::load(coreInfo_.path, &err);
    if (!loaded_) {
        message_ = juce::String(err);
        return false;
    }
    bindTier2Locked();                           // (a new image: new entries)
    pendingFlash_ = std::move(bytes);
    halted_.store(false);
    haltCode_.store(0);
    if (prepared_)
        bootLocked();
    return true;
}

bool FM1Processor::powerCycle()
{
    suspendProcessing(true);
    bool ok;
    {
        const juce::ScopedLock l(devLock_);
        ok = powerCycleLocked(std::nullopt);
    }
    suspendProcessing(false);
    return ok;
}

bool FM1Processor::switchCoreLocked(const juce::String &id, std::optional<std::vector<uint8_t>> bytes)
{
    requestedCore_ = id;
    if (id == coreId_ && loaded_) {
        if (bytes && !dev_) {                    // not powered on yet: the boot will use them
            pendingFlash_ = std::move(bytes);
            return true;
        }
        if (bytes)
            return powerCycleLocked(std::move(bytes));
        return true;
    }
    bool known = false;
    for (const CoreInfo &c : listCores())
        known = known || juce::String(c.id) == id;
    if (!known) {
        message_ = "firmware \"" + id + "\" is not installed";
        return false;
    }
    saveWorkingFlashLocked();                    // the old core's flash, into its own folder
    const juce::String oldId = coreId_;
    teardownLocked();
    pendingFlash_.reset();
    currentPreset_.clear();
    if (!loadCoreLocked(id)) {
        const juce::String why = message_;
        loadCoreLocked(oldId);
        message_ = why;
        if (prepared_)
            bootLocked();
        return false;
    }
    if (bytes)
        pendingFlash_ = std::move(bytes);
    halted_.store(false);
    haltCode_.store(0);
    if (prepared_)
        bootLocked();                            // from the state's bytes, else that core's working flash
    return true;
}

bool FM1Processor::switchCore(const juce::String &id)
{
    suspendProcessing(true);
    bool ok;
    {
        const juce::ScopedLock l(devLock_);
        ok = switchCoreLocked(id, std::nullopt);
    }
    relabelParameters();
    suspendProcessing(false);
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return ok;
}

FM1Processor::CoreStatus FM1Processor::getCoreInfo() const
{
    const juce::ScopedLock l(devLock_);
    CoreStatus s;
    s.loaded = loaded_ != nullptr;
    s.info = coreInfo_;
    s.requestedId = requestedCore_;
    s.halted = halted_.load();
    s.haltCode = haltCode_.load();
    s.message = message_;
    return s;
}

void FM1Processor::handleAsyncUpdate()
{
    if (halted_.load() && haltCode_.load() == 0x100)   // CR_REBOOT: exit(0) = the firmware reboots the unit
        powerCycle();
}

// ------------------------------------------------------------- presets ---
juce::StringArray FM1Processor::listPresets() const
{
    juce::StringArray names;
    for (const juce::File &f : presetsDir().findChildFiles(juce::File::findFiles, false, juce::String("*") + kPresetExt))
        names.add(f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool FM1Processor::savePreset(const juce::String &name)
{
    const juce::String n = legalName(name);
    if (n.isEmpty())
        return false;
    std::vector<uint8_t> f;
    PresetHeader h;
    {
        const juce::ScopedLock l(devLock_);
        if (!loaded_)
            return false;
        f = currentFlashLocked();
        if (f.empty())
            f.assign(loaded_->core()->flash_size, 0xFF);
        h.core = coreId_;
        h.version = juce::String(coreInfo_.version);
        saveWorkingFlashLocked();
    }
    h.name = n;
    h.date = juce::Time::getCurrentTime().toISO8601(true);
    const juce::MemoryBlock mb = encodePreset(h, f);
    if (!writeFileAtomic(presetsDir().getChildFile(n + kPresetExt), mb.getData(), mb.getSize()))
        return false;
    currentPreset_ = n;
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return true;
}

bool FM1Processor::backupLocked(const juce::String &why)
{
    std::vector<uint8_t> f = currentFlashLocked();
    if (f.empty() || !loaded_)
        return false;
    PresetHeader h;
    h.core = coreId_;
    h.version = juce::String(coreInfo_.version);
    const juce::Time now = juce::Time::getCurrentTime();
    h.date = now.toISO8601(true);
    h.name = "backup before " + why;
    const juce::String stamp = now.formatted("%Y-%m-%d_%H-%M-%S") + juce::String::formatted("-%03d", now.getMilliseconds());
    const juce::File dir = backupsDir();
    juce::File file = dir.getChildFile(stamp + kPresetExt);
    for (int i = 2; file.exists(); i++)
        file = dir.getChildFile(stamp + "_" + juce::String(i) + kPresetExt);
    const juce::MemoryBlock mb = encodePreset(h, f);
    if (!writeFileAtomic(file, mb.getData(), mb.getSize()))
        return false;
    juce::Array<juce::File> all = dir.findChildFiles(juce::File::findFiles, false, juce::String("*") + kPresetExt);
    std::sort(all.begin(), all.end(), [](const juce::File &a, const juce::File &b) { return a.getFileName() > b.getFileName(); });
    for (int i = kKeepBackups; i < all.size(); i++)
        all.getReference(i).deleteFile();
    return true;
}

bool FM1Processor::loadPreset(const juce::String &name)
{
    const juce::File file = presetsDir().getChildFile(legalName(name) + kPresetExt);
    PresetHeader h;
    std::vector<uint8_t> f;
    bool ok = false;
    suspendProcessing(true);
    {
        const juce::ScopedLock l(devLock_);
        if (loaded_ && readPresetFile(file, loaded_->core()->flash_size, h, f)) {
            if (h.core.isNotEmpty() && h.core != coreId_) {
                message_ = "preset \"" + name + "\" is for firmware \"" + h.core + "\"";
            } else {
                backupLocked("loading preset " + name);
                message_ = h.version.isNotEmpty() && h.version != juce::String(coreInfo_.version)
                               ? "preset made with " + h.core + " " + h.version + " (loaded: " + coreInfo_.version + ")"
                               : juce::String();
                ok = powerCycleLocked(std::move(f));
                if (ok)
                    currentPreset_ = legalName(name);
            }
        } else if (loaded_) {
            message_ = "cannot read preset \"" + name + "\"";
        }
    }
    suspendProcessing(false);
    if (ok)
        updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return ok;
}

bool FM1Processor::deletePreset(const juce::String &name)
{
    const juce::File f = presetsDir().getChildFile(legalName(name) + kPresetExt);
    if (!f.existsAsFile() || !f.deleteFile())
        return false;
    if (currentPreset_ == legalName(name))
        currentPreset_.clear();
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return true;
}

bool FM1Processor::renamePreset(const juce::String &from, const juce::String &to)
{
    const juce::String a = legalName(from), b = legalName(to);
    const juce::File src = presetsDir().getChildFile(a + kPresetExt), dst = presetsDir().getChildFile(b + kPresetExt);
    if (b.isEmpty() || !src.existsAsFile() || (dst.exists() && a != b))
        return false;
    PresetHeader h;
    std::vector<uint8_t> f;
    if (!readPresetFile(src, 64u << 20, h, f))
        return false;
    h.name = b;
    const juce::MemoryBlock mb = encodePreset(h, f);
    if (!writeFileAtomic(dst, mb.getData(), mb.getSize()))
        return false;
    if (a != b)
        src.deleteFile();
    if (currentPreset_ == a)
        currentPreset_ = b;
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return true;
}

bool FM1Processor::resetFlash()
{
    bool ok = false;
    suspendProcessing(true);
    {
        const juce::ScopedLock l(devLock_);
        if (loaded_) {
            backupLocked("a flash reset");
            ok = powerCycleLocked(std::vector<uint8_t>{});
            currentPreset_.clear();
        }
    }
    suspendProcessing(false);
    return ok;
}

bool FM1Processor::exportPreset(const juce::File &file)
{
    std::vector<uint8_t> f;
    PresetHeader h;
    {
        const juce::ScopedLock l(devLock_);
        if (!loaded_)
            return false;
        f = currentFlashLocked();
        if (f.empty())
            f.assign(loaded_->core()->flash_size, 0xFF);
        h.core = coreId_;
        h.version = juce::String(coreInfo_.version);
    }
    const juce::File out = file.hasFileExtension(kPresetExt) ? file : file.withFileExtension(kPresetExt);
    h.name = out.getFileNameWithoutExtension();
    h.date = juce::Time::getCurrentTime().toISO8601(true);
    const juce::MemoryBlock mb = encodePreset(h, f);
    return writeFileAtomic(out, mb.getData(), mb.getSize());
}

juce::String FM1Processor::importPreset(const juce::File &file)
{
    PresetHeader h;
    std::vector<uint8_t> f;
    if (!readPresetFile(file, 64u << 20, h, f))
        return {};
    if (h.core.isNotEmpty() && h.core != coreId_) {
        const juce::ScopedLock l(devLock_);
        message_ = "\"" + file.getFileName() + "\" is a preset for firmware \"" + h.core + "\"";
        return {};
    }
    juce::String n = legalName(h.name.isNotEmpty() ? h.name : file.getFileNameWithoutExtension());
    if (n.isEmpty())
        n = "Imported";
    juce::File dst = presetsDir().getChildFile(n + kPresetExt);
    for (int i = 2; dst.exists(); i++)
        dst = presetsDir().getChildFile(n + " " + juce::String(i) + kPresetExt);
    h.name = dst.getFileNameWithoutExtension();
    const juce::MemoryBlock mb = encodePreset(h, f);
    if (!writeFileAtomic(dst, mb.getData(), mb.getSize()))
        return {};
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return h.name;
}

// -------------------------------------------------------- host programs ---
int FM1Processor::getNumPrograms() { return std::max(1, listPresets().size()); }
int FM1Processor::getCurrentProgram() { return std::max(0, listPresets().indexOf(currentPreset_)); }
const juce::String FM1Processor::getProgramName(int index)
{
    const juce::StringArray p = listPresets();
    return index >= 0 && index < p.size() ? p[index] : juce::String("Init");
}
void FM1Processor::setCurrentProgram(int index)
{
    const juce::StringArray p = listPresets();
    // the current program again is a no-op: hosts re-send it when restoring a session (the state's flash wins)
    if (index < 0 || index >= p.size() || index == getCurrentProgram())
        return;
    loadPreset(p[index]);
}

// ------------------------------------------------------------- settings ---
void FM1Processor::setTranspose(int octaves) { transpose_.store(juce::jlimit(-2, 2, octaves)); }
Theme FM1Processor::getTheme() const
{
    const juce::SpinLock::ScopedLockType l(themeLock_);
    return theme_;
}
void FM1Processor::setTheme(const Theme &t)
{
    {
        const juce::SpinLock::ScopedLockType l(themeLock_);
        theme_ = t;
    }
    themeSerial_.fetch_add(1);
}

juce::StringArray FM1Processor::buttonNames() const
{
    const juce::ScopedLock l(devLock_);
    juce::StringArray out;
    for (int i = 0; i < EMU_NB; i++)
        out.add(loaded_ && loaded_->core()->button_names[i] ? loaded_->core()->button_names[i] : kPanelLabels[i]);
    return out;
}

// ------------------------------------------------------------ the panel ---
void FM1Processor::panelKey(int key, bool down)
{
    if (key < 0 || key >= EMU_NKEY)
        return;
    const uint32_t bit = 1u << key;
    if (down) {
        panelKeyTaps_.fetch_or(bit);
        panelKeys_.fetch_or(bit);
    } else {
        panelKeys_.fetch_and(~bit);
    }
}

void FM1Processor::panelButton(int label, bool down)
{
    if (label < 0 || label >= EMU_NB)
        return;
    const uint32_t bit = 1u << label;
    if (down) {
        panelBtnTaps_.fetch_or(bit);
        panelButtons_.fetch_or(bit);
    } else {
        panelButtons_.fetch_and(~bit);
    }
}

void FM1Processor::panelEnc(int role, int detents)
{
    if (!detents || role < 0 || role >= EMU_NE)
        return;
    if (role == EMU_E_MASTER)
        panelMaster(master_->get() + 16 * detents);
    else
        panelEnc_[(size_t)role].fetch_add(detents);
}

void FM1Processor::panelMaster(int value)
{
    const int v = juce::jlimit(0, 1023, value);
    if (v == master_->get())
        return;
    master_->beginChangeGesture();
    master_->setValueNotifyingHost(master_->convertTo0to1((float)v));
    master_->endChangeGesture();
}

void FM1Processor::panelFineTurn(int role, int detents)
{
    if (!detents || role < 0 || role >= EMU_NE - 1)
        return;
    fineRole_.store(role);
    fineReq_.fetch_add(detents);
}

void FM1Processor::panelReleaseAll()
{
    panelKeys_.store(0);
    panelButtons_.store(0);
}

bool FM1Processor::getPanelView(PanelView &out) const
{
    const juce::SpinLock::ScopedLockType l(viewLock_);
    if (out.seq == view_.seq)
        return false;
    if (out.lcdSeq != view_.lcdSeq)
        out.lcd = view_.lcd;
    out.seq = view_.seq;
    out.running = view_.running;
    out.lcdWrites = view_.lcdWrites;
    out.lcdSeq = view_.lcdSeq;
    out.keyLed = view_.keyLed;
    out.btnLed = view_.btnLed;
    out.playGreen = view_.playGreen;
    out.keys = view_.keys;
    out.buttons = view_.buttons;
    out.master = view_.master;
    out.knobs = view_.knobs;
    return true;
}

// the audio thread (devLock_ held): param | panel | the fine-turn GLO onto the device's buttons
void FM1Processor::applyButtons()
{
    const uint32_t b = paramButtons_ | panelButtonsCur_ | (fineGlo_ ? 1u << EMU_B_GLO : 0u);
    if (b != appliedButtons_) {
        dev_->buttons(b);
        appliedButtons_ = b;
    }
}

// emu.c fine_turn (at block start): a new request holds GLO now; the detent follows before the next UI frame
void FM1Processor::takeFineRequest()
{
    const int32_t n = fineReq_.exchange(0);
    if (!n)
        return;
    const int role = fineRole_.load();
    if (fineStage_ && role != fineRoleCur_)
        return;                                  // (another knob while one is stepping: dropped, as emu.c)
    fineRoleCur_ = role;
    fineSteps_ += n;
    if (!fineStage_) {
        fineGlo_ = true;
        applyButtons();
        fineStage_ = 1;
    } else if (fineStage_ == 2) {
        fineStage_ = 1;                          // another detent: stepped at the next frame, GLO kept down
    }
}

// emu.c fine_frame (before each UI frame, on the device clock)
void FM1Processor::fineFrame()
{
    if (fineStage_ == 1) {
        dev_->enc(fineRoleCur_, fineSteps_);
        fineSteps_ = 0;
        fineStage_ = 2;
    } else if (fineStage_ == 2) {
        fineGlo_ = false;
        applyButtons();
        fineStage_ = 0;
    }
}

// the end of a block (devLock_ held): a new snapshot for the GUI when anything it shows moved
void FM1Processor::snapshotPanel()
{
    const bool running = dev_ && dev_->booted() && !dev_->halted();
    const emu_hal_t *h = dev_ ? dev_->hal() : nullptr;
    const uint32_t keys = appliedKeys_ == 0xFFFFFFFFu ? 0u : appliedKeys_;
    const uint32_t buttons = appliedButtons_ == 0xFFFFFFFFu ? 0u : appliedButtons_;
    const bool lcdMoved = h && h->lcd_writes != view_.lcdWrites;
    const bool ledsMoved = h && (std::memcmp(viewLeds_, h->led, EMU_NCOL) || std::memcmp(viewLeds_ + EMU_NCOL, h->led_dim, EMU_NCOL));
    // the knobs: what each is bound to and where its value is (the firmware's own, whoever moved it)
    std::array<PanelView::Knob, EMU_NE - 1> kv{};
    for (int r = 0; r < kKnobs; r++) {
        PanelView::Knob &o = kv[(size_t)r];
        const KnobParameter *k = knob_[(size_t)r];
        const fm1param_t *e = running && k && !k->isRelative() ? k->entry() : nullptr;
        o.turns = dev_ ? dev_->enc_total(r) : 0;
        if (!e)
            continue;
        const int32_t v = e->get(), lo = e->min, hi = e->max > e->min ? e->max : e->min + 1;
        o.bound = true;
        o.norm = (float)juce::jlimit(0.0, 1.0, (double)(v - lo) / (double)(hi - lo));
        std::snprintf(o.fn, sizeof o.fn, "%s", e->name ? e->name : "");
    }
    const bool knobsMoved = kv != viewKnobs_;
    if (!viewDirty_ && !lcdMoved && !ledsMoved && !knobsMoved && running == viewRunning_ && keys == viewKeys_ &&
        buttons == viewButtons_ && (!h || h->master == viewMaster_))
        return;
    const juce::SpinLock::ScopedTryLockType l(viewLock_);
    if (!l.isLocked())
        return;                                  // (the GUI is copying: the next block)
    if (h) {
        if (lcdMoved || viewDirty_) {
            std::memcpy(view_.lcd.data(), h->lcd, sizeof h->lcd);
            view_.lcdWrites = h->lcd_writes;
            view_.lcdSeq++;
        }
        std::memcpy(viewLeds_, h->led, EMU_NCOL);
        std::memcpy(viewLeds_ + EMU_NCOL, h->led_dim, EMU_NCOL);
        for (int k = 0; k < EMU_NKEY; k++)
            view_.keyLed[(size_t)k] = (uint8_t)dev_->led_key(k);
        for (int b = 0; b < EMU_NB; b++)
            view_.btnLed[(size_t)b] = (uint8_t)dev_->led_button(b);
        view_.playGreen = (uint8_t)dev_->led_play_green();
        view_.master = h->master;
        viewMaster_ = h->master;
    }
    view_.knobs = viewKnobs_ = kv;
    view_.running = viewRunning_ = running;
    view_.keys = viewKeys_ = keys;
    view_.buttons = viewButtons_ = buttons;
    view_.seq++;
    viewDirty_ = false;
}

// ---------------------------------------------------------------- state ---
// A ValueTree "FM1VST" (binary): stateVersion, core, coreVersion, transpose, midiNotesPlayKeys, keyNotesToFirmware,
// master, preset, editorW, editorH, bigLcd, knobDetents, a child "theme" (name base membrane knob, and bed / label when the theme
// sets them; "#RRGGBB") and "flash": the image gzip-compressed (a MemoryBlock property; absent when the instance
// never had one, i.e. a fresh flash).
void FM1Processor::getStateInformation(juce::MemoryBlock &dest)
{
    juce::ValueTree t("FM1VST");
    std::vector<uint8_t> f;
    {
        const juce::ScopedLock l(devLock_);
        t.setProperty("stateVersion", 1, nullptr);
        t.setProperty("core", loaded_ ? coreId_ : requestedCore_, nullptr);
        t.setProperty("coreVersion", juce::String(coreInfo_.version), nullptr);
        f = currentFlashLocked();
    }
    t.setProperty("transpose", transpose_.load(), nullptr);
    t.setProperty("midiNotesPlayKeys", notesPlayKeys_.load(), nullptr);
    t.setProperty("keyNotesToFirmware", keyNotesToFirmware_.load(), nullptr);
    t.setProperty("master", master_->get(), nullptr);
    t.setProperty("preset", currentPreset_, nullptr);
    if (editorW_.load() > 0 && editorH_.load() > 0) {
        t.setProperty("editorW", editorW_.load(), nullptr);
        t.setProperty("editorH", editorH_.load(), nullptr);
    }
    t.setProperty("bigLcd", bigLcd_.load(), nullptr);
    t.setProperty("knobDetents", knobDetents_.load(), nullptr);
    const Theme th = getTheme();
    juce::ValueTree tt("theme");
    tt.setProperty("name", th.name, nullptr);
    tt.setProperty("base", toHex(th.base), nullptr);
    tt.setProperty("membrane", toHex(th.membrane), nullptr);
    tt.setProperty("knob", toHex(th.knob), nullptr);
    if (th.bed)
        tt.setProperty("bed", toHex(*th.bed), nullptr);
    if (th.label)
        tt.setProperty("label", toHex(*th.label), nullptr);
    t.appendChild(tt, nullptr);
    if (!f.empty())
        t.setProperty("flash", gzip(f), nullptr);
    juce::MemoryOutputStream mo(dest, false);
    t.writeToStream(mo);
}

void FM1Processor::setStateInformation(const void *data, int sizeInBytes)
{
    const juce::ValueTree t = juce::ValueTree::readFromData(data, (size_t)std::max(0, sizeInBytes));
    if (!t.isValid() || !t.hasType("FM1VST"))
        return;
    setTranspose((int)t.getProperty("transpose", 0));
    notesPlayKeys_.store((bool)t.getProperty("midiNotesPlayKeys", true));
    keyNotesToFirmware_.store((bool)t.getProperty("keyNotesToFirmware", false));
    if (t.hasProperty("master"))
        *master_ = juce::jlimit(0, 1023, (int)t.getProperty("master"));
    const juce::ValueTree tt = t.getChildWithName("theme");
    if (tt.isValid()) {                          // (the state's colours win over any theme file)
        Theme th;
        th.name = tt.getProperty("name", th.name).toString();
        th.base = parseHex(tt.getProperty("base", "").toString()).value_or(th.base);
        th.membrane = parseHex(tt.getProperty("membrane", "").toString()).value_or(th.membrane);
        th.knob = parseHex(tt.getProperty("knob", "").toString()).value_or(th.knob);
        th.bed = parseHex(tt.getProperty("bed", "").toString());
        th.label = parseHex(tt.getProperty("label", "").toString());
        if (!tt.hasProperty("label"))            // (a phase 2 state: no label key; a preset's own label)
            if (auto p = ThemeStore::preset(th.name))
                th.label = p->label;
        setTheme(th);
    }
    if (t.hasProperty("editorW") && t.hasProperty("editorH"))
        setEditorSize((int)t.getProperty("editorW"), (int)t.getProperty("editorH"));
    bigLcd_.store((bool)t.getProperty("bigLcd", false));
    setKnobDetents((int)t.getProperty("knobDetents", KnobParameter::kDefaultDetents));
    const juce::String id = t.getProperty("core", "choralroot").toString();
    std::optional<std::vector<uint8_t>> bytes;
    if (const juce::MemoryBlock *z = t.getProperty("flash").getBinaryData()) {
        std::vector<uint8_t> f;
        if (gunzip(z->getData(), z->getSize(), 64u << 20, f))
            bytes = std::move(f);
    } else {
        bytes = std::vector<uint8_t>{};          // the state was saved with a fresh flash
    }
    suspendProcessing(true);
    {
        const juce::ScopedLock l(devLock_);
        switchCoreLocked(id, std::move(bytes));
        currentPreset_ = t.getProperty("preset", "").toString();
    }
    relabelParameters();
    suspendProcessing(false);
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

// ========================================================== audio thread ===
void FM1Processor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    {
        const juce::ScopedLock l(devLock_);
        sampleRate_ = sampleRate > 0 ? sampleRate : 44100.0;
        prepared_ = true;
        if (!dev_ && loaded_)
            bootLocked();
    }
    setLatencySamples((int)Device::latency_frames(sampleRate_));
    scratch_.assign((size_t)std::max(1024, maximumExpectedSamplesPerBlock), 0.0f);
    midiOut_.ensureSize(4096);
}

void FM1Processor::releaseResources() { saveWorkingFlash(); }

bool FM1Processor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.inputBuses.isEmpty() && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void FM1Processor::pushKeys()
{
    const uint32_t m = paramKeys_ | midiKeys_ | panelKeysCur_;
    if (m != appliedKeys_) {
        dev_->keys(m);
        appliedKeys_ = m;
    }
}

void FM1Processor::applyTier1()
{
    uint32_t b = 0, k = 0;
    for (int i = 0; i < EMU_NB; i++)
        if (btn_[(size_t)i]->get())
            b |= 1u << i;
    for (int i = 0; i < EMU_NKEY; i++)
        if (key_[(size_t)i]->get())
            k |= 1u << i;
    // the panel GUI's held source (merged, never written into the parameters) and its taps: a press released
    // before this block reaches the firmware as a tap (emu.c hold_key / hold_btn set keys_tap / buttons_tap)
    const uint32_t btnTaps = panelBtnTaps_.exchange(0), keyTaps = panelKeyTaps_.exchange(0);
    panelButtonsCur_ = panelButtons_.load();
    panelKeysCur_ = panelKeys_.load();
    paramButtons_ = b;
    applyButtons();
    if (const uint32_t t = btnTaps & ~appliedButtons_)
        dev_->buttons_tap(t);
    takeFineRequest();
    paramKeys_ = k;
    const bool np = notesPlayKeys_.load();
    if (!np && lastNotesPlayKeys_) {             // turned off: let go of what MIDI notes held
        for (auto &ch : noteKey_)
            ch.fill(-1);
        keyCount_.fill(0);
        midiKeys_ = 0;
    }
    lastNotesPlayKeys_ = np;
    pushKeys();
    if (const uint32_t t = keyTaps & ~appliedKeys_)
        dev_->keys_tap(t);
    for (int r = 0; r < EMU_NE - 1; r++)         // (MASTER: the parameter, below)
        if (const int32_t n = panelEnc_[(size_t)r].exchange(0))
            dev_->enc(r, n);
    const int m = master_->get();
    if (m != appliedMaster_) {
        dev_->master(m);
        appliedMaster_ = m;
    }
}

void FM1Processor::handleMidi(const juce::MidiMessage &m)
{
    bool keyNote = false;                        // a note-on / off that pressed / released a key
    if (notesPlayKeys_.load()) {
        const int ch = juce::jlimit(1, 16, m.getChannel()) - 1;
        if (m.isNoteOn()) {                      // (velocity 0 is a note-off: isNoteOn() is false for it)
            const int note = m.getNoteNumber(), key = note - kNoteBase + 12 * transpose_.load();
            if (noteKey_[(size_t)ch][(size_t)note] < 0 && key >= 0 && key < EMU_NKEY) {
                noteKey_[(size_t)ch][(size_t)note] = (int8_t)key;
                keyCount_[(size_t)key]++;
                midiKeys_ |= 1u << key;
                keyNote = true;
            }
        } else if (m.isNoteOff()) {
            const int note = m.getNoteNumber(), key = noteKey_[(size_t)ch][(size_t)note];
            if (key >= 0) {
                noteKey_[(size_t)ch][(size_t)note] = -1;
                if (keyCount_[(size_t)key] && --keyCount_[(size_t)key] == 0)
                    midiKeys_ &= ~(1u << key);
                keyNote = true;
            }
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            for (int n = 0; n < 128; n++) {
                const int key = noteKey_[(size_t)ch][(size_t)n];
                if (key >= 0) {
                    noteKey_[(size_t)ch][(size_t)n] = -1;
                    if (keyCount_[(size_t)key] && --keyCount_[(size_t)key] == 0)
                        midiKeys_ &= ~(1u << key);
                }
            }
        }
        pushKeys();
    }
    if (!keyNote || keyNotesToFirmware_.load())
        forwardMidi(m);
}

// one MIDI message -> USB-MIDI event packets (cable 0) for the core's midi_in: CIN | status << 8 | d1 << 16 |
// d2 << 24, as tools/emu/emu_midi.c packs the bytes from CoreMIDI
static inline uint32_t usbPacket(uint32_t cin, uint32_t a, uint32_t b, uint32_t c)
{
    return (cin & 15u) | a << 8 | b << 16 | c << 24;
}

void FM1Processor::forwardMidi(const juce::MidiMessage &m)
{
    const uint8_t *d = m.getRawData();
    const int n = m.getRawDataSize();
    auto put = [this](uint32_t p) {
        if (inW_ - inR_ < kInRing)
            inRing_[inW_++ % kInRing] = p;       // (full: dropped)
    };
    if (n <= 0)
        return;
    const uint8_t st = d[0];
    if (st == 0xF0) {                            // SysEx: 3-byte packets, the last one CIN 5 / 6 / 7
        int i = 0;
        while (n - i > 3) {
            put(usbPacket(4, d[i], d[i + 1], d[i + 2]));
            i += 3;
        }
        const int r = n - i;
        put(usbPacket(r == 1 ? 5u : r == 2 ? 6u : 7u, d[i], r > 1 ? d[i + 1] : 0u, r > 2 ? d[i + 2] : 0u));
    } else if (st >= 0xF8) {
        put(usbPacket(0xF, st, 0, 0));
    } else if (st >= 0xF0) {                     // system common
        if (st == 0xF1 || st == 0xF3)
            put(usbPacket(2, st, n > 1 ? d[1] : 0u, 0));
        else if (st == 0xF2)
            put(usbPacket(3, st, n > 1 ? d[1] : 0u, n > 2 ? d[2] : 0u));
        else if (st == 0xF6)
            put(usbPacket(5, st, 0, 0));
    } else if (st >= 0x80) {
        put(usbPacket(st >> 4, st, n > 1 ? d[1] : 0u, n > 2 ? d[2] : 0u));
    }
    flushMidiIn();
}

void FM1Processor::flushMidiIn()
{
    while (inR_ != inW_ && dev_->midi_in(inRing_[inR_ % kInRing]))
        inR_++;                                  // (0: no room in the core: retried after the next render)
}

void FM1Processor::decodePacket(uint32_t p, int pos)
{
    static const uint8_t LEN[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    const uint32_t cin = p & 15u;
    const uint8_t b[3] = {(uint8_t)(p >> 8), (uint8_t)(p >> 16), (uint8_t)(p >> 24)};
    auto sx = [this](const uint8_t *s, int k) {
        for (int i = 0; i < k; i++) {
            if (sysexOut_.size() >= 4096)
                sysexOutOverflow_ = true;
            else
                sysexOut_.push_back(s[i]);
        }
    };
    auto sxEnd = [this, pos]() {
        if (!sysexOutOverflow_ && sysexOut_.size() >= 2 && sysexOut_.front() == 0xF0 && sysexOut_.back() == 0xF7)
            midiOut_.addEvent(sysexOut_.data(), (int)sysexOut_.size(), pos);
        sysexOut_.clear();
        sysexOutOverflow_ = false;
    };
    switch (cin) {
    case 0x4:                                    // SysEx starts or continues
        if (b[0] == 0xF0)
            sysexOut_.clear(), sysexOutOverflow_ = false;
        sx(b, 3);
        return;
    case 0x5:                                    // a single-byte system common, or SysEx ends with 1 byte
        if (b[0] == 0xF7 || !sysexOut_.empty()) {
            sx(b, 1);
            sxEnd();
        } else if (b[0] >= 0xF0) {
            midiOut_.addEvent(b, 1, pos);
        }
        return;
    case 0x6:
        sx(b, 2);
        sxEnd();
        return;
    case 0x7:
        sx(b, 3);
        sxEnd();
        return;
    default:
        break;
    }
    const int len = LEN[cin];
    if (!len || !(b[0] & 0x80))
        return;                                  // (reserved CINs, or no status byte: dropped)
    if (cin >= 0x8 && (b[0] >> 4) != cin)
        return;                                  // (a channel CIN must match its status)
    if (cin >= 0x8 && ((len > 1 && (b[1] & 0x80)) || (len > 2 && (b[2] & 0x80))))
        return;
    midiOut_.addEvent(b, len, pos);
}

void FM1Processor::drainMidiOut(int pos)
{
    uint32_t p;
    while (dev_->midi_out_take(&p))
        decodePacket(p, pos);
}

void FM1Processor::renderSegment(float *L, float *R, int from, int to)
{
    int k = from;
    while (k < to) {
        const int n = R ? to - k : std::min(to - k, (int)scratch_.size());
        dev_->render(L + k, R ? R + k : scratch_.data(), (uint32_t)n, sampleRate_);
        if (!R)
            for (int i = 0; i < n; i++)
                L[k + i] = 0.5f * (L[k + i] + scratch_[(size_t)i]);
        k += n;
    }
    flushMidiIn();
    drainMidiOut(std::max(from, to - 1));
}

void FM1Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    midiOut_.clear();
    const juce::ScopedTryLock l(devLock_);
    if (!l.isLocked() || !dev_ || !dev_->booted() || n <= 0 || buffer.getNumChannels() < 1) {
        buffer.clear();
        midi.clear();
        return;
    }
    applyTier1();
    applyHostParameters();

    float *L = buffer.getWritePointer(0);
    float *R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    if (!R && scratch_.empty()) {
        buffer.clear();
        midi.clear();
        return;
    }
    int pos = 0;
    for (const auto meta : midi) {               // MidiBuffer iterates in sample order
        const int at = juce::jlimit(0, n, meta.samplePosition);
        if (at > pos) {
            renderSegment(L, R, pos, at);
            pos = at;
        }
        handleMidi(meta.getMessage());
    }
    if (pos < n)
        renderSegment(L, R, pos, n);
    for (int c = 2; c < buffer.getNumChannels(); c++)
        buffer.clear(c, 0, n);

    if (dev_->halted()) {
        if (!halted_.exchange(true)) {
            haltCode_.store(dev_->core()->halted());
            triggerAsyncUpdate();
        }
        buffer.clear();
    }
    snapshotPanel();
    midi.swapWith(midiOut_);
}

// =============================================================== factory ===
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new FM1Processor(); }
