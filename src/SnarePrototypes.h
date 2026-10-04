#pragma once

#include <array>
#include <string>
#include <algorithm>
#include "DrumDsp.h"

//==============================================================================
//  SnarePrototypes.h
//
//  Two banks, one topology: the SNARE bank (keys 38 and 40) and the SIDE STICK
//  bank (key 37).  A side stick is a snare played without the wires ringing -
//  a short shell body, a hard rim resonance and a stick click - so it needs no
//  voice of its own, only different numbers.
//
//  FIXED TOPOLOGY, as with the kicks: every prototype fills the same fields,
//  so morphing between any two is pure interpolation.  Frequencies and times
//  in the log domain, levels linearly.
//==============================================================================

namespace snare
{

static constexpr int kNumBodyModes = 3;

//==============================================================================
struct SnarePrototype
{
    const char* name;

    // Body — shell/head modes (decaying sines) with a shared pitch envelope
    float bodyF0;                       // Hz
    float bodyRatio [kNumBodyModes];
    float bodyGain  [kNumBodyModes];
    float bodyTau   [kNumBodyModes];    // s
    float pitchRatio;                   // start multiplier (1 = none)
    float pitchTau;                     // s

    // Wires — filtered noise
    float wireLevel;
    float wireFreq;                     // filter centre, Hz
    float wireQ;
    float wireHpMix;                    // 0 = bandpass, 1 = highpass
    float wireTau;                      // s
    float wireAttack;                   // s

    // Click — stick attack, noise burst through a bandpass
    float clickLevel;
    float clickFreq;                    // Hz
    float clickTau;                     // s

    // Rim — one ringing high mode (rimshot / cross-stick crack)
    float rimLevel;
    float rimFreq;                      // Hz
    float rimTau;                       // s

    // Shaping
    float drive;                        // 0..1 baseline saturation
    float level;                        // linear

    // ---- Drum realism.  Defaulted, so every table entry above gets them. ----
    float onset       = 1.0f;           // 0 = body rises from zero, 1 = struck (starts at its peak)
    float bodyScatter = 0.008f;         // per-hit random detune of the upper body modes
    float wireBuzz    = 0.55f;          // wires rattle WITH the head (noise modulated by the body)
    float wireFast    = 0.35f;          // share of the wire noise lost in a fast first stage
    float clickQ      = 0.8f;           // stick noise bandwidth (lower = wider)

    // ---- ENGINE (2026-09 rebuild - see THE SNARE MODELS in SnareVoice.h) ----
    float model     = 0.0f;   // 0 original, 1 struck head, 2 808 circuit, 3 909 circuit
    float tone808   = 0.3f;   // 808: shell tone - 2 modes (the 808) up to 5
    float decay808  = 0.5f;   // 808: shell and snappy decay, 0..1
    float snappy    = 0.5f;   // 808 / 909: snare noise against the shell, 0..1
    float fmAmount  = 0.3f;   // 909: pitch sweep depth, 0..1
    float decay909  = 0.5f;   // 909: decay, 0..1
    float tension   = 0.15f;  // struck: pitch glide with the head's energy
    float hardness  = 0.7f;   // struck: stick hardness = contact time
    float bodyClick = 0.3f;   // struck: click made from the head's own motion
    float vcaDrive  = 0.2f;   // struck: level-dependent saturation + thump
    float wireHold  = 0.0f;   // struck: wires hold at full level first, seconds
    float wireRect  = 0.0f;   // struck: half-wave rectified wire grain (808), 0..1
};

//==============================================================================
//  SNARE bank — keys 38 (Snare) and 40 (E.Snare)
//==============================================================================
//==============================================================================
//  Each snare's and side stick's engine (SnareVoice.h, THE SNARE MODELS).
//  The settings are FITTED to reference recordings where one exists.
//==============================================================================
// Struck-head settings.  tauScale multiplies the sound's own head decays.
inline void setStruck (SnarePrototype& k, float bodyF0, float tension, float hardness, float tauScale,
                       float wireLevel, float wireFreq, float wireTau, float wireHold, float wireBuzz,
                       float rimLevel, float clickLevel, float bodyClick)
{
    k.model = 1.0f;
    if (bodyF0 > 0.0f) k.bodyF0 = bodyF0;
    k.tension = tension;  k.hardness = hardness;
    for (auto& t : k.bodyTau) t *= tauScale;
    if (wireLevel >= 0.0f) k.wireLevel = wireLevel;
    if (wireFreq  > 0.0f)  k.wireFreq  = wireFreq;
    if (wireTau   > 0.0f)  k.wireTau   = wireTau;
    k.wireHold = wireHold;  k.wireBuzz = wireBuzz;
    if (rimLevel   >= 0.0f) k.rimLevel   = rimLevel;
    if (clickLevel >= 0.0f) k.clickLevel = clickLevel;
    k.bodyClick = bodyClick;
}

inline void setCircuit (SnarePrototype& k, float model, float bodyF0, float a, float b, float snappy)
{
    k.model = model;  k.bodyF0 = bodyF0;  k.snappy = snappy;
    if (model == 2.0f) { k.tone808 = a;  k.decay808 = b; }       // 808: tone, decay
    else               { k.fmAmount = a; k.decay909 = b; }       // 909: sweep, decay
}

inline void applySnareEngines (std::array<SnarePrototype, 12>& b)
{
    for (auto& k : b)
    {
        const std::string n (k.name);
        //                                        f0     tone/fm  decay   snappy         fitted to
        if      (n == "808 Snare")    setCircuit (k, 2.0f, 191.0f, 0.790f, 0.247f, 0.360f);   // TR-808 SD 50/50
        else if (n == "Trap Snare")   setCircuit (k, 2.0f, 180.0f, 0.669f, 0.230f, 0.531f);   // TR-808 SD 75/75
        else if (n == "909 Snare")    setCircuit (k, 3.0f, 165.0f, 0.335f, 0.086f, 0.182f);   // TR-909 SD
        else if (n == "House Snare")  setCircuit (k, 3.0f, 100.0f, 0.467f, 0.242f, 0.305f);   // TR-909 SD, tone 3
        else if (n == "Electro Snap") setCircuit (k, 3.0f, 181.0f, 0.384f, 0.287f, 0.174f);   // TR-909 SD, tune 7
        //                                        f0      tens    hard    tau     wLvl    wFreq    wTau    wHold   wBuzz   rim     click   bClick
        else if (n == "Acoustic Maple") setStruck (k, 192.0f, 0.533f, 0.203f, 0.102f, 0.342f, 8140.0f, 0.031f, 0.044f, 0.805f, 1.24f, 0.319f, 0.049f);  // jazz kit snare
        else if (n == "Acoustic Rock")  setStruck (k, 366.0f, 0.010f, 0.530f, 0.061f, 0.683f, 4250.0f, 0.056f, 0.004f, 0.539f, 1.63f, 0.886f, 0.907f);  // acoustic snare
        // No reference of their own: derived from the fitted snares above
        else if (n == "Tight Funk")     setStruck (k, 0.0f,   0.010f, 0.650f, 0.050f, 0.600f, 5000.0f, 0.040f, 0.003f, 0.500f, 1.20f, 0.900f, 0.800f);  // Rock, tighter
        else if (n == "Fat Ballad")     setStruck (k, 0.0f,   0.400f, 0.250f, 0.150f, 0.450f, 6500.0f, 0.080f, 0.030f, 0.800f, 1.00f, 0.300f, 0.100f);  // Maple, fatter
        else if (n == "Piccolo")        setStruck (k, 0.0f,   0.050f, 0.700f, 0.050f, 0.600f, 8000.0f, 0.045f, 0.004f, 0.500f, 1.30f, 0.900f, 0.700f);  // Rock, high + bright
        else if (n == "Rimshot Crack")  setStruck (k, 0.0f,   0.050f, 0.800f, 0.061f, 0.550f, 4500.0f, 0.050f, 0.004f, 0.500f, 2.00f, 1.000f, 0.900f);  // Rock, rim forward
        else if (n == "Lo-fi Snare")  { setStruck (k, 0.0f,   0.300f, 0.300f, 0.080f, 0.450f, 5000.0f, 0.060f, 0.020f, 0.700f, 1.00f, 0.300f, 0.100f);  // Maple, grainy
                                        k.wireRect = 0.5f;  k.vcaDrive = 0.5f; }
    }
}

inline void applyStickEngines (std::array<SnarePrototype, 6>& b)
{
    for (auto& k : b)
    {
        const std::string n (k.name);
        k.wireLevel = 0.0f;
        //                                        f0      tens    hard    tau     wLvl  wFreq wTau  wHold wBuzz  rim      click   bClick
        if      (n == "808 Rimshot")  { setStruck (k, 581.0f, 0.000f, 0.726f, 0.274f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.049f, 0.181f, 0.20f);   // TR-808 RS
                                        k.rimFreq = 6490.0f;  k.rimTau = 0.116f; }
        else if (n == "909 Rim")      { setStruck (k, 581.0f, 0.000f, 0.850f, 0.274f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.080f, 0.350f, 0.30f);   // 808 RS, noisier
                                        k.rimFreq = 6490.0f;  k.rimTau = 0.090f; }
        // Cross sticks keep their own tones; the stick's hardness sets the bite
        else if (n == "Acoustic Cross Stick") setStruck (k, 0.0f, 0.10f, 0.70f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.30f);
        else if (n == "Woody Click")          setStruck (k, 0.0f, 0.10f, 0.50f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.25f);
        else if (n == "Bright Cross Stick")   setStruck (k, 0.0f, 0.10f, 0.85f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.40f);
        else if (n == "Soft Cross Stick")     setStruck (k, 0.0f, 0.10f, 0.35f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.15f);
        else                                  k.model = 1.0f;
    }
}

inline const std::array<SnarePrototype, 12>& snarePrototypes()
{
    static const std::array<SnarePrototype, 12> bank = []
    {
    std::array<SnarePrototype, 12> b =
    { {
        // 0 — Acoustic Maple: balanced wood snare, crisp wires
        { "Acoustic Maple",
          200.0f, { 1.00f, 1.59f, 2.14f }, { 1.00f, 0.45f, 0.25f }, { 0.160f, 0.090f, 0.060f },
          1.25f, 0.010f,
          0.75f, 4200.0f, 0.90f, 0.55f, 0.170f, 0.0008f,
          0.55f, 5200.0f, 0.0025f,
          0.10f, 1350.0f, 0.025f,
          0.10f, 0.80f },

        // 1 — Acoustic Rock: fatter body, longer wires, some rim
        { "Acoustic Rock",
          180.0f, { 1.00f, 1.59f, 2.14f }, { 1.00f, 0.55f, 0.30f }, { 0.220f, 0.120f, 0.080f },
          1.30f, 0.012f,
          0.85f, 3500.0f, 0.80f, 0.45f, 0.220f, 0.0010f,
          0.60f, 4500.0f, 0.0030f,
          0.25f, 1150.0f, 0.030f,
          0.25f, 0.85f },

        // 2 — Tight Funk: high, short, snappy
        { "Tight Funk",
          240.0f, { 1.00f, 1.59f, 2.14f }, { 1.00f, 0.35f, 0.18f }, { 0.090f, 0.050f, 0.035f },
          1.20f, 0.008f,
          0.65f, 5200.0f, 1.00f, 0.65f, 0.110f, 0.0006f,
          0.70f, 6000.0f, 0.0020f,
          0.20f, 1600.0f, 0.018f,
          0.10f, 0.80f },

        // 3 — Fat Ballad: low, open, long soft wires
        { "Fat Ballad",
          165.0f, { 1.00f, 1.59f, 2.14f }, { 1.00f, 0.60f, 0.35f }, { 0.300f, 0.160f, 0.100f },
          1.20f, 0.015f,
          0.80f, 2800.0f, 0.70f, 0.35f, 0.300f, 0.0015f,
          0.35f, 3600.0f, 0.0035f,
          0.05f, 1000.0f, 0.030f,
          0.12f, 0.85f },

        // 4 — Piccolo: very high and tight
        { "Piccolo",
          330.0f, { 1.00f, 1.59f, 2.14f }, { 1.00f, 0.40f, 0.20f }, { 0.080f, 0.050f, 0.030f },
          1.15f, 0.006f,
          0.70f, 6500.0f, 1.10f, 0.70f, 0.100f, 0.0005f,
          0.75f, 7000.0f, 0.0018f,
          0.30f, 2200.0f, 0.015f,
          0.08f, 0.75f },

        // 5 — Rimshot Crack: rim-forward, loud stick
        { "Rimshot Crack",
          210.0f, { 1.00f, 1.59f, 2.14f }, { 1.00f, 0.50f, 0.30f }, { 0.140f, 0.080f, 0.050f },
          1.30f, 0.010f,
          0.60f, 4000.0f, 0.90f, 0.50f, 0.160f, 0.0007f,
          0.90f, 5500.0f, 0.0025f,
          0.85f, 1450.0f, 0.040f,
          0.30f, 0.80f },

        // 6 — 808 Snare: two tones + highpassed noise
        { "808 Snare",
          180.0f, { 1.00f, 1.85f, 2.60f }, { 1.00f, 0.60f, 0.00f }, { 0.110f, 0.090f, 0.050f },
          1.12f, 0.015f,
          0.70f, 1800.0f, 0.70f, 0.85f, 0.140f, 0.0006f,
          0.15f, 3000.0f, 0.0020f,
          0.00f, 1200.0f, 0.020f,
          0.10f, 0.80f,
          1.0f, 0.0040f, 0.2500f, 0.5000f, 0.8000f },

        // 7 — 909 Snare: pitched-down body, bright snappy noise
        { "909 Snare",
          190.0f, { 1.00f, 1.62f, 2.20f }, { 1.00f, 0.40f, 0.15f }, { 0.070f, 0.050f, 0.035f },
          1.45f, 0.008f,
          0.85f, 3800.0f, 0.80f, 0.75f, 0.160f, 0.0005f,
          0.50f, 5000.0f, 0.0020f,
          0.00f, 1200.0f, 0.020f,
          0.20f, 0.82f },

        // 8 — House Snare: short body, bright wires
        { "House Snare",
          200.0f, { 1.00f, 1.60f, 2.20f }, { 1.00f, 0.35f, 0.15f }, { 0.060f, 0.040f, 0.030f },
          1.35f, 0.008f,
          0.80f, 5000.0f, 0.90f, 0.80f, 0.120f, 0.0005f,
          0.45f, 6000.0f, 0.0020f,
          0.10f, 1800.0f, 0.020f,
          0.25f, 0.80f,
          1.0f, 0.0080f, 0.4500f, 0.4500f, 0.7000f },

        // 9 — Trap Snare: hard, long noise, driven
        { "Trap Snare",
          230.0f, { 1.00f, 1.60f, 2.20f }, { 1.00f, 0.30f, 0.12f }, { 0.070f, 0.050f, 0.030f },
          1.60f, 0.010f,
          0.95f, 4500.0f, 0.70f, 0.90f, 0.200f, 0.0004f,
          0.60f, 6500.0f, 0.0020f,
          0.00f, 1500.0f, 0.020f,
          0.40f, 0.80f },

        // 10 — Lo-fi Snare: dull, dirty
        { "Lo-fi Snare",
          185.0f, { 1.00f, 1.59f, 2.14f }, { 1.00f, 0.50f, 0.30f }, { 0.120f, 0.070f, 0.050f },
          1.20f, 0.012f,
          0.75f, 2400.0f, 0.60f, 0.40f, 0.180f, 0.0010f,
          0.40f, 3000.0f, 0.0035f,
          0.15f, 1000.0f, 0.030f,
          0.80f, 0.78f },

        // 11 — Electro Snap: tiny body, very bright burst
        { "Electro Snap",
          260.0f, { 1.00f, 1.75f, 2.40f }, { 1.00f, 0.25f, 0.10f }, { 0.050f, 0.030f, 0.020f },
          1.80f, 0.006f,
          0.90f, 7000.0f, 1.20f, 0.95f, 0.090f, 0.0003f,
          0.80f, 8000.0f, 0.0015f,
          0.00f, 2000.0f, 0.015f,
          0.35f, 0.78f },
    } };
    applySnareEngines (b);
    return b;
    }();

    return bank;
}

//==============================================================================
//  SIDE STICK bank — key 37
//==============================================================================
inline const std::array<SnarePrototype, 6>& sideStickPrototypes()
{
    static const std::array<SnarePrototype, 6> bank = []
    {
    std::array<SnarePrototype, 6> b =
    { {
        // 0 — Acoustic Cross Stick: stick across the rim, woody crack
        { "Acoustic Cross Stick",
          420.0f, { 1.00f, 1.80f, 2.70f }, { 0.60f, 0.30f, 0.15f }, { 0.020f, 0.012f, 0.008f },
          1.05f, 0.004f,
          0.05f, 3000.0f, 0.80f, 0.50f, 0.030f, 0.0005f,
          0.90f, 3200.0f, 0.0020f,
          1.00f, 1700.0f, 0.035f,
          0.10f, 0.75f },

        // 1 — Woody Click: lower, rounder
        { "Woody Click",
          520.0f, { 1.00f, 1.90f, 2.90f }, { 0.70f, 0.25f, 0.10f }, { 0.018f, 0.010f, 0.006f },
          1.05f, 0.004f,
          0.00f, 3000.0f, 0.80f, 0.50f, 0.020f, 0.0005f,
          0.70f, 2400.0f, 0.0025f,
          0.90f, 1250.0f, 0.045f,
          0.05f, 0.75f },

        // 2 — Bright Cross Stick
        { "Bright Cross Stick",
          480.0f, { 1.00f, 1.80f, 2.70f }, { 0.50f, 0.30f, 0.20f }, { 0.015f, 0.010f, 0.007f },
          1.05f, 0.003f,
          0.08f, 5000.0f, 0.90f, 0.60f, 0.025f, 0.0004f,
          1.00f, 4800.0f, 0.0018f,
          1.00f, 2300.0f, 0.030f,
          0.12f, 0.72f },

        // 3 — Soft Cross Stick: ballad brush-era click
        { "Soft Cross Stick",
          380.0f, { 1.00f, 1.80f, 2.70f }, { 0.60f, 0.25f, 0.10f }, { 0.022f, 0.012f, 0.008f },
          1.03f, 0.004f,
          0.03f, 2500.0f, 0.80f, 0.40f, 0.025f, 0.0006f,
          0.45f, 2600.0f, 0.0025f,
          0.80f, 1500.0f, 0.040f,
          0.05f, 0.70f },

        // 4 — 808 Rimshot: two short high tones
        { "808 Rimshot",
          455.0f, { 1.00f, 2.20f, 3.66f }, { 0.80f, 0.30f, 0.60f }, { 0.012f, 0.008f, 0.010f },
          1.00f, 0.003f,
          0.00f, 3000.0f, 0.80f, 0.50f, 0.020f, 0.0005f,
          0.30f, 3000.0f, 0.0015f,
          0.90f, 1667.0f, 0.012f,
          0.30f, 0.72f },

        // 5 — 909 Rim: bright, short, a touch of noise
        { "909 Rim",
          500.0f, { 1.00f, 2.10f, 3.30f }, { 0.70f, 0.30f, 0.30f }, { 0.010f, 0.007f, 0.006f },
          1.05f, 0.003f,
          0.10f, 6000.0f, 1.00f, 0.80f, 0.015f, 0.0003f,
          0.80f, 6000.0f, 0.0012f,
          0.90f, 2100.0f, 0.015f,
          0.25f, 0.72f },
    } };
    applyStickEngines (b);
    return b;
    }();

    return bank;
}

//==============================================================================
//  Morph — interpolate two prototypes at t (0 = a, 1 = b)
//==============================================================================
inline SnarePrototype morph (const SnarePrototype& a, const SnarePrototype& b, float t)
{
    using drum::lerp;
    using drum::logLerp;
    t = std::clamp (t, 0.0f, 1.0f);

    SnarePrototype r = a;   // name pointer from A — display only
    r.bodyF0 = logLerp (a.bodyF0, b.bodyF0, t);
    for (int m = 0; m < kNumBodyModes; ++m)
    {
        r.bodyRatio[m] = logLerp (a.bodyRatio[m], b.bodyRatio[m], t);
        r.bodyGain [m] = lerp    (a.bodyGain [m], b.bodyGain [m], t);
        r.bodyTau  [m] = logLerp (a.bodyTau  [m], b.bodyTau  [m], t);
    }
    r.pitchRatio = logLerp (a.pitchRatio, b.pitchRatio, t);
    r.pitchTau   = logLerp (a.pitchTau,   b.pitchTau,   t);

    r.wireLevel  = lerp    (a.wireLevel,  b.wireLevel,  t);
    r.wireFreq   = logLerp (a.wireFreq,   b.wireFreq,   t);
    r.wireQ      = logLerp (a.wireQ,      b.wireQ,      t);
    r.wireHpMix  = lerp    (a.wireHpMix,  b.wireHpMix,  t);
    r.wireTau    = logLerp (a.wireTau,    b.wireTau,    t);
    r.wireAttack = logLerp (a.wireAttack, b.wireAttack, t);

    r.clickLevel = lerp    (a.clickLevel, b.clickLevel, t);
    r.clickFreq  = logLerp (a.clickFreq,  b.clickFreq,  t);
    r.clickTau   = logLerp (a.clickTau,   b.clickTau,   t);

    r.rimLevel   = lerp    (a.rimLevel,   b.rimLevel,   t);
    r.rimFreq    = logLerp (a.rimFreq,    b.rimFreq,    t);
    r.rimTau     = logLerp (a.rimTau,     b.rimTau,     t);

    r.drive      = lerp    (a.drive,      b.drive,      t);
    r.level      = lerp    (a.level,      b.level,      t);

    r.onset       = lerp    (a.onset,       b.onset,       t);
    r.bodyScatter = lerp    (a.bodyScatter, b.bodyScatter, t);
    r.wireBuzz    = lerp    (a.wireBuzz,    b.wireBuzz,    t);
    r.wireFast    = lerp    (a.wireFast,    b.wireFast,    t);
    r.clickQ      = logLerp (a.clickQ,      b.clickQ,      t);
    return r;
}

} // namespace snare
