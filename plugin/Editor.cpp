// SPDX-License-Identifier: GPL-3.0-only
// The editor (see Editor.h).
#include "Editor.h"

static const juce::Colour kBarBg(0xFF17171A);

// ===================================================== the theme editor ===
ThemeEditorPanel::ThemeEditorPanel()
{
    static const char *const titles[5] = {"Base", "Membrane", "Knobs", "Bed", "Label"};
    for (int i = 0; i < 5; i++) {
        Column &c = cols_[(size_t)i];
        c.optional = i >= 3;
        c.title.setText(titles[i], juce::dontSendNotification);
        c.title.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(c.title);
        c.sel = std::make_unique<juce::ColourSelector>(juce::ColourSelector::showColourAtTop | juce::ColourSelector::editableColour |
                                                       juce::ColourSelector::showColourspace);
        c.sel->addChangeListener(this);
        addAndMakeVisible(*c.sel);
        if (c.optional) {
            c.enable.setButtonText(juce::String("override ") + juce::String(titles[i]).toLowerCase());
            c.enable.onClick = [this, i] {
                cols_[(size_t)i].sel->setEnabled(cols_[(size_t)i].enable.getToggleState());
                changed();
            };
            addAndMakeVisible(c.enable);
        }
    }
    addAndMakeVisible(nameLabel_);
    addAndMakeVisible(name_);
    name_.onTextChange = [this] { changed(); };
    for (auto *b : {&save_, &default_, &delete_, &close_})
        addAndMakeVisible(*b);
    save_.onClick = [this] {
        if (onSave)
            onSave(theme(), false);
    };
    default_.onClick = [this] {
        if (onSave)
            onSave(theme(), true);
    };
    delete_.onClick = [this] {
        if (onDelete)
            onDelete(theme().name);
    };
    close_.onClick = [this] {
        if (onClose)
            onClose();
    };
    note_.setJustificationType(juce::Justification::centredLeft);
    note_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(note_);
}

ThemeEditorPanel::~ThemeEditorPanel()
{
    for (Column &c : cols_)
        c.sel->removeChangeListener(this);
}

void ThemeEditorPanel::setTheme(const Theme &t)
{
    setting_ = true;
    const Palette p = derive(t);
    cols_[0].sel->setCurrentColour(t.base, juce::dontSendNotification);
    cols_[1].sel->setCurrentColour(t.membrane, juce::dontSendNotification);
    cols_[2].sel->setCurrentColour(t.knob, juce::dontSendNotification);
    cols_[3].sel->setCurrentColour(t.bed.value_or(p.bed), juce::dontSendNotification);
    cols_[4].sel->setCurrentColour(t.label.value_or(p.label), juce::dontSendNotification);
    cols_[3].enable.setToggleState(t.bed.has_value(), juce::dontSendNotification);
    cols_[4].enable.setToggleState(t.label.has_value(), juce::dontSendNotification);
    cols_[3].sel->setEnabled(t.bed.has_value());
    cols_[4].sel->setEnabled(t.label.has_value());
    name_.setText(t.name, juce::dontSendNotification);
    setting_ = false;
}

Theme ThemeEditorPanel::theme() const
{
    Theme t;
    t.name = name_.getText().trim();
    t.base = cols_[0].sel->getCurrentColour().withAlpha((juce::uint8)255);
    t.membrane = cols_[1].sel->getCurrentColour().withAlpha((juce::uint8)255);
    t.knob = cols_[2].sel->getCurrentColour().withAlpha((juce::uint8)255);
    t.bed = cols_[3].enable.getToggleState() ? std::optional<juce::Colour>(cols_[3].sel->getCurrentColour().withAlpha((juce::uint8)255))
                                             : std::nullopt;
    t.label = cols_[4].enable.getToggleState() ? std::optional<juce::Colour>(cols_[4].sel->getCurrentColour().withAlpha((juce::uint8)255))
                                               : std::nullopt;
    return t;
}

void ThemeEditorPanel::changeListenerCallback(juce::ChangeBroadcaster *) { changed(); }

void ThemeEditorPanel::changed()
{
    if (!setting_ && onChange)
        onChange(theme());
}

void ThemeEditorPanel::paint(juce::Graphics &g)
{
    g.setColour(kBarBg.withAlpha(0.97f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.0f);
}

void ThemeEditorPanel::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto bottom = r.removeFromBottom(26);
    nameLabel_.setBounds(bottom.removeFromLeft(44));
    name_.setBounds(bottom.removeFromLeft(180));
    for (auto *b : {&save_, &default_, &delete_, &close_}) {
        bottom.removeFromLeft(6);
        b->setBounds(bottom.removeFromLeft(b == &default_ ? 120 : 64));
    }
    bottom.removeFromLeft(8);
    note_.setBounds(bottom);
    r.removeFromBottom(8);
    const int w = r.getWidth() / 5;
    for (Column &c : cols_) {
        auto col = r.removeFromLeft(w).reduced(4, 0);
        auto head = col.removeFromTop(22);
        if (c.optional) {
            c.title.setBounds(head.removeFromLeft(48));
            c.enable.setBounds(head);
        } else {
            c.title.setBounds(head);
        }
        c.sel->setBounds(col);
    }
}

// ================================================================ editor ===
void FM1Editor::Constrainer::checkBounds(juce::Rectangle<int> &b, const juce::Rectangle<int> &previous,
                                         const juce::Rectangle<int> &limits, bool top, bool left, bool bottom, bool right)
{
    if (!(bigLcd && bigLcd())) {             // the panel's aspect (with the big LCD shown: free, letterboxed)
        const double a = PanelComponent::frameAspect(false);
        int w = b.getWidth();
        if ((top || bottom) && !(left || right))
            w = juce::roundToInt((b.getHeight() - kBarH) * a);
        w = juce::jlimit(getMinimumWidth(), getMaximumWidth(), w);
        b.setSize(w, kBarH + juce::roundToInt(w / a));
    }
    juce::ComponentBoundsConstrainer::checkBounds(b, previous, limits, top, left, bottom, right);
}

juce::Point<int> FM1Editor::defaultSize() { return {1402, kBarH + 861}; }

FM1Editor::FM1Editor(FM1Processor &p)
    : juce::AudioProcessorEditor(p), proc_(p), store_(FM1Processor::home()), panel_(p)
{
    addAndMakeVisible(panel_);
    panel_.setButtonNames(proc_.buttonNames());
    panel_.setTheme(proc_.getTheme());
    themeSerial_ = proc_.themeSerial();
    panel_.setBigLcd(proc_.getBigLcd());
    panel_.onBigLcdToggled = [this](bool on) { setBigLcd(on); };

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
    presets_.setTooltip("Presets of this firmware (the whole flash)");
    presets_.onChange = [this] {
        const juce::String n = presets_.getText();
        if (n.isNotEmpty() && n != proc_.currentPresetName() && presetList_.contains(n)) {
            proc_.loadPreset(n);
            refreshStatus();
        }
    };

    for (auto *b : {&save_, &saveAs_, &rename_, &delete_, &reset_, &export_, &import_, &power_, &bigLcd_})
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
        const juce::String n = proc_.currentPresetName().isEmpty() ? juce::String("FM1") : proc_.currentPresetName();
        chooser_ = std::make_unique<juce::FileChooser>(
            "Export preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(n + ".fm1preset"),
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
    bigLcd_.setClickingTogglesState(true);
    bigLcd_.setTooltip("Show the LCD big above the panel (` key)");
    bigLcd_.setToggleState(proc_.getBigLcd(), juce::dontSendNotification);
    bigLcd_.onClick = [this] { setBigLcd(bigLcd_.getToggleState()); };

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

    addAndMakeVisible(themes_);
    themes_.setTooltip("Panel colours");
    themes_.onChange = [this] {
        const int id = themes_.getSelectedId();
        if (id == 10000) {
            openThemeEditor();
            refreshThemes();                     // (the menu shows the theme again, not "Custom...")
        } else if (id >= 1 && id <= (int)themeItems_.size()) {
            applyTheme(themeItems_[(size_t)id - 1]);
        }
    };

    addAndMakeVisible(status_);
    status_.setJustificationType(juce::Justification::centredLeft);

    refreshCores();
    refreshPresets();
    refreshThemes();
    refreshStatus();
    lastCore_ = juce::String(proc_.getCoreInfo().info.id);

    constrainer_.bigLcd = [this] { return panel_.bigLcd(); };
    constrainer_.setSizeLimits(760, kBarH + 300, 5000, 4000);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    juce::Point<int> sz = proc_.getEditorSize();
    if (sz.x <= 0 || sz.y <= 0)
        sz = defaultSize();
    setSize(juce::jlimit(760, 5000, sz.x), juce::jlimit(kBarH + 300, 4000, sz.y));
    startTimerHz(4);
}

FM1Editor::~FM1Editor() { stopTimer(); }

void FM1Editor::paint(juce::Graphics &g) { g.fillAll(kBarBg); }

void FM1Editor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop(kBarH).reduced(4, 3);
    auto row1 = bar.removeFromTop(bar.getHeight() / 2).reduced(0, 1);
    auto row2 = bar.reduced(0, 1);
    cores_.setBounds(row1.removeFromLeft(170));
    row1.removeFromLeft(4);
    presets_.setBounds(row1.removeFromLeft(180));
    power_.setBounds(row1.removeFromRight(80));
    row1.removeFromRight(4);
    for (auto *b : {&save_, &saveAs_, &rename_, &delete_, &reset_, &export_, &import_}) {
        row1.removeFromLeft(4);
        b->setBounds(row1.removeFromLeft(juce::jmin(84, juce::jmax(40, row1.getWidth() / 7))));
    }
    transpose_.setBounds(row2.removeFromLeft(130));
    row2.removeFromLeft(4);
    notesPlayKeys_.setBounds(row2.removeFromLeft(160));
    keyNotesToFw_.setBounds(row2.removeFromLeft(225));
    row2.removeFromLeft(4);
    themes_.setBounds(row2.removeFromLeft(150));
    row2.removeFromLeft(4);
    bigLcd_.setBounds(row2.removeFromLeft(70));
    row2.removeFromLeft(8);
    status_.setBounds(row2);
    panel_.setBounds(r);
    if (themeEditor_) {
        const int w = juce::jmin(960, r.getWidth() - 20), h = juce::jmin(320, r.getHeight() - 20);
        themeEditor_->setBounds(r.withSizeKeepingCentre(w, h));
    }
    proc_.setEditorSize(getWidth(), getHeight());
}

void FM1Editor::setBigLcd(bool on)
{
    panel_.setBigLcd(on);
    proc_.setBigLcd(on);
    bigLcd_.setToggleState(on, juce::dontSendNotification);
    if (!on)                                     // back to the panel's aspect
        setSize(getWidth(), kBarH + juce::roundToInt((float)getWidth() / PanelComponent::frameAspect(false)));
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

// presets, then the custom themes, then the state's own theme when no file has it, then "Custom..."
void FM1Editor::refreshThemes()
{
    const Theme cur = proc_.getTheme();
    themeItems_.clear();
    themes_.clear(juce::dontSendNotification);
    int sel = 0;
    auto add = [&](const Theme &t, const juce::String &text) {
        themeItems_.push_back(t);
        const int id = (int)themeItems_.size();
        themes_.addItem(text, id);
        if (!sel && t.name.equalsIgnoreCase(cur.name))
            sel = id;
    };
    for (const Theme &t : ThemeStore::presets())
        add(t, t.name);
    const std::vector<Theme> customs = store_.customs();
    if (!customs.empty())
        themes_.addSeparator();
    for (const Theme &t : customs)
        add(t, t.name);
    if (!sel) {                                  // (a custom theme whose file is gone: the state's colours)
        themes_.addSeparator();
        add(cur, cur.name + " (this set)");
    }
    themes_.addSeparator();
    themes_.addItem("Custom...", 10000);
    themes_.setSelectedId(sel, juce::dontSendNotification);
}

void FM1Editor::applyTheme(const Theme &t)
{
    proc_.setTheme(t);
    themeSerial_ = proc_.themeSerial();
    panel_.setTheme(t);
}

void FM1Editor::openThemeEditor()
{
    if (!themeEditor_) {
        themeEditor_ = std::make_unique<ThemeEditorPanel>();
        themeEditor_->onChange = [this](const Theme &t) { applyTheme(t); };
        themeEditor_->onSave = [this](const Theme &t, bool asDefault) {
            if (t.name.isEmpty()) {
                themeEditor_->setNote("give the theme a name");
                return false;
            }
            const auto p = ThemeStore::preset(t.name);
            if (p && !p->sameColours(t)) {
                themeEditor_->setNote("\"" + t.name + "\" is a preset: pick another name");
                return false;
            }
            if (!p && !store_.save(t)) {
                themeEditor_->setNote("cannot write " + store_.fileFor(t.name).getFullPathName());
                return false;
            }
            if (asDefault)
                store_.setDefaultName(t.name);
            applyTheme(t);
            refreshThemes();
            themeEditor_->setNote(asDefault ? "saved; the default for new instances" : "saved to " + store_.fileFor(t.name).getFullPathName());
            return true;
        };
        themeEditor_->onDelete = [this](const juce::String &n) {
            themeEditor_->setNote(store_.remove(n) ? "deleted \"" + n + "\" (this instance keeps its colours)"
                                                   : "\"" + n + "\" is not a custom theme file");
            refreshThemes();
        };
        themeEditor_->onClose = [this] {
            juce::Component::SafePointer<FM1Editor> self(this);
            juce::MessageManager::callAsync([self] {
                if (self) {
                    self->themeEditor_.reset();
                    self->panel_.grabKeyboardFocus();
                }
            });
        };
        addAndMakeVisible(*themeEditor_);
    }
    Theme t = proc_.getTheme();
    if (ThemeStore::isPresetName(t.name))
        t.name = "My " + t.name.replace("/", " ");
    themeEditor_->setTheme(t);
    themeEditor_->setNote("changes preview live; Save writes " + store_.themesDir().getFullPathName() + "/<name>.json");
    resized();
    themeEditor_->toFront(false);
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
    if (juce::String(s.info.id) != lastCore_) {  // (a core switch: its button labels)
        lastCore_ = juce::String(s.info.id);
        panel_.setButtonNames(proc_.buttonNames());
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
    if (proc_.themeSerial() != themeSerial_) {   // (a state restore set another theme)
        themeSerial_ = proc_.themeSerial();
        panel_.setTheme(proc_.getTheme());
        refreshThemes();
    }
    if (proc_.getBigLcd() != panel_.bigLcd())
        setBigLcd(proc_.getBigLcd());
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
