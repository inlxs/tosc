// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// Hosts the web interface (ui/) in a WebView and bridges it to the processor.
class TerrainEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit TerrainEditor (TerrainProcessor&);
    ~TerrainEditor() override;
    void resized() override;

private:
    using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;
    using Args = juce::Array<juce::var>;

    juce::WebBrowserComponent::Options makeOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    void timerCallback() override;

    juce::var stateForUI();
    juce::var reliefForUI();
    juce::var paramsForUI (bool onlyChanged);
    juce::var regionForUI();

    void fnSetRelief (const Args&, Completion);
    void fnExport (const Args&, Completion);
    void fnSendToBitwig (const Args&, Completion);
    void fnOpen (const Args&, Completion);

    TerrainProcessor& proc;
    std::unique_ptr<juce::FileChooser> chooser;
    std::map<juce::String, float> lastSent;
    int lastReliefVersion = -1;
    float lastPos = -1.0f;
    int lastVoices = -1;
    bool uiReady = false;

    juce::WebBrowserComponent web;   // keep last: its options capture `this`

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainEditor)
};
