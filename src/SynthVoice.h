#pragma once

#include <cstdint>
#include <algorithm>
#include "DrumDsp.h"
#include "KickVoice.h"
#include "SnareVoice.h"
#include "ClapVoice.h"
#include "HatVoice.h"
#include "CymbalVoice.h"
#include "MetalVoice.h"
#include "ToneVoice.h"

//==============================================================================
//  SynthVoice.h  —  the EDM kit's live voice, as Grex's Channel sees it.
//
//  Channel keeps its own sample-voice pipeline for every drum hit: amp
//  envelope (LENGTH), per-key band filter, gains, choke groups, FX bus and
//  inserts.  A synth hit rides that SAME pipeline; the only thing it replaces
//  is the sample read.  So this class does exactly one job: given a library
//  Sound, play it and hand back one mono block per render call.
//
//  JUCE-free and allocation-free, like every voice it wraps.
//==============================================================================

namespace edm
{

enum class VoiceType : uint8_t { None = 0, Kick, Snare, Clap, Hat, Cymbal, Metal, Tone };

//==============================================================================
//  One library sound: which voice plays it, and the prototype it plays.
//  Toms and hand drums are membranes, so they are VoiceType::Kick; side
//  sticks and woods are VoiceType::Snare; shakers are VoiceType::Hat;
//  scrapes and castanets are VoiceType::Clap; crash and ride are
//  VoiceType::Cymbal; bells are Metal; whistles, cuica and effects are Tone.
//==============================================================================
struct Sound
{
    VoiceType type = VoiceType::None;
    int articulation = 0;                 // hats: 0 closed, 1 pedal, 2 open

    union
    {
        kick::KickPrototype     kick;
        snare::SnarePrototype   snare;
        clap::ClapPrototype     clap;
        hat::HatPrototype       hat;
        cymbal::CymbalPrototype cymbal;
        metal::MetalPrototype   metal;
        tone::TonePrototype     tone;
    };

    Sound() noexcept : kick {} {}

    bool isEmpty() const noexcept   { return type == VoiceType::None; }

    const char* name() const noexcept
    {
        switch (type)
        {
            case VoiceType::Kick:   return kick.name;
            case VoiceType::Snare:  return snare.name;
            case VoiceType::Clap:   return clap.name;
            case VoiceType::Hat:    return hat.name;
            case VoiceType::Cymbal: return cymbal.name;
            case VoiceType::Metal:  return metal.name;
            case VoiceType::Tone:   return tone.name;
            case VoiceType::None:   break;
        }
        return "";
    }
};

inline kick::Macros toKickMacros (const drum::Macros& m) noexcept
{
    kick::Macros k;
    k.tuneSemis = m.tuneSemis;
    k.decay     = m.decay;
    k.damp      = m.damp;
    k.snap      = m.snap;
    k.color     = m.color;
    k.drive     = m.drive;
    k.humanize  = m.humanize;
    k.velSens   = m.velSens;
    return k;
}

//==============================================================================
//  One playing synth voice.  Holds one of each voice kind and runs whichever
//  the triggering Sound asks for — no allocation, no virtual calls.
//==============================================================================
class SynthVoice
{
public:
    void prepare (double sampleRate, uint32_t seed)
    {
        kickV.prepare (sampleRate);   kickV.setSeed  (seed ^ 0x6B1Du);
        snareV.prepare (sampleRate);  snareV.setSeed (seed ^ 0x5A17u);
        clapV.prepare (sampleRate);   clapV.setSeed (seed ^ 0xC1A9u);
        hatV.prepare (sampleRate);    hatV.setSeed (seed ^ 0x4A77u);
        cymV.prepare (sampleRate);    cymV.setSeed (seed ^ 0xCB31u);
        metalV.prepare (sampleRate);  metalV.setSeed (seed ^ 0x3E7Au);
        toneV.prepare (sampleRate);   toneV.setSeed (seed ^ 0x70E5u);
        type = VoiceType::None;
    }

    // tuneOffsetSemis carries Grex's per-key PITCH, so the drum editor's pitch
    // control moves a synth hit the way it moves a sample.
    void trigger (const Sound& s, drum::Macros m, int velocity, float tuneOffsetSemis) noexcept
    {
        m.tuneSemis += tuneOffsetSemis;
        type = s.type;

        switch (type)
        {
            case VoiceType::Kick:   kickV .trigger (s.kick,   toKickMacros (m), velocity);        break;
            case VoiceType::Snare:  snareV.trigger (s.snare,  m, velocity);                       break;
            case VoiceType::Clap:   clapV .trigger (s.clap,   m, velocity);                       break;
            case VoiceType::Hat:    hatV  .trigger (s.hat,    m, velocity, s.articulation);       break;
            case VoiceType::Cymbal: cymV  .trigger (s.cymbal, m, velocity);                       break;
            case VoiceType::Metal:  metalV.trigger (s.metal,  m, velocity);                       break;
            case VoiceType::Tone:   toneV .trigger (s.tone,   m, velocity);                       break;
            case VoiceType::None:   break;
        }
    }

    bool isActive() const noexcept
    {
        switch (type)
        {
            case VoiceType::Kick:   return kickV .isActive();
            case VoiceType::Snare:  return snareV.isActive();
            case VoiceType::Clap:   return clapV .isActive();
            case VoiceType::Hat:    return hatV  .isActive();
            case VoiceType::Cymbal: return cymV  .isActive();
            case VoiceType::Metal:  return metalV.isActive();
            case VoiceType::Tone:   return toneV .isActive();
            case VoiceType::None:   break;
        }
        return false;
    }

    // Steal fade (3-4 ms) — used when the voice slot is reused.
    void beginKill() noexcept
    {
        switch (type)
        {
            case VoiceType::Kick:   kickV .beginKill(); break;
            case VoiceType::Snare:  snareV.beginKill(); break;
            case VoiceType::Clap:   clapV .beginKill(); break;
            case VoiceType::Hat:    hatV  .beginKill(); break;
            case VoiceType::Cymbal: cymV  .beginKill(); break;
            case VoiceType::Metal:  metalV.beginKill(); break;
            case VoiceType::Tone:   toneV .beginKill(); break;
            case VoiceType::None:   break;
        }
    }

    void stop() noexcept   { type = VoiceType::None; }

    //==========================================================================
    //  Render the next numSamples as MONO into out, OVERWRITING it.
    //  Every wrapped voice adds the same sample to both of its outputs, so
    //  passing one buffer twice and halving gives the exact mono signal
    //  without a second scratch buffer.
    //==========================================================================
    void renderMono (float* out, int numSamples) noexcept
    {
        std::fill (out, out + numSamples, 0.0f);

        switch (type)
        {
            case VoiceType::Kick:   kickV .render (out, out, numSamples); break;
            case VoiceType::Snare:  snareV.render (out, out, numSamples); break;
            case VoiceType::Clap:   clapV .render (out, out, numSamples); break;
            case VoiceType::Hat:    hatV  .render (out, out, numSamples); break;
            case VoiceType::Cymbal: cymV  .render (out, out, numSamples); break;
            case VoiceType::Metal:  metalV.render (out, out, numSamples); break;
            case VoiceType::Tone:   toneV .render (out, out, numSamples); break;
            case VoiceType::None:   return;
        }

        for (int i = 0; i < numSamples; ++i)
            out[i] *= 0.5f;
    }

private:
    VoiceType type = VoiceType::None;

    kick::KickVoice     kickV;
    snare::SnareVoice   snareV;
    clap::ClapVoice     clapV;
    hat::HatVoice       hatV;
    cymbal::CymbalVoice cymV;
    metal::MetalVoice   metalV;
    tone::ToneVoice     toneV;
};

} // namespace edm
