// SPDX-License-Identifier: GPL-3.0-only
// The sheet's controls and the corner button (see Widgets.h).
#include "Widgets.h"

using juce::Rectangle;

// ------------------------------------------------------------ Segmented ---
int Segmented::preferredWidth() const
{
    float w = 4;
    for (const juce::String &l : labels_)
        w += Look::textWidth(Look::font(13.0f, Look::SemiBold), l) + 22.0f;
    return (int)std::ceil(w);
}

void Segmented::paint(juce::Graphics &g)
{
    Look::drawSegmented(g, getLocalBounds().toFloat().reduced(0.5f), labels_, sel_, hoverCell_, enabled_);
}

int Segmented::cellAt(juce::Point<float> p) const
{
    const auto cells = Look::segmentedCells(getLocalBounds().toFloat().reduced(0.5f), labels_);
    for (size_t i = 0; i < cells.size(); i++)
        if (cells[i].expanded(0, 4).contains(p))
            return (int)i;
    return -1;
}

void Segmented::mouseMove(const juce::MouseEvent &e)
{
    const int c = cellAt(e.position);
    if (c != hoverCell_) {
        hoverCell_ = c;
        repaint();
    }
}

void Segmented::mouseExit(const juce::MouseEvent &)
{
    hoverCell_ = -1;
    repaint();
}

void Segmented::mouseUp(const juce::MouseEvent &e)
{
    const int c = cellAt(e.position);
    if (enabled_ && e.mouseWasClicked() && c >= 0 && c != sel_) {
        sel_ = c;
        repaint();
        if (onChange)
            onChange(c);
    }
}

// -------------------------------------------------------------- Stepper ---
void Stepper::paint(juce::Graphics &g)
{
    Look::drawStepper(g, getLocalBounds().toFloat().reduced(0.5f), fmt_ ? fmt_(v_) : juce::String(v_), hoverZone_, enabled_);
}

int Stepper::zoneAt(juce::Point<float> p) const
{
    if (p.x < 30)
        return 0;
    if (p.x >= (float)getWidth() - 30)
        return 1;
    return -1;
}

void Stepper::mouseMove(const juce::MouseEvent &e)
{
    const int z = zoneAt(e.position);
    if (z != hoverZone_) {
        hoverZone_ = z;
        repaint();
    }
}

void Stepper::mouseExit(const juce::MouseEvent &)
{
    hoverZone_ = -1;
    stopTimer();
    repaint();
}

void Stepper::step()
{
    const int v = juce::jlimit(lo_, hi_, v_ + (held_ == 1 ? 1 : -1));
    if (v != v_) {
        v_ = v;
        repaint();
        if (onChange)
            onChange(v_);
    }
}

void Stepper::mouseDown(const juce::MouseEvent &e)
{
    held_ = zoneAt(e.position);
    if (!enabled_ || held_ < 0)
        return;
    step();
    startTimer(400);                              // (held: repeats)
}

void Stepper::timerCallback()
{
    if (!isMouseButtonDown()) {
        stopTimer();
        return;
    }
    step();
    startTimer(70);
}

// ----------------------------------------------------------- IconButton ---
void IconButton::paint(juce::Graphics &g)
{
    const Rectangle<float> r = getLocalBounds().toFloat();
    if (hover_) {
        g.setColour(Look::col(Look::C::raised));
        g.fillEllipse(r.reduced(1));
    }
    Look::strokeIcon(g, icon_, r.withSizeKeepingCentre(16, 16), Look::col(hover_ ? Look::C::text : Look::C::knobOff), 1.6f);
}

// ----------------------------------------------------------------- Text ---
void Text::setText(const juce::String &t, juce::Colour c)
{
    if (t == text_ && c == c_)
        return;
    const bool relayout = t != text_;
    text_ = t;
    c_ = c;
    repaint();
    if (relayout)
        if (auto *p = getParentComponent())
            p->resized();
}

juce::TextLayout Text::layoutFor(float width) const
{
    juce::AttributedString s;
    s.append(text_, Look::font(px_, weight_), c_);
    s.setLineSpacing(px_ * 0.25f);
    juce::TextLayout tl;
    tl.createLayout(s, juce::jmax(10.0f, width - (led_.isTransparent() ? 0.0f : 16.0f)));
    return tl;
}

int Text::heightFor(int width) const
{
    if (text_.isEmpty())
        return 0;
    return (int)std::ceil(layoutFor((float)width).getHeight()) + padTop_ + padBottom_;
}

void Text::paint(juce::Graphics &g)
{
    Rectangle<float> r = getLocalBounds().toFloat().withTrimmedTop((float)padTop_).withTrimmedBottom((float)padBottom_);
    if (!led_.isTransparent()) {
        Look::drawLed(g, {r.getX() + 4.5f, r.getY() + px_ * 0.7f}, 3.5f, led_);
        r.removeFromLeft(16);
    }
    layoutFor((float)getWidth()).draw(g, r);
}

// -------------------------------------------------------- Heading, Row ---
void Heading::paint(juce::Graphics &g)
{
    Look::drawHeading(g, text_, getLocalBounds().toFloat().withTrimmedTop(first_ ? 4.0f : 18.0f).withHeight(18));
}

void Row::paint(juce::Graphics &g)
{
    Rectangle<float> r = getLocalBounds().toFloat();
    const float a = enabled_ ? 1.0f : 0.45f;
    if (onClick && hover_ && enabled_) {
        g.setColour(Look::col(Look::C::raised));
        g.fillRoundedRectangle(r.expanded(8, 0).reduced(0, 2), 8);
    }
    Rectangle<float> right;
    if (onClick) {
        right = r.removeFromRight(16);
        Look::strokeIcon(g, Look::ChevronRight, right.withSizeKeepingCentre(14, 14), Look::col(Look::C::knobOff).withMultipliedAlpha(a),
                         1.7f);
        if (value_.isNotEmpty()) {
            const juce::Font f = Look::font(13.0f, Look::Medium);
            const float w = juce::jmin(r.getWidth() * 0.5f, Look::textWidth(f, value_) + 8);
            g.setFont(f);
            g.setColour(Look::col(Look::C::muted).withMultipliedAlpha(a));
            g.drawText(value_, r.removeFromRight(w), juce::Justification::centredRight, true);
        }
    } else if (control_) {
        r.removeFromRight((float)controlW_ + 10);
    } else if (value_.isNotEmpty()) {             // (a value alone on the right)
        const juce::Font f = Look::font(13.0f, Look::Medium);
        const float w = juce::jmin(r.getWidth() * 0.6f, Look::textWidth(f, value_) + 8);
        g.setFont(f);
        g.setColour(Look::col(Look::C::muted).withMultipliedAlpha(a));
        g.drawText(value_, r.removeFromRight(w), juce::Justification::centredRight, true);
    }
    Rectangle<float> label = r;
    if (caption_.isNotEmpty()) {
        Rectangle<float> cap = label.removeFromBottom(label.getHeight() * 0.44f);
        g.setFont(Look::font(11.5f, Look::Medium));
        g.setColour(captionColour_.withMultipliedAlpha(a));
        g.drawFittedText(caption_, cap.toNearestInt().withTrimmedTop(-1), juce::Justification::topLeft, 1, 0.85f);
        label = label.withTrimmedTop(4);
    }
    g.setFont(Look::font(14.0f, Look::Medium));
    g.setColour(Look::col(Look::C::rowText).withMultipliedAlpha(a));
    g.drawFittedText(label_, label.toNearestInt(), caption_.isNotEmpty() ? juce::Justification::bottomLeft : juce::Justification::centredLeft,
                     1, 0.8f);
}

void Row::resized()
{
    if (control_ && !onClick)
        control_->setBounds(getWidth() - controlW_, (getHeight() - controlH_) / 2, controlW_, controlH_);
}

void Divider::paint(juce::Graphics &g)
{
    g.setColour(Look::col(Look::C::divider));
    g.fillRect(getLocalBounds());
}

int Stack::layout(int width)
{
    int y = 0;
    for (const Item &it : items_) {
        if (!it.w->isVisible())
            continue;
        const int x = it.fullBleed ? 0 : pad_, w = it.fullBleed ? width : width - 2 * pad_;
        const int h = it.w->heightFor(w);
        it.w->setBounds(x, y, w, h);
        y += h;
    }
    return y;
}

// --------------------------------------------------------- CornerButton ---
CornerButton::CornerButton(Look &)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setTitle("Settings");
    setDescription("FM1VST settings (or right-click the panel)");
}

void CornerButton::setFade(float a)
{
    a = juce::jlimit(0.0f, 1.0f, a);
    if (std::abs(a - fade_) > 1e-4f) {
        fade_ = a;
        repaint();
    }
}

void CornerButton::setLed(juce::Colour c)
{
    if (c != led_) {
        led_ = c;
        repaint();
    }
}

void CornerButton::setAlarm(bool on)
{
    if (on != alarm_) {
        alarm_ = on;
        repaint();
    }
}

bool CornerButton::hitTest(int x, int y)
{
    // (faded out it lets every click through to the panel under it)
    return fade_ > 0.5f && getLocalBounds().toFloat().reduced((float)kPad).contains((float)x, (float)y);
}

void CornerButton::mouseUp(const juce::MouseEvent &e)
{
    if (e.mouseWasClicked() && getLocalBounds().contains(e.getPosition()) && onClick)
        onClick();
}

void CornerButton::paint(juce::Graphics &g)
{
    const Rectangle<float> r = getLocalBounds().toFloat().reduced((float)kPad + 1.0f);
    const float a = fade_;
    if (a > 0.001f) {
        juce::Path p;
        p.addEllipse(r);
        juce::DropShadow(juce::Colours::black.withAlpha(0.45f * a), 8, {0, 2}).drawForPath(g, p);
        g.setColour(Look::col(hover_ ? Look::C::raisedHover : Look::C::raised).withMultipliedAlpha(0.94f * a));
        g.fillEllipse(r);
        g.setColour(Look::col(Look::C::line).withMultipliedAlpha(a));
        g.drawEllipse(r.reduced(0.5f), 1.0f);
        const float s = juce::jmin(16.0f, r.getWidth() * 0.57f);
        Look::strokeIcon(g, Look::Dots, r.withSizeKeepingCentre(s, s), Look::col(hover_ ? Look::C::text : Look::C::rowText).withMultipliedAlpha(a));
    }
    if (alarm_) {
        g.setColour(Look::col(Look::C::red).withMultipliedAlpha(juce::jmax(0.6f, a)));
        g.drawEllipse(r.reduced(0.75f), 1.5f);
    }
    if (!led_.isTransparent()) {   // (top right on the rim; kept when the button fades)
        const float rad = juce::jmax(2.5f, r.getWidth() * 0.11f);
        const juce::Point<float> c = r.getCentre() + juce::Point<float>(r.getWidth() * 0.5f * 0.7071f, -r.getWidth() * 0.5f * 0.7071f);
        g.setColour(Look::col(Look::C::window).withAlpha(0.85f));
        g.fillEllipse(Rectangle<float>(rad * 2 + 3, rad * 2 + 3).withCentre(c));
        Look::drawLed(g, c, rad, led_);
    }
}
