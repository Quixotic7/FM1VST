// SPDX-License-Identifier: GPL-3.0-only
// The editor's look (everything around the panel): the "Graphite" palette, the type (Inter Tight, OFL 1.1, embedded
// as four static weights: plugin/fonts/make_font.py), the vector icons and the drawing of every control in the
// settings sheet (Sheet.h), the corner button and the screen view, plus the LookAndFeel overrides that keep JUCE's own
// pieces (popup menus, tooltips, scroll bars, text fields, the colour picker, the corner resizer) in the same style.
// No other file picks a colour: the accent is the active theme's button (membrane) colour, made legible on the sheet.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

class Look : public juce::LookAndFeel_V4 {
public:
    Look();

    // ---- the palette (graphite)
    struct C {
        static constexpr juce::uint32 window = 0xFF0F1113;   // behind everything
        static constexpr juce::uint32 surface = 0xFF1D2024;  // the sheet
        static constexpr juce::uint32 raised = 0xFF23272C;   // pills, cards, chips
        static constexpr juce::uint32 raisedHover = 0xFF2A2F35;
        static constexpr juce::uint32 well = 0xFF141619;     // segmented / stepper / text field wells
        static constexpr juce::uint32 border = 0xFF33383F;   // the sheet's edge
        static constexpr juce::uint32 line = 0xFF2E333A;     // control edges
        static constexpr juce::uint32 divider = 0xFF2B2F35;
        static constexpr juce::uint32 segOn = 0xFF2C3138;
        static constexpr juce::uint32 text = 0xFFE8EAED;
        static constexpr juce::uint32 rowText = 0xFFD6D9DD;
        static constexpr juce::uint32 muted = 0xFF8A9099;
        static constexpr juce::uint32 heading = 0xFF7D838C;
        static constexpr juce::uint32 switchOff = 0xFF30353C;
        static constexpr juce::uint32 knobOff = 0xFF9AA0A8;
        static constexpr juce::uint32 green = 0xFF3DDC84;    // Remote FM-1 connected
        static constexpr juce::uint32 amber = 0xFFF5A524;    // searching, in use, lost
        static constexpr juce::uint32 red = 0xFFFF6464;      // destructive, halted
        static constexpr juce::uint32 redFill = 0xFFD9443F;
        static constexpr juce::uint32 brand = 0xFFFF9A4D;    // the FM1VST mark
        static constexpr juce::uint32 fallbackAccent = 0xFFFF8A3D;
    };
    static juce::Colour col(juce::uint32 c) { return juce::Colour(c); }

    // the accent: the theme's membrane, lifted to read on the sheet (a grey membrane: the fallback orange)
    void setAccentFrom(const Theme &t);
    juce::Colour accent() const { return accent_; }
    juce::Colour onAccent() const;                   // text on an accent fill

    // ---- type
    enum Weight { Regular = 0, Medium = 1, SemiBold = 2, Bold = 3 };
    static juce::Font font(float px, Weight w = Medium, float tracking = 0.0f);   // px: CSS-like size
    static float textWidth(const juce::Font &f, const juce::String &s);

    // ---- icons (juce::Path, drawn in a 16 x 16 box scaled into r)
    enum Icon { Dots, Gear, ChevronLeft, ChevronRight, ChevronDown, Close, Search, Power, Save, Info, Check, Screen, Device };
    static juce::Path icon(Icon i, juce::Rectangle<float> r);
    static void strokeIcon(juce::Graphics &g, Icon i, juce::Rectangle<float> r, juce::Colour c, float width = 1.5f);

    // ---- pieces of the sheet
    static void drawHeading(juce::Graphics &g, const juce::String &text, juce::Rectangle<float> r);
    void drawSwitch(juce::Graphics &g, juce::Rectangle<float> r, bool on, bool enabled, bool hover) const;
    static void drawSegmented(juce::Graphics &g, juce::Rectangle<float> r, const juce::StringArray &labels, int sel, int hover,
                              bool enabled);
    static std::vector<juce::Rectangle<float>> segmentedCells(juce::Rectangle<float> r, const juce::StringArray &labels);
    static void drawStepper(juce::Graphics &g, juce::Rectangle<float> r, const juce::String &value, int hover, bool enabled);
    enum PillKind { Plain, Primary, Danger, DangerFill, Ghost };
    void drawPill(juce::Graphics &g, juce::Rectangle<float> r, const juce::String &text, PillKind k, bool hover, bool down,
                  bool enabled, float fontPx = 13.0f) const;
    static void drawLed(juce::Graphics &g, juce::Point<float> c, float radius, juce::Colour colour);
    static void drawSheetBackground(juce::Graphics &g, juce::Rectangle<float> r);
    static void styleTextEditor(juce::TextEditor &e, float fontPx = 14.0f);

    // ---- juce::LookAndFeel
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font &f) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackgroundWithOptions(juce::Graphics &, int w, int h, const juce::PopupMenu::Options &) override;
    void drawPopupMenuItemWithOptions(juce::Graphics &, const juce::Rectangle<int> &area, bool isHighlighted,
                                      const juce::PopupMenu::Item &item, const juce::PopupMenu::Options &) override;
    void drawPopupMenuSectionHeaderWithOptions(juce::Graphics &, const juce::Rectangle<int> &area, const juce::String &name,
                                               const juce::PopupMenu::Options &) override;
    void getIdealPopupMenuItemSizeWithOptions(const juce::String &text, bool isSeparator, int standardHeight, int &w, int &h,
                                              const juce::PopupMenu::Options &) override;
    int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options &) override { return 6; }
    juce::Rectangle<int> getTooltipBounds(const juce::String &tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics &, const juce::String &text, int width, int height) override;
    void drawScrollbar(juce::Graphics &, juce::ScrollBar &, int x, int y, int width, int height, bool vertical, int thumbStart,
                       int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 8; }
    void fillTextEditorBackground(juce::Graphics &, int width, int height, juce::TextEditor &) override;
    void drawTextEditorOutline(juce::Graphics &, int width, int height, juce::TextEditor &) override;
    void drawCornerResizer(juce::Graphics &, int w, int h, bool isMouseOver, bool isMouseDragging) override;

private:
    juce::Colour accent_{C::fallbackAccent};
};
