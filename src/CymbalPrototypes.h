#pragma once

#include <array>
#include <algorithm>
#include "DrumDsp.h"

//==============================================================================
//  CymbalPrototypes.h — key 49 (crash) and key 51 (ride)
//
//  One topology for both:
//      Wash  — eight square oscillators at inharmonic ratios + noise, through a
//              bandpass and a highpass, with an attack swell and a long decay
//      Ping  — three high partials with their own decay: the ride's stick
//              definition, and at low partial numbers, its bell
//      Stick — a short bandpassed noise tick
//  Crashes run with the ping at zero; rides use all three layers.
//==============================================================================

namespace cymbal
{

static constexpr int kNumPartials = 8;
static constexpr int kNumPings    = 3;

struct CymbalPrototype
{
    const char* name;

    float baseFreq;                   // Hz of partial 1
    float ratio[kNumPartials];        // inharmonic ratios
    float metalLevel;                 // 0..1
    float noiseLevel;                 // 0..1
    float bpFreq, bpQ;                // Hz
    float hpFreq;                     // Hz
    float washTau;                    // s
    float attack;                     // s (swell)

    float pingLevel;                  // 0 for crashes
    float pingFreq[kNumPings];        // Hz
    float pingTau;                    // s

    float clickLevel;
    float clickFreq;                  // Hz
    float clickTau;                   // s

    float drive;                      // 0..1
    float level;                      // linear

    // ---- Bell realism.  Defaulted, so every table entry above gets them. ----
    float pingDetune = 0.0035f;       // each ping partial is a detuned PAIR: they beat, like metal
    float pingSpread = 0.20f;         // higher ping partials decay faster by this much per step
};

//==============================================================================
//  Partial sets — measured-cymbal-like inharmonic spreads
//==============================================================================
#define CYM_SET_A { 1.00f, 1.41f, 1.63f, 2.05f, 2.37f, 2.71f, 3.13f, 3.79f }
#define CYM_SET_B { 1.00f, 1.37f, 1.58f, 1.97f, 2.29f, 2.66f, 3.05f, 3.61f }
#define CYM_SET_C { 1.00f, 1.49f, 1.71f, 2.19f, 2.54f, 2.87f, 3.38f, 3.97f }
#define CYM_SET_D { 1.00f, 1.33f, 1.67f, 1.91f, 2.43f, 2.78f, 3.29f, 3.71f }
#define CYM_SET_808 { 1.0000f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f, 4.62f, 5.31f }

//==============================================================================
//  CRASH bank — key 49
//==============================================================================
inline const std::array<CymbalPrototype, 8>& crashPrototypes()
{
    static const std::array<CymbalPrototype, 8> bank =
    { {
        { "Bright Crash",   360.0f, CYM_SET_A,   0.45f, 0.55f, 7500.0f, 0.50f, 3500.0f, 0.90f, 0.004f,
          0.0f, { 3000.0f, 4500.0f, 6000.0f }, 0.30f,  0.50f, 5000.0f, 0.0030f,  0.10f, 0.60f },
        { "Dark Crash",     300.0f, CYM_SET_B,   0.40f, 0.60f, 5000.0f, 0.50f, 2500.0f, 1.10f, 0.006f,
          0.0f, { 3000.0f, 4500.0f, 6000.0f }, 0.30f,  0.40f, 3500.0f, 0.0035f,  0.08f, 0.62f },
        { "909 Crash",      420.0f, CYM_SET_C,   0.60f, 0.40f, 9000.0f, 0.60f, 5000.0f, 0.80f, 0.002f,
          0.0f, { 3000.0f, 4500.0f, 6000.0f }, 0.30f,  0.40f, 6500.0f, 0.0025f,  0.25f, 0.58f },
        { "808 Cymbal",     205.3f, CYM_SET_808, 0.85f, 0.15f, 6500.0f, 0.70f, 3500.0f, 0.90f, 0.001f,
          0.0f, { 3000.0f, 4500.0f, 6000.0f }, 0.30f,  0.15f, 5000.0f, 0.0020f,  0.10f, 1.00f },
        { "Trash Crash",    330.0f, CYM_SET_D,   0.55f, 0.45f, 4500.0f, 0.70f, 2000.0f, 0.60f, 0.002f,
          0.0f, { 3000.0f, 4500.0f, 6000.0f }, 0.30f,  0.60f, 3000.0f, 0.0040f,  0.55f, 0.60f },
        { "Splash",         480.0f, CYM_SET_A,   0.45f, 0.55f, 9500.0f, 0.60f, 5500.0f, 0.35f, 0.002f,
          0.0f, { 3000.0f, 4500.0f, 6000.0f }, 0.30f,  0.50f, 7000.0f, 0.0025f,  0.10f, 0.58f },
        { "Big Room Crash", 340.0f, CYM_SET_B,   0.45f, 0.55f, 6500.0f, 0.50f, 3000.0f, 1.50f, 0.010f,
          0.0f, { 3000.0f, 4500.0f, 6000.0f }, 0.30f,  0.40f, 4500.0f, 0.0035f,  0.12f, 0.60f },
        { "Lo-fi Crash",    310.0f, CYM_SET_C,   0.50f, 0.50f, 4000.0f, 0.60f, 2200.0f, 0.70f, 0.004f,
          0.0f, { 3000.0f, 4500.0f, 6000.0f }, 0.30f,  0.35f, 3000.0f, 0.0040f,  0.70f, 0.58f },
    } };

    return bank;
}

//==============================================================================
//  RIDE bank — key 51
//==============================================================================
inline const std::array<CymbalPrototype, 8>& ridePrototypes()
{
    static const std::array<CymbalPrototype, 8> bank =
    { {
        { "Jazz Ride",      380.0f, CYM_SET_A,   0.35f, 0.40f, 6000.0f, 0.60f, 3000.0f, 1.50f, 0.002f,
          0.7150f, { 3200.0f, 4700.0f, 6100.0f }, 0.45f,  0.55f, 6000.0f, 0.0020f,  0.05f, 0.36f },
        { "909 Ride",       450.0f, CYM_SET_C,   0.55f, 0.30f, 9000.0f, 0.70f, 5500.0f, 1.00f, 0.001f,
          0.5200f, { 3800.0f, 5900.0f, 7400.0f }, 0.30f,  0.45f, 7000.0f, 0.0015f,  0.20f, 0.36f },
        { "Rock Ride",      400.0f, CYM_SET_D,   0.40f, 0.40f, 7000.0f, 0.60f, 3500.0f, 1.30f, 0.002f,
          0.7800f, { 2900.0f, 4300.0f, 5800.0f }, 0.50f,  0.65f, 5500.0f, 0.0020f,  0.15f, 0.36f },
        { "Ride Bell",      520.0f, CYM_SET_A,   0.3500f, 0.2500f, 5000.0f, 0.80f, 2500.0f, 1.00f, 0.001f,
          1.3000f, { 1650.0f, 2480.0f, 3710.0f }, 0.9500f,  0.8500f, 5500.0f, 0.0015f,  0.10f, 0.40f,
          0.0040f, 0.2500f },
        { "Ping Ride",      470.0f, CYM_SET_B,   0.30f, 0.25f, 8000.0f, 0.70f, 4500.0f, 1.00f, 0.001f,
          1.0400f, { 3500.0f, 5200.0f, 6900.0f }, 0.55f,  0.70f, 7000.0f, 0.0015f,  0.08f, 0.36f },
        { "Dark Ride",      330.0f, CYM_SET_B,   0.35f, 0.45f, 4500.0f, 0.60f, 2200.0f, 1.80f, 0.002f,
          0.5200f, { 2400.0f, 3500.0f, 4600.0f }, 0.60f,  0.45f, 4000.0f, 0.0025f,  0.05f, 0.37f },
        { "808 Ride",       260.0f, CYM_SET_808, 0.75f, 0.10f, 9000.0f, 0.80f, 6000.0f, 0.80f, 0.001f,
          0.3900f, { 4100.0f, 6200.0f, 8100.0f }, 0.25f,  0.20f, 6000.0f, 0.0015f,  0.10f, 0.40f },
        { "Trash Ride",     360.0f, CYM_SET_D,   0.50f, 0.40f, 5000.0f, 0.70f, 2500.0f, 1.10f, 0.002f,
          0.4550f, { 2700.0f, 3900.0f, 5300.0f }, 0.35f,  0.55f, 4500.0f, 0.0025f,  0.50f, 0.40f },
    } };

    return bank;
}

#undef CYM_SET_A
#undef CYM_SET_B
#undef CYM_SET_C
#undef CYM_SET_D
#undef CYM_SET_808

//==============================================================================
inline CymbalPrototype morph (const CymbalPrototype& a, const CymbalPrototype& b, float t)
{
    using drum::lerp;
    using drum::logLerp;
    t = std::clamp (t, 0.0f, 1.0f);

    CymbalPrototype r = a;
    r.baseFreq = logLerp (a.baseFreq, b.baseFreq, t);
    for (int k = 0; k < kNumPartials; ++k)
        r.ratio[k] = logLerp (a.ratio[k], b.ratio[k], t);
    r.metalLevel = lerp    (a.metalLevel, b.metalLevel, t);
    r.noiseLevel = lerp    (a.noiseLevel, b.noiseLevel, t);
    r.bpFreq     = logLerp (a.bpFreq,     b.bpFreq,     t);
    r.bpQ        = logLerp (a.bpQ,        b.bpQ,        t);
    r.hpFreq     = logLerp (a.hpFreq,     b.hpFreq,     t);
    r.washTau    = logLerp (a.washTau,    b.washTau,    t);
    r.attack     = logLerp (a.attack,     b.attack,     t);
    r.pingLevel  = lerp    (a.pingLevel,  b.pingLevel,  t);
    for (int k = 0; k < kNumPings; ++k)
        r.pingFreq[k] = logLerp (a.pingFreq[k], b.pingFreq[k], t);
    r.pingTau    = logLerp (a.pingTau,    b.pingTau,    t);
    r.clickLevel = lerp    (a.clickLevel, b.clickLevel, t);
    r.clickFreq  = logLerp (a.clickFreq,  b.clickFreq,  t);
    r.clickTau   = logLerp (a.clickTau,   b.clickTau,   t);
    r.drive      = lerp    (a.drive,      b.drive,      t);
    r.level      = lerp    (a.level,      b.level,      t);
    r.pingDetune = lerp    (a.pingDetune, b.pingDetune, t);
    r.pingSpread = lerp    (a.pingSpread, b.pingSpread, t);
    return r;
}

} // namespace cymbal
