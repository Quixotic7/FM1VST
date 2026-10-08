// SPDX-License-Identifier: GPL-3.0-only
// The eight physical knobs as host parameters (FM1-VST-PLAN.md 4.5: the Roto-Control's first page), headless, on the
// plugin's shared code: each knob parameter is whatever that knob does on the firmware's screen (the core's
// knob_target), relabelled when the screen changes, and a relative control where the knob has no value.
// ChoralRoot: (a) the view: KNOB1 Voicing, a host write moves the firmware; (b) the PERF layer opened: KNOB1..4 the
// mode's row, parameterInfoChanged; (c) the perform mode switched from the host (SELECT = Perform Mode in the layer):
// the row relabels again; (f) a panel turn of KNOB1 in the layer reports on knob_1 and on the target's own slot,
// with a gesture; (d) the FX layer: the effect's row, SELECT the effect; (e) Options: SELECT has no value there, a
// host turn 0.5 -> 0.75 is +6 detents at the device (the cursor moves: KNOB1 relabels to the row 6 down), the knob
// springs back to 0.5 within 500 ms, the re-centre makes no detents; a row no named entry covers (MIDI Perform): the
// hidden "Option" entry; (g) FM1P_HIDDEN entries have no Tier 2 slot; the sound editor: KNOB1 bound to the cell.
// Felucca: HOME's knobs (the engine's), ALGORITHM = Part; the ENV page; ENV DEST's empty column relative; SLICER (a
// page no named entry covers: the hidden "Knob 1" cell); the GLO and FX layers held; EDIT 1. Melodee: HOME. The
// message loop is not running: the test calls the drain itself. FM1EMU_HOME points under the build.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

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
    void run_ms(double ms)
    {
        const uint64_t until = frames + (uint64_t)(ms * sr / 1000.0);
        while (frames < until) {
            juce::MidiBuffer m;
            buf.clear();
            p.processBlock(buf, m);
            frames += (uint64_t)bs;
        }
    }
};

struct Watch : juce::AudioProcessorParameter::Listener, juce::AudioProcessorListener {
    std::map<int, int> changes, begins;
    std::map<int, float> last;
    int infoChanged = 0;
    void parameterValueChanged(int i, float v) override
    {
        changes[i]++;
        last[i] = v;
    }
    void parameterGestureChanged(int i, bool starting) override
    {
        if (starting)
            begins[i]++;
    }
    void audioProcessorParameterChanged(juce::AudioProcessor *, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor *, const ChangeDetails &d) override { infoChanged += d.parameterInfoChanged; }
    void clear()
    {
        changes.clear();
        begins.clear();
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
static std::string nm(KnobParameter *k) { return k->getName(100).toStdString(); }

int main()
{
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("knob_test: FM1EMU_HOME=%s\n", scratch.getFullPathName().toRawUTF8());

    FM1Processor p;
    Watch w;
    p.addListener(&w);
    for (auto *q : p.getParameters())
        q->addListener(&w);
    KnobParameter *sel = p.knobParam(EMU_E_SELECT), *pre = p.knobParam(EMU_E_PRESETS), *alg = p.knobParam(EMU_E_ALGO);
    KnobParameter *k[4] = {p.knobParam(EMU_E_K1), p.knobParam(EMU_E_K2), p.knobParam(EMU_E_K3), p.knobParam(EMU_E_K4)};
    auto row = [&]() {
        std::string s;
        for (int i = 0; i < 4; i++)
            s += (i ? " | " : "") + nm(k[i]);
        return s;
    };
    auto page = [&]() { return nm(sel) + " | " + nm(pre) + " | " + nm(alg) + " | " + row(); };
    auto idx = [](juce::AudioProcessorParameter *q) { return q ? q->getParameterIndex() : -1; };

    // the order: MASTER SELECT PRESETS ALGORITHM KNOB1..4 first
    {
        const auto &ps = p.getParameters();
        check(ps[0] == p.masterParam() && ps[1] == sel && ps[2] == pre && ps[3] == alg && ps[4] == k[0] && ps[7] == k[3] &&
                  p.masterParam()->getName(100) == "MASTER",
              "parameters 0..7: MASTER SELECT PRESETS ALGORITHM KNOB1..4");
    }

    Host h(p, 48000.0, 256);
    h.run_ms(600);
    w.clear();
    p.drainTier2();
    const int voicingIdx = idx(slotNamed(p, "Voicing"));
    // ---- (a) the view
    {
        std::printf("        (the view: %s)\n", page().c_str());
        check(nm(k[0]) == "KNOB1: Voicing" && nm(sel) == "SELECT: Tempo" && nm(k[1]) == "KNOB2: Bass Register" &&
                  nm(k[2]) == "KNOB3: Strum Rate" && nm(k[3]) == "KNOB4: Chord Reverb" && nm(pre) == "PRESETS (turn)" &&
                  nm(alg) == "ALGORITHM (turn)" && w.infoChanged > 0,
              "the view: KNOB1 Voicing, KNOB2 Bass Register, KNOB3 Strum Rate (the mode's first), KNOB4 the FX amount, "
              "SELECT Tempo; PRESETS / ALGORITHM (the sound lists) relative; parameterInfoChanged");
        check(k[0]->getName(8) == "K1 Voici" && sel->getName(12) == "SEL Tempo", "a short display: \"" +
                  k[0]->getName(8).toStdString() + "\", \"" + sel->getName(12).toStdString() + "\"");
        k[0]->setValueNotifyingHost(k[0]->toNorm(3));
        w.clear();
        h.run_ms(100);
        p.drainTier2();
        Tier2Parameter *vo = slotNamed(p, "Voicing");
        check(vo && vo->entry()->get() == 3 && vo->plainValue() == 3 && !w.changes.count(idx(k[0])),
              "host KNOB1 = 3: the firmware's voicing 3, reported on \"Voicing\", not echoed on KNOB1");
        (void)voicingIdx;
    }
    // ---- (b) the PERF layer opened (PERF held 500 ms: it locks open; scripts/cr_perf_row.txt)
    {
        w.clear();
        p.buttonParam(EMU_B_ARP)->setValueNotifyingHost(1.0f);
        h.run_ms(500);
        p.buttonParam(EMU_B_ARP)->setValueNotifyingHost(0.0f);
        h.run_ms(300);
        p.drainTier2();
        std::printf("        (PERF: %s)\n", page().c_str());
        check(row() == "KNOB1: Strum Rate | KNOB2: Strum Dir | KNOB3: Strum Range | KNOB4: Strum Hold" &&
                  nm(sel) == "SELECT: Perform Mode" && w.infoChanged > 0 && w.changes.count(idx(k[1])),
              "PERF held: KNOB1..4 relabel to Strum's row, SELECT to Perform Mode, parameterInfoChanged, values pushed");
        check(k[0]->plainValue() == slotNamed(p, "Strum Rate")->plainValue() && k[3]->isDiscrete() &&
                  k[3]->getCurrentValueAsText() == slotNamed(p, "Strum Hold")->getCurrentValueAsText(),
              "  ... KNOB1 has Strum Rate's value (" + std::to_string(k[0]->plainValue()) + "), KNOB4 Hold's text \"" +
                  k[3]->getCurrentValueAsText().toStdString() + "\"");
    }
    // ---- (c) the perform mode switched (from the host: SELECT is the PERF picker here)
    Tier2Parameter *ad = slotNamed(p, "Arp Division");
    {
        w.clear();
        sel->setValueNotifyingHost(sel->toNorm(3));       // Arpeggiate
        h.run_ms(200);
        p.drainTier2();
        std::printf("        (PERF, Arpeggiate: %s)\n", row().c_str());
        check(row() == "KNOB1: Arp Division | KNOB2: Arp Dir | KNOB3: Arp Gate | KNOB4: Arp Swing" &&
                  w.infoChanged > 0 && k[0]->plainValue() == ad->plainValue() && k[0]->getNumSteps() == 12 &&
                  k[0]->getCurrentValueAsText() == ad->getCurrentValueAsText(),
              "host SELECT = Arpeggiate: KNOB1..4 relabel to Arp's row; KNOB1 is Arp Division (" +
                  k[0]->getCurrentValueAsText().toStdString() + ", 12 steps)");
    }
    // ---- (f) a panel turn of KNOB1 in the layer: on knob_1 and on Arp Division's own slot, inside a gesture
    {
        const int32_t before = ad->plainValue();
        w.clear();
        p.panelEnc(EMU_E_K1, 2);
        h.run_ms(100);
        p.drainTier2();
        check(ad->plainValue() == before + 2 && k[0]->plainValue() == before + 2 && w.changes.count(idx(k[0])) &&
                  w.changes.count(idx(ad)) && w.begins[idx(k[0])] == 1 && w.begins[idx(ad)] == 1,
              "the panel's KNOB1 +2: Arp Division " + std::to_string(before) + " -> " + std::to_string(ad->plainValue()) +
                  ", reported on knob_1 and on \"Arp Division\", each inside a gesture");
        p.deviceForTest()->buttons_tap(1u << EMU_B_OCTDN);   // (OCT-: the layer closed)
        h.run_ms(200);
        w.clear();
        p.drainTier2();
        check(nm(k[0]) == "KNOB1: Voicing" && nm(k[2]) == "KNOB3: Arp Division" && w.infoChanged > 0,
              "OCT- (the view again): KNOB1 Voicing, KNOB3 the new mode's first knob, Arp Division");
    }
    // ---- (d) the FX layer
    {
        w.clear();
        p.buttonParam(EMU_B_FX)->setValueNotifyingHost(1.0f);
        h.run_ms(500);
        p.buttonParam(EMU_B_FX)->setValueNotifyingHost(0.0f);
        h.run_ms(300);
        p.drainTier2();
        std::printf("        (FX: %s)\n", page().c_str());
        check(row() == "KNOB1: Reverb Size | KNOB2: Reverb Damp | KNOB3: Reverb Type | KNOB4: Chord Reverb" &&
                  nm(sel) == "SELECT: FX Effect" && w.infoChanged > 0,
              "FX held: KNOB1..4 the Reverb row (size, damp, type, the amount), SELECT the effect (\"FX Effect\", hidden)");
        sel->setValueNotifyingHost(sel->toNorm(2));       // Delay
        h.run_ms(200);
        p.drainTier2();
        check(row() == "KNOB1: Delay Time | KNOB2: Delay Feedback | KNOB3: Delay Colour | KNOB4: Chord Delay",
              "host SELECT = Delay: " + row());
        sel->setValueNotifyingHost(sel->toNorm(1));       // Chorus: no third parameter
        h.run_ms(200);
        p.drainTier2();
        check(nm(k[0]) == "KNOB1: Chorus Rate" && nm(k[2]) == "KNOB3 (turn)" && k[2]->isRelative(),
              "Chorus: KNOB3 has nothing there: \"" + nm(k[2]) + "\"");
        sel->setValueNotifyingHost(sel->toNorm(0));
        h.run_ms(100);
        p.deviceForTest()->buttons_tap(1u << EMU_B_OCTDN);
        h.run_ms(200);
        p.drainTier2();
    }
    // ---- (e) Options: SELECT is the cursor (relative), KNOB1 the row's value
    {
        p.deviceForTest()->buttons_tap(1u << EMU_B_GLO);   // (OPT tapped: Options)
        h.run_ms(200);
        w.clear();
        p.drainTier2();
        std::printf("        (Options: %s)\n", page().c_str());
        const std::string row0 = nm(k[0]);
        check(sel->isRelative() && nm(sel) == "SELECT (turn)" && sel->getValue() == 0.5f && !sel->isDiscrete() &&
                  row0 == "KNOB1: Play Style",
              "Options: SELECT (the cursor) relative at 0.5, KNOB1 the first row: \"" + row0 + "\"");
        const int32_t sent0 = p.knobDetentsSent(EMU_E_SELECT);
        sel->setValueNotifyingHost(0.75f);
        h.run_ms(60);
        p.drainTier2();
        const int32_t d1 = p.knobDetentsSent(EMU_E_SELECT) - sent0;
        check(d1 == 6 && nm(k[0]) == "KNOB1: Split Point",
              "host SELECT 0.5 -> 0.75: " + std::to_string(d1) + " detents at the device; the cursor moved 6 rows: \"" +
                  nm(k[0]) + "\"");
        w.clear();
        h.run_ms(500);
        p.drainTier2();
        const int32_t d2 = p.knobDetentsSent(EMU_E_SELECT) - sent0;
        check(sel->getValue() == 0.5f && w.changes.count(idx(sel)) && w.last[idx(sel)] == 0.5f && !w.begins.count(idx(sel)) &&
                  d2 == 6 && nm(k[0]) == "KNOB1: Split Point",
              "500 ms later: SELECT re-centred to 0.5 (the host told, no gesture), no detents from it (still " +
                  std::to_string(d2) + "), the cursor stays");
        sel->setValueNotifyingHost(0.5f + 1.0f / 24.0f);   // one row down: MIDI Perform (no named entry)
        h.run_ms(60);
        p.drainTier2();
        check(nm(k[0]) == "KNOB1: MIDI Perform" && !k[0]->isRelative() && k[0]->getNumSteps() == 17,
              "+1 detent: KNOB1 \"" + nm(k[0]) + "\" (the hidden \"Option\" entry rewritten), 17 steps");
        k[0]->setValueNotifyingHost(k[0]->toNorm(5));
        h.run_ms(60);
        p.drainTier2();
        check(k[0]->entry() && k[0]->entry()->get() == 5 && k[0]->getCurrentValueAsText() == "ch 5",
              "host KNOB1 = 5: MIDI Perform \"" + k[0]->getCurrentValueAsText().toStdString() + "\"");
        k[0]->setValueNotifyingHost(k[0]->toNorm(1));
        h.run_ms(60);
        p.deviceForTest()->buttons_tap(1u << EMU_B_GLO);   // (Options closed)
        h.run_ms(600);
        p.drainTier2();
        check(nm(sel) == "SELECT: Tempo" && nm(k[0]) == "KNOB1: Voicing", "OPT again: the view (" + nm(sel) + ", " +
                                                                             nm(k[0]) + ")");
    }
    // ---- (g) the hidden entries have no slot; the sound editor's cells are knob targets
    {
        const fm1core_t *c = p.deviceForTest()->core();
        bool none = true;
        int hidden = 0;
        for (int i = 0; i < p.tier2Bound(); i++)
            none = none && !(p.tier2Param(i)->entry()->flags & FM1P_HIDDEN);
        for (uint32_t i = 0; i < c->nparams; i++)
            hidden += (c->params[i].flags & FM1P_HIDDEN) != 0;
        check(none && hidden > 0 && !slotNamed(p, "FX Effect") && !slotNamed(p, "Chord Attack") && !slotNamed(p, "Option"),
              std::to_string(hidden) + " FM1P_HIDDEN entries, none on a Tier 2 slot (\"FX Effect\", \"Chord Attack\" ..)");
        p.deviceForTest()->buttons_tap(1u << EMU_B_EDIT);  // EDIT: the chord sound's editor
        h.run_ms(300);
        w.clear();
        p.drainTier2();
        std::printf("        (the editor: %s)\n", page().c_str());
        const fm1param_t *e = k[0]->entry();
        bool moved = false;
        if (e) {
            const int32_t v0 = e->get(), want = v0 < e->max ? v0 + 1 : v0 - 1;
            k[0]->setValueNotifyingHost(k[0]->toNorm(want));
            h.run_ms(60);
            moved = e->get() == want;
            k[0]->setValueNotifyingHost(k[0]->toNorm(v0));
            h.run_ms(60);
        }
        check(e && !k[0]->isRelative() && nm(sel) == "SELECT (turn)" && w.infoChanged > 0 && moved,
              "EDIT: KNOB1 is the editor's cell (\"" + nm(k[0]) + "\"), a host write moves it; SELECT (the lanes) relative");
        p.deviceForTest()->buttons_tap(1u << EMU_B_ENV);   // the ENV section
        h.run_ms(300);
        p.drainTier2();
        std::printf("        (the editor, ENV: %s)\n", row().c_str());
        check(!k[0]->isRelative() && !k[3]->isRelative() && w.infoChanged > 0, "ENV: KNOB1..4 bound (" + row() + ")");
        p.deviceForTest()->buttons_tap(1u << EMU_B_HOME);
        h.run_ms(300);
        p.drainTier2();
        check(nm(k[0]) == "KNOB1: Voicing", "HOME: the view again (" + nm(k[0]) + ")");
    }

    // ---- Felucca
    {
        check(p.switchCore("felucca"), "switchCore(\"felucca\")");
        h.run_ms(700);
        w.clear();
        p.drainTier2();
        std::printf("        (felucca HOME: %s)\n", page().c_str());
        check(row() == "KNOB1: E5 CUT | KNOB2: E6 RES | KNOB3: Attack | KNOB4: Release" && nm(sel) == "SELECT: Tempo" &&
                  nm(alg) == "ALGORITHM: Part" && nm(pre) == "PRESETS (turn)",
              "HOME: the engine's four knobs (ANALOG: CUT RES ATK REL), SELECT Tempo, ALGORITHM Part, PRESETS relative");
        Tier2Parameter *cut = slotNamed(p, "E5 CUT");
        k[0]->setValueNotifyingHost(k[0]->toNorm(40));
        h.run_ms(60);
        p.drainTier2();
        check(cut && cut->entry()->get() == 40 && cut->plainValue() == 40, "host KNOB1 = 40: E5 CUT 40, on its slot too");
        auto tap = [&](int b) {
            p.deviceForTest()->buttons_tap(1u << b);
            h.run_ms(200);
            w.clear();
            p.drainTier2();
        };
        tap(EMU_B_ENV);
        check(row() == "KNOB1: Attack | KNOB2: Decay | KNOB3: Sustain | KNOB4: Release" && w.infoChanged > 0,
              "ENV tapped: the ENV page, KNOB1..4 ATK DEC SUS REL (" + row() + ")");
        tap(EMU_B_ENV);
        check(nm(k[0]) == "KNOB1: Env>Filter" && k[3]->isRelative(), "ENV again: ENV DEST, its empty fourth column "
                                                                     "relative (" + row() + ")");
        tap(EMU_B_FX);
        tap(EMU_B_FX);
        std::printf("        (felucca FX, FX: %s)\n", row().c_str());
        const fm1param_t *cell = k[0]->entry();
        bool cellOk = false;
        if (cell && (cell->flags & FM1P_HIDDEN)) {
            const int32_t want = (cell->min + cell->max) / 2;
            k[0]->setValueNotifyingHost(k[0]->toNorm(want));
            h.run_ms(60);
            cellOk = cell->get() == want;
        }
        check(nm(k[0]).rfind("KNOB1: SLICER", 0) == 0 && cellOk,
              "FX twice: SLICER, a page no named entry covers: KNOB1 the hidden cell \"" + nm(k[0]) + "\", a host write lands");
        tap(EMU_B_EDIT);
        check(nm(k[0]) == "KNOB1: E1 WAVE", "EDIT: EDIT 1, KNOB1 the engine entry (" + nm(k[0]) + ")");
        // the layers: GLO held: T1..T4 LEVEL; FX held: the macros
        p.buttonParam(EMU_B_GLO)->setValueNotifyingHost(1.0f);
        h.run_ms(700);
        p.drainTier2();
        const std::string glo = row();
        p.buttonParam(EMU_B_GLO)->setValueNotifyingHost(0.0f);
        h.run_ms(200);
        p.buttonParam(EMU_B_FX)->setValueNotifyingHost(1.0f);
        h.run_ms(700);
        p.drainTier2();
        const std::string fx = row();
        k[0]->setValueNotifyingHost(k[0]->toNorm(-50));
        h.run_ms(60);
        const int32_t filt = k[0]->entry() ? k[0]->entry()->get() : 0;
        p.buttonParam(EMU_B_FX)->setValueNotifyingHost(0.0f);
        h.run_ms(200);
        p.drainTier2();
        std::printf("        (felucca GLO held: %s; FX held: %s)\n", glo.c_str(), fx.c_str());
        check(glo == "KNOB1: T1 Level | KNOB2: T2 Level | KNOB3: T3 Level | KNOB4: T4 Level" &&
                  fx == "KNOB1: FX Filter | KNOB2: FX Crush | KNOB3: FX Throw | KNOB4: FX Depth" && filt == -50,
              "GLO held: the four parts' levels; FX held: the macros (host FILTER -50 lands); let go: the page again (" +
                  nm(k[0]) + ")");
    }
    // ---- Melodee
    {
        check(p.switchCore("melodee"), "switchCore(\"melodee\")");
        h.run_ms(700);
        p.drainTier2();
        std::printf("        (melodee HOME: %s)\n", page().c_str());
        check(nm(k[0]) == "KNOB1: E5 CUT" && nm(alg) == "ALGORITHM: Part" && nm(sel) == "SELECT (turn)",
              "HOME: the engine's knobs, ALGORITHM Part, SELECT relative (Melodee's SELECT turns pages)");
        check(p.switchCore("choralroot"), "switchCore(\"choralroot\"): back");
        h.run_ms(700);
        p.drainTier2();
        check(nm(k[0]) == "KNOB1: Voicing", "ChoralRoot's view again (" + nm(k[0]) + ")");
    }

    p.removeListener(&w);
    for (auto *q : p.getParameters())
        q->removeListener(&w);
    std::printf("knob_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
