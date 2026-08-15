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

        /** Produce a multi-line human-readable summary of a loaded style.
            Useful for verifying parsing against the Python reference dump. */
        static juce::String describe (const StyleData& style);
    };
} // namespace Betel
