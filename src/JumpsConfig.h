#pragma once
//==============================================================================
// JumpsConfig.h
//
// Data structure describing the post-transition jump destinations for every
// transition variation in a style (3 intros, 4 fills, 1 break = 8 sources).
//
// A "transition" variation is any non-main, non-ending section: it plays once
// (cycle loop), and once finished the sequencer must jump SOMEWHERE. The
// JumpsConfig records the user's chosen destination for each transition.
//
// Owned by the JumpsTab UI; queried by the future Set-preset serialiser; will
// be consulted by StyleSequencer at section-loop boundaries once we wire up
// the auto-transition logic.
//==============================================================================

#include "StyleData.h"
#include <array>

namespace Betel
{
    /** Number of transition variations that need a configurable destination. */
    constexpr int kNumJumpSources = 8;

    /** Source sections in canonical display order (intros → fills → break). */
    constexpr StyleSection kJumpSourceSections[kNumJumpSources] = {
        StyleSection::IntroA, StyleSection::IntroB, StyleSection::IntroC,
        StyleSection::FillAA, StyleSection::FillBB, StyleSection::FillCC, StyleSection::FillDD,
        StyleSection::FillBA
    };

    /** Total destinations a jump can target: 4 mains + 3 intros + 4 fills + break. */
    constexpr int kNumJumpDestinations = 12;

    constexpr StyleSection kJumpDestSections[kNumJumpDestinations] = {
        StyleSection::MainA, StyleSection::MainB, StyleSection::MainC, StyleSection::MainD,
        StyleSection::IntroA, StyleSection::IntroB, StyleSection::IntroC,
        StyleSection::FillAA, StyleSection::FillBB, StyleSection::FillCC, StyleSection::FillDD,
        StyleSection::FillBA
    };

    /** Per-source destination. Default layout mirrors the natural section
        pairing: IntroN → MainN, FillNN → MainN, Break → MainA. */
    struct JumpsConfig
    {
        std::array<StyleSection, kNumJumpSources> destinations;

        JumpsConfig()
        {
            destinations = {
                StyleSection::MainA, StyleSection::MainB, StyleSection::MainC,            // intros
                StyleSection::MainA, StyleSection::MainB, StyleSection::MainC, StyleSection::MainD,  // fills
                StyleSection::MainA                                                       // break
            };
        }
    };
} // namespace Betel
