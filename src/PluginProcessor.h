// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include "WavetableBank.h"
#include "Dsp.h"
#include "Effects.h"

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
    double getTailLengthSeconds() const override { return 12.0; }

    // Host programs stay a single slot: some hosts call setCurrentProgram(0) after loading
    // a project, which would overwrite the user's sound. Presets go through loadPreset().
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Factory sounds (message thread)
    static juce::StringArray presetNames();
    void loadPreset (int index);
    juce::String getPresetName() const { return presetName; }

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

    static constexpr int kVoices = 16;
    static constexpr int kMaxUnison = 7;
    static constexpr int kControlInterval = 16;          // samples between pitch / filter / envelope updates

private:
    struct Voice
    {
        int note = -1;
        bool held = false, sustained = false;
        float velocity = 0.0f;
        float pitch = 60.0f, target = 60.0f;             // MIDI notes; pitch glides to target
        juce::int64 age = 0;                             // samples since note on (fly modes)
        juce::uint32 order = 0;                          // note-on order, for stealing
        float pos = 0.5f;

        double phase[kMaxUnison] {}, inc[kMaxUnison] {};
        float panL[kMaxUnison] {}, panR[kMaxUnison] {};
        double subPhase = 0.0, subInc = 0.0;
        int level = 0;

        float wander = 0.0f, wanderTo = 0.0f;            // drift
        int wanderCount = 0;

        tosc::Env amp, filt;
        tosc::FilterCoefs coefs;
        tosc::VoiceFilter fl, fr;
        float drive = 1.0f, gain = 0.0f;

        bool isActive() const noexcept { return amp.stage != tosc::Env::Off; }
    };

    void parameterChanged (const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void releaseVoice (Voice&);
    void setSustain (bool down);
    Voice& allocate (int note);
    void readParams();
    void controlTick();
    void render (float* left, float* right, int start, int num);
    void swapInPendingBank();
    void setParamValue (const juce::String& id, float realValue);

    // tables
    mutable juce::CriticalSection reliefLock;
    terrain::Relief relief;
    juce::ValueTree region { "Region" };
    juce::SpinLock bankLock;
    std::shared_ptr<const terrain::WavetableBank> pendingBank;     // guarded by bankLock
    std::array<std::shared_ptr<const terrain::WavetableBank>, 8> retired;   // guarded by bankLock; freed off the audio thread
    std::shared_ptr<const terrain::WavetableBank> bank;             // audio thread only

    // voices
    std::array<Voice, kVoices> voices;
    std::array<std::array<float, kMaxUnison + 1>, kVoices> tolerance {};   // fixed per-voice "component" offsets, -1..1
    juce::uint32 voiceCounter = 0;
    double sr = 48000.0;
    int ctlCountdown = 0;
    bool sustainPedal = false;
    float bend = 0.0f, lastNote = 60.0f;
    tosc::Rng rng, driftRng { 0x9E3779B9u };

    // per-block parameter snapshot
    struct Snap
    {
        int scan = 1, shape = 0, unison = 3, model = 0, mode = 0;
        float position = 0.5f, depth = 0.6f, lfoInc = 0.0f, flySamples = 48000.0f;
        float detune = 12.0f, width = 0.8f, sub = 0.0f, drift = 0.3f, glideCoef = 1.0f;
        float cutoffNote = 100.0f, res = 0.2f, drive = 1.0f, envAmt = 0.0f, keyTrack = 0.5f;
        tosc::EnvParams ampEnv, filtEnv;
    } p;
    float cutoffSmooth = 100.0f, posSmooth = 0.5f, posCoef = 0.001f;
    double lfoPhase = 0.0;
    std::vector<float> posBuffer;                         // smoothed base position (+ LFO sweep) per sample

    // effects
    float dcX[2] {}, dcY[2] {};
    tosc::Chorus chorus;
    tosc::PingPongDelay delay;
    tosc::Reverb reverb;
    tosc::Limiter limiter;
    float volSmooth = 0.0f;

    juce::String presetName { "Init" };

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
    std::atomic<float>* pUnison;   std::atomic<float>* pWidth; std::atomic<float>* pSub;
    std::atomic<float>* pDrift;    std::atomic<float>* pGlide;
    std::atomic<float>* pModel;    std::atomic<float>* pMode;  std::atomic<float>* pDrive;
    std::atomic<float>* pEnvAmt;   std::atomic<float>* pKeyTrack;
    std::atomic<float>* pFAttack;  std::atomic<float>* pFDecay; std::atomic<float>* pFSustain; std::atomic<float>* pFRelease;
    std::atomic<float>* pChorus;   std::atomic<float>* pDelayMix; std::atomic<float>* pDelayTime; std::atomic<float>* pDelayFb;
    std::atomic<float>* pSize;     std::atomic<float>* pVerbType;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainProcessor)
};
