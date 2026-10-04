
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme

#include <JuceHeader.h>
#include "ChordTransposer.h"

namespace Betel
{

//==============================================================================
// Harmonizer.h  -  THE NOTE MATHS, AND NOTHING ELSE.
//==============================================================================
//
// Given a note the right hand just played and the chord the left hand is
// holding, work out which notes should sound with it.
//
// DELIBERATELY PURE. No engine, no MIDI, no state, no voices, no timers - one
// static function of its arguments. That is not tidiness for its own sake: the
// hard part of a harmonizer is the note choice, and a pure function can be
// checked on paper (C over Cmaj must give E and G; the SAME C over Amin must
// give E and A) before a single note reaches the engine. Voice pairing, note
// tracking and channel routing all live at the call site, where they belong
// alongside the code that already does that for the solo path.
//
// WHY GREX HARMONISES BY GENERATING NOTES RATHER THAN PITCH-SHIFTING.
// Grex is a sampler. Every harmony voice is a real sample at that pitch, so it
// sounds exactly as good as the lead does - no formant smearing, no artefacts,
// no latency. It is also how the hardware arrangers do it: the HARMONY button
// on a Yamaha or Korg generates notes, it does not process audio.
//
// WHY CHORD-TONE HARMONY AND NOT FIXED INTERVALS.
// Parallel thirds sound wrong the moment the chord moves under them. Walking
// DOWN the chord's own tone set means the interval changes with the chord - a
// major third here, a minor third two bars later - which is what a second
// singer actually does, and it cannot clash by construction because every note
// it produces is already in the chord.
//==============================================================================
class Harmonizer
{
public:
    //==========================================================================
    // The five types. Duet is the one people use; the other four earn their
    // place by being different in kind rather than in degree.
    //==========================================================================
    enum class Type : int
    {
        Duet   = 0,   // one voice,   nearest chord tone
        Trio   = 1,   // two voices,  the next two chord tones
        Block  = 2,   // three voices, close four-part with the lead on top
        Octave = 3,   // one voice at +/-12. No chord needed
        Fifth  = 4    // one voice at +/-7.  No chord needed
    };

    static constexpr int kMaxVoices = 3;

    struct Result
    {
        int notes[kMaxVoices] {};
        int count = 0;

        void add (int n) noexcept
        {
            if (count < kMaxVoices && n >= 0 && n <= 127)
                notes[count++] = n;
        }
    };

    /** How many voices a type asks for.  Kept beside the enum so a new type
        cannot be added without answering the question. */
    static int voiceCountFor (Type t) noexcept
    {
        switch (t)
        {
            case Type::Duet:   return 1;
            case Type::Trio:   return 2;
            case Type::Block:  return 3;
            case Type::Octave: return 1;
            case Type::Fifth:  return 1;
        }
        return 1;
    }

    /** True when the type ignores the chord entirely.  Octave and Fifth are
        fixed intervals, so they keep working with no left hand down at all -
        which is exactly when a player reaches for them. */
    static bool isFixedInterval (Type t) noexcept
    {
        return t == Type::Octave || t == Type::Fifth;
    }

    //==========================================================================
    // THE ONE FUNCTION.
    //
    //   playedNote  the MIDI note the right hand played (0..127)
    //   chord       what the left hand is holding. ChordZoneTracker latches
    //               this, so it stays valid after the left hand lifts
    //   type        see above
    //   below       true  -> harmony sits UNDER the melody (the normal case)
    //               false -> harmony sits OVER it, for a lead in a low register
    //
    // Returns 0..3 notes. NEVER returns the played note itself: a unison would
    // double the lead's level rather than harmonise it, and on a sampler that
    // reads as a phasing artefact, not as a second voice.
    //==========================================================================
    static Result compute (int playedNote,
                           const Chord& chord,
                           Type type,
                           bool below = true) noexcept
    {
        Result r;

        if (playedNote < 0 || playedNote > 127)
            return r;

        const int dir = below ? -1 : +1;

        // ── Fixed intervals: no chord consulted, so no way to fail ───────────
        if (type == Type::Octave) { r.add (playedNote + dir * 12); return r; }
        if (type == Type::Fifth)  { r.add (playedNote + dir *  7); return r; }

        const uint16_t mask = NoteTransposer::chordToneMask (chord.quality);

        // An empty mask means a quality we have no template for. Falling back
        // to an octave is deliberate: it is always musically safe, it never
        // clashes, and it means an unrecognised chord degrades to a thinner
        // harmony instead of to silence. Silence would read as a broken
        // feature; a plain octave reads as a simple one.
        if (mask == 0) { r.add (playedNote + dir * 12); return r; }

        const int want = voiceCountFor (type);

        // ── Walk outward one semitone at a time, collecting chord tones ──────
        //
        // Starting at +/-1 rather than at 0 is what excludes the unison. The
        // 36-semitone reach is three octaves: enough that a sparse chord (a
        // bare 1+5 has two tones) can still find three voices, and bounded so
        // a Block over a two-note chord cannot walk to the end of the keyboard
        // hunting for a third.
        for (int step = 1; step <= 36 && r.count < want; ++step)
        {
            const int cand = playedNote + dir * step;
            if (cand < 0 || cand > 127) break;

            const int rel = ((((cand - chord.root) % 12) + 12) % 12);

            if (mask & (uint16_t) (1u << rel))
                r.add (cand);
        }

        // Ran out of keyboard before finding enough voices - near the bottom of
        // the range with Block selected, say. Whatever was found still plays:
        // two voices of a three-voice harmony is a musical result, and dropping
        // the lot because the third would not fit is not.
        return r;
    }

    /** Caption for the UI and for logs.  Here rather than in the panel so the
        name and the behaviour cannot drift apart. */
    static const char* typeName (Type t) noexcept
    {
        switch (t)
        {
            case Type::Duet:   return "DUET";
            case Type::Trio:   return "TRIO";
            case Type::Block:  return "BLOCK";
            case Type::Octave: return "OCTAVE";
            case Type::Fifth:  return "5TH";
        }
        return "DUET";
    }

    static constexpr int kNumTypes = 5;

    //==========================================================================
    // THE HARMONY BLUE, in one place.
    //
    // Solo 8 is the harmony channel, and it carries this colour in FOUR
    // separate views: the HARMONY plate on the main tab, its selector in
    // Sounds, its fader in the Mixer, and the feature panel. Four literals
    // would drift, and the odd one out would read as a bug rather than as a
    // shade - so they all come from here.
    //==========================================================================
    static constexpr juce::uint32 kHarmonyBlueARGB = 0xFF2E6FB8;

    static juce::Colour harmonyBlue() noexcept
    {
        return juce::Colour (kHarmonyBlueARGB);
    }

    //==========================================================================
    // THE BASS ZONE'S COLOUR - multi split's third zone, as asked.
    //
    // Beside the harmony blue rather than in its own header because they answer
    // the same question: which feature owns this control. Two constants in one
    // place cannot drift apart; two constants in two places will.
    //
    // Warm against the harmony blue, so a glance separates them without
    // reading anything - and away from the Betel::Pal::kAccent amber that already means
    // "selected" everywhere else in the Sounds tab.
    //==========================================================================
    static constexpr juce::uint32 kBassZoneARGB = 0xFF7A4FA8;

    static juce::Colour bassZone() noexcept
    {
        return juce::Colour (kBassZoneARGB);
    }

    //==========================================================================
    // THE HARMONY LAMP'S GREEN - RETIRED, nothing reads it.
    //
    // It was the SOLO 8 lamp's colour while SOLO 8 was a grey button, chosen
    // because an LED in harmonyBlue() on a harmony-blue field does not read as
    // lit.  SOLO 7 and 8 are now one BLACK display (MainTab.h, SoloPairPanel),
    // and on black that objection is gone - so the harmony lamp is simply
    // harmonyBlue(), the colour harmony already wears on every other view, and
    // the MANUAL BASS lamp beside it is bassZone() purple.
    //
    // Kept rather than deleted only so the value is not lost if a green is
    // ever wanted back.  Do not read it for anything new.
    //==========================================================================
    static constexpr juce::uint32 kHarmonyGreenARGB = 0xFF3FC46A;

    static juce::Colour harmonyGreen() noexcept
    {
        return juce::Colour (kHarmonyGreenARGB);
    }
};

} // namespace Betel
