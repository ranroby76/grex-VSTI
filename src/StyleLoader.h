#pragma once
//==============================================================================
// StyleLoader.h
//
// Parses Yamaha SFF1/SFF2 (SFF GE) style files (.sty, .prs, .bcs, .sst, .pst,
// .pcs, .fps, .scp) into a StyleData. Loader is stateless — both methods are
// static — so it's safe to call from any thread, but please load on the
// message thread because file IO + a few KB of allocation are involved.
//==============================================================================

#include <JuceHeader.h>
#include "StyleData.h"

namespace Betel
{
    class StyleLoader
    {
    public:
        /** Load a style file from disk. Returns true on success.
            On failure, errorMsg is filled with a human-readable reason. */
        static bool loadFromFile  (const juce::File& file,
                                   StyleData& outStyle,
                                   juce::String& errorMsg);

        /** Load a style from a raw byte buffer (e.g. embedded data). */
        static bool loadFromBytes (const uint8_t* data,
                                   size_t size,
                                   StyleData& outStyle,
                                   juce::String& errorMsg);

        /** Run the six post-CASM passes on an already-populated StyleData.

            THIS EXISTS SO THE .fst LOADER CAN CALL THEM. loadFromBytes runs
            them itself at the end of its CASM parse; FstLoader builds the same
            StyleData from a converted file and needs the identical treatment,
            and the alternative - a second copy of six pieces of musical
            judgement - is two things that drift.

            They prune duplicate sources, normalise every instrumental source
            chord to CMaj7, work out which destinations are played as chords,
            fill the two per-quality dispatch tables, and drop duplicate drum
            sources. Every one of them has a permissive default, so skipping
            them never looks like a failure - it sounds like a style that is
            subtly wrong.

            REQUIRES A FINISHED TIMELINE: three of the passes read `events` and
            each section's `eventIdx`, so call this only once those are built,
            sorted and indexed. Idempotent - running it twice changes nothing. */
        static void finaliseCasm (StyleData& outStyle);

        /** Produce a multi-line human-readable summary of a loaded style.
            Useful for verifying parsing against the Python reference dump. */
        static juce::String describe (const StyleData& style);
    };
} // namespace Betel
