// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#if ! TERRAIN_HEADLESS
 #include "PluginEditor.h"
#endif
#include "BinaryData.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace tosc;

namespace
{
    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    constexpr float kOscLevel = 0.7f;     // oscillator sum into the filter (keeps drive at 0 nearly clean)
    constexpr float kVoiceGain = 0.42f;   // one voice peaks around -9 dBFS; chords go into the limiter

    struct PresetDef
    {
        const char* name;
        std::vector<std::pair<const char*, double>> values;   // real (not normalised) values; the rest are defaults
    };

    const std::vector<PresetDef>& presets()
    {
        static const std::vector<PresetDef> list {
            { "Init", {} },
            { "Terrain pad", { { "scan", 1 }, { "lfoRate", 0.07f }, { "lfoDepth", 0.8f }, { "unison", 5 }, { "detune", 18 }, { "width", 1 },
                               { "drift", 0.5f }, { "attack", 1.2f }, { "decay", 2.0f }, { "sustain", 0.85f }, { "release", 3.5f },
                               { "cutoff", 1300 }, { "res", 0.2f }, { "envAmount", 0.25f }, { "fAttack", 1.6f }, { "fDecay", 3.0f },
                               { "fSustain", 0.5f }, { "fRelease", 3.0f }, { "chorus", 0.45f }, { "delayMix", 0.15f },
                               { "delayTime", 0.5f }, { "delayFeedback", 0.45f }, { "space", 0.5f }, { "size", 0.75f } } },
            { "Glass pluck", { { "scan", 0 }, { "position", 0.52f }, { "unison", 2 }, { "detune", 6 }, { "filterModel", 1 },
                               { "attack", 0.002f }, { "decay", 0.55f }, { "sustain", 0 }, { "release", 0.6f },
                               { "cutoff", 700 }, { "res", 0.5f }, { "envAmount", 0.62f }, { "fAttack", 0.002f }, { "fDecay", 0.35f },
                               { "fSustain", 0 }, { "fRelease", 0.4f }, { "chorus", 0.2f }, { "delayMix", 0.28f }, { "delayTime", 0.375f },
                               { "delayFeedback", 0.45f }, { "space", 0.3f }, { "size", 0.5f }, { "reverbType", 0 } } },
            { "Ridge bass",  { { "scan", 0 }, { "position", 0.6f }, { "unison", 3 }, { "detune", 8 }, { "width", 0.3f }, { "sub", 0.6f },
                               { "glide", 0.04f }, { "attack", 0.003f }, { "decay", 0.4f }, { "sustain", 0.6f }, { "release", 0.25f },
                               { "cutoff", 260 }, { "res", 0.35f }, { "drive", 0.5f }, { "envAmount", 0.45f }, { "keyTrack", 0.3f },
                               { "fAttack", 0.002f }, { "fDecay", 0.3f }, { "fSustain", 0.15f }, { "fRelease", 0.3f },
                               { "chorus", 0 }, { "space", 0.08f } } },
            { "Fly over",    { { "scan", 2 }, { "flyTime", 8 }, { "position", 0 }, { "unison", 4 }, { "detune", 14 }, { "drift", 0.4f },
                               { "attack", 0.4f }, { "decay", 1.5f }, { "sustain", 0.9f }, { "release", 2.5f },
                               { "cutoff", 2600 }, { "res", 0.3f }, { "envAmount", 0.15f }, { "chorus", 0.3f },
                               { "delayMix", 0.2f }, { "delayTime", 0.45f }, { "space", 0.45f }, { "size", 0.7f } } },
            { "Wind drone",  { { "scan", 3 }, { "flyTime", 20 }, { "position", 0 }, { "unison", 7 }, { "detune", 30 }, { "width", 1 },
                               { "sub", 0.3f }, { "drift", 0.8f }, { "attack", 2.5f }, { "decay", 4 }, { "sustain", 1 }, { "release", 5 },
                               { "filterModel", 1 }, { "filterMode", 1 }, { "cutoff", 800 }, { "res", 0.55f }, { "envAmount", 0 },
                               { "chorus", 0.5f }, { "delayMix", 0.3f }, { "delayTime", 0.8f }, { "delayFeedback", 0.6f },
                               { "space", 0.7f }, { "size", 0.9f } } },
            { "Ridge lead",  { { "scan", 1 }, { "lfoRate", 0.25f }, { "lfoDepth", 0.3f }, { "unison", 3 }, { "detune", 10 }, { "glide", 0.08f },
                               { "attack", 0.01f }, { "decay", 0.6f }, { "sustain", 0.7f }, { "release", 0.3f },
                               { "cutoff", 1800 }, { "res", 0.4f }, { "drive", 0.4f }, { "envAmount", 0.35f }, { "fDecay", 0.5f },
                               { "delayMix", 0.2f }, { "delayTime", 0.3f }, { "space", 0.25f } } },
        };
        return list;
    }
}

APVTS::ParameterLayout TerrainProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    // Readable values for hosts and the UI: "20 ms", "1.20 s", "7.0 kHz", "0.20 Hz", "7.0 ct"
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); });
    auto signedPct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.004f ? "+" : "") + String (v, 2); });
    auto secs = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, v < 10.0f ? 2 : 1) + " s"; });
    auto glideTxt = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 0.001f ? String ("Off") : v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; });
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : v >= 100.0f ? String (roundToInt (v)) + " Hz" : String (v, 2) + " Hz"; });
    auto cents = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ct"; });
    auto sizeTxt = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { const float rt = 0.3f * std::pow (66.7f, v); return String (rt, rt < 10.0f ? 1 : 0) + " s"; });

    auto add1 = [&] (auto p) { l.add (std::move (p)); };

    // Flight (v0.1 IDs, kept so saved projects load)
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "position", 1 }, "Position", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    add1 (std::make_unique<AudioParameterChoice> (ParameterID { "scan", 1 }, "Scan", StringArray { "Off", "LFO", "Fly", "Fly loop" }, 1));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "lfoRate", 1 }, "LFO rate", skewed (0.01f, 12.0f, 0.3f), 0.2f, hz));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "lfoDepth", 1 }, "LFO depth", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    add1 (std::make_unique<AudioParameterChoice> (ParameterID { "lfoShape", 1 }, "LFO shape", StringArray { "Tri", "Sine", "Ramp" }, 0));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "flyTime", 1 }, "Fly time", skewed (0.5f, 30.0f, 6.0f), 6.0f, secs));

    // Oscillator
    add1 (std::make_unique<AudioParameterInt> (ParameterID { "unison", 2 }, "Unison", 1, kMaxUnison, 3));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "detune", 1 }, "Detune", NormalisableRange<float> (0.0f, 50.0f), 12.0f, cents));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "width", 2 }, "Width", NormalisableRange<float> (0.0f, 1.0f), 0.8f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "sub", 2 }, "Sub", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "drift", 2 }, "Drift", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 2 }, "Glide", skewed (0.0f, 2.0f, 0.15f), 0.0f, glideTxt));

    // Amp envelope
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack", skewed (0.002f, 4.0f, 0.3f), 0.02f, secs));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 }, "Decay", skewed (0.01f, 6.0f, 0.8f), 0.8f, secs));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain", NormalisableRange<float> (0.0f, 1.0f), 0.75f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", skewed (0.01f, 8.0f, 1.2f), 1.2f, secs));

    // Filter (one per voice)
    add1 (std::make_unique<AudioParameterChoice> (ParameterID { "filterModel", 2 }, "Filter model", StringArray { "Ladder", "SVF" }, 0));
    add1 (std::make_unique<AudioParameterChoice> (ParameterID { "filterMode", 2 }, "Filter mode", StringArray { "LP", "BP", "HP" }, 0));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", skewed (30.0f, 18000.0f, 1200.0f), 2400.0f, hz));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "res", 2 }, "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.2f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 2 }, "Drive", NormalisableRange<float> (0.0f, 1.0f), 0.15f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "envAmount", 2 }, "Filter env amount", NormalisableRange<float> (-1.0f, 1.0f), 0.3f, signedPct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "keyTrack", 2 }, "Key tracking", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));

    // Filter envelope
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "fAttack", 2 }, "Filter attack", skewed (0.002f, 4.0f, 0.3f), 0.005f, secs));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "fDecay", 2 }, "Filter decay", skewed (0.01f, 6.0f, 0.8f), 0.9f, secs));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "fSustain", 2 }, "Filter sustain", NormalisableRange<float> (0.0f, 1.0f), 0.35f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "fRelease", 2 }, "Filter release", skewed (0.01f, 8.0f, 1.2f), 1.0f, secs));

    // Effects
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "chorus", 2 }, "Chorus", NormalisableRange<float> (0.0f, 1.0f), 0.25f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "delayMix", 2 }, "Delay", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "delayTime", 2 }, "Delay time", skewed (0.02f, 1.5f, 0.35f), 0.375f, secs));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "delayFeedback", 2 }, "Delay feedback", NormalisableRange<float> (0.0f, 0.95f), 0.4f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "space", 1 }, "Reverb", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "size", 2 }, "Reverb size", NormalisableRange<float> (0.0f, 1.0f), 0.55f, sizeTxt));
    add1 (std::make_unique<AudioParameterChoice> (ParameterID { "reverbType", 2 }, "Reverb type", StringArray { "Plate", "Hall" }, 1));

    // Output
    add1 (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    add1 (std::make_unique<AudioParameterChoice> (ParameterID { "loudness", 1 }, "Loudness", StringArray { "Terrain", "Even" }, 0));
    return l;
}

TerrainProcessor::TerrainProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    auto P = [this] (const char* id) { auto* a = apvts.getRawParameterValue (id); jassert (a != nullptr); return a; };
    pPosition = P ("position");  pScan    = P ("scan");      pRate    = P ("lfoRate");   pDepth = P ("lfoDepth");
    pShape    = P ("lfoShape");  pFly     = P ("flyTime");   pAttack  = P ("attack");    pDecay = P ("decay");
    pSustain  = P ("sustain");   pRelease = P ("release");   pCutoff  = P ("cutoff");    pRes   = P ("res");
    pDetune   = P ("detune");    pSpace   = P ("space");     pVolume  = P ("volume");    pLoudness = P ("loudness");
    pUnison   = P ("unison");    pWidth   = P ("width");     pSub     = P ("sub");       pDrift = P ("drift");
    pGlide    = P ("glide");     pModel   = P ("filterModel"); pMode  = P ("filterMode"); pDrive = P ("drive");
    pEnvAmt   = P ("envAmount"); pKeyTrack = P ("keyTrack");
    pFAttack  = P ("fAttack");   pFDecay  = P ("fDecay");    pFSustain = P ("fSustain"); pFRelease = P ("fRelease");
    pChorus   = P ("chorus");    pDelayMix = P ("delayMix"); pDelayTime = P ("delayTime"); pDelayFb = P ("delayFeedback");
    pSize     = P ("size");      pVerbType = P ("reverbType");
    apvts.addParameterListener ("loudness", this);

    // Each voice's "component tolerances": fixed tuning offsets per unison oscillator, plus a cutoff offset.
    Rng r (0xC0FFEEu);
    for (auto& t : tolerance) for (auto& x : t) x = r.nextBipolar();

    // Built-in default table so the instrument makes sound offline
    terrain::Relief rel;
    juce::String err;
    if (terrain::readWT (BinaryData::Default_wt, (size_t) BinaryData::Default_wtSize, rel, err))
    {
        rel.name = "Mount Fuji";        // Copernicus GLO-30, 36 x 36 km, linear heights
        region.setProperty ("name", "Mount Fuji", nullptr);
        region.setProperty ("lat", 35.36, nullptr);
        region.setProperty ("lon", 138.73, nullptr);
        region.setProperty ("width", 36000.0, nullptr);
        region.setProperty ("height", 36000.0, nullptr);
        region.setProperty ("angle", 0.0, nullptr);
        region.setProperty ("sea", "off", nullptr);
        region.setProperty ("gamma", 1.0, nullptr);
        setRelief (std::move (rel));
    }
    startTimer (500);
}

TerrainProcessor::~TerrainProcessor()
{
    apvts.removeParameterListener ("loudness", this);
    stopTimer();
}

bool TerrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void TerrainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    uiMidi.reset (sampleRate);
    posBuffer.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.5f);
    for (auto& v : voices) v = Voice();
    ctlCountdown = 0;
    lfoPhase = 0.25;    // triangle and sine are at their centre here: the sweep starts at the set position
    sustainPedal = false;
    bend = 0.0f;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    posCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));    // 20 ms position smoothing
    readParams();
    cutoffSmooth = p.cutoffNote;
    posSmooth = p.position;

    chorus.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    limiter.prepare (sampleRate);
    readParams();
    delay.snap();
    volSmooth = 2.0f * std::pow (pVolume->load(), 1.6f);
    setLatencySamples (limiter.latency());
}

// ------------------------------------------------------------------ presets

juce::StringArray TerrainProcessor::presetNames()
{
    juce::StringArray names;
    for (auto& pr : presets()) names.add (pr.name);
    return names;
}

void TerrainProcessor::setParamValue (const juce::String& id, float realValue)
{
    if (auto* prm = apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

void TerrainProcessor::loadPreset (int index)
{
    const auto& list = presets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& pr = list[(size_t) index];
    // Everything that shapes the sound goes back to its default first; volume and loudness stay.
    for (auto* base : getParameters())
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (base))
        {
            const auto id = prm->getParameterID();
            if (id == "volume" || id == "loudness") continue;
            prm->setValueNotifyingHost (prm->getDefaultValue());
        }
    for (auto& [id, value] : pr.values) setParamValue (id, (float) value);
    presetName = pr.name;
}

// ------------------------------------------------------------------ tables

void TerrainProcessor::setRelief (terrain::Relief r)
{
    auto built = terrain::WavetableBank::build (r, pLoudness->load() > 0.5f);
    if (built == nullptr) return;
    {
        const juce::ScopedLock sl (reliefLock);
        relief = std::move (r);
    }
    ++reliefVersion;
    const juce::SpinLock::ScopedLockType bl (bankLock);
    pendingBank = std::move (built);
}

terrain::Relief TerrainProcessor::getRelief() const
{
    const juce::ScopedLock sl (reliefLock);
    return relief;
}

void TerrainProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "loudness") triggerAsyncUpdate();       // may arrive on the audio thread; rebuild later
}

void TerrainProcessor::handleAsyncUpdate()
{
    setRelief (getRelief());
}

void TerrainProcessor::timerCallback()
{
    // free retired banks on the message thread, never on the audio thread
    std::array<std::shared_ptr<const terrain::WavetableBank>, 8> toFree;
    {
        const juce::SpinLock::ScopedLockType bl (bankLock);
        toFree.swap (retired);
    }
}

void TerrainProcessor::swapInPendingBank()
{
    const juce::SpinLock::ScopedTryLockType bl (bankLock);
    if (! bl.isLocked() || pendingBank == nullptr) return;
    for (auto& slot : retired)
        if (slot == nullptr)
        {
            slot = std::move (bank);
            bank = std::move (pendingBank);
            return;
        }
    // no free slot this block: try again next block
}

// ------------------------------------------------------------------ voices

TerrainProcessor::Voice& TerrainProcessor::allocate (int note)
{
    // Same note already sounding > idle voice > oldest released voice > oldest voice.
    for (auto& v : voices) if (v.isActive() && v.note == note) return v;
    for (auto& v : voices) if (! v.isActive()) return v;
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (! v.held && ! v.sustained && (oldest == nullptr || v.order < oldest->order)) oldest = &v;
    if (oldest != nullptr) return *oldest;
    oldest = &voices[0];
    for (auto& v : voices) if (v.order < oldest->order) oldest = &v;
    return *oldest;
}

void TerrainProcessor::noteOn (int note, float velocity)
{
    Voice& v = allocate (note);
    if (! v.isActive())
    {
        // Fresh voice: the first oscillator starts at phase 0 for a consistent attack, the
        // unison copies anywhere (that is what makes them sound like separate players).
        v.phase[0] = 0.0;
        for (int k = 1; k < kMaxUnison; ++k) v.phase[k] = rng.next01();
        v.subPhase = 0.0;
        v.fl.clear(); v.fr.clear();
        v.amp.level = v.filt.level = 0.0f;
        v.wander = 0.0f; v.wanderCount = 0;
        v.pitch = p.glideCoef < 1.0f ? lastNote : (float) note;
    }
    else if (p.glideCoef >= 1.0f)
        v.pitch = (float) note;
    // (a retriggered or stolen voice keeps its phases and ramps up from its current level: no click)

    v.note = note;
    v.held = true;
    v.sustained = false;
    v.velocity = velocity;
    v.target = (float) note;
    v.age = 0;
    v.order = ++voiceCounter;
    v.amp.stage = Env::Attack;
    v.filt.stage = Env::Attack;
    lastNote = (float) note;
    ctlCountdown = 0;   // compute its pitch and filter before the next sample
}

void TerrainProcessor::releaseVoice (Voice& v)
{
    v.held = false;
    if (v.amp.stage != Env::Off) v.amp.stage = Env::Release;
    if (v.filt.stage != Env::Off) v.filt.stage = Env::Release;
}

void TerrainProcessor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.note == note && v.held)
        {
            v.held = false;
            if (sustainPedal) v.sustained = true;
            else releaseVoice (v);
        }
}

void TerrainProcessor::setSustain (bool down)
{
    sustainPedal = down;
    if (! down)
        for (auto& v : voices)
            if (v.sustained) { v.sustained = false; if (! v.held) releaseVoice (v); }
}

void TerrainProcessor::readParams()
{
    const float ctlRate = (float) sr / kControlInterval;
    p.scan  = (int) pScan->load();
    p.shape = (int) pShape->load();
    p.position = pPosition->load();
    p.depth = pDepth->load();
    p.lfoInc = (float) (pRate->load() / sr);
    p.flySamples = (float) (juce::jmax (0.05f, pFly->load()) * sr);

    p.unison = juce::jlimit (1, kMaxUnison, (int) std::lround (pUnison->load()));
    p.detune = pDetune->load();
    p.width  = pWidth->load();
    p.sub    = pSub->load();
    p.drift  = pDrift->load();
    const float glide = pGlide->load();
    p.glideCoef = glide < 0.001f ? 1.0f : settleCoef (glide, ctlRate);

    p.model = (int) pModel->load();
    p.mode  = (int) pMode->load();
    p.cutoffNote = hzToNote (pCutoff->load());
    p.res = pRes->load();
    const float dn = pDrive->load();
    p.drive = 1.0f + 7.0f * dn * dn;
    p.envAmt = pEnvAmt->load();
    p.keyTrack = pKeyTrack->load();

    p.ampEnv.set (pAttack->load(), pDecay->load(), pSustain->load(), pRelease->load(), (float) sr);
    p.filtEnv.set (pFAttack->load(), pFDecay->load(), pFSustain->load(), pFRelease->load(), ctlRate);

    chorus.set (pChorus->load(), 0.5f, 1.9f);
    delay.set (pDelayMix->load() * 0.8f, pDelayTime->load(), pDelayFb->load());
    reverb.set ((int) pVerbType->load(), pSpace->load(), pSize->load(), 0.55f, 0.018f);
}

// Pitch, unison spread, mip level, drift and filter for every sounding voice (every 16 samples).
void TerrainProcessor::controlTick()
{
    const float ctlRate = (float) sr / kControlInterval;
    const float nyq = 0.45f * (float) sr;
    const int n = p.unison;
    const float wanderCoef = 1.0f - std::exp (-1.0f / (0.17f * ctlRate));
    cutoffSmooth += (p.cutoffNote - cutoffSmooth) * 0.08f;

    for (size_t i = 0; i < voices.size(); ++i)
    {
        Voice& v = voices[i];
        if (! v.isActive()) continue;
        const auto& tol = tolerance[i];

        const float fe = v.filt.tick (p.filtEnv);
        v.pitch += (v.target - v.pitch) * p.glideCoef;

        float driftNote = 0.0f, driftCut = 0.0f;
        if (p.drift > 0.0f)
        {
            // The voice wanders towards a new random target every 0.2-0.6 s, on top of its
            // fixed tuning offset: up to +-6 cents, and its cutoff sits up to 2 semitones off.
            if (--v.wanderCount <= 0)
            {
                v.wanderTo = driftRng.nextBipolar();
                v.wanderCount = (int) (ctlRate * (0.2f + 0.4f * driftRng.next01()));
            }
            v.wander += (v.wanderTo - v.wander) * wanderCoef;
            driftNote = p.drift * 0.06f * (tol[0] + v.wander);
            driftCut = p.drift * 2.0f * tol[kMaxUnison];
        }

        const float hz = noteToHz (v.pitch + bend + driftNote);
        for (int k = 0; k < n; ++k)
        {
            const float t = n > 1 ? (float) k / (float) (n - 1) : 0.5f;
            float cents = n > 1 ? p.detune * (t - 0.5f) : 0.0f;
            if (k > 0) cents += p.drift * 3.0f * tol[(size_t) k];             // unison copies are never quite in tune
            v.inc[k] = hz * std::exp2 (cents / 1200.0f) / sr;
            const float pan = n > 1 ? p.width * (2.0f * t - 1.0f) : 0.0f;
            const float a = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            v.panL[k] = juce::MathConstants<float>::sqrt2 * std::cos (a);
            v.panR[k] = juce::MathConstants<float>::sqrt2 * std::sin (a);
        }
        v.subInc = hz * 0.5 / sr;
        const float topHz = hz * std::exp2 ((n > 1 ? 0.5f * p.detune : 0.0f) / 1200.0f + 0.01f);
        v.level = terrain::WavetableBank::levelFor (topHz, sr);

        const float cn = cutoffSmooth + fe * p.envAmt * 72.0f + p.keyTrack * (v.pitch - 60.0f)
                       + 10.0f * (v.velocity - 1.0f) + driftCut;
        v.coefs.set (p.model, clampf (noteToHz (cn), 16.0f, nyq), (float) sr, p.res);
        v.drive = p.drive;
        v.gain = kVoiceGain * (0.35f + 0.65f * v.velocity);
    }
}

void TerrainProcessor::render (float* left, float* right, int start, int num)
{
    if (bank == nullptr || num <= 0) return;
    const int n = p.unison;
    const float uniNorm = kOscLevel / std::sqrt ((float) n);
    const float subLevel = p.sub * kOscLevel;
    const int model = p.model, mode = p.mode, scan = p.scan;
    const float fly = p.flySamples;

    for (int done = 0; done < num;)
    {
        if (ctlCountdown <= 0) { controlTick(); ctlCountdown = kControlInterval; }
        const int chunk = juce::jmin (num - done, ctlCountdown);
        const int s0 = start + done;

        for (auto& v : voices)
        {
            if (! v.isActive()) continue;
            float pos = v.pos;
            for (int i = 0; i < chunk; ++i)
            {
                const float base = posBuffer[(size_t) (s0 + i)];
                if (scan == 2)      pos = base + juce::jmin (1.0f, (float) v.age / fly) * (1.0f - base);
                else if (scan == 3) pos = base + (float) std::fmod ((double) v.age / fly, 1.0) * (1.0f - base);
                else                pos = base;
                pos = clampf (pos, 0.0f, 1.0f);

                const auto slice = bank->at (v.level, pos);
                float l = 0.0f, r = 0.0f;
                for (int k = 0; k < n; ++k)
                {
                    const float s = terrain::WavetableBank::read (slice, v.phase[k]);
                    l += s * v.panL[k];
                    r += s * v.panR[k];
                    v.phase[k] += v.inc[k];
                    if (v.phase[k] >= 1.0) v.phase[k] -= 1.0;
                }
                l *= uniNorm; r *= uniNorm;
                if (subLevel > 0.0f)
                {
                    const float s = subLevel * (float) std::sin (juce::MathConstants<double>::twoPi * v.subPhase);
                    l += s; r += s;
                }
                v.subPhase += v.subInc;
                if (v.subPhase >= 1.0) v.subPhase -= 1.0;

                if (model == Analog) { l = v.fl.analog (l, v.coefs, v.drive, mode); r = v.fr.analog (r, v.coefs, v.drive, mode); }
                else                 { l = v.fl.ladder (l, v.coefs, v.drive, mode); r = v.fr.ladder (r, v.coefs, v.drive, mode); }

                const float g = v.amp.tick (p.ampEnv) * v.gain;
                left[s0 + i]  += l * g;
                right[s0 + i] += r * g;
                ++v.age;
            }
            v.pos = pos;
            if (! v.isActive()) { v.fl.clear(); v.fr.clear(); v.held = v.sustained = false; }
        }
        ctlCountdown -= chunk;
        done += chunk;
    }
}

void TerrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    swapInPendingBank();
    const int n = buffer.getNumSamples();
    uiMidi.removeNextBlockOfMessages (midi, n);
    readParams();

    // Shared position per sample: the knob smoothed (no zipper noise), plus the LFO sweep.
    if ((int) posBuffer.size() < n) posBuffer.resize ((size_t) n);    // only if the host lied about block size
    for (int i = 0; i < n; ++i)
    {
        posSmooth += (p.position - posSmooth) * posCoef;
        float pos = posSmooth;
        if (p.scan == 1)
        {
            const double ph = lfoPhase;
            const float val = p.shape == 0 ? (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph)
                            : p.shape == 1 ? (float) std::sin (juce::MathConstants<double>::twoPi * ph)
                                           : (float) (2.0 * ph - 1.0);
            pos += 0.5f * p.depth * val;
        }
        lfoPhase += p.lfoInc;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        posBuffer[(size_t) i] = clampf (pos, 0.0f, 1.0f);
    }

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    int at0 = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        render (L, R, at0, at - at0);
        at0 = at;
        const auto m = meta.getMessage();
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { setSustain (false); for (auto& v : voices) if (v.isActive()) releaseVoice (v); }
        else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        else if (m.isSustainPedalOn()) setSustain (true);
        else if (m.isSustainPedalOff()) setSustain (false);
        else if (m.isController())
        {
            // mod wheel + standard sound-controller numbers, so hardware knobs work without MIDI learn
            const char* id = nullptr;
            switch (m.getControllerNumber())
            {
                case 1:  id = "position";  break;
                case 5:  id = "glide";     break;
                case 7:  id = "volume";    break;
                case 71: id = "res";       break;
                case 72: id = "release";   break;
                case 73: id = "attack";    break;
                case 74: id = "cutoff";    break;
                case 75: id = "decay";     break;
                case 91: id = "space";     break;
                case 93: id = "chorus";    break;
                default: break;
            }
            if (id != nullptr)
                if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (m.getControllerValue() / 127.0f);
        }
    }
    render (L, R, at0, n - at0);

    // DC blocker -> chorus -> delay -> reverb -> volume -> limiter
    const float volTarget = 2.0f * std::pow (pVolume->load(), 1.6f);
    const float volCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        const float yl = l - dcX[0] + 0.9995f * dcY[0]; dcX[0] = l; dcY[0] = yl; l = yl;
        const float yr = r - dcX[1] + 0.9995f * dcY[1]; dcX[1] = r; dcY[1] = yr; r = yr;
        chorus.process (l, r);
        delay.process (l, r);
        reverb.process (l, r);
        volSmooth += (volTarget - volSmooth) * volCoef;
        l *= volSmooth; r *= volSmooth;
        limiter.process (l, r);
        L[i] = l; R[i] = r;
    }

    // report for the UI
    int count = 0; juce::uint32 newest = 0; float shown = posBuffer[(size_t) juce::jmax (0, n - 1)];
    for (auto& v : voices)
        if (v.isActive()) { ++count; if (v.order >= newest) { newest = v.order; shown = v.pos; } }
    displayPos.store (shown);
    activeVoices.store (count);
}

// ------------------------------------------------------------------ state

void TerrainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", presetName, nullptr);
    const auto r = getRelief();
    juce::ValueTree t ("Relief");
    t.setProperty ("frames", r.frames, nullptr);
    t.setProperty ("name", r.name, nullptr);
    juce::MemoryOutputStream pcm;
    for (auto x : r.values) pcm.writeShort ((short) juce::roundToInt (juce::jlimit (0.0f, 1.0f, x) * 32767.0f));
    t.setProperty ("data", pcm.getMemoryBlock().toBase64Encoding(), nullptr);
    state.appendChild (t, nullptr);
    state.appendChild (region.createCopy(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TerrainProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid()) return;

    auto t = state.getChildWithName ("Relief");
    auto reg = state.getChildWithName ("Region");
    if (t.isValid()) state.removeChild (t, nullptr);
    if (reg.isValid()) { state.removeChild (reg, nullptr); region = reg; }
    presetName = state.getProperty ("preset", "Init").toString();
    apvts.replaceState (state);

    if (t.isValid())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (t["data"].toString()))
        {
            terrain::Relief r;
            r.frames = (int) t["frames"];
            r.name = t["name"].toString();
            const auto* s = static_cast<const juce::int16*> (mb.getData());
            const size_t count = mb.getSize() / 2;
            r.values.resize (count);
            for (size_t i = 0; i < count; ++i) r.values[i] = (float) juce::ByteOrder::swapIfBigEndian (s[i]) / 32767.0f;
            if (r.isValid()) setRelief (std::move (r));
        }
    }
}

juce::AudioProcessorEditor* TerrainProcessor::createEditor()
{
   #if TERRAIN_HEADLESS
    return nullptr;
   #else
    return new TerrainEditor (*this);
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TerrainProcessor();
}
