// SPDX-License-Identifier: GPL-3.0-only
// The Standalone app's own settings, for the sheet (JUCE's StandaloneFilterWindow keeps them behind its "Options" bar,
// which the editor hides): the audio / MIDI device selector, its state files, a reset. Everything is a no-op (and
// available() false) inside a plugin host.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace fm1standalone {
bool available();                                    // running as the Standalone app, its holder found
std::unique_ptr<juce::Component> createAudioSettings();   // the device selector (audio out, MIDI in / out), or nullptr
void saveAudioState();                               // the device choice written to the app's settings
void saveState();                                    // "Save current state..." (a file chooser)
void loadState();                                    // "Load a saved state..."
void resetToDefault();                               // the plugin re-created in its default state (asynchronously)
void tweakWindow(juce::Component &editor);           // the window: no "Options" bar, the native title bar
}
