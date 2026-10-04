

#pragma once
#include <juce_graphics/juce_graphics.h>

//==============================================================================
//  BalladaPalette.h — THE ONE LINE THAT COLOURS THE WHOLE PRODUCT.
//
//  GREX IS GOLD.  Ballada is grey-blue with POOL BLUE as its accent, and kBase
//  below is the entire difference between the two products.
//
//  THE FILE NAME IS BALLADA'S, AND IT IS KEPT ON PURPOSE FOR NOW.  27 files
//  include it by name; renaming it to GrexPalette.h is a mechanical sweep worth
//  doing in a batch that is already touching those files, not on its own.
//
//  ────────────────────────────────────────────────────────────────────────────
//   TO RECOLOUR GREX, CHANGE kBase BELOW.  THAT IS THE WHOLE JOB.
//   Every other colour in this file is DERIVED from it at compile time, and all
//   27 UI files read this file.  Nothing else needs touching, ever.
//  ────────────────────────────────────────────────────────────────────────────
//
//  ── WHAT WAS WRONG WITH THE FIRST VERSION OF THIS FILE ──────────────────────
//
//  It centralised the colours but not the SCHEME.  The fifteen ramp entries were
//  fifteen independently hand-picked blues, so changing kAccent moved the
//  primary accent and left every fader gradient, panel tint and dark shade
//  sitting on the old hue.  "One line to change the colour" was true only of one
//  colour - which is a worse trap than no palette at all, because the file looks
//  like it did the job.
//
//  Now the ramp is COMPUTED from kBase.  Change kBase to a green and the faders,
//  the panel tints, the keyboard's chord wash and the tutorial headings all
//  follow, in the same relative brightnesses they have today.
//
//  ── constexpr uint32, NOT juce::Colour OBJECTS.  THIS MATTERS ───────────────
//
//  These are `constexpr juce::uint32` ARGB values and the call sites wrap them
//  (`juce::Colour (Pal::kAccent)`), rather than being ready-made Colour objects.
//
//  A namespace-scope juce::Colour is DYNAMICALLY initialised, and several users
//  of these are themselves static class members in other headers (SoundsTab's
//  kPageYellow, GlobalMacrosPanel's kSettingYellow).  One static depending on
//  another across translation units is the static initialisation order fiasco,
//  and the symptom would be a control painted black on some builds and correct
//  on others, changing with link order.  A constexpr integer has no
//  initialisation to order, and the whole ramp is folded by the compiler.
//
//  ── THE RULE THE WHOLE UI IS BUILT ON ───────────────────────────────────────
//
//  BLACK TEXT ON THE ACCENT FILL.  The gold (#B87A36) is light enough for it and
//  white reads badly on it: black on kAccent is 5.9:1.  Every
//  `highlighted ? black : white` in the codebase depends on that, and no call
//  site has to think about contrast.
//
//  IF YOU PICK A DARK kBase, THAT INVERTS.  A base darker than roughly #2A6070
//  drops black text under 4.5:1 and the labels on selected controls start to
//  disappear.  Check it before shipping a new base; the ramp will follow the
//  hue happily but it cannot fix a contrast decision.
//==============================================================================
namespace Betel::Pal
{
    //==========================================================================
    // COMPILE-TIME COLOUR MATHS
    //
    // Plain channel mixing on ARGB integers.  Deliberately not HSL: converting
    // to HSL and back rounds twice and drifts the hue on the dark end, and none
    // of what this file needs - lighter, darker, more or less colourful - is
    // easier to express there.
    //==========================================================================
    namespace detail
    {
        constexpr juce::uint32 ch (juce::uint32 c, int shift) { return (c >> shift) & 0xFFu; }

        constexpr juce::uint32 clamp255 (float v)
        {
            return v <= 0.0f   ? 0u
                 : v >= 255.0f ? 255u
                               : (juce::uint32) (v + 0.5f);
        }

        constexpr juce::uint32 pack (juce::uint32 a, float r, float g, float b)
        {
            return (a << 24) | (clamp255 (r) << 16) | (clamp255 (g) << 8) | clamp255 (b);
        }

        /** Perceived brightness — the same weights the WCAG contrast maths uses,
            so "desaturate" moves a colour toward a grey that LOOKS as bright as
            it did, rather than toward the arithmetic mean of its channels, which
            would visibly darken every blue it touched. */
        constexpr float luma (juce::uint32 c)
        {
            return 0.2126f * (float) ch (c, 16)
                 + 0.7152f * (float) ch (c,  8)
                 + 0.0722f * (float) ch (c,  0);
        }

        constexpr float lerp (float from, float to, float t) { return from + (to - from) * t; }
    }

    /** Mix every channel toward one grey level: 255 lightens, 0 darkens.
        Alpha is carried through untouched. */
    constexpr juce::uint32 mixToward (juce::uint32 c, float target, float t)
    {
        return detail::pack (detail::ch (c, 24),
                             detail::lerp ((float) detail::ch (c, 16), target, t),
                             detail::lerp ((float) detail::ch (c,  8), target, t),
                             detail::lerp ((float) detail::ch (c,  0), target, t));
    }

    constexpr juce::uint32 lighten (juce::uint32 c, float t) { return mixToward (c, 255.0f, t); }
    constexpr juce::uint32 darken  (juce::uint32 c, float t) { return mixToward (c,   0.0f, t); }

    /** k > 1 pushes the channels away from the colour's own grey (more vivid),
        k < 1 pulls them toward it (more muted). */
    constexpr juce::uint32 saturate (juce::uint32 c, float k)
    {
        return detail::pack (detail::ch (c, 24),
                             detail::lerp (detail::luma (c), (float) detail::ch (c, 16), k),
                             detail::lerp (detail::luma (c), (float) detail::ch (c,  8), k),
                             detail::lerp (detail::luma (c), (float) detail::ch (c,  0), k));
    }

    constexpr juce::uint32 withAlpha (juce::uint32 c, juce::uint32 a)
    {
        return (c & 0x00FFFFFFu) | (a << 24);
    }

    //==========================================================================
    //  ***  THE BASE.  THIS IS THE LINE TO CHANGE.  ***
    //==========================================================================
    inline constexpr juce::uint32 kBase = 0xFFB87A36;   // Grex gold

    //==========================================================================
    //  THE RAMP — all derived, none typed by hand.
    //
    //  SATURATION IS APPLIED ON THE WAY OUT, and that is not decoration.  A
    //  straight mix toward white or black bleeds the hue away at both ends: the
    //  light steps go pale cream and the near-black panel tints go simply black,
    //  which is how a "gold product" ends up looking like a grey one with a gold
    //  button on it.  Saturating BEFORE lightening keeps colour in the
    //  highlights; saturating AFTER darkening keeps a visible cast in the
    //  shadows.
    //
    //  The comment after each line is what it evaluates to at the current kBase,
    //  and what Ballada gets from the same expression.  Those are notes, not
    //  inputs - change kBase and they go stale while the colours stay correct.
    //==========================================================================
    inline constexpr juce::uint32 kAccent       = kBase;                                     // #B87A36  (Ballada #3AA8C9)

    inline constexpr juce::uint32 kAccentLight  = lighten (saturate (kBase, 1.35f), 0.50f);  // #E5BB8D  (Ballada #8DD7EE)
    inline constexpr juce::uint32 kAccentSoft   = lighten (saturate (kBase, 1.25f), 0.34f);  // #D9A66E  (Ballada #6EC9E5)
    inline constexpr juce::uint32 kAccentBright = lighten (saturate (kBase, 1.20f), 0.22f);  // #D09657  (Ballada #57BEDD)
    inline constexpr juce::uint32 kAccentMid    = lighten (saturate (kBase, 1.10f), 0.13f);  // #C68A49  (Ballada #4CB5D4)
    inline constexpr juce::uint32 kAccentWarm   = lighten (kBase, 0.07f);                    // #BD8344  (Ballada #48AECD)

    inline constexpr juce::uint32 kAccentGrad   = darken (kBase, 0.08f);                     // #A97032  (Ballada #359BB9)
    inline constexpr juce::uint32 kAccentDeep   = saturate (darken (kBase, 0.24f), 1.30f);   // #985B18  (Ballada #1885A5)
    inline constexpr juce::uint32 kAccentLit    = saturate (darken (kBase, 0.46f), 1.35f);   // #6D400F  (record lamp)
    inline constexpr juce::uint32 kAccentShade  = saturate (darken (kBase, 0.52f), 1.10f);   // #5B3B16  (Ballada #185263)
    inline constexpr juce::uint32 kAccentGradDk = saturate (darken (kBase, 0.54f), 1.30f);   // #5D370F  (Ballada #0F5063)
    inline constexpr juce::uint32 kAccentDark   = saturate (darken (kBase, 0.58f), 1.30f);   // #54320E  (Ballada #0D4A5B)
    inline constexpr juce::uint32 kAccentAlert  = saturate (darken (kBase, 0.63f), 1.30f);   // #4A2C0C  (crash button)

    // Near-black panel tints.  Warmed rather than greyed, so the surfaces still
    // read as a deliberate colour instead of dead charcoal.
    inline constexpr juce::uint32 kTintPanel     = saturate (darken (kBase, 0.80f), 1.55f);  // #2B1703  (Ballada #02242E)
    inline constexpr juce::uint32 kTintPanelDeep = saturate (darken (kBase, 0.84f), 1.50f);  // #211303  (Ballada #021D24)

    //==========================================================================
    //  NAVIGATION, AND IT IS DELIBERATELY NOT THE ACCENT.
    //
    //  The page and setting controls were yellow (#E8C33A) precisely so they did
    //  NOT read as "selected" - they MOVE you somewhere rather than choose
    //  something.  Making them kAccent would have destroyed that distinction, so
    //  they are the brightest, most vivid member of the same family: still
    //  obviously not the accent, and no longer a second yellow beside the gold.
    //==========================================================================
    inline constexpr juce::uint32 kNav = lighten (saturate (kBase, 1.45f), 0.42f);           // #E4B077  (old Grex #E8C33A)

    //==========================================================================
    //  THE SELECTOR LAMPS — MainTab's LedButton, reused by JumpsTab.
    //
    //  THESE WERE RED, NOT GOLD, AND THAT IS WHY THE FIRST SWEEP MISSED THEM.
    //  The recolour looked for the gold family by hue (R > G > B); a pure red
    //  lamp has G == B, so #FF2020 and its two dim states were excluded by the
    //  filter rather than by a decision.  Worth remembering: hue filters find
    //  what they were shaped to find, and a colour that is off-brand for a
    //  different reason walks straight past them.
    //
    //  THREE LEVELS, AND THE GAPS BETWEEN THEM ARE THE POINT.  A lamp that is
    //  ON, a lamp that COULD be on, and a section that has no lamp at all are
    //  three different messages, and they only work if each is visibly darker
    //  than the last.  The lit-to-dim ratio is held at ~19x, matching the red
    //  original almost exactly, so the lamps read with the same authority they
    //  did when they were red.  The available-to-unavailable gap is WIDER than
    //  the red scheme managed (4.9x against 1.7x): the old #400000 and #1C1010
    //  were nearly indistinguishable on a dim panel.
    //
    //  ON GOLD THEY COME OUT AMBER, NOT RED.  That is the derivation doing what
    //  it is told.  If the red lamps are wanted back they are three explicit
    //  constants here, and nothing else in the tree changes.
    //==========================================================================
    inline constexpr juce::uint32 kLedOn       = saturate (lighten (kBase, 0.18f), 1.55f);   // #DD8E37  (old Grex #FF2020)
    inline constexpr juce::uint32 kLedDimAvail = saturate (darken  (kBase, 0.74f), 1.45f);   // #361F05  (old Grex #400000)
    inline constexpr juce::uint32 kLedDimOff   = saturate (darken  (kBase, 0.90f), 1.20f);   // #130C03  (old Grex #1C1010)

    //==========================================================================
    //  TRANSLUCENT OVERLAYS — the keyboard's chord-zone wash and the ducker's
    //  reference curve.  Derived from the same base, so the alpha blends stay in
    //  the family too.
    //==========================================================================
    inline constexpr juce::uint32 kSplitZoneTint = withAlpha (kBase,          0x50u);        // was 0x50CC6600
    inline constexpr juce::uint32 kCurveGhost    = withAlpha (kAccentBright,  0x40u);        // was 0x40D4AF37

    //==========================================================================
    //  GUARDS.  These fire at COMPILE time, on the machine that changes kBase,
    //  rather than being noticed on screen three sessions later.
    //==========================================================================
    static_assert (detail::ch (kBase, 24) == 0xFFu,
                   "kBase must be fully opaque ARGB (0xFFrrggbb) - the ramp carries its alpha through.");

    static_assert (detail::luma (kAccent) > 90.0f,
                   "kBase is too dark: this UI paints BLACK text on the accent fill everywhere. "
                   "Pick a lighter base, or invert every 'highlighted ? black : white' in the 28 UI files.");

    static_assert (detail::luma (kNav) > detail::luma (kAccent),
                   "kNav must stay brighter than kAccent - that difference is the only thing "
                   "telling a page control apart from a selected one.");

    static_assert (detail::luma (kLedOn) > detail::luma (kLedDimAvail) * 4.0f,
                   "kLedOn is not clearly brighter than its own dim state - the selector lamps "
                   "will not read as lit. Raise the base, or lighten kLedOn's derivation.");

    static_assert (detail::luma (kLedDimAvail) > detail::luma (kLedDimOff),
                   "An AVAILABLE-but-silent lamp must sit above an UNAVAILABLE one, or the two "
                   "states the LedButton draws become one.");
}




