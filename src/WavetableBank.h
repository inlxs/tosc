// Terrain Oscillator - band-limited wavetable bank built from a terrain "relief".
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace terrain
{
constexpr int kSamples = 2048;   // samples per frame (Serum/Bitwig standard)
constexpr int kLevels  = 11;     // mip levels: 1024, 512, ... 1 harmonics

// Terrain shape as drawn: frames x kSamples values in 0..1 (0 = sea / lowest, 1 = highest).
struct Relief
{
    int frames = 0;
    std::vector<float> values;
    juce::String name;

    bool isValid() const { return frames > 0 && values.size() == (size_t) frames * kSamples; }
};

// Immutable once built; shared between the message thread and the audio thread.
class WavetableBank
{
public:
    static std::shared_ptr<const WavetableBank> build (const Relief& relief, bool evenLoudness);

    int frames() const noexcept { return numFrames; }

    // Harmonic-limited level for a note frequency, so high notes do not alias.
    static int levelFor (double freqHz, double sampleRate) noexcept;

    // pos 0..1 across frames, phase 0..1 across the cycle; linear interpolation on both axes.
    inline float read (int level, float pos, double phase) const noexcept
    {
        const float fp = pos * (float) (numFrames - 1);
        const int f0 = (int) fp;
        const int f1 = f0 + 1 < numFrames ? f0 + 1 : f0;
        const float fr = fp - (float) f0;

        const double x = phase * kSamples;
        const int j0 = ((int) x) & (kSamples - 1);
        const int j1 = (j0 + 1) & (kSamples - 1);
        const float jf = (float) (x - (double) (int) x);

        const float* a = data.data() + ((size_t) level * (size_t) numFrames + (size_t) f0) * kSamples;
        const float* b = data.data() + ((size_t) level * (size_t) numFrames + (size_t) f1) * kSamples;
        const float sa = a[j0] + (a[j1] - a[j0]) * jf;
        const float sb = b[j0] + (b[j1] - b[j0]) * jf;
        return sa + (sb - sa) * fr;
    }

private:
    int numFrames = 0;
    std::vector<float> data;   // kLevels * numFrames * kSamples
};

// .wt (Surge / Bitwig "vawt") import and export
bool readWT (const void* bytes, size_t size, Relief& out, juce::String& error);
juce::MemoryBlock writeWT (const Relief& relief, bool centred);
// Serum / Vital / Ableton-style WAV: 32-bit float mono, 2048 samples per frame, 'clm ' marker chunk
juce::MemoryBlock writeWAV (const Relief& relief, bool centred);

} // namespace terrain
