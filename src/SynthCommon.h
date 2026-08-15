#pragma once
//==============================================================================
// SynthCommon.h  —  per-voice DSP building blocks used by Channel/Voice
//
// AHDSREnvelope : Attack/Hold/Decay/Sustain/Release stage machine
// SVFFilter     : Topology-preserving state-variable filter (LP/HP/BP/Notch)
// LFO           : Sine oscillator with optional pre-delay
// WSOLAStretcher: Pitch-preserving tempo-stretch via overlap-add + cross-corr
//
// All structs are POD-like with default member initializers so that
// `Voice::reset()` can use aggregate-initialisation safely.
//==============================================================================

#include <JuceHeader.h>
#include <cmath>
#include <cstring>

namespace Betel
{
    //==========================================================================
    // AHDSR Envelope
    //==========================================================================
    struct AHDSREnvelope
    {
        enum Stage { Idle, Attack, Hold, Decay, Sustain, Release };
        enum Curve { Exp = 0, Lin = 1, Log = 2 };   // decay/release shape
        Stage stage = Idle;
        Curve curve = Exp;
        float level = 0.0f;

        // Attack: exponential overshoot-to-1.5 (Moog-style punch), reaching
        // full level in EXACTLY the labelled time (scale = ln(1.5/0.5)).
        //
        // Decay / Release: an exact-duration PHASE (0..1 over the labelled
        // time) mapped through `curve` to a "fraction fallen", so all three
        // shapes finish in the same real time and only the CONTOUR differs:
        //   Exp — fast drop, gentle tail (natural pluck/blown decay; least
        //         "picky" end; this is the pre-existing character).
        //   Lin — constant slope (mechanical hold-then-cut; gated/staccato).
        //   Log — holds near the top, then falls (smooth pad/ensemble fade;
        //         legato overlap on chord changes).
        float attackCoeff = 1.0f;
        float sustainLevel = 0.0f;
        float holdSamples  = 0.0f;
        int   holdCounter  = 0;

        // Active fall segment (decay OR release).
        float  segStart = 0.0f, segEnd = 0.0f;  // level end-points
        double segPhase = 0.0,  segInc = 0.0;   // 0..1 progress, per-sample step
        float  decayInc = 0.0f;                 // 1/decaySamples (applied at Decay)

        static constexpr float kAttackTarget = 1.5f;            // overshoot for punch
        static constexpr float kAttackScale  = 1.0f / 1.0986f;  // ln(1.5/0.5)

        static float msToCoeff (float ms, double sr)
        {
            if (ms < 0.1f) return 1.0f;
            return 1.0f - std::exp (-1.0f / ((float) ms * 0.001f * (float) sr));
        }

        // Fraction fallen (0 at segment start, 1 at end) for phase p.
        static float fallAmount (float p, Curve c)
        {
            p = juce::jlimit (0.0f, 1.0f, p);
            switch (c)
            {
                case Lin: return p;
                case Log: // convex: slow start, steep finish — holds then drops
                    return (std::exp (4.0f * p) - 1.0f) / (std::exp (4.0f) - 1.0f);
                case Exp:
                default:  // concave: steep start, gentle finish
                    return (1.0f - std::exp (-6.0f * p)) / (1.0f - std::exp (-6.0f));
            }
        }

        void noteOn (float attackMs, float holdMs, float decayMs, float sustain, double sr)
        {
            sustainLevel = sustain;
            attackCoeff  = msToCoeff (attackMs * kAttackScale, sr);
            const double decaySamples = decayMs * 0.001 * sr;
            decayInc = (decayMs < 0.1f || decaySamples < 1.0)
                         ? 1.0e9f : (float) (1.0 / decaySamples);
            holdSamples = (float) (sr * holdMs / 1000.0);
            holdCounter = 0;
            stage = Attack;
        }

        void noteOff (float releaseMs, double sr)
        {
            const double relSamples = releaseMs * 0.001 * sr;
            segStart = level;
            segEnd   = 0.0f;
            segPhase = 0.0;
            segInc   = (releaseMs < 0.1f || relSamples < 1.0) ? 1.0e9 : (1.0 / relSamples);
            if (stage != Idle) stage = Release;
        }

        float process()
        {
            switch (stage)
            {
                case Attack:
                    level += (kAttackTarget - level) * attackCoeff;
                    if (level >= 0.999f) { level = 1.0f; stage = Hold; holdCounter = 0; }
                    break;
                case Hold:
                    if ((float) ++holdCounter >= holdSamples)
                    {
                        segStart = level; segEnd = sustainLevel;
                        segPhase = 0.0;   segInc = decayInc;
                        stage = Decay;
                    }
                    break;
                case Decay:
                    segPhase += segInc;
                    if (segPhase >= 1.0) { level = sustainLevel; stage = Sustain; }
                    else level = segStart + (segEnd - segStart) * fallAmount ((float) segPhase, curve);
                    break;
                case Sustain:
                    level = sustainLevel;
                    break;
                case Release:
                    segPhase += segInc;
                    if (segPhase >= 1.0) { level = 0.0f; stage = Idle; }
                    else level = segStart + (segEnd - segStart) * fallAmount ((float) segPhase, curve);
                    break;
                case Idle:
                    break;
            }
            return level;
        }

        bool isActive() const { return stage != Idle; }
    };

    //==========================================================================
    // State Variable Filter (per-voice, per-channel-of-audio)
    // type: 0=LP, 1=HP, 2=BP, 3=Notch
    //==========================================================================
    struct SVFFilter
    {
        float ic1eq = 0.0f, ic2eq = 0.0f;

        void reset() { ic1eq = 0.0f; ic2eq = 0.0f; }

        float process(float input, float cutoffHz, float resonance, int type, double sr)
        {
            // Clamp cutoff well below Nyquist.  The TPT topology becomes
            // numerically unstable as g = tan(π·cutoff/sr) approaches the
            // tangent asymptote at sr/2; values above ~0.45·sr already start
            // accumulating round-off in the integrator path (audible as
            // subtle high-frequency distortion on bright material even when
            // the user expects a near-bypass low-pass).  0.45·sr keeps g
            // bounded and the filter well-conditioned across all sample
            // rates.  Channel.cpp also gates the call entirely when the
            // filter is at its "off" defaults, so this clamp matters only
            // when the filter is genuinely doing something.
            float g = std::tan(juce::MathConstants<float>::pi
                               * juce::jlimit(20.0f, (float)(sr * 0.45), cutoffHz)
                               / (float)sr);
            float k  = 2.0f - 2.0f * juce::jlimit(0.0f, 0.99f, resonance);
            float a1 = 1.0f / (1.0f + g * (g + k));
            float a2 = g * a1;
            float a3 = g * a2;
            float v3 = input - ic2eq;
            float v1 = a1 * ic1eq + a2 * v3;
            float v2 = ic2eq + a2 * ic1eq + a3 * v3;
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;
            switch (type)
            {
                case 0: return v2;                  // LP
                case 1: return input - k * v1 - v2; // HP
                case 2: return v1;                  // BP
                case 3: return input - k * v1;      // Notch
                default: return v2;
            }
        }
    };

    //==========================================================================
    // Sine LFO with pre-delay
    //==========================================================================
    struct LFO
    {
        double phase = 0.0;
        double phaseInc = 0.0;
        int    delaySamples = 0;
        int    delayCounter = 0;

        void reset(float rateHz, float delayMs, double sr)
        {
            phase = 0.0;
            phaseInc = (rateHz > 0.001f) ? (double)rateHz / sr : 0.0;
            delaySamples = (int)(sr * delayMs / 1000.0);
            delayCounter = 0;
        }

        float process()
        {
            if (delayCounter < delaySamples) { delayCounter++; return 0.0f; }
            float val = std::sin((float)(phase * juce::MathConstants<double>::twoPi));
            phase += phaseInc;
            if (phase >= 1.0) phase -= 1.0;
            return val;
        }
    };

    //==========================================================================
    // WSOLA Time-Stretcher — pitch-preserving tempo change for arranger loops.
    //
    // Overlaps Hann-windowed grains with cross-correlation alignment.
    // speedRatio = targetBPM / origBPM:
    //   >1 = faster target = advance through source faster
    //   <1 = slower target = advance through source slower
    //==========================================================================
    struct WSOLAStretcher
    {
        bool   active        = false;
        double speedRatio    = 1.0;
        double sourcePosition = 0.0;
        int    windowSize    = 0;
        int    hopSynthesis  = 0;
        int    searchRange   = 0;
        int64_t srcLength    = 0;

        static constexpr int maxBufSize = 16384;
        float bufL[maxBufSize] = {};
        float bufR[maxBufSize] = {};
        int   writePos = 0;
        int   readPos  = 0;
        int   available = 0;

        float hannWin[8192] = {};

        void prepare(double sampleRate, double ratio, int64_t srcLen)
        {
            speedRatio = juce::jlimit(0.25, 4.0, ratio);
            srcLength  = srcLen;
            windowSize = juce::jmin((int)(sampleRate * 0.040), 8192);  // 40ms
            if (windowSize < 256) windowSize = 256;
            hopSynthesis = windowSize / 2;
            searchRange  = windowSize / 4;
            sourcePosition = 0.0;
            writePos = 0; readPos = 0; available = 0;
            active = true;
            for (int i = 0; i < windowSize; ++i)
                hannWin[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                                                     * (float)i / (float)(windowSize - 1)));
            std::memset(bufL, 0, sizeof(bufL));
            std::memset(bufR, 0, sizeof(bufR));
        }

        void updateRatio(double ratio) { speedRatio = juce::jlimit(0.25, 4.0, ratio); }

        void writeGrain(const float* srcL, const float* srcR, int srcLen, int numCh)
        {
            if (windowSize <= 0 || srcLen <= 0) return;

            // Source hop: how far to advance through the source per grain
            double sourceHop = (double)hopSynthesis * speedRatio;
            int basePos = (int)sourcePosition;

            // Cross-correlation search for best grain alignment
            int bestOff = 0;
            float bestCorr = -1e30f;
            for (int off = -searchRange; off <= searchRange; off += 2)
            {
                float corr = 0.0f;
                int checkLen = juce::jmin(hopSynthesis, windowSize / 2);
                for (int j = 0; j < checkLen; j += 4)
                {
                    int si = wrap(basePos + off + j, srcLen);
                    int bi = (readPos + available + j) % maxBufSize;
                    corr += srcL[si] * bufL[bi];
                }
                if (corr > bestCorr) { bestCorr = corr; bestOff = off; }
            }

            // Write Hann-windowed grain into ring buffer (overlap-add)
            int grainStart = wrap(basePos + bestOff, srcLen);
            for (int i = 0; i < windowSize; ++i)
            {
                int si = wrap(grainStart + i, srcLen);
                int oi = (writePos + i) % maxBufSize;
                float w = hannWin[i];
                bufL[oi] += srcL[si] * w;
                bufR[oi] += (numCh >= 2 ? srcR[si] : srcL[si]) * w;
            }

            available += hopSynthesis;
            writePos = (writePos + hopSynthesis) % maxBufSize;

            sourcePosition += sourceHop;
            if (sourcePosition >= (double)srcLen)
                sourcePosition -= (double)srcLen;
        }

        bool readSample(float& outL, float& outR)
        {
            if (available <= 0) return false;
            outL = bufL[readPos];
            outR = bufR[readPos];
            bufL[readPos] = 0.0f;
            bufR[readPos] = 0.0f;
            readPos = (readPos + 1) % maxBufSize;
            available--;
            return true;
        }

        void resetPosition()
        {
            sourcePosition = 0.0;
            writePos = 0; readPos = 0; available = 0;
            std::memset(bufL, 0, sizeof(bufL));
            std::memset(bufR, 0, sizeof(bufR));
        }

        static int wrap(int pos, int len)
        {
            if (len <= 0) return 0;
            pos %= len;
            return pos < 0 ? pos + len : pos;
        }
    };
} // namespace Betel
