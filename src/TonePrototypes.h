#pragma once

#include <array>
#include <algorithm>
#include "DrumDsp.h"

//==============================================================================
//  TonePrototypes.h  —  pitched effects: whistles, cuica, scratches, zaps,
//  blips, noise hits and sweeps.
//
//  One oscillator (sine <-> band-limited saw) with a pitch path built from
//  three parts:  a GLIDE from pitchStartSemis back to 0 (cuica rise, zap
//  plunge), a late FALL of pitchFallSemis, and a pitch LFO (vibrato, or the
//  back-and-forth of a scratch at big depths).  Plus an amplitude trill, a
//  noise layer whose centre can follow the pitch, a low-pass, and an
//  attack / hold / decay envelope.  toneLevel 0 = a pure noise effect.
//==============================================================================

namespace tone
{

struct TonePrototype
{
    const char* name;

    float f0;                  // Hz, where the glide lands
    float toneLevel;           // 0..1 oscillator level (0 = noise-only effect)
    float saw;                 // 0 = sine .. 1 = saw

    float pitchStartSemis;     // offset at the hit, glides to 0
    float pitchTau;            // s
    float pitchFallSemis;      // late drift (negative = falls)
    float fallTau;             // s

    float vibRate, vibSemis;   // pitch LFO: rate Hz, depth semitones
    float tremRate, tremDepth; // amplitude trill: rate Hz, depth 0..1

    float noiseLevel;          // breath / grit
    float noiseFreq;           // Hz, noise bandpass centre
    float noiseQ;
    float noiseTrack;          // 0..1: noise centre follows the pitch path

    float lpFreq;              // Hz, output low-pass

    float attack;              // s (linear)
    float hold;                // s at full level
    float decayTau;            // s

    float drive, level;
};

inline const std::array<TonePrototype, 16>& prototypes()
{
    static const std::array<TonePrototype, 16> bank =
    { {
        //  name              f0      tone  saw   pStart pTau    pFall  fTau   vibR  vibSt tremR tremD  noise nFreq   nQ   nTrk  lp       att     hold   decay  drive level
        // ---- Whistles -------------------------------------------------------------------------------------------------------------
        { "Whistle Short",   2350.0f, 1.0f, 0.0f,  -2.0f, 0.010f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 0.15f, 2400.0f, 3.0f, 1.0f,  8000.0f, 0.004f, 0.080f, 0.030f, 0.05f, 0.23f },
        { "Whistle Long",    2350.0f, 1.0f, 0.0f,  -2.0f, 0.010f,  0.0f, 0.10f,  5.0f, 0.15f,  0.0f, 0.0f, 0.15f, 2400.0f, 3.0f, 1.0f,  8000.0f, 0.004f, 0.350f, 0.060f, 0.05f, 0.23f },
        { "Samba Whistle",   2600.0f, 1.0f, 0.0f,  -1.5f, 0.010f,  0.0f, 0.10f, 32.0f, 0.40f, 32.0f, 0.8f, 0.20f, 2600.0f, 3.0f, 1.0f,  9000.0f, 0.003f, 0.300f, 0.050f, 0.05f, 0.23f },
        // ---- Cuica ----------------------------------------------------------------------------------------------------------------
        { "Cuica Mute",       520.0f, 1.0f, 0.2f,  -9.0f, 0.020f, -3.0f, 0.05f,  0.0f, 0.0f,   0.0f, 0.0f, 0.10f, 1200.0f, 1.5f, 1.0f,  3000.0f, 0.003f, 0.020f, 0.050f, 0.10f, 0.36f },
        { "Cuica Open",       460.0f, 1.0f, 0.2f, -12.0f, 0.030f, -5.0f, 0.18f,  0.0f, 0.0f,   0.0f, 0.0f, 0.10f, 1000.0f, 1.5f, 1.0f,  3000.0f, 0.004f, 0.080f, 0.120f, 0.10f, 0.36f },
        // ---- Scratches ------------------------------------------------------------------------------------------------------------
        { "Scratch Push",     380.0f, 0.7f, 0.6f,  12.0f, 0.050f,  0.0f, 0.10f,  7.0f, 7.0f,   0.0f, 0.0f, 0.60f, 1800.0f, 1.2f, 1.0f,  5000.0f, 0.003f, 0.050f, 0.070f, 0.30f, 0.48f },
        { "Scratch Pull",     300.0f, 0.7f, 0.6f, -12.0f, 0.050f,  0.0f, 0.10f,  9.0f, 9.0f,   0.0f, 0.0f, 0.70f, 1500.0f, 1.2f, 1.0f,  4500.0f, 0.003f, 0.060f, 0.080f, 0.30f, 0.48f },
        // ---- Zaps and lasers ------------------------------------------------------------------------------------------------------
        { "Zap",              180.0f, 1.0f, 0.0f,  36.0f, 0.030f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 0.00f, 3000.0f, 1.0f, 0.0f, 12000.0f, 0.001f, 0.000f, 0.090f, 0.20f, 0.42f },
        { "Zap Low",           90.0f, 1.0f, 0.3f,  40.0f, 0.060f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 0.00f, 3000.0f, 1.0f, 0.0f, 10000.0f, 0.001f, 0.000f, 0.180f, 0.30f, 0.45f },
        { "Laser Rise",      2000.0f, 1.0f, 0.2f, -30.0f, 0.150f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 0.00f, 3000.0f, 1.0f, 0.0f, 12000.0f, 0.005f, 0.050f, 0.200f, 0.15f, 0.32f },
        // ---- Blips ----------------------------------------------------------------------------------------------------------------
        { "Hi Q",            1800.0f, 1.0f, 0.0f,   5.0f, 0.004f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 0.20f, 5000.0f, 1.0f, 0.0f, 14000.0f, 0.0005f,0.000f, 0.025f, 0.05f, 0.42f },
        { "Blip High",       2600.0f, 1.0f, 0.4f,   0.0f, 0.010f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 0.00f, 5000.0f, 1.0f, 0.0f, 12000.0f, 0.0005f,0.010f, 0.015f, 0.05f, 0.45f },
        { "Blip Low",        1300.0f, 1.0f, 0.4f,   0.0f, 0.010f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 0.00f, 5000.0f, 1.0f, 0.0f, 12000.0f, 0.0005f,0.012f, 0.020f, 0.05f, 0.50f },
        // ---- Noise effects --------------------------------------------------------------------------------------------------------
        { "Noise Click",      100.0f, 0.0f, 0.0f,   0.0f, 0.010f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 1.00f, 6000.0f, 0.8f, 0.0f, 16000.0f, 0.0003f,0.000f, 0.008f, 0.05f, 0.70f },
        { "Noise Hit",        100.0f, 0.0f, 0.0f,   0.0f, 0.010f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 1.00f, 3000.0f, 0.6f, 0.0f, 14000.0f, 0.0005f,0.000f, 0.120f, 0.20f, 0.56f },
        { "Noise Sweep",      800.0f, 0.0f, 0.0f, -24.0f, 0.250f,  0.0f, 0.10f,  0.0f, 0.0f,   0.0f, 0.0f, 1.00f,  800.0f, 1.5f, 1.0f, 16000.0f, 0.150f, 0.050f, 0.250f, 0.10f, 0.65f },
    } };

    return bank;
}

inline TonePrototype morph (const TonePrototype& a, const TonePrototype& b, float t)
{
    using drum::lerp;
    using drum::logLerp;
    t = std::clamp (t, 0.0f, 1.0f);

    TonePrototype r = a;
    r.f0              = logLerp (a.f0,        b.f0,        t);
    r.toneLevel       = lerp    (a.toneLevel, b.toneLevel, t);
    r.saw             = lerp    (a.saw,       b.saw,       t);
    r.pitchStartSemis = lerp    (a.pitchStartSemis, b.pitchStartSemis, t);
    r.pitchTau        = logLerp (a.pitchTau,  b.pitchTau,  t);
    r.pitchFallSemis  = lerp    (a.pitchFallSemis,  b.pitchFallSemis,  t);
    r.fallTau         = logLerp (a.fallTau,   b.fallTau,   t);
    r.vibRate         = lerp    (a.vibRate,   b.vibRate,   t);
    r.vibSemis        = lerp    (a.vibSemis,  b.vibSemis,  t);
    r.tremRate        = lerp    (a.tremRate,  b.tremRate,  t);
    r.tremDepth       = lerp    (a.tremDepth, b.tremDepth, t);
    r.noiseLevel      = lerp    (a.noiseLevel, b.noiseLevel, t);
    r.noiseFreq       = logLerp (a.noiseFreq, b.noiseFreq, t);
    r.noiseQ          = logLerp (a.noiseQ,    b.noiseQ,    t);
    r.noiseTrack      = lerp    (a.noiseTrack, b.noiseTrack, t);
    r.lpFreq          = logLerp (a.lpFreq,    b.lpFreq,    t);
    r.attack          = logLerp (std::max (a.attack, 1.0e-4f), std::max (b.attack, 1.0e-4f), t);
    r.hold            = lerp    (a.hold,      b.hold,      t);
    r.decayTau        = logLerp (a.decayTau,  b.decayTau,  t);
    r.drive           = lerp    (a.drive,     b.drive,     t);
    r.level           = lerp    (a.level,     b.level,     t);
    return r;
}

} // namespace tone
