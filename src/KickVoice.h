#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include "KickPrototypes.h"

//==============================================================================
//  KickVoice.h
//
//  One kick voice with the fixed Membrane-family topology:
//      Tone  — 8 exponentially decaying sine modes with a shared pitch envelope
//      Click — short noise burst through a bandpass (attack character)
//      Noise — filtered noise (LP/BP/HP morphable) with its own envelope
//      Drive — tanh saturation with makeup
//
//  Everything is latched at trigger() — per-hit latch, allocation-free render.
//  All humanize randomisation happens here at note-on, never per sample.
//==============================================================================

namespace kick
{

//==============================================================================
//  User macro set (the ~12-control surface), already read from APVTS.
//  All values are the raw knob positions 0..1 unless noted.
//==============================================================================
struct Macros
{
    float tuneSemis  = 0.0f;   // -12 .. +12
    float decay      = 0.5f;   // 0..1, 0.5 = neutral
    float damp       = 0.5f;   // 0..1, 0.5 = neutral
    float snap       = 0.5f;   // 0..1, 0.5 = neutral (1x click)
    float color      = 0.5f;   // 0..1, 0.5 = neutral
    float drive      = 0.15f;  // 0..1, ADDS to the prototype drive
    float humanize   = 0.25f;  // 0..1
    float velSens    = 0.6f;   // 0..1
};

//==============================================================================
//  TPT state-variable filter (Zavalishin). One instance per filtered layer.
//==============================================================================
struct Svf
{
    void set (float fc, float q, float sampleRate)
    {
        fc = std::clamp (fc, 20.0f, sampleRate * 0.45f);
        const float g = std::tan (3.14159265f * fc / sampleRate);
        k  = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() { ic1 = ic2 = 0.0f; }

    inline void process (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }

    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};


//==============================================================================
//  CircuitSvf - the resonant filter the circuit models ring.  Port of stmlib's
//  Svf (Mutable Instruments, MIT - see THE KICK MODELS below): band-pass and
//  low-pass from one topology-preserving update, retunable every sample.
//==============================================================================
struct CircuitSvf
{
    void setFQ (float fNorm, float resonance) noexcept
    {
        fNorm = std::clamp (fNorm, 1.0e-6f, 0.497f);
        // tan(pi f) - retuned every sample, so the kick range (x < 0.1, i.e.
        // up to ~1.6 kHz at 48 kHz) uses its series, exact to 1e-6; above
        // that std::tan.  Plaits does the same with its FREQUENCY_DIRTY.
        const float x = 3.14159265f * fNorm;
        g = (x < 0.1f) ? x * (1.0f + x * x * (0.33333333f + x * x * 0.13333333f))
                       : std::tan (x);
        r = 1.0f / std::max (0.5f, resonance);
        h = 1.0f / (1.0f + r * g + g * g);
    }
    inline void process (float in, float& bp, float& lp) noexcept
    {
        const float hp = (in - r * s1 - g * s1 - s2) * h;
        bp = g * hp + s1;
        s1 = g * hp + bp;
        lp = g * bp + s2;
        s2 = g * bp + lp;
    }
    void reset() noexcept { s1 = s2 = 0.0f; }
    float g = 0.0f, r = 1.0f, h = 1.0f, s1 = 0.0f, s2 = 0.0f;
};

//==============================================================================
class KickVoice
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) std::max (8000.0, sampleRate);
        twoPiOverSr = 6.28318530718f / sr;
        killStep = 1.0f / (0.003f * sr);          // 3 ms steal fade
        reset();
    }

    void reset()
    {
        active = false;
        killGain = 1.0f;
        killing = false;
        clickSvf.reset();
        noiseSvf.reset();
    }

    bool isActive() const noexcept { return active; }

    // Each voice slot needs its own stream: with one shared seed, every slot's
    // first hit would scatter identically and the per-hit variation would vanish.
    void setSeed (uint32_t s) noexcept { rngState = (s != 0u) ? s : 0x9E3779B9u; }

    // Fast fade-out used when a new hit steals this voice (kick cuts itself)
    void beginKill() noexcept { if (active) killing = true; }

    //==========================================================================
    //  Latch a hit. proto is the already-morphed prototype; macros are the
    //  live knob values; velocity is MIDI 1..127.
    //==========================================================================
    //==========================================================================
    //  THE KICK MODELS (2026-09 rebuild).  Every kick picks one via
    //  KickPrototype::model; toms and hand drums stay on 0 until theirs.
    //
    //    0  MODAL      the original engine - independent enveloped sines
    //    1  STRUCK     resonators rung by a strike, like a real head:
    //                  - 8 two-pole resonators excited by a raised-cosine
    //                    strike pulse whose width is the beater's hardness
    //                    (a soft beater's longer contact rolls off the top)
    //                  - pitch follows the drum's ENERGY (tension modulation:
    //                    the head's tension - its pitch - rises with how hard
    //                    it vibrates), so a hard hit glides more, a soft one
    //                    barely, and the glide always follows the decay
    //                  - a click made from the body's own motion (its band-
    //                    passed derivative), phase-locked to the drum
    //                  - a level-dependent VCA: saturates while loud, cleans
    //                    up as it fades, with a sub "thump" riding the level
    //    2  808        port of Mutable Instruments Plaits' AnalogBassDrum:
    //                  the TR-808 circuit - a diode-shaped trigger pulse pings
    //                  a resonant filter; a 6 ms FM pulse lifts the attack;
    //                  the resonator's own level lifts the pitch ("self FM")
    //    3  909        port of Plaits' SyntheticBassDrum: a triangle shaped
    //                  into a sine, phase reset and held 1.3 ms at the hit, a
    //                  fast FM sweep, a transistor VCA and a filtered click
    //
    //  Plaits drum code: Copyright 2016 Emilie Gillet, MIT licence -
    //  "Permission is hereby granted, free of charge, to any person obtaining
    //  a copy of this software ... to deal in the Software without
    //  restriction, including without limitation the rights to use, copy,
    //  modify, merge, publish, distribute, sublicense, and/or sell copies of
    //  the Software ...".  The notice travels with the ported code here.
    //  Per-sample constants written for 48 kHz are re-derived for the actual
    //  rate (rate48), so the circuits sound the same at 44.1 or 96 kHz.
    //==========================================================================
    void trigger (const KickPrototype& proto, const Macros& m, int velocity)
    {
        model = (int) std::lround (std::clamp (proto.model, 0.0f, 4.0f));
        if (model == 0) { triggerModal (proto, m, velocity); return; }

        const float vel = std::clamp ((float) velocity, 1.0f, 127.0f) / 127.0f;
        const float h   = std::clamp (m.humanize, 0.0f, 1.0f);
        const float humanTuneCents = bipolar() * 25.0f * h;
        const float humanDecay     = bipolar() * 0.08f * h;
        const float humanClickMul  = 1.0f + bipolar() * 0.25f * h;
        const float humanLevelDb   = bipolar() * 1.2f  * h;

        const float tuneMul  = std::pow (2.0f, (m.tuneSemis + humanTuneCents * 0.01f) / 12.0f);
        const float f0       = std::clamp (proto.f0 * tuneMul, 15.0f, 1500.0f);
        const float decayMul = std::pow (4.0f, (m.decay - 0.5f) * 2.0f) * (1.0f + humanDecay);
        const float decayAdj = (m.decay - 0.5f) * 0.6f + humanDecay;
        const float dampAmt  = (m.damp  - 0.5f) * 2.0f;
        const float colorAmt = (m.color - 0.5f) * 2.0f;
        const float snapMul  = std::clamp (m.snap, 0.0f, 1.0f) * 2.0f * humanClickMul;
        const float sens     = std::clamp (m.velSens, 0.0f, 1.0f);
        const float accent   = lerp (1.0f, vel, sens);
        const float velLevel = lerp (1.0f, std::pow (vel, 1.2f), sens);

        const float d = std::clamp (proto.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 9.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        asymBias    = std::clamp (proto.asym, 0.0f, 1.0f) * 0.3f;
        asymOffset  = std::tanh (asymBias * drivePre);

        static constexpr float kTrim[5] = { 1.0f, kTrimStruck, kTrim808, kTrim909, kTrimSynth };
        outGain = proto.level * kTrim[model] * ((model == 1 || model == 4) ? velLevel : 1.0f)
                * std::pow (10.0f, humanLevelDb / 20.0f);

        dcX1 = dcY1 = 0.0f;
        dc2X1 = dc2Y1 = 0.0f;
        dcR  = 1.0f - 6.28318530718f * 8.0f / sr;      // 8 Hz: the thump stays a transient
        quietRun = 0;

        switch (model)
        {
            case 1:  triggerStruck (proto, f0, vel, accent, decayMul, dampAmt, colorAmt, snapMul, sens); break;
            case 2:  trigger808 (proto, f0, accent, decayAdj - dampAmt * 0.2f, colorAmt, snapMul);      break;
            case 4:  triggerSynth (proto, f0, accent, decayMul, dampAmt, colorAmt, snapMul);          break;
            default: trigger909 (proto, f0, accent, decayAdj - dampAmt * 0.2f, colorAmt, snapMul);      break;
        }
        killGain = 1.0f;
        killing  = false;
        active   = true;
    }

    void render (float* left, float* right, int numSamples)
    {
        if (! active)
            return;
        if (model == 0) { renderModal (left, right, numSamples); return; }

        for (int i = 0; i < numSamples; ++i)
        {
            float s = (model == 2) ? tick808() : (model == 3) ? tick909() : (model == 4) ? tickSynth() : tickStruck();

            const float y = s - dcX1 + dcR * dcY1;         // DC blocker
            dcX1 = s;
            dcY1 = y;
            s = (std::tanh ((y + asymBias) * drivePre) - asymOffset) * driveMakeup;
            if (model == 4)                                  // SYNTH: no DC after the uneven drive
            {
                const float z = s - dc2X1 + dcR * dc2Y1;
                dc2X1 = s;
                dc2Y1 = z;
                s = z;
            }
            s *= outGain;

            if (killing)
            {
                killGain -= killStep;
                if (killGain <= 0.0f) { active = false; return; }
            }
            s *= killGain;
            left [i] += s;
            right[i] += s;

            // A resonator never quite reaches zero: stop once it has been
            // inaudible (below -100 dBFS) for 50 ms.
            quietRun = (std::fabs (s) < 1.0e-5f) ? quietRun + 1 : 0;
            if (--samplesLeft <= 0 || quietRun > (int) (0.05f * sr)) { active = false; return; }
        }
    }

    // MODEL 0 - the original modal engine (toms and hand drums still use it)
    void triggerModal (const KickPrototype& proto, const Macros& m, int velocity)
    {
        const float vel = std::clamp ((float) velocity, 1.0f, 127.0f) / 127.0f;
        const float h   = std::clamp (m.humanize, 0.0f, 1.0f);

        // ---- Humanize: per-hit randoms, latched now --------------------------
        const float humanTuneCents = bipolar() * 25.0f * h;
        const float humanDecayMul  = 1.0f + bipolar() * 0.10f * h;
        const float humanClickMul  = 1.0f + bipolar() * 0.25f * h;
        const float humanLevelDb   = bipolar() * 1.2f  * h;

        // ---- Tune ------------------------------------------------------------
        const float tuneMul = std::pow (2.0f, (m.tuneSemis + humanTuneCents * 0.01f) / 12.0f);
        // Ceiling raised from 400 Hz so the same membrane plays bongos and
        // timbales (EDM KIT percussion); kicks and toms never get near it.
        const float f0 = std::clamp (proto.f0 * tuneMul, 15.0f, 1500.0f);

        // ---- Decay macro: 0..1 -> 0.25x..4x, 0.5 = 1x ------------------------
        const float decayMul = std::pow (4.0f, (m.decay - 0.5f) * 2.0f) * humanDecayMul;

        // ---- Damp macro: decay tilt across the mode ladder -------------------
        // damp = 1  -> highest mode decays 4x faster (darker over time)
        // damp = 0  -> highest mode rings 4x longer (more open)
        const float dampAmt = (m.damp - 0.5f) * 2.0f;   // -1..+1

        // ---- Color macro: mode-gain tilt + noise cutoff shift ----------------
        const float colorAmt = (m.color - 0.5f) * 2.0f; // -1..+1

        // ---- Velocity response ----------------------------------------------
        const float sens = std::clamp (m.velSens, 0.0f, 1.0f);
        const float velLevel = lerp (1.0f, std::pow (vel, 1.2f), sens);
        const float velClick = lerp (1.0f, 0.25f + vel * 1.5f, sens);
        const float velPitch = lerp (1.0f, 0.55f + vel * 0.8f, sens);

        // ---- Latch the modal bank -------------------------------------------
        for (int k = 0; k < kNumModes; ++k)
        {
            const float tilt = (kNumModes > 1) ? (float) k / (float) (kNumModes - 1) : 0.0f;

            modeFreq[k] = f0 * proto.modeRatio[k];

            float g = proto.modeGain[k];
            // Color: +/-9 dB tilt on the upper modes
            g *= std::pow (10.0f, (colorAmt * 9.0f * tilt) / 20.0f);
            modeAmp[k] = g;

            float tau = proto.modeTau[k] * decayMul;
            tau *= std::pow (2.0f, -dampAmt * 2.0f * tilt);
            tau = std::clamp (tau, 0.004f, 8.0f);
            modeCoef[k] = std::exp (-1.0f / (tau * sr));
            modeEnv [k] = 1.0f;

            // Two-stage decay: a share of each mode drops fast (the energy a
            // struck head radiates at once), the rest rings on.
            modeFastCoef[k] = std::exp (-1.0f / (std::max (0.001f, tau * std::clamp (proto.fastRatio, 0.02f, 1.0f)) * sr));
            modeFastEnv [k] = 1.0f;

            // Struck, not faded in: every mode starts near its peak (a struck
            // head radiates velocity), the upper ones jittered and detuned a
            // little on each hit so no two hits are the same waveform.
            const float onsetPhase = 1.5707963f * std::clamp (proto.onset, 0.0f, 1.0f);
            modePhase[k] = onsetPhase + (k == 0 ? 0.0f : bipolar() * 0.5f);
            if (k > 0)
                modeFreq[k] *= 1.0f + std::clamp (proto.modeScatter, 0.0f, 0.05f) * bipolar();
        }
        fastShare = std::clamp (proto.fastShare, 0.0f, 0.9f);
        onsetStep = 1.0f / std::max (1.0f, 0.0003f * sr);    // 0.3 ms edge: a hit, not a digital click
        onsetGain = 0.0f;

        // ---- Pitch envelope --------------------------------------------------
        const float pRatio = 1.0f + (proto.pitchRatio - 1.0f) * velPitch;
        pitchDepth = std::max (0.0f, pRatio - 1.0f);
        pitchEnv   = 1.0f;
        {
            const float ptau = std::clamp (proto.pitchTau, 0.002f, 0.5f);
            pitchCoef = std::exp (-1.0f / (ptau * sr));
        }

        // Beater / stick spike: a very short extra pitch jump at the hit
        spikeDepth = std::max (0.0f, proto.spike) * velPitch;
        spikeEnv   = 1.0f;
        spikeCoef  = std::exp (-1.0f / (std::clamp (proto.spikeTau, 0.0005f, 0.02f) * sr));

        // ---- Click layer -----------------------------------------------------
        clickAmp = proto.clickLevel * (m.snap * 2.0f) * humanClickMul * velClick;
        {
            const float ctau = std::clamp (proto.clickTau * decayMul, 0.0008f, 0.06f);
            clickCoef = std::exp (-1.0f / (ctau * sr));
        }
        clickEnv = 1.0f;
        clickSvf.reset();
        clickSvf.set (proto.clickFreq, std::clamp (proto.clickQ, 0.3f, 3.0f), sr);

        // ---- Noise layer -----------------------------------------------------
        noiseAmp = proto.noiseLevel;
        {
            const float ntau = std::clamp (proto.noiseTau * decayMul, 0.004f, 2.0f);
            noiseCoef = std::exp (-1.0f / (ntau * sr));
        }
        noiseEnv = 1.0f;
        noiseAttCoef = std::exp (-1.0f / (0.0012f * sr));   // ~1.2 ms attack
        noiseAtt = 0.0f;
        noiseMix = std::clamp (proto.noiseMode, 0.0f, 1.0f);
        noiseSvf.reset();
        {
            const float ncut = proto.noiseCutoff * std::pow (4.0f, colorAmt);
            noiseSvf.set (ncut, 0.9f, sr);
        }

        // ---- Drive + output --------------------------------------------------
        const float d = std::clamp (proto.drive + m.drive, 0.0f, 1.0f);
        drivePre    = 1.0f + 9.0f * d;
        driveMakeup = 1.0f / std::sqrt (drivePre);
        asymBias    = std::clamp (proto.asym, 0.0f, 1.0f) * 0.3f;
        asymOffset  = std::tanh (asymBias * drivePre);

        outGain = proto.level * velLevel
                * std::pow (10.0f, humanLevelDb / 20.0f);

        // ---- Lifetime --------------------------------------------------------
        maxTau = 0.0f;
        for (int k = 0; k < kNumModes; ++k)
            maxTau = std::max (maxTau, -1.0f / (sr * std::log (std::max (1.0e-6f, modeCoef[k]))));
        maxTau = std::max ({ maxTau,
                             -1.0f / (sr * std::log (std::max (1.0e-6f, clickCoef))),
                             -1.0f / (sr * std::log (std::max (1.0e-6f, noiseCoef))) });
        samplesLeft = (int) std::min (sr * 8.0f, sr * maxTau * 7.0f + sr * 0.05f);

        killGain = 1.0f;
        killing  = false;
        active   = true;
    }

    //==========================================================================
    //  Additive render into an interleaved-free stereo pair.
    //==========================================================================
    void renderModal (float* left, float* right, int numSamples)
    {
        if (! active)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // Pitch envelope: multiplier on the whole modal bank
            pitchEnv *= pitchCoef;
            spikeEnv *= spikeCoef;
            const float freqMul = 1.0f + pitchDepth * pitchEnv + spikeDepth * spikeEnv;

            // Tone — modal bank
            float tone = 0.0f;
            for (int k = 0; k < kNumModes; ++k)
            {
                modePhase[k] += twoPiOverSr * modeFreq[k] * freqMul;
                if (modePhase[k] > 6.28318530718f)
                    modePhase[k] -= 6.28318530718f;

                modeEnv    [k] *= modeCoef    [k];
                modeFastEnv[k] *= modeFastCoef[k];
                const float env = (1.0f - fastShare) * modeEnv[k] + fastShare * modeFastEnv[k];
                tone += std::sin (modePhase[k]) * modeAmp[k] * env;
            }
            onsetGain = std::min (1.0f, onsetGain + onsetStep);
            tone *= onsetGain;

            // Click — noise burst through bandpass
            clickEnv *= clickCoef;
            float lp, bp, hp;
            clickSvf.process (white(), lp, bp, hp);
            const float click = bp * clickEnv * clickAmp;

            // Noise — filtered, own envelope with short attack
            noiseEnv *= noiseCoef;
            noiseAtt = 1.0f + (noiseAtt - 1.0f) * noiseAttCoef;
            noiseSvf.process (white(), lp, bp, hp);
            const float nMix = (noiseMix < 0.5f)
                                 ? lerp (lp, bp, noiseMix * 2.0f)
                                 : lerp (bp, hp, (noiseMix - 0.5f) * 2.0f);
            const float noise = nMix * noiseEnv * noiseAtt * noiseAmp;

            // Sum, drive, output
            float s = (tone + click + noise);
            s = (std::tanh ((s + asymBias) * drivePre) - asymOffset) * driveMakeup;
            s *= outGain;

            // Steal fade
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
    //==========================================================================
    //  MODEL 1 - STRUCK RESONATORS
    //==========================================================================
    void triggerStruck (const KickPrototype& p, float f0, float vel, float accent, float decayMul,
                        float dampAmt, float colorAmt, float snapMul, float sens)
    {
        auto& s = st;
        float e0 = 0.0f;
        for (int k = 0; k < kNumModes; ++k)
        {
            const float tilt = (float) k / (float) (kNumModes - 1);
            float g = p.modeGain[k] * std::pow (10.0f, (colorAmt * 9.0f * tilt) / 20.0f);
            float tau = p.modeTau[k] * decayMul * std::pow (2.0f, -dampAmt * 2.0f * tilt);
            tau = std::clamp (tau, 0.004f, 8.0f);
            float f = f0 * p.modeRatio[k];
            if (k > 0)
                f *= 1.0f + std::clamp (p.modeScatter, 0.0f, 0.05f) * bipolar();
            s.w0[k] = std::min (6.28318530718f * f / sr, 2.9f);
            s.r [k] = std::exp (-1.0f / (tau * sr));
            s.g [k] = g;
            s.y1[k] = s.y2[k] = 0.0f;
            s.ak[k] = g * accent;                      // decaying amplitude, for the energy
            e0 += g * g;
        }
        s.invE0   = e0 > 1.0e-9f ? 1.0f / e0 : 0.0f;
        s.tension = std::max (0.0f, p.tension);
        s.coefCountdown = 0;

        // The strike: a raised-cosine force pulse.  Its width is the contact
        // time - hard beaters and hard hits touch briefly and ring the top.
        const float hard  = std::clamp (p.hardness + (vel - 0.75f) * 0.3f * sens, 0.0f, 1.0f);
        const float width = lerp (0.0040f, 0.00025f, hard);
        s.strikeLen   = std::max (2, (int) (width * sr));
        s.strikePos   = 0;
        s.strikeAmp   = accent * 2.0f / (float) s.strikeLen;          // unit area
        s.strikeNoise = 0.35f * hard;

        // Pitch envelope + beater spike (the original fields, for hybrids)
        const float velPitch = lerp (1.0f, 0.55f + vel * 0.8f, sens);
        pitchDepth = std::max (0.0f, (p.pitchRatio - 1.0f) * velPitch);
        pitchEnv   = 1.0f;
        pitchCoef  = std::exp (-1.0f / (std::clamp (p.pitchTau, 0.002f, 0.5f) * sr));
        spikeDepth = std::max (0.0f, p.spike) * velPitch;
        spikeEnv   = 1.0f;
        spikeCoef  = std::exp (-1.0f / (std::clamp (p.spikeTau, 0.0005f, 0.02f) * sr));

        // Body click: the drum's own motion, band-passed
        s.bodyClick = std::max (0.0f, p.bodyClick) * snapMul;
        s.bcEnv  = 1.0f;
        s.bcCoef = std::exp (-1.0f / (std::clamp (p.clickTau, 0.0008f, 0.03f) * sr));
        s.bcSvf.reset();
        s.bcSvf.set (std::clamp (p.clickFreq * 0.6f, 400.0f, 8000.0f), 0.7f, sr);
        s.prevBody = 0.0f;

        // Beater noise + noise layer (the original click and noise paths)
        const float velClick = lerp (1.0f, 0.25f + vel * 1.5f, sens);
        clickAmp = p.clickLevel * snapMul * velClick;
        clickCoef = std::exp (-1.0f / (std::clamp (p.clickTau * decayMul, 0.0008f, 0.06f) * sr));
        clickEnv = 1.0f;
        clickSvf.reset();
        clickSvf.set (p.clickFreq, std::clamp (p.clickQ, 0.3f, 3.0f), sr);
        noiseAmp  = p.noiseLevel;
        noiseCoef = std::exp (-1.0f / (std::clamp (p.noiseTau * decayMul, 0.004f, 2.0f) * sr));
        noiseEnv  = 1.0f;
        noiseAttCoef = std::exp (-1.0f / (0.0012f * sr));
        noiseAtt  = 0.0f;
        noiseMix  = std::clamp (p.noiseMode, 0.0f, 1.0f);
        noiseSvf.reset();
        noiseSvf.set (p.noiseCutoff * std::pow (4.0f, colorAmt), 0.9f, sr);

        s.vcaDrive = std::clamp (p.vcaDrive, 0.0f, 1.0f);

        // Two-stage decay: a real head loses a share of its level in a fast
        // first drop, then rings on (0 = one smooth decay, as fitted kicks use)
        s.fastShare = std::clamp (p.strikeFast, 0.0f, 0.95f);
        s.fastEnv   = 1.0f;
        s.fastK     = std::exp (-1.0f / (std::max (0.002f, p.strikeFastTau) * sr));

        float maxT = 0.0f;
        for (int k = 0; k < kNumModes; ++k)
            if (s.g[k] > 1.0e-4f)
                maxT = std::max (maxT, -1.0f / (sr * std::log (std::max (1.0e-6f, s.r[k]))));
        maxT = std::max ({ maxT, -1.0f / (sr * std::log (std::max (1.0e-6f, noiseCoef))), 0.05f });
        samplesLeft = (int) std::min (sr * 8.0f, sr * (maxT * 7.0f + 0.05f));
    }

    inline float tickStruck()
    {
        auto& s = st;
        float x = 0.0f;
        if (s.strikePos < s.strikeLen)
        {
            const float ph = ((float) s.strikePos + 0.5f) / (float) s.strikeLen;
            x = s.strikeAmp * (0.5f - 0.5f * std::cos (6.28318530718f * ph)) * (1.0f + s.strikeNoise * white());
            ++s.strikePos;
        }

        // Energy of the head -> its tension -> its pitch
        float e = 0.0f;
        for (int k = 0; k < kNumModes; ++k)
        {
            s.ak[k] *= s.r[k];
            e += s.ak[k] * s.ak[k];
        }
        const float eNorm = e * s.invE0;
        pitchEnv *= pitchCoef;
        spikeEnv *= spikeCoef;
        const float fm = (1.0f + s.tension * eNorm) * (1.0f + pitchDepth * pitchEnv + spikeDepth * spikeEnv);

        if (--s.coefCountdown <= 0)                     // retune every 4 samples
        {
            s.coefCountdown = 4;
            for (int k = 0; k < kNumModes; ++k)
            {
                const float w = std::min (s.w0[k] * fm, 3.0f);
                s.a1[k] = 2.0f * s.r[k] * std::cos (w);
                s.a2[k] = s.r[k] * s.r[k];
                s.b0[k] = std::sin (w);
            }
        }

        float body = 0.0f;
        for (int k = 0; k < kNumModes; ++k)
        {
            const float y = s.b0[k] * x + s.a1[k] * s.y1[k] - s.a2[k] * s.y2[k];
            s.y2[k] = s.y1[k];
            s.y1[k] = y;
            body += y * s.g[k];
        }

        // Level-dependent VCA: saturates while loud, clean as it fades; the
        // thump rides the level (the DC blocker turns it into a sub transient)
        const float envK = std::sqrt (std::max (0.0f, eNorm));
        const float pre  = body * (1.0f + s.vcaDrive * 2.5f * envK);
        s.fastEnv *= s.fastK;
        const float vca  = (pre / (1.0f + s.vcaDrive * 0.8f * std::fabs (pre)) + s.vcaDrive * 0.3f * envK)
                         * ((1.0f - s.fastShare) + s.fastShare * s.fastEnv);

        // Click from the body's own motion
        float lp, bp, hp;
        const float dBody = body - s.prevBody;
        s.prevBody = body;
        s.bcEnv *= s.bcCoef;
        s.bcSvf.process (dBody, lp, bp, hp);
        const float bodyClick = bp * s.bcEnv * s.bodyClick * 8.0f;

        clickEnv *= clickCoef;
        clickSvf.process (white(), lp, bp, hp);
        const float beater = bp * clickEnv * clickAmp;

        noiseEnv *= noiseCoef;
        noiseAtt = 1.0f + (noiseAtt - 1.0f) * noiseAttCoef;
        noiseSvf.process (white(), lp, bp, hp);
        const float nMix = (noiseMix < 0.5f) ? lerp (lp, bp, noiseMix * 2.0f)
                                             : lerp (bp, hp, (noiseMix - 0.5f) * 2.0f);
        return vca + bodyClick + beater + nMix * noiseEnv * noiseAtt * noiseAmp;
    }

    //==========================================================================
    //  MODEL 2 - 808 CIRCUIT (Plaits AnalogBassDrum port, see above)
    //==========================================================================
    static inline float diode (float x) noexcept
    {
        if (x >= 0.0f) return x;
        x *= 2.0f;
        return 0.7f * x / (1.0f + std::fabs (x));
    }

    static inline float semis (float st) noexcept { return std::pow (2.0f, st / 12.0f); }

    // A per-sample smoothing coefficient written for 48 kHz, for this rate
    float rate48 (float c48) const noexcept { return 1.0f - std::pow (1.0f - c48, 48000.0f / sr); }

    void trigger808 (const KickPrototype& p, float f0, float accent, float decayAdj, float colorAmt, float snapMul)
    {
        auto& s = a808;
        s.f0       = f0;
        s.decay    = std::clamp (p.decay808 + decayAdj, 0.0f, 1.0f);
        const float tone = std::clamp (p.tone808 + colorAmt * 0.3f, 0.0f, 1.0f);
        s.attackFm = std::clamp (p.attackFm * snapMul, 0.0f, 2.0f);
        s.selfFm   = std::clamp (p.selfFm, 0.0f, 2.0f);
        s.pulseRemaining   = std::max (1, (int) (1.0e-3f * sr));
        s.fmPulseRemaining = std::max (1, (int) (6.0e-3f * sr));
        s.pulseHeight = 3.0f + 7.0f * accent;
        s.pulse = s.pulseLp = s.fmPulseLp = s.retrigPulse = s.lpOut = s.toneLp = 0.0f;
        s.res.reset();
        s.kPulseDecay  = 1.0f - 1.0f / (0.2e-3f * sr);
        s.kPulseFilter = 1.0f / (0.1e-3f * sr);
        s.kRetrigDecay = 1.0f - 1.0f / (0.05f * sr);
        s.q        = 1500.0f * semis (s.decay * 80.0f);   // Q = 1 + q * f(48 kHz-normalised)
        s.scale    = 0.001f / (f0 / 48000.0f);
        s.toneF    = std::min (4.0f * (f0 / sr) * semis (tone * 108.0f), 1.0f);
        s.exciterLeak = 0.08f * (tone + 0.25f);
        const float tau = (1.0f + s.q * f0 / 48000.0f) / (3.14159265f * f0);
        samplesLeft = (int) std::min (sr * 8.0f, sr * (tau * 8.0f + 0.05f));
    }

    inline float tick808()
    {
        auto& s = a808;
        float pulse;
        if (s.pulseRemaining > 0)                       // Q39 / Q40
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
        s.pulseLp += (pulse - s.pulseLp) * s.kPulseFilter;    // C40 / R163 / R162 / D83
        pulse = diode ((pulse - s.pulseLp) + pulse * 0.044f);

        float fmPulse = 0.0f;                           // Q41 / Q42
        if (s.fmPulseRemaining > 0)
        {
            --s.fmPulseRemaining;
            fmPulse = 1.0f;
            s.retrigPulse = (s.fmPulseRemaining > 0) ? 0.0f : -0.8f;   // C39 / C52
        }
        else
            s.retrigPulse *= s.kRetrigDecay;                           // C39 / R161
        s.fmPulseLp += (fmPulse - s.fmPulseLp) * s.kPulseFilter;

        const float punch    = 0.7f + diode (10.0f * s.lpOut - 1.0f); // Q43 and R170 leakage
        const float attackFm = s.fmPulseLp * 1.7f * s.attackFm;       // Q43 / R165
        const float selfFm   = punch * 0.08f * s.selfFm;
        const float f = std::clamp ((s.f0 / sr) * (1.0f + attackFm + selfFm), 0.0f, 0.4f);

        float bp, lp;
        s.res.setFQ (f, 1.0f + s.q * f * (sr / 48000.0f));
        s.res.process ((pulse - s.retrigPulse * 0.2f) * s.scale, bp, lp);
        s.lpOut = lp;
        s.toneLp += (pulse * s.exciterLeak + bp - s.toneLp) * s.toneF;
        return s.toneLp;
    }

    //==========================================================================
    //  MODEL 3 - 909 CIRCUIT (Plaits SyntheticBassDrum port, see above)
    //==========================================================================
    static inline float distortedSine (float phase, float phaseNoise, float dirt) noexcept
    {
        phase += phaseNoise * dirt;
        phase -= std::floor (phase);
        const float tri   = (phase < 0.5f ? phase : 1.0f - phase) * 4.0f - 1.0f;
        const float sine  = 2.0f * tri / (1.0f + std::fabs (tri));
        const float clean = std::sin (6.28318530718f * (phase + 0.75f));
        return sine + (1.0f - dirt) * (clean - sine);
    }

    static inline float transistorVca (float s, float gain) noexcept
    {
        s = (s - 0.6f) * gain;
        return 3.0f * s / (2.0f + std::fabs (s)) + gain * 0.3f;
    }

    void trigger909 (const KickPrototype& p, float f0, float accent, float decayAdj, float colorAmt, float snapMul)
    {
        auto& s = s909;
        s.f0N = f0 / sr;
        float decay = std::clamp (p.decay909 + decayAdj, 0.0f, 1.0f);
        decay *= decay;
        float fmDecay = std::clamp (p.fmDecay, 0.0f, 1.0f);
        fmDecay *= fmDecay;
        const float tone = std::clamp (p.tone909 + colorAmt * 0.3f, 0.0f, 1.0f);
        s.dirt      = std::clamp (p.dirt, 0.0f, 1.0f) * std::max (1.0f - 8.0f * (f0 / 48000.0f), 0.0f);
        s.fmAmount  = std::clamp (p.fmAmount, 0.0f, 2.0f);
        s.fmDecayK    = 1.0f - 1.0f / (0.008f * (1.0f + fmDecay * 4.0f) * sr);
        s.bodyDecayK  = 1.0f - 1.0f / (0.02f * sr) * semis (-decay * 60.0f);
        s.transDecayK = 1.0f - 1.0f / (0.005f * sr);
        s.toneF          = std::min (4.0f * s.f0N * semis (tone * 108.0f), 1.0f);
        s.transientLevel = tone * snapMul;
        s.fm = 1.0f;
        s.bodyEnv = s.transEnv = 0.3f + 0.7f * accent;
        s.bodyPulse = (int) (sr * 0.001f);
        s.fmPulse   = (int) (sr * 0.0013f);
        s.phase = 0.0f; s.phaseNoise = 0.0f;
        s.fmLp = s.bodyEnvLp = s.transEnvLp = s.toneLp = 0.0f;
        s.clickLp = s.clickHp = 0.0f; s.nLp = s.nHp = 0.0f;
        s.clickSvf.reset();
        s.clickSvf.setFQ (5000.0f / sr, 2.0f);
        s.kSlopeUp = rate48 (0.5f);  s.kSlopeDown = rate48 (0.1f);  s.kClickHp = rate48 (0.04f);
        s.kNoiseLp = rate48 (0.05f); s.kNoiseHp   = rate48 (0.005f); s.kPhaseNoise = rate48 (0.002f);
        s.kEnvLp   = rate48 (0.1f);
        const float tauSamples = -1.0f / std::log (std::max (1.0e-9f, s.bodyDecayK));
        samplesLeft = (int) std::min (sr * 8.0f, tauSamples * 9.0f + sr * 0.05f);
    }

    inline float tick909()
    {
        auto& s = s909;
        s.phaseNoise += ((white01() - 0.5f) - s.phaseNoise) * s.kPhaseNoise;
        if (s.fmPulse > 0) { --s.fmPulse; s.phase = 0.25f; }
        else
        {
            s.fm *= s.fmDecayK;
            const float fm = 1.0f + s.fmAmount * 3.5f * s.fmLp;
            s.phase += std::min (s.f0N * fm, 0.5f);
            if (s.phase >= 1.0f) s.phase -= 1.0f;
        }
        if (s.bodyPulse > 0) --s.bodyPulse;
        else { s.bodyEnv *= s.bodyDecayK; s.transEnv *= s.transDecayK; }
        s.bodyEnvLp  += (s.bodyEnv  - s.bodyEnvLp)  * s.kEnvLp;
        s.transEnvLp += (s.transEnv - s.transEnvLp) * s.kEnvLp;
        s.fmLp       += (s.fm       - s.fmLp)       * s.kEnvLp;

        const float body = distortedSine (s.phase, s.phaseNoise, s.dirt);

        const float stepIn = (s.bodyPulse > 0) ? 0.0f : 1.0f;          // the click
        const float err = stepIn - s.clickLp;
        s.clickLp += (err > 0.0f ? s.kSlopeUp : s.kSlopeDown) * err;
        s.clickHp += (s.clickLp - s.clickHp) * s.kClickHp;
        float cbp, clp;
        s.clickSvf.process (s.clickLp - s.clickHp, cbp, clp);
        const float n = white01();                                      // attack noise
        s.nLp += (n - s.nLp) * s.kNoiseLp;
        s.nHp += (s.nLp - s.nHp) * s.kNoiseHp;
        const float transient = clp + (s.nLp - s.nHp);

        float mix = -transistorVca (body, s.bodyEnvLp);
        mix -= transient * s.transEnvLp * s.transientLevel;
        s.toneLp += (mix - s.toneLp) * s.toneF;
        return s.toneLp;
    }

    inline float white01() { return 0.5f + 0.5f * white(); }

    // Per-model loudness trims, set so each model peaks like the original kicks
    static constexpr float kTrimStruck = 1.0f;
    static constexpr float kTrim808    = 1.0f;
    static constexpr float kTrim909    = 1.0f;
    static constexpr float kTrimSynth  = 1.0f;

    //==========================================================================
    //  MODEL 4 - SYNTH: the produced EDM kick.  A sine dives from sweepOct
    //  octaves above its end pitch (f0) - a fast dive, then a slow glide - and
    //  its level holds, falls, then fades on a quiet tail.  The start phase
    //  sets the attack; the click is band-passed noise; the shared saturation
    //  (drive, asym) adds the harmonics a produced kick carries.
    //==========================================================================
    void triggerSynth (const KickPrototype& p, float f0, float accent, float decayMul, float dampAmt,
                       float colorAmt, float snapMul)
    {
        auto& s = syn;
        s.fEnd  = f0;
        s.oct   = std::max (0.0f, p.sweepOct + colorAmt * 0.5f) * (0.6f + 0.4f * accent);
        s.mix   = std::clamp (p.sweepMix, 0.0f, 1.0f);
        s.eFast = s.eSlow = 1.0f;
        s.kFast = std::exp (-1.0f / (std::max (0.0003f, p.sweepFast * (1.5f - 0.5f * std::min (2.0f, snapMul))) * sr));
        s.kSlow = std::exp (-1.0f / (std::max (0.001f, p.sweepSlow) * sr));
        s.phase = std::clamp (p.startPhase, 0.0f, 1.0f) * 0.25f;          // 0.25 = the sine's peak

        s.hold  = (int) (std::max (0.0f, p.holdTime) * decayMul * sr);
        const float bodyT = std::max (0.002f, p.bodyDecay * decayMul);
        const float tailT = std::max (0.005f, p.tailDecay * decayMul * std::pow (2.0f, -dampAmt * 2.0f));
        s.kBody   = std::exp (-1.0f / (bodyT * sr));
        s.kTail   = std::exp (-1.0f / (tailT * sr));
        s.tail    = std::clamp (p.tailLevel * std::pow (2.0f, -dampAmt), 0.0f, 1.0f);
        s.envBody = s.envTail = 1.0f;
        s.gate    = (p.gateTime > 0.0f) ? std::max (1, (int) (p.gateTime * decayMul * sr)) : -1;
        s.gateEnv = 1.0f;
        s.kGate   = std::exp (-1.0f / (0.006f * sr));   // LENGTH: a 6 ms release

        s.clickAmp = std::max (0.0f, p.clickLevel) * snapMul;
        s.clickEnv = 1.0f;
        s.kClick   = std::exp (-1.0f / (std::max (0.0003f, p.clickTau) * sr));
        s.clickSvf.reset();
        s.clickSvf.set (p.clickFreq * std::pow (2.0f, colorAmt * 0.5f), std::clamp (p.clickQ, 0.3f, 3.0f), sr);

        samplesLeft = (int) std::min (sr * 8.0f, sr * (std::max (0.0f, p.holdTime) * decayMul
                                                       + std::max (bodyT, tailT) * 7.0f + 0.05f));
        if (s.gate > 0)
            samplesLeft = std::min (samplesLeft, s.gate + (int) (0.06f * sr));
    }

    inline float tickSynth()
    {
        auto& s = syn;
        s.eFast *= s.kFast;
        s.eSlow *= s.kSlow;
        const float f = s.fEnd * std::exp2 (s.oct * ((1.0f - s.mix) * s.eFast + s.mix * s.eSlow));
        s.phase += std::min (f, sr * 0.45f) / sr;
        s.phase -= std::floor (s.phase);
        float env = 1.0f;
        if (s.hold > 0) --s.hold;
        else
        {
            s.envBody *= s.kBody;
            s.envTail *= s.kTail;
            env = (1.0f - s.tail) * s.envBody + s.tail * s.envTail;
        }
        if (s.gate >= 0)
        {
            if (s.gate > 0) --s.gate;
            else            s.gateEnv *= s.kGate;
            env *= s.gateEnv;
        }
        float out = std::sin (6.28318530718f * s.phase) * env;
        if (s.clickAmp > 0.0f)
        {
            s.clickEnv *= s.kClick;
            float lp, bp, hp;
            s.clickSvf.process (bipolar(), lp, bp, hp);
            out += bp * s.clickEnv * s.clickAmp;
        }
        return out;
    }

    struct SynthState
    {
        float fEnd = 50.0f, oct = 0.0f, mix = 0.0f, eFast = 0.0f, eSlow = 0.0f, kFast = 0.0f, kSlow = 0.0f, phase = 0.0f;
        int   hold = 0;
        float kBody = 0.0f, kTail = 0.0f, tail = 0.0f, envBody = 0.0f, envTail = 0.0f;
        float clickAmp = 0.0f, clickEnv = 0.0f, kClick = 0.0f;
        int   gate = -1;
        float gateEnv = 1.0f, kGate = 0.0f;
        Svf   clickSvf;
    } syn;
    float dc2X1 = 0.0f, dc2Y1 = 0.0f;               // SYNTH: DC blocker after the saturation

    struct Struck
    {
        float w0[kNumModes] {}, r[kNumModes] {}, g[kNumModes] {};
        float a1[kNumModes] {}, a2[kNumModes] {}, b0[kNumModes] {};
        float y1[kNumModes] {}, y2[kNumModes] {}, ak[kNumModes] {};
        float invE0 = 0.0f, tension = 0.0f;
        int   coefCountdown = 0, strikeLen = 2, strikePos = 0;
        float strikeAmp = 0.0f, strikeNoise = 0.0f;
        float bodyClick = 0.0f, bcEnv = 0.0f, bcCoef = 0.0f, prevBody = 0.0f;
        Svf   bcSvf;
        float vcaDrive = 0.0f;
        float fastShare = 0.0f, fastEnv = 1.0f, fastK = 1.0f;
    } st;

    struct A808
    {
        float f0 = 50.0f, decay = 0.5f, attackFm = 0.0f, selfFm = 0.0f;
        int   pulseRemaining = 0, fmPulseRemaining = 0;
        float pulse = 0.0f, pulseHeight = 0.0f, pulseLp = 0.0f, fmPulseLp = 0.0f;
        float retrigPulse = 0.0f, lpOut = 0.0f, toneLp = 0.0f;
        float kPulseDecay = 0.0f, kPulseFilter = 0.0f, kRetrigDecay = 0.0f;
        float q = 0.0f, scale = 0.0f, toneF = 0.0f, exciterLeak = 0.0f;
        CircuitSvf res;
    } a808;

    struct S909
    {
        float f0N = 0.0f, dirt = 0.0f, fmAmount = 0.0f;
        float fmDecayK = 0.0f, bodyDecayK = 0.0f, transDecayK = 0.0f;
        float toneF = 0.0f, transientLevel = 0.0f;
        float fm = 0.0f, bodyEnv = 0.0f, transEnv = 0.0f;
        int   bodyPulse = 0, fmPulse = 0;
        float phase = 0.0f, phaseNoise = 0.0f;
        float fmLp = 0.0f, bodyEnvLp = 0.0f, transEnvLp = 0.0f, toneLp = 0.0f;
        float clickLp = 0.0f, clickHp = 0.0f, nLp = 0.0f, nHp = 0.0f;
        float kSlopeUp = 0.0f, kSlopeDown = 0.0f, kClickHp = 0.0f;
        float kNoiseLp = 0.0f, kNoiseHp = 0.0f, kPhaseNoise = 0.0f, kEnvLp = 0.0f;
        CircuitSvf clickSvf;
    } s909;

    int   model = 0;
    float dcX1 = 0.0f, dcY1 = 0.0f, dcR = 0.999f;
    int   quietRun = 0;

    //==========================================================================
    inline float white()
    {
        // xorshift32 — allocation-free, RT-safe
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (float) (int32_t) rngState * 4.6566129e-10f;   // -1..1
    }

    inline float bipolar() { return white(); }

    static inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

    //==========================================================================
    float sr = 48000.0f, twoPiOverSr = 0.0f;

    // Modal bank
    float modeFreq [kNumModes] {};
    float modeAmp  [kNumModes] {};
    float modeCoef [kNumModes] {};
    float modeEnv  [kNumModes] {};
    float modePhase[kNumModes] {};

    // Pitch envelope + beater spike
    float pitchDepth = 0.0f, pitchEnv = 0.0f, pitchCoef = 0.0f;
    float spikeDepth = 0.0f, spikeEnv = 0.0f, spikeCoef = 0.0f;

    // Two-stage decay + struck onset
    float modeFastEnv [kNumModes] {};
    float modeFastCoef[kNumModes] {};
    float fastShare = 0.0f, onsetGain = 1.0f, onsetStep = 1.0f;
    float asymBias = 0.0f, asymOffset = 0.0f;

    // Click
    float clickAmp = 0.0f, clickEnv = 0.0f, clickCoef = 0.0f;
    Svf   clickSvf;

    // Noise
    float noiseAmp = 0.0f, noiseEnv = 0.0f, noiseCoef = 0.0f;
    float noiseAtt = 0.0f, noiseAttCoef = 0.0f, noiseMix = 0.0f;
    Svf   noiseSvf;

    // Shaping / output
    float drivePre = 1.0f, driveMakeup = 1.0f, outGain = 1.0f;

    // Lifetime
    bool  active = false;
    int   samplesLeft = 0;
    float maxTau = 0.0f;

    // Steal fade
    bool  killing = false;
    float killGain = 1.0f, killStep = 0.0f;

    uint32_t rngState = 0x9E3779B9u;
};

} // namespace kick
