// SPDX-License-Identifier: GPL-3.0-only
// The plugin's editor: the FM-1 and nothing else around it.
//
// VIEWS (FM1Processor::getViewMode, saved in the state: a set reopens in its view)
//   Device   the panel (PanelComponent, drawn as before) edge to edge. Resizable with the panel's aspect locked (with
//            the big LCD above it, ` or the sheet's VIEW: free, the frame letterboxed); the size is remembered in the
//            state (editorW / editorH).
//   Screen   the 240 x 240 LCD alone in a thin rounded bezel, in a square window, resizable; its side is remembered
//            apart (screenSize). The LCD is nearest-neighbour at whole multiples of 240 physical pixels (the layout
//            snaps to one within 6 %), else scaled up nearest-neighbour to the next whole multiple and smoothed down
//            from there, so pixel edges stay sharp at any size. The computer keys still play the panel (the view
//            passes them on).
// SETTINGS are hidden the same way in both views: a small round "more" button fades in at the top-right corner while
// the mouse moves over the editor (150 ms) and fades out 1.5 s after it leaves or stops; in the device view it sits on
// the body's empty top-right corner, clear of every control at every size. While Remote FM-1 is on it carries a small
// LED that stays when the button fades (green: connected; amber: searching, in use or lost). A halted firmware keeps
// the button shown, ringed in red. The button, or a right-click anywhere off the panel's controls (a right-click on a
// key or button still latches it, on a knob still selects it), opens the settings sheet (Sheet.h).
//
// The Standalone app: its window loses JUCE's "Options" bar (the native title bar instead); its audio / MIDI settings
// and state files are in the sheet.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <vector>

#include "Look.h"
#include "PanelComponent.h"
#include "Processor.h"
#include "Sheet.h"
#include "Theme.h"
#include "Widgets.h"

// the screen view: the LCD in its bezel
class ScreenView : public juce::Component {
public:
    explicit ScreenView(PanelComponent &panel);
    void lcdChanged() { repaint(lcdArea().getSmallestIntegerContainer()); }
    juce::Rectangle<float> lcdArea() const;       // the LCD's rectangle at the last paint's pixel scale (logical)
    juce::Rectangle<float> bezelArea() const;
    int lcdScale() const { return lastN_; }       // the last paint: the LCD's whole multiple (0: smoothed)
    std::function<void()> onContextMenu;

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override;
    // the computer keys: the panel's (it is hidden, its key handling is not)
    bool keyPressed(const juce::KeyPress &k) override { return panel_.keyPressed(k); }
    bool keyStateChanged(bool down) override { return panel_.keyStateChanged(down); }
    void modifierKeysChanged(const juce::ModifierKeys &m) override { panel_.modifierKeysChanged(m); }
    void focusLost(FocusChangeType c) override { panel_.focusLost(c); }

private:
    juce::Rectangle<float> lcdAreaFor(float scale) const;
    PanelComponent &panel_;
    float lastScale_ = 1.0f;
    int lastN_ = 0;
    juce::Image cache_;                           // the LCD scaled up nearest-neighbour
    const void *cacheSrc_ = nullptr;
    int cacheN_ = 0;
    uint32_t cacheSerial_ = 0, serial_ = 1;
    friend class FM1Editor;
};

class FM1Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit FM1Editor(FM1Processor &p);
    ~FM1Editor() override;
    void resized() override;
    void paint(juce::Graphics &g) override;
    void focusGained(FocusChangeType) override;
    void parentHierarchyChanged() override;

    // ---- views
    PanelComponent &panel() { return panel_; }
    ScreenView &screen() { return screen_; }
    const Theme &panelTheme() const { return panel_.getTheme(); }
    int viewMode() const { return mode_; }
    void setViewMode(int m);
    void setBigLcd(bool on);
    // the device view's default size: the panel at 1.6 logical points per unit (an integer LCD scale: 1x, 2x on
    // Retina), 1402 x 861 points, about 80 % of a 1080p screen's height; the screen view's: the LCD at 2 points per
    // pixel
    static juce::Point<int> defaultSize() { return {1402, 861}; }
    static int defaultScreenSize() { return 546; }
    static constexpr int kMinW = 640, kMaxW = 5000, kMinScreen = 320, kMaxScreen = 2400;

    // ---- the sheet and what it changes
    void openSettings(bool animate = true);
    Sheet &sheet() { return sheet_; }
    FM1Processor &proc() { return proc_; }
    ThemeStore &themeStore() { return store_; }
    Look &look() { return look_; }
    void applyTheme(const Theme &t);
    void coreChanged();                           // after a switch, a preset load, a reset: labels, status
    void focusActiveView();                       // the keyboard focus back to the panel or the screen view
    juce::Component &activeView() { return mode_ == FM1Processor::kViewScreen ? (juce::Component &)screen_ : (juce::Component &)panel_; }
    bool isStandalone() const { return proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone; }

    // ---- the corner button
    CornerButton &cornerButton() { return corner_; }
    juce::Rectangle<float> cornerCircle() const;  // the drawn button (logical, in the editor)
    void showChromeNow();                         // shown at once (tests; a click)
    void hideChromeNow();
    void layoutChrome();

private:
    class Constrainer : public juce::ComponentBoundsConstrainer {
    public:
        std::function<int()> mode;
        std::function<bool()> bigLcd;
        void checkBounds(juce::Rectangle<int> &b, const juce::Rectangle<int> &previous, const juce::Rectangle<int> &limits,
                         bool top, bool left, bool bottom, bool right) override;
    };
    // the mouse anywhere over the editor (its children included)
    struct Watcher : juce::MouseListener {
        FM1Editor &e;
        explicit Watcher(FM1Editor &ed) : e(ed) {}
        void mouseMove(const juce::MouseEvent &) override { e.activity(); }
        void mouseEnter(const juce::MouseEvent &) override { e.activity(); }
        void mouseDrag(const juce::MouseEvent &) override { e.activity(); }
        void mouseExit(const juce::MouseEvent &) override { e.activity(); }
    };
    struct Ticker : juce::Timer {
        std::function<void()> fn;
        void timerCallback() override { fn(); }
    };

    void timerCallback() override;
    void updateLimits();
    void activity();
    void chromeTick();
    void refreshChrome();
    void tweakStandaloneWindow();

    FM1Processor &proc_;
    Look look_;
    ThemeStore store_;
    PanelComponent panel_;
    ScreenView screen_;
    CornerButton corner_;
    Sheet sheet_;
    Constrainer constrainer_;
    Watcher watcher_{*this};
    Ticker chrome_;
    juce::TooltipWindow tips_{this, 700};
    int mode_ = FM1Processor::kViewDevice;
    double lastMove_ = -1e9, lastTick_ = 0;
    juce::String lastCore_;
    uint32_t themeSerial_ = 0;
    bool standaloneDone_ = false, halted_ = false;
};
