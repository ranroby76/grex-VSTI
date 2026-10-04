#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "DrumDsp.h"
#include "CymbalPrototypes.h"

//==============================================================================
//  CymbalVoice.h — crash (49) and ride (51)
//
//      Wash  — 8 band-limited squares + noise -> bandpass -> highpass,
//              attack swell x long exponential decay
//      Ping  — 3 decaying sines (ride definition / bell)
//      Stick — bandpassed noise tick
//
//  Macro meanings on this voice:
//      TUNE   metal + ping pitch         DECAY  wash and ping length
//      DAMP   ping <-> wash balance (+ = drier: less wash, more ping)
//      SNAP   stick tick                 COLOR  wash brightness
//      DRIVE  saturation                 HUMAN  per-hit tune/decay/tick/level
//      VEL    velocity -> level, brightness, tick
//==============================================================================

namespace cymbal
{

class CymbalVoice
{
public:
    static constexpr float kMakeup = 1.0f;

    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);
        twoPiOverSr = 6.28318530718f / sr;
        killStep = 1.0f / (0.004f * sr);
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
        clickSvf.reset();
    }

    bool isActive() const noexcept   { return active; }
    void beginKill() noexcept        { if (active) killing = true; }

    //==========================================================================
    void trigger (const CymbalPrototype& p, const drum::Macros& m, int velocity)
    {
        const auto hum = drum::rollHumanize (rng, m.humanize);
        const auto vr  = drum::velocityResponse (velocity, m.velSens);

        const float tuneMul  = std::pow (2.0f, (m.tuneSemis + hum.tuneCents * 0.01f) / 12.0f);
        const float decayMul = std::pow (4.0f, drum::bipolarAmount (m.decay)) * hum.decayMul;
        const float dampAmt  = drum::bipolarAmount (m.damp);
        const float colorAmt = drum::bipolarAmount (m.color);
        const float snapMul  = std::clamp (m.snap, 0.0f, 1.0f) * 2.0f;

        // ---- Wash -------------------------------------------------------------------
        for (int k = 0; k < kNumPartials; ++k)
        {
            osc[k].setFrequency (p.baseFreq * p.ratio[k] * tuneMul, sr);
            osc[k].reset (rng.unipolar());
        }

        const float drier = std::max (0.0f, dampAmt);
        const float wetter = std::max (0.0f, -dampAmt);
        washAmp  = (1.0f - 0.7f * drier) * (1.0f + 0.3f * wetter);
        metalAmp = p.metalLevel;
        noiseAmp = p.noiseLevel;

        bpSvf.reset();
        hpSvf.reset();
        bpSvf.set (p.bpFreq * std::pow (2.0f, colorAmt) * vr.bright, std::max (0.3f, p.bpQ), sr);
        hpSvf.set (p.hpFreq * std::pow (2.0f, colorAmt * 0.7f), 0.707f, sr);

        const float washTau = std::clamp (p.washTau * decayMul * std::pow (2.0f, -drier * 0.5f),
                                          0.05f, 8.0f);
        washCoef = drum::coefForTau (washTau, sr);
        washEnv  = 1.0f;
        att      = 0.0f;
        attCoef  = drum::coefForTau (std::max (p.attack, 0.0002f), sr);

        // ---- Ping -------------------------------------------------------------------
        const float pingBoost = (1.0f + 0.8f * drier) * (1.0f - 0.5f * wetter);
        static constexpr float kPingWeights[kNumPings] = { 1.0f, 0.6f, 0.4f };
        // Each ping is a detuned PAIR (they beat, as struck metal does), with a
        // fresh random phase and detune every hit, and a decay that shortens
        // for the higher pings.
        const float detune = std::clamp (p.pingDetune, 0.0f, 0.03f);
        const float spread = std::clamp (p.pingSpread, 0.0f, 0.3f);
        const float pingTau = std::clamp (p.pingTau * decayMul, 0.02f, 4.0f);
        for (int k = 0; k < kNumPings; ++k)
            for (int side = 0; side < 2; ++side)
            {
                const int   j  = k * 2 + side;
                const float dt = (side == 0 ? -detune : detune) * (0.7f + 0.6f * rng.unipolar());
                pingFreq [j] = std::clamp (p.pingFreq[k] * tuneMul * (1.0f + dt), 100.0f, sr * 0.45f);
                pingAmp  [j] = p.pingLevel * pingBoost * kPingWeights[k] * 0.5f;
                pingPhase[j] = rng.unipolar() * 6.28318530718f;
                pingCoef [j] = drum::coefForTau (std::max (0.02f, pingTau * (1.0f - spread * (float) k)), sr);
                pingEnv  [j] = 1.0f;
            }

        // ---- Stick ------------------------------------------------------------------
        clickAmp  = p.clickLevel * snapMul * vr.attack * hum.attackMul;
        clickCoef = drum::coefForTau (std::clamp (p.clickTau, 0.0005f, 0.02f), sr);
        clickEnv  = 1.0f;
        clickSvf.set (p.clickFreq * std::pow (2.0f, colorAmt * 0.5f), 1.0f, sr);

        // ---- Drive + output ---------------------------------------------------------
        const float d = std::clamp (p.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 9.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        outGain     = p.level * vr.level * drum::dbToGain (hum.levelDb) * kMakeup;

        samplesLeft = drum::lifetimeSamples (std::max (washTau, pingTau), sr, 12.0f);
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
            // Wash
            float metal = 0.0f;
            for (int k = 0; k < kNumPartials; ++k)
                metal += osc[k].next();
            metal *= (1.0f / (float) kNumPartials);

            const float src = metal * metalAmp + rng.bipolar() * noiseAmp;
            float lp, bp, hp, lp2, bp2, hp2;
            bpSvf.process (src, lp, bp, hp);
            hpSvf.process (bp, lp2, bp2, hp2);

            washEnv *= washCoef;
            att = 1.0f + (att - 1.0f) * attCoef;
            const float wash = hp2 * washEnv * att * washAmp;

            // Ping: detuned pairs, each with its own decay
            float ping = 0.0f;
            for (int j = 0; j < kPingPartials; ++j)
            {
                pingPhase[j] += twoPiOverSr * pingFreq[j];
                if (pingPhase[j] > 6.28318530718f) pingPhase[j] -= 6.28318530718f;
                pingEnv[j] *= pingCoef[j];
                ping += std::sin (pingPhase[j]) * pingAmp[j] * pingEnv[j];
            }

            // Stick
            clickEnv *= clickCoef;
            clickSvf.process (rng.bipolar(), lp, bp, hp);
            const float click = bp * clickEnv * clickAmp;

            float s = std::tanh ((wash + ping + click) * drivePre) * driveMakeup;
            s *= outGain;

            if (killing)
            {
                killGain -= killStep;
                if (killGain <= 0.0f) { active = false; return; }
            }
            s *= killGain;

            left [i] += s;
            right[i] += s;

            if (--samplesLeft <= 0) { active = false; return; }
        }
    }

private:
    drum::Rng rng;
    float sr = 48000.0f, twoPiOverSr = 0.0f;

    drum::BlepSquare osc[kNumPartials];
    float metalAmp = 0.0f, noiseAmp = 0.0f, washAmp = 1.0f;
    drum::Svf bpSvf, hpSvf;
    float washEnv = 0.0f, washCoef = 0.0f, att = 0.0f, attCoef = 0.0f;

    static constexpr int kPingPartials = kNumPings * 2;
    float pingFreq [kPingPartials] {};
    float pingAmp  [kPingPartials] {};
    float pingPhase[kPingPartials] {};
    float pingEnv  [kPingPartials] {};
    float pingCoef [kPingPartials] {};

    float clickAmp = 0.0f, clickEnv = 0.0f, clickCoef = 0.0f;
    drum::Svf clickSvf;

    float drivePre = 1.0f, driveMakeup = 1.0f, outGain = 1.0f;

    bool  active = false;
    int   samplesLeft = 0;
    bool  killing = false;
    float killGain = 1.0f, killStep = 0.0f;
};

} // namespace cymbal
