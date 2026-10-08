// SPDX-License-Identifier: GPL-3.0-only
// The phase 2 editor (see Editor.h).
#include "Editor.h"

static constexpr int kBarH = 64;

FM1Editor::FM1Editor(FM1Processor &p) : juce::AudioProcessorEditor(p), proc_(p)
{
    addAndMakeVisible(cores_);
    cores_.setTooltip("Firmware");
    cores_.onChange = [this] {
        const int i = cores_.getSelectedItemIndex();
        if (i >= 0 && i < (int)coreList_.size() && juce::String(coreList_[(size_t)i].id) != juce::String(proc_.getCoreInfo().info.id)) {
            proc_.switchCore(coreList_[(size_t)i].id);
            refreshPresets();
            refreshStatus();
        }
    };

    addAndMakeVisible(presets_);
    presets_.setTextWhenNothingSelected("(no preset)");
    presets_.onChange = [this] {
        const juce::String n = presets_.getText();
        if (n.isNotEmpty() && n != proc_.currentPresetName() && presetList_.contains(n)) {
            proc_.loadPreset(n);
            refreshStatus();
        }
    };

    for (auto *b : {&save_, &saveAs_, &rename_, &delete_, &reset_, &export_, &import_, &power_})
        addAndMakeVisible(*b);
    save_.onClick = [this] {
        const juce::String n = proc_.currentPresetName();
        if (n.isEmpty())
            saveAs_.triggerClick();
        else
            proc_.savePreset(n), refreshPresets();
    };
    saveAs_.onClick = [this] {
        askName("Save preset as", proc_.currentPresetName(), [this](juce::String n) {
            proc_.savePreset(n);
            refreshPresets();
        });
    };
    rename_.onClick = [this] {
        const juce::String from = proc_.currentPresetName();
        if (from.isEmpty())
            return;
        askName("Rename preset", from, [this, from](juce::String to) {
            proc_.renamePreset(from, to);
            refreshPresets();
        });
    };
    delete_.onClick = [this] {
        const juce::String n = proc_.currentPresetName();
        if (n.isEmpty())
            return;
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                           "Delete \"" + n + "\"?", "Delete", "Cancel", this,
                                           juce::ModalCallbackFunction::create([this, n](int r) {
                                               if (r == 1) {
                                                   proc_.deletePreset(n);
                                                   refreshPresets();
                                               }
                                           }));
    };
    reset_.onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Reset flash",
                                           "Erase this firmware's whole flash (a backup is written first)?", "Reset",
                                           "Cancel", this, juce::ModalCallbackFunction::create([this](int r) {
                                               if (r == 1) {
                                                   proc_.resetFlash();
                                                   refreshPresets();
                                                   refreshStatus();
                                               }
                                           }));
    };
    export_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Export preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                                           .getChildFile((proc_.currentPresetName().isEmpty() ? juce::String("FM1")
                                                                                                                              : proc_.currentPresetName()) + ".fm1preset"),
                                                       "*.fm1preset");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this](const juce::FileChooser &fc) {
                                  const juce::File f = fc.getResult();
                                  if (f != juce::File())
                                      proc_.exportPreset(f);
                              });
    };
    import_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Import preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                       "*.fm1preset");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser &fc) {
                                  const juce::File f = fc.getResult();
                                  if (f != juce::File()) {
                                      proc_.importPreset(f);
                                      refreshPresets();
                                      refreshStatus();
                                  }
                              });
    };
    power_.onClick = [this] {
        proc_.powerCycle();
        refreshStatus();
    };

    addAndMakeVisible(transpose_);
    for (int o = -2; o <= 2; o++)
        transpose_.addItem(o == 0 ? juce::String("Transpose 0") : juce::String::formatted("Transpose %+d oct", o), o + 3);
    transpose_.setSelectedId(proc_.getTranspose() + 3, juce::dontSendNotification);
    transpose_.onChange = [this] { proc_.setTranspose(transpose_.getSelectedId() - 3); };

    addAndMakeVisible(notesPlayKeys_);
    notesPlayKeys_.setToggleState(proc_.getMidiNotesPlayKeys(), juce::dontSendNotification);
    notesPlayKeys_.onClick = [this] { proc_.setMidiNotesPlayKeys(notesPlayKeys_.getToggleState()); };
    addAndMakeVisible(keyNotesToFw_);
    keyNotesToFw_.setToggleState(proc_.getKeyNotesToFirmware(), juce::dontSendNotification);
    keyNotesToFw_.onClick = [this] { proc_.setKeyNotesToFirmware(keyNotesToFw_.getToggleState()); };

    addAndMakeVisible(status_);
    status_.setJustificationType(juce::Justification::centredLeft);

    generic_ = std::make_unique<juce::GenericAudioProcessorEditor>(p);
    addAndMakeVisible(*generic_);

    refreshCores();
    refreshPresets();
    refreshStatus();
    setResizable(true, true);
    setResizeLimits(640, 300, 2400, 2000);
    setSize(900, 640);
    startTimerHz(4);
}

FM1Editor::~FM1Editor() { stopTimer(); }

void FM1Editor::paint(juce::Graphics &g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void FM1Editor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop(kBarH).reduced(4);
    auto row1 = bar.removeFromTop(bar.getHeight() / 2).reduced(0, 1);
    auto row2 = bar.reduced(0, 1);
    cores_.setBounds(row1.removeFromLeft(180));
    row1.removeFromLeft(4);
    presets_.setBounds(row1.removeFromLeft(200));
    for (auto *b : {&save_, &saveAs_, &rename_, &delete_, &reset_, &export_, &import_}) {
        row1.removeFromLeft(4);
        b->setBounds(row1.removeFromLeft(juce::jmin(84, juce::jmax(40, row1.getWidth() / 7))));
    }
    transpose_.setBounds(row2.removeFromLeft(150));
    row2.removeFromLeft(4);
    notesPlayKeys_.setBounds(row2.removeFromLeft(170));
    keyNotesToFw_.setBounds(row2.removeFromLeft(230));
    row2.removeFromLeft(4);
    power_.setBounds(row2.removeFromRight(84));
    status_.setBounds(row2);
    generic_->setBounds(r);
}

void FM1Editor::refreshCores()
{
    coreList_ = proc_.listCores();
    const juce::String cur(proc_.getCoreInfo().info.id);
    cores_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < coreList_.size(); i++) {
        cores_.addItem(juce::String(coreList_[i].name) + " " + coreList_[i].version, (int)i + 1);
        if (juce::String(coreList_[i].id) == cur)
            cores_.setSelectedItemIndex((int)i, juce::dontSendNotification);
    }
}

void FM1Editor::refreshPresets()
{
    presetList_ = proc_.listPresets();
    presets_.clear(juce::dontSendNotification);
    for (int i = 0; i < presetList_.size(); i++)
        presets_.addItem(presetList_[i], i + 1);
    const int i = presetList_.indexOf(proc_.currentPresetName());
    if (i >= 0)
        presets_.setSelectedItemIndex(i, juce::dontSendNotification);
}

void FM1Editor::refreshStatus()
{
    const FM1Processor::CoreStatus s = proc_.getCoreInfo();
    juce::String t = s.loaded ? juce::String(s.info.name) + " " + s.info.version : juce::String("no firmware loaded");
    if (s.halted)
        t << "  |  HALTED (exit " << (s.haltCode & 0xFF) << "): Power on to restart";
    if (s.message.isNotEmpty())
        t << "  |  " << s.message;
    if (t != lastStatus_) {
        status_.setText(t, juce::dontSendNotification);
        status_.setColour(juce::Label::textColourId, s.halted ? juce::Colours::orangered : juce::Colours::white);
        lastStatus_ = t;
    }
}

void FM1Editor::timerCallback()
{
    refreshStatus();
    const juce::StringArray p = proc_.listPresets();
    if (p != presetList_ || presets_.getText() != proc_.currentPresetName())
        refreshPresets();
    if (transpose_.getSelectedId() != proc_.getTranspose() + 3)
        transpose_.setSelectedId(proc_.getTranspose() + 3, juce::dontSendNotification);
    if (keyNotesToFw_.getToggleState() != proc_.getKeyNotesToFirmware())
        keyNotesToFw_.setToggleState(proc_.getKeyNotesToFirmware(), juce::dontSendNotification);
    if (notesPlayKeys_.getToggleState() != proc_.getMidiNotesPlayKeys())
        notesPlayKeys_.setToggleState(proc_.getMidiNotesPlayKeys(), juce::dontSendNotification);
}

void FM1Editor::askName(const juce::String &title, const juce::String &initial, std::function<void(juce::String)> done)
{
    auto *w = new juce::AlertWindow(title, "Name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor("name", initial);
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([w, done](int r) {
                           const juce::String n = w->getTextEditorContents("name").trim();
                           if (r == 1 && n.isNotEmpty())
                               done(n);
                       }),
                       true);
}
