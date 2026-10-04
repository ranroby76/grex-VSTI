#pragma once

#include <array>
#include "KickPrototypes.h"
#include <string>

//==============================================================================
//  TomPrototypes.h — keys 41, 43, 45, 47, 48, 50
//
//  A tom is a tuned membrane, which is exactly what the kick voice models, so
//  the toms are played by kick::KickVoice unchanged and only need their own
//  numbers: higher fundamentals, the circular-membrane mode ladder, longer
//  ring and a stick instead of a beater.
//
//  f0 is the LOW-MID tom (key 47).  The other five are the same prototype
//  shifted along a fixed ladder (kTomLadderSemis), so one page tunes the whole
//  set and every tom keeps the same character.
//==============================================================================

namespace tom
{

// Semitone offsets from f0, in the order: 41 Low Floor, 43 High Floor, 45 Low,
// 47 Low-Mid, 48 Hi-Mid, 50 High.
static constexpr float kTomLadderSemis[6] = { -9.0f, -6.0f, -3.0f, 0.0f, 2.0f, 5.0f };

// Circular membrane mode ratios (Bessel zeros, normalised to the fundamental)
// Air-loaded, two-headed: close to harmonic, which is why a tom has a pitch
// and not the bell-like clang of an ideal membrane.
#define TOM_MEMBRANE { 1.00f, 1.50f, 1.98f, 2.44f, 2.87f, 3.30f, 3.72f, 4.14f }

//==============================================================================
//  Toms play on the struck membrane model (KickVoice.h, THE KICK MODELS).
//  Settings FITTED where a reference exists.
//==============================================================================
inline void applyTomEngines (std::array<kick::KickPrototype, 10>& b)
{
    // A struck head.  tauScale sets the whole ring; upGain / upTau shape the
    // UPPER modes (a real tom is its fundamental plus one partial - the rest
    // dies within milliseconds); fast / fastTau are the quick first drop a
    // real head makes before it rings on; the click is the stick or hand.
    auto set = [] (kick::KickPrototype& k, float f0, float tension, float hardness, float tauScale,
                   float upGain, float upTau, float fast, float fastTau, float pitchRatio, float pitchTau,
                   float bodyClick, float clickLevel, float clickFreq, float clickTau, float vcaDrive)
    {
        k.model = 1.0f;
        if (f0 > 0.0f) k.f0 = f0;
        k.tension = tension;  k.hardness = hardness;
        for (int i = 0; i < kick::kNumModes; ++i)
        {
            k.modeTau[i] *= tauScale;
            if (i > 0) { k.modeGain[i] *= upGain;  k.modeTau[i] *= upTau; }
        }
        k.strikeFast = fast;          k.strikeFastTau = fastTau;
        k.pitchRatio = pitchRatio;    k.pitchTau = pitchTau;
        k.bodyClick  = bodyClick;     k.vcaDrive = vcaDrive;
        if (clickLevel >= 0.0f) { k.clickLevel = clickLevel; k.clickFreq = clickFreq; k.clickTau = clickTau; }
    };
    for (auto& k : b)
    {
        const std::string n (k.name);
        //                                        f0      tens    hard    tau     upGain  upTau   fast    fastTau  pRatio  pTau     bClick  click   clkFreq  clkTau   vca
        if      (n == "Acoustic Rock Toms") set (k, 142.0f, 0.297f, 0.348f, 0.375f, 0.021f, 0.041f, 0.723f, 0.0386f, 1.010f, 0.0125f, 0.438f, 1.370f, 3110.0f, 0.0048f, 0.600f);  // DrumTraks tom 1
        else if (n == "Acoustic Jazz Toms") set (k, 102.0f, 0.058f, 0.830f, 0.320f, 1.700f, 0.040f, 0.527f, 0.1500f, 1.300f, 0.1000f, 1.290f, 0.935f, 6940.0f, 0.0078f, 0.310f);  // DrumTraks tom 2
        else if (n == "808 Tom")            set (k, 136.0f, 0.021f, 0.004f, 0.143f, 0.225f, 0.578f, 0.000f, 0.0500f, 1.300f, 0.0134f, 0.148f, -1.0f,  0.0f,    0.0f,    0.678f);  // TR-808 MT
        // No reference of their own: the fitted real tom's shape, their own pitch
        else if (n == "Power Toms")         set (k, 0.0f,   0.297f, 0.500f, 0.380f, 0.050f, 0.050f, 0.720f, 0.0350f, 1.010f, 0.0125f, 0.500f, 1.400f, 3000.0f, 0.0050f, 0.350f);
        else if (n == "Tight Pop Toms")     set (k, 0.0f,   0.250f, 0.500f, 0.250f, 0.030f, 0.041f, 0.750f, 0.0300f, 1.010f, 0.0125f, 0.450f, 1.300f, 3500.0f, 0.0045f, 0.300f);
        else if (n == "Deep Floor")         set (k, 0.0f,   0.297f, 0.400f, 0.450f, 0.030f, 0.041f, 0.700f, 0.0400f, 1.010f, 0.0125f, 0.400f, 1.200f, 2800.0f, 0.0050f, 0.200f);
        else if (n == "Lo-fi Tom")          set (k, 0.0f,   0.250f, 0.400f, 0.330f, 0.040f, 0.041f, 0.700f, 0.0400f, 1.010f, 0.0125f, 0.400f, 1.000f, 2800.0f, 0.0050f, 0.300f);
        // Electronic toms: the fitted 808 tom with a modest sweep, never a zap
        else if (n == "909 Tom")            set (k, 0.0f,   0.030f, 0.300f, 0.300f, 0.250f, 0.500f, 0.300f, 0.0400f, 1.250f, 0.0300f, 0.500f, -1.0f,  0.0f,    0.0f,    0.500f);
        else if (n == "Syn Tom")            set (k, 0.0f,   0.030f, 0.250f, 0.350f, 0.250f, 0.500f, 0.300f, 0.0400f, 1.400f, 0.0400f, 0.400f, -1.0f,  0.0f,    0.0f,    0.500f);
        else if (n == "Electro Tom")        set (k, 0.0f,   0.030f, 0.300f, 0.300f, 0.250f, 0.500f, 0.300f, 0.0400f, 1.300f, 0.0300f, 0.500f, -1.0f,  0.0f,    0.0f,    0.500f);
    }
}

inline const std::array<kick::KickPrototype, 10>& prototypes()
{
    static const std::array<kick::KickPrototype, 10> bank = []
    {
    std::array<kick::KickPrototype, 10> b =
    { {
        // 0 — Acoustic Rock Toms: full, medium ring, stick attack
        { "Acoustic Rock Toms",
          125.0f, TOM_MEMBRANE,
          { 1.00f, 0.50f, 0.32f, 0.22f, 0.15f, 0.10f, 0.07f, 0.05f },
          { 0.390f, 0.208f, 0.130f, 0.098f, 0.078f, 0.058f, 0.046f, 0.033f },
          1.12f, 0.040f,
          0.7000f, 3200.0f, 0.0030f,
          0.3000f, 900.0f, 0.5000f, 0.0350f,
          0.15f, 0.85f,
          1.0f, 0.0060f, 0.3500f, 0.18f, 0.3500f, 0.0030f, 0.5500f, 0.1200f },

        // 1 — Acoustic Jazz Toms: higher, open, long ring
        { "Acoustic Jazz Toms",
          150.0f, TOM_MEMBRANE,
          { 1.00f, 0.55f, 0.40f, 0.28f, 0.20f, 0.14f, 0.10f, 0.07f },
          { 0.520f, 0.293f, 0.195f, 0.143f, 0.111f, 0.085f, 0.065f, 0.052f },
          1.06f, 0.030f,
          0.5500f, 2800.0f, 0.0025f,
          0.2200f, 1000.0f, 0.5000f, 0.0350f,
          0.05f, 0.82f,
          1.0f, 0.0060f, 0.3000f, 0.18f, 0.2500f, 0.0030f, 0.6000f, 0.1200f },

        // 2 — Power Toms: deep, driven, strong drop
        { "Power Toms",
          110.0f, TOM_MEMBRANE,
          { 1.00f, 0.45f, 0.28f, 0.18f, 0.12f, 0.08f, 0.05f, 0.03f },
          { 0.455f, 0.195f, 0.117f, 0.078f, 0.058f, 0.046f, 0.033f, 0.026f },
          1.20f, 0.050f,
          0.8000f, 3500.0f, 0.0035f,
          0.3200f, 850.0f, 0.5000f, 0.0400f,
          0.35f, 0.85f,
          1.0f, 0.0060f, 0.3500f, 0.18f, 0.4500f, 0.0030f, 0.5500f, 0.1200f },

        // 3 — Tight Pop Toms: short and controlled
        { "Tight Pop Toms",
          140.0f, TOM_MEMBRANE,
          { 1.00f, 0.40f, 0.22f, 0.12f, 0.07f, 0.04f, 0.02f, 0.01f },
          { 0.208f, 0.104f, 0.065f, 0.046f, 0.033f, 0.026f, 0.019f, 0.013f },
          1.10f, 0.025f,
          0.7500f, 3800.0f, 0.0025f,
          0.2800f, 1000.0f, 0.5000f, 0.0300f,
          0.15f, 0.85f,
          1.0f, 0.0060f, 0.4000f, 0.18f, 0.4000f, 0.0030f, 0.5500f, 0.1200f },

        // 4 — 808 Tom: pure sine with a long pitch sweep
        { "808 Tom",
          130.0f, { 1.00f, 2.00f, 3.00f, 4.20f, 5.40f, 6.80f, 8.40f, 10.0f },
          { 1.00f, 0.05f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.360f, 0.064f, 0.040f, 0.032f, 0.032f, 0.024f, 0.024f, 0.024f },
          1.60f, 0.060f,
          0.2000f, 2000.0f, 0.0030f,
          0.1200f, 1500.0f, 0.5000f, 0.0300f,
          0.10f, 0.85f,
          1.0f, 0.0060f, 0.1500f, 0.18f, 0.3000f, 0.0030f, 0.7000f, 0.1200f },

        // 5 — 909 Tom: sweep + noise, a bit of grit
        { "909 Tom",
          140.0f, { 1.00f, 1.70f, 2.60f, 3.60f, 4.80f, 6.00f, 7.50f, 9.00f },
          { 1.00f, 0.20f, 0.06f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.297f, 0.102f, 0.051f, 0.043f, 0.034f, 0.034f, 0.025f, 0.025f },
          1.45f, 0.040f,
          0.6000f, 3000.0f, 0.0030f,
          0.2800f, 1800.0f, 0.5000f, 0.0400f,
          0.25f, 0.85f,
          1.0f, 0.0060f, 0.3000f, 0.18f, 0.7000f, 0.0030f, 0.6000f, 0.1200f },

        // 6 — Syn Tom: the big 80s electronic "pew"
        { "Syn Tom",
          150.0f, { 1.00f, 2.00f, 3.00f, 4.20f, 5.40f, 6.80f, 8.40f, 10.0f },
          { 1.00f, 0.10f, 0.03f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.400f, 0.080f, 0.040f, 0.032f, 0.032f, 0.024f, 0.024f, 0.024f },
          2.20f, 0.120f,
          0.3000f, 2500.0f, 0.0020f,
          0.2500f, 3000.0f, 0.7000f, 0.0800f,
          0.30f, 0.82f,
          1.0f, 0.0060f, 0.2000f, 0.18f, 0.3000f, 0.0030f, 0.7000f, 0.1200f },

        // 7 — Deep Floor: low, long, round
        { "Deep Floor",
          100.0f, TOM_MEMBRANE,
          { 1.00f, 0.50f, 0.35f, 0.24f, 0.16f, 0.10f, 0.07f, 0.05f },
          { 0.570f, 0.300f, 0.180f, 0.120f, 0.090f, 0.066f, 0.048f, 0.036f },
          1.10f, 0.050f,
          0.6000f, 2600.0f, 0.0035f,
          0.2800f, 800.0f, 0.5000f, 0.0400f,
          0.10f, 0.88f,
          1.0f, 0.0060f, 0.3500f, 0.18f, 0.3000f, 0.0030f, 0.5500f, 0.1200f },

        // 8 — Electro Tom: short, bright, driven
        { "Electro Tom",
          160.0f, { 1.00f, 1.50f, 2.00f, 2.50f, 3.00f, 3.50f, 4.00f, 4.50f },
          { 1.00f, 0.15f, 0.05f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.220f, 0.070f, 0.044f, 0.035f, 0.035f, 0.026f, 0.026f, 0.026f },
          1.80f, 0.030f,
          0.6000f, 4000.0f, 0.0020f,
          0.2200f, 2500.0f, 0.6000f, 0.0300f,
          0.40f, 0.82f,
          1.0f, 0.0060f, 0.3000f, 0.18f, 0.8000f, 0.0030f, 0.6000f, 0.1200f },

        // 9 — Lo-fi Tom: dull and dirty
        { "Lo-fi Tom",
          120.0f, TOM_MEMBRANE,
          { 1.00f, 0.40f, 0.25f, 0.15f, 0.10f, 0.06f, 0.04f, 0.02f },
          { 0.300f, 0.150f, 0.090f, 0.060f, 0.045f, 0.038f, 0.030f, 0.022f },
          1.15f, 0.030f,
          0.5000f, 2200.0f, 0.0040f,
          0.3000f, 1000.0f, 0.5000f, 0.0900f,
          0.80f, 0.82f,
          1.0f, 0.0060f, 0.3500f, 0.18f, 0.4000f, 0.0030f, 0.6000f, 0.1200f },
    } };
    applyTomEngines (b);
    return b;
    }();

    return bank;
}

#undef TOM_MEMBRANE

} // namespace tom
