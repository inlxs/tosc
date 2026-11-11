// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include "WavetableBank.h"

class TerrainProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    TerrainProcessor();
    ~TerrainProcessor() override;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Terrain table (message thread)
    void setRelief (terrain::Relief relief);
    terrain::Relief getRelief() const;
    juce::ValueTree& regionInfo() { return region; }    // where the table came from (for the UI)

    // --- for the UI
    juce::MidiMessageCollector uiMidi;                    // notes played on the on-screen / computer keyboard
    int getReliefVersion() const noexcept { return reliefVersion.load(); }
    float getDisplayPosition() const noexcept { return displayPos.load(); }
    int getActiveVoices() const noexcept { return activeVoices.load(); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    struct Voice
    {
        int note = -1;
        float velocity = 0.0f;
        double freq = 0.0, phaseA = 0.0, phaseB = 0.0;
        float env = 0.0f;
        enum class Stage { off, attack, decay, release } stage = Stage::off;
        juce::int64 age = 0;
        juce::uint32 order = 0;
        int level = 0;
        float pos = 0.5f;
        bool isActive() const noexcept { return stage != Stage::off; }
    };

    void parameterChanged (const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void render (float* left, float* right, int start, int num);
    void swapInPendingBank();

    // tables
    mutable juce::CriticalSection reliefLock;
    terrain::Relief relief;
    juce::ValueTree region { "Region" };
    juce::SpinLock bankLock;
    std::shared_ptr<const terrain::WavetableBank> pendingBank;     // guarded by bankLock
    std::array<std::shared_ptr<const terrain::WavetableBank>, 8> retired;   // guarded by bankLock; freed off the audio thread
    std::shared_ptr<const terrain::WavetableBank> bank;             // audio thread only

    // voices
    std::array<Voice, 16> voices;
    juce::uint32 voiceCounter = 0;
    double sr = 48000.0;
    double lfoPhase = 0.0;
    std::vector<float> lfoBuffer;

    // effects
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::Reverb reverb;
    float dcX[2] {}, dcY[2] {};

    std::atomic<int> reliefVersion { 0 };
    std::atomic<float> displayPos { 0.5f };
    std::atomic<int> activeVoices { 0 };

    // cached parameter pointers
    std::atomic<float>* pPosition; std::atomic<float>* pScan;  std::atomic<float>* pRate;
    std::atomic<float>* pDepth;    std::atomic<float>* pShape; std::atomic<float>* pFly;
    std::atomic<float>* pAttack;   std::atomic<float>* pDecay; std::atomic<float>* pSustain;
    std::atomic<float>* pRelease;  std::atomic<float>* pCutoff; std::atomic<float>* pRes;
    std::atomic<float>* pDetune;   std::atomic<float>* pSpace; std::atomic<float>* pVolume;
    std::atomic<float>* pLoudness;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainProcessor)
};
