#pragma once
//==============================================================================
// SoundsFx.h  —  Sounds-path insert effects for the Grex sampler.
//
// Real-time-safe stereo effect engines for the per-channel FX chain.  The
// Chorus / Phaser / Reverb / Delay DSP is ported from the Bambino synth
// (LabFxDsp.h): exponential/analog-flavoured, calibrated 0..1 controls.
//
//     ChorusFx        — 2 anti-phase modulated delay voices (Bambino Chorus)
//     AutoWahFx       — envelope+LFO swept resonant band-pass  (Grex original)
//     PhaserFx        — 4-stage all-pass cascade + feedback     (Bambino Phaser)
//     ReverbFx        — FDN Hall/Room + Dattorro Plate, 2 algos    (Grex)
//     StereoDelayFx   — ping-pong delay w/ HF damping           (Bambino delay)
//
// Conventions (matching the rest of the engine):
//   * prepare(sampleRate) sizes/zeroes state; reset() just zeroes it.
//   * process(L, R, n, <params...>, sr) edits the buffers in place per block.
//   * No allocations in process(); buffers are sized once in prepare().
//   * Params are passed per block (the channel loads them from its atomics).
//   * mix is the WET amount in [0,1].  StereoDelayFx also takes a separate
//     dry, so wet no longer implies less dry; the others still blend.
//==============================================================================

#include <JuceHeader.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>

namespace Betel
{
    static constexpr float kTwoPi = 6.283185307179586f;

    // Linear-interpolated read `delaySamples` back from write index `w` in a
    // ring buffer of length `len` (kept for any external callers).
    inline float fracDelayRead (const float* buf, int len, int w, float delaySamples)
    {
        float rp = (float) w - delaySamples;
        while (rp < 0.0f)        rp += (float) len;
        while (rp >= (float) len) rp -= (float) len;
        const int   i0 = (int) rp;
        const int   i1 = (i0 + 1) % len;
        const float f  = rp - (float) i0;
        return buf[i0] + (buf[i1] - buf[i0]) * f;
    }

    //==========================================================================
    //  Shared DSP building blocks (ported from Bambino LabFxDsp.h)
    //==========================================================================

    // Power-of-two ring buffer with fractional (linear-interp) read.
    struct DelayLine
    {
        std::vector<float> buffer;
        int mask = 0, writeIdx = 0;

        void prepare (int maxDelaySamples)
        {
            int n = 1; while (n < maxDelaySamples + 4) n <<= 1;
            buffer.assign ((size_t) n, 0.0f); mask = n - 1; writeIdx = 0;
        }
        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); writeIdx = 0; }
        void write (float x) { buffer[(size_t) writeIdx] = x; writeIdx = (writeIdx + 1) & mask; }

        //----------------------------------------------------------------------
        // FRACTIONAL READ — 4-POINT CUBIC HERMITE, NOT LINEAR.
        //
        // THIS IS WHERE THE REVERB'S TREMOLO CAME FROM, and it is worth writing
        // down because nothing about it looks like a modulation bug.
        //
        // Linear interpolation between two samples is a LOW-PASS FILTER whose
        // cutoff depends on the fractional part: at frac = 0 or 1 it is a
        // passthrough, at frac = 0.5 it is at its dullest.  So any delay line
        // with a MOVING read pointer has its high end attenuated by an amount
        // that rises and falls as the pointer sweeps - a brightness wobble at
        // twice the modulation rate.  One line of that is a slight shimmer.
        // Eight recirculating lines, each compounding the ripple on every pass
        // around the feedback loop, is a tail that breathes: heard as tremolo.
        //
        // Shrinking the modulation only shrinks the symptom, and shrinking it
        // far enough lets the FDN modes lock back into an audible ring, which is
        // the metallic sound the modulation exists to prevent.  Cubic Hermite
        // fixes the cause instead: its magnitude response is far flatter across
        // frac, so the sweep stops changing the tone and the tail just decays.
        //
        // Stateless, so unlike an allpass interpolator there is no filter state
        // to glitch when the delay jumps, and no stability edge as frac nears 0.
        // prepare() already reserves +4 samples, which is what the extra two
        // taps need.
        //----------------------------------------------------------------------
        float read (float delaySamples) const
        {
            const float fIdx = (float) writeIdx - delaySamples;
            const int   i1   = ((int) std::floor (fIdx)) & mask;
            const float frac = fIdx - std::floor (fIdx);

            const float y0 = buffer[(size_t) ((i1 - 1) & mask)];
            const float y1 = buffer[(size_t) i1];
            const float y2 = buffer[(size_t) ((i1 + 1) & mask)];
            const float y3 = buffer[(size_t) ((i1 + 2) & mask)];

            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        }
    };

    // One-pole low-pass (HF damping for reverb/delay feedback paths).
    struct OnePoleLP
    {
        double sampleRate = 44100.0;
        float coeffA = 0.0f, coeffB = 1.0f, y1L = 0.0f, y1R = 0.0f;

        void prepare (double sr) { sampleRate = sr; reset(); }
        void reset() { y1L = y1R = 0.0f; }
        void setCutoff (float hz)
        {
            const float a = std::exp (-kTwoPi * hz / (float) sampleRate);
            coeffA = a; coeffB = 1.0f - a;
        }
        float processL (float x) { y1L = coeffB * x + coeffA * y1L; return y1L; }
        float processR (float x) { y1R = coeffB * x + coeffA * y1R; return y1R; }
    };

    // Free-running sine LFO.
    struct LfoSine
    {
        double sampleRate = 44100.0;
        float phase = 0.0f, phaseInc = 0.0f;

        void prepare (double sr) { sampleRate = sr; phase = 0.0f; }
        void reset() { phase = 0.0f; }
        void setRateHz (float hz) { phaseInc = (float) (kTwoPi * (double) hz / sampleRate); }
        float next()
        {
            const float y = std::sin (phase);
            phase += phaseInc;
            if (phase > kTwoPi) phase -= kTwoPi;
            return y;
        }
    };

    // First-order all-pass biquad (Phaser stage).
    struct AllpassBQ
    {
        float b0c = 1.0f, b1c = 0.0f, b2c = 0.0f, a1c = 0.0f, a2c = 0.0f;
        float x1L = 0, x2L = 0, y1L = 0, y2L = 0;
        float x1R = 0, x2R = 0, y1R = 0, y2R = 0;

        void reset() { x1L = x2L = y1L = y2L = 0; x1R = x2R = y1R = y2R = 0; }
        void setAllpass1 (double sr, float fc)
        {
            const float w0 = kTwoPi * fc / (float) sr;
            const float t  = std::tan (w0 * 0.5f);
            const float a  = (t - 1.0f) / (t + 1.0f);
            b0c = a; b1c = 1.0f; b2c = 0.0f; a1c = a; a2c = 0.0f;
        }
        float processL (float x)
        {
            const float y = b0c * x + b1c * x1L + b2c * x2L - a1c * y1L - a2c * y2L;
            x2L = x1L; x1L = x; y2L = y1L; y1L = y; return y;
        }
        float processR (float x)
        {
            const float y = b0c * x + b1c * x1R + b2c * x2R - a1c * y1R - a2c * y2R;
            x2R = x1R; x1R = x; y2R = y1R; y1R = y; return y;
        }
    };

    //==========================================================================
    //  ChorusFx — two anti-phase modulated delay voices around a 15 ms tap.
    //  process: rate01 -> 0.1..6 Hz, depth01 -> 0..10 ms, mix 0..1.
    //==========================================================================
    //==========================================================================
    //  ChorusFx — 80s BBD (bucket-brigade) chorus, Juno-60/106 voicing.
    //
    //  Replaces a generic digital chorus that was, in effect, a stereo doubler.
    //  Five things separate the two, all of them audible:
    //
    //  1. BASE DELAY 3 ms, not 15.  At 15 ms the ear resolves a discrete
    //     slapback and the comb notches land in the low-mids where the music
    //     lives.  A BBD chorus sits at 2..4 ms, short enough to be heard as
    //     THICKENING rather than as a second voice.
    //
    //  2. DEPTH +/-1.3 ms, not +/-10.  Sweeping a 10 ms range is what produced
    //     the seasick pitch wobble: modulation rate of change IS pitch shift,
    //     and 10 ms of swing at 1 Hz is far more detune than any analogue
    //     chorus ever applied.
    //
    //  3. TRIANGLE LFO, not sine.  A sine dwells near its extremes and hurries
    //     through the middle, which reads as a lolloping, uneven wobble.  A
    //     triangle sweeps linearly — that even, continuous motion is the Juno
    //     signature.
    //
    //  4. DRY IS SUMMED, not crossfaded.  The chorus effect IS the interference
    //     between dry and delayed signal; the old dry*(1-mix) + wet*mix removed
    //     the dry entirely at mix=1, leaving detuned delay with nothing to comb
    //     against.  Dry stays at unity and MIX sets the wet against it.
    //
    //  5. WET IS BAND-LIMITED to ~8 kHz.  A BBD is a sampled analogue line with
    //     anti-alias and reconstruction filters either side; its output is
    //     audibly darker than its input.  Without that roll-off a digital
    //     chorus sounds glassy and phasey on top.
    //
    //  The anti-phase L/R modulation from the original is kept — that is what
    //  produces the width, and it was the one part already right.
    //==========================================================================
    struct ChorusFx
    {
        double    sampleRate  = 44100.0;
        DelayLine lineL, lineR;
        OnePoleLP wetLP;                     // BBD bandwidth limit
        float     phase = 0.0f, phaseInc = 0.0f;   // triangle LFO
        float     depthSamples = 0.0f, wetGain = 0.0f;

        // Juno voicing.  kRateLo/kRateHi bracket the useful region: the
        // hardware's two switch positions are ~0.5 Hz (I) and ~0.8 Hz (II),
        // with I+II landing near the top of this range.  Mapping the 0..1 knob
        // over 0.15..3.0 puts those settings in the MIDDLE of its travel — the
        // old 0.1..6 Hz map crushed the entire musical region into the bottom
        // 15% of the knob.
        static constexpr float kBaseDelayMs = 3.0f;
        // 1.3 ms was inaudibly shallow once the level bug below was fixed: with
        // the output no longer rising as MIX turns up, there was nothing left to
        // mistake for an effect.  2.4 ms is still well inside BBD territory
        // (the seasick range starts around 6-8) and it is enough detune to hear
        // as a SECOND PLAYER rather than as a filter.
        static constexpr float kDepthMs     = 2.4f;
        static constexpr float kRateLo      = 0.15f;
        static constexpr float kRateHi      = 3.0f;
        static constexpr float kWetCutoffHz = 8000.0f;

        void prepare (double sr)
        {
            sampleRate = sr;
            // 3 ms base + 1.3 ms swing needs ~5 ms; 20 ms is ample headroom and
            // keeps the power-of-two buffer small.
            const int maxSamples = (int) (sr * 0.020);
            lineL.prepare (maxSamples); lineR.prepare (maxSamples);
            wetLP.prepare (sr);
            wetLP.setCutoff (kWetCutoffHz);
            reset();
        }
        void reset() { lineL.reset(); lineR.reset(); wetLP.reset(); phase = 0.0f; }

        /** Triangle, -1..+1, from a 0..1 ramp.  Linear sweep both ways — see
            note 3 above. */
        static float triangle (float p01) noexcept
        {
            return 4.0f * std::abs (p01 - 0.5f) - 1.0f;
        }

        void process (float* L, float* R, int n,
                      float rate01, float depth01, float mix, double sr)
        {
            juce::ignoreUnused (sr);

            const float hz = juce::jmap (juce::jlimit (0.0f, 1.0f, rate01), kRateLo, kRateHi);
            phaseInc = (float) ((double) hz / sampleRate);

            depthSamples = juce::jlimit (0.0f, 1.0f, depth01)
                         * (float) (sampleRate * (kDepthMs * 0.001));
            wetGain      = juce::jlimit (0.0f, 1.0f, mix);

            const float baseDelay = (float) (sampleRate * (kBaseDelayMs * 0.001));

            for (int i = 0; i < n; ++i)
            {
                const float m = triangle (phase);
                phase += phaseInc;
                if (phase >= 1.0f) phase -= 1.0f;

                // Anti-phase: one line sweeps up as the other sweeps down.  The
                // two channels are never in the same place at the same time,
                // which is where the width comes from.
                const float dL = wetLP.processL (lineL.read (baseDelay + m * depthSamples));
                const float dR = wetLP.processR (lineR.read (baseDelay - m * depthSamples));

                lineL.write (L[i]); lineR.write (R[i]);

                // ── DRY + WET, THEN NORMALISED.  THIS IS THE FIX. ────────
                //
                // Note 4 above got half of it right and the audible half wrong.
                // Summing IS correct -- a chorus is the interference between dry
                // and delayed, so crossfading away the dry leaves nothing to comb
                // against.  But summing at UNITY means MIX is also a +6 dB gain
                // control, and that is what you hear: turn it up and the sound
                // gets LOUDER, not wider.  Every ear reads that as an amplifier.
                //
                // Dividing by (1 + wetGain) keeps both signals present in the
                // same ratio -- the comb is untouched -- while holding the output
                // level put.  MIX at 0 is bit-identical to the input; MIX at 1 is
                // half dry plus half wet at the same loudness, which is the
                // doubling that was there all along and could not be heard over
                // the level rise.
                const float norm = 1.0f / (1.0f + wetGain);
                L[i] = (L[i] + dL * wetGain) * norm;
                R[i] = (R[i] + dR * wetGain) * norm;
            }
        }
    };

    //==========================================================================
    //  AutoWahFx — envelope-follower + LFO swept resonant band-pass (Grex
    //  original, unchanged).
    //==========================================================================
    struct AutoWahFx
    {
        // SVF state per channel
        float lpL = 0.0f, bpL = 0.0f;
        float lpR = 0.0f, bpR = 0.0f;
        float env = 0.0f;          // shared mono envelope follower
        double phase = 0.0;

        void prepare (double) { reset(); }
        void reset()
        {
            lpL = bpL = lpR = bpR = 0.0f;
            env = 0.0f; phase = 0.0;
        }

        // sensitivity 0..1 (envelope amount), rateHz LFO, lfoDepth 0..1,
        // baseHz 100..1500, q 0.3..0.95 (resonance), mix 0..1
        void process (float* L, float* R, int n,
                      float sensitivity, float rateHz, float lfoDepth,
                      float baseHz, float q, float mix, double sr)
        {
            // Envelope-follower coefficients (fast attack, slower release).
            const float atkCoef = std::exp (-1.0f / (0.005f * (float) sr));   // ~5 ms
            const float relCoef = std::exp (-1.0f / (0.120f * (float) sr));   // ~120 ms
            const double inc     = (double) rateHz / sr;

            // SVF resonance term: lower => higher Q.  Map q 0..1 → 1.4..0.1.
            const float res = juce::jlimit (0.1f, 1.4f, 1.4f - q * 1.3f);

            // Blend amounts — see the note in the loop.  Both depend only on
            // mix and res, which are constant across the block, so the two
            // transcendentals are computed once here rather than per sample.
            const float wahTheta = mix * juce::MathConstants<float>::halfPi;
            const float dryAmt   = std::cos (wahTheta);
            const float wetAmt   = std::sin (wahTheta) * res;

            const float sweepRange = 2600.0f;   // Hz that full modulation adds

            for (int i = 0; i < n; ++i)
            {
                const float mono = 0.5f * (L[i] + R[i]);
                const float rect = std::fabs (mono);
                const float coef = (rect > env) ? atkCoef : relCoef;
                env = rect + coef * (env - rect);

                const float lfo = 0.5f * (1.0f + std::sin (kTwoPi * (float) phase));
                phase += inc; if (phase >= 1.0) phase -= 1.0;

                float fc = baseHz
                         + sensitivity * env * sweepRange * 4.0f
                         + lfoDepth * lfo * sweepRange;
                fc = juce::jlimit (80.0f, 0.45f * (float) sr, fc);

                const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
                const float k = res;
                const float a1 = 1.0f / (1.0f + g * (g + k));

                //--------------------------------------------------------------
                // (dryAmt / wetAmt are hoisted above the loop — see there.)
                //--------------------------------------------------------------
                // TWO LEVEL CORRECTIONS, both of which this filter needed.
                //
                // 1. An SVF's bandpass tap peaks at 1/k, and k IS the Q control
                //    (1.4 down to 0.1).  So the Q knob was moving LEVEL by 23 dB
                //    across its travel — -2.9 dB at Q 0, +20 dB at Q 1 — on top
                //    of moving width.  Scaling the tap by k normalises the peak
                //    to unity at every Q, which is what a Q control should do:
                //    change the shape, not the loudness.
                //
                // 2. The wet/dry blend was linear, so at MIX 0.5 both halves sat
                //    at 0.5 and anything uncorrelated between them lost 6 dB.
                //    An equal-power crossfade keeps dry² + wet² = 1, so sweeping
                //    MIX changes character without changing level.
                //
                // Together these are the "gain loss when applied": a bandpass
                // still removes the spectrum outside its band — that part is the
                // effect doing its job — but it no longer ALSO drops the level
                // twice over on the way through.
                //--------------------------------------------------------------
                // Left
                {
                    const float hp = (L[i] - (g + k) * bpL - lpL) * a1;
                    const float bp = g * hp + bpL;  bpL = g * hp + bp;
                    const float lp = g * bp + lpL;  lpL = g * bp + lp;
                    L[i] = L[i] * dryAmt + bp * wetAmt;
                }
                // Right
                {
                    const float hp = (R[i] - (g + k) * bpR - lpR) * a1;
                    const float bp = g * hp + bpR;  bpR = g * hp + bp;
                    const float lp = g * bp + lpR;  lpR = g * bp + lp;
                    R[i] = R[i] * dryAmt + bp * wetAmt;
                }
            }
        }
    };

    //==========================================================================
    //  PhaserFx — 4-stage first-order all-pass cascade with feedback, break
    //  frequency swept by an LFO.  (Bambino Phaser.)
    //  process: rate01 -> 0.05..4 Hz, depth01 0..1, fb01 0..0.9, mix 0..1.
    //==========================================================================
    struct PhaserFx
    {
        double                 sampleRate = 44100.0;
        LfoSine                lfo;
        std::array<AllpassBQ, 4> apL, apR;
        float                  fbL = 0.0f, fbR = 0.0f;

        void prepare (double sr) { sampleRate = sr; lfo.prepare (sr); reset(); }
        void reset()
        {
            for (auto& a : apL) a.reset();
            for (auto& a : apR) a.reset();
            fbL = fbR = 0.0f;
        }

        void process (float* L, float* R, int n,
                      float rate01, float depth01, float fb01, float mix, double sr)
        {
            juce::ignoreUnused (sr);
            const float hz = juce::jmap (juce::jlimit (0.0f, 1.0f, rate01), 0.05f, 4.0f);
            lfo.setRateHz (hz);
            const float depthP   = juce::jlimit (0.0f, 1.0f, depth01);
            const float feedback = juce::jlimit (0.0f, 0.9f, fb01);
            const float mixP     = juce::jlimit (0.0f, 1.0f, mix);

            // Equal-power blend amounts.  Constant across the block, so the
            // trig is done once here rather than per sample.
            const float thetaP = mixP * juce::MathConstants<float>::halfPi;
            const float dryP   = std::cos (thetaP);
            const float wetP   = std::sin (thetaP);

            for (int i = 0; i < n; ++i)
            {
                const float lv = 0.5f + 0.5f * lfo.next();
                const float fc = 200.0f * std::pow (10.0f, depthP * lv);
                for (auto& a : apL) a.setAllpass1 (sampleRate, fc);
                for (auto& a : apR) a.setAllpass1 (sampleRate, fc);

                float xL = L[i] + fbL * feedback;
                float xR = R[i] + fbR * feedback;
                for (auto& a : apL) xL = a.processL (xL);
                for (auto& a : apR) xR = a.processR (xR);

                fbL = xL; fbR = xR;

                // Equal-power blend (amounts hoisted above the loop): an
                // all-pass chain is unity magnitude, so the phaser's whole sound
                // IS the cancellation between wet and dry — and a linear blend
                // made that cancellation cost level as well as producing
                // notches.  dry² + wet² = 1 keeps the notches, returns the level.
                L[i] = L[i] * dryP + xL * wetP;
                R[i] = R[i] * dryP + xR * wetP;
            }
        }
    };

    //==========================================================================
    //  ReverbFx — two serious algorithms behind one drop-in API.
    //
    //    ALGO 0 — HALL / ROOM   (Feedback Delay Network)
    //      A 4-stage multichannel diffuser (8 channels, Hadamard mixing +
    //      polarity flips) feeding an 8-line FDN with a HOUSEHOLDER feedback
    //      matrix.  Householder is used in the loop deliberately: it is cheap
    //      and it does NOT over-mix, so the eigentones stay spread apart and
    //      long tails don't colour.  Diffusion and decay are handled by two
    //      SEPARATE stages, so the feedback loop never has to build up echo
    //      density by itself — that's what makes an FDN easy to tune.
    //
    //      Every FDN line carries:
    //        * a one-pole HF damper   — highs die before lows, like a real room
    //        * a one-pole LOW-CUT     — so bass can never pile up in the loop
    //        * a slow, mutually-ASYNCHRONOUS LFO on its read position
    //      That last one is the whole ball game.  The old Schroeder reverb had
    //      NO modulation at all, which is precisely why its tail rang metallic:
    //      fixed comb lengths = fixed resonances = a pitched, ringing decay.
    //
    //      Feedback is calibrated to a real RT60  (g = 10^(-3·t/RT60)), and
    //      size01 scales the delay lengths AND the RT60 together — so this one
    //      algorithm is a tight ROOM at 0 and a big HALL at 1.
    //
    //    ALGO 1 — PLATE   (Dattorro figure-of-eight tank, JAES 1997)
    //      pre-delay → bandwidth LPF → 4 series input allpasses → two
    //      cross-coupled halves, each: modulated allpass → delay → damper →
    //      allpass → delay.  Seven output taps per side, per the paper.
    //      Dense from the first millisecond, bright, no discrete early
    //      reflections — the sound a comb/allpass network simply cannot make.
    //
    //  Plain scalar C++: no SIMD, no intrinsics, no #ifdefs — behaves the same
    //  on x86 and Apple Silicon.  No allocation in process(); every buffer is
    //  sized once in prepare().
    //
    //  DROP-IN: prepare/reset/process signatures and the meanings of
    //  size01 / damp01 / mix01 / predelay01 are unchanged, so
    //  Channel::applyReverbInPlace() needs no edit at all.
    //  setAlgorithm(0|1) chooses Hall/Room vs Plate (default 0 = Hall/Room).
    //  process: size01, damp01, mix01, predelay01.
    //==========================================================================
    struct ReverbFx
    {
        enum Algo { AlgoHallRoom = 0, AlgoPlate = 1 };

        static constexpr int   kN          = 8;      // FDN + diffuser channels
        static constexpr int   kDiffStages = 4;
        static constexpr float kDiffBaseMs = 6.0f;   // stage lengths: 6/12/24/48 ms
        static constexpr float kFdnMaxMs   = 110.0f; // longest FDN line (big hall)
        static constexpr float kLowCutHz   = 110.0f; // in-loop low-cut

        //----------------------------------------------------------------------
        // BIG-HALL VOICING (AlgoHallRoom only — the Plate is untouched).
        //
        // The target is the 480L / M7 "large hall": the DRY IS NOT COLOURED, and
        // a long, wide tail blooms behind it.  Four things stood between the FDN
        // and that sound; the architecture itself was already right.
        //
        //  kHallPreDelayFloorMs — a hall's first returning energy arrives tens of
        //      ms after the direct sound.  Pre-delay was a knob starting at ZERO,
        //      so at low settings the tail began instantly and smeared the
        //      attack — which IS "the dry being touched".  A floor guarantees the
        //      source stays clear wherever the knob sits; the knob adds on top.
        //
        //  kHallRt60Max — the old 0.5 * 10^size reached ~5 s nominal but measured
        //      ~3.1 s once in-loop damping was applied.  A large hall is 5..8 s;
        //      it simply could not get there.
        //
        //  kHallSendHpHz — a high-pass on what ENTERS the tank, separate from the
        //      in-loop low-cut.  Low energy that never enters cannot accumulate,
        //      and that is what keeps a long reverb from swamping the low-mids.
        //
        //  kHallDampMinHz/MaxHz — the old 800 + 17000*(1-d)^2 curve slammed shut
        //      early, darkening the tail long before the knob looked extreme.
        //      Air absorption is gentler than that.
        //
        // The dry/wet law changes too — see the mix line at the end of
        // processFdn.  Same correction as the chorus: a hall SUMS.
        //----------------------------------------------------------------------
        static constexpr float kHallPreDelayFloorMs = 25.0f;
        static constexpr float kHallPreDelayKnobMs  = 175.0f;  // floor + knob = 200 ms, as before
        static constexpr float kHallRt60Min         = 0.45f;
        static constexpr float kHallRt60Max         = 8.0f;
        static constexpr float kHallSendHpHz        = 150.0f;
        static constexpr float kHallDampMinHz       = 2200.0f;
        static constexpr float kHallDampMaxHz       = 18000.0f;
        static constexpr float kAntiDenorm = 1.0e-20f;
        // Output trims: level-matched against the old Schroeder reverb so that a
        // given `mix` keeps meaning the same amount of reverb (measured, not guessed).
        static constexpr float kFdnWetGain   = 1.30f;
        static constexpr float kPlateWetGain = 1.50f;

        //----------------------------------------------------------------------
        // Records the wanted algorithm.  The actual swap (and the state clear
        // that goes with it) happens inside process(), i.e. on the audio thread
        // -- see the note there.  Safe to call from any thread.
        void setAlgorithm (int a) noexcept { pendingAlgo = (a == AlgoPlate) ? AlgoPlate : AlgoHallRoom; }
        int  getAlgorithm() const noexcept { return algo; }

        //----------------------------------------------------------------------
        // Deterministic hash -> [0,1).  No RNG at runtime: the "random" delay
        // jitter and polarity flips are identical on every run and every machine.
        static float hashToUnit (unsigned x) noexcept
        {
            x ^= x >> 16; x *= 0x7feb352dU;
            x ^= x >> 15; x *= 0x846ca68bU;
            x ^= x >> 16;
            return (float) (x & 0xFFFFFFu) * (1.0f / 16777216.0f);
        }

        // 8-point fast Walsh-Hadamard, normalised (orthogonal -> energy preserving).
        static void hadamard8 (float* x) noexcept
        {
            for (int s = 1; s < kN; s <<= 1)
                for (int i = 0; i < kN; i += (s << 1))
                    for (int j = i; j < i + s; ++j)
                    {
                        const float a = x[j], b = x[j + s];
                        x[j]     = a + b;
                        x[j + s] = a - b;
                    }
            constexpr float norm = 0.35355339f;   // 1/sqrt(8)
            for (int i = 0; i < kN; ++i) x[i] *= norm;
        }

        // Householder reflection  H = I - (2/N)·J   — orthogonal, O(N), gentle mixing.
        static void householder8 (float* x) noexcept
        {
            float s = 0.0f;
            for (int i = 0; i < kN; ++i) s += x[i];
            s *= (2.0f / (float) kN);
            for (int i = 0; i < kN; ++i) x[i] -= s;
        }

        // Schroeder allpass:  v[n] = x[n] + g·v[n-M];  y[n] = v[n-M] - g·v[n]
        static float allpass (DelayLine& dl, float x, float m, float g) noexcept
        {
            const float vD = dl.read (m);
            const float v  = x + g * vD;
            dl.write (v);
            return vD - g * v;
        }

        //======================================================================
        void prepare (double sr)
        {
            sampleRate = (sr > 0.0) ? sr : 44100.0;
            const float fs = (float) sampleRate;

            // ── shared pre-delay (0..200 ms) ────────────────────────────────
            const int maxPre = (int) (sampleRate * 0.200) + 8;
            preL.prepare (maxPre);
            preR.prepare (maxPre);

            // ── HALL/ROOM: diffuser ─────────────────────────────────────────
            // Stage ranges double (6/12/24/48 ms).  Each channel owns its own
            // slice of its stage's range, jittered, so no two taps ever coincide.
            for (int s = 0; s < kDiffStages; ++s)
            {
                const float stageMs  = kDiffBaseMs * (float) (1 << s);
                const int   maxSamps = (int) (stageMs * 0.001f * fs) + 8;

                for (int c = 0; c < kN; ++c)
                {
                    const float lo   = (float) c       / (float) kN;
                    const float hi   = (float) (c + 1) / (float) kN;
                    const float jit  = hashToUnit ((unsigned) (s * 977 + c * 131 + 17));
                    const float frac = lo + (hi - lo) * (0.25f + 0.5f * jit);

                    diff[s][c].prepare (maxSamps);
                    diffDelay[s][c] = juce::jmax (1.0f, frac * stageMs * 0.001f * fs);
                    diffSign [s][c] = (hashToUnit ((unsigned) (s * 7919 + c * 613 + 3)) < 0.5f)
                                      ? -1.0f : 1.0f;
                }
            }

            // ── HALL/ROOM: FDN lines ────────────────────────────────────────
            const int maxFdn = (int) (kFdnMaxMs * 0.001f * fs) + 8;
            for (int c = 0; c < kN; ++c)
            {
                fdn[c].prepare (maxFdn);
                // Asynchronous, non-harmonic LFO rates.  If these ever locked to
                // a common period the modulation would itself become a resonance.
                lfoInc[c]   = (float) (kTwoPi * (0.09 + 0.043 * (double) c
                                                 + 0.011 * (double) ((c * c) % 5)) / sampleRate);
                lfoPhase[c] = hashToUnit ((unsigned) (c * 2654435761u)) * kTwoPi;
            }

            // ── PLATE: Dattorro (his constants are at a 29761 Hz reference) ──
            const float k = fs / 29761.0f;
            pApDelay[0] = 142.0f * k;  pApGain[0] = 0.750f;
            pApDelay[1] = 107.0f * k;  pApGain[1] = 0.750f;
            pApDelay[2] = 379.0f * k;  pApGain[2] = 0.625f;
            pApDelay[3] = 277.0f * k;  pApGain[3] = 0.625f;
            for (int i = 0; i < 4; ++i)
                pAp[i].prepare ((int) pApDelay[i] + 8);

            dApA1 = 672.0f  * k;   dDlA1 = 4453.0f * k;
            dApA2 = 1800.0f * k;   dDlA2 = 3720.0f * k;
            dApB1 = 908.0f  * k;   dDlB1 = 4217.0f * k;
            dApB2 = 2656.0f * k;   dDlB2 = 3163.0f * k;

            pExcursion = 16.0f * k;

            tApA1.prepare ((int) (dApA1 + pExcursion) + 8);
            tDlA1.prepare ((int) dDlA1 + 8);
            tApA2.prepare ((int) dApA2 + 8);
            tDlA2.prepare ((int) dDlA2 + 8);
            tApB1.prepare ((int) (dApB1 + pExcursion) + 8);
            tDlB1.prepare ((int) dDlB1 + 8);
            tApB2.prepare ((int) dApB2 + 8);
            tDlB2.prepare ((int) dDlB2 + 8);

            // Output taps, straight from the paper (7 per side).  Every tap is
            // shorter than the line it reads, so the buffers above cover them.
            const float tL[7] = { 266.0f, 2974.0f, 1913.0f, 1996.0f, 1990.0f, 187.0f, 1066.0f };
            const float tR[7] = { 353.0f, 3627.0f, 1228.0f, 2673.0f, 2111.0f, 335.0f,  121.0f };
            for (int i = 0; i < 7; ++i) { pTapL[i] = tL[i] * k; pTapR[i] = tR[i] * k; }

            pLfoInc = (float) (kTwoPi * 0.93 / sampleRate);

            xfadeStep = 1.0f / juce::jmax (1.0f, 0.020f * fs);   // 20 ms each way

            reset();
        }

        //======================================================================
        void reset()
        {
            preL.reset(); preR.reset();
            preCur    = 0.0f;
            preXfade  = 1.0f;
            primed    = false;
            xfadeGain = 1.0f;
            xfadeDir  = 0;

            for (int s = 0; s < kDiffStages; ++s)
                for (int c = 0; c < kN; ++c)
                    diff[s][c].reset();

            for (int c = 0; c < kN; ++c)
            {
                fdn[c].reset();
                fdnLp[c] = fdnHp[c] = 0.0f;
                fdnCur[c] = 0.0f;
            }
            sendHpL = sendHpR = 0.0f;
            sendLpL = sendLpR = 0.0f;

            for (auto& a : pAp) a.reset();
            tApA1.reset(); tDlA1.reset(); tApA2.reset(); tDlA2.reset();
            tApB1.reset(); tDlB1.reset(); tApB2.reset(); tDlB2.reset();
            pBw = pDampA = pDampB = pTankFb = 0.0f;
            pLfoPhase = 0.0f;
        }

        //======================================================================
        /** dry01 defaults to 1.0 = the source untouched.

            A REVERB IS A SEND, NOT A CROSSFADE.  The plate used to fade the dry
            out as the wet came up, so raising the effect dimmed the instrument -
            the exact opposite of what a hall does, where the source stays put
            and the tail is added around it.  The FDN already summed; the plate
            did not, and the two disagreed silently depending on which algorithm
            the slot happened to be on.

            Both sum now, and DRY is its own control on top - which is also the
            shape a convolution engine would need, so nothing here has to change
            if this is ever swapped for an IR loader. */
        void process (float* L, float* R, int n,
                      float size01, float damp01, float mix01, float predelay01, double sr,
                      float dry01 = 1.0f, float tail01 = -1.0f,
                      float hp01 = -1.0f, float lp01 = 1.0f)
        {
            juce::ignoreUnused (sr);
            if (n <= 0) return;

            // ── engine swap, click-free ─────────────────────────────────────
            // Only ONE engine is clocked at a time, so the idle one's delay lines
            // sit FROZEN -- they never decay, because they never run.  Swapping
            // straight over would resume that stale tail and pop; clearing the
            // state instead cuts the LIVE tail dead, which pops just as loudly.
            // So: fade the wet out (~20 ms), swap + clear while silent, fade back
            // in.  Neither tail can ever be heard discontinuously.
            if (pendingAlgo != algo)
            {
                if (! primed)
                {
                    // Nothing is running yet (fresh prepare/reset, or the host
                    // selected the algorithm before the first block).  There is
                    // no tail to protect, so swap instantly -- fading here would
                    // just throw away the first 20 ms of audio.
                    algo      = pendingAlgo;
                    xfadeGain = 1.0f;
                    xfadeDir  = 0;
                }
                else if (xfadeDir == 0)
                {
                    xfadeDir = -1;                   // live switch -> fade out first
                }
            }

            if (xfadeDir < 0 && xfadeGain <= 0.0f)   // fully faded -> swap now
            {
                algo = pendingAlgo;
                reset();                             // (also restores xfade to 1/idle)
                xfadeGain = 0.0f;                    // ...so force it back to silent
                xfadeDir  = +1;                      // and fade the new engine in
            }

            const float size = juce::jlimit (0.0f, 1.0f, size01);
            const float damp = juce::jlimit (0.0f, 1.0f, damp01);
            const float mix  = juce::jlimit (0.0f, 1.0f, mix01);

            // Slewed per sample, so moving the PRE-DELAY knob never clicks.
            // Hall gets a pre-delay FLOOR so the dry always stays clear; the
            // Plate keeps the original knob-from-zero behaviour.
            const float wantPre = (algo == AlgoPlate)
                ? juce::jlimit (0.0f, 1.0f, predelay01) * (float) (sampleRate * 0.200)
                : (float) (sampleRate * 0.001)
                    * (kHallPreDelayFloorMs
                       + juce::jlimit (0.0f, 1.0f, predelay01) * kHallPreDelayKnobMs);

            // Arm a crossfade only on a REAL move.  The threshold matters: a
            // knob delivers a stream of tiny changes, and restarting the fade on
            // every one of them would hold the reverb permanently between two
            // taps and never let it settle.  Half a millisecond is below the
            // point where two taps differ audibly.
            if (std::abs (wantPre - preTarget) > (float) (sampleRate * 0.0005))
            {
                if (preXfade < 1.0f) preCur = preTarget;   // a move mid-fade: the
                                                           // tap being faded in
                                                           // becomes the old one
                preTarget    = wantPre;
                preXfade     = 0.0f;
                preXfadeStep = 1.0f / juce::jmax (1.0f, (float) (sampleRate * 0.045));  // 45 ms
            }

            const float dry = juce::jlimit (0.0f, 2.0f, dry01);

            // HALL ONLY, BY DECISION.
            //
            // AlgoPlate is still compiled and still correct, but nothing
            // selects it: reverbAlgo has never appeared in SlotParams, in the
            // XML or in any panel, so every channel has run the FDN since the
            // day the plate was written.  Rather than leave that as an accident
            // waiting for someone to wire up a selector, it is now the stated
            // behaviour - Rob wants a hall and the hall is what this returns.
            //
            // The plate is left in the file rather than deleted so the choice
            // stays reversible; if it is still unreachable in six months it
            // should go, because unreachable code that reads as live is exactly
            // the trap this codebase keeps setting for itself.
            juce::ignoreUnused (algo);
            processFdn (L, R, n, size, damp, mix, dry, tail01, hp01, lp01);
        }

    private:
        //======================================================================
        //  ALGO 0 — HALL / ROOM  (diffuser -> Householder FDN)
        //======================================================================
        void processFdn (float* L, float* R, int n, float size, float damp, float mix,
                         float dry = 1.0f, float tail = -1.0f,
                         float hp01 = -1.0f, float lp01 = 1.0f)
        {
            // TAIL < 0 means "no tail control supplied" - fall back to the old
            // behaviour where SIZE drove the decay, so nothing that calls the
            // short form changes character.
            if (tail < 0.0f) tail = size;
            const float fs = (float) sampleRate;

            // size scales the geometry AND the decay together.
            const float minMs = 13.0f + 32.0f * size;               // 13 -> 45 ms
            const float maxMs = 33.0f + 72.0f * size;               // 33 -> 105 ms

            // Computed here rather than below, because the per-line damping
            // coefficients are built inside the geometry loop.
            const float dampHzBase = kHallDampMinHz
                                   + (kHallDampMaxHz - kHallDampMinHz)
                                     * (1.0f - damp) * (1.0f - damp);
            // RT60 now reaches a genuine large hall.  Exponential so the knob
            // stays useful at the short end: 0.45 s -> 8.0 s nominal, which
            // measures roughly 0.4 -> 5.5 s once in-loop damping is applied.
            // ── SIZE AND TAIL ARE TWO DIFFERENT THINGS ──────────────────────
            //
            // SIZE used to drive both, which is why the hall could never be
            // made to sit still: asking for a longer tail also stretched the
            // geometry, and asking for a bigger room also made it ring.  In a
            // real space they are independent — a large hall full of drapes
            // decays fast, a small tiled room rings for seconds.  Geometry
            // (minMs / maxMs above) stays on SIZE; RT60 moves to TAIL.
            const float rt60  = kHallRt60Min
                              * std::pow (kHallRt60Max / kHallRt60Min,
                                          juce::jlimit (0.0f, 1.0f, tail));
            const float maxSamps = kFdnMaxMs * 0.001f * fs - 8.0f;

            for (int c = 0; c < kN; ++c)
            {
                // Log-spread, jittered lengths: deliberately NOT harmonically
                // related, so the modes never stack into an audible pitch.
                const float u  = (float) c / (float) (kN - 1);
                const float ji = 0.92f + 0.16f * hashToUnit ((unsigned) (c * 40503u + 7u));
                const float ms = minMs * std::pow (maxMs / minMs, u) * ji;

                fdnTarget[c] = juce::jlimit (4.0f, maxSamps, ms * 0.001f * fs);

                // ── DAMPING COMPENSATED FOR LINE LENGTH ─────────────────────
                //
                // One shared damping coefficient is applied ONCE PER PASS
                // through each line, and the lines differ in length by nearly
                // 3:1 — so the short ones were filtered nearly three times as
                // often per second as the long ones.  The result is a tail
                // whose high end dies unevenly across the modes: some die fast,
                // some hang on, and the survivors are heard as a metallic ring
                // rather than as air.  This is the harshness.
                //
                // Scaling each line's cutoff by its length relative to the mean
                // makes the damping per SECOND equal instead of per pass, which
                // is what a real room does — absorption is a property of the
                // surfaces, not of how often the wave happens to hit them.
                const float meanSamps = (minMs + maxMs) * 0.5f * 0.001f * fs;
                const float lenRatio  = juce::jlimit (0.35f, 3.0f,
                                            fdnTarget[c] / juce::jmax (1.0f, meanSamps));
                const float hz        = juce::jlimit (500.0f, 20000.0f,
                                            dampHzBase * lenRatio);
                fdnDampCoef[c] = std::exp (-kTwoPi * hz / fs);

                // RT60 -> per-line gain: exactly 60 dB of decay in rt60 seconds.
                const float tSec = fdnTarget[c] / fs;
                fdnGain[c] = juce::jlimit (0.0f, 0.995f,
                                 std::pow (10.0f, -3.0f * tSec / juce::jmax (0.05f, rt60)));
            }

            if (! primed)   // first block: jump to target instead of slewing from 0
            {
                for (int c = 0; c < kN; ++c) fdnCur[c] = fdnTarget[c];
                preCur   = preTarget;
                preXfade = 1.0f;
                primed   = true;
            }

            // Gentler than the old 800 + 17000*(1-d)^2: that reached its floor
            // far too early, so a moderate damp setting already sounded like a
            // heavily absorbent room rather than air.

            // ── THE BAND FILTER SHAPES THE SEND, SO ONLY THE WET IS TOUCHED ──
            //
            // Placed here, between the pre-delay and the tank, which is the
            // whole reason it works the way Rob asked: the DRY path further
            // down is `inL * dry` straight from the untouched input, so nothing
            // this does can reach it.  The tank never SEES the filtered-out
            // material at all.
            //
            // Send-side rather than return-side on purpose.  Filtering the
            // return would let low frequencies circulate through eight
            // recirculating lines first - intermodulating, eating headroom -
            // and only then be discarded: all of the mud's side effects, none
            // of the mud.  Filtering the send means it was never there.
            //
            // The left handle REPLACES kHallSendHpHz, which was a hard-coded
            // 150 Hz nobody could reach.  hp01 < 0 means "not supplied" and
            // falls back to exactly that, so a caller using the short form is
            // unchanged.
            const float hpHz = (hp01 < 0.0f)
                                 ? kHallSendHpHz
                                 : 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, hp01));
            const float lpHz = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, lp01));

            const float sendHpCoef = std::exp (-kTwoPi * juce::jlimit (20.0f, 20000.0f, hpHz) / fs);
            const float sendLpCoef = std::exp (-kTwoPi * juce::jlimit (20.0f, 20000.0f, lpHz) / fs);
            const bool  doSendLp   = (lpHz < 19000.0f);

            const float lowCoef  = std::exp (-kTwoPi * kLowCutHz / fs);

            // ── TANK MODULATION: SHALLOWER AGAIN ────────────────────────────
            //
            // With the cubic interpolator above, a sweep no longer changes the
            // tone - so the remaining reason to keep the depth down is the
            // pitch side: each line's moving pointer detunes it slightly, and
            // eight detuned copies of the same energy beat against each other.
            // That beating IS the residual tremolo, and it scales with depth.
            //
            // Halved. Enough movement to stop the FDN modes locking into an
            // audible ring, little enough that the beating drops below the tail.
            //
            // I DID CHECK the LFO rates before touching this: they look like an
            // arithmetic series but the (c*c)%5 term breaks it up, and only 2 of
            // the 23 pairwise beat frequencies are integer multiples of a common
            // one. The rates are not the problem, so they are left alone.
            const float modDepth = 0.30f + 0.55f * size;                  // samples
            const float modSlow  = 0.55f;                                 // rate trim

            // ── GEOMETRY SLEW: 30 ms WAS THE OTHER "STRANGE NOISE" ──────────
            //
            // Same Doppler fault as the pre-delay, times eight.  Dragging SIZE
            // retunes every FDN line at once, and a 30 ms slew across tens of
            // milliseconds of delay is a pitch ratio big enough to hear as a
            // chirp on the whole tail.
            //
            // A crossfade per line would cost eight extra reads a sample.  A
            // room does not resize instantly anyway, so the honest fix is to let
            // it take the time a swell takes: at ~1.5 s the same move is a
            // fraction of a semitone spread over long enough to read as the
            // space opening up, which is what the knob is FOR.
            const float slew     = 1.0f - std::exp (-1.0f / (1.500f * fs));  // ~1.5 s

            for (int i = 0; i < n; ++i)
            {
                const float inL = L[i], inR = R[i];

                // ── PRE-DELAY: CROSSFADE, NEVER SLIDE ───────────────────────
                //
                // This used to slew preCur toward preTarget and read from the
                // moving position.  Moving a delay READ POINTER is a Doppler
                // shift -- the rate of change IS the pitch ratio -- so dragging
                // PRE from 25 ms to 200 ms swept every sample already in the line
                // through a huge downward glide.  That is the "strange noise":
                // not distortion, a pitch-shifted whoosh of the recent past.
                //
                // Reading two FIXED taps and crossfading between them has no
                // Doppler at all: each tap plays at its own speed, and the fade
                // is a level move.  Both taps carry the same material a few ms
                // apart, so the overlap is a brief flange rather than a glide.
                preL.write (inL); preR.write (inR);

                if (preXfade < 1.0f)
                {
                    preXfade = juce::jmin (1.0f, preXfade + preXfadeStep);
                    if (preXfade >= 1.0f) preCur = preTarget;   // arrived: retire the old tap
                }

                const auto tap = [] (DelayLine& dl, float pos, float dryIn) noexcept
                { return (pos > 0.5f) ? dl.read (pos) : dryIn; };

                float dL, dR;
                if (preXfade >= 1.0f)
                {
                    dL = tap (preL, preCur, inL);
                    dR = tap (preR, preCur, inR);
                }
                else
                {
                    const float a = preXfade, b = 1.0f - a;
                    dL = tap (preL, preCur, inL) * b + tap (preL, preTarget, inL) * a;
                    dR = tap (preR, preCur, inR) * b + tap (preR, preTarget, inR) * a;
                }

                // ── stereo -> 8 channels.  Keep the L/R difference alive: the
                //    old reverb mono-summed here, which is why it had no image.
                // Send high-pass: keep low energy OUT of the tank entirely.
                // Separate from the in-loop low-cut, which only stops what is
                // already circulating from building up.
                sendHpL = (1.0f - sendHpCoef) * dL + sendHpCoef * sendHpL;
                sendHpR = (1.0f - sendHpCoef) * dR + sendHpCoef * sendHpR;
                float sL = dL - sendHpL;
                float sR = dR - sendHpR;

                if (doSendLp)
                {
                    sendLpL = (1.0f - sendLpCoef) * sL + sendLpCoef * sendLpL;
                    sendLpR = (1.0f - sendLpCoef) * sR + sendLpCoef * sendLpR;
                    sL = sendLpL;
                    sR = sendLpR;
                }

                const float mid  = 0.5f * (sL + sR);
                const float side = 0.5f * (sL - sR);
                float ch[kN];
                for (int c = 0; c < kN; ++c)
                    ch[c] = mid + ((c & 1) ? -side : side);

                // ── diffuser: delay -> polarity flip -> Hadamard, x4 ────────
                for (int s = 0; s < kDiffStages; ++s)
                {
                    for (int c = 0; c < kN; ++c)
                    {
                        diff[s][c].write (ch[c]);
                        ch[c] = diff[s][c].read (diffDelay[s][c]) * diffSign[s][c];
                    }
                    hadamard8 (ch);
                }

                // ── FDN: read (modulated) ───────────────────────────────────
                float rd[kN];
                for (int c = 0; c < kN; ++c)
                {
                    fdnCur[c] += (fdnTarget[c] - fdnCur[c]) * slew;

                    const float lfo = std::sin (lfoPhase[c]);
                    lfoPhase[c] += lfoInc[c] * modSlow;
                    if (lfoPhase[c] > kTwoPi) lfoPhase[c] -= kTwoPi;

                    rd[c] = fdn[c].read (juce::jmax (2.0f, fdnCur[c] + lfo * modDepth));
                }

                // taps: even lines -> L, odd -> R
                float wetL = 0.0f, wetR = 0.0f;
                for (int c = 0; c < kN; c += 2) wetL += rd[c];
                for (int c = 1; c < kN; c += 2) wetR += rd[c];
                wetL *= 0.5f * kFdnWetGain;
                wetR *= 0.5f * kFdnWetGain;

                advanceXfade();
                wetL *= xfadeGain;
                wetR *= xfadeGain;

                // ── feedback: damp -> low-cut -> RT60 gain -> Householder ────
                float fb[kN];
                for (int c = 0; c < kN; ++c)
                {
                    const float dc = fdnDampCoef[c];
                    fdnLp[c] = (1.0f - dc) * rd[c] + dc * fdnLp[c];
                    float v  = fdnLp[c];

                    fdnHp[c] = (1.0f - lowCoef) * v + lowCoef * fdnHp[c];
                    v -= fdnHp[c];                       // one-pole high-pass

                    fb[c] = v * fdnGain[c];
                }
                householder8 (fb);

                for (int c = 0; c < kN; ++c)
                    fdn[c].write (ch[c] + fb[c] + kAntiDenorm);

                // DRY AT UNITY, wet summed on top.  The old crossfade meant that
                // reaching for a bigger tail necessarily removed the source —
                // the exact opposite of a hall, where the dry is untouched and
                // the space sits behind it.  MIX is now a send level.
                L[i] = inL * dry + wetL * mix;
                R[i] = inR * dry + wetR * mix;
            }
        }

        //======================================================================
        //  ALGO 1 — PLATE  (Dattorro figure-of-eight tank)
        //======================================================================
        void processPlate (float* L, float* R, int n, float size, float damp, float mix,
                           float dry = 1.0f)
        {
            const float fs = (float) sampleRate;

            const float decay = juce::jlimit (0.10f, 0.78f, 0.30f + 0.48f * size);  // RT60 ~1.1 -> ~4.4 s
            const float dampC = juce::jlimit (0.0f,  0.95f, 0.05f + 0.80f * damp);
            const float bw    = juce::jlimit (0.10f, 0.9999f, 0.9995f - 0.35f * damp);
            const float dd1   = 0.70f;                                        // decay diffusion 1
            const float dd2   = juce::jlimit (0.25f, 0.50f, decay + 0.15f);   // decay diffusion 2
            // (the old pre-delay slew lived here; the crossfade below replaced it)

            if (! primed) { preCur = preTarget; preXfade = 1.0f; primed = true; }

            for (int i = 0; i < n; ++i)
            {
                const float inL = L[i], inR = R[i];

                // Same crossfade as the hall -- the Plate shares the pre-delay
                // lines, so it shared the Doppler glide too.
                if (preXfade < 1.0f)
                {
                    preXfade = juce::jmin (1.0f, preXfade + preXfadeStep);
                    if (preXfade >= 1.0f) preCur = preTarget;
                }
                preL.write (inL); preR.write (inR);

                const auto ptap = [] (DelayLine& dl, float pos, float dryIn) noexcept
                { return (pos > 0.5f) ? dl.read (pos) : dryIn; };

                float dL, dR;
                if (preXfade >= 1.0f)
                {
                    dL = ptap (preL, preCur, inL);
                    dR = ptap (preR, preCur, inR);
                }
                else
                {
                    const float a = preXfade, b = 1.0f - a;
                    dL = ptap (preL, preCur, inL) * b + ptap (preL, preTarget, inL) * a;
                    dR = ptap (preR, preCur, inR) * b + ptap (preR, preTarget, inR) * a;
                }
                float x = 0.5f * (dL + dR);

                // input bandwidth (a plate is bright — only roll off a little)
                pBw = bw * x + (1.0f - bw) * pBw;
                x   = pBw;

                // 4 series input allpasses
                for (int a = 0; a < 4; ++a)
                    x = allpass (pAp[a], x, pApDelay[a], pApGain[a]);

                // excursion: modulating the tank allpasses is what stops a plate
                // from ringing on a fixed set of modes.
                const float exA = pExcursion * std::sin (pLfoPhase);
                const float exB = pExcursion * std::sin (pLfoPhase + 1.61803f);
                pLfoPhase += pLfoInc;
                if (pLfoPhase > kTwoPi) pLfoPhase -= kTwoPi;

                // ── half A ──────────────────────────────────────────────────
                float a = x + pTankFb * decay;
                a = allpass (tApA1, a, juce::jmax (2.0f, dApA1 + exA), -dd1);
                tDlA1.write (a);
                a = tDlA1.read (dDlA1);
                pDampA = (1.0f - dampC) * a + dampC * pDampA;
                a = pDampA * decay;
                a = allpass (tApA2, a, dApA2, dd2);
                tDlA2.write (a);
                const float outA = tDlA2.read (dDlA2);

                // ── half B ──────────────────────────────────────────────────
                float b = x + outA * decay;
                b = allpass (tApB1, b, juce::jmax (2.0f, dApB1 + exB), -dd1);
                tDlB1.write (b);
                b = tDlB1.read (dDlB1);
                pDampB = (1.0f - dampC) * b + dampC * pDampB;
                b = pDampB * decay;
                b = allpass (tApB2, b, dApB2, dd2);
                tDlB2.write (b);
                pTankFb = tDlB2.read (dDlB2) + kAntiDenorm;

                // ── 7 taps per side ─────────────────────────────────────────
                const float wetL = 0.6f * kPlateWetGain * ( tDlB1.read (pTapL[0]) + tDlB1.read (pTapL[1])
                                          - tApB2.read (pTapL[2]) + tDlB2.read (pTapL[3])
                                          - tDlA1.read (pTapL[4]) - tApA2.read (pTapL[5])
                                          - tDlA2.read (pTapL[6]) );

                const float wetR = 0.6f * kPlateWetGain * ( tDlA1.read (pTapR[0]) + tDlA1.read (pTapR[1])
                                          - tApA2.read (pTapR[2]) + tDlA2.read (pTapR[3])
                                          - tDlB1.read (pTapR[4]) - tApB2.read (pTapR[5])
                                          - tDlB2.read (pTapR[6]) );

                advanceXfade();

                // SUMMED, NOT CROSSFADED - this line is the dimming.  It read
                // `inL * (1 - mix)`, so a plate at 50 % wet threw away half the
                // instrument.  The FDN twenty lines up never did that, which is
                // why the two algorithms felt like different effects.
                L[i] = inL * dry + wetL * xfadeGain * mix;
                R[i] = inR * dry + wetR * xfadeGain * mix;
            }
        }

        //----------------------------------------------------------------------
        // One step of the engine-swap fade.  Idle (dir 0) is the overwhelmingly
        // common case and costs a single compare.
        void advanceXfade() noexcept
        {
            if (xfadeDir == 0) return;
            xfadeGain += (float) xfadeDir * xfadeStep;
            if      (xfadeGain >= 1.0f) { xfadeGain = 1.0f; xfadeDir = 0; }
            else if (xfadeGain <= 0.0f) { xfadeGain = 0.0f; }   // hold; swap next block
        }

        //======================================================================
        //  State
        //======================================================================
        double sampleRate  = 44100.0;
        int    algo        = AlgoHallRoom;   // engine currently clocked
        float  fdnDampCoef[kN] {};           // per line — see processFdn
        int    pendingAlgo = AlgoHallRoom;   // engine requested via setAlgorithm()
        bool   primed      = false;

        // engine-swap crossfade
        float  xfadeGain = 1.0f;             // wet trim, 1 = normal
        int    xfadeDir  = 0;                // 0 idle, -1 fading out, +1 fading in
        float  xfadeStep = 0.0f;             // per-sample increment (20 ms)

        // shared pre-delay.  preCur is the tap currently sounding and preTarget
        // the one being faded in -- see the crossfade in processFdn.  They are
        // equal whenever preXfade has reached 1.
        DelayLine preL, preR;
        float     preTarget = 0.0f, preCur = 0.0f;
        float     preXfade = 1.0f, preXfadeStep = 0.0f;

        // HALL/ROOM
        DelayLine diff[kDiffStages][kN];
        float     diffDelay[kDiffStages][kN] {};
        float     diffSign [kDiffStages][kN] {};

        DelayLine fdn[kN];
        float     fdnTarget[kN] {}, fdnCur[kN] {}, fdnGain[kN] {};
        float     fdnLp[kN] {},     fdnHp[kN] {};
        // Hall send high-pass state (one per output channel) — keeps low
        // energy out of the tank.  See kHallSendHpHz.
        float     sendHpL = 0.0f,   sendHpR = 0.0f;
        float     sendLpL = 0.0f,   sendLpR = 0.0f;
        float     lfoPhase[kN] {},  lfoInc[kN] {};

        // PLATE
        DelayLine pAp[4];
        float     pApDelay[4] {}, pApGain[4] {};
        DelayLine tApA1, tDlA1, tApA2, tDlA2;
        DelayLine tApB1, tDlB1, tApB2, tDlB2;
        float     dApA1 = 0.0f, dDlA1 = 0.0f, dApA2 = 0.0f, dDlA2 = 0.0f;
        float     dApB1 = 0.0f, dDlB1 = 0.0f, dApB2 = 0.0f, dDlB2 = 0.0f;
        float     pTapL[7] {}, pTapR[7] {};
        float     pBw = 0.0f, pDampA = 0.0f, pDampB = 0.0f, pTankFb = 0.0f;
        float     pExcursion = 0.0f, pLfoPhase = 0.0f, pLfoInc = 0.0f;
    };

    //==========================================================================
    //  StereoDelayFx — ping-pong delay (each channel feeds back into the other)
    //  with one-pole HF damping in the feedback path.  (Bambino StereoDelay.)
    //  The channel computes `delaySamples` from musical time + host BPM and
    //  passes it in, preserving Grex's tempo-synced delay UX.
    //  process: delaySamples, fb01 (0..0.95), mix01.
    //==========================================================================
    struct StereoDelayFx
    {
        double    sampleRate = 44100.0;
        DelayLine lineL, lineR;
        OnePoleLP dampL, dampR;

        void prepare (double sr)
        {
            sampleRate = sr;
            const int maxSamples = (int) (sr * 2.5);   // 2.5 s max
            lineL.prepare (maxSamples); lineR.prepare (maxSamples);
            dampL.prepare (sr); dampL.setCutoff (5000.0f);
            dampR.prepare (sr); dampR.setCutoff (5000.0f);
            reset();
        }
        void reset() { lineL.reset(); lineR.reset(); dampL.reset(); dampR.reset(); }

        // dry01 IS INDEPENDENT OF mix01 - it is not 1-mix any more.
        //
        // This used to be a blend: the wet amount also decided how much dry
        // survived, so you could not have the source at full level with a delay
        // on top without the delay eating into it.  The reverb never worked that
        // way (ReverbFx has always taken dry and wet separately), so the two
        // effects behaved differently for no reason a user could see.
        //
        // Defaulted to 1.0 so any caller that has not been updated gets the
        // source untouched, which is the sane end of the range.
        void process (float* L, float* R, int n,
                      float delaySamples, float fb01, float mix01, double sr,
                      float dry01 = 1.0f)
        {
            juce::ignoreUnused (sr);
            const float delS     = juce::jlimit (1.0f, (float) (sampleRate * 2.4), delaySamples);
            const float feedback = juce::jlimit (0.0f, 0.95f, fb01);
            const float mixP     = juce::jlimit (0.0f, 1.0f, mix01);
            const float dryP     = juce::jlimit (0.0f, 1.0f, dry01);

            for (int i = 0; i < n; ++i)
            {
                const float inL = L[i], inR = R[i];
                const float dL = lineL.read (delS);
                const float dR = lineR.read (delS);
                const float dampedL = dampL.processL (dR);   // ping-pong cross-feedback
                const float dampedR = dampR.processR (dL);
                lineL.write (inL + dampedL * feedback);
                lineR.write (inR + dampedR * feedback);
                L[i] = inL * dryP + dL * mixP;
                R[i] = inR * dryP + dR * mixP;
            }
        }
    };
}


