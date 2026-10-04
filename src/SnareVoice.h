#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "DrumDsp.h"
#include "SnarePrototypes.h"

//==============================================================================
//  SnareVoice.h
//
//  One snare (or side-stick) hit:
//      Body  — 3 decaying sine modes with a shared pitch envelope
//      Wires — noise through a TPT SVF, bandpass <-> highpass, own envelope
//      Click — stick attack: noise burst through a bandpass
//      Rim   — one ringing high mode (rimshot / cross-stick crack)
//      Drive — tanh with makeup
//
//  Macro meanings on this voice:
//      TUNE   body + rim pitch          DECAY  body, wires and rim length
//      DAMP   body length only (the head, not the wires)
//      SNAP   stick click + rim         COLOR  wire / click brightness
//      DRIVE  saturation                HUMAN  per-hit tune/decay/click/level
//      VEL    velocity -> level, attack, brightness, pitch drop
//
//  Everything is latched in trigger(); render() allocates nothing.
//==============================================================================

namespace snare
{

class SnareVoice
{
public:
    // Engine makeup so a typical snare sits beside the kick without re-mixing.
    static constexpr float kMakeup = 1.0f;

    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);
        twoPiOverSr = 6.28318530718f / sr;
        killStep = 1.0f / (0.003f * sr);          // 3 ms steal fade
        reset();
    }

    void setSeed (uint32_t s) noexcept   { rng.seed (s); }

    void reset()
    {
        active = false;
        killing = false;
        killGain = 1.0f;
        wireSvf.reset();
        clickSvf.reset();
    }

    bool isActive() const noexcept   { return active; }
    void beginKill() noexcept        { if (active) killing = true; }

    //==========================================================================
    //==========================================================================
    //  THE SNARE MODELS (2026-09 rebuild).  Every snare, side stick and wood
    //  picks one via SnarePrototype::model.
    //
    //    0  ORIGINAL   the first engine: enveloped body sines + wire noise
    //    1  STRUCK     a stick hits a head: the strike pulse (its width is the
    //                  stick's hardness) rings 3 head resonators whose pitch
    //                  follows the head's energy, and a rim resonator (the
    //                  crack).  The wires hold, then decay; their buzz follows
    //                  the head's motion; RECT gives them the 808's rectified
    //                  grain.  A click made from the head's own motion, the
    //                  stick click, and a level-dependent VCA.  With the wires
    //                  at zero the same model plays cross sticks, claves and
    //                  woodblocks.
    //    2  808        port of Mutable Instruments Plaits' AnalogSnareDrum: 5
    //                  pulse-pinged shell resonators (1, 2, 3.18, 4.16, 5.62),
    //                  soft-clipped, plus half-wave rectified noise band-passed
    //                  at 16x the pitch - the TR-808 snare circuit
    //    3  909        port of Plaits' SyntheticSnareDrum: two shaped
    //                  oscillators (f, 1.47 f) with the 909's reset-noise grit,
    //                  noise band-limited to 10x..35x the pitch, a 40-70 ms hold
    //
    //  Plaits drum code: Copyright 2016 Emilie Gillet, MIT licence -
    //  "Permission is hereby granted, free of charge, to any person obtaining
    //  a copy of this software ... to deal in the Software without
    //  restriction, including without limitation the rights to use, copy,
    //  modify, merge, publish, distribute, sublicense, and/or sell copies of
    //  the Software ...".  The notice travels with the ported code here.
    //==========================================================================
    void trigger (const SnarePrototype& p, const drum::Macros& m, int velocity)
    {
        model = (int) std::lround (std::clamp (p.model, 0.0f, 3.0f));
        if (model == 0) { triggerOriginal (p, m, velocity); return; }

        const auto hum = drum::rollHumanize (rng, m.humanize);
        const auto vr  = drum::velocityResponse (velocity, m.velSens);
        const float vel      = std::clamp ((float) velocity, 1.0f, 127.0f) / 127.0f;
        const float sens     = std::clamp (m.velSens, 0.0f, 1.0f);
        const float tuneMul  = std::pow (2.0f, (m.tuneSemis + hum.tuneCents * 0.01f) / 12.0f);
        const float decayMul = std::pow (4.0f, drum::bipolarAmount (m.decay)) * hum.decayMul;
        const float decayAdj = (m.decay - 0.5f) * 0.6f + (hum.decayMul - 1.0f);
        const float dampAmt  = drum::bipolarAmount (m.damp);
        const float colorAmt = drum::bipolarAmount (m.color);
        const float snapMul  = std::clamp (m.snap, 0.0f, 1.0f) * 2.0f * hum.attackMul;
        const float accent   = lerp (1.0f, vel, sens);

        const float d = std::clamp (p.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 6.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        static constexpr float kTrim[4] = { 1.0f, kTrimStruck, kTrim808, kTrim909 };
        outGain = p.level * kTrim[model] * (model == 1 ? vr.level : 1.0f) * drum::dbToGain (hum.levelDb);
        dcX1 = dcY1 = 0.0f;
        dcR  = 1.0f - 6.28318530718f * 12.0f / sr;
        quietRun = 0;

        const float f0 = std::clamp (p.bodyF0 * tuneMul, 40.0f, 4000.0f);
        switch (model)
        {
            case 1:  triggerStruck (p, f0, tuneMul, vel, sens, vr, decayMul, dampAmt, colorAmt, snapMul); break;
            case 2:  trigger808 (p, f0, accent, decayAdj - dampAmt * 0.2f, colorAmt, m.snap);             break;
            default: trigger909 (p, f0, accent, decayAdj - dampAmt * 0.2f, colorAmt, m.snap);             break;
        }
        killGain = 1.0f;
        killing  = false;
        active   = true;
    }

    void render (float* left, float* right, int numSamples)
    {
        if (! active)
            return;
        if (model == 0) { renderOriginal (left, right, numSamples); return; }

        for (int i = 0; i < numSamples; ++i)
        {
            float s = (model == 2) ? tick808() : (model == 3) ? tick909() : tickStruck();
            const float y = s - dcX1 + dcR * dcY1;       // DC blocker
            dcX1 = s;
            dcY1 = y;
            s = std::tanh (y * drivePre) * driveMakeup * outGain;

            if (killing)
            {
                killGain -= killStep;
                if (killGain <= 0.0f) { active = false; return; }
            }
            s *= killGain;
            left [i] += s;
            right[i] += s;

            quietRun = (std::fabs (s) < 1.0e-5f) ? quietRun + 1 : 0;
            if (--samplesLeft <= 0 || quietRun > (int) (0.05f * sr)) { active = false; return; }
        }
    }

    // MODEL 0 - the original engine
    void triggerOriginal (const SnarePrototype& p, const drum::Macros& m, int velocity)
    {
        const auto hum = drum::rollHumanize (rng, m.humanize);
        const auto vr  = drum::velocityResponse (velocity, m.velSens);

        const float tuneMul  = std::pow (2.0f, (m.tuneSemis + hum.tuneCents * 0.01f) / 12.0f);
        const float decayMul = std::pow (4.0f, drum::bipolarAmount (m.decay)) * hum.decayMul;
        const float dampAmt  = drum::bipolarAmount (m.damp);    // + = shorter body
        const float colorAmt = drum::bipolarAmount (m.color);   // + = brighter
        const float snapMul  = std::clamp (m.snap, 0.0f, 1.0f) * 2.0f;

        float longest = 0.0f;

        // ---- Body ------------------------------------------------------------
        const float f0 = std::clamp (p.bodyF0 * tuneMul, 60.0f, 1200.0f);
        const float bodyTauMul = decayMul * std::pow (2.0f, -dampAmt * 2.0f);
        for (int k = 0; k < kNumBodyModes; ++k)
        {
            bodyFreq[k]  = f0 * p.bodyRatio[k];
            bodyAmp [k]  = p.bodyGain[k];
            const float tau = std::clamp (p.bodyTau[k] * bodyTauMul, 0.003f, 3.0f);
            bodyCoef[k]  = drum::coefForTau (tau, sr);
            bodyEnv [k]  = 1.0f;
            longest = std::max (longest, tau);

            // Struck: the body starts at its peak; upper modes jittered and
            // detuned per hit so repeated hits are never one waveform.
            bodyPhase[k] = 1.5707963f * std::clamp (p.onset, 0.0f, 1.0f)
                         + (k == 0 ? 0.0f : rng.bipolar() * 0.5f);
            if (k > 0)
                bodyFreq[k] *= 1.0f + std::clamp (p.bodyScatter, 0.0f, 0.05f) * rng.bipolar();
        }

        const float pRatio = 1.0f + (p.pitchRatio - 1.0f) * vr.pitch;
        pitchDepth = std::max (0.0f, pRatio - 1.0f);
        pitchEnv   = 1.0f;
        pitchCoef  = drum::coefForTau (std::clamp (p.pitchTau, 0.001f, 0.3f), sr);

        // ---- Wires -------------------------------------------------------------
        wireAmp = p.wireLevel;
        {
            const float tau = std::clamp (p.wireTau * decayMul, 0.01f, 3.0f);
            wireCoef = drum::coefForTau (tau, sr);
            longest = std::max (longest, tau);
        }
        wireEnv    = 1.0f;
        wireAtt    = 0.0f;
        wireFastEnv  = 1.0f;
        wireFastCoef = drum::coefForTau (std::clamp (p.wireTau * decayMul * 0.2f, 0.004f, 0.5f), sr);
        wireFast     = std::clamp (p.wireFast, 0.0f, 0.9f);
        wireBuzz     = std::clamp (p.wireBuzz, 0.0f, 1.0f);
        onsetStep    = 1.0f / std::max (1.0f, 0.0003f * sr);   // 0.3 ms edge
        onsetGain    = 0.0f;
        wireAttCoef = drum::coefForTau (std::clamp (p.wireAttack, 0.0001f, 0.02f), sr);
        wireHpMix  = std::clamp (p.wireHpMix, 0.0f, 1.0f);
        wireSvf.reset();
        wireSvf.set (p.wireFreq * std::pow (2.0f, colorAmt) * vr.bright,
                     std::max (0.3f, p.wireQ), sr);

        // ---- Click -------------------------------------------------------------
        clickAmp  = p.clickLevel * snapMul * hum.attackMul * vr.attack;
        clickCoef = drum::coefForTau (std::clamp (p.clickTau, 0.0005f, 0.03f), sr);
        clickEnv  = 1.0f;
        clickSvf.reset();
        clickSvf.set (p.clickFreq * std::pow (2.0f, colorAmt * 0.5f), std::clamp (p.clickQ, 0.3f, 3.0f), sr);

        // ---- Rim ---------------------------------------------------------------
        rimFreq  = std::clamp (p.rimFreq * tuneMul, 200.0f, 8000.0f);
        rimAmp   = p.rimLevel * (0.5f + 0.5f * snapMul) * vr.attack;
        {
            const float tau = std::clamp (p.rimTau * decayMul, 0.002f, 0.3f);
            rimCoef = drum::coefForTau (tau, sr);
            longest = std::max (longest, tau);
        }
        rimEnv   = 1.0f;
        rimPhase = 0.0f;

        // ---- Drive + output ----------------------------------------------------
        const float d = std::clamp (p.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 9.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        outGain     = p.level * vr.level * drum::dbToGain (hum.levelDb) * kMakeup;

        samplesLeft = drum::lifetimeSamples (longest, sr, 4.0f);
        killGain = 1.0f;
        killing  = false;
        active   = true;
    }

    //==========================================================================
    void renderOriginal (float* left, float* right, int numSamples)
    {
        if (! active)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // Body
            pitchEnv *= pitchCoef;
            const float fm = 1.0f + pitchDepth * pitchEnv;
            float body = 0.0f;
            for (int k = 0; k < kNumBodyModes; ++k)
            {
                bodyPhase[k] += twoPiOverSr * bodyFreq[k] * fm;
                if (bodyPhase[k] > 6.28318530718f) bodyPhase[k] -= 6.28318530718f;
                bodyEnv[k] *= bodyCoef[k];
                body += std::sin (bodyPhase[k]) * bodyAmp[k] * bodyEnv[k];
            }
            onsetGain = std::min (1.0f, onsetGain + onsetStep);
            body *= onsetGain;

            // The head's own motion (normalised fundamental) drives the rattle
            const float headMotion = std::sin (bodyPhase[0]) * bodyEnv[0];

            // Wires
            wireEnv     *= wireCoef;
            wireFastEnv *= wireFastCoef;
            wireAtt  = 1.0f + (wireAtt - 1.0f) * wireAttCoef;
            float lp, bp, hp;
            wireSvf.process (rng.bipolar(), lp, bp, hp);
            const float wireLevelNow = (1.0f - wireFast) * wireEnv + wireFast * wireFastEnv;
            const float rattle = 1.0f + wireBuzz * headMotion;          // wires ride the head
            const float wires  = drum::lerp (bp, hp, wireHpMix) * wireLevelNow * wireAtt * wireAmp * rattle;

            // Click
            clickEnv *= clickCoef;
            clickSvf.process (rng.bipolar(), lp, bp, hp);
            const float click = bp * clickEnv * clickAmp;

            // Rim
            rimPhase += twoPiOverSr * rimFreq;
            if (rimPhase > 6.28318530718f) rimPhase -= 6.28318530718f;
            rimEnv *= rimCoef;
            const float rim = std::sin (rimPhase) * rimEnv * rimAmp;

            float s = std::tanh ((body + wires + click + rim) * drivePre) * driveMakeup;
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
    static inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
    static inline float semis (float st) noexcept { return std::pow (2.0f, st / 12.0f); }
    float rate48 (float c48) const noexcept { return 1.0f - std::pow (1.0f - c48, 48000.0f / sr); }
    float decay48 (float d48) const noexcept { return std::pow (std::max (0.0f, d48), 48000.0f / sr); }
    inline float white01() noexcept { return 0.5f + 0.5f * rng.bipolar(); }
    static inline float softClip (float x) noexcept
    {
        if (x < -3.0f) return -1.0f;
        if (x >  3.0f) return  1.0f;
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    // One-pole TPT filter (stmlib OnePole): low- and high-pass
    struct OnePole
    {
        void setF (float fNorm) noexcept
        {
            const float g = std::tan (3.14159265f * std::clamp (fNorm, 1.0e-6f, 0.497f));
            G = g / (1.0f + g);
        }
        inline float lp (float x) noexcept { const float v = (x - s) * G; const float y = v + s; s = y + v; return y; }
        inline float hp (float x) noexcept { return x - lp (x); }
        void reset() noexcept { s = 0.0f; }
        float G = 0.0f, s = 0.0f;
    };

    // Two-pole resonator (b0 x + a1 y1 - a2 y2), unit gain at its frequency
    struct Res
    {
        void set (float w, float r) noexcept { a1 = 2.0f * r * std::cos (w); a2 = r * r; b0 = std::sin (w); }
        inline float tick (float x) noexcept { const float y = b0 * x + a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
        void reset() noexcept { y1 = y2 = 0.0f; }
        float a1 = 0.0f, a2 = 0.0f, b0 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };

    //==========================================================================
    //  MODEL 1 - STRUCK HEAD
    //==========================================================================
    void triggerStruck (const SnarePrototype& p, float f0, float tuneMul, float vel, float sens,
                        const drum::VelocityResponse& vr, float decayMul, float dampAmt, float colorAmt, float snapMul)
    {
        auto& s = st;
        float e0 = 0.0f, longest = 0.0f;
        for (int k = 0; k < kNumBodyModes; ++k)
        {
            float f = f0 * p.bodyRatio[k];
            if (k > 0) f *= 1.0f + std::clamp (p.bodyScatter, 0.0f, 0.05f) * rng.bipolar();
            const float tau = std::clamp (p.bodyTau[k] * decayMul * std::pow (2.0f, -dampAmt * 2.0f), 0.002f, 4.0f);
            s.w0[k] = std::min (6.28318530718f * f / sr, 2.9f);
            s.r [k] = std::exp (-1.0f / (tau * sr));
            s.g [k] = p.bodyGain[k];
            s.ak[k] = p.bodyGain[k];
            s.head[k].reset();
            e0 += s.g[k] * s.g[k];
            longest = std::max (longest, tau);
        }
        s.invE0   = e0 > 1.0e-9f ? 1.0f / e0 : 0.0f;
        s.tension = std::max (0.0f, p.tension);
        s.coefCountdown = 0;
        s.pitchDepth = std::max (0.0f, (p.pitchRatio - 1.0f) * vr.pitch);
        s.pitchEnv   = 1.0f;
        s.pitchCoef  = std::exp (-1.0f / (std::clamp (p.pitchTau, 0.001f, 0.2f) * sr));

        // The stick: a raised-cosine force pulse; hard sticks touch briefly
        const float hard  = std::clamp (p.hardness + (vel - 0.75f) * 0.3f * sens, 0.0f, 1.0f);
        const float width = lerp (0.0025f, 0.00015f, hard);
        s.strikeLen   = std::max (2, (int) (width * sr));
        s.strikePos   = 0;
        s.strikeAmp   = 2.0f / (float) s.strikeLen;
        s.strikeNoise = 0.4f * hard;

        // The rim: a pinged resonator (the crack)
        const float rimTau = std::clamp (p.rimTau * decayMul, 0.002f, 1.0f);
        s.rim.reset();
        s.rim.set (std::min (6.28318530718f * p.rimFreq * tuneMul / sr, 2.9f), std::exp (-1.0f / (rimTau * sr)));
        s.rimGain = p.rimLevel * (0.5f + 0.5f * snapMul) * vr.attack;
        longest = std::max (longest, rimTau);

        // The wires: attack, hold, decay (a fast share + the ring), buzz, grain
        s.wireAmp   = p.wireLevel;
        s.wireAtt   = 0.0f;
        s.wireAttK  = 1.0f - std::exp (-1.0f / (std::max (0.00005f, p.wireAttack) * sr));
        s.wireHold  = (int) (std::max (0.0f, p.wireHold) * sr);
        const float wtau = std::clamp (p.wireTau * decayMul * std::pow (2.0f, -dampAmt), 0.004f, 3.0f);
        s.wireEnv   = 1.0f;  s.wireK     = std::exp (-1.0f / (wtau * sr));
        s.wireFastEnv = 1.0f; s.wireFastK = std::exp (-1.0f / (std::max (0.001f, wtau * 0.15f) * sr));
        s.wireFast  = std::clamp (p.wireFast, 0.0f, 0.95f);
        s.wireBuzz  = std::clamp (p.wireBuzz, 0.0f, 1.0f);
        s.wireRect  = std::clamp (p.wireRect, 0.0f, 1.0f);
        s.wireHpMix = std::clamp (p.wireHpMix, 0.0f, 1.0f);
        s.wireSvf.reset();
        s.wireSvf.set (p.wireFreq * std::pow (2.0f, colorAmt) * vr.bright, std::clamp (p.wireQ, 0.3f, 4.0f), sr);
        longest = std::max (longest, wtau + std::max (0.0f, p.wireHold));

        // Clicks: from the head's own motion, and the stick itself
        s.bodyClick = std::max (0.0f, p.bodyClick) * snapMul;
        s.bcEnv  = 1.0f;
        s.bcK    = std::exp (-1.0f / (std::clamp (p.clickTau, 0.0005f, 0.03f) * sr));
        s.bcSvf.reset();
        s.bcSvf.set (std::clamp (p.clickFreq * 0.6f, 400.0f, 9000.0f), 0.7f, sr);
        s.prevHead = 0.0f;
        s.clickAmp = p.clickLevel * snapMul * vr.attack;
        s.clickEnv = 1.0f;
        s.clickK   = std::exp (-1.0f / (std::clamp (p.clickTau, 0.0003f, 0.03f) * sr));
        s.clickSvf.reset();
        s.clickSvf.set (p.clickFreq * std::pow (2.0f, colorAmt * 0.5f), std::clamp (p.clickQ, 0.3f, 3.0f), sr);

        s.vcaDrive = std::clamp (p.vcaDrive, 0.0f, 1.0f);
        s.headNorm = 1.0f / std::max (0.05f, std::sqrt (e0));
        samplesLeft = (int) std::min (sr * 6.0f, sr * (longest * 7.0f + 0.05f));
    }

    inline float tickStruck()
    {
        auto& s = st;
        float x = 0.0f;
        if (s.strikePos < s.strikeLen)
        {
            const float ph = ((float) s.strikePos + 0.5f) / (float) s.strikeLen;
            x = s.strikeAmp * (0.5f - 0.5f * std::cos (6.28318530718f * ph)) * (1.0f + s.strikeNoise * rng.bipolar());
            ++s.strikePos;
        }

        float e = 0.0f;
        for (int k = 0; k < kNumBodyModes; ++k) { s.ak[k] *= s.r[k]; e += s.ak[k] * s.ak[k]; }
        const float eNorm = e * s.invE0;
        s.pitchEnv *= s.pitchCoef;
        if (--s.coefCountdown <= 0)
        {
            s.coefCountdown = 4;
            const float fm = (1.0f + s.tension * eNorm) * (1.0f + s.pitchDepth * s.pitchEnv);
            for (int k = 0; k < kNumBodyModes; ++k)
                s.head[k].set (std::min (s.w0[k] * fm, 3.0f), s.r[k]);
        }
        float head = 0.0f;
        for (int k = 0; k < kNumBodyModes; ++k)
            head += s.head[k].tick (x) * s.g[k];
        const float rim = s.rim.tick (x) * s.rimGain;

        // Wires: the envelope, the buzz with the head, the grain
        float wires = 0.0f;
        if (s.wireAmp > 0.0f)
        {
            s.wireAtt += (1.0f - s.wireAtt) * s.wireAttK;
            if (s.wireHold > 0) --s.wireHold;
            else { s.wireEnv *= s.wireK; s.wireFastEnv *= s.wireFastK; }
            const float env = s.wireAtt * ((1.0f - s.wireFast) * s.wireEnv + s.wireFast * s.wireFastEnv);
            float n = rng.bipolar();
            n = lerp (n, std::max (0.0f, n) * 2.0f - 0.5f, s.wireRect);
            float lp, bp, hp;
            s.wireSvf.process (n, lp, bp, hp);
            const float band = lerp (bp, hp, s.wireHpMix);
            const float buzz = 1.0f - s.wireBuzz + s.wireBuzz * std::min (1.5f, std::fabs (head) * s.headNorm * 3.0f);
            wires = band * env * buzz * s.wireAmp;
        }

        const float envK = std::sqrt (std::max (0.0f, eNorm));
        const float pre  = (head + rim) * (1.0f + s.vcaDrive * 2.5f * envK);
        const float vca  = pre / (1.0f + s.vcaDrive * 0.8f * std::fabs (pre)) + s.vcaDrive * 0.25f * envK;

        float lp, bp, hp;
        s.bcEnv *= s.bcK;
        s.bcSvf.process (head - s.prevHead, lp, bp, hp);
        s.prevHead = head;
        const float bodyClick = bp * s.bcEnv * s.bodyClick * 8.0f;
        s.clickEnv *= s.clickK;
        s.clickSvf.process (rng.bipolar(), lp, bp, hp);
        const float stick = bp * s.clickEnv * s.clickAmp;
        return vca + wires + bodyClick + stick;
    }

    //==========================================================================
    //  MODEL 2 - 808 CIRCUIT (Plaits AnalogSnareDrum port, see above)
    //==========================================================================
    void trigger808 (const SnarePrototype& p, float f0, float accent, float decayAdj, float colorAmt, float snapKnob)
    {
        auto& s = a808;
        const float decay  = std::clamp (p.decay808 + decayAdj, 0.0f, 1.0f);
        float tone         = std::clamp (p.tone808 + colorAmt * 0.3f, 0.0f, 1.0f);
        float snappy       = std::clamp (p.snappy + (snapKnob - 0.5f) * 0.6f, 0.0f, 1.0f);
        const float decayXt = decay * (1.0f + decay * (decay - 1.0f));
        const float q = 2000.0f * semis (decayXt * 84.0f);
        s.noiseDecayK = decay48 (1.0f - 0.0017f * semis (-decay * (50.0f + snappy * 10.0f)));
        s.exciterLeak = snappy * (2.0f - snappy) * 0.1f;
        snappy = std::clamp (snappy * 1.1f - 0.05f, 0.0f, 1.0f);
        s.snappy = snappy;
        static constexpr float kRatios[5] = { 1.00f, 2.00f, 3.18f, 4.16f, 5.62f };
        for (int i = 0; i < 5; ++i)
        {
            const float fHz = f0 * kRatios[i];
            const float fN  = std::min (fHz / sr, 0.499f);
            const float f48 = fHz / 48000.0f;
            s.res[i].reset();
            s.res[i].setFQ (fN, 1.0f + f48 * (i == 0 ? q : q * 0.25f));
        }
        if (tone < 0.666667f)                                // 808-style: 2 modes
        {
            tone *= 1.5f;
            s.gain[0] = 1.5f + (1.0f - tone) * (1.0f - tone) * 4.5f;
            s.gain[1] = 2.0f * tone + 0.15f;
            s.gain[2] = s.gain[3] = s.gain[4] = 0.0f;
        }
        else                                                 // "what the 808 could have been"
        {
            tone = (tone - 0.666667f) * 3.0f;
            s.gain[0] = 1.5f - tone * 0.5f;
            s.gain[1] = 2.15f - tone * 0.7f;
            for (int i = 2; i < 5; ++i) { s.gain[i] = tone; tone *= tone; }
        }
        const float fNoiseHz = std::min (f0 * 16.0f, sr * 0.499f);
        s.noiseFilter.reset();
        s.noiseFilter.setFQ (fNoiseHz / sr, 1.0f + (fNoiseHz / 48000.0f) * 1.5f);
        s.pulseRemaining = std::max (1, (int) (1.0e-3f * sr));
        s.pulseHeight    = 3.0f + 7.0f * accent;
        s.pulse = s.pulseLp = 0.0f;
        s.kPulseDecay = 1.0f - 1.0f / (0.1e-3f * sr);
        s.kPulseLp    = rate48 (0.75f);
        s.noiseEnv    = 2.0f;
        const float tau = (1.0f + (f0 / 48000.0f) * q) / (3.14159265f * f0);
        const float nTau = -1.0f / (sr * std::log (std::max (1.0e-9f, s.noiseDecayK)));
        samplesLeft = (int) std::min (sr * 4.0f, sr * (std::max (tau, nTau) * 8.0f + 0.05f));
    }

    inline float tick808()
    {
        auto& s = a808;
        float pulse;
        if (s.pulseRemaining > 0)                            // Q45 / Q46
        {
            --s.pulseRemaining;
            pulse = (s.pulseRemaining > 0) ? s.pulseHeight : s.pulseHeight - 1.0f;
            s.pulse = pulse;
        }
        else
        {
            s.pulse *= s.kPulseDecay;
            pulse = s.pulse;
        }
        s.pulseLp += (pulse - s.pulseLp) * s.kPulseLp;      // R189 / C57 / R190 ...
        float shell = 0.0f;
        for (int i = 0; i < 5; ++i)
        {
            if (s.gain[i] == 0.0f) continue;
            const float excitation = (i == 0) ? (pulse - s.pulseLp) + 0.006f * pulse : 0.026f * pulse;
            float bp, lp;
            s.res[i].process (excitation, bp, lp);
            shell += s.gain[i] * (bp + excitation * s.exciterLeak);
        }
        shell = softClip (shell);
        float noise = 2.0f * white01() - 1.0f;               // C56 / R194 / Q48 ... D54
        if (noise < 0.0f) noise = 0.0f;
        s.noiseEnv *= s.noiseDecayK;
        noise *= s.noiseEnv * s.snappy * 2.0f;
        float nbp, nlp;
        s.noiseFilter.process (noise, nbp, nlp);             // C66 / R201 / C67 ... Q49
        return nbp + shell * (1.0f - s.snappy);             // IC13
    }

    //==========================================================================
    //  MODEL 3 - 909 CIRCUIT (Plaits SyntheticSnareDrum port, see above)
    //==========================================================================
    static inline float distortedSine909 (float phase) noexcept
    {
        const float tri = (phase < 0.5f ? phase : 1.0f - phase) * 4.0f - 1.3f;
        return 2.0f * tri / (1.0f + std::fabs (tri));
    }

    void trigger909 (const SnarePrototype& p, float f0, float accent, float decayAdj, float colorAmt, float snapKnob)
    {
        auto& s = s909;
        const float decay   = std::clamp (p.decay909 + decayAdj, 0.0f, 1.0f);
        float fmAmount      = std::clamp (p.fmAmount, 0.0f, 1.0f);
        float snappy        = std::clamp (p.snappy + (snapKnob - 0.5f) * 0.6f, 0.0f, 1.0f);
        const float decayXt = decay * (1.0f + decay * (decay - 1.0f));
        fmAmount *= fmAmount;
        s.fmAmount = fmAmount;
        s.drumDecayK  = 1.0f - 1.0f / (0.015f * sr) * semis (-decayXt * 72.0f - fmAmount * 12.0f + snappy * 7.0f);
        s.snareDecayK = 1.0f - 1.0f / (0.01f * sr)  * semis (-decay * 60.0f - snappy * 7.0f);
        s.fmDecayK    = 1.0f - 1.0f / (0.007f * sr);
        snappy = std::clamp (snappy * 1.1f - 0.05f, 0.0f, 1.0f);
        s.drumLevel  = std::sqrt (1.0f - snappy);
        s.snareLevel = std::sqrt (snappy);
        const float band = std::pow (2.0f, colorAmt * 0.5f);
        s.snareHp.reset();  s.snareHp.setF (std::min (10.0f * f0 * band / sr, 0.49f));
        s.snareLp.reset();  s.snareLp.setFQ (std::min (35.0f * f0 * band / sr, 0.49f), 0.5f + 2.0f * snappy);
        s.drumLp.reset();   s.drumLp.setF (std::min (3.0f * f0 / sr, 0.49f));
        s.f0N = f0 / sr;
        float resetAmt = std::clamp ((0.125f - f0 / 48000.0f) * 8.0f, 0.0f, 1.0f);
        s.resetNoiseAmt = resetAmt * resetAmt * fmAmount;
        s.snareAmp = s.drumAmp = 0.3f + 0.7f * accent;
        s.fm = 1.0f;
        s.phase0 = s.phase1 = 0.0f;
        s.hold = (int) ((0.04f + decay * 0.03f) * sr);
        s.halfRate = false;
        const float tauD = -1.0f / (sr * std::log (std::max (1.0e-9f, s.drumDecayK)));
        const float tauS = -1.0f / (sr * std::log (std::max (1.0e-9f, s.snareDecayK)));
        samplesLeft = (int) std::min (sr * 4.0f, sr * (std::max (tauD * 2.0f, tauS) * 8.0f + 0.1f));
    }

    inline float tick909()
    {
        auto& s = s909;
        s.halfRate = ! s.halfRate;
        s.drumAmp *= (s.drumAmp > 0.03f || s.halfRate) ? s.drumDecayK : 1.0f;   // the long tail
        if (s.hold > 0) --s.hold; else s.snareAmp *= s.snareDecayK;
        s.fm *= s.fmDecayK;

        // The 909's oscillator coupling: the reset lets them intermodulate
        float resetNoise = (s.phase0 > 0.5f ? -1.0f : 1.0f) + (s.phase1 > 0.5f ? -1.0f : 1.0f);
        resetNoise *= s.resetNoiseAmt * 0.025f;
        const float f = s.f0N * (1.0f + s.fmAmount * 4.0f * s.fm);
        s.phase0 += f;
        s.phase1 += f * 1.47f;
        if (s.resetNoiseAmt > 0.1f)
        {
            if (s.phase0 >= 1.0f + resetNoise) s.phase0 = 1.0f - s.phase0;
            if (s.phase1 >= 1.0f + resetNoise) s.phase1 = 1.0f - s.phase1;
        }
        else
        {
            if (s.phase0 >= 1.0f) s.phase0 -= 1.0f;
            if (s.phase1 >= 1.0f) s.phase1 -= 1.0f;
        }
        s.phase0 -= std::floor (s.phase0);
        s.phase1 -= std::floor (s.phase1);
        float drum = -0.1f + distortedSine909 (s.phase0) * 0.60f + distortedSine909 (s.phase1) * 0.25f;
        drum = s.drumLp.lp (drum * s.drumAmp * s.drumLevel);
        float bp, lp;
        s.snareLp.process (white01(), bp, lp);
        float snare = s.snareHp.hp (lp);
        snare = (snare + 0.1f) * (s.snareAmp + s.fm) * s.snareLevel;
        return snare + drum;
    }

    // Per-model loudness trims, set so each model peaks like the original snares
    static constexpr float kTrimStruck = 0.87f;     // measured against the original snares
    static constexpr float kTrim808    = 0.88f;
    static constexpr float kTrim909    = 0.87f;

    // The circuit filter (stmlib Svf: band-pass + low-pass, MIT - see above)
    struct CircuitSvf
    {
        void setFQ (float fNorm, float resonance) noexcept
        {
            fNorm = std::clamp (fNorm, 1.0e-6f, 0.497f);
            g = std::tan (3.14159265f * fNorm);
            r = 1.0f / std::max (0.5f, resonance);
            h = 1.0f / (1.0f + r * g + g * g);
        }
        inline void process (float in, float& bp, float& lp) noexcept
        {
            const float hp = (in - r * s1 - g * s1 - s2) * h;
            bp = g * hp + s1; s1 = g * hp + bp;
            lp = g * bp + s2; s2 = g * bp + lp;
        }
        void reset() noexcept { s1 = s2 = 0.0f; }
        float g = 0.0f, r = 1.0f, h = 1.0f, s1 = 0.0f, s2 = 0.0f;
    };

    struct Struck
    {
        Res   head[kNumBodyModes];
        float w0[kNumBodyModes] {}, r[kNumBodyModes] {}, g[kNumBodyModes] {}, ak[kNumBodyModes] {};
        float invE0 = 0.0f, tension = 0.0f, headNorm = 1.0f;
        int   coefCountdown = 0, strikeLen = 2, strikePos = 0;
        float strikeAmp = 0.0f, strikeNoise = 0.0f;
        float pitchDepth = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f;
        Res   rim;  float rimGain = 0.0f;
        float wireAmp = 0.0f, wireAtt = 0.0f, wireAttK = 0.0f, wireEnv = 0.0f, wireK = 0.0f;
        float wireFastEnv = 0.0f, wireFastK = 0.0f, wireFast = 0.0f, wireBuzz = 0.0f, wireRect = 0.0f, wireHpMix = 0.0f;
        int   wireHold = 0;
        drum::Svf wireSvf, bcSvf, clickSvf;
        float bodyClick = 0.0f, bcEnv = 0.0f, bcK = 0.0f, prevHead = 0.0f;
        float clickAmp = 0.0f, clickEnv = 0.0f, clickK = 0.0f;
        float vcaDrive = 0.0f;
    } st;

    struct A808
    {
        CircuitSvf res[5], noiseFilter;
        float gain[5] {};
        float snappy = 0.0f, exciterLeak = 0.0f, noiseDecayK = 0.0f, noiseEnv = 0.0f;
        int   pulseRemaining = 0;
        float pulse = 0.0f, pulseHeight = 0.0f, pulseLp = 0.0f, kPulseDecay = 0.0f, kPulseLp = 0.0f;
    } a808;

    struct S909
    {
        OnePole    snareHp, drumLp;
        CircuitSvf snareLp;
        float f0N = 0.0f, fmAmount = 0.0f, fm = 0.0f, resetNoiseAmt = 0.0f;
        float drumDecayK = 0.0f, snareDecayK = 0.0f, fmDecayK = 0.0f, drumLevel = 1.0f, snareLevel = 0.0f;
        float drumAmp = 0.0f, snareAmp = 0.0f, phase0 = 0.0f, phase1 = 0.0f;
        int   hold = 0;
        bool  halfRate = false;
    } s909;

    int   model = 0;
    float dcX1 = 0.0f, dcY1 = 0.0f, dcR = 0.999f;
    int   quietRun = 0;

    drum::Rng rng;
    float sr = 48000.0f, twoPiOverSr = 0.0f;

    // Body
    float bodyFreq [kNumBodyModes] {};
    float bodyAmp  [kNumBodyModes] {};
    float bodyCoef [kNumBodyModes] {};
    float bodyEnv  [kNumBodyModes] {};
    float bodyPhase[kNumBodyModes] {};
    float pitchDepth = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f;

    // Wires
    float wireAmp = 0.0f, wireEnv = 0.0f, wireCoef = 0.0f;
    float wireAtt = 0.0f, wireAttCoef = 0.0f, wireHpMix = 0.0f;
    float wireFastEnv = 0.0f, wireFastCoef = 0.0f, wireFast = 0.0f, wireBuzz = 0.0f;
    float onsetGain = 1.0f, onsetStep = 1.0f;
    drum::Svf wireSvf;

    // Click
    float clickAmp = 0.0f, clickEnv = 0.0f, clickCoef = 0.0f;
    drum::Svf clickSvf;

    // Rim
    float rimFreq = 0.0f, rimAmp = 0.0f, rimEnv = 0.0f, rimCoef = 0.0f, rimPhase = 0.0f;

    // Output
    float drivePre = 1.0f, driveMakeup = 1.0f, outGain = 1.0f;

    // Lifetime / steal
    bool  active = false;
    int   samplesLeft = 0;
    bool  killing = false;
    float killGain = 1.0f, killStep = 0.0f;
};

} // namespace snare
