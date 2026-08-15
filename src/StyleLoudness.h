#pragma once
#include <JuceHeader.h>
#include "GlobalMacros.h"     // GrexPaths — single root for everything Grex owns
#include "StyleData.h"        // StyleSection
#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
// StyleLoudness — per-section loudness measurement and corrective trim.
//
// WHY THIS EXISTS
//
//   The per-style makeup gain that came before this measured SYMBOLS, not
//   sound: it summed (slot gain x velocity) per tick across the four mains and
//   matched the 95th percentile to a target.  Nothing in that calculation ever
//   saw a sample.  A flute at velocity 60 and a grand piano at velocity 60
//   counted the same; note decay, the sample's own amplitude, the drum kit
//   calibration and the whole FX chain were invisible to it.
//
//   Measured on two real files, the consequences were:
//     - Unplugged16Bt_S558 (Korg conversion) mains p95 0.416 -> makeup +9.2 dB
//     - Bigger_Band_S162   (Yamaha)          mains p95 1.571 -> makeup -2.3 dB
//   11.5 dB apart, neither clamped, both "normalised" to the same target.  And
//   because a percentile is a DENSITY measure, the two files' crest factors
//   (+8.9 dB and +4.0 dB over their own p95) left the Korg style's peaks about
//   5 dB hotter even after normalisation.  Within Unplugged alone, after its
//   own makeup, Main A landed 0.1 dB from target while Ending C sat 11.9 dB
//   over it.
//
// WHAT THIS DOES INSTEAD
//
//   Measures the real audio, per section, with the broadcast standard for
//   exactly this problem: ITU-R BS.1770 K-weighted, gated, integrated loudness.
//   Correct by construction, because it is the signal you actually hear —
//   samples, envelopes, kit calibration, FX and all.
//
//   The measurement runs on the LIVE output rather than an offline render.
//   That was a deliberate choice over spinning up a second engine at load:
//     - no duplicate engine, no duplicate sample memory, no second preset load
//     - no risk of a half-second stall when a style is changed mid-performance
//     - it measures the true signal path instead of a simulation of it
//   The cost is that a section is uncorrected the FIRST time it plays.  Results
//   are cached per style file, so that happens once ever, not once per load.
//
// SELF-REFERENCE
//
//   The meter hears audio that already has the trim applied, so every stored
//   figure is referred back to unity (measured LUFS minus the trim that was
//   active while measuring).  Without that the loop would chase its own tail
//   and oscillate.  With it, re-measuring a section that is already correct
//   yields the same answer, and the correction is idempotent.
//
// PARTIAL CORRECTION
//
//   An ending SHOULD be bigger than a main; flattening every section to one
//   figure would iron the arrangement into a wall.  So there is a dead band:
//   anything within kDeadBandLU of target is left completely alone, and only
//   the EXCESS beyond it is removed.  The guarantee is "no section is ever more
//   than the dead band away from target", while the musical contrast inside
//   that window survives untouched.
//==============================================================================

namespace Betel
{
    class StyleLoudness
    {
    public:
        //----------------------------------------------------------------------
        // Tuning.  Deliberately conservative — this is meant to be auditioned
        // and adjusted, not trusted blind.
        //----------------------------------------------------------------------
        static constexpr float kDefaultTargetLufs =  -20.0f;  // style-bus target
        static constexpr float kDeadBandLU        =    3.0f;  // untouched window
        static constexpr float kMaxTrimDb         =   12.0f;  // clamp either way
        static constexpr float kTruePeakCeiling   =   -1.0f;  // dBFS, peak safety

        // A section must contribute at least this much gated audio before its
        // measurement is trusted.  Roughly two bars at a slow tempo; short fills
        // simply take a few passes to qualify, which is the correct behaviour —
        // a one-bar fill measured off half a bar would be noise.
        static constexpr int   kMinBlocksToPublish = 20;      // 20 x 100 ms hops

        static constexpr int   kMaxSections        = 32;      // StyleSection::Count + slack
        static constexpr int   kMaxBlocks          = 512;     // ~51 s of hops per section

        //======================================================================
        // Lifecycle
        //======================================================================
        void prepare (double sampleRate)
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            designKWeighting();
            hopSamples   = (int) std::llround (sr * 0.100);   // 100 ms
            blockSamples = (int) std::llround (sr * 0.400);   // 400 ms
            if (hopSamples   < 1) hopSamples   = 1;
            if (blockSamples < 1) blockSamples = 1;
            resetFilters();
            resetAccumulator();
        }

        /** Point the meter at a style file.  Message thread only.  Loads any
            cached measurement for this exact file; a style whose size or
            modification date changed is re-measured from scratch. */
        void setStyle (const juce::File& styleFile)
        {
            commitPending();                 // don't lose the outgoing style's work
            save();

            stylePath = styleFile.getFullPathName();
            styleSize = styleFile.existsAsFile() ? (juce::int64) styleFile.getSize() : 0;
            styleTime = styleFile.existsAsFile()
                          ? styleFile.getLastModificationTime().toMilliseconds() : 0;

            for (auto& s : sections) s.clear();
            resetAccumulator();
            loadCacheForCurrentStyle();
            dirty.store (false);
        }

        //======================================================================
        // Audio thread
        //======================================================================
        /** Called when the sequencer changes section.  Finalises the outgoing
            section's measurement and arms the incoming one. */
        void setSection (int sectionId) noexcept
        {
            if (sectionId == currentSection) return;
            commitPending();
            currentSection = juce::jlimit (-1, kMaxSections - 1, sectionId);
            resetAccumulator();
            // The first process() call of the new section adopts the reference
            // gain; until then nothing is accumulated.  Marked out of range so
            // that adoption always happens rather than inheriting the outgoing
            // section's figure.
            pendingTrimDb = -1000.0f;
        }

        /** Feed the rendered output.  `measurable` must be false whenever the
            block is not a clean look at the style alone — transport stopped, or
            the player's right hand sounding over the top.  A polluted block is
            skipped rather than averaged in. */
        void process (const float* left, const float* right,
                      int numSamples, bool measurable, float referenceDb) noexcept
        {
            if (! measurable || currentSection < 0 || numSamples <= 0) return;

            // Everything applied downstream of the thing being normalised —
            // the section trim itself, STYLE VOLUME and the master fader — is
            // handed in as one figure so the result can be referred back to
            // unity.  If the player moves any of them mid-section the pass is
            // void: half the audio was measured at one gain and half at
            // another, and averaging the two would bake a fader position into a
            // stored measurement.  Discard and start clean.
            if (std::abs (referenceDb - pendingTrimDb) > 0.1f)
            {
                resetAccumulator();
                pendingTrimDb = referenceDb;
                return;
            }

            if (blockCount >= kMaxBlocks) return;

            for (int i = 0; i < numSamples; ++i)
            {
                const double l = (double) left [i];
                const double r = (right != nullptr) ? (double) right[i] : l;

                // Peak BEFORE weighting: the ceiling is about clipping, and
                // clipping does not care what K-weighting thinks.  4x linear
                // interpolation catches most inter-sample peaks for a few adds.
                trackPeak (l, prevL);
                trackPeak (r, prevR);
                prevL = l; prevR = r;

                const double wl = hpL.process (shelfL.process (l));
                const double wr = hpR.process (shelfR.process (r));

                sumL += wl * wl;
                sumR += wr * wr;

                if (++hopFill >= hopSamples)
                {
                    // Ring of four 100 ms hops = one 400 ms block, 75% overlap.
                    ring[(size_t) ringPos] = { sumL, sumR, hopFill };
                    ringPos = (ringPos + 1) & 3;
                    if (ringFill < 4) ++ringFill;
                    sumL = sumR = 0.0; hopFill = 0;

                    if (ringFill >= 4 && blockCount < kMaxBlocks)
                    {
                        double sl = 0.0, sr = 0.0; int n = 0;
                        for (const auto& h : ring) { sl += h.l; sr += h.r; n += h.n; }
                        if (n >= blockSamples * 0.9)
                        {
                            const double ms = sl / (double) n + sr / (double) n;
                            if (ms > 0.0) blocks[(size_t) blockCount++] = (float) ms;
                        }
                    }
                }
            }
        }

        /** The multiplier the style bus should carry right now. */
        float currentTrim() const noexcept
        {
            return juce::Decibels::decibelsToGain (trimDbFor (currentSection));
        }

        //======================================================================
        // Reporting / persistence  (message thread)
        //======================================================================
        struct SectionInfo
        {
            bool  measured = false;
            float lufs     = 0.0f;    // referred to unity trim
            float peakDb   = -120.0f; // true-peak estimate, dBFS, at unity trim
            float trimDb   = 0.0f;    // what is being applied
        };

        SectionInfo infoFor (int sectionId) const noexcept
        {
            SectionInfo out;
            if (sectionId < 0 || sectionId >= kMaxSections) return out;
            const auto& s = sections[(size_t) sectionId];
            out.measured = s.measured.load();
            out.lufs     = s.lufs.load();
            out.peakDb   = s.peakDb.load();
            out.trimDb   = trimDbFor (sectionId);
            return out;
        }

        void  setTargetLufs (float lufs) noexcept { targetLufs.store (juce::jlimit (-40.0f, -6.0f, lufs)); }
        float getTargetLufs () const noexcept     { return targetLufs.load(); }
        bool  hasStyle      () const noexcept     { return stylePath.isNotEmpty(); }

        /** Fold the in-flight accumulator into its section.  Safe to call from
            the message thread between blocks; also called on section change. */
        void commitPending() noexcept
        {
            if (currentSection < 0 || blockCount < kMinBlocksToPublish
                || pendingTrimDb < -900.0f)
            { resetAccumulator(); return; }

            const float lufs = integratedLufs();
            resetAccumulator();
            if (lufs < -120.0f) return;

            auto& s = sections[(size_t) currentSection];

            // Refer BOTH figures back to unity trim — see the header note on
            // self-reference.  Without this the next pass would measure the
            // result of the last correction and correct it again.
            const float lufsAtUnity = lufs   - pendingTrimDb;
            const float peakAtUnity = peakDb - pendingTrimDb;

            if (s.measured.load())
            {
                // Slow average across passes: one atypical pass (a sparse chord,
                // a half-heard fill) should nudge the figure, not redefine it.
                s.lufs  .store (s.lufs.load()   * 0.7f + lufsAtUnity * 0.3f);
                s.peakDb.store (juce::jmax (s.peakDb.load(), peakAtUnity));
            }
            else
            {
                s.lufs  .store (lufsAtUnity);
                s.peakDb.store (peakAtUnity);
                s.measured.store (true);
            }
            dirty.store (true);
        }

        /** Write the cache if anything changed.  Message thread. */
        void save()
        {
            if (! dirty.load() || stylePath.isEmpty()) return;

            const auto file = cacheFile();
            std::unique_ptr<juce::XmlElement> root (file.existsAsFile()
                                                      ? juce::XmlDocument::parse (file)
                                                      : nullptr);
            if (root == nullptr || ! root->hasTagName ("GrexLoudness"))
                root = std::make_unique<juce::XmlElement> ("GrexLoudness");

            // Replace this style's entry wholesale rather than merging: the
            // measurements belong to one file version and are meaningless
            // spliced across two.
            for (int i = root->getNumChildElements(); --i >= 0;)
                if (auto* c = root->getChildElement (i))
                    if (c->getStringAttribute ("path") == stylePath)
                        root->removeChildElement (c, true);

            auto* st = root->createNewChildElement ("Style");
            // Written as STRINGS on purpose.  XmlElement::setAttribute has int
            // and double overloads but none for int64, so passing one is
            // ambiguous; and a file size or a millisecond timestamp does not
            // belong in an int anyway.  getLargeIntValue reads them back exact.
            st->setAttribute ("path",     stylePath);
            st->setAttribute ("size",     juce::String (styleSize));
            st->setAttribute ("modified", juce::String (styleTime));

            for (int i = 0; i < kMaxSections; ++i)
            {
                const auto& s = sections[(size_t) i];
                if (! s.measured.load()) continue;
                auto* e = st->createNewChildElement ("Section");
                e->setAttribute ("id",   i);
                e->setAttribute ("lufs", (double) s.lufs.load());
                e->setAttribute ("peak", (double) s.peakDb.load());
            }

            root->writeTo (file);
            dirty.store (false);
        }

        /** Throw away every measurement for the current style and start over. */
        void remeasure() noexcept
        {
            for (auto& s : sections) s.clear();
            resetAccumulator();
            dirty.store (true);
        }

    private:
        //======================================================================
        // BS.1770 K-weighting — a +4 dB high shelf then a ~38 Hz high-pass.
        // Coefficients are re-derived for the running sample rate rather than
        // using the spec's 48 kHz table, so 44.1 / 88.2 / 96 kHz are exact
        // instead of approximately right.
        //======================================================================
        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
            double z1 = 0.0, z2 = 0.0;
            void reset() noexcept { z1 = z2 = 0.0; }
            inline double process (double x) noexcept
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        void designKWeighting()
        {
            {   // Stage 1: high shelf, +3.999843853973347 dB at 1681.974450955533 Hz
                const double f0 = 1681.974450955533;
                const double G  = 3.999843853973347;
                const double Q  = 0.7071752369554196;
                const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sr);
                const double Vh = std::pow (10.0, G / 20.0);
                const double Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                shelfL.b0 = (Vh + Vb * K / Q + K * K) / a0;
                shelfL.b1 = 2.0 * (K * K - Vh) / a0;
                shelfL.b2 = (Vh - Vb * K / Q + K * K) / a0;
                shelfL.a1 = 2.0 * (K * K - 1.0) / a0;
                shelfL.a2 = (1.0 - K / Q + K * K) / a0;
                shelfR = shelfL;
            }
            {   // Stage 2: high-pass at 38.13547087602444 Hz
                const double f0 = 38.13547087602444;
                const double Q  = 0.5003270373238773;
                const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sr);
                const double a0 = 1.0 + K / Q + K * K;
                hpL.b0 = 1.0; hpL.b1 = -2.0; hpL.b2 = 1.0;
                hpL.a1 = 2.0 * (K * K - 1.0) / a0;
                hpL.a2 = (1.0 - K / Q + K * K) / a0;
                hpR = hpL;
            }
        }

        void resetFilters() noexcept
        {
            shelfL.reset(); shelfR.reset(); hpL.reset(); hpR.reset();
            prevL = prevR = 0.0;
        }

        void resetAccumulator() noexcept
        {
            sumL = sumR = 0.0;
            hopFill = 0; ringPos = 0; ringFill = 0;
            for (auto& h : ring) h = { 0.0, 0.0, 0 };
            blockCount = 0;
            peakLin = 0.0;
            peakDb  = -120.0f;
            resetFilters();
        }

        inline void trackPeak (double x, double prev) noexcept
        {
            // 4x linear interpolation between consecutive samples.  Not a true
            // 4x-oversampled true-peak, but it catches the great majority of
            // inter-sample overshoot for four multiply-adds.
            const double a = std::abs (x);
            if (a > peakLin) peakLin = a;
            const double d = (x - prev) * 0.25;
            for (int k = 1; k < 4; ++k)
            {
                const double v = std::abs (prev + d * k);
                if (v > peakLin) peakLin = v;
            }
        }

        /** Gated integrated loudness over the blocks collected so far. */
        float integratedLufs() noexcept
        {
            if (blockCount <= 0) return -1000.0f;

            peakDb = peakLin > 1.0e-9
                       ? (float) (20.0 * std::log10 (peakLin)) : -120.0f;

            // Absolute gate at -70 LUFS.
            double sum = 0.0; int n = 0;
            for (int i = 0; i < blockCount; ++i)
            {
                const double ms = (double) blocks[(size_t) i];
                if (-0.691 + 10.0 * std::log10 (ms) > -70.0) { sum += ms; ++n; }
            }
            if (n == 0) return -1000.0f;

            // Relative gate at 10 LU below the absolutely-gated mean.
            const double relGate = -0.691 + 10.0 * std::log10 (sum / n) - 10.0;
            double sum2 = 0.0; int n2 = 0;
            for (int i = 0; i < blockCount; ++i)
            {
                const double ms = (double) blocks[(size_t) i];
                if (-0.691 + 10.0 * std::log10 (ms) > relGate) { sum2 += ms; ++n2; }
            }
            if (n2 == 0) return -1000.0f;

            return (float) (-0.691 + 10.0 * std::log10 (sum2 / n2));
        }

        /** The correction curve: dead band, then remove the whole excess. */
        float trimDbFor (int sectionId) const noexcept
        {
            if (sectionId < 0 || sectionId >= kMaxSections) return 0.0f;
            const auto& s = sections[(size_t) sectionId];
            if (! s.measured.load()) return 0.0f;

            const float err = s.lufs.load() - targetLufs.load();   // + = too loud
            float trim = 0.0f;
            if (err >  kDeadBandLU) trim = -(err - kDeadBandLU);
            if (err < -kDeadBandLU) trim = -(err + kDeadBandLU);
            trim = juce::jlimit (-kMaxTrimDb, kMaxTrimDb, trim);

            // Peak safety: never let the trim push this section's measured peak
            // past the ceiling.  Only ever pulls DOWN — a quiet section is not
            // dragged up into the ceiling by a loudness correction.
            const float headroom = kTruePeakCeiling - s.peakDb.load();
            if (headroom < trim) trim = headroom;

            return juce::jlimit (-kMaxTrimDb, kMaxTrimDb, trim);
        }

        juce::File cacheFile() const { return GrexPaths::root().getChildFile ("grex_loudness.xml"); }

        void loadCacheForCurrentStyle()
        {
            const auto file = cacheFile();
            if (! file.existsAsFile()) return;

            std::unique_ptr<juce::XmlElement> root (juce::XmlDocument::parse (file));
            if (root == nullptr || ! root->hasTagName ("GrexLoudness")) return;

            for (auto* st : root->getChildWithTagNameIterator ("Style"))
            {
                if (st->getStringAttribute ("path") != stylePath) continue;

                // A style that was edited since it was measured must be measured
                // again — stale numbers are worse than none.
                if (st->getStringAttribute ("size")    .getLargeIntValue() != styleSize) return;
                if (st->getStringAttribute ("modified").getLargeIntValue() != styleTime) return;

                for (auto* e : st->getChildWithTagNameIterator ("Section"))
                {
                    const int id = e->getIntAttribute ("id", -1);
                    if (id < 0 || id >= kMaxSections) continue;
                    auto& s = sections[(size_t) id];
                    s.lufs    .store ((float) e->getDoubleAttribute ("lufs", -1000.0));
                    s.peakDb  .store ((float) e->getDoubleAttribute ("peak", -120.0));
                    s.measured.store (s.lufs.load() > -120.0f);
                }
                return;
            }
        }

        struct SectionState
        {
            std::atomic<bool>  measured { false };
            std::atomic<float> lufs     { 0.0f };
            std::atomic<float> peakDb   { -120.0f };
            void clear() noexcept { measured.store (false); lufs.store (0.0f); peakDb.store (-120.0f); }
        };

        struct Hop { double l, r; int n; };

        double sr = 48000.0;
        int    hopSamples = 4800, blockSamples = 19200;

        Biquad shelfL, shelfR, hpL, hpR;
        double prevL = 0.0, prevR = 0.0;

        double sumL = 0.0, sumR = 0.0;
        int    hopFill = 0, ringPos = 0, ringFill = 0;
        std::array<Hop, 4> ring { };
        std::array<float, kMaxBlocks> blocks { };
        int    blockCount = 0;
        double peakLin = 0.0;
        float  peakDb  = -120.0f;

        int    currentSection = -1;
        float  pendingTrimDb  = 0.0f;

        std::array<SectionState, kMaxSections> sections;
        std::atomic<float> targetLufs { kDefaultTargetLufs };

        juce::String  stylePath;
        juce::int64   styleSize = 0, styleTime = 0;
        std::atomic<bool> dirty { false };   // set from the audio thread on commit

        JUCE_LEAK_DETECTOR (StyleLoudness)
    };
} // namespace Betel
