// SPDX-License-Identifier: GPL-3.0-only
// A Tier 2 host parameter slot (FM1-VST-PLAN.md 4.5): one of a fixed pool, bound to an entry of the loaded core's
// parameter map (core-api/fm1core.h fm1param_t). Hosts expect a fixed parameter list, so the pool never changes size;
// a core switch rebinds the slots (name, range, texts), slots beyond the map are "(unused)" and inert.
//
// THREADS
//   - setValue (any thread: the host): stores the normalised value and, unless the change came from the firmware
//     (the processor's feedback drain, see fromFirmware), the denormalised target plus a pending flag, both atomic.
//     The audio thread takes it (takePending) and calls the map's set().
//   - getName / getText / getValueForText (any thread) read the binding under a short SpinLock: unbind() takes it
//     before the core module is unloaded, so a host thread never formats with a function of an unloaded image.
//   - bind / unbind: message thread with the device stopped (devLock_ held). retarget (a meta slot's view): the
//     audio thread, lock-free (both entries belong to the same loaded image).
// THE VALUE: integer steps min..max of the bound entry; host value 0..1 maps linearly (v = min + round(x (max-min))).
// The value text is the firmware's own (fm1param_t::text: "1/8", "120 ms", "Up"): it carries the unit, so
// getLabel() is empty (an AU host would print "120 ms ms").
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

#include "fm1core.h"

// the pool's size: ChoralRoot's map (77 with the editor off) plus headroom, chosen so that the 42 Tier 1 panel
// parameters and the pool make exactly 128, Live's limit for showing a plugin's parameters without configuring them
// (plan 4.5). A core built with FM1_EXPOSE_EDITOR (107 entries) needs -DFM1_TIER2_SLOTS=116 (or more).
#ifndef FM1_TIER2_SLOTS
#define FM1_TIER2_SLOTS 86
#endif
static constexpr int kTier2NameMax = 16;   // the longest name a slot reports (the Roto-Control's displays)

class Tier2Parameter : public juce::AudioProcessorParameterWithID {
public:
    explicit Tier2Parameter(int slot);

    // ---- binding
    void bind(const fm1param_t *entry, int metaKnob);   // message thread; entry nullptr: "(unused)"
    void unbind() { bind(nullptr, -1); }
    void retarget(const fm1param_t *view);              // audio thread: a meta slot now shows view's name / range
    const fm1param_t *entry() const { return own_.load(std::memory_order_acquire); }   // the map entry (audio thread)
    const fm1param_t *view() const { return view_.load(std::memory_order_acquire); }
    bool isMeta() const { return metaKnob_.load() >= 0; }
    int slot() const { return slot_; }

    int32_t toPlain(float x) const;
    float toNorm(int32_t v) const;
    int32_t minValue() const { return min_.load(); }
    int32_t maxValue() const { return max_.load(); }
    int32_t plainValue() const { return toPlain(getValue()); }   // what the host sees, in firmware units

    // ---- host -> firmware (audio thread): the latest host target since the last call
    bool takePending(int32_t &v);
    // the firmware's value reported to the host (message thread): setValueNotifyingHost without marking it pending
    void setFromFirmware(int32_t v, bool gesture);

    // ---- juce::AudioProcessorParameter
    float getValue() const override { return value_.load(); }
    void setValue(float newValue) override;
    float getDefaultValue() const override;
    juce::String getName(int maximumStringLength) const override;
    juce::String getLabel() const override { return {}; }
    juce::String getText(float normalisedValue, int maximumStringLength) const override;
    float getValueForText(const juce::String &text) const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isAutomatable() const override { return true; }
    // fresh every call (JUCE's default caches the first answer; a slot's texts change with its binding)
    juce::StringArray getAllValueStrings() const override;

private:
    juce::String nameLocked() const;
    juce::String textLocked(int32_t v) const;

    const int slot_;
    mutable juce::SpinLock lock_;                        // the binding against unbind (host threads)
    std::atomic<const fm1param_t *> own_{nullptr}, view_{nullptr};
    std::atomic<int> metaKnob_{-1};                      // 0..3 for "Perf Knob 1..4", -1 not a meta slot
    std::atomic<int32_t> min_{0}, max_{1}, def_{0};
    std::atomic<bool> enum_{false};
    std::atomic<float> value_{0.0f};
    std::atomic<int32_t> pendingV_{0};
    std::atomic<bool> pending_{false};
};
