#pragma once
#include <JuceHeader.h>
#include <cmath>

// =============================================================================
//  SliderNorm.h — EVERY SLIDER IN THE SOUND EDITORS READS 0..100.
//
//  The editors grew a slider at a time, and each one took whatever range its
//  parameter happened to use: seconds for one envelope, milliseconds for the
//  next, Hz for the LFOs, 0..1 for the depths.  Six different scales on one
//  screen means the numbers cannot be compared, a value learned on one control
//  teaches nothing about the next, and "half way up" means something different
//  every time.  CUT / RES / KEY / D / R were already 0..100, so the screen was
//  inconsistent with ITSELF.
//
//  These convert between the 0..100 the user sees and the native unit the
//  engine wants.  The parameters are UNCHANGED — a preset written before this
//  still loads and still sounds the same; only the number on screen moved.
//
//  TWO LAWS, chosen per parameter kind:
//
//    LINEAR   for anything already proportional — depths, amounts, sustain,
//             LFO rate.  50 means half, which is the whole point.
//
//    SQUARED  for TIMES.  A linear 0..5 s across 101 steps puts 50 ms between
//             neighbouring positions, and the entire useful range of an attack
//             lives inside the first two of them.  Squaring spends most of the
//             travel below a quarter of the maximum, which is where envelope
//             times are actually set, and still reaches the top.
//
//  Two sliders are deliberately NOT on this scale, and should stay that way:
//
//    GAIN  0..200, 100 = unity.  The scale is load-bearing — the per-sound
//          calibration, the base-unity box and the .ins file all read it as a
//          percentage of unity, and 100 has to sit at the centre detent.
//    OCT   -3..+3, seven discrete steps.  There is no 0..100 version of "two
//          octaves down" that is not worse than the number itself.
// =============================================================================
namespace Betel
{
    struct Norm
    {
        static constexpr float kUi = 100.0f;

        // ── linear 0..1 parameters (depth, amount, sustain) ──────────────────
        static float unitToUi (float native) noexcept
        { return juce::jlimit (0.0f, kUi, native * kUi); }

        static float uiToUnit (float ui) noexcept
        { return juce::jlimit (0.0f, 1.0f, ui / kUi); }

        // ── LFO rate, 0..20 Hz.  Linear: 0.2 Hz a step is fine resolution for
        //    a vibrato, and the slow end is not crowded the way a time is. ────
        static constexpr float kRateMaxHz = 20.0f;

        static float rateToUi (float hz) noexcept
        { return juce::jlimit (0.0f, kUi, hz / kRateMaxHz * kUi); }

        static float uiToRate (float ui) noexcept
        { return juce::jlimit (0.0f, kRateMaxHz, ui / kUi * kRateMaxHz); }

        // ── LFO delay, 0..5000 ms — a TIME, so squared ───────────────────────
        static constexpr float kDelayMaxMs = 5000.0f;

        static float delayToUi (float ms) noexcept { return timeToUi (ms, kDelayMaxMs); }
        static float uiToDelay (float ui) noexcept { return uiToTime (ui, kDelayMaxMs); }

        // ── portamento, 0..2000 ms ───────────────────────────────────────────
        static constexpr float kPortaMaxMs = 2000.0f;

        static float portaToUi (float ms) noexcept { return timeToUi (ms, kPortaMaxMs); }
        static float uiToPorta (float ui) noexcept { return uiToTime (ui, kPortaMaxMs); }

        // ── envelope times, in SECONDS.  Each stage keeps its own maximum so
        //    the full travel of every slider stays useful. ────────────────────
        static constexpr float kAmpAtkMaxS  = 5.0f;
        static constexpr float kFiltAtkMaxS = 5.0f;
        static constexpr float kFiltDecMaxS = 5.0f;
        static constexpr float kFiltRelMaxS = 4.0f;

        /** Floor for the filter envelope stages, which were declared with a
            0.001 s minimum: a squared law reaches 0 exactly, and a zero-length
            filter stage is a click rather than an envelope. */
        static constexpr float kMinEnvS = 0.001f;

        static float envToUi   (float sec, float maxS) noexcept { return timeToUi (sec, maxS); }
        static float uiToEnv   (float ui,  float maxS) noexcept { return uiToTime (ui,  maxS); }

        static float uiToEnvMin (float ui, float maxS) noexcept
        { return juce::jmax (kMinEnvS, uiToTime (ui, maxS)); }

    private:
        // ── the squared law, both directions ─────────────────────────────────
        static float timeToUi (float native, float maxNative) noexcept
        {
            if (maxNative <= 0.0f) return 0.0f;
            const float f = juce::jlimit (0.0f, 1.0f, native / maxNative);
            return juce::jlimit (0.0f, kUi, std::sqrt (f) * kUi);
        }

        static float uiToTime (float ui, float maxNative) noexcept
        {
            const float f = juce::jlimit (0.0f, 1.0f, ui / kUi);
            return f * f * maxNative;
        }
    };
}
