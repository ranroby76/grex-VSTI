#pragma once

#include <array>
#include <algorithm>
#include "DrumDsp.h"

//==============================================================================
//  MetalPrototypes.h  —  tuned metal percussion for the EDM kits.
//
//  Up to four partials (sine <-> band-limited square), a bandpass body, a
//  two-stage envelope (fast strike drop + slow ring), a stick click, a noise
//  shimmer, and optional STRIKE SEQUENCES: several hits in a row, each one
//  retuned by strikeStep semitones - a bell tree's descending run, a jingle
//  bell's rattle.  One topology, so any two morph cleanly.
//==============================================================================

namespace metal
{

static constexpr int kNumPartials = 4;

struct MetalPrototype
{
    const char* name;

    float f0;                        // Hz, partial 1
    float ratio[kNumPartials];       // partial ratios
    float gain [kNumPartials];       // partial levels (0 = unused)
    float square;                    // 0 = sines (bells) .. 1 = squares (808)

    float bpFreq, bpQ;               // body bandpass
    float bpMix;                     // 0 = raw partials, 1 = fully through the body

    float fastTau;                   // s, the strike's quick drop
    float slowTau;                   // s, the ring
    float fastAmount;                // 0..1 share of the fast stage

    float clickLevel, clickFreq;     // stick
    float noiseLevel, noiseFreq;     // shimmer

    float strikes;                   // 1 = one hit; more = a sequence
    float strikeSpacing;             // s between strikes
    float strikeStep;                // semitones per strike (negative = descending)
    float strikeDecay;               // level multiplier per strike

    float drive, level;
};

inline const std::array<MetalPrototype, 8>& prototypes()
{
    static const std::array<MetalPrototype, 8> bank =
    { {
        // 808 Cowbell: two squares at 540/800 Hz through a bandpass, two-stage decay
        { "808 Cowbell", 540.0f, { 1.0f, 1.4815f, 1.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 0.0f }, 1.0f,
          1800.0f, 1.2f, 1.0f,   0.012f, 0.180f, 0.60f,   0.20f, 3000.0f,   0.00f, 6000.0f,
          1.0f, 0.0f, 0.0f, 1.0f,   0.15f, 0.48f },

        // Cowbell: inharmonic, honky, stick-forward
        { "Cowbell", 560.0f, { 1.0f, 1.51f, 2.41f, 3.41f }, { 1.0f, 0.70f, 0.50f, 0.30f }, 0.30f,
          2000.0f, 0.9f, 0.5f,   0.020f, 0.180f, 0.45f,   0.60f, 4000.0f,   0.05f, 5000.0f,
          1.0f, 0.0f, 0.0f, 1.0f,   0.10f, 0.42f },

        // Agogo High / Low: two-tone bell, long clean ring
        { "Agogo High", 900.0f, { 1.0f, 2.35f, 3.80f, 5.20f }, { 1.0f, 0.45f, 0.25f, 0.15f }, 0.10f,
          2500.0f, 0.7f, 0.3f,   0.010f, 0.270f, 0.30f,   0.50f, 5000.0f,   0.00f, 6000.0f,
          1.0f, 0.0f, 0.0f, 1.0f,   0.05f, 0.37f },
        { "Agogo Low",  640.0f, { 1.0f, 2.35f, 3.80f, 5.20f }, { 1.0f, 0.45f, 0.25f, 0.15f }, 0.10f,
          2000.0f, 0.7f, 0.3f,   0.010f, 0.300f, 0.30f,   0.50f, 4500.0f,   0.00f, 6000.0f,
          1.0f, 0.0f, 0.0f, 1.0f,   0.05f, 0.38f },

        // Triangle Mute / Open: bar modes up high, a touch of air
        { "Triangle Mute", 1400.0f, { 1.0f, 2.76f, 5.40f, 8.93f }, { 0.6f, 1.0f, 0.8f, 0.5f }, 0.0f,
          6000.0f, 0.7f, 0.5f,   0.020f, 0.080f, 0.50f,   0.30f, 8000.0f,   0.10f, 9000.0f,
          1.0f, 0.0f, 0.0f, 1.0f,   0.00f, 0.33f },
        { "Triangle Open", 1400.0f, { 1.0f, 2.76f, 5.40f, 8.93f }, { 0.6f, 1.0f, 0.8f, 0.5f }, 0.0f,
          6000.0f, 0.7f, 0.5f,   0.020f, 0.700f, 0.20f,   0.30f, 8000.0f,   0.10f, 9000.0f,
          1.0f, 0.0f, 0.0f, 1.0f,   0.00f, 0.30f },

        // Jingle Bell: small bells rattling - five quick strikes with shimmer
        { "Jingle Bell", 2600.0f, { 1.0f, 1.47f, 2.13f, 2.86f }, { 1.0f, 0.8f, 0.6f, 0.4f }, 0.20f,
          6500.0f, 0.8f, 0.6f,   0.030f, 0.250f, 0.50f,   0.20f, 7000.0f,   0.50f, 7000.0f,
          5.0f, 0.018f, 0.0f, 0.70f,   0.05f, 0.33f },

        // Bell Tree: fourteen descending bell pings
        { "Bell Tree", 2300.0f, { 1.0f, 2.40f, 3.90f, 5.10f }, { 1.0f, 0.4f, 0.2f, 0.1f }, 0.0f,
          5000.0f, 0.7f, 0.0f,   0.010f, 0.250f, 0.30f,   0.20f, 6000.0f,   0.05f, 8000.0f,
          14.0f, 0.070f, -0.9f, 0.94f,   0.00f, 0.30f },
    } };

    return bank;
}

inline MetalPrototype morph (const MetalPrototype& a, const MetalPrototype& b, float t)
{
    using drum::lerp;
    using drum::logLerp;
    t = std::clamp (t, 0.0f, 1.0f);

    MetalPrototype r = a;
    r.f0 = logLerp (a.f0, b.f0, t);
    for (int k = 0; k < kNumPartials; ++k)
    {
        r.ratio[k] = logLerp (a.ratio[k], b.ratio[k], t);
        r.gain [k] = lerp    (a.gain [k], b.gain [k], t);
    }
    r.square        = lerp    (a.square,        b.square,        t);
    r.bpFreq        = logLerp (a.bpFreq,        b.bpFreq,        t);
    r.bpQ           = logLerp (a.bpQ,           b.bpQ,           t);
    r.bpMix         = lerp    (a.bpMix,         b.bpMix,         t);
    r.fastTau       = logLerp (a.fastTau,       b.fastTau,       t);
    r.slowTau       = logLerp (a.slowTau,       b.slowTau,       t);
    r.fastAmount    = lerp    (a.fastAmount,    b.fastAmount,    t);
    r.clickLevel    = lerp    (a.clickLevel,    b.clickLevel,    t);
    r.clickFreq     = logLerp (a.clickFreq,     b.clickFreq,     t);
    r.noiseLevel    = lerp    (a.noiseLevel,    b.noiseLevel,    t);
    r.noiseFreq     = logLerp (a.noiseFreq,     b.noiseFreq,     t);
    r.strikes       = lerp    (a.strikes,       b.strikes,       t);
    r.strikeSpacing = logLerp (std::max (a.strikeSpacing, 1.0e-3f), std::max (b.strikeSpacing, 1.0e-3f), t);
    r.strikeStep    = lerp    (a.strikeStep,    b.strikeStep,    t);
    r.strikeDecay   = lerp    (a.strikeDecay,   b.strikeDecay,   t);
    r.drive         = lerp    (a.drive,         b.drive,         t);
    r.level         = lerp    (a.level,         b.level,         t);
    return r;
}

} // namespace metal
