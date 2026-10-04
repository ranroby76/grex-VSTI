#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "DrumDsp.h"
#include "MetalPrototypes.h"

//==============================================================================
//  MetalVoice.h  —  cowbells, agogos, triangles, jingle bell, bell tree.
//
//  Macro meanings on this voice:
//      TUNE   partial pitch              DECAY  ring and strike length
//      DAMP   more of the fast stage (a hand on the bell)
//      SNAP   stick click                COLOR  body and shimmer brightness
//      DRIVE  saturation                 HUMAN  per-hit tune/decay/click/level
//      VEL    velocity -> level, brightness, click
//
//  Latched at trigger(); render() allocates nothing.  JUCE-free.
//==============================================================================

namespace metal
{

class MetalVoice
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
        bodySvf.reset();
        clickSvf.reset();
        noiseSvf.reset();
    }

    bool isActive() const noexcept   { return active; }
    void beginKill() noexcept        { if (active) killing = true; }

    //==========================================================================
    void trigger (const MetalPrototype& p, const drum::Macros& m, int velocity)
    {
        const auto hum = drum::rollHumanize (rng, m.humanize);
        const auto vr  = drum::velocityResponse (velocity, m.velSens);

        tuneMul = std::pow (2.0f, (m.tuneSemis + hum.tuneCents * 0.01f) / 12.0f);
        const float decayMul = std::pow (4.0f, drum::bipolarAmount (m.decay)) * hum.decayMul;
        const float dampAmt  = drum::bipolarAmount (m.damp);
        const float colorAmt = drum::bipolarAmount (m.color);
        const float snapMul  = std::clamp (m.snap, 0.0f, 1.0f) * 2.0f;

        proto = p;
        for (int k = 0; k < kNumPartials; ++k)
            phase[k] = rng.unipolar();

        squareMix  = std::clamp (p.square, 0.0f, 1.0f);
        bpMix      = std::clamp (p.bpMix,  0.0f, 1.0f);
        fastAmount = std::clamp (p.fastAmount + dampAmt * 0.4f, 0.0f, 0.95f);

        const float fastTau = std::clamp (p.fastTau * decayMul, 0.002f, 0.5f);
        const float slowTau = std::clamp (p.slowTau * decayMul, 0.01f, 6.0f);
        fastCoef = drum::coefForTau (fastTau, sr);
        slowCoef = drum::coefForTau (slowTau, sr);

        bodySvf.reset();
        bodySvf.set (p.bpFreq * std::pow (2.0f, colorAmt) * vr.bright, std::max (0.3f, p.bpQ), sr);
        noiseSvf.reset();
        noiseSvf.set (p.noiseFreq * std::pow (2.0f, colorAmt), 1.5f, sr);
        noiseAmp = p.noiseLevel;

        clickAmp  = p.clickLevel * snapMul * vr.attack * hum.attackMul;
        clickCoef = drum::coefForTau (0.0015f, sr);
        clickSvf.reset();
        clickSvf.set (p.clickFreq * std::pow (2.0f, colorAmt * 0.5f), 1.0f, sr);

        numStrikes    = std::clamp ((int) std::lround (p.strikes), 1, 32);
        strikeSamples = std::max (1, (int) (std::max (p.strikeSpacing, 0.001f) * sr));
        strikeIndex   = 0;
        clock         = 0;
        startStrike (0);

        const float d = std::clamp (p.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 9.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        outGain     = p.level * vr.level * drum::dbToGain (hum.levelDb) * kMakeup;

        samplesLeft = (numStrikes - 1) * strikeSamples
                    + drum::lifetimeSamples (slowTau, sr, 8.0f);
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
            if (numStrikes > 1 && strikeIndex + 1 < numStrikes
                && clock >= (strikeIndex + 1) * strikeSamples)
                startStrike (strikeIndex + 1);

            // Partials
            float mix = 0.0f;
            for (int k = 0; k < kNumPartials; ++k)
            {
                if (gain[k] <= 0.0f) continue;      // unused partial
                const float ph = phase[k];
                const float sine = std::sin (6.28318530718f * ph);
                float sq = (ph < 0.5f) ? 1.0f : -1.0f;
                sq += drum::polyBlep (ph, inc[k]);
                float shifted = ph + 0.5f;
                if (shifted >= 1.0f) shifted -= 1.0f;
                sq -= drum::polyBlep (shifted, inc[k]);
                mix += gain[k] * drum::lerp (sine, sq, squareMix);

                phase[k] += inc[k];
                if (phase[k] >= 1.0f) phase[k] -= 1.0f;
            }

            float lp, bp, hp;
            bodySvf.process (mix, lp, bp, hp);
            const float body = drum::lerp (mix, bp, bpMix);

            envFast *= fastCoef;
            envSlow *= slowCoef;
            const float env = strikeAmp * (fastAmount * envFast + (1.0f - fastAmount) * envSlow);

            noiseSvf.process (rng.bipolar(), lp, bp, hp);
            const float shimmer = bp * noiseAmp * env;

            clickEnv *= clickCoef;
            clickSvf.process (rng.bipolar(), lp, bp, hp);
            const float click = bp * clickEnv * clickAmp;

            float s = std::tanh ((body * env + shimmer + click) * drivePre) * driveMakeup;
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
    // Strike k of a sequence: retune by k * strikeStep, restart the envelopes
    void startStrike (int k)
    {
        strikeIndex = k;
        const float stepMul = std::pow (2.0f, (proto.strikeStep * (float) k) / 12.0f);
        for (int p = 0; p < kNumPartials; ++p)
        {
            gain[p] = proto.gain[p];
            inc [p] = std::clamp (proto.f0 * proto.ratio[p] * tuneMul * stepMul / sr, 0.0f, 0.45f);
        }
        strikeAmp = std::pow (std::clamp (proto.strikeDecay, 0.05f, 1.0f), (float) k)
                  * (k == 0 ? 1.0f : (1.0f - 0.15f * rng.unipolar()));
        envFast  = 1.0f;
        envSlow  = 1.0f;
        clickEnv = (k == 0) ? 1.0f : 0.5f;
    }

    drum::Rng rng;
    float sr = 48000.0f;

    MetalPrototype proto {};
    float tuneMul = 1.0f;

    float phase[kNumPartials] {};
    float inc  [kNumPartials] {};
    float gain [kNumPartials] {};
    float squareMix = 0.0f, bpMix = 0.0f;

    drum::Svf bodySvf, noiseSvf, clickSvf;
    float noiseAmp = 0.0f;

    float envFast = 0.0f, envSlow = 0.0f, fastCoef = 0.0f, slowCoef = 0.0f, fastAmount = 0.0f;
    float clickAmp = 0.0f, clickEnv = 0.0f, clickCoef = 0.0f;

    int   numStrikes = 1, strikeSamples = 1, strikeIndex = 0, clock = 0;
    float strikeAmp = 1.0f;

    float drivePre = 1.0f, driveMakeup = 1.0f, outGain = 1.0f;

    bool  active = false;
    int   samplesLeft = 0;
    bool  killing = false;
    float killGain = 1.0f, killStep = 0.0f;
};

} // namespace metal
