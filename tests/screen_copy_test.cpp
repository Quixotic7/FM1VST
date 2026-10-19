// SPDX-License-Identifier: GPL-3.0-only
// Copy the screen (plugin/ScreenCapture.h), headless:
//   (c) RGB565 big-endian -> 8-bit RGB pixel-exact on a known frame (white, red, green, blue, the lowest levels, every
//       value of a hashed pattern), and the x2 / x4 nearest scaling: each pixel an exact n x n block;
//   (b) ChoralRoot's boot frame: the panel's shown copy is the device's framebuffer, and the copy's pixels are exactly
//       the panel's lcdImage() (what the editor draws at a whole multiple); Freeze keeps the shown frame while the
//       firmware runs on, the thaw takes the latest;
//   (r) the raw frame: 115,200 bytes, RGB565 little-endian, row-major;
//   (p) PNG: our encoder's file decoded by JUCE (libpng) and by ImageIO equals the pixels; its chunks are IHDR, sRGB,
//       IDAT, IEND (no gAMA / cHRM / iCCP); Save as PNG / .bin files; the default file name;
//   (k) the clipboard writer on a private named pasteboard (pasteboardWithUniqueName; the general pasteboard is never
//       written, its change count checked): public.png and public.tiff read back at 240 / 480 / 960 with the same
//       pixels (the TIFF sRGB); the raw copy; the editor's Cmd+Shift+C and its toast.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "built_cores.h"
#include "Clipboard.h"
#include "Editor.h"
#include "PanelComponent.h"
#include "Processor.h"
#include "ScreenCapture.h"

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

using Frame = std::array<uint16_t, fm1screen::kPixels>;
static uint16_t be(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }   // (the frame holds the bytes as sent)

// the expected colour of a native RGB565 value, written apart from argbOf: r5 * 33 / 4, g6 * 65 / 16 (the bits replicated)
static void expect(uint16_t v, uint8_t *o)
{
    o[0] = (uint8_t)((((v >> 11) & 31u) * 33u) >> 2);
    o[1] = (uint8_t)((((v >> 5) & 63u) * 65u) >> 4);
    o[2] = (uint8_t)(((v & 31u) * 33u) >> 2);
}

// every pixel of rgb (240n x 240n) equals the expected colour of frame's pixel (x / n, y / n); the count that do not
static int mismatches(const Frame &frameBe, const std::vector<uint8_t> &rgb, int n)
{
    const int w = fm1screen::kW * n;
    if ((int)rgb.size() != w * w * 3)
        return -1;
    int bad = 0;
    for (int y = 0; y < w; y++)
        for (int x = 0; x < w; x++) {
            uint8_t e[3];
            expect(be(frameBe[(size_t)(y / n) * fm1screen::kW + (size_t)(x / n)]), e);
            const uint8_t *g = rgb.data() + ((size_t)y * (size_t)w + (size_t)x) * 3;
            bad += g[0] != e[0] || g[1] != e[1] || g[2] != e[2];
        }
    return bad;
}

static std::vector<std::string> pngChunks(const std::vector<uint8_t> &png)
{
    std::vector<std::string> c;
    for (size_t at = 8; at + 8 <= png.size();) {
        const uint32_t n = (uint32_t)png[at] << 24 | (uint32_t)png[at + 1] << 16 | (uint32_t)png[at + 2] << 8 | png[at + 3];
        c.emplace_back((const char *)png.data() + at + 4, 4);
        at += 12 + n;
    }
    return c;
}

static std::vector<uint8_t> juceDecode(const std::vector<uint8_t> &png, int &w, int &h)
{
    juce::PNGImageFormat f;
    juce::MemoryInputStream in(png.data(), png.size(), false);
    const juce::Image img = f.decodeImage(in);
    w = img.getWidth(), h = img.getHeight();
    std::vector<uint8_t> rgb;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const juce::Colour c = img.getPixelAt(x, y);
            rgb.push_back(c.getRed()), rgb.push_back(c.getGreen()), rgb.push_back(c.getBlue());
        }
    return rgb;
}

static bool has(const std::vector<std::string> &v, const std::string &s) { return std::find(v.begin(), v.end(), s) != v.end(); }

int main()
{
    require_cores("screen_copy_test", {"choralroot"});
    const juce::File scratch(FM1_SCRATCH_DIR);
    scratch.deleteRecursively();
    scratch.createDirectory();
    setenv("FM1EMU_HOME", scratch.getFullPathName().toRawUTF8(), 1);
    setenv("FM1VST_HUB_CATALOG", ("file://" + scratch.getFullPathName() + "/no-hub/catalog.json").toRawUTF8(), 1);
    setenv("FM1EMU_EDITOR_FAKE", "1", 1);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf("screen_copy_test: FM1EMU_HOME=%s\n", scratch.getFullPathName().toRawUTF8());

    // ---- (c) a known frame
    Frame known{};
    for (int i = 0; i < fm1screen::kPixels; i++)
        known[(size_t)i] = be((uint16_t)((uint32_t)i * 2654435761u >> 16));
    const uint16_t fixed[] = {0xFFFF, 0xF800, 0x07E0, 0x001F, 0x0841, 0x0000, 0x8410, 0x2104};
    for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
        known[i] = be(fixed[i]);
    {
        const std::vector<uint8_t> rgb = fm1screen::toRgb(known.data(), 1);
        const uint8_t want[][3] = {{255, 255, 255}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {8, 8, 8}, {0, 0, 0}, {132, 130, 132}, {33, 32, 33}};
        bool fixedOk = true;
        for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
            fixedOk = fixedOk && std::memcmp(rgb.data() + i * 3, want[i], 3) == 0;
        check(fixedOk, "white, red, green, blue, the lowest levels (8, 8, 8), greys: the colours the panel draws");
        for (int n : {1, 2, 4}) {
            const int bad = mismatches(known, fm1screen::toRgb(known.data(), n), n);
            check(bad == 0, "known frame x" + std::to_string(n) + ": " + std::to_string(240 * n) + " px, every pixel exact (off: " +
                                std::to_string(bad) + ")");
        }
        check(fm1screen::toRgb(known.data(), 3).size() == (size_t)fm1screen::kPixels * 3, "a scale other than 2 or 4 copies at 1:1");
        // (the panel's own conversion is argbOf)
        bool same = true;
        for (int v = 0; v < 65536; v++) {
            uint8_t e[3];
            expect((uint16_t)v, e);
            const uint32_t c = fm1screen::argbOf(be((uint16_t)v));
            same = same && (uint8_t)(c >> 16) == e[0] && (uint8_t)(c >> 8) == e[1] && (uint8_t)c == e[2] && (c >> 24) == 0xFF;
        }
        check(same, "argbOf: all 65,536 RGB565 values as expected, opaque");
    }

    // ---- (r) raw
    {
        const std::vector<uint8_t> raw = fm1screen::rawLE(known.data());
        bool ok = raw.size() == 115200;
        for (int i = 0; ok && i < fm1screen::kPixels; i++) {
            const uint16_t v = be(known[(size_t)i]);
            ok = raw[(size_t)i * 2] == (uint8_t)v && raw[(size_t)i * 2 + 1] == (uint8_t)(v >> 8);
        }
        check(ok && raw[2] == 0x00 && raw[3] == 0xF8, "raw: 115,200 bytes, RGB565 little-endian, row-major (red = 00 F8)");
    }

    // ---- (b) ChoralRoot's boot frame, as the panel shows it
    FM1Processor p;
    Host h(p, 44100.0, 256);
    h.run_ms(1500);
    Frame boot{};
    {
        PanelComponent panel(p);
        panel.refresh();
        boot = panel.shownLcd();
        const uint16_t *fb = p.deviceForTest()->lcd();
        check(std::memcmp(boot.data(), fb, sizeof boot) == 0, "the panel's shown frame is the device's framebuffer");
        int colours = 0;
        {
            std::set<uint16_t> seen(boot.begin(), boot.end());
            colours = (int)seen.size();
        }
        check(colours > 2, "the boot frame is not blank (" + std::to_string(colours) + " colours)");
        const juce::Image &img = panel.lcdImage();
        for (int n : {1, 2, 4}) {
            const std::vector<uint8_t> rgb = fm1screen::toRgb(boot.data(), n);
            const int w = 240 * n;
            int bad = 0;
            for (int y = 0; y < w; y++)
                for (int x = 0; x < w; x++) {
                    const juce::Colour c = img.getPixelAt(x / n, y / n);
                    const uint8_t *g = rgb.data() + ((size_t)y * (size_t)w + (size_t)x) * 3;
                    bad += g[0] != c.getRed() || g[1] != c.getGreen() || g[2] != c.getBlue();
                }
            check(bad == 0 && mismatches(boot, rgb, n) == 0,
                  "boot frame x" + std::to_string(n) + ": exactly the panel's drawn LCD (" + std::to_string(bad) + " off)");
        }
        // Freeze: the shown frame stays while the firmware draws on; the thaw takes the latest
        panel.setFrozen(true);
        p.panelButton(EMU_B_GLO, true);
        h.run_ms(300);
        p.panelButton(EMU_B_GLO, false);
        h.run_ms(600);
        panel.refresh();
        const bool moved = std::memcmp(p.deviceForTest()->lcd(), boot.data(), sizeof boot) != 0;
        check(panel.frozen() && panel.shownLcd() == boot, std::string("frozen: the shown frame is kept (the firmware's screen ") +
                                                              (moved ? "moved" : "did not move") + ")");
        panel.setFrozen(false);
        check(!panel.frozen() && std::memcmp(panel.shownLcd().data(), p.deviceForTest()->lcd(), sizeof boot) == 0,
              "thawed: the latest frame at once");
        p.panelButton(EMU_B_GLO, true);   // (GLO again: back to where it was)
        h.run_ms(300);
        p.panelButton(EMU_B_GLO, false);
        h.run_ms(600);
    }

    // ---- (p) PNG
    for (int n : {1, 4}) {
        const std::vector<uint8_t> rgb = fm1screen::toRgb(boot.data(), n);
        const std::vector<uint8_t> png = fm1screen::encodePng(rgb.data(), 240 * n, 240 * n);
        const std::vector<std::string> ch = pngChunks(png);
        check(ch == std::vector<std::string>{"IHDR", "sRGB", "IDAT", "IEND"} && png[24] == 8 && png[25] == 2,
              "PNG x" + std::to_string(n) + ": 8-bit RGB; IHDR sRGB IDAT IEND, nothing that moves a colour (" + std::to_string(png.size()) +
                  " bytes)");
        int w = 0, hh = 0;
        const std::vector<uint8_t> back = juceDecode(png, w, hh);
        check(w == 240 * n && hh == 240 * n && back == rgb, "PNG x" + std::to_string(n) + " decoded by libpng (JUCE): the same pixels");
        std::vector<uint8_t> io;
        std::string cs;
        const bool dec = fm1clip::decode(png, w, hh, io, cs);
        check(dec && w == 240 * n && io == rgb, "PNG x" + std::to_string(n) + " decoded by ImageIO: the same pixels (" + cs + ")");
    }
    {
        const juce::File f = scratch.getChildFile("shot.png"), b = scratch.getChildFile("shot.bin");
        juce::MemoryBlock pngFile, binFile;
        const bool ok = fm1screen::savePng(f, boot.data(), 2) && fm1screen::saveRaw(b, boot.data()) && f.loadFileAsData(pngFile) &&
                        b.loadFileAsData(binFile);
        const std::vector<uint8_t> rgb2 = fm1screen::toRgb(boot.data(), 2);
        const std::vector<uint8_t> want = fm1screen::encodePng(rgb2.data(), 480, 480);
        const std::vector<uint8_t> raw = fm1screen::rawLE(boot.data());
        check(ok && pngFile.getSize() == want.size() && std::memcmp(pngFile.getData(), want.data(), want.size()) == 0 &&
                  binFile.getSize() == raw.size() && std::memcmp(binFile.getData(), raw.data(), raw.size()) == 0,
              "Save as PNG (x2) and .bin write those bytes");
        const juce::String name = fm1screen::defaultFileName("choralroot", "0.25.1", juce::Time(2026, 9, 19, 14, 3, 5, 0, true), ".png");
        check(name == "choralroot-0.25.1-20261019-140305.png", "the default name: " + name.toStdString());
        check(fm1screen::defaultFileName("felucca", "1.6.1 + 0001", juce::Time(2026, 0, 2, 3, 4, 5, 0, true), ".bin") ==
                  "felucca-1.6.1_+_0001-20260102-030405.bin",
              "  ... file-name-safe");
    }

    // ---- (k) the clipboard, on a private pasteboard
    const long generalBefore = fm1clip::changeCount("");
    const std::string pb = fm1clip::makeUniquePasteboard();
    check(!pb.empty(), "a private pasteboard: " + pb);
    fm1clip::setPasteboardName(pb);
    for (int n : {1, 2, 4}) {
        const long before = fm1clip::changeCount(pb);
        const bool ok = fm1screen::copyImage(boot.data(), n);
        const std::vector<std::string> t = fm1clip::types(pb);
        check(ok && fm1clip::changeCount(pb) != before && has(t, "public.png") && has(t, "public.tiff"),
              "copy x" + std::to_string(n) + ": public.png and public.tiff on the pasteboard");
        const std::vector<uint8_t> rgb = fm1screen::toRgb(boot.data(), n);
        for (const char *type : {"public.png", "public.tiff"}) {
            const std::vector<uint8_t> data = fm1clip::read(pb, type);
            int w = 0, hh = 0;
            std::vector<uint8_t> px;
            std::string cs;
            const bool dec = fm1clip::decode(data, w, hh, px, cs);
            check(dec && w == 240 * n && hh == 240 * n && px == rgb && cs.find("SRGB") != std::string::npos,
                  "  " + std::string(type) + " (" + std::to_string(data.size()) + " bytes): " + std::to_string(w) + " x " +
                      std::to_string(hh) + ", every pixel exact, " + (cs.empty() ? std::string("no colour space") : cs));
        }
    }
    {
        const bool ok = fm1screen::copyRaw(boot.data());
        const std::vector<uint8_t> raw = fm1screen::rawLE(boot.data());
        check(ok && fm1clip::read(pb, "com.quixotic7.fm1vst.rgb565le") == raw && fm1clip::read(pb, "public.data") == raw &&
                  !has(fm1clip::types(pb), "public.png"),
              "copy raw: the 115,200 bytes as com.quixotic7.fm1vst.rgb565le and public.data (the image replaced)");
    }
    {   // the editor: Cmd+Shift+C, the screen menu's commands, the toast
        FM1Editor ed(p);
        ed.panel().refresh();
        const juce::KeyPress cmdShiftC('c', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        const bool took = ed.keyPressed(cmdShiftC);
        int w = 0, hh = 0;
        std::vector<uint8_t> px;
        std::string cs;
        fm1clip::decode(fm1clip::read(pb, "public.png"), w, hh, px, cs);
        check(took && w == 240 && px == fm1screen::toRgb(ed.panel().shownLcd().data(), 1) &&
                  ed.toast().text() == juce::String::fromUTF8("Screen copied (240\xc3\x97" "240)") && ed.toast().isVisible(),
              "the editor: Cmd+Shift+C copies 240 x 240; the toast says \"" + ed.toast().text().toStdString() + "\"");
        ed.setCopyScale(4);
        ed.keyPressed(cmdShiftC);
        fm1clip::decode(fm1clip::read(pb, "public.tiff"), w, hh, px, cs);
        check(w == 960, "  ... at the sheet's scale (x4: " + std::to_string(w) + ")");
        ed.screenCommand(FM1Editor::kCopy2);
        fm1clip::decode(fm1clip::read(pb, "public.png"), w, hh, px, cs);
        check(w == 480, "the menu's Copy screen x2: 480");
        ed.screenCommand(FM1Editor::kFreeze);
        check(ed.screenFrozen(), "the menu's Freeze screen freezes");
        ed.screenCommand(FM1Editor::kFreeze);
        check(!ed.screenFrozen(), "  ... and thaws");
        check(!ed.keyPressed(juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0)), "Cmd+C alone is left to the host");
    }
    fm1clip::setPasteboardName("");
    fm1clip::releasePasteboard(pb);
    check(fm1clip::changeCount("") == generalBefore, "the general pasteboard was never written");

    std::printf("screen_copy_test: %s (%d failure%s)\n", fails ? "FAILED" : "passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
