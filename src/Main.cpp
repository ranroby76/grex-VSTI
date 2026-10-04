

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>
#include "RegistrationManager.h"

#include "Main.h"
#include "GlobalMacros.h"       // GrexPaths + the funkey / big-drums macro presets
#include "StyleFavorites.h"     // the stars, read at startup with everything else
#include "CcMap.h"             // the rig's CC bindings, in force before the first event
#include "MainComponent.h"
#include "StyleLoader.h"
#include "FstLibrary.h"      // the styles folder, as loose .fgt files
#include "SearchHistory.h"   // saved search keywords, root-backed
#include "StyleLevels.h"    // per-style loudness-trim switch, read in the callback
#include "SetBaker.h"       // setFileNameForStyle - ONE spelling of the set name

// ======================================================
//  BetelgeuseProcessor — implementation
// ======================================================
BetelgeuseProcessor::BetelgeuseProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Wire the drum-kit registry into the engine immediately so PCs that
    // arrive before any explicit scan can still resolve once kits are added.
    engine.setDrumKitRegistry (&drumKitRegistry);

    // Pull the kits + styles folders from the folder manager.  If the saved
    // root doesn't exist yet (clean install) these paths simply yield empty
    // scans; the UI's locator LED will blink red and prompt the user to
    // pick a real folder, after which rescanFromFolderManager() re-runs
    // this same logic.
    //
    // It also sets GrexPaths' root, so everything below can reach a file.
    rescanFromFolderManager();

    //==========================================================================
    // EVERYTHING THAT READS A FILE AT STARTUP LIVES HERE, NOT IN THE EDITOR.
    //
    // All of this used to run in the MainComponent constructor, which meant an
    // offline bounce, a project played without opening the UI, or a host that
    // instantiates before showing a window got none of it.
    //
    // IT HAS ALL MOVED INTO rescanFromFolderManager, WHICH RAN AT LINE 37
    // ABOVE.  Not for tidiness: those files live under GrexPaths::root(), and
    // reading them from the constructor meant they were read ONCE, against
    // whatever root existed at startup.  On a clean install that root does not
    // exist yet, so every one of them silently fell back to defaults - and then
    // the user pressed LOCATE, the real files were right there, and nothing
    // re-read them until the plugin was reloaded.
    //
    // The comment at line 33 already promised that relocating "re-runs this
    // same logic".  It did not.  Now it does.
    //==========================================================================
    RegistrationManager::getInstance().checkRegistration();
}

//==============================================================================
// See the declaration in Main.h for the rule this enforces.
//
// CALLED FROM rescanFromFolderManager AND NOWHERE ELSE, so there is exactly one
// answer to "when is this re-read": whenever the root is set, including the
// first time.  Adding a second call site is how the two drift apart.
//==============================================================================
void BetelgeuseProcessor::reloadRootBackedSettings()
{
    auto& ms = Betel::MasterSettings::get();
    ms.load();

    // ── THE CC BINDINGS, BEFORE THE FIRST MIDI EVENT ─────────────────────────
    //
    // grex_cc_map.xml describes the RIG - which knob on this desk drives
    // what - so like the pitch bend range below it has to be in force before
    // anything can arrive, window or no window.
    {
        auto& cm = Betel::CcMap::get();
        cm.load();
        for (int i = 0; i < kNumCcTargets; ++i)
            setCcNumber (i, cm.ccFor (i));
    }

    // The wheel's throw and the right hand's base unity are properties of the
    // RIG, not of a song, so they must be in force before the first set loads -
    // and before the first note, in a session with no window.
    setSoloPitchBendRange (ms.getPitchBendRange());
    applyStoredSoloBaseUnity();

    chordTracker.setChordMode (
        ms.isFingeredChord() ? Betel::ChordZoneTracker::ChordMode::Fingered
                             : Betel::ChordZoneTracker::ChordMode::SingleFinger);

    // WHERE THE HANDS DIVIDE, BEFORE THE FIRST NOTE, WINDOW OR NO WINDOW.
    //
    // chordTracker directly, NOT setSplitPoint(): the full setter writes back
    // to MasterSettings, and seeding must never re-save the value it has just
    // finished reading.
    chordTracker.setSplitPoint (ms.getSplitPoint());

    setTempoSynced (ms.isTempoSynced());

    // Global macro presets (funkey / big drums) and the style stars are
    // installation-wide files under the same root.  Same reasoning: a style
    // played without the window open should sound the way this install sounds.
    Betel::GlobalMacros::get().loadFunkey();
    Betel::StyleFavorites::get().load();
    Betel::SearchHistory::get().load();

    // ── THE GLOBAL STYLE-BUS BOOST ───────────────────────────────────────────
    //
    // grex_boost.xml was the one root-backed file missing from this list, and
    // it is the worst one to miss: StyleLevels is a lazy singleton, so it reads
    // the file on FIRST USE, which on a clean install is before the user has
    // located the library.  It then resolved next to the executable, found
    // nothing, and sat on the factory value for the rest of the session while
    // the real file was in the folder the user had just picked.
    //
    // Re-reading here is the same rule every line above follows - whenever the
    // root is set, including the first time.
    Betel::StyleLevels::get().loadGlobalBoost();

    // AND PUSH IT, because loading a number nothing reads is half a fix.  This
    // is the same single call setStyleBoostDb makes; it is an atomic store on
    // the engine, so it is safe with no style loaded and safe from the ctor.
    stylePlayer.pushStyleBoost();

    // Crash settings own themselves (grex_crash.xml is the sole owner - the
    // set deliberately does not carry them).  The editor re-reads them after the
    // Crash tab has pushed its defaults, which is a no-op on an established
    // install and correct on a fresh one.
    loadCrashSettings();
}

void BetelgeuseProcessor::rescanFromFolderManager()
{
    // ── ONE ROOT FOR EVERY FILE WE OWN ───────────────────────────────────────
    //
    // Presets, registration, master settings, crash settings, the stars, the
    // macro presets, every diagnostic - all of them ask GrexPaths.  So the root
    // has to be set at the single choke point that runs both at construction
    // AND whenever the user relocates the library, which is this function.
    //
    // It used to be a lone call in the MainComponent constructor, and that had
    // two consequences.  Without a window it was never set at all, so every one
    // of those files resolved next to the executable.  And RELOCATING the
    // folder mid-session re-scanned the sounds and styles but left GrexPaths
    // pointing at the OLD root until the plugin was reloaded - so the browser
    // showed the new library while a saved preset went to the old folder.
    Betel::GrexPaths::setRoot (folderManager.getRootFolder());

    // ── Drum kits (live in <root>/sounds alongside other *.frb sound packs;
    //    the registry's scanFolder filters for the *_kit.frb suffix) ───────
    drumKitRegistry.unloadAll();

    // Drums ship with GM and live inside it - <root>/sounds_gm/drums - so a user
    // who never downloads WORLD or ORIENTAL still has every kit.
    const auto drums = folderManager.getDrumsFolder();
    // Drum component blobs live under <root>/sounds_gm/drums (per-role subfolders
    // kicks/ snares/ toms/ cymbals/ percussion/ ...); scanFolder recurses and
    // groups them into virtual kits by family-prefix.
    {
        // Always call scanFolder: it safely returns 0 when the folder is
        // absent, so a wrong path costs nothing and needs no guard here.
        drumKitRegistry.scanFolder (drums, juce::String (kFixedAccessCode));

        // The SAMPLED-KIT folders sit alongside the component folders:
        //   sounds_gm/drums/126-000-036_Arabic Kit/
        // Same root, so the two catalogs can never disagree about where the
        // drums are.
        engine.getFullKitMap().scan (drums);
    }

    // ── WHICH KITS LOAD WHOLE, AND WHAT ANSWERS A MISS ───────────────────────
    //
    // The policy lives in FullKitMap; these hand the player its two questions.
    // Composed stays the default and the only fallback: nothing ever falls back
    // INTO a sampled kit, so a miss can never land somewhere with different
    // calibration and no per-element editing.
    stylePlayer.onLoadFullKit =
        [this] (int engineCh, int msb, int lsb, int pc, bool isPercSlot)
        {
            return tryLoadFullKit (engineCh, msb, lsb, pc, isPercSlot);
        };

    stylePlayer.onComposedFallbackPc =
        [this] (int msb, int lsb, int pc) { return composedFallbackPc (msb, lsb, pc); };

    // Melodic per-instrument library ("NNN-Name.frb"); the scan skips *_kit.frb
    // so kits and melodic voices never clash.
    // THREE PACKS, three folders, scanned recursively into one flag-keyed
    // library.  WORLD and ORIENTAL are optional downloads: a missing folder is
    // an empty pack, never an error.
    engine.setSoundLibraryFolders (folderManager.getGmSoundsFolder(),
                                   folderManager.getWorldSoundsFolder(),
                                   folderManager.getOrientalSoundsFolder(),
                                   juce::String (kFixedAccessCode));

    // EDM KIT base kits (.dsin - SAVE PAD AS BASE) live in the GM pack beside the
    // style presets.  This used to be set only inside rescanSoundLibrary(), which
    // nothing calls: the saver never knew where to write ("The sound library
    // folder is not set yet") and no saved base was ever read.  Set here, where
    // the libraries actually load - before the first style pools its kits.
    EdmKitFiles::setBaseKitFolder (folderManager.getGmSoundsFolder()
                                                .getChildFile ("style_instruments_presets"));

    // A duplicate instrument number means a saved set can resolve to the wrong
    // sound, and nothing on screen would ever say so - the report is the only
    // way it becomes visible.
    if (const auto clashes = engine.getLibraryCollisions(); clashes.isNotEmpty())
        folderManager.getRootFolder().getChildFile ("grex_sound_library.txt")
                     .replaceWithText (clashes);

    // ── User default-preset files ─────────────────────────────────────────
    //   <pack>/instruments_presets\        .ins (melodic, SOLO) + .drm (kits)
    //   <pack>/style_instruments_presets\  .sins (melodic, STYLE)
    //
    // Both sit beside the sounds folder.  Without these scans the engine's
    // preset maps stay EMPTY, so every program change (and every selector click
    // that goes through the engine) silently ignores the saved SlotParams /
    // DrumKitParams the user wrote via "Save as Default".  This is the only
    // entry point at startup and on every folder change, so the scans MUST
    // live here.
    // One instruments_presets folder per installed pack, merged - a pack ships
    // its sounds and their voicing in one folder, so both arrive and leave
    // together.  Three of them now, one per pack.
    engine.setInstrumentPresetFolders      (allInstrumentPresetsFolders());
    engine.setStyleInstrumentPresetFolders (allStyleInstrumentPresetsFolders());

    // Give the right hand a voice out of the box: default every solo channel
    // to GM 000 (piano) -- or the first available flag -- so the upper zone
    // sounds immediately, before the user picks an instrument.
    {
        const auto flags = engine.getInstrumentFlags();
        if (! flags.empty())
        {
            const int def = engine.hasInstrumentFlag (0) ? 0 : flags.front();
            for (int s = 0; s < Betel::SamplePlayerEngine::kNumSoloChannels; ++s)
                engine.selectChannelPreset (Betel::SamplePlayerEngine::kNumStyleChannels + s, def);
        }
    }

    // ── Styles search roots ──────────────────────────────────────────────
    stylesFolders.clear();
    const auto styles = folderManager.getStylesFolder();
    if (styles.isDirectory())
        stylesFolders.push_back (styles);

    // ── RE-OPEN THE BLOB LIBRARY.  THIS LINE WAS MISSING, AND IT IS THE WHOLE
    //    REASON "LOCATE SYSTEM FOLDER" APPEARED NOT TO WORK ─────────────────
    //
    // openStyleLibrary() was called in exactly one place: the processor
    // constructor.  So the library was mapped ONCE, from whatever root was
    // resolved at startup, and nothing ever re-mapped it.
    //
    // Relocating the folder therefore did everything EXCEPT the one thing the
    // user pressed the button for.  The new root was saved to the locator, the
    // drum kits and the sound library were re-scanned, GrexPaths moved, the LED
    // went green - and `styleLibrary` still held whatever it had found (usually
    // nothing) at construction.  rescanStyleBrowser then faithfully published
    // that empty library to the browser.
    //
    // Every symptom pointed at the locator and none of them were the locator:
    // it had saved the path correctly, and a plugin RELOAD picked the styles up
    // immediately, because the constructor is where the one call lived.
    //
    // Safe to re-map with a style loaded and playing: readStyleById copies the
    // bytes out and StyleLoader parses them into a StyleData, so a loaded style
    // owns its data and holds no pointer into the mapping we are replacing.
    openStyleLibrary();

    // ── AND FORGET THE CACHED LICENCE CARRIER KEY ────────────────────────
    //
    // Same reasoning as the library above, for the same reason.  The carrier
    // (grex_ambience.wav) lives under GrexPaths::root(), and the registration
    // code caches the key it extracts so a per-track plugin instance does not
    // re-read a megabyte of WAV every time.
    //
    // On a clean install the constructor rescans BEFORE the root exists, so
    // checkRegistration finds no carrier and caches "no key".  Without this
    // line the user then locates the folder, the carrier is sitting right
    // there, and registration still fails for the rest of the session.
    RegistrationManager::getInstance().forgetCarrierKey();

    // ── AND EVERY OTHER FILE THAT LIVES UNDER THE ROOT ───────────────────────
    //
    // The style library was not the only thing cached against a root that had
    // not been chosen yet.  grex_master.xml, grex_cc_map.xml,
    // grex_funkey.xml, grex_bigdrums.xml, grex_style_favorites.xml and
    // grex_crash.xml were ALL read exactly once, in the constructor, and none
    // of them were re-read when the root moved.
    //
    // On a clean install that is six files falling back to defaults, then the
    // user locating the folder that holds the real ones, and every one of them
    // staying wrong until the plugin is reloaded: the master settings, the CC
    // rig, both macro presets, the style stars and the crash config.
    //
    // MUST STAY LAST IN THIS FUNCTION.  GrexPaths::setRoot runs at the top, and
    // every file above is resolved through it - reading them before the root
    // moves would re-read the OLD folder and cache it as the new answer, which
    // is worse than not re-reading at all.
    reloadRootBackedSettings();
}

void BetelgeuseProcessor::addStylesFolder (const juce::File& folder)
{
    if (! folder.isDirectory()) return;
    for (const auto& f : stylesFolders)
        if (f == folder) return;        // de-dupe
    stylesFolders.push_back (folder);
}

std::vector<BetelgeuseProcessor::StyleSearchResult>
BetelgeuseProcessor::searchStyles (const juce::String& keyword) const
{
    // ── SEARCHES THE LIBRARY, NOT THE DISK ───────────────────────────────────
    //
    // This used to walk stylesFolders with findChildFiles and a filter of
    // "*.sty;*.prs;*.bcs;*.sst;*.pst;*.pcs;*.fps;*.scp;*.aus" - every Yamaha SFF
    // extension.  GREX'S LIBRARY IS LOOSE .fgt FILES, so that filter matched
    // nothing, the scan returned an empty list, and EVERY search came back empty
    // for every keyword.  Reported as "I added the keyword pop and it didn't find
    // anything"; "pop" was never the problem.
    //
    // It also returned f.getFullPathName() as the ref, while the grid is fed
    // from getStylesInGenre, which returns the STYLE ID (the filename stem).
    // StylesTab::selectStyleByRef does stylePaths.indexOf(ref) against those
    // stems, so an absolute path could never match one: even with the extensions
    // fixed, clicking a result would have said "That style is no longer in the
    // library."  TWO faults, and fixing only the visible one leaves the second.
    //
    // Both go away by reading styleLibrary.allEntries() - the same source
    // getStylesInGenre uses.  One authority for what exists, so a search result
    // and a grid cell cannot disagree, and the ref is right by construction.
    // The old de-dupe pass is gone with it: the library is already unique.
    std::vector<StyleSearchResult> results;

    // ── ALL TOKENS MUST MATCH, IN ANY ORDER ──────────────────────────────────
    //
    // A plain containsIgnoreCase on the whole string only finds "pop ballad" if
    // the name has those two words adjacent and in that order.  Splitting on
    // whitespace and requiring every token lets "ballad pop" and "80s pop" find
    // "80s Pop Ballad", which is how anyone actually types a half-remembered
    // name.  An empty keyword still returns the whole library.
    juce::StringArray tokens;
    tokens.addTokens (keyword.trim(), false);
    tokens.removeEmptyStrings();

    for (const auto& e : styleLibrary.allEntries())
    {
        bool matches = true;
        for (const auto& tok : tokens)
            if (! e.displayName.containsIgnoreCase (tok)) { matches = false; break; }

        if (matches)
            results.push_back ({ e.displayName, e.styleId });
    }

    std::sort (results.begin(), results.end(),
               [] (const StyleSearchResult& a, const StyleSearchResult& b)
               { return a.displayName.compareIgnoreCase (b.displayName) < 0; });

    return results;
}

//==============================================================================
// OPEN THE LIBRARY. Called once, from the constructor.
//
// A file that fails to open is LOGGED AND SKIPPED, never fatal. Losing one style
// is recoverable by replacing one file; refusing to start because of it is not,
// and a player mid-gig cannot debug a style folder.
//
// THE SCAN IS RECURSIVE AND THE SUBFOLDERS ARE THE CATEGORIES - see
// FstLibrary.h.  <root>/styles/Latin/Bossa Nova.fgt files under "Latin"; a style
// sitting loose in <root>/styles files under GENERAL.
//==============================================================================
void BetelgeuseProcessor::openStyleLibrary()
{
    juce::StringArray problems;
    const auto stylesFolder = folderManager.getStylesFolder();

    styleLibrary.openFolder (stylesFolder, problems);

    // ── AN EMPTY LIBRARY IS A FAILURE AND MUST SAY SO ────────────────────────
    //
    // `problems` only collects files that failed to OPEN, so the two commonest
    // ways to end up with no styles - the folder is not there, or it holds no
    // *.fgt - would produce an empty problems list, no report, and a browser
    // that came up blank with nothing anywhere saying why. "Nothing failed" and
    // "there was nothing to try" are the same answer, and only one is healthy.
    //
    // The report is therefore written when the library is EMPTY as well, and it
    // names the folder that was searched and lists what is actually in it -
    // because the answer is nearly always visible the moment you see the path
    // the plugin looked at next to the path you put the styles in.
    const bool empty = styleLibrary.isEmpty();

    if (! problems.isEmpty() || empty)
    {
        juce::String report;
        report << "GREX - STYLE LIBRARY REPORT" << juce::newLine
               << juce::Time::getCurrentTime().toString (true, true) << juce::newLine
               << juce::newLine
               << "root folder   : " << folderManager.getRootFolder().getFullPathName()
               << (folderManager.isRootFolderValid() ? "" : "   <-- DOES NOT EXIST") << juce::newLine
               << "styles folder : " << stylesFolder.getFullPathName()
               << (stylesFolder.isDirectory() ? "" : "   <-- DOES NOT EXIST") << juce::newLine
               << "looking for   : " << Betel::Fst::kFileWildcard
               << "  (recursively, one category per subfolder)" << juce::newLine
               << "styles loaded : " << styleLibrary.getNumStyles() << juce::newLine
               << "rejected      : " << problems.size() << juce::newLine
               << juce::newLine;

        for (const auto& p : problems)
            report << "REJECTED: " << p << juce::newLine;

        if (empty)
        {
            report << juce::newLine << "NO STYLES LOADED." << juce::newLine << juce::newLine;

            if (! stylesFolder.isDirectory())
            {
                report << "The styles folder above does not exist." << juce::newLine
                       << "Either put the .fgt files there, or press LOCATE SYSTEM FOLDER in"
                       << juce::newLine
                       << "the plugin header and pick the folder that CONTAINS the styles folder."
                       << juce::newLine;
            }
            else
            {
                // WHAT IS ACTUALLY IN THERE, not what should be. A style still
                // named .sty, or one with a second extension appended by a
                // download, is invisible to the glob and completely visible in
                // this list.
                // RECURSIVE, to match the scan.  Listing only the top level
                // would show an organised library as "3 file(s)" - the three
                // folder entries findChildFiles does not return - and say
                // nothing at all about the 200 styles inside them.
                const auto found = stylesFolder.findChildFiles (juce::File::findFiles, true, "*");
                report << "The folder exists but holds no readable *.fgt. It contains "
                       << found.size() << " file(s):" << juce::newLine;
                for (const auto& f : found)
                    report << "   " << f.getRelativePathFrom (stylesFolder) << juce::newLine;
            }
        }

        folderManager.getRootFolder().getChildFile ("grex_style_library.txt")
                     .replaceWithText (report);
    }
}

juce::StringArray BetelgeuseProcessor::getStyleGenres() const
{
    // ONE PER SUBFOLDER OF <root>/styles, alphabetical.  Derived from what was
    // actually SCANNED rather than from the folders on disk, so an empty folder
    // does not produce a category button with nothing behind it.
    return styleLibrary.getGroups();
}

std::vector<BetelgeuseProcessor::StyleSearchResult>
BetelgeuseProcessor::getStylesInGenre (const juce::String& genre) const
{
    // The `absolutePath` field carries a STYLE ID, which is now the file's PATH
    // RELATIVE TO THE STYLES FOLDER without its extension - "Latin/Bossa Nova".
    // The browser never looks inside that string - it takes it, shows the name
    // beside it and hands it back on selection.
    //
    // THE ID IS THE PATH AND THE DISPLAY NAME IS THE STEM, which is the one
    // place they differ.  A bare stem would collide the first time two folders
    // both held a "Slow Rock", and one of them would then shadow the other in
    // every set that referenced it, silently.
    std::vector<StyleSearchResult> out;

    for (const auto& e : styleLibrary.allEntries())
        if (e.group == genre)
            out.push_back ({ e.displayName, e.styleId });

    std::sort (out.begin(), out.end(),
               [] (const StyleSearchResult& a, const StyleSearchResult& b)
               { return a.displayName.compareIgnoreCase (b.displayName) < 0; });
    return out;
}

//==============================================================================
bool BetelgeuseProcessor::loadStyleByRef (const juce::String& idOrPath,
                                          juce::String& errorMsg)
{
    if (idOrPath.isEmpty()) { errorMsg = "No style selected."; return false; }

    // ── 1. THE LIBRARY FIRST ─────────────────────────────────────────────────
    //
    // Resolved through FstLibrary::find, which matches an id, then a display
    // name, then the tail of a LEGACY BLOB ID - so a set saved while the blobs
    // existed still finds its style without anything being rewritten.
    if (const auto* e = styleLibrary.find (idOrPath))
    {
        auto fresh = std::make_unique<Betel::StyleData>();
        if (! Betel::Fst::loadFromFile (e->file, *fresh, errorMsg))
            return false;

        adoptFreshStyle (std::move (fresh), e->file, e->styleId);
        // THE LIBRARY'S id, not the string that was asked for. A legacy blob id
        // resolves through find() but must not be stored again, or the set that
        // carried it would keep carrying it forever.
        currentStyleRef     = e->styleId;
        lastLoadedStylePath = e->styleId;
        return true;
    }

    // ── 2. A LEGACY PATH ─────────────────────────────────────────────────────
    //
    // Every set written before the blobs stores a real filesystem path. Trying
    // it here is the whole migration: an old set keeps working from loose files
    // if they are still there, and the moment it is re-saved it carries an id
    // instead. Nothing is rewritten behind the player's back.
    const juce::File f (idOrPath);
    if (f.existsAsFile() && loadStyle (f, errorMsg))
    {
        currentStyleRef = f.getFullPathName();
        return true;
    }

    errorMsg = "Style not found in the library: " + idOrPath;
    return false;
}

bool BetelgeuseProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::mono();
}

void BetelgeuseProcessor::prepareToPlay(double sampleRate, int blockSize)
{
    engine.prepareToPlay(sampleRate, blockSize);
    sequencer.prepareToPlay(sampleRate);
    // K-weighting coefficients are re-derived for THIS rate rather than using
    // the spec's 48 kHz table, so the meter is exact at 44.1 / 88.2 / 96 too.
    styleLoudness.prepare(sampleRate);
    // The Finisher's detector/smoothing coefficients and filter set are all
    // sample-rate dependent — without this call it would run on the 44.1 kHz
    // construction defaults (and, before this fix, it was never run at all).
    finisher.prepare(sampleRate, blockSize);
    emittedEvents.reserve (512);
}

void BetelgeuseProcessor::releaseResources()
{
    // Commit and persist whatever the meter learned this session — the
    // in-flight section would otherwise be lost on every close.
    styleLoudness.commitPending();
    styleLoudness.save();

    engine.releaseResources();
    sequencer.releaseResources();
}

bool BetelgeuseProcessor::loadStyle (const juce::File& file, juce::String& errorMsg)
{
    auto fresh = std::make_unique<Betel::StyleData>();
    if (! Betel::StyleLoader::loadFromFile (file, *fresh, errorMsg))
        return false;

    // UNIFIED PATH: the parsed CASM is fed to the dispatcher AS AUTHORED —
    // real NTR / NTT / mutes intact.  The former Betelgeuse pass rewrote every
    // non-drum entry to uniform TRANS/MELODY/CMaj7, which flattened NTT=Chord
    // comp parts to melodic transposition; that let their passing/colour notes
    // play (and clash on minor chords) instead of snapping to chord tones.
    // Leaving the CASM untouched is what makes chord parts behave like the
    // hardware.  Multi-source variant selection now happens in StylePlayer's
    // dispatch gate (per-quality winner / chord-mute), so no load-time rewrite
    // is needed here.

    adoptFreshStyle (std::move (fresh), file, file.getFullPathName());
    lastLoadedStylePath = file.getFullPathName();
    currentStyleRef     = file.getFullPathName();
    return true;
}

//==============================================================================
// Everything that happens AFTER a style is parsed, wherever the bytes came from.
//
// Split out when the blob path arrived: two copies of "retire the old one, adopt
// the new one, re-voice, reset the tracker" would have drifted the first time
// one of those steps changed, and the symptom would be a style that behaves
// differently depending on whether it came from a file or a blob.
//==============================================================================
//==============================================================================
// WHERE A STYLE'S SET LIVES.  Moved here from MainComponent - see Main.h.
//==============================================================================
juce::File BetelgeuseProcessor::setFileForStyleRef (const juce::String& styleRef) const
{
    if (styleRef.isEmpty()) return {};
    if (! folderManager.isRootFolderValid()) return {};

    // A LIBRARY ID, which is what this receives almost always.  Betel::
    // setFileNameForStyle is the SAME function the baker names sets with; two
    // copies of that rule would differ by a dash one day and the sets would
    // exist but never be found, which looks correct on disk and is not.
    if (const auto* e = styleLibrary.find (styleRef))
        return folderManager.getSetsFolder()
                 .getChildFile (e->group)
                 .getChildFile (Betel::setFileNameForStyle (e->displayName) + ".bset");

    // A LEGACY PATH, from a set written before the library existed.
    const juce::File style (styleRef);
    const auto genre = style.getParentDirectory().getFileName();
    if (genre.isEmpty()) return {};

    return folderManager.getSetsFolder()
             .getChildFile (genre)
             .getChildFile (style.getFileNameWithoutExtension() + ".bset");
}

juce::ValueTree BetelgeuseProcessor::setTreeForStyleRef (const juce::String& styleRef) const
{
    const auto f = setFileForStyleRef (styleRef);
    if (! f.existsAsFile()) return {};

    // A SET THAT WILL NOT PARSE IS NO SET.  Returning an invalid tree here is
    // what lets CC 7 seed the faders anyway, which is the right answer: a
    // corrupt file must not leave every slot silent.
    if (auto xml = juce::XmlDocument::parse (f))
        return juce::ValueTree::fromXml (*xml);

    return {};
}

juce::ValueTree BetelgeuseProcessor::mixerSetForStyleRef (const juce::String& styleRef) const
{
    return setTreeForStyleRef (styleRef).getChildWithName ("MixerState");
}

void BetelgeuseProcessor::adoptFreshStyle (std::unique_ptr<Betel::StyleData> fresh,
                                           const juce::File& sourceFile,
                                           const juce::String& styleRef)
{
    if (currentStyle != nullptr)
        retiredStyles.push_back (std::move (currentStyle));

    currentStyle = std::move (fresh);
    sequencer.setStyle (currentStyle.get());

    // THE FADER RESET IS NOT HERE ANY MORE.
    //
    // It was, and the probe caught what that cost: file / engine / fader all
    // agreed at LOAD, and three seconds later the engine read unity on every
    // style AND solo slot.  resetUserVolumesToUnity touches both blocks, and
    // adoptFreshStyle is the only thing that calls it - so the style was being
    // adopted AGAIN after the set had finished restoring, with no set behind
    // that adoption to put the faders back.
    //
    // Tying the reset to "a style was adopted" was the mistake: a style can be
    // adopted several times for one user action (Rob saw the set load itself
    // run twice).  It now lives with the ACTION instead - applySetPayload
    // resets immediately before applying the set's own MixerState, and the
    // bare-style path resets because there is no set coming.  However many
    // times the style is re-adopted, nothing wipes a restored mixer.

    // ── balance.grexv, RE-READ ON EVERY STYLE LOAD ───────────────────────────
    //
    // BEFORE applyVoiceSetup, so the setup's own program changes pick up the
    // freshly-read trims rather than the previous file's. Re-reading here is
    // what makes the calibration loop workable: save the file, switch style,
    // hear it - no restart. It is a small text file, and a style load is
    // already the most expensive thing the plugin does.
    stylePlayer.reloadBalanceFile();

    // ── DOES A SET OWN THIS STYLE'S MIXER? ───────────────────────────────────
    //
    // ASKED BEFORE applyVoiceSetup, WHICH IS THE ENTIRE POINT.  The style's CC 7
    // numbers were written for Yamaha's oscillators; ours are calibrated per
    // instrument already, so CC 7 is a STARTING POINT and nothing more.  If a
    // set states the levels, the style never gets to state them at all - not
    // even for the instant it would take the set to overwrite them.
    //
    // The node, not the file - see mixerSetForStyleRef.
    const juce::ValueTree mixerSet = mixerSetForStyleRef (styleRef);
    const bool setOwnsMixer = mixerSet.isValid();

    stylePlayer.setStyleCc7Suppressed (setOwnsMixer);

    stylePlayer.applyVoiceSetup (*currentStyle);

    // ── AND APPLY IT, HERE, WITHOUT WAITING FOR AN EDITOR ────────────────────
    //
    // A DAW project reopened with the window closed never reaches
    // MainComponent::applySetPayload, and with CC 7 suppressed there would then
    // be nothing at all setting these faders.  The editor still applies the FULL
    // payload when it is open - slots, FX, the ducker - and repeating the levels
    // is harmless because both write the same numbers.
    //
    // UNITY FIRST, exactly as the editor does it: applyMixerState treats an
    // absent value as "leave it alone", so without the reset a set that does not
    // mention a slot would inherit the previous style's fader.
    if (setOwnsMixer)
    {
        resetUserVolumesToUnity();
        applyMixerState (mixerSet);
    }

    chordTracker.reset();

    // ── THE STYLE'S OWN TEMPO BECOMES THE BASE, NOT THE ANSWER ───────────────
    //
    // In FREE mode the sequencer reads manualBPM directly, so without this the
    // previous style's (or the 120 default) tempo would persist and the new
    // style would play at the wrong speed.  In SYNCED mode manualBPM isn't used
    // for playback, but setting it keeps the TEMPO knob correct for when the
    // user switches back.
    //
    // WHAT CHANGED: this used to store the style's tempo straight into
    // manualBPM, which threw away whatever the player had dialled in.  It now
    // moves the BASE and lets setStyleBaseBPM re-resolve, so a held offset
    // rides onto the new style - style 90 with +20 held plays at 110.  The
    // offset itself is untouched here; only RESET TEMPO and a set load move it.
    //
    // A style with no tempo meta event (originalBPM <= 0) falls back to 120,
    // matching what the SYNCED branch in processBlock assumes for the same case.
    setStyleBaseBPM (currentStyle->originalBPM > 0.0f ? currentStyle->originalBPM
                                                      : 120.0f);

    // Hand the meter the new file.  This commits and saves whatever the
    // OUTGOING style had learned, then loads this file's cached measurements if
    // it has any — keyed on path + size + modification date.
    //
    // GUARDED, because a style out of a blob HAS no file: the cache key does not
    // exist for it, and handing the meter an invalid File would ask it to key on
    // nothing. Skipping is harmless — the section trim has been hard-wired to
    // unity since MAKEUP was removed, so the measurement feeds nothing today.
    if (sourceFile.existsAsFile())
        styleLoudness.setStyle (sourceFile);

    lastMeasuredSection = -1;
    engine.setStyleSectionTrim (1.0f);
}

juce::ValueTree BetelgeuseProcessor::captureGlobalState() const
{
    juce::ValueTree t ("GlobalState");
    t.setProperty ("activeSoloSlot",   activeSoloSlot.load(),    nullptr);

    // splitPoint is DELIBERATELY not written.  Where this player's two hands
    // divide is a property of the PLAYER, not of the song: it follows the reach
    // and the habit of whoever is at the keyboard, and it must survive loading
    // somebody else's set the same way the pitch-bend range and the solo base
    // unity do.  Betel::MasterSettings owns it and persists it to
    // grex_master.xml the instant the knob moves.  Same reasoning, and the same
    // shape, as cc0..cc4 and the crash settings below.
    t.setProperty ("currentStylePath", lastLoadedStylePath,      nullptr);
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
        t.setProperty ("subFlag" + juce::String (ch),
                       stylePlayer.getSlotSubstitution (ch), nullptr);
    t.setProperty ("comments",         comments,                 nullptr);

    // HARMONY IS PART OF A SET, unlike the split point above.  Which harmony a
    // song wants - whether it is on at all, duet or block, how loud - is a
    // property of the ARRANGEMENT, not of the player: a ballad wants a soft
    // duet where the next number wants none. The split point is the opposite
    // case and is deliberately absent for exactly that reason.
    t.setProperty ("harmonyOn",    harmonyOn.load(),                nullptr);
    t.setProperty ("harmonyType",  harmonyType.load(),              nullptr);
    t.setProperty ("harmonyBelow", harmonyBelow.load(),             nullptr);
    t.setProperty ("harmonyLevel", harmonyLevel.load(),             nullptr);

    // MULTI SPLIT is a set property for the same reason harmony is: where the
    // zones fall belongs to the ARRANGEMENT. An organ number wants pedals; the
    // next number may not.
    t.setProperty ("multiSplitOn",   multiSplitOn.load(),           nullptr);
    t.setProperty ("bassSplitPoint", bassSplitPoint.load(),         nullptr);
    t.setProperty ("bassZoneSlot",   bassZoneSlot.load(),           nullptr);

    // The two bass switches, saved beside the rest. Which bass a number wants
    // is an arrangement property like the others here.
    t.setProperty ("bassInversionOn",   bassInversionOn.load(),     nullptr);
    t.setProperty ("bassInversionMode", bassInversionMode.load(),   nullptr);
    t.setProperty ("manualBassOn",      manualBassOn.load(),        nullptr);

    // THE MIDI CC LEARN MAP IS NOT PART OF A SET.  It lives in grex_cc_map.xml
    // and describes THE RIG — which physical knob is wired to which function.
    // Nothing about that is a property of a song, and a set that carried it
    // silently re-mapped the player's controller (or killed it outright, if the
    // set came from a rig where nothing had been learned).  See CcMap.h.
    // CRASH SETTINGS ARE NOT PART OF A SET.  They live in grex_crash.xml and
    // belong to the installation, like the macro presets and the style levels —
    // see saveCrashSettings() in Main.h, which already said so.  The set copy
    // that used to be written here outlived that decision and quietly undid it:
    // applyGlobalState restored it on every set load, so loading any set saved
    // before CRASH ON TRANSITION was switched on turned it straight back off,
    // and grex_crash.xml only ever won at editor construction (where
    // loadCrashSettings runs last).  One owner now, so nothing can flip it
    // behind the tab.

    // ── Session-wide performance state ────────────────────────────────────
    t.setProperty ("globalTranspose",  globalTranspose.load(),          nullptr);
    // TEMPO: THE SET OWNS THE OFFSET, NOT THE NUMBER.  See the tempo block in
    // Main.h for why.  `userBpmDelta` is what applyGlobalState reads back.
    t.setProperty ("userBpmDelta",     (double) userBpmDelta.load(),    nullptr);
    // Written under a NEW NAME.  The old "styleEnergy" was 0..100 with 50
    // neutral; 47 on that scale and 47 on this one are different settings, and a
    // silently reinterpreted value is worse than one that fails to load.
    t.setProperty ("styleEnergy200",   getStyleEnergy(),                nullptr);
    // `manualBPM` is still written, and is now PURELY INFORMATIONAL from this
    // build's point of view - the resolved tempo at save time, for a human
    // reading the .bset and for Grex, which shares this file format and still
    // reads the absolute.  Never read it back HERE when userBpmDelta is
    // present, or a set would re-impose the tempo of the style it was saved
    // against, which is the whole bug this change exists to remove.
    t.setProperty ("manualBPM",        (double) manualBPM.load(),       nullptr);
    t.setProperty ("tempoSpeedMult",   (double) tempoSpeedMult.load(),  nullptr);
    t.setProperty ("transitionQuant",  getTransitionQuant(),            nullptr);
    t.setProperty ("fillLength",       getFillLength(),                 nullptr);
    t.setProperty ("soloEnableMask",   (int) soloEnableMask.load(),     nullptr);
    t.setProperty ("pianoMode",        pianoMode.load(),                nullptr);

    // CHORD MODE and TEMPO FREE/SYNCED ARE NOT PART OF A SET.  They live in
    // grex_master.xml and belong to the PLAYER, like the crash settings above
    // and for the same reason: nothing in a song should be able to change how
    // you name a chord or who owns the clock.  A set that carried them could
    // silently put you in Fingered while the panel still read 1 FINGER, and
    // every melodic part would snap onto plain triad tones with nothing on
    // screen looking wrong.  See MasterSettings.h.
    //
    // An older set still carrying `fingeredChord` / `tempoSynced` is simply
    // ignored on load, which is the point — one owner, no argument.
    t.setProperty ("dawStartFollow",   dawStartFollow.load(),           nullptr);
    t.setProperty ("bypassInstrumentChain", getBypassInstrumentChain(), nullptr);

    // Per-channel style-element mutes (the 8 ON/OFF toggles in MainTab).
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
        t.setProperty ("mute" + juce::String (ch),
                       stylePlayer.isChannelMuted (ch), nullptr);

    // Oriental / Arabic scale cents on the solo (right hand) channels.
    // Only non-zero values are written to keep the set file slim.
    for (int s = 0; s < Betel::SamplePlayerEngine::kNumSoloChannels; ++s)
        for (int n = 0; n < 12; ++n)
        {
            const float c = engine.getChannelScaleTuningCents (
                Betel::SamplePlayerEngine::kNumStyleChannels + s, n);
            if (c != 0.0f)
                t.setProperty ("scale_" + juce::String (s) + "_" + juce::String (n),
                               (double) c, nullptr);
        }
    return t;
}

//==============================================================================
// MIXER SNAPSHOT.  Every fader the user can move, plus the Finisher.
//
// Captured from the PROCESSOR / ENGINE rather than from MixerTab, because those
// are the live owners — the tab is a view of them, and a set saved from the view
// would miss anything a CC or a style ride had changed underneath it.
//==============================================================================
juce::ValueTree BetelgeuseProcessor::captureMixerState() const
{
    juce::ValueTree t ("MixerState");

    // Per-channel faders: 8 style slots, then the solo slots.
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
        t.setProperty ("styleGain" + juce::String (ch),
                       (double) engine.getChannelVolume (ch), nullptr);

    for (int s = 0; s < Betel::SamplePlayerEngine::kNumSoloChannels; ++s)
        t.setProperty ("soloGain" + juce::String (s),
                       (double) engine.getChannelVolume (
                           Betel::SamplePlayerEngine::kNumStyleChannels + s), nullptr);

    // THE POST BASE TRIM, style side only - the right hand's base lives with the
    // instrument (.ins) and in the sound editor, where nothing competes for it.
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
        t.setProperty ("styleBaseDb" + juce::String (ch),
                       (double) engine.getChannelUserBaseDb (ch), nullptr);

    // Buses and master.  masterGain is the PRE-boost fader value — see
    // getMasterVolume for why reading the engine back would be wrong.
    t.setProperty ("styleBusGain", (double) getStyleVolume(),     nullptr);
    // What the STYLE VOLUME detent is worth for THIS song.  The solo base is
    // deliberately NOT here: that one is global, in grex_master.xml.
    t.setProperty ("styleBaseUnityDb", (double) getStyleBaseUnityDb(), nullptr);
    // What the STYLE VOLUME detent is worth for THIS song.  The solo base is
    // deliberately NOT here: that one is global, in grex_master.xml, because it
    // describes the install's balance between the two hands rather than a song.
    t.setProperty ("styleBaseUnityDb", (double) getStyleBaseUnityDb(), nullptr);
    t.setProperty ("soloBusGain",  (double) getRightHandVolume(), nullptr);
    t.setProperty ("masterGain",   (double) getMasterVolume(),    nullptr);
    t.setProperty ("masterBoostDb",(double) getMasterBoostDb(),   nullptr);

    // Finisher: on/off, the two macro controls, every slider and every stage
    // bypass.  It is a master-bus chain tuned per song, so all of it travels.
    // ── DUCKER — per set, because how much the band gets out of the way is a
    // property of the arrangement, not of the install.
    {
        auto& d = getDucker();
        t.setProperty ("duckOn", d.isEnabled(), nullptr);
        for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
        {
            const juce::String k = "duck" + juce::String (b);
            t.setProperty (k + "On",     d.isBandEnabled  (b),         nullptr);
            t.setProperty (k + "Freq",   (double) d.getFreq        (b), nullptr);
            t.setProperty (k + "Q",      (double) d.getQ           (b), nullptr);
            t.setProperty (k + "Depth",  (double) d.getDepthDb     (b), nullptr);
            t.setProperty (k + "Thresh", (double) d.getThresholdDb (b), nullptr);
            t.setProperty (k + "Ratio",  (double) d.getRatio       (b), nullptr);
            t.setProperty (k + "Atk",    (double) d.getAttackMs    (b), nullptr);
            t.setProperty (k + "Rel",    (double) d.getReleaseMs   (b), nullptr);
        }
    }

    t.setProperty ("finEnabled",   getFinisherEnabled(),   nullptr);
    t.setProperty ("finAmount",    (double) getFinisherAmount(), nullptr);
    // finCharacter is no longer written — the selector is gone (see Finisher.h).
    for (int i = 0; i < Betel::Finisher::kNumParams; ++i)
        t.setProperty ("finP" + juce::String (i),
                       (double) getFinisherParam (i), nullptr);
    for (int i = 0; i < Betel::Finisher::kNumStages; ++i)
        t.setProperty ("finS" + juce::String (i),
                       getFinisherStageEnabled (i), nullptr);

    return t;
}

void BetelgeuseProcessor::applyMixerState (const juce::ValueTree& ms)
{
    if (! ms.isValid() || ! ms.hasType ("MixerState")) return;

    // ABSENT MEANS UNCHANGED throughout, like applyGlobalState — so a set
    // written before a control existed leaves that control where it is instead
    // of snapping it to a default.
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
    {
        const auto key = "styleGain" + juce::String (ch);
        if (ms.hasProperty (key))
            engine.setChannelVolume (ch, (float) (double) ms.getProperty (key));
    }

    for (int s = 0; s < Betel::SamplePlayerEngine::kNumSoloChannels; ++s)
    {
        const auto key = "soloGain" + juce::String (s);
        if (ms.hasProperty (key))
            engine.setChannelVolume (Betel::SamplePlayerEngine::kNumStyleChannels + s,
                                     (float) (double) ms.getProperty (key));
    }

    // ABSENT MEANS UNCHANGED here too, and the style load has just zeroed these -
    // so a set written before the trim existed correctly leaves them at 0 dB.
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
    {
        const auto key = "styleBaseDb" + juce::String (ch);
        if (ms.hasProperty (key))
            engine.setChannelUserBaseDb (ch, (float) (double) ms.getProperty (key));
    }

    if (ms.hasProperty ("styleBusGain"))
        setStyleVolume    ((float) (double) ms.getProperty ("styleBusGain"));
    // ABSENT MEANS UNCHANGED does not serve here: a set written before this
    // existed was saved under an implicit 0 dB, so leaving the previous song's
    // base in place would re-voice it.
    setStyleBaseUnityDb ((float) (double) ms.getProperty ("styleBaseUnityDb",
                                                          (double) kDefaultStyleBaseUnityDb));
    // ABSENT MEANS UNCHANGED does not serve here: a set written before this
    // existed was saved under an implicit 0 dB, so leaving the previous song's
    // base in place would re-voice it. Default to 0 rather than to "whatever
    // happens to be loaded".
    setStyleBaseUnityDb ((float) (double) ms.getProperty ("styleBaseUnityDb",
                                                          (double) kDefaultStyleBaseUnityDb));
    if (ms.hasProperty ("soloBusGain"))
        setRightHandVolume((float) (double) ms.getProperty ("soloBusGain"));

    // Boost BEFORE the fader: both call applyMasterOut, and setting the fader
    // last means the value that lands on the engine already carries the boost
    // this set asked for rather than the previous song's.
    if (ms.hasProperty ("masterBoostDb"))
        setMasterBoostDb  ((float) (double) ms.getProperty ("masterBoostDb"));
    if (ms.hasProperty ("masterGain"))
        setMasterVolume   ((float) (double) ms.getProperty ("masterGain"));

    // ── DUCKER.  Every field DEFAULTS rather than "absent means unchanged":
    // a set written before the ducker existed was saved with no ducking at all,
    // and inheriting the previous song's carve would be audible and wrong.
    {
        auto& d = getDucker();
        d.setEnabled ((bool) ms.getProperty ("duckOn", false));
        for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
        {
            const juce::String k = "duck" + juce::String (b);
            const auto& def = Betel::StyleDucker::kDefaults[b];
            d.setBandEnabled  (b, (bool)  ms.getProperty (k + "On",     true));
            d.setFreq         (b, (float) (double) ms.getProperty (k + "Freq",   (double) def.freq));
            d.setQ            (b, (float) (double) ms.getProperty (k + "Q",      (double) def.q));
            d.setDepthDb      (b, (float) (double) ms.getProperty (k + "Depth",  (double) def.depth));
            d.setThresholdDb  (b, (float) (double) ms.getProperty (k + "Thresh", (double) def.thresh));
            d.setRatio        (b, (float) (double) ms.getProperty (k + "Ratio",  (double) def.ratio));
            d.setAttackMs     (b, (float) (double) ms.getProperty (k + "Atk",    (double) def.attack));
            d.setReleaseMs    (b, (float) (double) ms.getProperty (k + "Rel",    (double) def.release));
        }
    }

    if (ms.hasProperty ("finEnabled"))
        setFinisherEnabled ((bool) ms.getProperty ("finEnabled"));
    if (ms.hasProperty ("finAmount"))
        setFinisherAmount  ((float) (double) ms.getProperty ("finAmount"));
    // `finCharacter` in an older set is read and DISCARDED.  It named a preset
    // that has been deleted, and the eleven slider values it used to load are
    // saved in this same block anyway — so the set already carries the sound it
    // described, and re-applying the preset would overwrite it.

    for (int i = 0; i < Betel::Finisher::kNumParams; ++i)
    {
        const auto key = "finP" + juce::String (i);
        if (ms.hasProperty (key))
            setFinisherParam (i, (float) (double) ms.getProperty (key));
    }
    for (int i = 0; i < Betel::Finisher::kNumStages; ++i)
    {
        const auto key = "finS" + juce::String (i);
        if (ms.hasProperty (key))
            setFinisherStageEnabled (i, (bool) ms.getProperty (key));
    }
}

void BetelgeuseProcessor::applyGlobalState (const juce::ValueTree& gs)
{
    if (! gs.isValid()) return;

    setActiveSoloSlot ((int) gs.getProperty ("activeSoloSlot", getActiveSoloSlot()));
    comments = gs.getProperty ("comments", comments).toString();

    // Absent in a set written before harmony existed -> the CURRENT value is
    // the default, not a hard-coded one. A set that predates the feature says
    // nothing about it, so it must not silently switch it off underneath a
    // player who just turned it on.
    harmonyOn   .store ((bool) gs.getProperty ("harmonyOn",    harmonyOn.load()));
    harmonyBelow.store ((bool) gs.getProperty ("harmonyBelow", harmonyBelow.load()));
    harmonyType .store (juce::jlimit (0, Betel::Harmonizer::kNumTypes - 1,
                        (int) gs.getProperty ("harmonyType",  harmonyType.load())));
    harmonyLevel.store (juce::jlimit (0, 100,
                        (int) gs.getProperty ("harmonyLevel", harmonyLevel.load())));

    multiSplitOn  .store ((bool) gs.getProperty ("multiSplitOn", multiSplitOn.load()));

    // ABSENT MEANS UNCHANGED, like everything else here - and because the
    // default is 50, a set written before ENERGY existed leaves the band exactly
    // as authored.  Routed through the SETTER, not the atomic: the sixteen
    // channel pushes it performs are the entire point.
    // New name first; an older set carries the 0..100 form, which doubles onto
    // this scale exactly - 50 was neutral, 100 is neutral, and every point in
    // between maps one to two.
    if (gs.hasProperty ("styleEnergy200"))
        setStyleEnergy ((int) gs.getProperty ("styleEnergy200", getStyleEnergy()));
    else if (gs.hasProperty ("styleEnergy"))
        setStyleEnergy (2 * (int) gs.getProperty ("styleEnergy", 50));
    bassSplitPoint.store (juce::jlimit (0, 127,
                          (int) gs.getProperty ("bassSplitPoint", bassSplitPoint.load())));
    // setBassZoneSlot rather than a raw store: it refuses the harmony slot, and
    // an old or hand-edited set must not be able to put the pedals there.
    setBassZoneSlot ((int) gs.getProperty ("bassZoneSlot", bassZoneSlot.load()));

    bassInversionOn  .store ((bool) gs.getProperty ("bassInversionOn", bassInversionOn.load()));
    manualBassOn     .store ((bool) gs.getProperty ("manualBassOn",    manualBassOn.load()));
    bassInversionMode.store (juce::jlimit (0, 1,
                             (int) gs.getProperty ("bassInversionMode",
                                                   bassInversionMode.load())));

    // `splitPoint` is DELIBERATELY not read - see captureGlobalState.
    // MasterSettings owns it now, so a set written before this change is simply
    // ignored on that one property and cannot move the player's hands.  Reading
    // it here would be worse than merely wrong: applyGlobalState runs on the
    // message thread through the full setSplitPoint, which WRITES BACK to
    // MasterSettings - so one set load would not just change the split for now,
    // it would overwrite the saved split permanently.

    // `cc0..cc4` are DELIBERATELY not read — see captureGlobalState.  CcMap owns
    // them now, so an older set still carrying them cannot move the player's
    // controller assignments.

    // Crash settings are deliberately NOT read from the set — see
    // captureGlobalState.  An older set still carrying the five properties is
    // simply ignored, which is the point: grex_crash.xml is the only owner.

    // Re-load the named style file if it still exists; silently skip when
    // missing so the rest of the snapshot still applies cleanly.
    //
    // CRITICAL: skip the reload when this style is ALREADY the loaded one.
    // loadStyle() calls sequencer.setStyle(), which re-cues the sequencer to
    // bar 1 — so re-applying a snapshot that names the current style (e.g. the
    // editor re-applying default.bset every time its window is reopened) was
    // restarting playback from the top.  A genuine style change still loads.
    const juce::String path = gs.getProperty ("currentStylePath", juce::String()).toString();
    if (path.isNotEmpty())
    {
        // A LIBRARY STYLE IS STORED BY ITS ID ("Dance/6-8 Trance"), not a file
        // path - and this block used to reload FILES ONLY.  An id is not a file,
        // so a set saved by hand never loaded its style here, and the style
        // browser loaded it AFTER the whole set had been applied.  A new style
        // clears every slot lock and re-voices every slot, so what SoundsState
        // had just restored - an edited EDM KIT above all, but also a slot's
        // chosen instrument - was replaced by the style's own a moment later,
        // while the editor still showed the set's values.  loadStyleByRef takes
        // an id or a path, so the style now loads HERE, before SoundsState:
        // the order applySetPayload is built around.
        //
        // No juce::File is built from an id - a relative path is not a legal
        // File and asserts in a debug build.  An id the library no longer holds
        // is skipped, exactly like a missing file always was.
        const bool isFilePath    = juce::File::isAbsolutePath (path);
        const bool alreadyLoaded = hasStyle()
                                 && (path == lastLoadedStylePath
                                     || (isFilePath && juce::File (path).getFullPathName() == lastLoadedStylePath));
        if (! alreadyLoaded)
        {
            juce::String err;
            if (isFilePath)
            {
                const juce::File f (path);
                if (f.existsAsFile())
                    loadStyle (f, err);
            }
            else if (styleLibrary.find (path) != nullptr)
            {
                loadStyleByRef (path, err);
            }
        }
    }

    // Restore per-slot instrument substitutions on top of the (re)loaded style.
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
    {
        const int sub = (int) gs.getProperty ("subFlag" + juce::String (ch), -1);
        if (sub >= 0) stylePlayer.setSlotSubstitution (ch, sub);
        else          stylePlayer.clearSlotSubstitution (ch);
    }

    // ── Session-wide performance state ────────────────────────────────────
    setGlobalTranspose ((int)   gs.getProperty ("globalTranspose", globalTranspose.load()));
    // ── TEMPO, AND THE ORDER HERE MATTERS ────────────────────────────────
    //
    // This runs AFTER the style reload above, so styleBaseBPM already holds the
    // incoming style's own tempo.  Both branches therefore resolve against the
    // right base.
    //
    //   NEW SET  carries `userBpmDelta`: apply it and the tempo falls out as
    //            the new style's own + the offset the player saved.
    //
    //   OLD SET  carries only the absolute `manualBPM`.  Feeding it through
    //            setManualBPM reproduces exactly what that set used to do -
    //            it plays at the number it stored - AND derives the offset from
    //            it on the way past, so the next save is in the new form.  That
    //            is the whole migration; nothing rewrites anything on disk
    //            behind the player's back.
    //
    //   NEITHER  (a set from before either property, or a factory set) leaves
    //            the current offset alone, matching the ABSENT MEANS UNCHANGED
    //            rule the rest of this function follows.
    if (gs.hasProperty ("userBpmDelta"))
        setUserBpmDelta ((float) (double) gs.getProperty ("userBpmDelta",
                                                          (double) userBpmDelta.load()));
    else if (gs.hasProperty ("manualBPM"))
        setManualBPM    ((float) (double) gs.getProperty ("manualBPM",
                                                          (double) manualBPM.load()));
    setTempoSpeedMult  ((float) (double) gs.getProperty ("tempoSpeedMult",
                                                         (double) tempoSpeedMult.load()));
    setTransitionQuant ((int)   gs.getProperty ("transitionQuant", getTransitionQuant()));
    setFillLength      ((int)   gs.getProperty ("fillLength",      getFillLength()));
    setSoloEnableMask  ((juce::uint8) (int) gs.getProperty ("soloEnableMask",
                                                            (int) soloEnableMask.load()));
    setPianoMode       ((bool)  gs.getProperty ("pianoMode",       pianoMode.load()));

    // `fingeredChord` and `tempoSynced` are DELIBERATELY not read — see
    // captureGlobalState.  MasterSettings owns them now, so a set cannot move
    // them however old it is or whatever it happens to contain.
    setDawStartFollow  ((bool)  gs.getProperty ("dawStartFollow",  dawStartFollow.load()));
    setBypassInstrumentChain ((bool) gs.getProperty ("bypassInstrumentChain",
                                                     getBypassInstrumentChain()));

    // Style-element mutes (after the style reload so they land on top).
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
        stylePlayer.setChannelMute (ch,
            (bool) gs.getProperty ("mute" + juce::String (ch),
                                   stylePlayer.isChannelMuted (ch)));

    // Oriental scale cents on the solo channels: clear, then apply saved.
    for (int s = 0; s < Betel::SamplePlayerEngine::kNumSoloChannels; ++s)
    {
        const int ch = Betel::SamplePlayerEngine::kNumStyleChannels + s;
        engine.clearChannelScaleTuning (ch);
        for (int n = 0; n < 12; ++n)
        {
            const auto key = "scale_" + juce::String (s) + "_" + juce::String (n);
            if (gs.hasProperty (key))
                engine.setChannelScaleTuningCents (ch, n,
                    (float) (double) gs.getProperty (key, 0.0));
        }
    }
}

void BetelgeuseProcessor::tapTempo()
{
    // Drop the new timestamp into the small circular buffer.  Any tap that
    // lands more than 2 seconds after the previous one is treated as the
    // start of a fresh sequence (user paused, retried, etc.).
    const double now = juce::Time::getMillisecondCounterHiRes();
    constexpr double kRestartWindowMs = 2000.0;
    constexpr double kMinIntervalMs   =  200.0;   // 300 BPM
    constexpr double kMaxIntervalMs   = 2000.0;   //  30 BPM

    if (tapCount > 0 && (now - tapTimestamps[(size_t) ((tapCount - 1) % kTapBufferSize)]) > kRestartWindowMs)
        tapCount = 0;

    tapTimestamps[(size_t) (tapCount % kTapBufferSize)] = now;
    ++tapCount;

    if (tapCount < 2) return;     // need at least two taps to compute an interval

    // Average the last (tapCount-1, clamped to buffer-1) intervals.
    const int usable = juce::jmin (tapCount, kTapBufferSize);
    double sum   = 0.0;
    int    count = 0;
    for (int i = 1; i < usable; ++i)
    {
        const int idxA = (tapCount - usable + i)     % kTapBufferSize;
        const int idxB = (tapCount - usable + i - 1) % kTapBufferSize;
        const double dt = tapTimestamps[(size_t) idxA] - tapTimestamps[(size_t) idxB];
        if (dt >= kMinIntervalMs && dt <= kMaxIntervalMs)
        {
            sum += dt;
            ++count;
        }
    }
    if (count == 0) return;       // every interval was out of range

    const double avgMs = sum / (double) count;
    const float  bpm   = (float) juce::jlimit (30.0, 300.0, 60000.0 / avgMs);

    // THROUGH THE SETTER, NOT A BARE STORE.  A raw manualBPM.store() here would
    // move the resolved tempo and leave userBpmDelta describing the tempo the
    // player had BEFORE tapping - so the next style load would snap the tapped
    // tempo away, and saving the set would record an offset nobody asked for.
    // TAP is a tempo decision like the knob, and takes the same route.
    setManualBPM (bpm);
}

void BetelgeuseProcessor::auditionDrumKey (int engineChannel, int midiKey)
{
    if (midiKey < 0 || midiKey >= 128) return;
    // One-shot drum voices play to their natural end on a bare noteOn.
    engine.noteOn (engineChannel, midiKey, 110);
}

//==============================================================================
// Instrument-editor piano strip
//==============================================================================
void BetelgeuseProcessor::editorNoteOn (int engineChannel, int midiNote, int velocity)
{
    if (midiNote < 0 || midiNote >= 128) return;
    engine.noteOn (engineChannel, midiNote, juce::jlimit (1, 127, velocity),
                   /*fromEditor*/ true);
}

void BetelgeuseProcessor::editorNoteOff (int engineChannel, int midiNote)
{
    if (midiNote < 0 || midiNote >= 128) return;
    engine.noteOff (engineChannel, midiNote, /*fromEditor*/ true);
}

void BetelgeuseProcessor::getSoundingNotes (int engineChannel, uint32_t out[4]) const
{
    engine.getSoundingNotes (engineChannel, out);
}

void BetelgeuseProcessor::getPadActivity (int engineChannel, uint32_t out[4]) const
{
    engine.getPadActivity (engineChannel, out);
}

void BetelgeuseProcessor::triggerCrash()
{
    // Fire every crash element selected in the Crash tab (46 / 55 / 56 / 57)
    // on the DRUMS style slot (engine channel 0).  Drum voices are one-shot,
    // so a noteOn is enough — the kit's own decay rings each element out.
    // THE FOUR CYMBALS, by their real GM keys.
    //
    // Two of these were wrong.  Bit 0 fired note 46 under the label "CYMBAL 1",
    // but 46 is the OPEN HI-HAT — Crash Cymbal 1 is 49.  Bit 2 fired 56, which
    // is the COWBELL.  So half the toggles asked the kit for something that is
    // not a cymbal, and on a kit whose metal library maps only real cymbals they
    // asked for a key with nothing on it and fired silently.
    //
    // 55 and 57 keep their bits so an existing mask (and the default, bit 3 =
    // Crash 2) still means what it used to.
    // THE FOUR CYMBALS, by their real GM keys — see the note below on why two of
    // these were wrong before.
    static constexpr int kCrashNotes[4] = { 49, 55, 52, 57 };

    const juce::uint8 mask = crashNoteMask.load();
    const int         vel  = crashVelocity.load();   // fixed, from the Crash tab

    // The diagnostic that used to sit here is gone.  It opened and appended to a
    // file on EVERY trigger, from processBlock's tail — that is the audio thread,
    // and a disk write there is exactly the thing that must never happen no
    // matter how cheap it looks.  Its job is done: the cause was found.

    // CRASH GAIN.  Per voice, not per channel: the crash fires onto the DRUMS
    // slot, which it shares with the style's own kit, so anything channel-wide
    // would drag the whole kit along with it.
    const float g = getCrashGainLinear();

    // AN EDM KIT ON THE DRUMS SLOT: the four options are the kit's own crash
    // pads - the purple ones.  Every factory kit keeps them on these same four
    // keys, so nothing moves there; an edited kit whose crash now sits on
    // another pad still gets its crash (edm::crashPads).  Any other kit: GM.
    int keys[4] = { kCrashNotes[0], kCrashNotes[1], kCrashNotes[2], kCrashNotes[3] };
    engine.edmCrashKeys (0, kCrashNotes, keys);

    for (int i = 0; i < 4; ++i)
        if ((mask & (1u << i)) && keys[i] >= 0)
            engine.noteOn (0, keys[i], vel, /*fromEditor*/ false, g);
}

void BetelgeuseProcessor::performVariation (int variButtonIdx)
{
    // ── FLAG THE GESTURE FOR THE AUDIO THREAD TO RECORD ──────────────────────
    //
    // THE CHOKE POINT, and that is why it is here and not in the button handler
    // or in performRemote. A variation reaches this function from three places -
    // a mouse click, an assigned pad or CC through performRemote, and a legacy
    // control note - and recording at any one of them would miss the other two,
    // while recording at all three would record some presses twice.
    //
    // BUT IT IS NOT RECORDED HERE. This function runs on the MESSAGE thread for
    // a click and on the AUDIO thread for a pad, and the recorder's FIFO is
    // single-producer: two threads writing it is exactly the corruption that
    // kind of queue cannot survive. So the gesture is parked in one atomic and
    // the AUDIO thread picks it up, keeping one producer.
    //
    // One slot, so two gestures inside a single audio block would lose the
    // first. A block is a few milliseconds and these are button presses; a
    // human cannot produce two in that window, and the alternative is a second
    // queue to maintain for an event rate of a few per minute.
    if (variButtonIdx >= 0 && variButtonIdx < 16)
        pendingUiGesture.store (variButtonIdx, std::memory_order_relaxed);

    // Mirrors MainTab's 16-button indexing:
    //   0-2 INTRO 1-3, 3 INTRO 4 (no backing section), 4-7 VAR 1-4,
    //   8-11 FILL 1-4, 12 BRAKE, 13-15 END 1-3.
    if (! hasStyle()) return;

    if (variButtonIdx >= 0 && variButtonIdx <= 2)
    {
        // INTRO 1-3 — set pendingIntro.  When the style is stopped this is
        // consumed by the next start() so playback begins WITH the intro
        // (Yamaha style).  When the style is already playing, the boundary
        // check in applyPendingAtBarBoundary consumes it at the next quant
        // grid point, cutting from whatever's playing to the intro.  Either
        // way, the intro then plays through its full sequence and jumps to
        // its configured destination main.
        sequencer.triggerIntro (variButtonIdx);
    }
    else if (variButtonIdx == 3) { /* INTRO 4 — no backing section */ }
    else if (variButtonIdx >= 4 && variButtonIdx <= 7)
    {
        // VAR 1-4: go straight there, no fill on the way.
        sequencer.selectVariation (static_cast<Betel::StyleVariation> (variButtonIdx - 4), false);
    }
    else if (variButtonIdx >= 8 && variButtonIdx <= 11)
    {
        // FILL 1-4: the fill IS the request.  selectVariation carries the
        // destination; triggerFill still matters for the case where the target
        // equals the current variation, because the variation branch is a no-op
        // then and the plain-fill branch below it is what runs.
        sequencer.selectVariation (static_cast<Betel::StyleVariation> (variButtonIdx - 8), true);
        sequencer.triggerFill();
    }
    else if (variButtonIdx == 12)
        sequencer.triggerBreak();
    else if (variButtonIdx >= 13 && variButtonIdx <= 15)
        sequencer.triggerEnding (variButtonIdx - 13);
}

void BetelgeuseProcessor::togglePlayStop()
{
    if (sequencer.isPlaying()) { sequencer.stop(); return; }

    if (hasStyle()) { sequencer.start(); return; }

    // ── NO STYLE: SAY SO, DO NOT JUST SIT THERE ─────────────────────────────
    //
    // This used to be the silent tail of an `else if` and it is the single line
    // that has cost the most debugging time in this project.  With no style
    // loaded, PLAY produced no start, no lamp, no caption change and no sound -
    // from the mouse and from an assigned hardware pad alike, because both
    // arrive here.  That reads as a dead transport, so every investigation
    // started at the transport, and the fault was always upstream in style
    // loading (a styleless project snapshot one time, an orphaned factory-set
    // StyleLink the next).
    //
    // The flag makes the refusal VISIBLE and RECOVERABLE.  The editor drains it
    // and loads a style, then starts - so the button now does the obvious
    // thing.  Headless, nothing drains it and the behaviour is exactly as
    // before: an offline render with no style still renders nothing, which is
    // correct.
    notePlayRefusedNoStyle();
}

void BetelgeuseProcessor::toggleSyncPlay()
{
    auto& ct = chordTracker;
    if (ct.isSyncStartArmed()) ct.disarmSyncStart();
    else                       ct.armSyncStart();
}

void BetelgeuseProcessor::toggleHold()
{
    sequencer.setTransitionHold (! sequencer.isTransitionHeld());
}

void BetelgeuseProcessor::toggleStyleElement (int slot)
{
    if (slot < 0 || slot >= 8) return;
    stylePlayer.setChannelMute (slot, ! stylePlayer.isChannelMuted (slot));
}

//==============================================================================
// ONE ACTION PER REMOTE ID.
//
// Everything here is PROCESSOR state, and that is the rule this table enforces:
// a remote assignment may only drive something that works with no editor open.
// An offline bounce, or a project played without ever showing the window, must
// behave identically to a live one - the failure mode we chased repeatedly
// through the sound-editor and macro paths, and the reason the UI-only controls
// on the tabs are not in the id list at all.
//
// `value` is 0-127: a CC's value, or 127 for a pad hit. Continuous ids scale it;
// button ids treat anything over 63 as a press and ignore the rest, so a CC on
// a button behaves like a switch rather than firing twice per sweep.
//==============================================================================
void BetelgeuseProcessor::performRemote (Betel::RemoteId id, int value)
{
    using R = Betel::RemoteId;

    const bool pressed = value > 63;
    const auto norm    = juce::jlimit (0.0f, 1.0f, (float) value / 127.0f);

    const int i = (int) id;

    // ── MODE ─────────────────────────────────────────────────────────────────
    //
    // Push and Toggle differ ONLY in what a release does, so this is the one
    // place that has to know: in Push a release gives the control back, in
    // Toggle it is ignored and the press latches.  Knob never sees a release at
    // all - a sweep is values, not edges - so it falls straight through to the
    // continuous handlers below.
    const auto mode = Betel::RemoteMap::get().modeOf (id);

    if (! pressed && ! Betel::remoteIsContinuous (id))
    {
        if (mode == Betel::RemoteMap::Mode::Push)
            performRemoteRelease (id);
        return;
    }

    // ── the sixteen variation pads ───────────────────────────────────────────
    if (i >= (int) R::Intro1 && i <= (int) R::End3)
    {
        if (pressed) performVariation (i - (int) R::Intro1);
        return;
    }

    // ── the eight element mutes ──────────────────────────────────────────────
    if (i >= (int) R::Element1 && i <= (int) R::Element8)
    {
        if (pressed) toggleStyleElement (i - (int) R::Element1);
        return;
    }

    // ── the eight solo selectors ─────────────────────────────────────────────
    if (i >= (int) R::Solo1 && i <= (int) R::Solo8)
    {
        if (pressed) selectSoloSlot (i - (int) R::Solo1);
        return;
    }

    // ── mixer channel faders ─────────────────────────────────────────────────
    //
    // SILENCE TO UNITY, not silence to double. The existing ccMasterVol target
    // maps a CC to `norm` and stops at unity, and matching it matters more than
    // reaching the headroom: a controller that can double a gain will do it by
    // accident, and the faders' upper half is there for a deliberate hand.
    if (i >= (int) R::StyleCh1 && i <= (int) R::StyleCh8)
    {
        engine.setChannelVolume (i - (int) R::StyleCh1, norm);
        return;
    }
    if (i >= (int) R::SoloCh1 && i <= (int) R::SoloCh8)
    {
        engine.setChannelVolume (Betel::SamplePlayerEngine::kNumStyleChannels
                                     + (i - (int) R::SoloCh1), norm);
        return;
    }

    switch (id)
    {
        case R::PlayStop:  if (pressed) togglePlayStop();   break;
        case R::Restart:   if (pressed) requestRestart();   break;
        case R::SyncPlay:  if (pressed) toggleSyncPlay();   break;
        case R::Hold:      if (pressed) toggleHold();       break;
        case R::PianoMode: if (pressed) togglePianoMode();  break;

        // Ranges lifted VERBATIM from handleControllerMessage's five targets.
        // Two spellings of one mapping is how they drift, and a remote that
        // reaches a different tempo than the Settings-tab CC for the same knob
        // position would be indefensible.
        case R::Tempo:      setManualBPM (30.0f + norm * 270.0f); break;
        case R::Transpose:  setGlobalTranspose ((int) std::lround (norm * (2.0f * kMaxTranspose)
                                                                   - kMaxTranspose)); break;
        case R::SplitPoint: setSplitPoint ((int) std::lround (36.0f + norm * (127.0f - 36.0f))); break;

        case R::StyleVolume:  setStyleVolume     (norm); break;
        case R::SoloVolume:   setRightHandVolume (norm); break;
        case R::MasterVolume: setMasterVolume    (norm); break;
        // MASTER BOOST IS FIVE TICKBOXES, not a continuous control - 0/+3/+6/
        // +9/+12. A CC has to land ON one of them or the mixer would show no
        // box lit while the gain sat between two, which reads as broken.
        case R::MasterBoost:
        {
            static constexpr float kSteps[5] = { 0.0f, 3.0f, 6.0f, 9.0f, 12.0f };
            setMasterBoostDb (kSteps[juce::jlimit (0, 4, (int) std::lround (norm * 4.0f))]);
            break;
        }
        case R::FinisherOn:   if (pressed) setFinisherEnabled (! getFinisherEnabled()); break;

        // LIVE AGAIN, and GLOBAL: this is one number for the whole install, not
        // a per-style value - see StyleLevels.h.  A CC mapped here moves the
        // level of every style and writes grex_boost.xml as it goes.
        // THE SAME DOOR THE SLIDER USES - store, save and PUSH.  Writing the
        // value without pushing is what made the slider look dead.
        case R::StyleBoost:
            setStyleBoostDb (norm * Betel::StyleLevels::kBoostMaxDb);
            break;

        // ENERGY is 0..100 with 50 neutral, so a controller at its centre detent
        // lands on "as authored" - which is the position that has to be
        // reachable by feel on a hardware fader.
        case R::StyleEnergy: setStyleEnergy ((int) std::lround (norm * 200.0f)); break;

        // StyleFollow still STORES, and deliberately does nothing audible.
        // FOLLOW PROGRAMMED GAINS is retired - the style states the base and the
        // mixer trims after it, so there is no blend left to drive. The case is
        // kept rather than deleted so a controller already mapped to it does not
        // fall through to whatever the default arm does, and so the value still
        // round-trips into the .bset with every other set already on disk.
        case R::StyleFollow: Betel::StyleLevels::get().setStyleVolFollow (norm * 100.0f); break;

        // ── DELIBERATELY NOT ACTED ON HERE ───────────────────────────────────
        // OnPress, Crash, Fingered, DawStart, Comments, OrientalScale and
        // FunkeyMode all live in the EDITOR today. Assigning them
        // is allowed - the popup opens and the map stores it - but the action
        // has to be delivered by MainComponent while the window is open, and it
        // is honest for that to be visible here rather than hidden behind a
        // silent default case.
        default: break;
    }

    // Whatever moved, the UI has to catch up. The mirror already polls at 30 Hz,
    // so nothing is pushed from the audio thread; this only marks the fact.
    remoteTouched.store (true, std::memory_order_relaxed);
}

//==============================================================================
// THE RELEASE HALF OF PUSH MODE.
//
// Only a STATEFUL control has anything to give back.  Every action in the table
// below is already a toggle, so "restore" is simply the same call again - which
// is also why this cannot drift out of step with the press path.
//
// A variation pad, RESTART or CRASH is momentary by nature: it fired on the
// press and there is no state left over, so the release is correctly ignored
// rather than firing the thing twice.
//==============================================================================
void BetelgeuseProcessor::performRemoteRelease (Betel::RemoteId id)
{
    using R = Betel::RemoteId;
    const int i = (int) id;

    if (i >= (int) R::Element1 && i <= (int) R::Element8)
    {
        toggleStyleElement (i - (int) R::Element1);
        remoteTouched.store (true, std::memory_order_relaxed);
        return;
    }

    switch (id)
    {
        case R::PlayStop:   togglePlayStop();  break;
        case R::Hold:       toggleHold();      break;
        case R::PianoMode:  togglePianoMode(); break;
        case R::FinisherOn: setFinisherEnabled (! getFinisherEnabled()); break;
        default: return;    // momentary - nothing to restore, and no repaint
    }

    remoteTouched.store (true, std::memory_order_relaxed);
}

void BetelgeuseProcessor::handleControllerMessage (int ccNum, int value)
{
    // Learn mode: the next controller seen is bound to the armed target.
    const int learn = ccLearnTarget.load();
    if (learn >= 0 && learn < kNumCcTargets)
    {
        ccNumber[(size_t) learn].store (ccNum);
        ccLearnTarget.store (-1);
        ccDirty.store (true);   // tell the UI to refresh the assignment
        return;
    }

    // Apply: drive every target whose assigned controller matches.
    for (int t = 0; t < kNumCcTargets; ++t)
        if (ccNumber[(size_t) t].load() == ccNum)
            applyCcToTarget (t, value);
}

void BetelgeuseProcessor::applyCcToTarget (int target, int value)
{
    const float norm = (float) value / 127.0f;   // 0..1

    // EACH TARGET MAPS ONTO ITS CONTROL'S OWN RANGE, not onto the parameter's.
    // The two differ for SPLIT: the parameter accepts 0..127 but the knob only
    // goes 36..127, so a straight pass-through spent the bottom quarter of a
    // pedal's travel on split points the knob cannot show - and the mirror in
    // the editor would then clamp, leaving control and parameter disagreeing.
    switch (target)
    {
        case ccMasterVol:  setMasterVolume (norm);                       break;
        case ccStyleVol:   setStyleVolume  (norm);                       break;
        case ccTempo:      setManualBPM    (30.0f + norm * 270.0f);      break;  // 30..300
        case ccTranspose:  setGlobalTranspose ((int) std::lround (norm * (2.0 * kMaxTranspose)
                                                                 - kMaxTranspose)); break;  // +/-12, the knob's range
        case ccSplit:      setSplitPoint   ((int) std::lround (36.0f + norm * (127.0f - 36.0f)));
                           break;  // the SPLIT knob's range, 36..127
        default: return;   // unknown target: nothing moved, nothing to mirror
    }

    ccValueDirty.store (true);   // tell the editor to move the control
}

void BetelgeuseProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // ── DRAIN THE PARKED UI GESTURE ──────────────────────────────────────────
    //
    // Once per block, before anything else touches the recorder, so it keeps
    // exactly ONE producer - see performVariation for why the gesture cannot be
    // written where it happens.
    if (const int g = pendingUiGesture.exchange (-1, std::memory_order_relaxed); g >= 0)
        if (songRecorder.isRecording())
            songRecorder.recordRemote (perfSongBeats, g, 127);

    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // 1) Merge virtual on-screen keyboard events into the incoming MIDI buffer.
    keyboardState.processNextMidiBuffer(midi, 0, numSamples, true);

    // 2) Resolve the playback tempo.
    //
    //    SYNCED : host BPM (or the style's original BPM in standalone).
    //    FREE   : the manual BPM (TEMPO knob / TAP).
    //    Either way, multiply by the ×1 / ×2 / ׽ speed selector.
    //    The pre-multiplier base is cached in currentBaseBPM so the UI can
    //    show the live tempo on the (locked) knob while SYNCED.
    double base = 120.0;
    {
        bool   gotHostBPM = false;
        double host = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpm = pos->getBpm()) { host = *bpm; gotHostBPM = true; }

        if (tempoSyncedToHost.load())
            base = gotHostBPM ? host
                              : (currentStyle != nullptr && currentStyle->originalBPM > 0.0f
                                    ? (double) currentStyle->originalBPM : 120.0);
        else
            base = (double) manualBPM.load();
    }
    currentBaseBPM.store ((float) base);

    const double effectiveBPM = base * (double) tempoSpeedMult.load();
    sequencer.setHostBPM (effectiveBPM);

    // DAW START -- follow the host transport when enabled: a play edge starts
    // the style (current variation, from the top), a stop edge stops it.
    // Edge-detected, so the manual PLAY/STOP button keeps working while the
    // host transport is idle.
    if (dawStartFollow.load())
    {
        bool hostPlaying = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                hostPlaying = pos->getIsPlaying();

        if (hostPlaying && ! wasHostPlaying)
        {
            if (hasStyle() && ! sequencer.isPlaying()) sequencer.start();
        }
        else if (! hostPlaying && wasHostPlaying)
        {
            if (sequencer.isPlaying()) sequencer.stop();
        }
        wasHostPlaying = hostPlaying;
    }
    else
        wasHostPlaying = false;

    // 2b) MIDI performance recorder / player (RT-safe, no allocation).
    //
    //     Beat domain: a monotonic performance clock (perfSongBeats) advances by
    //     the block's beat length each call.  Recording timestamps incoming
    //     events against it; the recorder latches its own origin on the first
    //     action, so the absolute value is irrelevant.  Playback re-emits the
    //     captured stream — including the arranger control-notes — into the same
    //     'midi' buffer, so the dispatch below regenerates the whole performance.
    {
        const double sr            = juce::jmax (1.0, getSampleRate());
        const double beatsPerSample = (effectiveBPM / 60.0) / sr;

        if (songRecorder.isActive())
            for (const auto meta : midi)
            {
                const auto  m = meta.getMessage();
                const auto* d = m.getRawData();
                const int   n = m.getRawDataSize();
                if (n >= 1)
                    songRecorder.recordEvent (perfSongBeats + meta.samplePosition * beatsPerSample,
                                              (uint8_t)  d[0],
                                              (uint8_t) (n > 1 ? d[1] : 0),
                                              (uint8_t) (n > 2 ? d[2] : 0));
            }

        songPlayer.renderBlock (midi, numSamples, sr, effectiveBPM);

        perfSongBeats += numSamples * beatsPerSample;
    }

    // 3) Route incoming MIDI to the solo channel(s).
    //
    // The 8 SOLO ELEMENTS buttons in MainTab drive an 8-bit enable mask.
    // When the mask is non-zero, every above-split event is layered across
    // every enabled slot.  When the mask is zero (default at first launch
    // and after a clear), we keep the legacy single-slot routing via
    // activeSoloSlot so nothing breaks for users who never touch the row.
    // SLOT 7 (SOLO 8) IS THE HARMONY CHANNEL and the keyboard must never reach
    // it directly - harmony writes there and nothing else may. Masked off HERE,
    // at the single point every keyboard event passes through, rather than
    // trusted to stay clear everywhere upstream: a set, a project or a
    // favourite saved before harmony existed can carry bit 7 set, and the main
    // tab's SOLO 8 button is a plain HARMONY plate now, so there would be no
    // control on screen to turn it back off.
    const uint8_t soloMask =
        (uint8_t) (soloEnableMask.load() & ~(1u << kHarmonySlot));

    const auto forEachSoloChannel = [&](auto&& fn)
    {
        if (soloMask == 0)
        {
            // The legacy single-slot path. Clamped away from the harmony slot
            // for the same reason: activeSoloSlot is restored from old state
            // too, and landing on 7 would put the whole right hand on the
            // harmony voice.
            const int slot = juce::jlimit (0, kHarmonySlot - 1, activeSoloSlot.load());
            fn (Betel::SamplePlayerEngine::kNumStyleChannels + slot);
            return;
        }
        for (int s = 0; s < 8; ++s)
            if (soloMask & (1u << s))
                fn (Betel::SamplePlayerEngine::kNumStyleChannels + s);
    };

    //==========================================================================
    // HARMONY - the two helpers the note dispatch below uses.
    //
    // Kept here rather than as members because they close over `engine` and the
    // per-block settings reads, and because they are only ever meaningful
    // inside this loop.
    //==========================================================================
    const int  harmonyCh    = Betel::SamplePlayerEngine::kNumStyleChannels + kHarmonySlot;
    const bool harmonyLive  = harmonyOn.load (std::memory_order_relaxed);

    // RELEASE WHATEVER THIS KEY HOLDS. Used at note-off AND before a restrike,
    // because they are the same operation - see takeHarmony in Main.h.
    const auto releaseHarmony = [&](int phys)
    {
        int notes[kMaxHarmonyVoices];
        const int n = takeHarmony (phys, notes);
        for (int i = 0; i < n; ++i)
            engine.noteOff (harmonyCh, notes[i]);
    };

    const auto spawnHarmony = [&](int phys, int velocity)
    {
        // The previous strike goes first, ALWAYS - even when harmony is now
        // off. Switching it off mid-note must not strand the voices a note-on
        // already started, and this is the only place that can release them.
        releaseHarmony (phys);

        if (! harmonyLive) return;

        const auto r = Betel::Harmonizer::compute (
                           phys,
                           chordTracker.getCurrentChord(),
                           (Betel::Harmonizer::Type) harmonyType.load (std::memory_order_relaxed),
                           harmonyBelow.load (std::memory_order_relaxed));

        if (r.count == 0) return;

        // Quieter than the lead by default, or the melody stops reading as the
        // melody. jmax(1) because a velocity of 0 IS a note-off in MIDI - at a
        // level of 0 with a soft touch it would round down and silently
        // release the note it was supposed to start.
        const int lvl = harmonyLevel.load (std::memory_order_relaxed);
        const int vel = juce::jlimit (1, 127, (velocity * lvl) / 100);

        for (int i = 0; i < r.count; ++i)
            engine.noteOn (harmonyCh, r.notes[i], (juce::uint8) vel);

        latchHarmony (phys, r);
    };

    // GLOBAL TRANSPOSE IS NO LONGER APPLIED HERE.
    //
    // It used to be baked into the note number on the way in, which had three
    // consequences.  It shifted only what passed through THIS loop — the solo
    // voices and the chord-recognition input — so the style's own parts were
    // never transposed; the best it could do was make the tracker report a
    // different chord, which does nothing at all while a chord is held or
    // latched.  It clamped at the keyboard edges, collapsing many keys onto one
    // pitch.  And because note-on and note-off each recomputed it from the LIVE
    // knob, moving the knob mid-note left the two disagreeing and hung the
    // voice.
    //
    // The transpose now lives in Channel, inside the same shift as the octave
    // controls: it therefore reaches the style parts and the solo voices alike,
    // drops out-of-range notes instead of clamping them, and is latched per
    // note so it can never strand one.  Everything here works in PHYSICAL keys,
    // which also means the chord tracker sees the chord actually played.
    //
    // Piano mode disables the chord zone entirely — the whole keyboard plays
    // the solo voice(s).
    const bool piano = pianoMode.load();

    // Restart re-cue (from the RESTART button / note 32).  Done on the audio
    // thread: re-cue the sequencer's current section and flush any hung style
    // notes so the loop restarts cleanly from tick 0.
    if (restartRequested.exchange (false))
    {
        sequencer.restart();
        stylePlayer.allNotesOff();
    }

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();

        // ── CONTROL PADS ─────────────────────────────────────────────────
        //
        // NOTES 0-35 ARE THE ARRANGER'S CONTROL RANGE and are consumed here.
        // Above 35 the keyboard is the keyboard: an earlier version swallowed
        // everything below 36 AND above it too under some paths, which cost
        // three octaves of playable range on an 88-key controller.
        //
        // What each control note DOES is no longer decided here - it comes from
        // RemoteMap, which is seeded with the historic table on first run. One
        // mapping, in one place, visible in the popup.

        if (m.isNoteOn() || m.isNoteOff())
        {
            const int note = m.getNoteNumber();

            // The monitor sees every note-on, assigned or not - that is the
            // point of it: you look at it to find out what a pad sends BEFORE
            // you have assigned anything.
            if (m.isNoteOn())
                noteLastMidi ((int) Betel::RemoteMap::Kind::Pad, note, m.getVelocity());

            const auto id  = Betel::RemoteMap::get().findPad (note);

            if (id != Betel::RemoteId::kNumRemoteIds)
            {
                // BOTH HALVES GO THROUGH NOW.  The note-off used to be
                // swallowed here, which was correct while every assignment was
                // latching - but PUSH mode is defined by what a release does,
                // and a release that never arrives is a control that engages
                // and never lets go.  performRemote ignores it in the other two
                // modes, so nothing else changes.
                performRemote (id, m.isNoteOn() ? 127 : 0);
                continue;                       // swallowed either way
            }

            if (note < 36)
            {
                // NOTES 0-35 STAY RESERVED even with nothing assigned to this
                // one: they are the arranger's control range, and letting an
                // unassigned one through would play it as a bass note.
                //
                // The old dispatcher that used to run here is gone. Its whole
                // table now lives in RemoteMap::seedFactoryDefaults as real
                // entries, so the notes still do what they always did while
                // being visible and overridable - and there is no second,
                // invisible copy of the mapping to drift out of step with the
                // first.
                continue;
            }
        }

        if (m.isController())
        {
            const int cc  = m.getControllerNumber();
            const int val = m.getControllerValue();

            noteLastMidi ((int) Betel::RemoteMap::Kind::Cc, cc, val);

            // The user's own map first, then the five original CC targets. Both
            // layers exist because the original five are learnable from the
            // Settings tab and people already have them set; a control assigned
            // here simply takes precedence over the older route.
            const auto id = Betel::RemoteMap::get().findCc (cc);
            if (id != Betel::RemoteId::kNumRemoteIds)
            {
                performRemote (id, val);
                continue;
            }

            handleControllerMessage (cc, val);
            continue;   // CC consumed by the arranger control layer
        }

        if (m.isNoteOn())
        {
            const int phys = m.getNoteNumber();

            // LATCH THE ROUTE - see noteRouteSolo in Main.h.  The note-off must
            // reach exactly the channels this note-on reached, whatever the
            // split, the mode or the solo mask do in between.
            // ── ZONE 1: BASS ─────────────────────────────────────────────────
            //
            // Its own solo slot, so the pedals get their own instrument, level
            // and FX - AND the chord tracker, because a left hand playing a bass
            // line is still telling the band what the chord is. Dropping the
            // chord feed here would make a wide left hand stop moving the
            // harmony, which reads as the arranger losing the plot rather than
            // as a routing choice.
            //
            // PIANO MODE still wins: it means "the whole keyboard is the right
            // hand", and a zone boundary underneath it would contradict that.
            //
            // WHETHER IT ALSO FEEDS CHORDS DEPENDS ON WHICH MODE OPENED THE
            // ZONE, and the two answers are opposite on purpose - see
            // bassZoneFeedsChords() in Main.h.  MULTI SPLIT yes, MANUAL BASS no.
            //
            // The slot comes from getActiveBassSlot(), not getBassZoneSlot():
            // manual bass is always solo 7 and has no cycler, while multi split
            // keeps its own.  One call answers for both so the router and the
            // M.BASS lamp can never disagree about which slot is the bass.
            if (! piano && isBassZone (phys))
            {
                const int bassSlot = getActiveBassSlot();
                const int ch = Betel::SamplePlayerEngine::kNumStyleChannels + bassSlot;

                engine.noteOn (ch, phys, m.getVelocity());
                noteRouteSolo[phys] = (uint8_t) (noteRouteSolo[phys] | (1u << bassSlot));

                if (bassZoneFeedsChords())
                {
                    chordTracker.noteOn (phys);
                    noteRouteChord[phys] = true;
                }
            }
            else if (! piano && chordTracker.isChordZone(phys))
            {
                chordTracker.noteOn(phys);
                noteRouteChord[phys] = true;
            }
            else
            {
                forEachSoloChannel ([&](int ch)
                {
                    engine.noteOn (ch, phys, m.getVelocity());

                    const int slot = ch - Betel::SamplePlayerEngine::kNumStyleChannels;
                    if (slot >= 0 && slot < 8)
                        noteRouteSolo[phys] = (uint8_t) (noteRouteSolo[phys] | (1u << slot));
                });

                // ── HARMONY: THE TOP NOTE ONLY ───────────────────────────────
                //
                // Harmonising every note of a right-hand CHORD multiplies the
                // voice count - four notes in Trio is twelve - and it sounds
                // wrong besides: a hardware arranger harmonises the MELODY, and
                // the melody is the top note. Anything below the current
                // highest held key is accompaniment and is left alone.
                //
                // `>=` rather than `>` so a restrike of the same top note still
                // re-spawns, which is what a repeated melody note should do.
                //
                // THIS BRANCH IS THE UPPER ZONE BY CONSTRUCTION - the bass and
                // chord zones are the two `else if`s above it, so anything
                // reaching here is already right-hand material. Stated rather
                // than left implicit: with one split that was obvious, with
                // three zones it is a property of the branch ORDER, and
                // reordering those branches would silently start harmonising
                // the bass zone.
                if (phys >= highestHeldSoloNote)
                {
                    // The old top note stops being the melody the moment a
                    // higher one arrives, so its harmony goes with it -
                    // otherwise a rising line leaves a harmony stack behind it
                    // on every note it passes.
                    if (highestHeldSoloNote >= 0 && highestHeldSoloNote != phys)
                        releaseHarmony (highestHeldSoloNote);

                    highestHeldSoloNote = phys;
                    spawnHarmony (phys, m.getVelocity());
                }
            }
        }
        else if (m.isNoteOff())
        {
            const int phys = m.getNoteNumber();

            const uint8_t routed   = noteRouteSolo [phys];
            const bool    wasChord = noteRouteChord[phys];

            if (routed == 0 && ! wasChord)
            {
                // No latch: the key went down before this build started tracking
                // (or its note-on was swallowed).  Fall back to the live decision
                // - it is what the old code always did, and it is still the best
                // guess available when there is nothing recorded.
                if (! piano && chordTracker.isChordZone(phys))
                    chordTracker.noteOff(phys);
                else
                    forEachSoloChannel ([&](int ch) { engine.noteOff (ch, phys); });
            }
            else
            {
                if (wasChord)
                {
                    chordTracker.noteOff(phys);
                    noteRouteChord[phys] = false;
                }

                for (int slot = 0; slot < 8; ++slot)
                    if (routed & (1u << slot))
                        engine.noteOff (Betel::SamplePlayerEngine::kNumStyleChannels + slot,
                                        phys);

                noteRouteSolo[phys] = 0;
            }

            // UNCONDITIONAL, outside the latch test above. A key can hold
            // harmony even when its solo route was lost, and the only thing
            // that can ever release a harmony voice is the note-off of the key
            // that spawned it - so this must not sit behind a branch.
            releaseHarmony (phys);

            if (phys == highestHeldSoloNote)
                highestHeldSoloNote = -1;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            // EVERY solo channel, not just the routed ones: an all-notes-off is
            // the panic button, and a channel holding a note whose route was
            // lost is exactly what it exists to clear.
            for (int slot = 0; slot < 8; ++slot)
                engine.allNotesOff (Betel::SamplePlayerEngine::kNumStyleChannels + slot);

            // The loop above already covers the harmony channel - it is solo
            // slot 7 - so this only has to forget the melody, which
            // clearNoteRoutes below cannot know about.
            highestHeldSoloNote = -1;

            clearNoteRoutes();
            chordTracker.reset();
            stylePlayer.allNotesOff();
        }
        else if (m.isPitchWheel())
        {
            const float n = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
            forEachSoloChannel ([&](int ch) { engine.pitchBend (ch, n); });
        }
        else if (m.isProgramChange())
        {
            const int pc = m.getProgramChangeNumber();
            forEachSoloChannel ([&](int ch) { engine.programChangeDrum (ch, pc); });
        }
    }

    // 4) Tick the style sequencer.
    emittedEvents.clear();
    sequencer.renderBlock(numSamples, emittedEvents);


    // Always dispatch — even when no events fell in this block — so any
    // pending chord change can latch on the 1/8 boundary that crosses inside
    // this block.  Pass the sequencer's tick position (end-of-block) and the
    // style's 1/8 grid in ticks (TPQ / 2); StylePlayer uses them to defer
    // the chord-change retrigger to the nearest musical 1/8 instead of
    // re-attacking every held note the instant the chord changes.
    {
        const auto* st = sequencer.getStyle();
        const int tpq          = (st != nullptr) ? st->ticksPerQuarter : 1920;
        const int ticksPerEighth = juce::jmax (1, tpq / 2);
        const int currentTick  = (int) sequencer.getCurrentTickInSection();
        // The two bass overrides, resolved on this side because both depend on
        // the chord tracker and on switches the processor owns. Set immediately
        // before the call, on the same thread - see setBassOverrides.
        //
        // THE THIRD ARGUMENT SILENCES THE STYLE'S OWN BASS PART while manual
        // bass is on, and without it the mode gives you TWO basses: the left
        // hand on solo 7 and the style's bass pattern still running underneath
        // on whatever chord it was last given.  Taking the bass over is what
        // the mode is for, so the style's bass stands down for the duration.
        //
        // Note that getManualBassNote() is inert whenever this is true: it
        // reads the chord tracker's lowest held note, and in manual bass the
        // left hand no longer feeds the tracker.  The note-substitution path it
        // drives is the OLD implementation of this mode and is left in place
        // only because bass inversion shares the same call.
        stylePlayer.setBassOverrides (getBassRootOverride(), getManualBassNote(),
                                      isManualBassEnabled());

        stylePlayer.dispatchBlock (emittedEvents,
                                   chordTracker.getCurrentChord(),
                                   currentTick,
                                   ticksPerEighth,
                                   chordTracker.getChordMode()
                                       == Betel::ChordZoneTracker::ChordMode::Fingered);
    }

    // Flush hung style notes on the playing->stopped edge.  Catches every way
    // the sequencer can stop -- manual STOP, ending auto-stop, sync-stop -- so
    // long sustained voices (strings / pads) don't ring on after playback ends.
    {
        const bool nowPlaying = sequencer.isPlaying();
        if (wasSequencerPlaying && ! nowPlaying)
        {
            stylePlayer.allNotesOff();
            autoCrashCounter = 0;   // a stop ends the count; the next var starts a new one
        }
        wasSequencerPlaying = nowPlaying;
    }

    // 4b) Crash tab: hit a crash on a real section transition, and/or every N
    //     loop cycles of the current section.  Counters are always drained so
    //     they never accumulate while a feature is disabled.
    // CRASH ON TRANSITION fires on the LANDING, not the departure.
    //
    // It used to consume transitionEvents, which are raised the instant a
    // transition is entered — so pressing FILL 1 cracked the cymbal immediately
    // and the fill then played underneath it.  A crash marks an arrival: the
    // fill is the run-up, and the cymbal belongs on the downbeat it runs up to.
    // consumeLandingEvents counts that boundary instead.
    //
    // transitionEvents is still drained so it cannot accumulate while unused.
    sequencer.consumeTransitionEvents();

    const int landings = sequencer.consumeLandingEvents();

    // NOT ON AN ENDING.  A crash marks an arrival you are going to play through;
    // an ending is the one arrival you are not, so a cymbal there lands on top of
    // the final chord and rings past it.  Every other landing still gets one.
    const auto landedOn = sequencer.getCurrentSection();
    const bool endingLanding = landedOn == Betel::StyleSection::EndingA
                            || landedOn == Betel::StyleSection::EndingB
                            || landedOn == Betel::StyleSection::EndingC;

    if (crashOnTransition.load() && landings > 0 && ! endingLanding)
        triggerCrash();

    // ── AUTO CRASH ────────────────────────────────────────────────────────
    //
    // Hits the SAME cymbals the Crash tab has selected, and only while a var
    // 1-4 is playing.  The cycle count is measured from the moment a variation
    // STARTS, not from some running total:
    //
    //   var 1 starts            -> count = 0
    //   var 1 loops             -> 1, 2, 3 ...
    //   count reaches N         -> CRASH, count = 0 (so it repeats every N)
    //   FILL pressed mid-var    -> the fill is not a main, nothing counts
    //   fill lands back on var  -> a main ENTRY: count = 0, counting restarts
    //
    // That last line is the rule that matters.  A transition in the middle does
    // not accumulate and does not carry over — it ends the old count and begins
    // a new one, so the crash always arrives N full cycles after the variation
    // you are listening to began.
    //
    // Only main self-loops raise loopEvents, so "never on intro / fill / break /
    // ending" needs no separate test: those sections cannot produce a cycle.
    {
        const int mainEntries = sequencer.consumeMainEntryEvents();
        const int loops       = sequencer.consumeLoopEvents();

        if (! autoCrashEnabled.load())
        {
            autoCrashCounter = 0;             // stays drained while disabled
        }
        else
        {
            // An entry resets FIRST: a landing and the loop that completed the
            // outgoing section can arrive in the same block, and the entry is
            // the later of the two musically.
            if (mainEntries > 0)
                autoCrashCounter = 0;
            else if (loops > 0)
            {
                autoCrashCounter += loops;
                const int n = juce::jmax (1, autoCrashEveryN.load());
                if (autoCrashCounter >= n)
                {
                    triggerCrash();
                    autoCrashCounter = 0;
                }
            }
        }
    }

    // 4c) LOUDNESS — follow the arrangement and push this section's trim.
    //     Set BEFORE the render so the block about to be produced already
    //     carries the correction; measuring audio that lags its own trim by a
    //     block would smear the referral at every section boundary.
    {
        const int sec = (int) sequencer.getCurrentSection();   // StyleSection enum
        if (sec != lastMeasuredSection)
        {
            styleLoudness.setSection (sec);
            lastMeasuredSection = sec;
        }
        // THE BS.1770 SECTION TRIM IS RETIRED.
        //
        // StyleLoudness keeps measuring — the meter is genuinely useful and the
        // cache is cheap — but nothing applies its correction any more.  It went
        // with the role percentages and the two drum factors, for the same
        // reason: with the mixer in the set and a sound editor per slot, section
        // balance is the user's to set, and an automatic trim moving underneath
        // them is a gain stage nobody can see.
        //
        // MAKEUP is the control that replaced it, and it is on screen.
        engine.setStyleSectionTrim (1.0f);
    }

    // 5) Synth convention: clear before render.
    buffer.clear();
    engine.renderBlock(buffer, numSamples, effectiveBPM);

    // 5b) Feed the loudness meter — deliberately HERE, after the engine and
    //     before the Finisher.  The Finisher is a master chain: measuring
    //     through it would fold its compression into a figure that is supposed
    //     to describe the style, and the correction would then fight it.
    //
    //     A block only counts when the transport is running and the right hand
    //     is silent, because the buffer carries both buses and a chord voicing
    //     played over the top would be measured as if the style had played it.
    //     The reference figure is every gain applied downstream of the style
    //     parts, so the stored result can be referred back to unity.
    {
        const bool measurable = sequencer.isPlaying()
                             && engine.soloVoicesActive() == 0;

        const float refDb = juce::Decibels::gainToDecibels (
                                juce::jmax (1.0e-6f, engine.getStyleSectionTrim()))
                          + juce::Decibels::gainToDecibels (
                                juce::jmax (1.0e-6f, engine.getStyleBusGain()))
                          + juce::Decibels::gainToDecibels (
                                juce::jmax (1.0e-6f, engine.getMasterVolume()));

        styleLoudness.process (buffer.getReadPointer (0),
                               buffer.getNumChannels() > 1 ? buffer.getReadPointer (1)
                                                           : nullptr,
                               numSamples, measurable, refDb);
    }

    // 6) FINISHER — master-bus chain, applied at the very END of the audio
    //    path so it sits after the master fader and the master BOOST (both are
    //    baked into engine.masterVolume inside renderBlock).  This is the call
    //    that was missing: everything else (UI, params, GR meter) was wired,
    //    but the DSP was never invoked, so the effect was inaudible and the
    //    meter never moved.  process() bails immediately when disabled.
    finisher.process(buffer);

    // ── DEMO MODE ────────────────────────────────────────────────────────────
    //
    // AFTER the Finisher, so the silence is the last thing that happens and no
    // stage downstream can partially undo it - the ceiling limiter would
    // otherwise still be pumping on a signal nobody hears.
    //
    // GREX IS PAID, SO THIS BLOCK IS LIVE.  updateDemoMode() advances the
    // timer and isDemoSilenceActive() answers true during the silent window -
    // 18 s playing, 3 s muted - on a machine with no valid licence.  In Ballada
    // the same two calls are an empty function and a constant false, so the
    // optimiser deletes the block outright; the code is identical in both trees
    // and only RegistrationManager differs.
    {
        auto& rm = RegistrationManager::getInstance();
        rm.updateDemoMode();

        if (rm.isDemoSilenceActive())
            buffer.clear();
    }

    // 7) We consumed all MIDI.
    midi.clear();
}

// ======================================================
//  Aspect ratio constrainer — keeps 1600:853 proportion
// ======================================================
//
// ── THERE WAS NEVER A MAXIMUM, AND THAT IS THE CLAP PROBLEM ─────────────────
//
// setMinimumSize + setFixedAspectRatio were set and nothing else, so
// getMaximumWidth() / getMaximumHeight() were still ComponentBoundsConstrainer's
// defaults: 0x3fffffff, i.e. about a billion pixels each.
//
// VST3 never showed it, because JUCE's VST3 wrapper takes the editor's INITIAL
// size (the setSize below) as the window size and only consults the constrainer
// afterwards, when the user drags.  A CLAP host asks the other way round: it
// asks the plugin to ADJUST a size the host proposes - its remembered window
// state, or its maximised frame - and a plugin that answers "any size at all is
// fine" gets exactly what it asked for.  The layout then scales itself up to
// that reported size while the visible surface is whatever the host actually
// drew, which is what giant controls in a small canvas look like.
//
// So the ceiling is set here rather than through AudioProcessorEditor's
// setResizeLimits: that helper only writes to JUCE's OWN default constrainer
// and asserts when a custom one is installed, which is the case here.
class ProportionalConstrainer : public juce::ComponentBoundsConstrainer
{
public:
    ProportionalConstrainer()
    {
        setFixedAspectRatio(kAspect);
        setMinimumSize(800, (int)(800.0 * kDesignH / kDesignW));

        // Never larger than the screen the plugin opened on, and never more
        // than twice the artwork's own resolution - past 2x the PNGs are being
        // magnified and the window is bigger than any arranger needs to be.
        int maxW = kDesignW * 2;
        int maxH = kDesignH * 2;

        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = display->userArea;

            // jmax against the design size so a small laptop panel can never
            // produce a maximum BELOW the minimum, which would leave the
            // constrainer with an impossible range.
            maxW = juce::jmin (maxW, juce::jmax (kDesignW, area.getWidth()));
            maxH = juce::jmin (maxH, juce::jmax (kDesignH, area.getHeight()));
        }

        // Snap the ceiling onto the design aspect, so the maximum and the fixed
        // ratio agree.  If they disagree the constrainer clamps to one and then
        // re-proportions against the other, and the window creeps a pixel or
        // two on every resize.
        if ((double) maxW / kAspect > (double) maxH) maxW = (int) std::floor (maxH * kAspect);
        else                                         maxH = (int) std::floor (maxW / kAspect);

        setMaximumSize (maxW, maxH);
    }

    /** The size the editor should OPEN at: the artwork's own resolution when it
        fits, shrunk onto the ceiling above when it does not.  A 1366x768 laptop
        used to get a 1600x853 window - larger than its own screen - because the
        constructor asked for the design size unconditionally. */
    juce::Rectangle<int> preferredOpenSize() const
    {
        const int w = juce::jlimit (getMinimumWidth(), getMaximumWidth(), kDesignW);
        const int h = juce::jlimit (getMinimumHeight(), getMaximumHeight(),
                                    (int) std::round ((double) w / kAspect));
        return { 0, 0, w, h };
    }

private:
    static constexpr int    kDesignW = 1600;
    static constexpr int    kDesignH = 853;
    static constexpr double kAspect  = (double) kDesignW / (double) kDesignH;
};

// ======================================================
//  Editor class
// ======================================================
//
// ── SET THIS TO 0 TO TEST THE CLAP SCALING THEORY ──────────────────────────
//
// If the CLAP window comes up with the controls drawn larger than the surface
// they sit on, the next thing to rule out is the OpenGL context.  A CLAP editor
// is parented into a window the HOST owns and the host may already have applied
// its own DPI scale to it; JUCE's OpenGLContext independently applies the
// display scale to its render surface, and when both happen the component tree
// is rasterised at one scale and presented at another - which looks exactly
// like oversized controls in a shrunken canvas.
//
// Build with this at 0 and load the CLAP again.  If the UI is correct, the
// context is the cause and the fix belongs there rather than in the layout.  If
// it is still wrong, the host is reporting a size the editor never agreed to
// and the constrainer above is where to keep looking.  Software rendering costs
// some smoothness on the animations and nothing else.
#define GREX_EDITOR_USE_OPENGL 1

class BetelgeuseEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    static constexpr int kDesignW = 1600;
    static constexpr int kDesignH = 853;

    explicit BetelgeuseEditor(BetelgeuseProcessor& proc)
        : juce::AudioProcessorEditor(&proc),
          processor(proc),
          mainComponent(proc)
    {
        addAndMakeVisible(mainComponent);

        // OPENGL IS **NOT** ATTACHED HERE - see attachOpenGLWhenReady() below.
        // Attaching in the constructor is what this bug turned out to be.

        setConstrainer(&constrainer);
        setResizable(true, true);

        // OPEN AT A SIZE THE CONSTRAINER ACTUALLY ALLOWS.
        //
        // This used to be a bare setSize(kDesignW, kDesignH), which is both
        // bigger than a 1366x768 laptop screen and - more to the point - a size
        // the plugin then had no ceiling to defend.  preferredOpenSize() is the
        // design resolution when it fits and the constrainer's ceiling when it
        // does not, so the first size the host is told is already inside the
        // range the plugin is willing to be resized to.
        const auto open = constrainer.preferredOpenSize();
        setSize(open.getWidth(), open.getHeight());
    }

    ~BetelgeuseEditor() override
    {
        // Before the context: a watchdog tick during teardown would re-attach a
        // context onto a component that is halfway through being destroyed.
        stopTimer();

        if (openGLContext.isAttached())
            openGLContext.detach();

        // The constrainer is a MEMBER of this class, so it dies before
        // ~AudioProcessorEditor runs - and the base class holds a raw pointer to
        // it and hands that same pointer to the corner resizer it owns.  Taking
        // both down here means nothing can reach a constrainer that no longer
        // exists during teardown.
        setResizable(false, false);
        setConstrainer(nullptr);
    }

    //==========================================================================
    // ATTACH OPENGL ONLY ONCE THE EDITOR IS REALLY IN THE HOST'S WINDOW.
    //
    // THIS IS THE CLAP BUG.  attachTo() was called from the constructor, at
    // which point the editor has no peer, is not showing, and has not been
    // parented into anything.  juce::OpenGLContext latches a RENDERING SCALE at
    // attach time, and with no peer to ask it takes the primary display's - so
    // on a 150% Windows desktop it bakes in 1.5.  The component tree is then
    // rasterised into a framebuffer sized getWidth() * 1.5 while the host's
    // window is whatever the host made it, and the difference is exactly the
    // 2400-wide layout showing through a 1990-wide window in the screenshot.
    //
    // VST3 hides it because JUCE's VST3 wrapper is handed the host's scale
    // through setContentScaleFactor before the editor is shown, so the numbers
    // agree by the time anything is drawn.  A CLAP editor is parented later and
    // reports its scale through a different call, so the context is already
    // attached and already wrong when that arrives.
    //
    // parentHierarchyChanged + visibilityChanged between them fire on every
    // route a host can take to put this window on screen, and the guard makes
    // the attach happen exactly once whichever arrives first.
    //
    // NOTE the guard is a MEMBER, not a function-local static.  A static would
    // be per-PROCESS, so the second instance of the plugin in the same session
    // would skip its attach entirely and come up unaccelerated for no visible
    // reason.
    //==========================================================================
    void parentHierarchyChanged() override
    {
        pinPeerScale();
        attachOpenGLWhenReady();
    }

    void visibilityChanged() override
    {
        pinPeerScale();
        attachOpenGLWhenReady();
    }

    //==========================================================================
    // PIN THE PEER TO 1:1.  THIS IS THE FIX, AND THE LOG IS WHY.
    //
    // The diagnostic run said, in one settled block:
    //
    //     editor size   : 1603 x 855
    //     editor xform  : mat00=1.2500   identity=NO
    //     wrapper size  : 2003 x 1068
    //     peer scale    : 1.2500
    //     display scale : 1.2500
    //
    // Read down that list: the editor is 1603 logical, the transform takes it
    // to 2003, and then the PEER multiplies by 1.25 A SECOND TIME, so the
    // native window is about 2504 real pixels.  The host was told 2003 - that
    // is what guiGetSize reports.  2504 / 2003 = 1.25, which is the oversize
    // on screen, and it is why the UI grows proportionally with the window:
    // both numbers scale together and the ratio between them never changes.
    //
    // ONE SCALE, APPLIED TWICE.  Not a size negotiation problem, not a
    // constrainer problem, not OpenGL - the same log block says
    // "opengl scale : 1.0000", so the context was never involved.
    //
    // The wrapper is supposed to prevent this: upstream clap-juce-extensions
    // pins the peer in guiWin32Attach for exactly this reason ("the peer must
    // stay 1:1 or the scale is applied twice").  The log says peer scale is
    // 1.25, so that patch is not in this build.  Doing it here as well is not
    // a workaround for that - it is the same one-line assertion made from the
    // file that is certain to be rebuilt, and it is idempotent, so it does no
    // harm if the wrapper starts doing it too.
    //
    // WHY THE EDITOR CAN REACH IT: Component::getPeer() walks up to the
    // top-level component that owns a peer.  The editor is a child of the
    // wrapper's EditorWrapperComponent, which is the thing added to the
    // desktop, so this is the very same peer object the wrapper would pin -
    // confirmed by the log, where the editor's own getPeer() is what reported
    // the 1.25.
    //
    // The setBounds afterwards is upstream's second half and it is needed:
    // the native window was already created at the monitor scale, and pinning
    // the factor alone does not resize it, so the stale geometry would flow
    // back into the component on the first window event.
    //==========================================================================
    void pinPeerScale()
    {
        //======================================================================
        // WINDOWS AND LINUX ONLY - macOS IS DELIBERATELY EXCLUDED.
        //
        // This started life ungated, which was a mistake: it would have run on
        // the macOS build too, where it is at best pointless and at worst an
        // unwanted native-window call on the attach path.
        //
        // macOS CANNOT HAVE THIS BUG, for three independent reasons:
        //
        //  1. UNITS AGREE.  CLAP measures win32 and x11 window sizes in
        //     PHYSICAL PIXELS but cocoa sizes in LOGICAL POINTS - and JUCE on
        //     macOS also works in logical points.  The unit mismatch that let
        //     one scale be applied twice simply does not exist there.
        //
        //  2. THERE IS NO SECOND MULTIPLIER.  JUCE's NSViewComponentPeer does
        //     not override getPlatformScaleFactor() at all, so it returns
        //     ComponentPeer's base 1.0 forever.  Retina is handled by AppKit's
        //     backing scale, underneath JUCE, and never appears as a factor
        //     JUCE applies on top.  Pinning 1.0 there pins it to what it
        //     already is.
        //
        //  3. THERE IS NO TRANSFORM EITHER.  The wrapper's createEditor already
        //     wraps its setScaleFactor call in #if !JUCE_MAC.
        //
        // Upstream clap-juce-extensions reaches the same conclusion the same
        // way: it pins the peer in guiWin32Attach and guiX11Attach, and its
        // guiCocoaAttach has no pin at all.
        //
        // setCustomPlatformScaleFactor would in fact be harmless on macOS - the
        // Cocoa peer does not override it either, so the base class's empty
        // body runs.  The setBounds beside it is NOT harmless: that is a real
        // native window resize, issued during attach, on the VST-style
        // attachComponentToWindowRefVST path macOS uses.  No reason to fire it
        // at a platform with nothing to correct.
        //======================================================================
       #if JUCE_VERSION >= 0x090000 && (JUCE_WINDOWS || JUCE_LINUX || JUCE_BSD)
        if (peerPinned) return;

        auto* peer = getPeer();
        if (peer == nullptr) return;

        peerPinned = true;
        peer->setCustomPlatformScaleFactor (1.0);
        peer->setBounds (peer->getComponent().getBounds(), false);
       #endif
    }


    void attachOpenGLWhenReady()
    {
       #if GREX_EDITOR_USE_OPENGL
        if (glAttached) return;
        if (! isShowing() || getPeer() == nullptr) return;

        glAttached = true;

        openGLContext.setComponentPaintingEnabled(true);
        // Event-driven rendering ONLY.  Continuous repainting re-rasterised the
        // entire component tree every vsync, which made static pages (the Mixer
        // tab especially) shimmer/blink from per-frame re-composition.  Nothing
        // here needs a free-running GL loop: the space animations drive their
        // own 60 Hz repaint() timers, and every other update already calls
        // repaint() when state changes - so animation still runs, and static
        // UI is now genuinely static.
        openGLContext.setContinuousRepainting(false);
        openGLContext.attachTo(*this);

        // Start the suspend watchdog only once there is a context to rescue.
        startTimer (kWatchdogMs);
        lastWatchdogTickMs = juce::Time::getMillisecondCounter();
       #endif
    }

    //==========================================================================
    // SLEEP / WAKE: THE GUI FREEZES UNTIL THE WINDOW IS CLOSED AND REOPENED.
    //
    // On resume the OpenGL context this editor attached before the machine went
    // down is no longer valid - the driver tears down GL contexts across a
    // suspend, and juce::OpenGLContext has no way to notice or rebuild itself.
    // Rendering is EVENT-DRIVEN here (setContinuousRepainting is false), so
    // there is no render loop to fail loudly either: repaint() requests are
    // simply handed to a dead context and nothing more happens.  The component
    // tree is alive the whole time, which is why closing the window with the X
    // and reopening it fixes it - that builds a new editor and a new context.
    //
    // DETECTING THE SUSPEND WITHOUT ANY PLATFORM API.
    //
    // Timers do not fire while the machine is asleep.  So if two ticks of a
    // one-second timer are separated by far more than one second of WALL CLOCK,
    // the gap is time the process was not running - a suspend, a hibernate, or
    // a host that froze the message thread hard enough to have the same effect
    // on the context.  That single observation is the whole detector, and it
    // works identically on Windows and macOS with no WM_POWERBROADCAST and no
    // NSWorkspace notification.
    //
    // The threshold is deliberately far above any legitimate stall.  A busy
    // message thread, a big style load, a modal file chooser - all of those can
    // eat a second or two and none of them kills the context.  Only a real
    // suspend produces a gap this size.
    //
    // Re-attaching is done on a LATER message, not inline: detach() shuts the
    // GL render thread down, and asking for a new context in the same callback
    // that killed the old one is how this turns into a hang instead of a fix.
    //==========================================================================
    void timerCallback() override
    {
       #if GREX_EDITOR_USE_OPENGL
        const auto now  = juce::Time::getMillisecondCounter();
        const auto prev = lastWatchdogTickMs;
        lastWatchdogTickMs = now;

        if (prev == 0 || now < prev) return;              // first tick, or counter wrap

        if (now - prev < kSuspendGapMs) return;           // ordinary tick

        if (! glAttached || ! isShowing() || getPeer() == nullptr) return;

        // Rebuild. glAttached is cleared here so attachOpenGLWhenReady's own
        // guard lets the re-attach through; every other precondition it checks
        // is re-checked there rather than duplicated.
        openGLContext.detach();
        glAttached = false;

        juce::Component::SafePointer<BetelgeuseEditor> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->attachOpenGLWhenReady();
            safe->repaint();
        });
       #endif
    }

    //==========================================================================
    // NOTE FOR ANYONE RE-ADDING A TRANSFORM WORKAROUND HERE.
    //
    // A dropHostScaleTransform() used to live at this spot: it reset the
    // editor's AffineTransform to identity on every parentSizeChanged, on the
    // theory that the host's scale factor was what oversized the UI.
    //
    // IT IS GONE BECAUSE IT NOW FIGHTS THE FIX.  The CLAP wrapper's design -
    // and upstream clap-juce-extensions says so in its own comment - is that
    // the PEER stays pinned at 1:1 (peer->setCustomPlatformScaleFactor(1.0),
    // JUCE 9 and later) and the EDITOR TRANSFORM is what applies the host's
    // scale.  guiGetSize then reports transform-inflated bounds and everything
    // agrees.  Killing the transform from in here removes the one half of that
    // pair the plugin can see, and the window and the content disagree again -
    // this time in the other direction.
    //
    // If the UI is ever the wrong size again, the fault is in the wrapper's
    // unit handling or the peer's scale, NOT here.  Nothing in this editor
    // should touch getTransform().
    //==========================================================================

    void paint(juce::Graphics&) override {}

    //==========================================================================
    // NEVER LAY THE UI OUT BIGGER THAN THE CEILING, WHATEVER THE HOST SAYS.
    //
    // The constrainer added earlier is consulted by the CLAP wrapper's
    // "adjust this size for me" call - and NOT by its "set this size" call,
    // which reaches plain Component::setSize and bypasses every constrainer
    // JUCE has.  A host that skips the adjust step therefore sets whatever it
    // likes and the ceiling never gets a vote.  This is where it gets one.
    //
    // ANCHORED TOP-LEFT, not centred, and that matters: when the editor has
    // been made larger than the window showing it, the part the user can
    // actually see is the top-left corner.  Centring a clamped layout inside an
    // oversized editor would push it further off the right edge - fixing the
    // scale and losing the content.
    //
    // When the size is legitimate this is exactly getLocalBounds() and nothing
    // changes, so a real 2560x1364 window on a big monitor is untouched.
    //==========================================================================
    void resized() override
    {
        const auto b = getLocalBounds();

        const int w = juce::jmin (b.getWidth(),  juce::jmax (1, constrainer.getMaximumWidth()));
        const int h = juce::jmin (b.getHeight(), juce::jmax (1, constrainer.getMaximumHeight()));

        mainComponent.setBounds (0, 0, w, h);
    }

    juce::OpenGLContext& getOpenGLContext() { return openGLContext; }

private:
    BetelgeuseProcessor&    processor;
    MainComponent           mainComponent;
    ProportionalConstrainer constrainer;
    juce::OpenGLContext     openGLContext;
    bool                    glAttached = false;
    bool                    peerPinned = false;

    // Suspend watchdog - see timerCallback.  kSuspendGapMs is the wall-clock gap
    // between two watchdog ticks that counts as "the machine was asleep".
    static constexpr int    kWatchdogMs   = 1000;
    static constexpr int    kSuspendGapMs = 10000;
    juce::uint32            lastWatchdogTickMs = 0;
};

juce::AudioProcessorEditor* BetelgeuseProcessor::createEditor()
{
    return new BetelgeuseEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BetelgeuseProcessor();
}
