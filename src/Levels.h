
#pragma once
#include <cmath>

namespace Betel
{
namespace Levels
{

/*
 ============================================================================
 ||                                                                        ||
 ||                      ADD YOUR NUMBERS HERE                             ||
 ||                                                                        ||
 ||        Change ONLY the number.  Keep the  f  and the  ;                ||
 ||                                                                        ||
 ||             0.0f   = no change                                         ||
 ||            -6.0f   = quieter                                           ||
 ||             6.0f   = louder                                            ||
 ||                                                                        ||
 ||        6 = big step     3 = clear step     1 = barely hear it          ||
 ||                                                                        ||
 ============================================================================
*/

    // THE STYLE  -  all 16 style parts together        <<<  START HERE
    // (applies to EVERY style.  The SET EDITOR's BOOST slider is the per-style
    //  one and lives in the .bset; the two multiply.)
    inline constexpr float kStyleBusDb = 0.0f;

    // YOUR RIGHT HAND  -  the solo channels
    inline constexpr float kSoloBusDb = 0.0f;

    // EVERYTHING  -  style + right hand, at the very end
    inline constexpr float kMasterDb = 0.0f;

    // THE STYLE PARTS, BEFORE THE REVERB AND DELAY.
    // Leave this at 0.0f.  Use THE STYLE above instead.
    inline constexpr float kStyleChannelDb = 0.0f;

/*
 ============================================================================
 ||                                                                        ||
 ||                   NOTHING BELOW HERE IS YOURS TO EDIT                  ||
 ||                                                                        ||
 ============================================================================
*/

    // dB -> linear.  Not constexpr: std::pow is not constexpr before C++26, and
    // writing the linear value out by hand beside the dB is the exact mistake
    // this file exists to prevent - a hand-computed 0.7499 keeps its old value
    // the first time somebody edits the dB above it, invisibly.
    inline float toGain (float db) noexcept
    {
        return db == 0.0f ? 1.0f : std::pow (10.0f, db / 20.0f);
    }

    inline float styleBusGain()     noexcept { return toGain (kStyleBusDb); }
    inline float styleChannelGain() noexcept { return toGain (kStyleChannelDb); }
    inline float soloBusGain()      noexcept { return toGain (kSoloBusDb); }
    inline float masterGain()       noexcept { return toGain (kMasterDb); }

} // namespace Levels
} // namespace Betel

//==============================================================================
//  Levels.h - EVERY LEVEL IN GREX, IN ONE PLACE, IN DECIBELS.
//
//  Change a number in the box above, rebuild, listen.  That is the whole
//  procedure, and no other file in the tree holds a level of its own.
//
//  WHICH NUMBER:
//     the whole plugin is too loud / too quiet   -> kMasterDb
//     the style is too loud vs the right hand    -> kStyleBusDb
//     the right hand is too loud vs the style    -> kSoloBusDb
//     the style's parts sit wrong against EACH
//     OTHER                                      -> not here.  That is balance,
//                                                   not level.  Use the mixer.
//
//  WHERE THEY LAND in the engine's gain chain:
//     per style channel   fader x expression x AUTO-LEVEL x balance
//                                              ^^^^^^^^^^ kStyleChannelDb
//     style bus           fader x baseUnity x MAKEUP x sectionTrim
//                                             ^^^^^^ kStyleBusDb
//     solo bus            fader x baseUnity x TRIM
//                                             ^^^^ kSoloBusDb
//     master              sum x volume x TRIM
//                                       ^^^^ kMasterDb
//
//  WHY EVERYTHING IS IN dB AND NOTHING IS ON A 0..100 SCALE: StyleLevels' old
//  default was 80 out of 100 against a 24 dB range, which is +19.2 dB on every
//  style with no .bset behind it.  Nobody spotted it for months because 80 does
//  not look like 19.2 dB.  One unit, everywhere, forever.
//==============================================================================
