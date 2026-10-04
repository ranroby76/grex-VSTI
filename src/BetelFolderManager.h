
#pragma once
//==============================================================================
// BetelFolderManager.h
//
// Centralised lookup for every path Betelgeuse reads from or writes to.
// The "root folder" is a single user-chosen directory that holds the
// standard sub-folders:
//
//     <root>/favorites   — favorites.xml (and per-favorite files later)
//     <root>/sets        — set / session export files
//     <root>/sounds      — *.frb sound packs (drum kits = *_kit.frb,
//                          melodic sound packs too)
//     <root>/styles      — Yamaha SFF style files
//                          (.sty .prs .bcs .sst .pst .pcs .fps .scp .aus)
//
// (Folder names are intentionally lowercase to match the on-disk layout
// Rob is using.  More sub-folders will likely be added later.)
//
// The chosen root is remembered in a tiny "locator" XML stored in the OS's
// user-application-data area (chicken-and-egg: the locator can't live
// inside the root it points to):
//
//     macOS:   ~/Library/Application Support/Fanan Team/Betelgeuse/locator.xml
//     Windows: %APPDATA%\Fanan Team\Betelgeuse\locator.xml
//
// On first run, the manager defaults to ~/Documents/Grex VSTI so people who
// already have files there don't have to click anything.  The locator LED in
// the plugin header shows GREEN when the root resolves to an existing directory
// and BLINKS RED otherwise.
//
//==============================================================================
// GREX HAS THREE SOUND PACKS, AND THAT IS ENFORCED HERE RATHER THAN IN THE UI.
//
// sounds_gm, sounds_world, sounds_oriental, with a PACK column in the SOUNDS tab
// to pick between them.  Ballada - which this tree was forked from - ships GM
// alone and had both extra folders DELETED rather than merely hidden, so
// restoring them here is what makes the PACK column mean something again.
//
// GM is the only pack that ships with the plugin; WORLD and ORIENTAL are
// optional downloads, so every consumer has to treat a missing folder as an
// EMPTY pack rather than an error.
//==============================================================================

#include <JuceHeader.h>

class BetelFolderManager
{
public:
    /** Fired whenever the root folder is changed via setRootFolder().
        Wire this from the host to re-scan sounds / styles / favorites. */
    std::function<void()> onRootFolderChanged;

    BetelFolderManager()
    {
        loadLocator();
        if (rootFolder.getFullPathName().isEmpty())
            rootFolder = defaultRootFolder();
    }

    //==========================================================================
    // Read API
    //==========================================================================
    juce::File getRootFolder()      const { return rootFolder; }
    bool       isRootFolderValid()  const { return rootFolder.isDirectory(); }

    juce::File getFavoritesFolder() const { return rootFolder.getChildFile ("favorites"); }
    juce::File getSetsFolder()      const { return rootFolder.getChildFile ("sets"); }

    // ── THE THREE SOUND PACKS ────────────────────────────────────────────────
    //
    // "sounds" is gone.  Each pack is its own top-level folder so a user can
    // install one by dropping it into the root and uninstall it by deleting it -
    // no merge step, nothing shared, nothing left behind.
    //
    // The folder name "sounds_gm" is spelled exactly as Ballada spells it, so a
    // GM pack downloaded for either product installs into either without being
    // repackaged.
    juce::File getGmSoundsFolder()       const { return rootFolder.getChildFile ("sounds_gm"); }
    juce::File getWorldSoundsFolder()    const { return rootFolder.getChildFile ("sounds_world"); }
    juce::File getOrientalSoundsFolder() const { return rootFolder.getChildFile ("sounds_oriental"); }

    /** Pack root by index - 0 GM, 1 WORLD, 2 ORIENTAL, matching
        SamplePlayerEngine::SoundPack.  Out of range answers GM. */
    juce::File getPackFolder (int pack) const
    {
        return pack == 1 ? getWorldSoundsFolder()
             : pack == 2 ? getOrientalSoundsFolder()
                         : getGmSoundsFolder();
    }

    //==========================================================================
    // PRESETS LIVE INSIDE THE PACK THEY DESCRIBE.
    //
    // They used to sit beside the library in one shared folder.  That made a
    // pack two downloads that had to be extracted to two places and stay in
    // step - and deleting a pack left its presets behind, keyed to flags that
    // no longer resolve.
    //
    // Inside the pack, one folder is the whole install: extract into the root
    // and the sounds and their voicing arrive together.  Delete it and both go.
    //==========================================================================
    juce::File getPackPresetsFolder (int pack) const
    {
        return getPackFolder (pack).getChildFile ("instruments_presets");
    }

    juce::File getPackStylePresetsFolder (int pack) const
    {
        return getPackFolder (pack).getChildFile ("style_instruments_presets");
    }

    /** Drum kits ship with GM and live inside it - both the composed component
        blobs and the sampled full kits. */
    juce::File getDrumsFolder() const { return getGmSoundsFolder().getChildFile ("drums"); }
    juce::File getStylesFolder()    const { return rootFolder.getChildFile ("styles"); }

    juce::File getFavoritesFile()   const { return getFavoritesFolder().getChildFile ("favorites.xml"); }

    //==========================================================================
    // Write API — call from the LED button click handler after the user has
    // picked a folder.  Creates the standard sub-folders if missing, persists
    // the choice, and fires onRootFolderChanged.
    //==========================================================================
    bool setRootFolder (const juce::File& folder)
    {
        if (! folder.isDirectory()) return false;
        rootFolder = folder;

        getFavoritesFolder()     .createDirectory();
        getSetsFolder()          .createDirectory();
        getGmSoundsFolder()      .createDirectory();
        getWorldSoundsFolder()   .createDirectory();
        getOrientalSoundsFolder().createDirectory();
        getStylesFolder()        .createDirectory();

        // THE RESULT OF THE WRITE IS KEPT, NOT DISCARDED.  See saveLocator.
        locatorSaved = saveLocator();

        if (onRootFolderChanged) onRootFolderChanged();
        return true;
    }

    /** Did the LAST setRootFolder actually persist to disk?

        Separate from setRootFolder's return value on purpose: those are two
        different questions.  "Is this a usable folder" is answered true, and
        correctly - the root IS set and the library WILL load for this session.
        "Will it still be set next time" is this one, and a false here is the
        only warning the user gets that they are going to have to do it again. */
    bool isLocatorPersisted() const noexcept { return locatorSaved; }

private:
    //==========================================================================
    // Default + locator persistence
    //==========================================================================
    static juce::File defaultRootFolder()
    {
        // "Grex VSTI".  Ballada defaults to "Grex Ballada VSTI"; the VSTI
        // suffix is not decoration, it is what tells a user with both products
        // which Documents folder belongs to which plugin.
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Grex VSTI");
    }

    static juce::File getLocatorFile()
    {
        // "Betelgeuse", NOT "Grex Ballada".  Ballada writes its locator to
        // Fanan Team/Grex Ballada/locator.xml; if the two shared one, the last
        // product launched would drag the other one's root folder with it.
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Fanan Team")
                   .getChildFile ("Betelgeuse")
                   .getChildFile ("locator.xml");
    }

    void loadLocator()
    {
        const auto f = getLocatorFile();
        if (! f.existsAsFile()) return;

        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr) return;

        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid() || ! tree.hasType ("BetelgeuseLocator")) return;

        const auto path = tree.getProperty ("rootFolder", juce::String()).toString();
        if (path.isNotEmpty())
            rootFolder = juce::File (path);
    }

    //==========================================================================
    // RETURNS WHETHER IT ACTUALLY WROTE, and that return is the whole point.
    //
    // This used to be void, and `xml->writeTo()`'s bool was dropped on the
    // floor.  So a failed write - AppData redirected to a locked share, a
    // roaming profile, antivirus holding the file, a full disk - produced
    // EXACTLY the same outcome as a successful one: green LED, styles loading
    // fine for the rest of the session, and nothing at all on the next launch.
    //
    // That is the worst shape a bug can take here, because the user's own
    // evidence says it worked. They would relocate the folder, watch it work,
    // close the DAW, and find it forgotten again - forever, with no message
    // anywhere blaming the thing that actually failed.
    //
    // The parent directory is created first and its result checked too: writeTo
    // into a directory that does not exist fails for a reason worth separating
    // from a permissions problem, even though both end the same way here.
    //==========================================================================
    bool saveLocator() const
    {
        const auto f = getLocatorFile();
        f.getParentDirectory().createDirectory();

        juce::ValueTree tree ("BetelgeuseLocator");
        tree.setProperty ("version",    1,                          nullptr);
        tree.setProperty ("rootFolder", rootFolder.getFullPathName(), nullptr);

        auto xml = tree.createXml();
        if (xml == nullptr) return false;

        return xml->writeTo (f, {});
    }

    juce::File rootFolder;

    // Starts TRUE: a locator that was loaded from disk, or a default root that
    // nobody has changed, has nothing outstanding to persist.  Only an actual
    // setRootFolder can turn this false.
    bool locatorSaved = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BetelFolderManager)
};










