// SPDX-License-Identifier: GPL-3.0-only
// The plugin's processor, headless (no editor, no window): MIDI notes to keys, sound at 48 and 44.1 kHz, a panel
// button parameter reaching the HAL, MIDI out decoding, the state round trip of the flash, presets, the cores
// folders and the bundle path logic, the firmware switch (ChoralRoot -> Felucca -> Melodee -> ChoralRoot: the button
// labels, the Tier 2 slots rebound, a note on each, ChoralRoot's working flash back); (i) the button parameters' modes (Tap: one ~90 ms press per rising edge, FX
// toggled once, OCT+ held at 1 = exactly one octave and no panic; Hold: held 2 s, the layer locked; Toggle-hold:
// held from the first rise to the second) and their state. FM1EMU_HOME points at a scratch folder under the build (set here, before any
// processor exists), so nothing is written to ~/Library/Application Support or ~/Library/Caches.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Processor.h"

static int fails = 0;
static void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    fails += !c;
}

// drives one processor block by block, collecting the output's peak and the MIDI it emits
struct Host {
    FM1Processor &p;
    double sr;
    int bs;
    juce::AudioBuffer<float> buf;
    float peak = 0.0f;
    int midiOut = 0, midiBad = 0, noteOnsOut = 0, allNotesOff = 0;   // allNotesOff: CC 123 (the firmware's panic)
    uint64_t frames = 0;

    Host(FM1Processor &proc, double rate, int block) : p(proc), sr(rate), bs(block), buf(2, block)
    {
        p.setRateAndBufferSizeDetails(rate, block);
        p.prepareToPlay(rate, block);
    }
    void block(juce::MidiBuffer &midi)
    {
        buf.clear();
        p.processBlock(buf, midi);
        for (int c = 0; c < 2; c++)
            peak = std::max(peak, buf.getMagnitude(c, 0, bs));
        for (const auto m : midi) {
            midiOut++;
            const juce::MidiMessage msg = m.getMessage();
            const uint8_t *d = msg.getRawData();
            const int n = msg.getRawDataSize();
            bool ok = n >= 1 && (d[0] & 0x80) && m.samplePosition >= 0 && m.samplePosition < bs;
            if (ok && d[0] == 0xF0)
                ok = d[n - 1] == 0xF7;
            else if (ok && d[0] < 0xF0)
                ok = n == ((d[0] & 0xE0) == 0xC0 ? 2 : 3) && !(d[1] & 0x80) && (n < 3 || !(d[2] & 0x80));
            midiBad += !ok;
            noteOnsOut += msg.isNoteOn();
            allNotesOff += msg.isController() && msg.getControllerNumber() == 123;
        }
        frames += (uint64_t)bs;
    }
    void block()
    {
        juce::MidiBuffer m;
        block(m);
    }
    void run_ms(double ms)
    {
        const uint64_t until = frames + (uint64_t)(ms * sr / 1000.0);
        while (frames < until)
            block();
    }
    void note(bool on, int note, int at = 0)
    {
        juce::MidiBuffer m;
        m.addEvent(on ? juce::MidiMessage::noteOn(1, note, (juce::uint8)100) : juce::MidiMessage::noteOff(1, note), at);
        block(m);
    }
};

static bool keyHeld(FM1Processor &p, int key) { return (p.deviceForTest()->hal()->keys >> key) & 1u; }

static std::vector<uint8_t> flashOf(FM1Processor &p)
{
    Device *d = p.deviceForTest();
    return d && d->flash() ? std::vector<uint8_t>(d->flash(), d->flash() + d->flash_size()) : std::vector<uint8_t>{};
}

static size_t diffBytes(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    if (a.size() != b.size())
        return (size_t)-1;
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i++)
        n += a[i] != b[i];
    return n;
}

int main()
{
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();                 // (the test's own folder under the build)
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;         // (the message manager: async updates, host display updates)

    std::printf("plugin_test: FM1EMU_HOME=%s\n", scratch.getFullPathName().toRawUTF8());
    check(FM1Processor::home() == scratch.getChildFile("fm1emu"), "home() follows FM1EMU_HOME");

    // ---- (g) the cores folders, the bundle path logic
    {
        FM1Processor p;
        const std::vector<CoreInfo> cores = p.listCores();
        bool found = false;
        for (const CoreInfo &c : cores)
            found = found || c.id == "choralroot";
        check(found, "listCores() finds choralroot (bundled dir " + FM1Processor::bundledCoresDir().getFullPathName().toStdString() + ")");
        check(FM1Processor::userCoresDir().isDirectory() &&
                  FM1Processor::userCoresDir().isAChildOf(scratch),
              "the user cores folder is under FM1EMU_HOME: " + FM1Processor::userCoresDir().getFullPathName().toStdString());
        for (const char *bundle : {FM1_INSTALLED_VST3, FM1_INSTALLED_AU}) {
            const juce::File b(bundle);
            const juce::File exe = b.getChildFile("Contents/MacOS/FM1VST");
            const juce::File d = FM1Processor::bundledCoresDirFor(exe);
            check(exe.existsAsFile() && d == b.getChildFile("Contents/Resources/cores") &&
                      d.getChildFile("choralroot.fm1core").existsAsFile() &&
                      d.getChildFile("felucca.fm1core").existsAsFile() && d.getChildFile("melodee.fm1core").existsAsFile(),
                  "installed " + b.getFileName().toStdString() + ": cores resolve to " + d.getFullPathName().toStdString() +
                      " (choralroot, felucca, melodee .fm1core there)");
            if (d.isDirectory()) {
                const std::vector<CoreInfo> in = CoreLoader::scan({d.getFullPathName().toStdString()});
                std::string ids;
                for (const CoreInfo &ci : in)
                    ids += " " + ci.id + " (" + ci.name + " " + ci.version + ")";
                check(in.size() == 3, "  ... and the modules there load:" + ids);
            }
        }
        check(FM1Processor::bundledCoresDirFor(juce::File("/tmp/plugin_test")) == juce::File(),
              "an executable outside a bundle has no bundled cores dir");

        // the parameters: the eight knobs, the Tier 2 slots t2_00 .., then the 41 of Tier 1, the ids in order
        const auto &ps = p.getParameters();
        const int t2 = FM1Processor::kTier2Slots;
        static const char *const KNOBS[8] = {"knob_master", "knob_select", "knob_presets", "knob_algo",
                                             "knob_1",      "knob_2",      "knob_3",       "knob_4"};
        auto pid = [&](int i) { return dynamic_cast<juce::AudioProcessorParameterWithID *>(ps[i])->getParameterID(); };
        bool idsOk = ps.size() == 8 + t2 + 41;
        for (int i = 0; idsOk && i < 8; i++)
            idsOk = pid(i) == KNOBS[i];
        for (int i = 0; idsOk && i < t2; i++)
            idsOk = pid(8 + i) == juce::String::formatted("t2_%02d", i);
        for (int i = 0; idsOk && i < 14; i++)
            idsOk = pid(8 + t2 + i) == FM1Processor::kButtonIds[i];
        for (int k = 0; idsOk && k < 27; k++)
            idsOk = pid(8 + t2 + 14 + k) == juce::String::formatted("key_%02d", k);
        check(idsOk, "the eight knobs knob_master .. knob_4, " + std::to_string(t2) + " Tier 2 slots t2_00 .., then 41 "
                     "Tier 1 parameters: btn_fx .. btn_octup, key_00 .. key_26 (" + std::to_string(ps.size()) + " in all)");
        check(ps.size() == (size_t)(t2 ? 128 : 49), "the parameter count: " + std::to_string(ps.size()) +
                                                        (t2 ? " (the opt-in Tier 2 slots: Live's 128)" : " (8 knobs + 14 buttons + 27 keys)"));
        check(p.buttonParam(EMU_B_SEL)->getName(100) == "KEY (SEL)" && p.keyParam(9)->getName(100) == "D4" &&
                  p.keyParam(0)->getName(100) == "F3" && p.masterParam()->get() == 724 &&
                  p.masterParam()->getName(100) == "Knob Master" && p.knobParam(EMU_E_K4)->getName(100) == "Knob 4",
              "names: \"" + p.buttonParam(EMU_B_SEL)->getName(100).toStdString() + "\", key_09 \"" +
                  p.keyParam(9)->getName(100).toStdString() + "\", master " + std::to_string(p.masterParam()->get()));
        check(p.getNumPrograms() == 1 && p.getProgramName(0) == "Init", "no presets: one program, \"Init\"");
    }

    // ---- (a) 48 kHz: a note plays key 9 (D4), sound, release; (d) the MIDI out
    FM1Processor a;
    Host ha(a, 48000.0, 256);
    check(a.getLatencySamples() == (int)Device::latency_frames(48000.0),
          "latency " + std::to_string(a.getLatencySamples()) + " samples at 48 kHz");
    ha.run_ms(600);                              // the power-on (as the emulator's scripts wait 500 ms first)
    check(ha.peak == 0.0f, "600 ms after the power-on, nothing played: silence");
    ha.note(true, 62, 0);
    check(keyHeld(a, 9), "note on 62 at sample 0: key bit 9 (D4) held in the HAL");
    ha.run_ms(300);
    check(ha.peak > 0.01f, "300 ms after the note: sound (peak " + std::to_string(ha.peak) + ")");
    ha.note(false, 62, 100);
    check(!keyHeld(a, 9), "note off: key bit 9 released");
    ha.run_ms(100);
    check(ha.midiBad == 0, "MIDI out: " + std::to_string(ha.midiOut) + " messages (" + std::to_string(ha.noteOnsOut) +
                               " note-ons), none malformed");
    std::printf("        (ChoralRoot's default MIDI out: %s)\n",
                ha.noteOnsOut ? "the chord's notes are sent" : "nothing sent by default");

    // transpose: +1 octave, note 50 -> key 9
    a.setTranspose(1);
    ha.note(true, 50);
    check(keyHeld(a, 9), "transpose +1: note 50 plays key 9");
    ha.note(false, 50);
    check(!keyHeld(a, 9), "transpose +1: released");
    a.setTranspose(0);
    // the setting off: a note does not touch the keys (it still reaches the firmware's MIDI in)
    a.setMidiNotesPlayKeys(false);
    ha.run_ms(1500);                             // (the chord's release tail)
    ha.peak = 0.0f;
    ha.note(true, 62);
    check(!keyHeld(a, 9), "MIDI notes play keys off: note 62 leaves the keys alone");
    ha.run_ms(300);
    std::printf("        (note 62 on channel 1 through the firmware's own MIDI in only: peak %.3f)\n", ha.peak);
    ha.note(false, 62);
    a.setMidiNotesPlayKeys(true);
    ha.run_ms(50);

    // ---- (c) btn_sel held for 200 ms: the button bit reaches hal->buttons; the LED of KEY (SEL)
    {
        emu_hal_t *h = a.deviceForTest()->hal();
        const uint32_t bit = 1u << h->btn_id[EMU_B_SEL];
        const int ledBefore = a.deviceForTest()->led_button(EMU_B_SEL);
        a.buttonParam(EMU_B_SEL)->setValueNotifyingHost(1.0f);
        ha.block();
        const bool down = (h->buttons & bit) != 0;
        ha.run_ms(200);
        const int ledHeld = a.deviceForTest()->led_button(EMU_B_SEL);
        a.buttonParam(EMU_B_SEL)->setValueNotifyingHost(0.0f);
        ha.block();
        const bool up = (h->buttons & bit) == 0;
        ha.run_ms(100);
        const int ledAfter = a.deviceForTest()->led_button(EMU_B_SEL);
        check(down && up, "btn_sel on for 200 ms, then off: the matrix bit set, then cleared in hal->buttons");
        std::printf("        (KEY (SEL) LED: before %d, held %d, after %d; 0 off, 1 dim, 2 lit)\n", ledBefore, ledHeld, ledAfter);
    }
    // a key parameter merged with MIDI-held keys
    a.keyParam(0)->setValueNotifyingHost(1.0f);
    ha.note(true, 62);
    check(keyHeld(a, 0) && keyHeld(a, 9), "key_00 parameter and MIDI note 62 held together");
    a.keyParam(0)->setValueNotifyingHost(0.0f);
    ha.note(false, 62);
    check(!keyHeld(a, 0) && !keyHeld(a, 9), "both released");
    // MASTER
    a.masterParam()->setValueNotifyingHost(a.masterParam()->convertTo0to1(300));
    ha.block();
    check(a.deviceForTest()->hal()->master == 300, "master parameter 300 -> hal->master " +
                                                       std::to_string(a.deviceForTest()->hal()->master));
    a.masterParam()->setValueNotifyingHost(a.masterParam()->convertTo0to1(724));
    ha.block();

    // a setting the firmware persists (OCT+ moves the keyboard's octave): makes the flash differ from a fresh one
    a.buttonParam(EMU_B_OCTUP)->setValueNotifyingHost(1.0f);
    ha.run_ms(60);
    a.buttonParam(EMU_B_OCTUP)->setValueNotifyingHost(0.0f);
    ha.run_ms(100);

    // ---- (b) 44.1 kHz: sound too (the resampler bypassed)
    std::vector<uint8_t> freshFlash;
    {
        FM1Processor b;
        Host hb(b, 44100.0, 256);
        hb.run_ms(600);
        hb.note(true, 62);
        hb.run_ms(300);
        check(hb.peak > 0.01f, "44.1 kHz: sound (peak " + std::to_string(hb.peak) + ")");
        hb.note(false, 62);
        hb.run_ms(50);
        check(hb.midiBad == 0, "44.1 kHz: MIDI out well formed (" + std::to_string(hb.midiOut) + " messages)");
        freshFlash = b.currentFlash();
    }   // (b writes its working flash: the next instance below would boot from it; removed so (e) starts as the
        // state says, not from b's file)
    a.workingFlashFile().deleteFile();

    // ---- (e) the state round trip: the flash image survives
    juce::MemoryBlock state;
    a.getStateInformation(state);
    const std::vector<uint8_t> flashA = flashOf(a);
    check(flashA.size() == 0x100000u, "flash image " + std::to_string(flashA.size()) + " bytes; state " +
                                          std::to_string(state.getSize()) + " bytes");
    const size_t dFresh = diffBytes(flashA, freshFlash);
    check(dFresh != 0 && dFresh != (size_t)-1, "the flash after OCT+ differs from a fresh unit's (" + std::to_string(dFresh) + " bytes)");
    {
        FM1Processor c;
        c.setStateInformation(state.getData(), (int)state.getSize());
        Host hc(c, 48000.0, 256);
        const std::vector<uint8_t> flashC = flashOf(c);
        check(diffBytes(flashA, flashC) == 0, "a second instance from the state boots on the same flash bytes");
        hc.run_ms(600);
        hc.note(true, 62);
        hc.run_ms(200);
        check(hc.peak > 0.01f, "  ... and plays (peak " + std::to_string(hc.peak) + ")");
        hc.note(false, 62);
        // setState on a running instance: an immediate power cycle on the state's flash
        juce::MemoryBlock fresh;
        FM1Processor tmp;
        tmp.getStateInformation(fresh);          // (never booted, no working flash: a fresh flash)
        c.setStateInformation(fresh.getData(), (int)fresh.getSize());
        hc.block();
        const size_t dz = diffBytes(flashOf(c), flashA);
        check(dz != 0, "setStateInformation while running reboots on the new state's flash (" + std::to_string(dz) + " bytes changed)");
    }
    a.workingFlashFile().deleteFile();

    // ---- (f) presets
    {
        check(a.savePreset("T1"), "savePreset(\"T1\")");
        const juce::File pf = scratch.getChildFile("fm1emu/choralroot/presets/T1.fm1preset");
        check(pf.existsAsFile(), "the preset file " + pf.getFullPathName().toStdString() + " (" + std::to_string(pf.getSize()) + " bytes)");
        juce::MemoryBlock raw;
        pf.loadFileAsData(raw);
        const juce::String head = juce::String::fromUTF8((const char *)raw.getData(), juce::jmin(200, (int)raw.getSize())).upToFirstOccurrenceOf("\n", false, false);
        check(head.startsWith("{\"fm1preset\":1") && head.contains("\"core\":\"choralroot\"") && head.contains("\"name\":\"T1\""),
              "its header: " + head.toStdString());
        const std::vector<uint8_t> t1 = flashOf(a);
        check(a.listPresets().contains("T1") && a.getNumPrograms() == 1 && a.getProgramName(0) == "T1",
              "listPresets() and the host programs show T1");
        check(a.resetFlash(), "resetFlash()");
        ha.run_ms(50);
        const size_t dReset = diffBytes(flashOf(a), t1);
        check(dReset != 0, "after the reset the flash differs from T1 (" + std::to_string(dReset) + " bytes)");
        check(a.loadPreset("T1"), "loadPreset(\"T1\")");
        ha.run_ms(50);
        check(diffBytes(flashOf(a), t1) == 0 && a.currentPresetName() == "T1", "the flash is T1's again");
        const int nb = a.backupsDir().getNumberOfChildFiles(juce::File::findFiles, "*.fm1preset");
        check(nb >= 2, std::to_string(nb) + " backups in " + a.backupsDir().getFullPathName().toStdString() +
                           " (before the reset, before the load)");
        check(a.savePreset("T2") && a.renamePreset("T2", "T3") && a.listPresets().contains("T3") && !a.listPresets().contains("T2"),
              "save T2, rename to T3");
        const juce::File ex = scratch.getChildFile("export/Shared.fm1preset");
        check(a.exportPreset(ex) && ex.existsAsFile(), "exportPreset");
        check(a.importPreset(ex) == "Shared" && a.listPresets().contains("Shared"), "importPreset: \"Shared\" in the list");
        a.setCurrentProgram(a.listPresets().indexOf("T1"));   // (current: a no-op)
        check(a.currentPresetName() == "T1", "setCurrentProgram on the current program: no reload");
        check(a.deletePreset("T1") && !pf.exists() && !a.listPresets().contains("T1"), "deletePreset(\"T1\") removes it");
        a.deletePreset("T3");
        a.deletePreset("Shared");
    }

    // ---- the working flash: written when the instance is released
    a.releaseResources();
    check(a.workingFlashFile().existsAsFile() && a.workingFlashFile().getSize() == 0x100000,
          "releaseResources wrote the working flash " + a.workingFlashFile().getFullPathName().toStdString());

    // ---- (h) the firmware switch: ChoralRoot -> Felucca -> Melodee -> ChoralRoot
    {
        a.workingFlashFile().deleteFile();
        FM1Processor s;
        Host hs(s, 48000.0, 256);
        hs.run_ms(600);
        // Knob Select is "Tempo" on the view (a setting ChoralRoot keeps in its flash)
        KnobParameter *tempo = s.knobParam(EMU_E_SELECT);
        tempo->setValueNotifyingHost(tempo->toNorm(133));
        hs.run_ms(100);
        const std::vector<uint8_t> crFlash = s.currentFlash();
        const std::vector<uint8_t> crFresh = freshFlash;
        // the Tier 2 slots' names (none by default) and the knobs' functions (their names never change)
        auto slotNames = [&](int n) {
            std::string out;
            for (int i = 0; i < n && i < FM1Processor::kTier2Slots; i++)
                out += (i ? " | " : "") + s.tier2Param(i)->getName(100).toStdString();
            for (int r = 0; r < FM1Processor::kKnobs; r++) {
                const juce::String f = s.knobParam(r)->functionName();
                out += " / " + (f.isEmpty() ? std::string("turn") : f.toStdString());
            }
            return out;
        };
        auto names = [&]() {
            std::string out;
            for (const juce::String &b : s.buttonNames())
                out += " " + b.toStdString();
            return out;
        };
        const std::string crSlots = slotNames(8), crButtons = names();
        const int crBound = s.tier2Bound();
        std::printf("        (choralroot: %d Tier 2 slots bound, page 1: %s; buttons%s)\n", crBound, crSlots.c_str(),
                    crButtons.c_str());
        check(tempo->functionName() == "Tempo" && tempo->entry()->get() == 133 && diffBytes(crFlash, crFresh) != 0 &&
                  diffBytes(crFlash, crFresh) != (size_t)-1,
              "choralroot: host Tempo 133, its flash differs from a fresh unit's (" +
                  std::to_string(diffBytes(crFlash, crFresh)) + " bytes)");
        for (const char *id : {"felucca", "melodee"}) {
            const bool ok = s.switchCore(id);
            const FM1Processor::CoreStatus st = s.getCoreInfo();
            hs.peak = 0.0f;
            hs.run_ms(700);                       // (the power-on and its 430 ms splash; the first read-backs)
            s.drainTier2();
            const fm1core_t *c = s.deviceForTest() ? s.deviceForTest()->core() : nullptr;
            const std::string sl = slotNames(8), bn = names();
            std::printf("        (%s: %d Tier 2 slots bound, page 1: %s; buttons%s)\n", id, s.tier2Bound(), sl.c_str(),
                        bn.c_str());
            check(ok && st.loaded && st.info.id == id && c && !std::strcmp(c->id, id) && !s.getCoreInfo().halted,
                  std::string("switchCore(\"") + id + "\"): loaded and powered on (" + st.info.name + " " + st.info.version + ")");
            check(s.buttonParam(EMU_B_SEL)->getName(100) == "SEL (SEL)" && s.buttonNames()[EMU_B_ARP] == "ARP" &&
                      s.buttonParam(EMU_B_PLAY)->getName(100) == "PLAY (PLAY)" && bn != crButtons,
                  std::string(id) + ": the button parameters relabelled (\"" +
                      s.buttonParam(EMU_B_SEL)->getName(100).toStdString() + "\", was \"KEY (SEL)\")");
            int visible = 0;
            for (uint32_t i = 0; c && i < c->nparams; i++)
                visible += !(c->params[i].flags & FM1P_HIDDEN);
            if (FM1Processor::kTier2Slots > 0)
                check(c && s.tier2Bound() == visible && s.tier2Param(0)->getName(100) == "Level" && sl != crSlots &&
                          s.tier2Param(visible)->getName(100) == "(unused)",
                      std::string(id) + ": the Tier 2 slots rebound to its " + std::to_string(visible) +
                          " visible entries (slot 0 \"" + s.tier2Param(0)->getName(100).toStdString() + "\", the rest unused)");
            else
                check(c && s.tier2Bound() == 0 && sl != crSlots && s.knobParam(EMU_E_K1)->getName(100) == "Knob 1",
                      std::string(id) + ": no Tier 2 slots; the knobs follow its screen (" + sl + "), their names fixed");
            hs.note(true, 62);
            hs.run_ms(300);
            check(hs.peak > 0.01f && keyHeld(s, 9), std::string(id) + ": note 62 plays key 9 and sounds (peak " +
                                                        std::to_string(hs.peak) + ")");
            hs.note(false, 62);
            hs.run_ms(100);
        }
        const juce::File crWorking = FM1Processor::home().getChildFile("choralroot/flash.bin");
        juce::MemoryBlock saved;
        crWorking.loadFileAsData(saved);
        const std::vector<uint8_t> savedV((const uint8_t *)saved.getData(), (const uint8_t *)saved.getData() + saved.getSize());
        check(diffBytes(savedV, crFlash) == 0, "the switch away saved ChoralRoot's flash: " +
                                                   crWorking.getFullPathName().toStdString() + " equals it");
        check(s.switchCore("choralroot"), "switchCore(\"choralroot\")");
        const std::vector<uint8_t> back = flashOf(s);
        check(diffBytes(back, crFlash) == 0, "back on choralroot: it booted on its working flash (the bytes saved before "
                                             "the switch, " + std::to_string(back.size()) + ")");
        hs.run_ms(700);
        s.drainTier2();
        check(slotNames(8) == crSlots && s.tier2Bound() == crBound && names() == crButtons &&
                  tempo->entry() && tempo->entry()->get() == 133 && tempo->plainValue() == 133,
              "  ... its labels, its knobs and its " + std::to_string(crBound) +
                  " Tier 2 slots again, Tempo 133 (firmware and host)");
        hs.peak = 0.0f;
        hs.note(true, 62);
        hs.run_ms(300);
        check(hs.peak > 0.01f, "  ... and it plays (peak " + std::to_string(hs.peak) + ")");
        hs.note(false, 62);
        hs.run_ms(50);
    }

    // ---- (i) the button parameters' modes. A Roto-Control button (and most controller buttons mapped in Live) is a
    // toggle: it sends 1, and the value stays 1 until it is pressed again. Each mode drives btn_fx 0 -> 1 (stays 1
    // for 2 s) -> 0 and watches the device's FX bit block by block (256 frames at 48 kHz: 5.3 ms)
    {
        a.workingFlashFile().deleteFile();
        FM1Processor q;
        Host hq(q, 48000.0, 256);
        hq.run_ms(700);
        Device *d = q.deviceForTest();
        emu_hal_t *h = d->hal();
        struct Press {
            uint32_t down, up;                    // device ms (up: 0 while still down)
        };
        // run ms of device time, recording the label's presses on the HAL (down: the block's start, up: its end)
        auto watch = [&](int label, double ms, std::vector<Press> &out) {
            const uint32_t bit = 1u << h->btn_id[label];
            const uint64_t until = hq.frames + (uint64_t)(ms * hq.sr / 1000.0);
            while (hq.frames < until) {
                const uint32_t t0 = d->ms();
                const bool was = !out.empty() && !out.back().up;
                hq.block();
                const bool is = (h->buttons & bit) != 0;
                if (is && !was)
                    out.push_back({t0, 0});
                else if (!is && was)
                    out.back().up = d->ms();
            }
        };
        auto lens = [](const std::vector<Press> &v) {
            std::string o;
            for (const Press &p : v)
                o += (o.empty() ? "" : ", ") + (p.up ? std::to_string(p.up - p.down) + " ms" : std::string("held"));
            return o.empty() ? std::string("none") : o;
        };
        auto lit = [&](int label) { return d->led_button(label) == 2; };
        auto set = [&](int label, float v) { q.buttonParam(label)->setValueNotifyingHost(v); };
        const int tapMs = q.getButtonTapMs();
        check(q.getButtonMode() == FM1Processor::kButtonTap && tapMs == FM1Processor::kDefaultTapMs && tapMs == 90,
              "Button params: Tap by default, a tap of " + std::to_string(tapMs) + " ms of device time");

        // Tap: one press of ~90 ms on the rise, nothing on the fall; FX (ChoralRoot: the effects on / off) toggled once
        {
            const bool fx0 = lit(EMU_B_FX);
            std::vector<Press> v;
            set(EMU_B_FX, 1.0f);
            watch(EMU_B_FX, 2000, v);
            const bool fx1 = lit(EMU_B_FX), lockOpen = lit(EMU_B_OCTDN);
            set(EMU_B_FX, 0.0f);
            watch(EMU_B_FX, 500, v);
            const bool fx2 = lit(EMU_B_FX);
            const uint32_t len = v.size() == 1 && v[0].up ? v[0].up - v[0].down : 0;
            check(v.size() == 1 && len >= (uint32_t)tapMs && len <= (uint32_t)tapMs + 6,
                  "Tap: btn_fx 0 -> 1 (2 s) -> 0: one press on the device (" + lens(v) + "), none on the falling edge");
            check(fx1 != fx0 && fx2 == fx1 && !lockOpen,
                  std::string("Tap: FX toggled once (its LED ") + (fx0 ? "lit" : "off") + " -> " + (fx1 ? "lit" : "off") +
                      ", unchanged by the falling edge); no layer locked open (OCT- not lit)");
            v.clear();
            set(EMU_B_FX, 1.0f);                  // (the controller's next press: the next tap, FX back)
            watch(EMU_B_FX, 300, v);
            set(EMU_B_FX, 0.0f);
            watch(EMU_B_FX, 200, v);
            check(v.size() == 1 && lit(EMU_B_FX) == fx0, "Tap: the next rising edge is the next tap (" + lens(v) +
                                                             "), FX back as it was");
            // two rising edges inside one tap (a momentary button pressed twice within 90 ms): two taps, 30 ms apart
            v.clear();
            const double blk = 1000.0 * hq.bs / hq.sr;   // (one block)
            set(EMU_B_HOME, 1.0f);
            watch(EMU_B_HOME, blk, v);
            set(EMU_B_HOME, 0.0f);
            watch(EMU_B_HOME, blk, v);
            set(EMU_B_HOME, 1.0f);
            watch(EMU_B_HOME, blk, v);
            set(EMU_B_HOME, 0.0f);
            watch(EMU_B_HOME, 400, v);
            check(v.size() == 2 && v[0].up && v[1].up && v[0].up - v[0].down <= (uint32_t)tapMs + 6 &&
                      v[1].up - v[1].down <= (uint32_t)tapMs + 6 && v[1].down - v[0].up >= 24 && v[1].down - v[0].up <= 36,
                  "Tap: two rising edges within one tap: two taps (" + lens(v) + "), released " +
                      (v.size() == 2 && v[1].up ? std::to_string(v[1].down - v[0].up) : std::string("?")) +
                      " ms between them");
        }
        // OCT+ in Tap with a toggle (1, then it stays): exactly one octave up, no panic; ChoralRoot shifts the octave
        // on OCT's release, lights OCT+ while the octave is above 0, and holding both OCTs is panic (CC 123 out)
        {
            std::vector<Press> v;
            const bool up0 = lit(EMU_B_OCTUP), dn0 = lit(EMU_B_OCTDN);
            const int cc0 = hq.allNotesOff;
            set(EMU_B_OCTUP, 1.0f);
            watch(EMU_B_OCTUP, 2000, v);
            const bool up1 = lit(EMU_B_OCTUP);
            set(EMU_B_OCTUP, 0.0f);               // (the toggle's second press)
            watch(EMU_B_OCTUP, 300, v);
            const bool up2 = lit(EMU_B_OCTUP);
            std::vector<Press> w;
            set(EMU_B_OCTDN, 1.0f);               // one OCT- tap: back to octave 0 only if OCT+ moved it by exactly 1
            watch(EMU_B_OCTDN, 300, w);
            set(EMU_B_OCTDN, 0.0f);
            watch(EMU_B_OCTDN, 300, w);
            const bool up3 = lit(EMU_B_OCTUP), dn3 = lit(EMU_B_OCTDN);
            check(!up0 && !dn0 && v.size() == 1 && v[0].up && v[0].up - v[0].down <= (uint32_t)tapMs + 6 && up1 && up2,
                  "Tap: btn_octup 1 (2 s), then 0: one press (" + lens(v) + "), the octave up (OCT+ lit), the falling "
                  "edge nothing");
            check(w.size() == 1 && !up3 && !dn3, "  ... one OCT- tap brings it back to octave 0 (neither OCT lit): "
                                                 "exactly one octave shift");
            check(hq.allNotesOff == cc0, "  ... and no panic (no CC 123 out)");
        }
        // Hold: the value is the held state: held 2 s, FX's layer opens and stays (locked; OCT- lit to close it)
        {
            q.setButtonMode(FM1Processor::kButtonHold);
            std::vector<Press> v;
            const bool fx0 = lit(EMU_B_FX);
            set(EMU_B_FX, 1.0f);
            watch(EMU_B_FX, 2000, v);
            set(EMU_B_FX, 0.0f);
            watch(EMU_B_FX, 300, v);
            const bool locked = lit(EMU_B_OCTDN);
            const uint32_t len = v.size() == 1 && v[0].up ? v[0].up - v[0].down : 0;
            check(v.size() == 1 && len >= 1995 && len <= 2010 && locked,
                  "Hold: btn_fx held as long as it is 1 (" + lens(v) + "): the FX layer locked open (OCT- lit)");
            d->buttons_tap(1u << EMU_B_OCTDN);    // (OCT-: the layer closed)
            hq.run_ms(300);
            check(!lit(EMU_B_OCTDN) && lit(EMU_B_FX) == fx0, "  ... OCT- closes it; FX's on / off untouched by the hold");
        }
        // Toggle-hold: the first rise holds, the fall keeps it, the second rise lets go
        {
            q.setButtonMode(FM1Processor::kButtonToggleHold);
            std::vector<Press> v;
            set(EMU_B_FX, 1.0f);
            watch(EMU_B_FX, 2000, v);
            const bool held1 = v.size() == 1 && !v[0].up;
            set(EMU_B_FX, 0.0f);
            watch(EMU_B_FX, 300, v);
            const bool held2 = v.size() == 1 && !v[0].up;
            set(EMU_B_FX, 1.0f);
            watch(EMU_B_FX, 300, v);
            const bool released = v.size() == 1 && v[0].up;
            set(EMU_B_FX, 0.0f);
            watch(EMU_B_FX, 300, v);
            const bool locked = lit(EMU_B_OCTDN);
            check(held1 && held2 && released && v.size() == 1 && locked,
                  "Toggle-hold: held from the first rise (through the fall), released at the second (" + lens(v) +
                      "): the FX layer held open, then locked");
            d->buttons_tap(1u << EMU_B_OCTDN);
            hq.run_ms(300);
        }
        // the panic detector itself: Hold, both OCTs held = panic, with a note sounding
        {
            q.setButtonMode(FM1Processor::kButtonHold);
            const int cc0 = hq.allNotesOff;
            hq.note(true, 62);
            hq.run_ms(200);
            set(EMU_B_OCTDN, 1.0f);
            set(EMU_B_OCTUP, 1.0f);
            hq.run_ms(200);
            set(EMU_B_OCTDN, 0.0f);
            set(EMU_B_OCTUP, 0.0f);
            hq.note(false, 62);
            hq.run_ms(300);
            check(hq.allNotesOff > cc0, "(the check above sees a panic: Hold, both OCTs held, " +
                                            std::to_string(hq.allNotesOff - cc0) + " CC 123 out)");
        }
        // the state keeps the mode and the tap length
        {
            q.setButtonMode(FM1Processor::kButtonToggleHold);
            q.setButtonTapMs(120);
            juce::MemoryBlock st;
            q.getStateInformation(st);
            FM1Processor r;
            r.setStateInformation(st.getData(), (int)st.getSize());
            check(r.getButtonMode() == FM1Processor::kButtonToggleHold && r.getButtonTapMs() == 120,
                  "the state keeps Button params (Toggle-hold) and the tap length (120 ms)");
        }
    }

    std::printf("plugin_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
