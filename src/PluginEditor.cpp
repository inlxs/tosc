// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String mimeFor (const juce::String& path)
    {
        const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (ext == "html") return "text/html";
        if (ext == "js")   return "text/javascript";
        if (ext == "css")  return "text/css";
        if (ext == "svg")  return "image/svg+xml";
        if (ext == "png")  return "image/png";
        if (ext == "json") return "application/json";
        return "application/octet-stream";
    }

    std::vector<std::byte> toBytes (const void* data, size_t size)
    {
        const auto* p = static_cast<const std::byte*> (data);
        return { p, p + size };
    }

    juce::String safeFileName (juce::String s)
    {
        s = s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ").trim();
        return s.isEmpty() ? juce::String ("Terrain") : s.replaceCharacter (' ', '_');
    }
}

TerrainEditor::TerrainEditor (TerrainProcessor& p)
    : AudioProcessorEditor (p), proc (p), web (makeOptions())
{
    addAndMakeVisible (web);
    setResizable (true, true);
    setResizeLimits (860, 560, 2400, 1600);
    setSize (1100, 720);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (30);
}

TerrainEditor::~TerrainEditor() { stopTimer(); }

void TerrainEditor::resized() { web.setBounds (getLocalBounds()); }

juce::WebBrowserComponent::Options TerrainEditor::makeOptions()
{
    using Opts = juce::WebBrowserComponent::Options;
    const auto dataDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TerrainOscillatorWebView");

    return Opts{}
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2{}
                                     .withUserDataFolder (dataDir)
                                     .withBackgroundColour (juce::Colour (0xff0b1012)))
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const auto& url) { return getResource (url); })
        .withNativeFunction ("getState", [this] (const Args&, Completion done)
        {
            uiReady = true;
            lastReliefVersion = proc.getReliefVersion();
            done (stateForUI());
        })
        .withNativeFunction ("setParam", [this] (const Args& a, Completion done)
        {
            if (a.size() >= 2)
                if (auto* prm = proc.apvts.getParameter (a[0].toString()))
                {
                    prm->setValueNotifyingHost ((float) (double) a[1]);
                    lastSent[a[0].toString()] = prm->getValue();
                }
            done ({});
        })
        .withNativeFunction ("beginEdit", [this] (const Args& a, Completion done)
        {
            if (auto* prm = proc.apvts.getParameter (a[0].toString())) prm->beginChangeGesture();
            done ({});
        })
        .withNativeFunction ("endEdit", [this] (const Args& a, Completion done)
        {
            if (auto* prm = proc.apvts.getParameter (a[0].toString())) prm->endChangeGesture();
            done ({});
        })
        .withNativeFunction ("note", [this] (const Args& a, Completion done)
        {
            const int note = juce::jlimit (0, 127, (int) a[0]);
            const float vel = a.size() > 1 ? (float) (double) a[1] : 0.0f;
            auto m = vel > 0.0f ? juce::MidiMessage::noteOn (1, note, vel) : juce::MidiMessage::noteOff (1, note);
            m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
            proc.uiMidi.addMessageToQueue (m);
            done ({});
        })
        .withNativeFunction ("setRelief", [this] (const Args& a, Completion done) { fnSetRelief (a, std::move (done)); })
        .withNativeFunction ("exportWT",  [this] (const Args& a, Completion done) { fnExport (a, std::move (done)); })
        .withNativeFunction ("sendToBitwig", [this] (const Args& a, Completion done) { fnSendToBitwig (a, std::move (done)); })
        .withNativeFunction ("openWT",    [this] (const Args& a, Completion done) { fnOpen (a, std::move (done)); });
}

std::optional<juce::WebBrowserComponent::Resource> TerrainEditor::getResource (const juce::String& url)
{
    auto path = url.fromFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (path.isEmpty()) path = "index.html";

    // Development: serve straight from disk when TERRAIN_UI_DIR is set (no rebuild needed)
    const auto devDir = juce::SystemStats::getEnvironmentVariable ("TERRAIN_UI_DIR", {});
    if (devDir.isNotEmpty())
    {
        const auto f = juce::File (devDir).getChildFile (path);
        juce::MemoryBlock mb;
        if (f.existsAsFile() && f.loadFileAsData (mb))
            return juce::WebBrowserComponent::Resource { toBytes (mb.getData(), mb.getSize()), mimeFor (path) };
    }

    const auto fileName = path.fromLastOccurrenceOf ("/", false, false);
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (fileName == BinaryData::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return juce::WebBrowserComponent::Resource { toBytes (data, (size_t) size), mimeFor (path) };
        }
    return std::nullopt;
}

// ------------------------------------------------------------------ state for the UI

juce::var TerrainEditor::reliefForUI()
{
    const auto r = proc.getRelief();
    juce::MemoryOutputStream pcm;
    for (auto x : r.values) pcm.writeShort ((short) juce::roundToInt (juce::jlimit (0.0f, 1.0f, x) * 32767.0f));
    auto* o = new juce::DynamicObject();
    o->setProperty ("frames", r.frames);
    o->setProperty ("name", r.name);
    o->setProperty ("data", juce::Base64::toBase64 (pcm.getData(), pcm.getDataSize()));   // standard Base64 for atob()
    return o;
}

juce::var TerrainEditor::regionForUI()
{
    auto* o = new juce::DynamicObject();
    const auto& reg = proc.regionInfo();
    for (int i = 0; i < reg.getNumProperties(); ++i)
        o->setProperty (reg.getPropertyName (i), reg.getProperty (reg.getPropertyName (i)));
    return o;
}

juce::var TerrainEditor::paramsForUI (bool onlyChanged)
{
    juce::Array<juce::var> list;
    for (auto* base : proc.getParameters())
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (base))
        {
            const auto id = prm->getParameterID();
            const float v = prm->getValue();
            if (onlyChanged)
            {
                auto it = lastSent.find (id);
                if (it != lastSent.end() && std::abs (it->second - v) < 1.0e-6f && ! prm->isDiscrete()) continue;
                if (it != lastSent.end() && it->second == v) continue;
            }
            lastSent[id] = v;
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", id);
            o->setProperty ("value", v);
            o->setProperty ("text", prm->getCurrentValueAsText() + (prm->getLabel().isNotEmpty() ? " " + prm->getLabel() : juce::String()));
            if (! onlyChanged)
            {
                o->setProperty ("name", prm->getName (40));
                o->setProperty ("default", prm->getDefaultValue());
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (prm))
                {
                    juce::Array<juce::var> choices;
                    for (auto& s : c->choices) choices.add (s);
                    o->setProperty ("choices", choices);
                }
            }
            list.add (o);
        }
    return list;
}

juce::var TerrainEditor::stateForUI()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("relief", reliefForUI());
    o->setProperty ("region", regionForUI());
    o->setProperty ("params", paramsForUI (false));
    o->setProperty ("standalone", juce::JUCEApplicationBase::isStandaloneApp());
    o->setProperty ("version", JucePlugin_VersionString);
    return o;
}

void TerrainEditor::timerCallback()
{
    if (! uiReady) return;

    const auto params = paramsForUI (true);
    if (params.size() > 0) web.emitEventIfBrowserIsVisible ("params", params);

    const float pos = proc.getDisplayPosition();
    const int voices = proc.getActiveVoices();
    if (std::abs (pos - lastPos) > 1.0e-4f || voices != lastVoices)
    {
        lastPos = pos; lastVoices = voices;
        auto* o = new juce::DynamicObject();
        o->setProperty ("pos", pos);
        o->setProperty ("voices", voices);
        web.emitEventIfBrowserIsVisible ("live", juce::var (o));
    }

    if (proc.getReliefVersion() != lastReliefVersion)       // changed by the host (project load) or a file
    {
        lastReliefVersion = proc.getReliefVersion();
        auto* o = new juce::DynamicObject();
        o->setProperty ("relief", reliefForUI());
        o->setProperty ("region", regionForUI());
        web.emitEventIfBrowserIsVisible ("relief", juce::var (o));
    }
}

// ------------------------------------------------------------------ native functions

void TerrainEditor::fnSetRelief (const Args& a, Completion done)
{
    // args: frames, base64 int16 (0..32767), name, region object
    terrain::Relief r;
    r.frames = (int) a[0];
    r.name = a[2].toString();
    juce::MemoryOutputStream decoded;
    if (! juce::Base64::convertFromBase64 (decoded, a[1].toString())) { done (juce::var ("bad data")); return; }
    const auto mb = decoded.getMemoryBlock();
    const auto* s = static_cast<const juce::int16*> (mb.getData());
    r.values.resize (mb.getSize() / 2);
    for (size_t i = 0; i < r.values.size(); ++i) r.values[i] = (float) s[i] / 32767.0f;
    if (! r.isValid()) { done (juce::var ("wrong size")); return; }

    auto& reg = proc.regionInfo();
    reg.removeAllProperties (nullptr);
    if (auto* obj = a[3].getDynamicObject())
        for (auto& prop : obj->getProperties())
            reg.setProperty (prop.name, prop.value, nullptr);

    proc.setRelief (std::move (r));
    lastReliefVersion = proc.getReliefVersion();      // the UI already has it
    done (juce::var (true));
}

void TerrainEditor::fnExport (const Args& a, Completion done)
{
    const bool centred = a[0].toString() == "ctr";
    const bool wav = a.size() > 1 && a[1].toString() == "wav";
    const auto r = proc.getRelief();
    const auto name = safeFileName (r.name) + (centred ? "_ctr_" : "_abs_") + juce::String (r.frames) + (wav ? ".wav" : ".wt");
    chooser = std::make_unique<juce::FileChooser> (wav ? "Save wavetable (Serum / Vital / Ableton WAV)" : "Save wavetable (.wt for Bitwig, Surge)",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name), wav ? "*.wav" : "*.wt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, r, centred, wav, done = std::move (done)] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) { done (juce::var()); return; }
            const auto data = wav ? terrain::writeWAV (r, centred) : terrain::writeWT (r, centred);
            done (f.replaceWithData (data.getData(), data.getSize()) ? juce::var (f.getFullPathName()) : juce::var());
        });
}

void TerrainEditor::fnSendToBitwig (const Args& a, Completion done)
{
    const bool centred = a[0].toString() == "ctr";
    const auto r = proc.getRelief();
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto dir = docs.getChildFile ("Bitwig Studio").getChildFile ("Library").getChildFile ("Wavetables").getChildFile ("Terrain Oscillator");
    if (! dir.createDirectory()) { done (juce::var()); return; }
    const auto f = dir.getChildFile (safeFileName (r.name) + (centred ? "_ctr_" : "_abs_") + juce::String (r.frames) + ".wt");
    const auto data = terrain::writeWT (r, centred);
    done (f.replaceWithData (data.getData(), data.getSize()) ? juce::var (f.getFullPathName()) : juce::var());
}

void TerrainEditor::fnOpen (const Args&, Completion done)
{
    chooser = std::make_unique<juce::FileChooser> ("Open wavetable",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.wt");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, done = std::move (done)] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            juce::MemoryBlock mb;
            if (f == juce::File() || ! f.loadFileAsData (mb)) { done (juce::var()); return; }
            terrain::Relief r;
            juce::String err;
            if (! terrain::readWT (mb.getData(), mb.getSize(), r, err)) { done (juce::var ("error: " + err)); return; }
            r.name = f.getFileNameWithoutExtension();
            auto& reg = proc.regionInfo();
            reg.removeAllProperties (nullptr);
            reg.setProperty ("name", r.name, nullptr);
            reg.setProperty ("source", "file", nullptr);
            proc.setRelief (std::move (r));
            lastReliefVersion = proc.getReliefVersion();
            auto* o = new juce::DynamicObject();
            o->setProperty ("relief", reliefForUI());
            o->setProperty ("region", regionForUI());
            done (juce::var (o));
        });
}
