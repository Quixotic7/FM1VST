// SPDX-License-Identifier: GPL-3.0-only
// The small controls of the settings sheet (Sheet.h) and the editor's corner button, all drawn by Look: a switch, a
// segmented control, a stepper, a pill button, a labelled row, wrapped text, a stack that lays its visible items out
// top to bottom. None takes the keyboard focus (the sheet keeps it, and passes the computer keys on to the panel).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "Look.h"

// a control of the sheet: knows its look, its height for a width, tracks the mouse
class Widget : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit Widget(Look &l) : look(l) { setWantsKeyboardFocus(false); }
    virtual int heightFor(int /*width*/) const { return 40; }
    void setEnabledLook(bool e)
    {
        if (e != enabled_) {
            enabled_ = e;
            repaint();
        }
    }
    bool enabledLook() const { return enabled_; }
    void mouseEnter(const juce::MouseEvent &) override { setHover(true); }
    void mouseExit(const juce::MouseEvent &) override { setHover(false); }

protected:
    void setHover(bool h)
    {
        if (h != hover_) {
            hover_ = h;
            repaint();
        }
    }
    Look &look;
    bool hover_ = false, enabled_ = true;
};

class Switch : public Widget {
public:
    using Widget::Widget;
    void setOn(bool on)
    {
        if (on != on_) {
            on_ = on;
            repaint();
        }
    }
    bool isOn() const { return on_; }
    std::function<void(bool)> onChange;
    void paint(juce::Graphics &g) override { look.drawSwitch(g, getLocalBounds().toFloat(), on_, enabled_, hover_); }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (enabled_ && e.mouseWasClicked() && getLocalBounds().contains(e.getPosition())) {
            on_ = !on_;
            repaint();
            if (onChange)
                onChange(on_);
        }
    }
    static constexpr int kW = 40;

private:
    bool on_ = false;
};

class Segmented : public Widget {
public:
    Segmented(Look &l, juce::StringArray labels) : Widget(l), labels_(std::move(labels)) {}
    void setSelected(int i)
    {
        if (i != sel_) {
            sel_ = i;
            repaint();
        }
    }
    int selected() const { return sel_; }
    std::function<void(int)> onChange;
    int preferredWidth() const;
    void paint(juce::Graphics &g) override;
    void mouseMove(const juce::MouseEvent &e) override;
    void mouseExit(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &e) override;

private:
    int cellAt(juce::Point<float> p) const;
    juce::StringArray labels_;
    int sel_ = 0, hoverCell_ = -1;
};

class Stepper : public Widget, private juce::Timer {
public:
    Stepper(Look &l, int lo, int hi, std::function<juce::String(int)> fmt) : Widget(l), lo_(lo), hi_(hi), fmt_(std::move(fmt)) {}
    void setValue(int v)
    {
        if (v != v_) {
            v_ = v;
            repaint();
        }
    }
    int value() const { return v_; }
    std::function<void(int)> onChange;
    void paint(juce::Graphics &g) override;
    void mouseMove(const juce::MouseEvent &e) override;
    void mouseExit(const juce::MouseEvent &) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &) override { stopTimer(); }
    static constexpr int kW = 104;

private:
    void timerCallback() override;
    int zoneAt(juce::Point<float> p) const;   // 0 minus, 1 plus, -1 the value
    void step();
    int lo_, hi_, v_ = 0, hoverZone_ = -1, held_ = -1;
    std::function<juce::String(int)> fmt_;
};

class Pill : public Widget {
public:
    Pill(Look &l, juce::String text, Look::PillKind k = Look::Plain) : Widget(l), text_(std::move(text)), kind_(k) {}
    void setText(const juce::String &t)
    {
        text_ = t;
        repaint();
    }
    void setKind(Look::PillKind k)
    {
        kind_ = k;
        repaint();
    }
    void setFontPx(float px) { fontPx_ = px; }
    std::function<void()> onClick;
    int preferredWidth() const { return (int)std::ceil(Look::textWidth(Look::font(fontPx_, Look::SemiBold), text_)) + 28; }
    void paint(juce::Graphics &g) override
    {
        look.drawPill(g, getLocalBounds().toFloat().reduced(0.5f), text_, kind_, hover_, isMouseButtonDown(), enabled_, fontPx_);
    }
    void mouseDown(const juce::MouseEvent &) override { repaint(); }
    void mouseUp(const juce::MouseEvent &e) override
    {
        repaint();
        if (enabled_ && e.mouseWasClicked() && getLocalBounds().contains(e.getPosition()) && onClick)
            onClick();
    }

private:
    juce::String text_;
    Look::PillKind kind_;
    float fontPx_ = 13.0f;
};

// a round icon button (the sheet's close and back)
class IconButton : public Widget {
public:
    IconButton(Look &l, Look::Icon i) : Widget(l), icon_(i) {}
    std::function<void()> onClick;
    void paint(juce::Graphics &g) override;
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mouseWasClicked() && getLocalBounds().contains(e.getPosition()) && onClick)
            onClick();
    }

private:
    Look::Icon icon_;
};

// wrapped text (a status, a note, credits): its height follows its width
class Text : public Widget {
public:
    Text(Look &l, float px = 12.5f, juce::uint32 colour = Look::C::muted, Look::Weight w = Look::Medium)
        : Widget(l), px_(px), colour_(colour), weight_(w)
    {
        setInterceptsMouseClicks(false, false);
    }
    void setText(const juce::String &t, juce::Colour c);
    void setText(const juce::String &t) { setText(t, juce::Colour(colour_)); }
    const juce::String &text() const { return text_; }
    void setLed(juce::Colour c) { led_ = c, repaint(); }    // transparent: none
    void setPadding(int top, int bottom) { padTop_ = top, padBottom_ = bottom; }
    int heightFor(int width) const override;
    void paint(juce::Graphics &g) override;

private:
    juce::TextLayout layoutFor(float width) const;
    juce::String text_;
    float px_;
    juce::uint32 colour_;
    juce::Colour c_{Look::C::muted}, led_{juce::Colours::transparentBlack};
    Look::Weight weight_;
    int padTop_ = 2, padBottom_ = 8;
};

// a section heading in small caps (the first of a page has less space above)
class Heading : public Widget {
public:
    Heading(Look &l, juce::String t, bool first = false) : Widget(l), text_(std::move(t)), first_(first)
    {
        setInterceptsMouseClicks(false, false);
    }
    int heightFor(int) const override { return first_ ? 28 : 45; }
    void paint(juce::Graphics &g) override;

private:
    juce::String text_;
    bool first_;
};

// a label (and a caption under it) on the left, one control on the right; or, with onClick, a link row: the value and
// a chevron on the right
class Row : public Widget {
public:
    Row(Look &l, juce::String label, juce::Component *control = nullptr, int controlW = 0, int controlH = 28)
        : Widget(l), label_(std::move(label)), control_(control), controlW_(controlW), controlH_(controlH)
    {
        if (control_)
            addAndMakeVisible(*control_);
    }
    void setLabel(const juce::String &t)
    {
        label_ = t;
        repaint();
    }
    void setCaption(const juce::String &t, juce::Colour c = juce::Colour(Look::C::muted))
    {
        caption_ = t;
        captionColour_ = c;
        repaint();
    }
    void setValue(const juce::String &t)
    {
        value_ = t;
        repaint();
    }
    void setControlWidth(int w)
    {
        controlW_ = w;
        resized();
    }
    std::function<void()> onClick;                // (a link row)
    int heightFor(int) const override { return caption_.isEmpty() ? 40 : 50; }
    void paint(juce::Graphics &g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (onClick && enabled_ && e.mouseWasClicked() && getLocalBounds().contains(e.getPosition()))
            onClick();
    }

private:
    juce::String label_, caption_, value_;
    juce::Colour captionColour_{Look::C::muted};
    juce::Component *control_;
    int controlW_, controlH_;
};

// a thin line between groups
class Divider : public Widget {
public:
    using Widget::Widget;
    int heightFor(int) const override { return 1; }
    void paint(juce::Graphics &g) override;
};

// the items laid out top to bottom at their heights (the hidden ones skipped), padded left and right
class Stack : public juce::Component {
public:
    explicit Stack(int pad = 16) : pad_(pad) {}
    void add(Widget &w, bool fullBleed = false)
    {
        items_.push_back({&w, fullBleed});
        addAndMakeVisible(w);
    }
    int layout(int width);                        // sets the children's bounds; the total height
    void resized() override
    {
        const int h = layout(getWidth());
        if (h != getHeight())
            setSize(getWidth(), h);
    }

private:
    struct Item {
        Widget *w;
        bool fullBleed;
    };
    std::vector<Item> items_;
    int pad_;
};

// the editor's corner button: a round 28 point "more" button that fades in while the mouse moves over the editor,
// and a small LED for Remote FM-1 that stays when the button fades (green connected, amber searching / in use / lost)
class CornerButton : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit CornerButton(Look &);
    void setFade(float a);                        // 0..1, the button's opacity (the LED is always drawn)
    float fade() const { return fade_; }
    void setLed(juce::Colour c);                  // transparent: none
    void setAlarm(bool on);                       // the firmware halted: a red ring
    std::function<void()> onClick;
    void paint(juce::Graphics &g) override;
    bool hitTest(int x, int y) override;
    void mouseEnter(const juce::MouseEvent &) override { hover_ = true, repaint(); }
    void mouseExit(const juce::MouseEvent &) override { hover_ = false, repaint(); }
    void mouseUp(const juce::MouseEvent &e) override;
    bool hovered() const { return hover_; }
    static constexpr int kPad = 7;                // around the drawn button: room for its shadow and the LED's glow

private:
    float fade_ = 0;
    juce::Colour led_{juce::Colours::transparentBlack};
    bool alarm_ = false, hover_ = false;
};

// the video recorder's button (the Standalone; VideoCapture.h), opposite the corner button and in its language (the
// site's recorder): idle, a round button with a red dot that fades with the corner button; recording, a pill with the
// dot pulsing and the take's running time in red, always shown; after a take, "Show in Finder" for a few seconds
class RecordButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    RecordButton();
    enum State { kIdle, kRecording, kSaved };
    void setState(State s);
    State state() const { return state_; }
    void setDiameter(float d) { d_ = d; }
    float diameter() const { return d_; }
    void setFade(float a);                        // 0..1 (idle only: recording and saved are always shown)
    float fade() const { return fade_; }
    juce::String text() const;                    // what the pill says now ("" idle)
    int preferredWidth() const;                   // the drawn shape's width at the diameter (logical)
    std::function<double()> seconds;              // the take's time (recording)
    std::function<void()> onClick;
    void paint(juce::Graphics &g) override;
    bool hitTest(int x, int y) override;
    void mouseEnter(const juce::MouseEvent &) override { hover_ = true, repaint(); }
    void mouseExit(const juce::MouseEvent &) override { hover_ = false, repaint(); }
    void mouseUp(const juce::MouseEvent &e) override;
    static constexpr int kPad = 7;                // (as the corner button's: room for the shadow and the pulse)

private:
    void timerCallback() override;
    juce::Rectangle<float> shape() const;
    State state_ = kIdle;
    float d_ = 28, fade_ = 0;
    bool hover_ = false;
    juce::String shown_;
};
