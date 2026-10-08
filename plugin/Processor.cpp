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
    createTier2Parameters();                     // first: the Roto-Control's first page is the map's live page
    createTier1Parameters();
    {
        const juce::ScopedLock l(devLock_);
        bindTier2Locked();
    }
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
    // the emulator's power-on value: emu_fw_init sets emu_hal.master = 724 when the host left it 0
    master_ = new juce::AudioParameterInt(juce::ParameterID{"master", 1}, "MASTER", 0, 1023, 724);
    addParameter(master_);
}

// ============================================================== Tier 2 ===
void FM1Processor::createTier2Parameters()
{
    for (int i = 0; i < kTier2Slots; i++) {
        auto *p = new Tier2Parameter(i);
        t2_[(size_t)i] = p;
        addParameter(p);
    }
}

void FM1Processor::bindTier2Locked()
{
    if (!t2_[0])
        return;                                  // (the constructor: the slots come after the first load)
    const fm1core_t *c = loaded_ ? loaded_->core() : nullptr;
    const uint32_t n = c && c->params ? std::min<uint32_t>(c->nparams, (uint32_t)kTier2Slots) : 0u;
    int meta = 0;
    for (int i = 0; i < kTier2Slots; i++) {
        const fm1param_t *e = (uint32_t)i < n ? &c->params[i] : nullptr;
        t2_[(size_t)i]->bind(e, e && (e->flags & FM1P_META) ? meta++ : -1);
    }
    t2Bound_ = (int)n;
    t2Gen_.fetch_add(1);
    t2Reported_.fill(0);
    t2Valid_.fill(false);
    t2Holdoff_.fill(0);
    t2EpochValid_ = false;
    t2Sweep_.store(true);
    if (c && c->nparams > (uint32_t)kTier2Slots)
        message_ = juce::String(c->name) + ": " + juce::String(c->nparams) + " firmware parameters, " +
                   juce::String(kTier2Slots) + " host slots (build with -DFM1_TIER2_SLOTS)";
}

void FM1Processor::unbindTier2Locked()
{
    for (Tier2Parameter *p : t2_)
        if (p)
            p->unbind();
    t2Bound_ = 0;
    t2Gen_.fetch_add(1);
}

bool FM1Processor::pushTier2(int slot, int32_t v, uint8_t kind)
{
    int s1, n1, s2, n2;
    t2Fifo_.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 + n2 < 1)
        return false;
    t2Buf_[(size_t)(n1 ? s1 : s2)] = T2Msg{(int16_t)slot, kind, t2Gen_.load(std::memory_order_relaxed), v};
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
}

void FM1Processor::readBackParameters()
{
    if (!t2Bound_ || !dev_ || dev_->halted())
        return;
    const fm1core_t *c = dev_->core();
    // the meta slots: the perform mode changed (or the first frame after a bind): their targets
    if (c->param_epoch) {
        const uint32_t e = c->param_epoch();
        if (!t2EpochValid_ || e != t2Epoch_) {
            bool ok = true;
            for (int i = 0; i < t2Bound_; i++) {
                Tier2Parameter *p = t2_[(size_t)i];
                const fm1param_t *own = p->entry();
                if (!p->isMeta() || !own || !own->target)
                    continue;
                const int32_t t = own->target();
                p->retarget(t >= 0 && (uint32_t)t < c->nparams ? &c->params[t] : own);
                const int32_t v = own->get();
                if (pushTier2(i, v, kT2Relabel)) {
                    t2Reported_[(size_t)i] = v;
                    t2Valid_[(size_t)i] = true;
                } else {
                    ok = false;
                }
            }
            t2Epoch_ = e;
            t2EpochValid_ = ok;                  // (the FIFO full: retried next frame)
        }
    }
    const bool sweep = t2Sweep_.load();
    bool swept = true;
    for (int i = 0; i < t2Bound_; i++) {
        const fm1param_t *own = t2_[(size_t)i]->entry();
        if (!own)
            continue;
        if (!sweep && t2Holdoff_[(size_t)i]) {
            t2Holdoff_[(size_t)i]--;
            continue;
        }
        const int32_t v = own->get();
        if (!sweep && t2Valid_[(size_t)i] && v == t2Reported_[(size_t)i])
            continue;
        if (pushTier2(i, v, sweep ? kT2Sweep : kT2Change)) {
            t2Reported_[(size_t)i] = v;
            t2Valid_[(size_t)i] = true;
        } else {
            swept = false;                       // (full: what was not reported is retried next frame)
        }
    }
    if (sweep && swept)
        t2Sweep_.store(false);
}

void FM1Processor::drainTier2()
{
    struct Acc {
        bool any = false, gesture = true;
        int32_t v = 0;
    };
    std::array<Acc, FM1_TIER2_SLOTS> acc{};
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
                if (m.gen != gen || m.slot < 0 || m.slot >= kTier2Slots)
                    continue;                    // (from before a rebind)
                Acc &a = acc[(size_t)m.slot];
                a.any = any = true;
                a.v = m.value;                   // coalesced: the latest
                if (m.kind != kT2Change)
                    a.gesture = false;
                relabel = relabel || m.kind == kT2Relabel;
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
    for (int i = 0; i < kTier2Slots; i++)
        if (acc[(size_t)i].any && acc[(size_t)i].gesture)
            t2_[(size_t)i]->beginChangeGesture();
    for (int i = 0; i < kTier2Slots; i++)
        if (acc[(size_t)i].any)
            t2_[(size_t)i]->setFromFirmware(acc[(size_t)i].v, false);
    for (int i = 0; i < kTier2Slots; i++)
        if (acc[(size_t)i].any && acc[(size_t)i].gesture)
            t2_[(size_t)i]->endChangeGesture();
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
        if (frame)
            readBackParameters();
    });
    dev_->boot_from(bytes.empty() ? nullptr : bytes.data(), (uint32_t)bytes.size());
    resetAudioState();
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
FM1Theme FM1Processor::getTheme() const
{
    const juce::SpinLock::ScopedLockType l(themeLock_);
    return theme_;
}
void FM1Processor::setTheme(const FM1Theme &t)
{
    const juce::SpinLock::ScopedLockType l(themeLock_);
    theme_ = t;
}

// ---------------------------------------------------------------- state ---
// A ValueTree "FM1VST" (binary): stateVersion, core, coreVersion, transpose, midiNotesPlayKeys, keyNotesToFirmware,
// master, preset, a
// child "theme" (name base membrane bed knob) and "flash": the image gzip-compressed (a MemoryBlock property; absent
// when the instance never had one, i.e. a fresh flash).
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
    const FM1Theme th = getTheme();
    juce::ValueTree tt("theme");
    tt.setProperty("name", th.name, nullptr);
    tt.setProperty("base", th.base, nullptr);
    tt.setProperty("membrane", th.membrane, nullptr);
    tt.setProperty("bed", th.bed, nullptr);
    tt.setProperty("knob", th.knob, nullptr);
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
    if (tt.isValid()) {
        FM1Theme th;
        th.name = tt.getProperty("name", th.name).toString();
        th.base = tt.getProperty("base", th.base).toString();
        th.membrane = tt.getProperty("membrane", th.membrane).toString();
        th.bed = tt.getProperty("bed", th.bed).toString();
        th.knob = tt.getProperty("knob", th.knob).toString();
        setTheme(th);
    }
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
    const uint32_t m = paramKeys_ | midiKeys_;
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
    if (b != appliedButtons_) {
        dev_->buttons(b);
        appliedButtons_ = b;
    }
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
    midi.swapWith(midiOut_);
}

// =============================================================== factory ===
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new FM1Processor(); }
