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

// the pool's size: ChoralRoot's visible map (73 entries; FM1P_HIDDEN ones get no slot) plus headroom, chosen so that
// the eight knob parameters (MASTER and the seven KnobParameters), the pool and the 41 Tier 1 panel booleans make
// exactly 128, Live's limit for showing a plugin's parameters without configuring them (plan 4.5). A ChoralRoot core
// built with FM1_EXPOSE_EDITOR (103 visible) needs -DFM1_TIER2_SLOTS=103 (or more).
#ifndef FM1_TIER2_SLOTS
#define FM1_TIER2_SLOTS 79
#endif
static constexpr int kTier2NameMax = 16;   // the longest name a slot reports (the Roto-Control's displays)

class Tier2Parameter : public juce::AudioProcessorParameterWithID {
public:
    explicit Tier2Parameter(int slot);
    Tier2Parameter(const juce::String &id, const juce::String &initialName, int slot);   // (a KnobParameter)

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

    // ---- host -> firmware (audio thread): the latest host target since the last call (a write made before the slot
    // was retargeted is dropped: its value belonged to the old range)
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

protected:
    juce::String nameLocked() const;
    juce::String textLocked(int32_t v) const;
    void storeRange(const fm1param_t *e);                // min / max / def / enum from e

    const int slot_;
    mutable juce::SpinLock lock_;                        // the binding against unbind (host threads)
    std::atomic<const fm1param_t *> own_{nullptr}, view_{nullptr};
    std::atomic<int> metaKnob_{-1};                      // 0..3 for "Perf Knob 1..4", -1 not a meta slot
    std::atomic<int32_t> min_{0}, max_{1}, def_{0};
    std::atomic<bool> enum_{false};
    std::atomic<float> value_{0.0f};
    std::atomic<int32_t> pendingV_{0};
    std::atomic<bool> pending_{false};
    std::atomic<uint32_t> gen_{0}, pendingGen_{0};       // moves on every knob retarget
};

// One of the FM-1's physical knobs as a host parameter (plan 4.5: the first page, MASTER SELECT PRESETS ALGORITHM
// KNOB1..4; MASTER itself is a plain AudioParameterInt). What it is follows the firmware's screen: the core's
// knob_target(role) names the map entry that knob turns now, and the slot is that entry exactly as a Tier 2 slot
// would be (name "KNOB1: Strum Rate", range, texts, discrete), or -1: no value there, and the slot is a RELATIVE
// control, "KNOB1 (turn)", continuous, centred at 0.5: a host change becomes detents (the change of the normalised
// value x detentsPerTravel, rounded; the remainder kept), sent to the device's encoder as a panel turn, and after
// kRecentreMs of device time without a host change the value springs back to 0.5 (reported to the host without a
// gesture; a re-centre never makes detents).
// THREADS: retargetKnob / takeDetents / recentre: the audio thread (lock-free, as Tier2Parameter::retarget); the
// rest as Tier2Parameter.
class KnobParameter : public Tier2Parameter {
public:
    KnobParameter(int role, const juce::String &id, const juce::String &knobLabel, const juce::String &shortLabel);

    int role() const { return role_; }
    bool isRelative() const { return relative_.load(std::memory_order_acquire); }
    // the audio thread: bind to entry e of the loaded map, nullptr: relative (value 0.5 at once)
    void retargetKnob(const fm1param_t *e);
    // the message thread, the device stopped: relative, under the lock the host threads' texts take (before the
    // core module is unloaded, as Tier2Parameter::unbind)
    void unbindKnob();
    // the audio thread: a relative slot's host change since the last call, as detents (false: none)
    bool takeDetents(int32_t &detents, int detentsPerTravel);
    bool relativeOffCentre() const { return isRelative() && getValue() != 0.5f; }
    void recentre();                                      // the audio thread: value 0.5 (the host is told by the drain)
    uint32_t hostSeq() const { return hostSeq_.load(); }  // moves with every host write
    // the message thread: the host told the slot's value is x (0.5: relative), without making it a host change
    void setNormFromFirmware(float x);

    void setValue(float newValue) override;
    float getDefaultValue() const override;
    juce::String getName(int maximumStringLength) const override;
    juce::String getText(float normalisedValue, int maximumStringLength) const override;
    float getValueForText(const juce::String &text) const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    juce::StringArray getAllValueStrings() const override;

    static constexpr uint32_t kRecentreMs = 400;          // device ms without a host change
    static constexpr int kDefaultDetents = 24;            // detents per full travel (FM1Processor's setting)

private:
    const int role_;
    const juce::String label_, short_;
    std::atomic<bool> relative_{true}, relPending_{false};
    std::atomic<double> relBase_{0.5};
    std::atomic<uint32_t> hostSeq_{0};
};
