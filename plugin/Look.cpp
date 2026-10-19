// SPDX-License-Identifier: GPL-3.0-only
// The editor's look (see Look.h).
#include "Look.h"

#include <FontData.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>

using juce::Colour;
using juce::Rectangle;

// ------------------------------------------------------------------ type ---
static juce::Typeface::Ptr typeface(int w)
{
    static juce::Typeface::Ptr tf[4] = {
        juce::Typeface::createSystemTypefaceFor(FontData::InterTightRegular_ttf, (size_t)FontData::InterTightRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor(FontData::InterTightMedium_ttf, (size_t)FontData::InterTightMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor(FontData::InterTightSemiBold_ttf, (size_t)FontData::InterTightSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor(FontData::InterTightBold_ttf, (size_t)FontData::InterTightBold_ttfSize),
    };
    return tf[juce::jlimit(0, 3, w)];
}

juce::Font Look::font(float px, Weight w, float tracking)
{
    juce::FontOptions o(typeface((int)w));
    o = o.withPointHeight(px);
    if (tracking != 0.0f)
        o = o.withKerningFactor(tracking);
    return juce::Font(o);
}

float Look::textWidth(const juce::Font &f, const juce::String &s) { return juce::GlyphArrangement::getStringWidth(f, s); }

Look::Look()
{
    setColour(juce::ResizableWindow::backgroundColourId, col(C::window));
    setColour(juce::PopupMenu::backgroundColourId, col(C::surface).withAlpha(0.99f));   // (not opaque: round corners)
    setColour(juce::PopupMenu::textColourId, col(C::text));
    setColour(juce::PopupMenu::headerTextColourId, col(C::heading));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col(C::raisedHover));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextEditor::backgroundColourId, col(C::well));
    setColour(juce::TextEditor::textColourId, col(C::text));
    setColour(juce::TextEditor::outlineColourId, col(C::line));
    setColour(juce::TextEditor::focusedOutlineColourId, accent_);
    setColour(juce::TextEditor::highlightColourId, accent_.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour(juce::CaretComponent::caretColourId, accent_);
    setColour(juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha(0.2f));
    setColour(juce::TooltipWindow::backgroundColourId, col(C::raised));
    setColour(juce::TooltipWindow::textColourId, col(C::text));
    setColour(juce::TooltipWindow::outlineColourId, col(C::border));
    setColour(juce::ColourSelector::backgroundColourId, col(C::surface));
    setColour(juce::ColourSelector::labelTextColourId, col(C::muted));
    setColour(juce::Label::textColourId, col(C::text));
    setColour(juce::Label::textWhenEditingColourId, col(C::text));
    setColour(juce::Slider::backgroundColourId, col(C::well));
    setColour(juce::Slider::trackColourId, col(C::segOn));
    setColour(juce::Slider::thumbColourId, col(C::text));
    setColour(juce::Slider::textBoxTextColourId, col(C::text));
    setColour(juce::Slider::textBoxBackgroundColourId, col(C::well));
    setColour(juce::Slider::textBoxOutlineColourId, col(C::line));
}

void Look::setAccentFrom(const Theme &t)
{
    const Colour m = t.membrane.withAlpha((juce::uint8)255);
    Colour a = m.getSaturation() < 0.18f ? col(C::fallbackAccent) : m;
    // lifted toward white until it reads on the sheet (contrast 3:1 with the surface)
    for (int i = 0; i < 40 && contrastRatio(a, col(C::surface)) < 3.0; i++)
        a = a.interpolatedWith(juce::Colours::white, 0.06f).withAlpha((juce::uint8)255);
    accent_ = a;
    setColour(juce::TextEditor::focusedOutlineColourId, accent_);
    setColour(juce::TextEditor::highlightColourId, accent_.withAlpha(0.35f));
    setColour(juce::CaretComponent::caretColourId, accent_);
}

Colour Look::onAccent() const
{
    return contrastRatio(accent_, Colour(0xFF1A0E06)) >= contrastRatio(accent_, juce::Colours::white) ? Colour(0xFF1A0E06)
                                                                                                    : juce::Colours::white;
}

// ----------------------------------------------------------------- icons ---
juce::Path Look::icon(Icon i, Rectangle<float> r)
{
    juce::Path p;
    auto dot = [&](float x, float y) {
        p.startNewSubPath(x, y);
        p.lineTo(x + 0.01f, y);
    };
    switch (i) {
    case Dots:
        dot(3.5f, 8);
        dot(8, 8);
        dot(12.5f, 8);
        break;
    case Gear:
        p.addEllipse(5.6f, 5.6f, 4.8f, 4.8f);
        for (int k = 0; k < 8; k++) {
            const float a = (float)k * juce::MathConstants<float>::pi / 4.0f;
            p.startNewSubPath(8 + 4.4f * std::cos(a), 8 + 4.4f * std::sin(a));
            p.lineTo(8 + 6.4f * std::cos(a), 8 + 6.4f * std::sin(a));
        }
        break;
    case ChevronLeft:
        p.startNewSubPath(10, 3.5f);
        p.lineTo(5.5f, 8);
        p.lineTo(10, 12.5f);
        break;
    case ChevronRight:
        p.startNewSubPath(6, 3.5f);
        p.lineTo(10.5f, 8);
        p.lineTo(6, 12.5f);
        break;
    case ChevronDown:
        p.startNewSubPath(4, 6);
        p.lineTo(8, 10);
        p.lineTo(12, 6);
        break;
    case Close:
        p.startNewSubPath(4, 4);
        p.lineTo(12, 12);
        p.startNewSubPath(12, 4);
        p.lineTo(4, 12);
        break;
    case Search:
        p.addEllipse(2.5f, 2.5f, 9, 9);
        p.startNewSubPath(10.2f, 10.2f);
        p.lineTo(13.5f, 13.5f);
        break;
    case Power:
        p.startNewSubPath(8, 2);
        p.lineTo(8, 7.5f);
        p.addCentredArc(8, 8.6f, 5.2f, 5.2f, 0, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
        break;
    case Save:
        p.startNewSubPath(3, 2.5f);
        p.lineTo(11, 2.5f);
        p.lineTo(13.5f, 5);
        p.lineTo(13.5f, 13.5f);
        p.lineTo(3, 13.5f);
        p.closeSubPath();
        p.startNewSubPath(5.5f, 2.5f);
        p.lineTo(5.5f, 6);
        p.lineTo(10.5f, 6);
        p.lineTo(10.5f, 2.5f);
        break;
    case Info:
        p.addEllipse(2, 2, 12, 12);
        p.startNewSubPath(8, 7.2f);
        p.lineTo(8, 11.2f);
        dot(8, 4.9f);
        break;
    case Check:
        p.startNewSubPath(3.5f, 8.5f);
        p.lineTo(6.5f, 11.5f);
        p.lineTo(12.5f, 4.5f);
        break;
    case Screen:
        p.addRoundedRectangle(2.5f, 2.5f, 11, 11, 2.0f);
        p.addRectangle(5, 5, 6, 6);
        break;
    case Device:
        p.addRoundedRectangle(1.5f, 4, 13, 8, 2.0f);
        p.addRectangle(4, 6, 3, 3);
        dot(10, 7.5f);
        dot(12, 7.5f);
        break;
    }
    p.applyTransform(juce::AffineTransform::scale(r.getWidth() / 16.0f, r.getHeight() / 16.0f).translated(r.getX(), r.getY()));
    return p;
}

void Look::strokeIcon(juce::Graphics &g, Icon i, Rectangle<float> r, Colour c, float width)
{
    const float s = r.getWidth() / 16.0f;
    const float w = (i == Dots ? 3.0f : width) * s;
    g.setColour(c);
    g.strokePath(icon(i, r), juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// --------------------------------------------------------------- pieces ---
void Look::drawHeading(juce::Graphics &g, const juce::String &text, Rectangle<float> r)
{
    g.setColour(col(C::heading));
    g.setFont(font(11.0f, Bold, 0.12f));
    g.drawText(text.toUpperCase(), r, juce::Justification::centredLeft, false);
}

void Look::drawSwitch(juce::Graphics &g, Rectangle<float> r, bool on, bool enabled, bool hover) const
{
    const Rectangle<float> t = r.withSizeKeepingCentre(38, 22);
    const float a = enabled ? 1.0f : 0.4f;
    Colour bg = on ? accent_ : col(C::switchOff);
    if (hover && enabled)
        bg = bg.brighter(0.08f);
    g.setColour(bg.withMultipliedAlpha(a));
    g.fillRoundedRectangle(t, 11);
    const Rectangle<float> knob = on ? Rectangle<float>(t.getRight() - 19, t.getY() + 3, 16, 16) : Rectangle<float>(t.getX() + 3, t.getY() + 3, 16, 16);
    if (on) {
        g.setColour(juce::Colours::black.withAlpha(0.18f * a));
        g.fillEllipse(knob.translated(0, 0.75f));
    }
    g.setColour((on ? juce::Colours::white : col(C::knobOff)).withMultipliedAlpha(a));
    g.fillEllipse(knob);
}

std::vector<Rectangle<float>> Look::segmentedCells(Rectangle<float> r, const juce::StringArray &labels)
{
    // cells as wide as their labels (+ 22), spread over r's width
    const juce::Font f = font(13.0f, SemiBold);
    std::vector<float> w;
    float total = 0;
    for (const juce::String &l : labels) {
        w.push_back(textWidth(f, l) + 22.0f);
        total += w.back();
    }
    const Rectangle<float> in = r.reduced(2);
    const float k = total > 0 ? in.getWidth() / total : 1.0f;
    std::vector<Rectangle<float>> cells;
    float x = in.getX();
    for (float cw : w) {
        cells.emplace_back(x, in.getY(), cw * k, in.getHeight());
        x += cw * k;
    }
    return cells;
}

void Look::drawSegmented(juce::Graphics &g, Rectangle<float> r, const juce::StringArray &labels, int sel, int hover, bool enabled)
{
    const float a = enabled ? 1.0f : 0.45f;
    g.setColour(col(C::well).withMultipliedAlpha(a));
    g.fillRoundedRectangle(r, 8);
    g.setColour(col(C::line).withMultipliedAlpha(a));
    g.drawRoundedRectangle(r.reduced(0.5f), 8, 1);
    const auto cells = segmentedCells(r, labels);
    g.setFont(font(13.0f, SemiBold));
    for (int i = 0; i < labels.size(); i++) {
        const Rectangle<float> c = cells[(size_t)i];
        if (i == sel) {
            g.setColour(col(C::segOn).withMultipliedAlpha(a));
            g.fillRoundedRectangle(c, 6);
            g.setColour(juce::Colours::white.withAlpha(0.06f * a));
            g.drawHorizontalLine((int)c.getY() + 1, c.getX() + 5, c.getRight() - 5);
        } else if (i == hover && enabled) {
            g.setColour(col(C::raised));
            g.fillRoundedRectangle(c, 6);
        }
        g.setColour((i == sel ? juce::Colours::white : i == hover ? col(C::rowText) : col(C::muted)).withMultipliedAlpha(a));
        g.drawText(labels[i], c, juce::Justification::centred, false);
    }
}

void Look::drawStepper(juce::Graphics &g, Rectangle<float> r, const juce::String &value, int hover, bool enabled)
{
    const float a = enabled ? 1.0f : 0.45f;
    g.setColour(col(C::well).withMultipliedAlpha(a));
    g.fillRoundedRectangle(r, 8);
    g.setColour(col(C::line).withMultipliedAlpha(a));
    g.drawRoundedRectangle(r.reduced(0.5f), 8, 1);
    const Rectangle<float> minus = r.withWidth(30), plus = r.withLeft(r.getRight() - 30);
    for (int i = 0; i < 2; i++) {
        const Rectangle<float> b = i ? plus : minus;
        if (hover == i && enabled) {
            g.setColour(col(C::raised));
            g.fillRoundedRectangle(b.reduced(2), 6);
        }
        const juce::Colour c = (hover == i && enabled ? col(C::text) : col(C::knobOff)).withMultipliedAlpha(a);
        g.setColour(c);
        const juce::Point<float> m = b.getCentre();
        g.drawLine(m.x - 4.5f, m.y, m.x + 4.5f, m.y, 1.6f);
        if (i)
            g.drawLine(m.x, m.y - 4.5f, m.x, m.y + 4.5f, 1.6f);
    }
    g.setColour(col(C::text).withMultipliedAlpha(a));
    g.setFont(font(13.0f, SemiBold));
    g.drawText(value, r.reduced(30, 0), juce::Justification::centred, false);
}

void Look::drawPill(juce::Graphics &g, Rectangle<float> r, const juce::String &text, PillKind k, bool hover, bool down,
                    bool enabled, float fontPx) const
{
    const float a = enabled ? 1.0f : 0.4f;
    Colour bg = col(C::raised), edge = col(C::line), fg = col(C::rowText);
    switch (k) {
    case Plain: break;
    case Primary:
        bg = accent_;
        edge = accent_.darker(0.2f);
        fg = onAccent();
        break;
    case Danger:
        fg = col(C::red);
        edge = col(C::red).withAlpha(0.35f);
        break;
    case DangerFill:
        bg = col(C::redFill);
        edge = col(C::redFill).darker(0.2f);
        fg = juce::Colours::white;
        break;
    case Ghost:
        bg = juce::Colours::transparentBlack;
        edge = juce::Colours::transparentBlack;
        fg = col(C::muted);
        break;
    }
    if (enabled && hover)
        bg = k == Ghost ? col(C::raised) : bg.brighter(k == Plain || k == Danger ? 0.06f : 0.1f);
    if (enabled && down)
        bg = bg.darker(0.12f);
    g.setColour(bg.withMultipliedAlpha(a));
    g.fillRoundedRectangle(r, 8);
    g.setColour(edge.withMultipliedAlpha(a));
    g.drawRoundedRectangle(r.reduced(0.5f), 8, 1);
    if (k == Plain || k == Danger) {
        g.setColour(juce::Colours::white.withAlpha(0.04f * a));
        g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 6, r.getRight() - 6);
    }
    g.setColour(fg.withMultipliedAlpha(a));
    g.setFont(font(fontPx, SemiBold));
    g.drawFittedText(text, r.reduced(6, 0).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void Look::drawLed(juce::Graphics &g, juce::Point<float> c, float radius, Colour colour)
{
    g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.45f), c, colour.withAlpha(0.0f), c.translated(radius * 2.6f, 0), true));
    g.fillEllipse(Rectangle<float>(radius * 5.2f, radius * 5.2f).withCentre(c));
    g.setColour(colour);
    g.fillEllipse(Rectangle<float>(radius * 2, radius * 2).withCentre(c));
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.fillEllipse(Rectangle<float>(radius * 0.8f, radius * 0.8f).withCentre(c.translated(-radius * 0.3f, -radius * 0.35f)));
}

void Look::drawSheetBackground(juce::Graphics &g, Rectangle<float> r)
{
    juce::Path p;
    p.addRoundedRectangle(r, 14.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 40, {0, 14}).drawForPath(g, p);
    g.setColour(col(C::surface));
    g.fillPath(p);
    g.setColour(col(C::border));
    g.strokePath(p, juce::PathStrokeType(1.0f));
}

void Look::styleTextEditor(juce::TextEditor &e, float fontPx)
{
    e.setFont(font(fontPx, Medium));
    e.setIndents(10, 0);
    e.setJustification(juce::Justification::centredLeft);
    e.setColour(juce::TextEditor::backgroundColourId, col(C::well));
    e.setColour(juce::TextEditor::textColourId, col(C::text));
    e.setColour(juce::TextEditor::outlineColourId, col(C::line));
    e.setTextToShowWhenEmpty(e.getTextToShowWhenEmpty(), col(C::muted));
}

// ------------------------------------------------------- LookAndFeel ---
juce::Typeface::Ptr Look::getTypefaceForFont(const juce::Font &f)
{
    const juce::String n = f.getTypefaceName();
    if (n == juce::Font::getDefaultSansSerifFontName() || n.isEmpty())
        return typeface(f.isBold() ? Bold : Medium);
    return juce::LookAndFeel_V4::getTypefaceForFont(f);
}

juce::Font Look::getPopupMenuFont() { return font(13.5f, Medium); }

void Look::drawPopupMenuBackgroundWithOptions(juce::Graphics &g, int w, int h, const juce::PopupMenu::Options &)
{
    const Rectangle<float> r(0, 0, (float)w, (float)h);
    g.setColour(col(C::surface));
    g.fillRoundedRectangle(r, 10);
    g.setColour(col(C::border));
    g.drawRoundedRectangle(r.reduced(0.5f), 10, 1);
}

void Look::drawPopupMenuItemWithOptions(juce::Graphics &g, const juce::Rectangle<int> &area, bool isHighlighted,
                                        const juce::PopupMenu::Item &item, const juce::PopupMenu::Options &)
{
    const Rectangle<float> r = area.toFloat();
    if (item.isSeparator) {
        g.setColour(col(C::divider));
        g.fillRect(r.withSizeKeepingCentre(r.getWidth() - 16, 1));
        return;
    }
    if (isHighlighted && item.isEnabled) {
        g.setColour(col(C::raisedHover));
        g.fillRoundedRectangle(r.reduced(4, 1), 6);
    }
    Rectangle<float> t = r.reduced(12, 0);
    const Rectangle<float> tick = t.removeFromLeft(18);
    if (item.isTicked)
        strokeIcon(g, Check, tick.withSizeKeepingCentre(14, 14), accent_, 1.8f);
    // (a submenu: a chevron at the right; a shortcut: its keys at the right, muted)
    if (item.subMenu != nullptr)
        strokeIcon(g, ChevronRight, t.removeFromRight(14).withSizeKeepingCentre(12, 12), col(C::muted), 1.5f);
    else if (item.shortcutKeyDescription.isNotEmpty()) {
        g.setFont(getPopupMenuFont());
        g.setColour(col(C::muted));
        const float sw = textWidth(getPopupMenuFont(), item.shortcutKeyDescription);
        g.drawText(item.shortcutKeyDescription, t.removeFromRight(sw + 2).toNearestInt(), juce::Justification::centredRight, false);
        t.removeFromRight(12);
    }
    g.setColour(!item.isEnabled ? col(C::muted).withAlpha(0.7f) : isHighlighted ? juce::Colours::white : col(C::text));
    g.setFont(getPopupMenuFont());
    g.drawFittedText(item.text, t.reduced(4, 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
}

void Look::drawPopupMenuSectionHeaderWithOptions(juce::Graphics &g, const juce::Rectangle<int> &area, const juce::String &name,
                                                 const juce::PopupMenu::Options &)
{
    drawHeading(g, name, area.toFloat().reduced(16, 0));
}

void Look::getIdealPopupMenuItemSizeWithOptions(const juce::String &text, bool isSeparator, int, int &w, int &h,
                                                const juce::PopupMenu::Options &)
{
    if (isSeparator) {
        w = 50;
        h = 9;
        return;
    }
    w = juce::roundToInt(textWidth(getPopupMenuFont(), text)) + 12 + 18 + 4 + 28 + 14;   // (+ a submenu's chevron)
    h = 32;
}

static juce::TextLayout tooltipLayout(const juce::String &text)
{
    juce::AttributedString s;
    s.setJustification(juce::Justification::topLeft);
    s.append(text, Look::font(12.5f, Look::Medium), Look::col(Look::C::text));
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths(s, 320.0f);
    return tl;
}

juce::Rectangle<int> Look::getTooltipBounds(const juce::String &tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const juce::TextLayout tl = tooltipLayout(tipText);
    const int w = (int)std::ceil(tl.getWidth()) + 20, h = (int)std::ceil(tl.getHeight()) + 14;
    return Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                          screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin(parentArea);
}

void Look::drawTooltip(juce::Graphics &g, const juce::String &text, int width, int height)
{
    const Rectangle<float> r(0, 0, (float)width, (float)height);
    g.setColour(col(C::raised));
    g.fillRoundedRectangle(r, 8);
    g.setColour(col(C::border));
    g.drawRoundedRectangle(r.reduced(0.5f), 8, 1);
    tooltipLayout(text).draw(g, r.reduced(10, 7));
}

void Look::drawScrollbar(juce::Graphics &g, juce::ScrollBar &, int x, int y, int width, int height, bool vertical, int thumbStart,
                         int thumbSize, bool isMouseOver, bool isMouseDown)
{
    if (thumbSize <= 0)
        return;
    Rectangle<float> t = vertical ? Rectangle<float>((float)x, (float)thumbStart, (float)width, (float)thumbSize)
                                  : Rectangle<float>((float)thumbStart, (float)y, (float)thumbSize, (float)height);
    t = vertical ? t.withSizeKeepingCentre(isMouseOver || isMouseDown ? 6.0f : 4.0f, t.getHeight() - 4)
                 : t.withSizeKeepingCentre(t.getWidth() - 4, isMouseOver || isMouseDown ? 6.0f : 4.0f);
    g.setColour(juce::Colours::white.withAlpha(isMouseDown ? 0.4f : isMouseOver ? 0.3f : 0.16f));
    g.fillRoundedRectangle(t, 3);
}

void Look::fillTextEditorBackground(juce::Graphics &g, int width, int height, juce::TextEditor &e)
{
    g.setColour(e.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(Rectangle<float>(0, 0, (float)width, (float)height), 8);
}

void Look::drawTextEditorOutline(juce::Graphics &g, int width, int height, juce::TextEditor &e)
{
    const bool focus = e.hasKeyboardFocus(true) && !e.isReadOnly();
    g.setColour(focus ? accent_ : e.findColour(juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle(Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f), 8, focus ? 1.5f : 1.0f);
}

void Look::drawCornerResizer(juce::Graphics &g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour(juce::Colours::white.withAlpha(isMouseDragging ? 0.5f : isMouseOver ? 0.35f : 0.16f));
    const float s = (float)juce::jmin(w, h);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j <= i; j++) {
            const float cx = s - 3.5f - (float)(i - j) * 4.0f, cy = s - 3.5f - (float)j * 4.0f;
            g.fillEllipse(cx - 1.1f, cy - 1.1f, 2.2f, 2.2f);
        }
}
