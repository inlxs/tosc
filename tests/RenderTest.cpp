// Offline render test: drives TerrainProcessor without a host and writes a WAV.
// Usage: RenderTest <out.wav> [scan 0..3 | -1 = keep] [preset index]
#include <juce_audio_formats/juce_audio_formats.h>
#include "../src/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("render.wav").getFullPathName());

    TerrainProcessor proc;
    if (argc > 3) proc.loadPreset (juce::String (argv[3]).getIntValue());
    if (argc > 2 && juce::String (argv[2]).getIntValue() >= 0)
        if (auto* p = proc.apvts.getParameter ("scan")) p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::String (argv[2]).getIntValue()));

    const double sr = 48000.0;
    const int block = 256, seconds = 10;
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buf (2, block), all (2, (int) sr * seconds);
    all.clear();
    float peak = 0.0f;
    bool bad = false;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();

    for (int s = 0; s < all.getNumSamples(); s += block)
    {
        juce::MidiBuffer midi;
        const double t = s / sr;
        auto at = [&] (double sec) { return t <= sec && sec < t + block / sr; };
        if (at (0.0))  { midi.addEvent (juce::MidiMessage::noteOn (1, 45, 0.8f), 0); midi.addEvent (juce::MidiMessage::noteOn (1, 52, 0.7f), 0); }
        if (at (3.0))  midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 10);
        if (at (4.0))  midi.addEvent (juce::MidiMessage::noteOn (1, 100, 0.6f), 10);     // high note: band-limiting
        if (at (5.0))  midi.addEvent (juce::MidiMessage::noteOff (1, 100), 0);
        if (at (6.0))  { midi.addEvent (juce::MidiMessage::noteOff (1, 45), 0); midi.addEvent (juce::MidiMessage::noteOff (1, 52), 0); midi.addEvent (juce::MidiMessage::noteOff (1, 64), 0); }

        proc.processBlock (buf, midi);
        const int n = juce::jmin (block, all.getNumSamples() - s);
        for (int ch = 0; ch < 2; ++ch)
        {
            all.copyFrom (ch, s, buf, ch, 0, n);
            for (int i = 0; i < n; ++i)
            {
                const float x = buf.getSample (ch, i);
                if (! std::isfinite (x)) bad = true;
                peak = juce::jmax (peak, std::abs (x));
            }
        }
    }
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    // state round trip
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    TerrainProcessor proc2;
    proc2.setStateInformation (state.getData(), (int) state.getSize());
    const auto r1 = proc.getRelief(), r2 = proc2.getRelief();
    double maxDiff = 0;
    for (size_t i = 0; i < r1.values.size() && i < r2.values.size(); ++i) maxDiff = juce::jmax (maxDiff, (double) std::abs (r1.values[i] - r2.values[i]));

    out.deleteFile();
    juce::WavAudioFormat wav;
    if (auto stream = out.createOutputStream())
        if (auto* w = wav.createWriterFor (stream.release(), sr, 2, 24, {}, 0))
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (w);
            writer->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
        }

    std::printf ("table: %s, %d frames, sound: %s\n", r1.name.toRawUTF8(), r1.frames, proc.getPresetName().toRawUTF8());
    std::printf ("rendered %d s in %.0f ms (%.0fx realtime), peak %.3f, finite %s\n", seconds, ms, seconds * 1000.0 / ms, peak, bad ? "NO" : "yes");
    std::printf ("state: %d bytes, relief round-trip max error %.6f, frames %d -> %d\n", (int) state.getSize(), maxDiff, r1.frames, r2.frames);
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    return bad || peak <= 0.001f || r2.frames != r1.frames ? 1 : 0;
}
