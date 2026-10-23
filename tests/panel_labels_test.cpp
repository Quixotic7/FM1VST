// SPDX-License-Identifier: GPL-3.0-only
// FIZZ P1.6g: the panel shows FIZZ's live LABELS (its SysEx 20, subscribed by the processor; FMToneSeq docs/SYSEX.md),
// headless: FIZZ loaded, its ARP button reads MIDI FX, SHIFT held relabels SELECT to TRACK (and F1 to UNDO), SHIFT let
// go: back; the keys read FIZZ's labels; ChoralRoot keeps its static labels (no live ones). FM1EMU_HOME points at a
// scratch folder.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "built_cores.h"
#include "PanelComponent.h"
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
    Host(FM1Processor &proc, double rate, int block) : p(proc), sr(rate), bs(block), buf(2, block)
    {
        p.setRateAndBufferSizeDetails(rate, block);
        p.prepareToPlay(rate, block);
    }
    void run_ms(double ms)
    {
        for (int n = (int)(ms * sr / 1000.0 / bs) + 1; n > 0; n--) {
            juce::MidiBuffer m;
            buf.clear();
            p.processBlock(buf, m);
        }
    }
};

int main()
{
    require_cores("panel_labels_test", {"fizz"});
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    setenv("FM1VST_HUB_CATALOG", ("file://" + scratch.getFullPathName() + "/no-hub/catalog.json").toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    FM1Processor p;
    Host h(p, 44100.0, 256);
    h.run_ms(500);
    check(p.switchCore("fizz"), "FIZZ: switched to");
    h.run_ms(2500);
    PanelComponent panel(p);
    panel.setButtonNames(p.buttonNames());
    panel.refresh();
    check(panel.buttonCap(EMU_B_ARP) == "MIDI FX",
          "FIZZ: the ARP button reads MIDI FX (" + panel.buttonCap(EMU_B_ARP).toStdString() + ")");
    const juce::String sel0 = panel.knobCaption(EMU_E_SELECT);
    p.panelButton(EMU_B_GLO, true);              // SHIFT held
    h.run_ms(400);
    panel.refresh();
    check(panel.knobCaption(EMU_E_SELECT) == "TRACK",
          "SHIFT held: SELECT reads TRACK (" + panel.knobCaption(EMU_E_SELECT).toStdString() + ")");
    check(panel.buttonCap(EMU_B_OCTDN) == "UNDO", "  .. F1 reads UNDO (" + panel.buttonCap(EMU_B_OCTDN).toStdString() + ")");
    p.panelButton(EMU_B_GLO, false);
    h.run_ms(400);
    panel.refresh();
    check(panel.knobCaption(EMU_E_SELECT) == "PAGE" && sel0 != "TRACK",
          "SHIFT let go: SELECT back (" + panel.knobCaption(EMU_E_SELECT).toStdString() + ")");
    check(panel.keyText(0).isNotEmpty() && panel.keyText(0) != "(note)",
          "the keys: FIZZ's labels (W1 " + panel.keyText(0).toStdString() + ")");
    if (core_built("choralroot")) {              // (another firmware: its static labels)
        check(p.switchCore("choralroot"), "ChoralRoot: switched to");
        h.run_ms(2500);
        panel.setButtonNames(p.buttonNames());
        panel.refresh();
        check(panel.keyText(0) == "(note)" && panel.buttonCap(EMU_B_ARP) == p.buttonNames()[EMU_B_ARP],
              "ChoralRoot: its static labels, the keys' note names (no live labels)");
    } else {
        std::printf("  (ChoralRoot not built here: its static labels not checked)\n");
    }
    std::printf("panel_labels_test: %s (%d failure%s)\n", fails ? "FAILED" : "passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
