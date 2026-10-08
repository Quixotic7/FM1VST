// SPDX-License-Identifier: GPL-3.0-only
// The plugin's Tier 2 parameters (FM1-VST-PLAN.md 4.5), headless, on the plugin's shared code: the slots and their
// names, a host write reaching the firmware (and not echoed back), a panel turn reaching the host (with a gesture),
// the full report after a state restore and a preset load (no gestures), the perform mode changed on the panel, the
// value texts; (6) after switchCore("felucca"): Felucca's map on the same slots, a host write, the part selected on
// the panel (ALGORITHM) relabelling the engine slots (an engine slot's range follows the engine: ANALOG's WAVE 5
// values, FM6's ALG 33). The eight knob parameters in front of the slots: knob_test. The message thread's feedback timer is not running here
// (no message loop): the test calls the drain itself between blocks. FM1EMU_HOME points under the build.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "Processor.h"

static int fails = 0;
static void check(bool c, const std::string &what)
{
    std::printf("  %s  %s\n", c ? "ok  " : "FAIL", what.c_str());
    fails += !c;
}

struct Host {
    FM1Processor &p;
    double sr;
    int bs;
    juce::AudioBuffer<float> buf;
    uint64_t frames = 0;
    Host(FM1Processor &proc, double rate, int block) : p(proc), sr(rate), bs(block), buf(2, block)
    {
        p.setRateAndBufferSizeDetails(rate, block);
        p.prepareToPlay(rate, block);
    }
    void block()
    {
        juce::MidiBuffer m;
        buf.clear();
        p.processBlock(buf, m);
        frames += (uint64_t)bs;
    }
    void run_ms(double ms)
    {
        const uint64_t until = frames + (uint64_t)(ms * sr / 1000.0);
        while (frames < until)
            block();
    }
};

// what the host would see: every value change and gesture of the Tier 2 slots, and the processor's info changes
struct Watch : juce::AudioProcessorParameter::Listener, juce::AudioProcessorListener {
    std::map<int, int> changes, begins, ends;     // by parameter index
    std::map<int, float> last;
    int infoChanged = 0;
    void parameterValueChanged(int i, float v) override
    {
        changes[i]++;
        last[i] = v;
    }
    void parameterGestureChanged(int i, bool starting) override { (starting ? begins : ends)[i]++; }
    void audioProcessorParameterChanged(juce::AudioProcessor *, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor *, const ChangeDetails &d) override { infoChanged += d.parameterInfoChanged; }
    void clear()
    {
        changes.clear();
        begins.clear();
        ends.clear();
        last.clear();
        infoChanged = 0;
    }
};

static Tier2Parameter *slotNamed(FM1Processor &p, const juce::String &name)
{
    for (int i = 0; i < FM1Processor::kTier2Slots; i++)
        if (p.tier2Param(i)->getName(100) == name)
            return p.tier2Param(i);
    return nullptr;
}

int main()
{
    if (FM1Processor::kTier2Slots == 0) {             // (the default: no slots; the knobs are knob_test's)
        std::printf("tier2_test: SKIP (FM1_TIER2_SLOTS 0: no Tier 2 slots; configure with -DFM1_TIER2_SLOTS=79)\n");
        return 77;
    }
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("tier2_test: FM1EMU_HOME=%s\n", scratch.getFullPathName().toRawUTF8());

    FM1Processor p;
    p.setButtonMode(FM1Processor::kButtonHold);   // (the button parameters held as long as they are on: layers)
    Watch w;
    p.addListener(&w);
    for (int i = 0; i < FM1Processor::kTier2Slots; i++)
        p.tier2Param(i)->addListener(&w);
    const int n = p.tier2Bound();
    auto idx = [](Tier2Parameter *t) { return t ? t->getParameterIndex() : -1; };

    // ---- (5) the slots: the count, the names
    {
        int longest = 0, unused = 0;
        bool namesOk = true;
        for (int i = 0; i < FM1Processor::kTier2Slots; i++) {
            const juce::String nm = p.tier2Param(i)->getName(1000);
            longest = std::max(longest, nm.length());
            if (i >= n)
                unused += nm == "(unused)";
            namesOk = namesOk && nm.isNotEmpty() && (i < n) == (nm != "(unused)");
        }
        check(n == 73 && p.getParameters().size() == 8 + FM1Processor::kTier2Slots + 41,
              std::to_string(n) + " of " + std::to_string(FM1Processor::kTier2Slots) + " slots bound (ChoralRoot's map), " +
                  std::to_string(p.getParameters().size()) + " host parameters in all");
        check(namesOk && unused == FM1Processor::kTier2Slots - n, "the slots beyond the map are \"(unused)\" (" +
                                                                       std::to_string(unused) + ")");
        check(longest <= kTier2NameMax, "no slot name longer than " + std::to_string(kTier2NameMax) + " (longest " +
                                            std::to_string(longest) + ")");
        std::printf("        (the live page: ");
        for (int i = 0; i < 8; i++)
            std::printf("%s%s", i ? " | " : "", p.tier2Param(i)->getName(100).toRawUTF8());
        std::printf(")\n");
    }

    Host h(p, 48000.0, 256);
    h.run_ms(600);

    // ---- the power-on's full report
    w.clear();
    p.drainTier2();
    {
        int reported = 0, gestures = 0;
        for (int i = 0; i < n; i++) {
            reported += w.changes.count(idx(p.tier2Param(i))) > 0;
            gestures += w.begins.count(idx(p.tier2Param(i))) > 0;
        }
        check(reported == n && gestures == 0, "after the power-on the first drain reports every bound slot (" +
                                                  std::to_string(reported) + "), no gestures");
        check(p.tier2Param(0)->getName(100) == "Voicing" && p.tier2Param(1)->getName(100) == "Tempo",
              "slot 0 \"" + p.tier2Param(0)->getName(100).toStdString() + "\", slot 1 \"" +
                  p.tier2Param(1)->getName(100).toStdString() + "\" (the map's first visible entries)");
    }

    Tier2Parameter *tempo = slotNamed(p, "Tempo");
    Tier2Parameter *ad = slotNamed(p, "Arp Division");
    check(tempo && ad && tempo->entry() && tempo->entry()->get() == 120 && tempo->plainValue() == 120,
          "slot \"Tempo\": the firmware's 120 BPM, the host's value too");
    if (!tempo || !ad)
        return 1;

    // ---- (1) the host writes Tempo: the firmware follows, nothing is echoed back
    {
        tempo->setValueNotifyingHost(tempo->toNorm(140));   // (the test thread: as a host's automation thread)
        w.clear();
        h.run_ms(100);
        const int32_t fw = tempo->entry()->get();
        p.drainTier2();
        check(fw == 140, "host Tempo 140 -> after 100 ms of blocks the firmware's tempo is " + std::to_string(fw));
        check(w.changes.count(idx(tempo)) == 0, "the host's own write is not echoed back (no update for Tempo)");
        check(w.changes.empty(), "nothing else moved (" + std::to_string(w.changes.size()) + " slots reported)");
    }

    // ---- (2) the panel turns the tempo (SELECT on the view, +5): the host's value follows, inside a gesture
    {
        w.clear();
        p.deviceForTest()->enc(EMU_E_SELECT, 5);
        h.run_ms(100);
        p.drainTier2();
        const int i = idx(tempo);
        const bool moved = w.changes.count(i) && w.changes[i] == 1 && tempo->plainValue() == 145;
        check(moved, "SELECT +5 on the panel: the host's Tempo moves once per drain, to " +
                         std::to_string(tempo->plainValue()) + " (\"" + tempo->getCurrentValueAsText().toStdString() + "\")");
        check(w.begins[i] == 1 && w.ends[i] == 1, "  ... inside one beginChangeGesture / endChangeGesture");
        // several turns between two drains: coalesced into one update
        w.clear();
        for (int k = 0; k < 4; k++) {
            p.deviceForTest()->enc(EMU_E_SELECT, 1);
            h.run_ms(40);
        }
        p.drainTier2();
        check(w.changes[i] == 1 && tempo->plainValue() == 149, "four turns between drains: one update, 149");
        // a write and a panel turn of the same slot do not fight: the host's write, then the panel's, both win in order
        tempo->setValueNotifyingHost(tempo->toNorm(120));
        h.run_ms(60);
        p.drainTier2();
        check(tempo->entry()->get() == 120 && tempo->plainValue() == 120, "the host writes 120 again: firmware 120");
    }

    // ---- the texts the host shows
    {
        const juce::String t6 = ad->getText(ad->toNorm(6), 100);
        const float back = ad->getValueForText("1/16");
        check(t6 == "1/8" && ad->toPlain(back) == 8 && ad->isDiscrete() && ad->getNumSteps() == 12,
              "Arp Division: text(6) \"" + t6.toStdString() + "\", \"1/16\" -> 8, discrete, 12 steps");
        Tier2Parameter *sr = slotNamed(p, "Strum Rate");
        check(sr && sr->getText(sr->toNorm(120), 100) == "120 ms" && !sr->isDiscrete() &&
                  tempo->getText(tempo->toNorm(120), 100) == "120 BPM",
              "Strum Rate \"120 ms\" (continuous), Tempo \"120 BPM\"");
        check(ad->getAllValueStrings().size() == 12 && ad->getAllValueStrings()[11] == "1/32T",
              "Arp Division's value strings: 12, the last \"1/32T\"");
    }

    // ---- (4) the perform mode changed on the panel: "Perform Mode" reports it; a host write of Arp Division
    {
        w.clear();
        // PERF (ARP) held 500 ms: the perform picker locks open; SELECT +3: Arpeggiate (scripts/cr_perf_row.txt)
        p.buttonParam(EMU_B_ARP)->setValueNotifyingHost(1.0f);
        h.run_ms(500);
        p.buttonParam(EMU_B_ARP)->setValueNotifyingHost(0.0f);
        h.run_ms(300);
        p.deviceForTest()->enc(EMU_E_SELECT, 3);
        h.run_ms(400);
        p.drainTier2();
        Tier2Parameter *pm = slotNamed(p, "Perform Mode");
        check(pm && pm->plainValue() == 3 && pm->getCurrentValueAsText() == "Arpeggiate" && w.changes.count(idx(pm)),
              "PERF held + SELECT +3: \"Perform Mode\" reports Arpeggiate");
        ad->setValueNotifyingHost(ad->toNorm(2));
        w.clear();
        h.run_ms(100);
        p.drainTier2();
        check(ad->entry()->get() == 2 && ad->plainValue() == 2 && !w.changes.count(idx(ad)),
              "host Arp Division = 2: the firmware's Arp Division 2, not echoed");
        p.deviceForTest()->buttons_tap(1u << EMU_B_OCTDN);   // (OCT-: the picker closed)
        h.run_ms(200);
        p.drainTier2();
    }

    // ---- (3) a state restore and a preset load report every slot, without gestures
    {
        juce::MemoryBlock state;
        p.getStateInformation(state);                // (Tempo 120, Arpeggiate, Arp Division 2)
        tempo->setValueNotifyingHost(tempo->toNorm(90));
        h.run_ms(100);
        p.drainTier2();
        w.clear();
        p.setStateInformation(state.getData(), (int)state.getSize());
        h.run_ms(600);
        p.drainTier2();
        int reported = 0, gestures = 0;
        for (int i = 0; i < n; i++) {
            reported += w.changes.count(idx(p.tier2Param(i))) > 0;
            gestures += w.begins.count(idx(p.tier2Param(i))) > 0;
        }
        check(reported == n && gestures == 0, "setStateInformation: every bound slot reported (" + std::to_string(reported) +
                                                  "), no gestures");
        check(tempo->plainValue() == 120 && ad->plainValue() == 2 && p.tier2Param(0)->getName(100) == "Voicing",
              "  ... the state's values: Tempo " + std::to_string(tempo->plainValue()) + ", Arp Division " +
                  std::to_string(ad->plainValue()) + ", slot 0 \"" + p.tier2Param(0)->getName(100).toStdString() + "\"");

        check(p.savePreset("Fast"), "savePreset(\"Fast\") at Tempo 120");
        tempo->setValueNotifyingHost(tempo->toNorm(200));
        h.run_ms(100);
        p.drainTier2();
        w.clear();
        check(p.loadPreset("Fast"), "loadPreset(\"Fast\") at Tempo 200");
        h.run_ms(600);
        p.drainTier2();
        reported = gestures = 0;
        for (int i = 0; i < n; i++) {
            reported += w.changes.count(idx(p.tier2Param(i))) > 0;
            gestures += w.begins.count(idx(p.tier2Param(i))) > 0;
        }
        check(reported == n && gestures == 0 && tempo->plainValue() == 120,
              "loadPreset: every bound slot reported (" + std::to_string(reported) + "), no gestures, Tempo back to " +
                  std::to_string(tempo->plainValue()));
        // after the sweep, quiet: nothing more is reported while nothing changes
        w.clear();
        h.run_ms(300);
        p.drainTier2();
        check(w.changes.empty(), "300 ms later with nothing touched: no updates (" + std::to_string(w.changes.size()) + ")");
    }

    // ---- an unused slot is inert
    {
        Tier2Parameter *u = p.tier2Param(FM1Processor::kTier2Slots - 1);
        u->setValueNotifyingHost(0.7f);
        w.clear();
        h.run_ms(50);
        p.drainTier2();
        check(u->entry() == nullptr && w.changes.empty() && u->getText(0.7f, 100).isEmpty(),
              "an \"(unused)\" slot: a write does nothing, no text");
    }

    // ---- (6) Felucca's map on the same slots
    {
        check(p.switchCore("felucca"), "switchCore(\"felucca\")");
        w.clear();
        h.run_ms(700);
        p.drainTier2();
        const int nf = p.tier2Bound();
        const fm1core_t *c = p.deviceForTest()->core();
        Tier2Parameter *k1 = p.tier2Param(0), *e1 = slotNamed(p, "E1 WAVE"), *lvl = slotNamed(p, "Level");
        int visible = 0;
        for (uint32_t i = 0; i < c->nparams; i++)
            visible += !(c->params[i].flags & FM1P_HIDDEN);
        std::printf("        (felucca's live page: ");
        for (int i = 0; i < 8; i++)
            std::printf("%s%s", i ? " | " : "", p.tier2Param(i)->getName(100).toRawUTF8());
        std::printf(")\n");
        check(nf == visible && k1->getName(100) == "Level" && e1 && lvl && w.infoChanged > 0,
              std::to_string(nf) + " slots bound to Felucca's visible map; slot 0 \"" + k1->getName(100).toStdString() +
                  "\", \"E1 WAVE\", \"Level\"");
        if (e1 && lvl) {
            check(e1->getNumSteps() == 5 && e1->isDiscrete() && e1->getCurrentValueAsText() == "SAW",
                  "E1 WAVE: 5 values, discrete, \"" + e1->getCurrentValueAsText().toStdString() + "\"");
            lvl->setValueNotifyingHost(lvl->toNorm(90));
            h.run_ms(60);
            check(lvl->entry()->get() == 90, "host Level 90: the part's level " + std::to_string(lvl->entry()->get()) +
                                                 " (\"" + lvl->getCurrentValueAsText().toStdString() + "\")");
            w.clear();
            p.deviceForTest()->enc(EMU_E_ALGO, 1);       // ALGORITHM +1: part 2 (FM6)
            h.run_ms(200);
            p.drainTier2();
            check(slotNamed(p, "E1 ALG") == e1 && e1->getNumSteps() == 33 && w.infoChanged > 0 && w.changes.count(idx(lvl)),
                  "ALGORITHM +1 (part 2, FM6): the engine slot \"" + e1->getName(100).toStdString() + "\" with " +
                      std::to_string(e1->getNumSteps()) + " steps, parameterInfoChanged, Level reports part 2's (" +
                      std::to_string(lvl->plainValue()) + ")");
            Tier2Parameter *mlvl = slotNamed(p, "E3 MLVL");
            if (mlvl) {
                mlvl->setValueNotifyingHost(mlvl->toNorm(20));
                h.run_ms(60);
                p.drainTier2();
            }
            check(mlvl && mlvl->entry()->get() == 20 && mlvl->plainValue() == 20,
                  "host E3 MLVL = 20: FM6's MLVL is 20 (\"" + (mlvl ? mlvl->getCurrentValueAsText().toStdString() : "?") + "\")");
        }
        check(p.switchCore("choralroot"), "switchCore(\"choralroot\"): back");
        h.run_ms(700);
        p.drainTier2();
        check(p.tier2Bound() == n && p.tier2Param(0)->getName(100) == "Voicing", "ChoralRoot's map again (" +
                                                                                        p.tier2Param(0)->getName(100).toStdString() + ")");
    }

    p.removeListener(&w);
    for (int i = 0; i < FM1Processor::kTier2Slots; i++)
        p.tier2Param(i)->removeListener(&w);
    std::printf("tier2_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
