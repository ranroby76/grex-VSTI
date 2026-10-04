




#pragma once
//==============================================================================
// StyleLevels.h — three controls, and only three.
//
//   BOOST                     0..100   how hard the style bus is driven
//   FOLLOW PROGRAMMED GAINS   0..100   how much of the style's own gain writing lands
//
// Owned by the SET.  Nothing else writes them, nothing else reads them, and
// there is no file of defaults any more: the values below ARE the defaults.
//
// ── WHAT WENT, AND WHY ───────────────────────────────────────────────────────
//
// This block used to carry ten things: three role-fader percentages (PERC /
// BASS / DRUMS) and four switches for automatic corrections (auto-makeup, drum
// auto-balance, rhythm ceiling, BS.1770 section trim), on top of the three that
// remain.
//
// Every one of those was a gain stage nobody could see.  With eight faders in
// the set and a full sound editor per slot, the user has direct control of the
// same levels — and an automatic correction riding underneath a fader someone
// deliberately moved does not help them, it argues with them.  So the role
// percentages went to the MIXER, the per-slot shaping went to the SOUND EDITOR,
// and the automatic corrections went away entirely rather than being switched
// off by default and left as a trap.
//
// AUTO-MAKEUP did not need a switch either: MAKEUP at 0 was the switch.
//
// ── AND THEN MAKEUP WENT TOO ─────────────────────────────────────────────────
//
// It was a loudness normaliser fed by `slotLoadGain`, which is built from the
// user's own faders and per-instrument calibration.  So a number meant to be a
// per-STYLE constant was computed from the user's level settings and applied to
// the whole style bus: turn one instrument up and everything else moved
// underneath it.  Worse, it only recomputed on a style load or a slider move, so
// edits piled up invisibly and then arrived all at once.
//
// A correction the user cannot see, cannot predict and did not ask for is not a
// feature.  It is gone, exactly as the four before it went.
//
// ── AND SO DID IGNORE STYLE VOLUMES ──────────────────────────────────────────
//
// FOLLOW at 0 IS that switch, and it is the same collapse MAKEUP's own on/off
// button made: a depth whose zero is the off position needs no separate button.
// What it buys over the old boolean is everything between - the style's writing
// applied at a quarter strength rather than all or nothing.
//
// ── THE 0..100 SCALE ─────────────────────────────────────────────────────────
//
// Both sliders read 0 = NO EFFECT, 100 = FULL EFFECT, in 101 steps.  Same scale,
// same direction, same meaning at both ends — which is the point.  Two controls
// that both say "how much of this?" should not be one in decibels and one in a
// ratio around an invisible target.
//
//   BOOST   0 -> 0 dB (untouched)      100 -> +24 dB
//   FOLLOW  0 -> style gains ignored   100 -> style obeyed exactly
//
// FOLLOW is a DEPTH.  At 100 every gain the style writes is applied as written;
// at 25 each one travels a quarter of the way from where the user's fader
// already sits to where the style asked for it.  That is what lets a style keep
// the SHAPE of its own mix while giving the player back the last word on the
// level - and it is why 0 needed no separate switch beside it.
//
// ── THREAD RULES ─────────────────────────────────────────────────────────────
//
// Atomics.  Written from the UI and from set load on the message thread, read at
// style load and from the runtime CC 7 handler.  Never per-sample.
//==============================================================================

#include "Levels.h"        // Betel::Levels - EVERY level constant lives there
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <cmath>

namespace Betel
{
    class StyleLevels
    {
    public:
        static StyleLevels& get()
        {
            static StyleLevels instance;
            return instance;
        }

        //======================================================================
        //  ***  THE STYLE-BUS BOOST.  ONE GLOBAL NUMBER, IN DECIBELS.  ***
        //
        //  The slider is on SETTINGS > GLOBAL SETTINGS and writes grex_boost.xml
        //  on every move.  kForcedBoostDb below is only the FACTORY value a
        //  fresh install opens on - delete the file to get it back.
        //
        //  kForceBoostDb IS THE OLD TUNING AID AND IS OFF.  true makes boostDb()
        //  answer the constant and ignore everything else, which is exactly how
        //  the slider came to look dead once already.  It is kept because it is
        //  genuinely useful for finding a number with one build, and it must go
        //  back to false the moment the slider matters.
        //
        //  ── GLOBAL, WITH ITS OWN FILE.  NOT PER SET. ────────────────────────
        //
        //  BOOST is one number for the whole installation, saved in
        //  grex_boost.xml and loaded at startup.  It is NOT stored in a .bset and
        //  a set can no longer change it: every style plays through the same
        //  boost, which is the point - it is the level the style bus runs at, not
        //  a property of any one style.
        //
        //  WHY IT CAME BACK AFTER BEING BAKED.  It was fixed at +19.2 dB and the
        //  slider deleted, on the argument that it duplicated the STYLE fader.
        //  That argument was about WHERE it sits, and it still holds - what it
        //  missed is that a global level wants to be reachable without a rebuild.
        //  So: one control, one file, one value, and the STYLE fader keeps doing
        //  the per-style work beside it.
        //
        //  +19.2 dB is slider 80 on the old 0..100 scale (80 x kBoostMaxDb/100),
        //  which is what it was tuned to by ear across the library, and it is the
        //  factory value a fresh install opens on.
        //
        //  AND IT IS THE SAME 80 THIS FILE SHIPPED WITH.  The +19.2 dB that took
        //  a session to find was never the wrong VALUE - it was an invisible one,
        //  on a 0..100 scale where 80 does not look like 19.2 dB, applying only
        //  to styles that had no .bset.  Fixed, stated in dB, and applied to
        //  everything, it is simply the level the style bus runs at.
        //
        //  WHY NO SLIDER.  BOOST sat in the same multiply as the STYLE fader, at
        //  the same point in the chain, doing the arithmetic the fader already
        //  does - two controls for one gain, saved in two different parts of the
        //  set, with nothing on screen to say which one somebody had used.  The
        //  fader stays and does the per-style work; this is the constant it
        //  works from.
        //
        //  THE DUCKER STILL SEES THE BOOSTED SIGNAL.  It runs downstream of the
        //  bus gain, so it reacts to what the player actually hears - unchanged
        //  by this, and the reason the value belongs here rather than after it.
        //
        //  DECOUPLED FROM Levels.h ON PURPOSE — they are different jobs.
        //
        //    Levels::kStyleBusDb  a GLOBAL trim, applied at the style bus on
        //                         every style, set or no set.  One number for
        //                         "the band is too loud".
        //    this BOOST           a PER-SET value, owned by the SET EDITOR's
        //                         slider and stored in the .bset.  One number
        //                         per style, for "this style is too loud".
        //
        //  They were briefly wired together, which made the global trim stop
        //  applying to any style that had a set.  Wrong: a global trim that only
        //  works on half the library is not a global trim.
        //
        //  ***  THE FACTORY VALUE.  +12.5 dB.  ***
        //
        //  This is ONLY the value a fresh install opens on, i.e. what you get
        //  when grex_boost.xml does not exist yet.  The live value is a GLOBAL
        //  slider backed by that file - see the block below.
        //
        //  WAS +19.2 dB (slider 80 on the retired 0..100 scale), which is the
        //  number that sat unnoticed for months because 80 does not look like
        //  19.2 dB.  12.5 dB replaces it as the tuned-by-ear sweet spot for the
        //  library.  CHANGING THIS MOVES NOBODY WHO HAS ALREADY TOUCHED THE
        //  SLIDER - their grex_boost.xml wins - so it only re-voices a fresh
        //  install, which is exactly what a factory value is allowed to do.
        static constexpr float kForcedBoostDb = 12.5f;

        //  FORCING IS OFF.  boostDb() reads the live value again.
        static constexpr bool  kForceBoostDb  = false;

        //----------------------------------------------------------------------
        // The same number on the 0..100 slider scale, DERIVED so the two can
        // never disagree.  Clamped, because the slider has no room for a cut -
        // when kForcedBoostDb is negative the forced path below bypasses this
        // entirely and the slider simply reads 0.
        //
        // There is no grex_levels.xml: a template file for two values that every
        // set carries anyway is one more owner than the design has room for.
        //----------------------------------------------------------------------
        static constexpr float kDefaultBoost =
            kForcedBoostDb <= 0.0f ? 0.0f
          : kForcedBoostDb >= 24.0f ? 100.0f
          : kForcedBoostDb * (100.0f / 24.0f);

        //----------------------------------------------------------------------
        // FOLLOW defaults to 100 - the behaviour every existing install already
        // has - so a set written before this control existed comes back sounding
        // exactly as it did.  A new default would silently re-voice the whole
        // library, which is the one thing a compatibility default must not do.
        //----------------------------------------------------------------------
        static constexpr float kDefaultStyleVolFollow = 100.0f;

        /** BOOST 100 in decibels.  The old slider topped out here too, so a set
            written against the dB scale converts cleanly (see valuesFromTree). */
        static constexpr float kBoostMaxDb = 24.0f;

        /** One style's level settings, as a plain value.

            `boost` IS A REPORT, NOT AN OWNER.  current() fills it in so callers
            can read the live number, but applyValues() deliberately does NOT
            write it back - see the note there.  The boost is global and has
            exactly two writers. */
        struct Values
        {
            float boost          = kDefaultBoost;           // 0..100 (read-only, see above)
            float styleVolFollow = kDefaultStyleVolFollow;  // 0..100

            void clampInPlace() noexcept
            {
                boost          = juce::jlimit (0.0f, 100.0f, boost);
                styleVolFollow = juce::jlimit (0.0f, 100.0f, styleVolFollow);
            }

            bool operator== (const Values& o) const noexcept
            {
                auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };
                return same (boost, o.boost)
                    && same (styleVolFollow, o.styleVolFollow);
            }

            bool operator!= (const Values& o) const noexcept { return ! (*this == o); }
        };

        static Values factoryDefaults() noexcept { return {}; }

        // ── Reads (engine side) ───────────────────────────────────────────────

        /** BOOST as a LINEAR multiplier on the style bus.

            std::pow rather than juce::Decibels so this header stays light — it is
            included by the engine, and dragging juce_audio_basics in for one
            conversion would be a poor trade. */
        float sectionBoost() const noexcept
        { return std::pow (10.0f, boostDb() / 20.0f); }

        /** THE ONE READ THAT REACHES THE AUDIO.  StylePlayer calls sectionBoost()
            above and nothing else, so gating here gates everything - the style
            load, the section boundaries and the runtime rides all come through
            this single point.  That is what makes the force flag a guarantee
            rather than a hope. */
        float boostDb() const noexcept
        {
            if (kForceBoostDb) return kForcedBoostDb;
            return boostVal.load() * (kBoostMaxDb * 0.01f);
        }

        /** Is the stored value being ignored right now?  The panel asks so it
            can say so rather than showing a slider that does nothing. */
        static constexpr bool isBoostForced() noexcept { return kForceBoostDb; }

        float boostValue()  const noexcept { return boostVal.load(); }

        /** FOLLOW PROGRAMMED GAINS.

            A Yamaha style writes its own gain staging: a CC 7 per channel when
            it loads, then more of them at every section boundary and through
            every fill.  This is how much of ALL of that Grex actually applies.

            It governs both halves, and that is the whole point of it being one
            control.  At load it decides how far each part is moved from where
            the user's fader already sits toward where the style wants it; while
            playing it decides how far a mid-song ride may travel from where the
            style itself parked that channel.

              0    the style's gain writing is ignored completely.  Every part
                   sits at the user's own fader and stays there.
              25   a quarter of it lands - the arrangement keeps its shape and
                   its direction and loses most of its travel.
              100  the style is obeyed exactly, which is what every build before
                   this control did.

            The one exception is deliberate and lives in StylePlayer: a CC 7 that
            arrives WITH a program change is the style correcting for an
            instrument it just swapped, not stating a preference, so it lands in
            full at any depth.  Damping it would leave the new voice at the old
            one's level.

            CC 11 EXPRESSION is untouched at every depth: that is the composer's
            performance shape - the swells and the ending fade-outs - not a mix
            level, and flattening it would remove the arrangement rather than
            hand over control of it. */
        //======================================================================
        // FOLLOW PROGRAMMED GAINS IS RETIRED.
        //
        // It blended each style channel's fader between the player's level and
        // the style's, because both wrote the SAME number and something had to
        // arbitrate. The gain model made them two numbers that MULTIPLY, so the
        // competition - and with it the arbitrator - is gone. The style states
        // the base; the mixer trims afterwards.
        //
        // THE FIELD IS KEPT AND STILL ROUND-TRIPS through the .bset on purpose:
        // every set already on disk carries a styleVolFollow, and a reader that
        // dropped it would rewrite those files without it the first time each
        // was saved. Nothing reads the value any more.
        //======================================================================
        float styleVolFollowValue() const noexcept { return styleVolFollow.load(); }

        // ── Writes (UI side) ──────────────────────────────────────────────────
        void setBoost     (float v) { boostVal .store (juce::jlimit (0.0f, 100.0f, v)); }
        void setStyleVolFollow (float v) { styleVolFollow.store (juce::jlimit (0.0f, 100.0f, v)); }

        // ── The whole block, in and out ───────────────────────────────────────
        Values current() const noexcept
        {
            Values v;
            v.boost          = boostVal.load();
            v.styleVolFollow = styleVolFollow.load();
            return v;
        }

        //======================================================================
        //  *** applyValues DOES NOT WRITE THE BOOST, AND THAT IS THE FIX. ***
        //
        //  The boost used to be a PER-SET value, so every style load and every
        //  set load re-applied it from the set's block.  It is GLOBAL now, with
        //  its own file - but the two callers were left in place:
        //
        //    onStyleSelected -> applyDefaults()   (every style pick)
        //    applySetPayload -> fromTree()        (every set load)
        //
        //  and both route through here with a Values whose `boost` came from
        //  factoryDefaults().  valuesFromTree politely refuses to READ a boost
        //  out of an old set - but refusing to read it means the factory value
        //  is what arrives, so "not read" silently meant "RESET".  The slider
        //  was saving correctly all along; the number was being thrown away a
        //  moment later by the next style pick.
        //
        //  So the boost is taken off this path entirely rather than fixed at
        //  each call site.  THE BOOST HAS EXACTLY TWO WRITERS:
        //
        //      loadGlobalBoost()   startup, and again whenever the root moves
        //      setBoostDb()        the slider and the MIDI remote
        //
        //  A guarantee that lives in one function cannot be broken by a third
        //  caller arriving later, which is precisely how this one broke.
        //======================================================================
        void applyValues (Values v)
        {
            v.clampInPlace();
            styleVolFollow.store (v.styleVolFollow);
        }

        /** Back to the hard-coded defaults. */
        void applyDefaults() { applyValues (factoryDefaults()); }
        void resetToDefaults() { applyDefaults(); }

        // ── Set-file round trip ───────────────────────────────────────────────
        static constexpr const char* kTreeType = "StyleLevels";

        juce::ValueTree toTree() const
        {
            const auto v = current();
            juce::ValueTree t (kTreeType);
            // BOOST IS NOT WRITTEN.  It is global now and lives in
            // grex_boost.xml; a copy in the set would be a second owner, and the
            // whole reason it moved is that two owners for one gain is how a
            // level ends up unexplainable.
            juce::ignoreUnused (v);
            t.setProperty ("styleVolFollow", v.styleVolFollow, nullptr);
            return t;
        }

        /** Read a block written by toTree().

            Also understands the OLD ten-property shape, so a set saved before
            this change still loads: sectionBoostDb converts straight onto the
            0..100 scale, and anything that no longer exists is simply dropped.
            The old makeupTarget does NOT convert — it was a target, this is a
            depth, and there is no honest mapping between them — so such a set
            comes back on the default depth. */
        static Values valuesFromTree (const juce::ValueTree& t, const Values& base)
        {
            if (! t.isValid() || ! t.hasType (kTreeType)) return base;

            Values v = base;

            // BOOST IS DELIBERATELY NOT READ.  An older set carries one and it
            // is ignored: the global file is the only owner now, so loading a
            // set can never move the whole library's level.

            // A set written before FOLLOW existed has no such property, so the
            // getProperty default carries `base` through - and base comes from
            // factoryDefaults(), which is 100.  An old set therefore loads on
            // full follow, i.e. exactly the behaviour it was saved under.
            v.styleVolFollow = (float) t.getProperty ("styleVolFollow", v.styleVolFollow);

            // MIGRATION: a set saved with IGNORE STYLE VOLUMES on meant "apply
            // none of the style's gain writing", and that is precisely FOLLOW 0.
            // Only honoured when the set predates FOLLOW - once both exist the
            // slider is the owner and a stale boolean must not override it.
            if (! t.hasProperty ("styleVolFollow") && (bool) t.getProperty ("ignoreCc7", false))
                v.styleVolFollow = 0.0f;

            // `makeup` is read and discarded.  The stage it drove is gone, and a
            // depth for a normaliser that no longer exists has nothing to mean.

            v.clampInPlace();
            return v;
        }

        void fromTree (const juce::ValueTree& t)
        { applyValues (valuesFromTree (t, factoryDefaults())); }

        //======================================================================
        //  THE GLOBAL BOOST FILE — grex_boost.xml
        //
        //  Its own file rather than a corner of an existing one, so it can be
        //  copied, deleted or hand-edited on its own.  Deleting it is how you
        //  get the factory kForcedBoostDb back.
        //
        //  WRITTEN ON EVERY CHANGE, not on a SAVE button.  There is one value
        //  and one slider; a save step for a single number is a step somebody
        //  forgets, and then the boost they tuned is gone at the next launch.
        //======================================================================
        /** Read grex_boost.xml into the live value.

            CALLED TWICE BY DESIGN: from the constructor, and again from
            BetelgeuseProcessor::reloadRootBackedSettings() every time the root
            is set.  The second call is the one that matters on a clean install -
            the singleton may well be built before the user has located the
            library, and a value read against the wrong root is not a value. */
        void loadGlobalBoost()
        {
            const auto f = GrexPaths::styleBoost();
            if (! f.existsAsFile()) return;        // no file = factory, not an error

            if (auto xml = juce::XmlDocument::parse (f))
            {
                const auto t = juce::ValueTree::fromXml (*xml);
                if (t.isValid() && t.hasProperty ("boostDb"))
                    setBoostDb ((float) t.getProperty ("boostDb", (double) kForcedBoostDb));
            }
        }

        void saveGlobalBoost() const
        {
            juce::ValueTree t ("GrexBoost");
            t.setProperty ("boostDb", boostDb(), nullptr);

            const auto f = GrexPaths::styleBoost();
            f.getParentDirectory().createDirectory();
            if (auto xml = t.createXml()) xml->writeTo (f);
        }

        /** The slider's setter: dB in, stored, and written to disk at once. */
        void setBoostDb (float db)
        {
            const float clamped = juce::jlimit (0.0f, kBoostMaxDb, db);
            boostVal.store (clamped * (100.0f / kBoostMaxDb));
        }

        /** Set AND persist.  The UI calls this one. */
        void setBoostDbAndSave (float db) { setBoostDb (db); saveGlobalBoost(); }

    private:
        StyleLevels() { loadGlobalBoost(); }

        std::atomic<float> boostVal       { kDefaultBoost  };
        std::atomic<float> styleVolFollow { kDefaultStyleVolFollow };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleLevels)
    };
} // namespace Betel
