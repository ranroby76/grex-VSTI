#pragma once
//==============================================================================
// FullKitMap.h - which drum kits load WHOLE, and which are composed.
//
// Grex has two ways to make a kit and they are not interchangeable:
//
//   COMPOSED  the nine GM kits, assembled per key from the component folders -
//             kick/, snare/, stick/, metal/, tom/ - with the shared metal
//             library, the kick-mix layers, the XG low-key substitution and the
//             per-element editor all built on top.
//
//   FULL      one .frb holding an entire sampled kit, loaded as it is.  For the
//             kits GM has no answer to: Arabic, Cuban, Pop Latin, Dance, the
//             Live! tiers.
//
// ── THE POLICY ───────────────────────────────────────────────────────────────
//
//   1. No folder for this bank/PC        -> COMPOSED, always.
//   2. Folder exists, and Grex OWNS a    -> COMPOSED on the DRUMS slot,
//      composed kit for that PC - any         FULL on the PERC slot.
//      of the NINE (Standard 1/2, Room,
//      Power, Electro, TR-808, Jazz,
//      Brush, Orchestra, HipHop)
//   3. Folder exists, no composed kit    -> FULL.
//      for that PC
//
// Rule 2 is the one worth explaining.  Dropping a monolithic sampled kit onto
// the DRUMS slot would bypass everything the composed path carries - the shared
// metal, the kick mix, the low-key substitution, and every per-element edit the
// user has saved.  So wherever Grex has the kit the style ACTUALLY ASKED FOR,
// that kit wins on DRUMS, and the sampled version stays available on PERC where
// it is additive rather than a substitution.
//
// The test used to name only four PCs, which meant a style asking for a ROOM
// kit got a sampled one instead purely because a folder existed.  All nine now
// have priority.  What still loads sampled on DRUMS is everything with NO
// composed answer of its own: bank 126 entirely (Arabic, Cuban and the rest of
// the ethnic kits have no GM equivalent to lose to), and bank-127 PCs outside
// the nine, where the alternative would be a nearestDrumFamily approximation
// rather than the real thing.
//
// The consequence, accepted deliberately: every FALLBACK stays inside the
// composed world.  Nothing ever falls back into a sampled kit.
//
// ── THE FOLDER NAMING ────────────────────────────────────────────────────────
//
//   <root>/sounds/drums/<MSB>-<LSB>-<PC>_<readable name>.frb     <- a FILE
//   <root>/sounds/drums/<MSB>-<LSB>-<PC>_<readable name>/         <- or a FOLDER
//   e.g.  126-000-036_Arabic Kit.frb
//
// Both shapes work.  A flat file is the simple case and what you get straight
// out of a sampler export; a folder is there for a kit that ends up split
// across several blobs later.  Neither needs a code change.
//
// Three digits each, zero padded, and PC is the RAW MIDI BYTE.  Yamaha's data
// lists print PC# from 1, so every number here is theirs minus one - the same
// off-by-one that has cost this project time twice already, pinned down in the
// folder name where it cannot drift.
//
// The numbers are what the code matches on; the name after the underscore is
// for humans and is ignored.  Drop in a folder and it works on next scan: no
// table to edit, no rebuild.
//==============================================================================

#include <JuceHeader.h>
#include <algorithm>
#include <map>
#include <vector>

namespace Betel
{
    class FullKitMap
    {
    public:
        /** One sampled kit folder found on disk. */
        struct Entry
        {
            int          msb = -1, lsb = 0, pc = -1;
            juce::File   path;       // the .frb itself, or the folder holding it
            bool         isFile = true;
            juce::String label;      // the part after the underscore, for logs
        };

        /** Does this name carry a full-kit prefix?

            Public and static because the DRUM REGISTRY needs the same test: it
            recurses this very folder collecting *.frb as COMPONENTS, and without
            this it would take "127-000-048_Symphony Kit.frb" for a global
            component and layer a whole sampled kit onto every GM kit in the
            library.  One test, both callers. */
        static bool isFullKitName (const juce::String& stem, int* msb = nullptr,
                                   int* lsb = nullptr, int* pc = nullptr)
        {
            const auto head = stem.upToFirstOccurrenceOf ("_", false, false);
            juce::StringArray parts;
            parts.addTokens (head, "-", "");
            if (parts.size() != 3) return false;

            for (const auto& p : parts)
                if (p.isEmpty() || ! p.containsOnly ("0123456789")) return false;

            if (msb) *msb = parts[0].getIntValue();
            if (lsb) *lsb = parts[1].getIntValue();
            if (pc)  *pc  = parts[2].getIntValue();
            return true;
        }

        /** Scan <drumsRoot> for FILES and FOLDERS named <MSB>-<LSB>-<PC>_<name>.

            Top level only, deliberately: the component folders (kick/, snare/,
            metal/ ...) live one level down and are full of ordinary .frb files
            that must never be mistaken for whole kits. */
        void scan (const juce::File& drumsRoot)
        {
            entries.clear();
            if (! drumsRoot.isDirectory()) return;

            auto add = [this] (const juce::File& f, bool isFile)
            {
                const auto stem = isFile ? f.getFileNameWithoutExtension()
                                         : f.getFileName();
                Entry en;
                if (! isFullKitName (stem, &en.msb, &en.lsb, &en.pc)) return;

                en.path   = f;
                en.isFile = isFile;
                en.label  = stem.fromFirstOccurrenceOf ("_", false, false);

                // A FILE wins over a folder of the same numbers: it is the
                // simpler shape and the one a sampler export produces, so if
                // both somehow exist the flat file is what the user just made.
                auto k = key (en.msb, en.lsb, en.pc);
                auto it = entries.find (k);
                if (it != entries.end() && it->second.isFile && ! isFile) return;
                entries[k] = en;
            };

            for (const auto& e : juce::RangedDirectoryIterator (drumsRoot, false, "*.frb",
                                                                juce::File::findFiles))
                add (e.getFile(), true);

            for (const auto& e : juce::RangedDirectoryIterator (drumsRoot, false, "*",
                                                                juce::File::findDirectories))
                add (e.getFile(), false);
        }

        int size() const noexcept { return (int) entries.size(); }

        /** Every sampled kit found, ordered bank-then-PC so the UI grid is
            stable across launches and across machines. */
        std::vector<Entry> all() const
        {
            std::vector<Entry> out;
            out.reserve (entries.size());
            for (const auto& e : entries) out.push_back (e.second);
            std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b)
            {
                if (a.msb != b.msb) return a.msb < b.msb;
                return a.pc < b.pc;
            });
            return out;
        }

        /** The readable half of the folder name, underscores back to spaces:
            "126-000-043_Pop_Latin_Kit" -> "Pop Latin Kit". */
        static juce::String prettyLabel (const Entry& e)
        {
            auto s = e.label.replaceCharacter ('_', ' ').trim();
            return s.isEmpty() ? juce::String (e.msb) + "/" + juce::String (e.pc) : s;
        }

        const Entry* find (int msb, int lsb, int pc) const
        {
            auto it = entries.find (key (msb, lsb, pc));
            return it != entries.end() ? &it->second : nullptr;
        }

        /** Does Grex already have a REAL composed kit for this request, so the
            composed version should own the DRUMS slot?

            THE NINE COMPOSED KITS HAVE PRIORITY ON DRUMS.  ALL of them.

            This used to name only four - Standard 1 and 2, Electro and TR-808 -
            on the reasoning that they were the well-calibrated ones.  The other
            five are just as real: Room, Power, Jazz, Brush and Orchestra are
            built from the same component blobs and mapped at the same canonical
            PCs.  Leaving them out meant a style asking for a ROOM kit got a
            sampled kit instead, purely because a folder happened to exist -
            which is backwards.  If Grex has the kit the style ASKED FOR, that is
            the kit to play.

            The list mirrors SamplePlayerEngine::populateDefaultDrumKitPCMap and
            has to keep mirroring it; PC 1 rides along because Standard 2 is an
            alias nearestDrumFamily already resolves onto Standard.

            WHAT STILL LOADS SAMPLED ON DRUMS, and this is the whole point of
            keeping the test narrow: anything with NO composed answer of its own.
            Bank 126 fails at the first line, so Arabic, Cuban and the rest of
            the ethnic kits are untouched - they have no GM equivalent to lose
            to.  So do bank-127 PCs outside the nine, where the alternative is a
            nearestDrumFamily approximation rather than the real thing.

            PERC is unaffected either way - see shouldLoadFull.  A style that
            puts a second kit on PERC wants that kit, not a GM stand-in. */
        static bool gmCoversIt (int msb, int lsb, int pc) noexcept
        {
            if (msb != 127 || lsb != 0) return false;

            switch (pc)
            {
                case 0:  case 1:    // Standard 1 / Standard 2
                case 8:             // Room
                case 16:            // Power
                case 24:            // Electro
                case 25:            // TR-808 / Analog
                case 32:            // Jazz
                case 40:            // Brush
                case 48:            // Orchestra
                case 56:            // HipHop -> Electro
                    return true;
                default:
                    return false;
            }
        }

        /** THE DECISION.  True when this request should load the sampled kit.

            `isPercSlot` is the destination, not the source: a style's rhythm
            channels resolve to slot 0 (DRUMS) or slot 1 (PERC) before anything
            loads, so the caller always knows which it is. */
        bool shouldLoadFull (int msb, int lsb, int pc, bool isPercSlot) const
        {
            if (find (msb, lsb, pc) == nullptr) return false;      // rule 1

            // Rule 2 is where DRUMS and PERC part company, and deliberately so.
            // On DRUMS a composed kit Grex actually owns beats a sampled one
            // every time; on PERC the sampled kit loads, because a style that
            // puts a second kit there asked for that kit specifically.
            if (gmCoversIt (msb, lsb, pc))      return isPercSlot; // rule 2

            return true;                                           // rule 3
        }

        /** The .frb inside a kit folder.  One file per folder is the expected
            shape; if several exist the first is taken, so a stray backup does
            not silently change which kit loads. */
        juce::File blobFor (int msb, int lsb, int pc) const
        {
            const auto* e = find (msb, lsb, pc);
            if (e == nullptr) return {};
            if (e->isFile)   return e->path;

            juce::Array<juce::File> blobs;
            e->path.findChildFiles (blobs, juce::File::findFiles, false, "*.frb");
            blobs.sort();
            return blobs.isEmpty() ? juce::File() : blobs[0];
        }

        //======================================================================
        // FALLBACK - which COMPOSED kit answers a request nothing else can.
        //
        // Composed only, by design: a miss must never land in a sampled kit, or
        // the user gets a sound with different calibration and no per-element
        // editing, chosen by an accident of which folders happen to exist.
        //
        // The electronic family is the one place worth being deliberate.  House,
        // Break, Hip Hop and the T8/T9 analog kits are ~137 requests across the
        // library with no exact match, and Electro or TR-808 is a far closer
        // answer than Standard - same family, same register, same intent.
        //======================================================================
        static int composedFallbackPc (int msb, int lsb, int pc) noexcept
        {
            // ── BANK 127 HAS NO OPINION HERE ANY MORE, DELIBERATELY ─────────
            //
            // A second bank-127 table used to live in this function, and it
            // DISAGREED with SamplePlayerEngine::nearestDrumFamily on five
            // programs - four of them in the families that misfire most:
            //
            //     PC  4  HitKit         nearest Electro   here Standard
            //     PC 17  RockDrumKit    nearest POWER     here Standard
            //     PC 61  DrumMachine    nearest TR-808    here Electro
            //     PC 73  PopDrumKit     nearest Electro   here Standard
            //     PC 75  JazzBrushComp  nearest BRUSH     here Standard
            //
            // Both answer the SAME question - which composed kit stands in for
            // this program - and which one a style got depended on whether a
            // sampled-kit folder happened to exist on that machine.  The same
            // file played a Rock kit on one install and a Standard kit on
            // another, with nothing on screen to explain it.
            //
            // nearestDrumFamily is now the only answer for bank 127, and every
            // program this table used to catch has been folded into it.
            // Returning -1 is exactly how the callers ask for that.
            if (msb == 127 && lsb == 0)
                return -1;

            if (msb == 126 && lsb == 0)
            {
                // ETHNIC AND SFX NEVER FALL INTO A DRUM KIT.
                //
                // Bank 126 holds riq, sagat, tabla and noise kits.  Substituting
                // Standard puts a snare where a doum should be, which is worse
                // than the sound being absent.  Arabic answers the Arabic
                // family; the rest keep the existing percussion handling.
                switch (pc)
                {
                    case 35: case 37: case 38:
                    case 64: case 65: case 66:
                    case 67: case 72: case 73:
                    case 74: case 75: case 76:  return -2;   // -2 = "use Arabic if present"
                    default: break;
                }
                return -1;
            }

            return -1;
        }

        /** Sentinel returned by composedFallbackPc for the Arabic family. */
        static constexpr int kFallbackArabic = -2;

    private:
        static juce::String key (int msb, int lsb, int pc)
        {
            return juce::String (msb) + "/" + juce::String (lsb) + "/" + juce::String (pc);
        }

        std::map<juce::String, Entry> entries;
    };
} // namespace Betel
