// SPDX-License-Identifier: GPL-3.0-only
// The editor around the panel, headless (no window is opened): FM1Editor painted offscreen at 2x (HiDPI) into
// build/panel/ui-*.png, and:
//   (v) the device view: the panel edge to edge at the default size, nothing else; the corner button shown
//       (ui-device-button.png), the settings sheet (ui-sheet.png, ui-sheet-tall.png), the firmware browser
//       (ui-firmware.png, ui-firmware-synth.png), an inline rename (ui-rename.png), the flash reset's confirmation
//       (ui-reset-confirm.png), the about page and the custom theme page; the smallest and largest device views;
//   (c) the corner button never covers a control, a printed name, a key hint or the screen, at every width from the
//       smallest to the largest (and with the big LCD), and stays inside the window;
//   (s) the screen view at two sizes, with and without the button: square, its LCD equals the device's framebuffer at a
//       whole multiple, the computer keys still play the panel;
//   (m) the mouse: a right-click off the controls opens the sheet; on a key it still latches the key (no sheet);
//   (p) the state: the view mode and both sizes round-trip; a state from before them (no viewMode / screenSize, the
//       old bar's height in editorH) opens the device view at the panel's aspect;
//   (a) the accent of every theme preset reads on the sheet (3:1);
//   (e) emulated firmwares (the Hub unreachable: the bundled list): the browser's engine filter (ui-firmware-all.png,
//       ui-firmware-emulated.png), a Hub firmware with a native core too picked on its emulated engine (its package
//       put in the cache first: no network): the main page's Engine, clock and Source rows (ui-sheet-emulated.png),
//       its About page (ui-about-emulated.png).
// FM1EMU_HOME points at a scratch folder under the build.
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "built_cores.h"
#include "Editor.h"
#include "Hub.h"
#include "Look.h"
#include "PanelComponent.h"
#include "Processor.h"
#include "Sheet.h"
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

static juce::Image render(FM1Editor &ed, float scale)
{
    ed.panel().refresh();
    juce::Image img(juce::Image::RGB, juce::roundToInt((float)ed.getWidth() * scale), juce::roundToInt((float)ed.getHeight() * scale),
                    true, juce::SoftwareImageType());
    juce::Graphics g(img);
    g.addTransform(juce::AffineTransform::scale(scale));
    ed.paintEntireComponent(g, true);
    return img;
}

static juce::File outDir() { return juce::File(FM1_PANEL_OUT); }

static void shot(FM1Editor &ed, const juce::String &name, float scale = 2.0f)
{
    const juce::File f = outDir().getChildFile("ui-" + name + ".png");
    f.deleteFile();
    juce::FileOutputStream out(f);
    juce::PNGImageFormat png;
    const juce::Image img = render(ed, scale);
    check(out.openedOk() && png.writeImageToStream(img, out),
          "wrote " + f.getFullPathName().toStdString() + " (" + std::to_string(ed.getWidth()) + " x " + std::to_string(ed.getHeight()) +
              " at " + std::to_string((int)scale) + "x)");
}

static juce::MouseEvent mouseAt(juce::Component &c, juce::Point<float> p, juce::ModifierKeys mods)
{
    const juce::Time now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p,
                            now, 1, false);
}

int main()
{
    require_cores("ui_render_test", {"choralroot"});
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    // (the Hub unreachable: the browser shows the bundled list, and nothing is fetched)
    setenv("FM1VST_HUB_CATALOG", ("file://" + scratch.getFullPathName() + "/no-hub/catalog.json").toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    outDir().createDirectory();
    std::printf("ui_render_test: FM1EMU_HOME=%s, pictures in %s\n", scratch.getFullPathName().toRawUTF8(),
                outDir().getFullPathName().toRawUTF8());

    FM1Processor p;
    Host h(p, 44100.0, 256);
    h.run_ms(1200);
    p.savePreset("Choir of Glass");
    p.savePreset("Warm Pads");

    {
        FM1Editor ed(p);
        check(ed.viewMode() == FM1Processor::kViewDevice && ed.getWidth() == 1402 && ed.getHeight() == 861,
              "a new instance opens the device view at 1402 x 861 (" + std::to_string(ed.getWidth()) + " x " +
                  std::to_string(ed.getHeight()) + ")");
        check(ed.panel().getBounds() == ed.getLocalBounds() && ed.panel().isVisible() && !ed.screen().isVisible() &&
                  !ed.sheet().isVisible(),
              "the panel fills the editor edge to edge; no screen view, no sheet");

        // ---- (v) the device view
        ed.hideChromeNow();
        shot(ed, "device");
        ed.showChromeNow();
        shot(ed, "device-button");

        // ---- (c) the corner button, every width
        {
            bool clear = true, inside = true;
            std::string worst;
            for (int big = 0; big < 2; big++) {
                ed.setBigLcd(big != 0);
                for (int w = FM1Editor::kMinW; w <= FM1Editor::kMaxW; w += (w < 1600 ? 7 : 61)) {
                    const int hgt = big ? juce::jlimit(300, 4000, (int)(w * 0.9)) : juce::roundToInt((float)w / PanelComponent::frameAspect(false));
                    ed.setSize(w, hgt);
                    ed.panel().refresh();
                    const juce::Rectangle<float> c = ed.cornerCircle();
                    if (ed.panel().overlapsControl(c)) {
                        clear = false;
                        worst = std::to_string(w) + " x " + std::to_string(hgt);
                    }
                    if (!ed.getLocalBounds().toFloat().contains(c))
                        inside = false, worst = std::to_string(w) + " x " + std::to_string(hgt) + " (outside)";
                }
            }
            ed.setBigLcd(false);
            check(clear && inside, "the corner button covers no control, name, hint or the screen and stays inside the window, at "
                                   "every width 640 .. 5000 (with and without the big LCD)" +
                                       (worst.empty() ? std::string() : ": fails at " + worst));
        }

        // smallest and largest
        ed.setSize(FM1Editor::kMinW, juce::roundToInt((float)FM1Editor::kMinW / PanelComponent::frameAspect(false)));
        ed.showChromeNow();
        shot(ed, "device-min");
        ed.setSize(FM1Editor::kMaxW, juce::roundToInt((float)FM1Editor::kMaxW / PanelComponent::frameAspect(false)));
        shot(ed, "device-max", 1.0f);
        ed.setSize(1402, 861);
        render(ed, 1.0f);                         // (the panel takes a click by its last paint's layout)

        // ---- (m) right-click: off the controls the sheet; on a key a latch
        {
            PanelComponent &panel = ed.panel();
            panel.refresh();
            ed.hideChromeNow();
            const juce::Point<float> keyPt = panel.unitToLocal(71 + 21, 408 + 60);   // the first white key (F3, key 0)
            panel.mouseDown(mouseAt(panel, keyPt, juce::ModifierKeys::rightButtonModifier));
            panel.mouseUp(mouseAt(panel, keyPt, juce::ModifierKeys()));
            h.run_ms(60);
            const bool latched = (p.deviceForTest()->panel()->keys & 1u) != 0;
            check(latched && !ed.sheet().isOpen(), "a right-click on key F3 latches it (no sheet)");
            panel.mouseDown(mouseAt(panel, keyPt, juce::ModifierKeys::rightButtonModifier));   // (unlatched)
            h.run_ms(60);
            const juce::Point<float> plate = panel.unitToLocal(140, 520);   // (the body under the keys)
            panel.mouseDown(mouseAt(panel, plate, juce::ModifierKeys::rightButtonModifier));
            check(ed.sheet().isOpen() && (p.deviceForTest()->panel()->keys & 1u) == 0,
                  "a right-click on the body (off the controls) opens the sheet");
            ed.sheet().close(false);
        }

        // ---- the sheet and its pages
        ed.openSettings(false);
        check(ed.sheet().isOpen() && ed.sheet().isVisible() && ed.sheet().cardBounds().getWidth() == 360,
              "the sheet opens, 360 wide (" + std::to_string(ed.sheet().cardBounds().getWidth()) + ")");
        shot(ed, "sheet");
        ed.sheet().showPage(Sheet::kFirmware);
        shot(ed, "firmware");
        ed.sheet().setBrowserFilter("", 1);
        shot(ed, "firmware-synth");
        ed.sheet().setBrowserFilter("", 0);
        ed.sheet().showPage(Sheet::kAbout);
        shot(ed, "about");
        ed.sheet().showPage(Sheet::kTheme);
        shot(ed, "theme");
        ed.sheet().showPage(Sheet::kMain);
        ed.sheet().beginRename();
        shot(ed, "rename");
        ed.sheet().beginResetConfirm();
        shot(ed, "reset-confirm");
        ed.sheet().close(false);
        ed.setSize(2600, juce::roundToInt(2600.0f / PanelComponent::frameAspect(false)));
        ed.openSettings(false);
        shot(ed, "sheet-tall", 1.0f);
        ed.sheet().close(false);
        check(!ed.sheet().isVisible(), "the sheet closes");

        // ---- (e) emulated firmwares
        const juce::Rectangle<int> sizeBefore = ed.getLocalBounds();
        ed.setSize(1402, 861);
        {
            juce::SharedResourcePointer<hub::Service> svc;
            const hub::Catalog cat = svc->catalog();
            const hub::Entry *fel = cat.find("felucca");
            juce::MemoryBlock pkg;
            const bool have = juce::File(FM1_FELUCCA_FWSC).loadFileAsData(pkg);
            if (fel && have && hub::verify(pkg.getData(), pkg.getSize(), fel->sha256, fel->size).isEmpty() &&
                hub::store(fel->id, fel->version, fel->sha256, pkg.getData(), pkg.getSize()) != juce::File()) {
                ed.openSettings(false);
                for (int i = 0; i < 200 && svc->refreshing(); i++)
                    juce::Thread::sleep(10);
                ed.sheet().showPage(Sheet::kFirmware);
                ed.sheet().refresh();
                shot(ed, "firmware-all");
                ed.sheet().setEngineFilter(2);
                shot(ed, "firmware-emulated");
                ed.sheet().setEngineFilter(0);
                ed.sheet().pickInBrowser("felucca", 2);   // (cached: at once)
                h.run_ms(2500);
                check(p.isEmulated() && p.getEmuFirmware().id == "felucca", "the browser picks Felucca's emulated engine");
                ed.sheet().showPage(Sheet::kMain);
                ed.sheet().refresh();
                shot(ed, "sheet-emulated");
                ed.sheet().showPage(Sheet::kAbout);
                shot(ed, "about-emulated");
                ed.sheet().showPage(Sheet::kMain);
                ed.sheet().pickInBrowser("felucca", 1);
                check(!p.isEmulated() && p.getCoreInfo().info.id == "felucca", "  ... and its Engine switch back to the native core");
                ed.sheet().close(false);
            } else {
                std::printf("  (no Felucca package at %s: the emulated pages are not drawn)\n", FM1_FELUCCA_FWSC);
            }
        }
        p.switchCore("choralroot");               // (the rest of the test is ChoralRoot's, at its size)
        h.run_ms(1200);
        ed.coreChanged();
        ed.setSize(sizeBefore.getWidth(), sizeBefore.getHeight());

        // ---- (s) the screen view
        ed.setViewMode(FM1Processor::kViewScreen);
        check(ed.getWidth() == FM1Editor::defaultScreenSize() && ed.getHeight() == ed.getWidth() && ed.screen().isVisible() &&
                  !ed.panel().isVisible(),
              "the screen view: square at its default side " + std::to_string(ed.getWidth()));
        for (int side : {FM1Editor::defaultScreenSize(), 900}) {
            ed.setSize(side, side);
            ed.hideChromeNow();
            const juce::Image img = render(ed, 2.0f);
            shot(ed, "screen-" + juce::String(side));
            ed.showChromeNow();
            shot(ed, "screen-" + juce::String(side) + "-button");
            const juce::Rectangle<float> l = ed.screen().lcdArea();
            const int n = ed.screen().lcdScale();
            if (side == FM1Editor::defaultScreenSize()) {
                const uint16_t *fb = p.deviceForTest()->lcd();
                int bad = 0;
                const int x0 = juce::roundToInt(l.getX() * 2), y0 = juce::roundToInt(l.getY() * 2);
                for (int y = 0; y < EMU_LCD_H; y += 3)
                    for (int x = 0; x < EMU_LCD_W; x += 3) {
                        const uint16_t v = fb[y * EMU_LCD_W + x];
                        const uint32_t q = (uint32_t)((v >> 8) | ((v & 0xFFu) << 8));
                        const uint32_t r5 = (q >> 11) & 31u, g6 = (q >> 5) & 63u, b5 = q & 31u;
                        const juce::Colour want((juce::uint8)((r5 << 3) | (r5 >> 2)), (juce::uint8)((g6 << 2) | (g6 >> 4)),
                                                (juce::uint8)((b5 << 3) | (b5 >> 2)));
                        for (int sy = 0; sy < n; sy++)
                            for (int sx = 0; sx < n; sx++) {
                                const juce::Colour got = img.getPixelAt(x0 + x * n + sx, y0 + y * n + sy);
                                bad += std::abs(got.getRed() - want.getRed()) > 1 || std::abs(got.getGreen() - want.getGreen()) > 1 ||
                                       std::abs(got.getBlue() - want.getBlue()) > 1;
                            }
                    }
                check(n == 4 && bad == 0, "screen view " + std::to_string(side) + " at 2x: the LCD pixel for pixel at " + std::to_string(n) +
                                              "x (pixels off: " + std::to_string(bad) + ")");
            }
            const juce::Rectangle<float> c = ed.cornerCircle();
            check(ed.getLocalBounds().toFloat().contains(c) && c.getWidth() >= 22,
                  "screen view " + std::to_string(side) + ": the corner button inside the window (" + std::to_string((int)c.getWidth()) + " pt)");
        }
        {   // the computer keys in the screen view
            std::set<int> down;
            ed.panel().setKeyProbe([&](int code) { return down.count(code) != 0; });
            down.insert('S');
            ed.screen().keyStateChanged(true);
            ed.screen().keyPressed(juce::KeyPress('S'));
            h.run_ms(40);
            const int D4 = 62 - FM1Processor::kNoteBase;
            const bool held = ((p.deviceForTest()->panel()->keys >> D4) & 1u) != 0;
            down.clear();
            ed.screen().keyStateChanged(false);
            h.run_ms(40);
            check(held && ((p.deviceForTest()->panel()->keys >> D4) & 1u) == 0, "screen view: S plays D4, its release lets go");
            ed.panel().setKeyProbe(nullptr);
        }
        ed.setSize(700, 700);
        ed.openSettings(false);
        check(ed.sheet().cardBounds().getWidth() == 360, "screen view 700: the sheet fits (360)");
        shot(ed, "screen-sheet");
        ed.sheet().close(false);
        ed.setSize(FM1Editor::kMinScreen, FM1Editor::kMinScreen);
        ed.openSettings(false);
        check(ed.sheet().cardBounds().getRight() <= ed.getWidth() && ed.sheet().cardBounds().getX() >= 0,
              "screen view at its smallest (320): the sheet stays inside");
        shot(ed, "screen-min-sheet");
        ed.sheet().close(false);
        ed.setSize(700, 700);
    }

    // ---- (p) the state
    {
        juce::MemoryBlock st;
        p.getStateInformation(st);
        FM1Processor b;
        b.setStateInformation(st.getData(), (int)st.getSize());
        check(b.getViewMode() == FM1Processor::kViewScreen && b.getScreenSize() == 700 && b.getEditorSize() == juce::Point<int>(2600, 1597),
              "state: the screen view, its side 700 and the device size 2600 x 1597 round-trip (" + std::to_string(b.getScreenSize()) +
                  ", " + std::to_string(b.getEditorSize().x) + " x " + std::to_string(b.getEditorSize().y) + ")");
        {
            FM1Editor ed(b);
            check(ed.viewMode() == FM1Processor::kViewScreen && ed.getWidth() == 700 && ed.getHeight() == 700,
                  "the restored instance reopens in the screen view, 700 x 700");
            ed.setViewMode(FM1Processor::kViewDevice);
            check(ed.getWidth() == 2600 && ed.getHeight() == 1597, "... and the device view at its own size, 2600 x 1597");
        }
        // a state from before: no viewMode / screenSize; editorH with the old bar (60)
        juce::ValueTree t = juce::ValueTree::readFromData(st.getData(), st.getSize());
        t.removeProperty("viewMode", nullptr);
        t.removeProperty("screenSize", nullptr);
        t.setProperty("editorW", 1402, nullptr);
        t.setProperty("editorH", 921, nullptr);
        juce::MemoryBlock old;
        {
            juce::MemoryOutputStream mo(old, false);
            t.writeToStream(mo);
        }
        FM1Processor c;
        c.setViewMode(FM1Processor::kViewScreen);
        c.setScreenSize(640);
        c.setStateInformation(old.getData(), (int)old.getSize());
        check(c.getViewMode() == FM1Processor::kViewDevice && c.getScreenSize() == 0,
              "an old state (no viewMode / screenSize) loads: the device view, no screen size");
        FM1Editor ed(c);
        check(ed.viewMode() == FM1Processor::kViewDevice && ed.getWidth() == 1402 && ed.getHeight() == 861,
              "it opens the device view at the panel's aspect: 1402 x 861 (the old 921 had the bar) (" +
                  std::to_string(ed.getWidth()) + " x " + std::to_string(ed.getHeight()) + ")");
    }

    // ---- (a) the accent
    {
        bool ok = true;
        std::string list;
        for (const Theme &t : ThemeStore::presets()) {
            Look l;
            l.setAccentFrom(t);
            const double r = contrastRatio(l.accent(), Look::col(Look::C::surface));
            ok = ok && r >= 3.0;
            list += " " + t.name.toStdString() + " " + toHex(l.accent()).toStdString() + " (" + juce::String(r, 1).toStdString() + ")";
        }
        check(ok, "every preset's accent reads on the sheet:" + list);
    }

    std::printf("ui_render_test: %s (%d failure%s)\n", fails ? "FAILED" : "passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
