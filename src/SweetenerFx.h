#pragma once
//==============================================================================
// SweetenerFx.h  —  the per-slot SWEETENER.
//
// One insert block, three stages, on every channel — melodic and drum alike.
// It exists because Grex had no dynamics per slot at all: SoundsFx gives every
// channel Chorus / Wah / Phaser / Delay / Reverb, all time-based, and the only
// thing that could tame a hot instrument was the Finisher on the MASTER bus.
// A hot converted-style kit therefore pushed the master glue compressor and
// ducked the WHOLE mix — the aggression was spread rather than fixed.
//
// ── WHY THREE STAGES AND NOT ONE COMPRESSOR ──────────────────────────────────
//
// "Aggression" is three different problems and a compressor is the wrong answer
// to all of them:
//
//   1. PEAK TRANSIENTS.  A sampled snare peaks 10-15 dB above its body inside
//      1-3 ms.  A normal compressor is precisely backwards here: its attack
//      lets the transient through and then squashes the body, so you keep the
//      crack and lose the weight behind it.  SOFTEN works on the transient
//      itself, not on level.
//
//   2. THE HARSHNESS BAND.  2.5-6 kHz: snare crack, hat sizzle, brass bite.  A
//      static EQ cut kills the air on quiet passages too.  TAME only acts when
//      that band is actually dominant.
//
//   3. PEAK STACKING.  Several notes landing together summing to a hard peak.
//      ROUND rounds it, and the even harmonics read as warmth, not level.
//
// ── LEVEL-INDEPENDENT BY DESIGN ──────────────────────────────────────────────
//
// Neither SOFTEN nor TAME has a threshold, and that is deliberate.  A threshold
// would make every setting depend on the channel fader, the style's CC 7, the
// per-sound GAIN trim and the loudness makeup — four things that move on their
// own in this plugin.  Instead both stages measure a RATIO:
//
//   SOFTEN  fast envelope / slow envelope   ->  "how transient is this"
//   TAME    high-band energy / full energy  ->  "how harsh is this"
//
// Both are dimensionless, so a setting that works at one level works at every
// level, and a preset survives a style change.
//
// ── ZERO LATENCY ─────────────────────────────────────────────────────────────
//
// No lookahead anywhere, matching the Finisher's existing decision.  A
// lookahead limiter catches peaks a one-pole follower rounds instead, but it
// would put the whole plugin out of sample alignment for a feature meant to
// sit on 16 channels at once.  Rounding is what a sweetener wants anyway.
//
// Everything is one-pole followers and a single tanh: about 20 operations per
// sample per channel, negligible beside ReverbFx.
//
// Thread model: process() runs on the AUDIO thread and takes its Params BY
// VALUE from the caller, which reads them out of atomics once per block.  The
// three metering values are atomics written here and read by the UI timer.
//==============================================================================

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace Betel
{
    class SweetenerFx
    {
    public:
        //======================================================================
        // Parameters.  Native units, matching what the panels store.
        //======================================================================
        struct Params
        {
            bool  enabled = false;
            float mix     = 1.0f;      // 0..1 parallel blend of the WHOLE block

            // SOFTEN — bipolar.  Negative softens the attack, positive sharpens
            // it, so the same stage can put life back into a dull kit.
            bool  softenOn    = true;
            float softenDepth = 0.0f;   // -1..+1
            float softenMs    = 8.0f;   // 1..150  how much of the hit is treated

            // PEAK — the reducer.  SOFTEN answers "is this a transient"; this
            // one answers "how far above its own body is this allowed to poke",
            // which is the question a savage hit actually loses on.
            bool  peakOn       = true;
            float peakCeilDb   = 12.0f; // 3..24  headroom over the running body
            float peakRatio    = 4.0f;  // 1..20  1 = off, 20 = limiting

            // TAME — dynamic high shelf keyed on brightness, not level.
            bool  tameOn      = true;
            float tameDepthDb = 0.0f;  // 0..12  maximum duck
            float tameFreqHz  = 4000.0f;

            // ROUND — tanh saturation, peak-normalised, parallel.
            bool  roundOn    = true;
            float roundDrive = 0.0f;   // 0..1
            float roundMix   = 1.0f;   // 0..1
        };

        //======================================================================
        void prepare (double sampleRate) noexcept
        {
            sr = (sampleRate > 1000.0) ? sampleRate : 44100.0;
            reset();
        }

        void reset() noexcept
        {
            for (int c = 0; c < 2; ++c)
            {
                fastEnv[c] = slowEnv[c] = 0.0f;
                highEnv[c] = fullEnv[c] = 0.0f;
                lpZ[c]     = 0.0f;
            }
            softGain = tameGain = peakGain = 1.0f;
            bodyEnv = peakEnv = 0.0f;
            grSoften.store (0.0f);
            grTame  .store (0.0f);
            grPeak  .store (0.0f);
            grRound .store (0.0f);
        }

        /** In-place, stereo (R may alias L for a mono channel). */
        void process (float* L, float* R, int numSamples, const Params& p) noexcept
        {
            if (! p.enabled || numSamples <= 0 || L == nullptr)
            {
                grSoften.store (0.0f); grTame.store (0.0f);
                grPeak.store (0.0f);   grRound.store (0.0f);
                return;
            }
            if (R == nullptr) R = L;

            const float mix = juce::jlimit (0.0f, 1.0f, p.mix);
            if (mix <= 1.0e-4f)
            {
                grSoften.store (0.0f); grTame.store (0.0f);
                grPeak.store (0.0f);   grRound.store (0.0f);
                return;
            }
            const float dryAmt = 1.0f - mix;

            // ── Coefficients, once per block ─────────────────────────────────
            // WINDOW REACHES 150 ms NOW, NOT 30.
            //
            // At 30 ms the reduction was over almost as soon as it arrived, so
            // SOFTEN only ever shaved the tick off the very front of a hit and
            // the body behind it came through at full size.  A window that can
            // cover the WHOLE hit is what makes the stage audible on a savage
            // kit, and it is the single change that took it from subtle to
            // useful.
            const float windowMs = juce::jlimit (1.0f, 150.0f, p.softenMs);
            const float aFast    = onePole (0.5f);              // 0.5 ms attack
            const float rFast    = onePole (windowMs);
            const float aSlow    = onePole (windowMs);
            const float rSlow    = onePole (windowMs * 4.0f);

            // GAIN SMOOTHING IS ASYMMETRIC, AND THIS IS WHY.
            //
            // The first build used one 2 ms one-pole for both directions, which
            // is SLOWER THAN THE TRANSIENT IT IS MEANT TO CATCH.  Measured at
            // 48 kHz against a -3.7 dB request: 0.5 ms into the hit the gain had
            // only reached -0.70 dB, 1 ms -1.29 dB, 2 ms -2.17 dB.  The peak was
            // already gone by the time the reduction arrived, so the stage left
            // the attack untouched and ducked the BODY behind it — which makes a
            // kit sound PUNCHIER, not softer.  That is the exact compressor
            // failure mode this stage exists to avoid, and it is what Rob heard
            // as "adding energy".
            //
            // Down is now effectively instant and up is gentle, so the reduction
            // lands ON the peak and recovers over the window.
            const float gAtk     = onePole (0.05f);             // catch the peak
            const float gRel     = onePole (windowMs * 3.0f);   // let the body back

            const float tameCoef = std::exp (-2.0f * juce::MathConstants<float>::pi
                                             * juce::jlimit (500.0f, 12000.0f, p.tameFreqHz)
                                             / (float) sr);
            const float envCoef  = onePole (12.0f);             // brightness follower
            const float tameSmooth = onePole (10.0f);           // shelf de-zipper

            // PEAK: a fast peak follower against a slow BODY follower.  The body
            // is what makes the threshold automatic - see the stage itself.
            const float pkAtk   = onePole (0.05f);
            const float pkRel   = onePole (juce::jmax (20.0f, windowMs * 2.0f));
            const float bodyCf  = onePole (150.0f);
            const float pkGAtk  = onePole (0.08f);
            const float pkGRel  = onePole (juce::jmax (40.0f, windowMs * 3.0f));

            const float softDepth = juce::jlimit (-1.0f, 1.0f, p.softenDepth);
            const float tameMaxDb = juce::jlimit (0.0f, 12.0f, p.tameDepthDb);
            const float peakCeil  = juce::jlimit (3.0f, 24.0f, p.peakCeilDb);
            const float peakRat   = juce::jlimit (1.0f, 20.0f, p.peakRatio);
            const float drive     = juce::jlimit (0.0f, 1.0f, p.roundDrive);
            const float roundMix  = juce::jlimit (0.0f, 1.0f, p.roundMix);

            const bool  doSoft  = p.softenOn && std::abs (softDepth) > 1.0e-3f;
            const bool  doTame  = p.tameOn   && tameMaxDb > 1.0e-3f;
            const bool  doPeak  = p.peakOn   && peakRat > 1.01f;
            const bool  doRound = p.roundOn  && drive > 1.0e-3f && roundMix > 1.0e-4f;

            if (! (doSoft || doTame || doPeak || doRound))
            {
                grSoften.store (0.0f); grTame.store (0.0f);
                grPeak.store (0.0f);   grRound.store (0.0f);
                return;
            }

            // ROUND IS NORMALISED BY THE SLOPE, NOT BY THE PEAK.
            //
            // The first build divided by tanh(pre), which normalises so that
            // FULL SCALE in gives full scale out — and therefore multiplies
            // everything BELOW full scale by pre/tanh(pre).  Musical material
            // lives well below full scale, so that was not a saturator at all,
            // it was a straight gain stage:
            //
            //     drive 0.10 -> +5.6 dB      drive 0.35 -> +11.6 dB
            //     drive 0.20 -> +8.4 dB      drive 1.00 -> +19.1 dB
            //
            // and 0.20 was the shipped default on both kit slots.  That single
            // line is why the sweetener sounded louder and more aggressive
            // rather than softer.
            //
            // Dividing by `pre` normalises the SMALL-SIGNAL SLOPE instead: quiet
            // material passes at exactly unity and only the loud part bends.
            // That is the correct shape for a stage that must sit on 16 channels
            // without touching the gain staging.
            const float pre    = 1.0f + drive * 8.0f;
            const float invSat = doRound ? (1.0f / pre) : 1.0f;

            float peakSoftGr = 0.0f, peakTameGr = 0.0f,
                  peakPeakGr = 0.0f, peakRoundGr = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                const float dryL = L[i], dryR = R[i];
                float l = dryL, r = dryR;

                // ── 1. SOFTEN ────────────────────────────────────────────────
                // fast/slow envelope RATIO, not level - see the header note.
                if (doSoft)
                {
                    const float m = 0.5f * (std::abs (l) + std::abs (r));

                    fastEnv[0] = follow (fastEnv[0], m, aFast, rFast);
                    slowEnv[0] = follow (slowEnv[0], m, aSlow, rSlow);

                    // ratio > 1 means the signal is rising faster than its own
                    // body: that IS the transient, at any level.
                    const float ratio = fastEnv[0] / juce::jmax (slowEnv[0], 1.0e-6f);

                    // x0.5, not x0.25: full transient-ness is now reached at a
                    // ratio of 3 instead of 5, so an ordinary hit gets the whole
                    // of DEPTH rather than a fraction of it.  Only a genuinely
                    // gentle stroke stays under the knee.
                    const float tr    = juce::jlimit (0.0f, 1.0f, (ratio - 1.0f) * 0.5f);

                    const float target = juce::jlimit (0.125f, 4.0f, 1.0f - softDepth * tr);
                    softGain += (target - softGain) * (target < softGain ? gAtk : gRel);

                    l *= softGain;
                    r *= softGain;

                    if (softGain < 1.0f)
                        peakSoftGr = juce::jmax (peakSoftGr, -20.0f * std::log10 (juce::jmax (softGain, 1.0e-4f)));
                }

                // ── 1b. PEAK — THE REDUCER ───────────────────────────────────
                //
                // A real downward compressor, but with an AUTOMATIC threshold,
                // and that is the whole idea.  A fixed threshold on a per-slot
                // insert is worthless here: it would move with the channel
                // fader, the style's CC 7, the per-sound GAIN trim and the
                // loudness makeup, so a setting that worked on one style would
                // be inert or brutal on the next.
                //
                // Instead the threshold IS the material.  A slow 150 ms body
                // follower says how loud this part has been running; a fast peak
                // follower says how loud this hit is.  CEILING is how far above
                // its own body a hit may poke before the reducer takes over, so
                // "savage" is defined relative to the performance rather than to
                // an absolute number that nothing here can predict.
                //
                // CEILING 12 dB / RATIO 4 is polite.  CEILING 4 / RATIO 20 is
                // a limiter that flattens anything sticking out of the groove.
                if (doPeak)
                {
                    const float m = juce::jmax (std::abs (l), std::abs (r));

                    peakEnv = follow (peakEnv, m, pkAtk, pkRel);
                    bodyEnv = bodyEnv + (m - bodyEnv) * bodyCf;

                    const float overDb = 20.0f * std::log10 (
                        juce::jmax (peakEnv, 1.0e-6f) / juce::jmax (bodyEnv, 1.0e-6f));

                    const float excess = juce::jmax (0.0f, overDb - peakCeil);
                    const float grDb   = excess * (1.0f - 1.0f / peakRat);
                    const float target = std::pow (10.0f, -grDb / 20.0f);

                    peakGain += (target - peakGain) * (target < peakGain ? pkGAtk : pkGRel);

                    l *= peakGain;
                    r *= peakGain;

                    if (peakGain < 1.0f)
                        peakPeakGr = juce::jmax (peakPeakGr,
                                                 -20.0f * std::log10 (juce::jmax (peakGain, 1.0e-4f)));
                }

                // ── 2. TAME ──────────────────────────────────────────────────
                // One-pole split, then duck the top by how much of the total
                // energy lives up there.  Brightness, not loudness.
                if (doTame)
                {
                    lpZ[0] = l + tameCoef * (lpZ[0] - l);
                    lpZ[1] = r + tameCoef * (lpZ[1] - r);
                    const float lowL = lpZ[0], lowR = lpZ[1];
                    const float hiL  = l - lowL, hiR = r - lowR;

                    const float hMag = 0.5f * (std::abs (hiL) + std::abs (hiR));
                    const float fMag = 0.5f * (std::abs (l)   + std::abs (r));
                    highEnv[0] += (hMag - highEnv[0]) * envCoef;
                    fullEnv[0] += (fMag - fullEnv[0]) * envCoef;

                    // 0 at "half the energy is up there", 1 at "almost all of
                    // it".  Below 0.5 nothing happens, so warm material is
                    // untouched no matter how loud it gets.
                    const float harsh = highEnv[0] / juce::jmax (fullEnv[0], 1.0e-6f);
                    const float amt   = juce::jlimit (0.0f, 1.0f, (harsh - 0.5f) * 2.0f);

                    // TAME deliberately keeps a SLOW smoother.  It is a shelf,
                    // not a transient catcher: moving it at SOFTEN's speed would
                    // modulate the top end audibly instead of leaning on it.
                    const float target = std::pow (10.0f, -(tameMaxDb * amt) / 20.0f);
                    tameGain += (target - tameGain) * tameSmooth;

                    l = lowL + hiL * tameGain;
                    r = lowR + hiR * tameGain;

                    if (tameGain < 1.0f)
                        peakTameGr = juce::jmax (peakTameGr, -20.0f * std::log10 (juce::jmax (tameGain, 1.0e-4f)));
                }

                // ── 3. ROUND ─────────────────────────────────────────────────
                if (doRound)
                {
                    const float satL = std::tanh (l * pre) * invSat;
                    const float satR = std::tanh (r * pre) * invSat;

                    const float before = juce::jmax (std::abs (l), std::abs (r));
                    const float after  = juce::jmax (std::abs (satL), std::abs (satR));
                    if (before > 1.0e-4f && after < before)
                        peakRoundGr = juce::jmax (peakRoundGr,
                                                  -20.0f * std::log10 (juce::jmax (after / before, 1.0e-4f)));

                    l += (satL - l) * roundMix;
                    r += (satR - r) * roundMix;
                }

                // ── Parallel blend of the whole block ────────────────────────
                L[i] = dryL * dryAmt + l * mix;
                R[i] = dryR * dryAmt + r * mix;
            }

            grSoften.store (peakSoftGr);
            grTame  .store (peakTameGr);
            grPeak  .store (peakPeakGr);
            grRound .store (peakRoundGr);
        }

        // ── Metering, in dB of reduction (0 = doing nothing) ─────────────────
        float getSoftenGrDb() const noexcept { return grSoften.load(); }
        float getTameGrDb()   const noexcept { return grTame  .load(); }
        float getPeakGrDb()   const noexcept { return grPeak  .load(); }
        float getRoundGrDb()  const noexcept { return grRound .load(); }

    private:
        /** One-pole coefficient for a time constant in milliseconds. */
        float onePole (float ms) const noexcept
        {
            const float t = juce::jmax (0.05f, ms) * 0.001f * (float) sr;
            return 1.0f - std::exp (-1.0f / juce::jmax (1.0f, t));
        }

        static float follow (float env, float in, float atk, float rel) noexcept
        {
            return env + (in - env) * (in > env ? atk : rel);
        }

        double sr = 44100.0;

        float fastEnv[2] {}, slowEnv[2] {};
        float highEnv[2] {}, fullEnv[2] {};
        float lpZ[2] {};
        float softGain = 1.0f, tameGain = 1.0f, peakGain = 1.0f;
        float bodyEnv  = 0.0f, peakEnv  = 0.0f;

        std::atomic<float> grSoften { 0.0f }, grTame  { 0.0f },
                           grPeak   { 0.0f }, grRound { 0.0f };
    };
} // namespace Betel
