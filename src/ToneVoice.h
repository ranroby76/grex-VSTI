#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "DrumDsp.h"
#include "TonePrototypes.h"

//==============================================================================
//  ToneVoice.h  —  whistles, cuica, scratches, zaps, blips, noise effects.
//
//  Pitch (semitones from f0) =  glide  pitchStart * e^(-t/pitchTau)
//                            +  fall   pitchFall  * (1 - e^(-t/fallTau))
//                            +  LFO    vibSemis   * sin(2 pi vibRate t)
//
//  Macro meanings on this voice:
//      TUNE   pitch                      DECAY  hold + decay length
//      DAMP   darker (low-pass down)     SNAP   faster attack, deeper glide
//      COLOR  sine -> saw, noise higher  DRIVE  saturation
//      HUMAN  per-hit tune/decay/level   VEL    velocity -> level, glide depth
//
//  Latched at trigger(); render() allocates nothing.  JUCE-free.
//==============================================================================

namespace tone
{

class ToneVoice
{
public:
    static constexpr float kMakeup = 1.0f;

    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);
        killStep = 1.0f / (0.004f * sr);
        reset();
    }

    void setSeed (uint32_t s) noexcept   { rng.seed (s); }

    void reset()
    {
        active = false;
        killing = false;
        killGain = 1.0f;
        noiseSvf.reset();
        lpSvf.reset();
    }

    bool isActive() const noexcept   { return active; }
    void beginKill() noexcept        { if (active) killing = true; }

    //==========================================================================
    void trigger (const TonePrototype& p, const drum::Macros& m, int velocity)
    {
        const auto hum = drum::rollHumanize (rng, m.humanize);
        const auto vr  = drum::velocityResponse (velocity, m.velSens);

        const float decayMul = std::pow (4.0f, drum::bipolarAmount (m.decay)) * hum.decayMul;
        const float dampAmt  = drum::bipolarAmount (m.damp);
        const float snapAmt  = drum::bipolarAmount (m.snap);
        const float colorAmt = drum::bipolarAmount (m.color);

        baseHz = p.f0 * std::pow (2.0f, (m.tuneSemis + hum.tuneCents * 0.01f) / 12.0f);

        toneAmp = std::clamp (p.toneLevel, 0.0f, 1.0f);
        sawMix  = std::clamp (p.saw + colorAmt * 0.4f, 0.0f, 1.0f);

        glideSemis = p.pitchStartSemis * vr.pitch * (1.0f + 0.5f * std::max (0.0f, snapAmt));
        glideCoef  = drum::coefForTau (std::clamp (p.pitchTau, 0.001f, 2.0f), sr);
        glideEnv   = 1.0f;
        fallSemis  = p.pitchFallSemis;
        fallCoef   = drum::coefForTau (std::clamp (p.fallTau, 0.005f, 4.0f), sr);
        fallEnv    = 1.0f;

        vibInc   = std::max (0.0f, p.vibRate)  / sr;
        vibSemis = p.vibSemis;
        vibPhase = 0.0f;
        tremInc  = std::max (0.0f, p.tremRate) / sr;
        tremDepth = std::clamp (p.tremDepth, 0.0f, 1.0f);
        tremPhase = 0.0f;

        noiseAmp   = p.noiseLevel;
        noiseBase  = p.noiseFreq * std::pow (2.0f, colorAmt * 0.5f);
        noiseQ     = std::max (0.3f, p.noiseQ);
        noiseTrack = std::clamp (p.noiseTrack, 0.0f, 1.0f);
        noiseSvf.reset();
        noiseSvf.set (noiseBase, noiseQ, sr);
        lpSvf.reset();
        lpSvf.set (p.lpFreq * std::pow (2.0f, -dampAmt * 2.0f), 0.707f, sr);

        attackSamples = std::max (1, (int) (std::max (p.attack, 1.0e-4f)
                                            * std::pow (2.0f, -snapAmt * 2.0f) * sr));
        holdSamples   = (int) (std::max (0.0f, p.hold) * decayMul * sr);
        const float decayTau = std::clamp (p.decayTau * decayMul, 0.003f, 4.0f);
        decayCoef = drum::coefForTau (decayTau, sr);
        decayEnv  = 1.0f;

        phase = 0.0f;
        clock = 0;

        const float d = std::clamp (p.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 9.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        outGain     = p.level * vr.level * drum::dbToGain (hum.levelDb) * kMakeup;

        samplesLeft = attackSamples + holdSamples + drum::lifetimeSamples (decayTau, sr, 6.0f);
        noiseRetune = 0;
        killGain = 1.0f;
        killing  = false;
        active   = true;
    }

    //==========================================================================
    void render (float* left, float* right, int numSamples)
    {
        if (! active)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // Pitch path
            glideEnv *= glideCoef;
            fallEnv  *= fallCoef;
            const float vib = (vibInc > 0.0f) ? std::sin (6.28318530718f * vibPhase) * vibSemis : 0.0f;
            vibPhase += vibInc;
            if (vibPhase >= 1.0f) vibPhase -= 1.0f;

            const float move = glideSemis * glideEnv + fallSemis * (1.0f - fallEnv) + vib;
            const float hz   = std::clamp (baseHz * std::exp2 (move / 12.0f), 20.0f, sr * 0.45f);
            const float inc  = hz / sr;

            // Oscillator: sine <-> band-limited saw
            const float sine = std::sin (6.28318530718f * phase);
            const float saw  = 2.0f * phase - 1.0f - drum::polyBlep (phase, inc);
            const float osc  = drum::lerp (sine, saw, sawMix) * toneAmp;
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;

            // Noise, optionally following the pitch path (retuned every 32 samples)
            if (noiseTrack > 0.0f && --noiseRetune <= 0)
            {
                noiseSvf.set (noiseBase * std::exp2 (move * noiseTrack / 12.0f), noiseQ, sr);
                noiseRetune = 32;
            }
            float lp, bp, hp;
            noiseSvf.process (rng.bipolar(), lp, bp, hp);
            const float noise = bp * noiseAmp;

            lpSvf.process (osc + noise, lp, bp, hp);

            // Envelope: linear attack, hold, exponential decay; then the trill
            float env;
            if (clock < attackSamples)                       env = (float) clock / (float) attackSamples;
            else if (clock < attackSamples + holdSamples)    env = 1.0f;
            else                                             { decayEnv *= decayCoef; env = decayEnv; }

            if (tremDepth > 0.0f)
            {
                env *= 1.0f - tremDepth * (0.5f + 0.5f * std::sin (6.28318530718f * tremPhase));
                tremPhase += tremInc;
                if (tremPhase >= 1.0f) tremPhase -= 1.0f;
            }

            float s = std::tanh (lp * env * drivePre) * driveMakeup;
            s *= outGain;

            if (killing)
            {
                killGain -= killStep;
                if (killGain <= 0.0f) { active = false; return; }
            }
            s *= killGain;

            left [i] += s;
            right[i] += s;

            ++clock;
            if (--samplesLeft <= 0) { active = false; return; }
        }
    }

private:
    drum::Rng rng;
    float sr = 48000.0f;

    float baseHz = 440.0f, phase = 0.0f, toneAmp = 1.0f, sawMix = 0.0f;
    float glideSemis = 0.0f, glideEnv = 0.0f, glideCoef = 0.0f;
    float fallSemis = 0.0f, fallEnv = 0.0f, fallCoef = 0.0f;
    float vibInc = 0.0f, vibSemis = 0.0f, vibPhase = 0.0f;
    float tremInc = 0.0f, tremDepth = 0.0f, tremPhase = 0.0f;

    float noiseAmp = 0.0f, noiseBase = 1000.0f, noiseQ = 1.0f, noiseTrack = 0.0f;
    int   noiseRetune = 0;
    drum::Svf noiseSvf, lpSvf;

    int   attackSamples = 1, holdSamples = 0, clock = 0;
    float decayEnv = 1.0f, decayCoef = 0.0f;

    float drivePre = 1.0f, driveMakeup = 1.0f, outGain = 1.0f;

    bool  active = false;
    int   samplesLeft = 0;
    bool  killing = false;
    float killGain = 1.0f, killStep = 0.0f;
};

} // namespace tone
