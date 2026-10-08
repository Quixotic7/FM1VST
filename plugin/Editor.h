// SPDX-License-Identifier: GPL-3.0-only
// The phase 2 editor: functional, plain JUCE (phase 3 replaces it with the panel). A settings bar (firmware,
// presets and their buttons, transpose, "MIDI notes play keys", status) above a GenericAudioProcessorEditor for the
// host parameters.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "Processor.h"

class FM1Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit FM1Editor(FM1Processor &p);
    ~FM1Editor() override;
    void resized() override;
    void paint(juce::Graphics &g) override;

private:
    void timerCallback() override;
    void refreshCores();
    void refreshPresets();
    void refreshStatus();
    void askName(const juce::String &title, const juce::String &initial, std::function<void(juce::String)> done);

    FM1Processor &proc_;
    juce::ComboBox cores_, presets_, transpose_;
    juce::TextButton save_{"Save"}, saveAs_{"Save as"}, rename_{"Rename"}, delete_{"Delete"}, reset_{"Reset flash"},
        export_{"Export"}, import_{"Import"}, power_{"Power on"};
    juce::ToggleButton notesPlayKeys_{"MIDI notes play keys"}, keyNotesToFw_{"... and go to the firmware's MIDI in"};
    juce::Label status_;
    std::unique_ptr<juce::GenericAudioProcessorEditor> generic_;
    std::vector<CoreInfo> coreList_;
    juce::StringArray presetList_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastStatus_;
};
