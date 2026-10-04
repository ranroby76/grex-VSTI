


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
//   4. Glue compressor     — peak detector, HARD knee, single release.
//      All four values (threshold / ratio / attack / release) are SLIDERS;
//      the factory numbers are what used to be compiled in.
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
    //==========================================================================
    // THE CHARACTER SELECTOR IS GONE — five presets and a CUSTOM slot, removed.
    //
    // It was a preset LOADER, not a mode: picking one wrote nine numbers into
    // the sliders and then stood back.  So it added a second owner for values
    // the sliders already owned, and a state ("CUSTOM") whose only meaning was
    // "those numbers are no longer the ones I wrote".  Two things to reason
    // about where one would do.
    //
    // What it sounded like is kept exactly: `loadFactoryDefaults()` below
    // writes what CLEAN used to write, and the auto-level target is fixed at
    // the value every preset but LIVE used.  Nothing about the shipping sound
    // changes - only the selector is gone.
    //==========================================================================

    /** Stages that can be individually bypassed from the window. */
    enum Stage { kStageHpf = 0, kStageEq, kStageBass, kStageWidth,
                 kStageGlue, kStageAuto, kStageDrive, kNumStages };

    /** Editable parameters, one per slider. */
    //  *** THE STRIPPED CHAIN — PUSH -> COMP -> CEILING -> OUTPUT ***
    //
    //  Everything else is GONE FROM THE DSP AND FROM THE WINDOW, but NOT from
    //  these two enums, and that distinction is the whole reason this was safe
    //  to do.  Main.cpp saves the Finisher as finParam<i> / finStage<i>, KEYED
    //  BY INDEX.  Deleting pEqLow would shift every parameter above it down one
    //  and every .bset on disk would restore the wrong number into the wrong
    //  control - the same trap as renumbering a RemoteId.
    //
    //  So the dead entries stay exactly where they are.  resolve() below pins
    //  each one to its neutral value regardless of what a set restores, the
    //  window skips them, and old sets keep loading without migration.
    //
    //  pPush is APPENDED, never inserted.  Old sets carry no finParam11, the
    //  loader's hasProperty() check leaves it alone, and it defaults to 0 dB -
    //  so no existing set changes how it sounds.
    //  THE FOUR COMP CONTROLS ARE APPENDED TOO, for the same reason pPush was:
    //  a set written before they existed carries no finParam12..15, the loader's
    //  hasProperty() check leaves them alone, and they fall back to the factory
    //  numbers - which are exactly the constants that used to be compiled in.
    //  So no existing set changes how it sounds.
    enum ParamId { pHpfHz = 0, pEqLow, pEqAir, pXoverHz, pBassWeight, pWidth,
                   pGlue, pAutoDepth, pDrive, pCeilingDb, pOutTrimDb,
                   pPush,
                   pGlueThreshDb, pGlueRatio, pGlueAttackMs, pGlueReleaseMs,
                   kNumParams };

    /** Which parameters still exist as controls.  The window builds its rows
        from this, so the panel and the DSP cannot disagree about what is real. */
    static bool paramIsLive (int id) noexcept
    {
        return id == pPush || id == pCeilingDb || id == pOutTrimDb || id == pGlue
            || id == pGlueThreshDb || id == pGlueRatio
            || id == pGlueAttackMs || id == pGlueReleaseMs;
    }

    /** pGlue is the RETIRED 0-100 amount: it keeps the stage's on/off tick and
        has no slider.  The four real controls beside it do. */
    static bool paramHasSlider (int id) noexcept
    {
        return paramIsLive (id) && id != pGlue;
    }

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
            case pGlue:       return "COMP";
            case pAutoDepth:  return "AUTO LEVEL";
            case pDrive:      return "DRIVE";
            case pCeilingDb:  return "CEILING";
            case pOutTrimDb:  return "OUTPUT";
            case pPush:          return "PUSH";
            // The ids keep their pGlue* spelling - they are persistence keys,
            // and renaming one re-points every finParam<i> in every saved set.
            case pGlueThreshDb:  return "COMP - thresh";
            case pGlueRatio:     return "COMP - ratio";
            case pGlueAttackMs:  return "COMP - attack";
            case pGlueReleaseMs: return "COMP - release";
            default:          return "?";
        }
    }

    static const char* paramSuffix (int id) noexcept
    {
        switch (id)
        {
            case pHpfHz: case pXoverHz:                 return " Hz";
            case pEqLow: case pEqAir:
            case pCeilingDb: case pOutTrimDb:
            case pPush: case pGlueThreshDb:             return " dB";
            case pGlueAttackMs: case pGlueReleaseMs:    return " ms";
            case pGlueRatio:                            return ":1";
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
            // PUSH is gain INTO the ceiling: the one loudness control.  It
            // cannot go negative - OUTPUT is the control for coming down.
            case pPush:       lo =   0.0f; hi = 12.0f;  step = 0.1f;   break;

            // ── THE COMP CONTROLS ────────────────────────────────────────────
            // Factory values are the constants that used to be compiled in:
            // -14.9 dB (0.18 linear), 2:1, 15 ms, 80 ms.
            //
            // RELEASE TOPS OUT AT 300 ms, not the 400 the dead kGlueReleaseSlowMs
            // suggested: 400 was judged too slow for anything here, and a range
            // that reaches a setting nobody wants is a range that invites it.
            case pGlueThreshDb:  lo = -40.0f; hi =   0.0f; step = 0.5f; break;
            case pGlueRatio:     lo =   1.0f; hi =  10.0f; step = 0.1f; break;
            case pGlueAttackMs:  lo =   0.5f; hi = 100.0f; step = 0.5f; break;
            case pGlueReleaseMs: lo =   5.0f; hi = 300.0f; step = 5.0f; break;
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
            case pGlueThreshDb: case pGlueRatio:
            case pGlueAttackMs: case pGlueReleaseMs:
                                            return kStageGlue;
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
            case kStageGlue:  return "COMP";
            case kStageAuto:  return "AUTO LEVEL";
            case kStageDrive: return "DRIVE";
            default:          return "?";
        }
    }

    Finisher() { loadFactoryDefaults(); }

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
        // COMP's attack/release coefficients are NOT set here: those times are
        // sliders, and resolve() computes their coefficients once per block so a
        // knob move takes effect without a re-prepare.
        //
        // peakRelCoeff is dead with the old fixed-release limiter - the follower
        // takes p.compRelease now.
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
        peakEnv       = 0.0f;
        grDb.store (0.0f);

        const Resolved r = resolve();
        rmsEst      = r.targetRms * r.targetRms;   // neutral opening target
        smOutGain   = r.outGain;
        smPush      = r.push;
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

    /** THE FACTORY SOUND — literally the numbers CLEAN used to write.
        Kept verbatim so removing the selector could not change what anyone
        already has. Also what RESET restores. */
    void loadFactoryDefaults() noexcept
    {
        //                          was: Preset{ hpf, low, air, xover, weight, width, glue, auto, drive }
        setParamRaw (pHpfHz,       25.0f);
        setParamRaw (pEqLow,        0.5f);
        setParamRaw (pEqAir,        1.5f);
        setParamRaw (pXoverHz,    120.0f);
        setParamRaw (pBassWeight,  10.0f);
        setParamRaw (pWidth,      112.0f);
        setParamRaw (pGlue,        40.0f);
        setParamRaw (pAutoDepth,   60.0f);
        setParamRaw (pDrive,        1.5f);
        setParamRaw (pCeilingDb,  kCeilingDb);
        setParamRaw (pOutTrimDb,    0.0f);
        setParamRaw (pPush,         0.0f);   // neutral: no gain into the ceiling
        // The four COMP controls open on exactly what the glue used to use.
        setParamRaw (pGlueThreshDb, kGlueThresholdDb);
        setParamRaw (pGlueRatio,    kGlueRatio);
        setParamRaw (pGlueAttackMs, kGlueAttackMs);
        setParamRaw (pGlueReleaseMs, kGlueReleaseMs);
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
            smPush    += paramCoeff * (p.push    - smPush);
            smOutGain += paramCoeff * (p.outGain - smOutGain);

            float l = L[i];
            float r = (R != nullptr) ? R[i] : l;

            // ── 1. PUSH — gain into the ceiling ──────────────────────────────
            //
            // The whole loudness control, and it is deliberately the FIRST
            // thing: everything after it is a safety net, so pushing harder can
            // only ever mean "ask the limiter for more", never "colour it more".
            l *= smPush;
            r *= smPush;

            // ── 2. COMP — the one dynamics stage, and it is the limiter ──────
            //
            // THE GLUE COMPRESSOR IS GONE.  It sat here as its own stage with
            // its own envelope, and it was the wrong processor for this
            // instrument: a 2:1 bus compressor on the MASTER makes the style and
            // the player's right hand share one gain, so every melody note
            // pulled the band down.  That is not ducking and no setting fixed
            // it - it is what a bus compressor does once the two buses have been
            // summed, and the only cure was to stop using one.
            //
            // WHAT REPLACES IT IS THE LIMITER THAT WAS ALREADY HERE, given the
            // four controls the glue used to own.  It behaves differently in the
            // way that matters: a FEEDBACK peak follower acting only on what
            // passes its threshold, rather than a feed-forward envelope riding
            // the whole programme continuously.  It reaches for a peak and lets
            // go instead of leaning on everything all the time.
            //
            // STILL ZERO LATENCY.  No lookahead, so the follower can overshoot,
            // and the soft clip below is what guarantees the ceiling holds.
            //
            // ATTACK IS A SLIDER NOW, where it used to be instant.  Instant is
            // still reachable - the minimum is 0.5 ms - but it is no longer
            // compulsory, and a slower attack is what lets a transient through
            // and keeps the band from flinching on every note.
            //
            // THE FOLLOWER RUNS WHETHER OR NOT THE STAGE IS ON.  An envelope
            // that stops being fed goes stale, and re-ticking COMP mid-song
            // would land a full-depth grab on the first note while it caught up.
            // Only the GAIN is gated.
            const float pk = juce::jmax (std::abs (l), std::abs (r));

            if (pk > peakEnv) peakEnv += p.compAttack  * (pk - peakEnv);
            else              peakEnv += p.compRelease * (pk - peakEnv);

            float compGain = 1.0f;
            if (p.compOn && peakEnv > p.compThresh)
            {
                // HARD KNEE: smooth ABOVE the threshold, slope changing abruptly
                // AT it.  Worth knowing when setting a low threshold by ear.
                //
                // At ratio 1:1 the exponent is 0 and pow() returns 1, so the
                // slider reaching 1.0 is a genuine bypass rather than a special
                // case somebody has to remember.
                const float over = peakEnv / p.compThresh;
                compGain = std::pow (over, p.compExp);
            }

            l *= compGain;
            r *= compGain;

            // ── 3. Ceiling: the backstop, and nothing else ───────────────────
            //
            // NOT A SECOND COMPRESSOR.  No threshold, ratio or times of its own -
            // a hard divide down to the ceiling plus a soft clip, and with COMP
            // set sensibly above it this should almost never engage.  It is here
            // so a transient the follower overshot cannot leave the plugin above
            // the ceiling.
            float limGain = 1.0f;
            const float post = juce::jmax (std::abs (l), std::abs (r));
            if (post > p.ceiling)
                limGain = p.ceiling / post;

            l *= limGain;
            r *= limGain;

            l = softClip (l, p.ceiling);
            r = softClip (r, p.ceiling);

            // ── 4. Output trim ───────────────────────────────────────────────
            l *= smOutGain;
            r *= smOutGain;

            L[i] = l;
            if (R != nullptr) R[i] = r;

            // No AMOUNT blend any more, so what the meter reports is simply what
            // the two remaining gain stages did.
            const float grLin = compGain * limGain;
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
    // ONE release, replacing the old fast/slow pair (~23 ms / ~113 ms).  80 ms
    // lets go inside a bar at ballad tempo, so a phrase cannot leave the band
    // sitting reduced after it ends.
    static constexpr float kGlueReleaseMs      = 80.0f;
    // Factory values for COMP, kept as the numbers the glue was tuned to -
    // whether it runs, nothing decides how hard.
    static constexpr float kGlueRatio          = 2.0f;
    static constexpr float kGlueFixedAmount    = 1.0f;   // retired amount, unread

    // THE DUAL-STAGE RELEASE kGlueReleaseFastMs / kGlueReleaseSlowMs DESCRIBED
    // WAS NEVER IMPLEMENTED - only one release time ever reached the detector.
    // Both are removed rather than left describing machinery that is not there;
    // the release is a slider now, which is the honest version of the same idea.
    static constexpr float kPeakReleaseMs      = 40.0f;
    static constexpr float kParamSmoothMs      = 30.0f;

    // ~ -15 dBFS.  Was -12, which a typical arranger mix rarely reached, so the
    // glue stage sat idle and the GR meter never moved — "the effect isn't
    // hearable".  Low enough to engage on normal material, high enough that it
    // still lets quiet passages breathe.
    // FACTORY THRESHOLD, stated in dB so it reads as what it is: 0.18 linear
    // was the compiled-in value, and -14.9 dB is the same number.
    static constexpr float kGlueThresholdDb    = -14.9f;
    static constexpr float kSubDriveRange      = 2.5f;
    static constexpr float kClipKnee           = 0.80f;  // clipper is linear below 80% of ceiling
    static constexpr float kAutoLevelMinGain   = 0.5f;    // -6 dB
    static constexpr float kAutoLevelMaxGain   = 4.0f;    // +12 dB
    static constexpr float kAutoLevelFloor     = 0.003f;  // ~ -50 dBFS: below this, HOLD
    static constexpr float kAutoLevelRearmMs   = 250.0f;  // silence before priming re-arms
    static constexpr float kAutoLevelPrimeMs   = 25.0f;   // fast constant while priming
    static constexpr float kAutoLevelPrimeWindowMs = 80.0f;   // how long priming lasts

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
        // hpfHz/eqLow/eqAir/xoverHz/bassWeight/width/autoDepth/drive/targetRms/mix
        // are RETIRED.  The fields are kept so nothing that reads Resolved has to
        // change, and resolve() pins each to its neutral value.
        float hpfHz, eqLow, eqAir, xoverHz;
        float bassWeight, width, glue, autoDepth, drive;
        float ceiling, outGain, targetRms, mix;
        float push;          // linear gain into the ceiling
        bool  compOn;        // the stage tick

        // ── THE COMP CONTROLS, RESOLVED ──────────────────────────────────────
        // The two TIME values arrive as coefficients rather than milliseconds,
        // because coeffFor() is an exp() and the audio loop must not pay for one
        // per sample.  resolve() runs once per block, which is exactly the right
        // place for it.
        float compThresh;    // linear, from dB
        float compExp;       // (1/ratio) - 1, the exponent used per sample
        float compAttack;    // one-pole coefficient
        float compRelease;   // one-pole coefficient
    };

    Resolved resolve() const noexcept
    {
        Resolved r {};

        // ── EVERY RETIRED STAGE IS PINNED NEUTRAL, UNCONDITIONALLY ───────────
        //
        // Not read from params[], not gated on stageOn[].  All 90 sets on disk
        // carry values for these - a width of 112%, an auto-level depth of 60% -
        // and if resolve() still honoured them, stripping the DSP would have
        // done nothing for anyone who ever saved a set.  Pinning here is what
        // makes the removal actually take effect, on old and new sets alike.
        r.hpfHz      = 5.0f;    // parked below audio: the biquad runs, does nothing
        r.eqLow      = 0.0f;
        r.eqAir      = 0.0f;
        r.xoverHz    = 120.0f;
        r.bassWeight = 0.0f;
        r.width      = 1.0f;
        r.autoDepth  = 0.0f;
        r.drive      = 1.0f;
        r.targetRms  = 0.12f;
        r.mix        = 1.0f;    // AMOUNT is gone; the chain is always fully wet

        // ── WHAT IS LEFT ─────────────────────────────────────────────────────
        //
        // pGlue is the retired 0-100 amount and is read by nothing.
        // slow pumping be dialled in without anyone meaning to.
        r.compOn  = stageOn[kStageGlue].load();
        r.glue    = kGlueFixedAmount;          // retired amount, unread

        // COMP's four controls.  These constants were the glue's fixed settings
        // and are now the factory defaults of four sliders - see paramRange.
        r.compThresh  = juce::Decibels::decibelsToGain (params[pGlueThreshDb].load());
        const float ratio = juce::jmax (1.0f, params[pGlueRatio].load());
        r.compExp     = (1.0f / ratio) - 1.0f;   // 1:1 -> 0 -> unity gain
        r.compAttack  = coeffFor (params[pGlueAttackMs].load());
        r.compRelease = coeffFor (params[pGlueReleaseMs].load());

        r.push    = juce::Decibels::decibelsToGain (params[pPush].load());
        r.ceiling = juce::Decibels::decibelsToGain (params[pCeilingDb].load());
        r.outGain = juce::Decibels::decibelsToGain (params[pOutTrimDb].load());
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
    std::atomic<bool>  dirty     { true };

    std::array<std::atomic<float>, kNumParams> params {};
    std::array<std::atomic<bool>,  kNumStages> stageOn {};
    std::atomic<float> grDb      { 0.0f };

    Coef  hpfCoef, loShelfCoef, hiShelfCoef, lpCoef;
    State hpfState[2], lowShelfState[2], hiShelfState[2], lpState[2];

    float rmsCoeff = 0.001f, autoLevelCoeff = 0.001f, primeCoeff = 0.01f;
    // ONE release, not the old dual-stage pair.  The program-dependent slow leg
    // ran at ~113 ms and held reduction on after a phrase - that is the tail
    // heard as "the band ducks and recovers slowly".  A single moderate release
    // is predictable, which matters more here than being clever.
    // peakRelCoeff is retired: the follower's release is p.compRelease now.
    float paramCoeff = 0.01f;

    float rmsEst = 1.0e-4f, autoLevelGain = 1.0f;
    // Auto-level snap state — see the "FIRST signal after silence" block.
    bool  autoLevelPrimed        = false;
    int   autoLevelSilentSamples = 0;
    int   autoLevelPrimeSamples  = 0;
    // glueEnv went with the glue stage; peakEnv is the one follower left.
    float peakEnv = 0.0f;

    float smOutGain = 1.0f;
    // AMOUNT as a dry/wet blend — see the note in resolve().
    float smPush      = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Finisher)
};

} // namespace Betel




