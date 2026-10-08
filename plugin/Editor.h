// SPDX-License-Identifier: GPL-3.0-only
// The plugin's editor (phase 3): a slim settings bar on top (firmware, presets and their buttons, transpose, the two
// MIDI toggles, the theme, the big LCD view, the status with Power on) and the FM-1 panel (PanelComponent) filling
// the rest. Resizable, the panel's aspect kept (with the big LCD shown the frame letterboxes instead); the last size
// is remembered per instance in the plugin state. The host parameters have no generic list any more: they remain
// the host's (automation, the Roto-Control).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "PanelComponent.h"
#include "Processor.h"
#include "Theme.h"

// "Custom...": the three colours (Base, Membrane, Knobs), the optional Bed and Label overrides, a name; every change
// previews live on the panel
class ThemeEditorPanel : public juce::Component, private juce::ChangeListener {
public:
    ThemeEditorPanel();
    ~ThemeEditorPanel() override;
    void setTheme(const Theme &t);               // (no onChange)
    Theme theme() const;
    void paint(juce::Graphics &g) override;
    void resized() override;

    std::function<void(const Theme &)> onChange;
    std::function<bool(const Theme &, bool asDefault)> onSave;   // false: not saved (see the note)
    void setNote(const juce::String &s) { note_.setText(s, juce::dontSendNotification); }
    std::function<void(const juce::String &)> onDelete;
    std::function<void()> onClose;

private:
    void changeListenerCallback(juce::ChangeBroadcaster *) override;
    void changed();

    struct Column {
        juce::Label title;
        juce::ToggleButton enable;               // (Bed, Label: the override is on)
        std::unique_ptr<juce::ColourSelector> sel;
        bool optional = false;
    };
    std::array<Column, 5> cols_;                 // base, membrane, knob, bed, label
    juce::Label nameLabel_{{}, "Name"};
    juce::TextEditor name_;
    juce::TextButton save_{"Save"}, default_{"Save as default"}, delete_{"Delete"}, close_{"Close"};
    juce::Label note_;
    bool setting_ = false;
};

class FM1Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit FM1Editor(FM1Processor &p);
    ~FM1Editor() override;
    void resized() override;
    void paint(juce::Graphics &g) override;

    static constexpr int kBarH = 60;
    PanelComponent &panel() { return panel_; }
    const Theme &panelTheme() const { return panel_.getTheme(); }
    // the editor's default size: the panel at 1.6 logical points per unit (an integer LCD scale: 1x, 2x on Retina),
    // i.e. 1401 x 861 points, about 80 % of a 1080p screen's height with the bar
    static juce::Point<int> defaultSize();

private:
    class Constrainer : public juce::ComponentBoundsConstrainer {
    public:
        std::function<bool()> bigLcd;
        void checkBounds(juce::Rectangle<int> &b, const juce::Rectangle<int> &previous, const juce::Rectangle<int> &limits,
                         bool top, bool left, bool bottom, bool right) override;
    };

    void timerCallback() override;
    void refreshCores();
    void refreshPresets();
    void refreshStatus();
    void refreshThemes();
    void applyTheme(const Theme &t);
    void openThemeEditor();
    void setBigLcd(bool on);
    void askName(const juce::String &title, const juce::String &initial, std::function<void(juce::String)> done);

    FM1Processor &proc_;
    ThemeStore store_;
    PanelComponent panel_;
    Constrainer constrainer_;
    juce::ComboBox cores_, presets_, transpose_, themes_;
    juce::TextButton save_{"Save"}, saveAs_{"Save as"}, rename_{"Rename"}, delete_{"Delete"}, reset_{"Reset flash"},
        export_{"Export"}, import_{"Import"}, power_{"Power on"}, bigLcd_{"Big LCD"};
    juce::ToggleButton notesPlayKeys_{"MIDI notes play keys"}, keyNotesToFw_{"... and go to the firmware's MIDI in"};
    juce::Label status_;
    std::unique_ptr<ThemeEditorPanel> themeEditor_;
    std::vector<CoreInfo> coreList_;
    juce::StringArray presetList_;
    std::vector<Theme> themeItems_;              // the theme menu's entries by item id - 1
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastStatus_, lastCore_;
    uint32_t themeSerial_ = 0;
};
