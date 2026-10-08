// SPDX-License-Identifier: GPL-3.0-only
// The eight physical knobs as host parameters (FM1-VST-PLAN.md 4.5: the Roto-Control's first page), headless, on the
// plugin's shared code. Each knob parameter has a FIXED name ("Knob Master" "Knob Select" "Knob Presets" "Knob Algo"
// "Knob 1".."Knob 4": the Roto-Control binds by name) and fixed host info (continuous, default steps, default 0.5:
// never parameterInfoChanged); what the knob does on the firmware's screen (the core's knob_target) is its value
// text's prefix ("Voicing: 3") and KnobParameter::functionName(); a relative control ("turn") where it has no value.
// ONLY THE KNOBS REPORT: a device turn of a knob reports on that knob, inside a gesture (a relative one: a nudge off
// the centre, then the spring back); any other knob reports only when its own target's value changed (no gesture);
// nothing else ever reports (with FM1_TIER2_SLOTS 0 there is nothing else; with the opt-in slots, the slots are
// tier2_test's).
// ChoralRoot: the names and the order; (a) the view: the functions, the value texts, a host write; (t) the turn
// rule for all seven knobs on the view and in the PERF, FX and KEY layers (Device::enc +2: Knob 4 among them);
// (p) PRESETS turned on the view (a new chord sound: only Knob Presets touched, Knob 4 follows its send untouched)
// and in the KEY layer (no knob shows a send: only Knob Presets reports at all); (b) the PERF layer opened: KNOB1..4
// the mode's row; (c) the perform mode switched from the host; (f) a panel turn of KNOB1 in the layer; (d) the FX
// layer; (e) Options: SELECT relative, a host turn 0.5 -> 0.75 is +6 detents at the device, the spring back makes
// none, the hidden "Option" entry; (g) the sound editor: KNOB1 bound to the cell. Felucca: HOME's knobs, the ENV
// page, ENV DEST's empty column relative, SLICER (the hidden cell), the GLO and FX layers held, EDIT 1. Melodee:
// HOME. The message loop is not running: the test calls the drain itself. FM1EMU_HOME points under the build.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
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

// what the host is told, the opt-in Tier 2 slots (FM1_TIER2_SLOTS > 0: indices 8 .. 8 + slots - 1) left out: they
// report their own entries' changes (tier2_test)
static bool isSlot(int i) { return i >= 8 && i < 8 + FM1Processor::kTier2Slots; }
struct Watch : juce::AudioProcessorParameter::Listener, juce::AudioProcessorListener {
    std::map<int, int> changes, begins;
    std::map<int, float> last;
    std::vector<int> order;                       // the parameters as they first changed
    int infoChanged = 0;
    void parameterValueChanged(int i, float v) override
    {
        if (isSlot(i))
            return;
        if (!changes[i]++)
            order.push_back(i);
        last[i] = v;
    }
    void parameterGestureChanged(int i, bool starting) override
    {
        if (starting && !isSlot(i))
            begins[i]++;
    }
    void audioProcessorParameterChanged(juce::AudioProcessor *, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor *, const ChangeDetails &d) override { infoChanged += d.parameterInfoChanged; }
    void clear()
    {
        changes.clear();
        begins.clear();
        last.clear();
        order.clear();
        infoChanged = 0;
    }
};

static const fm1param_t *entryNamed(FM1Processor &p, const char *name)
{
    const fm1core_t *c = p.deviceForTest()->core();
    for (uint32_t i = 0; i < c->nparams; i++)
        if (!std::strcmp(c->params[i].name, name))
            return &c->params[i];
    return nullptr;
}
static std::string fn(KnobParameter *k)
{
    const juce::String f = k->functionName();
    return f.isEmpty() ? "turn" : f.toStdString();
}

int main()
{
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("knob_test: FM1EMU_HOME=%s, %d Tier 2 slots\n", scratch.getFullPathName().toRawUTF8(),
                FM1Processor::kTier2Slots);

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
            s += (i ? " | " : "") + fn(k[i]);
        return s;
    };
    auto page = [&]() { return fn(sel) + " | " + fn(pre) + " | " + fn(alg) + " | " + row(); };
    auto idx = [](juce::AudioProcessorParameter *q) { return q ? q->getParameterIndex() : -1; };
    const bool noSlots = FM1Processor::kTier2Slots == 0;

    // the order and the names: fixed, whatever the screen shows
    static const char *const kNames[8] = {"Knob Master", "Knob Select", "Knob Presets", "Knob Algo",
                                          "Knob 1",      "Knob 2",      "Knob 3",       "Knob 4"};
    auto namesFixed = [&]() {
        const auto &ps = p.getParameters();
        for (int i = 0; i < 8; i++)
            if (ps[i]->getName(100) != kNames[i])
                return false;
        return true;
    };
    {
        const auto &ps = p.getParameters();
        bool info = true;
        for (int r = 0; r < FM1Processor::kKnobs; r++) {
            KnobParameter *q = p.knobParam(r);
            info = info && !q->isDiscrete() && q->getNumSteps() == juce::AudioProcessor::getDefaultNumParameterSteps() &&
                   q->getDefaultValue() == 0.5f;
        }
        check(ps[0] == p.masterParam() && ps[1] == sel && ps[2] == pre && ps[3] == alg && ps[4] == k[0] && ps[7] == k[3] &&
                  namesFixed() && info,
              "parameters 0..7: \"Knob Master\" \"Knob Select\" \"Knob Presets\" \"Knob Algo\" \"Knob 1\"..\"Knob 4\"; "
              "continuous, default steps, default 0.5");
        check(p.masterParam()->getCurrentValueAsText() == "Master: 724" &&
                  std::abs(static_cast<juce::AudioProcessorParameter *>(p.masterParam())->getValueForText("Master: 100") -
                           p.masterParam()->convertTo0to1(100.0f)) < 1e-6f,
              "MASTER's value text: \"" + p.masterParam()->getCurrentValueAsText().toStdString() + "\"");
    }

    Host h(p, 48000.0, 256);
    h.run_ms(600);
    w.clear();
    p.drainTier2();
    // ---- (a) the view
    {
        std::printf("        (the view: %s)\n", page().c_str());
        check(fn(k[0]) == "Voicing" && fn(sel) == "Tempo" && fn(k[1]) == "Bass Register" && fn(k[2]) == "Strum Rate" &&
                  fn(k[3]) == "Chord Reverb" && fn(pre) == "turn" && fn(alg) == "turn" && namesFixed() &&
                  (!noSlots || w.infoChanged == 0),
              "the view: KNOB1 Voicing, KNOB2 Bass Register, KNOB3 Strum Rate, KNOB4 Chord Reverb, SELECT Tempo; "
              "PRESETS / ALGORITHM relative; the names unchanged, no parameterInfoChanged");
        check(k[0]->getCurrentValueAsText().startsWith("Voicing: ") && k[2]->getCurrentValueAsText().endsWith(" ms") &&
                  sel->getCurrentValueAsText() == "Tempo: 120 BPM" && pre->getCurrentValueAsText() == "turn",
              "value texts: \"" + k[0]->getCurrentValueAsText().toStdString() + "\", \"" +
                  k[2]->getCurrentValueAsText().toStdString() + "\", \"" + sel->getCurrentValueAsText().toStdString() +
                  "\", \"" + pre->getCurrentValueAsText().toStdString() + "\"");
        check(std::abs(k[0]->getValueForText("Voicing: 2") - k[0]->toNorm(2)) < 1e-6f &&
                  std::abs(k[0]->getValueForText("2") - k[0]->toNorm(2)) < 1e-6f,
              "getValueForText: \"Voicing: 2\" and \"2\" both mean voicing 2");
        const fm1param_t *vo = entryNamed(p, "Voicing");
        k[0]->setValueNotifyingHost(k[0]->toNorm(3));
        w.clear();
        h.run_ms(100);
        p.drainTier2();
        check(vo && vo->get() == 3 && k[0]->plainValue() == 3 && w.changes.empty(),
              "host Knob 1 = 3: the firmware's voicing 3, nothing reported (not echoed)");
        k[0]->setValueNotifyingHost(k[0]->toNorm(0));
        h.run_ms(100);
        p.drainTier2();
    }

    // ---- (s) page switches: the host-visible value of every knob follows its new target BEFORE anything is turned
    // (the screen changed by a panel click, as in Live: panelButton from the message thread; the device and the
    // drain pumped as the audio thread and the 30 Hz timer would, nothing else), and the first turn after the
    // switch reports a step from the new target's value, never a jump from the old one
    auto click = [&](int b) {                     // a tap on the panel (down and up before the next block)
        p.panelButton(b, true);
        p.panelButton(b, false);
    };
    auto hold = [&](int b) {                      // a layer button held past HOLD_MS: the layer locks open
        p.panelButton(b, true);
        h.run_ms(500);
        p.panelButton(b, false);
    };
    auto pump = [&](double ms) {                  // blocks with a drain every ~33 ms
        for (double t = 0; t < ms; t += 33) {
            h.run_ms(33);
            p.drainTier2();
        }
    };
    // every knob's host value is its target's place now (hostWrote: a value the host wrote itself may be any x that
    // quantises to the target's step)
    auto followed = [&](const std::string &where, bool hostWrote = false) {
        std::string s, bad;
        for (int r = 0; r < FM1Processor::kKnobs; r++) {
            KnobParameter *q = p.knobParam(r);
            const fm1param_t *e = q->isRelative() ? nullptr : q->entry();
            const float want = e ? q->toNorm(e->get()) : 0.5f;
            const bool ok = std::abs(q->hostValue() - want) < 1e-6f || (hostWrote && e && q->toPlain(q->hostValue()) == e->get());
            char b[96];
            std::snprintf(b, sizeof b, "%s%s %.3f", r ? ", " : "", fn(q).c_str(), q->hostValue());
            s += b;
            if (!ok)
                bad += " " + q->getName(100).toStdString() + " (host " + std::to_string(q->hostValue()) + ", want " +
                       std::to_string(want) + ")";
        }
        check(bad.empty() && namesFixed(), where + ": every knob's host value is its new target's (" + s + ")" +
                                               (bad.empty() ? "" : "; STALE:" + bad));
    };
    // the first panel turn of a knob: reported one step from its target's value as the host already had it
    auto firstTurn = [&](const std::string &where, int role, int detents) {
        KnobParameter *q = p.knobParam(role);
        const fm1param_t *e = q->entry();
        if (q->isRelative() || !e) {
            check(false, where + ": " + q->getName(100).toStdString() + " is not bound");
            return;
        }
        const float hostBefore = q->hostValue();
        const int32_t v0 = e->get();
        w.clear();
        p.panelEnc(role, detents);
        pump(100);
        const int32_t v1 = e->get();
        const int i = idx(q);
        const bool adjacent = v1 != v0 && std::abs(v1 - v0) <= std::abs(detents) * 8 &&
                              std::abs(w.last[i] - q->toNorm(v1)) < 1e-6f && w.begins[i] == 1;
        check(adjacent && std::abs(hostBefore - q->toNorm(v0)) < 1e-6f,
              where + ": the first panel turn of " + q->getName(100).toStdString() + " (" + fn(q) + ") " +
                  std::to_string(v0) + " -> " + std::to_string(v1) + ": the host goes " + std::to_string(hostBefore) +
                  " -> " + std::to_string(w.last[i]) + " (a step, inside a gesture)");
        p.panelEnc(role, -detents);
        pump(100);
    };
    {
        const fm1param_t *vo = entryNamed(p, "Voicing");
        k[0]->setValueNotifyingHost(0.25f);       // a known voicing from the host
        pump(100);
        const float voNorm = k[0]->toNorm(vo->get());
        check(std::abs(k[0]->hostValue() - 0.25f) < 1e-6f && vo->get() == k[0]->toPlain(0.25f),
              "the view: host Knob 1 = 0.25: voicing " + std::to_string(vo->get()));
        click(EMU_B_EDIT);                        // EDIT tapped: the chord sound's editor
        pump(300);
        std::printf("        (the editor: %s)\n", page().c_str());
        const fm1param_t *lv = k[0]->entry();
        check(lv && fn(k[0]) == "Level" && std::abs(k[0]->hostValue() - k[0]->toNorm(lv->get())) < 1e-6f &&
                  std::abs(k[0]->hostValue() - 0.25f) > 1e-3f,
              "EDIT (no knob turned): Knob 1 is Level " + std::to_string(lv ? lv->get() : -1) + ", the host was pushed " +
                  std::to_string(k[0]->hostValue()) + " (was the voicing's 0.25)");
        followed("EDIT");
        firstTurn("EDIT", EMU_E_K1, 1);
        firstTurn("EDIT", EMU_E_K4, -1);
        click(EMU_B_HOME);
        pump(300);
        check(fn(k[0]) == "Voicing" && std::abs(k[0]->hostValue() - voNorm) < 1e-6f,
              "EDIT -> HOME: Knob 1 Voicing again, the host back at the voicing's " + std::to_string(voNorm));
        followed("EDIT -> HOME");
        firstTurn("HOME", EMU_E_K1, 1);
        hold(EMU_B_ARP);                          // the PERF layer
        pump(300);
        followed("PERF open (" + row() + ")");
        firstTurn("PERF", EMU_E_K1, 1);
        firstTurn("PERF", EMU_E_K3, 1);
        p.panelEnc(EMU_E_SELECT, 1);              // the perform mode: Strum -> Slop (SELECT is the picker here)
        pump(300);
        followed("PERF, the mode changed by a panel turn of SELECT (" + row() + ")");
        firstTurn("PERF, new mode", EMU_E_K1, 1);
        sel->setValueNotifyingHost(sel->toNorm(3));   // and from the host: Arpeggiate
        pump(400);
        followed("PERF, the mode changed by the host (" + row() + ")");
        firstTurn("PERF, Arpeggiate", EMU_E_K2, 1);
        sel->setValueNotifyingHost(sel->toNorm(0));
        pump(400);
        click(EMU_B_OCTDN);                       // OCT-: the layer closed
        pump(300);
        followed("PERF closed (" + row() + ")");
        hold(EMU_B_FX);
        pump(300);
        followed("FX open (" + row() + ")");
        firstTurn("FX", EMU_E_K1, 1);
        click(EMU_B_OCTDN);
        pump(300);
        followed("FX closed");
        hold(EMU_B_SEL);                          // KEY
        pump(300);
        followed("KEY open (" + row() + ")");
        firstTurn("KEY", EMU_E_K3, 1);
        click(EMU_B_OCTDN);
        pump(300);
        click(EMU_B_GLO);                         // OPT tapped: Options
        pump(300);
        followed("Options open (" + page() + ")");
        firstTurn("Options", EMU_E_K1, 1);
        click(EMU_B_GLO);
        pump(600);
        followed("Options closed (" + page() + ")");
        check(fn(k[0]) == "Voicing", "back on the view (" + page() + ")");
    }
    // the race: the screen changed, the host (a Roto-Control's motor still on the old target) writes before the push
    // reached it. The write is dropped (kSettleMs after the retarget), the host is told the new target's value; a
    // write after the window lands. And each page switch's pushes (no gesture) ask a VST3 host to re-read the values
    // (restartComponent kParamValuesChanged: Live ignored the gestureless performEdit); a turn alone does not.
    {
        k[0]->setValueNotifyingHost(0.25f);
        pump(100);
        const uint32_t ref0 = p.hostRefreshes();
        const int32_t drop0 = p.knobWritesDropped(EMU_E_K1);
        click(EMU_B_EDIT);
        h.run_ms(40);                             // (the retarget happened; the drain has not run yet)
        const fm1param_t *lv = k[0]->entry();
        const int32_t level0 = lv ? lv->get() : -1;
        k[0]->setValueNotifyingHost(0.27f);       // the stale motor: the old voicing's place, nudged
        h.run_ms(60);
        p.drainTier2();
        pump(100);
        check(fn(k[0]) == "Level" && lv->get() == level0 && p.knobWritesDropped(EMU_E_K1) == drop0 + 1 &&
                  std::abs(k[0]->hostValue() - k[0]->toNorm(level0)) < 1e-6f,
              "EDIT, the host writes 0.27 (meant for the voicing) within " + std::to_string(FM1Processor::kSettleMs) +
                  " ms of the retarget: dropped, Level stays " + std::to_string(lv->get()) + ", the host is told " +
                  std::to_string(k[0]->hostValue()));
        check(p.hostRefreshes() > ref0, "  ... the switch's pushes asked a VST3 host to re-read the values (" +
                                            std::to_string(p.hostRefreshes() - ref0) + " restartComponent)");
        k[0]->setValueNotifyingHost(k[0]->toNorm(level0 - 3));
        pump(100);
        check(lv->get() == level0 - 3, "  ... a host write after the window lands: Level " + std::to_string(lv->get()));
        k[0]->setValueNotifyingHost(k[0]->toNorm(level0));
        pump(100);
        const uint32_t ref1 = p.hostRefreshes();
        p.panelEnc(EMU_E_K1, 1);
        pump(100);
        check(p.hostRefreshes() == ref1, "  ... a panel turn alone (a touch) asks no re-read");
        p.panelEnc(EMU_E_K1, -1);
        pump(100);
        click(EMU_B_HOME);
        pump(300);
        check(fn(k[0]) == "Voicing", "HOME: the view again");
    }

    // ---- (t) the turn rule: Device::enc(role, +2) on the screen showing now
    auto turnRule = [&](const char *where, int role, int detents) {
        KnobParameter *kn = p.knobParam(role);
        std::array<const fm1param_t *, 7> tgt{};
        std::array<int32_t, 7> val{};
        for (int r = 0; r < 7; r++) {
            KnobParameter *q = p.knobParam(r);
            tgt[(size_t)r] = q->isRelative() ? nullptr : q->entry();
            val[(size_t)r] = tgt[(size_t)r] ? tgt[(size_t)r]->get() : 0;
        }
        const bool relative = kn->isRelative();
        const std::string before = fn(kn);
        w.clear();
        p.deviceForTest()->enc(role, detents);
        h.run_ms(100);
        p.drainTier2();
        const int ki = idx(kn);
        bool othersOk = true;
        std::string why;
        for (const auto &c : w.changes) {
            if (c.first == ki)
                continue;
            if (w.begins[c.first]) {
                othersOk = false;
                why += " touched:" + p.getParameters()[c.first]->getName(100).toStdString();
            }
            int r = -1;
            for (int q = 0; q < 7; q++)
                if (idx(p.knobParam(q)) == c.first)
                    r = q;
            if (r < 0) {
                othersOk = false;                 // (not a knob)
                why += " " + p.getParameters()[c.first]->getName(100).toStdString();
                continue;
            }
            KnobParameter *q = p.knobParam(r);
            const fm1param_t *now = q->isRelative() ? nullptr : q->entry();
            const bool moved = now != tgt[(size_t)r] || (now && now->get() != val[(size_t)r]);
            if (!moved) {
                othersOk = false;
                why += " unmoved:" + q->getName(100).toStdString();
            }
        }
        const bool own = w.changes[ki] > 0 && w.begins[ki] == 1 && (w.order.empty() || w.order[0] == ki);
        const bool movedOwn = relative ? std::abs(w.last[ki] - (0.5f + (float)detents / 24.0f)) < 1e-4f
                                       : (tgt[(size_t)role] && tgt[(size_t)role]->get() != val[(size_t)role]);
        std::string others;
        for (const auto &c : w.changes)
            if (c.first != ki)
                others += " " + p.getParameters()[c.first]->getName(100).toStdString();
        check(own && movedOwn && othersOk && namesFixed(),
              std::string(where) + ": Device::enc(" + kn->getName(100).toStdString() + " [" + before + "], " +
                  std::to_string(detents) + "): reported on it first, inside a gesture" +
                  (relative ? " (a nudge to " + std::to_string(w.last[ki]) + ")" : "") +
                  (others.empty() ? "; nothing else" : "; untouched, their targets moved:" + others) + why);
        if (relative) {                           // the spring back: 0.5, no gesture, no detents
            w.clear();
            h.run_ms(500);
            p.drainTier2();
            check(w.last.count(ki) && w.last[ki] == 0.5f && !w.begins.count(ki) && kn->getValue() == 0.5f,
                  "  ... 500 ms later it springs back to 0.5 (no gesture)");
        }
        p.deviceForTest()->enc(role, -detents);   // (put back)
        h.run_ms(600);
        p.drainTier2();
    };
    static const int kOrder[7] = {EMU_E_SELECT, EMU_E_PRESETS, EMU_E_ALGO, EMU_E_K1, EMU_E_K2, EMU_E_K3, EMU_E_K4};
    for (int r : kOrder)
        turnRule("view", r, 2);
    // Knob 4 explicitly (the report from Live: "Knob 4 wasn't registering"): its own turn is its own report
    {
        const fm1param_t *rv = entryNamed(p, "Chord Reverb");
        const int32_t v0 = rv ? rv->get() : -1;
        w.clear();
        p.deviceForTest()->enc(EMU_E_K4, 2);
        h.run_ms(100);
        p.drainTier2();
        check(fn(k[3]) == "Chord Reverb" && rv && rv->get() == v0 + 8 && w.changes[idx(k[3])] == 1 &&
                  w.begins[idx(k[3])] == 1 && w.changes.size() == 1 && k[3]->plainValue() == v0 + 8 &&
                  k[3]->getCurrentValueAsText().startsWith("Chord Reverb: "),
              "the view: Device::enc(Knob 4, +2): Chord Reverb " + std::to_string(v0) + " -> " +
                  std::to_string(rv ? rv->get() : -1) + ", reported on Knob 4 alone, inside a gesture (\"" +
                  k[3]->getCurrentValueAsText().toStdString() + "\")");
        p.deviceForTest()->enc(EMU_E_K4, -2);
        h.run_ms(200);
        p.drainTier2();
    }

    // ---- (p) PRESETS on the view: a new chord sound. Only Knob Presets is touched; Knob 4 (the send it shows) may
    // follow its value, untouched; nothing else
    {
        const fm1param_t *sends[4] = {entryNamed(p, "Chord Reverb"), entryNamed(p, "Chord Chorus"),
                                      entryNamed(p, "Chord Delay"), entryNamed(p, "Chord Drive")};
        int32_t s0[4];
        for (int i = 0; i < 4; i++)
            s0[i] = sends[i] ? sends[i]->get() : 0;
        const int32_t rv0 = k[3]->plainValue();
        w.clear();
        p.deviceForTest()->enc(EMU_E_PRESETS, 1);
        h.run_ms(150);
        p.drainTier2();
        int sendsMoved = 0;
        for (int i = 0; i < 4; i++)
            sendsMoved += sends[i] && sends[i]->get() != s0[i];
        std::set<int> touched, reported;
        for (const auto &c : w.begins)
            if (c.second)
                touched.insert(c.first);
        for (const auto &c : w.changes)
            reported.insert(c.first);
        const bool k4moved = sends[0]->get() != s0[0];
        std::set<int> allowed = {idx(pre)};
        if (k4moved)
            allowed.insert(idx(k[3]));
        check(touched == std::set<int>{idx(pre)} && reported == allowed && w.order[0] == idx(pre) &&
                  (!k4moved || k[3]->plainValue() == sends[0]->get()),
              "the view: PRESETS +1 (" + std::to_string(sendsMoved) + " of the chord sends moved; Chord Reverb " +
                  std::to_string(rv0) + " -> " + std::to_string(sends[0]->get()) +
                  "): only Knob Presets touched (first); " + (k4moved ? "Knob 4 follows its send, untouched" : "Knob 4 unmoved") +
                  "; nothing else reported (" + std::to_string(reported.size()) + ")");
        h.run_ms(500);
        p.drainTier2();
    }
    // the KEY layer (SEL held 500 ms: it locks open): no knob shows a send there
    {
        p.buttonParam(EMU_B_SEL)->setValueNotifyingHost(1.0f);
        h.run_ms(500);
        p.buttonParam(EMU_B_SEL)->setValueNotifyingHost(0.0f);
        h.run_ms(300);
        p.drainTier2();
        std::printf("        (KEY: %s)\n", page().c_str());
        check(fn(sel) == "Tempo" && fn(k[0]) == "Key Tonic" && fn(k[1]) == "Key Scale" && fn(k[2]) == "Transpose" &&
                  fn(k[3]) == "Single Notes" && namesFixed(),
              "KEY held: KNOB1..4 Key Tonic, Key Scale, Transpose, Single Notes (" + row() + "), the names unchanged");
        const fm1param_t *sends[4] = {entryNamed(p, "Chord Reverb"), entryNamed(p, "Chord Chorus"),
                                      entryNamed(p, "Chord Delay"), entryNamed(p, "Chord Drive")};
        int sendsMoved = 0, tries = 0;
        while (!sendsMoved && tries < 6) {        // (a sound whose sends differ from the one before)
            int32_t s0[4];
            for (int i = 0; i < 4; i++)
                s0[i] = sends[i]->get();
            w.clear();
            p.deviceForTest()->enc(EMU_E_PRESETS, 1);
            h.run_ms(150);
            p.drainTier2();
            for (int i = 0; i < 4; i++)
                sendsMoved += sends[i]->get() != s0[i];
            tries++;
            if (!sendsMoved) {
                h.run_ms(500);
                p.drainTier2();
            }
        }
        std::string others;
        for (const auto &c : w.changes)
            if (c.first != idx(pre))
                others += " " + p.getParameters()[c.first]->getName(100).toStdString();
        check(sendsMoved > 0 && w.changes.size() == 1 && w.changes[idx(pre)] && w.begins[idx(pre)] == 1,
              "KEY layer: PRESETS +1 changed " + std::to_string(sendsMoved) +
                  " chord sends; no knob shows one: only Knob Presets reported (the nudge)" +
                  (others.empty() ? "" : "; ALSO:" + others));
        h.run_ms(500);
        p.drainTier2();
        for (int r : kOrder)
            if (r != EMU_E_PRESETS && r != EMU_E_ALGO)   // (the sound lists: above)
                turnRule("KEY", r, 2);
        p.deviceForTest()->buttons_tap(1u << EMU_B_OCTDN);   // (OCT-: the layer closed)
        h.run_ms(200);
        p.drainTier2();
    }

    // ---- (b) the PERF layer opened (PERF held 500 ms: it locks open; scripts/cr_perf_row.txt)
    const fm1param_t *ad = entryNamed(p, "Arp Division");
    {
        w.clear();
        p.buttonParam(EMU_B_ARP)->setValueNotifyingHost(1.0f);
        h.run_ms(500);
        p.buttonParam(EMU_B_ARP)->setValueNotifyingHost(0.0f);
        h.run_ms(300);
        p.drainTier2();
        std::printf("        (PERF: %s)\n", page().c_str());
        bool untouched = true;
        for (const auto &c : w.begins)
            untouched = untouched && !c.second;
        check(row() == "Strum Rate | Strum Dir | Strum Range | Strum Hold" && fn(sel) == "Perform Mode" &&
                  namesFixed() && (!noSlots || w.infoChanged == 0) && w.changes.count(idx(k[1])) && untouched,
              "PERF held: KNOB1..4 Strum's row, SELECT Perform Mode; the names unchanged, no parameterInfoChanged; the "
              "new values pushed without a gesture");
        const fm1param_t *sr = entryNamed(p, "Strum Rate");
        check(k[0]->plainValue() == sr->get() && k[3]->getCurrentValueAsText().startsWith("Strum Hold: "),
              "  ... Knob 1 has Strum Rate's value (" + std::to_string(k[0]->plainValue()) + "), Knob 4 \"" +
                  k[3]->getCurrentValueAsText().toStdString() + "\"");
        for (int r : kOrder)
            turnRule("PERF", r, 2);
    }
    // ---- (c) the perform mode switched (from the host: SELECT is the PERF picker here)
    {
        w.clear();
        sel->setValueNotifyingHost(sel->toNorm(3));       // Arpeggiate
        h.run_ms(200);
        p.drainTier2();
        std::printf("        (PERF, Arpeggiate: %s)\n", row().c_str());
        check(row() == "Arp Division | Arp Dir | Arp Gate | Arp Swing" && namesFixed() &&
                  (!noSlots || w.infoChanged == 0) && k[0]->plainValue() == ad->get() &&
                  k[0]->getCurrentValueAsText().startsWith("Arp Division: "),
              "host SELECT = Arpeggiate: KNOB1..4 Arp's row; Knob 1 \"" + k[0]->getCurrentValueAsText().toStdString() + "\"");
    }
    // ---- (f) a panel turn of KNOB1 in the layer: on Knob 1, inside a gesture
    {
        const int32_t before = ad->get();
        w.clear();
        p.panelEnc(EMU_E_K1, 2);
        h.run_ms(100);
        p.drainTier2();
        check(ad->get() == before + 2 && k[0]->plainValue() == before + 2 && w.changes.count(idx(k[0])) &&
                  w.begins[idx(k[0])] == 1 && (!noSlots || w.changes.size() == 1),
              "the panel's KNOB1 +2: Arp Division " + std::to_string(before) + " -> " + std::to_string(ad->get()) +
                  ", reported on Knob 1 inside a gesture");
        p.deviceForTest()->buttons_tap(1u << EMU_B_OCTDN);   // (OCT-: the layer closed)
        h.run_ms(200);
        w.clear();
        p.drainTier2();
        check(fn(k[0]) == "Voicing" && fn(k[2]) == "Arp Division",
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
        check(row() == "Reverb Size | Reverb Damp | Reverb Type | Chord Reverb" && fn(sel) == "FX Effect" && namesFixed(),
              "FX held: KNOB1..4 the Reverb row (size, damp, type, the amount), SELECT the effect (\"FX Effect\", hidden)");
        for (int r : kOrder)
            if (r != EMU_E_SELECT)                // (SELECT: the effect, below)
                turnRule("FX", r, 2);
        sel->setValueNotifyingHost(sel->toNorm(2));       // Delay
        h.run_ms(200);
        p.drainTier2();
        check(row() == "Delay Time | Delay Feedback | Delay Colour | Chord Delay", "host SELECT = Delay: " + row());
        turnRule("FX, Delay", EMU_E_K4, 2);
        sel->setValueNotifyingHost(sel->toNorm(1));       // Chorus: no third parameter
        h.run_ms(200);
        p.drainTier2();
        check(fn(k[0]) == "Chorus Rate" && fn(k[2]) == "turn" && k[2]->isRelative() &&
                  k[2]->getCurrentValueAsText() == "turn",
              "Chorus: KNOB3 has nothing there (relative, \"" + k[2]->getCurrentValueAsText().toStdString() + "\")");
        sel->setValueNotifyingHost(sel->toNorm(0));
        h.run_ms(100);
        turnRule("FX", EMU_E_SELECT, 1);
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
        check(sel->isRelative() && fn(sel) == "turn" && sel->getValue() == 0.5f && fn(k[0]) == "Play Style",
              "Options: SELECT (the cursor) relative at 0.5, KNOB1 the first row: \"" + fn(k[0]) + "\"");
        const int32_t sent0 = p.knobDetentsSent(EMU_E_SELECT);
        w.clear();
        sel->setValueNotifyingHost(0.75f);
        h.run_ms(60);
        p.drainTier2();
        const int32_t d1 = p.knobDetentsSent(EMU_E_SELECT) - sent0;
        check(d1 == 6 && fn(k[0]) == "Split Point" && w.changes[idx(sel)] == 1 && !w.begins.count(idx(sel)),
              "host SELECT 0.5 -> 0.75: " + std::to_string(d1) + " detents at the device; the cursor moved 6 rows: \"" +
                  fn(k[0]) + "\" (the host's own turn is not nudged back)");
        w.clear();
        h.run_ms(500);
        p.drainTier2();
        const int32_t d2 = p.knobDetentsSent(EMU_E_SELECT) - sent0;
        check(sel->getValue() == 0.5f && w.changes.count(idx(sel)) && w.last[idx(sel)] == 0.5f && !w.begins.count(idx(sel)) &&
                  d2 == 6 && fn(k[0]) == "Split Point",
              "500 ms later: SELECT re-centred to 0.5 (the host told, no gesture), no detents from it (still " +
                  std::to_string(d2) + "), the cursor stays");
        sel->setValueNotifyingHost(0.5f + 1.0f / 24.0f);   // one row down: MIDI Perform (no named entry)
        h.run_ms(200);                            // (a host write right after a retarget would be dropped: kSettleMs)
        p.drainTier2();
        check(fn(k[0]) == "MIDI Perform" && !k[0]->isRelative() && k[0]->entry()->max - k[0]->entry()->min == 16,
              "+1 detent: KNOB1 \"" + fn(k[0]) + "\" (the hidden \"Option\" entry rewritten), 17 values");
        k[0]->setValueNotifyingHost(k[0]->toNorm(5));
        h.run_ms(60);
        p.drainTier2();
        check(k[0]->entry() && k[0]->entry()->get() == 5 && k[0]->getCurrentValueAsText() == "MIDI Perform: ch 5",
              "host KNOB1 = 5: \"" + k[0]->getCurrentValueAsText().toStdString() + "\"");
        k[0]->setValueNotifyingHost(k[0]->toNorm(1));
        h.run_ms(60);
        p.deviceForTest()->buttons_tap(1u << EMU_B_GLO);   // (Options closed)
        h.run_ms(600);
        p.drainTier2();
        check(fn(sel) == "Tempo" && fn(k[0]) == "Voicing", "OPT again: the view (" + fn(sel) + ", " + fn(k[0]) + ")");
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
        check(none && hidden > 0, std::to_string(hidden) + " FM1P_HIDDEN entries, none on a Tier 2 slot (" +
                                      std::to_string(p.tier2Bound()) + " slots bound)");
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
        check(e && !k[0]->isRelative() && fn(sel) == "turn" && moved && namesFixed(),
              "EDIT: KNOB1 is the editor's cell (\"" + fn(k[0]) + "\"), a host write moves it; SELECT (the lanes) relative");
        p.deviceForTest()->buttons_tap(1u << EMU_B_ENV);   // the ENV section
        h.run_ms(300);
        p.drainTier2();
        std::printf("        (the editor, ENV: %s)\n", row().c_str());
        check(!k[0]->isRelative() && !k[3]->isRelative(), "ENV: KNOB1..4 bound (" + row() + ")");
        p.deviceForTest()->buttons_tap(1u << EMU_B_HOME);
        h.run_ms(300);
        p.drainTier2();
        check(fn(k[0]) == "Voicing", "HOME: the view again (" + fn(k[0]) + ")");
    }

    // ---- Felucca
    {
        check(p.switchCore("felucca"), "switchCore(\"felucca\")");
        h.run_ms(700);
        w.clear();
        p.drainTier2();
        std::printf("        (felucca HOME: %s)\n", page().c_str());
        check(row() == "E5 CUT | E6 RES | Attack | Release" && fn(sel) == "Tempo" && fn(alg) == "Part" &&
                  fn(pre) == "turn" && namesFixed(),
              "HOME: the engine's four knobs (ANALOG: CUT RES ATK REL), SELECT Tempo, ALGORITHM Part, PRESETS relative");
        const fm1param_t *cut = entryNamed(p, "E5 CUT");
        k[0]->setValueNotifyingHost(k[0]->toNorm(40));
        h.run_ms(60);
        p.drainTier2();
        check(cut && cut->get() == 40 && k[0]->getCurrentValueAsText().startsWith("E5 CUT: "),
              "host Knob 1 = 40: E5 CUT 40 (\"" + k[0]->getCurrentValueAsText().toStdString() + "\")");
        turnRule("felucca HOME", EMU_E_K4, 2);
        {
            k[0]->setValueNotifyingHost(0.25f);
            pump(100);
            followed("felucca HOME, host Knob 1 = 0.25", true);
            click(EMU_B_ENV);
            pump(300);
            followed("felucca HOME -> ENV (" + row() + ")");
            firstTurn("felucca ENV", EMU_E_K1, 1);
            click(EMU_B_HOME);
            pump(300);
            followed("felucca ENV -> HOME (" + row() + ")");
            check(fn(k[0]) == "E5 CUT" && std::abs(k[0]->hostValue() - 0.25f) < 0.01f,
                  "felucca HOME again: Knob 1 E5 CUT, the host back at " + std::to_string(k[0]->hostValue()));
        }
        auto tap = [&](int b) {
            p.deviceForTest()->buttons_tap(1u << b);
            h.run_ms(200);
            w.clear();
            p.drainTier2();
        };
        tap(EMU_B_ENV);
        check(row() == "Attack | Decay | Sustain | Release", "ENV tapped: the ENV page, KNOB1..4 ATK DEC SUS REL (" + row() + ")");
        tap(EMU_B_ENV);
        check(fn(k[0]) == "Env>Filter" && k[3]->isRelative(), "ENV again: ENV DEST, its empty fourth column relative (" +
                                                                  row() + ")");
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
        check(fn(k[0]).rfind("SLICER", 0) == 0 && cellOk,
              "FX twice: SLICER, a page no named entry covers: KNOB1 the hidden cell \"" + fn(k[0]) + "\", a host write lands");
        tap(EMU_B_EDIT);
        check(fn(k[0]) == "E1 WAVE", "EDIT: EDIT 1, KNOB1 the engine entry (" + fn(k[0]) + ")");
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
        check(glo == "T1 Level | T2 Level | T3 Level | T4 Level" && fx == "FX Filter | FX Crush | FX Throw | FX Depth" &&
                  filt == -50 && namesFixed(),
              "GLO held: the four parts' levels; FX held: the macros (host FILTER -50 lands); let go: the page again (" +
                  fn(k[0]) + ")");
    }
    // ---- Melodee
    {
        check(p.switchCore("melodee"), "switchCore(\"melodee\")");
        h.run_ms(700);
        p.drainTier2();
        std::printf("        (melodee HOME: %s)\n", page().c_str());
        check(fn(k[0]) == "E5 CUT" && fn(alg) == "Part" && fn(sel) == "turn",
              "HOME: the engine's knobs, ALGORITHM Part, SELECT relative (Melodee's SELECT turns pages)");
        check(p.switchCore("choralroot"), "switchCore(\"choralroot\"): back");
        h.run_ms(700);
        p.drainTier2();
        check(fn(k[0]) == "Voicing" && namesFixed(), "ChoralRoot's view again (" + fn(k[0]) + "), the names unchanged");
    }

    p.removeListener(&w);
    for (auto *q : p.getParameters())
        q->removeListener(&w);
    std::printf("knob_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
