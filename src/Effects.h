// Tosc - stereo effects and output stage:
//   chorus -> ping-pong delay -> reverb (plate or hall) -> volume -> look-ahead limiter.
// Ported from the Okllo π0 (first-synth/fx.h), with every length scaled to the host sample rate.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Dsp.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace tosc
{
inline float sine01 (float t) noexcept { return std::sin (6.2831853f * t); }

// --------------------------------------------------------------- chorus
// Juno-style: one short modulated delay per side, the two sides swept in opposite
// directions by a triangle LFO, slightly darkened like a BBD.
class Chorus
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        int n = 1;
        while (n < (int) (0.012 * sampleRate) + 4) n <<= 1;
        bufL.assign ((size_t) n, 0.0f); bufR.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0;
        base = 0.0035f * sr;
        tone = 1.0f - std::exp (-6.2831853f * 9000.0f / sr);
        lpL = lpR = 0.0f;
    }
    void set (float mix01, float rateHz, float depthMs) noexcept
    {
        mix = 0.5f * mix01;               // 1 = 50/50, the classic sound
        inc = rateHz / sr;
        depth = depthMs * 0.001f * sr;
    }
    inline void process (float& l, float& r) noexcept
    {
        bufL[(size_t) w] = l; bufR[(size_t) w] = r;
        phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        const float tri = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
        const float wl = read (bufL, base + depth * tri), wr = read (bufR, base - depth * tri);
        lpL += (wl - lpL) * tone; lpR += (wr - lpR) * tone;
        w = (w + 1) & mask;
        if (mix > 0.0f)
        {
            l = l * (1.0f - mix) + lpL * mix;
            r = r * (1.0f - mix) + lpR * mix;
        }
    }

private:
    inline float read (const std::vector<float>& b, float delay) const noexcept
    {
        float rp = (float) w - delay;
        if (rp < 0.0f) rp += (float) (mask + 1);
        const int i0 = (int) rp & mask, i1 = (i0 + 1) & mask;
        const float fr = rp - std::floor (rp);
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
    }
    std::vector<float> bufL, bufR;
    int mask = 0, w = 0;
    float sr = 48000.0f, base = 168.0f, tone = 0.7f, phase = 0.0f, inc = 0.0f, depth = 0.0f, mix = 0.0f;
    float lpL = 0.0f, lpR = 0.0f;
};

// ---------------------------------------------------------------- delay
// Ping-pong feedback delay with damped repeats: the mono input feeds the left line and
// each repeat bounces to the other side. The read position glides when the time
// changes (tape-like pitch bend). Repeats saturate softly instead of clipping.
class PingPongDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        n = (int) (2.1 * sampleRate);
        bufL.assign ((size_t) n, 0.0f); bufR.assign ((size_t) n, 0.0f);
        w = 0; dampL = dampR = 0.0f;
        glide = 1.0f - std::exp (-1.0f / (0.04f * sr));
        len = target;
    }
    void set (float mix01, float seconds, float feedback01) noexcept
    {
        mix = mix01;
        target = clampf (seconds * sr, 1.0f, (float) n - 2.0f);
        feedback = feedback01;
    }
    void snap() noexcept { len = target; }
    inline void process (float& l, float& r) noexcept
    {
        len += (target - len) * glide;
        float rp = (float) w - len;
        if (rp < 0.0f) rp += (float) n;
        const int i0 = (int) rp, i1 = i0 + 1 == n ? 0 : i0 + 1;
        const float fr = rp - (float) i0;
        const float dl = bufL[(size_t) i0] + (bufL[(size_t) i1] - bufL[(size_t) i0]) * fr;
        const float dr = bufR[(size_t) i0] + (bufR[(size_t) i1] - bufR[(size_t) i0]) * fr;
        dampL += (dl - dampL) * 0.45f;
        dampR += (dr - dampR) * 0.45f;
        const float wl = 0.5f * (l + r) + dampR * feedback;
        const float wr = dampL * feedback;
        bufL[(size_t) w] = 4.0f * softClip (wl * 0.25f);
        bufR[(size_t) w] = 4.0f * softClip (wr * 0.25f);
        if (++w == n) w = 0;
        if (mix > 0.0f) { l += dl * mix; r += dr * mix; }
    }

private:
    std::vector<float> bufL, bufR;
    int n = 1, w = 0;
    float sr = 48000.0f, len = 1000.0f, target = 1000.0f, glide = 0.0005f;
    float feedback = 0.0f, mix = 0.0f, dampL = 0.0f, dampR = 0.0f;
};

// --------------------------------------------------------------- reverb
// Delay line on a slice of a reverb's memory. read(d) is the sample written d writes ago.
struct ReverbLine
{
    float* buf = nullptr;
    int size = 0, pos = 0;
    void init (float* mem, int n) noexcept { buf = mem; size = n; pos = 0; }
    inline float read (int d) const noexcept { int i = pos - d; if (i < 0) i += size; return buf[i]; }
    inline float readFrac (float d) const noexcept
    {
        const int di = (int) d;
        const float fr = d - (float) di, a = read (di), b = read (di + 1);
        return a + (b - a) * fr;
    }
    inline void write (float x) noexcept { buf[pos] = x; if (++pos == size) pos = 0; }
};

inline float allpass (ReverbLine& line, int len, float x, float g) noexcept
{
    const float d = line.read (len);
    const float w = x - g * d;
    line.write (w);
    return d + g * w;
}

// Dattorro plate ("Effect Design Part 1", 1997), delays scaled from the paper's 29.761 kHz.
class PlateReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        auto S = [&] (int n29k) { return (int) std::lround (n29k * sampleRate / 29761.0); };
        for (int i = 0; i < kTaps; ++i) L[i] = S (kPaper[i]);
        exc = std::max (2, S (16));
        preMax = (int) (0.1 * sampleRate) + 2;
        int total = preMax;
        for (int i = 0; i < 12; ++i) total += lineSize (i);
        mem.assign ((size_t) total, 0.0f);
        float* p = mem.data();
        pre.init (p, preMax); p += preMax;
        for (int i = 0; i < 12; ++i) { lines[i].init (p, lineSize (i)); p += lineSize (i); }
        loopSeconds = (float) (L[iApL1] + L[iDL1] + L[iApL2] + L[iDL2] + L[iApR1] + L[iDR1] + L[iApR2] + L[iDR2]) / sr;
        modInc = 0.8f / sr;
        bwCoef = 1.0f - std::exp (-6.2831853f * 15000.0f / sr);
        clear();
    }
    void clear() noexcept
    {
        std::fill (mem.begin(), mem.end(), 0.0f);
        bw = dampL = dampR = endL = endR = 0.0f;
    }
    // RT60 in seconds. The signal passes four decay multipliers per trip around the tank.
    void setDecay (float rt60) noexcept
    {
        decay = std::pow (10.0f, -3.0f * loopSeconds / (4.0f * rt60));
        decayDiff2 = clampf (decay + 0.15f, 0.25f, 0.5f);
    }
    void setTone (float bright01) noexcept { damping = 0.0005f + (1.0f - bright01) * 0.7f; }
    void setPreDelay (float seconds) noexcept { preDelay = (int) clampf (seconds * sr, 1.0f, (float) preMax - 1.0f); }

    inline void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        float x = pre.read (preDelay);
        pre.write (0.5f * (inL + inR));
        bw += (x - bw) * bwCoef;
        x = allpass (lines[0], L[0], bw, 0.75f);
        x = allpass (lines[1], L[1], x, 0.75f);
        x = allpass (lines[2], L[2], x, 0.625f);
        x = allpass (lines[3], L[3], x, 0.625f);

        // Slow modulation of the first tank allpasses keeps the tail from ringing.
        modPhase += modInc; if (modPhase >= 1.0f) modPhase -= 1.0f;
        const float modL = (float) exc * 0.5f * (1.0f + sine01 (modPhase));
        const float modR = (float) exc * 0.5f * (1.0f + sine01 (modPhase + 0.25f));

        float a = x + endR * decay;
        a = allpassMod (lines[iApL1], (float) L[iApL1] + modL, a, -0.7f);
        float t = lines[iDL1].read (L[iDL1]);
        lines[iDL1].write (a);
        dampL += (t - dampL) * (1.0f - damping);
        a = allpass (lines[iApL2], L[iApL2], dampL * decay, decayDiff2);
        const float eL = lines[iDL2].read (L[iDL2]);
        lines[iDL2].write (a);

        float b = x + endL * decay;
        b = allpassMod (lines[iApR1], (float) L[iApR1] + modR, b, -0.7f);
        t = lines[iDR1].read (L[iDR1]);
        lines[iDR1].write (b);
        dampR += (t - dampR) * (1.0f - damping);
        b = allpass (lines[iApR2], L[iApR2], dampR * decay, decayDiff2);
        const float eR = lines[iDR2].read (L[iDR2]);
        lines[iDR2].write (b);
        endL = eL; endR = eR;

        const auto& dL1 = lines[iDL1]; const auto& apL2 = lines[iApL2]; const auto& dL2 = lines[iDL2];
        const auto& dR1 = lines[iDR1]; const auto& apR2 = lines[iApR2]; const auto& dR2 = lines[iDR2];
        outL = 0.6f * (dR1.read (L[12]) + dR1.read (L[13]) - apR2.read (L[14]) + dR2.read (L[15])
                     - dL1.read (L[16]) - apL2.read (L[17]) - dL2.read (L[18]));
        outR = 0.6f * (dL1.read (L[19]) + dL1.read (L[20]) - apL2.read (L[21]) + dL2.read (L[22])
                     - dR1.read (L[23]) - apR2.read (L[24]) - dR2.read (L[25]));
    }

private:
    enum { iApL1 = 4, iDL1, iApL2, iDL2, iApR1, iDR1, iApR2, iDR2 };
    static constexpr int kTaps = 26;
    // 4 input diffusers, the 8 tank lines, then the 14 output taps (paper's sample counts).
    static constexpr int kPaper[kTaps] = { 142, 107, 379, 277,  672, 4453, 1800, 3720, 908, 4217, 2656, 3163,
                                           266, 2974, 1913, 1996, 1990, 187, 1066,  353, 3627, 1228, 2673, 2111, 335, 121 };
    int lineSize (int i) const noexcept { return L[i] + ((i == iApL1 || i == iApR1) ? exc + 2 : 1); }
    static inline float allpassMod (ReverbLine& line, float len, float x, float g) noexcept
    {
        const float d = line.readFrac (len);
        const float w = x - g * d;
        line.write (w);
        return d + g * w;
    }

    std::vector<float> mem;
    ReverbLine pre, lines[12];
    int L[kTaps] {}, exc = 26, preMax = 4802, preDelay = 1;
    float sr = 48000.0f, loopSeconds = 0.5f, modInc = 0.0f, modPhase = 0.0f, bwCoef = 0.9f;
    float decay = 0.5f, decayDiff2 = 0.5f, damping = 0.3f;
    float bw = 0, dampL = 0, dampR = 0, endL = 0, endR = 0;
};

// A larger, smoother room: early reflections, then a feedback delay network of 8 lines
// (43-97 ms) mixed by a Hadamard matrix, four of them slowly modulated so the tail does
// not ring. Each line loses exactly enough per trip for the decay time and damps the
// highs on the way, like a real hall.
class HallReverb
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const double k = sampleRate / 48000.0;
        auto sc = [&] (int n48k) { return std::max (1, (int) std::lround (n48k * k)); };
        for (int i = 0; i < kLines; ++i) len[i] = sc (kLen48[i]);
        for (int j = 0; j < 8; ++j) { erL[j] = sc (kErL48[j]); erR[j] = sc (kErR48[j]); }
        for (int j = 0; j < 4; ++j) inLen[j] = sc (kIn48[j]);
        mod = std::max (4, sc (24));
        preMax = (int) (0.1 * sampleRate) + 2;
        erMax = (int) (0.08 * sampleRate);
        int total = preMax + erMax + inLen[0] + inLen[1] + inLen[2] + inLen[3] + 1 * 4;
        for (int i = 0; i < kLines; ++i) total += len[i] + mod + 2;
        mem.assign ((size_t) total, 0.0f);
        float* p = mem.data();
        auto take = [&p] (ReverbLine& line, int n) { line.init (p, n); p += n; };
        take (pre, preMax + erMax);
        for (int j = 0; j < 4; ++j) take (in[j], inLen[j] + 1);
        for (int i = 0; i < kLines; ++i) take (line[i], len[i] + mod + 2);
        bwCoef = 1.0f - std::exp (-6.2831853f * 11000.0f / sr);
        clear();
    }
    void clear() noexcept
    {
        std::fill (mem.begin(), mem.end(), 0.0f);
        bw = 0.0f;
        for (auto& d : damp) d = 0.0f;
    }
    void setDecay (float rt60) noexcept
    {
        for (int i = 0; i < kLines; ++i) gain[i] = std::pow (10.0f, -3.0f * (float) len[i] / (sr * rt60));
    }
    void setTone (float bright01) noexcept { toneCoef = 1.0f - (0.0005f + (1.0f - bright01) * 0.7f); }
    void setPreDelay (float seconds) noexcept { preDelay = (int) clampf (seconds * sr, 1.0f, (float) preMax - 1.0f); }

    inline void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        float x = pre.read (preDelay);
        float eL = 0.0f, eR = 0.0f;
        for (int j = 0; j < 8; ++j) { eL += pre.read (erL[j]) * kErGain[j]; eR += pre.read (erR[j]) * kErGain[j]; }
        pre.write (0.5f * (inL + inR));
        bw += (x - bw) * bwCoef;
        x = allpass (in[0], inLen[0], bw, 0.7f);
        x = allpass (in[1], inLen[1], x, 0.7f);
        x = allpass (in[2], inLen[2], x, 0.6f);
        x = allpass (in[3], inLen[3], x, 0.6f);

        modA += 0.5f / sr;  if (modA >= 1.0f) modA -= 1.0f;
        modB += 0.73f / sr; if (modB >= 1.0f) modB -= 1.0f;
        const float half = 0.5f * (float) mod;
        const float m[4] = { half * (1.0f + sine01 (modA)), half * (1.0f + sine01 (modA + 0.25f)),
                             half * (1.0f + sine01 (modB)), half * (1.0f + sine01 (modB + 0.25f)) };
        float y[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            const float d = i < 4 ? line[i].readFrac ((float) len[i] + m[i]) : line[i].read (len[i] + (int) half);
            damp[i] += (d - damp[i]) * toneCoef;
            y[i] = damp[i] * gain[i];
        }
        const float wl = y[0] - y[2] + y[4] - y[6];
        const float wr = y[1] - y[3] + y[5] - y[7];

        for (int h = 1; h < kLines; h <<= 1)               // fast Walsh-Hadamard
            for (int i = 0; i < kLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = y[j], b = y[j + h];
                    y[j] = a + b; y[j + h] = a - b;
                }
        for (int i = 0; i < kLines; ++i) line[i].write (y[i] * 0.35355339f + ((i & 1) ? -x : x) * 0.5f);

        outL = wl * 1.2f + eL;
        outR = wr * 1.2f + eR;
    }

private:
    static constexpr int kLen48[kLines] = { 2053, 2437, 2777, 3109, 3491, 3851, 4219, 4637 };
    static constexpr int kErL48[8] = { 206, 466, 725, 1099, 1502, 2098, 2741, 3451 };
    static constexpr int kErR48[8] = { 283, 542, 898, 1262, 1723, 2270, 2962, 3710 };
    static constexpr int kIn48[4] = { 229, 173, 613, 449 };
    static constexpr float kErGain[8] = { 0.22f, 0.19f, 0.17f, 0.15f, 0.12f, 0.1f, 0.08f, 0.06f };

    std::vector<float> mem;
    ReverbLine pre, in[4], line[kLines];
    int len[kLines] {}, erL[8] {}, erR[8] {}, inLen[4] {}, mod = 24, preMax = 4802, erMax = 3840, preDelay = 1;
    float gain[kLines] {}, damp[kLines] {};
    float sr = 48000.0f, toneCoef = 0.7f, bw = 0.0f, bwCoef = 0.85f, modA = 0.0f, modB = 0.25f;
};

// The reverb slot: plate or hall, wet/dry mixed.
class Reverb
{
public:
    void prepare (double sampleRate) { plate.prepare (sampleRate); hall.prepare (sampleRate); }
    void clear() noexcept { plate.clear(); hall.clear(); }
    // type 0 = plate, 1 = hall; size 0..1 -> RT60 0.3 .. 20 s
    void set (int newType, float mix01, float size01, float bright01, float preDelaySeconds) noexcept
    {
        if (newType != type) { type = newType; clear(); }   // the other algorithm's tail is noise to this one
        mix = mix01;
        const float rt60 = 0.3f * std::pow (66.7f, size01);
        if (rt60 != lastRt) { lastRt = rt60; plate.setDecay (rt60); hall.setDecay (rt60); }
        plate.setTone (bright01); hall.setTone (bright01);
        plate.setPreDelay (preDelaySeconds); hall.setPreDelay (preDelaySeconds);
    }
    inline void process (float& l, float& r) noexcept
    {
        if (mix <= 0.0f) return;
        float wl, wr;
        if (type == 0) plate.process (l, r, wl, wr);
        else           hall.process (l, r, wl, wr);
        // equal-ish power crossfade, dry stays near full up to the middle
        const float dry = 1.0f - mix * mix * 0.6f;
        l = l * dry + wl * mix;
        r = r * dry + wr * mix;
    }

private:
    PlateReverb plate;
    HallReverb hall;
    int type = 1;
    float mix = 0.0f, lastRt = -1.0f;
};

// ------------------------------------------------------------- limiter
// Stereo-linked look-ahead peak limiter. The detector sees each sample 1.3 ms before it
// plays and the gain glides down over that time, so peaks are turned down smoothly
// instead of the waveform being sliced. The detector holds the highest peak of the last
// 33-67 ms and releases over 150 ms, so bass notes do not make the gain wobble.
class Limiter
{
public:
    void prepare (double sampleRate)
    {
        look = 1;
        while (look < (int) (0.0013 * sampleRate)) look <<= 1;
        bufL.assign ((size_t) look, 0.0f); bufR.assign ((size_t) look, 0.0f);
        pos = 0;
        attack = 1.0f - std::exp (-5.0f / (float) look);
        segment = std::max (1, (int) (sampleRate / 30.0));
        release = std::exp (-1.0f / (0.15f * (float) sampleRate));
        segPos = 0; segMax = prevSegMax = env = 0.0f; gain = 1.0f;
    }
    inline void process (float& l, float& r) noexcept
    {
        const float a = std::fmax (std::abs (l), std::abs (r));
        segMax = std::fmax (segMax, a);
        if (++segPos == segment) { segPos = 0; prevSegMax = segMax; segMax = 0.0f; }
        env = std::fmax (std::fmax (prevSegMax, segMax), env * release);
        const float target = env > kCeiling ? kCeiling / env : 1.0f;
        gain = target < gain ? gain + (target - gain) * attack : target;
        bufL[(size_t) pos] = l; bufR[(size_t) pos] = r;
        pos = (pos + 1) & (look - 1);
        l = clampf (bufL[(size_t) pos] * gain, -0.999f, 0.999f);
        r = clampf (bufR[(size_t) pos] * gain, -0.999f, 0.999f);
        if (gain < minGain) minGain = gain;
    }
    int latency() const noexcept { return look - 1; }
    float takeMinGain() noexcept { const float g = minGain; minGain = 1.0f; return g; }

private:
    static constexpr float kCeiling = 0.89f * 0.985f;    // -1 dBFS, with room for the smooth attack
    std::vector<float> bufL, bufR;
    int look = 64, pos = 0, segment = 1600, segPos = 0;
    float attack = 0.07f, release = 0.9999f, segMax = 0, prevSegMax = 0, env = 0, gain = 1.0f, minGain = 1.0f;
};

} // namespace tosc
