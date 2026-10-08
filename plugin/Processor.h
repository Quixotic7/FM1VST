// SPDX-License-Identifier: GPL-3.0-only
// The plugin's AudioProcessor (FM1-VST-PLAN.md 4.1-4.7, phase 2): a loaded core run by the engine's Device on the
// audio thread, the Tier 1 panel parameters, MIDI in / out, the flash image as the plugin state, presets per
// firmware and the firmware switch.
//
// THREADS
//   - The audio thread owns the Device: processBlock runs the device clock (Device::render) and is the only caller
//     of the core while it plays. Everything else that touches the core (getStateInformation's flash copy, presets,
//     power cycles, core switches) runs on the message thread while holding devLock_; processBlock only TRIES the
//     lock (a block that finds it taken is silent). Power cycles and core switches additionally run inside
//     suspendProcessing(true) .. (false), so the host stops calling processBlock while the Device is replaced.
//   - Host parameter values (Tier 1) are atomics (JUCE's parameters) read once at the start of each block.
//   - The non-automatable settings the audio thread needs (transpose, "MIDI notes play keys") are atomics.
//   - The panel GUI (PanelComponent): its held keys / buttons, encoder detents and fine turns are atomics taken at
//     block start (panelKey ..); what it shows (LCD, LEDs, held state, MASTER) is a PanelView snapshot the audio
//     thread writes after a block in which it changed, under viewLock_, which the audio thread only tries.
//
// THE HOST PARAMETERS (plan 4.5), created once, in this order (hosts expect a fixed list: a core switch rebinds and
// relabels, updateHostDisplay(parameterInfoChanged), it never adds or removes):
//   1. the eight physical knobs, panel order (a Roto-Control's first page): knob_master (MASTER, the pot, 0..1023),
//      knob_select knob_presets knob_algo knob_1 .. knob_4 (KnobParameter, Tier2Parameter.h): each is whatever that
//      knob does on the screen showing now (the core's knob_target), or a relative control where it has no value;
//   2. the Tier 2 slots t2_00 .. (FM1_TIER2_SLOTS): the map's visible entries (FM1P_HIDDEN ones are knob targets
//      only), so a specific parameter can still be mapped directly;
//   3. Tier 1: btn_fx .. btn_octup, key_00 .. key_26.
// Binding: bindTier2Locked() after every module load (the knobs relative until the first read-back evaluates them),
// unbindTier2Locked() before every unload (no slot may call into an unloaded image).
//   host -> firmware   a host change (setValue, any thread) stores the target in the slot (atomic, pending);
//                      applyHostParameters() (audio thread, devLock_ held, block start, after Tier 1) calls the map's
//                      set() for each pending slot (a bound knob: its target's) and records the value as the host's (so
//                      the read-back does not echo it; kHoldoffFrames frames of read-back are skipped for the slot). A
//                      relative knob's change becomes detents (KnobParameter::takeDetents, knobDetents() per full
//                      travel) sent to the device's encoder (Device::enc, as a panel turn); kRecentreMs of device time
//                      after the last one the read-back springs it back to 0.5 (no detents, no gesture).
//   firmware -> host   readBackParameters() (audio thread, devLock_ held, once per device UI frame: Device's
//                      between hook with frame == true, i.e. every 15 ms of device time right before frame()) calls
//                      get() on every bound slot and knob and pushes what differs from the last reported value into a
//                      lock-free FIFO (juce::AbstractFifo, slot + value); the message thread's timer (30 Hz,
//                      drainTier2) coalesces it (one update per slot per drain) and calls setValueNotifyingHost,
//                      inside beginChangeGesture .. endChangeGesture for the burst (a knob turned on the device is a
//                      touch to Live: it moves the Roto-Control's motor and records automation when armed). The
//                      feedback never runs inside setValue (plan 4.5's automation caveat).
//   the epoch          when the core's param_epoch() moved (a screen, layer, page, perform mode, part or engine
//                      changed), the read-back re-reads knob_target() for the seven knobs and target() for the meta
//                      slots, retargets the ones whose target (entry, range, name) changed, and the drain announces
//                      parameterInfoChanged before pushing their new values (without a gesture).
//   the full sweep     after every power-on (boot, preset load, state restore, flash reset, core switch) the first
//                      read-back reports every bound slot, without gestures (a preset load is not a touch). Tier 2
//                      values are not stored in the plugin state: the flash image is the truth.
//
// FILES (everything under home() = <FM1EMU_HOME or ~/Library/Application Support>/fm1emu)
//   cores/                     user-installed *.fm1core modules (the bundled ones: <bundle>/Contents/Resources/cores)
//   <core-id>/flash.bin        the working flash: what a new instance of that core boots from; written when the
//                              instance is released or destroyed, before a core switch, and on an explicit save
//   <core-id>/presets/NAME.fm1preset    a named snapshot of the whole flash (format below)
//   <core-id>/backups/DATE.fm1preset    written before a preset load or a flash reset (the newest 50 are kept)
//   themes/NAME.json, settings.json     custom colour themes and the default theme (Theme.h: ThemeStore)
// The per-instance copies of the module go to <FM1EMU_HOME>/Caches/fm1emu/instances (else ~/Library/Caches/...).
//
// THE .fm1preset FORMAT: one line of JSON, a '\n', then the flash image gzip-compressed (RFC 1952: `tail -n +2 F |
// gunzip` gives the raw image):
//   {"fm1preset":1,"core":"choralroot","version":"0.1","name":"Warm Pads","date":"2026-10-07T12:00:00.000+02:00",
//    "size":1048576}
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Theme.h"
#include "Tier2Parameter.h"
#include "cores.h"
#include "device.h"

// A momentary panel control as a host boolean (the value IS the held state). Its name can change at run time (a
// core switch relabels the buttons from the new core's descriptor).
class PanelBoolParameter : public juce::AudioParameterBool {
public:
    PanelBoolParameter(const juce::String &id, const juce::String &name);
    juce::String getName(int maximumStringLength) const override;
    void setDisplayName(const juce::String &n);

private:
    mutable juce::SpinLock nameLock_;
    juce::String displayName_;
};

// What the panel shows, snapshotted on the audio thread at the end of a block when it changed (the GUI never reads
// the core's hal directly: it copies this under a spin lock the audio thread only ever tries).
struct PanelView {
    uint32_t seq = 0;                                 // moves with every snapshot
    bool running = false;                             // a booted, not halted device
    uint32_t lcdWrites = 0;                           // hal->lcd_writes when lcd was copied
    uint32_t lcdSeq = 0;                              // moves with every copy of lcd (a power cycle restarts lcd_writes)
    std::array<uint16_t, EMU_LCD_W * EMU_LCD_H> lcd{};   // RGB565 big-endian, as sent
    std::array<uint8_t, EMU_NKEY> keyLed{};           // 0 off, 1 dim, 2 lit
    std::array<uint8_t, EMU_NB> btnLed{};
    uint8_t playGreen = 0;                            // 0 / 2
    uint32_t keys = 0, buttons = 0;                   // what the device holds now (all sources merged; label bits)
    int master = 724;                                 // hal->master, 0..1023
};

class FM1Processor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::Timer {
public:
    FM1Processor();
    ~FM1Processor() override;

    // ---- juce::AudioProcessor ----
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "FM1VST"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String &) override {}

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    // ---- cores (message thread) ----
    static juce::File home();                        // <FM1EMU_HOME or ~/Library/Application Support>/fm1emu
    static juce::File userCoresDir();                // home()/cores
    // <bundle>/Contents/Resources/cores for an executable inside a bundle (<bundle>/Contents/MacOS/<exe>), else {}
    static juce::File bundledCoresDirFor(const juce::File &executable);
    // the running bundle's cores folder if it exists, else the build's FM1_CORES_DIR (tests, a dev build)
    static juce::File bundledCoresDir();
    std::vector<CoreInfo> listCores() const;         // bundled + user (a user module with the same id wins)
    bool switchCore(const juce::String &id);
    struct CoreStatus {
        bool loaded = false;
        CoreInfo info;                               // the loaded core (id / name / version / path)
        juce::String requestedId;                    // what the state or the user asked for
        bool halted = false;
        int haltCode = 0;
        juce::String message;                        // the last error or warning, for the editor
    };
    CoreStatus getCoreInfo() const;

    // ---- presets of the loaded core (message thread) ----
    juce::File coreDir() const;                      // home()/<core-id>
    juce::File presetsDir() const { return coreDir().getChildFile("presets"); }
    juce::File backupsDir() const { return coreDir().getChildFile("backups"); }
    juce::File workingFlashFile() const { return coreDir().getChildFile("flash.bin"); }
    juce::StringArray listPresets() const;           // names, sorted case-insensitively
    bool savePreset(const juce::String &name);       // overwrites; also writes the working flash
    bool loadPreset(const juce::String &name);       // backup, then a power cycle on the preset's flash
    bool deletePreset(const juce::String &name);
    bool renamePreset(const juce::String &from, const juce::String &to);
    bool resetFlash();                               // backup, then a power cycle on a fresh (erased) flash
    bool exportPreset(const juce::File &file);       // the current flash as a .fm1preset file
    juce::String importPreset(const juce::File &file);   // copied into presets/ (name from its header); "" on failure
    juce::String currentPresetName() const { return currentPreset_; }
    bool powerCycle();                               // reboot on the current flash (also after a firmware halt)
    bool saveWorkingFlash();

    // ---- settings (not parameters; stored in the state) ----
    int getTranspose() const { return transpose_.load(); }       // octaves, -2..2: MIDI note n plays key
    void setTranspose(int octaves);                              //   n - 53 + 12 * transpose
    bool getMidiNotesPlayKeys() const { return notesPlayKeys_.load(); }
    void setMidiNotesPlayKeys(bool on) { notesPlayKeys_.store(on); }
    // a note that pressed a key is ALSO handed to the firmware's MIDI in (default off: ChoralRoot's CHORD channel
    // is 1, so the same note would also sound raw on the chord part, on top of the chord the key plays). Every
    // other message (and every note when "MIDI notes play keys" is off, or out of the key range) always goes in.
    bool getKeyNotesToFirmware() const { return keyNotesToFirmware_.load(); }
    void setKeyNotesToFirmware(bool on) { keyNotesToFirmware_.store(on); }
    // the panel's colour theme (Theme.h): a new instance starts with the user's default theme (ThemeStore); the
    // state stores the theme itself (name and colours), so a set reopens looking the same even when its custom
    // theme file is gone. themeSerial() moves on every setTheme (the editor follows a state restore with it).
    Theme getTheme() const;
    void setTheme(const Theme &t);
    uint32_t themeSerial() const { return themeSerial_.load(); }
    // the editor's last size and big-LCD view, per instance (stored in the state; 0 x 0: never opened)
    juce::Point<int> getEditorSize() const { return {editorW_.load(), editorH_.load()}; }
    void setEditorSize(int w, int h) { editorW_.store(w), editorH_.store(h); }
    bool getBigLcd() const { return bigLcd_.load(); }
    void setBigLcd(bool on) { bigLcd_.store(on); }
    // a relative knob's detents per full travel of the host value (the Roto-Control's 0..1; stored in the state)
    int getKnobDetents() const { return knobDetents_.load(); }
    void setKnobDetents(int n) { knobDetents_.store(juce::jlimit(4, 256, n)); }
    // the loaded core's labels for the 14 buttons (EMU_B_* order; the panel's own printed label when the core has
    // none)
    juce::StringArray buttonNames() const;

    // ---- the panel GUI's input (message thread). A separate "held" source, merged on the audio thread with the
    // host parameters and MIDI (keys: param | MIDI | panel; buttons: param | panel | fine-turn GLO), as emu.c merges
    // SRC_KEY / SRC_MOUSE / SRC_LATCH: the panel never moves the btn_ / key_ host parameters. A press that is
    // released before the next block still reaches the firmware as a tap.
    void panelKey(int key, bool down);
    void panelButton(int label, bool down);
    // turn an encoder (EMU_E_* role) by detents, + clockwise. MASTER is the host's "master" parameter: the panel
    // moves it (16 of 1023 per detent, inside a gesture) as any plugin GUI moves a continuous parameter.
    void panelEnc(int role, int detents);
    void panelMaster(int value);
    // Shift + Up / Down (emu.c fine_turn): GLO held around the detent: GLO goes down at the next block, the detent
    // arrives before the next UI frame, GLO up one frame later (all on the device clock)
    void panelFineTurn(int role, int detents);
    void panelReleaseAll();                          // every panel key and button up
    // the latest snapshot: copies it into out when out.seq differs (the LCD only when out.lcdSeq differs);
    // false when nothing changed
    bool getPanelView(PanelView &out) const;

    // the current flash image (synced first when the device runs); empty = a fresh flash
    std::vector<uint8_t> currentFlash();

    // ---- for tests (call only between processBlock calls, on the thread that calls them) ----
    Device *deviceForTest() { return dev_.get(); }
    PanelBoolParameter *buttonParam(int label) { return btn_[(size_t)label]; }
    PanelBoolParameter *keyParam(int key) { return key_[(size_t)key]; }
    juce::AudioParameterInt *masterParam() { return master_; }
    Tier2Parameter *tier2Param(int slot) { return slot >= 0 && slot < kTier2Slots ? t2_[(size_t)slot] : nullptr; }
    int tier2Bound() const { return t2Bound_; }      // slots bound to the loaded core's map (its visible entries)
    // the knob of an EMU_E_* role (SELECT .. KNOB4; MASTER: masterParam), and the detents its relative turns sent
    KnobParameter *knobParam(int role) { return role >= 0 && role < kKnobs ? knob_[(size_t)role] : nullptr; }
    int32_t knobDetentsSent(int role) const { return role >= 0 && role < kKnobs ? knobSent_[(size_t)role].load() : 0; }
    // the feedback drain (the timer's work; message thread): tests call it instead of running the message loop
    void drainTier2();

    static constexpr int kTier2Slots = FM1_TIER2_SLOTS;
    static constexpr int kKnobs = EMU_NE - 1;        // the KnobParameters (MASTER is master_)
    static constexpr int kHoldoffFrames = 2;         // read-backs skipped after a host write (30 ms of device time)

    static constexpr int kNoteBase = 53;             // MIDI note of key 0 (F3)
    static const char *const kPanelLabels[EMU_NB];   // the printed labels: FX SEL ENV .. OCT+
    static const char *const kButtonIds[EMU_NB];     // btn_fx .. btn_octup

private:
    // the Tier 2 seam (see the top)
    void applyHostParameters();
    void readBackParameters();

    void createKnobParameters();
    void createTier1Parameters();
    void createTier2Parameters();
    void bindTier2Locked();                          // devLock_ held: the slots onto loaded_'s map
    void unbindTier2Locked();
    bool pushTier2(int slot, int32_t v, uint8_t kind);   // audio thread -> the FIFO; false: full
    void timerCallback() override { drainTier2(); }
    void relabelParameters();
    juce::String buttonName(int i) const;

    // with devLock_ held
    bool loadCoreLocked(const juce::String &id);     // load (not boot) the module for id
    void bootLocked();                               // power on from pendingFlash_ / the working flash / fresh
    void teardownLocked();                           // the device and the module gone
    std::vector<uint8_t> currentFlashLocked();
    bool powerCycleLocked(std::optional<std::vector<uint8_t>> bytes);
    bool saveWorkingFlashLocked();
    bool switchCoreLocked(const juce::String &id, std::optional<std::vector<uint8_t>> bytes);
    bool backupLocked(const juce::String &why);

    // the audio thread
    void applyTier1();
    void pushKeys();
    void handleMidi(const juce::MidiMessage &m);
    void forwardMidi(const juce::MidiMessage &m);
    void flushMidiIn();
    void drainMidiOut(int samplePos);
    void decodePacket(uint32_t pkt, int samplePos);
    void renderSegment(float *L, float *R, int from, int to);
    void resetAudioState();

    void handleAsyncUpdate() override;               // the firmware rebooted itself (exit(0)): power on again

    // the core and its device
    mutable juce::CriticalSection devLock_;
    std::unique_ptr<LoadedCore> loaded_;
    std::unique_ptr<Device> dev_;
    CoreInfo coreInfo_;
    juce::String coreId_ = "choralroot", requestedCore_ = "choralroot";
    std::optional<std::vector<uint8_t>> pendingFlash_;   // the next boot's flash (state, preset, reset)
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    juce::String message_, currentPreset_;
    std::atomic<bool> halted_{false};
    std::atomic<int> haltCode_{0};

    // parameters (owned by the AudioProcessor)
    std::array<PanelBoolParameter *, EMU_NB> btn_{};
    std::array<PanelBoolParameter *, EMU_NKEY> key_{};
    juce::AudioParameterInt *master_ = nullptr;
    std::array<Tier2Parameter *, FM1_TIER2_SLOTS> t2_{};
    std::array<KnobParameter *, EMU_NE - 1> knob_{};    // by EMU_E_* role
    int t2Bound_ = 0;                                    // (written with devLock_ held and the audio stopped)

    // Tier 2 feedback: audio-thread state (devLock_ held) and the FIFO to the message thread
    // kKnobNorm: a knob turned relative (a relabel), kKnobCentre: re-centred; both: its value 0.5, unless the host
    // wrote it since (value: the KnobParameter's hostSeq when pushed). FIFO slots: 0 .. kTier2Slots - 1 the Tier 2
    // slots, then the knobs by role.
    enum : uint8_t { kT2Change, kT2Sweep, kT2Relabel, kKnobNorm, kKnobCentre };
    struct T2Msg {
        int16_t slot;
        uint8_t kind;
        uint32_t gen;                                    // t2Gen_ when pushed: a rebind drops older messages
        int32_t value;
    };
    static constexpr int kT2Fifo = 1024;
    juce::AbstractFifo t2Fifo_{kT2Fifo};
    std::array<T2Msg, kT2Fifo> t2Buf_{};
    std::atomic<uint32_t> t2Gen_{0};
    std::atomic<bool> t2Sweep_{true};                    // the next read-back reports every slot
    std::array<int32_t, FM1_TIER2_SLOTS> t2Reported_{};  // the value the host was last told (or wrote)
    std::array<bool, FM1_TIER2_SLOTS> t2Valid_{};
    std::array<uint8_t, FM1_TIER2_SLOTS> t2Holdoff_{};
    uint32_t t2Epoch_ = 0;
    bool t2EpochValid_ = false;
    // the knobs (audio thread)
    std::array<int32_t, EMU_NE - 1> knobReported_{};
    std::array<bool, EMU_NE - 1> knobValid_{};
    std::array<uint8_t, EMU_NE - 1> knobHoldoff_{};
    std::array<uint32_t, EMU_NE - 1> knobSig_{}, knobLastTurn_{};   // the target's signature; device ms of a turn
    std::array<bool, EMU_NE - 1> knobSigValid_{};
    std::array<std::atomic<int32_t>, EMU_NE - 1> knobSent_{};       // detents sent (tests)
    std::atomic<int> knobDetents_{KnobParameter::kDefaultDetents};
    void retargetKnobs();

    // settings
    std::atomic<int> transpose_{0};
    std::atomic<bool> notesPlayKeys_{true};
    std::atomic<bool> keyNotesToFirmware_{false};
    mutable juce::SpinLock themeLock_;
    Theme theme_;
    std::atomic<uint32_t> themeSerial_{0};
    std::atomic<int> editorW_{0}, editorH_{0};
    std::atomic<bool> bigLcd_{false};

    // the panel GUI's input (written by the message thread, taken by the audio thread at block start)
    std::atomic<uint32_t> panelKeys_{0}, panelButtons_{0}, panelKeyTaps_{0}, panelBtnTaps_{0};
    std::array<std::atomic<int32_t>, EMU_NE> panelEnc_{};
    std::atomic<int32_t> fineReq_{0};
    std::atomic<int> fineRole_{EMU_E_SELECT};
    // the audio thread's side
    uint32_t paramButtons_ = 0, panelKeysCur_ = 0, panelButtonsCur_ = 0;
    int fineStage_ = 0, fineRoleCur_ = EMU_E_SELECT;  // emu.c fine_frame: 0 idle, 1 GLO down, 2 stepped
    int32_t fineSteps_ = 0;
    bool fineGlo_ = false;
    void applyButtons();
    void takeFineRequest();
    void fineFrame();
    // the panel snapshot
    void snapshotPanel();
    mutable juce::SpinLock viewLock_;
    PanelView view_;
    uint8_t viewLeds_[2 * EMU_NCOL] = {};
    uint32_t viewKeys_ = 0xFFFFFFFFu, viewButtons_ = 0xFFFFFFFFu;
    int viewMaster_ = -1;
    bool viewRunning_ = false, viewDirty_ = true;

    // audio-thread state
    uint32_t paramKeys_ = 0, midiKeys_ = 0, appliedKeys_ = 0xFFFFFFFFu, appliedButtons_ = 0xFFFFFFFFu;
    int appliedMaster_ = -1;
    bool lastNotesPlayKeys_ = true;
    std::array<std::array<int8_t, 128>, 16> noteKey_{};  // the key a held note pressed, -1 none
    std::array<uint8_t, EMU_NKEY> keyCount_{};           // MIDI notes holding each key
    static constexpr uint32_t kInRing = 1024;
    std::array<uint32_t, kInRing> inRing_{};             // USB-MIDI packets waiting for room in the core
    uint32_t inW_ = 0, inR_ = 0;
    std::vector<uint8_t> sysexOut_;                      // reassembling a SysEx from the core (CIN 4..7)
    bool sysexOutOverflow_ = false;
    juce::MidiBuffer midiOut_;
    std::vector<float> scratch_;                         // the right channel when the host gives only one
};
