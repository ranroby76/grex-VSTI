

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
        // TEMP diagnostics -> D:\\workspace\\BetelgeuseArranger\\grex_drum.txt
        static void regLog (const juce::String& line, bool reset = false)
        {
            juce::File f ("D:/workspace/BetelgeuseArranger/grex_drum.txt");
            if (reset) f.replaceWithText (line + juce::newLine);
            else       f.appendText     (line + juce::newLine);
        }

        int scanFolder (const juce::File& folder, const juce::String& accessCode)
        {
            regLog ("=== drum scanFolder ===", true);
            regLog ("folder=" + folder.getFullPathName()
                    + "  isDir=" + juce::String ((int) folder.isDirectory()));
            if (! folder.isDirectory()) return 0;

            int loaded = 0;
            juce::Array<juce::File> files;
            folder.findChildFiles (files, juce::File::findFiles, true, "*.frb");
            regLog ("frbFound=" + juce::String (files.size()));

            //------------------------------------------------------------------
            // COMPONENT INDEX — built from the FOLDER STRUCTURE, before any of
            // the skips below.
            //
            // This has to be independent of what becomes a "kit", because four
            // of the ten folders never do: clap/ and kickmix/ are `continue`d a
            // few lines down and loaded as pinned globals, and the_first/ is
            // skipped outright.  Indexing off the kit list would therefore leave
            // those editors pointing at nothing.  (low_kick/ was on this list
            // until the sub-kick moved inside the kick blobs.)
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
                regLog ("  component '" + c.first + "': "
                        + juce::String ((int) c.second.size()) + " file(s)"
                        + (c.second.size() > 1 ? "" : "   <<< no alternatives"));
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
                if (f.getFileName().equalsIgnoreCase ("the_first.frb")) continue;
                // kickmix/EDM.frb + WOOD.frb are supplemental crossfade layers
                // for notes 35/36, not kits -- loaded separately below.
                if (f.getParentDirectory().getFileName().equalsIgnoreCase ("kickmix")) continue;
                if (loadKit (f, accessCode)) ++loaded;
            }
            regLog ("kitsLoaded=" + juce::String (loaded));

            // Velocity-layer census.  If a kit reports keys == layers it is a
            // SINGLE-LAYER blob and velocity can only change its LEVEL, never its
            // timbre -- worth knowing, because that is the ceiling on how
            // expressive it can ever be.
            for (const auto& k : kits)
            {
                if (k == nullptr) continue;
                std::array<int, 128> perKey {};
                for (const auto& e : k->elements)
                    if (e.midiKey >= 0 && e.midiKey < 128) ++perKey[(size_t) e.midiKey];

                int keys = 0, layers = 0, maxLayers = 0;
                for (int n = 0; n < 128; ++n)
                    if (perKey[(size_t) n] > 0)
                    {
                        ++keys;
                        layers   += perKey[(size_t) n];
                        maxLayers = juce::jmax (maxLayers, perKey[(size_t) n]);
                    }

                regLog ("  kit '" + k->name + "': " + juce::String (keys) + " keys, "
                        + juce::String (layers) + " layers (max "
                        + juce::String (maxLayers) + "/key)"
                        + (maxLayers <= 1 ? "   <<< SINGLE-LAYER: velocity can only change level"
                                          : juce::String()));
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
                regLog ("  skip (full kit): " + file.getFileName());
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
                regLog ("  OPEN-FAIL " + file.getFileName()
                        + ": " + juce::String (info->reader->getError()));
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
                const bool numericKit = prefix.isNotEmpty()
                                     && prefix.containsOnly ("0123456789");
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

            // Walk every preset / region; catalog only regions with a real role.
            int cataloged = 0;
            int totalRegions = 0;
            for (int pi = 0; pi < (int) presets.size(); ++pi)
            {
                const auto& preset = presets[(size_t) pi];
                for (int ri = 0; ri < (int) preset.regions.size(); ++ri)
                {
                    ++totalRegions;
                    const auto& region = preset.regions[(size_t) ri];

                    // ── Resolve the GM key of this drum element ──────────────
                    // v1/v2 blobs carry no elementRoleId and may leave rootKey
                    // unset, so derive the key in priority order:
                    //   1. single-key range (lokey == hikey) — how drum elements
                    //      are authored (one key per element);
                    //   2. rootKey, if it lands in the GM percussion span;
                    //   3. otherwise skip the region.
                    // Role follows the key (GM role = note - 34) unless the blob
                    // explicitly tagged one (v3+).
                    const int lo = (int) region.keyRangeLow;
                    const int hi = (int) region.keyRangeHigh;
                    const int rk = (int) region.rootKey;
                    int key = -1;
                    if (lo == hi && lo >= 35 && lo <= 81)      key = lo;
                    else if (rk >= 35 && rk <= 81)             key = rk;
                    if (key < 0) continue;   // unmappable -> skip

                    int roleId = region.elementRoleId;
                    if (roleId == 0) roleId = key - 34;

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

            {
                juce::String extra;
                if (! presets.empty() && ! presets[0].regions.empty())
                {
                    const auto& r0 = presets[0].regions[0];
                    extra = "  r0(rk=" + juce::String ((int) r0.rootKey)
                          + " lo=" + juce::String ((int) r0.keyRangeLow)
                          + " hi=" + juce::String ((int) r0.keyRangeHigh)
                          + " role=" + juce::String ((int) r0.elementRoleId) + ")";
                }
                int kmin = 128, kmax = -1;
                for (const auto& e : info->elements)
                { kmin = juce::jmin (kmin, e.midiKey); kmax = juce::jmax (kmax, e.midiKey); }
                if (kmax >= 0)
                    extra += "  keys=" + juce::String (kmin) + ".." + juce::String (kmax);

                regLog ("  " + file.getFileName() + "  name=" + info->name
                        + " presets=" + juce::String ((int) presets.size())
                        + " regions=" + juce::String (totalRegions)
                        + " roleRegions=" + juce::String (cataloged)
                        + (info->isGlobal ? "  [GLOBAL]" : "") + extra);
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
            One editor per entry: clap, kick, kickmix, low_kick, metal, snare,
            stick, the_first, the_last, tom. */
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
            actually loads - clap / low_kick / kickmix / the_first are pinned or
            skipped, so they come back empty by design. */
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
            { regLog ("global clap: missing " + clapFile.getFullPathName()); return; }

            BlobReader reader;
            if (! reader.open (clapFile.getFullPathName().toStdString(), accessCode.toStdString()))
            { regLog ("global clap: open failed - " + juce::String (reader.getError())); return; }

            const auto& presets = reader.getPresets();
            if (presets.empty() || presets.front().regions.empty())
            { regLog ("global clap: no usable region"); return; }

            globalClapSample = reader.getSampleDataFloat (presets.front().regions.front());
            globalClapLoaded = (globalClapSample.numFrames > 0);
            regLog ("global clap: " + juce::String (globalClapLoaded ? "loaded " : "decode failed ")
                    + juce::String (globalClapSample.numFrames) + " frames");
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
        // NOTE: the old "the_first" global (a separate blob for the low keys) is
        // gone.  The XG extended keys 25..34 are now filled by Channel.cpp's
        // xgLowKeySubstitute(), which borrows the closest sound the KIT ITSELF
        // already carries -- so a brush kit's soft kick (XG note 33) is that
        // kit's own kick, in character, with no extra file to ship.
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
                { regLog ("kickmix: missing " + f.getFullPathName()); return; }

                BlobReader reader;
                if (! reader.open (f.getFullPathName().toStdString(), accessCode.toStdString()))
                { regLog ("kickmix: open failed " + fileName + " - " + juce::String (reader.getError())); return; }

                const auto& presets = reader.getPresets();
                if (presets.empty() || presets.front().regions.empty())
                { regLog ("kickmix: no usable region in " + fileName); return; }

                for (const auto& reg : presets.front().regions)
                {
                    auto fs = reader.getSampleDataFloat (reg);
                    if (fs.numFrames <= 0) continue;
                    out.push_back ({ reg, std::move (fs) });
                }
                okFlag = ! out.empty();
                regLog ("kickmix " + fileName + ": "
                        + juce::String (okFlag ? "loaded " : "decode failed ")
                        + juce::String ((int) out.size()) + " layers");
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
