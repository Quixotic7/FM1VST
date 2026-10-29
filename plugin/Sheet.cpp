// SPDX-License-Identifier: GPL-3.0-only
// The settings sheet (see Sheet.h).
#include "EditorPort.h"
#include "Sheet.h"

#include "Editor.h"
#include "StandaloneHooks.h"
#include "VideoCapture.h"

#include <algorithm>
#include <optional>

using juce::Colour;
using juce::Rectangle;
using juce::String;

static String u8(const std::string &s) { return String::fromUTF8(s.c_str()); }
static Colour C(juce::uint32 c) { return Colour(c); }

// --------------------------------------------------------- the catalogue ---
const juce::StringArray &FirmwareMeta::categories()
{
    static const juce::StringArray c{"All", "Synth", "Groovebox", "Chords", "Drums", "Other"};
    return c;
}

FirmwareMeta FirmwareMeta::of(const String &id, const String &sourceUrl)
{
    struct E {
        const char *id, *cat, *tag, *author;
    };
    static const E table[] = {
        {"choralroot", "Chords", "Chord instrument", "Quixotic7"},
        {"coralflower", "Chords", "Chords, a flower grows", "FicusFicusov"},
        {"fomni", "Chords", "Chord harp", "C. Vestal"},
        {"felucca", "Synth", "Multi-engine synth", "H\xc3\xbcgelton"},
        {"melodee", "Synth", "Multi-engine synth", "keremimo"},
        {"ghoulbox", "Synth", "Dungeon synth", "J. Persinger"},
        {"duell", "Synth", "Bitimbral synth", "OSCILL/R"},
        {"obervave", "Synth", "8-voice poly synth", "zvenson"},
        {"s1vave", "Synth", "8-voice analog tweak synth", "Andrew GG"},
        {"frodhes", "Synth", "Electric piano", "L. Mirabal"},
        {"fm106", "Synth", "Juno-106 style poly", "PatttF"},
        {"fimba", "Synth", "Kalimba", "J. Sowers"},
        {"jiant", "Groovebox", "Bio-synthetic OS", "juanjiant"},
        {"sloop", "Groovebox", "Live groovebox", "isod89"},
        {"orbit", "Groovebox", "Live groovebox", "dspaudio"},
        {"sloopdx", "Drums", "FM drums and a DX7", "zvenson"},
        {"x0x", "Drums", "909, 808 and 303s", "C. Vestal"},
        {"fartmachine", "Other", "Sampler toy", "A. C. Wilson"},
        {"amb", "Other", "Generative ambient", "C. Vestal"},
        {"tonitor", "Other", "Acousmatic, live ambient", "N. Zimas"},
        {"purplemonkey", "Other", "For a small child", "Quixotic7"},
        {"rainbow", "Other", "Music toy", "J. Bartlett"},
        {"nomansky", "Other", "Space adventure", "Hericdk"},
        {"minitone", "Synth", "Six-track multi-engine", "S. Schmitz"},
        {"fmini", "Synth", "Three-oscillator mono", "ruppert"},
        {"skiff", "Groovebox", "Six-track groovebox", "illuminus5"},
        {"grimm", "Synth", "Dark ambient", "GRiMMBIENT"},
        {"loom", "Groovebox", "Pocket groovebox", "C. Vestal"},
        {"pigeon", "Groovebox", "Performance sampler", "The Shrimp Blimp"},
        {"speak37", "Other", "Talking synth", "M. Geisler"},
    };
    for (const E &e : table)
        if (id == e.id)
            return {e.cat, e.tag, String::fromUTF8(e.author)};
    String owner = sourceUrl.fromFirstOccurrenceOf("github.com/", false, true).upToFirstOccurrenceOf("/", false, false);
    return {"Other", "Firmware", owner.isNotEmpty() ? owner : String("unknown")};
}

// ===================================================== the sheet's parts ===
namespace {
constexpr int kHeaderH = 54;

// the pills of a group, `cols` to a row
class PillGrid : public Widget {
public:
    PillGrid(Look &l, int cols) : Widget(l), cols_(cols) {}
    Pill &add(const String &text, Look::PillKind k = Look::Plain)
    {
        pills_.push_back(std::make_unique<Pill>(look, text, k));
        addAndMakeVisible(*pills_.back());
        return *pills_.back();
    }
    int heightFor(int) const override
    {
        const int rows = ((int)pills_.size() + cols_ - 1) / cols_;
        return rows * 32 + (rows - 1) * 8 + 12;
    }
    void resized() override
    {
        const int w = (getWidth() - (cols_ - 1) * 8) / cols_;
        for (size_t i = 0; i < pills_.size(); i++) {
            const int c = (int)i % cols_, r = (int)i / cols_;
            pills_[i]->setBounds(c * (w + 8), 4 + r * 40, c == cols_ - 1 ? getWidth() - c * (w + 8) : w, 32);
        }
    }

private:
    int cols_;
    std::vector<std::unique_ptr<Pill>> pills_;
};

// a name typed in place: the field, the confirming pill, Cancel (Return / Esc)
class InlineEdit : public Widget {
public:
    explicit InlineEdit(Look &l) : Widget(l), ok_(l, "Save", Look::Primary), cancel_(l, "Cancel")
    {
        Look::styleTextEditor(field_);
        field_.setTextToShowWhenEmpty("Preset name", C(Look::C::muted));
        field_.onReturnKey = [this] { commit(); };
        field_.onEscapeKey = [this] { cancel(); };
        ok_.onClick = [this] { commit(); };
        cancel_.onClick = [this] { cancel(); };
        for (juce::Component *c : std::initializer_list<juce::Component *>{&field_, &ok_, &cancel_})
            addAndMakeVisible(*c);
    }
    void begin(const String &okText, const String &initial)
    {
        ok_.setText(okText);
        field_.setText(initial, false);
        field_.selectAll();
        setVisible(true);
        resized();
        if (field_.isShowing())
            field_.grabKeyboardFocus();
    }
    std::function<void(String)> onCommit;
    std::function<void()> onCancel;
    int heightFor(int) const override { return 48; }
    void resized() override
    {
        Rectangle<int> r = getLocalBounds().withTrimmedTop(4).withHeight(36);
        cancel_.setBounds(r.removeFromRight(72).reduced(0, 2));
        r.removeFromRight(6);
        ok_.setBounds(r.removeFromRight(juce::jmax(64, ok_.preferredWidth())).reduced(0, 2));
        r.removeFromRight(8);
        field_.setBounds(r);
    }
    juce::TextEditor &field() { return field_; }

private:
    void commit()
    {
        const String n = field_.getText().trim();
        if (n.isEmpty())
            return;
        if (onCommit)
            onCommit(n);
    }
    void cancel()
    {
        if (onCancel)
            onCancel();
    }
    juce::TextEditor field_;
    Pill ok_, cancel_;
};

// a destructive action asked again, in place, in red
class Confirm : public Widget {
public:
    explicit Confirm(Look &l) : Widget(l), msg_(l, 13.0f, Look::C::text), yes_(l, "Delete", Look::DangerFill), no_(l, "Cancel")
    {
        msg_.setPadding(0, 0);
        yes_.onClick = [this] {
            if (onYes)
                onYes();
        };
        no_.onClick = [this] {
            if (onNo)
                onNo();
        };
        addAndMakeVisible(msg_);
        addAndMakeVisible(yes_);
        addAndMakeVisible(no_);
    }
    void begin(const String &message, const String &yesText)
    {
        msg_.setText(message, C(Look::C::text));
        yes_.setText(yesText);
        setVisible(true);
    }
    std::function<void()> onYes, onNo;
    int heightFor(int w) const override { return msg_.heightFor(w - 28) + 32 + 34; }
    void paint(juce::Graphics &g) override
    {
        const Rectangle<float> r = getLocalBounds().toFloat().reduced(0, 4);
        g.setColour(C(Look::C::red).withAlpha(0.08f));
        g.fillRoundedRectangle(r, 10);
        g.setColour(C(Look::C::red).withAlpha(0.45f));
        g.drawRoundedRectangle(r.reduced(0.5f), 10, 1);
    }
    void resized() override
    {
        Rectangle<int> r = getLocalBounds().reduced(14, 4).withTrimmedTop(10).withTrimmedBottom(10);
        Rectangle<int> b = r.removeFromBottom(32);
        msg_.setBounds(r.withTrimmedBottom(8));
        no_.setBounds(b.removeFromRight(80));
        b.removeFromRight(8);
        yes_.setBounds(b.removeFromRight(juce::jmax(96, yes_.preferredWidth())));
    }

private:
    Text msg_;
    Pill yes_, no_;
};

// ‹ the current preset › (the name: the list)
class PresetNav : public Widget {
public:
    using Widget::Widget;
    void setPresetName(const String &n)
    {
        if (n != name_) {
            name_ = n;
            repaint();
        }
    }
    std::function<void(int)> onStep;              // -1, +1
    std::function<void()> onList;
    int heightFor(int) const override { return 44; }
    void paint(juce::Graphics &g) override
    {
        const Rectangle<float> r = getLocalBounds().toFloat().reduced(0, 4);
        g.setColour(C(Look::C::raised));
        g.fillRoundedRectangle(r, 9);
        g.setColour(C(Look::C::line));
        g.drawRoundedRectangle(r.reduced(0.5f), 9, 1);
        const Rectangle<float> l = r.withWidth(34), rr = r.withLeft(r.getRight() - 34);
        for (int i = 0; i < 2; i++) {
            const Rectangle<float> z = i ? rr : l;
            if (zone_ == (i ? 2 : 0)) {
                g.setColour(C(Look::C::raisedHover));
                g.fillRoundedRectangle(z.reduced(3), 7);
            }
            Look::strokeIcon(g, i ? Look::ChevronRight : Look::ChevronLeft, z.withSizeKeepingCentre(14, 14),
                             C(zone_ == (i ? 2 : 0) ? Look::C::text : Look::C::knobOff), 1.7f);
        }
        Rectangle<float> mid = r.reduced(34, 0);
        if (zone_ == 1) {
            g.setColour(C(Look::C::raisedHover));
            g.fillRoundedRectangle(mid.reduced(0, 3), 7);
        }
        g.setFont(Look::font(14.5f, Look::SemiBold));
        g.setColour(name_.isEmpty() ? C(Look::C::muted) : C(Look::C::text));
        g.drawFittedText(name_.isEmpty() ? String("No preset") : name_, mid.reduced(22, 0).toNearestInt(), juce::Justification::centred, 1,
                         0.8f);
        Look::strokeIcon(g, Look::ChevronDown, Rectangle<float>(mid.getRight() - 20, mid.getCentreY() - 6, 12, 12),
                         C(Look::C::muted), 1.6f);
    }
    void mouseMove(const juce::MouseEvent &e) override
    {
        const int z = e.x < 34 ? 0 : e.x >= getWidth() - 34 ? 2 : 1;
        if (z != zone_) {
            zone_ = z;
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent &) override
    {
        zone_ = -1;
        repaint();
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!e.mouseWasClicked())
            return;
        if (zone_ == 1 && onList)
            onList();
        else if ((zone_ == 0 || zone_ == 2) && onStep)
            onStep(zone_ == 0 ? -1 : 1);
    }

private:
    String name_;
    int zone_ = -1;
};

// the loaded firmware (main page): name, version, its tag; a chevron to the browser
class FirmwareCard : public Widget {
public:
    using Widget::Widget;
    void set(const String &name, const String &version, const String &meta, Colour metaColour)
    {
        if (name == name_ && version == version_ && meta == meta_ && metaColour == metaColour_)
            return;
        name_ = name, version_ = version, meta_ = meta, metaColour_ = metaColour;
        repaint();
    }
    std::function<void()> onClick;
    int heightFor(int) const override { return 66; }
    void paint(juce::Graphics &g) override
    {
        const Rectangle<float> r = getLocalBounds().toFloat().reduced(0, 4);
        g.setColour(C(hover_ ? Look::C::raisedHover : Look::C::raised));
        g.fillRoundedRectangle(r, 10);
        g.setColour(C(Look::C::line));
        g.drawRoundedRectangle(r.reduced(0.5f), 10, 1);
        Rectangle<float> in = r.reduced(14, 9);
        Look::strokeIcon(g, Look::ChevronRight, in.removeFromRight(14).withSizeKeepingCentre(14, 14), C(Look::C::knobOff), 1.7f);
        Rectangle<float> top = in.removeFromTop(in.getHeight() * 0.55f);
        Look::drawLed(g, {top.getX() + 4, top.getCentreY() + 1}, 3.5f, look.accent());
        top.removeFromLeft(15);
        const juce::Font nf = Look::font(15.5f, Look::Bold);
        g.setFont(nf);
        g.setColour(C(Look::C::text));
        const float nw = juce::jmin(top.getWidth() * 0.7f, Look::textWidth(nf, name_) + 2);
        g.drawText(name_, top.removeFromLeft(nw), juce::Justification::centredLeft, true);
        g.setFont(Look::font(13.0f, Look::Medium));
        g.setColour(C(Look::C::muted));
        g.drawText(version_, top.withTrimmedLeft(7), juce::Justification::centredLeft, true);
        g.setFont(Look::font(12.0f, Look::Medium));
        g.setColour(metaColour_);
        g.drawFittedText(meta_, in.withTrimmedLeft(15).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }

private:
    String name_, version_, meta_;
    Colour metaColour_;
};

// the themes as swatches (the base and the membrane, split on the diagonal), the current one ringed; "+" a custom one
class Swatches : public Widget {
public:
    using Widget::Widget;
    void set(std::vector<Theme> themes, int sel)
    {
        themes_ = std::move(themes);
        sel_ = sel;
        repaint();
    }
    std::function<void(int)> onPick;
    std::function<void()> onCustom;
    std::function<void(String)> onHoverName;
    int heightFor(int w) const override
    {
        const int per = juce::jmax(1, (w + kGap) / (kS + kGap)), n = (int)themes_.size() + 1;
        return ((n + per - 1) / per) * (kS + kGap) + 6;
    }
    Rectangle<float> tile(int i) const
    {
        const int per = juce::jmax(1, (getWidth() + kGap) / (kS + kGap));
        return Rectangle<float>((float)((i % per) * (kS + kGap)), (float)(2 + (i / per) * (kS + kGap)), (float)kS, (float)kS);
    }
    void paint(juce::Graphics &g) override
    {
        for (int i = 0; i <= (int)themes_.size(); i++) {
            const Rectangle<float> t = tile(i).reduced(2.5f);
            if (i == (int)themes_.size()) {   // "+"
                g.setColour(C(hoverI_ == i ? Look::C::raisedHover : Look::C::raised));
                g.fillRoundedRectangle(t, 7);
                g.setColour(C(Look::C::line));
                g.drawRoundedRectangle(t.reduced(0.5f), 7, 1);
                const juce::Point<float> c = t.getCentre();
                g.setColour(C(hoverI_ == i ? Look::C::text : Look::C::knobOff));
                g.drawLine(c.x - 5, c.y, c.x + 5, c.y, 1.6f);
                g.drawLine(c.x, c.y - 5, c.x, c.y + 5, 1.6f);
                continue;
            }
            const Theme &th = themes_[(size_t)i];
            juce::Path clip;
            clip.addRoundedRectangle(t, 7);
            {
                juce::Graphics::ScopedSaveState s(g);
                g.reduceClipRegion(clip);
                g.setColour(th.base);
                g.fillRect(t);
                juce::Path tri;
                tri.addTriangle(t.getRight(), t.getY(), t.getRight(), t.getBottom(), t.getX(), t.getBottom());
                g.setColour(th.membrane);
                g.fillPath(tri);
                g.setColour(th.knob);
                g.fillEllipse(Rectangle<float>(7, 7).withCentre(t.getCentre()));
            }
            g.setColour(juce::Colours::white.withAlpha(0.14f));
            g.strokePath(clip, juce::PathStrokeType(1.0f));
            if (i == sel_ || i == hoverI_) {
                g.setColour(i == sel_ ? juce::Colours::white : C(Look::C::muted));
                g.drawRoundedRectangle(t.expanded(2.5f), 9, i == sel_ ? 2.0f : 1.0f);
            }
        }
    }
    void mouseMove(const juce::MouseEvent &e) override
    {
        int h = -1;
        for (int i = 0; i <= (int)themes_.size(); i++)
            if (tile(i).contains(e.position))
                h = i;
        if (h != hoverI_) {
            hoverI_ = h;
            repaint();
            if (onHoverName)
                onHoverName(h < 0 ? String() : h == (int)themes_.size() ? String("Custom theme...") : themes_[(size_t)h].name);
        }
    }
    void mouseExit(const juce::MouseEvent &) override
    {
        hoverI_ = -1;
        repaint();
        if (onHoverName)
            onHoverName({});
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!e.mouseWasClicked() || hoverI_ < 0)
            return;
        if (hoverI_ == (int)themes_.size()) {
            if (onCustom)
                onCustom();
        } else if (onPick) {
            onPick(hoverI_);
        }
    }

private:
    static constexpr int kS = 32, kGap = 4;
    std::vector<Theme> themes_;
    int sel_ = -1, hoverI_ = -1;
};

// a key and its value (the about page)
class KeyValue : public Widget {
public:
    KeyValue(Look &l, String key) : Widget(l), key_(std::move(key)), value_(l, 13.0f, Look::C::rowText)
    {
        value_.setPadding(0, 0);
        addAndMakeVisible(value_);
        setInterceptsMouseClicks(false, false);
    }
    void setValue(const String &v) { value_.setText(v, C(Look::C::rowText)); }
    int heightFor(int w) const override { return juce::jmax(24, value_.heightFor(w - kKeyW)) + 8; }
    void paint(juce::Graphics &g) override
    {
        g.setFont(Look::font(12.5f, Look::Medium));
        g.setColour(C(Look::C::muted));
        g.drawText(key_, Rectangle<int>(0, 4, kKeyW - 8, 18), juce::Justification::centredLeft, true);
    }
    void resized() override { value_.setBounds(kKeyW, 5, getWidth() - kKeyW, getHeight() - 5); }

private:
    static constexpr int kKeyW = 92;
    String key_;
    Text value_;
};

// any component as a stack item, at a fixed height
class Holder : public Widget {
public:
    Holder(Look &l, juce::Component &c, int h) : Widget(l), c_(c), h_(h) { addAndMakeVisible(c_); }
    void setHeight(int h) { h_ = h; }
    int heightFor(int) const override { return h_; }
    void resized() override { c_.setBounds(getLocalBounds().reduced(0, 4)); }

private:
    juce::Component &c_;
    int h_;
};

// a page: a stack in a scrolling viewport
class ScrollPage : public juce::Component {
public:
    explicit ScrollPage(Look &)
    {
        vp_.setViewedComponent(&stack_, false);
        vp_.setScrollBarsShown(true, false);
        vp_.setScrollBarThickness(8);
        vp_.setWantsKeyboardFocus(false);
        addAndMakeVisible(vp_);
    }
    void resized() override { relayout(); }
    void relayout()
    {
        vp_.setBounds(getLocalBounds());
        const int w = vp_.getWidth() - 4;
        const int h = stack_.layout(w) + 18;
        stack_.setSize(w, h);
    }

protected:
    juce::Viewport vp_;
    Stack stack_;
};

// the VIEW section's Screen row: Copy, and the scale (its menu: the scale, Save as PNG, the raw frame, Freeze)
class ScreenCopyControls : public juce::Component {
public:
    explicit ScreenCopyControls(Look &l) : copy(l, "Copy", Look::Primary), scale(l, String::fromUTF8("\xc3\x97" "1"))
    {
        addAndMakeVisible(copy);
        addAndMakeVisible(scale);
        scale.setTooltip("The copy's scale (nearest-neighbour), and more: Save as PNG, the raw frame, Freeze");
    }
    void resized() override
    {
        Rectangle<int> r = getLocalBounds();
        scale.setBounds(r.removeFromRight(kScaleW));
        r.removeFromRight(8);
        copy.setBounds(r);
    }
    static constexpr int kCopyW = 72, kScaleW = 58, kW = kCopyW + 8 + kScaleW;
    Pill copy, scale;
};
}   // namespace

// =============================================================== main ===
class Sheet::MainPage : public ScrollPage {
public:
    MainPage(Sheet &s, FM1Editor &ed);
    void refresh(bool full = true);               // (not full: the theme files are not read again)
    void beginRename();
    void beginSaveAs();
    void beginDeleteConfirm();
    void beginResetConfirm();
    void endInline();
    void scrollToView()                           // (tests: the View section at the top of the view)
    {
        endInline();
        refresh(false);
        vp_.setViewPosition(0, juce::jmax(0, hView_.getY() - 8));
    }
    void scrollToEditors()                        // (tests: the Editors section at the top of the view)
    {
        endInline();
        refresh(false);
        vp_.setViewPosition(0, juce::jmax(0, hEditors_.getY() - 8));
    }
    void scrollToVideo()                          // (tests: the Video section at the top of the view)
    {
        endInline();
        refresh(false);
        vp_.setViewPosition(0, juce::jmax(0, hVideo_.getY() - 8));
    }

private:
    void refreshVideo();
    void setVideo(const std::function<void(fm1video::Settings &)> &change);
    void showInputMenu();
    String inputLabel() const;
    void refreshPresets();
    void refreshThemes();
    void loadPresetAt(int i);
    void showPresetList();
    void exportPreset();
    void importPreset();
    void showClockMenu();
    void showScreenMenu();

    Sheet &sheet_;
    FM1Editor &ed_;
    FM1Processor &p_;
    Look &look_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::StringArray presets_;
    std::vector<Theme> themeItems_;

    Heading hFirmware_{look_, "Firmware", true};
    FirmwareCard firmware_{look_};
    Text emuStatus_{look_, 12.0f, Look::C::muted};
    Segmented engineSeg_{look_, {"Native", "Emulated"}};
    Row engine_{look_, "Engine", &engineSeg_, 170, 30};
    Pill clockP_{look_, "Auto"};
    Row clock_{look_, "Emulated clock", &clockP_, 150, 30};
    Row source_{look_, "Source"};
    String sourceUrl_;
    Heading hPreset_{look_, "Preset"};
    PresetNav presetNav_{look_};
    InlineEdit edit_{look_};
    Confirm confirmDelete_{look_};
    PillGrid presetButtons_{look_, 3};
    Pill *save_ = nullptr, *saveAs_ = nullptr, *rename_ = nullptr, *delete_ = nullptr, *export_ = nullptr, *import_ = nullptr;
    int editMode_ = 0;                            // 1 save as, 2 rename

    Heading hRemote_{look_, "Remote FM-1"};
    Switch remoteSw_{look_};
    Row remote_{look_, "Remote FM-1", &remoteSw_, Switch::kW, 24};
    Text remoteStatus_{look_, 13.0f, Look::C::rowText};
    Text remoteDiag_{look_, 11.5f, Look::C::muted};
    Switch ignoreSw_{look_};
    Row ignore_{look_, "Ignore the FM-1's own MIDI", &ignoreSw_, Switch::kW, 24};

    Heading hEditors_{look_, "Editors"};
    Switch editorSw_{look_};
    Row editorPort_{look_, "Show to editors", &editorSw_, Switch::kW, 24};
    Text editorStatus_{look_, 13.0f, Look::C::rowText};
    Text editorHint_{look_, 11.5f, Look::C::muted};
    Row editorLink_{look_, "Editor"};
    String editorUrl_;

    Heading hPlaying_{look_, "Playing"};
    Stepper transposeSt_{look_, -2, 2, [](int v) { return v == 0 ? String("0") : String::formatted("%+d", v); }};
    Row transpose_{look_, "Transpose", &transposeSt_, Stepper::kW, 28};
    Switch notesSw_{look_};
    Row notes_{look_, "MIDI notes play keys", &notesSw_, Switch::kW, 24};
    Switch toFwSw_{look_};
    Row toFw_{look_, String::fromUTF8("\xe2\x80\xa6" "and reach the firmware's MIDI in"), &toFwSw_, Switch::kW, 24};

    Heading hHost_{look_, "Host control"};
    Segmented buttonModeSeg_{look_, {"Tap", "Hold", "Toggle"}};
    Row buttonMode_{look_, "Button parameters", &buttonModeSeg_, 150, 30};
    Segmented encModeSeg_{look_, {"Absolute", "Stepped"}};
    Row encMode_{look_, "Encoders", &encModeSeg_, 150, 30};
    Stepper stepsSt_{look_, FM1Processor::kMinEncSteps, FM1Processor::kMaxEncSteps, [](int v) { return String(v); }};
    Row steps_{look_, "Steps per turn", &stepsSt_, Stepper::kW, 28};

    Heading hLook_{look_, "Look"};
    Row themeRow_{look_, "Theme"};
    Swatches swatches_{look_};

    Heading hView_{look_, "View"};
    Segmented viewSeg_{look_, {"Device", "Screen"}};
    Row view_{look_, "Show", &viewSeg_, 150, 30};
    Switch bigSw_{look_};
    Row big_{look_, "Big LCD above the panel", &bigSw_, Switch::kW, 24};
    Switch hintsSw_{look_};
    Row hints_{look_, "Show keyboard shortcuts", &hintsSw_, Switch::kW, 24};
    ScreenCopyControls screenCtl_{look_};
    Row screen_{look_, "Screen", &screenCtl_, ScreenCopyControls::kW, 30};

    // VIDEO (the Standalone only: VideoCapture.h)
    fm1video::Session *video_ = nullptr;
    Heading hVideo_{look_, "Video"};
    Pill recP_{look_, "Record", Look::Primary};
    Row record_{look_, "Record", &recP_, 96, 30};
    Segmented vViewSeg_{look_, {"Device", "Screen", "LCD"}};
    Row vView_{look_, "View", &vViewSeg_, 200, 30};
    Segmented vSizeSeg_{look_, {"720p", "1080p"}};
    Row vSize_{look_, "Size", &vSizeSeg_, 150, 30};
    Segmented vFpsSeg_{look_, {"30", "60"}};
    Row vFps_{look_, "Frame rate", &vFpsSeg_, 110, 30};
    Segmented vFmtSeg_{look_, {"MP4", "MOV"}};
    Row vFmt_{look_, "Format", &vFmtSeg_, 120, 30};
    Switch micSw_{look_};
    Row mic_{look_, "Mic / line input", &micSw_, Switch::kW, 24};
    Pill inputP_{look_, "Input 1"};
    Row input_{look_, "Input", &inputP_, 150, 30};
    Stepper gainSt_{look_, -24, 24, [](int v) { return (v > 0 ? String("+") : String()) + String(v) + " dB"; }};
    Row gain_{look_, "Mic gain", &gainSt_, Stepper::kW, 28};
    Segmented trackSeg_{look_, {"Separate", "Mix"}};
    Row track_{look_, "Mic track", &trackSeg_, 150, 30};
    Switch monSw_{look_};
    Row monitor_{look_, "Monitor the input", &monSw_, Switch::kW, 24};
    Text videoNote_{look_, 11.5f, Look::C::muted};
    Row lastTake_{look_, "Last take"};
    String micNote_;

    Heading hDevice_{look_, "Device"};
    Pill restartP_{look_, "Restart"};
    Row power_{look_, "Power", &restartP_, 96, 30};
    Pill resetP_{look_, String::fromUTF8("Reset\xe2\x80\xa6"), Look::Danger};
    Row reset_{look_, "Flash", &resetP_, 96, 30};
    Confirm confirmReset_{look_};

    Heading hStandalone_{look_, "Standalone"};
    Row audio_{look_, String::fromUTF8("Audio/MIDI settings\xe2\x80\xa6")};
    Row saveState_{look_, String::fromUTF8("Save state\xe2\x80\xa6")};
    Row loadState_{look_, String::fromUTF8("Load state\xe2\x80\xa6")};
    Row resetState_{look_, "Reset to default state"};
    Confirm confirmApp_{look_};

    Heading hAbout_{look_, "About"};
    Row about_{look_, "About"};
    Text credit_{look_, 11.5f, Look::C::heading};

    String hoverTheme_;
};

Sheet::MainPage::MainPage(Sheet &s, FM1Editor &ed) : ScrollPage(ed.look()), sheet_(s), ed_(ed), p_(ed.proc()), look_(ed.look())
{
    // FIRMWARE
    stack_.add(hFirmware_);
    stack_.add(firmware_);
    firmware_.onClick = [this] { sheet_.showPage(kFirmware); };
    stack_.add(emuStatus_);
    stack_.add(engine_);
    engine_.setControlWidth(engineSeg_.preferredWidth());
    engine_.setTooltip("This firmware runs two ways. Native: FM1VST's core, compiled from its source for this computer: "
                       "light on the CPU. Emulated: its own FM-1 package from the Firmware Hub on fm1-emu, the FM-1's chip "
                       "emulated instruction by instruction: the Hub's latest version, heavier on the CPU. Each keeps its "
                       "own flash.");
    engineSeg_.onChange = [this](int i) {
        const FM1Processor::EmuFirmware fw = p_.getEmuFirmware();
        const String key = fw.valid() ? fw.id : hub::hubIdForCore(u8(p_.getCoreInfo().info.id));
        sheet_.pickInBrowser(key, i == 0 ? 1 : 2);
    };
    stack_.add(clock_);
    clock_.setTooltip("The emulated FM-1's chip clock (the real one runs at about 320 to 360 MHz). Auto starts at 360 and "
                      "steps down (320, 240, 180, 120) when this computer can't keep up; 480 and 640 are an overclock, "
                      "more headroom for heavy firmwares. A change restarts the firmware with its flash kept. Saved with "
                      "the set.");
    clockP_.onClick = [this] { showClockMenu(); };
    stack_.add(source_);
    source_.onClick = [this] {
        if (sourceUrl_.isNotEmpty())
            juce::URL(sourceUrl_).launchInDefaultBrowser();
    };

    // PRESET
    stack_.add(hPreset_);
    stack_.add(presetNav_);
    presetNav_.onStep = [this](int d) {
        const int i = presets_.indexOf(p_.currentPresetName());
        if (presets_.isEmpty())
            return;
        loadPresetAt(i < 0 ? (d > 0 ? 0 : presets_.size() - 1) : (i + d + presets_.size()) % presets_.size());
    };
    presetNav_.onList = [this] { showPresetList(); };
    stack_.add(edit_);
    edit_.setVisible(false);
    edit_.onCommit = [this](String n) {
        if (editMode_ == 2) {
            const String from = p_.currentPresetName();
            if (from.isNotEmpty() && n != from)
                p_.renamePreset(from, n);
        } else {
            p_.savePreset(n);
        }
        endInline();
        refreshPresets();
    };
    edit_.onCancel = [this] { endInline(); };
    stack_.add(confirmDelete_);
    confirmDelete_.setVisible(false);
    confirmDelete_.onYes = [this] {
        const String n = p_.currentPresetName();
        if (n.isNotEmpty())
            p_.deletePreset(n);
        endInline();
        refreshPresets();
    };
    confirmDelete_.onNo = [this] { endInline(); };
    stack_.add(presetButtons_);
    save_ = &presetButtons_.add("Save");
    saveAs_ = &presetButtons_.add("Save as");
    rename_ = &presetButtons_.add("Rename");
    delete_ = &presetButtons_.add("Delete", Look::Danger);
    export_ = &presetButtons_.add(String::fromUTF8("Export\xe2\x80\xa6"));
    import_ = &presetButtons_.add(String::fromUTF8("Import\xe2\x80\xa6"));
    save_->setTooltip("Save the whole flash under the current preset's name");
    save_->onClick = [this] {
        const String n = p_.currentPresetName();
        if (n.isEmpty()) {
            beginSaveAs();
        } else {
            p_.savePreset(n);
            refreshPresets();
        }
    };
    saveAs_->onClick = [this] { beginSaveAs(); };
    rename_->onClick = [this] { beginRename(); };
    delete_->onClick = [this] { beginDeleteConfirm(); };
    export_->onClick = [this] { exportPreset(); };
    import_->onClick = [this] { importPreset(); };

    // REMOTE FM-1
    stack_.add(hRemote_);
    stack_.add(remote_);
    remote_.setTooltip("Remote FM-1: a real FM-1 running the RemoteFM1 firmware, on USB or Bluetooth MIDI, becomes this panel: its keys, "
                       "buttons, knobs and MASTER play the firmware here, and this screen and its LEDs show on the unit. One "
                       "FM1VST at a time drives the unit (another instance or the Standalone shows \"in use\" and takes over "
                       "when that one lets go). Saved with the set.");
    remoteSw_.onChange = [this](bool on) {
        p_.setRemoteEnabled(on);
        refresh();
    };
    stack_.add(remoteStatus_);
    stack_.add(remoteDiag_);
    stack_.add(ignore_);
    ignore_.setCaption("its notes and knobs on the DAW's MIDI input (Live's All Ins)");
    ignoreSw_.onChange = [this](bool on) { p_.setRemoteIgnoreMidi(on); };

    // EDITORS (Show to editors: EditorPort.h; macOS)
    const bool ports = EditorPort::available();
    for (Widget *w : std::initializer_list<Widget *>{&hEditors_, &editorPort_, &editorStatus_, &editorHint_, &editorLink_}) {
        stack_.add(*w);
        w->setVisible(ports);
    }
    editorPort_.setTooltip("A MIDI port named like this firmware's own USB port, so its web editor or preset manager (in "
                           "Chrome) finds this FM1VST as if it were an FM-1: back up and restore presets, edit sounds. The "
                           "editor's messages go straight to the firmware and its answers straight back, never through "
                           "the DAW; the firmware keeps running for the editor while the host is stopped. Saved with the set.");
    editorSw_.onChange = [this](bool on) {
        p_.setEditorPortEnabled(on);
        refresh();
    };
    editorLink_.onClick = [this] {
        if (editorUrl_.isNotEmpty())
            juce::URL(editorUrl_).launchInDefaultBrowser();
    };

    // PLAYING
    stack_.add(hPlaying_);
    stack_.add(transpose_);
    transpose_.setCaption("octaves");
    transposeSt_.onChange = [this](int v) { p_.setTranspose(v); };
    stack_.add(notes_);
    notesSw_.onChange = [this](bool on) { p_.setMidiNotesPlayKeys(on); };
    stack_.add(toFw_);
    toFwSw_.onChange = [this](bool on) { p_.setKeyNotesToFirmware(on); };
    toFw_.setTooltip("A note that pressed a key is also handed to the firmware's MIDI in (off: ChoralRoot's CHORD channel "
                     "is 1, so the same note would also sound raw on the chord part)");

    // HOST CONTROL
    stack_.add(hHost_);
    stack_.add(buttonMode_);
    buttonModeSeg_.onChange = [this](int m) { p_.setButtonMode(m); };
    const String bt = "What the btn_* host parameters (a Roto-Control's buttons, automation) do. Tap: each press is one tap "
                      "on the device, whatever the controller sends after (toggle buttons). Hold: on = held (momentary "
                      "buttons: long presses, layer locks). Toggle: each press toggles held. The panel's own mouse and "
                      "keys are always real presses.";
    buttonMode_.setTooltip(bt);
    buttonModeSeg_.setTooltip(bt);
    buttonMode_.setControlWidth(buttonModeSeg_.preferredWidth());
    stack_.add(encMode_);
    encModeSeg_.onChange = [this](int m) {
        p_.setEncoderMode(m);
        refresh();
    };
    const String et = "What the seven encoder parameters (Knob Select .. Knob 4) take from the host. Absolute: the value of "
                      "what the knob turns now. Stepped: a controller whose knobs click in steps (a Roto-Control knob set to "
                      "24 steps): each click is one detent of the FM-1's encoder, and the firmware's value is sent back. "
                      "Knob Master (the pot) is always absolute.";
    encMode_.setTooltip(et);
    encModeSeg_.setTooltip(et);
    encMode_.setControlWidth(encModeSeg_.preferredWidth());
    stack_.add(steps_);
    stepsSt_.onChange = [this](int v) { p_.setEncoderSteps(v); };
    steps_.setTooltip("Stepped: the controller's steps per full turn of a knob (the Roto-Control's step setting)");

    // LOOK
    stack_.add(hLook_);
    stack_.add(themeRow_);
    stack_.add(swatches_);
    swatches_.onPick = [this](int i) {
        if (i >= 0 && i < (int)themeItems_.size()) {
            ed_.applyTheme(themeItems_[(size_t)i]);
            refreshThemes();
        }
    };
    swatches_.onCustom = [this] { sheet_.showPage(kTheme); };
    swatches_.onHoverName = [this](String n) {
        hoverTheme_ = n;
        themeRow_.setValue(n.isNotEmpty() ? n : p_.getTheme().name);
    };

    // VIEW
    stack_.add(hView_);
    stack_.add(view_);
    view_.setControlWidth(viewSeg_.preferredWidth());
    viewSeg_.onChange = [this](int m) {
        juce::Component::SafePointer<Sheet> alive(&sheet_);
        ed_.setViewMode(m);
        if (alive)
            refresh();
    };
    stack_.add(big_);
    big_.setCaption("also the ` key");
    bigSw_.onChange = [this](bool on) {
        ed_.setBigLcd(on);
        refresh(false);                          // (the Video section's Device size follows)
    };
    stack_.add(hints_);
    hints_.setCaption("the computer keys printed on the panel");
    hintsSw_.onChange = [this](bool on) { ed_.setKeyHints(on); };
    stack_.add(screen_);
    screen_.setCaption(String::fromUTF8("to the clipboard, pixel for pixel \xc2\xb7 \xe2\x87\xa7\xe2\x8c\x98" "C"));
    screen_.setTooltip("Copies the firmware's screen as an image (PNG and TIFF, sRGB), for an image editor: Photoshop, Figma, "
                       "Affinity, Pixelmator, Preview (File > New from Clipboard). Also: right-click the screen.");
    screenCtl_.copy.onClick = [this] { ed_.copyScreen(ed_.copyScale()); };
    screenCtl_.scale.onClick = [this] { showScreenMenu(); };

    // VIDEO (the Standalone; a plugin has none: the DAW records)
    video_ = ed_.videoAvailable() ? p_.videoSession() : nullptr;
    for (Widget *w : std::initializer_list<Widget *>{&hVideo_, &record_, &vView_, &vSize_, &vFps_, &vFmt_, &mic_, &input_, &gain_,
                                                     &track_, &monitor_, &videoNote_, &lastTake_}) {
        stack_.add(*w);
        w->setVisible(video_ != nullptr);
    }
    record_.setTooltip("A video of the firmware playing, drawn by FM1VST itself (covering or moving the window changes "
                       "nothing), with FM1VST's own sound and the mic / line input if it is on. Saved to ~/Movies/FM1VST. A "
                       "firmware switch or restart ends the take.");
    recP_.onClick = [this] {
        ed_.toggleRecording();
        refresh(false);
    };
    vView_.setTooltip("Device: the whole FM-1 as the device view draws it. Screen: the LCD in its bezel. LCD: the 240 x 240 "
                      "screen alone, nearest-neighbour (720: 3x; 1080: 4.5x)");
    vViewSeg_.onChange = [this](int i) { setVideo([i](fm1video::Settings &st) { st.view = i; }); };
    vSizeSeg_.onChange = [this](int i) { setVideo([i](fm1video::Settings &st) { st.size = i ? 1080 : 720; }); };
    vFpsSeg_.onChange = [this](int i) { setVideo([i](fm1video::Settings &st) { st.fps = i ? 60 : 30; }); };
    vFmtSeg_.onChange = [this](int i) { setVideo([i](fm1video::Settings &st) { st.mov = i == 1; }); };
    vFmt_.setTooltip("MP4 (H.264, AAC) plays everywhere; MOV for Final Cut and QuickTime. Both keep a take readable up to "
                     "its last two seconds if the app is stopped mid-take.");
    mic_.setTooltip("Records a microphone or a line input of the audio interface with the take. The first time, macOS asks "
                    "for the microphone permission. The input opens only while recording (or monitoring).");
    micSw_.onChange = [this](bool on) {
        juce::Component::SafePointer<MainPage> self(this);
        micNote_.clear();
        video_->setMic(on, [self](bool, String why) {
            if (self) {
                self->micNote_ = why;
                self->refresh(false);
            }
        });
    };
    inputP_.onClick = [this] { showInputMenu(); };
    gainSt_.onChange = [this](int v) { setVideo([v](fm1video::Settings &st) { st.micGainDb = v; }); };
    track_.setTooltip("Separate: the input is a second audio track of the file (an editor shows both). Mix: it is summed "
                      "into the main track.");
    trackSeg_.onChange = [this](int i) { setVideo([i](fm1video::Settings &st) { st.micMix = i == 1; }); };
    monitor_.setTooltip("Plays the input through the outputs. Use headphones: with speakers a microphone feeds back.");
    monSw_.onChange = [this](bool on) { setVideo([on](fm1video::Settings &st) { st.monitor = on; }); };
    lastTake_.onClick = [this] {
        if (video_ && video_->lastFile().existsAsFile())
            video_->lastFile().revealToUser();
    };

    // DEVICE
    stack_.add(hDevice_);
    stack_.add(power_);
    restartP_.onClick = [this] {
        p_.powerCycle();
        ed_.coreChanged();
    };
    stack_.add(reset_);
    reset_.setCaption("erase this firmware's whole flash");
    resetP_.onClick = [this] { beginResetConfirm(); };
    stack_.add(confirmReset_);
    confirmReset_.setVisible(false);
    confirmReset_.onYes = [this] {
        p_.resetFlash();
        endInline();
        refreshPresets();
        ed_.coreChanged();
    };
    confirmReset_.onNo = [this] { endInline(); };

    // STANDALONE
    const bool sa = ed_.isStandalone() && fm1standalone::available();
    for (Widget *w : std::initializer_list<Widget *>{&hStandalone_, &audio_, &saveState_, &loadState_, &resetState_}) {
        stack_.add(*w);
        w->setVisible(sa);
    }
    stack_.add(confirmApp_);
    confirmApp_.setVisible(false);
    audio_.onClick = [this] { sheet_.showPage(kAudio); };
    saveState_.onClick = [] { fm1standalone::saveState(); };
    loadState_.onClick = [] { fm1standalone::loadState(); };
    resetState_.onClick = [this] {
        endInline();
        confirmApp_.begin("Reset the app to its default state? The firmware, its flash and every setting start over.",
                          "Reset app");
        relayout();
    };
    confirmApp_.onYes = [] { fm1standalone::resetToDefault(); };
    confirmApp_.onNo = [this] { endInline(); };

    // ABOUT
    stack_.add(hAbout_);
    stack_.add(about_);
    about_.onClick = [this] { sheet_.showPage(kAbout); };
    stack_.add(credit_);
    credit_.setText(String("FM1VST ") + JucePlugin_VersionString + String::fromUTF8(" \xc2\xb7 GPL-3.0-only \xc2\xb7 "
                                                                                    "type: Inter Tight (OFL 1.1)"));
    refresh();
}

void Sheet::MainPage::endInline()
{
    editMode_ = 0;
    edit_.setVisible(false);
    confirmDelete_.setVisible(false);
    confirmReset_.setVisible(false);
    confirmApp_.setVisible(false);
    relayout();
    if (sheet_.isShowing())
        sheet_.grabKeyboardFocus();
}

void Sheet::MainPage::beginSaveAs()
{
    endInline();
    editMode_ = 1;
    edit_.begin("Save", p_.currentPresetName());
    relayout();
}

void Sheet::MainPage::beginRename()
{
    const String from = p_.currentPresetName();
    if (from.isEmpty())
        return;
    endInline();
    editMode_ = 2;
    edit_.begin("Rename", from);
    relayout();
}

void Sheet::MainPage::beginDeleteConfirm()
{
    const String n = p_.currentPresetName();
    if (n.isEmpty())
        return;
    endInline();
    confirmDelete_.begin(String::fromUTF8("Delete the preset \xe2\x80\x9c") + n + String::fromUTF8("\xe2\x80\x9d? Its file is removed."),
                         "Delete");
    relayout();
}

void Sheet::MainPage::beginResetConfirm()
{
    endInline();
    const String name = u8(p_.getCoreInfo().info.name);
    confirmReset_.begin("Erase the whole flash of " + (name.isNotEmpty() ? name : String("this firmware")) +
                            "? A backup is written first (its backups folder).",
                        "Reset flash");
    relayout();
    vp_.setViewPosition(0, juce::jmax(0, confirmReset_.getBottom() - vp_.getHeight() + 12));
}

void Sheet::MainPage::loadPresetAt(int i)
{
    if (i < 0 || i >= presets_.size())
        return;
    p_.loadPreset(presets_[i]);
    refreshPresets();
    ed_.coreChanged();
}

void Sheet::MainPage::showPresetList()
{
    juce::PopupMenu m;
    m.setLookAndFeel(&look_);
    if (presets_.isEmpty())
        m.addItem(-1, "No presets yet: Save as makes one", false, false);
    for (int i = 0; i < presets_.size(); i++)
        m.addItem(i + 1, presets_[i], true, presets_[i] == p_.currentPresetName());
    juce::Component::SafePointer<MainPage> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetNav_).withMinimumWidth(presetNav_.getWidth()),
                    [self](int r) {
                        if (self && r > 0)
                            self->loadPresetAt(r - 1);
                    });
}

void Sheet::MainPage::exportPreset()
{
    const String n = p_.currentPresetName().isEmpty() ? String("FM1") : p_.currentPresetName();
    chooser_ = std::make_unique<juce::FileChooser>(
        "Export preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(n + ".fm1preset"), "*.fm1preset");
    juce::Component::SafePointer<MainPage> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [self](const juce::FileChooser &fc) {
                              const juce::File f = fc.getResult();
                              if (self && f != juce::File())
                                  self->p_.exportPreset(f);
                          });
}

void Sheet::MainPage::importPreset()
{
    chooser_ = std::make_unique<juce::FileChooser>("Import preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                   "*.fm1preset");
    juce::Component::SafePointer<MainPage> self(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self](const juce::FileChooser &fc) {
                              const juce::File f = fc.getResult();
                              if (self && f != juce::File()) {
                                  self->p_.importPreset(f);
                                  self->refreshPresets();
                                  self->ed_.coreChanged();
                              }
                          });
}

void Sheet::MainPage::showClockMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel(&look_);
    const int cur = p_.getEmuClockChoice();
    for (int c : emuclock::kChoices) {
        String t = c == emuclock::kAuto ? "Auto (now " + String(p_.getEmuMhz()) + " MHz)" : String(c) + " MHz";
        if (emuclock::inDeviceRange(c))
            t << String::fromUTF8(" \xc2\xb7 the FM-1's own");
        else if (emuclock::isOverclock(c))
            t << String::fromUTF8(" \xc2\xb7 overclock");
        m.addItem(c + 1, t, true, c == cur);
    }
    juce::Component::SafePointer<MainPage> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&clockP_).withMinimumWidth(clockP_.getWidth()), [self](int r) {
        if (self && r > 0) {
            self->p_.setEmuClockChoice(r - 1);
            self->refresh();
        }
    });
}

void Sheet::MainPage::showScreenMenu()
{
    const String times = String::fromUTF8("\xc3\x97"), ellipsis = String::fromUTF8("\xe2\x80\xa6");
    enum { kScale = 100 };
    juce::PopupMenu m;
    m.setLookAndFeel(&look_);
    for (int sc : {1, 2, 4})
        m.addItem(kScale + sc, times + String(sc) + "   " + String(240 * sc) + times + String(240 * sc), true, ed_.copyScale() == sc);
    m.addSeparator();
    const int cur = ed_.copyScale();
    m.addItem(cur == 4 ? FM1Editor::kSave4 : cur == 2 ? FM1Editor::kSave2 : FM1Editor::kSave1, "Save screen as PNG" + ellipsis);
    m.addItem(FM1Editor::kCopyRaw, "Copy raw RGB565 (.bin)");
    m.addItem(FM1Editor::kSaveRaw, "Save raw RGB565 as .bin" + ellipsis);
    m.addSeparator();
    m.addItem(FM1Editor::kFreeze, "Freeze screen", true, ed_.screenFrozen());
    juce::Component::SafePointer<MainPage> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&screenCtl_.scale).withMinimumWidth(screenCtl_.getWidth()),
                    [self](int r) {
                        if (!self || r <= 0)
                            return;
                        if (r > kScale)
                            self->ed_.setCopyScale(r - kScale);
                        else
                            self->ed_.screenCommand(r);
                        if (self)
                            self->refresh(false);
                    });
}

void Sheet::MainPage::refreshPresets()
{
    presets_ = p_.listPresets();
    const String cur = p_.currentPresetName();
    presetNav_.setPresetName(cur);
    const bool has = cur.isNotEmpty();
    rename_->setEnabledLook(has);
    delete_->setEnabledLook(has);
}

// the presets, then the custom themes, then the state's own theme when no file has it
void Sheet::MainPage::refreshThemes()
{
    const Theme cur = p_.getTheme();
    themeItems_.clear();
    int sel = -1;
    auto add = [&](const Theme &t) {
        themeItems_.push_back(t);
        if (sel < 0 && t.name.equalsIgnoreCase(cur.name))
            sel = (int)themeItems_.size() - 1;
    };
    for (const Theme &t : ThemeStore::presets())
        add(t);
    for (const Theme &t : ed_.themeStore().customs())
        add(t);
    if (sel < 0) {                               // (a custom theme whose file is gone: the state's colours)
        Theme t = cur;
        add(t);
    }
    swatches_.set(themeItems_, sel);
    themeRow_.setValue(hoverTheme_.isNotEmpty() ? hoverTheme_ : (sel >= (int)ThemeStore::presets().size() &&
                                                                         !ed_.themeStore().find(cur.name)
                                                                     ? cur.name + " (this set)"
                                                                     : cur.name));
}

void Sheet::MainPage::refresh(bool full)
{
    // FIRMWARE
    const FM1Processor::CoreStatus s = p_.getCoreInfo();
    const FM1Processor::EmuFirmware fw = p_.getEmuFirmware();
    const bool emu = fw.valid();
    const String key = emu ? fw.id : hub::hubIdForCore(u8(s.info.id));
    const hub::Entry *he = sheet_.catalog().find(key);
    bool hasNative = false;
    for (const CoreInfo &c : sheet_.natives())
        hasNative = hasNative || u8(c.id) == hub::coreIdForHub(key);
    const FM1Processor::EmuStatus es = p_.getEmuStatus();
    if (emu) {
        const hub::Entry e = he ? *he : fw.entry();
        // (a firmware with a native core too: that core's tag and author, as the browser shows them)
        const FirmwareMeta nm = FirmwareMeta::of(hub::coreIdForHub(key), {});
        const String tag = hasNative ? nm.tag : he ? he->tag() : String();
        String meta = String::fromUTF8("Emulated \xc2\xb7 ") + String(p_.getEmuMhz()) + String::fromUTF8(" MHz \xc2\xb7 ") +
                      (tag.isNotEmpty() ? tag + String::fromUTF8(" \xc2\xb7 ") : String()) + (hasNative ? nm.author : e.shortAuthor());
        Colour mc = C(Look::C::muted);
        if (s.halted) {
            meta = "Halted (exit " + String(s.haltCode & 0xFF) + "): Restart below";
            mc = C(Look::C::red);
        } else if (es.waiting) {
            meta = "Not running: its package is not here";
            mc = C(Look::C::amber);
        }
        firmware_.set(fw.name, fw.version, meta, mc);
        about_.setLabel("About " + fw.name);
        about_.setValue(fw.version);
    } else if (s.loaded) {
        const FirmwareMeta m = FirmwareMeta::of(u8(s.info.id), u8(s.info.source_url));
        String meta = m.category + String::fromUTF8(" \xc2\xb7 ") + m.tag + String::fromUTF8(" \xc2\xb7 ") + m.author;
        Colour mc = C(Look::C::muted);
        if (s.halted) {
            meta = "Halted (exit " + String(s.haltCode & 0xFF) + "): Restart below";
            mc = C(Look::C::red);
        } else if (s.message.isNotEmpty()) {
            meta = s.message;
            mc = C(Look::C::amber);
        }
        firmware_.set(u8(s.info.name), u8(s.info.version), meta, mc);
        about_.setLabel("About " + u8(s.info.name));
        about_.setValue(u8(s.info.version));
    } else {
        firmware_.set("No firmware", "", s.message.isNotEmpty() ? s.message : String("choose one"), C(Look::C::amber));
        about_.setLabel("About the firmware");
        about_.setValue({});
    }
    // the download, the engine, the clock, the source
    {
        String st;
        Colour sc = C(Look::C::muted);
        if (es.busy)
            st = es.text + (es.progress >= 0 ? " " + String(juce::roundToInt(es.progress * 100)) + " %" : String());
        else if (es.text.isNotEmpty())
            st = es.text, sc = C(es.error ? Look::C::red : Look::C::amber);
        emuStatus_.setText(st, sc);
        emuStatus_.setLed(es.busy ? look_.accent() : juce::Colours::transparentBlack);
        emuStatus_.setVisible(st.isNotEmpty());
    }
    engine_.setVisible(he != nullptr && hasNative);
    engineSeg_.setSelected(emu ? 1 : 0);
    if (he && hasNative)
        engine_.setCaption(emu ? "the Hub's package " + he->version + " on fm1-emu" : "FM1VST's core " + u8(s.info.version),
                           C(Look::C::muted));
    clock_.setVisible(emu);
    {
        const int c = p_.getEmuClockChoice(), mhz = p_.getEmuMhz();
        clockP_.setText(c == emuclock::kAuto ? "Auto" : String(c) + " MHz");
        clock_.setControlWidth(juce::jmax(96, clockP_.preferredWidth()));
        const String note = p_.getEmuClockNote();   // (Auto's step, or one waiting for the transport to stop)
        if (note.isNotEmpty())
            clock_.setCaption(note, C(Look::C::amber));
        else
            clock_.setCaption(String(mhz) + " MHz now" +
                              (emuclock::isOverclock(mhz) ? String::fromUTF8(" \xc2\xb7 overclock")
                               : emuclock::inDeviceRange(mhz) ? String::fromUTF8(" \xc2\xb7 the FM-1's own range")
                                                              : String::fromUTF8(" \xc2\xb7 below the FM-1's")),
                          C(Look::C::muted));
    }
    sourceUrl_ = emu ? fw.source : u8(s.info.source_url);
    if (sourceUrl_.contains("@"))                // (a core's "repo@commit": the commit's tree)
        sourceUrl_ = sourceUrl_.upToFirstOccurrenceOf("@", false, false) + "/tree/" + sourceUrl_.fromFirstOccurrenceOf("@", false, false);
    source_.setVisible(sourceUrl_.startsWith("http"));
    source_.setValue(sourceUrl_.fromFirstOccurrenceOf("://", false, false).upToFirstOccurrenceOf("/tree/", false, false)
                         .upToFirstOccurrenceOf("/blob/", false, false));
    source_.setTooltip(sourceUrl_);
    power_.setCaption(s.halted ? "halted (exit " + String(s.haltCode & 0xFF) + "): restart it" : String("power off and on again"),
                      C(s.halted ? Look::C::red : Look::C::muted));
    restartP_.setKind(s.halted ? Look::Primary : Look::Plain);

    // PRESET
    if (!edit_.isVisible())
        refreshPresets();

    // REMOTE FM-1
    const RemoteLink::Status st = p_.remoteStatus();
    const bool on = p_.getRemoteEnabled();
    remoteSw_.setOn(on);
    Colour led = juce::Colours::transparentBlack;
    if (st.state == RemoteLink::kConnected)
        led = C(Look::C::green);
    else if (on || st.state != RemoteLink::kOff)
        led = C(Look::C::amber);
    remoteStatus_.setLed(led);
    remoteStatus_.setText(on || st.state != RemoteLink::kOff ? (st.text.empty() ? String("starting") : u8(st.text)) : String("Off"),
                          C(on ? Look::C::rowText : Look::C::muted));
    String diag;
    if (st.sessions || st.frames)
        diag << String::formatted("Sessions %u, frames %u (acknowledged %u), panel events %u", st.sessions, st.frames, st.acks,
                                  st.eventsIn);
    if (st.ble)                                  // (RemoteFM1 0.2.2's pacing: reduced tiles, then exact; changes whole)
        diag << (diag.isEmpty() ? "" : "\n")
             << String::formatted("Bluetooth: tiles sent in %u colours %u, then exact %u; held frames %u", st.colors,
                                  st.reduced, st.refined, st.held);
    if (!st.lastLog.empty() && on)
        diag << (diag.isEmpty() ? "" : "\n") << "Last: " << u8(st.lastLog);
    if (!RemoteLink::hasBulk())
        diag << (diag.isEmpty() ? "" : "\n") << "Built without libusb: frames go over SysEx only";
    remoteDiag_.setText(diag);
    ignoreSw_.setOn(p_.getRemoteIgnoreMidi());

    // EDITORS
    {
        const FM1Processor::EditorPortStatus ps = p_.editorPortStatus();
        editorSw_.setOn(ps.on);
        Colour eled = juce::Colours::transparentBlack;
        String etext;
        if (ps.open)
            eled = C(Look::C::green), etext = ps.name;
        else if (ps.on)
            eled = C(Look::C::amber), etext = ps.error.isNotEmpty() ? ps.error : String("starting");
        else
            etext = "Off: it would show as " + EditorPort::nameFor(ps.usbName, 1);
        editorStatus_.setLed(eled);
        editorStatus_.setText(etext, C(ps.open ? Look::C::rowText : Look::C::muted));
        String hint = "Open the firmware's editor in Chrome; it finds this FM1VST as an FM-1. Don't enable this port as a "
                      "MIDI input in your DAW (Live lists it under All Ins).";
        if (ps.emulated)
            hint << "\nAn emulated firmware has no USB MIDI: its editor gets no answer.";
        if (ps.open && (ps.in || ps.out))
            hint << "\n" << String::formatted("Messages in %u, out %u", ps.in, ps.out)
                 << (ps.idling ? String::fromUTF8(" \xc2\xb7 running for the editor (the host is stopped)") : String());
        editorHint_.setText(hint);
        editorUrl_ = ps.url;
        editorLink_.setVisible(ps.available && ps.url.isNotEmpty());
        editorLink_.setValue(ps.url.fromFirstOccurrenceOf("://", false, false).trimCharactersAtEnd("/"));
        editorLink_.setTooltip(ps.url);
    }

    // PLAYING, HOST CONTROL
    transposeSt_.setValue(p_.getTranspose());
    notesSw_.setOn(p_.getMidiNotesPlayKeys());
    toFwSw_.setOn(p_.getKeyNotesToFirmware());
    buttonModeSeg_.setSelected(p_.getButtonMode());
    encModeSeg_.setSelected(p_.getEncoderMode());
    stepsSt_.setValue(p_.getEncoderSteps());
    const bool stepped = p_.getEncoderMode() == FM1Processor::kEncStepped;
    stepsSt_.setEnabledLook(stepped);
    steps_.setEnabledLook(stepped);

    // LOOK, VIEW
    if (full)
        refreshThemes();
    viewSeg_.setSelected(ed_.viewMode());
    bigSw_.setOn(ed_.panel().bigLcd());
    hintsSw_.setOn(ed_.keyHints());
    const bool dev = ed_.viewMode() == FM1Processor::kViewDevice;
    bigSw_.setEnabledLook(dev);
    big_.setEnabledLook(dev);
    screenCtl_.scale.setText(String::fromUTF8("\xc3\x97") + String(ed_.copyScale()));
    refreshVideo();
    relayout();
}

void Sheet::MainPage::setVideo(const std::function<void(fm1video::Settings &)> &change)
{
    if (!video_)
        return;
    fm1video::Settings s = video_->settings();
    change(s);
    video_->setSettings(s);
    refresh(false);
}

String Sheet::MainPage::inputLabel() const
{
    const fm1video::Settings &s = video_->settings();
    const juce::StringArray names = video_->inputChannelNames(video_->inputDevice());
    auto chName = [&](int c) { return c < names.size() && names[c].isNotEmpty() ? names[c] : String(c + 1); };
    if (s.inChannels == 2)
        return String(s.inChannel + 1) + "+" + String(s.inChannel + 2);
    return names.isEmpty() ? String(s.inChannel + 1) : chName(s.inChannel);
}

void Sheet::MainPage::showInputMenu()
{
    if (!video_)
        return;
    juce::PopupMenu m;
    m.setLookAndFeel(&look_);
    const juce::StringArray devs = video_->inputDevices();
    const String cur = video_->inputDevice();
    const fm1video::Settings &s = video_->settings();
    // (ids: device index * 1000 + channel * 2 + stereo, + 1)
    for (int d = 0; d < devs.size(); d++) {
        const juce::StringArray ch = video_->inputChannelNames(devs[d]);
        if (ch.isEmpty())
            continue;
        m.addSectionHeader(devs[d]);
        for (int c = 0; c < ch.size(); c++)
            m.addItem(d * 1000 + c * 2 + 1, String(c + 1) + "  " + ch[c] + String::fromUTF8(" \xc2\xb7 mono"), true,
                      devs[d] == cur && s.inChannels == 1 && s.inChannel == c);
        for (int c = 0; c + 1 < ch.size(); c += 2)
            m.addItem(d * 1000 + c * 2 + 2, String(c + 1) + "+" + String(c + 2) + String::fromUTF8(" \xc2\xb7 stereo"), true,
                      devs[d] == cur && s.inChannels == 2 && s.inChannel == c);
    }
    if (m.getNumItems() == 0)
        m.addItem(-1, "No audio input found", false, false);
    juce::Component::SafePointer<MainPage> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&inputP_).withMinimumWidth(inputP_.getWidth()),
                    [self, devs](int r) {
                        if (!self || r <= 0)
                            return;
                        const int id = r - 1, d = id / 1000, c = (id % 1000) / 2, st = (id % 1000) % 2;
                        self->setVideo([&](fm1video::Settings &v) {
                            v.inputDevice = d < devs.size() ? devs[d] : String();
                            v.inChannel = c;
                            v.inChannels = st ? 2 : 1;
                        });
                    });
}

void Sheet::MainPage::refreshVideo()
{
    if (!video_)
        return;
    const fm1video::Settings &s = video_->settings();
    const bool rec = video_->recording();
    const String dot = String::fromUTF8(" \xc2\xb7 "), times = String::fromUTF8(" \xc3\x97 ");
    recP_.setText(rec ? "Stop" : "Record");
    recP_.setKind(rec ? Look::DangerFill : Look::Primary);
    const juce::int64 drops = video_->droppedFrames();
    record_.setCaption(rec ? fm1video::formatTime(video_->seconds()) + dot +
                                 (drops > 0 ? String(drops) + " frames dropped" + dot : String()) + video_->currentFile().getFileName()
                           : String::fromUTF8("\xe2\x87\xa7\xe2\x8c\x98" "R") + dot + "~/Movies/FM1VST",
                       Look::col(rec ? Look::C::red : Look::C::muted));
    vViewSeg_.setSelected(s.view);
    vSizeSeg_.setSelected(s.size >= 1080 ? 1 : 0);
    const juce::Point<int> fs = video_->frameSize();   // (Device with the big LCD: the taller layout; a take's own)
    vSize_.setCaption(String(fs.x) + times + String(fs.y) +
                      (s.view == fm1video::kViewLcd ? dot + (s.size >= 1080 ? "4.5" : "3") + String::fromUTF8("\xc3\x97 nearest")
                       : video_->bigLcd()           ? dot + "the big LCD above"
                                                    : String()));
    vFpsSeg_.setSelected(s.fps >= 60 ? 1 : 0);
    vFmtSeg_.setSelected(s.mov ? 1 : 0);
    for (Widget *w : std::initializer_list<Widget *>{&vView_, &vViewSeg_, &vSize_, &vSizeSeg_, &vFps_, &vFpsSeg_, &vFmt_, &vFmtSeg_})
        w->setEnabledLook(!rec);                 // (fixed for a take)
    const bool dev = video_->hasAudioDevice();
    micSw_.setOn(s.mic);
    micSw_.setEnabledLook(!rec && dev);
    mic_.setEnabledLook(!rec && dev);
    mic_.setCaption(dev ? (s.mic ? (s.micMix ? "mixed into the main track" : "a second audio track") : "off")
                        : "no audio device here");
    inputP_.setText(inputLabel());
    input_.setControlWidth(juce::jlimit(96, 190, inputP_.preferredWidth()));
    input_.setCaption(video_->inputDevice());
    for (Widget *w : std::initializer_list<Widget *>{&input_, &inputP_, &track_, &trackSeg_})
        w->setEnabledLook(s.mic && !rec && dev);
    for (Widget *w : std::initializer_list<Widget *>{&gain_, &gainSt_, &monitor_, &monSw_})
        w->setEnabledLook(s.mic && dev);         // (the gain and the monitor change live)
    gainSt_.setValue(s.micGainDb);
    trackSeg_.setSelected(s.micMix ? 1 : 0);
    monSw_.setOn(s.monitor);
    monitor_.setCaption("headphones: a microphone feeds back", Look::col(s.monitor ? Look::C::amber : Look::C::muted));
    const String note = micNote_.isNotEmpty() ? micNote_ : rec ? String() : video_->lastError();
    videoNote_.setText(note, Look::col(Look::C::amber));
    videoNote_.setVisible(note.isNotEmpty());
    const juce::File last = video_->lastFile();
    lastTake_.setVisible(last.existsAsFile());
    lastTake_.setValue(last.getFileName());
    lastTake_.setTooltip("Show in Finder: " + last.getFullPathName());
}

// ============================================================ firmware ===
namespace {
class Chips : public Widget {
public:
    Chips(Look &l, juce::StringArray labels) : Widget(l), labels_(std::move(labels)) {}
    void setSelected(int i)
    {
        sel_ = i;
        repaint();
    }
    std::function<void(int)> onChange;
    std::vector<Rectangle<float>> cells() const
    {
        std::vector<Rectangle<float>> c;
        float x = 0, y = 4;
        const juce::Font f = Look::font(12.5f, Look::SemiBold);
        for (const String &l : labels_) {
            const float w = Look::textWidth(f, l) + 22;
            if (x + w > (float)getWidth() && x > 0) {
                x = 0;
                y += 34;
            }
            c.emplace_back(x, y, w, 28.0f);
            x += w + 6;
        }
        return c;
    }
    int heightFor(int w) const override
    {
        float x = 0;
        int rows = 1;
        const juce::Font f = Look::font(12.5f, Look::SemiBold);
        for (const String &l : labels_) {
            const float cw = Look::textWidth(f, l) + 22;
            if (x + cw > (float)w && x > 0) {
                x = 0;
                rows++;
            }
            x += cw + 6;
        }
        return rows * 34 + 6;
    }
    void paint(juce::Graphics &g) override
    {
        const auto c = cells();
        g.setFont(Look::font(12.5f, Look::SemiBold));
        for (int i = 0; i < labels_.size(); i++) {
            const bool a = i == sel_, h = i == hoverI_;
            g.setColour(a ? look.accent() : C(h ? Look::C::raisedHover : Look::C::raised));
            g.fillRoundedRectangle(c[(size_t)i], 7);
            g.setColour(a ? look.onAccent() : C(h ? Look::C::text : Look::C::muted).brighter(0.15f));
            g.drawText(labels_[i], c[(size_t)i], juce::Justification::centred, false);
        }
    }
    void mouseMove(const juce::MouseEvent &e) override
    {
        int h = -1;
        const auto c = cells();
        for (size_t i = 0; i < c.size(); i++)
            if (c[i].contains(e.position))
                h = (int)i;
        if (h != hoverI_) {
            hoverI_ = h;
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent &) override
    {
        hoverI_ = -1;
        repaint();
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mouseWasClicked() && hoverI_ >= 0) {
            sel_ = hoverI_;
            repaint();
            if (onChange)
                onChange(sel_);
        }
    }

private:
    juce::StringArray labels_;
    int sel_ = 0, hoverI_ = -1;
};

// one firmware of the browser: FM1VST's native core, the Hub's package (emulated), or both
struct FirmwareItem {
    String key;                                   // the Hub's id (a native core's own id mapped: hub::hubIdForCore)
    String name, tag, author, category;
    std::optional<CoreInfo> native;
    std::optional<hub::Entry> emu;
    bool nativeBehind() const { return native && emu && hub::hubNewer(emu->version, u8(native->version)); }
    // the engine a click starts: the one there is; with both, native unless its version is behind the Hub's (RemoteFM1's
    // newest-wins)
    int defaultEngine() const { return !native ? 2 : !emu ? 1 : nativeBehind() ? 2 : 1; }
    String version(int engine) const { return engine == 1 && native ? u8(native->version) : emu ? emu->version : String(); }
};

class CoreCard : public Widget {
public:
    CoreCard(Look &l, FirmwareItem it) : Widget(l), item(std::move(it))
    {
        String tip = item.name;
        if (item.native)
            tip << "\nNative: FM1VST's core " << u8(item.native->version)
                << (item.native->license.empty() ? String() : ", " + u8(item.native->license))
                << (item.native->gated.empty() ? String() : "\nNot in public builds: " + u8(item.native->gated));
        if (item.emu)
            tip << "\nEmulated: the Hub's package " << item.emu->version << " on fm1-emu" << (item.emu->beta ? " (beta)" : "")
                << (item.emu->note.isNotEmpty() ? "\nKnown not to run on fm1-emu: " + item.emu->note : String());
        tip << "\n" << item.tag << String::fromUTF8(" \xc2\xb7 ") << item.author;
        setTooltip(tip);
    }
    FirmwareItem item;
    int running = 0;                              // the engine running it now: 0 none, 1 native, 2 emulated
    std::function<void()> onClick;
    void paint(juce::Graphics &g) override
    {
        const bool current = running != 0;
        const Rectangle<float> r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(current ? C(Look::C::raised).interpolatedWith(look.accent(), 0.08f) : C(hover_ ? Look::C::raisedHover : Look::C::raised));
        g.fillRoundedRectangle(r, 10);
        if (current) {
            g.setColour(look.accent());
            g.drawRoundedRectangle(r.reduced(0.5f), 10, 2.0f);
        } else {
            g.setColour(C(hover_ ? Look::C::border : Look::C::line));
            g.drawRoundedRectangle(r, 10, 1.0f);
        }
        Rectangle<float> in = r.reduced(12, 9);
        Rectangle<float> top = in.removeFromTop(19);
        const juce::Font vf = Look::font(11.0f, Look::SemiBold);
        const String v = item.version(running ? running : item.defaultEngine());
        const float vw = juce::jmin(top.getWidth() * 0.45f, Look::textWidth(vf, v) + 2);
        g.setFont(vf);
        g.setColour(look.accent());
        g.drawText(v, top.removeFromRight(vw), juce::Justification::centredRight, true);
        g.setFont(Look::font(14.0f, Look::Bold));
        g.setColour(C(Look::C::text));
        g.drawFittedText(item.name, top.withTrimmedRight(6).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        // the engines: N (native) and E (emulated) badges, the running one filled
        const juce::Font bf = Look::font(9.5f, Look::Bold);
        g.setFont(bf);
        Rectangle<float> line = in;
        for (int e = 2; e >= 1; e--) {
            if ((e == 1 && !item.native) || (e == 2 && !item.emu))
                continue;
            const String t = e == 1 ? "NATIVE" : "EMU";
            const Rectangle<float> b = line.removeFromRight(Look::textWidth(bf, t) + 10).withSizeKeepingCentre(Look::textWidth(bf, t) + 10, 15);
            line.removeFromRight(4);
            const bool on = running == e;
            const bool dim = e == 2 && item.emu->note.isNotEmpty();
            g.setColour(on ? look.accent() : C(Look::C::line));
            if (on)
                g.fillRoundedRectangle(b, 4);
            else
                g.drawRoundedRectangle(b.reduced(0.5f), 4, 1.0f);
            g.setColour(on ? look.onAccent() : C(dim ? Look::C::red : Look::C::muted));
            g.drawText(t, b, juce::Justification::centred, false);
        }
        g.setFont(Look::font(11.5f, Look::Medium));
        g.setColour(C(Look::C::muted));
        g.drawFittedText(item.tag + String::fromUTF8(" \xc2\xb7 ") + item.author, line.withTrimmedRight(4).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.8f);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }
};

class CardGrid : public Widget {
public:
    using Widget::Widget;
    std::vector<std::unique_ptr<CoreCard>> cards;
    int columnsFor(int w) const { return juce::jmax(1, (w + kGap) / (164 + kGap)); }
    int heightFor(int w) const override
    {
        int n = 0;
        for (const auto &c : cards)
            n += c->isVisible();
        const int cols = columnsFor(w), rows = (n + cols - 1) / cols;
        return rows > 0 ? rows * (kH + kGap) + 4 : 64;
    }
    void resized() override
    {
        const int cols = columnsFor(getWidth()), w = (getWidth() - (cols - 1) * kGap) / cols;
        int i = 0;
        for (const auto &c : cards) {
            if (!c->isVisible())
                continue;
            c->setBounds((i % cols) * (w + kGap), 2 + (i / cols) * (kH + kGap), w, kH);
            i++;
        }
    }
    void paint(juce::Graphics &g) override
    {
        for (const auto &c : cards)
            if (c->isVisible())
                return;
        g.setFont(Look::font(13.0f, Look::Medium));
        g.setColour(C(Look::C::muted));
        g.drawText("No firmware matches", getLocalBounds().withHeight(48), juce::Justification::centred, false);
    }
    static constexpr int kH = 58, kGap = 8;
};
}   // namespace

class Sheet::FirmwarePage : public ScrollPage {
public:
    FirmwarePage(Sheet &s, FM1Editor &ed) : ScrollPage(ed.look()), sheet_(s), ed_(ed), look_(ed.look())
    {
        Look::styleTextEditor(search_, 14.0f);
        search_.setIndents(34, 0);
        search_.onTextChange = [this] { filter(); };
        search_.onEscapeKey = [this] {
            if (search_.getText().isNotEmpty())
                search_.clear(), filter();
            else
                sheet_.close();
        };
        stack_.add(searchHolder_);
        stack_.add(engineRow_);
        engineRow_.setControlWidth(engineSeg_.preferredWidth());
        engineRow_.setTooltip("Native: FM1VST's cores, each compiled from its firmware's source (light on the CPU). Emulated: "
                              "every firmware on the FM-1 Firmware Hub, its own package run on fm1-emu, the FM-1's chip "
                              "emulated (heavier). A package is downloaded from the Hub when you pick it, and kept.");
        engineSeg_.onChange = [this](int i) {
            engineSel_ = i;
            filter();
        };
        stack_.add(chips_);
        chips_.onChange = [this](int i) {
            chipSel_ = i;
            filter();
        };
        stack_.add(status_);
        stack_.add(grid_);
        stack_.add(own_);
        own_.setCaption("M-VAVE's stock V15 (your FM-1.fwsc) or any FM-1 package");
        own_.setValue(String::fromUTF8("Open\xe2\x80\xa6"));
        own_.onClick = [this] { openOwn(); };
    }
    void paintOverChildren(juce::Graphics &g) override
    {
        const Rectangle<int> r = search_.getBounds() + searchHolder_.getPosition() + stack_.getPosition() + vp_.getPosition() -
                                 vp_.getViewPosition();
        Look::strokeIcon(g, Look::Search, Rectangle<float>((float)r.getX() + 11, (float)r.getCentreY() - 7, 14, 14), C(Look::C::muted), 1.5f);
    }
    // the items: the native cores and the Hub's firmwares, one per firmware, by name
    std::vector<FirmwareItem> items() const
    {
        std::vector<FirmwareItem> out;
        for (const CoreInfo &c : sheet_.natives()) {
            FirmwareItem it;
            it.key = hub::hubIdForCore(u8(c.id));
            const FirmwareMeta m = FirmwareMeta::of(u8(c.id), u8(c.source_url));
            it.name = u8(c.name), it.tag = m.tag, it.author = m.author, it.category = m.category;
            it.native = c;
            out.push_back(std::move(it));
        }
        for (const hub::Entry &e : sheet_.catalog().firmwares) {
            auto f = std::find_if(out.begin(), out.end(), [&](const FirmwareItem &i) { return i.key == e.id; });
            if (f != out.end()) {
                f->emu = e;
                continue;
            }
            FirmwareItem it;
            it.key = e.id, it.name = e.name, it.tag = e.tag(), it.author = e.shortAuthor(), it.category = e.category();
            it.emu = e;
            out.push_back(std::move(it));
        }
        std::sort(out.begin(), out.end(), [](const FirmwareItem &a, const FirmwareItem &b) {
            return a.name.compareNatural(b.name) < 0;
        });
        return out;
    }
    void rebuild()
    {
        std::vector<FirmwareItem> list = items();
        bool same = list.size() == grid_.cards.size();
        for (size_t i = 0; same && i < list.size(); i++)
            same = list[i].key == grid_.cards[i]->item.key && list[i].version(1) == grid_.cards[i]->item.version(1) &&
                   list[i].version(2) == grid_.cards[i]->item.version(2) && (bool)list[i].native == (bool)grid_.cards[i]->item.native &&
                   (bool)list[i].emu == (bool)grid_.cards[i]->item.emu;
        if (!same) {
            grid_.cards.clear();
            for (FirmwareItem &it : list) {
                auto card = std::make_unique<CoreCard>(look_, std::move(it));
                CoreCard *p = card.get();
                card->onClick = [this, p] { sheet_.pickInBrowser(p->item.key, engineSel_ == 0 ? -1 : engineSel_); };
                grid_.addAndMakeVisible(*card);
                grid_.cards.push_back(std::move(card));
            }
        }
        markRunning();
        search_.setTextToShowWhenEmpty("Search " + String((int)grid_.cards.size()) + " firmwares", C(Look::C::muted));
        tick();
        filter();
    }
    void markRunning()
    {
        FM1Processor &p = ed_.proc();
        const FM1Processor::EmuFirmware fw = p.getEmuFirmware();
        const String cur = fw.valid() ? fw.id : hub::hubIdForCore(u8(p.getCoreInfo().info.id));
        for (auto &c : grid_.cards) {
            const int r = c->item.key != cur ? 0 : fw.valid() ? 2 : 1;
            if (r != c->running) {
                c->running = r;
                c->repaint();
            }
        }
    }
    // the catalog's state and a download's progress (the editor's timer)
    void tick()
    {
        const hub::Catalog &cat = sheet_.catalog();
        hub::Service &svc = ed_.proc().hubService();
        int nEmu = 0, nNat = 0;
        for (const auto &c : grid_.cards)
            nEmu += (bool)c->item.emu, nNat += (bool)c->item.native;
        String t = String(nNat) + " native, " + String(nEmu) + " on the Hub";
        Colour col = C(Look::C::muted);
        if (svc.refreshing())
            t << String::fromUTF8(" \xc2\xb7 checking the Hub\xe2\x80\xa6");
        else if (cat.origin == "live")
            t << String::fromUTF8(" \xc2\xb7 the Hub's catalog, ") << cat.updated;
        else if (cat.origin == "cache")
            t << String::fromUTF8(" \xc2\xb7 offline: the catalog of ") << cat.updated, col = C(Look::C::amber);
        else
            t << String::fromUTF8(" \xc2\xb7 offline: the built-in list (") << cat.updated << ")", col = C(Look::C::amber);
        const FM1Processor::EmuStatus es = ed_.proc().getEmuStatus();
        if (es.busy)
            t << "\n" << es.text << (es.progress >= 0 ? " " + String(juce::roundToInt(es.progress * 100)) + " %" : String());
        else if (es.error && es.text.isNotEmpty())
            t << "\n" << es.text, col = C(Look::C::red);
        if (t != status_.text()) {
            status_.setText(t, col);
            status_.setLed(es.busy || svc.refreshing() ? look_.accent() : juce::Colours::transparentBlack);
            relayout();
        }
        markRunning();
    }
    void setFilter(const String &text, int category)
    {
        search_.setText(text, false);
        chipSel_ = category;
        chips_.setSelected(category);
        filter();
    }
    void setEngine(int e)
    {
        engineSel_ = juce::jlimit(0, 2, e);
        engineSeg_.setSelected(engineSel_);
        filter();
    }
    void focusSearch()
    {
        if (search_.isShowing())
            search_.grabKeyboardFocus();
    }

private:
    void filter()
    {
        const String q = search_.getText().trim();
        const String want = chipSel_ <= 0 ? String() : FirmwareMeta::categories()[chipSel_];
        for (auto &c : grid_.cards) {
            const FirmwareItem &it = c->item;
            const bool engOk = engineSel_ == 0 || (engineSel_ == 1 && it.native) || (engineSel_ == 2 && it.emu);
            const bool catOk = want.isEmpty() || it.category == want;
            const String hay = it.name + " " + it.key + " " + it.tag + " " + it.author + " " + it.category +
                               (it.native ? " native" : "") + (it.emu ? " emulated emu hub" : "");
            c->setVisible(engOk && catOk && (q.isEmpty() || hay.containsIgnoreCase(q)));
        }
        grid_.resized();
        relayout();
        grid_.repaint();
    }
    void openOwn()
    {
        chooser_ = std::make_unique<juce::FileChooser>("Open an FM-1 package", juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                       "*.fwsc");
        juce::Component::SafePointer<FirmwarePage> self(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [self](const juce::FileChooser &fc) {
                                  const juce::File f = fc.getResult();
                                  if (!self || f == juce::File())
                                      return;
                                  const String err = self->ed_.proc().openLocalPackage(f);
                                  self->ed_.coreChanged();
                                  if (err.isEmpty())
                                      self->sheet_.showPage(kMain);
                                  else
                                      self->status_.setText(err, C(Look::C::red));
                              });
    }

    Sheet &sheet_;
    FM1Editor &ed_;
    Look &look_;
    juce::TextEditor search_;
    Holder searchHolder_{look_, search_, 46};
    Segmented engineSeg_{look_, {"All", "Native", "Emulated"}};
    Row engineRow_{look_, "Engine", &engineSeg_, 220, 30};
    Chips chips_{look_, FirmwareMeta::categories()};
    Text status_{look_, 11.5f, Look::C::muted};
    CardGrid grid_{look_};
    Row own_{look_, "Your own .fwsc"};
    std::unique_ptr<juce::FileChooser> chooser_;
    int chipSel_ = 0, engineSel_ = 0;
};

// =============================================================== about ===
class Sheet::AboutPage : public ScrollPage {
public:
    AboutPage(Sheet &, FM1Editor &ed) : ScrollPage(ed.look()), ed_(ed), look_(ed.look())
    {
        stack_.add(title_);
        title_.setPadding(6, 10);
        for (KeyValue *k : {&author_, &core_, &clock_, &licence_, &source_, &package_, &gated_, &module_})
            stack_.add(*k);
        stack_.add(hCredits_);
        stack_.add(credits_);
        stack_.add(hPlugin_);
        stack_.add(plugin_);
    }
    void refresh()
    {
        const FM1Processor::CoreStatus s = ed_.proc().getCoreInfo();
        const CoreInfo &c = s.info;
        const FM1Processor::EmuFirmware fw = ed_.proc().getEmuFirmware();
        for (KeyValue *k : {&author_, &clock_, &package_})
            k->setVisible(fw.valid());
        if (s.loaded && fw.valid()) {
            // an emulated firmware: the Hub's entry (or the user's file), its package, the engine
            title_.setText(fw.name + "  " + fw.version, C(Look::C::text));
            author_.setValue(fw.author);
            core_.setValue(u8(c.name) + ": " + u8(c.version) + " (the fm1emu core, core ABI " + String((int)c.abi) +
                           String::fromUTF8("): fm1-emu by Joakim Eriksson, MIT, runs the firmware's own FM-1 package"));
            clock_.setValue(String(ed_.proc().getEmuMhz()) + " MHz (" + String(emuclock::choiceName(ed_.proc().getEmuClockChoice())) + ")");
            licence_.setValue(fw.local ? String("your own file's") : String("the firmware's own: see its source"));
            source_.setValue(fw.source.isNotEmpty() ? fw.source : String("(no public source)"));
            package_.setValue((fw.pkg.isNotEmpty() ? fw.pkg : String("your file")) + "\nSHA-256 " + fw.sha256.substring(0, 16) +
                              String::fromUTF8("\xe2\x80\xa6") + (fw.size > 0 ? ", " + String(fw.size) + " bytes" : String()));
            package_.setTooltip("SHA-256 " + fw.sha256);
            module_.setValue(u8(c.path));
            for (KeyValue *k : {&core_, &licence_, &source_, &module_})
                k->setVisible(true);
            gated_.setVisible(false);
            credits_.setText(c.credits.empty() ? String("(none)") : u8(c.credits), C(Look::C::rowText));
        } else if (!s.loaded) {
            title_.setText("No firmware loaded", C(Look::C::text));
            for (KeyValue *k : {&core_, &licence_, &source_, &gated_, &module_})
                k->setVisible(false);
            credits_.setText(s.message, C(Look::C::rowText));
        } else {
            title_.setText(u8(c.name) + "  " + u8(c.version), C(Look::C::text));
            core_.setValue(u8(c.id) + " (" + (c.family.empty() ? u8(c.id) : u8(c.family)) + " glue, core ABI " + String((int)c.abi) + ")");
            licence_.setValue(c.license.empty() ? String("(not given)") : u8(c.license));
            source_.setValue(u8(c.source_url));
            gated_.setValue(u8(c.gated));
            module_.setValue(u8(c.path));
            for (KeyValue *k : {&core_, &licence_, &source_, &module_})
                k->setVisible(true);
            gated_.setVisible(!c.gated.empty());
            credits_.setText(c.credits.empty() ? String("(none beyond the licence)") : u8(c.credits), C(Look::C::rowText));
        }
        plugin_.setText(String("FM1VST ") + JucePlugin_VersionString +
                            ": the FM-1 emulator, GPL-3.0-only. The editor's type is Inter Tight (The Inter Project Authors, "
                            "SIL Open Font License 1.1).",
                        C(Look::C::rowText));
        relayout();
    }

private:
    FM1Editor &ed_;
    Look &look_;
    Text title_{look_, 19.0f, Look::C::text, Look::Bold};
    KeyValue core_{look_, "Core"}, licence_{look_, "Licence"}, source_{look_, "Source"}, gated_{look_, "Gated"},
        module_{look_, "Module"}, author_{look_, "Author"}, clock_{look_, "Clock"}, package_{look_, "Package"};
    Heading hCredits_{look_, "Credits and notices"};
    Text credits_{look_, 12.5f, Look::C::rowText, Look::Regular};
    Heading hPlugin_{look_, "FM1VST"};
    Text plugin_{look_, 12.5f, Look::C::rowText, Look::Regular};
};

// ============================================================ a theme ===
namespace {
// which of the five colours the picker edits
class ColourTargets : public Widget {
public:
    using Widget::Widget;
    std::array<Colour, 5> colours{};
    std::array<bool, 5> active{};
    int sel = 0;
    std::function<void(int)> onChange;
    static const char *name(int i)
    {
        static const char *const n[5] = {"Base", "Membrane", "Knobs", "Bed", "Label"};
        return n[i];
    }
    int heightFor(int) const override { return 52; }
    Rectangle<float> cell(int i) const
    {
        const float w = (float)getWidth() / 5.0f;
        return Rectangle<float>((float)i * w, 4, w, 44).reduced(2, 0);
    }
    void paint(juce::Graphics &g) override
    {
        for (int i = 0; i < 5; i++) {
            const Rectangle<float> c = cell(i);
            g.setColour(C(i == sel ? Look::C::segOn : Look::C::raised));
            g.fillRoundedRectangle(c, 8);
            g.setColour(i == sel ? look.accent() : C(Look::C::line));
            g.drawRoundedRectangle(c.reduced(0.5f), 8, i == sel ? 1.5f : 1.0f);
            const Rectangle<float> dot = Rectangle<float>(14, 14).withCentre({c.getCentreX(), c.getY() + 14});
            g.setColour(active[(size_t)i] ? colours[(size_t)i] : colours[(size_t)i].withAlpha(0.3f));
            g.fillEllipse(dot);
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.drawEllipse(dot, 1.0f);
            g.setFont(Look::font(11.0f, Look::SemiBold));
            g.setColour(C(i == sel ? Look::C::text : Look::C::muted));
            g.drawFittedText(name(i), c.withTrimmedTop(24).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        for (int i = 0; i < 5; i++)
            if (cell(i).contains(e.position) && i != sel) {
                sel = i;
                repaint();
                if (onChange)
                    onChange(i);
            }
    }
};
}   // namespace

class Sheet::ThemePage : public ScrollPage, private juce::ChangeListener {
public:
    ThemePage(Sheet &, FM1Editor &ed) : ScrollPage(ed.look()), ed_(ed), look_(ed.look())
    {
        Look::styleTextEditor(name_, 14.0f);
        name_.setTextToShowWhenEmpty("Theme name", C(Look::C::muted));
        name_.onTextChange = [this] { changed(); };
        name_.onReturnKey = [this] { save(false); };
        stack_.add(nameRow_);
        stack_.add(targets_);
        targets_.onChange = [this](int) { showTarget(); };
        stack_.add(override_);
        overrideSw_.onChange = [this](bool on) {
            on_[(size_t)targets_.sel] = on;
            showTarget();
            changed();
        };
        sel_.addChangeListener(this);
        stack_.add(selHolder_);
        stack_.add(buttons_);
        Pill &saveP = buttons_.add("Save", Look::Primary), &def = buttons_.add("Save as default"), &del = buttons_.add("Delete", Look::Danger);
        saveP.onClick = [this] { save(false); };
        def.onClick = [this] { save(true); };
        del.onClick = [this] {
            const String n = theme().name;
            note(ed_.themeStore().remove(n) ? "Deleted \"" + n + "\" (this instance keeps its colours)" : "\"" + n + "\" is not a custom theme file");
        };
        stack_.add(note_);
        sel_.setColour(juce::ColourSelector::backgroundColourId, C(Look::C::surface));
    }
    ~ThemePage() override { sel_.removeChangeListener(this); }
    void begin()
    {
        Theme t = ed_.proc().getTheme();
        if (ThemeStore::isPresetName(t.name))
            t.name = "My " + t.name.replace("/", " ");
        const Palette p = derive(t);
        setting_ = true;
        cols_ = {t.base, t.membrane, t.knob, t.bed.value_or(p.bed), t.label.value_or(p.label)};
        on_ = {true, true, true, t.bed.has_value(), t.label.has_value()};
        name_.setText(t.name, false);
        targets_.sel = 0;
        setting_ = false;
        showTarget();
        note("Changes preview live. Save writes " + ed_.themeStore().themesDir().getFullPathName() + "/<name>.json");
    }

private:
    Theme theme() const
    {
        Theme t;
        t.name = name_.getText().trim();
        t.base = cols_[0];
        t.membrane = cols_[1];
        t.knob = cols_[2];
        t.bed = on_[3] ? std::optional<Colour>(cols_[3]) : std::nullopt;
        t.label = on_[4] ? std::optional<Colour>(cols_[4]) : std::nullopt;
        return t;
    }
    void showTarget()
    {
        const int i = targets_.sel;
        setting_ = true;
        sel_.setCurrentColour(cols_[(size_t)i], juce::dontSendNotification);
        setting_ = false;
        override_.setVisible(i >= 3);
        override_.setLabel(i == 3 ? "Own bed colour" : "Own label colour");
        override_.setCaption(i == 3 ? "else a darker shade of the membrane" : "else white or dark, by contrast with the body");
        overrideSw_.setOn(on_[(size_t)i]);
        sel_.setEnabled(i < 3 || on_[(size_t)i]);
        sel_.setAlpha(i < 3 || on_[(size_t)i] ? 1.0f : 0.35f);
        for (int k = 0; k < 5; k++) {
            targets_.colours[(size_t)k] = cols_[(size_t)k];
            targets_.active[(size_t)k] = on_[(size_t)k];
        }
        targets_.repaint();
        relayout();
    }
    void changeListenerCallback(juce::ChangeBroadcaster *) override
    {
        if (setting_)
            return;
        cols_[(size_t)targets_.sel] = sel_.getCurrentColour().withAlpha((juce::uint8)255);
        targets_.colours[(size_t)targets_.sel] = cols_[(size_t)targets_.sel];
        targets_.repaint();
        changed();
    }
    void changed()
    {
        if (!setting_)
            ed_.applyTheme(theme());
    }
    void note(const String &s) { note_.setText(s, C(Look::C::muted)), relayout(); }
    void save(bool asDefault)
    {
        const Theme t = theme();
        ThemeStore &store = ed_.themeStore();
        if (t.name.isEmpty()) {
            note("Give the theme a name");
            return;
        }
        const auto p = ThemeStore::preset(t.name);
        if (p && !p->sameColours(t)) {
            note("\"" + t.name + "\" is a preset: pick another name");
            return;
        }
        if (!p && !store.save(t)) {
            note("Cannot write " + store.fileFor(t.name).getFullPathName());
            return;
        }
        if (asDefault)
            store.setDefaultName(t.name);
        ed_.applyTheme(t);
        note(asDefault ? "Saved; the default for new instances" : "Saved to " + store.fileFor(t.name).getFullPathName());
    }

    FM1Editor &ed_;
    Look &look_;
    juce::TextEditor name_;
    Row nameRow_{look_, "Name", &name_, 196, 32};
    ColourTargets targets_{look_};
    Switch overrideSw_{look_};
    Row override_{look_, "Own bed colour", &overrideSw_, Switch::kW, 24};
    juce::ColourSelector sel_{juce::ColourSelector::showColourAtTop | juce::ColourSelector::editableColour |
                              juce::ColourSelector::showColourspace};
    Holder selHolder_{look_, sel_, 236};
    PillGrid buttons_{look_, 3};
    Text note_{look_, 11.5f, Look::C::muted};
    std::array<Colour, 5> cols_{};
    std::array<bool, 5> on_{};
    bool setting_ = false;
};

// =========================================== the Standalone's audio ===
class Sheet::AudioHolder : public juce::Component {
public:
    explicit AudioHolder(juce::Component &selector) : sel_(selector)
    {
        vp_.setViewedComponent(&sel_, false);
        vp_.setScrollBarsShown(true, false);
        vp_.setScrollBarThickness(8);
        addAndMakeVisible(vp_);
    }
    void resized() override
    {
        vp_.setBounds(getLocalBounds().reduced(8, 4));
        sel_.setSize(vp_.getWidth() - 10, juce::jmax(sel_.getHeight(), 300));
    }

private:
    juce::Component &sel_;
    juce::Viewport vp_;
};

// ============================================================ the sheet ===
Sheet::Sheet(FM1Editor &ed) : ed_(ed), look_(ed.look()), back_(ed.look(), Look::ChevronLeft), close_(ed.look(), Look::Close)
{
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);
    catalog_ = ed.proc().hubService().catalog();  // (the native cores are scanned when the sheet opens)
    ed.proc().hubService().addChangeListener(this);
    main_ = std::make_unique<MainPage>(*this, ed);
    firmware_ = std::make_unique<FirmwarePage>(*this, ed);
    about_ = std::make_unique<AboutPage>(*this, ed);
    theme_ = std::make_unique<ThemePage>(*this, ed);
    for (juce::Component *c : std::initializer_list<juce::Component *>{main_.get(), firmware_.get(), about_.get(), theme_.get()})
        addChildComponent(*c);
    addAndMakeVisible(back_);
    addAndMakeVisible(close_);
    back_.onClick = [this] { showPage(kMain); };
    close_.onClick = [this] { close(); };
    back_.setTooltip("Back");
    close_.setTooltip("Close (Esc)");
    showPage(kMain);
}

Sheet::~Sheet()
{
    ed_.proc().hubService().removeChangeListener(this);
    stopTimer();
    if (audio_)
        fm1standalone::saveAudioState();
}

void Sheet::reloadSources()
{
    natives_ = ed_.proc().firmwareCores();
    catalog_ = ed_.proc().hubService().catalog();
}

void Sheet::changeListenerCallback(juce::ChangeBroadcaster *)
{
    // the Hub's catalog came (or a download moved): the browser's list again, when it shows
    const hub::Catalog c = ed_.proc().hubService().catalog();
    const bool changed = c.updated != catalog_.updated || c.origin != catalog_.origin || c.firmwares.size() != catalog_.firmwares.size();
    catalog_ = c;
    if (page_ == kFirmware)
        changed ? firmware_->rebuild() : firmware_->tick();
    else if (page_ == kMain && isOpen())
        main_->refresh(false);
}

void Sheet::setEngineFilter(int engine) { firmware_->setEngine(engine); }

void Sheet::pickInBrowser(const String &key, int engine)
{
    FM1Processor &p = ed_.proc();
    std::optional<FirmwareItem> it;
    for (FirmwareItem &i : firmware_->items())
        if (i.key == key)
            it = std::move(i);
    if (!it)
        return;
    if (engine < 1 || (engine == 1 && !it->native) || (engine == 2 && !it->emu))
        engine = it->defaultEngine();
    const FM1Processor::EmuFirmware fw = p.getEmuFirmware();
    if (engine == 1) {
        if (fw.valid() || u8(p.getCoreInfo().info.id) != u8(it->native->id))
            p.switchCore(u8(it->native->id));
    } else if (!(fw.valid() && fw.sha256 == it->emu->sha256)) {
        p.selectEmuFirmware(*it->emu);           // (from the cache at once; else downloaded, the progress on the main page)
    }
    ed_.coreChanged();
    showPage(kMain);
}

void Sheet::open(bool animate)
{
    reloadSources();
    ed_.proc().hubService().refresh();           // (the worker: at most every 10 minutes)
    showPage(kMain);
    refresh();
    setVisible(true);
    toFront(false);
    target_ = 1;
    if (!animate) {
        t_ = 1;
        stopTimer();
        layoutCard();
        repaint();
    } else {
        animFrom_ = t_;
        animStart_ = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
    if (isShowing())
        grabKeyboardFocus();
}

void Sheet::close(bool animate)
{
    if (target_ < 0.5f)
        return;
    target_ = 0;
    main_->endInline();
    if (!animate) {
        t_ = 0;
        stopTimer();
        setVisible(false);
    } else {
        animFrom_ = t_;
        animStart_ = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
    if (audio_)
        fm1standalone::saveAudioState();
    ed_.focusActiveView();
}

void Sheet::timerCallback()
{
    const float dur = target_ > 0.5f ? 180.0f : 150.0f;
    const float x = juce::jlimit(0.0f, 1.0f, (float)(juce::Time::getMillisecondCounterHiRes() - animStart_) / dur);
    t_ = animFrom_ + (target_ - animFrom_) * x;
    if (x >= 1.0f) {
        t_ = target_;
        stopTimer();
        if (t_ <= 0)
            setVisible(false);
    }
    layoutCard();
    repaint();
}

int Sheet::sheetWidth() const
{
    const int want = page_ == kFirmware ? 620 : page_ == kAudio ? 440 : page_ == kAbout ? 400 : 360;
    return juce::jmax(200, juce::jmin(want, getWidth() - 16));
}

void Sheet::layoutCard()
{
    const int sw = sheetWidth();
    const float e = 1.0f - std::pow(1.0f - t_, 3.0f);   // (ease out)
    const int off = juce::roundToInt((1.0f - e) * (float)(sw + 24));
    cardBounds_ = Rectangle<int>(getWidth() - sw - 8 + off, 8, sw, juce::jmax(80, getHeight() - 16));
    const Rectangle<int> head = cardBounds_.withHeight(kHeaderH);
    close_.setBounds(Rectangle<int>(30, 30).withCentre({head.getRight() - 26, head.getCentreY()}));
    back_.setBounds(Rectangle<int>(30, 30).withCentre({head.getX() + 24, head.getCentreY()}));
    back_.setVisible(page_ != kMain);
    if (current_)
        current_->setBounds(cardBounds_.withTrimmedTop(kHeaderH).reduced(0, 1).withTrimmedBottom(8));
}

void Sheet::resized() { layoutCard(); }

void Sheet::paint(juce::Graphics &g)
{
    const float e = 1.0f - std::pow(1.0f - t_, 3.0f);
    g.fillAll(juce::Colours::black.withAlpha((page_ == kTheme ? 0.12f : 0.45f) * e));
    Look::drawSheetBackground(g, cardBounds_.toFloat());
    // the header: the mark (main) or the page's title
    const Rectangle<float> head = cardBounds_.withHeight(kHeaderH).toFloat();
    if (page_ == kMain) {
        const Rectangle<float> mark(head.getX() + 18, head.getCentreY() - 10, 20, 20);
        g.setGradientFill(juce::ColourGradient(C(0xFFFFA45E), mark.getTopLeft(), C(0xFFE07A3C), mark.getBottomRight(), false));
        g.fillRoundedRectangle(mark, 6);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawRoundedRectangle(mark.reduced(0.5f), 6, 1);
        g.setFont(Look::font(15.5f, Look::Bold, 0.02f));
        g.setColour(C(Look::C::text));
        g.drawText("FM1VST", Rectangle<float>(mark.getRight() + 9, head.getY(), 120, head.getHeight()), juce::Justification::centredLeft,
                   false);
        g.setFont(Look::font(12.0f, Look::SemiBold));
        g.setColour(C(Look::C::heading));
        g.drawText(JucePlugin_VersionString, Rectangle<float>(mark.getRight() + 76, head.getY() + 1, 60, head.getHeight()),
                   juce::Justification::centredLeft, false);
    } else {
        static const char *const titles[] = {"", "Firmware", "About", "Custom theme", "Audio / MIDI"};
        g.setFont(Look::font(15.5f, Look::Bold));
        g.setColour(C(Look::C::text));
        g.drawText(titles[(int)page_], head.withTrimmedLeft(46).withTrimmedRight(50), juce::Justification::centredLeft, true);
    }
    g.setColour(C(Look::C::divider));
    g.fillRect(head.getX() + 1, head.getBottom() - 1, head.getWidth() - 2, 1.0f);
}

void Sheet::showPage(Page p)
{
    if (p == kAudio && !audio_) {
        audio_ = fm1standalone::createAudioSettings();
        if (!audio_)
            return;
        audioPage_ = std::make_unique<AudioHolder>(*audio_);
        addChildComponent(*audioPage_);
    }
    page_ = p;
    juce::Component *next = p == kMain ? (juce::Component *)main_.get()
                            : p == kFirmware ? (juce::Component *)firmware_.get()
                            : p == kAbout    ? (juce::Component *)about_.get()
                            : p == kTheme    ? (juce::Component *)theme_.get()
                                             : (juce::Component *)audioPage_.get();
    if (current_ && current_ != next)
        current_->setVisible(false);
    current_ = next;
    if (p == kFirmware) {
        if (natives_.empty())
            reloadSources();
        ed_.proc().hubService().refresh();
        firmware_->rebuild();
    } else if (p == kAbout)
        about_->refresh();
    else if (p == kTheme)
        theme_->begin();
    else if (p == kMain)
        main_->refresh();
    current_->setVisible(true);
    layoutCard();
    repaint();
    if (p == kFirmware)
        firmware_->focusSearch();
    else if (isShowing() && !hasKeyboardFocus(true))
        grabKeyboardFocus();
}

void Sheet::refresh()
{
    if (page_ == kMain)
        main_->refresh(false);
    else if (page_ == kFirmware)
        firmware_->tick();
}

void Sheet::beginRename() { main_->beginRename(); }
void Sheet::beginSaveAs() { main_->beginSaveAs(); }
void Sheet::beginDeleteConfirm() { main_->beginDeleteConfirm(); }
void Sheet::beginResetConfirm() { main_->beginResetConfirm(); }
void Sheet::scrollToView()
{
    showPage(kMain);
    main_->scrollToView();
}
void Sheet::scrollToEditors()
{
    showPage(kMain);
    main_->scrollToEditors();
}
void Sheet::scrollToVideo()
{
    showPage(kMain);
    main_->scrollToVideo();
}
void Sheet::setBrowserFilter(const String &search, int category)
{
    firmware_->setFilter(search, category);
}

void Sheet::mouseDown(const juce::MouseEvent &e)
{
    if (!cardBounds_.contains(e.getPosition()))
        close();
}

bool Sheet::keyPressed(const juce::KeyPress &k)
{
    if (k == juce::KeyPress::escapeKey) {
        close();
        return true;
    }
    // the computer keys still play the panel while the sheet itself has the focus (not while typing in a field)
    return hasKeyboardFocus(false) && ed_.panel().keyPressed(k);
}

bool Sheet::keyStateChanged(bool down) { return ed_.panel().keyStateChanged(down); }
void Sheet::modifierKeysChanged(const juce::ModifierKeys &m) { ed_.panel().modifierKeysChanged(m); }
void Sheet::focusLost(FocusChangeType c) { ed_.panel().focusLost(c); }
