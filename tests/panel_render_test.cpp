// SPDX-License-Identifier: GPL-3.0-only
// The panel GUI, headless (FM1-VST-PLAN.md phase 3): no window is opened. The processor is driven block by block,
// the PanelComponent is painted offscreen into juce::Images (JUCE's software renderer, and the native one), and:
//   - build/panel/<theme>.png for every preset in themes/presets.json and one custom theme (904 x 566 at scale 2,
//     key D4 held through panelKey), build/panel/lcd.png (the LCD alone), lcd-1x.png / lcd-2x.png / big-lcd.png;
//   (a) the LCD on the panel, at integer scales (1x, 2x, and the big view), sampled back to 240 x 240, equals the
//       device's framebuffer converted to RGB888 within 1 LSB per channel (the nearest-neighbour path);
//   (b) key D4's LED is the lit colour while panelKey holds it and the LED-off colour after, and the key_09 host
//       parameter never moved;
//   (c) every preset's derived palette keeps the contrast floors (label / plate, pointer / knob cap, LED-off / cap,
//       pressed / cap), printed as a table;
//   (d) a custom theme: saved, listed, the default for a new instance; a state naming it restores its colours into
//       the editor after its file is deleted;
//   (r) the face knobs: a host write of Knob 1 (Voicing) at 0.25 and at 0.75 turns its pointer by the firmware's
//       values' distance on the 270 degree sweep (build/panel/knob1-025.png, knob1-075.png: the knob's pixels
//       differ), the panel's own turn moves it too; a relative knob turns 15 degrees per detent from any source;
//   (e) every other core: build/panel/core-<id>.png (switchCore, 1.5 s after the power-on, then D4 held 300 ms; the
//       Emulator theme): its screen and its button labels; the LCD region (at lcd-1x's layout) equals its
//       framebuffer.
//   (k) the computer keys at the component (keyPressed / keyStateChanged with the key-down probe faked): a press and
//       its repeats press once; a release the host swallowed is caught by the check; the fallback row (C = FX as
//       F5) and its hints; two keys of one control; Cmd ignored; focus lost lets go; nothing sticks.
// FM1EMU_HOME points at a scratch folder under the build.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "Editor.h"
#include "PanelComponent.h"
#include "Processor.h"
#include "Theme.h"

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

static juce::Image render(PanelComponent &panel, float scale, bool native = false)
{
    panel.refresh();
    const int w = juce::roundToInt((float)panel.getWidth() * scale), h = juce::roundToInt((float)panel.getHeight() * scale);
    juce::Image img = native ? juce::Image(juce::Image::RGB, w, h, true)
                             : juce::Image(juce::Image::RGB, w, h, true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(scale));
        panel.paintEntireComponent(g, true);
    }
    return img;
}

static bool writePng(const juce::Image &img, const juce::File &f)
{
    f.deleteFile();
    juce::FileOutputStream out(f);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream(img, out);
}

static juce::String fileNameFor(const juce::String &theme)
{
    return theme.replaceCharacters("/ ", "--");
}

static int maxDiff(juce::Colour a, juce::Colour b)
{
    return std::max({std::abs(a.getRed() - b.getRed()), std::abs(a.getGreen() - b.getGreen()), std::abs(a.getBlue() - b.getBlue())});
}

// the LCD region of img (n pixels per LCD pixel from r's origin) against the device's framebuffer: the worst channel
// difference, and the number of pixels off by more than 1
static void compareLcd(const juce::Image &img, juce::Rectangle<int> r, int n, const uint16_t *fb, int &worst, int &bad)
{
    worst = 0;
    bad = 0;
    for (int y = 0; y < EMU_LCD_H; y++)
        for (int x = 0; x < EMU_LCD_W; x++) {
            const uint16_t v = fb[y * EMU_LCD_W + x];
            const uint32_t p = (uint32_t)((v >> 8) | ((v & 0xFFu) << 8));
            const uint32_t r5 = (p >> 11) & 31u, g6 = (p >> 5) & 63u, b5 = p & 31u;
            const juce::Colour want((juce::uint8)((r5 << 3) | (r5 >> 2)), (juce::uint8)((g6 << 2) | (g6 >> 4)),
                                    (juce::uint8)((b5 << 3) | (b5 >> 2)));
            // every pixel of the n x n block (nearest-neighbour: all equal)
            for (int sy = 0; sy < n; sy++)
                for (int sx = 0; sx < n; sx++) {
                    const int d = maxDiff(img.getPixelAt(r.getX() + x * n + sx, r.getY() + y * n + sy), want);
                    worst = std::max(worst, d);
                    bad += d > 1;
                }
        }
}

static uint32_t lcdChecksum(const uint16_t *fb)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < EMU_LCD_W * EMU_LCD_H; i++)
        h = (h ^ fb[i]) * 16777619u;
    return h;
}

int main()
{
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(FM1_PANEL_OUT);
    out.createDirectory();
    std::printf("panel_render_test: FM1EMU_HOME=%s, pictures in %s\n", scratch.getFullPathName().toRawUTF8(),
                out.getFullPathName().toRawUTF8());

    const Theme custom = [] {
        Theme t;
        t.name = "Test Teal";
        t.base = juce::Colour(0xFF1F3B3D);
        t.membrane = juce::Colour(0xFFE07A3C);
        t.knob = juce::Colour(0xFFE8E2D6);
        t.bed = std::nullopt;
        t.label = std::nullopt;
        return t;
    }();

    {
        FM1Processor p;
        Host h(p, 44100.0, 256);
        h.run_ms(600);
        Device *dev = p.deviceForTest();
        check(dev && dev->booted() && !dev->halted(), "the device booted and runs");

        PanelComponent panel(p);
        panel.setButtonNames(p.buttonNames());
        panel.setBounds(0, 0, 904, 566);
        const Theme emu = ThemeStore::preset("Emulator").value_or(Theme{});
        panel.setTheme(emu);

        // ---- (b) key D4 (MIDI 62 = key 9) held from the panel
        const int D4 = 62 - FM1Processor::kNoteBase;
        p.panelKey(D4, true);
        h.run_ms(300);
        check(dev->led_key(D4) == 2, "D4 held through panelKey: its LED is lit on the device (led_key " +
                                         std::to_string(dev->led_key(D4)) + ")");
        check(((dev->hal()->keys >> D4) & 1u) != 0, "D4 held through panelKey: the HAL holds the key");
        check(!p.keyParam(D4)->get(), "the key_09 host parameter did not move (the panel is its own source)");
        juce::Image held = render(panel, 2.0f);
        const juce::Point<int> led = panel.keyLedPx(D4);
        const juce::Colour litPx = held.getPixelAt(led.x, led.y);
        check(maxDiff(litPx, panel.palette().white) <= 1,
              "D4's LED pixel while held is the lit colour " + toHex(panel.palette().white).toStdString() + " (got " +
                  toHex(litPx).toStdString() + ")");

        // the pictures: every preset and one custom theme, D4 held
        for (const Theme &t : ThemeStore::presets()) {
            panel.setTheme(t);
            const juce::File f = out.getChildFile(fileNameFor(t.name) + ".png");
            check(writePng(render(panel, 2.0f), f), "wrote " + f.getFullPathName().toStdString());
        }
        panel.setTheme(custom);
        {
            const juce::File f = out.getChildFile("custom-" + fileNameFor(custom.name) + ".png");
            check(writePng(render(panel, 2.0f), f), "wrote " + f.getFullPathName().toStdString());
        }
        panel.setTheme(emu);

        p.panelKey(D4, false);
        h.run_ms(300);
        // ChoralRoot's keys glow dim at rest (fm1_led_dim): D4 goes from lit back to dim; the LED-off colour is
        // checked on an LED that is off (PLAY's green one, stopped)
        const int after = dev->led_key(D4);
        check(after != 2, "D4 released: its LED is no longer lit on the device (led_key " + std::to_string(after) +
                              (after == 1 ? ": ChoralRoot's keys glow dim at rest)" : ")"));
        juce::Image released = render(panel, 2.0f);
        const Palette &pal = panel.palette();
        auto dimOf = [](juce::Colour off, juce::Colour lit) {   // emu.c blend at alpha .38
            auto ch = [](int s, int d) { return (juce::uint8)((float)s + (float)(d - s) * .38f); };
            return juce::Colour(ch(off.getRed(), lit.getRed()), ch(off.getGreen(), lit.getGreen()), ch(off.getBlue(), lit.getBlue()));
        };
        const juce::Colour wantD4 = after == 1 ? dimOf(pal.ledOff, pal.white) : after == 2 ? pal.white : pal.ledOff;
        const juce::Colour d4Px = released.getPixelAt(led.x, led.y);
        check(maxDiff(d4Px, wantD4) <= 1, "D4's LED pixel after the release follows the device: " +
                                              std::string(after == 1 ? "dim " : after == 2 ? "lit " : "off ") +
                                              toHex(wantD4).toStdString() + " (got " + toHex(d4Px).toStdString() + ")");
        int offKeys = 0;
        for (int k = 0; k < EMU_NKEY; k++)
            offKeys += dev->led_key(k) == 0;
        std::printf("  (key LEDs off on the device at rest: %d of %d)\n", offKeys, EMU_NKEY);
        {
            const juce::Point<int> o = panel.playGreenPx();
            const juce::Colour offPx = released.getPixelAt(o.x, o.y);
            check(dev->led_play_green() == 0 && maxDiff(offPx, pal.ledOff) <= 1,
                  "PLAY's green LED (off on the device) is the LED-off colour " + toHex(pal.ledOff).toStdString() + " (got " +
                      toHex(offPx).toStdString() + ")");
        }
        writePng(released, out.getChildFile("Emulator-released.png"));

        // a button through the panel: SEL (ChoralRoot KEY) reaches the HAL, its parameter does not move
        p.panelButton(EMU_B_SEL, true);
        h.run_ms(50);
        const bool selDown = (dev->hal()->buttons >> dev->hal()->btn_id[EMU_B_SEL]) & 1u;
        p.panelButton(EMU_B_SEL, false);
        h.run_ms(50);
        const bool selUp = !((dev->hal()->buttons >> dev->hal()->btn_id[EMU_B_SEL]) & 1u);
        check(selDown && selUp && !p.buttonParam(EMU_B_SEL)->get(), "panelButton holds and releases SEL on the HAL; btn_sel unmoved");
        // MASTER from the panel moves the master parameter (16 per detent)
        {
            const int m0 = p.masterParam()->get();
            p.panelEnc(EMU_E_MASTER, -2);
            h.run_ms(20);
            check(p.masterParam()->get() == m0 - 32 && dev->hal()->master == m0 - 32,
                  "panelEnc(MASTER, -2): the master parameter and the HAL move by 32 (" + std::to_string(m0) + " -> " +
                      std::to_string(dev->hal()->master) + ")");
        }

        // ---- (r) the face knobs follow what they control: Knob 1 (Voicing on the view) written by the host to 0.25,
        // then 0.75: the pointer on MASTER's 270 degree sweep at the firmware's value, the caption "VOICING" under
        // KNOB1; a relative knob (PRESETS) turns by its detents, whoever gave them (the host's relative turn here)
        {
            KnobParameter *k1 = p.knobParam(EMU_E_K1);
            const fm1param_t *e = k1->entry();
            auto normOf = [&]() { return e ? (float)(e->get() - e->min) / (float)(e->max - e->min) : -1.f; };
            k1->setValueNotifyingHost(0.25f);
            h.run_ms(60);
            const float n25 = normOf();
            const juce::Image a = render(panel, 2.0f);
            const float a25 = panel.knobAngle(EMU_E_K1);
            k1->setValueNotifyingHost(0.75f);
            h.run_ms(60);
            const float n75 = normOf();
            const juce::Image b = render(panel, 2.0f);
            const float a75 = panel.knobAngle(EMU_E_K1);
            const juce::Rectangle<int> box = panel.knobBoxPx(EMU_E_K1);
            int differ = 0;
            for (int y = box.getY(); y < box.getBottom(); y++)
                for (int x = box.getX(); x < box.getRight(); x++)
                    differ += maxDiff(a.getPixelAt(x, y), b.getPixelAt(x, y)) > 24;
            const float want = (n75 - n25) * 1.5f * 3.14159265f;
            check(k1->functionName() == "Voicing" && std::abs((a75 - a25) - want) < 1e-3f && differ > 20,
                  "host Knob 1 (Voicing) 0.25 -> 0.75 (the firmware's value at " + std::to_string(n25) + " -> " +
                      std::to_string(n75) + " of its range): the KNOB1 pointer turned " +
                      std::to_string((a75 - a25) * 180.f / 3.14159265f) + " degrees, " + std::to_string(differ) +
                      " pixels of the knob differ");
            const juce::File fa = out.getChildFile("knob1-025.png"), fb = out.getChildFile("knob1-075.png");
            check(writePng(a, fa) && writePng(b, fb), "wrote " + fa.getFullPathName().toStdString() + " and " +
                                                          fb.getFileName().toStdString());
            // a firmware-side change moves it too: the panel's own turn of KNOB1 (+3 detents of voicing)
            const float before = panel.knobAngle(EMU_E_K1);
            p.panelEnc(EMU_E_K1, -3);
            h.run_ms(60);
            render(panel, 2.0f);
            check(panel.knobAngle(EMU_E_K1) < before, "the panel's KNOB1 -3: the pointer follows the firmware's value (" +
                                                          std::to_string(normOf()) + ")");
            KnobParameter *pr = p.knobParam(EMU_E_PRESETS);
            const float r0 = panel.knobAngle(EMU_E_PRESETS);
            pr->setValueNotifyingHost(0.5f + 2.0f / 24.0f);   // (the host's relative turn: 2 detents)
            h.run_ms(60);
            render(panel, 2.0f);
            const float dr = (panel.knobAngle(EMU_E_PRESETS) - r0) * 180.f / 3.14159265f;
            check(pr->isRelative() && std::abs(dr - 30.f) < 0.5f,
                  "PRESETS (relative) turned 2 detents by the host: its pointer turned " + std::to_string(dr) +
                      " degrees (15 per detent)");
            pr->setValueNotifyingHost(0.5f);
            h.run_ms(600);
            k1->setValueNotifyingHost(k1->toNorm(0));
            h.run_ms(60);
            panel.refresh();
        }

        // ---- (a) the LCD region, pixel for pixel, at integer scales
        h.run_ms(200);
        panel.refresh();
        {
            struct Case {
                const char *name;
                int w, h;
                float scale;
                bool big;
                int wantN;
            };
            const Case cases[] = {{"lcd-1x", 1402, 861, 1.0f, false, 1}, {"lcd-2x", 1450, 880, 2.0f, false, 2},
                                  {"big-lcd", 960, 1550, 1.0f, true, 4}};
            for (const Case &c : cases) {
                for (int native = 0; native < 2; native++) {
                    panel.setBigLcd(c.big);
                    panel.setBounds(0, 0, c.w, c.h);
                    const juce::Image img = render(panel, c.scale, native != 0);
                    const PanelComponent::Layout &L = panel.layout();
                    const uint16_t *fb = dev->lcd();
                    int worst = 0, bad = 0;
                    const int n = c.big ? L.nBig : L.nSmall;
                    const juce::Rectangle<int> r = c.big ? L.lcdBig : L.lcdSmall;
                    check(n == c.wantN, std::string(c.name) + ": the layout snapped to an integer LCD scale " + std::to_string(n) +
                                            " (k " + std::to_string(L.k) + ", want " + std::to_string(c.wantN) + ")");
                    if (n > 0) {
                        compareLcd(img, r, n, fb, worst, bad);
                        check(bad == 0, std::string(c.name) + (native ? " (native renderer)" : " (software renderer)") +
                                            ": the LCD region equals the device framebuffer (checksum " +
                                            juce::String::toHexString((int)lcdChecksum(fb)).toStdString() + ") at " +
                                            std::to_string(n) + "x: worst channel difference " + std::to_string(worst) +
                                            ", pixels off by more than 1: " + std::to_string(bad));
                    }
                    if (!native) {
                        const juce::File f = out.getChildFile(juce::String(c.name) + ".png");
                        writePng(img, f);
                    }
                }
            }
            panel.setBigLcd(false);
            panel.setBounds(0, 0, 904, 566);
            const juce::File f = out.getChildFile("lcd.png");
            check(writePng(panel.lcdImage(), f), "wrote " + f.getFullPathName().toStdString() + " (the LCD alone)");
            // the screen shows something (the home screen is not all one colour)
            const juce::Image &lcd = panel.lcdImage();
            int distinct = 0;
            const juce::Colour c0 = lcd.getPixelAt(0, 0);
            for (int y = 0; y < EMU_LCD_H; y += 4)
                for (int x = 0; x < EMU_LCD_W; x += 4)
                    distinct += lcd.getPixelAt(x, y) != c0;
            check(distinct > 50, "the LCD shows the firmware's screen (" + std::to_string(distinct) + " sampled pixels differ from the corner)");
        }
    }

    // ---- (e) the other cores: their screen and labels on the panel
    for (const char *id : {"felucca", "melodee"}) {
        FM1Processor p;
        Host h(p, 44100.0, 256);
        const bool ok = p.switchCore(id);
        h.run_ms(1500);
        Device *dev = p.deviceForTest();
        const int D4 = 62 - FM1Processor::kNoteBase;
        p.panelKey(D4, true);
        h.run_ms(300);
        PanelComponent panel(p);
        panel.setButtonNames(p.buttonNames());
        panel.setBounds(0, 0, 904, 566);
        panel.setTheme(ThemeStore::preset("Emulator").value_or(Theme{}));
        const juce::Image img = render(panel, 2.0f);
        const juce::File f = out.getChildFile(juce::String("core-") + id + ".png");
        std::string labels;
        for (const juce::String &b : p.buttonNames())
            labels += " " + b.toStdString();
        check(ok && dev && dev->booted() && !dev->halted() && !std::strcmp(dev->core()->id, id) && writePng(img, f),
              std::string(id) + ": wrote " + f.getFullPathName().toStdString() + " (buttons" + labels + ")");
        check(dev && dev->led_key(D4) == 2 && ((dev->hal()->keys >> D4) & 1u), std::string(id) + ": D4 held, its LED lit");
        if (dev) {
            int worst = 0, bad = 0;
            panel.setBounds(0, 0, 1402, 861);            // (lcd-1x's layout: the LCD at an integer scale)
            const juce::Image one = render(panel, 1.0f);
            const PanelComponent::Layout &L = panel.layout();
            compareLcd(one, L.lcdSmall, L.nSmall, dev->lcd(), worst, bad);
            int distinct = 0;
            for (int i = 0; i < EMU_LCD_W * EMU_LCD_H; i += 37)
                distinct += dev->lcd()[i] != dev->lcd()[0];
            check(L.nSmall > 0 && bad == 0 && distinct > 50, std::string(id) + ": the LCD region equals its framebuffer at " +
                                                                 std::to_string(L.nSmall) + "x (worst " + std::to_string(worst) +
                                                                 "), the screen is drawn (" + std::to_string(distinct) + ")");
        }
        p.panelKey(D4, false);
        h.run_ms(50);
    }

    // ---- (k) the computer keys at the component: keyPressed / keyStateChanged as a host passes them (or not), the
    // key-down probe faked (what a key's physical state says), the device's key and button bits after each block
    {
        FM1Processor p;
        Host h(p, 44100.0, 256);
        h.run_ms(600);
        Device *dev = p.deviceForTest();
        emu_hal_t *hal = dev->hal();
        PanelComponent panel(p);
        panel.setBounds(0, 0, 904, 566);
        std::set<int> down;                       // the keys physically down
        panel.setKeyProbe([&](int code) { return down.count(code) != 0; });
        auto keyBit = [&](int k) { return ((hal->keys >> k) & 1u) != 0; };
        auto btnBit = [&](int b) { return ((hal->buttons >> hal->btn_id[b]) & 1u) != 0; };
        auto press = [&](int code, juce::ModifierKeys mods = {}) {
            down.insert(code);
            panel.keyStateChanged(true);
            return panel.keyPressed(juce::KeyPress(code, mods, 0));
        };
        auto release = [&](int code, bool hostPassesIt) {
            down.erase(code);
            if (hostPassesIt)
                panel.keyStateChanged(false);
        };
        const int D4 = 62 - FM1Processor::kNoteBase;

        // a press, its repeats (keyPressed again while held: no second press), a normal release
        press('S');
        h.run_ms(30);
        const bool held = keyBit(D4);
        bool stayed = true;
        for (int i = 0; i < 6; i++) {             // (the key repeat: keyPressed every ~35 ms)
            panel.keyPressed(juce::KeyPress('s'));
            panel.keyStateChanged(true);
            h.run_ms(35);
            stayed = stayed && keyBit(D4);
        }
        check(held && stayed && panel.keysHeldFromKeyboard() == 1,
              "S (D4) pressed: the key held on the device; 6 repeats of keyPressed: held throughout, pressed once");
        release('S', true);
        h.run_ms(30);
        check(!keyBit(D4) && panel.keysHeldFromKeyboard() == 0, "S released (keyStateChanged): the key let go");

        // a release the host swallowed: no keyStateChanged; the 30 Hz check finds the key up
        press('X');                               // OCT+
        h.run_ms(30);
        const bool octHeld = btnBit(EMU_B_OCTUP);
        release('X', false);
        h.run_ms(100);
        const bool stuckBefore = btnBit(EMU_B_OCTUP);
        panel.checkHeldKeys();                    // (the timer's work)
        h.run_ms(30);
        check(octHeld && stuckBefore && !btnBit(EMU_B_OCTUP) && panel.keysHeldFromKeyboard() == 0,
              "X (OCT+) pressed, its release never passed on: held until the check, which finds the key up and lets go");
        // .. and the next press after a swallowed release is a press again, not a repeat
        press('X');
        h.run_ms(30);
        const bool again = btnBit(EMU_B_OCTUP);
        release('X', false);
        panel.keyStateChanged(false);             // (another key's event: the check runs too)
        h.run_ms(30);
        check(again && !btnBit(EMU_B_OCTUP), "  ... the next press of X presses OCT+ again; any key event lets go of it");
        press('Z');                               // (back to octave 0)
        h.run_ms(100);
        release('Z', true);
        h.run_ms(100);

        // the fallback row: C = FX, as F5; the panel prints both
        press('C');
        h.run_ms(30);
        const bool fxC = btnBit(EMU_B_FX);
        release('C', true);
        h.run_ms(30);
        const bool fxCUp = !btnBit(EMU_B_FX);
        press(juce::KeyPress::F5Key);
        h.run_ms(30);
        const bool fxF5 = btnBit(EMU_B_FX);
        release(juce::KeyPress::F5Key, true);
        h.run_ms(30);
        check(fxC && fxCUp && fxF5 && !btnBit(EMU_B_FX) && PanelComponent::keymapHintText(PanelComponent::KM_BTN, EMU_B_FX) == "F5/C" &&
                  PanelComponent::keymapHintText(PanelComponent::KM_BTN, EMU_B_GLO) == "F10/," &&
                  PanelComponent::keymapHintText(PanelComponent::KM_BTN, EMU_B_HOME) == "7",
              "C presses FX as F5 does (the fallback row C V B N M , for F5 .. F10); the panel prints \"F5/C\" .. \"F10/,\"");
        for (int code : {'V', 'B', 'N', 'M', ','}) {
            press(code);
            h.run_ms(30);
            const bool on = hal->buttons != 0;
            release(code, true);
            h.run_ms(30);
            if (!on || hal->buttons != 0)
                check(false, std::string("fallback key '") + (char)code + "' presses and releases its button");
        }
        h.run_ms(400);
        dev->buttons_tap(1u << EMU_B_OCTDN);      // (OCT-: whatever the taps opened, closed)
        h.run_ms(200);

        // two keys of one control (Z and Esc: OCT-): held while either is
        press('Z');
        press(juce::KeyPress::escapeKey);
        h.run_ms(30);
        release('Z', true);
        h.run_ms(30);
        const bool stillHeld = btnBit(EMU_B_OCTDN);
        release(juce::KeyPress::escapeKey, true);
        h.run_ms(30);
        check(stillHeld && !btnBit(EMU_B_OCTDN), "Z and Esc (both OCT-) held, Z let go: OCT- still held; Esc let go: released");

        // Cmd is the host's; losing the focus lets go of everything the keyboard holds
        check(!panel.keyPressed(juce::KeyPress('S', juce::ModifierKeys::commandModifier, 0)), "Cmd+S is not the panel's");
        down.erase('S');
        press('S');
        press('A');
        press('V');
        h.run_ms(30);
        const bool three = keyBit(D4) && keyBit(7) && btnBit(EMU_B_SEL);
        panel.focusLost(juce::Component::focusChangedDirectly);
        h.run_ms(30);
        check(three && hal->keys == 0 && hal->buttons == 0 && panel.keysHeldFromKeyboard() == 0,
              "S A V held, the focus lost (keys still down): every key and button let go on the device");
        down.clear();
        h.run_ms(500);
        check(hal->keys == 0 && hal->buttons == 0, "nothing sticks: no key or button bit left on the device");
    }

    // ---- (c) the contrast floors, every preset (and the custom theme)
    {
        constexpr double kLabel = 3.0, kPointer = 2.0, kLed = 1.3, kPressed = 1.3;
        std::printf("\n  contrast ratios (floors: label/plate %.1f, pointer/knob %.1f, LED-off/cap %.1f, pressed/cap %.1f)\n",
                    kLabel, kPointer, kLed, kPressed);
        std::printf("  %-12s %-8s %-8s %-8s | %11s %13s %11s %11s | %9s %9s %9s\n", "theme", "base", "membrane", "knob",
                    "label/plate", "pointer/knob", "ledoff/cap", "pressed/cap", "bedlabel", "captext", "hint/cap");
        std::vector<Theme> all = ThemeStore::presets();
        all.push_back(custom);
        bool ok = true;
        for (const Theme &t : all) {
            const Palette p = derive(t);
            const double a = contrastRatio(p.label, p.plate), b = contrastRatio(p.pointer, p.knobCap);
            const double c = contrastRatio(p.ledOff, p.cap), d = contrastRatio(p.pressed, p.cap);
            std::printf("  %-12s %s  %s  %s  | %11.2f %13.2f %11.2f %11.2f | %9.2f %9.2f %9.2f\n", t.name.toRawUTF8(),
                        toHex(t.base).toRawUTF8(), toHex(t.membrane).toRawUTF8(), toHex(t.knob).toRawUTF8(), a, b, c, d,
                        contrastRatio(p.bedLabel, p.bed), contrastRatio(p.capText, p.cap), contrastRatio(p.hint, p.cap));
            ok = ok && a >= kLabel && b >= kPointer && c >= kLed && d >= kPressed;
        }
        std::printf("\n");
        check(ok, "every theme keeps the contrast floors");
    }

    // ---- (d) the custom theme round trip
    {
        ThemeStore store(FM1Processor::home());
        check(store.save(custom), "saved the custom theme to " + store.fileFor(custom.name).getFullPathName().toStdString());
        bool listed = false;
        for (const Theme &t : store.customs())
            listed = listed || (t.name == custom.name && t.sameColours(custom));
        check(listed, "customs() lists it with its colours");
        check(!store.save(ThemeStore::presets().front()), "a custom theme cannot take a preset's name");
        check(store.setDefaultName(custom.name) && store.defaultName() == custom.name, "set as the default theme");
        {
            FM1Processor fresh;
            const Theme t = fresh.getTheme();
            check(t.name == custom.name && t.sameColours(custom), "a new instance starts with the default theme");
        }
        juce::MemoryBlock state;
        {
            FM1Processor a;
            a.setTheme(custom);
            a.getStateInformation(state);
        }
        store.setDefaultName("");
        check(store.remove(custom.name) && !store.fileFor(custom.name).exists() && !store.find(custom.name),
              "deleted the theme file");
        FM1Processor b;
        check(b.getTheme().name == "Emulator", "without a default, a new instance starts with Emulator (" + b.getTheme().name.toStdString() + ")");
        b.setStateInformation(state.getData(), (int)state.getSize());
        {
            FM1Editor ed(b);
            const Theme t = ed.panelTheme();
            check(t.name == custom.name && t.sameColours(custom),
                  "the state restores the theme into the editor with its file gone (" + t.name.toStdString() + " " +
                      toHex(t.base).toStdString() + " " + toHex(t.membrane).toStdString() + " " + toHex(t.knob).toStdString() + ")");
            const juce::Image img = [&] {
                juce::Image i(juce::Image::RGB, ed.getWidth(), ed.getHeight(), true, juce::SoftwareImageType());
                juce::Graphics g(i);
                ed.paintEntireComponent(g, true);
                return i;
            }();
            const juce::File f = out.getChildFile("editor.png");
            check(writePng(img, f), "wrote " + f.getFullPathName().toStdString() + " (the whole editor, " +
                                        std::to_string(ed.getWidth()) + " x " + std::to_string(ed.getHeight()) + ")");
        }
    }

    std::printf("panel_render_test: %s (%d failure%s)\n", fails ? "FAILED" : "passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
