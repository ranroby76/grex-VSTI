#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>
#include "RegistrationManager.h"

#include "Main.h"
#include "PerfMonitor.h"
#include "GlobalMacros.h"       // GrexPaths + the funkey / big-drums macro presets
#include "StyleFavorites.h"     // the stars, read at startup with everything else
#include "CcMap.h"             // the rig's CC bindings, in force before the first event
#include "MainComponent.h"
#include "StyleLoader.h"
#include "StyleLevels.h"    // per-style loudness-trim switch, read in the callback

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
    // All of this used to run in the MainComponent constructor, which means it
    // ran only when the plugin WINDOW was built.  The processor is built every
    // time the plugin is instantiated; the editor is built only when somebody
    // looks at it.  Three ordinary things never open the window:
    //
    //   * an offline bounce / freeze / render-in-place;
    //   * reopening a saved project and pressing play without clicking the
    //     plugin;
    //   * any headless or scripted host.
    //
    // In every one of those the plugin used to come up UNREGISTERED - three
    // seconds of silence out of every twenty-one, in a render, on a machine
    // that owns a licence - and with the master settings still at their
    // defaults, so the wheel range, the solo base unity and the chord mode
    // were all wrong too.  None of it had anything to do with the UI; it was
    // simply parked in the only constructor that happened to be there.
    //==========================================================================
    {
        // The licence is re-validated against THIS machine every launch, so
        // copying grex_license.key to another computer achieves nothing there.
        RegistrationManager::getInstance().checkRegistration();

        auto& ms = Betel::MasterSettings::get();
        ms.load();

        // ── THE CC BINDINGS, BEFORE THE FIRST MIDI EVENT ─────────────────────
        //
        // grex_cc_map.xml describes the RIG - which knob on this desk drives
        // what - so like the pitch bend range above it has to be in force before
        // anything can arrive, window or no window.
        //
        // It was loaded in the EDITOR, which meant a window-less session (an
        // offline bounce, or a project simply played without opening the UI) ran
        // on whatever the processor's own defaults were.  Those defaults are now
        // "nothing assigned", so that case is already safe - but loading here is
        // what makes the user's REAL assignments work in the same session.
        {
            auto& cm = Betel::CcMap::get();
            cm.load();
            for (int i = 0; i < kNumCcTargets; ++i)
                setCcNumber (i, cm.ccFor (i));
        }

        // The wheel's throw and the right hand's base unity are properties of
        // the RIG, not of a song, so they must be in force before the first set
        // loads - and before the first note, in a session with no window.
        setSoloPitchBendRange (ms.getPitchBendRange());
        applyStoredSoloBaseUnity();

        chordTracker.setChordMode (
            ms.isFingeredChord() ? Betel::ChordZoneTracker::ChordMode::Fingered
                                 : Betel::ChordZoneTracker::ChordMode::SingleFinger);
        setTempoSynced (ms.isTempoSynced());
    }

    // Global macro presets (funkey / big drums) and the style stars are
    // installation-wide files under the same root.  Same reasoning: a style
    // played without the window open should sound the way this install sounds.
    Betel::GlobalMacros::get().loadFunkey();
    Betel::GlobalMacros::get().loadBigDrums();
    Betel::StyleFavorites::get().load();

    // Crash settings own themselves (grex_crash.xml is the sole owner - the set
    // deliberately does not carry them), so they can be read here too.  The
    // editor re-reads them after the Crash tab has pushed its defaults, which
    // is a no-op on an established install and correct on a fresh one.
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
        // Always call scanFolder (it logs the resolved path + isDir to
        // grex_drum.txt and safely returns 0 if the folder is absent), so the
        // diagnostic is written even when the path is wrong.
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
    // together.
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
    std::vector<StyleSearchResult> results;
    const juce::String kw = keyword.trim();

    for (const auto& folder : stylesFolders)
    {
        if (! folder.isDirectory()) continue;

        // Recursive scan for every Yamaha SFF style variant we ship parsing
        // for.  Extensions are categorical hints — all of them are SFF1 /
        // SFF2 / SFF GE internally and parse through the same StyleLoader.
        //
        //   .sty — generic   .prs — Pro          .bcs — Basic
        //   .sst — Session   .pst — Pianist      .pcs — Piano Combo
        //   .fps — Free Play .scp — DJ           .aus — Audio
        const auto files = folder.findChildFiles (
            juce::File::findFiles | juce::File::ignoreHiddenFiles,
            true,                          // recursive
            "*.sty;*.prs;*.bcs;*.sst;*.pst;*.pcs;*.fps;*.scp;*.aus");

        for (const auto& f : files)
        {
            const auto name = Betel::displayNameFor (f);
            if (kw.isEmpty() || name.containsIgnoreCase (kw))
                results.push_back ({ name, f.getFullPathName() });
        }
    }

    // De-dupe by absolute path (same file showing up under multiple
    // overlapping folders gets folded to one entry).
    std::sort (results.begin(), results.end(),
               [] (const StyleSearchResult& a, const StyleSearchResult& b)
               { return a.absolutePath < b.absolutePath; });
    results.erase (std::unique (results.begin(), results.end(),
                                [] (const StyleSearchResult& a, const StyleSearchResult& b)
                                { return a.absolutePath == b.absolutePath; }),
                   results.end());

    // Sort the visible list alphabetically by display name.
    std::sort (results.begin(), results.end(),
               [] (const StyleSearchResult& a, const StyleSearchResult& b)
               { return a.displayName.compareIgnoreCase (b.displayName) < 0; });

    return results;
}

juce::StringArray BetelgeuseProcessor::getStyleGenres() const
{
    juce::StringArray genres;
    const auto root = folderManager.getStylesFolder();
    if (! root.isDirectory()) return genres;

    const auto dirs = root.findChildFiles (
        juce::File::findDirectories | juce::File::ignoreHiddenFiles, false);
    for (const auto& d : dirs)
        genres.add (d.getFileName());

    genres.sort (true);   // case-insensitive alphabetical
    return genres;
}

std::vector<BetelgeuseProcessor::StyleSearchResult>
BetelgeuseProcessor::getStylesInGenre (const juce::String& genre) const
{
    std::vector<StyleSearchResult> out;
    const auto dir = folderManager.getStylesFolder().getChildFile (genre);
    if (! dir.isDirectory()) return out;

    // Non-recursive: only the styles sitting directly inside this genre folder.
    const auto files = dir.findChildFiles (
        juce::File::findFiles | juce::File::ignoreHiddenFiles, false,
        "*.sty;*.prs;*.bcs;*.sst;*.pst;*.pcs;*.fps;*.scp;*.aus");

    for (const auto& f : files)
        out.push_back ({ Betel::displayNameFor (f), f.getFullPathName() });

    std::sort (out.begin(), out.end(),
               [] (const StyleSearchResult& a, const StyleSearchResult& b)
               { return a.displayName.compareIgnoreCase (b.displayName) < 0; });
    return out;
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

    // Spike diagnostics.  Writes alongside grex_log.txt; see PerfMonitor.h for
    // how to read it.  prepare() must run here so block times can be expressed
    // as a percentage of the REAL-TIME BUDGET for this sample rate / block size
    // — a 3 ms block is fine at 512/48k and a dropout at 64/48k.
    Betel::PerfMonitor::get().prepare (sampleRate, blockSize);
    if (! Betel::PerfMonitor::get().isEnabled())
        Betel::PerfMonitor::get().start (Betel::GrexPaths::perfLog());
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

    if (currentStyle != nullptr)
        retiredStyles.push_back (std::move (currentStyle));

    currentStyle = std::move (fresh);
    sequencer.setStyle (currentStyle.get());
    stylePlayer.applyVoiceSetup (*currentStyle);
    chordTracker.reset();

    // Commit the loaded style's own tempo as the working BPM.  In FREE mode the
    // sequencer reads manualBPM directly, so without this the previous style's
    // (or the 120 default) tempo would persist and the new style would play at
    // the wrong speed.  In SYNCED mode manualBPM isn't used for playback, but
    // setting it keeps the TEMPO knob correct for when the user switches back.
    if (currentStyle->originalBPM > 0.0f)
        manualBPM.store (juce::jlimit (30.0f, 300.0f, currentStyle->originalBPM));

    // Hand the meter the new file.  This commits and saves whatever the
    // OUTGOING style had learned, then loads this file's cached measurements if
    // it has any — keyed on path + size + modification date, so a style edited
    // since it was measured is re-measured rather than trusted.
    styleLoudness.setStyle (file);
    lastMeasuredSection = -1;
    engine.setStyleSectionTrim (1.0f);

    lastLoadedStylePath = file.getFullPathName();
    return true;
}

juce::ValueTree BetelgeuseProcessor::captureGlobalState() const
{
    juce::ValueTree t ("GlobalState");
    t.setProperty ("activeSoloSlot",   activeSoloSlot.load(),    nullptr);
    t.setProperty ("splitPoint",       chordTracker.getSplitPoint(), nullptr);
    t.setProperty ("currentStylePath", lastLoadedStylePath,      nullptr);
    for (int ch = 0; ch < Betel::StylePlayer::kNumUserStyleSlots; ++ch)
        t.setProperty ("subFlag" + juce::String (ch),
                       stylePlayer.getSlotSubstitution (ch), nullptr);
    t.setProperty ("comments",         comments,                 nullptr);

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

    // Buses and master.  masterGain is the PRE-boost fader value — see
    // getMasterVolume for why reading the engine back would be wrong.
    t.setProperty ("styleBusGain", (double) getStyleVolume(),     nullptr);
    t.setProperty ("soloBusGain",  (double) getRightHandVolume(), nullptr);
    t.setProperty ("masterGain",   (double) getMasterVolume(),    nullptr);
    t.setProperty ("masterBoostDb",(double) getMasterBoostDb(),   nullptr);

    // Finisher: on/off, the two macro controls, every slider and every stage
    // bypass.  It is a master-bus chain tuned per song, so all of it travels.
    t.setProperty ("finEnabled",   getFinisherEnabled(),   nullptr);
    t.setProperty ("finAmount",    (double) getFinisherAmount(), nullptr);
    t.setProperty ("finCharacter", getFinisherCharacter(), nullptr);
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

    if (ms.hasProperty ("styleBusGain"))
        setStyleVolume    ((float) (double) ms.getProperty ("styleBusGain"));
    if (ms.hasProperty ("soloBusGain"))
        setRightHandVolume((float) (double) ms.getProperty ("soloBusGain"));

    // Boost BEFORE the fader: both call applyMasterOut, and setting the fader
    // last means the value that lands on the engine already carries the boost
    // this set asked for rather than the previous song's.
    if (ms.hasProperty ("masterBoostDb"))
        setMasterBoostDb  ((float) (double) ms.getProperty ("masterBoostDb"));
    if (ms.hasProperty ("masterGain"))
        setMasterVolume   ((float) (double) ms.getProperty ("masterGain"));

    if (ms.hasProperty ("finEnabled"))
        setFinisherEnabled ((bool) ms.getProperty ("finEnabled"));
    if (ms.hasProperty ("finAmount"))
        setFinisherAmount  ((float) (double) ms.getProperty ("finAmount"));
    if (ms.hasProperty ("finCharacter"))
        setFinisherCharacter ((int) ms.getProperty ("finCharacter"));

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
    setSplitPoint     ((int) gs.getProperty ("splitPoint",     getSplitPoint()));
    comments = gs.getProperty ("comments", comments).toString();

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
        juce::File f (path);
        const bool alreadyLoaded = hasStyle()
                                 && f.getFullPathName() == lastLoadedStylePath;
        if (! alreadyLoaded && f.existsAsFile())
        {
            juce::String err;
            loadStyle (f, err);
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
    setManualBPM       ((float) (double) gs.getProperty ("manualBPM", (double) manualBPM.load()));
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
    manualBPM.store (bpm);
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

    for (int i = 0; i < 4; ++i)
        if (mask & (1u << i))
            engine.noteOn (0, kCrashNotes[i], vel, /*fromEditor*/ false, g);
}

void BetelgeuseProcessor::performVariation (int variButtonIdx)
{
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
    if (sequencer.isPlaying())      sequencer.stop();
    else if (hasStyle())            sequencer.start();
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

void BetelgeuseProcessor::handleControlNote (int note)
{
    // Reserved control-notes (0–35).  Note-ON only; note-OFF is swallowed by
    // the caller.  Semantics per group:
    //   1-8   style elements   → toggle mute
    //   9-14  solo channels 1-6 (single mode) → radio select
    //   15-30 variation pads    → momentary trigger
    //   31    PLAY/STOP         → toggle
    //   32    RESTART           → momentary
    //   33    HOLD              → toggle
    //   34    ARRANGER/PIANO    → toggle
    //   35    SYNCED PLAY       → toggle (arm)
    if (note >= 1 && note <= 8)            // style elements
    {
        toggleStyleElement (note - 1);
    }
    else if (note >= 9 && note <= 14)      // solo channels 1-6
    {
        selectSoloSlot (note - 9);
    }
    else if (note >= 15 && note <= 30)     // variation pads
    {
        // note 15 → INTRO 1 (idx 0) ... note 29 → END 3 (idx 15) except BREAK.
        // Map per the spec: 15-18 INTRO1-4 (0-3), 19-22 VAR1-4 (4-7),
        // 23-26 FILL1-4 (8-11), 27-29 END1-3 (13-15), 30 BREAK (12).
        int varIdx;
        if      (note <= 18) varIdx = note - 15;        // 0-3
        else if (note <= 22) varIdx = note - 19 + 4;    // 4-7
        else if (note <= 26) varIdx = note - 23 + 8;    // 8-11
        else if (note <= 29) varIdx = note - 27 + 13;   // 13-15
        else                 varIdx = 12;               // 30 = BREAK
        performVariation (varIdx);
    }
    else if (note == 31) togglePlayStop();
    else if (note == 32) requestRestart();
    else if (note == 33) toggleHold();
    else if (note == 34) togglePianoMode();
    else if (note == 35) toggleSyncPlay();
    // note 0 (and any unassigned) — reserved but no action.
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
    juce::ScopedNoDenormals noDenormals;
    const int64_t blockStart = Betel::PerfMonitor::get().tick();   // spike diagnostics
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
    const uint8_t soloMask = soloEnableMask.load();

    const auto forEachSoloChannel = [&](auto&& fn)
    {
        if (soloMask == 0)
        {
            fn (Betel::SamplePlayerEngine::kNumStyleChannels + activeSoloSlot.load());
            return;
        }
        for (int s = 0; s < 8; ++s)
            if (soloMask & (1u << s))
                fn (Betel::SamplePlayerEngine::kNumStyleChannels + s);
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

        // Reserved control-notes (0–35) never reach chord recognition or solo
        // routing — they drive the transport / element / variation controls.
        if ((m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() < 36)
        {
            if (m.isNoteOn())
                handleControlNote (m.getNoteNumber());
            continue;   // swallow both note-on and note-off
        }

        if (m.isController())
        {
            handleControllerMessage (m.getControllerNumber(), m.getControllerValue());
            continue;   // CC consumed by the arranger control layer
        }

        if (m.isNoteOn())
        {
            const int phys = m.getNoteNumber();

            // LATCH THE ROUTE - see noteRouteSolo in Main.h.  The note-off must
            // reach exactly the channels this note-on reached, whatever the
            // split, the mode or the solo mask do in between.
            if (! piano && chordTracker.isChordZone(phys))
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
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            // EVERY solo channel, not just the routed ones: an all-notes-off is
            // the panic button, and a channel holding a note whose route was
            // lost is exactly what it exists to clear.
            for (int slot = 0; slot < 8; ++slot)
                engine.allNotesOff (Betel::SamplePlayerEngine::kNumStyleChannels + slot);

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
    auto& perf = Betel::PerfMonitor::get();
    const auto tRender = perf.tick();
    engine.renderBlock(buffer, numSamples, effectiveBPM);
    const auto tAfterRender = perf.tick();

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
    // clear() rather than a gain ramp: a fade would let a determined user
    // reconstruct the missing seconds from the shoulders, and an abrupt cut is
    // unambiguously "this is a demo" rather than "this plugin has a glitch".
    // Every voice keeps running underneath, so the style stays in time and the
    // audio simply reappears when the window ends.
    {
        auto& rm = RegistrationManager::getInstance();
        rm.updateDemoMode();

        if (rm.isDemoSilenceActive())
            buffer.clear();
    }

    // Spike diagnostics: one entry per block, atomics only — no allocation, no
    // lock, no file I/O on this thread.  The engine reports its own style/solo
    // split, so a spike can be attributed to a bus rather than just "the audio
    // thread was slow".
    perf.endBlock (blockStart,
                   engine.getLastStyleRenderTicks(),
                   engine.getLastSoloRenderTicks(),
                   perf.tick() - tAfterRender,
                   engine.getActiveVoiceCount());
    juce::ignoreUnused (tRender);

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

// ============================================================================
// TEMPORARY UI DIAGNOSTICS.  Set to 0 to remove entirely.
//
// Six attempts at this bug have been built on reasoning about what the host,
// the wrapper and the peer are doing.  None of them measured it.  This writes
// the real numbers to grex_ui_debug.txt in the Grex VSTI folder so the next
// fix is aimed at something observed rather than something assumed.
// ============================================================================
#define GREX_UI_DIAGNOSTICS 1

class BetelgeuseEditor : public juce::AudioProcessorEditor
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

       #if GREX_UI_DIAGNOSTICS
        // Start a clean file per editor, then take a reading once everything
        // has settled - the constructor is far too early to be interesting.
        grexDebugFile().deleteFile();

        juce::Component::SafePointer<BetelgeuseEditor> safe (this);
        juce::Timer::callAfterDelay (3000, [safe]() mutable
        {
            if (auto* ed = safe.getComponent())
                ed->logUiState ("3 seconds after open (SETTLED - read this one)");
        });
       #endif
    }

    ~BetelgeuseEditor() override
    {
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
        logUiState ("parentHierarchyChanged");
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
    //==========================================================================
    // WHERE THE DIAGNOSTIC FILE GOES.  One definition, used by both the
    // delete-on-open and the append, so the two can never drift apart.
    //
    // Forward slashes deliberately: juce::File normalises them on Windows, and
    // they keep the string free of backslash escaping.  createDirectory() is
    // there so a missing folder produces a file rather than a silent no-op -
    // appendText into a non-existent directory just fails and says nothing,
    // which is the last thing this particular file should do.
    //==========================================================================
    static juce::File grexDebugFile()
    {
        auto dir = juce::File ("D:/workspace/BetelgeuseArranger/Grex VSTI");
        dir.createDirectory();
        return dir.getChildFile ("grex_ui_debug.txt");
    }

    //==========================================================================
    // WRITE THE REAL GEOMETRY TO A FILE.  Remove with GREX_UI_DIAGNOSTICS 0.
    //
    // Every line here is a number one of the last six fixes ASSUMED.  If the
    // assumption was right the file will say so; if it was not, the file says
    // which one was wrong, which is the thing no amount of further reasoning
    // was going to produce.
    //==========================================================================
    void logUiState (const char* when)
    {
       #if GREX_UI_DIAGNOSTICS
        juce::String t;
        t << "---- " << when << " ----\n";

        // WHICH FORMAT AND WHICH HOST.  Never established in six rounds, and
        // half the reasoning depended on it.
        t << "wrapper       : " << juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType) << "\n";
        t << "host          : " << juce::PluginHostType().getHostDescription() << "\n";

        // THE EDITOR ITSELF.
        t << "editor size   : " << getWidth() << " x " << getHeight() << "\n";
        t << "editor xform  : mat00=" << juce::String (getTransform().mat00, 4)
          << "  mat11=" << juce::String (getTransform().mat11, 4)
          << "  identity=" << (getTransform().isIdentity() ? "yes" : "NO") << "\n";

        // THE PARENT THE WRAPPER OWNS.
        if (auto* parent = getParentComponent())
            t << "wrapper size  : " << parent->getWidth() << " x " << parent->getHeight() << "\n";
        else
            t << "wrapper size  : (no parent component)\n";

        // THE PEER.  This is the layer rounds 4 and 5 argued about.
        if (auto* peer = getPeer())
        {
            t << "peer scale    : " << juce::String (peer->getPlatformScaleFactor(), 4) << "\n";
            t << "peer bounds   : " << peer->getBounds().toString() << "\n";
        }
        else
        {
            t << "peer          : (none)\n";
        }

        // THE DISPLAY, as JUCE sees it.
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            t << "display scale : " << juce::String (d->scale, 4) << "  dpi=" << juce::String (d->dpi, 1) << "\n";
            t << "display area  : total=" << d->totalArea.toString() << "  user=" << d->userArea.toString() << "\n";
        }
        t << "global scale  : " << juce::String (juce::Desktop::getInstance().getGlobalScaleFactor(), 4) << "\n";

        // OPENGL.  getRenderingScale() is the multiplier the GL renderer
        // rasterises the component tree at - the one suspect never tested.
        t << "opengl build  : " << (GREX_EDITOR_USE_OPENGL ? "ON" : "OFF") << "\n";
        t << "opengl attach : " << (openGLContext.isAttached() ? "yes" : "no") << "\n";
        if (openGLContext.isAttached())
            t << "opengl scale  : " << juce::String (openGLContext.getRenderingScale(), 4) << "\n";

        // WHAT MainComponent WILL ACTUALLY DRAW AT.
        const double uiScale = juce::jmin ((double) mainComponent.getWidth()  / (double) kDesignW,
                                           (double) mainComponent.getHeight() / (double) kDesignH);
        t << "maincomp size : " << mainComponent.getWidth() << " x " << mainComponent.getHeight() << "\n";
        t << "maincomp scale: " << juce::String (uiScale, 4) << "   (design "
          << kDesignW << "x" << kDesignH << ")\n\n";

        grexDebugFile().appendText (t);
       #else
        juce::ignoreUnused (when);
       #endif
    }

    void resized() override
    {
        const auto b = getLocalBounds();

        const int w = juce::jmin (b.getWidth(),  juce::jmax (1, constrainer.getMaximumWidth()));
        const int h = juce::jmin (b.getHeight(), juce::jmax (1, constrainer.getMaximumHeight()));

        mainComponent.setBounds (0, 0, w, h);

        logUiState ("resized");
    }

    juce::OpenGLContext& getOpenGLContext() { return openGLContext; }

private:
    BetelgeuseProcessor&    processor;
    MainComponent           mainComponent;
    ProportionalConstrainer constrainer;
    juce::OpenGLContext     openGLContext;
    bool                    glAttached = false;
    bool                    peerPinned = false;
};

juce::AudioProcessorEditor* BetelgeuseProcessor::createEditor()
{
    return new BetelgeuseEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BetelgeuseProcessor();
}



