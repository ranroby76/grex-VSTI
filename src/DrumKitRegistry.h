
#pragma once
//==============================================================================
// DrumKitRegistry.h
//
// Owns the set of *_kit.frb blobs loaded by the plugin and exposes a flat
// catalog of every drum element across every kit, so the DrumsPopup per-key
// element picker can offer "all kicks across all kits" with one query.
//
// LIFECYCLE
//   1.  Plugin startup: instantiate one registry, call scanFolder() on the
//       user's kits folder.  Each *_kit.frb gets memory-mapped via BlobReader
//       (cheap — no sample data is read upfront, just the metadata).
//   2.  Runtime: UI calls getElementsForRole() / getElementsForKit() etc. to
//       build dropdowns and load samples on demand.
//   3.  Shutdown: registry destruction unmaps every loaded blob.
//
// THREAD SAFETY
//   - scanFolder / loadKit / unloadAll: message thread only.
//   - All read-only queries: safe from any thread after a scan completes,
//     as long as no concurrent scanFolder is in flight.  The registry does
//     NOT hot-add kits at runtime from the audio thread.
//
// KIT NAMING
//   A loaded kit's display name is taken from the FIRST preset inside the
//   blob (preset.name).  If the preset name is empty, falls back to the
//   filename stem with "_kit" stripped (so "pop_kit.frb" → "pop").
//
// NOTE
//   Blob files older than v3 (legacy melodic presets) are still openable but
//   every region's elementRoleId is 0 (Unset). Such blobs contribute zero
//   entries to the element catalog and are silently skipped.
//==============================================================================

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <memory>
#include <map>

#include "FullKitMap.h"
#include <unordered_map>
#include <vector>
#include "BlobReader.h"
#include "BlobFormat.h"
#include "DrumElementRoles.h"

// NOTE: std::hash<juce::String> is already specialised by JUCE itself in
// juce_core/text/juce_String.h, so unordered_map<juce::String, …> works out
// of the box.  Do NOT add another specialisation here — MSVC rejects it as
// a duplicate explicit specialisation (C2766).

namespace Betel
{
    class DrumKitRegistry
    {
    public:
        /** The XG sub-GM zone: the whole span of keys that sit below GM.

            Yamaha XG kits map below GM's floor of 35, from Surdo Mute at 13 up
            to Open Rim Shot at 34 — twenty-two keys.

            THESE TWO CONSTANTS NO LONGER DESCRIBE ANY LIVE COMPONENT.  One blob
            (the_first) used to carry the whole span; it has been split three
            ways and deleted — see the split below, whose four constants are what
            the live components actually use.  This pair survives only as the
            zone of legacy the_first, for a library that has not been re-blobbed.

            Anything a blob holds outside its own zone belongs to a kit's own
            components and is dropped at catalog time; see the clamp in
            loadBlob. */
        static constexpr int kXgLowZoneLoKey = 13;
        static constexpr int kXgLowZoneHiKey = 34;

        /** The XG upper-percussion zone: the span the_last is allowed to supply.

            Bongo H at 60 up to Jingle Bell at 83 — twenty-four keys.  Note the
            top is 83, not GM's 81: keys 82 Shaker and 83 Jingle Bell are GM2 /
            XG percussion that Yamaha styles use heavily (one Korg conversion put
            331 hits on key 82 alone), and the_last is what supplies them. */
        static constexpr int kXgHighZoneLoKey = 60;
        static constexpr int kXgHighZoneHiKey = 83;

        //======================================================================
        // THE SUB-GM ZONE IS SPLIT IN TWO, AND ONE HALF IS KIT-DEPENDENT
        //
        // A Revo! kit (bank LSB 8) does not use the XG map below 29.  Yamaha's
        // Drum Kit Assign List puts hi-hat articulations on 13..22 and a
        // tambourine plus three no-rim snares on 25..28, where a Standard kit
        // has surdos, scratches, metronome clicks and brush taps.  One global
        // component cannot serve both readings, so the_first has been broken up:
        //
        //     13..22   KIT-DEPENDENT   revo_first | gm_first
        //     23..24   COMMON          the_second   (Seq Click L/H — identical)
        //     25..28   KIT-DEPENDENT   revo_first OVERRIDES the_second
        //     29..34   COMMON          the_second   (kick and snare — compatible)
        //
        // revo_first's span is therefore 13..28 and DELIBERATELY NOT CONTIGUOUS
        // in content: its blob simply holds nothing at 23..24, and the clamp
        // below only ever DISCARDS regions outside a span — it never demands
        // that one be filled.  So a span is the right shape for it even though
        // two keys in the middle belong to somebody else.
        //
        // gm_first stops at 22 because the XG reading of 25..28 is what
        // the_second already carries; a GM install needs no second opinion.
        //
        // ON OCTAVE CORRECTION.  An earlier version of this note claimed the
        // correction only fires when EVERY key sits outside the zone, and warned
        // that a +12 revo_first export would therefore go uncorrected.  BOTH
        // CLAIMS WERE WRONG.  The test below is on the SPAN — blobLo and blobHi
        // against the zone — so a +12 export of any of these three lands wholly
        // outside its zone and is corrected:
        //
        //     revo_first  +12 -> 25..34, shifts to 13..22
        //     gm_first    +12 -> 25..34, shifts to 13..22
        //     the_second  +12 -> 35..46, shifts to 23..34
        //
        // The one case that genuinely will not self-correct is a blob whose span
        // is WIDER than its zone: no single octave brings it fully inside, the
        // shift stays 0, and the clamp discards whatever falls outside.  That
        // shows up as missing keys, not as keys in the wrong place.
        //======================================================================
        static constexpr int kLowSplitKitLoKey    = 13;   // revo_first / gm_first floor
        static constexpr int kLowSplitKitHiKey    = 22;   // gm_first ceiling
        static constexpr int kRevoExtraHiKey      = 28;   // revo_first also owns 25..28
        static constexpr int kLowSplitCommonLoKey = 23;   // the_second floor
        static constexpr int kLowSplitCommonHiKey = 34;   // the_second ceiling

        //======================================================================
        // KEY 33 — THE SOFT KICK — HAS ITS OWN TWO BLOBS
        //
        // XG's key 33 is BASS DRUM SOFT, and it is not a garnish: measured across
        // the 830-style library, 214 styles play it and in 118 of them it is the
        // ONLY kick in the section.  the_second used to carry one generic soft
        // kick there for every kit, so a Dance-kit style whose four-on-the-floor
        // lives on 33 played an acoustic soft kick under a synth groove.
        //
        // the_second is now re-sampled WITHOUT 33, and the key is served by one
        // of two blobs that live in the ordinary kick folder:
        //
        //     kick/33_soft_kick.frb   every kit that is not electronic
        //     kick/33_edm_kick.frb    the electronic / analog family
        //
        // WHICH ONE IS DECIDED BY THE COMPOSED KIT, not by the requested program.
        // drumFamilyFor already folds every requested kit onto one of the nine
        // composed families, and the electronic ones fold onto exactly two:
        // 024 Electro (Dance, Break, House, EDM, Trap, Dubstep, Hit, 80s R&B,
        // PSR-E Dance/House, and HipHop - drumKitPCMap sends PC 56 there) and
        // 025 TR-808 (Analog, Analog T8/T9, Drum Machine).  Keying the choice on
        // those two means the soft-kick choice can never disagree with the kit
        // the channel actually builds - there is one table deciding both, not
        // two that could drift.
        //
        // NOT "056".  The composed 056 kit is an SFX kit that only a hand pick
        // reaches (no style program change points at it, per drumKitPCMap), so
        // it stays on the soft kick it always had from the_second.  Revo! and
        // Ambient styles (drum bank LSB 8/9) all compose onto 000 Standard via
        // revoFallbackFamily, so they take the soft kick too.
        //
        // THEY ARE GLOBALS, NOT KIT COMPONENTS, DESPITE THE FOLDER.  Their stems
        // start with a number, and the scan names a kit by its numeric prefix,
        // so without the special case below both files would join a phantom
        // kit "33" - and 33_soft_kick is authored at key 45 (+12, the Genos
        // Kontakt export offset), which the unzoned 35..81 window would accept
        // as that phantom kit's LOW TOM.  They are recognised by stem, zoned to
        // exactly [33, 33], octave-corrected by the same detector the low-zone
        // blobs use, and kept out of the REPLACE droplists.
        //======================================================================
        static constexpr int kSoftKickKey = 33;

        static bool isSoftKickStem (const juce::String& stem) noexcept
        {
            return stem.trim().equalsIgnoreCase ("33_soft_kick");
        }

        static bool isEdmKickStem (const juce::String& stem) noexcept
        {
            return stem.trim().equalsIgnoreCase ("33_edm_kick");
        }

        static bool isKick33Stem (const juce::String& stem) noexcept
        {
            return isSoftKickStem (stem) || isEdmKickStem (stem);
        }

        /** True when the COMPOSED kit (drumFamilyFor's answer: "000".."056")
            is one of the two electronic families, and so takes 33_edm_kick. */
        static bool usesEdmKick33 (const juce::String& composedKitName) noexcept
        {
            const auto k = composedKitName.trim();
            return k == "024" || k == "025";
        }

        /** How a GLOBAL component relates to the kit-type split above.

            The registry catalogs components at scan time, when nothing is known
            about which style will load.  Revo-ness is a property of the STYLE
            (its drum bank LSB), so the choice between revo_first and gm_first
            cannot be made here — it is made at kit-composition time in
            SamplePlayerEngine::resolveDrumKitParams, which asks this. */
        enum class LowZoneKind
        {
            NotLowZone,   // low_kick, the_lasts, clap ... nothing to do with 13..34
            KitAgnostic,  // the_second (and legacy the_first): merged for every kit
            RevoOnly,     // revo_first: merged ONLY when the style asked for a Revo! kit
            GmOnly,       // gm_first:   merged ONLY when it did not
            SoftKick33,   // 33_soft_kick: key 33 for every NON-electronic composed kit
            EdmKick33     // 33_edm_kick:  key 33 for the electronic families (usesEdmKick33)
        };

        //======================================================================
        // KEYS A COMPONENT MAPS BUT MUST NOT CONTRIBUTE
        //
        // the_last carries the whole 60..83 upper-percussion span, and two of
        // those drums are unwanted in a ballad library: MIDI 78 Mute Cuica and
        // 79 Open Cuica.  They are the 19th and 20th keys the blob maps, the
        // blob being all 24 keys of its zone in order.
        //
        // ── SILENCED BY KEY, NOT BY POSITION.  THIS MATTERS ──────────────────
        //
        // There was once a COUNT cap here - "keep the 18 lowest keys" - and it
        // was removed because a positional rule silences whatever happens to sit
        // at that position.  When the blob was re-sampled from 18 keys to 24, the
        // same cap started cutting 78..83 and took key 82 Shaker with it, the
        // single most-used key above the GM ceiling across the whole style
        // library.  Nothing announced that; the Shaker simply stopped.
        //
        // Naming the two keys outright cannot drift that way.  Re-sample the
        // blob, add keys, reorder it - 78 and 79 are still the two Cuicas, and
        // everything else still sounds.
        //
        // ── AND IT IS DONE AT CATALOG TIME, BESIDE THE ZONE CLAMP ────────────
        //
        // So the editor and the engine agree.  Dropping these at MERGE time
        // instead would leave REST GM showing two keys that can never sound -
        // sliders that write nowhere, which is the fault the LOW KICK page was
        // fixed for.  A key the catalog does not carry is absent from both.
        //======================================================================
        static bool isSilencedComponentKey (const juce::String& componentName,
                                            int key) noexcept
        {
            const auto n = componentName.trim();
            if (! (n.equalsIgnoreCase ("the_last") || n.equalsIgnoreCase ("the_lasts")))
                return false;

            return key == 78 || key == 79;   // Mute Cuica / Open Cuica
        }

        static LowZoneKind lowZoneKindFor (const juce::String& componentName)
        {
            const auto n = componentName.trim();
            auto is = [&n] (const char* a, const char* b)
            {
                return n.equalsIgnoreCase (a) || n.equalsIgnoreCase (b);
            };

            if (is ("revo_first", "revo_firsts")) return LowZoneKind::RevoOnly;
            if (is ("gm_first",   "gm_firsts"))   return LowZoneKind::GmOnly;
            if (is ("the_second", "the_seconds")) return LowZoneKind::KitAgnostic;

            // The two key-33 blobs.  Their catalog name is the full stem (see the
            // special case in loadKit), so this is the name that arrives here.
            if (isSoftKickStem (n)) return LowZoneKind::SoftKick33;
            if (isEdmKickStem  (n)) return LowZoneKind::EdmKick33;

            // LEGACY.  the_first was the single component covering the whole of
            // 13..34 before the split.  It is still recognised so an install
            // that has not been re-blobbed keeps working, but it is merged AFTER
            // the new trio (see resolveDrumKitParams), so where both are present
            // the split wins and the_first only fills what nothing else did.
            if (is ("the_first",  "the_firsts"))  return LowZoneKind::KitAgnostic;

            return LowZoneKind::NotLowZone;
        }

        //======================================================================
        // ElementHandle — one entry in the flat element catalog.  Identifies
        // exactly one region inside one preset inside one loaded blob, plus
        // pretty-printing metadata for the UI.
        //
        // Lifetime: valid until the registry is destroyed or unloadAll() is
        // called.  The `reader` pointer is non-owning and lives in the
        // registry's `kits` vector.
        //======================================================================
        struct ElementHandle
        {
            juce::String       kitName;        // e.g. "Pop Kit"
            /** The COMPONENT FOLDER this element was read from, lower-cased:
                "kick", "snare", "stick", "metal", "tom", "the_last"...

                This was the missing link.  loadKit derives its name from the
                FILE STEM, so kick/000_standard.frb and snare/000_standard.frb
                both come back as "000" - which is deliberate (it is how the
                per-kit folders join into one virtual kit) but left nothing
                downstream able to say which folder an element belongs to.  An
                editor cannot point at the right folder if the element does not
                know its own. */
            juce::String       component;
            juce::String       displayName;    // "Pop Kit - Kick" for dropdowns
            uint8_t            roleId   = 0;
            int                midiKey  = -1;  // original key assignment in source blob
            BlobReader*        reader   = nullptr;
            int                presetIndex = -1;
            int                regionIndex = -1;
        };

        DrumKitRegistry()  = default;
        ~DrumKitRegistry() = default;

        //======================================================================
        // scanFolder — RECURSIVELY load every *.frb under `folder` (e.g.
        // <root>/sounds/drums, with per-role subfolders kick/ snare/ stick/
        // metal/ tom/ "the last"/ ...).  Each blob is a drum COMPONENT (one or
        // more role-tagged regions); its kit "name" is the PPP program-change
        // prefix of the filename (text before the first '_'), so every
        // "000_*.frb" across all role folders joins one virtual "000" kit.
        // The family word after '_' is human-readable only.  Returns the number
        // of component blobs indexed.  A blob that fails to open OR carries no
        // role-tagged region (e.g. a stray melodic file) is silently skipped.
        //======================================================================
        int scanFolder (const juce::File& folder, const juce::String& accessCode)
        {
            if (! folder.isDirectory()) return 0;

            int loaded = 0;
            juce::Array<juce::File> files;
            folder.findChildFiles (files, juce::File::findFiles, true, "*.frb");

            //------------------------------------------------------------------
            // COMPONENT INDEX — built from the FOLDER STRUCTURE, before any of
            // the skips below.
            //
            // This has to be independent of what becomes a "kit", because two
            // of the folders never do: clap/ and kickmix/ are `continue`d a
            // few lines down and loaded as pinned globals.  Indexing off the
            // kit list would therefore leave those editors pointing at nothing.
            // (low_kick/ was on this list until the sub-kick moved inside the
            // kick blobs; the_first/ was on it until its skip was removed and
            // it became a real global component.)
            //
            // Indexing off the directory gives every folder the same treatment:
            // what files are in it, and which kit each one belongs to.  That is
            // exactly the two questions an editor asks - "what folder am I" and
            // "what else could go here".
            //------------------------------------------------------------------
            componentIndex.clear();
            for (const auto& f : files)
            {
                const auto comp = f.getParentDirectory().getFileName().trim().toLowerCase();
                if (comp.isEmpty()) continue;

                // The key-33 blobs sit in kick/ but are not a kit's kick: their
                // numeric "33" prefix would list them in the kick REPLACE droplist
                // as a kit called "33", and picking it would re-source KICK and
                // LOW KICK onto a file that carries neither.  They are chosen per
                // kit family, never swapped, so they are not an alternative.
                if (isKick33Stem (f.getFileNameWithoutExtension())) continue;

                auto stem = f.getFileNameWithoutExtension();
                const int us = stem.indexOfChar ('_');
                const juce::String prefix = (us > 0) ? stem.substring (0, us) : stem;
                const bool numericKit = prefix.isNotEmpty()
                                     && prefix.containsOnly ("0123456789");

                componentIndex[comp].push_back ({ (numericKit ? prefix : stem)
                                                      .trim().toLowerCase(), f });
            }

            for (auto& c : componentIndex)
            {
                std::sort (c.second.begin(), c.second.end(),
                           [] (const ComponentFile& a, const ComponentFile& b)
                           { return a.kitKey < b.kitKey; });
            }
            for (const auto& f : files)
            {
                // clap.frb is a FORCED global (pinned to a fixed note for every
                // kit) -- loaded separately below, never as a standalone kit,
                // and never via the role-tagged element catalog.
                if (f.getFileName().equalsIgnoreCase ("clap.frb"))     continue;
                // low_kick.frb WAS skipped here.  The sub-kick is no longer a
                // shared file: every re-sampled kick blob carries its own note
                // 35 beside its note 36, so there is nothing left to pin and
                // nothing to skip.
                // the_first.frb WAS skipped here, back when the sub-GM keys were
                // filled by cloning from the kit itself.  The skip was removed
                // when a real blob replaced the clones, and that blob has since
                // been split into revo_first / gm_first / the_second — all three
                // GLOBAL, all catalogued through this same path, none skipped.
                // kickmix/EDM.frb + WOOD.frb are supplemental crossfade layers
                // for notes 35/36, not kits -- loaded separately below.
                if (f.getParentDirectory().getFileName().equalsIgnoreCase ("kickmix")) continue;
                if (loadKit (f, accessCode)) ++loaded;
            }

            // Decode the shared forced globals once.
            loadGlobalClap    (folder, accessCode);   // <drums>/clap/clap.frb        -> note 39
            loadGlobalKickMix (folder, accessCode);   // <drums>/kickmix/{EDM,WOOD}.frb -> layered on 35/36
            return loaded;
        }

        //======================================================================
        // loadKit — open a single *_kit.frb, catalog every region in every
        // preset whose role is not Unset.  Returns false if the file fails
        // to open OR yields no usable elements.
        //
        // Idempotent on filename: if the same path is loaded twice, the
        // previous catalog entries are replaced.
        //======================================================================
        bool loadKit (const juce::File& file, const juce::String& accessCode)
        {
            if (! file.existsAsFile()) return false;

            // A FULL SAMPLED KIT IS NOT A COMPONENT.
            //
            // "127-000-048_Symphony Kit.frb" sits in this same folder, and its
            // stem has no NUMERIC prefix before the underscore - so without this
            // it would be catalogued as a GLOBAL component and layered onto
            // every GM kit in the library.  FullKitMap owns those files; the
            // registry must not even open them.
            if (FullKitMap::isFullKitName (file.getFileNameWithoutExtension()))
            {
                return false;
            }

            // Drop any pre-existing entry with the same source path.
            unloadKitByFile (file);

            auto info = std::make_unique<KitInfo>();
            info->file   = file;
            info->reader = std::make_unique<BlobReader>();
            if (! info->reader->open (file.getFullPathName().toStdString(),
                                      accessCode.toStdString()))
            {
                lastError = "Failed to open " + file.getFileName()
                          + ": " + juce::String (info->reader->getError());

                return false;
            }

            // Resolve the kit key from the FILENAME.  Two cases:
            //
            //  PER-KIT components — files named PPP_Family.frb whose prefix
            //  (before the first '_') is the 3-digit program-change number.
            //  Keyed by that number, so one component per role across the
            //  kick/ snare/ stick/ metal/ tom/ folders joins one virtual kit:
            //    kick/000_standard.frb + snare/000_standard.frb + ...
            //      -> all name == "000"  (matches drumKitPCMap[0] = "000").
            //
            //  GLOBAL components — files whose prefix is NOT numeric (e.g.
            //  the_last/the_lasts.frb).  These are loaded for EVERY kit
            //  regardless of program change (the_lasts = all percussion above
            //  key 59).  Keyed by the full filename stem and flagged isGlobal so
            //  programChangeDrum can merge them into every assembled kit.
            //
            // The family word after '_' is human-readable only; the internal
            // preset name is intentionally ignored so the builder can't break
            // grouping.
            const auto& presets = info->reader->getPresets();
            {
                auto stem = file.getFileNameWithoutExtension();
                const int us = stem.indexOfChar ('_');
                const juce::String prefix = (us > 0) ? stem.substring (0, us) : stem;
                // The key-33 blobs are GLOBALS despite their numeric prefix:
                // "33_soft_kick" must not collapse onto a kit named "33".  See
                // the note beside kSoftKickKey.
                const bool numericKit = prefix.isNotEmpty()
                                     && prefix.containsOnly ("0123456789")
                                     && ! isKick33Stem (stem);
                info->isGlobal = ! numericKit;
                info->name = (numericKit ? prefix : stem).trim().toLowerCase();
                if (info->name.isEmpty())
                    info->name = stem.toLowerCase();

                // THE FOLDER IS THE COMPONENT.  kick/, snare/, stick/, metal/,
                // tom/, the_last/ ... captured here because it is the only place
                // it is still known: `name` above deliberately collapses every
                // folder's 000_*.frb onto "000", which is what joins them into
                // one virtual kit and also what erased the distinction.
                info->component = file.getParentDirectory().getFileName()
                                      .trim().toLowerCase();
            }

            // ── A ZONED COMPONENT OWNS ITS SPAN AND NOTHING ELSE ─────────────
            //
            // Written when one blob covered the whole sub-GM span; it now reads
            // for all four zoned components, which is what the clamp below has
            // always actually enforced.  Beyond that zone it would also
            // carry kick, side stick, snare, hand clap, toms and hats.  As a
            // GLOBAL that is offered to every kit, and the merge in
            // resolveDrumKitParams gap-fills: the kit's own element wins where
            // it has one, the global lands where it does not.  So extra keys are
            // discarded on a complete kit and FILLED IN on an incomplete one —
            // which quietly puts Standard's snare on a Brush kit and Standard's
            // hats on an Electro kit.  Kit identity is the whole point of having
            // nine kits.
            //
            // Clamped HERE, at catalog time, rather than at the merge: the
            // editor's keysForComponent reads the registry directly, so a merge-
            // only clamp would leave the UNDER GM page showing controls for keys
            // that cannot sound.  One place, one answer.
            //
            // This is the mirror of the_lasts' kTheLastsMaxNotes cap, which
            // exists for exactly the same reason at the other end of the kit.
            // A range is used rather than a count because this component's job
            // is a fixed span of the XG map, not "however many it happens to
            // hold".
            // A component whose job is a FIXED SPAN of the map declares it here.
            // Everything else (kick, snare, stick, metal, tom, clap, kickmix)
            // has no zone and is left exactly as it was.
            // MATCHED ON THE FOLDER *OR* THE BLOB'S OWN STEM, both spellings.
            //
            // Keying this on the folder name alone was too fragile: a blob that
            // is not in a folder of exactly that name falls to the UNZONED path,
            // which takes the raw 35..81 window with no shift and no clamp - so
            // a +12 sub-GM export's raw keys 35..46 are accepted at face value
            // and land on the kit's toms and hi-hats. Key 41 is a SNARE ROLL and
            // 44 is STICKS, which is a screech on a tom and a scratch on a hat.
            //
            // Silent, and it only shows on kits with a hole there, so it reads
            // as a random bad sample rather than a mapping fault. Accept every
            // spelling instead: the folder (the_first / the_last) and the blob
            // stem (the_firsts / the_lasts, and the singular forms).
            const auto zoneTag = [&info] (const char* folder, const char* stem)
            {
                return info->component.equalsIgnoreCase (folder)
                    || info->name     .equalsIgnoreCase (folder)
                    || info->name     .equalsIgnoreCase (stem);
            };

            int zoneLo = -1, zoneHi = -1;
            if      (zoneTag ("the_first", "the_firsts"))
            {   zoneLo = kXgLowZoneLoKey;      zoneHi = kXgLowZoneHiKey;      }
            else if (zoneTag ("the_second", "the_seconds"))
            {   zoneLo = kLowSplitCommonLoKey; zoneHi = kLowSplitCommonHiKey; }
            else if (zoneTag ("revo_first", "revo_firsts"))
            {   zoneLo = kLowSplitKitLoKey;    zoneHi = kRevoExtraHiKey;      }
            else if (zoneTag ("gm_first",   "gm_firsts"))
            {   zoneLo = kLowSplitKitLoKey;    zoneHi = kLowSplitKitHiKey;    }
            else if (zoneTag ("the_last",  "the_lasts"))
            {   zoneLo = kXgHighZoneLoKey;     zoneHi = kXgHighZoneHiKey;     }
            // The key-33 blobs own exactly one key.  A zone of [33, 33] is what
            // lets the octave detector below repair a +12 export by itself:
            // 33_soft_kick authored at 45 lies outside the zone and lands inside
            // after -12, so it moves; a blob authored at 33 is already inside,
            // fails the detector's first test, and stays exactly where it is.
            // Neither file needs to announce which kind of export it is.
            else if (isKick33Stem (info->name))
            {   zoneLo = kSoftKickKey;         zoneHi = kSoftKickKey;         }
            const bool hasZone = (zoneLo >= 0);

            // ── OCTAVE CORRECTION, DETECTED RATHER THAN ASSUMED ───────────────
            //
            // The first the_first export was authored an octave high: its keys
            // ran 25..46 where the XG zone is 13..34.  The sample names track the
            // written keys, so the offset is in the export, not in the audio.
            //
            // A hard-coded -12 would become a trap the day the exporter is
            // fixed — a correct blob would then be shifted DOWN into 1..22.  So
            // the shift is derived instead: it applies only when EVERY key the
            // blob maps sits outside the zone AND lands inside it after the
            // shift.  A correctly authored blob is already inside the zone, the
            // test fails on the first condition, and nothing moves.
            //
            // Only whole octaves are considered, and only for this component.
            int zoneKeyShift = 0;
            if (hasZone)
            {
                int blobLo = 128, blobHi = -1;
                for (const auto& p : presets)
                    for (const auto& r : p.regions)
                    {
                        const int a = (int) r.keyRangeLow, b = (int) r.keyRangeHigh;
                        const int kk = (a == b) ? a : (int) r.rootKey;
                        if (kk < 0 || kk > 127) continue;
                        blobLo = juce::jmin (blobLo, kk);
                        blobHi = juce::jmax (blobHi, kk);
                    }

                if (blobHi >= blobLo
                    && ! (blobLo >= zoneLo && blobHi <= zoneHi))
                {
                    // Closest octave first, both directions.  The guard is what
                    // makes this safe: the shift is taken only if it brings the
                    // WHOLE span inside the zone, so a correctly authored blob
                    // fails at the `already inside` test above and never moves.
                    for (const int oct : { -12, 12, -24, 24, -36, 36, -48, 48 })
                        if (blobLo + oct >= zoneLo && blobHi + oct <= zoneHi)
                        {
                            zoneKeyShift = oct;
                            break;
                        }
                }
            }

            // Walk every preset / region; catalog only regions with a real role.
            int cataloged = 0;
            for (int pi = 0; pi < (int) presets.size(); ++pi)
            {
                const auto& preset = presets[(size_t) pi];
                for (int ri = 0; ri < (int) preset.regions.size(); ++ri)
                {
                    const auto& region = preset.regions[(size_t) ri];

                    // ── Resolve the GM key of this drum element ──────────────
                    // v1/v2 blobs carry no elementRoleId and may leave rootKey
                    // unset, so derive the key in priority order:
                    //   1. single-key range (lokey == hikey) — how drum elements
                    //      are authored (one key per element);
                    //   2. rootKey;
                    //   3. otherwise skip the region.
                    //
                    // THE ACCEPTANCE WINDOW DEPENDS ON WHETHER THE COMPONENT HAS
                    // A ZONE, AND IT MUST BE TESTED AFTER THE OCTAVE SHIFT.
                    //
                    // A zoned component is filtered by its zone, so the raw
                    // window has to be wide open: the_last's blob is authored at
                    // 72..95 and a raw window ending at 81 would throw away keys
                    // 84..95 — half the component — BEFORE the shift that makes
                    // them legal ever ran.  Order matters here, not just range.
                    //
                    // An unzoned component (kick, snare, stick, metal, tom …)
                    // keeps the original 35..81 GM span exactly, so nothing
                    // about those changes.
                    //
                    // Role follows the key unless the blob explicitly tagged one
                    // (v3+), and it is resolved by getGMRoleForMidiKey rather
                    // than by `key - 34` here — that arithmetic is only valid
                    // for 35..83 and runs NEGATIVE below it, which a uint8_t
                    // cast then wraps into a garbage role.
                    const int rawLo = hasZone ?   0 : 35;
                    const int rawHi = hasZone ? 127 : 81;

                    const int lo = (int) region.keyRangeLow;
                    const int hi = (int) region.keyRangeHigh;
                    const int rk = (int) region.rootKey;
                    int key = -1;
                    if (lo == hi && lo >= rawLo && lo <= rawHi)      key = lo;
                    else if (rk >= rawLo && rk <= rawHi)             key = rk;
                    if (key < 0) continue;   // unmappable -> skip

                    // Octave correction, then the zone.  Both are no-ops for a
                    // component with no zone, and the shift is 0 for a blob
                    // already authored inside its zone.
                    key += zoneKeyShift;

                    if (hasZone && (key < zoneLo || key > zoneHi))
                        continue;

                    // Named keys this component must not contribute — applied
                    // AFTER the octave correction, so it is the key the player
                    // actually hears that is tested, not the key as authored.
                    if (isSilencedComponentKey (info->component, key)
                        || isSilencedComponentKey (info->name, key))
                        continue;

                    int roleId = region.elementRoleId;
                    if (roleId == 0)
                        roleId = (int) DrumRoles::getGMRoleForMidiKey (key);
                    if (roleId == 0) continue;   // no role -> nothing to catalog

                    // ── EVERY velocity layer, not just the loudest ───────────
                    // A velocity-layered blob carries SEVERAL regions per key --
                    // soft / medium / hard samples of the same drum.  This used to
                    // keep only the layer with the highest velRangeHigh and throw
                    // the rest away, because composeDrumKit mapped a single region
                    // per key.
                    //
                    // The effect was exactly what it sounds like: a ghost note at
                    // velocity 30 played the FULL-FORCE sample, merely turned down.
                    // Level moved a little; timbre never moved at all.  That is why
                    // the drums sounded monotonically loud.
                    //
                    // Now every layer is cataloged and composeDrumKit builds one
                    // engine Region per layer, honouring each layer's
                    // velRangeLow..velRangeHigh, so findRegion() picks the sample
                    // the incoming velocity actually asks for.
                    ElementHandle h;
                    h.kitName     = info->name;
                    h.component   = info->component;
                    h.roleId      = (uint8_t) roleId;
                    h.midiKey     = key;
                    h.reader      = info->reader.get();
                    h.presetIndex = pi;
                    h.regionIndex = ri;
                    h.displayName = info->name + " - "
                                  + juce::String (DrumRoles::getName (roleId));

                    info->elements.push_back (h);
                    ++cataloged;
                }
            }

            if (cataloged == 0)
            {
                lastError = file.getFileName()
                          + " has no regions tagged with a drum role (v3+ needed).";
                return false;
            }

            kits.push_back (std::move (info));
            rebuildFlatCatalog();
            return true;
        }

        /** Forget every loaded kit. Unmaps all blob files. */
        void unloadAll()
        {
            kits.clear();
            allElements.clear();
            componentIndex.clear();
            roleIndex.clear();
            kitNameIndex.clear();
        }

        //======================================================================
        // Read-only queries
        //======================================================================
        const std::vector<ElementHandle>& getAllElements() const noexcept
        {
            return allElements;
        }

        std::vector<juce::String> getLoadedKitNames() const
        {
            std::vector<juce::String> names;
            names.reserve (kits.size());
            for (const auto& k : kits) names.push_back (k->name);
            return names;
        }

        int getKitCount() const noexcept { return (int) kits.size(); }

        //======================================================================
        // COMPONENT QUERIES — what an editor needs to point at its own folder.
        //======================================================================

        /** Every component folder found under <root>/sounds/drums, sorted.
            One editor per entry: clap, gm_first, kick, kickmix, metal,
            revo_first, snare, stick, the_last, the_second, tom.

            NOT a fixed list - it is whatever the scan found, so a library
            missing a folder simply has no page for it. */
        std::vector<juce::String> getComponentNames() const
        {
            std::vector<juce::String> out;
            out.reserve (componentIndex.size());
            for (const auto& c : componentIndex) out.push_back (c.first);
            std::sort (out.begin(), out.end());
            return out;
        }

        /** The kits that have a file in this folder - i.e. what the REPLACEMENT
            droplist should offer.  Empty or single-entry means there is nothing
            to choose between, and the caller should show no droplist at all. */
        std::vector<juce::String> getKitsForComponent (const juce::String& component) const
        {
            std::vector<juce::String> out;
            auto it = componentIndex.find (component.trim().toLowerCase());
            if (it == componentIndex.end()) return out;
            for (const auto& cf : it->second) out.push_back (cf.kitKey);
            return out;
        }

        /** Is there anything to swap to?  A folder holding one file - clap/ and
            low_kick/ typically - has no alternative, and an editor that offers a
            droplist there is offering a choice that does not exist. */
        bool componentHasAlternatives (const juce::String& component) const
        {
            auto it = componentIndex.find (component.trim().toLowerCase());
            return it != componentIndex.end() && it->second.size() > 1;
        }

        /** The .frb backing one kit's version of a component, or an invalid File. */
        juce::File getComponentFile (const juce::String& component,
                                     const juce::String& kitKey) const
        {
            auto it = componentIndex.find (component.trim().toLowerCase());
            if (it == componentIndex.end()) return {};
            const auto want = kitKey.trim().toLowerCase();
            for (const auto& cf : it->second)
                if (cf.kitKey == want) return cf.file;
            return {};
        }

        /** Every catalogued element that came from this folder, optionally
            narrowed to one kit.  Elements only exist for folders the scan
            actually loads - kickmix is loaded separately as a crossfade layer
            and comes back empty by design, and low_kick is a PAGE rather than a
            folder (note 35 lives in the kick blob).

            the_first was named here as skipped.  It is not: the skip was removed
            long before the folder was retired, and every low-zone component -
            revo_first, gm_first, the_second - is catalogued normally and returns
            its elements like any other. */
        std::vector<ElementHandle> getElementsForComponent (const juce::String& component,
                                                            const juce::String& kitKey = {}) const
        {
            std::vector<ElementHandle> out;
            const auto comp = component.trim().toLowerCase();
            const auto want = kitKey.trim().toLowerCase();
            for (const auto& h : allElements)
                if (h.component == comp && (want.isEmpty() || h.kitName == want))
                    out.push_back (h);
            return out;
        }

        /** Unique names of the GLOBAL drum components (non PPP-prefixed files
            such as low_kick / the_lasts) that programChangeDrum merges into
            every assembled kit. */
        std::vector<juce::String> getGlobalComponentNames() const
        {
            std::vector<juce::String> out;
            for (const auto& k : kits)
                if (k->isGlobal
                    && std::find (out.begin(), out.end(), k->name) == out.end())
                    out.push_back (k->name);
            return out;
        }

        /** Returns every element across every loaded kit whose roleId matches.
            Result is fresh by-value so callers can sort / filter freely. */
        std::vector<ElementHandle> getElementsForRole (uint8_t roleId) const
        {
            std::vector<ElementHandle> out;
            auto it = roleIndex.find (roleId);
            if (it == roleIndex.end()) return out;
            out.reserve (it->second.size());
            for (size_t idx : it->second)
                out.push_back (allElements[idx]);
            return out;
        }

        std::vector<ElementHandle> getElementsForRole (DrumElementRole role) const
        {
            return getElementsForRole ((uint8_t) role);
        }

        /** Every element belonging to the kit with the given display name. */
        //======================================================================
        // ONE METAL LIBRARY FOR EVERY KIT.
        //
        // Cymbals and hats live in drums/metal/PPP_Family.frb, one per kit, and
        // the per-kit versions have been abandoned: every kit now takes its
        // metal from drums/metal/000_Standard.frb.
        //
        // Done here rather than by renaming the file, because the registry's
        // global mechanism (low_kick, the_lasts, clap) PINS its blobs to fixed
        // notes.  Metal is not one note — it is a whole role across a dozen keys
        // — so it has to arrive as the kit's metal component and simply come
        // from a different kit's file.  Substituting at gather time keeps every
        // 000_*.frb on disk exactly where it is, and reverting is one constant.
        //======================================================================
        static constexpr const char* kSharedMetalKit = "000";

        std::vector<ElementHandle> getElementsForKit (const juce::String& kitName) const
        {
            std::vector<ElementHandle> out;
            auto it = kitNameIndex.find (kitName);
            if (it == kitNameIndex.end()) return out;

            const bool substituteMetal = ! kitName.equalsIgnoreCase (kSharedMetalKit);

            for (size_t kitIdx : it->second)
                for (const auto& h : kits[kitIdx]->elements)
                {
                    // Drop this kit's own metal; the shared library replaces it.
                    if (substituteMetal
                        && Betel::DrumRoles::groupForRole (h.roleId)
                               == Betel::DrumComponentGroup::Metal)
                        continue;

                    out.push_back (h);
                }

            if (substituteMetal)
                appendSharedMetal (out, kitName);

            return out;
        }

        /** The shared metal role, re-labelled so the editor still shows it as
            belonging to the kit that asked for it rather than to kit 000. */
        void appendSharedMetal (std::vector<ElementHandle>& out,
                                const juce::String& forKitName) const
        {
            auto it = kitNameIndex.find (juce::String (kSharedMetalKit));
            if (it == kitNameIndex.end()) return;

            for (size_t kitIdx : it->second)
                for (const auto& h : kits[kitIdx]->elements)
                    if (Betel::DrumRoles::groupForRole (h.roleId)
                            == Betel::DrumComponentGroup::Metal)
                    {
                        auto copy = h;
                        copy.kitName = forKitName;
                        out.push_back (copy);
                    }
        }

        /** Exact lookup: the element in `kitName` with `roleId`.  Returns
            nullptr if the kit doesn't carry that role.  Multiple matches
            (rare — usually a kit has one of each role) return the first. */
        /** The blob's velocity window for one handle.  Falls back to the full
            0..127 range if the blob never set one (a single-layer element). */
        bool velRangeOf (const ElementHandle& h, int& velLo, int& velHi) const
        {
            velLo = 0; velHi = 127;
            if (h.reader == nullptr) return false;

            const auto& presets = h.reader->getPresets();
            if (h.presetIndex < 0 || h.presetIndex >= (int) presets.size()) return false;

            const auto& regs = presets[(size_t) h.presetIndex].regions;
            if (h.regionIndex < 0 || h.regionIndex >= (int) regs.size()) return false;

            velLo = (int) regs[(size_t) h.regionIndex].velRangeLow;
            velHi = (int) regs[(size_t) h.regionIndex].velRangeHigh;
            return true;
        }

        /** EVERY velocity layer of one element (same kit + role), ordered soft to
            loud.  A single-layer element returns exactly one.  This is what makes
            a drum key track velocity: composeDrumKit turns each entry into its own
            Region, carrying that layer's velocity window. */
        std::vector<ElementHandle> findElementLayers (const juce::String& kitName,
                                                      uint8_t roleId) const
        {
            std::vector<ElementHandle> out;

            auto it = kitNameIndex.find (kitName);
            if (it == kitNameIndex.end()) return out;

            for (const auto ki : it->second)
                for (const auto& h : kits[(size_t) ki]->elements)
                    if (h.roleId == roleId)
                        out.push_back (h);

            std::sort (out.begin(), out.end(),
                       [this] (const ElementHandle& a, const ElementHandle& b)
                       {
                           int aLo = 0, aHi = 127, bLo = 0, bHi = 127;
                           velRangeOf (a, aLo, aHi);
                           velRangeOf (b, bLo, bHi);
                           if (aLo != bLo) return aLo < bLo;
                           return aHi < bHi;
                       });
            return out;
        }

        const ElementHandle* findElement (const juce::String& kitName, uint8_t roleId) const
        {
            auto it = kitNameIndex.find (kitName);
            if (it == kitNameIndex.end()) return nullptr;
            for (size_t kitIdx : it->second)
                for (const auto& h : kits[kitIdx]->elements)
                    if (h.roleId == roleId) return &h;
            return nullptr;
        }

        const ElementHandle* findElement (const juce::String& kitName, DrumElementRole role) const
        {
            return findElement (kitName, (uint8_t) role);
        }

        /** Direct access to a kit's BlobReader (needed for region sample
            decoding via getSampleDataFloat).  Returns nullptr if unknown. */
        BlobReader* getReader (const juce::String& kitName) const
        {
            auto it = kitNameIndex.find (kitName);
            if (it == kitNameIndex.end() || it->second.empty()) return nullptr;
            return kits[it->second.front()]->reader.get();
        }

        /** Convenience: decode the sample data for an ElementHandle.  Returns
            an empty FloatSample if the handle is stale or the region offset
            is bad. */
        BlobReader::FloatSample getSampleData (const ElementHandle& h) const
        {
            if (h.reader == nullptr) return {};
            const auto& presets = h.reader->getPresets();
            if (h.presetIndex < 0 || h.presetIndex >= (int) presets.size()) return {};
            const auto& preset = presets[(size_t) h.presetIndex];
            if (h.regionIndex < 0 || h.regionIndex >= (int) preset.regions.size()) return {};
            return h.reader->getSampleDataFloat (preset.regions[(size_t) h.regionIndex]);
        }

        const juce::String& getError() const noexcept { return lastError; }

        //======================================================================
        // Global CLAP -- a single clap shared by every kit, decoded once from
        // <drums>/clap/clap.frb.  Channel::loadDrumKit forces it onto MIDI note
        // 39 whatever kit is loaded.  scanFolder() calls loadGlobalClap().
        //======================================================================
        void loadGlobalClap (const juce::File& drumsFolder, const juce::String& accessCode)
        {
            globalClapLoaded = false;
            globalClapSample = {};

            const auto clapFile = drumsFolder.getChildFile ("clap").getChildFile ("clap.frb");
            if (! clapFile.existsAsFile())
                return;

            BlobReader reader;
            if (! reader.open (clapFile.getFullPathName().toStdString(), accessCode.toStdString()))
                return;

            const auto& presets = reader.getPresets();
            if (presets.empty() || presets.front().regions.empty())
                return;

            globalClapSample = reader.getSampleDataFloat (presets.front().regions.front());
            globalClapLoaded = (globalClapSample.numFrames > 0);
        }

        bool                            hasGlobalClap()       const noexcept { return globalClapLoaded; }
        const BlobReader::FloatSample&  getGlobalClapSample() const noexcept { return globalClapSample; }

        //======================================================================
        // Global LOW KICK -- the sub-kick shared by every kit, decoded once from
        // <drums>/low_kick/low_kick.frb and force-loaded onto MIDI note 35.
        // Force-loading (rather than the role-tagged element catalog) means it
        // connects even if the blob's region role is Unset or mis-keyed.
        //======================================================================
        //======================================================================
        // THE GLOBAL LOW KICK IS RETIRED.
        //
        // <drums>/low_kick/low_kick.frb was ONE sub-kick shared by every kit,
        // decoded once here and force-pinned onto MIDI 35 by composeDrumKit --
        // which had to delete the kit's own 35 first, because there was no way
        // for a kit to disagree with it.  Every kit in the library therefore had
        // the same sub-kick under a different snare.
        //
        // The re-sampled kick blobs carry BOTH kicks: note 35 and note 36 in one
        // SFZ per kit.  So note 35 is now an ordinary catalogued element that
        // arrives with its kit, gets its kit's character, and is swapped with it.
        //
        // Nothing replaces this function.  The LOW KICK page in the editor stays
        // exactly where it was -- it now reads note 35 of the `kick` component
        // instead of a folder of its own (see DrumsPopup::keysForComponent).
        //======================================================================

        //======================================================================
        // NOTE: THIS BLOCK IS HISTORY, AND IT DESCRIBES THE ERA BEFORE LAST.
        //
        // There was a time when no blob covered the sub-GM keys and 25..34 were
        // filled entirely by Channel.cpp's xgLowKeySubstitute(), borrowing the
        // closest sound the kit itself already carried.
        //
        // Sampled blobs replaced that, and are now three: revo_first / gm_first
        // over 13..22 and the_second over 23..34.  xgLowKeySubstitute still runs
        // and still matters — it is the fallback for a library with no low-zone
        // blob at all, and for any kit whose blob leaves a hole.
        //======================================================================

        //======================================================================
        // Global KICK MIX — two supplemental, VELOCITY-LAYERED kicks (EDM +
        // WOOD) decoded once from <drums>/kickmix/EDM.frb and WOOD.frb.  Unlike
        // low_kick / clap (single samples) these keep ALL velocity layers, so
        // composeDrumKit can crossfade them against the kit's own kick on notes
        // 35 and 36 (per-note MIX slider + EDM/WOOD selector + on/off).
        //======================================================================
        struct KickMixLayer
        {
            BlobFormat::Region       meta;     // velrange / rootkey / tune / loop
            BlobReader::FloatSample  sample;   // decoded audio
        };

        void loadGlobalKickMix (const juce::File& drumsFolder, const juce::String& accessCode)
        {
            edmKickLayers.clear();   edmKickLoaded  = false;
            woodKickLayers.clear();  woodKickLoaded = false;

            auto loadOne = [&] (const juce::String& fileName,
                                std::vector<KickMixLayer>& out, bool& okFlag)
            {
                const auto f = drumsFolder.getChildFile ("kickmix").getChildFile (fileName);
                if (! f.existsAsFile())
                    return;

                BlobReader reader;
                if (! reader.open (f.getFullPathName().toStdString(), accessCode.toStdString()))
                    return;

                const auto& presets = reader.getPresets();
                if (presets.empty() || presets.front().regions.empty())
                    return;

                for (const auto& reg : presets.front().regions)
                {
                    auto fs = reader.getSampleDataFloat (reg);
                    if (fs.numFrames <= 0) continue;
                    out.push_back ({ reg, std::move (fs) });
                }
                okFlag = ! out.empty();

            };

            loadOne ("EDM.frb",  edmKickLayers,  edmKickLoaded);
            loadOne ("WOOD.frb", woodKickLayers, woodKickLoaded);
        }

        // variant: 0 = EDM, 1 = WOOD
        bool hasKickMix (int variant) const noexcept
        { return variant == 1 ? woodKickLoaded : edmKickLoaded; }
        const std::vector<KickMixLayer>& getKickMixLayers (int variant) const noexcept
        { return variant == 1 ? woodKickLayers : edmKickLayers; }

    private:
        struct KitInfo
        {
            juce::String                 name;
            juce::String                 component;          // parent folder, lower-cased
            bool                         isGlobal = false;   // loaded for every kit
            juce::File                   file;
            std::unique_ptr<BlobReader>  reader;
            std::vector<ElementHandle>   elements;
        };

        /** One .frb inside a component folder, with the kit it belongs to. */
        struct ComponentFile
        {
            juce::String kitKey;   // "000", "008" ... or the stem for a global
            juce::File   file;
        };

        std::vector<std::unique_ptr<KitInfo>>                     kits;
        // folder name -> its files.  Built from the DIRECTORY in scanFolder, so
        // it covers the folders the kit loader skips as well as the ones it does.
        std::map<juce::String, std::vector<ComponentFile>>        componentIndex;
        std::vector<ElementHandle>                                allElements;     // flat catalog
        std::unordered_map<uint8_t, std::vector<size_t>>          roleIndex;       // role → indices into allElements
        std::unordered_map<juce::String, std::vector<size_t>>     kitNameIndex;    // kit name → indices into `kits`
        juce::String                                              lastError;
        BlobReader::FloatSample                                   globalClapSample;     // shared clap (note 39)
        bool                                                      globalClapLoaded = false;

        std::vector<KickMixLayer>                                 edmKickLayers;        // EDM kick, velocity-layered
        std::vector<KickMixLayer>                                 woodKickLayers;       // WOOD kick, velocity-layered
        bool                                                      edmKickLoaded  = false;
        bool                                                      woodKickLoaded = false;

        void unloadKitByFile (const juce::File& file)
        {
            for (auto it = kits.begin(); it != kits.end(); ++it)
                if ((*it)->file == file)
                {
                    kits.erase (it);
                    rebuildFlatCatalog();
                    return;
                }
        }

        // Rebuilds the flat allElements vector + the two lookup indices.
        // Called whenever the kits collection changes (load / unload).  Cost
        // is linear in total element count (~hundreds of entries at worst),
        // so cheap.
        void rebuildFlatCatalog()
        {
            allElements.clear();
            roleIndex.clear();
            kitNameIndex.clear();

            for (size_t ki = 0; ki < kits.size(); ++ki)
            {
                kitNameIndex[kits[ki]->name].push_back (ki);
                for (const auto& h : kits[ki]->elements)
                {
                    const size_t flatIdx = allElements.size();
                    allElements.push_back (h);
                    roleIndex[h.roleId].push_back (flatIdx);
                }
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitRegistry)
    };
} // namespace Betel
