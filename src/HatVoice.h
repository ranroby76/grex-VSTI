#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "DrumDsp.h"
#include "HatPrototypes.h"

//==============================================================================
//  HatVoice.h
//
//  Metal (six band-limited squares) + noise -> bandpass -> highpass -> envelope.
//  The stick click is injected BEFORE the filters so it takes the hat's own
//  colour instead of sounding like a separate click on top.
//
//  CHOKE: an open hat is cut by the next closed or pedal hit, as on a real
//  stand.  beginChoke() fades over 5 ms — longer than a steal, so the cut is
//  a quick close rather than a click.
//
//  Macro meanings on this voice:
//      TUNE   metal pitch                DECAY  articulation length
//      DAMP   metal <-> noise balance (+ = more noise, less ring)
//      SNAP   stick click                COLOR  filter brightness
//      DRIVE  saturation                 HUMAN  per-hit tune/decay/click/level
//      VEL    velocity -> level, brightness, click
//==============================================================================

namespace hat
{

class HatVoice
{
public:
    static constexpr float kMakeup = 1.0f;

    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);
        killStep  = 1.0f / (0.003f * sr);    // steal
        chokeStep = 1.0f / (0.005f * sr);    // choke by closed/pedal
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
    bool isOpen()   const noexcept   { return active && open; }

    void beginKill()  noexcept   { if (active) { killing = true; fadeStep = killStep;  } }
    void beginChoke() noexcept   { if (active) { killing = true; fadeStep = chokeStep; } }

    //==========================================================================
    void trigger (const HatPrototype& p, const drum::Macros& m, int velocity, int articulation)
    {
        const auto hum = drum::rollHumanize (rng, m.humanize);
        const auto vr  = drum::velocityResponse (velocity, m.velSens);

        const float tuneMul  = std::pow (2.0f, (m.tuneSemis + hum.tuneCents * 0.01f) / 12.0f);
        const float decayMul = std::pow (4.0f, drum::bipolarAmount (m.decay)) * hum.decayMul;
        const float dampAmt  = drum::bipolarAmount (m.damp);
        const float colorAmt = drum::bipolarAmount (m.color);
        const float snapMul  = std::clamp (m.snap, 0.0f, 1.0f) * 2.0f;

        articulation = std::clamp (articulation, 0, 2);
        open = (articulation == Open);

        // ---- Metal cluster ------------------------------------------------------
        for (int k = 0; k < kNumOscs; ++k)
        {
            osc[k].setFrequency (p.baseFreq * p.ratio[k] * tuneMul, sr);
            osc[k].reset (rng.unipolar());      // free-running phases, like the circuit
        }

        // DAMP trades ring for hiss
        const float moreNoise = std::max (0.0f, dampAmt);
        const float moreMetal = std::max (0.0f, -dampAmt);
        metalAmp = p.metalLevel * (1.0f - 0.8f * moreNoise) * (1.0f + 0.5f * moreMetal);
        noiseAmp = p.noiseLevel * (1.0f + moreNoise)        * (1.0f - 0.6f * moreMetal);

        // ---- Filters ------------------------------------------------------------
        const float pedalShift = (articulation == Pedal) ? 0.8f : 1.0f;
        bpSvf.reset();
        hpSvf.reset();
        bpSvf.set (p.bpFreq * std::pow (2.0f, colorAmt) * vr.bright, std::max (0.3f, p.bpQ), sr);
        hpSvf.set (p.hpFreq * std::pow (2.0f, colorAmt * 0.7f) * pedalShift, 0.707f, sr);

        // ---- Envelope -----------------------------------------------------------
        const float baseTau = (articulation == Closed) ? p.closedTau
                            : (articulation == Pedal)  ? p.pedalTau
                                                       : p.openTau;
        const float tau = std::clamp (baseTau * decayMul, 0.005f, 4.0f);
        envCoef = drum::coefForTau (tau, sr);
        env     = 1.0f;
        att     = 0.0f;
        attCoef = drum::coefForTau (std::max (p.attack, 0.0001f), sr);

        // ---- Stick click ----------------------------------------------------------
        clickAmp  = p.clickLevel * snapMul * vr.attack * hum.attackMul;
        clickCoef = drum::coefForTau (0.0015f, sr);
        clickEnv  = 1.0f;

        // ---- Drive + output -------------------------------------------------------
        const float d = std::clamp (p.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 9.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        outGain     = p.level * vr.level * drum::dbToGain (hum.levelDb) * kMakeup;

        samplesLeft = drum::lifetimeSamples (tau, sr, 5.0f);
        killGain = 1.0f;
        killing  = false;
        fadeStep = killStep;
        active   = true;
    }

    //==========================================================================
    void render (float* left, float* right, int numSamples)
    {
        if (! active)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float metal = 0.0f;
            for (int k = 0; k < kNumOscs; ++k)
                metal += osc[k].next();
            metal *= (1.0f / (float) kNumOscs);

            clickEnv *= clickCoef;
            const float src = metal * metalAmp
                            + rng.bipolar() * noiseAmp
                            + rng.bipolar() * clickEnv * clickAmp;

            float lp, bp, hp, lp2, bp2, hp2;
            bpSvf.process (src, lp, bp, hp);
            hpSvf.process (bp, lp2, bp2, hp2);

            env *= envCoef;
            att  = 1.0f + (att - 1.0f) * attCoef;

            float s = hp2 * env * att;
            s = std::tanh (s * drivePre) * driveMakeup;
            s *= outGain;

            if (killing)
            {
                killGain -= fadeStep;
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
    float sr = 48000.0f;

    drum::BlepSquare osc[kNumOscs];
    float metalAmp = 0.0f, noiseAmp = 0.0f;

    drum::Svf bpSvf, hpSvf;

    float env = 0.0f, envCoef = 0.0f, att = 0.0f, attCoef = 0.0f;
    float clickAmp = 0.0f, clickEnv = 0.0f, clickCoef = 0.0f;

    float drivePre = 1.0f, driveMakeup = 1.0f, outGain = 1.0f;

    bool  open = false;
    bool  active = false;
    int   samplesLeft = 0;
    bool  killing = false;
    float killGain = 1.0f, killStep = 0.0f, chokeStep = 0.0f, fadeStep = 0.0f;
};

} // namespace hat
