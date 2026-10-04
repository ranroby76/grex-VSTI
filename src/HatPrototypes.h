#pragma once

#include <array>
#include <algorithm>
#include "DrumDsp.h"

//==============================================================================
//  HatPrototypes.h — keys 42 (closed), 44 (pedal), 46 (open)
//
//  One hi-hat, three articulations.  The metal is six square oscillators at
//  inharmonic ratios (the 808 circuit's cluster), mixed with noise, then a
//  bandpass and a highpass.  Electronic hats lean on the metal, acoustic ones
//  on the noise — the same fields, different numbers.
//==============================================================================

namespace hat
{

static constexpr int kNumOscs = 6;

enum Articulation { Closed = 0, Pedal = 1, Open = 2 };

struct HatPrototype
{
    const char* name;

    float baseFreq;               // Hz of oscillator 1
    float ratio[kNumOscs];        // oscillator ratios relative to baseFreq
    float metalLevel;             // 0..1
    float noiseLevel;             // 0..1

    float bpFreq, bpQ;            // Hz
    float hpFreq;                 // Hz

    float closedTau;              // s
    float pedalTau;               // s
    float openTau;                // s
    float attack;                 // s

    float clickLevel;             // stick tip on the bow
    float drive;                  // 0..1
    float level;                  // linear
};

inline const std::array<HatPrototype, 10>& prototypes()
{
    static const std::array<HatPrototype, 10> bank =
    { {
        { "808 Hat",
          205.3f, { 1.0000f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f },
          0.85f, 0.15f,  8500.0f, 0.90f, 6000.0f,
          0.030f, 0.022f, 0.300f, 0.0003f,
          0.10f, 0.10f, 1.30f },

        { "909 Hat",
          330.0f, { 1.00f, 1.33f, 1.71f, 2.08f, 2.62f, 3.31f },
          0.45f, 0.55f,  9000.0f, 0.80f, 6500.0f,
          0.040f, 0.028f, 0.350f, 0.0003f,
          0.25f, 0.20f, 0.70f },

        { "Acoustic Bright",
          420.0f, { 1.00f, 1.27f, 1.52f, 1.89f, 2.31f, 2.87f },
          0.40f, 0.60f,  8500.0f, 0.70f, 5500.0f,
          0.055f, 0.040f, 0.400f, 0.0006f,
          0.45f, 0.05f, 0.70f },

        { "Acoustic Dark",
          340.0f, { 1.00f, 1.23f, 1.49f, 1.81f, 2.24f, 2.73f },
          0.35f, 0.65f,  6500.0f, 0.60f, 4000.0f,
          0.070f, 0.050f, 0.450f, 0.0008f,
          0.35f, 0.05f, 0.72f },

        { "Trap Hat",
          290.0f, { 1.00f, 1.49f, 1.83f, 2.51f, 2.70f, 3.90f },
          0.60f, 0.40f,  11000.0f, 1.00f, 8000.0f,
          0.020f, 0.016f, 0.250f, 0.0002f,
          0.15f, 0.25f, 0.70f },

        { "House Hat",
          310.0f, { 1.00f, 1.45f, 1.77f, 2.49f, 2.66f, 3.80f },
          0.55f, 0.45f,  9500.0f, 0.80f, 7000.0f,
          0.045f, 0.030f, 0.350f, 0.0003f,
          0.20f, 0.20f, 0.70f },

        { "Techno Hat",
          250.0f, { 1.00f, 1.51f, 1.95f, 2.62f, 2.80f, 4.10f },
          0.70f, 0.30f,  8000.0f, 1.20f, 6000.0f,
          0.035f, 0.025f, 0.300f, 0.0002f,
          0.30f, 0.45f, 0.68f },

        { "Lo-fi Hat",
          230.0f, { 1.0000f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f },
          0.50f, 0.50f,  6000.0f, 0.70f, 4500.0f,
          0.050f, 0.035f, 0.350f, 0.0005f,
          0.20f, 0.70f, 0.68f },

        { "Crisp Pop",
          380.0f, { 1.00f, 1.29f, 1.58f, 1.97f, 2.43f, 3.02f },
          0.35f, 0.65f,  10500.0f, 0.80f, 7500.0f,
          0.040f, 0.030f, 0.380f, 0.0004f,
          0.40f, 0.10f, 0.70f },

        { "Vintage Tight",
          270.0f, { 1.00f, 1.40f, 1.75f, 2.40f, 2.60f, 3.60f },
          0.60f, 0.40f,  7500.0f, 0.90f, 5500.0f,
          0.028f, 0.020f, 0.280f, 0.0004f,
          0.25f, 0.30f, 0.70f },
    } };

    return bank;
}

inline HatPrototype morph (const HatPrototype& a, const HatPrototype& b, float t)
{
    using drum::lerp;
    using drum::logLerp;
    t = std::clamp (t, 0.0f, 1.0f);

    HatPrototype r = a;
    r.baseFreq = logLerp (a.baseFreq, b.baseFreq, t);
    for (int k = 0; k < kNumOscs; ++k)
        r.ratio[k] = logLerp (a.ratio[k], b.ratio[k], t);
    r.metalLevel = lerp    (a.metalLevel, b.metalLevel, t);
    r.noiseLevel = lerp    (a.noiseLevel, b.noiseLevel, t);
    r.bpFreq     = logLerp (a.bpFreq,     b.bpFreq,     t);
    r.bpQ        = logLerp (a.bpQ,        b.bpQ,        t);
    r.hpFreq     = logLerp (a.hpFreq,     b.hpFreq,     t);
    r.closedTau  = logLerp (a.closedTau,  b.closedTau,  t);
    r.pedalTau   = logLerp (a.pedalTau,   b.pedalTau,   t);
    r.openTau    = logLerp (a.openTau,    b.openTau,    t);
    r.attack     = logLerp (a.attack,     b.attack,     t);
    r.clickLevel = lerp    (a.clickLevel, b.clickLevel, t);
    r.drive      = lerp    (a.drive,      b.drive,      t);
    r.level      = lerp    (a.level,      b.level,      t);
    return r;
}

} // namespace hat
