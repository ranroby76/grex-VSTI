#pragma once

#include <array>
#include "KickPrototypes.h"
#include "HatPrototypes.h"
#include "SnarePrototypes.h"
#include "ClapPrototypes.h"

//==============================================================================
//  PercPrototypes.h  —  EDM KIT percussion that the existing voices can play.
//
//      HAND DRUMS  bongos, congas, timbales  -> the membrane (kick) voice
//      SHAKERS     shaker, maracas, cabasa, tambourines -> the hat voice's
//                  noise path (tambourine jingles use its metal cluster)
//      WOODS       claves, woodblocks        -> the snare voice's rim + click
//      SCRAPES     guiros, vibraslap, castanets -> the clap voice's burst train
//
//  The metal bells live in MetalPrototypes.h, whistles / cuica / effects in
//  TonePrototypes.h.  Same fixed-topology rule as every other bank.
//==============================================================================

namespace perc
{

#define PERC_MEMBRANE { 1.00f, 1.50f, 1.98f, 2.44f, 2.87f, 3.30f, 3.72f, 4.14f }   // hand drums: near-harmonic
#define PERC_SHELL    { 1.00f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f, 3.50f }   // timbales: ringing metal shell
#define PERC_HARMONIC { 1.00f, 2.00f, 3.00f, 4.20f, 5.40f, 6.80f, 8.40f, 10.0f }

//==============================================================================
//  Woods play on the struck snare model (no wires); hand drums on the struck
//  membrane model (KickVoice.h).  Settings FITTED where a reference exists.
//==============================================================================
inline void applyWoodEngines (std::array<snare::SnarePrototype, 4>& b)
{
    for (auto& k : b)
    {
        const std::string n (k.name);
        k.wireLevel = 0.0f;
        if (n == "808 Claves")                                                         // TR-808 CL
        {
            snare::setStruck (k, 6160.0f, 0.0f, 0.699f, 0.355f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.01f, 0.023f, 0.10f);
            k.rimFreq = 3020.0f;  k.rimTau = 0.0094f;
            k.level  *= 1.28f;                  // level-matched to the original
        }
        else    // claves and woodblocks keep their own tones, struck hard
            snare::setStruck (k, 0.0f, 0.0f, 0.80f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.20f);
    }
}

// Hand drums on the struck membrane model: fitted to the TR-808 congas; the
// acoustic ones take the fitted real tom's shape - a quick first drop, then
// the ring - with a hand slap for the click.
inline void applyHandDrumEngines (std::array<kick::KickPrototype, 10>& b)
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
        //                                    f0      tens    hard    tau     upGain  upTau   fast    fastTau  pRatio  pTau     bClick  click   clkFreq  clkTau   vca
        if      (n == "808 Conga Hi")  set (k, 340.0f, 0.188f, 0.174f, 0.398f, 1.510f, 0.674f, 0.000f, 0.0500f, 1.300f, 0.0041f, 0.192f, -1.0f,  0.0f,    0.0f,    0.045f);  // TR-808 HC
        else if (n == "808 Conga Mid") set (k, 257.0f, 0.194f, 0.041f, 0.190f, 1.060f, 5.050f, 0.517f, 0.0717f, 1.000f, 0.0533f, 0.747f, -1.0f,  0.0f,    0.0f,    0.365f);  // TR-808 MC
        else if (n == "808 Conga Lo")  set (k, 177.0f, 0.105f, 0.555f, 0.573f, 0.196f, 2.940f, 0.900f, 0.0076f, 1.000f, 0.0509f, 1.360f, -1.0f,  0.0f,    0.0f,    0.000f);  // TR-808 LC
        else if (n == "Conga Mute")    set (k, 0.0f,   0.100f, 0.800f, 0.150f, 0.050f, 0.050f, 0.750f, 0.0200f, 1.010f, 0.0100f, 0.600f, 1.200f, 3500.0f, 0.0040f, 0.200f);
        else if (n == "Timbale High" || n == "Timbale Low")
                                       set (k, 0.0f,   0.100f, 0.900f, 0.450f, 0.300f, 0.300f, 0.500f, 0.0300f, 1.010f, 0.0100f, 0.500f, 1.300f, 5000.0f, 0.0040f, 0.200f);  // metal shell ring
        else                           set (k, 0.0f,   0.150f, 0.800f, 0.450f, 0.050f, 0.050f, 0.650f, 0.0300f, 1.010f, 0.0100f, 0.600f, 1.200f, 3500.0f, 0.0040f, 0.200f);  // bongos, congas: a hand slap

        // Level-matched to the original sounds
        if      (n == "808 Conga Hi")  k.level *= 1.72f;
        else if (n == "808 Conga Mid") k.level *= 1.19f;
        else if (n == "808 Conga Lo")  k.level *= 1.08f;
    }
}

//==============================================================================
inline const std::array<kick::KickPrototype, 10>& handDrums()
{
    static const std::array<kick::KickPrototype, 10> bank = []
    {
    std::array<kick::KickPrototype, 10> b =
    { {
        { "Bongo High", 480.0f, PERC_MEMBRANE,
          { 1.00f, 0.55f, 0.35f, 0.25f, 0.18f, 0.12f, 0.08f, 0.05f },
          { 0.105f, 0.060f, 0.038f, 0.030f, 0.022f, 0.019f, 0.015f, 0.011f },
          1.08f, 0.008f,   0.8500f, 4500.0f, 0.0015f,   0.2500f, 3000.0f, 0.8f, 0.0150f,   0.10f, 0.38f,
          1.0f, 0.0060f, 0.4000f, 0.18f, 0.3000f, 0.0020f, 0.6000f, 0.1000f },
        { "Bongo Low", 330.0f, PERC_MEMBRANE,
          { 1.00f, 0.55f, 0.35f, 0.25f, 0.18f, 0.12f, 0.08f, 0.05f },
          { 0.135f, 0.075f, 0.049f, 0.038f, 0.030f, 0.024f, 0.019f, 0.015f },
          1.08f, 0.009f,   0.8000f, 4000.0f, 0.0015f,   0.2500f, 2600.0f, 0.8f, 0.0180f,   0.10f, 0.39f,
          1.0f, 0.0060f, 0.4000f, 0.18f, 0.3000f, 0.0020f, 0.6000f, 0.1000f },
        { "Conga Mute", 300.0f, PERC_MEMBRANE,
          { 1.00f, 0.50f, 0.40f, 0.30f, 0.20f, 0.15f, 0.10f, 0.08f },
          { 0.050f, 0.035f, 0.025f, 0.020f, 0.018f, 0.015f, 0.012f, 0.010f },
          1.05f, 0.006f,   0.9500f, 3500.0f, 0.0020f,   0.3500f, 2500.0f, 0.7f, 0.0120f,   0.10f, 0.40f,
          1.0f, 0.0060f, 0.5000f, 0.18f, 0.3000f, 0.0020f, 0.6000f, 0.1000f },
        { "Conga Open", 290.0f, PERC_MEMBRANE,
          { 1.00f, 0.40f, 0.25f, 0.15f, 0.10f, 0.07f, 0.05f, 0.03f },
          { 0.192f, 0.096f, 0.060f, 0.048f, 0.036f, 0.030f, 0.024f, 0.018f },
          1.04f, 0.010f,   0.8000f, 3000.0f, 0.0020f,   0.2800f, 2200.0f, 0.6f, 0.0180f,   0.08f, 0.40f,
          1.0f, 0.0060f, 0.4000f, 0.18f, 0.3500f, 0.0020f, 0.6000f, 0.1000f },
        { "Conga Low", 200.0f, PERC_MEMBRANE,
          { 1.00f, 0.40f, 0.25f, 0.15f, 0.10f, 0.07f, 0.05f, 0.03f },
          { 0.252f, 0.120f, 0.072f, 0.054f, 0.042f, 0.033f, 0.027f, 0.021f },
          1.04f, 0.012f,   0.7500f, 2800.0f, 0.0020f,   0.2600f, 1800.0f, 0.6f, 0.0200f,   0.08f, 0.41f,
          1.0f, 0.0060f, 0.4000f, 0.18f, 0.3500f, 0.0022f, 0.6000f, 0.1000f },
        // Timbales: metal shells - the upper modes ring as long as the head
        { "Timbale High", 470.0f, PERC_SHELL,
          { 1.00f, 0.60f, 0.50f, 0.45f, 0.40f, 0.35f, 0.30f, 0.25f },
          { 0.150f, 0.180f, 0.168f, 0.150f, 0.132f, 0.120f, 0.108f, 0.090f },
          1.02f, 0.005f,   1.0000f, 5000.0f, 0.0020f,   0.2500f, 4000.0f, 0.9f, 0.0200f,   0.15f, 0.31f,
          1.0f, 0.0060f, 0.2500f, 0.18f, 0.2000f, 0.0015f, 0.7000f, 0.1000f },
        { "Timbale Low", 340.0f, PERC_SHELL,
          { 1.00f, 0.60f, 0.50f, 0.45f, 0.40f, 0.35f, 0.30f, 0.25f },
          { 0.180f, 0.204f, 0.180f, 0.162f, 0.144f, 0.126f, 0.114f, 0.096f },
          1.02f, 0.006f,   0.9500f, 4500.0f, 0.0020f,   0.2500f, 3500.0f, 0.9f, 0.0200f,   0.15f, 0.32f,
          1.0f, 0.0060f, 0.2500f, 0.18f, 0.2000f, 0.0015f, 0.7000f, 0.1000f },
        // 808 congas: one sine and a pitch drop
        { "808 Conga Hi", 370.0f, PERC_HARMONIC,
          { 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.108f, 0.030f, 0.024f, 0.024f, 0.018f, 0.018f, 0.018f, 0.018f },
          1.25f, 0.020f,   0.0500f, 3000.0f, 0.0020f,   0.0000f, 2000.0f, 0.5f, 0.0200f,   0.05f, 0.40f,
          1.0f, 0.0060f, 0.1000f, 0.18f, 0.2000f, 0.0020f, 0.7000f, 0.1000f },
        { "808 Conga Mid", 250.0f, PERC_HARMONIC,
          { 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.132f, 0.030f, 0.024f, 0.024f, 0.018f, 0.018f, 0.018f, 0.018f },
          1.25f, 0.022f,   0.0500f, 3000.0f, 0.0020f,   0.0000f, 2000.0f, 0.5f, 0.0200f,   0.05f, 0.41f,
          1.0f, 0.0060f, 0.1000f, 0.18f, 0.2000f, 0.0020f, 0.7000f, 0.1000f },
        { "808 Conga Lo", 165.0f, PERC_HARMONIC,
          { 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.156f, 0.030f, 0.024f, 0.024f, 0.018f, 0.018f, 0.018f, 0.018f },
          1.25f, 0.025f,   0.0500f, 3000.0f, 0.0020f,   0.0000f, 2000.0f, 0.5f, 0.0200f,   0.05f, 0.42f,
          1.0f, 0.0060f, 0.1000f, 0.18f, 0.2000f, 0.0020f, 0.7000f, 0.1000f },
    } };
    applyHandDrumEngines (b);
    return b;
    }();

    return bank;
}

#undef PERC_MEMBRANE
#undef PERC_SHELL
#undef PERC_HARMONIC

//==============================================================================
//  Shakers - played as the hat voice's CLOSED articulation.
//==============================================================================
inline const std::array<hat::HatPrototype, 6>& shakers()
{
    static const std::array<hat::HatPrototype, 6> bank =
    { {
        { "Shaker",   2000.0f, { 1.00f, 1.48f, 1.80f, 2.55f, 2.63f, 3.90f },
          0.00f, 1.00f,  6000.0f, 0.60f, 2800.0f,   0.045f, 0.030f, 0.120f, 0.0100f,   0.00f, 0.05f, 0.66f },
        { "Maracas",  2000.0f, { 1.00f, 1.48f, 1.80f, 2.55f, 2.63f, 3.90f },
          0.00f, 1.00f,  7500.0f, 0.80f, 4200.0f,   0.030f, 0.022f, 0.080f, 0.0040f,   0.10f, 0.05f, 0.63f },
        { "Cabasa",   3000.0f, { 1.00f, 1.31f, 1.73f, 2.07f, 2.61f, 3.12f },
          0.15f, 0.85f,  9000.0f, 0.70f, 5000.0f,   0.050f, 0.035f, 0.120f, 0.0020f,   0.20f, 0.05f, 0.63f },
        { "Tambourine", 1250.0f, { 1.00f, 1.37f, 1.63f, 2.11f, 2.52f, 3.03f },
          0.60f, 0.50f,  8000.0f, 0.70f, 4500.0f,   0.120f, 0.100f, 0.400f, 0.0010f,   0.40f, 0.05f, 0.56f },
        { "Tambourine Short", 1350.0f, { 1.00f, 1.37f, 1.63f, 2.11f, 2.52f, 3.03f },
          0.55f, 0.55f,  8500.0f, 0.70f, 5000.0f,   0.090f, 0.060f, 0.250f, 0.0010f,   0.45f, 0.05f, 0.56f },
        { "808 Maracas", 2000.0f, { 1.00f, 1.48f, 1.80f, 2.55f, 2.63f, 3.90f },
          0.00f, 1.00f,  9500.0f, 1.00f, 7000.0f,   0.015f, 0.012f, 0.050f, 0.0010f,   0.00f, 0.05f, 0.66f },
    } };
    return bank;
}

//==============================================================================
//  Woods - the snare voice with no wires: a shell mode or two, the rim sine,
//  a stick click.
//==============================================================================
inline const std::array<snare::SnarePrototype, 4>& woods()
{
    static const std::array<snare::SnarePrototype, 4> bank = []
    {
    std::array<snare::SnarePrototype, 4> b =
    { {
        { "Claves",
          1000.0f, { 1.00f, 2.10f, 3.30f }, { 0.15f, 0.05f, 0.02f }, { 0.020f, 0.010f, 0.010f },
          1.00f, 0.003f,
          0.00f, 3000.0f, 0.80f, 0.50f, 0.020f, 0.0005f,
          0.30f, 6000.0f, 0.0010f,
          1.00f, 2500.0f, 0.065f,
          0.05f, 0.35f },
        { "808 Claves",
          1000.0f, { 1.00f, 2.10f, 3.30f }, { 0.00f, 0.00f, 0.00f }, { 0.020f, 0.010f, 0.010f },
          1.00f, 0.003f,
          0.00f, 3000.0f, 0.80f, 0.50f, 0.020f, 0.0005f,
          0.05f, 6000.0f, 0.0010f,
          1.00f, 2500.0f, 0.030f,
          0.05f, 0.36f },
        { "Woodblock High",
          1100.0f, { 1.00f, 2.63f, 4.20f }, { 0.80f, 0.35f, 0.15f }, { 0.035f, 0.020f, 0.012f },
          1.02f, 0.003f,
          0.00f, 3000.0f, 0.80f, 0.50f, 0.020f, 0.0005f,
          0.50f, 3500.0f, 0.0015f,
          0.50f, 1700.0f, 0.030f,
          0.05f, 0.36f },
        { "Woodblock Low",
          800.0f, { 1.00f, 2.63f, 4.20f }, { 0.80f, 0.35f, 0.15f }, { 0.040f, 0.024f, 0.015f },
          1.02f, 0.003f,
          0.00f, 3000.0f, 0.80f, 0.50f, 0.020f, 0.0005f,
          0.50f, 3000.0f, 0.0015f,
          0.50f, 1250.0f, 0.035f,
          0.05f, 0.37f },
    } };
    applyWoodEngines (b);
    return b;
    }();

    return bank;
}

//==============================================================================
//  Scrapes - long burst trains through a resonant bandpass, no tail.
//==============================================================================
inline const std::array<clap::ClapPrototype, 4>& scrapes()
{
    static const std::array<clap::ClapPrototype, 4> bank =
    { {
        //  name            bursts spacing  spread  burstTau  tailLvl tailTau  bpFreq  bpQ   hpMix hpFreq  drive level  burstDecay
        { "Guiro Short",     7.0f, 0.011f, 0.25f, 0.0025f,  0.00f, 0.030f, 3200.0f, 2.5f, 0.20f, 5000.0f, 0.05f, 0.68f, 0.68f },
        { "Guiro Long",     20.0f, 0.013f, 0.25f, 0.0025f,  0.00f, 0.030f, 3000.0f, 2.5f, 0.20f, 5000.0f, 0.05f, 0.68f, 0.69f },
        { "Vibraslap",      26.0f, 0.030f, 0.15f, 0.0060f,  0.00f, 0.030f, 2800.0f, 5.0f, 0.30f, 4500.0f, 0.10f, 0.64f, 0.63f },
        { "Castanets",       2.0f, 0.012f, 0.20f, 0.0020f,  0.00f, 0.020f, 3500.0f, 2.0f, 0.30f, 6000.0f, 0.05f, 0.68f, 0.56f },
    } };
    return bank;
}

} // namespace perc
