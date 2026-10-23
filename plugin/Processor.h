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
//   - Remote FM-1 (RemoteLink.h): a real FM-1 on the RemoteFM1 firmware as a second panel. Its session runs on the
//     link's own thread; its panel events reach the audio thread through a lock-free queue (drained at block start,
//     devLock_ held: applyRemote), the LCD + LEDs go back through a lock-free triple buffer (publishRemote, at block
//     end, at most every 50 device ms and only when they changed). See "REMOTE FM-1" below.
//
// THE HOST PARAMETERS (plan 4.5), created once, in this order (hosts expect a fixed list: a core switch rebinds and
// relabels, updateHostDisplay(parameterInfoChanged), it never adds or removes):
//   1. the eight physical knobs, panel order (a Roto-Control's first page), names fixed ("Knob Master" .. "Knob 4"):
//      knob_master (the pot, 0..1023), knob_select knob_presets knob_algo knob_1 .. knob_4 (KnobParameter,
//      Tier2Parameter.h): each is whatever that knob does on the screen showing now (the core's knob_target; its
//      value text "Voicing: -1"), or a relative control where it has no value; their host info never changes;
//   2. the Tier 2 slots t2_00 .. (FM1_TIER2_SLOTS, 0 by default: none; 79 the opt-in): the map's visible entries
//      (FM1P_HIDDEN ones are knob targets only), so a specific parameter can be mapped directly;
//   3. Tier 1: btn_fx .. btn_octup, key_00 .. key_26. The button parameters' edge semantics is a setting (Button
//      params, below): Tap (the default: a rising edge is one press of getButtonTapMs() device ms, a falling edge
//      nothing: a controller's toggle button and a momentary one both give one tap per press), Hold (the value IS
//      the held state) or Toggle-hold (a rising edge toggles held). The key parameters are always value = held.
// ONLY THE KNOBS REPORT (the slots off): a firmware change reports only on the knob(s) whose current target's value
// changed, inside a gesture only for the knob turned on the device (Device::enc_total minus the host's own relative
// detents, kOwnTurnMs); the panel never moves the btn_ / key_ parameters.
// Binding: bindTier2Locked() after every module load (the knobs relative until the first read-back evaluates them),
// unbindTier2Locked() before every unload (no slot may call into an unloaded image).
//   host -> firmware   a host change (setValue, any thread) stores the target in the slot (atomic, pending);
//                      applyHostParameters() (audio thread, devLock_ held, block start, after Tier 1) calls the map's
//                      set() for each pending slot (a bound knob: its target's) and records the value as the host's (so
//                      the read-back does not echo it; kHoldoffFrames frames of read-back are skipped for the slot). A
//                      relative knob's change becomes detents (KnobParameter::takeDetents, knobDetents() per full
//                      travel) sent to the device's encoder (Device::enc, as a panel turn); kRecentreMs of device time
//                      after the last one the read-back springs it back to 0.5 (no detents, no gesture).
//                      Encoders: Stepped (setEncoderMode): every write of the seven encoder knobs, bound or relative,
//                      is clicks (KnobParameter::takeSteps) sent as detents (Device::enc); the read-back pushes the
//                      target's new value back, and the next click counts from it.
//   firmware -> host   readBackParameters() (audio thread, devLock_ held, once per device UI frame: Device's
//                      between hook with frame == true, i.e. every 15 ms of device time right before frame()) calls
//                      get() on every bound slot and knob and pushes what differs from the last reported value into a
//                      lock-free FIFO (juce::AbstractFifo, slot + value); the message thread's timer (30 Hz,
//                      drainTier2) coalesces it (one update per slot per drain), drops what the host already has, and
//                      calls setValueNotifyingHost: a Tier 2 slot inside beginChangeGesture .. endChangeGesture; a
//                      knob inside one only when it was turned on the device (a touch to Live: the Roto-Control's
//                      motor, Configure, automation when armed), those first; a relative knob turned on the device
//                      is nudged (0.5 + detents / 24, a gesture) and springs back. The feedback never runs inside
//                      setValue (plan 4.5's automation caveat).
//   the epoch          when the core's param_epoch() moved (a screen, layer, page, perform mode, part or engine
//                      changed), the read-back re-reads knob_target() for the seven knobs and target() for the meta
//                      slots, retargets the ones whose target (entry, range, name) changed, and the drain pushes their
//                      new values (without a gesture); parameterInfoChanged only for a Tier 2 meta slot (a knob's
//                      host info never changes).
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
// REMOTE FM-1 (setRemoteEnabled; the toggle is in the state): the unit's input merges with the others exactly as the
// panel GUI's does: its held keys are one more source in the OR of key masks (param | MIDI | panel | remote), its
// held buttons one more in the button OR (param | panel | remote | fine-turn GLO), a press released within a block
// still taps. Its encoders are Device::enc detents (the matrix encoder through panel->enc_id / enc_dir): a device turn
// for the knob logic (enc_total moves, the host's own detents do not), so the host parameter feedback and a
// Roto-Control's motor follow a physical turn exactly as a panel turn. MASTER (the unit's absolute pot) sets the
// device's pot at once and the "Knob Master" parameter follows from the message thread (a gesture, as the panel's
// MASTER); until the parameter has caught up, the parameter's older value is not applied back. "Ignore the FM-1's own
// MIDI" (default on): while the link holds the unit, host MIDI that is the unit's own panel events (the unit's port is
// also a MIDI input of the DAW: Live's "All Ins") and RemoteFM1 SysEx is dropped (isRemotePanelMidi).
//
// THE .fm1preset FORMAT: one line of JSON, a '\n', then the flash image gzip-compressed (RFC 1952: `tail -n +2 F |
// gunzip` gives the raw image):
//   {"fm1preset":1,"core":"choralroot","version":"0.1","name":"Warm Pads","date":"2026-10-07T12:00:00.000+02:00",
//    "size":1048576}
// An emulated firmware's preset (core "fm1emu") adds "firmware" (its Hub id), "fwVersion" and "sha256" (its package):
// it loads only on that package, since its flash holds the firmware's own code.
//
// EMULATED FIRMWARES (plugin/Hub.h, plugin/EmuClock.h): the fm1emu core (core-glue/fm1emu: fm1-emu's emulated FM-1)
// running an FM-1 update package, a Hub firmware the user picked or their own .fwsc. The module is one more bundled
// core; it is not offered as a firmware of its own (listCores() leaves it out: firmwareCores()). What the host keeps
// of an emulated firmware is its NOR (the first 1 MiB of the core's 0x210000-byte image, without the package slot):
//   - the package lives in the Hub cache (hub/packages, by SHA-256) and in memory (emuPkg_); every boot composes the
//     core's image from the NOR and the package (core-glue/fm1emu README "The firmware"), or stages the package alone
//     (a fresh flash);
//   - the working flash: home()/fm1emu/<hub id>/<version>-<sha256[0..8]>/flash.bin, so each package version keeps its
//     own flash (a NOR from another version would put that version's code back over the new one); presets and backups
//     per firmware: home()/fm1emu/<hub id>/presets, backups;
//   - the state: core "fm1emu", a child "emu" (id name author version sha256 size pkg source product local), and the
//     NOR as "flash" (gzip): the package by id and SHA-256, never its bytes. A state whose package is not cached boots
//     the core empty (its LOAD .FWSC screen), keeps the NOR it brought (emuHeld_: getStateInformation returns it
//     unchanged), downloads the package from the state's URL (checked against its SHA-256 and size) and then powers on
//     with that NOR. Offline with the package cached, nothing is fetched.
//   - the clock: the instance's choice (Auto or MHz, and Auto's current step) in the state ("emuClock", "emuAutoMhz"),
//     given to each power-on as the core's boot option "mhz" (fm1core.h OPTIONAL MODULE SYMBOLS). Auto's governor
//     (EmuClock.h) is fed from processBlock's own timing (two atomics, no lock) by the message thread's timer; a step
//     down reboots with the flash kept, as a manual change does.
//
// SHOW TO EDITORS (setEditorPortEnabled; the switch is in the state; macOS): a virtual MIDI port pair named after the
// loaded firmware's USB port (EditorPort.h), so the firmware's own web manager finds this instance as an FM-1. The
// timer (message thread) makes it, and remakes it when the firmware changes. Its messages go straight into the core's
// MIDI in (pullEditorMidi: whole messages into inRing_, at block start and in the idle clock), and while it is open
// every SysEx the core sends goes to the port, not to the host; channel messages still go to the host. Nothing of
// the port's traffic is in the host's MIDI buffers. When no processBlock has started for 50 ms, the port's service
// thread runs the core (editorIdleTick: devLock_ held, the audio discarded) so a manager is answered while the host
// does not play.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AudioTap.h"
#include "EditorPort.h"
#include "EmuClock.h"
#include "HostRefresh.h"
#include "Hub.h"

#ifndef FM1_DEFAULT_CORE
#define FM1_DEFAULT_CORE "choralroot"   // the firmware a new instance loads (CMakeLists.txt sets it)
#endif
#include "RemoteLink.h"
#include "Theme.h"
#include "Tier2Parameter.h"
#include "cores.h"
#include "device.h"

// A panel control as a host boolean (a key: the value IS the held state; a button: as the Button params setting
// says, FM1Processor::ButtonMode). Its name can change at run time (a core switch relabels the buttons from the new
// core's descriptor).
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
// the core's panel directly: it copies this under a spin lock the audio thread only ever tries).
struct PanelView {
    uint32_t seq = 0;                                 // moves with every snapshot
    bool running = false;                             // a booted, not halted device
    uint32_t lcdWrites = 0;                           // panel->lcd_writes when lcd was copied
    uint32_t lcdSeq = 0;                              // moves with every copy of lcd (a power cycle restarts lcd_writes)
    std::array<uint16_t, EMU_LCD_W * EMU_LCD_H> lcd{};   // RGB565 big-endian, as sent
    std::array<uint8_t, EMU_NKEY> keyLed{};           // brightness 0..255 (fm1core.h FM1P_LED_*: the panel's level)
    std::array<uint8_t, EMU_NB> btnLed{};
    uint8_t playGreen = 0;                            // 0..255
    uint32_t keys = 0, buttons = 0;                   // what the device holds now (all sources merged; label bits)
    int master = 724;                                 // panel->master, 0..1023
    // the seven knobs (EMU_E_* role, SELECT .. KNOB4): bound to a firmware value (the panel draws the pointer at its
    // place on a 270 degree sweep, as MASTER's) or relative (the pointer turns per detent); the value's place 0..1
    // in its range; every detent the device was given (any source: the panel, the host's relative turns); what the
    // knob does now ("Voicing"; "" relative)
    struct Knob {
        bool bound = false;
        float norm = 0.5f;
        int32_t turns = 0;
        char fn[24] = {};
        bool operator==(const Knob &o) const
        {
            return bound == o.bound && juce::exactlyEqual(norm, o.norm) && turns == o.turns && std::strcmp(fn, o.fn) == 0;
        }
        bool operator!=(const Knob &o) const { return !(*this == o); }
    };
    std::array<Knob, EMU_NE - 1> knobs{};
};

namespace fm1video {
class Session;
}

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
    juce::VST3ClientExtensions *getVST3ClientExtensions() override { return &vst3_; }
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
    std::vector<CoreInfo> firmwareCores() const;     // listCores() without the fm1emu engine (the native firmwares)
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
    juce::File workingFlashFile() const;             // coreDir()/flash.bin (emulated: per package version)
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
    // the editor's view (stored in the state; a state without it: the device): the whole FM-1, or its screen alone in
    // a square window whose side is remembered apart from the device view's size (0: never shown)
    enum ViewMode { kViewDevice = 0, kViewScreen = 1 };
    int getViewMode() const { return viewMode_.load(); }
    void setViewMode(int m) { viewMode_.store(juce::jlimit(0, 1, m)); }
    int getScreenSize() const { return screenSize_.load(); }
    void setScreenSize(int side) { screenSize_.store(side); }
    // a relative knob's detents per full travel of the host value (the Roto-Control's 0..1; stored in the state)
    int getKnobDetents() const { return knobDetents_.load(); }
    void setKnobDetents(int n) { knobDetents_.store(juce::jlimit(4, 256, n)); }
    // what the seven encoder parameters (SELECT PRESETS ALGORITHM KNOB1..4; never Knob Master, the pot) take from the
    // host (stored in the state; the editor's "Encoders"):
    //   Absolute     (default) a write is the value of what the knob turns now (a relative knob: its change x
    //                knobDetents per travel, as detents)
    //   Stepped      a write is clicks of a stepped controller (a Roto-Control knob set to getEncoderSteps() steps,
    //                feeling like the FM-1's detents): its change in steps (KnobParameter::stepDelta) is that many
    //                detents of the device's encoder (Device::enc, the panel's path), bound or relative; the firmware's
    //                value is pushed back so the next click counts from where the motor was put
    // A switch drops the old mode's pending writes; nothing is turned by the switch itself.
    enum EncoderMode { kEncAbsolute = 0, kEncStepped = 1 };
    int getEncoderMode() const { return encMode_.load(); }
    void setEncoderMode(int m);
    int getEncoderSteps() const { return encSteps_.load(); }
    void setEncoderSteps(int n);
    static juce::String encoderModeName(int m);      // "Absolute" "Stepped"
    static constexpr int kDefaultEncSteps = 24, kMinEncSteps = 8, kMaxEncSteps = 64;
    // what the btn_* host parameters do to the device's buttons (stored in the state; the editor's "Button params"):
    //   Tap          (default) a rising edge (value from < 0.5 to >= 0.5) presses the button for getButtonTapMs()
    //                of device time, then releases it; the falling edge does nothing. A rising edge while a tap is
    //                still down or in its kTapGapMs release queues one more tap (up to kTapQueue)
    //   Hold         the value is the held state (a momentary controller button: layer locks, long presses)
    //   ToggleHold   a rising edge toggles held (a toggle controller button can hold a layer open)
    // A mode change lets go of what the old mode held (a power-on also takes the values as they are: no edge).
    enum ButtonMode { kButtonTap = 0, kButtonHold = 1, kButtonToggleHold = 2 };
    int getButtonMode() const { return btnMode_.load(); }
    void setButtonMode(int m) { btnMode_.store(juce::jlimit(0, 2, m)); }
    static juce::String buttonModeName(int m);       // "Tap" "Hold" "Toggle-hold"
    // a Tap's press length in device ms (stored in the state; no editor control). The default 90: the firmware
    // needs EMU_RELEASE_MS (9) of press and sees the buttons once per 15 ms UI frame; a layer locks at its hold
    // threshold (HOLD_MS, 300 ms or more)
    int getButtonTapMs() const { return btnTapMs_.load(); }
    void setButtonTapMs(int ms) { btnTapMs_.store(juce::jlimit(kMinTapMs, kMaxTapMs, ms)); }
    static constexpr int kDefaultTapMs = 90, kMinTapMs = 20, kMaxTapMs = 1000;
    static constexpr int kTapGapMs = 30;             // the release between two queued taps (two UI frames)
    static constexpr int kTapQueue = 4;
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
    // every power-on (boot, preset load, state restore, flash reset, core switch, a reboot) lets go of what the panel
    // held (no button held into a boot) and moves this count: the panel then drops its latches
    uint32_t panelPowerOns() const { return panelPowerOns_.load(); }
    // the latest snapshot: copies it into out when out.seq differs (the LCD only when out.lcdSeq differs);
    // false when nothing changed
    bool getPanelView(PanelView &out) const;

    // the current flash image (synced first when the device runs); empty = a fresh flash. An emulated firmware: its
    // NOR (1 MiB), without the package
    std::vector<uint8_t> currentFlash();

    // ---- emulated firmwares (message thread; see EMULATED FIRMWARES above) ----
    static constexpr const char *kEmuCore = "fm1emu";
    static constexpr uint32_t kNor = 0x100000;       // the emulated FM-1's NOR: what the host keeps
    struct EmuFirmware {
        juce::String id, name, author, version, sha256, pkg, source, product;
        juce::int64 size = 0;
        bool local = false;                          // the user's own file (no Hub URL)
        bool valid() const { return id.isNotEmpty() && sha256.isNotEmpty(); }
        hub::Entry entry() const;                    // as a Hub entry (to fetch it again)
        static EmuFirmware of(const hub::Entry &e);
    };
    bool isEmulated() const;                         // the fm1emu core with a firmware picked
    EmuFirmware getEmuFirmware() const;
    // pick a Hub firmware: from the cache at once, else downloaded on the Hub's worker (the current firmware plays on
    // meanwhile; getEmuStatus shows the progress) and then booted. The current firmware's flash is saved first, and the
    // new one boots from its own working flash for that version.
    void selectEmuFirmware(const hub::Entry &e);
    // the same, blocking (the download on the calling thread): tests. false and why on failure
    bool selectEmuFirmwareNow(const hub::Entry &e, juce::String *error = nullptr);
    // the user's own .fwsc (M-VAVE's stock V15 is recognised by the catalog's SHA-256): copied into the Hub cache, then
    // picked. "" or why not
    juce::String openLocalPackage(const juce::File &f);
    struct EmuStatus {
        bool busy = false;                           // a download for this instance is under way
        double progress = 0;                         // 0..1, -1 unknown
        juce::String text;                           // "Downloading Melodee 1.4.4", or the last error
        bool error = false;
        bool waiting = false;                        // the core runs empty: its package is not here (yet)
    };
    EmuStatus getEmuStatus() const;
    // the emulated chip's clock: the choice (emuclock::kAuto or MHz; per instance, in the state) and what runs now
    int getEmuClockChoice() const { return emuChoice_.load(); }
    void setEmuClockChoice(int choice);              // an emulated firmware reboots with its flash kept
    int getEmuMhz() const { return emuclock::mhzFor(emuChoice_.load(), emuAutoMhz_.load()); }
    // Auto's governor, one report (the timer calls it every half second; tests call it with fake timing)
    void emuGovernorFeed(double load, double lateMs, double nowMs);
    juce::String getEmuClockNote() const;            // what the governor last said ("" none)
    // in a plugin, Auto's step down waits while the host's transport plays (the clock row says so); the Standalone
    // steps at once. The step (MHz) waiting, 0 none; the timer's work, for tests
    int getEmuDeferredStep() const { return deferredStep_.load(); }
    void emuTimerForTest() { emuTimer(); }
    hub::Service &hubService() { return *hub_; }

    // ---- Remote FM-1 (message thread; stored in the state). On: the link takes the unit lock and runs; off: SESSION 0,
    // the unit back on its waiting screen. Neither blocks.
    void setRemoteEnabled(bool on);
    bool getRemoteEnabled() const { return remoteOn_.load(); }
    void setRemoteIgnoreMidi(bool on) { remoteIgnoreMidi_.store(on); }
    bool getRemoteIgnoreMidi() const { return remoteIgnoreMidi_.load(); }
    RemoteLink::Status remoteStatus() const { return remote_->status(); }
    // the unit's own panel events as they reach a DAW from its MIDI port (RemoteFM1 docs/PROTOCOL.md section 4): note
    // on / off 53..79 on channel 16 (keys), 0..13 on channel 15 (buttons), CC 7 / 39 / 16..22 on channel 15 (MASTER,
    // the encoders); and RemoteFM1's SysEx (F0 7D 52 46)
    static bool isRemotePanelMidi(const uint8_t *d, int n);
    static bool isRemotePanelMidi(const juce::MidiMessage &m) { return isRemotePanelMidi(m.getRawData(), m.getRawDataSize()); }

    // ---- Show to editors (message thread; stored in the state; see SHOW TO EDITORS above)
    void setEditorPortEnabled(bool on);
    bool getEditorPortEnabled() const { return editorOn_.load(); }
    struct EditorPortStatus {
        bool available = false;                      // the platform has virtual ports (macOS)
        bool on = false, open = false, idling = false, emulated = false;
        juce::String name;                           // the port's name ("" closed)
        juce::String usbName;                        // the loaded firmware's USB name (what the port is named after)
        juce::String error;                          // why no port could be made
        juce::String url;                            // the firmware's manager page, "" none known
        uint32_t in = 0, out = 0, dropped = 0;       // messages taken by the core, sent back, refused
        uint64_t idleMs = 0;                         // device ms the idle clock has run
    };
    EditorPortStatus editorPortStatus() const;
    juce::String editorUsbName() const;              // the loaded firmware's USB product string
    void syncEditorPort();                           // (the timer's work) the port as the switch and the firmware want
    // the idle clock (the port's service thread; tests): ms device milliseconds of the core, devLock_ held, its audio
    // and channel messages discarded, its SysEx to the port
    void editorIdleTick(uint32_t ms);
    EditorPort *editorPortForTest() { return editorPort_.get(); }

    // ---- video recording (the Standalone app: VideoCapture.h). The output captured in processBlock, after the core
    // rendered it, while a take runs (lock-free); a power-on or a device restart (prepareToPlay) ends the take first
    AudioTap &videoTap() { return videoTap_; }
    // what the frames show, captured after each block at the frames' instants (the panel's snapshot; lock-free)
    SnapshotRing<PanelView> &videoFrames() { return videoFrames_; }
    fm1video::Session *videoSession();             // (message thread) made on first use; nullptr in a plugin

    // ---- for tests (call only between processBlock calls, on the thread that calls them) ----
    Device *deviceForTest() { return dev_.get(); }
    RemoteLink *remoteForTest() { return remote_.get(); }
    uint32_t remotePublished() const { return remotePubs_; }     // snapshots given to the link
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
    uint32_t hostRefreshes() const { return vst3_.refreshes(); }   // the drains that asked a VST3 host to re-read
    int32_t knobWritesDropped(int role) const { return role >= 0 && role < kKnobs ? knobDropped_[(size_t)role].load() : 0; }

    static constexpr int kTier2Slots = FM1_TIER2_SLOTS;
    static constexpr int kKnobs = EMU_NE - 1;        // the KnobParameters (MASTER is master_)
    static constexpr int kHoldoffFrames = 2;         // read-backs skipped after a host write (30 ms of device time)
    // after a knob's retarget, host writes are dropped for this long (device ms): the controller's motor and the
    // host still hold the old target's value until the push reaches them; the knob is then told its value again
    static constexpr uint32_t kSettleMs = 150;

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
    bool pushTier2(int slot, int32_t v, uint8_t kind, int32_t aux = 0);   // audio thread -> the FIFO; false: full
    void timerCallback() override
    {
        drainTier2();
        emuTimer();
        syncEditorPort();
    }
    void relabelParameters();
    juce::String buttonName(int i) const;

    // emulated firmwares (with devLock_ held)
    bool switchEmuLocked(const EmuFirmware &fw, std::vector<uint8_t> pkg, std::optional<std::vector<uint8_t>> nor);
    void clearEmuLocked();                           // (a native core takes over)
    void emuPackageArrived(const EmuFirmware &fw, const juce::File &file, const juce::String &error);
    void applyEmu(const EmuFirmware &fw, std::vector<uint8_t> pkg);   // message thread: suspend, switch, relabel
    void emuTimer();                                 // (the timer) Auto's load reports

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
    void renderBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &);   // (processBlock: this, then the video tap)
    AudioTap videoTap_;
    SnapshotRing<PanelView> videoFrames_;
    std::unique_ptr<fm1video::Session> video_;
    void applyTier1();
    void pushKeys();
    void handleMidi(const juce::MidiMessage &m);
    void forwardMidi(const juce::MidiMessage &m);
    void flushMidiIn();
    void drainMidiOut(int samplePos);
    void pullEditorMidi();                           // (devLock_ held) the port's whole messages into inRing_
    void decodePacket(uint32_t pkt, int samplePos);
    void renderSegment(float *L, float *R, int from, int to);
    void resetAudioState();
    void applyRemote(uint32_t &keyTaps, uint32_t &btnTaps);   // the unit's events (block start)
    void applyMaster();
    void publishRemote();                            // the LCD + LEDs to the unit (block end)
    static void remoteLeds(const fm1panel_t *h, rfm_leds_t &l);   // the panel's LED levels as the LEDS planes

    void handleAsyncUpdate() override;               // the firmware rebooted itself (exit(0)): power on again

    // emulated firmwares (written with devLock_ held on the message thread)
    juce::SharedResourcePointer<hub::Service> hub_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);   // (async callbacks)
    EmuFirmware emu_;                                // the firmware picked (valid() with the fm1emu core)
    std::vector<uint8_t> emuPkg_;                    // its package's bytes ("": not here yet)
    std::optional<std::vector<uint8_t>> emuHeld_;    // the NOR a state brought, kept while its package is missing
    bool emuRunning_ = false;                        // booted with its package (not the empty LOAD .FWSC screen)
    juce::String emuPending_;                        // the SHA-256 being fetched for this instance ("" none)
    EmuFirmware emuPendingFw_;
    juce::String emuError_;
    std::atomic<int> emuChoice_{emuclock::kAuto}, emuAutoMhz_{emuclock::kDeviceMhz};
    // Auto: processBlock's timing (microseconds rendered, of audio made, late), taken by the timer
    std::atomic<juce::int64> loadBusyUs_{0}, loadAudioUs_{0}, loadLateUs_{0};
    std::atomic<bool> emuMeasure_{false};            // an emulated firmware runs with its package
    static constexpr uint32_t kEmuPaceMs = 4;        // an emulated firmware's pacing lead (device.h PACING)
    mutable std::atomic<int> devLatency_{0};         // the device's own part of the latency (latencyLocked)
    fm1core_latency_fn latencyFn_ = nullptr;        // (looked up at boot: no dlsym on the audio thread)
    std::atomic<int> coreLatency_{-1};               // the core's own latency (fm1core_latency_frames), read on the
                                                     // audio thread after a block: the timer re-reports a change (an
                                                     // emulated package whose DMA half is larger than 256 frames)
    emuclock::Governor governor_;
    double lastReport_ = 0;
    std::atomic<bool> governorReset_{false};
    juce::String clockNote_;
    std::atomic<bool> hostPlaying_{false};           // the host's transport plays (processBlock, an emulated firmware)
    std::atomic<int> deferredStep_{0};               // Auto's step down waiting for the transport to stop (0 none)
    void applyAutoStep(int step, int was);
    std::vector<uint8_t> composeEmuImage(const std::vector<uint8_t> &nor) const;
    int latencyLocked() const;                       // (devLock_ held) the samples the host is told

    // the core and its device
    mutable juce::CriticalSection devLock_;
    std::unique_ptr<LoadedCore> loaded_;
    std::unique_ptr<Device> dev_;
    CoreInfo coreInfo_;
    juce::String coreId_ = FM1_DEFAULT_CORE, requestedCore_ = FM1_DEFAULT_CORE;   // (CMake: ChoralRoot, or Felucca in a
                                                                                // public build, which leaves it out)
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
    bool t2Created_ = false;                             // createTier2Parameters ran (the pool may be empty)
    int t2Bound_ = 0;                                    // (written with devLock_ held and the audio stopped)

    // Tier 2 feedback: audio-thread state (devLock_ held) and the FIFO to the message thread
    // kT2Change: a firmware value (aux 1: inside a gesture: a Tier 2 slot always, a knob only when it was turned on
    // the device); kKnobNorm: a knob turned relative (a relabel), kKnobCentre: re-centred; both: its value 0.5,
    // unless the host wrote it since (value: the KnobParameter's hostSeq when pushed); kKnobNudge: a relative knob
    // turned on the device (aux: the detents off the centre). FIFO slots: 0 .. kTier2Slots - 1 the Tier 2 slots, then
    // the knobs by role.
    enum : uint8_t { kT2Change, kT2Sweep, kT2Relabel, kKnobNorm, kKnobCentre, kKnobNudge };
    struct T2Msg {
        int16_t slot;
        uint8_t kind;
        uint32_t gen;                                    // t2Gen_ when pushed: a rebind drops older messages
        int32_t value, aux;
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
    // who turned a knob: the device's detent counters (Device::enc_total) against the host's own relative detents
    std::array<int32_t, EMU_NE - 1> encSeen_{}, hostEnc_{}, nudgeAcc_{};
    std::array<uint32_t, EMU_NE - 1> ownTurnMs_{};      // device ms of the last device turn
    std::array<bool, EMU_NE - 1> ownTurnValid_{};
    static constexpr uint32_t kOwnTurnMs = 150;          // a target change this soon after a device turn is its own
    std::array<uint32_t, EMU_NE - 1> knobRetargetMs_{};
    std::array<bool, EMU_NE - 1> knobSettling_{};
    std::array<std::atomic<int32_t>, EMU_NE - 1> knobDropped_{};   // writes dropped (tests)
    Vst3HostRefresh vst3_;
    void retargetKnobs();
    void resetKnobTurns();
    void takeKnobTurns(std::array<int32_t, EMU_NE - 1> &own);
    bool ownTurnRecent(int role) const;

    // settings
    std::atomic<int> transpose_{0};
    std::atomic<bool> notesPlayKeys_{true};
    std::atomic<bool> keyNotesToFirmware_{false};
    mutable juce::SpinLock themeLock_;
    Theme theme_;
    std::atomic<uint32_t> themeSerial_{0};
    std::atomic<int> editorW_{0}, editorH_{0};
    std::atomic<bool> bigLcd_{false};
    std::atomic<int> viewMode_{kViewDevice}, screenSize_{0};

    // the panel GUI's input (written by the message thread, taken by the audio thread at block start)
    std::atomic<uint32_t> panelKeys_{0}, panelButtons_{0}, panelKeyTaps_{0}, panelBtnTaps_{0};
    std::atomic<uint32_t> panelPowerOns_{0};
    std::array<std::atomic<int32_t>, EMU_NE> panelEnc_{};
    std::atomic<int32_t> fineReq_{0};
    std::atomic<int> fineRole_{EMU_E_SELECT};
    // the audio thread's side
    uint32_t paramButtons_ = 0, panelKeysCur_ = 0, panelButtonsCur_ = 0;
    // the button parameters' edges (audio thread): the values at the last block start, the Toggle-hold state, the
    // Tap presses counted down per device ms (tickTaps, from the between hook)
    std::atomic<int> btnMode_{kButtonTap}, btnTapMs_{kDefaultTapMs};
    std::atomic<int> encMode_{kEncAbsolute}, encSteps_{kDefaultEncSteps};
    void applyEncoderMode();                         // the knobs' setStepped from encMode_ / encSteps_
    int btnModeCur_ = -1;                            // -1: take the values as they are at the next block (no edge)
    uint32_t btnPrev_ = 0, btnToggled_ = 0, tapHeld_ = 0, tapBusy_ = 0;
    std::array<uint16_t, EMU_NB> tapLeft_{}, tapGap_{};
    std::array<uint8_t, EMU_NB> tapQueued_{};
    void buttonParamsToDevice();                     // (block start) the btn_* values -> paramButtons_
    void startTap(int label);
    void tickTaps();                                 // (every device ms)
    void resetButtonEdges();                         // a new device: also the next block's values taken as they are
    void clearButtonEdges();                         // the taps and the toggles let go
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
    uint8_t viewLeds_[FM1P_NCOL * FM1P_NLINE + 1] = {};   // the panel's led[][] and PLAY green last snapshotted
    uint32_t viewKeys_ = 0xFFFFFFFFu, viewButtons_ = 0xFFFFFFFFu;
    int viewMaster_ = -1;
    std::array<PanelView::Knob, EMU_NE - 1> viewKnobs_{};
    bool viewRunning_ = false, viewDirty_ = true;

    // audio-thread state
    uint32_t paramKeys_ = 0, midiKeys_ = 0, appliedKeys_ = 0xFFFFFFFFu, appliedButtons_ = 0xFFFFFFFFu;
    int appliedMaster_ = -1;
    bool lastNotesPlayKeys_ = true;
    std::array<std::array<int8_t, 128>, 16> noteKey_{};  // the key a held note pressed, -1 none
    std::array<uint8_t, EMU_NKEY> keyCount_{};           // MIDI notes holding each key
    std::array<uint8_t, EMU_NKEY> midiVel_{};            // the velocity of the MIDI note that pressed each key
    uint32_t remoteKeyTaps_ = 0;                         // this block's key taps from the unit (velocity kRemoteVel)
    static constexpr uint32_t kInRing = 4096;
    std::array<uint32_t, kInRing> inRing_{};             // USB-MIDI packets waiting for room in the core
    uint32_t inW_ = 0, inR_ = 0;
    std::vector<uint8_t> sysexOut_;                      // reassembling a SysEx from the core (CIN 4..7)
    bool sysexOutOverflow_ = false;
    juce::MidiBuffer midiOut_;
    bool toHost_ = true;                                 // (devLock_ held) false in the idle clock: no host to take MIDI
    std::vector<float> scratch_;                         // the right channel when the host gives only one

    // Remote FM-1 (see the top): the link (created with the processor; its thread starts with the first enable)
    std::unique_ptr<RemoteLink> remote_;
    std::atomic<bool> remoteOn_{false}, remoteIgnoreMidi_{true};
    // the audio thread's side: what the unit holds (keys; buttons as label bits), its MASTER until the parameter
    // follows, what was last published
    uint32_t remoteKeys_ = 0, remoteButtons_ = 0;
    int remoteMaster_ = -1;
    bool remoteMasterAwait_ = false;
    uint32_t remoteMasterMs_ = 0;
    static constexpr uint32_t kRemoteMasterWaitMs = 1000;   // the parameter not caught up by then: it is left alone
    std::atomic<int> remoteMasterReq_{-1};               // the unit's MASTER for the parameter (message thread)
    bool remotePubValid_ = false;
    uint32_t remotePubMs_ = 0, remotePubWrites_ = 0, remotePubs_ = 0;
    rfm_leds_t remotePubLeds_{};

    // Show to editors (see the top): the port (made with the processor; its devices and thread only while on)
    std::unique_ptr<EditorPort> editorPort_;
    std::atomic<bool> editorOn_{false};
};
