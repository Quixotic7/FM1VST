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
// audio thread: the panel never moves the btn_ / key_ parameters). Left click: press / release; right-click or
// ctrl-click: latch down, again to release; wheel over a knob: one detent per notch (MASTER: 16 of 1023); vertical
// drag on a knob: one detent per 8 points; a click on a knob selects it for Up / Down. The keyboard map is
// keymap.c's (by key: see kKeymap in PanelComponent.cpp; the screenshot, recording and dump keys are left out).
// Losing the keyboard focus releases what the keyboard and the mouse hold (latches stay); destroying the
// component releases everything.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <set>

#include "Processor.h"
#include "Theme.h"

class PanelComponent : public juce::Component, private juce::Timer {
public:
    explicit PanelComponent(FM1Processor &p);
    ~PanelComponent() override;

    void setTheme(const Theme &t);
    const Theme &getTheme() const { return theme_; }
    const Palette &palette() const { return pal_; }
    void setButtonNames(const juce::StringArray &names);   // the core's 14 labels (EMU_B_* order)
    void setBigLcd(bool on);
    bool bigLcd() const { return bigLcd_; }
    std::function<void(bool)> onBigLcdToggled;             // the backtick key toggled it

    // the frame's width / height in units: the panel alone, or with the big LCD above it
    static float frameAspect(bool bigLcd);

    // pull the processor's snapshot, redraw what changed (the timer's work; tests call it directly)
    void refresh();

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
    static const char *keymapHint(Kind kind, int idx);

private:
    enum { SRC_KEY = 1, SRC_ESC = 2, SRC_MOUSE = 4, SRC_LATCH = 8 };
    void timerCallback() override { refresh(); }
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
    Theme theme_;
    Palette pal_;
    juce::StringArray names_;
    bool bigLcd_ = false;

    // layout and images
    Layout lay_;
    bool layoutDirty_ = true, fullRedraw_ = true;
    juce::Image base_, panel_;                 // pixW x pixH, software ARGB
    juce::Image lcd240_, lcdSmall_, lcdBig_;   // the LCD; pre-scaled copies for the integer cases
    bool lcdImagesDirty_ = true;

    // the processor's snapshot
    PanelView view_;
    uint32_t lcdSeen_ = 0xFFFFFFFFu;

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
    int selKnob_ = EMU_E_PRESETS;
    int mouseKind_ = -1, mouseIdx_ = 0, dragKnob_ = -1;
    float dragAcc_ = 0, wheelAcc_ = 0, dragY_ = 0;
    bool shiftHeld_ = false;
    std::set<int> heldCodes_;                  // key codes down (keymap entries that hold)
};
