#pragma once

#include <array>
#include <string>
#include <cmath>
#include <algorithm>

//==============================================================================
//  KickPrototypes.h
//
//  The baked prototype bank for the Kick Machine audition.
//  FIXED TOPOLOGY: every prototype fills the exact same parameter set, so
//  morphing between any two prototypes is pure interpolation — no topology
//  switching, no clicks. Frequencies and times interpolate in the log domain,
//  gains and levels interpolate linearly.
//==============================================================================

namespace kick
{

static constexpr int kNumModes = 8;

//==============================================================================
struct KickPrototype
{
    const char* name;

    // Tone layer — modal bank (decaying sines)
    float f0;                          // fundamental, Hz
    float modeRatio [kNumModes];       // frequency ratios relative to f0
    float modeGain  [kNumModes];       // linear gain per mode
    float modeTau   [kNumModes];       // decay time constant per mode, seconds

    // Pitch envelope (applies to the whole modal bank)
    float pitchRatio;                  // start frequency multiplier (1 = none)
    float pitchTau;                    // decay time constant, seconds

    // Click layer — short noise burst through a bandpass
    float clickLevel;                  // 0..1+
    float clickFreq;                   // bandpass centre, Hz
    float clickTau;                    // decay time constant, seconds

    // Noise layer — filtered noise with its own envelope
    float noiseLevel;                  // 0..1
    float noiseCutoff;                 // filter cutoff / centre, Hz
    float noiseMode;                   // 0 = LP, 0.5 = BP, 1 = HP (morphable)
    float noiseTau;                    // decay time constant, seconds

    // Shaping
    float drive;                       // 0..1 baseline saturation
    float level;                       // overall prototype level, linear

    // ---- Drum realism.  Defaulted, so every table entry above gets them. ----
    float onset       = 1.0f;          // 0 = modes rise from zero (soft), 1 = struck (start at their peak)
    float modeScatter = 0.006f;        // per-hit random detune of the upper modes (fraction)
    float fastShare   = 0.30f;         // share of each mode lost in a fast first stage (radiation)
    float fastRatio   = 0.18f;         // fast stage tau = mode tau * fastRatio
    float spike       = 0.0f;          // beater / stick pitch spike at the hit (ratio above 1)
    float spikeTau    = 0.003f;        // s
    float clickQ      = 0.7f;          // stick / beater noise bandwidth (lower = wider, fuller)
    float asym        = 0.12f;         // asymmetric saturation: even harmonics, weight

    // ---- ENGINE (2026-09 rebuild - see THE KICK MODELS in KickVoice.h) --------
    float model     = 0.0f;   // 0 modal, 1 struck resonators, 2 808 circuit, 3 909 circuit
    float decay808  = 0.5f;   // 808: resonator decay, 0..1
    float tone808   = 0.3f;   // 808: tone (exciter leak + output low-pass), 0..1
    float attackFm  = 0.4f;   // 808: pitch lift during the first 6 ms
    float selfFm    = 0.4f;   // 808: pitch follows the resonator's own level
    float decay909  = 0.5f;   // 909: body decay, 0..1
    float tone909   = 0.5f;   // 909: tone and click level, 0..1
    float dirt      = 0.2f;   // 909: triangle-shaper dirt + phase drift, 0..1
    float fmAmount  = 0.5f;   // 909: pitch sweep depth
    float fmDecay   = 0.3f;   // 909: pitch sweep time, 0..1
    float tension   = 0.3f;   // struck: pitch glide with the head's energy (tension modulation)
    float hardness  = 0.5f;   // struck: beater hardness = contact time (soft 4 ms .. hard 0.25 ms)
    float bodyClick = 0.3f;   // struck: click made from the body's own motion
    float vcaDrive  = 0.3f;   // struck: level-dependent VCA saturation + sub thump
    float strikeFast    = 0.0f;   // struck: share of the level lost in a fast first drop (real heads do)
    float strikeFastTau = 0.05f;  // struck: time of that drop, seconds

    // ---- SYNTH model (model 4): the produced kick - see THE KICK MODELS ----
    float sweepOct   = 3.0f;    // synth: the sweep starts this many octaves above f0 (its end pitch)
    float sweepFast  = 0.008f;  // synth: the fast dive, seconds
    float sweepSlow  = 0.06f;   // synth: the slow glide, seconds
    float sweepMix   = 0.4f;    // synth: share of the sweep left for the slow glide, 0..1
    float holdTime   = 0.03f;   // synth: the body holds at full level, seconds
    float bodyDecay  = 0.08f;   // synth: the fall after the hold, seconds
    float tailLevel  = 0.15f;   // synth: share of the level left on the quiet tail, 0..1
    float tailDecay  = 0.25f;   // synth: the tail's fade, seconds
    float startPhase = 0.0f;    // synth: 0 = the sine starts at zero (soft), 1 = at its peak (hard)
    float gateTime   = 0.0f;    // synth: LENGTH - the kick is cut off here with a quick release (0 = off)
};

//==============================================================================
//  The bank. Values authored as starting points for the audition — the whole
//  point of this build is to judge them by ear and iterate.
//==============================================================================
//==============================================================================
//  THE FITTED PRODUCED KICKS - the last six slots.  Four on the SYNTH model
//  (model 4, the produced EDM kick in KickVoice.h) and two on the 909 circuit,
//  each FITTED to a produced reference kick: the dive, the hold, the cut-off,
//  the tail, the harmonics and the click.
//==============================================================================
inline void setSynth (KickPrototype& k, const char* name, float f0, float oct, float fast, float slow, float mix,
                      float hold, float bodyDecay, float tail, float tailDecay, float phase,
                      float drive, float asym, float click, float clickFreq, float clickTau, float gate)
{
    k.name = name;           k.model = 4.0f;          k.f0 = f0;
    k.sweepOct = oct;        k.sweepFast = fast;      k.sweepSlow = slow;      k.sweepMix = mix;
    k.holdTime = hold;       k.bodyDecay = bodyDecay; k.tailLevel = tail;      k.tailDecay = tailDecay;
    k.startPhase = phase;    k.drive = drive;         k.asym = asym;
    k.clickLevel = click;    k.clickFreq = clickFreq; k.clickTau = clickTau;   k.gateTime = gate;
}

inline void set909 (KickPrototype& k, const char* name, float f0, float decay, float tone, float dirt,
                    float fm, float fmDecay, float drive, float asym)
{
    k.name = name;       k.model = 3.0f;       k.f0 = f0;
    k.decay909 = decay;  k.tone909 = tone;     k.dirt = dirt;
    k.fmAmount = fm;     k.fmDecay = fmDecay;  k.drive = drive;   k.asym = asym;
}

inline void addSynthKicks (std::array<KickPrototype, 18>& b)
{
    for (size_t i = 12; i < 18; ++i) b[i] = b[2];        // shared fields from 909 Punch
    //                    name          f0     oct    fast     slow    mix     hold     decay    tail    tailDec  phase   drive   asym    click  clkFreq   clkTau   gate
    setSynth (b[12], "Deep Punch", 39.7f, 4.00f, 0.0284f, 0.0134f, 0.380f, 0.0461f, 0.0811f, 0.059f, 0.823f, 0.992f, 0.091f, 0.264f, 0.981f, 1640.0f, 0.0185f, 0.265f);
    setSynth (b[13], "Big Room",   52.4f, 4.00f, 0.0140f, 0.1840f, 0.310f, 0.0774f, 0.0479f, 0.095f, 0.469f, 0.852f, 0.156f, 0.988f, 0.456f, 5580.0f, 0.0032f, 0.089f);
    setSynth (b[14], "Long Dive",  36.4f, 3.85f, 0.0235f, 0.2180f, 0.071f, 0.0045f, 0.0289f, 0.450f, 0.134f, 0.168f, 0.334f, 0.024f, 1.150f, 7370.0f, 0.0137f, 0.526f);
    setSynth (b[15], "Sub Drop",   22.5f, 4.44f, 0.0142f, 0.0759f, 0.552f, 0.0650f, 0.0030f, 0.380f, 0.888f, 0.000f, 0.000f, 0.053f, 0.313f, 1500.0f, 0.0010f, 0.000f);
    //                 name           f0     decay   tone    dirt    fm      fmDecay drive   asym
    set909   (b[16], "909 Classic", 56.8f, 0.561f, 0.297f, 0.765f, 0.913f, 0.925f, 0.158f, 0.384f);
    set909   (b[17], "909 Round",   53.4f, 0.607f, 0.493f, 0.251f, 1.000f, 0.350f, 0.000f, 0.540f);

    // Level-matched to the other kicks (the same peak through the voice)
    static constexpr float kLevel[6] = { 0.81f, 0.61f, 1.21f, 0.83f, 0.81f, 0.67f };
    for (size_t i = 0; i < 6; ++i) b[12 + i].level *= kLevel[i];
}

//==============================================================================
//  Each kick's engine and settings (KickVoice.h, THE KICK MODELS).  The
//  settings are FITTED to reference recordings of the drums they stand for -
//  pitch trajectory, decay and brightness measured, then matched.
//==============================================================================
inline void applyEngines (std::array<KickPrototype, 18>& b)
{
    // THE SYNTH KICKS (model 4) take the last four slots, named FIRST - every
    // line below reads the names.  Their settings come from addSynthKicks.
    addSynthKicks (b);

    // tauScale multiplies the kick's own mode decay times (struck model)
    auto struck = [] (KickPrototype& k, float f0, float tension, float hardness, float tauScale,
                      float pitchRatio, float pitchTau, float bodyClick, float vcaDrive)
    {
        k.model = 1.0f;
        if (f0 > 0.0f) k.f0 = f0;
        k.tension = tension;   k.hardness = hardness;
        for (auto& t : k.modeTau) t *= tauScale;
        k.pitchRatio = pitchRatio;  k.pitchTau = pitchTau;
        k.bodyClick = bodyClick;    k.vcaDrive = vcaDrive;
    };
    auto a808 = [] (KickPrototype& k, float f0, float decay, float tone, float attackFm, float selfFm)
    {
        k.model = 2.0f; k.f0 = f0; k.decay808 = decay; k.tone808 = tone; k.attackFm = attackFm; k.selfFm = selfFm;
    };
    auto s909 = [] (KickPrototype& k, float f0, float decay, float tone, float dirt, float fmAmount, float fmDecay)
    {
        k.model = 3.0f; k.f0 = f0; k.decay909 = decay; k.tone909 = tone; k.dirt = dirt; k.fmAmount = fmAmount; k.fmDecay = fmDecay;
    };

    for (auto& k : b)
    {
        const std::string n (k.name);
        //                                     f0      decay  tone   attFM  selfFM          fitted to
        if      (n == "808 Sub")      { a808 (k, 50.00f, 0.662f, 0.528f, 0.000f, 2.000f); k.level *= 1.4f; }  // TR-808 BD 50/50
        else if (n == "808 Long Boom")  a808 (k, 52.19f, 0.784f, 0.560f, 1.118f, 0.249f);   // TR-808 BD 00/10 (long)
        else if (n == "Trap Boom")      a808 (k, 50.58f, 0.688f, 0.000f, 1.334f, 0.474f);   // TR-808 BD 75/75
        //                                     f0      decay  tone   dirt   fmAmt  fmDecay
        else if (n == "909 Punch")      s909 (k, 50.50f, 0.474f, 0.994f, 0.100f, 1.028f, 0.000f);   // TR-909 BD, classic
        else if (n == "Deep House")     s909 (k, 49.62f, 0.589f, 1.000f, 1.000f, 1.045f, 0.000f);   // TR-909 BD, long decay
        else if (n == "Hardstyle")      s909 (k, 48.37f, 0.461f, 0.560f, 0.366f, 1.038f, 0.587f);   // TR-909 BD, tuned up
        //                                        f0      tension hard   tau    pRatio pTau    bClick vca
        else if (n == "Acoustic Jazz 18") struck (k, 43.32f, 2.584f, 0.016f, 0.086f, 1.810f, 0.100f, 0.840f, 0.142f);  // jazz kit BD
        else if (n == "Vintage R&B")      struck (k, 60.28f, 0.796f, 0.528f, 0.050f, 1.600f, 0.015f, 0.269f, 0.282f);  // LinnDrum kick
        else if (n == "Acoustic Rock 22") struck (k, 61.55f, 2.027f, 0.200f, 0.139f, 1.632f, 0.043f, 0.730f, 0.490f);  // acoustic kick
        // No reference of their own: derived from the fitted kicks above
        else if (n == "Beater Head")    { struck (k, 0.0f,   2.000f, 0.750f, 0.120f, 1.600f, 0.030f, 0.900f, 0.400f);  // Rock, harder beater
                                          k.noiseTau = 0.020f; }     // the beater's noise stays on the attack
        else if (n == "Tight Pop")        struck (k, 0.0f,   1.500f, 0.600f, 0.090f, 1.800f, 0.025f, 0.700f, 0.500f);  // Rock, shorter + VCA
        else if (n == "Lo-fi Crunch")   { struck (k, 0.0f,   0.800f, 0.400f, 0.070f, 1.500f, 0.020f, 0.400f, 0.700f);  // R&B, more crunch
                                          k.noiseTau = 0.025f; }
    }
}

inline const std::array<KickPrototype, 18>& prototypes()
{
    static const std::array<KickPrototype, 18> bank = []
    {
    std::array<KickPrototype, 18> b =
    { {
        // 0 — 808 Sub: one sine, deep sweep, almost no click
        { "808 Sub",
          47.0f,
          { 1.00f, 2.00f, 3.00f, 4.20f, 5.40f, 6.80f, 8.40f, 10.0f },
          { 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.95f, 0.10f, 0.07f, 0.06f, 0.05f, 0.05f, 0.04f, 0.04f },
          4.5f, 0.032f,
          0.18f, 1800.0f, 0.0040f,
          0.03f, 300.0f, 0.0f, 0.050f,
          0.12f, 0.90f,
          1.0f, 0.0000f, 0.0000f, 0.18f, 0.3500f, 0.0020f, 0.7000f, 0.1500f },

        // 1 — 808 Long Boom: lower, much longer tail
        { "808 Long Boom",
          41.0f,
          { 1.00f, 2.00f, 3.00f, 4.20f, 5.40f, 6.80f, 8.40f, 10.0f },
          { 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 1.70f, 0.12f, 0.08f, 0.06f, 0.05f, 0.05f, 0.04f, 0.04f },
          6.0f, 0.045f,
          0.12f, 1500.0f, 0.0030f,
          0.02f, 300.0f, 0.0f, 0.060f,
          0.18f, 0.90f,
          1.0f, 0.0000f, 0.0000f, 0.18f, 0.3000f, 0.0020f, 0.7000f, 0.1500f },

        // 2 — 909 Punch: harder sweep, strong click, a bit of dirt
        { "909 Punch",
          51.0f,
          { 1.00f, 1.70f, 2.60f, 3.60f, 4.80f, 6.00f, 7.50f, 9.00f },
          { 1.00f, 0.18f, 0.05f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.30f, 0.12f, 0.06f, 0.05f, 0.04f, 0.04f, 0.03f, 0.03f },
          7.0f, 0.020f,
          0.80f, 2700.0f, 0.0050f,
          0.12f, 900.0f, 0.5f, 0.050f,
          0.35f, 0.95f,
          1.0f, 0.0060f, 0.2500f, 0.18f, 1.4000f, 0.0025f, 0.6000f, 0.2000f },

        // 3 — Acoustic Rock 22": membrane mode ladder, beater click, shell noise
        { "Acoustic Rock 22",
          56.0f,
          { 1.00f, 1.53f, 2.29f, 2.92f, 3.62f, 4.30f, 5.10f, 6.05f },
          { 1.00f, 0.55f, 0.34f, 0.22f, 0.15f, 0.10f, 0.07f, 0.05f },
          { 0.32f, 0.18f, 0.11f, 0.08f, 0.06f, 0.045f, 0.035f, 0.028f },
          1.7f, 0.013f,
          0.55f, 3600.0f, 0.0030f,
          0.22f, 2000.0f, 0.7f, 0.060f,
          0.18f, 0.95f,
          1.0f, 0.0060f, 0.3500f, 0.18f, 0.7000f, 0.0030f, 0.5500f, 0.1500f },

        // 4 — Acoustic Jazz 18": smaller, open, resonant, gentle beater
        { "Acoustic Jazz 18",
          74.0f,
          { 1.00f, 1.53f, 2.29f, 2.92f, 3.62f, 4.30f, 5.10f, 6.05f },
          { 1.00f, 0.60f, 0.42f, 0.30f, 0.20f, 0.14f, 0.10f, 0.07f },
          { 0.50f, 0.30f, 0.20f, 0.14f, 0.10f, 0.08f, 0.06f, 0.05f },
          1.4f, 0.012f,
          0.30f, 3000.0f, 0.0025f,
          0.12f, 1800.0f, 0.6f, 0.080f,
          0.08f, 0.90f,
          1.0f, 0.0060f, 0.3000f, 0.18f, 0.4000f, 0.0030f, 0.6000f, 0.1000f },

        // 5 — Tight Pop: short, controlled, present click
        { "Tight Pop",
          59.0f,
          { 1.00f, 1.53f, 2.29f, 2.92f, 3.62f, 4.30f, 5.10f, 6.05f },
          { 1.00f, 0.35f, 0.15f, 0.08f, 0.04f, 0.02f, 0.01f, 0.01f },
          { 0.14f, 0.08f, 0.05f, 0.04f, 0.03f, 0.03f, 0.02f, 0.02f },
          2.2f, 0.012f,
          0.60f, 3800.0f, 0.0030f,
          0.15f, 1200.0f, 0.5f, 0.040f,
          0.22f, 0.95f,
          1.0f, 0.0060f, 0.4000f, 0.18f, 0.9000f, 0.0025f, 0.6000f, 0.1500f },

        // 6 — Trap Boom: long saturated sub with slow sweep
        { "Trap Boom",
          43.0f,
          { 1.00f, 2.00f, 3.00f, 4.20f, 5.40f, 6.80f, 8.40f, 10.0f },
          { 1.00f, 0.12f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 1.00f, 0.20f, 0.08f, 0.06f, 0.05f, 0.05f, 0.04f, 0.04f },
          3.2f, 0.050f,
          0.30f, 2200.0f, 0.0040f,
          0.05f, 500.0f, 0.0f, 0.120f,
          0.40f, 0.95f,
          1.0f, 0.0000f, 0.0000f, 0.18f, 0.5000f, 0.0030f, 0.7000f, 0.2500f },

        // 7 — Hardstyle: violent sweep, heavy drive, distorted tail
        { "Hardstyle",
          62.0f,
          { 1.00f, 1.70f, 2.60f, 3.60f, 4.80f, 6.00f, 7.50f, 9.00f },
          { 1.00f, 0.40f, 0.25f, 0.15f, 0.10f, 0.06f, 0.04f, 0.02f },
          { 0.35f, 0.20f, 0.12f, 0.08f, 0.06f, 0.05f, 0.04f, 0.03f },
          12.0f, 0.016f,
          0.70f, 3000.0f, 0.0050f,
          0.25f, 1500.0f, 0.5f, 0.150f,
          0.85f, 0.95f,
          1.0f, 0.0060f, 0.2000f, 0.18f, 1.8000f, 0.0030f, 0.6000f, 0.3000f },

        // 8 — Deep House: round, warm, soft attack
        { "Deep House",
          46.0f,
          { 1.00f, 2.00f, 3.00f, 4.20f, 5.40f, 6.80f, 8.40f, 10.0f },
          { 1.00f, 0.20f, 0.06f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
          { 0.42f, 0.15f, 0.07f, 0.05f, 0.04f, 0.04f, 0.03f, 0.03f },
          2.6f, 0.028f,
          0.35f, 2400.0f, 0.0040f,
          0.06f, 700.0f, 0.2f, 0.060f,
          0.20f, 0.92f,
          1.0f, 0.0040f, 0.1500f, 0.18f, 0.6000f, 0.0030f, 0.7000f, 0.1500f },

        // 9 — Beater Head: click-forward acoustic, tight damped shell
        { "Beater Head",
          57.0f,
          { 1.00f, 1.53f, 2.29f, 2.92f, 3.62f, 4.30f, 5.10f, 6.05f },
          { 1.00f, 0.50f, 0.30f, 0.20f, 0.13f, 0.09f, 0.06f, 0.04f },
          { 0.20f, 0.11f, 0.07f, 0.05f, 0.04f, 0.03f, 0.025f, 0.02f },
          1.6f, 0.011f,
          1.00f, 4300.0f, 0.0035f,
          0.30f, 3000.0f, 0.9f, 0.060f,
          0.15f, 0.95f,
          1.0f, 0.0060f, 0.4000f, 0.18f, 1.0000f, 0.0025f, 0.5000f, 0.1500f },

        // 10 — Lo-fi Crunch: noisy, dirty, short
        { "Lo-fi Crunch",
          49.0f,
          { 1.00f, 1.70f, 2.60f, 3.60f, 4.80f, 6.00f, 7.50f, 9.00f },
          { 1.00f, 0.30f, 0.18f, 0.10f, 0.06f, 0.03f, 0.02f, 0.01f },
          { 0.20f, 0.10f, 0.06f, 0.05f, 0.04f, 0.03f, 0.03f, 0.02f },
          2.8f, 0.020f,
          0.50f, 2000.0f, 0.0060f,
          0.45f, 1100.0f, 0.5f, 0.090f,
          0.95f, 0.90f,
          1.0f, 0.0060f, 0.3000f, 0.18f, 0.6000f, 0.0030f, 0.6000f, 0.3000f },

        // 11 — Vintage R&B: warm, mid-length, second mode audible
        { "Vintage R&B",
          50.0f,
          { 1.00f, 1.53f, 2.29f, 2.92f, 3.62f, 4.30f, 5.10f, 6.05f },
          { 1.00f, 0.45f, 0.20f, 0.10f, 0.05f, 0.03f, 0.02f, 0.01f },
          { 0.50f, 0.25f, 0.12f, 0.07f, 0.05f, 0.04f, 0.03f, 0.03f },
          1.9f, 0.020f,
          0.40f, 2600.0f, 0.0030f,
          0.10f, 900.0f, 0.4f, 0.070f,
          0.30f, 0.92f,
          1.0f, 0.0060f, 0.3000f, 0.18f, 0.5000f, 0.0030f, 0.6000f, 0.1500f },
    } };
    applyEngines (b);
    return b;
    }();

    return bank;
}

inline int numPrototypes() { return (int) prototypes().size(); }

//==============================================================================
//  Morph helpers
//==============================================================================
inline float lerp (float a, float b, float t)      { return a + (b - a) * t; }

// Log-domain interpolation for frequencies and time constants.
// Both inputs are clamped away from zero so the ratio is always defined.
inline float logLerp (float a, float b, float t)
{
    const float lo = 1.0e-5f;
    a = std::max (a, lo);
    b = std::max (b, lo);
    return a * std::pow (b / a, t);
}

// Interpolate two prototypes at position t (0 = a, 1 = b).
inline KickPrototype morph (const KickPrototype& a, const KickPrototype& b, float t)
{
    t = std::clamp (t, 0.0f, 1.0f);

    KickPrototype r = a;   // copies the name pointer of A; name is display-only
    r.f0 = logLerp (a.f0, b.f0, t);

    for (int m = 0; m < kNumModes; ++m)
    {
        r.modeRatio[m] = logLerp (a.modeRatio[m], b.modeRatio[m], t);
        r.modeGain [m] = lerp    (a.modeGain [m], b.modeGain [m], t);
        r.modeTau  [m] = logLerp (a.modeTau  [m], b.modeTau  [m], t);
    }

    r.pitchRatio  = logLerp (a.pitchRatio,  b.pitchRatio,  t);
    r.pitchTau    = logLerp (a.pitchTau,    b.pitchTau,    t);

    r.clickLevel  = lerp    (a.clickLevel,  b.clickLevel,  t);
    r.clickFreq   = logLerp (a.clickFreq,   b.clickFreq,   t);
    r.clickTau    = logLerp (a.clickTau,    b.clickTau,    t);

    r.noiseLevel  = lerp    (a.noiseLevel,  b.noiseLevel,  t);
    r.noiseCutoff = logLerp (a.noiseCutoff, b.noiseCutoff, t);
    r.noiseMode   = lerp    (a.noiseMode,   b.noiseMode,   t);
    r.noiseTau    = logLerp (a.noiseTau,    b.noiseTau,    t);

    r.drive       = lerp    (a.drive,       b.drive,       t);
    r.level       = lerp    (a.level,       b.level,       t);

    r.onset       = lerp    (a.onset,       b.onset,       t);
    r.modeScatter = lerp    (a.modeScatter, b.modeScatter, t);
    r.fastShare   = lerp    (a.fastShare,   b.fastShare,   t);
    r.fastRatio   = logLerp (a.fastRatio,   b.fastRatio,   t);
    r.spike       = lerp    (a.spike,       b.spike,       t);
    r.spikeTau    = logLerp (a.spikeTau,    b.spikeTau,    t);
    r.clickQ      = logLerp (a.clickQ,      b.clickQ,      t);
    r.asym        = lerp    (a.asym,        b.asym,        t);

    // Engine fields: the model is taken from the nearer end, the rest blend
    r.model     = (t < 0.5f) ? a.model : b.model;
    r.decay808  = lerp (a.decay808,  b.decay808,  t);  r.tone808   = lerp (a.tone808,   b.tone808,   t);
    r.attackFm  = lerp (a.attackFm,  b.attackFm,  t);  r.selfFm    = lerp (a.selfFm,    b.selfFm,    t);
    r.decay909  = lerp (a.decay909,  b.decay909,  t);  r.tone909   = lerp (a.tone909,   b.tone909,   t);
    r.dirt      = lerp (a.dirt,      b.dirt,      t);  r.fmAmount  = lerp (a.fmAmount,  b.fmAmount,  t);
    r.fmDecay   = lerp (a.fmDecay,   b.fmDecay,   t);  r.tension   = lerp (a.tension,   b.tension,   t);
    r.hardness  = lerp (a.hardness,  b.hardness,  t);  r.bodyClick = lerp (a.bodyClick, b.bodyClick, t);
    r.vcaDrive  = lerp (a.vcaDrive,  b.vcaDrive,  t);
    r.strikeFast    = lerp (a.strikeFast,    b.strikeFast,    t);
    r.strikeFastTau = lerp (a.strikeFastTau, b.strikeFastTau, t);
    r.sweepOct   = lerp (a.sweepOct,   b.sweepOct,   t);
    r.sweepFast  = lerp (a.sweepFast,  b.sweepFast,  t);
    r.sweepSlow  = lerp (a.sweepSlow,  b.sweepSlow,  t);
    r.sweepMix   = lerp (a.sweepMix,   b.sweepMix,   t);
    r.holdTime   = lerp (a.holdTime,   b.holdTime,   t);
    r.bodyDecay  = lerp (a.bodyDecay,  b.bodyDecay,  t);
    r.tailLevel  = lerp (a.tailLevel,  b.tailLevel,  t);
    r.tailDecay  = lerp (a.tailDecay,  b.tailDecay,  t);
    r.startPhase = lerp (a.startPhase, b.startPhase, t);
    r.gateTime   = lerp (a.gateTime,   b.gateTime,   t);
    return r;
}

} // namespace kick
