#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace Betel
{

//==============================================================================
// STYLE DUCKER — two dynamic bell cuts on the STYLE bus, keyed from the SOLO.
//
// WHAT IT IS FOR, because that decides every choice below.
//
// The band and the right hand share one master bus, and the FINISHER lives on
// that bus.  So the loud thing wins: push the style up and the Finisher's
// compression pulls the whole master down, right hand included.  That is
// backwards for an arranger — the melody is the thing that must stay audible,
// and the band is what should give way.
//
// This makes the give-way explicit and puts it BEFORE the sum, where it can be
// aimed.  Two bell cuts sit on the style; each one listens to the solo IN ITS
// OWN BAND and deepens only when the solo is actually occupying that band.
// Play nothing and the style is untouched; play a line and the band opens a
// window exactly where the line lives.
//
// BAND-LIMITED DETECTION IS THE WHOLE IDEA.  A broadband key would duck both
// bands off any solo energy at all, which is a pumping effect — musical in its
// own way, but it is not what carves space.  Keying each band from its own
// slice means a low melody note moves the low point and leaves the high one
// alone, so the style keeps its brightness while its middle gets out of the
// way.
//
//==============================================================================
// HOW THE CUT IS BUILT — and why it is not a peaking EQ whose gain moves.
//
// The obvious implementation recomputes a peaking-EQ biquad every time the gain
// reduction changes.  That means transcendentals in the audio path at whatever
// rate you update, and a choice between zipper noise (update rarely) and cost
// (update often).  Neither is necessary, because of an identity:
//
//     peaking(x, G) == x + (G - 1) * bandpass(x)
//
// with `bandpass` the RBJ constant-0-dB-peak form at the same f0 and Q.  That
// is not an approximation; it is how the RBJ peaking filter is derived.
//
// So the FILTER never changes: its coefficients depend only on f0 and Q, which
// move when the user drags a point and at no other time.  What moves per sample
// is a plain scalar.  A scalar can be smoothed for free and can never make a
// filter unstable — the two failure modes of the recompute approach are simply
// absent.
//
//     styleOut = styleIn - (1 - g) * bandpass(styleIn)
//
// g = 1 is flat, g = 0 is a full notch, and everything between is a bell of
// exactly the depth asked for.
//
//==============================================================================
// THREADING.  Parameters are atomics written from the message thread and read
// once per block into a local snapshot; the audio path touches nothing else.
// `grDb` is written by the audio thread and read by the UI for the pumping
// display — a plain relaxed atomic, because a meter that is one frame stale is
// indistinguishable from one that is not.
//==============================================================================
class StyleDucker
{
public:
    static constexpr int kNumBands = 2;

    // Ranges are shared with the editor so the two can never disagree about
    // what a value means.
    static constexpr float kMinFreqHz   =   40.0f;
    static constexpr float kMaxFreqHz   = 16000.0f;
    static constexpr float kMinQ        =    0.3f;
    static constexpr float kMaxQ        =    8.0f;
    static constexpr float kMaxDepthDb  =   24.0f;   // how deep a point can be dragged
    static constexpr float kMinThreshDb =  -60.0f;
    static constexpr float kMaxThreshDb =    0.0f;
    static constexpr float kMinRatio    =    1.0f;
    static constexpr float kMaxRatio    =   20.0f;
    static constexpr float kMinAttackMs =    0.5f;
    static constexpr float kMaxAttackMs =  200.0f;
    static constexpr float kMinRelMs    =   20.0f;
    static constexpr float kMaxRelMs    = 1000.0f;

    // DEFAULTS: two points at the frequencies a melody most often fights the
    // band for — the lower midrange where pads and guitars sit, and the
    // presence region where a lead's articulation lives.  Depth 0 means the
    // ducker is audibly absent until a point is dragged down, which is what
    // makes it safe to ship enabled-by-default-off.
    struct BandDefaults { float freq, q, depth, thresh, ratio, attack, release; };
    static constexpr BandDefaults kDefaults[kNumBands] =
    {
        //  freq     Q   depth  thresh  ratio  attack  release
        {  450.0f, 1.2f,  0.0f, -30.0f,  4.0f,   8.0f,  180.0f },
        { 2600.0f, 1.4f,  0.0f, -30.0f,  4.0f,   4.0f,  140.0f }
    };

    StyleDucker()
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& d = kDefaults[b];
            bands[(size_t) b].freqHz  .store (d.freq);
            bands[(size_t) b].q       .store (d.q);
            bands[(size_t) b].depthDb .store (d.depth);
            bands[(size_t) b].threshDb.store (d.thresh);
            bands[(size_t) b].ratio   .store (d.ratio);
            bands[(size_t) b].attackMs.store (d.attack);
            bands[(size_t) b].relMs   .store (d.release);
        }
    }

    //==========================================================================
    // Lifecycle
    //==========================================================================
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = (newSampleRate > 0.0 ? newSampleRate : 44100.0);
        reset();
        coeffsDirty.store (true);
    }

    void reset() noexcept
    {
        for (auto& b : bands)
        {
            b.styleL.reset();
            b.styleR.reset();
            b.side  .reset();
            b.env = 0.0f;
            b.gain = 1.0f;
            b.grDb.store (0.0f);
        }
    }

    //==========================================================================
    // Parameters (message thread).  Anything that changes a FILTER marks the
    // coefficients dirty; anything that only changes the dynamics does not,
    // because those are read fresh every block anyway.
    //==========================================================================
    void setEnabled (bool on) noexcept { enabled.store (on); }
    bool isEnabled() const noexcept    { return enabled.load(); }

    void setBandEnabled (int b, bool on) noexcept
    { if (validBand (b)) bands[(size_t) b].on.store (on); }
    bool isBandEnabled (int b) const noexcept
    { return validBand (b) && bands[(size_t) b].on.load(); }

    void setFreq (int b, float hz) noexcept
    {
        if (! validBand (b)) return;
        bands[(size_t) b].freqHz.store (juce::jlimit (kMinFreqHz, kMaxFreqHz, hz));
        coeffsDirty.store (true);
    }

    void setQ (int b, float q) noexcept
    {
        if (! validBand (b)) return;
        bands[(size_t) b].q.store (juce::jlimit (kMinQ, kMaxQ, q));
        coeffsDirty.store (true);
    }

    /** HOW FAR THE POINT HAS BEEN DRAGGED DOWN, in dB, as a POSITIVE number.
        This is the CEILING on the cut, not the cut itself — the dynamics
        decide how much of it is used at any moment. */
    void setDepthDb (int b, float db) noexcept
    { if (validBand (b)) bands[(size_t) b].depthDb.store (juce::jlimit (0.0f, kMaxDepthDb, db)); }

    void setThresholdDb (int b, float db) noexcept
    { if (validBand (b)) bands[(size_t) b].threshDb.store (juce::jlimit (kMinThreshDb, kMaxThreshDb, db)); }

    void setRatio (int b, float r) noexcept
    { if (validBand (b)) bands[(size_t) b].ratio.store (juce::jlimit (kMinRatio, kMaxRatio, r)); }

    void setAttackMs (int b, float ms) noexcept
    { if (validBand (b)) bands[(size_t) b].attackMs.store (juce::jlimit (kMinAttackMs, kMaxAttackMs, ms)); }

    void setReleaseMs (int b, float ms) noexcept
    { if (validBand (b)) bands[(size_t) b].relMs.store (juce::jlimit (kMinRelMs, kMaxRelMs, ms)); }

    float getFreq        (int b) const noexcept { return validBand (b) ? bands[(size_t) b].freqHz  .load() : 0.0f; }
    float getQ           (int b) const noexcept { return validBand (b) ? bands[(size_t) b].q       .load() : 1.0f; }
    float getDepthDb     (int b) const noexcept { return validBand (b) ? bands[(size_t) b].depthDb .load() : 0.0f; }
    float getThresholdDb (int b) const noexcept { return validBand (b) ? bands[(size_t) b].threshDb.load() : 0.0f; }
    float getRatio       (int b) const noexcept { return validBand (b) ? bands[(size_t) b].ratio   .load() : 1.0f; }
    float getAttackMs    (int b) const noexcept { return validBand (b) ? bands[(size_t) b].attackMs.load() : 1.0f; }
    float getReleaseMs   (int b) const noexcept { return validBand (b) ? bands[(size_t) b].relMs   .load() : 1.0f; }

    /** LIVE GAIN REDUCTION for the display, in dB, positive = ducking.  This
        is what makes the point pump: the editor draws the bell at
        (depth - gr) so it sits at rest when nothing is happening and dips
        toward its dragged depth as the solo pushes it. */
    float getGainReductionDb (int b) const noexcept
    { return validBand (b) ? bands[(size_t) b].grDb.load() : 0.0f; }

    /** THE KEY LEVEL this band's detector is seeing, in dB.

        Without this a band that never moves is unexplainable: the depth might
        be zero, or the solo might simply have nothing at that frequency, and
        those want opposite fixes.  Read against `getThresholdDb` — over the
        threshold and the band WILL duck, under it and no amount of depth will
        make it. */
    float getSidechainLevelDb (int b) const noexcept
    { return validBand (b) ? bands[(size_t) b].sideDb.load() : -120.0f; }

    //==========================================================================
    // AUDIO.  `style` is modified in place; `side` is read only.
    //
    // Both buffers are the FULL block and must be the same length.  The side
    // chain is summed to mono before detection: a melody panned anywhere should
    // duck the band by the same amount, and a stereo detector would make the
    // duck depend on where the right hand happens to sit in the image.
    //==========================================================================
    void process (juce::AudioBuffer<float>& style,
                  const juce::AudioBuffer<float>& side) noexcept
    {
        if (! enabled.load()) { clearMeters(); return; }

        const int numSamples = style.getNumSamples();
        if (numSamples <= 0) return;

        if (coeffsDirty.exchange (false))
            recalcCoefficients();

        const int    styleCh = style.getNumChannels();
        const int    sideCh  = side .getNumChannels();
        const int    sideN   = side .getNumSamples();
        float* const sL = styleCh > 0 ? style.getWritePointer (0) : nullptr;
        float* const sR = styleCh > 1 ? style.getWritePointer (1) : sL;
        if (sL == nullptr) return;

        const float* const kL = sideCh > 0 ? side.getReadPointer (0) : nullptr;
        const float* const kR = sideCh > 1 ? side.getReadPointer (1) : kL;

        for (int b = 0; b < kNumBands; ++b)
        {
            auto& bd = bands[(size_t) b];
            if (! bd.on.load()) { bd.grDb.store (0.0f); bd.sideDb.store (-120.0f); continue; }

            // Snapshot once per block.  Reading atomics per sample would be
            // both slower and less coherent - a threshold could change halfway
            // through a block and split the envelope's meaning in two.
            const float depth  = bd.depthDb .load();
            const float thresh = bd.threshDb.load();
            const float ratio  = juce::jmax (1.0f, bd.ratio.load());
            const float slope  = 1.0f - 1.0f / ratio;

            // A DEPTH OF ZERO NO LONGER SKIPS THE BAND, and that matters more
            // than the cycles it costs.
            //
            // It used to `continue` here, which was efficient and wrong: with
            // the detector not running, the KEY METER below read nothing, so a
            // point that was not moving gave the player no way to tell WHY.
            // "I set no depth" and "the right hand has no energy in this band"
            // look identical from the outside and want opposite responses.
            //
            // Running it always also keeps the envelope warm: dragging depth up
            // from zero now engages against a level that is already tracking,
            // instead of one starting from silence.
            //
            // No cut is applied at depth 0 regardless - grDb clamps to depth,
            // so gain stays 1 and the apply branch below does nothing.

            const float atkC = onePoleCoef (bd.attackMs.load());
            const float relC = onePoleCoef (bd.relMs   .load());

            float env  = bd.env;
            float gain = bd.gain;
            float peakGr   = 0.0f;
            float peakSide = -120.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                // ── DETECT: the solo, through this band's own bandpass ───────
                const float keyIn = (kL == nullptr || i >= sideN)
                                        ? 0.0f
                                        : 0.5f * (kL[i] + kR[i]);
                const float keyBp = bd.side.process (keyIn);
                const float rect  = std::abs (keyBp);

                // Attack when rising, release when falling - the standard
                // asymmetric follower.  This IS the smoothing; no separate
                // smoother is needed on the gain, and adding one would only
                // blur the attack the user dialled in.
                env = (rect > env) ? rect + atkC * (env - rect)
                                   : rect + relC * (env - rect);

                // ── GAIN COMPUTER ───────────────────────────────────────────
                const float lvlDb = juce::Decibels::gainToDecibels (env, -120.0f);
                if (lvlDb > peakSide) peakSide = lvlDb;
                const float over  = lvlDb - thresh;
                float grDb = (over > 0.0f) ? over * slope : 0.0f;
                if (grDb > depth) grDb = depth;          // the drag is the ceiling
                if (grDb > peakGr) peakGr = grDb;

                gain = juce::Decibels::decibelsToGain (-grDb);

                // ── APPLY: x - (1 - g) * bandpass(x), per the identity above ─
                const float amt = 1.0f - gain;
                if (amt > 1.0e-5f)
                {
                    sL[i] -= amt * bd.styleL.process (sL[i]);
                    if (sR != sL) sR[i] -= amt * bd.styleR.process (sR[i]);
                }
                else
                {
                    // Still RUN the filters even when nothing is being
                    // subtracted.  A biquad that stops being fed goes stale,
                    // and the first sample after it resumes would be computed
                    // from history that is a block old - an audible tick every
                    // time the duck re-engages.
                    bd.styleL.process (sL[i]);
                    if (sR != sL) bd.styleR.process (sR[i]);
                }
            }

            bd.env  = env;
            bd.gain = gain;
            bd.grDb  .store (peakGr);
            bd.sideDb.store (peakSide);
        }
    }

private:
    static bool validBand (int b) noexcept { return b >= 0 && b < kNumBands; }

    void clearMeters() noexcept
    { for (auto& b : bands) { b.grDb.store (0.0f); b.sideDb.store (-120.0f); } }

    /** One-pole time constant.  exp(-1/(ms * 0.001 * fs)) is the coefficient
        applied to the DIFFERENCE, so 0 is instant and ->1 is slow. */
    float onePoleCoef (float ms) const noexcept
    {
        const double t = juce::jmax (0.01, (double) ms) * 0.001 * sampleRate;
        return (float) std::exp (-1.0 / juce::jmax (1.0, t));
    }

    //==========================================================================
    // RBJ BANDPASS, constant 0 dB peak gain.  Direct Form I transposed, which
    // is the numerically better-behaved arrangement for fixed coefficients and
    // costs nothing extra.
    //==========================================================================
    struct Biquad
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void reset() noexcept { z1 = z2 = 0.0f; }

        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void setBandpass (double fs, double f0, double q) noexcept
        {
            f0 = juce::jlimit (10.0, fs * 0.45, f0);
            q  = juce::jmax (0.05, q);

            const double w0    = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
            const double cosw0 = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * q);

            const double a0i = 1.0 / (1.0 + alpha);
            b0 = (float) ( alpha        * a0i);
            b1 = 0.0f;
            b2 = (float) (-alpha        * a0i);
            a1 = (float) (-2.0 * cosw0  * a0i);
            a2 = (float) ((1.0 - alpha) * a0i);
        }
    };

    struct Band
    {
        std::atomic<bool>  on       { true };
        std::atomic<float> freqHz   { 1000.0f };
        std::atomic<float> q        { 1.0f };
        std::atomic<float> depthDb  { 0.0f };
        std::atomic<float> threshDb { -30.0f };
        std::atomic<float> ratio    { 4.0f };
        std::atomic<float> attackMs { 8.0f };
        std::atomic<float> relMs    { 180.0f };

        std::atomic<float> grDb     { 0.0f };     // audio -> UI
        std::atomic<float> sideDb   { -120.0f };  // audio -> UI: the KEY level

        Biquad styleL, styleR, side;
        float  env  = 0.0f;
        float  gain = 1.0f;
    };

    void recalcCoefficients() noexcept
    {
        for (auto& b : bands)
        {
            const double f = (double) b.freqHz.load();
            const double q = (double) b.q     .load();
            b.styleL.setBandpass (sampleRate, f, q);
            b.styleR.setBandpass (sampleRate, f, q);
            b.side  .setBandpass (sampleRate, f, q);
        }
    }

    std::array<Band, kNumBands> bands;
    std::atomic<bool>  enabled     { false };   // OFF until a point is dragged
    std::atomic<bool>  coeffsDirty { true };
    double             sampleRate  { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleDucker)
};

} // namespace Betel
