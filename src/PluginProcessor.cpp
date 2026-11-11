// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#if ! TERRAIN_HEADLESS
 #include "PluginEditor.h"
#endif
#include "BinaryData.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
}

APVTS::ParameterLayout TerrainProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); });
    auto secs = AudioParameterFloatAttributes().withLabel ("s");

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "position", 1 }, "Position", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scan", 1 }, "Scan", StringArray { "Off", "LFO sweep", "Fly per note", "Fly, looping" }, 1));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoRate", 1 }, "LFO rate", skewed (0.01f, 12.0f, 0.3f), 0.2f, AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoDepth", 1 }, "LFO depth", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoShape", 1 }, "LFO shape", StringArray { "Triangle", "Sine", "Ramp" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "flyTime", 1 }, "Fly time", skewed (0.5f, 30.0f, 6.0f), 6.0f, secs));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack", skewed (0.002f, 4.0f, 0.3f), 0.02f, secs));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 }, "Decay", skewed (0.01f, 6.0f, 0.8f), 0.6f, secs));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain", NormalisableRange<float> (0.0f, 1.0f), 0.75f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", skewed (0.01f, 8.0f, 1.2f), 1.2f, secs));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", skewed (60.0f, 18000.0f, 1500.0f), 7000.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "resonance", 1 }, "Resonance", skewed (0.5f, 12.0f, 2.0f), 0.9f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "detune", 1 }, "Stereo detune", NormalisableRange<float> (0.0f, 40.0f), 7.0f, AudioParameterFloatAttributes().withLabel ("ct")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "space", 1 }, "Space", NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "loudness", 1 }, "Loudness", StringArray { "Follows terrain", "Even per frame" }, 0));
    return l;
}

TerrainProcessor::TerrainProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    pPosition = apvts.getRawParameterValue ("position");  pScan    = apvts.getRawParameterValue ("scan");
    pRate     = apvts.getRawParameterValue ("lfoRate");   pDepth   = apvts.getRawParameterValue ("lfoDepth");
    pShape    = apvts.getRawParameterValue ("lfoShape");  pFly     = apvts.getRawParameterValue ("flyTime");
    pAttack   = apvts.getRawParameterValue ("attack");    pDecay   = apvts.getRawParameterValue ("decay");
    pSustain  = apvts.getRawParameterValue ("sustain");   pRelease = apvts.getRawParameterValue ("release");
    pCutoff   = apvts.getRawParameterValue ("cutoff");    pRes     = apvts.getRawParameterValue ("resonance");
    pDetune   = apvts.getRawParameterValue ("detune");    pSpace   = apvts.getRawParameterValue ("space");
    pVolume   = apvts.getRawParameterValue ("volume");    pLoudness = apvts.getRawParameterValue ("loudness");
    apvts.addParameterListener ("loudness", this);

    // Built-in default table so the instrument makes sound offline
    terrain::Relief r;
    juce::String err;
    if (terrain::readWT (BinaryData::Default_wt, (size_t) BinaryData::Default_wtSize, r, err))
    {
        r.name = "Mount Fuji";        // Copernicus GLO-30, 36 x 36 km, linear heights
        region.setProperty ("name", "Mount Fuji", nullptr);
        region.setProperty ("lat", 35.36, nullptr);
        region.setProperty ("lon", 138.73, nullptr);
        region.setProperty ("width", 36000.0, nullptr);
        region.setProperty ("height", 36000.0, nullptr);
        region.setProperty ("angle", 0.0, nullptr);
        region.setProperty ("sea", "off", nullptr);
        region.setProperty ("gamma", 1.0, nullptr);
        setRelief (std::move (r));
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
    lfoBuffer.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    filter.prepare (spec);
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    reverb.prepare (spec);
    for (auto& v : voices) v = Voice();
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
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

void TerrainProcessor::noteOn (int note, float velocity)
{
    for (auto& v : voices)
        if (v.isActive() && v.note == note && v.stage != Voice::Stage::release)
            v.stage = Voice::Stage::release;

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.isActive()) { target = &v; break; }
    if (target == nullptr)                      // steal the oldest
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.order < target->order) target = &v;
    }

    auto& v = *target;
    v.note = note;
    v.velocity = velocity;
    v.freq = juce::MidiMessage::getMidiNoteInHertz (note);
    v.level = terrain::WavetableBank::levelFor (v.freq, sr);
    v.phaseA = v.phaseB = juce::Random::getSystemRandom().nextDouble();
    v.env = 0.0f;
    v.stage = Voice::Stage::attack;
    v.age = 0;
    v.order = ++voiceCounter;
}

void TerrainProcessor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.isActive() && v.note == note && v.stage != Voice::Stage::release)
            v.stage = Voice::Stage::release;
}

void TerrainProcessor::render (float* left, float* right, int start, int num)
{
    if (bank == nullptr || num <= 0) return;

    const int scan = (int) pScan->load();
    const float basePos = pPosition->load(), depth = pDepth->load();
    const int shape = (int) pShape->load();
    const double lfoInc = pRate->load() / sr;
    const double flySamples = juce::jmax (0.05, (double) pFly->load()) * sr;
    const double da = std::pow (2.0, pDetune->load() / 2400.0), db = 1.0 / da;
    const float aInc = 1.0f / (float) juce::jmax (1.0, pAttack->load() * sr);
    const float dCo = 1.0f - (float) std::exp (-1.0 / juce::jmax (1.0, pDecay->load() * sr * 0.25));
    const float rCo = (float) std::exp (-1.0 / juce::jmax (1.0, pRelease->load() * sr * 0.25));
    const float sus = pSustain->load();

    // shared LFO for this segment
    if ((int) lfoBuffer.size() < num) lfoBuffer.resize ((size_t) num);   // only if the host lied about block size
    for (int i = 0; i < num; ++i)
    {
        const double ph = lfoPhase;
        float val = shape == 0 ? (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph)
                  : shape == 1 ? (float) std::sin (juce::MathConstants<double>::twoPi * ph)
                               : (float) (2.0 * ph - 1.0);
        lfoBuffer[(size_t) i] = val;
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
    }

    for (auto& v : voices)
    {
        if (! v.isActive()) continue;
        const double incA = v.freq * da / sr, incB = v.freq * db / sr;
        float pos = basePos;
        for (int i = 0; i < num; ++i)
        {
            pos = basePos;
            if (scan == 1)      pos += 0.5f * depth * lfoBuffer[(size_t) i];
            else if (scan == 2) pos += (float) juce::jmin (1.0, (double) v.age / flySamples) * (1.0f - basePos);
            else if (scan == 3) pos += (float) std::fmod ((double) v.age / flySamples, 1.0) * (1.0f - basePos);
            pos = juce::jlimit (0.0f, 1.0f, pos);

            const float sa = bank->read (v.level, pos, v.phaseA);
            const float sb = bank->read (v.level, pos, v.phaseB);
            v.phaseA += incA; if (v.phaseA >= 1.0) v.phaseA -= 1.0;
            v.phaseB += incB; if (v.phaseB >= 1.0) v.phaseB -= 1.0;

            switch (v.stage)
            {
                case Voice::Stage::attack:  v.env += aInc; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Voice::Stage::decay; } break;
                case Voice::Stage::decay:   v.env += (sus - v.env) * dCo; break;
                case Voice::Stage::release: v.env *= rCo; break;
                case Voice::Stage::off:     break;
            }
            const float g = v.env * v.velocity * 0.35f;
            left[start + i]  += sa * g;
            right[start + i] += sb * g;
            ++v.age;
        }
        v.pos = pos;
        if (v.stage == Voice::Stage::release && v.env < 1.0e-4f)
            v.stage = Voice::Stage::off;
    }
}

void TerrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    swapInPendingBank();
    uiMidi.removeNextBlockOfMessages (midi, buffer.getNumSamples());

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    const int n = buffer.getNumSamples();

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        render (L, R, pos, at - pos);
        pos = at;
        const auto m = meta.getMessage();
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& v : voices) v.stage = Voice::Stage::release;
        else if (m.isController())
        {
            // mod wheel + standard sound-controller numbers, so hardware knobs work without MIDI learn
            const char* id = nullptr;
            switch (m.getControllerNumber())
            {
                case 1:  id = "position";  break;
                case 7:  id = "volume";    break;
                case 71: id = "resonance"; break;
                case 72: id = "release";   break;
                case 73: id = "attack";    break;
                case 74: id = "cutoff";    break;
                case 91: id = "space";     break;
                default: break;
            }
            if (id != nullptr)
                if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (m.getControllerValue() / 127.0f);
        }
    }
    render (L, R, pos, n - pos);

    // DC blocker
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float y = d[i] - dcX[ch] + 0.9995f * dcY[ch];
            dcX[ch] = d[i]; dcY[ch] = y; d[i] = y;
        }
    }

    // filter + space + volume + soft limit
    filter.setCutoffFrequency (pCutoff->load());
    filter.setResonance (pRes->load());
    juce::dsp::AudioBlock<float> block (buffer);
    filter.process (juce::dsp::ProcessContextReplacing<float> (block));

    const float space = pSpace->load();
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.86f; rp.damping = 0.45f; rp.width = 1.0f;
    rp.wetLevel = space * 0.55f; rp.dryLevel = 1.0f - space * 0.35f;
    reverb.setParameters (rp);
    if (space > 0.001f) reverb.process (juce::dsp::ProcessContextReplacing<float> (block));

    const float gain = 2.5f * std::pow (pVolume->load(), 1.6f);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i) d[i] = std::tanh (d[i] * gain);
    }

    // report for the UI
    int count = 0; juce::uint32 newest = 0; float shown = pPosition->load();
    for (auto& v : voices)
        if (v.isActive()) { ++count; if (v.order >= newest) { newest = v.order; shown = v.pos; } }
    if (count == 0 && (int) pScan->load() == 1)
    {
        const double ph = lfoPhase;
        const int shape = (int) pShape->load();
        const float val = shape == 0 ? (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph)
                        : shape == 1 ? (float) std::sin (juce::MathConstants<double>::twoPi * ph) : (float) (2.0 * ph - 1.0);
        shown = juce::jlimit (0.0f, 1.0f, pPosition->load() + 0.5f * pDepth->load() * val);
    }
    displayPos.store (shown);
    activeVoices.store (count);
}

// ------------------------------------------------------------------ state

void TerrainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
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
