// SPDX-License-Identifier: GPL-3.0-only
// The Standalone app's settings (see StandaloneHooks.h), through JUCE's own StandalonePluginHolder (header only).
#include "StandaloneHooks.h"

#include "Look.h"

#include <juce_audio_utils/juce_audio_utils.h>

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter();   // (Processor.cpp; the header below names it)
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace fm1standalone {
static juce::StandalonePluginHolder *holder()
{
    return juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone
               ? juce::StandalonePluginHolder::getInstance()
               : nullptr;
}

bool available() { return holder() != nullptr; }

std::unique_ptr<juce::Component> createAudioSettings()
{
    auto *h = holder();
    if (!h)
        return nullptr;
    const bool midiOut = h->processor != nullptr && h->processor->producesMidi();
    auto sel = std::make_unique<juce::AudioDeviceSelectorComponent>(h->deviceManager, 0, 0, 0, 2, true, midiOut, true, false);
    sel->setItemHeight(28);
    sel->setSize(400, 420);
    return sel;
}

juce::AudioDeviceManager *deviceManager()
{
    auto *h = holder();
    return h ? &h->deviceManager : nullptr;
}

juce::PropertySet *appSettings()
{
    auto *h = holder();
    return h ? h->settings.get() : nullptr;
}

void saveAudioState()
{
    if (auto *h = holder())
        h->saveAudioDeviceState();
}

void saveState()
{
    if (auto *h = holder())
        h->askUserToSaveState();
}

void loadState()
{
    if (auto *h = holder())
        h->askUserToLoadState();
}

void resetToDefault()
{
    // (it deletes the editor: never from inside the editor's own call)
    juce::MessageManager::callAsync([] {
        auto &d = juce::Desktop::getInstance();
        for (int i = 0; i < d.getNumComponents(); i++)
            if (auto *w = dynamic_cast<juce::StandaloneFilterWindow *>(d.getComponent(i))) {
                w->resetToDefaultState();
                return;
            }
    });
}

void tweakWindow(juce::Component &editor)
{
    auto *w = editor.findParentComponentOfClass<juce::DocumentWindow>();
    if (!w)
        return;
    for (juce::Component *c : w->getChildren())
        if (auto *b = dynamic_cast<juce::TextButton *>(c))
            if (b->getButtonText() == "Options")
                b->setVisible(false);
    w->setBackgroundColour(Look::col(Look::C::window));
    if (!w->isUsingNativeTitleBar())
        w->setUsingNativeTitleBar(true);
}
}   // namespace fm1standalone
