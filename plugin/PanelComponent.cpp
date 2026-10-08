// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 panel (see PanelComponent.h): tools/emu/emu.c's drawing, layout and input, ported.
#include "PanelComponent.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// ================================================================ geometry ===
// ChoralRootFM1Designer/index.html, drawing units (904 x 566): emu.c's geometry block, verbatim
struct R {
    float x, y, w, h;
};
const float WHITE_X[16] = {71, 119, 167, 215, 264, 312, 360, 408, 456, 504, 553, 601, 649, 697, 745, 794};
const float BLACK_X[11] = {95, 143, 191, 288, 336, 432, 480, 529, 625, 673, 769};
constexpr float KEY_W = 42.f, KEY_H = 76.f;
const float BTN_X[6] = {500, 554, 608, 661, 715, 768}, BTN_Y[2] = {177, 231};
constexpr float BTN_S = 36.f;
// knobs in EMU_E_* order: SELECT ALGORITHM PRESETS KNOB1..4 MASTER
const float ENC_CX[EMU_NE] = {186.5f, 186.5f, 95, 508, 604, 701, 797, 95};
const float ENC_CY[EMU_NE] = {101, 191, 191, 101, 101, 101, 101, 101};
const float ENC_LY[EMU_NE] = {66, 156, 156, 66, 66, 66, 66, 66};
constexpr float ENC_R = 21.f, ENC_CAP = 12.f;
const R BODY = {22, 22, 860, 522}, SCREEN = {247, 59, 201, 201}, SCREEN_A = {272, 84, 150, 150};
const R VIEW = {14, 14, 876, 538};
const char *const ENC_NAME[EMU_NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB1", "KNOB2", "KNOB3", "KNOB4", "MASTER"};
constexpr float kPi = 3.14159265358979f;

int keyBlack(int k) { return (0x54A >> ((k + 5) % 12)) & 1; }   // seq.c key_black

struct Geometry {
    R key[EMU_NKEY], btn[EMU_NB];
    Geometry()
    {
        int w = 0, b = 0;
        for (int k = 0; k < EMU_NKEY; k++) {
            const int bl = keyBlack(k);
            key[k] = R{bl ? BLACK_X[b] : WHITE_X[w], bl ? 326.f : 408.f, KEY_W, KEY_H};
            if (bl)
                b++;
            else
                w++;
        }
        for (int i = 0; i < 12; i++)
            btn[i] = R{BTN_X[i % 6], BTN_Y[i / 6], BTN_S, BTN_S};
        btn[EMU_B_OCTDN] = R{87, 250, 42, 22};
        btn[EMU_B_OCTUP] = R{153, 250, 42, 22};
    }
};
const Geometry &geo()
{
    static const Geometry g;
    return g;
}

std::string noteName(int key)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int m = 53 + key;
    return std::string(N[m % 12]) + std::to_string(m / 12 - 1);
}

// ================================================================== raster ===
// emu.c's canvas: px = (unit - o) * k, onto a software ARGB image (alpha 255 everywhere, so premultiplied = straight)
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline uint32_t rgbOf(juce::Colour c) { return c.getARGB() & 0xFFFFFFu; }

struct Canvas {
    juce::Image::BitmapData bd;
    int w, h;
    float k, ox, oy;
    Canvas(juce::Image &img, float scale) : bd(img, juce::Image::BitmapData::readWrite), w(img.getWidth()), h(img.getHeight()),
                                            k(scale), ox(VIEW.x), oy(VIEW.y) {}
    uint32_t *row(int y) { return (uint32_t *)(void *)(bd.data + (size_t)y * (size_t)bd.lineStride); }
    void blend(int x, int y, uint32_t rgb, float a)
    {
        if (a <= 0.f || x < 0 || y < 0 || x >= w || y >= h)
            return;
        if (a > 1.f)
            a = 1.f;
        uint32_t *d = row(y) + x;
        const uint32_t s = *d;
        const uint32_t r = (uint32_t)(((s >> 16) & 255u) + (((rgb >> 16) & 255u) - (float)((s >> 16) & 255u)) * a);
        const uint32_t g = (uint32_t)(((s >> 8) & 255u) + (((rgb >> 8) & 255u) - (float)((s >> 8) & 255u)) * a);
        const uint32_t b = (uint32_t)((s & 255u) + ((rgb & 255u) - (float)(s & 255u)) * a);
        *d = 0xFF000000u | r << 16 | g << 8 | b;
    }
    void fill(uint32_t rgb)
    {
        for (int y = 0; y < h; y++) {
            uint32_t *p = row(y);
            for (int x = 0; x < w; x++)
                p[x] = 0xFF000000u | rgb;
        }
    }
    // signed distance (pixels) of a rounded box; lw 0 fill, else a stroke lw px wide inside the edge
    void rrectPx(float x0, float y0, float bw, float bh, float r, uint32_t rgb, float a, float lw)
    {
        const int xa = (int)std::floor(x0) - 1, ya = (int)std::floor(y0) - 1;
        const int xb = (int)std::ceil(x0 + bw) + 1, yb = (int)std::ceil(y0 + bh) + 1;
        const float cx = x0 + bw / 2, cy = y0 + bh / 2, hx = bw / 2, hy = bh / 2;
        r = clampf(r, 0, std::fmin(hx, hy));
        for (int y = std::max(ya, 0); y <= std::min(yb, h - 1); y++)
            for (int x = std::max(xa, 0); x <= std::min(xb, w - 1); x++) {
                const float qx = std::fabs((float)x + .5f - cx) - (hx - r), qy = std::fabs((float)y + .5f - cy) - (hy - r);
                const float ex = std::fmax(qx, 0.f), ey = std::fmax(qy, 0.f);
                const float d = std::sqrt(ex * ex + ey * ey) + std::fmin(std::fmax(qx, qy), 0.f) - r;
                const float cov = lw > 0 ? clampf(.5f - (std::fabs(d + lw / 2) - lw / 2), 0, 1) : clampf(.5f - d, 0, 1);
                if (cov > 0)
                    blend(x, y, rgb, cov * a);
            }
    }
    void rrect(R r, float rad, juce::Colour c, float a = 1)
    {
        rrectPx((r.x - ox) * k, (r.y - oy) * k, r.w * k, r.h * k, rad * k, rgbOf(c), a, 0);
    }
    void rrectLine(R r, float rad, juce::Colour c, float a, float lw)
    {
        rrectPx((r.x - ox) * k, (r.y - oy) * k, r.w * k, r.h * k, rad * k, rgbOf(c), a, std::fmax(lw * k, 1.f));
    }
    void circle(float cx, float cy, float r, juce::Colour c, float a = 1) { rrect(R{cx - r, cy - r, 2 * r, 2 * r}, r, c, a); }
    // a circle line (centred on r), dashed: dash units on, dash * 1.1 off
    void ring(float cx, float cy, float r, juce::Colour c, float a, float lw, float dash)
    {
        const uint32_t rgb = rgbOf(c);
        const float px = (cx - ox) * k, py = (cy - oy) * k, Rr = r * k, W = std::fmax(lw * k, 1.f);
        for (int y = (int)(py - Rr - W) - 1; y <= (int)(py + Rr + W) + 1; y++)
            for (int x = (int)(px - Rr - W) - 1; x <= (int)(px + Rr + W) + 1; x++) {
                const float dx = (float)x + .5f - px, dy = (float)y + .5f - py;
                const float d = std::fabs(std::sqrt(dx * dx + dy * dy) - Rr) - W / 2;
                float cov = clampf(.5f - d, 0, 1);
                if (cov > 0 && dash > 0) {
                    const float s = (std::atan2(dy, dx) + kPi) * r;   // arc length in units
                    if (std::fmod(s, dash * 2.1f) > dash)
                        cov = 0;
                }
                blend(x, y, rgb, cov * a);
            }
    }
    // a line with round caps
    void segment(float x1, float y1, float x2, float y2, float lw, juce::Colour c, float a = 1)
    {
        const uint32_t rgb = rgbOf(c);
        const float ax = (x1 - ox) * k, ay = (y1 - oy) * k, bx = (x2 - ox) * k, by = (y2 - oy) * k;
        const float W = lw * k / 2, vx = bx - ax, vy = by - ay, L2 = vx * vx + vy * vy;
        for (int y = (int)(std::fmin(ay, by) - W) - 1; y <= (int)(std::fmax(ay, by) + W) + 1; y++)
            for (int x = (int)(std::fmin(ax, bx) - W) - 1; x <= (int)(std::fmax(ax, bx) + W) + 1; x++) {
                const float qx = (float)x + .5f - ax, qy = (float)y + .5f - ay, t = L2 > 0 ? clampf((qx * vx + qy * vy) / L2, 0, 1) : 0;
                const float dx = qx - t * vx, dy = qy - t * vy;
                blend(x, y, rgb, clampf(.5f - (std::sqrt(dx * dx + dy * dy) - W), 0, 1) * a);
            }
    }
    void text(float cx, float y, float size, const char *s, juce::Colour c, float a = 1);
};

// emu.c's 5x7 font, ASCII 32..95, columns, bit 0 = top
const uint8_t FONT[64][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},{0x3E,0x41,0x41,0x51,0x32},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
};
int fontPx(int ch, int col, int row)
{
    if (ch >= 'a' && ch <= 'z')
        ch -= 32;
    if (ch < 32 || ch > 95 || col < 0 || col > 4 || row < 0 || row > 6)
        return 0;
    return (FONT[ch - 32][col] >> row) & 1;
}
float textWidth(const char *s, float size) { return ((float)std::strlen(s) * 6 - 1) * size / 7.f; }

// text centred on cx, cap height = size units, top at y (units); 3x3 supersampled
void Canvas::text(float cx, float y, float size, const char *s, juce::Colour c, float a)
{
    const uint32_t rgb = rgbOf(c);
    const int n = (int)std::strlen(s);
    const float f = size / 7.f, tw = ((float)n * 6 - 1) * f, x0 = cx - tw / 2;
    const int xa = (int)((x0 - ox) * k) - 1, xb = (int)((x0 + tw - ox) * k) + 1;
    const int ya = (int)((y - oy) * k) - 1, yb = (int)((y + size - oy) * k) + 1;
    for (int yy = ya; yy <= yb; yy++)
        for (int x = xa; x <= xb; x++) {
            int hitN = 0;
            for (int sy = 0; sy < 3; sy++)
                for (int sx = 0; sx < 3; sx++) {
                    const float ux = ((float)x + ((float)sx + .5f) / 3.f) / k + ox - x0, uy = ((float)yy + ((float)sy + .5f) / 3.f) / k + oy - y;
                    if (ux < 0 || uy < 0)
                        continue;
                    const int gx = (int)(ux / f), gy = (int)(uy / f), ci = gx / 6;
                    if (ci < n)
                        hitN += fontPx((unsigned char)s[ci], gx % 6, gy);
                }
            if (hitN)
                blend(x, yy, rgb, a * (float)hitN / 9.f);
        }
}

// a name on a cap: at most `room` units wide (shrunk to fit)
float fitSize(const char *s, float size, float room)
{
    const float w = textWidth(s, size);
    return w > room ? size * room / w : size;
}

juce::Image softwareImage(int w, int h)
{
    return juce::Image(juce::Image::ARGB, std::max(1, w), std::max(1, h), false, juce::SoftwareImageType());
}

// ---- the drawing of one control (emu.c draw_panel), with the palette
void drawKey(Canvas &c, const Palette &p, int k, bool held, int lv)
{
    const R r = geo().key[k];
    const float cx = r.x + KEY_W / 2;
    c.rrect(r, 21, held ? p.pressed : p.cap);
    c.rrectLine(r, 21, held ? p.hint : p.capOutline, 1, held ? 1.5f : 1);
    if (lv == 2)
        c.rrect(R{cx - 9, r.y + 9, 18, 40}, 9, p.white, .16f);
    c.rrect(R{cx - 4, r.y + 14, 8, 30}, 4, p.ledOff);
    if (lv)
        c.rrect(R{cx - 4, r.y + 14, 8, 30}, 4, p.white, lv == 2 ? 1.f : .38f);
    c.text(cx, r.y + 50, 6.5f, noteName(k).c_str(), p.noteText);
    if (const char *h = PanelComponent::keymapHint(PanelComponent::KM_KEY, k))
        c.text(cx, r.y + 61, 6.5f, h, p.hint, .9f);
}

void drawButton(Canvas &c, const Palette &p, int i, const juce::String &name, bool held, int lv, int green)
{
    const R r = geo().btn[i];
    const bool oct = i >= EMU_B_OCTDN;
    const juce::Colour col = i == EMU_B_REC ? p.red : i == EMU_B_PLAY ? p.orange : p.white;
    const float rad = oct ? 6 : 5;
    c.rrect(r, rad, p.cap);
    if (lv)
        c.rrect(r, rad, col, lv == 2 ? 1.f : .38f);
    c.rrectLine(r, rad, held ? p.hint : p.capOutline, 1, held ? 2 : 1);
    const juce::String n = name.toUpperCase();
    const float size = fitSize(n.toRawUTF8(), oct ? 6.5f : 7.5f, r.w - 5);
    c.text(r.x + r.w / 2, r.y + (oct ? 7.5f : 11) + ((oct ? 6.5f : 7.5f) - size) / 2, size, n.toRawUTF8(),
           lv == 2 ? p.litText : lv == 1 ? p.capTextDim : p.capText);
    const char *h = PanelComponent::keymapHint(PanelComponent::KM_BTN, i);
    if (h && !oct)
        c.text(r.x + r.w / 2, r.y + 25, 5, h, lv == 2 ? p.hintLit : lv == 1 ? p.hintDim : p.hint, .9f);
    else if (h)
        c.text(r.x + r.w / 2, r.y + r.h + 4, 5, h, p.hintBed, .9f);
    if (i == EMU_B_PLAY)                          // the green LED
        c.circle(r.x + 6, r.y + r.h - 6, 3.2f, green ? p.green : p.ledOff);
}

void drawKnob(Canvas &c, const Palette &p, int i, float a, bool sel)
{
    const float cx = ENC_CX[i], cy = ENC_CY[i];
    c.circle(cx, cy, ENC_R, p.knobBody);
    c.ring(cx, cy, ENC_R - 3, p.knobRing, 1, 2.5f, 2);
    c.circle(cx, cy, ENC_CAP, p.knobCap);
    c.segment(cx + 4 * std::sin(a), cy - 4 * std::cos(a), cx + (ENC_CAP - 1) * std::sin(a), cy - (ENC_CAP - 1) * std::cos(a), 2,
              i == EMU_E_MASTER ? p.masterPointer : p.pointer);
    if (sel)
        c.ring(cx, cy, ENC_R + 3, p.hint, 1, 1.5f, 0);
}

float masterAngle(int master) { return (float)((master / 1023.0 - .5) * 1.5 * kPi); }
}   // namespace

// ================================================================ keymap ===
// keymap.c's KEYMAP, in its order, by key (JUCE key codes); left out: PrintScreen / F13 (screenshot), Insert
// (recording), F12 (dump). Shift is read from the modifiers (emu.c KM_SHIFT).
const std::vector<PanelComponent::KeymapEntry> &PanelComponent::keymap()
{
    using K = juce::KeyPress;
    static const std::vector<KeymapEntry> m = {
        // the chord block (first: the panel hint of keys 7 and 8 stays "5" / "F4")
        {K::F1Key, KM_KEY, 1, "F1"}, {K::F2Key, KM_KEY, 3, "F2"}, {K::F3Key, KM_KEY, 5, "F3"}, {K::F4Key, KM_KEY, 8, "F4"},
        {'2', KM_KEY, 0, "2"}, {'3', KM_KEY, 2, "3"}, {'4', KM_KEY, 4, "4"}, {'5', KM_KEY, 7, "5"},
        {K::tabKey, KM_KEY, 6, "TAB"},
        // note keys, Ableton Live's layout: white C4 = key 7 upward
        {'A', KM_KEY, 7, "A"}, {'S', KM_KEY, 9, "S"}, {'D', KM_KEY, 11, "D"}, {'F', KM_KEY, 12, "F"},
        {'G', KM_KEY, 14, "G"}, {'H', KM_KEY, 16, "H"}, {'J', KM_KEY, 18, "J"}, {'K', KM_KEY, 19, "K"},
        {'L', KM_KEY, 21, "L"}, {';', KM_KEY, 23, ";"}, {'\'', KM_KEY, 24, "'"}, {']', KM_KEY, 26, "]"},
        // note keys, black
        {'W', KM_KEY, 8, "W"}, {'E', KM_KEY, 10, "E"}, {'T', KM_KEY, 13, "T"}, {'Y', KM_KEY, 15, "Y"},
        {'U', KM_KEY, 17, "U"}, {'O', KM_KEY, 20, "O"}, {'P', KM_KEY, 22, "P"},
        // octave: Z / X (first: the panel hint), Esc / Return too; End = both = panic
        {'Z', KM_BTN, EMU_B_OCTDN, "Z"}, {'X', KM_BTN, EMU_B_OCTUP, "X"},
        {K::escapeKey, KM_BTN, EMU_B_OCTDN, "ESC"}, {K::returnKey, KM_BTN, EMU_B_OCTUP, "RETURN"},
        {K::endKey, KM_OCTBOTH, 0, "END"},
        // buttons, the FM-1's two rows
        {K::F5Key, KM_BTN, EMU_B_FX, "F5"}, {K::F6Key, KM_BTN, EMU_B_SEL, "F6"}, {K::F7Key, KM_BTN, EMU_B_ENV, "F7"},
        {K::F8Key, KM_BTN, EMU_B_LFO, "F8"}, {K::F9Key, KM_BTN, EMU_B_EDIT, "F9"}, {K::F10Key, KM_BTN, EMU_B_GLO, "F10"},
        {'7', KM_BTN, EMU_B_HOME, "7"}, {'8', KM_BTN, EMU_B_SAVE, "8"}, {'9', KM_BTN, EMU_B_ARP, "9"},
        {'0', KM_BTN, EMU_B_SEQ, "0"}, {'-', KM_BTN, EMU_B_PLAY, "-"}, {'=', KM_BTN, EMU_B_REC, "="},
        // knobs: Page Down / Up cycle SELECT KNOB1..KNOB4; MASTER, PRESETS, ALGORITHM: the mouse
        {K::pageDownKey, KM_CYCLE, +1, "PGDN"}, {K::pageUpKey, KM_CYCLE, -1, "PGUP"},
        {K::upKey, KM_TURN, +1, "UP"}, {K::downKey, KM_TURN, -1, "DOWN"},
        // tools
        {'`', KM_LCDVIEW, 0, "`"},
    };
    return m;
}

const char *PanelComponent::keymapHint(Kind kind, int idx)
{
    for (const KeymapEntry &e : keymap())
        if (e.kind == kind && e.idx == idx)
            return e.cap;
    return nullptr;
}

static int normKey(int code) { return code >= 'a' && code <= 'z' ? code - 32 : code; }

// ============================================================ component ===
PanelComponent::PanelComponent(FM1Processor &p) : proc_(p)
{
    for (int i = 0; i < EMU_NB; i++)
        names_.add(FM1Processor::kPanelLabels[i]);
    pal_ = derive(theme_);
    lcd240_ = softwareImage(EMU_LCD_W, EMU_LCD_H);
    lcd240_.clear(lcd240_.getBounds(), juce::Colours::black);
    setWantsKeyboardFocus(true);
    setOpaque(true);
    startTimerHz(60);
}

PanelComponent::~PanelComponent()
{
    stopTimer();
    proc_.panelReleaseAll();                     // (no stuck notes when the editor closes)
}

float PanelComponent::frameAspect(bool big) { return VIEW.w / (VIEW.h + (big ? VIEW.w : 0.f)); }

void PanelComponent::setTheme(const Theme &t)
{
    theme_ = t;
    pal_ = derive(t);
    fullRedraw_ = true;
    repaint();
}

void PanelComponent::setButtonNames(const juce::StringArray &n)
{
    if (n.size() != EMU_NB || n == names_)
        return;
    names_ = n;
    fullRedraw_ = true;
    repaint();
}

void PanelComponent::setBigLcd(bool on)
{
    if (on == bigLcd_)
        return;
    bigLcd_ = on;
    layoutDirty_ = true;
    repaint();
}

// ---------------------------------------------------------------- layout ---
// emu.c relayout: fit the frame (the big LCD, if shown, a square as wide as the panel; the panel) in the
// component's physical pixels, keeping the aspect ratio; snap to an integer scale of the panel's 240x240 screen when
// one is within 8 %
void PanelComponent::relayout(float s)
{
    Layout L;
    L.scale = s;
    L.outW = juce::roundToInt((float)getWidth() * s);
    L.outH = juce::roundToInt((float)getHeight() * s);
    const float fh = VIEW.h + (bigLcd_ ? VIEW.w : 0);
    float k = std::fmin((float)L.outW / VIEW.w, (float)L.outH / fh);
    {
        const int n = (int)(k * SCREEN_A.w / EMU_LCD_W);
        const float ks = (float)n * EMU_LCD_W / SCREEN_A.w;
        if (n >= 1 && ks >= 0.92f * k)
            k = ks;
    }
    if (k <= 0)
        k = 0.05f;
    L.k = k;
    L.pixW = std::max(16, (int)(VIEW.w * k));
    L.pixH = std::max(16, (int)(VIEW.h * k));
    L.lcdPx = bigLcd_ ? L.pixW : 0;
    L.frX = std::max(0, (L.outW - L.pixW) / 2);
    L.frY = std::max(0, (L.outH - L.pixH - L.lcdPx) / 2);
    {   // the panel's screen: nearest when its scale is an integer (then exactly n * 240 pixels)
        const float ss = SCREEN_A.w * k / EMU_LCD_W;
        const int x0 = (int)std::lround((SCREEN_A.x - VIEW.x) * k), y0 = (int)std::lround((SCREEN_A.y - VIEW.y) * k);
        int w = (int)std::lround((SCREEN_A.x + SCREEN_A.w - VIEW.x) * k) - x0;
        int h = (int)std::lround((SCREEN_A.y + SCREEN_A.h - VIEW.y) * k) - y0;
        if (std::fabs(ss - std::round(ss)) < 0.01f && std::round(ss) >= 1) {
            L.nSmall = (int)std::round(ss);
            w = h = L.nSmall * EMU_LCD_W;
        }
        L.lcdSmall = {L.frX + x0, L.frY + L.lcdPx + y0, w, h};
    }
    if (bigLcd_) {  // as wide as the panel; snapped down to a multiple of 240 when one is within 10 %
        int size = L.pixW;
        const int nb = L.pixW / EMU_LCD_W;
        if (nb >= 1 && (float)(nb * EMU_LCD_W) >= 0.9f * (float)L.pixW)
            size = nb * EMU_LCD_W;
        L.nBig = size % EMU_LCD_W == 0 ? size / EMU_LCD_W : 0;
        L.lcdBig = {L.frX + (L.pixW - size) / 2, L.frY + (L.lcdPx - size) / 2, size, size};
    }
    const bool resize = !panel_.isValid() || L.pixW != panel_.getWidth() || L.pixH != panel_.getHeight() || std::fabs(L.k - lay_.k) > 1e-6f;
    lay_ = L;
    layoutDirty_ = false;
    lcdImagesDirty_ = true;
    if (resize || fullRedraw_) {
        base_ = softwareImage(L.pixW, L.pixH);
        drawBase();
        panel_ = base_.createCopy();
        updateControls(true);
        fullRedraw_ = false;
    }
}

// emu.c draw_base: everything that never changes (for this size and palette)
void PanelComponent::drawBase()
{
    Canvas c(base_, lay_.k);
    const Palette &p = pal_;
    c.fill(rgbOf(p.backdrop));
    c.rrect(BODY, 38, p.plate);
    c.rrectLine(BODY, 38, p.outline, 1, 1.5f);
    c.rrectLine(R{BODY.x + 7, BODY.y + 7, BODY.w - 14, BODY.h - 14}, 32, p.edge, 1, 1);
    for (const R &b : {R{52, 306, 800, 198}, R{484, 162, 338, 124}, R{77, 243, 127, 36}}) {
        const float rad = b.w > 700 ? 20.f : b.w > 300 ? 8.f : 6.f;
        c.rrect(b, rad, p.bed);
        c.rrectLine(b, rad, p.bedEdge, 1, 1);
    }
    for (float dx : {262.f, 406.f, 599.f, 743.f})
        c.circle(dx, 404, 2, p.bedEdge);
    for (int i = 0; i < EMU_NE; i++) {
        c.text(ENC_CX[i], ENC_LY[i] - 6, 7, ENC_NAME[i], p.label);
        if (const char *h = keymapHint(KM_SELECT, i))
            c.text(ENC_CX[i], ENC_CY[i] + ENC_R + 6, 7, h, p.hint, .9f);
    }
    // the panel's own printed labels (FX SEL ENV ..), small on the bed over the top row and under the bottom row,
    // where the core names a button differently (ChoralRoot: KEY over SEL's cap, ...)
    for (int i = 0; i < 12; i++) {
        const R r = geo().btn[i];
        const char *lbl = FM1Processor::kPanelLabels[i];
        if (names_[i].equalsIgnoreCase(lbl))
            continue;
        c.text(r.x + r.w / 2, i < 6 ? r.y - 10.5f : r.y + r.h + 4.5f, 5.5f, lbl, p.bedLabel, .85f);
    }
    c.rrect(SCREEN, 10, p.screenBezel);
    c.rrectLine(SCREEN, 10, p.outline, 1, 1);
}

// copy a box of base_ back over panel_ (the control in it is then drawn again); its pixels in the component
void PanelComponent::restoreBox(float ux, float uy, float uw, float uh, juce::Rectangle<int> *pxOut)
{
    const float k = lay_.k;
    const int x0 = std::max(0, (int)std::floor((ux - VIEW.x) * k) - 2), y0 = std::max(0, (int)std::floor((uy - VIEW.y) * k) - 2);
    const int x1 = std::min(panel_.getWidth(), (int)std::ceil((ux + uw - VIEW.x) * k) + 2);
    const int y1 = std::min(panel_.getHeight(), (int)std::ceil((uy + uh - VIEW.y) * k) + 2);
    if (x1 <= x0 || y1 <= y0)
        return;
    const juce::Image::BitmapData src(base_, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst(panel_, juce::Image::BitmapData::readWrite);
    for (int y = y0; y < y1; y++)
        std::memcpy(dst.getPixelPointer(x0, y), src.getPixelPointer(x0, y), (size_t)(x1 - x0) * 4u);
    if (pxOut)
        *pxOut = {lay_.frX + x0, lay_.frY + lay_.lcdPx + y0, x1 - x0, y1 - y0};
}

bool PanelComponent::updateControls(bool force)
{
    if (!panel_.isValid())
        return false;
    std::vector<juce::Rectangle<int>> dirty;
    // first the boxes back from base_, then the drawing (one BitmapData per image at a time)
    struct Todo {
        int kind, i;
        Drawn d;
    };
    std::vector<Todo> todo;
    for (int k = 0; k < EMU_NKEY; k++) {
        Drawn d;
        d.a = keySrc_[(size_t)k] || ((view_.keys >> k) & 1u);
        d.b = view_.keyLed[(size_t)k];
        if (force || d != keyDrawn_[(size_t)k])
            todo.push_back({0, k, d});
    }
    for (int i = 0; i < EMU_NB; i++) {
        Drawn d;
        d.a = btnSrc_[(size_t)i] || ((view_.buttons >> i) & 1u);
        d.b = view_.btnLed[(size_t)i];
        d.c = i == EMU_B_PLAY ? view_.playGreen : 0;
        if (force || d != btnDrawn_[(size_t)i])
            todo.push_back({1, i, d});
    }
    for (int i = 0; i < EMU_NE; i++) {
        Drawn d;
        const float a = i == EMU_E_MASTER ? masterAngle(view_.master) : knobAngle_[(size_t)i];
        d.a = (int)std::lround(a * 1000.0f);
        d.b = selKnob_ == i;
        if (force || d != knobDrawn_[(size_t)i])
            todo.push_back({2, i, d});
    }
    if (todo.empty())
        return false;
    for (const Todo &t : todo) {
        juce::Rectangle<int> px;
        if (t.kind == 0) {
            const R r = geo().key[t.i];
            restoreBox(r.x - 1, r.y - 1, r.w + 2, r.h + 2, &px);
        } else if (t.kind == 1) {
            const R r = geo().btn[t.i];
            restoreBox(r.x - 2, r.y - 2, r.w + 4, r.h + (t.i >= EMU_B_OCTDN ? 11 : 4), &px);
        } else {
            const float e = ENC_R + 5;
            restoreBox(ENC_CX[t.i] - e, ENC_CY[t.i] - e, 2 * e, 2 * e, &px);
        }
        dirty.push_back(px);
    }
    {
        Canvas c(panel_, lay_.k);
        for (const Todo &t : todo) {
            if (t.kind == 0) {
                drawKey(c, pal_, t.i, t.d.a != 0, t.d.b);
                keyDrawn_[(size_t)t.i] = t.d;
            } else if (t.kind == 1) {
                drawButton(c, pal_, t.i, names_[t.i], t.d.a != 0, t.d.b, t.d.c);
                btnDrawn_[(size_t)t.i] = t.d;
            } else {
                const float a = t.i == EMU_E_MASTER ? masterAngle(view_.master) : knobAngle_[(size_t)t.i];
                drawKnob(c, pal_, t.i, a, t.d.b != 0);
                knobDrawn_[(size_t)t.i] = t.d;
            }
        }
    }
    if (!force)
        for (const auto &r : dirty)
            repaintPx(r);
    return true;
}

void PanelComponent::repaintPx(juce::Rectangle<int> px)
{
    const float s = lay_.scale > 0 ? lay_.scale : 1.0f;
    repaint(px.toFloat().expanded(1.0f).transformedBy(juce::AffineTransform::scale(1.0f / s)).getSmallestIntegerContainer());
}

// ------------------------------------------------------------------- LCD ---
static juce::Image nearestScaled(const juce::Image &src, int n)
{
    juce::Image out = softwareImage(src.getWidth() * n, src.getHeight() * n);
    const juce::Image::BitmapData s(src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData d(out, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < out.getHeight(); y++) {
        const uint32_t *sr = (const uint32_t *)(const void *)s.getLinePointer(y / n);
        uint32_t *dr = (uint32_t *)(void *)d.getLinePointer(y);
        for (int x = 0; x < out.getWidth(); x++)
            dr[x] = sr[x / n];
    }
    return out;
}

void PanelComponent::rebuildLcdImages()
{
    lcdSmall_ = lay_.nSmall > 0 ? nearestScaled(lcd240_, lay_.nSmall) : juce::Image();
    lcdBig_ = bigLcd_ && lay_.nBig > 0 ? nearestScaled(lcd240_, lay_.nBig) : juce::Image();
    lcdImagesDirty_ = false;
}

void PanelComponent::refresh()
{
    proc_.getPanelView(view_);
    if (view_.lcdSeq != lcdSeen_) {
        lcdSeen_ = view_.lcdSeq;
        // RGB565 big-endian (as sent) -> ARGB, the 5 / 6 bits replicated (emu_img.c)
        juce::Image::BitmapData d(lcd240_, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < EMU_LCD_H; y++) {
            uint32_t *row = (uint32_t *)(void *)d.getLinePointer(y);
            for (int x = 0; x < EMU_LCD_W; x++) {
                const uint16_t v = view_.lcd[(size_t)(y * EMU_LCD_W + x)];
                const uint32_t p = (uint32_t)((v >> 8) | ((v & 0xFFu) << 8));
                const uint32_t r = (p >> 11) & 31u, g = (p >> 5) & 63u, b = p & 31u;
                row[x] = 0xFF000000u | ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
            }
        }
        lcdImagesDirty_ = true;
        if (!layoutDirty_) {
            repaintPx(lay_.lcdSmall);
            if (bigLcd_)
                repaintPx(lay_.lcdBig);
        }
    }
    if (!layoutDirty_ && !fullRedraw_)
        updateControls(false);
}

// ----------------------------------------------------------------- paint ---
void PanelComponent::paint(juce::Graphics &g)
{
    float s = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (!(s > 0))
        s = 1;
    if (layoutDirty_ || fullRedraw_ || std::fabs(s - lay_.scale) > 1e-4f || juce::roundToInt((float)getWidth() * s) != lay_.outW ||
        juce::roundToInt((float)getHeight() * s) != lay_.outH)
        relayout(s);
    if (lcdImagesDirty_)
        rebuildLcdImages();
    const juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::scale(1.0f / s));   // physical pixels from here on
    g.setColour(pal_.backdrop);
    g.fillRect(0, 0, lay_.outW, lay_.outH);
    auto lcd = [&](const juce::Image &pre, juce::Rectangle<int> r) {
        if (pre.isValid()) {
            g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
            g.drawImageAt(pre, r.getX(), r.getY());
        } else {
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImage(lcd240_, r.toFloat());
        }
    };
    if (bigLcd_)
        lcd(lcdBig_, lay_.lcdBig);
    g.drawImageAt(panel_, lay_.frX, lay_.frY + lay_.lcdPx);
    lcd(lcdSmall_, lay_.lcdSmall);
}

juce::Point<int> PanelComponent::keyLedPx(int key) const
{
    const R r = geo().key[juce::jlimit(0, EMU_NKEY - 1, key)];
    return {lay_.frX + (int)std::floor((r.x + KEY_W / 2 - VIEW.x) * lay_.k),
            lay_.frY + lay_.lcdPx + (int)std::floor((r.y + 22 - VIEW.y) * lay_.k)};
}

juce::Point<int> PanelComponent::playGreenPx() const
{
    const R r = geo().btn[EMU_B_PLAY];
    return {lay_.frX + (int)std::floor((r.x + 6 - VIEW.x) * lay_.k), lay_.frY + lay_.lcdPx + (int)std::floor((r.y + r.h - 6 - VIEW.y) * lay_.k)};
}

// ================================================================= input ===
void PanelComponent::holdKey(int k, int src, bool down)
{
    if (k < 0 || k >= EMU_NKEY)
        return;
    const bool was = keySrc_[(size_t)k] != 0;
    keySrc_[(size_t)k] = (uint8_t)(down ? keySrc_[(size_t)k] | src : keySrc_[(size_t)k] & ~src);
    const bool now = keySrc_[(size_t)k] != 0;
    if (was != now)
        proc_.panelKey(k, now);
    updateControls(false);
}

void PanelComponent::holdBtn(int b, int src, bool down)
{
    if (b < 0 || b >= EMU_NB)
        return;
    const bool was = btnSrc_[(size_t)b] != 0;
    btnSrc_[(size_t)b] = (uint8_t)(down ? btnSrc_[(size_t)b] | src : btnSrc_[(size_t)b] & ~src);
    const bool now = btnSrc_[(size_t)b] != 0;
    if (was != now)
        proc_.panelButton(b, now);
    updateControls(false);
}

void PanelComponent::releaseSrc(int src)
{
    for (int i = 0; i < EMU_NKEY; i++)
        if (keySrc_[(size_t)i] & src)
            holdKey(i, src, false);
    for (int i = 0; i < EMU_NB; i++)
        if (btnSrc_[(size_t)i] & src)
            holdBtn(i, src, false);
}

void PanelComponent::turn(int role, int steps)
{
    if (!steps || role < 0 || role >= EMU_NE)
        return;
    proc_.panelEnc(role, steps);                 // (MASTER: 16 per step, the parameter)
    if (role != EMU_E_MASTER)
        knobAngle_[(size_t)role] += (float)steps * (2 * kPi / 24);
    updateControls(false);
}

void PanelComponent::fineTurn(int role, int steps)
{
    proc_.panelFineTurn(role, steps);
    knobAngle_[(size_t)role] += (float)steps * (2 * kPi / 24);
    updateControls(false);
}

void PanelComponent::keyAction(const KeymapEntry &m, bool down, int src)
{
    switch (m.kind) {
    case KM_KEY: holdKey(m.idx, src, down); break;
    case KM_BTN: holdBtn(m.idx, src, down); break;
    case KM_OCTBOTH:
        holdBtn(EMU_B_OCTDN, src == SRC_KEY ? SRC_ESC : src, down);
        holdBtn(EMU_B_OCTUP, src == SRC_KEY ? SRC_ESC : src, down);
        break;
    case KM_SELECT:
        if (down)
            selKnob_ = m.idx, updateControls(false);
        break;
    case KM_CYCLE:
        if (down) {
            static const int CYC[5] = {EMU_E_SELECT, EMU_E_K1, EMU_E_K2, EMU_E_K3, EMU_E_K4};
            int at = m.idx > 0 ? -1 : 0;         // off the cycle: Page Down -> SELECT, Page Up -> KNOB4
            for (int j = 0; j < 5; j++)
                if (CYC[j] == selKnob_)
                    at = j;
            selKnob_ = CYC[((at + m.idx) % 5 + 5) % 5];
            updateControls(false);
        }
        break;
    case KM_SHIFT: shiftHeld_ = down; break;
    case KM_TURN:
        if (down && shiftHeld_ && selKnob_ != EMU_E_MASTER)
            fineTurn(selKnob_, m.idx);
        else if (down)
            turn(selKnob_, selKnob_ == EMU_E_MASTER ? 2 * m.idx : m.idx);
        break;
    case KM_LCDVIEW:
        if (down) {
            setBigLcd(!bigLcd_);
            if (onBigLcdToggled)
                onBigLcdToggled(bigLcd_);
        }
        break;
    }
}

// a logical point -> drawing units; false if not on the panel (the big LCD, the letterbox)
bool PanelComponent::toUnits(juce::Point<float> pt, float &ux, float &uy) const
{
    if (lay_.k <= 0)
        return false;
    const float px = pt.x * lay_.scale - (float)lay_.frX, py = pt.y * lay_.scale - (float)lay_.frY - (float)lay_.lcdPx;
    if (py < 0 || px < 0 || px >= (float)lay_.pixW)
        return false;
    ux = px / lay_.k + VIEW.x;
    uy = py / lay_.k + VIEW.y;
    return true;
}

bool PanelComponent::hit(float ux, float uy, int &kind, int &idx) const
{
    for (int i = 0; i < EMU_NKEY; i++) {
        const R &r = geo().key[i];
        if (ux >= r.x && ux < r.x + r.w && uy >= r.y && uy < r.y + r.h) {
            kind = KM_KEY;
            idx = i;
            return true;
        }
    }
    for (int i = 0; i < EMU_NB; i++) {
        const R &r = geo().btn[i];
        if (ux >= r.x && ux < r.x + r.w && uy >= r.y && uy < r.y + r.h) {
            kind = KM_BTN;
            idx = i;
            return true;
        }
    }
    for (int i = 0; i < EMU_NE; i++) {
        const float dx = ux - ENC_CX[i], dy = uy - ENC_CY[i];
        if (dx * dx + dy * dy <= (ENC_R + 5) * (ENC_R + 5)) {
            kind = KM_SELECT;
            idx = i;
            return true;
        }
    }
    return false;
}

void PanelComponent::mouseDown(const juce::MouseEvent &e)
{
    if (isShowing() && getWantsKeyboardFocus())
        grabKeyboardFocus();
    float ux, uy;
    int kind, idx;
    mouseKind_ = -1;
    if (!toUnits(e.position, ux, uy) || !hit(ux, uy, kind, idx))
        return;
    if (kind == KM_SELECT) {
        selKnob_ = idx;
        dragKnob_ = idx;
        dragY_ = e.position.y;
        dragAcc_ = 0;
        updateControls(false);
        return;
    }
    if (e.mods.isPopupMenu() || e.mods.isCtrlDown()) {   // right-click / ctrl-click: latch
        const bool on = kind == KM_KEY ? (keySrc_[(size_t)idx] & SRC_LATCH) != 0 : (btnSrc_[(size_t)idx] & SRC_LATCH) != 0;
        if (kind == KM_KEY)
            holdKey(idx, SRC_LATCH, !on);
        else
            holdBtn(idx, SRC_LATCH, !on);
        return;
    }
    mouseKind_ = kind;
    mouseIdx_ = idx;
    if (kind == KM_KEY)
        holdKey(idx, SRC_MOUSE, true);
    else
        holdBtn(idx, SRC_MOUSE, true);
}

void PanelComponent::mouseUp(const juce::MouseEvent &)
{
    dragKnob_ = -1;
    if (mouseKind_ == KM_KEY)
        holdKey(mouseIdx_, SRC_MOUSE, false);
    else if (mouseKind_ == KM_BTN)
        holdBtn(mouseIdx_, SRC_MOUSE, false);
    mouseKind_ = -1;
}

void PanelComponent::mouseDrag(const juce::MouseEvent &e)
{
    if (dragKnob_ < 0)
        return;
    dragAcc_ += (dragY_ - e.position.y) / 8.f;   // 8 points per detent, up = clockwise
    dragY_ = e.position.y;
    const int s = (int)dragAcc_;
    dragAcc_ -= (float)s;
    turn(dragKnob_, s);
}

void PanelComponent::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w)
{
    float ux, uy;
    int kind, idx;
    if (!toUnits(e.position, ux, uy) || !hit(ux, uy, kind, idx) || kind != KM_SELECT)
        return;
    // JUCE (mac): a wheel notch is deltaY 10/256 (one SDL preciseY); a trackpad's precise deltas are 0.5/256 per
    // point (SDL: 0.1 per point)
    float d = w.deltaY * (w.isSmooth ? 51.2f : 25.6f);
    if (w.isReversed)
        d = -d;
    wheelAcc_ += d;
    const int s = (int)wheelAcc_;
    wheelAcc_ -= (float)s;
    turn(idx, s);
}

bool PanelComponent::keyPressed(const juce::KeyPress &k)
{
    if (k.getModifiers().isCommandDown())
        return false;                            // (Cmd shortcuts are the host's)
    const int code = normKey(k.getKeyCode());
    const KeymapEntry *m = nullptr;
    for (const KeymapEntry &e : keymap())
        if (e.keyCode == code)
            m = &e;
    if (!m)
        return false;
    shiftHeld_ = k.getModifiers().isShiftDown();
    if (m->kind == KM_TURN) {                    // (repeats)
        keyAction(*m, true, SRC_KEY);
        return true;
    }
    if (heldCodes_.count(code))
        return true;                             // (a repeat)
    heldCodes_.insert(code);
    keyAction(*m, true, SRC_KEY);
    return true;
}

bool PanelComponent::keyStateChanged(bool)
{
    bool any = false;
    for (auto it = heldCodes_.begin(); it != heldCodes_.end();) {
        const int code = *it;
        const bool down = juce::KeyPress::isKeyCurrentlyDown(code) ||
                          (code >= 'A' && code <= 'Z' && juce::KeyPress::isKeyCurrentlyDown(code + 32));
        if (down) {
            ++it;
            continue;
        }
        it = heldCodes_.erase(it);
        for (const KeymapEntry &e : keymap())
            if (e.keyCode == code)
                keyAction(e, false, SRC_KEY);
        any = true;
    }
    return any;
}

void PanelComponent::modifierKeysChanged(const juce::ModifierKeys &m) { shiftHeld_ = m.isShiftDown(); }

void PanelComponent::focusLost(FocusChangeType)
{
    heldCodes_.clear();
    releaseSrc(SRC_KEY | SRC_ESC | SRC_MOUSE);   // no stuck notes (latches stay)
    shiftHeld_ = false;
    dragKnob_ = mouseKind_ = -1;
}
