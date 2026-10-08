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
//
// THE TIER 2 SEAM (the second half of phase 2)
//   applyHostParameters()  audio thread, devLock_ held, at the start of every block after the Tier 1 values were
//                          applied: write host changes of the firmware's parameters into the core.
//   readBackParameters()   audio thread, devLock_ held, once per device UI frame (every 15 ms of device time, from
//                          Device::set_between with frame == true, i.e. right before that ms's frame()): compare the
//                          core's values with what the host last saw and report the differences.
//   Both are empty here. Parameters are created once, in the constructor (createTier1Parameters(); a
//   createTier2Parameters() goes right after it): hosts expect a fixed parameter list, so a core switch relabels
//   (PanelBoolParameter::setDisplayName + updateHostDisplay(parameterInfoChanged)) rather than adding or removing.
//
// FILES (everything under home() = <FM1EMU_HOME or ~/Library/Application Support>/fm1emu)
//   cores/                     user-installed *.fm1core modules (the bundled ones: <bundle>/Contents/Resources/cores)
//   <core-id>/flash.bin        the working flash: what a new instance of that core boots from; written when the
//                              instance is released or destroyed, before a core switch, and on an explicit save
//   <core-id>/presets/NAME.fm1preset    a named snapshot of the whole flash (format below)
//   <core-id>/backups/DATE.fm1preset    written before a preset load or a flash reset (the newest 50 are kept)
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

struct FM1Theme {                       // stored in the state only (phase 3 draws with it); colours "#RRGGBB"
    juce::String name = "Emulator";
    juce::String base = "#1C1C20", membrane = "#2B2B31", bed = "#141417", knob = "#35353C";
};

class FM1Processor : public juce::AudioProcessor, private juce::AsyncUpdater {
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
    FM1Theme getTheme() const;
    void setTheme(const FM1Theme &t);

    // the current flash image (synced first when the device runs); empty = a fresh flash
    std::vector<uint8_t> currentFlash();

    // ---- for tests (call only between processBlock calls, on the thread that calls them) ----
    Device *deviceForTest() { return dev_.get(); }
    PanelBoolParameter *buttonParam(int label) { return btn_[(size_t)label]; }
    PanelBoolParameter *keyParam(int key) { return key_[(size_t)key]; }
    juce::AudioParameterInt *masterParam() { return master_; }

    static constexpr int kNoteBase = 53;             // MIDI note of key 0 (F3)
    static const char *const kPanelLabels[EMU_NB];   // the printed labels: FX SEL ENV .. OCT+
    static const char *const kButtonIds[EMU_NB];     // btn_fx .. btn_octup

private:
    // the Tier 2 seam (see the top)
    void applyHostParameters();
    void readBackParameters();

    void createTier1Parameters();
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

    // settings
    std::atomic<int> transpose_{0};
    std::atomic<bool> notesPlayKeys_{true};
    std::atomic<bool> keyNotesToFirmware_{false};
    mutable juce::SpinLock themeLock_;
    FM1Theme theme_;

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
