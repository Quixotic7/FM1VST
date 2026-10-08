// SPDX-License-Identifier: GPL-3.0-only
// Asking a VST3 host to re-read the parameter values (FM1-VST-PLAN.md 4.5). A knob's value pushed without a gesture
// (its target changed with the screen, a re-centre, a firmware change it did not cause) reaches a VST3 host as
// performEdit outside beginEdit / endEdit, which the VST3 contract does not cover and a host may ignore (Live left
// Knob 1 at the voicing's value after the sound editor opened). JUCE's wrapper has stored the value in its edit
// controller (setParamNormalized) by then, so IComponentHandler::restartComponent(kParamValuesChanged) makes the host
// read it back, as after a preset load, without a touch (Configure, automation). JUCE hands the handler to the
// processor through VST3ClientExtensions::setIComponentHandler; the AU and Standalone wrappers never call it, and
// refresh() is then a no-op (an AU host takes the value change events JUCE sends as they are).
// THREADS: the message thread (the wrapper sets the handler there; the drain calls refresh there).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class Vst3HostRefresh : public juce::VST3ClientExtensions {
public:
    ~Vst3HostRefresh() override;
    void setIComponentHandler(Steinberg::FUnknown *handler) override;
    bool refresh();                                 // true: asked the host (a VST3 handler is set)
    uint32_t refreshes() const { return count_; }   // refresh() calls (with or without a handler: tests)

private:
    Steinberg::FUnknown *handler_ = nullptr;        // a Vst::IComponentHandler, referenced
    uint32_t count_ = 0;
};
