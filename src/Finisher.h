#pragma once
//==============================================================================
// Finisher.h  —  master-bus "finisher" for Betelgeuse / Grex.
//
// A mastering-style chain tuned for LIVE arranger playing rather than for
// offline mastering.  A CHARACTER selector picks the flavour and AMOUNT is a
// DRY/WET blend across the whole tone-and-dynamics section.
//
// AMOUNT used to SCALE every depth parameter instead, which is why the chain
// felt inert: at the shipped 50% the already-gentle CLEAN preset resolved to
// EQ LOW 0.25 dB, AIR 0.75 dB, glue 1.20:1 and width 1.06 - below audibility
// on five of the seven stages, while the ceiling (the one control AMOUNT never
// touched) went on doing obvious work.  The sliders are absolute now: what you
// set is what the wet path does, and AMOUNT decides how much of it you hear.
//
// ── Signal chain ─────────────────────────────────────────────────────────────
//   1. Rumble HPF          — sub junk below ~25 Hz eats headroom and is
//                            inaudible on any stage rig.  LIVE lifts it higher.
//   2. Tilt / Air EQ       — low shelf + high shelf.  The "polish".
//   3. Crossover @ ~120 Hz — split once, used by stages 4 and 5.
//        low  -> mono-fold + sub weight (soft saturation on the low band adds
//                harmonics the ear reads as WEIGHT without extra peak level)
//        high -> M/S stereo width (bass stays mono, so the mix survives a
//                mono PA — a real concern live, not a theoretical one)
//   4. Glue compressor     — slow attack, DUAL-STAGE release, soft knee, ~2:1.
//   5. Auto-level          — see "density adaptation" below.
//   6. Saturation          — tanh drive.  Harmonics raise PERCEIVED loudness
//                            without raising peaks; this is what actually makes
//                            it sound loud, not the ceiling stage.
//   6b. AMOUNT dry/wet    — blends stages 1-6 against the untouched input.
//   7. Soft-clip ceiling   — the backstop, at kCeilingDb.  ALWAYS fully wet,
//                            so AMOUNT 0 is the limiter alone, not a bypass;
//                            the enable switch is the bypass.
//   8. Output trim         — after the blend and the ceiling, so it trims what
//                            actually leaves the plugin.
//
// ── Zero latency, by design ─────────────────────────────────────────────────
// There is NO lookahead and NO oversampling anywhere in this file, so the
// reported latency is exactly 0 samples.  A true brickwall limiter needs
// lookahead, and a player feels anything past a few ms; oversampling filters
// add their own delay.  Instead the ceiling is a smooth soft-clipper fed by a
// fast feedback peak-follower: the follower does the musical level control, and
// the clipper catches whatever the (lookahead-free) follower overshoots.  The
// trade is a little harmonic content on transients instead of a little delay —
// the right trade on a stage.
//
// Aliasing: the nonlinearities are tanh-based, whose harmonics roll off far
// faster than a hard clip, so at finisher drive levels this stays musical
// without oversampling.  If bright material ever sounds gritty, the fix is to
// wrap stages 6-7 in 2x polyphase-IIR oversampling and accept its small latency
// — deliberately NOT done here, because zero latency was the requirement.
//
// ── Density adaptation (the arranger-specific part) ─────────────────────────
// An arranger jumps from a two-voice intro to eight parts plus drums on one
// beat.  A fixed threshold leaves the intro untouched and crushes the main
// section.  So a slow RMS estimate of the input drives an auto-level stage that
// targets a consistent loudness: sparse passages get lifted, dense ones get
// held, and the perceived level stays put across sections.  Its range is capped
// (kAutoLevelMinGain / Max) so it can never run away or pump.
//
// Real-time safe: no allocation, no locks, no file or string work in process().
// Parameters arrive as atomics from the message thread and are smoothed.
//==============================================================================

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace Betel
{

class Finisher
{
public:
    // kCustom is not a preset — it is what the CHARACTER selector shows once a
    // slider has been touched, so the user always knows the preset is no longer
    // being followed verbatim.  loadCharacter() never loads it.
    enum Character { kClean = 0, kWarm, kBright, kFat, kLive, kCustom, kNumCharacters };

    /** Stages that can be individually bypassed from the window. */
    enum Stage { kStageHpf = 0, kStageEq, kStageBass, kStageWidth,
                 kStageGlue, kStageAuto, kStageDrive, kNumStages };

    /** Editable parameters, one per slider. */
    enum ParamId { pHpfHz = 0, pEqLow, pEqAir, pXoverHz, pBassWeight, pWidth,
                   pGlue, pAutoDepth, pDrive, pCeilingDb, pOutTrimDb, kNumParams };

    //==========================================================================
    // Parameter metadata.  The WINDOW builds its rows from these, so a range can
    // never drift out of sync between the UI and the DSP — there is exactly one
    // definition of each, and it lives here next to the code that uses it.
    //==========================================================================
    static const char* paramName (int id) noexcept
    {
        switch (id)
        {
            case pHpfHz:      return "HPF";
            case pEqLow:      return "EQ - low";
            case pEqAir:      return "EQ - air";
            case pXoverHz:    return "BASS - xover";
            case pBassWeight: return "BASS - weight";
            case pWidth:      return "WIDTH";
            case pGlue:       return "GLUE";
            case pAutoDepth:  return "AUTO LEVEL";
            case pDrive:      return "DRIVE";
            case pCeilingDb:  return "CEILING";
            case pOutTrimDb:  return "OUTPUT";
            default:          return "?";
        }
    }

    static const char* paramSuffix (int id) noexcept
    {
        switch (id)
        {
            case pHpfHz: case pXoverHz:                 return " Hz";
            case pEqLow: case pEqAir:
            case pCeilingDb: case pOutTrimDb:           return " dB";
            case pBassWeight: case pWidth:
            case pGlue: case pAutoDepth:                return " %";
            default:                                     return "";
        }
    }

    static void paramRange (int id, float& lo, float& hi, float& step) noexcept
    {
        switch (id)
        {
            case pHpfHz:      lo = 20.0f;  hi = 80.0f;  step = 1.0f;   break;
            case pEqLow:      lo = -6.0f;  hi =  6.0f;  step = 0.1f;   break;
            case pEqAir:      lo = -6.0f;  hi =  6.0f;  step = 0.1f;   break;
            case pXoverHz:    lo = 60.0f;  hi = 250.0f; step = 1.0f;   break;
            case pBassWeight: lo = 0.0f;   hi = 100.0f; step = 1.0f;   break;
            case pWidth:      lo = 0.0f;   hi = 200.0f; step = 1.0f;   break;
            case pGlue:       lo = 0.0f;   hi = 100.0f; step = 1.0f;   break;
            case pAutoDepth:  lo = 0.0f;   hi = 100.0f; step = 1.0f;   break;
            case pDrive:      lo = 1.0f;   hi =  3.0f;  step = 0.05f;  break;
            // The ceiling can be lowered for extra headroom but never disabled —
            // it is the guarantee the whole zero-latency design rests on.
            case pCeilingDb:  lo = -3.0f;  hi = -0.1f;  step = 0.1f;   break;
            case pOutTrimDb:  lo = -12.0f; hi = 12.0f;  step = 0.1f;   break;
            default:          lo = 0.0f;   hi = 1.0f;   step = 0.01f;  break;
        }
    }

    /** Which stage's toggle owns a parameter (-1 = always active). */
    static int stageForParam (int id) noexcept
    {
        switch (id)
        {
            case pHpfHz:                    return kStageHpf;
            case pEqLow: case pEqAir:       return kStageEq;
            case pXoverHz: case pBassWeight:return kStageBass;
            case pWidth:                    return kStageWidth;
            case pGlue:                     return kStageGlue;
            case pAutoDepth:                return kStageAuto;
            case pDrive:                    return kStageDrive;
            default:                        return -1;   // ceiling / output
        }
    }

    static const char* stageName (int s) noexcept
    {
        switch (s)
        {
            case kStageHpf:   return "HPF";
            case kStageEq:    return "EQ";
            case kStageBass:  return "BASS";
            case kStageWidth: return "WIDTH";
            case kStageGlue:  return "GLUE";
            case kStageAuto:  return "AUTO LEVEL";
            case kStageDrive: return "DRIVE";
            default:          return "?";
        }
    }

    Finisher() { loadCharacter (kClean); }

    //==========================================================================
    // Message thread
    //==========================================================================
    void prepare (double newSampleRate, int /*maxBlockSize*/)
    {
        sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;

        // Smoothing / detector coefficients (one-pole time constants).
        rmsCoeff        = coeffFor (kRmsTimeMs);
        autoLevelCoeff  = coeffFor (kAutoLevelTimeMs);
        primeCoeff      = coeffFor (kAutoLevelPrimeMs);
        attackCoeff     = coeffFor (kGlueAttackMs);
        relFastCoeff    = coeffFor (kGlueReleaseFastMs);
        relSlowCoeff    = coeffFor (kGlueReleaseSlowMs);
        peakRelCoeff    = coeffFor (kPeakReleaseMs);
        paramCoeff      = coeffFor (kParamSmoothMs);

        reset();
        updateFilters();
    }

    void reset()
    {
        for (auto& s : hpfState)      s = {};
        for (auto& s : lowShelfState) s = {};
        for (auto& s : hiShelfState)  s = {};
        for (auto& s : lpState)       s = {};

        autoLevelGain = 1.0f;
        // Unprimed, with the detector seeded NEUTRAL — see the priming window in
        // process().  rmsEst must start at the target, not near zero: from near
        // zero the first sample reads as silence and asks for the +12 dB clamp.
        autoLevelPrimed        = false;
        autoLevelSilentSamples = 0;
        autoLevelPrimeSamples  = 0;
        glueEnv       = 0.0f;
        peakEnv       = 0.0f;
        grDb.store (0.0f);

        const Resolved r = resolve();
        rmsEst      = r.targetRms * r.targetRms;   // neutral opening target
        smDrive     = r.drive;
        smWidth     = r.width;
        smSubWeight = r.bassWeight;
        smGlue      = r.glue;
        smOutGain   = r.outGain;
        smMix       = r.mix;
    }

    void setEnabled   (bool b)   noexcept { enabled.store (b); }
    bool isEnabled    () const   noexcept { return enabled.load(); }

    /** 0..100 — one macro that scales the whole chain. */
    void setAmount    (float pct) noexcept
    {
        amount.store (juce::jlimit (0.0f, 100.0f, pct));
        dirty.store (true);
    }
    float getAmount   () const   noexcept { return amount.load(); }

    /** Selecting a character LOADS its values into the sliders (so the window
        shows exactly what is running).  kCustom is never loaded — it is only
        ever set as a side effect of touching a slider. */
    void setCharacter (int c) noexcept
    {
        c = juce::jlimit (0, (int) kNumCharacters - 1, c);
        character.store (c);
        if (c != kCustom) loadCharacter (c);
        dirty.store (true);
    }
    int  getCharacter () const noexcept { return character.load(); }

    void  setParam (int id, float v) noexcept
    {
        if (id < 0 || id >= kNumParams) return;
        float lo, hi, st; paramRange (id, lo, hi, st);
        params[(size_t) id].store (juce::jlimit (lo, hi, v));
        dirty.store (true);
    }
    float getParam (int id) const noexcept
    {
        if (id < 0 || id >= kNumParams) return 0.0f;
        return params[(size_t) id].load();
    }

    void setStageEnabled (int stage, bool on) noexcept
    {
        if (stage < 0 || stage >= kNumStages) return;
        stageOn[(size_t) stage].store (on);
        dirty.store (true);
    }
    bool getStageEnabled (int stage) const noexcept
    {
        if (stage < 0 || stage >= kNumStages) return false;
        return stageOn[(size_t) stage].load();
    }

    /** Write a character preset into the parameter set.  Message thread. */
    void loadCharacter (int c) noexcept
    {
        const Preset p = presetFor (c);
        setParamRaw (pHpfHz,      p.hpfHz);
        setParamRaw (pEqLow,      p.eqLow);
        setParamRaw (pEqAir,      p.eqAir);
        setParamRaw (pXoverHz,    p.xoverHz);
        setParamRaw (pBassWeight, p.bassWeight);
        setParamRaw (pWidth,      p.width);
        setParamRaw (pGlue,       p.glue);
        setParamRaw (pAutoDepth,  p.autoDepth);
        setParamRaw (pDrive,      p.drive);
        setParamRaw (pCeilingDb,  kCeilingDb);
        setParamRaw (pOutTrimDb,  0.0f);
        for (int i = 0; i < kNumStages; ++i) stageOn[(size_t) i].store (true);
        dirty.store (true);
    }

    /** Current gain reduction in dB (positive = reducing) — for the meter. */
    float getGainReductionDb() const noexcept { return grDb.load(); }

    /** Always zero — see the header note. Kept so the host can ask. */
    static constexpr int getLatencySamples() noexcept { return 0; }

    //==========================================================================
    // Audio thread
    //==========================================================================
    void process (juce::AudioBuffer<float>& buffer)
    {
        if (! enabled.load()) { grDb.store (0.0f); return; }

        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        if (dirty.exchange (false))
            updateFilters();

        // Per-block parameter targets (smoothed per sample below).  Read once
        // here, never per sample, so a knob move can't tear mid-block.
        const Resolved p = resolve();

        float* L = buffer.getWritePointer (0);
        float* R = (numCh > 1) ? buffer.getWritePointer (1) : nullptr;

        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // Smooth the macro-driven parameters so a knob move never zippers.
            smDrive     += paramCoeff * (p.drive      - smDrive);
            smWidth     += paramCoeff * (p.width      - smWidth);
            smSubWeight += paramCoeff * (p.bassWeight - smSubWeight);
            smGlue      += paramCoeff * (p.glue       - smGlue);
            smOutGain   += paramCoeff * (p.outGain    - smOutGain);
            smMix       += paramCoeff * (p.mix        - smMix);

            // Keep the untouched input for the AMOUNT blend below.
            const float dryL = L[i];
            const float dryR = (R != nullptr) ? R[i] : dryL;

            float l = dryL;
            float r = dryR;

            // ── 1. Rumble HPF ────────────────────────────────────────────────
            l = biquad (hpfCoef, hpfState[0], l);
            r = biquad (hpfCoef, hpfState[1], r);

            // ── 2. Tilt / Air EQ ─────────────────────────────────────────────
            l = biquad (loShelfCoef, lowShelfState[0], l);
            r = biquad (loShelfCoef, lowShelfState[1], r);
            l = biquad (hiShelfCoef, hiShelfState[0], l);
            r = biquad (hiShelfCoef, hiShelfState[1], r);

            // ── 3. Crossover: low band (mono + weight), high band (width) ────
            // The high band is derived by SUBTRACTION, not by a second filter.
            // Summing a 2nd-order Butterworth LP and HP produces a complete NULL
            // at the crossover frequency (LP+HP = (1+s^2)/(s^2+sqrt2 s+1), which
            // is zero at s=j) — it would have punched a hole at 120 Hz.
            // low + (input - low) reconstructs the input exactly, at any order.
            const float lLow = biquad (lpCoef, lpState[0], l);
            const float rLow = biquad (lpCoef, lpState[1], r);
            const float lHi  = l - lLow;
            const float rHi  = r - rLow;

            // Low band -> mono, then soft-saturate for weight.
            float lowMono = 0.5f * (lLow + rLow);
            if (smSubWeight > 1.0e-4f)
            {
                const float d = 1.0f + smSubWeight * kSubDriveRange;
                lowMono = std::tanh (lowMono * d) / d;
            }

            // High band -> M/S width.  Bass is already mono and untouched.
            float mid  = 0.5f * (lHi + rHi);
            float side = 0.5f * (lHi - rHi);
            side *= smWidth;

            l = lowMono + (mid + side);
            r = lowMono + (mid - side);

            // ── 4. Glue compressor (peak/RMS hybrid, dual-stage release) ─────
            const float det = juce::jmax (std::abs (l), std::abs (r));

            if (det > glueEnv) glueEnv += attackCoeff * (det - glueEnv);
            else
            {
                // Release speed is program dependent: the further BELOW the
                // envelope the signal has fallen, the slower we let go — this
                // is what stops fills and crashes making the mix breathe.
                const float depth = (glueEnv > 1.0e-6f) ? (glueEnv - det) / glueEnv : 0.0f;
                const float c     = relFastCoeff + (relSlowCoeff - relFastCoeff)
                                                     * juce::jlimit (0.0f, 1.0f, depth);
                glueEnv += c * (det - glueEnv);
            }

            float glueGain = 1.0f;
            if (smGlue > 1.0e-4f)
            {
                const float thr = kGlueThreshold;
                if (glueEnv > thr)
                {
                    // Soft-knee 2:1 above the threshold, scaled by the macro.
                    const float over  = glueEnv / thr;                 // > 1
                    const float ratio = 1.0f + smGlue;                 // 1..2
                    glueGain = std::pow (over, (1.0f / ratio) - 1.0f);
                }
            }
            l *= glueGain;
            r *= glueGain;

            // ── 5. Auto-level (density adaptation) ───────────────────────────
            const float sq = 0.5f * (l * l + r * r);
            rmsEst += (autoLevelPrimed ? rmsCoeff : primeCoeff) * (sq - rmsEst);
            const float rms = std::sqrt (juce::jmax (rmsEst, 1.0e-12f));

            // Adapt only while there IS signal.  On silence the ratio
            // target/rms runs away to the clamp, and the first note after a
            // pause would then arrive up to +12 dB hot before the smoother
            // caught up.  Below the floor we HOLD the current gain instead.
            if (rms > kAutoLevelFloor)
            {
                const float wanted = juce::jlimit (kAutoLevelMinGain, kAutoLevelMaxGain,
                                                   p.targetRms / juce::jmax (rms, 1.0e-5f));
                // Only engage as far as the macro asks for.
                const float wantedMix = 1.0f + (wanted - 1.0f) * p.autoDepth;

                // PRIMING WINDOW — the fix for "the first notes of the style are
                // very loud, then it settles".
                //
                // reset() leaves the gain at 1.0 (no attenuation) and the normal
                // constants are deliberately slow: a 400 ms detector feeding a
                // 250 ms smoother.  On a loud style the eventual target is the
                // -6 dB clamp, so the opening ~580 ms played more than 1 dB hot,
                // peaking around +6.
                //
                // NOT fixed by snapping straight to wantedMix: at the first
                // sample the DETECTOR has not converged either — rmsEst is still
                // its seed, which reads as near-silence and asks for +12 dB.  A
                // naive snap therefore makes the burst WORSE, not better
                // (measured: +18 dB instead of +6).
                //
                // So prime BOTH: seed rmsEst to targetRms^2 (below, in reset)
                // so the opening target is exactly neutral whatever autoDepth
                // is, then run detector and gain on a fast constant until the
                // level is actually known, and only then hand over to the slow
                // pair.  Measured on a loud style: time spent >1 dB above the
                // settled level drops from 580 ms to 54 ms, with no change to
                // the steady state and no burst at all on a quiet style.
                if (! autoLevelPrimed)
                {
                    autoLevelGain += primeCoeff * (wantedMix - autoLevelGain);
                    if (++autoLevelPrimeSamples
                          >= (int) (kAutoLevelPrimeWindowMs * 0.001 * sampleRate))
                        autoLevelPrimed = true;
                }
                else
                {
                    autoLevelGain += autoLevelCoeff * (wantedMix - autoLevelGain);
                }
                autoLevelSilentSamples = 0;
            }
            else if (autoLevelPrimed)
            {
                // Re-arm the priming window only after SUSTAINED silence, so a
                // gap between notes can't restart it.  The floor is ~-50 dBFS
                // and the detector runs at 400 ms, so nothing musical gets
                // there — only a real stop does.  rmsEst is re-seeded with it so
                // the next start begins from the same neutral target.
                if (++autoLevelSilentSamples
                      >= (int) (kAutoLevelRearmMs * 0.001 * sampleRate))
                {
                    autoLevelPrimed       = false;
                    autoLevelPrimeSamples = 0;
                    rmsEst                = p.targetRms * p.targetRms;
                }
            }

            l *= autoLevelGain;
            r *= autoLevelGain;

            // ── 6. Saturation ────────────────────────────────────────────────
            if (smDrive > 1.0f + 1.0e-4f)
            {
                const float inv = 1.0f / std::tanh (smDrive);
                l = std::tanh (l * smDrive) * inv;
                r = std::tanh (r * smDrive) * inv;
            }

            // ── 6b. AMOUNT = DRY / WET, AND IT SITS HERE ON PURPOSE ──────────
            //
            // Everything above is the tone-and-dynamics section; everything
            // below is the safety net.  Blending here means AMOUNT controls how
            // much of the CHAIN you hear, while the ceiling and the output trim
            // still run on whatever comes out of the blend.
            //
            // Blending AFTER the limiter would have been the obvious place and
            // it is wrong: the dry path has never been limited, so any mix below
            // 100% could push the output past the ceiling - and that ceiling is
            // the guarantee the whole zero-latency design rests on (its range
            // cannot even reach 0 dB, see paramRange).
            //
            // Consequence worth knowing: AMOUNT at 0 is not "Finisher off", it
            // is the limiter alone, which is a genuinely useful setting.  The
            // enable switch is what turns the whole thing off.
            //
            // The usual parallel-processing caveat applies - the HPF and the
            // shelves are minimum-phase, so blending them against dry shifts
            // their effective curve slightly rather than simply scaling it.
            // That is inherent to any wet/dry across an EQ, and it is why the
            // sliders are absolute now: what you set is what the WET path does.
            l = dryL + (l - dryL) * smMix;
            r = dryR + (r - dryR) * smMix;

            // ── 7. Ceiling: fast feedback peak-follower + soft clip ──────────
            // No lookahead, so the follower can overshoot on a sharp transient;
            // the soft clipper below is what guarantees the ceiling is never
            // exceeded.  Follower first = the clipper only ever sees small
            // overshoots, which keeps it clean.
            const float pk = juce::jmax (std::abs (l), std::abs (r));
            if (pk > peakEnv) peakEnv = pk;                        // instant attack
            else              peakEnv += peakRelCoeff * (pk - peakEnv);

            float limGain = 1.0f;
            if (peakEnv > p.ceiling)
                limGain = p.ceiling / peakEnv;

            l *= limGain;
            r *= limGain;

            l = softClip (l, p.ceiling);
            r = softClip (r, p.ceiling);

            // ── 8. Output trim ───────────────────────────────────────────────
            l *= smOutGain;
            r *= smOutGain;

            L[i] = l;
            if (R != nullptr) R[i] = r;

            // Glue reduction happens inside the wet path, so at a low mix most
            // of it never reaches the output - report what is actually heard.
            const float glueHeard = 1.0f + (glueGain - 1.0f) * smMix;
            const float grLin = glueHeard * limGain;
            maxGr = juce::jmax (maxGr, 1.0f - grLin);
        }

        // Meter: report the block's worst reduction, in dB.
        const float lin = juce::jlimit (0.0001f, 1.0f, 1.0f - maxGr);
        grDb.store (-20.0f * std::log10 (lin));
    }

private:
    //==========================================================================
    // Tuning constants — all in one place so the whole thing can be dialled by
    // ear without hunting through the process loop.
    //==========================================================================
    static constexpr float kCeilingDb          = -0.3f;   // never exceeded
    static constexpr float kCeiling            = 0.9660509f;   // 10^(-0.3/20)

    static constexpr float kHpfHz              = 25.0f;
    static constexpr float kHpfHzLive          = 40.0f;   // stage rumble control
    static constexpr float kCrossoverHz        = 120.0f;

    static constexpr float kRmsTimeMs          = 400.0f;  // density estimate
    static constexpr float kAutoLevelTimeMs    = 250.0f;
    static constexpr float kGlueAttackMs       = 15.0f;
    static constexpr float kGlueReleaseFastMs  = 80.0f;
    static constexpr float kGlueReleaseSlowMs  = 400.0f;
    static constexpr float kPeakReleaseMs      = 40.0f;
    static constexpr float kParamSmoothMs      = 30.0f;

    // ~ -15 dBFS.  Was -12, which a typical arranger mix rarely reached, so the
    // glue stage sat idle and the GR meter never moved — "the effect isn't
    // hearable".  Low enough to engage on normal material, high enough that it
    // still lets quiet passages breathe.
    static constexpr float kGlueThreshold      = 0.18f;
    static constexpr float kSubDriveRange      = 2.5f;
    static constexpr float kClipKnee           = 0.80f;  // clipper is linear below 80% of ceiling
    static constexpr float kAutoLevelMinGain   = 0.5f;    // -6 dB
    static constexpr float kAutoLevelMaxGain   = 4.0f;    // +12 dB
    static constexpr float kAutoLevelFloor     = 0.003f;  // ~ -50 dBFS: below this, HOLD
    static constexpr float kAutoLevelRearmMs   = 250.0f;  // silence before priming re-arms
    static constexpr float kAutoLevelPrimeMs   = 25.0f;   // fast constant while priming
    static constexpr float kAutoLevelPrimeWindowMs = 80.0f;   // how long priming lasts

    //==========================================================================
    // A character preset — the values loadCharacter() writes into the sliders.
    // These are the FULL-STRENGTH settings; the AMOUNT macro decides how far
    // toward them the chain actually goes (see resolve()).
    //==========================================================================
    struct Preset
    {
        float hpfHz, eqLow, eqAir, xoverHz, bassWeight, width, glue, autoDepth, drive;
    };

    static Preset presetFor (int c) noexcept
    {
        switch (c)
        {
            //          hpf   low   air  xover  weight  width  glue  auto  drive
            case kWarm:   return {  25.f,  3.0f, -1.0f, 120.f,  45.f, 108.f, 60.f, 75.f, 2.4f };
            case kBright: return {  25.f, -1.0f,  4.0f, 120.f,   0.f, 140.f, 45.f, 70.f, 1.7f };
            case kFat:    return {  25.f,  4.0f,  1.0f, 140.f,  80.f, 118.f, 80.f, 85.f, 2.2f };
            // LIVE: mono-safe width, higher HPF for stage rumble, heaviest glue
            // so the level stays put across sections.
            case kLive:   return {  40.f,  1.5f,  2.5f, 120.f,  35.f, 100.f, 90.f, 95.f, 2.0f };
            case kClean:
            default:      return {  25.f,  0.5f,  1.5f, 120.f,  10.f, 112.f, 40.f, 60.f, 1.5f };
        }
    }

    /** Resolved, ready-to-use values for one block: the slider settings with
        every bypassed stage neutralised.

        AMOUNT IS NO LONGER APPLIED HERE, AND THAT IS THE POINT.

        It used to multiply every depth parameter — shelf gains, bass weight,
        the width and drive deviations from unity, glue and auto-level — so at
        the shipped default of 50% every slider delivered half of its span, and
        the gentle CLEAN preset then halved again landed below audibility: EQ
        LOW 0.25 dB, AIR 0.75 dB, glue 1.20:1, width 1.06.  The only parameters
        that escaped were the two frequencies, the ceiling and the output trim
        — and since the ceiling IS the soft-clip limiter, the one control that
        bypassed AMOUNT was the one thing that could be heard working.

        The sliders are absolute now.  AMOUNT is a DRY/WET blend applied once,
        in process(), across the whole tone-and-dynamics section. */
    struct Resolved
    {
        float hpfHz, eqLow, eqAir, xoverHz;
        float bassWeight, width, glue, autoDepth, drive;
        float ceiling, outGain, targetRms, mix;
    };

    Resolved resolve() const noexcept
    {
        const bool  onHpf   = stageOn[kStageHpf]  .load();
        const bool  onEq    = stageOn[kStageEq]   .load();
        const bool  onBass  = stageOn[kStageBass] .load();
        const bool  onWidth = stageOn[kStageWidth].load();
        const bool  onGlue  = stageOn[kStageGlue] .load();
        const bool  onAuto  = stageOn[kStageAuto] .load();
        const bool  onDrive = stageOn[kStageDrive].load();

        Resolved r {};
        // A bypassed HPF still needs a corner: park it where it does nothing.
        r.hpfHz      = onHpf ? params[pHpfHz].load() : 5.0f;
        r.eqLow      = onEq  ? params[pEqLow].load() : 0.0f;
        r.eqAir      = onEq  ? params[pEqAir].load() : 0.0f;
        r.xoverHz    = params[pXoverHz].load();
        r.bassWeight = onBass  ? params[pBassWeight].load() * 0.01f : 0.0f;
        r.width      = onWidth ? params[pWidth].load() * 0.01f : 1.0f;
        r.glue       = onGlue  ? params[pGlue].load() * 0.01f : 0.0f;
        r.autoDepth  = onAuto  ? params[pAutoDepth].load() * 0.01f : 0.0f;
        r.drive      = onDrive ? params[pDrive].load() : 1.0f;

        r.ceiling    = juce::Decibels::decibelsToGain (params[pCeilingDb].load());
        r.outGain    = juce::Decibels::decibelsToGain (params[pOutTrimDb].load());
        r.targetRms  = (character.load() == kLive) ? 0.14f : 0.12f;
        r.mix        = juce::jlimit (0.0f, 1.0f, amount.load() * 0.01f);
        return r;
    }

    void setParamRaw (int id, float v) noexcept
    {
        float lo, hi, st; paramRange (id, lo, hi, st);
        params[(size_t) id].store (juce::jlimit (lo, hi, v));
    }

    //==========================================================================
    // Minimal biquad (transposed direct form II) — no juce::dsp allocation and
    // no latency of any kind.
    //==========================================================================
    struct Coef  { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { float z1 = 0, z2 = 0; };

    static inline float biquad (const Coef& c, State& s, float x) noexcept
    {
        const float y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    static Coef makeHighpass (double sr, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw = std::cos (w), sw = std::sin (w);
        const double al = sw / (2.0 * q);
        const double a0 = 1.0 + al;
        Coef c;
        c.b0 = (float) (((1.0 + cw) * 0.5) / a0);
        c.b1 = (float) ((-(1.0 + cw)) / a0);
        c.b2 = c.b0;
        c.a1 = (float) ((-2.0 * cw) / a0);
        c.a2 = (float) ((1.0 - al) / a0);
        return c;
    }

    static Coef makeLowpass (double sr, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw = std::cos (w), sw = std::sin (w);
        const double al = sw / (2.0 * q);
        const double a0 = 1.0 + al;
        Coef c;
        c.b0 = (float) (((1.0 - cw) * 0.5) / a0);
        c.b1 = (float) ((1.0 - cw) / a0);
        c.b2 = c.b0;
        c.a1 = (float) ((-2.0 * cw) / a0);
        c.a2 = (float) ((1.0 - al) / a0);
        return c;
    }

    static Coef makeLowShelf (double sr, double f, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw = std::cos (w), sw = std::sin (w);
        const double al = sw * 0.5 * std::sqrt ((A + 1.0 / A) * (1.0 / 0.9 - 1.0) + 2.0);
        const double ap1 = A + 1.0, am1 = A - 1.0, sq = 2.0 * std::sqrt (A) * al;
        const double a0 = ap1 + am1 * cw + sq;
        Coef c;
        c.b0 = (float) ((A * (ap1 - am1 * cw + sq)) / a0);
        c.b1 = (float) ((2.0 * A * (am1 - ap1 * cw)) / a0);
        c.b2 = (float) ((A * (ap1 - am1 * cw - sq)) / a0);
        c.a1 = (float) ((-2.0 * (am1 + ap1 * cw)) / a0);
        c.a2 = (float) ((ap1 + am1 * cw - sq) / a0);
        return c;
    }

    static Coef makeHighShelf (double sr, double f, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw = std::cos (w), sw = std::sin (w);
        const double al = sw * 0.5 * std::sqrt ((A + 1.0 / A) * (1.0 / 0.9 - 1.0) + 2.0);
        const double ap1 = A + 1.0, am1 = A - 1.0, sq = 2.0 * std::sqrt (A) * al;
        const double a0 = ap1 - am1 * cw + sq;
        Coef c;
        c.b0 = (float) ((A * (ap1 + am1 * cw + sq)) / a0);
        c.b1 = (float) ((-2.0 * A * (am1 + ap1 * cw)) / a0);
        c.b2 = (float) ((A * (ap1 + am1 * cw - sq)) / a0);
        c.a1 = (float) ((2.0 * (am1 - ap1 * cw)) / a0);
        c.a2 = (float) ((ap1 - am1 * cw - sq) / a0);
        return c;
    }

    /** Smooth ceiling.  Perfectly LINEAR below the knee (so quiet material is
        untouched and adds no harmonics), then bends over and approaches
        kCeiling asymptotically — the output can never exceed it.  This is what
        makes the zero-latency design safe: the feedback peak-follower has no
        lookahead and will overshoot on a sharp transient, and this catches the
        overshoot musically instead of letting it hard-clip. */
    static inline float softClip (float x, float k) noexcept
    {
        const float knee = kClipKnee * k;              // linear below this

        const float ax = std::abs (x);
        if (ax <= knee) return x;                      // untouched

        const float sign = (x < 0.0f) ? -1.0f : 1.0f;
        const float over = (ax - knee) / (k - knee);   // 0 .. inf
        return sign * (knee + (k - knee) * std::tanh (over));
    }

    void updateFilters()
    {
        const Resolved p = resolve();
        hpfCoef     = makeHighpass  (sampleRate, juce::jmax (5.0f, p.hpfHz), 0.707);
        loShelfCoef = makeLowShelf  (sampleRate, 180.0, p.eqLow);
        hiShelfCoef = makeHighShelf (sampleRate, 8000.0, p.eqAir);
        lpCoef      = makeLowpass   (sampleRate, juce::jlimit (40.0f, 400.0f, p.xoverHz), 0.707);
    }

    float coeffFor (float ms) const noexcept
    {
        const double t = juce::jmax (1.0e-4, (double) ms * 0.001);
        return (float) (1.0 - std::exp (-1.0 / (t * sampleRate)));
    }

    //==========================================================================
    double sampleRate = 44100.0;

    // ON by default: the Finisher is part of Grex's intended master sound, not
    // an opt-in extra, so a fresh instance is already running it (the mixer
    // tickbox seeds itself from this via getFinisherEnabled(), so the UI shows
    // ON to match).  process() still returns immediately when switched off.
    std::atomic<bool>  enabled   { true };
    std::atomic<float> amount    { 50.0f };
    std::atomic<int>   character { kClean };
    std::atomic<bool>  dirty     { true };

    std::array<std::atomic<float>, kNumParams> params {};
    std::array<std::atomic<bool>,  kNumStages> stageOn {};
    std::atomic<float> grDb      { 0.0f };

    Coef  hpfCoef, loShelfCoef, hiShelfCoef, lpCoef;
    State hpfState[2], lowShelfState[2], hiShelfState[2], lpState[2];

    float rmsCoeff = 0.001f, autoLevelCoeff = 0.001f, primeCoeff = 0.01f;
    float attackCoeff = 0.01f, relFastCoeff = 0.001f, relSlowCoeff = 0.0002f;
    float peakRelCoeff = 0.001f, paramCoeff = 0.01f;

    float rmsEst = 1.0e-4f, autoLevelGain = 1.0f;
    // Auto-level snap state — see the "FIRST signal after silence" block.
    bool  autoLevelPrimed        = false;
    int   autoLevelSilentSamples = 0;
    int   autoLevelPrimeSamples  = 0;
    float glueEnv = 0.0f, peakEnv = 0.0f;

    float smDrive = 1.0f, smWidth = 1.0f;
    float smSubWeight = 0.0f, smGlue = 0.0f, smOutGain = 1.0f;
    // AMOUNT as a dry/wet blend — see the note in resolve().
    float smMix       = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Finisher)
};

} // namespace Betel
