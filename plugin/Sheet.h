// SPDX-License-Identifier: GPL-3.0-only
// The settings sheet: every option of the editor, out of sight until asked for (the corner button, or a right-click
// off the panel's controls). It slides in from the right over a dimmed scrim (180 ms, eased); a click on the scrim or
// Esc closes it. Pages:
//   Main       FIRMWARE (the loaded one; opens the browser), PRESET (‹ name ›, Save, Save as, Rename, Delete, Export,
//              Import; Save as and Rename edit inline, Delete confirms inline), REMOTE FM-1 (the switch, the link's status,
//              "Ignore the FM-1's own MIDI"), EDITORS (macOS: "Show to editors", the port's name, a hint, the firmware's
//              editor as a link; EditorPort.h), PLAYING (Transpose, MIDI notes play keys, ...and reach the firmware's MIDI
//              in), HOST CONTROL (Button parameters, Encoders, steps per turn), LOOK (the themes, a custom one), VIEW
//              (Device / Screen, the big LCD), DEVICE (Restart, Reset flash: confirmed inline), STANDALONE (the
//              Standalone app's audio / MIDI settings and its state files; only there), ABOUT
//   Firmware   the browser: a search field, the engine filter (All / Native / Emulated), category chips, a card per
//              firmware (the loaded one outlined): FM1VST's native cores and the Hub's firmwares in one list (one card
//              per firmware, its engines as badges: the one running filled), the catalog's state, and "Your own .fwsc"
//   About      the loaded firmware: version, licence, source, gating, credits (an emulated firmware: the Hub's entry,
//              its package, the engine and its clock)
// The main page's FIRMWARE also shows, for an emulated firmware, its download (progress, errors), the Engine switch
// (a firmware with both), the emulated clock (Auto 240 320 360 480 640 MHz) and a Source link (any firmware).
//   Audio      (the Standalone) its audio and MIDI devices
//   Theme      a custom theme: the name, the five colours (Bed and Label optional), Save / Save as default / Delete;
//              every change previews live on the panel (the scrim dims less on this page)
// Nothing here is a stock dialog: confirmations and names are inline; Import and Export use the system's file chooser.
// The sheet keeps the keyboard focus while open and passes the computer keys on to the panel (Esc: close).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "Hub.h"
#include "Look.h"
#include "Widgets.h"
#include "cores.h"

class FM1Editor;

// what the browser shows of a firmware beyond its descriptor: a category, a short tag, its author (a small table by
// core id; an unknown core: "Other", its source's owner)
struct FirmwareMeta {
    juce::String category, tag, author;
    static FirmwareMeta of(const juce::String &id, const juce::String &sourceUrl);
    static const juce::StringArray &categories();   // All Synth Groovebox Chords Drums Other
};

class Sheet : public juce::Component, private juce::Timer, private juce::ChangeListener {
public:
    explicit Sheet(FM1Editor &ed);
    ~Sheet() override;

    enum Page { kMain, kFirmware, kAbout, kTheme, kAudio };
    void open(bool animate = true);
    void close(bool animate = true);
    bool isOpen() const { return target_ > 0.5f; }
    void showPage(Page p);
    Page page() const { return page_; }
    void refresh();                               // the values from the processor (the editor's timer)

    // ---- for tests: the inline states and the browser's filter
    void beginRename();
    void beginSaveAs();
    void beginDeleteConfirm();
    void beginResetConfirm();
    void scrollToEditors();                       // the main page scrolled to its Editors section
    void setBrowserFilter(const juce::String &search, int category);
    void setEngineFilter(int engine);             // 0 All, 1 Native, 2 Emulated
    void pickInBrowser(const juce::String &key, int engine = -1);   // a card's click (engine: -1 the default)

    // the browser's sources, read when the sheet opens and when the Hub's catalog changes (never per refresh: a scan
    // probes every module)
    const hub::Catalog &catalog() const { return catalog_; }
    const std::vector<CoreInfo> &natives() const { return natives_; }
    void reloadSources();
    juce::Rectangle<int> cardBounds() const { return cardBounds_; }

    void paint(juce::Graphics &g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent &e) override;
    bool keyPressed(const juce::KeyPress &k) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys &m) override;
    void focusLost(FocusChangeType) override;

    class MainPage;
    class FirmwarePage;
    class AboutPage;
    class ThemePage;
    class AudioHolder;

private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster *) override;
    hub::Catalog catalog_;
    std::vector<CoreInfo> natives_;
    int sheetWidth() const;
    void layoutCard();

    FM1Editor &ed_;
    Look &look_;
    float t_ = 0, target_ = 0;                    // the slide: 0 out .. 1 in
    double animStart_ = 0;
    float animFrom_ = 0;
    Page page_ = kMain;
    juce::Rectangle<int> cardBounds_;
    // the header
    IconButton back_, close_;
    std::unique_ptr<MainPage> main_;
    std::unique_ptr<FirmwarePage> firmware_;
    std::unique_ptr<AboutPage> about_;
    std::unique_ptr<ThemePage> theme_;
    std::unique_ptr<juce::Component> audio_;      // (the Standalone: its device selector, made when first shown)
    std::unique_ptr<AudioHolder> audioPage_;
    juce::Component *current_ = nullptr;
};
