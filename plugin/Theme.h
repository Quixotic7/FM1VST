// SPDX-License-Identifier: GPL-3.0-only
// Panel colour themes (FM1-VST-PLAN.md 4.8): a theme is three colours the user picks (the body plate, the silicone
// membrane of the keys and buttons, the knob caps) plus two optional overrides (the recessed bed the keys sit in,
// the printed labels). derive() computes every colour the panel draws from them, so a theme always stays legible:
// edges from the base, pressed / LED-off / outlines from the membrane, the pointer and the labels by contrast. The
// LEDs' lit colours are fixed (they are the LEDs, not the paint).
//
// FILES (dir = FM1Processor::home(), i.e. <FM1EMU_HOME or ~/Library/Application Support>/fm1emu)
//   presets          themes/presets.json of the repo, embedded in the plugin (BinaryData); read-only
//   dir/themes/*.json custom themes, one per file, the keys of a presets.json entry:
//                     {"name": "My Teal", "base": "#RRGGBB", "membrane": "#RRGGBB", "knob": "#RRGGBB",
//                      "bed": "#RRGGBB" (optional), "label": "#RRGGBB" (optional)}
//   dir/settings.json {"defaultTheme": "Black"}: the theme a new instance starts with (other keys are kept)
// The plugin state carries the theme itself (name and colours): a set reopens looking the same even when the
// custom file is gone.
#pragma once
#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <vector>

struct Theme {
    juce::String name = "Emulator";
    juce::Colour base{0xFF1C1C20}, membrane{0xFF2B2B31}, knob{0xFF35353C};
    std::optional<juce::Colour> bed = juce::Colour(0xFF141417), label = juce::Colour(0xFF8A8A92);

    bool sameColours(const Theme &o) const
    {
        return base == o.base && membrane == o.membrane && knob == o.knob && bed == o.bed && label == o.label;
    }
};

// every colour the panel draws (emu.c's C_* constants, routed through the theme)
struct Palette {
    juce::Colour backdrop;      // around the body (the letterbox)
    juce::Colour plate;         // the body = base
    juce::Colour outline;       // the body's outer line
    juce::Colour edge;          // the inner line on the plate, by contrast with the base
    juce::Colour bed;           // the recessed key / button beds
    juce::Colour bedEdge;       // their outline, and the dots between the key groups
    juce::Colour cap;           // an unlit key / button = membrane
    juce::Colour pressed;       // a held key's cap: membrane lightened (darkened when it cannot be)
    juce::Colour ledOff;        // the LED slot when off: membrane darkened
    juce::Colour capOutline;    // the dark line around each cap
    juce::Colour capText;       // the button names printed on the caps
    juce::Colour noteText;      // the note names on the keys (dimmer)
    juce::Colour litText;       // a name on a lit (white / orange / red) cap
    juce::Colour label;         // the printed labels on the plate (knob names): theme.label or auto
    juce::Colour bedLabel;      // the printed labels on the bed (the panel's own button names)
    juce::Colour hint;          // the computer-key hints and the held / selected highlight
    juce::Colour hintLit;       // a hint on a lit cap
    juce::Colour capDim;        // a dim-lit button cap (the LED at 38 % over the membrane, as drawn)
    juce::Colour capTextDim;    // a name on a dim-lit cap
    juce::Colour hintDim;       // a hint on a dim-lit cap
    juce::Colour hintBed;       // a hint on the bed (OCT- / OCT+)
    juce::Colour knobBody, knobRing, knobCap, pointer, masterPointer;
    juce::Colour screenBezel;
    juce::Colour white, red, orange, green;   // the LEDs, fixed
};

Palette derive(const Theme &t);

// WCAG relative luminance (sRGB) and contrast ratio (1 .. 21)
double relativeLuminance(juce::Colour c);
double contrastRatio(juce::Colour a, juce::Colour b);

juce::String toHex(juce::Colour c);                       // "#RRGGBB"
std::optional<juce::Colour> parseHex(const juce::String &s);   // "#RRGGBB" / "RRGGBB"
juce::var themeToVar(const Theme &t);
std::optional<Theme> themeFromVar(const juce::var &v);

class ThemeStore {
public:
    explicit ThemeStore(const juce::File &fm1emuDir);

    static const std::vector<Theme> &presets();              // presets.json (embedded), in its order
    static std::optional<Theme> preset(const juce::String &name);
    static bool isPresetName(const juce::String &name);

    juce::File themesDir() const { return dir_.getChildFile("themes"); }
    juce::File settingsFile() const { return dir_.getChildFile("settings.json"); }
    juce::File fileFor(const juce::String &name) const;      // themesDir()/<legal name>.json

    std::vector<Theme> customs() const;                      // themesDir()/*.json, sorted by name
    std::optional<Theme> find(const juce::String &name) const;   // a preset, else a custom one
    bool save(const Theme &t);                               // false: no name, a preset's name, or a write error
    bool remove(const juce::String &name);

    juce::String defaultName() const;                        // settings.json defaultTheme ("" if unset)
    bool setDefaultName(const juce::String &name);
    Theme defaultTheme() const;                              // find(defaultName()), else the "Emulator" preset

private:
    juce::File dir_;
};
