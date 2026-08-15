// =============================================================================
//  SlotParamConvert.h
//
//  Pure SlotParams -> Channel::ChannelParams conversion (unit conversions:
//  seconds->ms ADSR, 0..1->Hz log cutoff, mono sub-mode, etc.).  Shared by the
//  host (editor knob moves) and the engine (.ins default-preset application on
//  program-change / selector loads), so both paths produce identical channels.
// =============================================================================
#pragma once

#include <JuceHeader.h>
#include <cmath>
#include "InstrEditPanel.h"   // SlotParams
#include "Channel.h"          // Betel::Channel::ChannelParams

inline Betel::Channel::ChannelParams slotParamsToChannelParams (const SlotParams& sp)
{
    Betel::Channel::ChannelParams cp;

    // ── Amp envelope: seconds → milliseconds ────────────────────────────────
    cp.ampAttack   = sp.attack  * 1000.0f;
    cp.ampHold     = 0.0f;
    cp.ampDecay    = sp.decay   * 1000.0f;
    cp.ampSustain  = sp.sustain;
    cp.pan         = sp.pan;

    // sp.gainPercent is NOT converted here.  The calibration trim never travels
    // on the bulk params path — setChannelInstrumentGainPercent is the only way
    // in, driven by a sound load or the GAIN slider.  See the note on
    // Channel::instrumentGain.
    cp.ampRelease  = sp.release * 1000.0f;
    cp.ampCurve    = sp.ampCurve;

    // ── Filter: SlotParams.filterCutoff is 0..1 normalised; the engine wants
    //   Hz.  Use a log mapping 20 Hz .. 20 kHz so the slider feels musical
    //   (each octave covers an equal slider distance).
    cp.filterType  = sp.filterType;
    {
        const float cv = juce::jlimit (0.0f, 1.0f, sp.filterCutoff);
        cp.filterCutoff = 20.0f * std::pow (1000.0f, cv);  // 20 .. 20000 Hz
    }
    cp.filterReson    = sp.filterReson;
    cp.filterKeytrack = sp.filterKeytrack;

    // ── Band filter: NOT converted here, and that is the point.
    //
    //   filterHpNorm / filterLpNorm describe a PER-CHANNEL effect.  This
    //   function feeds the bulk params push, which the engine also runs on
    //   every program change (SamplePlayerEngine::selectChannelPreset), so
    //   anything that travels this way is re-stamped whenever a style swaps an
    //   instrument mid-performance.  That is what dropped the two-handle filter
    //   back to fully open under a playing style.
    //
    //   The band now reaches the engine through its own narrow call --
    //   BetelgeuseProcessor::setChannelBandFilterNorm -> Channel::setBandFilterHz
    //   -- from the sound editor and from a set restore, and from nowhere else.
    //   Same treatment, and the same reason, as sp.gainPercent above.

    // ── Filter envelope: seconds → ms ───────────────────────────────────────
    cp.filtAttack    = sp.fEnvA * 1000.0f;
    cp.filtHold      = 0.0f;
    cp.filtDecay     = sp.fEnvD * 1000.0f;
    cp.filtSustain   = sp.fEnvS;
    cp.filtRelease   = sp.fEnvR * 1000.0f;
    cp.filtEnvAmount = sp.fEnvAmount;

    // ── Pitch envelope: seconds → ms ────────────────────────────────────────
    cp.pitchAttack   = sp.pEnvA * 1000.0f;
    cp.pitchHold     = 0.0f;
    cp.pitchDecay    = sp.pEnvD * 1000.0f;
    cp.pitchSustain  = sp.pEnvS;
    cp.pitchRelease  = sp.pEnvR * 1000.0f;
    cp.pitchEnvDepth = sp.pEnvDepth;

    // ── Mono / portamento ───────────────────────────────────────────────────
    cp.playMode         = sp.playMode;
    cp.monoHoldStolen   = sp.monoHoldStolen;
    cp.monoRetrigNew    = sp.monoRetrigNew;
    cp.monoRetrigStolen = sp.monoRetrigStolen;
    cp.portamentoTime   = sp.portamentoTime;
    cp.octaveOffset   = sp.octaveOffset;

    // ── LFOs (units already match: Hz / 0..1 / ms) ─────────────────────────
    cp.ampLfoEnabled  = sp.ampLfoEnabled;
    cp.ampLfoRate    = sp.ampLfoRate;
    cp.ampLfoDepth   = sp.ampLfoDepth;
    cp.ampLfoDelay   = sp.ampLfoDelay;
    cp.filtLfoEnabled = sp.filtLfoEnabled;
    cp.filtLfoRate   = sp.filtLfoRate;
    cp.filtLfoDepth  = sp.filtLfoDepth;
    cp.filtLfoDelay  = sp.filtLfoDelay;
    cp.pitchLfoRate  = sp.pitchLfoRate;
    cp.pitchLfoDepth = sp.pitchLfoDepth;
    cp.pitchLfoDelay = sp.pitchLfoDelay;

    // sp.sweet is NOT converted here.  Like gainPercent above, the sweetener
    // belongs to the SLOT and must survive an instrument change, so it never
    // travels on the bulk params path — see the note in Channel::ChannelParams.

    cp.velCurve = sp.velCurve;

    // ── EQ (units already match: dB / Hz) ──────────────────────────────────
    cp.eqEnabled = sp.eqEnabled;
    for (int i = 0; i < 5; ++i)
    {
        cp.eqGain[i] = sp.eqGain[i];
        cp.eqFreq[i] = sp.eqFreq[i];
    }

    // ── Reverb (0..1 throughout) ───────────────────────────────────────────
    cp.reverbEnabled = sp.reverbEnabled;
    cp.reverbSize = sp.reverbSize;
    cp.reverbDamp = sp.reverbDamp;
    cp.reverbWet  = sp.reverbWet;
    cp.reverbDry  = sp.reverbDry;
    cp.reverbTail = sp.reverbTail;
    cp.reverbHpNorm = sp.reverbHpNorm;
    cp.reverbLpNorm = sp.reverbLpNorm;
    cp.reverbPreDelay = sp.reverbPreDelay;

    // ── Delay (musical-time indices + 0..1 mix) ────────────────────────────
    cp.delayEnabled  = sp.delayEnabled;
    cp.delayTimeSig  = sp.delayTimeSig;
    cp.delayDiv      = sp.delayDiv;
    cp.delayFeedback = sp.delayFeedback;
    cp.delayWet      = sp.delayWet;

    // ── Sounds-path insert FX (direct copy) ────────────────────────────────
    cp.chorusEnabled  = sp.chorusEnabled;
    cp.chorusRate     = sp.chorusRate;
    cp.chorusDepth    = sp.chorusDepth;
    cp.chorusMix      = sp.chorusMix;

    cp.wahEnabled     = sp.wahEnabled;
    cp.wahSensitivity = sp.wahSensitivity;
    cp.wahRate        = sp.wahRate;
    cp.wahLfoDepth    = sp.wahLfoDepth;
    cp.wahBaseHz      = sp.wahBaseHz;
    cp.wahQ           = sp.wahQ;
    cp.wahMix         = sp.wahMix;

    cp.phaserEnabled  = sp.phaserEnabled;
    cp.phaserRate     = sp.phaserRate;
    cp.phaserDepth    = sp.phaserDepth;
    cp.phaserFeedback = sp.phaserFeedback;
    cp.phaserMix      = sp.phaserMix;

    return cp;
}
