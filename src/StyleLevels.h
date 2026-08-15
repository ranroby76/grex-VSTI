
#pragma once
//==============================================================================
// StyleLevels.h — three controls, and only three.
//
//   BOOST                0..100   how hard the style bus is driven
//   MAKEUP               0..100   how much loudness normalisation is glued on
//   IGNORE STYLE VOLUMES bool     discard the style's own instrument volumes
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
// AUTO-MAKEUP did not need a switch either: MAKEUP at 0 is the switch.
//
// ── THE 0..100 SCALE ─────────────────────────────────────────────────────────
//
// Both sliders read 0 = NO EFFECT, 100 = FULL EFFECT, in 101 steps.  Same scale,
// same direction, same meaning at both ends — which is the point.  Two controls
// that both say "how much of this?" should not be one in decibels and one in a
// ratio around an invisible target.
//
//   BOOST   0 -> 0 dB (untouched)      100 -> +24 dB
//   MAKEUP  0 -> normalisation off     100 -> fully normalised to the target
//
// MAKEUP is a DEPTH, not a target.  At 100 the section is driven all the way to
// kMakeupTarget; at 30 it travels 30% of that distance.  That is what makes it
// behave like glue rather than like a limiter: it pulls the quiet sections up
// toward the loud ones by a chosen amount instead of flattening everything onto
// one number.
//
// ── THREAD RULES ─────────────────────────────────────────────────────────────
//
// Atomics.  Written from the UI and from set load on the message thread, read at
// style load and from the runtime CC 7 handler.  Never per-sample.
//==============================================================================

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

        //----------------------------------------------------------------------
        // THE DEFAULTS.  Hard-coded, deliberately: there is no grex_levels.xml
        // any more, because a template file for three values that every set
        // carries anyway is one more owner than the design has room for.
        //----------------------------------------------------------------------
        static constexpr float kDefaultBoost  = 80.0f;
        static constexpr float kDefaultMakeup = 20.0f;

        /** BOOST 100 in decibels.  The old slider topped out here too, so a set
            written against the dB scale converts cleanly (see valuesFromTree). */
        static constexpr float kBoostMaxDb = 24.0f;

        /** The loudness MAKEUP drives a section toward at depth 100.  Fixed now
            rather than exposed: it is a reference point, and a reference point
            the user can move is not one. */
        static constexpr float kMakeupTarget = 1.20f;

        /** One style's level settings, as a plain value. */
        struct Values
        {
            float boost     = kDefaultBoost;    // 0..100
            float makeup    = kDefaultMakeup;   // 0..100
            bool  ignoreCc7 = false;

            void clampInPlace() noexcept
            {
                boost  = juce::jlimit (0.0f, 100.0f, boost);
                makeup = juce::jlimit (0.0f, 100.0f, makeup);
            }

            bool operator== (const Values& o) const noexcept
            {
                auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };
                return same (boost, o.boost) && same (makeup, o.makeup)
                    && ignoreCc7 == o.ignoreCc7;
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

        float boostDb() const noexcept
        { return boostVal.load() * (kBoostMaxDb * 0.01f); }

        float boostValue()  const noexcept { return boostVal.load(); }
        float makeupValue() const noexcept { return makeupVal.load(); }

        /** MAKEUP as a 0..1 depth, for blending the computed normalisation. */
        float makeupDepth() const noexcept { return makeupVal.load() * 0.01f; }

        /** IGNORE THE STYLE'S INSTRUMENT VOLUMES.

            True means every CC 7-derived level is suppressed — the fader, the GM
            volume law, the per-instrument trims — and each part's level is the
            user's mixer fader alone.  CC 11 EXPRESSION is deliberately untouched:
            that is the composer's performance shape, not a mix level, and killing
            it would flatten the arrangement rather than hand over control of it. */
        bool ignoreCc7() const noexcept { return ignoreCc7Flag.load(); }

        // ── Writes (UI side) ──────────────────────────────────────────────────
        void setBoost     (float v) { boostVal .store (juce::jlimit (0.0f, 100.0f, v)); }
        void setMakeup    (float v) { makeupVal.store (juce::jlimit (0.0f, 100.0f, v)); }
        void setIgnoreCc7 (bool  b) { ignoreCc7Flag.store (b); }

        // ── The whole block, in and out ───────────────────────────────────────
        Values current() const noexcept
        {
            Values v;
            v.boost     = boostVal.load();
            v.makeup    = makeupVal.load();
            v.ignoreCc7 = ignoreCc7Flag.load();
            return v;
        }

        void applyValues (Values v)
        {
            v.clampInPlace();
            boostVal .store (v.boost);
            makeupVal.store (v.makeup);
            ignoreCc7Flag.store (v.ignoreCc7);
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
            t.setProperty ("boost",     v.boost,     nullptr);
            t.setProperty ("makeup",    v.makeup,    nullptr);
            t.setProperty ("ignoreCc7", v.ignoreCc7, nullptr);
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

            if (t.hasProperty ("boost"))
                v.boost = (float) t.getProperty ("boost", v.boost);
            else if (t.hasProperty ("sectionBoostDb"))
                v.boost = (float) t.getProperty ("sectionBoostDb", 0.0) * (100.0f / kBoostMaxDb);

            v.makeup    = (float) t.getProperty ("makeup",    v.makeup);
            v.ignoreCc7 = (bool)  t.getProperty ("ignoreCc7", v.ignoreCc7);

            v.clampInPlace();
            return v;
        }

        void fromTree (const juce::ValueTree& t)
        { applyValues (valuesFromTree (t, factoryDefaults())); }

    private:
        StyleLevels() = default;

        std::atomic<float> boostVal      { kDefaultBoost  };
        std::atomic<float> makeupVal     { kDefaultMakeup };
        std::atomic<bool>  ignoreCc7Flag { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleLevels)
    };
} // namespace Betel
