#pragma once

#include <array>
#include <algorithm>
#include "DrumDsp.h"

//==============================================================================
//  ClapPrototypes.h — key 39
//
//  A clap is several short noise bursts a few milliseconds apart (many hands,
//  never quite together) followed by a filtered tail (the room).  The 808 and
//  909 claps are exactly that circuit; a real clap is the same shape with more
//  bursts and looser timing, so one topology covers both.
//==============================================================================

namespace clap
{

struct ClapPrototype
{
    const char* name;

    float bursts;        // number of bursts, 1..32 (float so it can morph; rounded per hit)
    float spacing;       // s between bursts
    float spread;        // 0..1 random variation of spacing and burst level
    float burstTau;      // s, decay of each burst

    float tailLevel;     // tail relative to a burst
    float tailTau;       // s

    float bpFreq;        // Hz, body band
    float bpQ;
    float hpMix;         // 0..1+ highpassed noise added for sizzle
    float hpFreq;        // Hz

    float drive;         // 0..1
    float level;         // linear

    // Level multiplier from one burst to the next (1 = even).  Below 1 the
    // train dies away - a vibraslap rattle, a guiro stroke.  Defaulted so
    // every clap in the table above keeps its exact sound.
    float burstDecay = 1.0f;
};

inline const std::array<ClapPrototype, 10>& prototypes()
{
    static const std::array<ClapPrototype, 10> bank =
    { {
        //  name                bursts spacing  spread  burstTau  tailLvl tailTau  bpFreq  bpQ   hpMix hpFreq  drive level
        { "808 Clap",           4.0f, 0.0095f, 0.15f, 0.0035f,  0.60f, 0.120f, 1050.0f, 1.2f, 0.15f, 3000.0f, 0.15f, 0.85f },
        { "909 Clap",           4.0f, 0.0080f, 0.20f, 0.0030f,  0.55f, 0.150f, 1400.0f, 1.1f, 0.35f, 4000.0f, 0.25f, 0.85f },
        { "Real Clap Small",    5.0f, 0.0110f, 0.55f, 0.0045f,  0.35f, 0.090f, 1600.0f, 0.8f, 0.40f, 3500.0f, 0.05f, 0.85f },
        { "Big Room Clap",      6.0f, 0.0130f, 0.45f, 0.0050f,  0.85f, 0.380f, 1300.0f, 0.9f, 0.30f, 3000.0f, 0.20f, 0.80f },
        { "Tight Clap",         3.0f, 0.0065f, 0.20f, 0.0025f,  0.30f, 0.060f, 1700.0f, 1.3f, 0.35f, 4500.0f, 0.20f, 0.85f },
        { "Trap Clap",          4.0f, 0.0070f, 0.25f, 0.0030f,  0.50f, 0.140f, 1250.0f, 1.0f, 0.45f, 5000.0f, 0.45f, 0.82f },
        { "House Clap",         4.0f, 0.0085f, 0.30f, 0.0035f,  0.65f, 0.200f, 1500.0f, 1.0f, 0.40f, 4000.0f, 0.30f, 0.82f },
        { "Snap Clap",          2.0f, 0.0050f, 0.15f, 0.0020f,  0.20f, 0.050f, 2200.0f, 1.6f, 0.50f, 6000.0f, 0.15f, 0.80f },
        { "Lo-fi Clap",         4.0f, 0.0100f, 0.35f, 0.0040f,  0.45f, 0.130f, 1100.0f, 0.9f, 0.10f, 2500.0f, 0.75f, 0.80f },
        { "Reverb Clap",        4.0f, 0.0090f, 0.30f, 0.0035f,  1.00f, 0.550f, 1350.0f, 0.9f, 0.35f, 3500.0f, 0.15f, 0.78f },
    } };

    return bank;
}

inline ClapPrototype morph (const ClapPrototype& a, const ClapPrototype& b, float t)
{
    using drum::lerp;
    using drum::logLerp;
    t = std::clamp (t, 0.0f, 1.0f);

    ClapPrototype r = a;
    r.bursts    = lerp    (a.bursts,    b.bursts,    t);
    r.spacing   = logLerp (a.spacing,   b.spacing,   t);
    r.spread    = lerp    (a.spread,    b.spread,    t);
    r.burstTau  = logLerp (a.burstTau,  b.burstTau,  t);
    r.tailLevel = lerp    (a.tailLevel, b.tailLevel, t);
    r.tailTau   = logLerp (a.tailTau,   b.tailTau,   t);
    r.bpFreq    = logLerp (a.bpFreq,    b.bpFreq,    t);
    r.bpQ       = logLerp (a.bpQ,       b.bpQ,       t);
    r.hpMix     = lerp    (a.hpMix,     b.hpMix,     t);
    r.hpFreq    = logLerp (a.hpFreq,    b.hpFreq,    t);
    r.drive     = lerp    (a.drive,     b.drive,     t);
    r.level     = lerp    (a.level,     b.level,     t);
    r.burstDecay = lerp   (a.burstDecay, b.burstDecay, t);
    return r;
}

} // namespace clap
