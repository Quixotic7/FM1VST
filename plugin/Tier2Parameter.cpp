// SPDX-License-Identifier: GPL-3.0-only
// A Tier 2 host parameter slot: see Tier2Parameter.h.
#include "Tier2Parameter.h"

#include <cmath>

// set while the processor's feedback drain pushes a firmware value to the host (message thread): the setValue that
// setValueNotifyingHost makes is then not a host change to send back to the firmware
static thread_local bool tl_fromFirmware = false;

Tier2Parameter::Tier2Parameter(int slot)
    : juce::AudioProcessorParameterWithID(juce::ParameterID{juce::String::formatted("t2_%02d", slot), 1},
                                          "(unused)"),
      slot_(slot)
{
}

void Tier2Parameter::bind(const fm1param_t *e, int metaKnob)
{
    const juce::SpinLock::ScopedLockType l(lock_);
    own_.store(e, std::memory_order_release);
    view_.store(e, std::memory_order_release);
    metaKnob_.store(e && (e->flags & FM1P_META) ? metaKnob : -1);
    pending_.store(false);
    if (!e) {
        min_.store(0);
        max_.store(1);
        def_.store(0);
        enum_.store(false);
        value_.store(0.0f);
        return;
    }
    min_.store(e->min);
    max_.store(e->max > e->min ? e->max : e->min + 1);
    def_.store(e->def);
    enum_.store((e->flags & FM1P_ENUM) != 0);
    value_.store(toNorm(e->def));                       // (the boot's full read-back reports the real value)
}

void Tier2Parameter::retarget(const fm1param_t *v)
{
    if (!v || !own_.load())
        return;
    // the range first: a host write racing the switch is denormalised in one range or the other, both valid
    min_.store(v->min);
    max_.store(v->max > v->min ? v->max : v->min + 1);
    def_.store(v->def);
    enum_.store((v->flags & FM1P_ENUM) != 0);
    view_.store(v, std::memory_order_release);
}

int32_t Tier2Parameter::toPlain(float x) const
{
    const int32_t lo = min_.load(), hi = max_.load();
    const double t = juce::jlimit(0.0, 1.0, (double)x);
    return lo + (int32_t)std::lround(t * (double)(hi - lo));
}

float Tier2Parameter::toNorm(int32_t v) const
{
    const int32_t lo = min_.load(), hi = max_.load();
    return hi > lo ? (float)juce::jlimit(0.0, 1.0, (double)(v - lo) / (double)(hi - lo)) : 0.0f;
}

bool Tier2Parameter::takePending(int32_t &v)
{
    if (!pending_.exchange(false, std::memory_order_acq_rel))
        return false;
    v = pendingV_.load(std::memory_order_acquire);
    return true;
}

void Tier2Parameter::setFromFirmware(int32_t v, bool gesture)
{
    const bool was = tl_fromFirmware;
    tl_fromFirmware = true;
    if (gesture)
        beginChangeGesture();
    setValueNotifyingHost(toNorm(v));
    if (gesture)
        endChangeGesture();
    tl_fromFirmware = was;
}

void Tier2Parameter::setValue(float x)
{
    value_.store(x);
    if (tl_fromFirmware || !own_.load(std::memory_order_acquire))
        return;                                          // (the firmware's own value, or an unused slot: inert)
    pendingV_.store(toPlain(x), std::memory_order_release);
    pending_.store(true, std::memory_order_release);
}

float Tier2Parameter::getDefaultValue() const { return toNorm(def_.load()); }

juce::String Tier2Parameter::nameLocked() const
{
    const fm1param_t *o = own_.load(std::memory_order_acquire), *v = view_.load(std::memory_order_acquire);
    if (!o)
        return "(unused)";
    const int k = metaKnob_.load();
    juce::String n = k >= 0 && v && v != o ? "K" + juce::String(k + 1) + " " + juce::String(v->name) : juce::String(o->name);
    return n.substring(0, kTier2NameMax);
}

juce::String Tier2Parameter::getName(int maximumStringLength) const
{
    const juce::SpinLock::ScopedLockType l(lock_);
    const juce::String n = nameLocked();
    return maximumStringLength > 0 ? n.substring(0, maximumStringLength) : n;
}

juce::String Tier2Parameter::textLocked(int32_t v) const
{
    const fm1param_t *e = view_.load(std::memory_order_acquire);
    if (!e)
        return {};
    char b[64];
    b[0] = 0;
    if (e->text)
        e->text(v, b, sizeof b);
    else
        std::snprintf(b, sizeof b, "%d", (int)v);
    return juce::String::fromUTF8(b);
}

juce::String Tier2Parameter::getText(float x, int maximumStringLength) const
{
    const juce::SpinLock::ScopedLockType l(lock_);
    const juce::String t = textLocked(toPlain(x));
    return maximumStringLength > 0 ? t.substring(0, maximumStringLength) : t;
}

float Tier2Parameter::getValueForText(const juce::String &text) const
{
    const juce::String want = text.trim();
    {
        const juce::SpinLock::ScopedLockType l(lock_);
        const int32_t lo = min_.load(), hi = max_.load();
        if (view_.load() && hi - lo <= 2048)
            for (int32_t v = lo; v <= hi; v++)
                if (textLocked(v).equalsIgnoreCase(want))
                    return toNorm(v);
    }
    return toNorm(want.getIntValue());                   // ("120", "120 ms", "-3")
}

int Tier2Parameter::getNumSteps() const { return (int)(max_.load() - min_.load()) + 1; }

// a firmware value is an integer step: named values and short ranges are discrete (the Roto-Control's detents
// click per value); long ranges (a rate of 1..1000 ms, a level of 0..127) are continuous knobs
bool Tier2Parameter::isDiscrete() const { return enum_.load() || max_.load() - min_.load() <= 24; }

juce::StringArray Tier2Parameter::getAllValueStrings() const
{
    juce::StringArray all;
    if (!isDiscrete())
        return all;
    const juce::SpinLock::ScopedLockType l(lock_);
    for (int32_t v = min_.load(), hi = max_.load(); v <= hi; v++)
        all.add(textLocked(v));
    return all;
}
