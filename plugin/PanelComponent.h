// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 panel (FM1-VST-PLAN.md 4.8, phase 3): a port of the ChoralRoot emulator's window (tools/emu/emu.c
// draw_base / draw_panel, relayout, present, the mouse and keymap.c) as a juce::Component.
//
// DRAWING. The panel is drawn in the designer geometry (ChoralRootFM1Designer/index.html: 904 x 566 units, the
// VIEW rect 876 x 538 shown) with emu.c's own rasteriser (signed-distance rounded boxes, rings, round-capped
// segments, its 5x7 font supersampled 3x3), ported to draw into a juce::Image in physical pixels: the component
// lays the frame out letterboxed in its bounds at the context's physical pixel scale (Retina: 2), snapping the
// scale to an integer multiple of the 240 x 240 LCD when one is within 8 % (relayout), and rasterises the panel
// once per size and palette (sharp at any size). Two images: base_ (everything static: the body, the beds, the
// printed labels, the screen bezel) and panel_ (base_ plus the 27 keys, 14 buttons and 8 knobs). A 60 Hz timer
// polls the processor's PanelView snapshot; a control whose state changed (held, LED, knob angle, selection) is
// redrawn alone (its box copied back from base_, then drawn again) and only its box is repainted; the LCD is
// repainted when lcd_writes moved. The LCD: RGB565 big-endian -> ARGB (bits replicated, as emu_img.c), drawn
// nearest-neighbour when its on-screen size is an integer multiple of 240 (pre-scaled, then blitted 1:1), else
// with JUCE's high-quality resampling. The big LCD view (backtick, or the editor's button) shows the LCD above the
// panel as a square as wide as the panel (snapped down to a multiple of 240 when one is within 10 %).
//
// INPUT (emu.c mouse_event / key_event). Each key and button has a set of sources (keyboard, End's OCT pair,
// mouse, latch), as emu.c's key_src / btn_src; a control is held while any source holds it, and the processor
// hears panelKey / panelButton when that changes (a separate "held" source merged with the host parameters on the
// audio thread: the panel never moves the btn_ / key_ parameters). Left click: press / release; Shift-click,
// right-click or ctrl-click on a key or button: latch it down (it stays down with no mouse button held: hold several
// at once), and any click on a latched one lets it go; plain clicks on the others stay momentary while some are
// latched (latch HOME, then tap keys). A latched control shows a ring and a dot in the hint colour. Esc lets go of
// every latch (then not OCT-; with none latched Esc is OCT- as before), as do the focus leaving the editor for good
// (1 s, no menu open), a power-on (a firmware switch, a reboot) and closing the editor. Shift held over the panel the
// first time: a toast, "Shift-click to hold" (onShiftHint; the editor remembers it). (On the LCD a right-click is the
// screen's menu, Editor.h.) Shift on a knob changes nothing (a drag turns it as before). Wheel over a knob: one detent
// per notch (MASTER: 16 of 1023); vertical drag on a knob: one detent per 8 points; a click on a knob selects it for
// Up / Down. The keyboard map is keymap.c's (by key: see keymap() in PanelComponent.cpp; the screenshot, recording
// and dump keys are left out), plus a fallback row for the F-key buttons (a host or macOS often takes F5 .. F10): C V B N M , = FX SEL ENV LFO
// EDIT GLO; a control whose first key is an F key shows both on the panel ("F5/C"; setKeyHints(false): no hints
// drawn, the keys still work).
// A key held from the keyboard is pressed once (keyPressed's repeats are ignored while it is held) and let go when
// its release arrives (keyStateChanged) or, when a host swallowed the release, when the check finds it up: on every
// keyStateChanged and at 30 Hz while any key is held (checkHeldKeys). "Up" is the physical key's state when its press
// could be matched to one key (macOS: the HID state, CGEventSourceKeyState; JUCE's KeyPress::isKeyCurrentlyDown only
// moves with the events the host passes on), else JUCE's. Two keys of one control (Z and Esc): it is held while
// either is. Losing the keyboard focus releases what the keyboard and the mouse hold (latches: see above); destroying
// the component releases everything.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <map>
#include <set>

#include "Processor.h"
#include "Theme.h"

class PanelComponent : public juce::Component, private juce::Timer {
public:
    // mirror: a copy for the video recorder (VideoCapture.h): no timer, no input, never on the desktop; its owner calls
    // refresh() and paints it on its own thread, and destroying it lets go of nothing
    explicit PanelComponent(FM1Processor &p, bool mirror = false);
    ~PanelComponent() override;

    void setTheme(const Theme &t);
    const Theme &getTheme() const { return theme_; }
    const Palette &palette() const { return pal_; }
    void setButtonNames(const juce::StringArray &names);   // the core's 14 labels (EMU_B_* order)
    void setBigLcd(bool on);
    bool bigLcd() const { return bigLcd_; }
    std::function<void(bool)> onBigLcdToggled;             // the backtick key toggled it
    // the computer-key hints (the green labels: "F3 2", "TAB", "F5/C"; the sheet's "Show keyboard shortcuts", on by
    // default): off, none is drawn, and the keys' LED and note name and the buttons' names sit centred in the room the
    // labels took (as the site's panel); the keys still work
    void setKeyHints(bool on);
    bool keyHints() const { return keyHints_; }

    // the frame's width / height in units: the panel alone, or with the big LCD above it
    static float frameAspect(bool bigLcd);

    // pull the processor's snapshot, redraw what changed (the timer's work; tests call it directly)
    void refresh();
    // (a mirror) show this snapshot instead of the processor's latest: the video recorder's frame at its instant
    void showView(const PanelView &v);

    // ---- for the editor's chrome (Editor.h): right-click off every control; the LCD changed (the screen view
    // shows lcdImage()); where a point of the drawing (units) is and how big a unit is, in this component's logical
    // points, at the layout the next paint uses; whether a rectangle (logical) covers any control, its printed name or
    // key hint, or the screen
    std::function<void()> onContextMenu;
    std::function<void()> onLcdChanged;
    // ---- copy the screen (ScreenCapture.h): a right-click / ctrl-click on the LCD (on the panel or the big view)
    // asks for the screen's menu instead of the settings; the frame shown now (RGB565 big-endian, as sent: the copy
    // lcdImage() was converted from); frozen: the shown frame is kept (the firmware and the audio run on) until thawed
    std::function<void(const juce::MouseEvent &)> onScreenMenu;
    std::function<void()> onShiftHint;                    // Shift went down over the shown panel (the editor's toast)
    bool overScreen(juce::Point<float> logical) const;   // on the LCD (logical, at the last paint's layout)
    const std::array<uint16_t, EMU_LCD_W * EMU_LCD_H> &shownLcd() const { return shown_; }
    void setFrozen(bool on);
    bool frozen() const { return frozen_; }
    juce::Point<float> unitToLocal(float ux, float uy) const;
    float pointsPerUnit() const;
    bool overlapsControl(juce::Rectangle<float> local) const;

    // ---- the layout of the last paint, in physical pixels of that paint (tests) ----
    struct Layout {
        float scale = 0;            // physical pixels per logical point
        int outW = 0, outH = 0;     // the component in physical pixels
        float k = 0;                // physical pixels per unit
        int frX = 0, frY = 0;       // the frame's origin
        int pixW = 0, pixH = 0;     // the panel image
        int lcdPx = 0;              // the big LCD's band height (0: hidden)
        juce::Rectangle<int> lcdSmall, lcdBig;   // the LCD on the panel's screen, the big view (in the component)
        int nSmall = 0, nBig = 0;   // their integer scale (0: not an integer: smoothed)
    };
    const Layout &layout() const { return lay_; }
    juce::Point<int> keyLedPx(int key) const;      // the centre of key's LED slot (physical, in the component)
    juce::Point<int> playGreenPx() const;          // the centre of PLAY's green LED (physical)
    juce::Rectangle<int> knobBoxPx(int role) const;   // a knob's body (EMU_E_* role; physical, in the component)
    float knobAngle(int role) const { return role >= 0 && role < EMU_NE ? knobAngle_[(size_t)role] : 0.f; }
    const juce::Image &lcdImage() const { return lcd240_; }   // the 240 x 240 LCD as last converted

    // ---- juce::Component ----
    void paint(juce::Graphics &g) override;
    void resized() override { layoutDirty_ = true; }
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override;
    bool keyPressed(const juce::KeyPress &k) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys &m) override;
    void focusLost(FocusChangeType) override;

    enum Kind { KM_KEY, KM_BTN, KM_OCTBOTH, KM_SELECT, KM_CYCLE, KM_SHIFT, KM_TURN, KM_LCDVIEW };
    struct KeymapEntry {
        int keyCode;                // juce::KeyPress key code
        Kind kind;
        int idx;
        const char *cap;            // the hint printed on the panel
    };
    static const std::vector<KeymapEntry> &keymap();
    static const char *keymapHint(Kind kind, int idx);    // the first key of a control
    static juce::String keymapHintText(Kind kind, int idx);   // what the panel prints: "F5/C" when the first is an F key

    // ---- the key-up safety net (see the top). Tests replace the probe (code -> still down) and call the check.
    using KeyProbe = std::function<bool(int keyCode)>;
    void setKeyProbe(KeyProbe p) { keyProbe_ = std::move(p); }
    bool checkHeldKeys();                          // lets go of every held key found up; true if any
    int keysHeldFromKeyboard() const { return (int)heldCodes_.size(); }

    // ---- latches (Shift-click; see the top)
    bool keyLatched(int k) const { return k >= 0 && k < EMU_NKEY && (keySrc_[(size_t)k] & SRC_LATCH) != 0; }
    bool buttonLatched(int b) const { return b >= 0 && b < EMU_NB && (btnSrc_[(size_t)b] & SRC_LATCH) != 0; }
    int latchCount() const;
    void releaseLatches();
    // the focus left the editor (focusLost) and has not come back for 1 s, no menu or dialog open: let go of the
    // latches; true if it did (the timer's work; tests pass the time, Time::getMillisecondCounterHiRes)
    bool checkFocusGone(double nowMs);

private:
    enum { SRC_KEY = 1, SRC_ESC = 2, SRC_MOUSE = 4, SRC_LATCH = 8 };
    void timerCallback() override;
    void applyView();                          // (refresh, showView) view_ onto the drawing
    Layout computeLayout(float scale) const;  // relayout's geometry, nothing drawn
    void relayout(float scale);
    void drawBase();
    void drawAllControls();
    bool updateControls(bool force);           // redraw changed controls into panel_; true if any
    void rebuildLcdImages();
    void repaintPx(juce::Rectangle<int> px);
    void restoreBox(float ux, float uy, float uw, float uh, juce::Rectangle<int> *pxOut);

    void holdKey(int k, int src, bool down);
    void holdBtn(int b, int src, bool down);
    void releaseSrc(int src);
    void turn(int role, int steps);
    void fineTurn(int role, int steps);
    void keyAction(const KeymapEntry &m, bool down, int src);
    bool toUnits(juce::Point<float> logical, float &ux, float &uy) const;
    bool hit(float ux, float uy, int &kind, int &idx) const;

    FM1Processor &proc_;
    const bool mirror_ = false;
    Theme theme_;
    Palette pal_;
    juce::StringArray names_;
    bool bigLcd_ = false, keyHints_ = true;

    // layout and images
    Layout lay_;
    bool layoutDirty_ = true, fullRedraw_ = true;
    juce::Image base_, panel_;                 // pixW x pixH, software ARGB
    juce::Image lcd240_, lcdSmall_, lcdBig_;   // the LCD; pre-scaled copies for the integer cases
    bool lcdImagesDirty_ = true;

    // the processor's snapshot
    PanelView view_;
    uint32_t lcdSeen_ = 0xFFFFFFFFu;
    std::array<uint16_t, EMU_LCD_W * EMU_LCD_H> shown_{};   // the frame lcd240_ shows (as sent)
    bool frozen_ = false;

    // what each control was last drawn with
    struct Drawn {
        int a = -1, b = -1, c = -1, d = -1;
        bool operator!=(const Drawn &o) const { return a != o.a || b != o.b || c != o.c || d != o.d; }
    };
    std::array<Drawn, EMU_NKEY> keyDrawn_;
    std::array<Drawn, EMU_NB> btnDrawn_;
    std::array<Drawn, EMU_NE> knobDrawn_;

    // input state (emu.c's panel state)
    std::array<uint8_t, EMU_NKEY> keySrc_{};
    std::array<uint8_t, EMU_NB> btnSrc_{};
    std::array<float, EMU_NE> knobAngle_{};
    std::array<int32_t, EMU_NE - 1> turnsSeen_{};   // the snapshot's detent counts last seen
    bool turnsInit_ = false;
    int selKnob_ = EMU_E_PRESETS;
    int mouseKind_ = -1, mouseIdx_ = 0, dragKnob_ = -1;
    float dragAcc_ = 0, wheelAcc_ = 0, dragY_ = 0;
    bool shiftHeld_ = false;
    std::set<int> swallowed_;                  // key codes eaten (Esc letting go of the latches) until they are up
    double focusLostAt_ = 0;                   // ms, while the focus is away with latches down (0: not away)
    uint32_t powerOnsSeen_ = 0;
    std::map<int, int> heldCodes_;             // key codes down -> the physical key seen at the press (-1 unknown)
    KeyProbe keyProbe_;
    int pollTick_ = 0;
    bool keyStillDown(int code, int physical) const;
    int physicalKeyAtPress() const;            // the one physical key newly down (macOS virtual key code), or -1
    void releaseCode(int code);
};
