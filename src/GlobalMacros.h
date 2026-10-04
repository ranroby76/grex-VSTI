

#pragma once
//==============================================================================
// GlobalMacros.h — "FUNKEY MODE".
//
// WHAT THESE ARE
// --------------
// Two global macros that swap the ENTIRE effects chain of the affected channels
// for a set of tuned presets.  They are not layered on top of the instrument's
// own effects — they run IN PARALLEL AND TOGGLED:
//
//     macro OFF -> the instrument's / kit's own private FX are in charge
//     macro ON  -> ONLY the macro's FX are in charge
//
// That parallel-and-toggled rule is the whole design, and it has one hard
// consequence that shapes everything below: the private FX must SURVIVE while a
// macro is on, so switching back restores exactly what was there.  Nothing here
// ever writes into a slot's own parameters — the macro set and the private set
// are stored separately and the ACTIVE one is pushed to the engine.  Toggling is
// therefore lossless and repeatable.
//
// FUNKEY MODE is keyed on GM FAMILY (flag >> 3).  There is no bus shared by
// "all guitars" — the FX chain lives per channel — so a family preset is a
// LOOKUP applied whenever a slot loads an instrument in that family, and
// re-applied when the sounding program changes.  Six families are covered,
// chosen as the ones that carry a funk arrangement:
//
//      1  Chromatic Percussion  (GM   8..15)
//      2  Organ                 (GM  16..23)
//      3  Guitar                (GM  24..31)
//     10  Synth Lead            (GM  80..87)
//     13  Ethnic                (GM 104..111)
//     14  Percussive            (GM 112..119)
//
// PIANO WAS DROPPED.  It is the family a ballad arrangement leans on hardest as
// itself, and a wah on it is a novelty rather than a voicing.
//
// ── FUNKEY OWNS WAH AND PHASER.  NOTHING ELSE.  ─────────────────────────────
//
// It used to take the whole six-stage chain — EQ, chorus, wah, phaser, delay,
// reverb — and that was the source of its one real defect, recorded here at the
// time: a family's chorus and reverb "were never the ones it played through, so
// tuning a family by ear was impossible."  A shared glue pair was tried against
// that and removed; giving each family its own room did not fix it either,
// because the room a family is tuned in is still not the room the arrangement
// sits in.
//
// The answer was to stop taking the room at all.  Wah and phaser are the two
// stages that ARE the funk character and that no instrument's own preset has a
// strong opinion about; EQ, delay, chorus and reverb are placement and voicing,
// and those belong to the instrument and to the section buses that were tuned
// with the rest of the arrangement.  So the macro now overlays two stages on top
// of a chain that is otherwise left exactly as it was, and what you tune in a
// family editor is what you hear.
//
// An instrument outside the six keeps everything, wah and phaser included, even
// while Funkey Mode is on.
//
// BIG DRUMS WAS REMOVED, and this is the whole note it gets: it replaced a
// loaded kit's ENTIRE FX rack with one global one, which is backwards for a kit
// voiced to sit in a particular style.  Editors, preset file and remote id went
// with it.
//
// PERSISTENCE
// -----------
// One file each, deliberately separate so the two can be shared, replaced or
// reverted independently:
//
//     grex_funkey.xml      the six family presets
//
// (grex_bigdrums.xml went with Big Drums.  Nothing reads or writes it.)
//
// It is loaded at plugin start and written by the editor's SAVE button.  It is
// NOT part of a set: a set records whether the macro is ENGAGED, while the
// macro preset itself is global to the installation.  Loading someone else's
// set therefore cannot silently redefine what your Funkey Mode sounds like.
//
// A MISSING FILE IS NORMAL AND NO LONGER MEANS SILENCE: the six families are
// SEEDED with working wah and phaser settings in the constructor, so the macro
// does something the first time it is switched on.  See seedFamilyDefaults.
//==============================================================================

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

namespace Betel
{
    //==========================================================================
    // GrexPaths — every file the plugin owns lives under ONE root.
    //
    // Before this there were three conventions at once: some files in the
    // installation root, some in the user's Documents folder, and some at a
    // hardcoded "D:/workspace/BetelgeuseArranger/...".  That meant an install
    // could not be moved or backed up as a unit, and a machine without that
    // exact D: path silently wrote diagnostics nowhere useful.
    //
    // setRoot() is called once, at startup, with BetelFolderManager's root.
    // Everything else asks here.  Any file added later should get an accessor
    // in this struct rather than building its own path.
    //==========================================================================
    //==========================================================================
    // displayNameFor — the one place a style or set file becomes readable text.
    //
    // Downloaded styles arrive named `<name>.<model>.<ext>` — `Waltz.S460.sty`,
    // `8Beat.T170.prs` — and getFileNameWithoutExtension strips only the LAST
    // extension, so the model code survives into every list, every selector and
    // every header in the plugin.  Cut at the FIRST dot and the model code goes
    // with it, which is exactly the shape the whole library shares.
    //
    // ONE implementation, called from all sixteen sites that used to derive a
    // display name on their own.  Sixteen private copies is how the eq/reverb/
    // delay enables drifted apart, and a name that reads differently in the set
    // list than in the header is the same class of bug.
    //
    // Two guards worth knowing:
    //   • A stem with no dot comes back untouched, so ordinary names are safe.
    //   • If cutting would leave NOTHING (a name that starts with a dot), the
    //     original is returned instead — a blank row in a selector is worse
    //     than an ugly one.
    //
    // Deliberately NOT used where a name becomes a FILENAME (SetBaker's .bset,
    // the set path in MainComponent).  Two styles that differ only by model
    // code — Waltz.S460 and Waltz.T170 — would bake to the same file and one
    // would silently overwrite the other.  On disk the model code earns its
    // keep; on screen it does not.
    //==========================================================================
    //==========================================================================
    // prettyName - UNDERSCORES BECOME SPACES, and nothing else.
    //
    // Sound and style files are named for a filesystem, not for a reader:
    // `1042_Accordion_Musette`, `126-000-036_Arabic Kit`, `8Beat_Pop_Rock`.
    // The underscore is there because it survives every OS and every zip; it
    // has no business on screen.
    //
    // DISPLAY ONLY.  NEVER call this on anything that is an IDENTITY:
    //   * a style's `styleId` - the browser hands that string back on selection
    //   * a set's filename, or SetBaker's .bset path
    //   * a sound's FLAG (the leading digits) - a saved set stores that number
    // Rewriting any of those turns a saved set into a dangling reference.  The
    // safe sites are the ones whose value is only ever DRAWN, or only ever
    // compared against other values from the same converted store.
    //
    // Doubles are collapsed, because `Grand__Piano` should not read as a gap,
    // and the result is trimmed so a trailing `_` does not leave a hanging
    // space that quietly breaks an equality test elsewhere.
    //==========================================================================
    inline juce::String prettyName (const juce::String& raw)
    {
        if (! raw.containsChar ('_')) return raw;

        auto out = raw.replaceCharacter ('_', ' ');
        while (out.contains ("  ")) out = out.replace ("  ", " ");
        out = out.trim();

        // A name that was nothing BUT underscores would come back empty, and a
        // blank row in a selector is worse than an ugly one - the same guard
        // displayNameFor already makes for a stem that begins with a dot.
        return out.isEmpty() ? raw : out;
    }

    inline juce::String displayNameFor (const juce::String& stem)
    {
        // BOTH TRANSFORMS IN ONE PLACE.  This function is already documented as
        // the single site where a style or set file becomes readable text, and
        // sixteen callers go through it - so the underscore swap belongs here
        // rather than at sixteen call sites, for exactly the reason the model-
        // code cut did.
        const int dot = stem.indexOfChar ('.');
        if (dot < 0) return prettyName (stem);

        const auto head = stem.substring (0, dot).trim();
        return prettyName (head.isNotEmpty() ? head : stem);
    }

    inline juce::String displayNameFor (const juce::File& f)
    {
        return displayNameFor (f.getFileNameWithoutExtension());
    }

    struct GrexPaths
    {
        static juce::File& rootRef()
        {
            static juce::File root;
            return root;
        }

        /** Called once from the processor/editor once the folder manager knows
            where the library is.  Safe to call again if the user relocates it. */
        static void setRoot (const juce::File& f) { rootRef() = f; }

        /** The plugin root.  Falls back to the executable's folder so a
            diagnostic written before setRoot() still lands somewhere sane
            rather than at a hardcoded absolute path. */
        static juce::File root()
        {
            const auto& r = rootRef();
            if (r.isDirectory()) return r;
            return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                       .getParentDirectory();
        }

        // ── EVERY FILE NAME HERE CARRIES THE grex_ PREFIX, NOT ballada_ ───
        //
        // Grex and Ballada can be installed side by side, and a player who owns
        // both will point them at DIFFERENT root folders - but nothing stops
        // him pointing them at the same one, and if he does, a shared file name
        // means the free plugin silently rewrites the paid plugin's settings.
        // The prefix is what makes that impossible.
        //
        // THE ValueTree TAG NAMES INSIDE THESE FILES ARE UNCHANGED on purpose
        // ("GrexMaster", "GrexCrash", "GrexFunkey"): a tuned ballada_master.xml
        // can be seeded into Grex by copying it and renaming it grex_master.xml.
        // Changing the tags as well would have made that copy fail silently,
        // which is a worse trade than it looks.
        static juce::File funkeyPreset()   { return root().getChildFile ("grex_funkey.xml");   }

        /** THE GLOBAL STYLE-BUS BOOST, one number, written on every change.
            Its own file so it can be copied, deleted or hand-edited alone -
            deleting it restores StyleLevels::kForcedBoostDb.  See StyleLevels.h. */
        static juce::File styleBoost()     { return root().getChildFile ("grex_boost.xml");    }
        static juce::File favorites()      { return root().getChildFile ("favorites.xml");        }
        static juce::File registration()   { return root().getChildFile ("grex_registration.xml"); }
        static juce::File settings()       { return root().getChildFile ("grex_settings.xml"); }
        static juce::File styleLevels()    { return root().getChildFile ("grex_levels.xml");  }
        // The star on every style button.  Its own file rather than a corner of
        // grex_settings.xml: it is per-STYLE data about the library, not a
        // preference about the plugin, and it grows with the library.
        static juce::File styleFavorites() { return root().getChildFile ("grex_style_favorites.xml"); }
        static juce::File searchHistory()  { return root().getChildFile ("grex_searches.xml"); }
        // The two PLAYER-wide settings — chord mode and tempo free/synced.
        // Deliberately not part of a set: see MasterSettings.h.
        static juce::File master()         { return root().getChildFile ("grex_master.xml"); }
        // The MIDI CC learn map.  Its own file, NOT part of grex_master.xml:
        // that holds two flags about how the player plays, this is a HARDWARE
        // map with its own lifetime — re-learned when the controller changes.
        static juce::File ccMap()          { return root().getChildFile ("grex_cc_map.xml"); }
        static juce::File crashSettings()  { return root().getChildFile ("grex_crash.xml"); }
        // NO LOG PATHS HERE.  styleLog / perfLog / drumLog (grex_log.txt,
        // grex_perf.txt, grex_drum.txt) are gone along with the tracing that
        // wrote them.  If a future fault needs instrumentation, add it behind
        // a #define that ships as 0 - a helper sitting here is an invitation to
        // put file I/O back on a thread that cannot afford it.
    };

    //==========================================================================
    // The six-stage melodic chain, mirroring the per-slot parameters exactly so
    // a family preset and a slot preset are interchangeable at the point they
    // are pushed to a Channel.  Field names match the slot params one-for-one —
    // that is intentional, so the push helper is a straight member-for-member
    // copy and cannot drift as either side gains a parameter.
    //==========================================================================
    // DEFAULTS ARE A CLEAN SLATE: every stage disabled and every value zeroed,
    // so a fresh install starts silent and neutral.  The shipped voicing comes
    // from grex_funkey.xml, not from these.
    struct FamilyFxParams
    {
        // ── 5-band EQ ────────────────────────────────────────────────────────
        bool  eqEnabled = false;
        // Flat, with the band centres left at the editor's own defaults so a
        // reset preset is silent rather than merely quiet.
        float eqGain[5] { 0, 0, 0, 0, 0 };
        float eqFreq[5] { 80.0f, 240.0f, 750.0f, 2500.0f, 8000.0f };

        // ── Chorus (80s BBD — see SoundsFx.h ChorusFx) ────────────────────────
        bool  chorusEnabled = false;
        float chorusRate    = 0.0f;
        float chorusDepth   = 0.0f;
        float chorusMix     = 0.0f;

        // ── Auto-wah ─────────────────────────────────────────────────────────
        bool  wahEnabled     = false;
        float wahSensitivity = 0.0f;
        float wahRate        = 0.0f;
        float wahLfoDepth    = 0.0f;
        float wahBaseHz      = 0.0f;
        float wahQ           = 0.0f;
        float wahMix         = 0.0f;

        // ── Phaser ───────────────────────────────────────────────────────────
        bool  phaserEnabled  = false;
        float phaserRate     = 0.0f;
        float phaserDepth    = 0.0f;
        float phaserFeedback = 0.0f;
        float phaserMix      = 0.0f;

        // ── Delay ────────────────────────────────────────────────────────────
        bool  delayEnabled  = false;
        int   delayTimeSig  = 0;
        int   delayDiv      = 0;
        float delayFeedback = 0.0f;
        // Delay tone.  Present so EffectsPanel's templated loadFx / readFx sees
        // the same field names on both types; the Funkey macro copies only wah
        // and phaser, so these ride along unused on a family and are the section
        // rack's real values everywhere else.
        float delayDampHz   = 5000.0f;
        float delayHpHz     = 20.0f;
        float delaySmoothMs = 40.0f;
        float delayWet      = 0.0f;
        // THESE THREE EXIST FOR THE SAME REASON reverbDry BELOW DOES: loadFx /
        // readFx in EffectsPanel are TEMPLATED and shared with SlotParams, so a
        // field the panel touches has to exist on both structs or the family
        // editor will not compile.  Same defaults and same meanings as there.
        float delayDry      = 1.0f;   // independent of wet, 1.0 = untouched
        float delayWetBase  = 0.5f;   // what full WET travel is worth
        float reverbWetBase = 1.0f;   // 1.0 keeps every saved family unchanged

        // ── Reverb ───────────────────────────────────────────────────────────
        // NOTE ON COST: each engaged family reverb is an 8-line FDN.  Seven
        // families all running reverb is seven tanks, on top of the per-slot
        // ones — the perf log's style-bus stage is where that will show up.  The
        // enable flag defaults OFF for exactly this reason; switch it on per
        // family only where it earns its place.
        bool  reverbEnabled  = false;
        float reverbSize     = 0.0f;
        float reverbDamp     = 0.0f;
        float reverbWet      = 0.0f;
        float reverbPreDelay = 0.0f;
        // A FAMILY OWNS ITS WHOLE REVERB, so it needs the same four controls a
        // slot has.  They are here because loadFx / readFx in EffectsPanel are
        // TEMPLATED and shared between SlotParams and this struct: a field the
        // panel reads must exist on both, or the macro editor shows a control
        // that silently does nothing.
        //
        // DRY 1.0 and LP 1.0 are the untouched defaults - a family loaded from
        // an older grex_funkey.xml sounds exactly as it did.  TAIL falls back to
        // reverbSize on read, which is what used to drive the decay.
        float reverbDry      = 1.0f;
        float reverbTail     = 0.5f;
        float reverbHpNorm   = 0.2917f;   // 150 Hz, the old fixed send high-pass
        float reverbLpNorm   = 1.0f;      // open
    };

    //==========================================================================
    // The six families Funkey Mode covers, as GM family indices (flag >> 3).
    //
    // SLOT NUMBERS ARE NOT A STABLE KEY, and dropping Piano is what proved it:
    // every family below shifted down by one, so a preset file written before
    // that would have loaded Organ's settings into Chromatic Percussion and so
    // on down the list — silently, with no error anywhere.  loadFunkey now
    // matches on the NAME the file already carried and treats the slot index as
    // a fallback, so this list can change again without corrupting anything.
    //==========================================================================
    struct FunkeyFamilies
    {
        enum Slot { ChromPerc = 0, Organ, Guitar, SynthLead, Ethnic, Percussive, Count };

        /** GM family index (flag >> 3) for each covered slot. */
        static constexpr int gmFamily (int slot) noexcept
        {
            constexpr int f[Count] = { 1, 2, 3, 10, 13, 14 };
            return (slot >= 0 && slot < Count) ? f[slot] : -1;
        }

        static const char* name (int slot) noexcept
        {
            switch (slot)
            {
                case ChromPerc:  return "Chromatic Percussion";
                case Organ:      return "Organ";
                case Guitar:     return "Guitar";
                case SynthLead:  return "Synth Lead";
                case Ethnic:     return "Ethnic";
                case Percussive: return "Percussive";
                default:         return "?";
            }
        }

        /** The slot whose stored name matches, or -1.  loadFunkey's primary key
            — see the note above on why the index is not one. */
        static int slotForName (const juce::String& n) noexcept
        {
            for (int i = 0; i < Count; ++i)
                if (n.equalsIgnoreCase (name (i))) return i;
            return -1;
        }

        /** Which covered slot a GM program belongs to, or -1 when the program's
            family is not one Funkey Mode touches.  This is the whole routing
            decision: -1 means "leave this instrument's own FX alone". */
        static int slotForProgram (int gmProgram) noexcept
        {
            if (gmProgram < 0 || gmProgram > 127) return -1;
            const int fam = gmProgram >> 3;
            for (int i = 0; i < Count; ++i)
                if (gmFamily (i) == fam) return i;
            return -1;
        }
    };

    //==========================================================================
    // The macro state: the presets themselves plus whether each macro is
    // engaged.  The engaged flags are atomic because the audio thread may read
    // them; the presets are message-thread only (edited, saved, loaded) and are
    // pushed to the engine from there.
    //==========================================================================
    class GlobalMacros
    {
    public:
        static GlobalMacros& get() { static GlobalMacros inst; return inst; }

        //----------------------------------------------------------------------
        // Engaged state.  Saved WITH a set (the set records what was switched
        // on), unlike the presets, which are global to the installation.
        //----------------------------------------------------------------------
        bool isFunkeyOn() const noexcept { return funkeyOn.load(); }
        void setFunkeyOn (bool b) noexcept { funkeyOn.store (b); }

        //----------------------------------------------------------------------
        //  THE FUNKEY MIX - 0..1, the live value (the slider shows 0..100).
        //
        //  Blends the instrument's DRY signal with Funkey's finished output
        //  (wah then phaser), right after the stage - see
        //  Channel::applyFunkeyInPlace.  One value for every funkeyed
        //  instrument at once, and it BELONGS TO THE SET, i.e. to the style:
        //  it travels in the set's UiState next to funkeyMode and is put back
        //  by MainComponent::applyMacroEngagedState.  That is the point of it -
        //  one style wants Funkey barely there, another wants it soaked.
        //
        //  Like funkeyMode, silence from a set is an answer: a set that does
        //  not mention it, and a style with no set at all, get the default -
        //  never whatever the previous style was using.
        //
        //  DEFAULT 0.5 is Rob's call (2026-10), made knowing it leaves Funkey
        //  half as wet as it was before the slider existed.
        //----------------------------------------------------------------------
        static constexpr float kFunkeyMixDefault = 0.5f;

        float funkeyMix() const noexcept      { return funkeyMixVal.load(); }
        void  setFunkeyMix (float mix01) noexcept
        { funkeyMixVal.store (juce::jlimit (0.0f, 1.0f, mix01)); }

        //----------------------------------------------------------------------
        // Presets (message thread).
        //----------------------------------------------------------------------
        FamilyFxParams&       family (int slot)       { return fams[(size_t) clampSlot (slot)]; }
        const FamilyFxParams& family (int slot) const { return fams[(size_t) clampSlot (slot)]; }

        /** The family preset that should govern a given GM program, or nullptr
            when Funkey Mode is off or the program's family is not covered.
            Callers use nullptr as "use the instrument's own FX". */
        const FamilyFxParams* presetForProgram (int gmProgram) const
        {
            if (! funkeyOn.load()) return nullptr;
            const int slot = FunkeyFamilies::slotForProgram (gmProgram);
            return slot < 0 ? nullptr : &fams[(size_t) slot];
        }

        /** THE TWELVE VALUES THE FUNKEY STAGE RUNS ON, for a given GM program.

            Writes wah and phaser into `out` and returns true when the macro is
            on AND the program's family is covered.  Returns false and leaves
            `out` alone otherwise - a false answer means "bypass the stage", and
            the caller pushes a default-constructed FunkeyFx to do exactly that.

            TEMPLATED on the destination for the same reason overrideFx is: the
            struct it fills is Betel::Channel::FunkeyFx, and this header knows
            nothing about the engine and should not start now. */
        template <typename FunkeyFxT>
        bool funkeyFxForProgram (int gmProgram, FunkeyFxT& out) const
        {
            const FamilyFxParams* fx = presetForProgram (gmProgram);
            if (fx == nullptr) return false;
            toFunkeyFx (*fx, out);
            return true;
        }

        /** Copy a family's twelve wah/phaser values into an engine-side struct.
            Templated because this header knows nothing about the engine: the
            destination is Betel::Channel::FunkeyFx and the only requirement is
            that it carries these field names. */
        template <typename FunkeyFxT>
        static void toFunkeyFx (const FamilyFxParams& f, FunkeyFxT& out)
        {
            const FamilyFxParams* fx = &f;

            out.wahEnabled     = fx->wahEnabled;
            out.wahSensitivity = fx->wahSensitivity;
            out.wahRate        = fx->wahRate;
            out.wahLfoDepth    = fx->wahLfoDepth;
            out.wahBaseHz      = fx->wahBaseHz;
            out.wahQ           = fx->wahQ;
            out.wahMix         = fx->wahMix;

            out.phaserEnabled  = fx->phaserEnabled;
            out.phaserRate     = fx->phaserRate;
            out.phaserDepth    = fx->phaserDepth;
            out.phaserFeedback = fx->phaserFeedback;
            out.phaserMix      = fx->phaserMix;
        }

        //----------------------------------------------------------------------
        // Files.  Separate on purpose — see the header note.
        //----------------------------------------------------------------------
        static juce::File funkeyFile()   { return GrexPaths::funkeyPreset();   }

        /** Written by the editor's SAVE button. */
        bool saveFunkey() const
        {
            juce::ValueTree t ("GrexFunkey");
            for (int i = 0; i < FunkeyFamilies::Count; ++i)
            {
                juce::ValueTree f ("Family");
                f.setProperty ("slot", i, nullptr);
                f.setProperty ("name", FunkeyFamilies::name (i), nullptr);
                writeFamily (f, fams[(size_t) i]);
                t.appendChild (f, nullptr);
            }
            return writeTree (funkeyFile(), t);
        }

        /** Read at plugin start.  A missing or malformed file is not an error —
            the built-in defaults stand, so a fresh install still works. */
        void loadFunkey()
        {
            const auto xml = juce::XmlDocument::parse (funkeyFile());
            if (xml == nullptr) return;
            const auto t = juce::ValueTree::fromXml (*xml);
            if (! t.isValid() || ! t.hasType ("GrexFunkey")) return;

            for (int c = 0; c < t.getNumChildren(); ++c)
            {
                const auto f = t.getChild (c);
                if (f.hasType ("Glue")) continue;      // legacy shared-glue child

                // NAME FIRST, index only as a fallback.  Both are written, and
                // the name is the one that survives the family list changing —
                // a file from before Piano was dropped resolves every remaining
                // family correctly, and its Piano block finds no home and is
                // discarded, which is exactly right.
                int slot = FunkeyFamilies::slotForName (f.getProperty ("name").toString());

                if (slot < 0)
                    slot = (int) f.getProperty ("slot", -1);

                if (slot >= 0 && slot < FunkeyFamilies::Count)
                    readFamily (f, fams[(size_t) slot]);
            }
        }

    private:
        //----------------------------------------------------------------------
        //  SEEDED DEFAULTS — WHY THE MACRO USED TO DO NOTHING AT ALL.
        //
        //  FamilyFxParams defaults every field to 0/false, `fams` is
        //  value-initialised from it, and grex_funkey.xml is only ever written
        //  by a family editor's SAVE button.  On an install where nobody had
        //  pressed SAVE, all six families were wah OFF and phaser OFF - so
        //  switching Funkey Mode on copied "off" over "off" and was correctly,
        //  silently inaudible.
        //
        //  These give each family something to BE the first time the button is
        //  pressed.  They are a starting point, not a house style: the editor
        //  overwrites any of them, and a SAVE makes the change permanent.
        //----------------------------------------------------------------------
        GlobalMacros() { seedFamilyDefaults(); }

        void seedFamilyDefaults()
        {
            auto wah = [] (FamilyFxParams& f, float sens, float rate, float lfo,
                           float base, float q, float mix)
            {
                f.wahEnabled = true;  f.wahSensitivity = sens; f.wahRate = rate;
                f.wahLfoDepth = lfo;  f.wahBaseHz = base;      f.wahQ = q;
                f.wahMix = mix;
            };
            auto phaser = [] (FamilyFxParams& f, float rate, float depth,
                              float fb, float mix)
            {
                f.phaserEnabled = true; f.phaserRate = rate; f.phaserDepth = depth;
                f.phaserFeedback = fb;  f.phaserMix = mix;
            };

            // GUITAR — the classic funk auto-wah: envelope-led, quick, narrow,
            // low centre so a muted chord opens it and a held one closes.
            wah    (fams[FunkeyFamilies::Guitar],     0.80f, 2.2f, 0.10f, 320.0f, 0.70f, 0.65f);
            phaser (fams[FunkeyFamilies::Guitar],     0.30f, 0.70f, 0.45f, 0.30f);

            // CHROMATIC PERCUSSION — clav and e-piano territory.  Mostly wah,
            // higher centre so the attack keeps its bite.
            wah    (fams[FunkeyFamilies::ChromPerc],  0.75f, 2.6f, 0.05f, 480.0f, 0.60f, 0.60f);
            phaser (fams[FunkeyFamilies::ChromPerc],  0.25f, 0.55f, 0.35f, 0.20f);

            // ORGAN — phaser-led, and slow.  The wah is a gentle LFO sweep
            // rather than an envelope, because an organ holds notes flat and has
            // almost no attack for a follower to track.
            wah    (fams[FunkeyFamilies::Organ],      0.25f, 0.9f, 0.55f, 420.0f, 0.50f, 0.35f);
            phaser (fams[FunkeyFamilies::Organ],      0.18f, 0.85f, 0.55f, 0.55f);

            // SYNTH LEAD — wide phaser, deliberately more than the others: a
            // lead is one line and can carry movement the others cannot.
            wah    (fams[FunkeyFamilies::SynthLead],  0.55f, 1.8f, 0.30f, 500.0f, 0.65f, 0.45f);
            phaser (fams[FunkeyFamilies::SynthLead],  0.35f, 0.90f, 0.60f, 0.50f);

            // ETHNIC — light touch.  These are character instruments already and
            // a heavy sweep buries what makes them recognisable.
            wah    (fams[FunkeyFamilies::Ethnic],     0.45f, 1.4f, 0.20f, 380.0f, 0.55f, 0.30f);
            phaser (fams[FunkeyFamilies::Ethnic],     0.22f, 0.50f, 0.30f, 0.20f);

            // PERCUSSIVE — wah only, fast and dry-ish.  A phaser on a plucked
            // transient smears the one thing it is there for.
            wah    (fams[FunkeyFamilies::Percussive], 0.85f, 3.0f, 0.05f, 550.0f, 0.75f, 0.50f);
        }

        static int clampSlot (int s) noexcept
            { return juce::jlimit (0, (int) FunkeyFamilies::Count - 1, s); }

        static bool writeTree (const juce::File& f, const juce::ValueTree& t)
        {
            if (auto xml = t.createXml())
            {
                f.getParentDirectory().createDirectory();
                return xml->writeTo (f);
            }
            return false;
        }

        static void writeFamily (juce::ValueTree& t, const FamilyFxParams& p)
        {
            #define GMW(x) t.setProperty (#x, p.x, nullptr);
            GMW(eqEnabled)
            GMW(chorusEnabled) GMW(chorusRate) GMW(chorusDepth) GMW(chorusMix)
            GMW(wahEnabled) GMW(wahSensitivity) GMW(wahRate) GMW(wahLfoDepth)
            GMW(wahBaseHz) GMW(wahQ) GMW(wahMix)
            GMW(phaserEnabled) GMW(phaserRate) GMW(phaserDepth) GMW(phaserFeedback) GMW(phaserMix)
            GMW(delayEnabled) GMW(delayTimeSig) GMW(delayDiv) GMW(delayFeedback) GMW(delayWet)
            GMW(delayDry) GMW(delayWetBase) GMW(reverbWetBase)
            GMW(reverbEnabled) GMW(reverbSize) GMW(reverbDamp) GMW(reverbWet) GMW(reverbPreDelay)
            GMW(reverbDry) GMW(reverbTail) GMW(reverbHpNorm) GMW(reverbLpNorm)
            #undef GMW
            for (int i = 0; i < 5; ++i)
            {
                t.setProperty ("eqGain_" + juce::String (i), p.eqGain[i], nullptr);
                t.setProperty ("eqFreq_" + juce::String (i), p.eqFreq[i], nullptr);
            }
        }

        static void readFamily (const juce::ValueTree& t, FamilyFxParams& p)
        {
            #define GMRB(x) p.x = (bool)  t.getProperty (#x, p.x);
            #define GMRF(x) p.x = (float) t.getProperty (#x, p.x);
            #define GMRI(x) p.x = (int)   t.getProperty (#x, p.x);
            GMRB(eqEnabled)
            GMRB(chorusEnabled) GMRF(chorusRate) GMRF(chorusDepth) GMRF(chorusMix)
            GMRB(wahEnabled) GMRF(wahSensitivity) GMRF(wahRate) GMRF(wahLfoDepth)
            GMRF(wahBaseHz) GMRF(wahQ) GMRF(wahMix)
            GMRB(phaserEnabled) GMRF(phaserRate) GMRF(phaserDepth) GMRF(phaserFeedback) GMRF(phaserMix)
            GMRB(delayEnabled) GMRI(delayTimeSig) GMRI(delayDiv) GMRF(delayFeedback) GMRF(delayWet)
            // Absent in an older grex_funkey.xml, so these fall back to the
            // struct defaults above - which is the intended migration.
            GMRF(delayDry) GMRF(delayWetBase) GMRF(reverbWetBase)
            GMRB(reverbEnabled) GMRF(reverbSize) GMRF(reverbDamp) GMRF(reverbWet) GMRF(reverbPreDelay)
            GMRF(reverbDry) GMRF(reverbHpNorm) GMRF(reverbLpNorm)
            // TAIL falls back to SIZE, which used to drive the decay on its own,
            // so a family written before the split comes back unchanged.
            p.reverbTail = (float) t.getProperty ("reverbTail", (double) p.reverbSize);
            #undef GMRB
            #undef GMRF
            #undef GMRI
            for (int i = 0; i < 5; ++i)
            {
                p.eqGain[i] = (float) t.getProperty ("eqGain_" + juce::String (i), p.eqGain[i]);
                p.eqFreq[i] = (float) t.getProperty ("eqFreq_" + juce::String (i), p.eqFreq[i]);
            }
        }

    public:
        //----------------------------------------------------------------------
        // APPLICATION — "parallel and toggled" in one function.
        //
        // Every FX push already funnels through SoundsTab::commitSlotParamsToEngine
        // -> onSlotParamsChanged, so that is the single place a macro has to
        // intervene.  overrideFx() takes the slot's OWN parameters and returns
        // what should actually be sent:
        //
        //     macro off / family not covered  ->  p unchanged (private FX)
        //     macro on and family covered     ->  p with its six FX stages
        //                                         replaced by the family preset
        //
        // Nothing is written back into the slot's own params, so the private
        // settings survive untouched and toggling the macro off restores them
        // exactly — the whole point of the toggled design.  The caller simply
        // re-commits every slot when a macro flips.
        //
        // Only the six FX stages are swapped.  Envelopes, filter, LFOs, note
        // range, click, octave bias and everything else stay the slot's own:
        // the macro is a FX character, not a whole voice.
        //----------------------------------------------------------------------
        template <typename SlotParamsT>
        void overrideFx (SlotParamsT& p, int gmProgram, bool isDrumSlot) const
        {
            if (isDrumSlot)       return;                 // Big Drums handles those
            if (! funkeyOn.load()) return;                // private FX stay in charge

            // ── IT COPIES NOTHING NOW, AND THAT IS THE CHANGE ────────────────
            //
            //     STYLE INSTRUMENT  ->  FUNKEY  ->  CHANNEL EFFECTS
            //
            // Funkey's wah and phaser are a SEPARATE STAGE on the channel, ahead
            // of the instrument's own chain - see Channel::applyFunkeyInPlace.
            // They no longer overwrite the slot's own wah and phaser, so a slot
            // KEEPS its private pair while the macro is on and the two run in
            // series instead of one replacing the other.
            //
            // The function survives because it is the one place a caller asks
            // "does the macro want to change this slot", and a future macro that
            // DOES take a slot parameter over would live here.  Today it is a
            // deliberate no-op.
            juce::ignoreUnused (p, gmProgram);
        }

    private:
        std::array<FamilyFxParams, FunkeyFamilies::Count> fams {};
        std::atomic<bool> funkeyOn { false };
        std::atomic<float> funkeyMixVal { kFunkeyMixDefault };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalMacros)
    };
} // namespace Betel
