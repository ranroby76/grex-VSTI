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
// On first run, the manager defaults to ~/Documents/Grex VSTI so people
// who already have files there don't have to click anything.  The locator
// LED in the plugin header shows GREEN when the root resolves to an
// existing directory and BLINKS RED otherwise.
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
    // "sounds" is gone.  Each pack is now its own top-level folder so a user can
    // install one by dropping it into the root and uninstall it by deleting it -
    // no merge step, nothing shared, nothing left behind.
    //
    // GM is the only one that ships with the plugin; WORLD and ORIENTAL are
    // optional downloads, so every consumer has to treat a missing folder as an
    // empty pack rather than an error.
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

        getFavoritesFolder().createDirectory();
        getSetsFolder()     .createDirectory();
        getGmSoundsFolder()      .createDirectory();
        getWorldSoundsFolder()   .createDirectory();
        getOrientalSoundsFolder().createDirectory();
        getStylesFolder()   .createDirectory();

        saveLocator();
        if (onRootFolderChanged) onRootFolderChanged();
        return true;
    }

private:
    //==========================================================================
    // Default + locator persistence
    //==========================================================================
    static juce::File defaultRootFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Grex VSTI");
    }

    static juce::File getLocatorFile()
    {
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

    void saveLocator() const
    {
        const auto f = getLocatorFile();
        f.getParentDirectory().createDirectory();

        juce::ValueTree tree ("BetelgeuseLocator");
        tree.setProperty ("version",    1,                          nullptr);
        tree.setProperty ("rootFolder", rootFolder.getFullPathName(), nullptr);

        if (auto xml = tree.createXml())
            xml->writeTo (f, {});
    }

    juce::File rootFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BetelFolderManager)
};


