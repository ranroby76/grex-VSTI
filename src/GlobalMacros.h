#pragma once
//==============================================================================
// GlobalMacros.h — "FUNKEY MODE" and "BIG DRUMS".
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
// "all guitars" — the six-stage FX chain lives per channel — so a family preset
// is a LOOKUP applied whenever a slot loads an instrument in that family, and
// re-applied when the sounding program changes.  Seven families are covered,
// chosen as the ones that carry a funk arrangement:
//
//      0  Piano                 (GM   0.. 7)
//      1  Chromatic Percussion  (GM   8..15)
//      2  Organ                 (GM  16..23)
//      3  Guitar                (GM  24..31)
//     10  Synth Lead            (GM  80..87)
//     13  Ethnic                (GM 104..111)
//     14  Percussive            (GM 112..119)
//
// An instrument outside those seven keeps its own EQ / wah / phaser / delay
// even while Funkey Mode is on.
//
// Each family owns its WHOLE chain, chorus and reverb included.  A shared
// "glue" pair applied on top of every family was tried and removed: one room
// for seven families sounds tidy in principle, but it meant the chorus and
// reverb a family was tuned with were never the ones it played through, so
// tuning a family by ear was impossible.
//
// BIG DRUMS is simpler: one preset, applied to the rhythm slots, replacing the
// loaded kit's own FX rack for as long as it is engaged.
//
// PERSISTENCE
// -----------
// One file each, deliberately separate so the two can be shared, replaced or
// reverted independently:
//
//     grex_funkey.xml      the seven family presets
//     grex_bigdrums.xml    the drum preset
//
// Both are loaded at plugin start and written by the editors' SAVE buttons.
// They are NOT part of a set: a set records whether the macros are ENGAGED,
// while the macro presets themselves are global to the installation.  Loading
// someone else's set therefore cannot silently redefine what your Funkey Mode
// sounds like.
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
    inline juce::String displayNameFor (const juce::String& stem)
    {
        const int dot = stem.indexOfChar ('.');
        if (dot < 0) return stem;

        const auto head = stem.substring (0, dot).trim();
        return head.isNotEmpty() ? head : stem;
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

        static juce::File funkeyPreset()   { return root().getChildFile ("grex_funkey.xml");   }
        static juce::File bigDrumsPreset() { return root().getChildFile ("grex_bigdrums.xml"); }
        static juce::File favorites()      { return root().getChildFile ("favorites.xml");     }
        static juce::File registration()   { return root().getChildFile ("grex_registration.xml"); }
        static juce::File settings()       { return root().getChildFile ("grex_settings.xml"); }
        static juce::File styleLevels()    { return root().getChildFile ("grex_levels.xml");  }
        // The star on every style button.  Its own file rather than a corner of
        // grex_settings.xml: it is per-STYLE data about the library, not a
        // preference about the plugin, and it grows with the library.
        static juce::File styleFavorites() { return root().getChildFile ("grex_style_favorites.xml"); }
        // The two PLAYER-wide settings — chord mode and tempo free/synced.
        // Deliberately not part of a set: see MasterSettings.h.
        static juce::File master()         { return root().getChildFile ("grex_master.xml"); }
        // The MIDI CC learn map.  Its own file, NOT part of grex_master.xml:
        // that holds two flags about how the player plays, this is a HARDWARE
        // map with its own lifetime — re-learned when the controller changes.
        static juce::File ccMap()          { return root().getChildFile ("grex_cc_map.xml"); }
        static juce::File crashSettings()  { return root().getChildFile ("grex_crash.xml");   }
        static juce::File styleLog()       { return root().getChildFile ("grex_log.txt");      }
        static juce::File perfLog()        { return root().getChildFile ("grex_perf.txt");     }
        static juce::File drumLog()        { return root().getChildFile ("grex_drum.txt");     }
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
    // from grex_funkey.xml / grex_bigdrums.xml, not from these.
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
    // BIG DRUMS — the kit FX rack, mirroring DrumKitFxParams field-for-field
    // for the same reason FamilyFxParams mirrors the melodic slot params: the
    // push is a straight member-for-member copy, so neither side can drift as
    // the rack gains a stage.
    //
    // It is declared HERE rather than reusing DrumKitFxParams because that type
    // lives in InstrEditPanel.h, a GUI header — and this file is included by
    // Channel.cpp and StylePlayer.h, which must not drag juce_gui_basics into
    // the engine.  The mirror keeps GlobalMacros.h juce_core-only.
    //
    // DEFAULTS MATCH DrumKitFxParams exactly, so an unsaved Big Drums preset is
    // the same neutral rack a freshly-loaded kit gets: every stage off, unity
    // wet, centred.
    //==========================================================================
    struct BigDrumsFxParams
    {
        bool  eqEnabled   = false;
        bool  satEnabled  = false;
        bool  compEnabled = false;
        bool  revEnabled  = false;
        bool  delEnabled  = false;

        // 10-band EQ at 31/62/125/250/500/1k/2k/4k/8k/16k Hz, [-60..+20] dB.
        float eqGainDb [10] = { 0,0,0,0,0,0,0,0,0,0 };

        float satDrive     = 0.0f;      // 0..1
        float satMix       = 1.0f;      // 0..1

        float compThreshDb = 0.0f;      // -60..0 dB
        float compRatio    = 1.0f;      // 1..20
        float compAttackMs = 5.0f;      // 0.1..200 ms
        float compReleaseMs= 50.0f;     // 5..2000 ms
        float compMakeupDb = 0.0f;      // 0..24 dB

        // MUST MIRROR DrumKitFxParams FIELD FOR FIELD - overrideDrumFx copies
        // between them by name, so a field that exists on one side and not the
        // other is silently dropped on the way through.  It already was: revDry
        // lived on the kit rack and never on the Big Drums preset, so switching
        // the macro on reset every kit's reverb dry to 1.0 without saying so.
        float revSize      = 0.5f;      // 0..1
        float revDamp      = 0.5f;      // 0..1
        float revWet       = 0.0f;      // 0..1
        float revDry       = 1.0f;      // 0..1  — was MISSING, see above
        float revTail      = 0.5f;
        float revPreDelay  = 0.0f;
        float revHpNorm    = 0.2917f;
        float revLpNorm    = 1.0f;
        int   revAlgo      = 0;         // 0 = HALL/ROOM (FDN), 1 = PLATE
        float revWetBase   = 1.0f;

        bool  delSync      = false;     // false = free ms, true = tempo-synced
        int   delTimeSig   = 0;         // 0 = 4/4, 1 = 3/4
        int   delDiv       = 2;         // index into the active division table
        float delTimeMs    = 250.0f;    // 1..2000 ms (free mode)
        float delFb        = 0.3f;      // 0..0.95
        float delWet       = 0.0f;      // 0..1
        float delDry       = 1.0f;      // 0..1
        float delWetBase   = 0.5f;

        float pan          = 0.0f;      // -1..+1
        float fxWet        = 1.0f;      // rack dry/wet
    };

    //==========================================================================
    // The seven families Funkey Mode covers, as GM family indices (flag >> 3).
    //==========================================================================
    struct FunkeyFamilies
    {
        enum Slot { Piano = 0, ChromPerc, Organ, Guitar, SynthLead, Ethnic, Percussive, Count };

        /** GM family index (flag >> 3) for each covered slot. */
        static constexpr int gmFamily (int slot) noexcept
        {
            constexpr int f[Count] = { 0, 1, 2, 3, 10, 13, 14 };
            return (slot >= 0 && slot < Count) ? f[slot] : -1;
        }

        static const char* name (int slot) noexcept
        {
            switch (slot)
            {
                case Piano:      return "Piano";
                case ChromPerc:  return "Chromatic Percussion";
                case Organ:      return "Organ";
                case Guitar:     return "Guitar";
                case SynthLead:  return "Synth Lead";
                case Ethnic:     return "Ethnic";
                case Percussive: return "Percussive";
                default:         return "?";
            }
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
        bool isFunkeyOn()   const noexcept { return funkeyOn.load(); }
        bool isBigDrumsOn() const noexcept { return bigDrumsOn.load(); }
        void setFunkeyOn   (bool b) noexcept { funkeyOn.store (b); }
        void setBigDrumsOn (bool b) noexcept { bigDrumsOn.store (b); }

        //----------------------------------------------------------------------
        // Presets (message thread).
        //----------------------------------------------------------------------
        FamilyFxParams&       family (int slot)       { return fams[(size_t) clampSlot (slot)]; }
        const FamilyFxParams& family (int slot) const { return fams[(size_t) clampSlot (slot)]; }

        BigDrumsFxParams&       bigDrums()       { return drums; }
        const BigDrumsFxParams& bigDrums() const { return drums; }

        /** The drum rack that should govern the rhythm slots, or nullptr when
            Big Drums is off.  Same contract as presetForProgram: nullptr means
            "use the kit's own FX". */
        const BigDrumsFxParams* drumPreset() const
        {
            return bigDrumsOn.load() ? &drums : nullptr;
        }

        /** The family preset that should govern a given GM program, or nullptr
            when Funkey Mode is off or the program's family is not covered.
            Callers use nullptr as "use the instrument's own FX". */
        const FamilyFxParams* presetForProgram (int gmProgram) const
        {
            if (! funkeyOn.load()) return nullptr;
            const int slot = FunkeyFamilies::slotForProgram (gmProgram);
            return slot < 0 ? nullptr : &fams[(size_t) slot];
        }

        //----------------------------------------------------------------------
        // Files.  Separate on purpose — see the header note.
        //----------------------------------------------------------------------
        static juce::File funkeyFile()   { return GrexPaths::funkeyPreset();   }
        static juce::File bigDrumsFile() { return GrexPaths::bigDrumsPreset(); }

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
                const int slot = (int) f.getProperty ("slot", -1);
                if (slot >= 0 && slot < FunkeyFamilies::Count)
                    readFamily (f, fams[(size_t) slot]);
            }
        }

        /** Written by the Big Drums editor's SAVE button. */
        bool saveBigDrums() const
        {
            juce::ValueTree t ("GrexBigDrums");
            writeDrums (t, drums);
            return writeTree (bigDrumsFile(), t);
        }

        /** Read at plugin start.  Missing / malformed file leaves the defaults
            standing, exactly like loadFunkey. */
        void loadBigDrums()
        {
            const auto xml = juce::XmlDocument::parse (bigDrumsFile());
            if (xml == nullptr) return;
            const auto t = juce::ValueTree::fromXml (*xml);
            if (! t.isValid() || ! t.hasType ("GrexBigDrums")) return;
            readDrums (t, drums);
        }

    private:
        GlobalMacros() = default;

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

        static void writeDrums (juce::ValueTree& t, const BigDrumsFxParams& p)
        {
            #define GDW(x) t.setProperty (#x, p.x, nullptr);
            GDW(eqEnabled) GDW(satEnabled) GDW(compEnabled) GDW(revEnabled) GDW(delEnabled)
            GDW(satDrive) GDW(satMix)
            GDW(compThreshDb) GDW(compRatio) GDW(compAttackMs) GDW(compReleaseMs) GDW(compMakeupDb)
            GDW(revSize) GDW(revDamp) GDW(revWet) GDW(revDry)
            GDW(revTail) GDW(revPreDelay) GDW(revHpNorm) GDW(revLpNorm)
            GDW(revAlgo) GDW(revWetBase)
            GDW(delSync) GDW(delTimeSig) GDW(delDiv)
            GDW(delTimeMs) GDW(delFb) GDW(delWet) GDW(delDry) GDW(delWetBase)
            GDW(pan) GDW(fxWet)
            #undef GDW
            for (int i = 0; i < 10; ++i)
                t.setProperty ("eqGainDb_" + juce::String (i), p.eqGainDb[i], nullptr);
        }

        static void readDrums (const juce::ValueTree& t, BigDrumsFxParams& p)
        {
            #define GDRB(x) p.x = (bool)  t.getProperty (#x, p.x);
            #define GDRF(x) p.x = (float) t.getProperty (#x, p.x);
            #define GDRI(x) p.x = (int)   t.getProperty (#x, p.x);
            GDRB(eqEnabled) GDRB(satEnabled) GDRB(compEnabled) GDRB(revEnabled) GDRB(delEnabled)
            GDRF(satDrive) GDRF(satMix)
            GDRF(compThreshDb) GDRF(compRatio) GDRF(compAttackMs) GDRF(compReleaseMs) GDRF(compMakeupDb)
            GDRF(revSize) GDRF(revDamp) GDRF(revWet) GDRF(revDry)
            // Absent from an older grex_bigdrums.xml, so these fall back to the
            // struct defaults - tail 0.5, band wide open, no pre-delay, HALL.
            GDRF(revTail) GDRF(revPreDelay) GDRF(revHpNorm) GDRF(revLpNorm)
            GDRI(revAlgo) GDRF(revWetBase)
            GDRB(delSync) GDRI(delTimeSig) GDRI(delDiv)
            GDRF(delTimeMs) GDRF(delFb) GDRF(delWet) GDRF(delDry) GDRF(delWetBase)
            GDRF(pan) GDRF(fxWet)
            #undef GDRB
            #undef GDRF
            #undef GDRI
            for (int i = 0; i < 10; ++i)
                p.eqGainDb[i] = (float) t.getProperty ("eqGainDb_" + juce::String (i), p.eqGainDb[i]);
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

            // The family's whole chain, when this program's family is covered.
            // An instrument outside the seven keeps its private FX untouched.
            if (const FamilyFxParams* fx = presetForProgram (gmProgram))
            {
                p.eqEnabled = fx->eqEnabled;
                for (int i = 0; i < 5; ++i) { p.eqGain[i] = fx->eqGain[i]; p.eqFreq[i] = fx->eqFreq[i]; }

                p.chorusEnabled = fx->chorusEnabled; p.chorusRate = fx->chorusRate;
                p.chorusDepth   = fx->chorusDepth;   p.chorusMix  = fx->chorusMix;

                p.wahEnabled = fx->wahEnabled; p.wahSensitivity = fx->wahSensitivity;
                p.wahRate    = fx->wahRate;    p.wahLfoDepth    = fx->wahLfoDepth;
                p.wahBaseHz  = fx->wahBaseHz;  p.wahQ           = fx->wahQ;
                p.wahMix     = fx->wahMix;

                p.phaserEnabled  = fx->phaserEnabled;  p.phaserRate = fx->phaserRate;
                p.phaserDepth    = fx->phaserDepth;    p.phaserMix  = fx->phaserMix;
                p.phaserFeedback = fx->phaserFeedback;

                p.delayEnabled  = fx->delayEnabled;  p.delayTimeSig  = fx->delayTimeSig;
                p.delayDiv      = fx->delayDiv;      p.delayFeedback = fx->delayFeedback;
                p.delayWet      = fx->delayWet;      p.delayDry      = fx->delayDry;
                p.delayWetBase  = fx->delayWetBase;

                p.reverbEnabled  = fx->reverbEnabled;  p.reverbSize     = fx->reverbSize;
                p.reverbDamp     = fx->reverbDamp;     p.reverbWet      = fx->reverbWet;
                p.reverbPreDelay = fx->reverbPreDelay;
                p.reverbDry      = fx->reverbDry;      p.reverbTail     = fx->reverbTail;
                p.reverbHpNorm   = fx->reverbHpNorm;   p.reverbLpNorm   = fx->reverbLpNorm;
                p.reverbWetBase  = fx->reverbWetBase;
            }
        }

        //----------------------------------------------------------------------
        // BIG DRUMS — the drum-side twin of overrideFx, and the same contract:
        // takes the KIT's own rack and returns what should actually be sent.
        //
        //     macro off  ->  fx unchanged (the kit's private rack)
        //     macro on   ->  fx replaced wholesale by the Big Drums preset
        //
        // The caller passes a COPY, so the kit's own rack is never written and
        // switching Big Drums off restores it exactly.  Only the rack is
        // swapped — per-key gain / pitch / round-robin and the kit's samples
        // are untouched, because Big Drums is a rack character, not a kit.
        //----------------------------------------------------------------------
        template <typename DrumFxT>
        void overrideDrumFx (DrumFxT& fx) const
        {
            const BigDrumsFxParams* d = drumPreset();
            if (d == nullptr) return;                     // kit's own rack stays

            fx.eqEnabled   = d->eqEnabled;   fx.satEnabled = d->satEnabled;
            fx.compEnabled = d->compEnabled; fx.revEnabled = d->revEnabled;
            fx.delEnabled  = d->delEnabled;

            for (int i = 0; i < 10; ++i) fx.eqGainDb[i] = d->eqGainDb[i];

            fx.satDrive = d->satDrive;  fx.satMix = d->satMix;

            fx.compThreshDb  = d->compThreshDb;  fx.compRatio     = d->compRatio;
            fx.compAttackMs  = d->compAttackMs;  fx.compReleaseMs = d->compReleaseMs;
            fx.compMakeupDb  = d->compMakeupDb;

            fx.revSize     = d->revSize;      fx.revDamp    = d->revDamp;
            fx.revWet      = d->revWet;       fx.revDry     = d->revDry;
            fx.revTail     = d->revTail;      fx.revPreDelay= d->revPreDelay;
            fx.revHpNorm   = d->revHpNorm;    fx.revLpNorm  = d->revLpNorm;
            fx.revAlgo     = d->revAlgo;      fx.revWetBase = d->revWetBase;

            fx.delSync    = d->delSync;     fx.delTimeSig = d->delTimeSig;
            fx.delDiv     = d->delDiv;      fx.delTimeMs  = d->delTimeMs;
            fx.delFb      = d->delFb;       fx.delWet     = d->delWet;
            fx.delDry     = d->delDry;      fx.delWetBase = d->delWetBase;

            fx.pan   = d->pan;
            fx.fxWet = d->fxWet;
        }

    private:
        std::array<FamilyFxParams, FunkeyFamilies::Count> fams {};
        BigDrumsFxParams                                  drums {};
        std::atomic<bool> funkeyOn   { false };
        std::atomic<bool> bigDrumsOn { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalMacros)
    };
} // namespace Betel


