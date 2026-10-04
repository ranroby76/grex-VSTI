

#pragma once
#include <JuceHeader.h>
#include "FstLoader.h"

//==============================================================================
//  FstLibrary.h — THE STYLE LIBRARY, AS A TREE OF FOLDERS.
//
//  Every style is a loose .fgt under <root>/styles, which is what the converter
//  produces and what the player can see, count, back up and delete without a
//  tool.
//
//  ── THE SUBFOLDER IS THE CATEGORY, AND THAT IS THE WHOLE FILING SYSTEM ──────
//
//  <root>/styles/Ballad/12-8 Ballad.fgt   ->  category "Ballad"
//  <root>/styles/Latin/Bossa Nova.fgt     ->  category "Latin"
//  <root>/styles/Loose Style.fgt          ->  category "GENERAL"
//
//  Ballada scanned NON-recursively and gave every style the single category
//  "Ballad", because it shipped one genre and a folder tree would have been an
//  empty structure.  Grex ships the full library, so the tree IS the browsing
//  model: the player organises the folder and the STYLES tab shows exactly what
//  the folder says, with no index file, no naming convention inside the file and
//  nothing to rebuild when a style is moved between folders.
//
//  ONE LEVEL DEEP IS WHAT COUNTS.  The scan itself is recursive so a style
//  buried two levels down is still FOUND rather than silently lost, but its
//  category is the FIRST segment - "Latin/Brazil/Samba.fgt" files under "Latin".
//  A category row that grew a level every time someone made a subfolder would be
//  unbrowsable, and a found-but-uncategorised style would be worse.
//
//  ── ONLY THE *STYLE* BLOBS WENT. SOUNDS ARE STILL BLOBS. ────────────────────
//
//  This is worth stating because "we abandoned the blobs" is the kind of half-
//  remembered sentence that gets acted on a year later against the wrong layer.
//
//  STILL BLOBS, UNCHANGED, and none of them were touched by this migration:
//     <root>/sounds_gm/*.frb        the melodic sample packs
//     <root>/sounds_world/*.frb     ditto
//     <root>/sounds_oriental/*.frb  ditto
//     <root>/sounds_gm/drums/*_kit.frb   the drum kits
//  All are memory-mapped through BlobReader and unlocked with AccessCode.h's
//  ChaCha20 - real protection over real sample data, which is a different
//  problem from telling one style library apart from another.
//
//  A style is a few kilobytes of note numbers; a sound pack is hundreds of
//  megabytes of audio. The blob exists to map the second cheaply, and it was
//  never earning its keep on the first.
//
//  ── THE ID IS THE RELATIVE PATH, AND THAT IS THE WHOLE DESIGN ──────────────
//
//  "Latin/Bossa Nova" - the path from the styles folder, no extension, forward
//  slashes on every platform.  NOT the bare stem, and the difference matters the
//  first time two folders both hold a "Slow Rock": with bare stems one of them
//  would shadow the other in every set that referenced it, silently, forever.
//
//  The DISPLAY name is still just the stem, so the grid reads "Bossa Nova" while
//  the set stores "Latin/Bossa Nova".
//
//  ── SCANNED, NOT PARSED ─────────────────────────────────────────────────────
//
//  Opening the folder reads a 96-byte header per file and stops. 300 styles is
//  300 short reads, and no note data is touched until one is actually chosen.
//  That is deliberately unlike the blob, which mapped everything at open.
//
//  ── AN ID THAT DOES NOT RESOLVE IS TRIED AS A NAME ──────────────────────────
//
//  Sets written against the flat Ballada-era library carry a bare stem, and sets
//  written while the blob existed carry `ballad/80s-pop-ballad`.  Rather than
//  migrate them — which means rewriting files the player did not ask us to touch
//  — the last path segment is matched against the display names.  An old set
//  finds its style and keeps working; nothing is rewritten behind anyone's back.
//  A style that MOVED between folders resolves through the same door.
//==============================================================================
namespace Betel
{

class FstLibrary
{
public:
    // Declared because JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR brings in a
    // leak-detector member, and a class with one is no longer an aggregate with
    // an implicit default constructor.
    FstLibrary() = default;

    /** The category given to a style sitting loose in the styles folder rather
        than in a subfolder.  A real name rather than an empty string, because
        the browser draws it on a button and "" is not a button anyone can aim
        at. */
    static constexpr const char* kUncategorised = "GENERAL";

    struct Entry
    {
        juce::String styleId;      // "Latin/Bossa Nova" — stable, what sets store
        juce::String displayName;  // "Bossa Nova" — the stem alone
        juce::String group;        // "Latin" — the first folder under styles/
        juce::File   file;
        double       tempo = 120.0;
        int          timeSigNum = 4, timeSigDen = 4;
        int          sectionCount = 0;
    };

    /** Scan a folder tree for *.fgt.  RECURSIVE, unlike Ballada's flat scan:
        the subfolders are the categories, so refusing to descend would mean
        finding nothing at all in an organised library. */
    void openFolder (const juce::File& folder, juce::StringArray& problems)
    {
        entries.clear();
        scanned = folder;

        if (! folder.isDirectory()) return;

        const auto wanted = Fst::hashAccessCode (Fst::kAccessCode);

        for (const auto& f : folder.findChildFiles (juce::File::findFiles, true,
                                                    Fst::kFileWildcard))
        {
            const auto s = Fst::peek (f);
            if (! s.ok)
            {
                // REPORTED AND SKIPPED. One unreadable file must not cost the
                // other 299 — and a style that vanished silently is the fault
                // nobody can trace.  The RELATIVE path is reported rather than
                // the bare name, or "Slow Rock.fgt: bad magic" would not say
                // which of the three Slow Rocks in the library it meant.
                problems.add (relativePathOf (f, folder)
                              + ": not a readable " + Fst::kFileExtension
                              + " (bad magic, wrong version, or truncated)");
                continue;
            }

            if (s.accessCodeHash != wanted)
            {
                problems.add (relativePathOf (f, folder)
                              + ": converted for a different product "
                                "(access code does not match)");
                continue;
            }

            Entry e;
            e.file         = f;
            e.styleId      = relativeIdOf (f, folder);
            e.displayName  = f.getFileNameWithoutExtension();
            e.group        = groupOf (f, folder);
            e.tempo        = s.tempo;
            e.timeSigNum   = s.timeSigNum;
            e.timeSigDen   = s.timeSigDen;
            e.sectionCount = s.sectionCount;
            entries.push_back (std::move (e));
        }

        // BY CATEGORY, THEN BY NAME.  The browser filters by category and pages
        // within it, so sorting the flat list this way means every category is
        // already alphabetical by the time it is filtered and no consumer has to
        // sort again.
        std::sort (entries.begin(), entries.end(),
                   [] (const Entry& a, const Entry& b)
                   {
                       const int g = a.group.compareIgnoreCase (b.group);
                       if (g != 0) return g < 0;
                       return a.displayName.compareNatural (b.displayName) < 0;
                   });
    }

    const std::vector<Entry>& allEntries() const noexcept { return entries; }
    int  getNumStyles() const noexcept { return (int) entries.size(); }
    bool isEmpty()      const noexcept { return entries.empty(); }
    juce::File getScannedFolder() const { return scanned; }

    /** Every category present, alphabetical.  Derived from what was actually
        scanned rather than from the folders on disk, so an empty subfolder does
        not produce a category button with nothing behind it. */
    juce::StringArray getGroups() const
    {
        juce::StringArray groups;
        for (const auto& e : entries)
            if (! groups.contains (e.group))
                groups.add (e.group);
        groups.sort (true);
        return groups;
    }

    /** Find by id, then by display name, then by the last segment of a legacy
        id.  Returns nullptr rather than throwing: a missing style is a normal
        state for a set that references something no longer installed. */
    const Entry* find (const juce::String& ref) const
    {
        if (ref.isEmpty()) return nullptr;

        for (const auto& e : entries)
            if (e.styleId.equalsIgnoreCase (ref)) return &e;

        for (const auto& e : entries)
            if (e.displayName.equalsIgnoreCase (ref)) return &e;

        // A LEGACY ID: "ballad/80s-pop-ballad", or a Grex id whose style has
        // since been moved to another folder.  The tail is the name, so it is
        // compared with the separators normalised out rather than by guessing at
        // a slug rule.
        const auto tail = ref.fromLastOccurrenceOf ("/", false, false);
        if (tail.isNotEmpty())
        {
            const auto want = squash (tail);
            for (const auto& e : entries)
                if (squash (e.displayName) == want) return &e;
        }
        return nullptr;
    }

    /** Read one style's bytes. The bytes are still scrambled — decoding is
        FstLoader's job, and keeping the two apart means the library never needs
        to hold a decoded style it was not asked for. */
    bool readStyleById (const juce::String& ref, juce::MemoryBlock& dest) const
    {
        if (const auto* e = find (ref))
            return e->file.loadFileAsData (dest) && dest.getSize() > 0;
        return false;
    }

private:
    /** Lower-cased with every space, dash, dot and underscore removed, so
        "80s Pop Ballad", "80s-pop-ballad" and "80sPopBallad" all match. */
    static juce::String squash (const juce::String& s)
    {
        return s.toLowerCase().removeCharacters (" -_.");
    }

    /** The path from the styles folder down, with forward slashes on every
        platform.  juce::File::getRelativePathFrom yields backslashes on Windows,
        and an id whose spelling depends on the OS is an id that stops matching
        the moment a set crosses machines. */
    static juce::String relativePathOf (const juce::File& f, const juce::File& root)
    {
        return f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    }

    static juce::String relativeIdOf (const juce::File& f, const juce::File& root)
    {
        const auto rel = relativePathOf (f, root);
        return rel.upToLastOccurrenceOf (".", false, false);
    }

    /** The FIRST path segment, or kUncategorised for a style sitting directly in
        the styles folder. */
    static juce::String groupOf (const juce::File& f, const juce::File& root)
    {
        const auto rel = relativePathOf (f, root);
        const int  cut = rel.indexOfChar ('/');
        if (cut <= 0) return kUncategorised;
        return rel.substring (0, cut);
    }

    std::vector<Entry> entries;
    juce::File         scanned;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FstLibrary)
};

} // namespace Betel
