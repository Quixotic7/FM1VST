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
// COPY THE SCREEN (ScreenCapture.h): a right-click (or ctrl-click) on the LCD, in the device view (the panel's screen or
// the big LCD) and in the screen view (the bezel), opens the screen's menu: Copy screen (240 x 240), Copy screen x2 /
// x4 (480 / 960, nearest-neighbour), Save screen as PNG (x1 / x2 / x4), Advanced (Copy raw RGB565 (.bin), Save raw
// RGB565 as .bin), Freeze screen, Settings. Option + right-click copies at once (x1), no menu. Cmd+Shift+C copies at the
// sheet's scale (its VIEW section's "Screen" row: Copy and the scale), where the host passes the key on (the Standalone
// does; a plugin host may keep it). A toast over the LCD says what was copied.
//
// VIDEO RECORDING (the Standalone only; VideoCapture.h): a record button, the corner button's mirror image at the
// body's top-left corner (the window's in the big-LCD view, the bezel's in the screen view), fading with it while idle;
// while recording, a pill with the red dot pulsing and the take's time, always shown; after a take, "Show in Finder"
// for 8 s. Cmd+Shift+R starts and stops a take (the computer-key map has no Cmd keys: the panel passes them on). A
// toast says what was saved, or why a take could not start or ended early. The sheet's Video section has the rest.
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

namespace fm1video {
class Session;
}

// the screen view: the LCD in its bezel
class ScreenView : public juce::Component {
public:
    explicit ScreenView(PanelComponent &panel);
    void lcdChanged()                             // (its scaled copy made again at the next paint)
    {
        serial_++;
        repaint(lcdArea().getSmallestIntegerContainer());
    }
    juce::Rectangle<float> lcdArea() const;       // the LCD's rectangle at the last paint's pixel scale (logical)
    juce::Rectangle<float> bezelArea() const;
    int lcdScale() const { return lastN_; }       // the last paint: the LCD's whole multiple (0: smoothed)
    std::function<void()> onContextMenu;
    std::function<void(const juce::MouseEvent &)> onScreenMenu;   // a right-click on the bezel / LCD

    void paint(juce::Graphics &g) override;     // paintBody, then paintLcd
    void paintBody(juce::Graphics &g);            // the body and the bezel (the video recorder caches it)
    void paintLcd(juce::Graphics &g);             // the LCD alone
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

// a short note over the LCD ("Screen copied (240×240)"), fading out by itself; never takes a click
class Toast : public juce::Component, private juce::Timer {
public:
    Toast() { setInterceptsMouseClicks(false, false); }
    void show(const juce::String &text);
    const juce::String &text() const { return text_; }
    float alpha() const { return alpha_; }
    int preferredWidth() const;
    void paint(juce::Graphics &g) override;

private:
    void timerCallback() override;
    juce::String text_;
    double shownAt_ = 0;
    float alpha_ = 0;
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
    void setKeyHints(bool on);                    // the panel's computer-key hints (the sheet's "Show keyboard shortcuts"),
                                                  // remembered in settings.json; the recorder's Device view follows
    bool keyHints() const { return panel_.keyHints(); }
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

    // ---- copy the screen (see the top): the frame shown now; every call on the message thread
    enum ScreenCmd { kCopy1 = 1, kCopy2, kCopy4, kSave1, kSave2, kSave4, kCopyRaw, kSaveRaw, kFreeze, kSettings };
    juce::PopupMenu screenMenu();                 // the menu (its items' ids: ScreenCmd)
    void showScreenMenu(const juce::MouseEvent &e);
    void screenCommand(int cmd);
    bool copyScreen(int scale);                   // the clipboard (PNG + TIFF); the toast
    bool copyScreenRaw();
    void saveScreen(int scale);                   // a file chooser, then the PNG
    void saveScreenRaw();
    void setScreenFrozen(bool on);
    bool screenFrozen() const { return panel_.frozen(); }
    int copyScale() const { return copyScale_; }  // the sheet's scale (Cmd+Shift+C, its Copy button): 1, 2 or 4
    void setCopyScale(int s) { copyScale_ = s == 2 || s == 4 ? s : 1; }
    juce::String screenFileName(const juce::String &ext) const;   // <firmware id>-<version>-<YYYYMMDD-HHMMSS><ext>
    static juce::String fileNameFor(const FM1Processor &p, const juce::String &ext);   // (the same, for any instance)
    Toast &toast() { return toast_; }
    void showToast(const juce::String &text);
    bool keyPressed(const juce::KeyPress &k) override;

    // ---- video recording (the Standalone only: VideoCapture.h; a plugin has none of it, the DAW records): the record
    // button opposite the corner button, Cmd+Shift+R, the sheet's Video section
    bool videoAvailable() const { return video_ != nullptr; }
    void toggleRecording();
    RecordButton &recordButton() { return rec_; }
    juce::Rectangle<float> recordCircle() const;  // the idle button's circle (logical, in the editor)

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
    RecordButton rec_;
    fm1video::Session *video_ = nullptr;
    uint32_t videoSerial_ = 0;
    double savedUntil_ = 0;
    void videoChanged();
    Sheet sheet_;
    Toast toast_;
    std::unique_ptr<juce::FileChooser> chooser_;
    int copyScale_ = 1;
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
