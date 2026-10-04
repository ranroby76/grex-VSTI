#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "DrumDsp.h"
#include "ClapPrototypes.h"

//==============================================================================
//  ClapVoice.h
//
//  Burst train + tail, both on one noise source filtered through a bandpass
//  (the body) plus a highpass (the sizzle).  Burst start times and levels are
//  rolled per hit, so every clap is a slightly different group of hands.
//
//  Macro meanings on this voice:
//      TUNE   filter pitch               DECAY  tail + burst length
//      DAMP   tail amount (the room)     SNAP   burst sharpness
//      COLOR  brightness                 DRIVE  saturation
//      HUMAN  timing spread + per-hit level/decay
//      VEL    velocity -> level, brightness
//==============================================================================

namespace clap
{

class ClapVoice
{
public:
    static constexpr int   kMaxBursts = 32;   // guiro and vibraslap need long trains
    static constexpr float kMakeup    = 1.0f;

    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);
        killStep = 1.0f / (0.003f * sr);
        reset();
    }

    void setSeed (uint32_t s) noexcept   { rng.seed (s); }

    void reset()
    {
        active = false;
        killing = false;
        killGain = 1.0f;
        bpSvf.reset();
        hpSvf.reset();
    }

    bool isActive() const noexcept   { return active; }
    void beginKill() noexcept        { if (active) killing = true; }

    //==========================================================================
    void trigger (const ClapPrototype& p, const drum::Macros& m, int velocity)
    {
        const auto hum = drum::rollHumanize (rng, m.humanize);
        const auto vr  = drum::velocityResponse (velocity, m.velSens);

        const float tuneMul  = std::pow (2.0f, (m.tuneSemis + hum.tuneCents * 0.01f) / 12.0f);
        const float decayMul = std::pow (4.0f, drum::bipolarAmount (m.decay)) * hum.decayMul;
        const float dampAmt  = drum::bipolarAmount (m.damp);    // + = less room
        const float snapAmt  = drum::bipolarAmount (m.snap);    // + = sharper bursts
        const float colorAmt = drum::bipolarAmount (m.color);   // + = brighter

        // ---- Burst schedule -----------------------------------------------------
        numBursts = std::clamp ((int) std::lround (p.bursts), 1, kMaxBursts);
        const float spread = std::clamp (p.spread + 0.3f * std::clamp (m.humanize, 0.0f, 1.0f), 0.0f, 1.0f);

        float t = 0.0f;
        for (int k = 0; k < numBursts; ++k)
        {
            if (k > 0)
                t += p.spacing * (1.0f + spread * 0.35f * rng.bipolar());
            burstStart[k] = (int) (t * sr);
            burstAmp  [k] = ((k == 0) ? 1.0f : 1.0f - spread * 0.35f * rng.unipolar())
                          * std::pow (std::clamp (p.burstDecay, 0.05f, 1.0f), (float) k);
        }
        nextBurst = 0;
        burstEnv  = 0.0f;

        const float burstTau = std::clamp (p.burstTau * decayMul * std::pow (2.0f, -snapAmt * 0.8f),
                                           0.0008f, 0.02f);
        burstCoef = drum::coefForTau (burstTau, sr);

        // ---- Tail -----------------------------------------------------------------
        tailStart = burstStart[numBursts - 1];
        tailAmp   = p.tailLevel * std::pow (2.0f, -dampAmt * 1.5f);
        const float tailTau = std::clamp (p.tailTau * decayMul, 0.01f, 3.0f);
        tailCoef  = drum::coefForTau (tailTau, sr);
        tailEnv   = 0.0f;
        tailOn    = false;

        // ---- Filters --------------------------------------------------------------
        const float colorMul = std::pow (2.0f, colorAmt * 0.5f);
        bpSvf.reset();
        hpSvf.reset();
        bpSvf.set (p.bpFreq * tuneMul * colorMul * vr.bright, std::max (0.3f, p.bpQ), sr);
        hpSvf.set (p.hpFreq * tuneMul * colorMul, 0.707f, sr);
        hpMix = std::clamp (p.hpMix * (1.0f + colorAmt * 0.5f), 0.0f, 1.5f);

        // ---- Drive + output -------------------------------------------------------
        const float d = std::clamp (p.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 9.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        outGain     = p.level * vr.level * drum::dbToGain (hum.levelDb) * kMakeup;

        clock = 0;
        samplesLeft = tailStart + drum::lifetimeSamples (std::max (tailTau, burstTau), sr, 4.0f);
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
            if (nextBurst < numBursts && clock >= burstStart[nextBurst])
            {
                burstEnv = burstAmp[nextBurst];
                ++nextBurst;
            }
            burstEnv *= burstCoef;

            if (! tailOn && clock >= tailStart)
            {
                tailOn  = true;
                tailEnv = tailAmp;
            }
            if (tailOn)
                tailEnv *= tailCoef;

            const float n = rng.bipolar();
            float lp, bp, hp, lp2, bp2, hp2;
            bpSvf.process (n, lp, bp, hp);
            hpSvf.process (n, lp2, bp2, hp2);
            const float filtered = bp + hpMix * hp2;

            float s = filtered * (burstEnv + tailEnv);
            s = std::tanh (s * drivePre) * driveMakeup;
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

    int   numBursts = 1, nextBurst = 0;
    int   burstStart[kMaxBursts] {};
    float burstAmp  [kMaxBursts] {};
    float burstEnv = 0.0f, burstCoef = 0.0f;

    int   tailStart = 0;
    bool  tailOn = false;
    float tailAmp = 0.0f, tailEnv = 0.0f, tailCoef = 0.0f;

    drum::Svf bpSvf, hpSvf;
    float hpMix = 0.0f;

    float drivePre = 1.0f, driveMakeup = 1.0f, outGain = 1.0f;

    int   clock = 0;
    bool  active = false;
    int   samplesLeft = 0;
    bool  killing = false;
    float killGain = 1.0f, killStep = 0.0f;
};

} // namespace clap
