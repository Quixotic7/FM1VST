// SPDX-License-Identifier: GPL-3.0-only
// Panel colour themes (see Theme.h).
#include "Theme.h"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>

using juce::Colour;

// ------------------------------------------------------------- contrast ---
static double linear(double c8)
{
    const double c = c8 / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double relativeLuminance(Colour c)
{
    return 0.2126 * linear(c.getRed()) + 0.7152 * linear(c.getGreen()) + 0.0722 * linear(c.getBlue());
}
double contrastRatio(Colour a, Colour b)
{
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

static const Colour kWhite{0xFFFFFFFF}, kBlack{0xFF000000};
static Colour mix(Colour a, Colour b, float t) { return a.interpolatedWith(b, t).withAlpha((juce::uint8)255); }
// dark: a light colour contrasts better with it than a dark one (the crossover of the two ratios is L ~ 0.18)
static bool isDark(Colour c) { return relativeLuminance(c) < 0.179; }

// c moved toward white (lighterFirst) or black until its contrast with `against` reaches floor; the other way when
// that cannot; the better extreme when neither can
static Colour ensureContrast(Colour c, Colour against, double floor, bool lighterFirst)
{
    if (contrastRatio(c, against) >= floor)
        return c;
    for (int pass = 0; pass < 2; pass++) {
        const bool lighter = pass == 0 ? lighterFirst : !lighterFirst;
        for (int i = 1; i <= 64; i++) {
            const Colour cand = mix(c, lighter ? kWhite : kBlack, (float)i / 64.0f);
            if (contrastRatio(cand, against) >= floor)
                return cand;
        }
    }
    return contrastRatio(kWhite, against) >= contrastRatio(kBlack, against) ? kWhite : kBlack;
}

// a light or a dark text colour, whichever reads better on bg
static Colour autoText(Colour bg, Colour light, Colour dark)
{
    return contrastRatio(light, bg) >= contrastRatio(dark, bg) ? light : dark;
}

Palette derive(const Theme &t)
{
    Palette p;
    p.backdrop = Colour(0xFF0E0E10);
    p.plate = t.base.withAlpha((juce::uint8)255);
    p.outline = kBlack;
    p.edge = isDark(p.plate) ? mix(p.plate, kWhite, 0.06f) : mix(p.plate, kBlack, 0.08f);

    const Colour m = t.membrane.withAlpha((juce::uint8)255);
    p.bed = t.bed ? t.bed->withAlpha((juce::uint8)255) : mix(m, kBlack, 0.15f);
    p.bedEdge = isDark(p.bed) ? mix(p.bed, kWhite, 0.09f) : mix(p.bed, kBlack, 0.12f);

    p.cap = m;
    p.pressed = ensureContrast(relativeLuminance(m) < 0.6 ? mix(m, kWhite, 0.12f) : mix(m, kBlack, 0.12f), m, 1.3,
                               relativeLuminance(m) < 0.6);
    p.ledOff = ensureContrast(mix(m, kBlack, 0.4f), m, 1.3, false);
    p.capOutline = isDark(m) ? mix(m, kBlack, 0.78f) : mix(m, kBlack, 0.55f);
    p.capText = ensureContrast(autoText(m, Colour(0xFFC8C8D0), Colour(0xFF1A1A1E)), m, 3.0, isDark(m));
    p.noteText = mix(p.capText, m, 0.4f);
    p.litText = Colour(0xFF111114);

    const Colour light(0xFFE8E8EC), dark(0xFF202024);
    p.label = t.label ? t.label->withAlpha((juce::uint8)255) : autoText(p.plate, light, dark);
    p.bedLabel = t.label && contrastRatio(*t.label, p.bed) >= 3.0 ? t.label->withAlpha((juce::uint8)255)
                                                                   : autoText(p.bed, light, dark);

    p.hint = ensureContrast(Colour(0xFF54D678), m, 2.0, isDark(m));
    p.hintLit = Colour(0xFF1E3A26);
    p.capDim = mix(m, Colour(0xFFF4F4FF), 0.38f);
    p.capTextDim = ensureContrast(autoText(p.capDim, Colour(0xFFC8C8D0), Colour(0xFF1A1A1E)), p.capDim, 3.0, isDark(p.capDim));
    p.hintDim = ensureContrast(Colour(0xFF54D678), p.capDim, 2.0, isDark(p.capDim));
    p.hintBed = ensureContrast(Colour(0xFF54D678), p.bed, 2.0, isDark(p.bed));

    const Colour k = t.knob.withAlpha((juce::uint8)255);
    p.knobCap = k;
    p.knobBody = isDark(k) ? mix(k, kBlack, 0.28f) : mix(k, kBlack, 0.2f);
    p.knobRing = ensureContrast(mix(k, kWhite, 0.03f), p.knobBody, 1.25, true);
    p.pointer = ensureContrast(isDark(k) ? Colour(0xFFC8C8D0) : Colour(0xFF1A1A1E), k, 2.0, isDark(k));
    p.masterPointer = isDark(k) ? kWhite : kBlack;

    p.screenBezel = Colour(0xFF0B0B0E);
    p.white = Colour(0xFFF4F4FF);
    p.red = Colour(0xFFFF4242);
    p.orange = Colour(0xFFFFA52A);
    p.green = Colour(0xFF3DDC5A);
    return p;
}

// ------------------------------------------------------------------ JSON ---
juce::String toHex(Colour c)
{
    return "#" + juce::String::toHexString((int)(c.getARGB() & 0xFFFFFFu)).paddedLeft('0', 6).toUpperCase();
}

std::optional<Colour> parseHex(const juce::String &s)
{
    juce::String h = s.trim();
    if (h.startsWithChar('#'))
        h = h.substring(1);
    if (h.length() != 6 || !h.containsOnly("0123456789abcdefABCDEF"))
        return std::nullopt;
    return Colour((juce::uint32)(0xFF000000u | (juce::uint32)h.getHexValue32()));
}

juce::var themeToVar(const Theme &t)
{
    auto *o = new juce::DynamicObject();
    o->setProperty("name", t.name);
    o->setProperty("base", toHex(t.base));
    o->setProperty("membrane", toHex(t.membrane));
    o->setProperty("knob", toHex(t.knob));
    if (t.bed)
        o->setProperty("bed", toHex(*t.bed));
    if (t.label)
        o->setProperty("label", toHex(*t.label));
    return juce::var(o);
}

std::optional<Theme> themeFromVar(const juce::var &v)
{
    if (!v.isObject())
        return std::nullopt;
    Theme t;
    t.name = v.getProperty("name", "").toString().trim();
    const auto base = parseHex(v.getProperty("base", "").toString());
    const auto membrane = parseHex(v.getProperty("membrane", "").toString());
    const auto knob = parseHex(v.getProperty("knob", "").toString());
    if (t.name.isEmpty() || !base || !membrane || !knob)
        return std::nullopt;
    t.base = *base;
    t.membrane = *membrane;
    t.knob = *knob;
    t.bed = parseHex(v.getProperty("bed", "").toString());
    t.label = parseHex(v.getProperty("label", "").toString());
    return t;
}

// ----------------------------------------------------------------- store ---
ThemeStore::ThemeStore(const juce::File &fm1emuDir) : dir_(fm1emuDir) {}

const std::vector<Theme> &ThemeStore::presets()
{
    static const std::vector<Theme> list = [] {
        std::vector<Theme> out;
        const juce::var root = juce::JSON::parse(juce::String::fromUTF8(BinaryData::presets_json, BinaryData::presets_jsonSize));
        if (const juce::Array<juce::var> *a = root.getProperty("presets", juce::var()).getArray())
            for (const juce::var &e : *a)
                if (auto t = themeFromVar(e))
                    out.push_back(*t);
        if (out.empty())
            out.push_back(Theme{});              // (never: the embedded file always parses; the Emulator default)
        return out;
    }();
    return list;
}

std::optional<Theme> ThemeStore::preset(const juce::String &name)
{
    for (const Theme &t : presets())
        if (t.name.equalsIgnoreCase(name))
            return t;
    return std::nullopt;
}

bool ThemeStore::isPresetName(const juce::String &name) { return preset(name.trim()).has_value(); }

juce::File ThemeStore::fileFor(const juce::String &name) const
{
    return themesDir().getChildFile(juce::File::createLegalFileName(name.trim()).trim() + ".json");
}

std::vector<Theme> ThemeStore::customs() const
{
    std::vector<Theme> out;
    for (const juce::File &f : themesDir().findChildFiles(juce::File::findFiles, false, "*.json")) {
        juce::var v = juce::JSON::parse(f.loadFileAsString());
        if (v.isObject() && v.getProperty("name", "").toString().trim().isEmpty())
            v.getDynamicObject()->setProperty("name", f.getFileNameWithoutExtension());
        if (auto t = themeFromVar(v))
            if (!isPresetName(t->name))
                out.push_back(*t);
    }
    std::sort(out.begin(), out.end(), [](const Theme &a, const Theme &b) { return a.name.compareNatural(b.name) < 0; });
    return out;
}

std::optional<Theme> ThemeStore::find(const juce::String &name) const
{
    if (auto p = preset(name))
        return p;
    for (const Theme &t : customs())
        if (t.name.equalsIgnoreCase(name.trim()))
            return t;
    return std::nullopt;
}

bool ThemeStore::save(const Theme &t)
{
    Theme c = t;
    c.name = c.name.trim();
    if (c.name.isEmpty() || isPresetName(c.name) || juce::File::createLegalFileName(c.name).trim().isEmpty())
        return false;
    if (!themesDir().createDirectory())
        return false;
    return fileFor(c.name).replaceWithText(juce::JSON::toString(themeToVar(c)) + "\n");
}

bool ThemeStore::remove(const juce::String &name)
{
    const juce::File f = fileFor(name);
    return !isPresetName(name) && f.existsAsFile() && f.deleteFile();
}

juce::String ThemeStore::defaultName() const
{
    const juce::var v = juce::JSON::parse(settingsFile().loadFileAsString());
    return v.isObject() ? v.getProperty("defaultTheme", "").toString() : juce::String();
}

bool ThemeStore::setDefaultName(const juce::String &name)
{
    juce::var v = juce::JSON::parse(settingsFile().loadFileAsString());
    if (!v.isObject())
        v = juce::var(new juce::DynamicObject());
    v.getDynamicObject()->setProperty("defaultTheme", name.trim());
    return dir_.createDirectory() && settingsFile().replaceWithText(juce::JSON::toString(v) + "\n");
}

Theme ThemeStore::defaultTheme() const
{
    const juce::String n = defaultName();
    if (n.isNotEmpty())
        if (auto t = find(n))
            return *t;
    if (auto e = preset("Emulator"))
        return *e;
    return presets().front();
}
