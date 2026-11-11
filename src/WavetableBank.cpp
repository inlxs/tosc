// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WavetableBank.h"
#include <cmath>
#include <complex>
#include <cstring>

namespace terrain
{
namespace
{
    // In-place radix-2 FFT. inverse=true also divides by n.
    void fft (std::vector<std::complex<double>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * juce::MathConstants<double>::pi / (double) len * (inverse ? 1.0 : -1.0);
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse)
            for (auto& x : a) x /= (double) n;
    }
}

int WavetableBank::levelFor (double freqHz, double sampleRate) noexcept
{
    const double maxHarmonics = (sampleRate * 0.5) / juce::jmax (1.0, freqHz);
    const int lvl = (int) std::ceil (std::log2 (1024.0 / maxHarmonics));
    return juce::jlimit (0, kLevels - 1, lvl);
}

std::shared_ptr<const WavetableBank> WavetableBank::build (const Relief& relief, bool evenLoudness)
{
    if (! relief.isValid())
        return {};

    auto bank = std::make_shared<WavetableBank>();
    const int F = relief.frames;
    bank->numFrames = F;
    bank->data.assign ((size_t) kLevels * (size_t) F * kSamples, 0.0f);

    // Remove each frame's DC offset; scale globally (terrain loudness) or per frame (even).
    std::vector<float> centred ((size_t) F * kSamples);
    double globalMax = 1e-9;
    std::vector<double> frameMax ((size_t) F, 1e-9);
    for (int f = 0; f < F; ++f)
    {
        const float* src = relief.values.data() + (size_t) f * kSamples;
        double mean = 0.0;
        for (int j = 0; j < kSamples; ++j) mean += src[j];
        mean /= kSamples;
        for (int j = 0; j < kSamples; ++j)
        {
            const float x = (float) (src[j] - mean);
            centred[(size_t) f * kSamples + (size_t) j] = x;
            frameMax[(size_t) f] = juce::jmax (frameMax[(size_t) f], (double) std::abs (x));
        }
        globalMax = juce::jmax (globalMax, frameMax[(size_t) f]);
    }

    std::vector<std::complex<double>> spec (kSamples), work (kSamples);
    for (int f = 0; f < F; ++f)
    {
        const double scale = 1.0 / (evenLoudness ? frameMax[(size_t) f] : globalMax);
        for (int j = 0; j < kSamples; ++j)
            spec[(size_t) j] = { centred[(size_t) f * kSamples + (size_t) j] * scale, 0.0 };
        fft (spec, false);

        for (int L = 0; L < kLevels; ++L)
        {
            const int maxH = 1024 >> L;
            for (int b = 0; b < kSamples; ++b)
            {
                const int h = b <= kSamples / 2 ? b : kSamples - b;
                work[(size_t) b] = (b != 0 && h <= maxH) ? spec[(size_t) b] : std::complex<double> (0.0, 0.0);
            }
            fft (work, true);
            float* dst = bank->data.data() + ((size_t) L * (size_t) F + (size_t) f) * kSamples;
            for (int j = 0; j < kSamples; ++j) dst[j] = (float) work[(size_t) j].real();
        }
    }
    return bank;
}

bool readWT (const void* bytes, size_t size, Relief& out, juce::String& error)
{
    juce::MemoryInputStream in (bytes, size, false);
    char magic[4] = {};
    if (size < 12 || in.read (magic, 4) != 4 || std::memcmp (magic, "vawt", 4) != 0)
    {
        error = "not a .wt wavetable";
        return false;
    }
    const int n = in.readInt();
    const int F = (int) (juce::uint16) in.readShort();
    const int flags = (int) (juce::uint16) in.readShort();
    const bool int16 = (flags & 4) != 0;
    const size_t need = (size_t) n * (size_t) F * (int16 ? 2u : 4u);
    if (n <= 0 || F <= 0 || size < 12 + need)
    {
        error = "file is truncated or has an unsupported layout";
        return false;
    }

    std::vector<float> src ((size_t) n * (size_t) F);
    const double div = (flags & 8) ? 32768.0 : 16384.0;
    for (auto& x : src)
        x = int16 ? (float) (in.readShort() / div) : in.readFloat();

    out.frames = F;
    out.values.assign ((size_t) F * kSamples, 0.0f);
    for (int f = 0; f < F; ++f)
        for (int j = 0; j < kSamples; ++j)
        {
            const double x = (double) j / kSamples * n;
            const int x0 = (int) x, x1 = (x0 + 1) % n;
            const double k = x - x0;
            const double v = src[(size_t) f * (size_t) n + (size_t) x0] * (1.0 - k) + src[(size_t) f * (size_t) n + (size_t) x1] * k;
            out.values[(size_t) f * kSamples + (size_t) j] = (float) juce::jlimit (0.0, 1.0, (v + 1.0) * 0.5);
        }
    return true;
}

namespace
{
    std::vector<float> exportValues (const Relief& r, bool centred)
    {
    std::vector<float> v (r.values.size());
    if (! centred)
        for (size_t i = 0; i < v.size(); ++i) v[i] = r.values[i] * 2.0f - 1.0f;
    else
    {
        float mx = 1e-9f;
        for (int f = 0; f < r.frames; ++f)
        {
            const float* s = r.values.data() + (size_t) f * kSamples;
            double m = 0; for (int j = 0; j < kSamples; ++j) m += s[j];
            m /= kSamples;
            for (int j = 0; j < kSamples; ++j)
            {
                v[(size_t) f * kSamples + (size_t) j] = (float) (s[j] - m);
                mx = juce::jmax (mx, std::abs (v[(size_t) f * kSamples + (size_t) j]));
            }
        }
        for (auto& x : v) x /= mx;
    }
    for (auto& x : v) x = juce::jlimit (-1.0f, 1.0f, x) * 0.98f;
    return v;
    }
}

juce::MemoryBlock writeWT (const Relief& r, bool centred)
{
    juce::MemoryOutputStream out;
    out.write ("vawt", 4);
    out.writeInt (kSamples);
    out.writeShort ((short) r.frames);
    out.writeShort ((short) 0x0C);
    for (auto x : exportValues (r, centred))
        out.writeShort ((short) juce::roundToInt (x * 32767.0f));
    return out.getMemoryBlock();
}

juce::MemoryBlock writeWAV (const Relief& r, bool centred)
{
    const auto v = exportValues (r, centred);
    juce::MemoryOutputStream data;
    for (auto x : v) data.writeFloat (x);

    juce::MemoryOutputStream clm;
    clm << "<!>2048 01000000 wavetable (Terrain Oscillator)";
    while (clm.getDataSize() < 48) clm.writeByte (0);

    juce::MemoryOutputStream out;
    const int fmtSize = 16;
    const auto riffSize = 4 + (8 + fmtSize) + (8 + (int) clm.getDataSize()) + (8 + (int) data.getDataSize());
    out.write ("RIFF", 4); out.writeInt (riffSize); out.write ("WAVE", 4);
    out.write ("fmt ", 4); out.writeInt (fmtSize);
    out.writeShort (3); out.writeShort (1);                 // IEEE float, mono
    out.writeInt (44100); out.writeInt (44100 * 4);
    out.writeShort (4); out.writeShort (32);
    out.write ("clm ", 4); out.writeInt ((int) clm.getDataSize()); out.write (clm.getData(), clm.getDataSize());
    out.write ("data", 4); out.writeInt ((int) data.getDataSize()); out.write (data.getData(), data.getDataSize());
    return out.getMemoryBlock();
}

} // namespace terrain
