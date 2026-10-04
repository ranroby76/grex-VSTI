#pragma once

#include <JuceHeader.h>
#include <array>
#include <complex>
#include <vector>

//==============================================================================
//  ChorusConvolver.h — the section chorus, plus the two knob-taper helpers.
//
//  ── WHAT HAPPENED TO THE FLOWSTONE CONVOLVER ─────────────────────────────────
//
//  It was a faithful port: an FFT convolution against an impulse response of a
//  thousand random taps that drift, each drifting at a constant rate and so
//  detuning by its own amount.  The maths was right and it was measurably
//  producing output — and it was never worth the tuning it demanded.  Three
//  separate faults in it reached a build (an in-place FFT, a normalisation
//  10 dB low, a level that then vanished under a tapered send), and even
//  working it carried 768 samples of latency, which a chorus should not.
//
//  This is juce::dsp::Chorus instead: a modulated delay line, part of the
//  framework, already a dependency, tested by everyone who uses JUCE.  Zero
//  latency, four parameters, and no impulse response to get wrong.
//
//  ── THE JUNO ARRANGEMENT ─────────────────────────────────────────────────────
//
//  One instance per channel at DIFFERENT RATES, which is how the Juno-60 gets
//  its width: two bucket-brigade lines modulated out of step, one per side.
//  A single instance across both channels would modulate them identically and
//  collapse to something narrow.
//
//  Settings are JUCE's own documented recipe for "classic chorus" — centre delay
//  7.5 ms, low depth, no feedback — rather than anything dialled by ear.
//
//  FULLY WET, because this is a SEND: the dry reaches the bus by its own route,
//  and adding it here would double it.  A fully-wet modulated delay on its own
//  is a vibrato; summed with the bus dry it is a chorus, which is exactly the
//  arrangement the bus provides.
//==============================================================================

namespace Betel
{
    //==========================================================================
    //  Martin Vicanek's rational mapper.  Maps (0, 0.5, 1) -> (a, c, b), so a
    //  knob can hit an exact value at centre detent without a piecewise curve.
    //
    //      y = a + (b-a)(c-a)x / ((b-c) + (2c-a-b)x)
    //
    //  `c` must lie strictly between a and b or the denominator can reach zero.
    //==========================================================================
    inline float rationalMap (float x, float a, float b, float c) noexcept
    {
        const float den = (b - c) + (2.0f * c - a - b) * x;
        if (std::abs (den) < 1.0e-9f) return c;
        return a + (b - a) * (c - a) * x / den;
    }

    //==========================================================================
    //  The mix knob's taper, read off the FlowStone schematic.
    //
    //      y = (base^x - 1) / (base - 1)
    //
    //  `base` is whatever makes y(0.5) equal the wanted level at half rotation.
    //  With g = 10^(dB/20):  sqrt(base) = (1-g)/g, so base = ((1-g)/g)^2.
    //  The patch's -20 dBFS gives g = 0.1 and base = 81 exactly, i.e. (81^x-1)/80.
    //
    //  Unlike a plain x^k this hits 0 at 0 and 1 at 1 with no fudging, which is
    //  why it is worth carrying rather than approximating.
    //==========================================================================
    inline float logKnobTaper (float x, float dbAtHalfRotation = -20.0f) noexcept
    {
        const float g = std::pow (10.0f, dbAtHalfRotation / 20.0f);
        if (g <= 0.0f || g >= 0.5f) return juce::jlimit (0.0f, 1.0f, x);

        const float s    = (1.0f - g) / g;
        const float base = s * s;
        return (std::pow (base, juce::jlimit (0.0f, 1.0f, x)) - 1.0f) / (base - 1.0f);
    }

    /** The inverse of logKnobTaper — a stored level back to a knob position, so
        the control can be seeded from the value it last wrote.

            y = (base^x - 1)/(base - 1)   =>   x = log_base( y(base-1) + 1 )
    */
    inline float logKnobTaperInv (float y, float dbAtHalfRotation = -20.0f) noexcept
    {
        const float g = std::pow (10.0f, dbAtHalfRotation / 20.0f);
        if (g <= 0.0f || g >= 0.5f) return juce::jlimit (0.0f, 1.0f, y);

        const float s    = (1.0f - g) / g;
        const float base = s * s;
        const float t    = juce::jlimit (0.0f, 1.0f, y) * (base - 1.0f) + 1.0f;
        return juce::jlimit (0.0f, 1.0f, std::log (t) / std::log (base));
    }

    //==========================================================================
    //  SectionChorusFx — one per section, on the chorus send bus.
    //==========================================================================
    struct SectionChorusFx
    {
        std::atomic<bool>  enabled { false };

        // JUCE's documented classic-chorus values.  Exposed so they can be
        // dialled later, but the point of this replacement is that they need not
        // be: these are the settings the framework recommends, not a guess.
        std::atomic<float> rateHz  { 0.9f };    // LFO speed, must stay under 100
        std::atomic<float> depth   { 0.28f };   // 0..1, low for chorus
        std::atomic<float> centreMs{ 7.5f };    // 7-8 ms is the classic range
        std::atomic<float> level   { 1.0f };    // applied by the bus, kept for parity

        void prepare (double sr, int blockSize)
        {
            const juce::dsp::ProcessSpec spec {
                sr, (juce::uint32) juce::jmax (1, blockSize), 1u };

            for (auto& v : voice) v.prepare (spec);

            pushParams();
            reset();
        }

        void reset()
        {
            for (auto& v : voice) v.reset();
            pushed = false;      // force a re-push: reset may clear the targets
        }

        /** IN PLACE, WET ONLY.  `buf` is the section's summed chorus send going
            in and the finished wet coming out; the caller sums it into the bus. */
        void process (juce::AudioBuffer<float>& buf, int numSamples)
        {
            if (! enabled.load() || numSamples <= 0 || buf.getNumChannels() < 2)
                return;

            pushParams();

            // One block per channel, because the two instances are deliberately
            // running at different rates — see the Juno note at the top.
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::dsp::AudioBlock<float> block (
                    buf.getArrayOfWritePointers() + ch, 1, (size_t) numSamples);

                juce::dsp::ProcessContextReplacing<float> ctx (block);
                voice[(size_t) ch].process (ctx);
            }
        }

    private:
        /** ONLY WHEN SOMETHING ACTUALLY MOVED.

            This used to run on every block, re-setting all four parameters
            hundreds of times a second with the same values.  JUCE's setters
            take smoothed targets, so that is wasteful at best — and any setter
            that resets or re-ramps internal state would be re-triggered
            continuously, which is a very good way to end up with an effect that
            processes and produces nothing you can hear.

            Caching the last-pushed values means the setters fire on a real
            change and never otherwise. */
        void pushParams()
        {
            const float r = juce::jlimit (0.01f, 20.0f, rateHz  .load());
            const float d = juce::jlimit (0.0f,  1.0f,  depth   .load());
            const float c = juce::jlimit (1.0f,  50.0f, centreMs.load());

            if (pushed && r == lastRate && d == lastDepth && c == lastCentre)
                return;

            lastRate = r; lastDepth = d; lastCentre = c; pushed = true;

            // ── SAME RATE, DIFFERENT DELAY.  NOT DIFFERENT RATES ─────────────
            //
            // The first version ran the two sides at 0.85x and 1.18x, reasoning
            // that a rate difference decorrelates them.  It does — and it also
            // means the two LFOs walk in and out of alignment continuously, so
            // the image drifts and the second layer wanders instead of sitting
            // beside the first.  That is heard as unbalanced, and it is.
            //
            // The Juno does not do that: it modulates BOTH bucket-brigade lines
            // from ONE LFO, the second phase-inverted.  Same speed, fixed
            // relationship, stable image.
            //
            // juce::dsp::Chorus exposes no LFO phase, so the offset goes on the
            // CENTRE DELAY instead — 7.5 ms against 10.5 ms.  Fixed rather than
            // drifting, which is the property that matters; the two sides stay
            // in a constant relationship however long the note is held.
            static constexpr float kDelaySkew[2] = { 1.0f, 1.4f };

            for (int i = 0; i < 2; ++i)
            {
                auto& v = voice[(size_t) i];
                v.setRate        (r);
                v.setDepth       (d);
                v.setCentreDelay (juce::jlimit (1.0f, 50.0f, c * kDelaySkew[i]));
                v.setFeedback    (0.0f);   // feedback turns a chorus into a flanger
                v.setMix         (1.0f);   // wet only — the bus holds the dry
            }
        }

        juce::dsp::Chorus<float> voice[2];

        float lastRate = 0.0f, lastDepth = 0.0f, lastCentre = 0.0f;
        bool  pushed   = false;
    };
}
