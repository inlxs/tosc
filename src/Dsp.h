// Tosc - DSP building blocks for the voices: envelopes, drive, filters.
// Ported from the Okllo π0 voice engine (first-synth/synth.h), made sample-rate independent.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>

namespace tosc
{
inline float clampf (float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }

// Smooth saturation, ~linear for |x| < 0.5, limits to +-1.
inline float softClip (float x) noexcept
{
    x = clampf (x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// Cubic soft limiter: linear-ish to ~0.5, flat at +-1 from 1.5 on.
inline float cubicClip (float x) noexcept
{
    x = clampf (x, -1.5f, 1.5f);
    return x - 0.14814815f * x * x * x;
}

// Its antiderivative, for AdaaClip.
inline float cubicClipInt (float x) noexcept
{
    const float a = std::abs (x);
    if (a >= 1.5f) return a - 0.5625f;
    const float x2 = x * x;
    return x2 * (0.5f - x2 * (1.0f / 27.0f));
}

// Drive with first-order antiderivative anti-aliasing: each output is the clip curve
// averaged over the step from the previous input, which removes most of the aliasing
// hard drive causes on high notes.
struct AdaaClip
{
    float xp = 0.0f, Fp = 0.0f;
    void clear() noexcept { xp = Fp = 0.0f; }
    inline float operator() (float x) noexcept
    {
        const float F = cubicClipInt (x), d = x - xp;
        const float y = std::abs (d) > 1.0e-3f ? (F - Fp) / d : cubicClip (0.5f * (x + xp));
        xp = x;
        Fp = F;
        return y;
    }
};

// xorshift32, uniform 0..1.
struct Rng
{
    uint32_t s = 0x12345678u;
    Rng() = default;
    explicit Rng (uint32_t seed) : s (seed ? seed : 1u) {}
    inline float next01() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s >> 8) * (1.0f / 16777216.0f);
    }
    inline float nextBipolar() noexcept { return next01() * 2.0f - 1.0f; }
};

// One-pole coefficient that settles (to ~1%) in `seconds` at `rate` Hz.
inline float settleCoef (float seconds, float rate) noexcept
{
    return 1.0f - std::exp (-4.6f / std::fmax (1.0e-6f, seconds * rate));
}

inline float noteToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }
inline float hzToNote (float hz) noexcept   { return 69.0f + 12.0f * std::log2 (std::fmax (1.0f, hz) / 440.0f); }

// ----------------------------------------------------------------- envelope
// Analog-style: the attack is an RC charge aimed past the top (so it is curved but
// still reaches 1 in the set time), decay and release are exponential.
struct EnvParams
{
    float attack = 1.0f, decay = 1.0f, sustain = 1.0f, release = 1.0f;

    void set (float a, float d, float s, float r, float rate) noexcept
    {
        attack  = 1.0f - std::exp (-1.466f / std::fmax (1.0e-6f, a * rate));   // aims at 1.3, stops at 1.0
        decay   = settleCoef (d, rate);
        sustain = clampf (s, 0.0f, 1.0f);
        release = settleCoef (r, rate);
    }
};

struct Env
{
    enum Stage : uint8_t { Off, Attack, Decay, Release };
    Stage stage = Off;
    float level = 0.0f;

    inline float tick (const EnvParams& p) noexcept
    {
        switch (stage)
        {
            case Attack:
                level += (1.3f - level) * p.attack;
                if (level >= 1.0f) { level = 1.0f; stage = Decay; }
                break;
            case Decay:   // also the sustain phase: keeps tracking the sustain level
                level += (p.sustain - level) * p.decay;
                break;
            case Release:
                level -= level * p.release;
                if (level < 0.0003f) { level = 0.0f; stage = Off; }
                break;
            default: break;
        }
        return level;
    }
};

// ------------------------------------------------------------------- filter
enum FilterModel : int { Ladder = 0, Analog = 1 };   // matches the "filterModel" choice order
enum FilterMode  : int { LowPass = 0, BandPass = 1, HighPass = 2 };

// Coefficients for one control tick, shared by both channels of a voice.
struct FilterCoefs
{
    float a1 = 0, a2 = 0, a3 = 0, k = 2.0f, resComp = 1.0f;   // Analog SVF
    float G = 0, fbNorm = 1.0f, inGain = 1.0f;                // Ladder

    // fc in Hz (already clamped below Nyquist), res 0..1.
    void set (int model, float fc, float sampleRate, float res) noexcept
    {
        const float g = std::tan (3.14159265f * fc / sampleRate);
        if (model == Analog)
        {
            // Q 0.5 .. 25, pushed just below zero damping at the top so it self-oscillates;
            // the saturating band-pass state keeps it bounded.
            const float r3 = res * res * res;
            k = 2.0f * std::pow (0.02f, res) - 0.07f * r3 * r3;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
            resComp = 0.5f + 0.25f * std::fmax (k, 0.0f);    // tame the level boost at high resonance
        }
        else
        {
            // Feedback 0..4.3: self-oscillation starts at 4 (res ~0.93). Input gain makes up
            // some of the bass the feedback takes away.
            k = 4.3f * res;
            G = g / (1.0f + g);
            const float g2 = G * G;
            fbNorm = 1.0f / (1.0f + k * g2 * g2);
            inGain = 1.0f + 0.5f * k;
        }
    }
};

// One channel of a voice filter, drive included.
struct VoiceFilter
{
    float ic1 = 0, ic2 = 0, ic3 = 0, ic4 = 0;
    AdaaClip clip;

    void clear() noexcept { ic1 = ic2 = ic3 = ic4 = 0.0f; clip.clear(); }

    // Nonlinear state-variable filter (12 dB): its band-pass state saturates, which
    // compresses the resonance on loud notes like a circuit.
    inline float analog (float s, const FilterCoefs& c, float drive, int mode) noexcept
    {
        s = clip (s * drive);
        s *= c.resComp;
        const float v3 = s - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * cubicClip ((2.0f * v1 - ic1) * 0.5f);
        ic2 = 2.0f * v2 - ic2;
        return mode == LowPass ? v2 : (mode == BandPass ? v1 : s - c.k * v1 - v2);
    }

    // 4-pole transistor ladder (24 dB), zero-delay feedback solved for the linear case,
    // with a saturator where the input meets the feedback.
    inline float ladder (float s, const FilterCoefs& c, float drive, int mode) noexcept
    {
        s = clip (s * drive);
        const float G = c.G, G2 = G * G, G3 = G2 * G;
        const float fb = (1.0f - G) * (G3 * ic1 + G2 * ic2 + G * ic3 + ic4);
        float u = (s * c.inGain - c.k * fb) * c.fbNorm;
        u = 2.0f * softClip (u * 0.5f);
        float t = (u - ic1) * G;  const float y1 = t + ic1; ic1 = y1 + t;
        t = (y1 - ic2) * G;       const float y2 = t + ic2; ic2 = y2 + t;
        t = (y2 - ic3) * G;       const float y3 = t + ic3; ic3 = y3 + t;
        t = (y3 - ic4) * G;       const float y4 = t + ic4; ic4 = y4 + t;
        if (mode == LowPass) return y4;
        if (mode == BandPass) return 4.0f * (y2 - 2.0f * y3 + y4);
        return u - 4.0f * y1 + 6.0f * y2 - 4.0f * y3 + y4;
    }
};

} // namespace tosc
