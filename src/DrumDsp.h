#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

//==============================================================================
//  DrumDsp.h
//
//  Shared building blocks for the kit's voices (snare, clap, hats, cymbals).
//
//  DELIBERATELY JUCE-FREE.  Every voice built on this header is plain C++, so
//  the same code can run inside Grex to render electronic kits straight into
//  kit samples — one copy of the DSP, two products.  Keep it that way: no
//  juce:: types in here or in any *Voice.h / *Prototypes.h.
//
//  The kick voice (KickVoice.h) predates this header and keeps its own copies
//  of the SVF and macro struct, unchanged from the approved audition.  The
//  processor converts drum::Macros to kick::Macros for the kick and tom slots.
//==============================================================================

namespace drum
{

//==============================================================================
//  The per-page control surface, read from the host parameters at note-on.
//  Same eight macros on every page; each voice interprets them for its drum.
//==============================================================================
struct Macros
{
    float tuneSemis  = 0.0f;   // -12 .. +12
    float decay      = 0.5f;   // 0..1, 0.5 = neutral
    float damp       = 0.5f;   // 0..1, 0.5 = neutral (meaning differs per voice)
    float snap       = 0.5f;   // 0..1, 0.5 = neutral (1x attack)
    float color      = 0.5f;   // 0..1, 0.5 = neutral
    float drive      = 0.15f;  // 0..1, ADDS to the prototype drive
    float humanize   = 0.25f;  // 0..1
    float velSens    = 0.6f;   // 0..1
};

//==============================================================================
inline float lerp (float a, float b, float t) noexcept   { return a + (b - a) * t; }

// Log-domain interpolation for frequencies and time constants.
inline float logLerp (float a, float b, float t) noexcept
{
    const float lo = 1.0e-5f;
    a = std::max (a, lo);
    b = std::max (b, lo);
    return a * std::pow (b / a, t);
}

// One-pole decay coefficient for an exponential with time constant tau.
inline float coefForTau (float tauSeconds, float sampleRate) noexcept
{
    return std::exp (-1.0f / (std::max (tauSeconds, 1.0e-5f) * sampleRate));
}

inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db / 20.0f); }

// Macro knob 0..1 centred at 0.5 -> -1..+1
inline float bipolarAmount (float knob01) noexcept
{
    return (std::clamp (knob01, 0.0f, 1.0f) - 0.5f) * 2.0f;
}

//==============================================================================
//  xorshift32 — allocation-free and real-time safe.  Each voice owns one and
//  the processor seeds them differently, so no two voices share a noise stream.
//==============================================================================
class Rng
{
public:
    void seed (uint32_t s) noexcept   { state = (s != 0u) ? s : 0x9E3779B9u; }

    inline float bipolar() noexcept                      // -1..1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (int32_t) state * 4.6566129e-10f;
    }

    inline float unipolar() noexcept                     // 0..1
    {
        return 0.5f * (bipolar() + 1.0f);
    }

private:
    uint32_t state = 0x9E3779B9u;
};

//==============================================================================
//  TPT state-variable filter (Zavalishin) — same maths as the kick's.
//==============================================================================
struct Svf
{
    void set (float fc, float q, float sampleRate) noexcept
    {
        fc = std::clamp (fc, 20.0f, sampleRate * 0.45f);
        const float g = std::tan (3.14159265f * fc / sampleRate);
        k  = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept   { ic1 = ic2 = 0.0f; }

    inline void process (float v0, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }

    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

//==============================================================================
//  PolyBLEP square — the metal voices stack several of these.  A naive square
//  folds its upper harmonics back below Nyquist, and the hat/cymbal filters sit
//  exactly where that fold-back lands, so the band-limiting is audible there.
//==============================================================================
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

struct BlepSquare
{
    void reset (float startPhase01) noexcept
    {
        phase = startPhase01 - std::floor (startPhase01);
    }

    void setFrequency (float hz, float sampleRate) noexcept
    {
        inc = std::clamp (hz / sampleRate, 0.0f, 0.45f);
    }

    inline float next() noexcept
    {
        float v = (phase < 0.5f) ? 1.0f : -1.0f;
        v += polyBlep (phase, inc);
        float shifted = phase + 0.5f;
        if (shifted >= 1.0f) shifted -= 1.0f;
        v -= polyBlep (shifted, inc);

        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        return v;
    }

    float phase = 0.0f, inc = 0.0f;
};

//==============================================================================
//  Velocity response shared by every new voice — the same curves the kick
//  uses, plus a brightness factor for the noise-based drums.
//==============================================================================
struct VelocityResponse
{
    float level  = 1.0f;
    float attack = 1.0f;   // click / stick emphasis
    float pitch  = 1.0f;   // pitch-envelope depth
    float bright = 1.0f;   // filter frequency multiplier
};

inline VelocityResponse velocityResponse (int velocity, float velSens) noexcept
{
    const float vel  = std::clamp ((float) velocity, 1.0f, 127.0f) / 127.0f;
    const float sens = std::clamp (velSens, 0.0f, 1.0f);

    VelocityResponse r;
    r.level  = lerp (1.0f, std::pow (vel, 1.2f), sens);
    r.attack = lerp (1.0f, 0.25f + vel * 1.5f, sens);
    r.pitch  = lerp (1.0f, 0.55f + vel * 0.8f, sens);
    r.bright = lerp (1.0f, 0.60f + vel * 0.8f, sens);
    return r;
}

//==============================================================================
//  Per-hit humanize: the four randoms every voice latches at note-on.
//==============================================================================
struct Humanize
{
    float tuneCents = 0.0f;
    float decayMul  = 1.0f;
    float attackMul = 1.0f;
    float levelDb   = 0.0f;
};

inline Humanize rollHumanize (Rng& rng, float amount01) noexcept
{
    const float h = std::clamp (amount01, 0.0f, 1.0f);
    Humanize r;
    r.tuneCents = rng.bipolar() * 25.0f * h;
    r.decayMul  = 1.0f + rng.bipolar() * 0.10f * h;
    r.attackMul = 1.0f + rng.bipolar() * 0.25f * h;
    r.levelDb   = rng.bipolar() * 1.2f  * h;
    return r;
}

// Samples a voice should live for, given its longest decay time constant.
// 7 tau is ~ -60 dB; the tail beyond that is inaudible.
inline int lifetimeSamples (float longestTau, float sampleRate, float maxSeconds) noexcept
{
    const float s = sampleRate * (longestTau * 7.0f + 0.03f);
    return (int) std::min (s, sampleRate * maxSeconds);
}

} // namespace drum
