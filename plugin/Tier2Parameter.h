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

// the pool's size: 0 by default (plan 4.5): the host sees the eight knob parameters and the 41 panel booleans only, so
// Live's Configure collects nothing but the knobs (a firmware change of a sound's sends, a preset, a mode reported on
// a parameter of its own lands in Configure too). -DFM1_TIER2_SLOTS=79 brings the slots back (an opt-in for direct
// mappings: ChoralRoot's 73 visible entries plus headroom; 8 + 79 + 41 = Live's 128); a ChoralRoot core built with
// FM1_EXPOSE_EDITOR (103 visible) needs 103 or more.
#ifndef FM1_TIER2_SLOTS
#define FM1_TIER2_SLOTS 0
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

// One of the FM-1's physical knobs as a host parameter (plan 4.5: the first page, "Knob Master" "Knob Select" "Knob
// Presets" "Knob Algo" "Knob 1".."Knob 4"; MASTER itself is a plain AudioParameterInt). The NAME NEVER CHANGES (a
// Roto-Control binds by name), nor does anything else the host caches (continuous 0..1, the default number of steps,
// default 0.5): what the knob does on the firmware's screen goes into the VALUE TEXT ("Voicing: -1", "Strum Rate:
// 126 ms"), so the host never needs parameterInfoChanged for a knob. The core's knob_target(role) names the map entry
// that knob turns now: the host value 0..1 is that entry's range, quantised inside (v = min + round(x (max - min))).
// -1: no value there, and the knob is a RELATIVE control (text "turn", centred at 0.5): a host change becomes detents
// (the change of the normalised value x detentsPerTravel, rounded; the remainder kept), sent to the device's encoder
// as a panel turn, and after kRecentreMs of device time without a host change the value springs back to 0.5
// (reported to the host without a gesture; a re-centre never makes detents). A relative knob turned on the device
// (panel, mouse, keys) is NUDGED: the host is told 0.5 + detents / kDefaultDetents inside a gesture (the
// Roto-Control's motor follows, Live's Configure collects the knob), then it springs back the same way.
// STEPPED (FM1Processor's "Encoders: Stepped", setStepped(N)): the host writes are the clicks of a stepped
// controller (a Roto-Control knob set to N steps: each click moves the value 1/N of the travel), so a write is never
// a value: its change against the host's previous value (hostValue(): what the host last wrote or was told, i.e.
// where the controller's motor sits) in steps (stepDelta) becomes that many detents of the device's encoder, bound
// or relative alike (takeSteps), as the real knob's clicks. Pushes from the firmware (setFromFirmware, the re-centre,
// a retarget) move hostValue() without making steps, so the next click counts from where the motor was put.
// THREADS: retargetKnob / takeDetents / takeSteps / recentre: the audio thread (lock-free, as
// Tier2Parameter::retarget); setStepped any thread; the rest as Tier2Parameter.
class KnobParameter : public Tier2Parameter {
public:
    KnobParameter(int role, const juce::String &id, const juce::String &fixedName);

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
    // what the host was last told or wrote (getValue() is what the processor holds: a re-centre or a retarget sets it
    // before the drain tells the host)
    float hostValue() const { return hostValue_.load(); }
    // the message thread: the host told the slot's value is x (0.5: relative), without making it a host change
    void setNormFromFirmware(float x);
    // the message thread: a device turn of a relative knob shown to the host (x: 0.5 + detents / kDefaultDetents),
    // inside a gesture; the next host change counts its detents from x
    void nudge(float x);
    // ---- Stepped: steps > 0 (8..64), 0 absolute. A change drops the old mode's pending work: the next write counts
    // from the host's current value, no detent from the switch itself
    void setStepped(int steps);
    int steps() const { return steps_.load(std::memory_order_acquire); }
    // the audio thread: the clicks the host made since the last call (false: none)
    bool takeSteps(int32_t &detents);
    // a stepped controller's move from x0 to x1 in clicks of a `steps` controller, rounded. Which quantisation the
    // controller uses is not known (k / (steps - 1): 0 and 1 both steps; k / steps): a value on either grid (within
    // 0.02 of a step) counts as that grid's step index against the previous value's index on the same grid (so a
    // value echoed back quantised, on either grid, is 0 clicks); a value on neither (a controller adding 1 / steps
    // to where it sits) counts its distance in steps (x (steps - 0.5): 1 / steps and 1 / (steps - 1) both 1)
    static int32_t stepDelta(float x0, float x1, int steps);
    // what the knob does now ("Voicing", "Strum Rate"; "" relative): the value text's prefix, the panel's caption
    juce::String functionName() const;

    void setValue(float newValue) override;
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName(int maximumStringLength) const override;
    juce::String getText(float normalisedValue, int maximumStringLength) const override;
    float getValueForText(const juce::String &text) const override;
    int getNumSteps() const override { return juce::AudioProcessor::getDefaultNumParameterSteps(); }
    bool isDiscrete() const override { return false; }
    juce::StringArray getAllValueStrings() const override { return {}; }

    static constexpr uint32_t kRecentreMs = 400;          // device ms without a host change
    static constexpr int kDefaultDetents = 24;            // detents per full travel (FM1Processor's setting)

private:
    const int role_;
    const juce::String name_;
    std::atomic<bool> relative_{true}, relPending_{false};
    std::atomic<double> relBase_{0.5};
    std::atomic<uint32_t> hostSeq_{0};
    std::atomic<float> hostValue_{0.5f};
    std::atomic<int> steps_{0};
    std::atomic<int32_t> stepAcc_{0};                     // clicks not yet taken by the audio thread
};
