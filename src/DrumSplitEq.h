#pragma once
//==============================================================================
// DrumSplitEq.h - the drum rack's 10-band EQ, built as a real band SPLIT.
//
// EACH SLIDER OWNS ITS OWN BAND, AND ONLY IT.  The kit is divided into ten
// octave bands at the midpoints between the slider captions, and every slider
// is simply the volume of its band:
//
//     31    everything below 44 Hz        1k    707 Hz .. 1.4 kHz
//     62    44 .. 88 Hz                   2k    1.4 .. 2.8 kHz
//     125   88 .. 177 Hz                  4k    2.8 .. 5.7 kHz
//     250   177 .. 354 Hz                 8k    5.7 .. 11.3 kHz
//     500   354 .. 707 Hz                 16k   everything above 11.3 kHz
//
// WHY IT REPLACED THE TEN BELL FILTERS.  A bell is not a band.  Ten of them
// overlap, so a slider reached into its neighbours (at the bottom, the 62
// slider also took 24 dB off 125 Hz and 10 dB off 500 Hz), and ten sliders at
// the same position did not give that level: all at -12 dB came out anywhere
// between -2.4 and -18.5 dB depending on the frequency.
//
// THE SPLIT.  Nine Linkwitz-Riley 8th-order crossovers (48 dB per octave),
// taken lowest first.  The LR8 low-pass is the 4th-order Butterworth low-pass
// squared, and LR8 low-pass + LR8 high-pass is exactly an all-pass - so the
// high side is computed as (all-pass - low-pass) rather than by four more
// sections, and the ten bands are summed back through the same all-passes in
// Horner order (eight all-pass pairs, not thirty-six), which lines every band
// up in phase with every other.
//
// WHAT THAT GIVES:
//   * every slider in the middle      -> the kit's own level, flat, 20 Hz..20 kHz
//   * every slider at the same place  -> that level, flat
//   * one slider at the bottom        -> its band muted: about -19 dB at its
//                                        centre, -6 dB at its borders, and the
//                                        neighbours' centres untouched (-0.5 dB)
//
// NO LATENCY - THE FIRST LAW.  Every filter here is minimum phase; nothing
// looks ahead and nothing is added to the plugin's latency.  The price is the
// slope: 48 dB per octave cannot wall a one-octave band off completely, which
// is why a muted band keeps a little of its neighbours at its edges.  A true
// wall needs a linear-phase filter, and a linear-phase filter IS latency.
//
// THE SLIDER RULE lives here too, so both drum editors and the EDM kit file
// read one definition: the bottom half of a slider runs silence .. unity, the
// top half unity .. +24 dB.  kSilenceDb is where a band is muted outright.
//
// DOUBLE PRECISION on purpose: the lowest split sits at 44 Hz, where float
// coefficients start to blur the poles, and the flat sum relies on the
// low-pass and the all-pass matching to the last digit.
//
// No JUCE in here: the engine, both editors and the EDM kit parser include it,
// and a test harness can build it on its own.
//==============================================================================
#include <cmath>
#include <algorithm>

namespace Betel
{
    struct DrumSplitEq
    {
        static constexpr int kBands  = 10;
        static constexpr int kSplits = kBands - 1;

        // The slider captions - the centre of each band.
        static constexpr float kCentreHz [kBands] =
            { 31.0f, 62.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };

        // THE SLIDER RULE.  Bottom = silence, middle = unity, top = +24 dB.
        static constexpr float kSilenceDb = -60.0f;   // at or below: the band is muted (gain 0)
        static constexpr float kMaxDb     =  24.0f;

        // Slider position t (0..1, 0.5 = unity) <-> band gain in dB.  Linear in
        // dB within each half: the bottom half spans 60 dB, the top half 24.
        static float dbFromPosition (float t) noexcept
        {
            t = std::min (1.0f, std::max (0.0f, t));
            return t <= 0.5f ? kSilenceDb * (1.0f - t / 0.5f)
                             : kMaxDb * ((t - 0.5f) / 0.5f);
        }

        static float positionFromDb (float db) noexcept
        {
            if (! (db > kSilenceDb)) return 0.0f;                 // silence (and NaN)
            if (db <= 0.0f) return 0.5f * (1.0f - db / kSilenceDb);
            return 0.5f + 0.5f * std::min (1.0f, db / kMaxDb);
        }

        // The band's linear gain.  Exactly 0 at the bottom of the slider - a
        // muted band, not a quiet one.
        static float gainFromDb (float db) noexcept
        {
            if (! (db > kSilenceDb + 0.001f)) return 0.0f;
            return std::pow (10.0f, std::min (db, kMaxDb) / 20.0f);
        }

        // Message thread (prepare): the nine crossovers for this sample rate.
        void design (double sampleRate) noexcept
        {
            const double fs  = sampleRate > 1000.0 ? sampleRate : 44100.0;
            const double kPi = 3.14159265358979323846;

            // The 4th-order Butterworth's two sections.
            const double q [2] = { 1.0 / (2.0 * std::sin (kPi / 8.0)),           // 1.3066
                                   1.0 / (2.0 * std::sin (3.0 * kPi / 8.0)) };   // 0.5412

            for (int k = 0; k < kSplits; ++k)
            {
                // Halfway, in octaves, between two captions:
                // 1000 * 2^(k - 4.5) = 44, 88, 177, 354, 707, 1414, 2828, 5657, 11314 Hz.
                // Held under Nyquist so a low host rate cannot fold the top split.
                const double fc = std::min (1000.0 * std::pow (2.0, (double) k - 4.5), 0.45 * fs);

                const double w0 = 2.0 * kPi * fc / fs;
                const double cw = std::cos (w0);
                const double sw = std::sin (w0);

                for (int s = 0; s < 2; ++s)
                {
                    const double alpha = sw / (2.0 * q[s]);
                    const double a0    = 1.0 + alpha;

                    // RBJ low-pass ...
                    lp[k][s].b0 = ((1.0 - cw) * 0.5) / a0;
                    lp[k][s].b1 =  (1.0 - cw)        / a0;
                    lp[k][s].b2 = ((1.0 - cw) * 0.5) / a0;
                    lp[k][s].a1 = (-2.0 * cw)        / a0;
                    lp[k][s].a2 = (1.0 - alpha)      / a0;

                    // ... and the RBJ all-pass on the same poles.  Low-pass
                    // squared plus high-pass squared equals these two in series.
                    ap[k][s].b0 = (1.0 - alpha) / a0;
                    ap[k][s].b1 = (-2.0 * cw)   / a0;
                    ap[k][s].b2 = 1.0;                  // (1 + alpha) / a0
                    ap[k][s].a1 = (-2.0 * cw)   / a0;
                    ap[k][s].a2 = (1.0 - alpha) / a0;
                }
            }
        }

        // Filter memory to zero, gains to unity, stage out.
        void reset() noexcept
        {
            clearStates();
            for (auto& g : gainNow) g = 1.0f;
            running = false;
        }

        // Audio thread, in place.  targetDb: the ten band gains in dB.  enabled:
        // the EQ stage switch.  R may be null for mono.
        //
        // NOTHING CLICKS: gains glide across the block, and switching the stage
        // in or out crossfades against the untouched signal over one block.
        // While every band is flat the stage is not run at all, so an untouched
        // EQ costs nothing.
        void process (float* L, float* R, int numSamples,
                      const float* targetDb, bool enabled) noexcept
        {
            if (L == nullptr || numSamples <= 0) return;

            float target [kBands];
            bool  anyOff = false;
            for (int b = 0; b < kBands; ++b)
            {
                target[b] = gainFromDb (targetDb[b]);
                if (std::abs (target[b]  - 1.0f) > 1.0e-4f
                 || std::abs (gainNow[b] - 1.0f) > 1.0e-4f)
                    anyOff = true;
            }
            const bool want = enabled && anyOff;

            if (! running && ! want) return;

            // 0 = steady, +1 = fading in from the dry signal, -1 = fading out to it
            int fade = 0;
            if (! running)
            {
                clearStates();          // memory from a stage that stopped long ago
                running = true;
                fade = +1;
            }
            else if (! want)
            {
                fade = -1;
            }

            const double inv = 1.0 / (double) numSamples;
            double g [kBands], dg [kBands];
            for (int b = 0; b < kBands; ++b)
            {
                g[b]  = (double) gainNow[b];
                dg[b] = ((double) target[b] - g[b]) * inv;
            }

            for (int n = 0; n < numSamples; ++n)
            {
                for (int b = 0; b < kBands; ++b) g[b] += dg[b];     // lands on target at the last sample

                double w = 1.0;
                if (fade != 0)
                {
                    const double t = (double) (n + 1) * inv;
                    w = fade > 0 ? t : 1.0 - t;
                }

                const double xl = (double) L[n];
                const double yl = processSide (side[0], xl, g);
                L[n] = (float) (xl + (yl - xl) * w);

                if (R != nullptr)
                {
                    const double xr = (double) R[n];
                    const double yr = processSide (side[1], xr, g);
                    R[n] = (float) (xr + (yr - xr) * w);
                }
            }

            for (int b = 0; b < kBands; ++b) gainNow[b] = target[b];
            if (fade < 0) running = false;
        }

    private:
        struct Coefs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
        struct State { double z1 = 0.0, z2 = 0.0; };

        struct Side
        {
            State lp   [kSplits][4];   // LR8 low-pass: sections Q1, Q2, Q1, Q2
            State ap   [kSplits][2];   // the split's all-pass (gives the high side)
            State comp [kSplits][2];   // the recombination all-pass (splits 1..8)
        };

        // Direct form II transposed.
        static double run (const Coefs& c, State& s, double x) noexcept
        {
            const double y = c.b0 * x + s.z1;
            s.z1 = c.b1 * x - c.a1 * y + s.z2;
            s.z2 = c.b2 * x - c.a2 * y;
            return y;
        }

        // One channel, one sample: split lowest first, scale each band, and sum
        // the bands back through the all-passes they skipped (Horner order), so
        // every band arrives in phase and a flat setting sums flat.
        double processSide (Side& sd, double x, const double* g) noexcept
        {
            double rest = x;      // what is still above the splits done so far
            double acc  = 0.0;    // the bands summed so far
            for (int k = 0; k < kSplits; ++k)
            {
                double lo = run (lp[k][0], sd.lp[k][0], rest);
                lo = run (lp[k][1], sd.lp[k][1], lo);
                lo = run (lp[k][0], sd.lp[k][2], lo);
                lo = run (lp[k][1], sd.lp[k][3], lo);

                double a = run (ap[k][0], sd.ap[k][0], rest);
                a = run (ap[k][1], sd.ap[k][1], a);

                if (k == 0)
                {
                    acc = g[0] * lo;
                }
                else
                {
                    double c = run (ap[k][0], sd.comp[k][0], acc);
                    c = run (ap[k][1], sd.comp[k][1], c);
                    acc = c + g[k] * lo;
                }

                rest = a - lo;    // the high side: all-pass minus low-pass
            }
            return acc + g[kSplits] * rest;
        }

        void clearStates() noexcept
        {
            for (auto& sd : side) sd = Side{};
        }

        Coefs lp [kSplits][2];     // the Butterworth sections as low-pass
        Coefs ap [kSplits][2];     // ... and as all-pass
        Side  side [2];
        float gainNow [kBands] { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        bool  running = false;
    };
}
