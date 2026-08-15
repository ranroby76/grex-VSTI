#include "MainComponent.h"
#include "GlobalMacros.h"
#include "ArabicScaleKeyboard.h"
#include "Main.h"
#include "InstrEditPanel.h"  // SlotParams (UI-side struct) used by the
                              // soundsTab.onSlotParamsChanged lambda below.
#include "DrumKitRegistry.h" // returned by the soundsTab.onGetDrumKitRegistry lambda
#include "SlotParamConvert.h" // shared SlotParams -> ChannelParams conversion
#include <array>
#include "RegistrationManager.h"

#include <BinaryData.h>

static const juce::StringArray kTabNames {
    "MAIN", "STYLES", "SOUNDS", "MIXER", "SET EDITOR", "JUMPS", "CRASH", "SETTINGS",
    "TUTORIAL"
};

//==============================================================================
// Format a recognised chord (root pitch-class 0..11 + quality) into a compact
// display string like "C", "Am7", "G7", "Dmaj7", "F#m7b5".
//==============================================================================
static juce::String chordToDisplayString (const Betel::Chord& c)
{
    static const char* kNoteNames[12] =
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const int root = ((c.root % 12) + 12) % 12;
    juce::String s = kNoteNames[root];

    using Q = Betel::ChordQuality;
    switch (c.quality)
    {
        case Q::Maj:                                   break;
        case Q::Maj6:        s += "6";                 break;
        case Q::Maj7:        s += "maj7";              break;
        case Q::Maj7s11:     s += "maj7#11";           break;
        case Q::MajAdd9:     s += "add9";              break;
        case Q::Maj7_9:      s += "maj9";              break;
        case Q::Maj6_9:      s += "6/9";               break;
        case Q::Aug:         s += "aug";               break;
        case Q::Min:         s += "m";                 break;
        case Q::Min6:        s += "m6";                break;
        case Q::Min7:        s += "m7";                break;
        case Q::Min7b5:      s += "m7b5";              break;
        case Q::MinAdd9:     s += "m add9";            break;
        case Q::Min7_9:      s += "m9";                break;
        case Q::Min7_11:     s += "m11";               break;
        case Q::MinMaj7:     s += "mMaj7";             break;
        case Q::MinMaj7_9:   s += "mMaj9";             break;
        case Q::Dim:         s += "dim";               break;
        case Q::Dim7:        s += "dim7";              break;
        case Q::Dom7:        s += "7";                 break;
        case Q::Dom7sus4:    s += "7sus4";             break;
        case Q::Dom7b5:      s += "7b5";               break;
        case Q::Dom7_9:      s += "9";                 break;
        case Q::Dom7s11:     s += "7#11";              break;
        case Q::Dom7_13:     s += "13";                break;
        case Q::Dom7b9:      s += "7b9";               break;
        case Q::Dom7b13:     s += "7b13";              break;
        case Q::Dom7s9:      s += "7#9";               break;
        case Q::Maj7Aug:     s += "maj7#5";            break;
        case Q::Dom7Aug:     s += "7#5";               break;
        case Q::OnePlus8:    s += "(oct)";             break;
        case Q::OnePlus5:    s += "5";                 break;
        case Q::Sus4:        s += "sus4";              break;
        case Q::OnePlus2Plus5: s += "sus2";            break;
        default:                                       break;
    }
    return s;
}


MainComponent::MainComponent(BetelgeuseProcessor& proc)
    : processor(proc),
      keyboardState(proc.getKeyboardState()),
      keyboardComponent(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setWantsKeyboardFocus(true);
    setOpaque(true);

    // ── Load background from compiled binary data ─────────────────────────────
    {
        int dataSize = 0;
        const char* data = BinaryData::getNamedResource("back_png", dataSize);
        if (data != nullptr && dataSize > 0)
            background = juce::ImageFileFormat::loadFrom(data, (size_t)dataSize);
        else
            background = {};
    }

    // ── Header animation ──────────────────────────────────────────────────────
    addAndMakeVisible(headerAnimation);

    // ── Left panel: Row 1 ────────────────────────────────────────────────────
    knobTempo.setDragPixelsPerStep(3.0);
    knobTempo.onChange = [this] (double v)
    {
        // Only meaningful in FREE mode; in SYNCED the knob is locked and the
        // timer drives its displayed value from the host clock.
        processor.setManualBPM ((float) v);
    };
    addAndMakeVisible(knobTempo);

    selectorSpeed.onChange = [this] (int idx)
    {
        // {"x1","x2","x1/2"}
        const float m = (idx == 1) ? 2.0f : (idx == 2) ? 0.5f : 1.0f;
        processor.setTempoSpeedMult (m);
    };
    addAndMakeVisible(selectorSpeed);

    selectorTempoType.onChange = [this] (int idx)
    {
        // {"FREE","SYNCED"} — index 1 = SYNCED (follow host clock)
        //
        // MASTER SETTING.  Who owns the clock is a property of this install and
        // this host, not of a song, so it is written to grex_master.xml the
        // instant it moves and no set can change it.  See MasterSettings.h.
        processor.setTempoSynced (idx == 1);
        Betel::MasterSettings::get().setTempoSynced (idx == 1);
    };
    addAndMakeVisible(selectorTempoType);

    // ── Left panel: Row 2 ────────────────────────────────────────────────────
    btnTap.onClick = [this] { processor.tapTempo(); };
    addAndMakeVisible(btnTap);
    addAndMakeVisible(btnResetTempo);
    btnResetTempo.onClick = [this] { processor.resetTempoOverride(); };

    // ── Left panel: Row 3 ────────────────────────────────────────────────────
    selectorArrangerPiano.onChange = [this] (int idx)
    {
        // index 0 = ARRANGER (accompaniment on), 1 = PIANO (whole-keyboard solo)
        processor.setPianoMode (idx == 1);
        keyboardComponent.setPianoMode (idx == 1);
    };
    addAndMakeVisible(selectorArrangerPiano);

    selectorSingleMulti.onChange = [this] (int idx)
    {
        // index 0 = SINGLE (one solo slot at a time), 1 = MULTI (layered)
        mainTab.setSoloSingleMode (idx == 0);
    };
    addAndMakeVisible(selectorSingleMulti);
    addAndMakeVisible(selectorTransition);
    selectorTransition.onChange = [this] (int idx)
    {
        // TRANS grid: 0 = coarse (two beats, or the whole bar in odd meters),
        // 1 = one beat, 2 = half a beat.  METER-AWARE — derived from the
        // style's bar/beat, so it divides the bar evenly in 4/4, 3/4, 9/8 …
        // (in 4/4 that is literally a 1/2, 1/4 and 1/8 note, matching the
        // labels).  Applied to ANY pending section change (variation switch,
        // fill, break, ending) and fires regardless of the section currently
        // playing (intros / fills / endings included).
        processor.setTransitionQuant (idx);
    };
    addAndMakeVisible(selectorFillLength);
    selectorFillLength.onChange = [this] (int idx)
    {
        // Subdivides the ONE-BAR CAP that every variation fill now gets (see
        // enterSection): a fill longer than a bar is already reduced to its
        // last bar before this applies.
        //   0 = "1"   : that capped window whole — a 1-bar fill plays entire,
        //               a 2-bar fill plays its second bar.
        //   1 = "1/2" : its second half, for a short punchy fill.
        // Either way the fill ENDS on the same beat, so the hand-off to the
        // destination section is unchanged; only the entry moves.
        // Applies to the four variation fills; the BREAK keeps its authored
        // length, and intros / endings are untouched.
        processor.setFillLength (idx);
    };
    selectorFillLength.setSelectedIndex (0, false);      // default: whole fill
    processor.setFillLength (selectorFillLength.getSelectedIndex());

    // Default the transition quantize to 1/4 (the second option) instead of
    // 1/2.  setSelectedIndex(.., false) updates the selector silently; we then
    // push the value to the processor explicitly so the engine starts on 1/4.
    selectorTransition.setSelectedIndex (1, false);
    processor.setTransitionQuant (selectorTransition.getSelectedIndex());

    // ── Left panel: Row 4 ────────────────────────────────────────────────────
    knobGlobalSemitone.setDragPixelsPerStep(6.0);
    knobGlobalSemitone.onChange = [this] (double v)
    {
        processor.setGlobalTranspose ((int) std::lround (v));
    };
    addAndMakeVisible(knobGlobalSemitone);
    addAndMakeVisible(lblPlayedChord);

    // ── Left panel: Row 5 ────────────────────────────────────────────────────
    addAndMakeVisible(lblStyleName);

    // ── Left panel: Row 6 ────────────────────────────────────────────────────
    // ── Global macros ────────────────────────────────────────────────────────
    // Toggle the flag, re-commit every slot (that is what makes it audible —
    // see SoundsTab::refreshMacroFx), then relight the button.  The macro
    // presets themselves are global and live in their own files; only the
    // ENGAGED state travels with a set.
    addAndMakeVisible (btnBigDrums);
    btnBigDrums.onClick = [this]
    {
        auto& gm = Betel::GlobalMacros::get();
        gm.setBigDrumsOn (! gm.isBigDrumsOn());
        soundsTab.refreshBigDrums();     // the drum rack only — see refreshBigDrums
        refreshMacroButtons();
    };

    addAndMakeVisible (btnFunkeyMode);
    btnFunkeyMode.onClick = [this]
    {
        auto& gm = Betel::GlobalMacros::get();
        gm.setFunkeyOn (! gm.isFunkeyOn());
        soundsTab.refreshMacroFx();
        refreshMacroButtons();
    };
    refreshMacroButtons();

    addAndMakeVisible(btnLoadSet);
    addAndMakeVisible(btnSaveSet);
    addAndMakeVisible(btnFastSave);

    // SAVE SET → write {sounds + global} payload to a chosen .bset file.
    btnSaveSet.onClick = [this]
    {
        auto& m = processor.getFolderManager();
        const auto start = m.isRootFolderValid()
                              ? m.getSetsFolder()
                              : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        setChooser = std::make_unique<juce::FileChooser> (
            "Save Betelgeuse set", start, "*.bset");

        setChooser->launchAsync (juce::FileBrowserComponent::saveMode
                               | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File()) return;
                if (file.getFileExtension().isEmpty())
                    file = file.withFileExtension ("bset");

                // ONE writer for both save buttons — see writeSetToFile.  This
                // used to build its own payload, and it built a SMALLER one than
                // the editor-close snapshot did: no macro engaged-state, no
                // style selection.  So a set saved from this dialog quietly lost
                // Funkey/Big Drums and came back on the wrong browser page.
                writeSetToFile (file);
            });
    };

    // LOAD SET → read a .bset file and apply it.
    btnLoadSet.onClick = [this]
    {
        auto& m = processor.getFolderManager();
        const auto start = m.isRootFolderValid()
                              ? m.getSetsFolder()
                              : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        setChooser = std::make_unique<juce::FileChooser> (
            "Load Betelgeuse set", start, "*.bset");

        setChooser->launchAsync (juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file.existsAsFile())
                    applySetFromFile (file);
            });
    };

    // FAST SAVE -> write the current state straight back to the set it came
    // from.  No chooser, no confirmation, no round trip: one press and it is on
    // disk.  This is the button you reach for after every good tweak, and a
    // dialog in that loop is the reason tweaks go unsaved.
    //
    // TARGET, in order:
    //   1. the set this session was loaded from (lastSetFile), else
    //   2. the set belonging to the style the BROWSER has selected, else
    //   3. the set belonging to the style the PROCESSOR is actually holding.
    //
    // STEP 3 IS THE ONE THAT WAS MISSING, and it is why FAST SAVE could answer
    // NO TARGET underneath a style you could hear playing.  pendingStylePath is
    // an EDITOR member, written only by StylesTab::onStyleSelected, so it is
    // empty in every session where nobody clicked the browser -- reopening a
    // DAW project, or just closing and reopening the plugin window.  The
    // processor kept playing throughout and knew the path the whole time:
    // lastLoadedStylePath is stamped inside loadStyle, which EVERY route to a
    // loaded style passes through, so it is the honest authority for "what is
    // playing" in a way the browser selection never was.
    //
    // The target does NOT have to exist.  writeSetToFile creates the parent
    // directory, so on the first press this MAKES the style's set out of the
    // current settings -- the same self-maintaining behaviour the factory pack
    // already relied on, no longer conditional on having browsed to the style
    // by hand.  writeSetToFile also stamps lastSetFile, so the SET MANAGER pill
    // picks the new name up on the next timer tick and every later press goes
    // straight to step 1.
    btnFastSave.onClick = [this]
    {
        juce::File target = lastSetFile;

        if (! target.existsAsFile())
            target = setFileForStyle (pendingStylePath);

        if (target == juce::File())
            target = setFileForStyle (processor.getLastLoadedStylePath());

        if (target == juce::File())
        {
            // Still nothing to aim at.  Only three things can cause that now,
            // and they want different actions from the user, so name the one
            // that actually happened instead of making them guess:
            //
            //   NO STYLE  - nothing loaded anywhere.  Pick a style first.
            //   NO ROOT   - a style IS loaded, but the Grex root folder is unset
            //               or gone (moved library, absent drive), so no set
            //               path can be built at all.  This is the one that used
            //               to look like a bug, because everything on screen
            //               still looks normal.
            //   NO TARGET - the leftover: a style sitting outside a genre
            //               folder, leaving setFileForStyle no genre to name the
            //               set's folder after.
            const bool haveStyle = pendingStylePath.isNotEmpty()
                                || processor.getLastLoadedStylePath().isNotEmpty();
            const bool rootOk    = processor.getFolderManager().isRootFolderValid();

            btnFastSave.setButtonText (! haveStyle ? "NO\nSTYLE"
                                     : ! rootOk    ? "NO\nROOT"
                                                   : "NO\nTARGET");

            juce::Component::SafePointer<MainComponent> safe (this);
            juce::Timer::callAfterDelay (1400, [safe]
            {
                if (auto* c = safe.getComponent()) c->btnFastSave.setButtonText ("FAST\nSAVE");
            });
            return;
        }

        const bool ok = writeSetToFile (target);
        if (ok)
        {
            btnFastSave.flash (450);      // the receipt - nothing else opens
        }
        else
        {
            btnFastSave.setButtonText ("SAVE\nFAILED");
            juce::Component::SafePointer<MainComponent> safe (this);
            juce::Timer::callAfterDelay (1800, [safe]
            {
                if (auto* c = safe.getComponent()) c->btnFastSave.setButtonText ("FAST\nSAVE");
            });
        }
    };

    // #C2C2C2 like every other caption on this panel — see
    // InstrEditStyle::kPanelLabel.
    btnComments .setTextColour(InstrEditStyle::kPanelLabel);
    btnForgetAll.setTextColour(InstrEditStyle::kPanelLabel);
    btnDawStart .setTextColour(InstrEditStyle::kPanelLabel);
    btnForgetAll.setDrawTopSeparator(true);

    addAndMakeVisible(btnComments);
    btnComments.onClick = [this]
    {
        if (commentsWindow == nullptr)
        {
            commentsWindow = std::make_unique<CommentsWindow> (processor.getComments());
            commentsWindow->onSave  = [this] (const juce::String& t) { processor.setComments (t); };
            commentsWindow->onClose = [this] { commentsWindow.reset(); };
        }
        else
        {
            commentsWindow->setVisible (true);
            commentsWindow->toFront (true);
        }
    };
    addAndMakeVisible(btnForgetAll);
    btnForgetAll.onClick = [this] { toggleArabicQuickEditor(); };
    addAndMakeVisible(btnDawStart);
    btnDawStart.onClick = [this]
    {
        processor.setDawStartFollow (btnDawStart.getToggleState());
    };

    // ── Left panel: Row 7 ────────────────────────────────────────────────────
    addAndMakeVisible(lblSetName);

    // ── Right panel: Tab buttons ──────────────────────────────────────────────
    for (int i = 0; i < kNumTabs; ++i)
    {
        tabButtons[i].setButtonText(kTabNames[i]);
        tabButtons[i].onClick = [this, i] { selectTab(i); };
        addAndMakeVisible(tabButtons[i]);
    }

    // ── Right panel: Tab content ──────────────────────────────────────────────
    tabContentContainer.setOpaque(false);
    addAndMakeVisible(tabContentContainer);

    tabPages = { &mainTab, &stylesTab, &soundsTab, &mixerTab,
                 &setEditorTab, &jumpsTab, &crashTab, &settingsTab,
                 &tutorialTab };

    for (auto* page : tabPages)
    {
        page->setOpaque(false);
        tabContentContainer.addChildComponent(page);
    }

    selectTab(0);

    // ── MainTab ↔ StyleSequencer: arranger transport wiring ───────────────────
    //
    // The 16 variation buttons map onto the 15 sections of a Yamaha style as:
    //
    //   variButton  | label    | sequencer action
    //   ────────────┼──────────┼───────────────────────────────────────────────
    //     0..2      | INTRO 1-3| triggerIntro(0..2); auto-start if stopped
    //     3         | INTRO 4  | no-op (format has no IntroD)
    //     4..7      | VAR 1-4  | selectVariation(A..D, viaFill=false) - direct
    //     8..11     | FILL 1-4 | selectVariation(idx-8, viaFill=true) + triggerFill()
    //     12        | BRAKE    | triggerBreak()
    //     13..15    | END 1-3  | triggerEnding(0..2)
    //
    // Play-control row maps directly: PLAY/STOP toggles transport, CRASH fires
    // a fill on the current variation, RESTART does stop+start.
    mainTab.onVariationClicked = [this] (int variIdx)
    {
        processor.performVariation (variIdx);
    };

    // 1 FINGER ↔ FINGERED — toggle the ChordZoneTracker's recognition mode.
    // THE BUTTON USED TO LIE.  It is captioned "1 FINGER" and lighting it puts
    // you in FINGERED — so the one state where the label mattered was the one
    // state where it was wrong, and the panel read "1 FINGER" while the
    // detector was resolving full chord qualities.  The alternate caption makes
    // the lit state say what it is.
    mainTab.getBtnFingered().setAlternateText ("FINGERED");

    mainTab.onChordModeToggled = [this] (bool fingered)
    {
        // MASTER SETTING — see MasterSettings.h.  Which hand shape names a
        // chord follows the PLAYER across every style and every set, and is
        // saved the moment it changes.
        Betel::MasterSettings::get().setFingeredChord (fingered);

        processor.getChordTracker().setChordMode (
            fingered ? Betel::ChordZoneTracker::ChordMode::Fingered
                     : Betel::ChordZoneTracker::ChordMode::SingleFinger);
    };

    mainTab.onPlayStopToggled = [this] (bool wantsPlay)
    {
        auto& seq = processor.getSequencer();
        if (wantsPlay)
        {
            if (processor.hasStyle()) seq.start();
            else                      mainTab.setPlayStopVisual (false);   // refuse silently
        }
        else
        {
            seq.stop();
        }
    };

    mainTab.onSyncPlayToggled = [this] (bool on)
    {
        // Arm / disarm sync-start in the chord tracker.  When armed, the
        // first chord-zone noteOn that lands in an empty buffer fires
        // sequencer.start() (see the onSyncStartTriggered hook below).
        // The flag clears itself on trigger; the timerCallback resyncs
        // the button's visual state to match.
        if (on) processor.getChordTracker().armSyncStart();
        else    processor.getChordTracker().disarmSyncStart();
    };

    mainTab.onCrashClicked = [this]
    {
        // Literal crash-cymbal hit on the drum kit (GM note 49), independent
        // of whether a style is loaded or playing.
        processor.triggerCrash();
    };

    mainTab.onRestartClicked = [this]
    {
        // Re-cue the current variation's loop to tick 0 without stopping or
        // changing the variation.  Bypasses HOLD.
        processor.requestRestart();
    };

    // STYLE ELEMENTS ON/OFF — each of the 8 buttons in MainTab maps 1:1 to
    // an engine style channel (0..7 = DRUMS / PERC / BASS / CHORD 1 /
    // CHORD 2 / PAD / LEAD 1 / LEAD 2).  Clearing the toggle mutes the
    // corresponding channel in the dispatcher and flushes its held notes.
    mainTab.onStyleElementToggled = [this] (int slotIdx, bool on)
    {
        processor.getStylePlayer().setChannelMute (slotIdx, ! on);
    };

    // SOLO ELEMENTS — the 8-bit enable mask is what processBlock uses to
    // route above-split MIDI.  Empty mask falls back to legacy single-slot
    // routing via activeSoloSlot so the default behaviour is unchanged.
    mainTab.onSoloMaskChanged = [this] (juce::uint8 mask)
    {
        processor.setSoloEnableMask (mask);
    };

    // ONPRESS = gate mode.  While enabled, the style plays only as long as a
    // chord is held in the chord zone: pressing a chord starts it, releasing
    // all chord-zone keys stops it, repeatable.
    mainTab.onSyncStopToggled = [this] (bool on)
    {
        processor.getChordTracker().setGateMode (on);
    };

    // Sync-start one-shot — fires on the audio thread from
    // ChordZoneTracker::noteOn.  sequencer.start() just flips an atomic, so
    // it's safe to call from there.  The PLAY/STOP button visual catches up
    // on the next timerCallback tick.
    processor.getChordTracker().onSyncStartTriggered = [this]
    {
        processor.getSequencer().start();
    };

    // Sync-stop one-shot — fires on the audio thread when the chord zone
    // empties while sync-stop is armed.  sequencer.stop() is atomic-safe.
    processor.getChordTracker().onSyncStopTriggered = [this]
    {
        processor.getSequencer().stop();
    };

    // Poll the sequencer ~30 Hz so the PLAY button visual catches up when
    // transport stops on its own (e.g. an ending finishes playing), the
    // SYNC PLAY button releases on auto-disarm, and the green
    // "currently-playing" pip moves to the section the sequencer entered
    // at the last bar boundary.
    startTimerHz (30);

    // ── StylesTab ↔ processor: genre/style browser (folder-driven) ────────────
    //
    // Genres are the sub-folders of <root>/styles; each genre's styles are the
    // SFF files inside it.  rescanStyleBrowser() populates both grids; it runs
    // now and again whenever the styles folder changes.
    rescanStyleBrowser();

    stylesTab.onStyleSelected = [this] (const juce::String& absolutePath)
    {
        pendingStylePath = absolutePath;
        // The PATH is what lights a cell — one style in the whole library, not
        // cell N of the folder on screen.  setSelectedStylePath sets the name
        // from it, so this is the single call that keeps the grid honest.
        stylesTab.setSelectedStylePath (absolutePath);

        // No LOAD button anymore — selecting a style loads it immediately.
        if (pendingStylePath.isEmpty()) return;

        // LEVELS ARE PER STYLE.  Start from the default template rather than
        // inheriting whatever the previous style was tuned to — otherwise one
        // style's "pull the perc down" would silently follow the user around
        // the whole library.  The style's own set overwrites this a moment
        // later if it has one.
        Betel::StyleLevels::get().applyDefaults();

        // EVERY USER SFZ IS DROPPED ON A STYLE CHANGE.  The override holds its
        // slot against program changes, so without this the previous song's
        // instrument would survive into the next style and the arrangement could
        // never take the slot back.  A set restores its own a moment later.
        processor.clearAllSfz();
        soundsTab.refreshSfzState();

        // ── THE SET IS WHAT LOADS, NOT THE STYLE ─────────────────────────────
        //
        // Every style ships with a set beside it, and that set is the thing the
        // user is really picking: it carries the style's levels and whatever
        // else has been tuned for it.  The style file is loaded BY the set.
        //
        // The style path is pushed into the payload before applying it, so the
        // set never has to name an absolute path that would break the moment
        // the library moved — the browser already knows where the file is.
        const auto setFile = setFileForStyle (pendingStylePath);
        if (setFile.existsAsFile())
        {
            if (auto xml = juce::XmlDocument::parse (setFile))
            {
                auto payload = juce::ValueTree::fromXml (*xml);
                if (payload.isValid())
                {
                    if (auto gs = payload.getChildWithName ("GlobalState"); gs.isValid())
                        gs.setProperty ("currentStylePath", pendingStylePath, nullptr);
                    else
                    {
                        // Factory set: give it a link the applier can resolve.
                        auto link = payload.getChildWithName ("StyleLink");
                        if (! link.isValid())
                        {
                            link = juce::ValueTree ("StyleLink");
                            payload.appendChild (link, nullptr);
                        }
                        const auto rel = juce::File (pendingStylePath)
                                             .getRelativePathFrom (processor.getFolderManager()
                                                                        .getStylesFolder());
                        link.setProperty ("relPath", rel, nullptr);
                    }

                    applySetPayload (payload);
                    lastSetFile = setFile;
                    setEditorTab.refreshFromState();
                    soundsTab.applySoundCalibration();
                    refreshVariationAvailability();
                    return;
                }
            }
        }

        // NO SET FOR THIS STYLE — load the style bare, on the template levels.
        // Hand the style slots back to calibration: nothing owns them now.
        soundsTab.clearStyleSlotOwnership();

        // Nothing declares FUNKEY / BIG DRUMS for this style, so nothing asks
        // for them: an invalid tree means both off.  Without this the toggles
        // stayed lit from whatever style was loaded before, which is the third
        // and last of the sticky paths.
        applyMacroEngagedState (juce::ValueTree());

        // IGNORE PROGRAM CHANGE, for the same reason and with the same shape.
        // A bare style never reached applySetPayload, so all eight toggles kept
        // the previous set's answer - and a slot locked against a style it was
        // never locked FOR is a slot the style cannot voice and the set cannot
        // correct.  An invalid tree clears all eight.
        processor.applyIgnorePcState (juce::ValueTree());
        soundsTab.refreshIgnorePcButton();

        // Not an error: a style dropped into the folder by hand has no set yet,
        // and it should still play.  The first FAST SAVE gives it one.
        setEditorTab.refreshFromState();

        juce::String err;
        if (! processor.loadStyle (juce::File (pendingStylePath), err))
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Could not load style",
                err.isEmpty() ? juce::String ("Unknown error") : err);
        else
            soundsTab.applySoundCalibration();   // force PAD cutoff + drum FX onto the engine

        refreshVariationAvailability();
    };

    // STOP-ONLY style changes.  The tab asks before it acts on a click, and
    // says so in the pill when the answer is no — see StylesTab::onGridButtonClicked.
    stylesTab.onIsPlaying = [this] { return processor.isStylePlaying(); };

    // Startup auto-arm veto: only arm when nothing has loaded a style yet.
    stylesTab.onNeedsStyle = [this] { return ! processor.hasStyle(); };

    stylesTab.onShowStyleData = [this]
    {
        const Betel::StyleData* st = processor.getCurrentStyle();
        if (st == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon,
                "Style Data",
                "No style is loaded yet. Select a style first.");
            return;
        }

        if (styleDataWindow == nullptr)
        {
            styleDataWindow = std::make_unique<Betel::StyleDataWindow> (*st, &processor.getStylePlayer());
            styleDataWindow->onClose = [this] { styleDataWindow.reset(); };
            // The LIBRARY CENSUS tab needs to know where the styles live.
            styleDataWindow->setStylesFolder (processor.getFolderManager().getStylesFolder());
        }
        else
        {
            styleDataWindow->setStylesFolder (processor.getFolderManager().getStylesFolder());
            styleDataWindow->refreshFrom (*st);
            styleDataWindow->setVisible (true);
            styleDataWindow->toFront (true);
        }
    };

    // ── Global style-search bar (fixed strip under the tab menu) ──────────────
    styleSearchBox.setTextToShowWhenEmpty ("Search styles across all genres...",
                                           juce::Colour (0xFF777777));
    styleSearchBox.setColour (juce::TextEditor::backgroundColourId,      juce::Colour (0xFF1A1A1A));
    styleSearchBox.setColour (juce::TextEditor::textColourId,            juce::Colours::white);
    styleSearchBox.setColour (juce::TextEditor::outlineColourId,         juce::Colour (0xFF555555));
    styleSearchBox.setColour (juce::TextEditor::focusedOutlineColourId,  juce::Colour (0xFFCC6600));
    styleSearchBox.setReturnKeyStartsNewLine (false);
    styleSearchBox.onReturnKey = [this] { runStyleSearch(); };
    addAndMakeVisible (styleSearchBox);

    btnStyleSearch     .onClick = [this] { runStyleSearch();   };
    btnStyleSearchClear.onClick = [this] { clearStyleSearch(); };
    addAndMakeVisible (btnStyleSearch);
    addAndMakeVisible (btnStyleSearchClear);

    // The search bar belongs to the STYLES tab only.  The initial tab is MAIN
    // (selectTab(0) above ran before these were created), so hide them now to
    // avoid a flash of the search bar on first load.
    styleSearchBox     .setVisible (false);
    btnStyleSearch     .setVisible (false);
    btnStyleSearchClear.setVisible (false);

    // ── SET EDITOR tab -> host ───────────────────────────────────────────────
    //
    // The set BROWSER that used to be here is gone: every style now ships with
    // its own set and the Styles tab loads it, so there was nothing left to go
    // and fetch.  What remains is editing the set that is loaded, and the only
    // wire that needs is "a level moved, make it audible".
    setEditorTab.styleNameProvider = [this] { return stylesTab.getSelectedStyleName(); };
    setEditorTab.onLevelsChanged   = [this] { processor.reapplyCurrentStyle(); };

    // ── BAKE THE SET PACK ────────────────────────────────────────────────────
    //
    // Writes each style's GM starting point into its own set, so the values the
    // old automatic table used to impose on every load are now WRITTEN DOWN,
    // per style, and editable.  Parses rather than loads — see SetBaker.
    setEditorTab.onBakeAllSets = [this] (bool overwrite)
    {
        auto& m = processor.getFolderManager();
        if (! m.isRootFolderValid())
        {
            setEditorTab.setBakeResult ("no library folder", false);
            return;
        }

        setBaker = std::make_unique<Betel::SetBaker> (
                       processor.getStylePlayer().getBankMap());

        setBaker->onProgress = [this] (int done, int total, const juce::String&)
        {
            setEditorTab.setBakeProgress (done, total);
        };

        setBaker->onFinished = [this] (int written, int skipped, int failed, bool cancelled)
        {
            juce::String msg = juce::String (written) + " written";
            if (skipped > 0) msg += ", " + juce::String (skipped) + " kept";
            if (failed  > 0) msg += ", " + juce::String (failed)  + " failed";
            if (cancelled)   msg += " (stopped)";
            setEditorTab.setBakeResult (msg, failed == 0);
        };

        if (! setBaker->start (m.getStylesFolder(), m.getSetsFolder(), overwrite))
            setEditorTab.setBakeResult ("no styles found", false);
        else
            setEditorTab.setBakeProgress (0, setBaker->total());
    };

    setEditorTab.onCancelBake = [this] { if (setBaker) setBaker->cancel(); };

    // ── Wire SettingsTab CC-learn → engine ────────────────────────────────────
    settingsTab.onLearnRequested = [this] (int target)
    {
        processor.setCcLearnTarget (target);   // -1 cancels
    };
    settingsTab.onForgetRequested = [this] (int target)
    {
        processor.forgetCc (target);
        Betel::CcMap::get().setCc (target, -1);      // persist immediately
        settingsTab.setCcNumber (target, processor.getCcNumber (target));
    };
    // ── THE CC LEARN MAP ─────────────────────────────────────────────────────
    //
    // Installation-wide, in grex_cc_map.xml — it describes the rig, not a song,
    // so it is read here and pushed onto the processor before the rows are
    // drawn.  No set can move it any more.  See CcMap.h.
    {
        auto& cm = Betel::CcMap::get();
        cm.load();
        for (int i = 0; i < SettingsTab::kNumCcTargets; ++i)
            processor.setCcNumber (i, cm.ccFor (i));
    }

    // ── THE TWO OPTIONAL PACKS, BY CATEGORY ──────────────────────────────────
    //
    // Both ask the ENGINE, which knows which folder each sound was found in.
    // The old provider filtered on `flag >= 200`, which could only say "not GM"
    // - with two packs installed it would have merged them into one list.
    //
    // Asked fresh on every pack and category click rather than cached: a pack
    // is a folder the user can drop in or delete, so "what is installed" is a
    // question with a different answer between two clicks.
    soundsTab.onGetInstrumentPack = [this] (int flag)
    {
        return processor.getInstrumentPack (flag);
    };

    soundsTab.onGetPackCategories = [this] (int pack)
    {
        return processor.getPackCategories (pack);
    };

    soundsTab.onGetPackInstruments = [this] (int pack, const juce::String& category)
    {
        return processor.getPackInstruments (pack, category);
    };

    for (int i = 0; i < SettingsTab::kNumCcTargets; ++i)
        settingsTab.setCcNumber (i, processor.getCcNumber (i));

    // STYLE VOLUME and RIGHT-HAND (solo) VOLUME now live on the Mixer tab — see
    // the MixerTab wiring below.  The engine bus gains default to unity, so no
    // startup push is needed here.

    // (The global "style bass allowed-notes" slider has been relocated to the
    // per-instrument sound editor — SynthesisPanel "ALLOWED NOTES" — and is now
    // driven per slot via soundsTab.onSlotParamsChanged below.)

    // ── Wire SettingsTab bypass-instrument-audio-chain toggle → processor ─────
    // Single ON/OFF button; ON strips every per-channel filter + post-mix FX
    // stage so the only thing between the sample and the mixer is the amp
    // envelope (plus pan/volume/velocity).  Includes drum channels.
    settingsTab.onBypassInstrumentChainChanged = [this] (bool b)
    {
        processor.setBypassInstrumentChain (b);
    };
    settingsTab.setBypassInstrumentChain (processor.getBypassInstrumentChain());

    // ── Wire the GLOBAL SETTINGS macro sections ──────────────────────────────
    // Identical handling to the left-panel BIG DRUMS / FUNKEY MODE buttons —
    // deliberately so: the two surfaces are two views of the same flag, and
    // refreshMacroButtons() relights both, so flipping either moves the other.
    settingsTab.onFunkeyModeToggled = [this] (bool on)
    {
        Betel::GlobalMacros::get().setFunkeyOn (on);
        soundsTab.refreshMacroFx();
        refreshMacroButtons();
    };

    settingsTab.onBigDrumsToggled = [this] (bool on)
    {
        Betel::GlobalMacros::get().setBigDrumsOn (on);
        soundsTab.refreshBigDrums();     // the drum rack only — see refreshBigDrums
        refreshMacroButtons();
    };

    // A knob moved inside a macro editor.  The preset is already updated (the
    // editor writes straight into GlobalMacros), so all that is left is to
    // re-commit every slot — which is what makes the edit audible on the
    // running arrangement instead of only after a reload.
    settingsTab.onMacroFxEdited = [this] { soundsTab.refreshMacroFx(); };

    settingsTab.onPitchBendRangeChanged = [this] (int semis)
    {
        processor.setSoloPitchBendRange (semis);
    };

    settingsTab.onLowVelBoostChanged = [this] (int amount)
    {
        processor.setSoloLowVelBoost (amount);
    };

    // IGNORE PRESET CHANGES reaches the engine as well as the tab: a .ins can
    // arrive at a channel without the tab being involved at all.
    soundsTab.onSlotFreezeChanged = [this] (int slot, bool isSolo, bool ignore)
    {
        processor.setSlotIgnorePresetParams (slot, isSolo, ignore);
    };

    // A style-level setting moved.  The bus gain, the makeup and the role fader
    // trims are all decided at style load, so the change only becomes audible
    // once the style is re-applied — do it here rather than making the user
    // reload by hand to hear what a slider did.
    // (style levels moved to the SET EDITOR tab — wired above)

    // ── MIDI song transport (record / play) ───────────────────────────────────
    // PARKED: the recorder/player are wired and ready, but the transport UI is
    // hidden for now. With no visible controls it can never arm or play, so the
    // feature stays fully dormant. To bring it to life later, switch this to
    // addAndMakeVisible (and shape its placement/look then).
    addChildComponent (transportBar);

    transportBar.onRecord = [this]
    {
        processor.getSongPlayer().stop();
        processor.getSongRecorder().arm (captureSongSetup());
        transportBar.setPlaying (false);
        transportBar.setArmed   (true);
    };

    transportBar.onStop = [this]
    {
        processor.getSongRecorder().stop();
        processor.getSongPlayer().stop();
        transportBar.setArmed     (false);
        transportBar.setRecording (false);
        transportBar.setPlaying   (false);
    };

    transportBar.onSave = [this]
    {
        if (processor.getSongRecorder().getEventCount() == 0) return;
        const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                             .getChildFile ("Grex VSTI");
        auto* chooser = new juce::FileChooser ("Save Grex song",
                                               dir.getChildFile ("song.grxsong"), "*.grxsong");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f != juce::File())
                    processor.getSongRecorder().saveToFile (f.withFileExtension ("grxsong"));
                delete chooser;
            });
    };

    transportBar.onLoad = [this]
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                             .getChildFile ("Grex VSTI");
        auto* chooser = new juce::FileChooser ("Load Grex song", dir, "*.grxsong");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f.existsAsFile())
                {
                    GrexSongRecorder::Setup setup;
                    std::vector<GrexSongRecorder::Event> events;
                    if (GrexSongRecorder::loadFromFile (f, setup, events))
                    {
                        applySongSetup (setup);                        // restore session
                        processor.getSongPlayer().setSong (setup, events);
                        transportBar.setArmed     (false);
                        transportBar.setRecording (false);
                        transportBar.setPlaying   (false);   // press PLAY to start
                    }
                }
                delete chooser;
            });
    };

    transportBar.onPlay = [this]
    {
        processor.getSongRecorder().stop();
        processor.getSongPlayer().start();
        transportBar.setArmed     (false);
        transportBar.setRecording (false);
        transportBar.setPlaying   (true);
    };

    // ── Wire CrashTab → processor ─────────────────────────────────────────────
    crashTab.onCrashOnTransitionChanged = [this] (bool b) { processor.setCrashOnTransition (b); };
    crashTab.onAutoCrashEnabledChanged  = [this] (bool b) { processor.setAutoCrashEnabled (b); };
    crashTab.onAutoCrashEveryNChanged   = [this] (int n)  { processor.setAutoCrashEveryN (n); };
    crashTab.onManualCrash              = [this]          { processor.triggerCrash(); };
    crashTab.onCrashNotesChanged = [this] (juce::uint8 m) { processor.setCrashNoteMask (m); };
    crashTab.onCrashVelocityChanged     = [this] (int v)  { processor.setCrashVelocity (v); };
    crashTab.onCrashGainChanged         = [this] (float p){ processor.setCrashGainPercent (p); };

    // Double-click the GAIN handle -> a dB text box.  Same gesture, same shape
    // and the same wording as BASE UNITY on an instrument, because it is the
    // same idea: a level you TYPE because you know the number, rather than one
    // you hunt for with a drag.
    crashTab.onCrashGainDialogRequested = [this]
    {
        // BASE UNITY, not a fader position.
        //
        // This box used to call setCrashGainDb, which converted the figure into
        // a position and MOVED the handle you had just double-clicked.  So the
        // one gesture meant for calibrating the cymbal destroyed the setting it
        // was launched from, and there was no way to say "this sample is 4 dB
        // hot" without also losing your fader.
        //
        // It now sets what the 75 detent is WORTH.  The handle does not move:
        // park it on 75 and you hear exactly the base; everything below stays a
        // proportion of it and everything above stays the same +6 dB above it.
        // Same gesture and same meaning as the instrument GAIN handle.
        const float base = processor.getCrashBaseUnityDb();

        auto* w = new juce::AlertWindow (
            "CRASH BASE UNITY",
            "What the GAIN fader's unity detent (75) is worth, in dB.\n"
            "This calibrates the cymbal SAMPLE; it does not move the fader.\n"
            "0 dB is the sample as recorded. Range: -40 to +40 dB.\n"
            "Fader now at " + juce::String (processor.getCrashGainPercent(), 0)
                            + ", giving "
                            + juce::String (processor.getCrashGainDb(), 2) + " dB.",
            juce::MessageBoxIconType::NoIcon);

        w->addTextEditor ("db", juce::String (base, 2), "dB:");
        w->addButton ("SET",    1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, w] (int result)
            {
                std::unique_ptr<juce::AlertWindow> owned (w);
                if (result != 1) return;

                processor.setCrashBaseUnityDb (
                    juce::jlimit (-40.0f, 40.0f,
                                  w->getTextEditorContents ("db").getFloatValue()));

                // Deliberately NO crashTab.setCrashGainPercent here.  The base
                // changes what the position is worth, never the position, and
                // pushing the (unchanged) percent back would be the round trip
                // that made this look like a fader control in the first place.
            }), false);
    };
    crashTab.onSaveSettings             = [this]
    { crashTab.setSaveResult (processor.saveCrashSettings()); };
    refreshCrashFromProcessor();
    processor.setCrashNoteMask    (crashTab.notesMask());   // tab default = source of truth
    processor.setCrashVelocity    (crashTab.crashVelocity());  // tab default = source of truth

    // ── Wire SoundsTab → engine via processor ─────────────────────────────────
    soundsTab.onActiveSoloSlotChanged = [this](int slot)
    {
        processor.setActiveSoloSlot(slot);
    };
    // The reference-voice pill substitutes a slot's instrument.  refId is an
    // instrument FLAG from the per-instrument library (leading "NNN" of
    // "NNN-Name.frb"); refId < 0 = none.  For a style slot this locks out the
    // style's program changes; for a solo slot it just selects the voice.
    soundsTab.onReferenceSelected = [this](bool isSolo, int slot, int refId)
    {
        if (isSolo)
        {
            if (refId >= 0) processor.setSoloPreset (slot, refId);
        }
        else
        {
            if (refId >= 0) processor.setSlotSubstitution (slot, refId);
            else            processor.clearSlotSubstitution (slot);
        }
    };
    soundsTab.onLoadBlobRequested = [this](const juce::File& f, const juce::String& code)
    {
        return processor.loadBlob(f, code);
    };
    soundsTab.onGetBlobPresetNames = [this]
    {
        // Per-flag sound library: return a FLAG-INDEXED list (index == GM flag,
        // 128 entries) so the editor's selected index maps 1:1 onto the flag
        // that setSoloPreset / selectChannelPreset expect.
        std::vector<juce::String> names;
        names.reserve (128);
        for (int flag = 0; flag < 128; ++flag)
        {
            juce::String entry = juce::String (flag).paddedLeft ('0', 3) + " - ";
            entry += processor.hasInstrumentFlag (flag)
                       ? processor.getInstrumentName (flag)
                       : juce::String ("(not installed)");
            names.push_back (entry);
        }
        return names;
    };
    soundsTab.onSoloPresetSelected = [this](int slot, int presetIndex)
    {
        processor.setSoloPreset(slot, presetIndex);
    };

    // Click library — file picker and runtime params route to the engine
    // via the correct channel index (style slot 0..15, solo slot 16..23).
    auto engineChannelFor = [](int slot, bool isSolo)
    {
        return isSolo ? (Betel::SamplePlayerEngine::kNumStyleChannels + slot) : slot;
    };
    soundsTab.onClickFileChosen = [this, engineChannelFor]
        (int slot, bool isSolo, const juce::File& f)
    {
        processor.loadClickSample(engineChannelFor(slot, isSolo), f);
    };

    // ── THE SAMPLED KIT GRID ─────────────────────────────────────────────────
    soundsTab.onGetFullKits = [this]() { return processor.getFullKitList(); };

    soundsTab.onGetFullKitOnSlot = [this, engineChannelFor] (int slot, bool isSolo)
    {
        return processor.getFullKitOnChannel (engineChannelFor (slot, isSolo));
    };

    soundsTab.onFullKitSelected =
        [this, engineChannelFor] (int slot, bool isSolo, int msb, int lsb, int pc)
    {
        // The USER picked it, so the DRUMS-vs-PERC rule that governs a STYLE's
        // request does not apply - an explicit choice is always honoured.
        processor.loadFullKitDirect (engineChannelFor (slot, isSolo), msb, lsb, pc);

        // AND IT HAS TO SURVIVE THE STYLE, exactly as a GM kit pick does.
        //
        // onApplyDrumKit has locked the slot against later style PCs since it
        // was written; picking a SAMPLED kit did not, so a mid-track drum PC
        // could compose over it - the sound reverted and, now that the grid
        // tracks the engine honestly, the cell went dark with it.  Same gate,
        // same reason: an explicit choice is not a style's request.
        if (! isSolo)
            processor.setSlotReferencePinned (false, slot, /*lock*/ 1);

        soundsTab.recommitSlot (slot, isSolo);
    };

    // ── USER SFZ, per slot ───────────────────────────────────────────────────
    soundsTab.onGetSfzState = [this, engineChannelFor] (int slot, bool isSolo)
    {
        const int ch = engineChannelFor (slot, isSolo);
        return std::make_pair (processor.getSfzName (ch), processor.isSfzActive (ch));
    };

    soundsTab.onSfzToggled = [this, engineChannelFor] (int slot, bool isSolo, bool on)
    {
        const int ch = engineChannelFor (slot, isSolo);

        // SLEEP, not clear.  The SFZ stays parked on the channel so the next
        // press is a pointer swap rather than a re-parse of the file and all
        // its samples - which is what makes A/B against the style's own sound
        // usable rather than a two-second wait each way.
        if (! on) { processor.sleepSfzOnChannel (ch); return; }

        if (processor.hasSfzVoice (ch))
        {
            processor.wakeSfzOnChannel (ch);
            soundsTab.recommitSlot (slot, isSolo);
            return;
        }

        // Nothing parked yet: fall back to the remembered path (a set restore
        // that has not loaded yet), else there is simply nothing to wake.
        const auto path = processor.getSfzPath (ch);
        if (path.isEmpty()) return;

        juce::String err, name;
        if (processor.loadSfzOnChannel (ch, juce::File (path), err, name))
            soundsTab.recommitSlot (slot, isSolo);
    };

    soundsTab.onSfzLoadRequested = [this, engineChannelFor] (int slot, bool isSolo)
    {
        const int ch = engineChannelFor (slot, isSolo);

        auto chooser = std::make_shared<juce::FileChooser> (
            "Load an SFZ instrument",
            processor.sfzFallbackFolder().isDirectory()
                ? processor.sfzFallbackFolder()
                : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.sfz");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this, ch, slot, isSolo, chooser] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == juce::File()) return;

                juce::String err, name;
                if (processor.loadSfzOnChannel (ch, f, err, name))
                {
                    // THE CHANNEL'S DSP HAS TO BE RE-ASSERTED.
                    //
                    // A normal load ends with commitSlotParamsToEngine; an SFZ
                    // load only publishes a voice.  Without this the SFZ plays
                    // through a channel whose filter, EQ, envelopes and FX were
                    // never configured - audible, but nothing in the editor
                    // touches it.
                    soundsTab.recommitSlot (slot, isSolo);
                    soundsTab.refreshSfzState();
                    soundsTab.refreshInstrDisplay();
                }
                else
                {
                    // The loader's message names the actual problem - missing
                    // samples, no regions, or the size in MB against the cap -
                    // so it is shown verbatim rather than reduced to "failed".
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "Could not load that SFZ", err);
                }
            });
    };

    soundsTab.onClickParamsChanged = [this, engineChannelFor]
        (int slot, bool isSolo, bool enabled, float vol, float decayMs)
    {
        const int ch = engineChannelFor(slot, isSolo);
        processor.setClickEnabled (ch, enabled);
        processor.setClickVolume  (ch, vol);
        processor.setClickDecayMs (ch, decayMs);
    };

    // Full SlotParams → engine push.  Fires on every editor knob move AND on
    // slot / mode change so the engine channel reflects the slot's current
    // SlotParams in real time.  Unit-converts (seconds → ms ADSR, 0..1 → Hz
    // log cutoff, mono sub-mode → three booleans) inside slotParamsToChannelParams.
    // Hidden octave bias (bass register rule) — not a user parameter, so it
    // rides its own path rather than SlotParams.  The OCTAVE slider still reads 0.
    soundsTab.onOctaveBiasChanged = [this, engineChannelFor]
        (int slot, bool isSolo, int octaves)
    {
        processor.setChannelOctaveBias (engineChannelFor(slot, isSolo), octaves);
    };

    soundsTab.onSlotParamsChanged = [this, engineChannelFor]
        (int slot, bool isSolo, const SlotParams& sp)
    {
        const int ch = engineChannelFor(slot, isSolo);
        processor.applyChannelParams (ch, slotParamsToChannelParams (sp));

        // THE TWO-HANDLE BAND FILTER, ON ITS OWN ROUTE.
        //
        // It used to ride inside the bulk push above, and the bulk push is also
        // what a program change performs — so a style swapping an instrument
        // mid-performance re-stamped the channel with a default-constructed
        // ChannelParams and threw the band back to 20 Hz / 20 kHz. The slider
        // still showed the set's values, which is why it looked like the filter
        // "came back" the moment you touched anything: touching it re-committed
        // what the UI had all along.
        //
        // The filter is now per-channel and only this line moves it. This
        // callback is the one path both writers share — an editor handle move,
        // and repushAllSlotParams during a set restore — so the set keeps
        // owning the value and nothing else can reach it.
        processor.setChannelBandFilterNorm (ch, sp.filterHpNorm, sp.filterLpNorm);

        // Allowed-notes window folds the STYLE slot's final pitch; StylePlayer
        // ignores solo / out-of-range channels, so this is safe to call always.
        processor.setChannelNoteRange (ch, sp.noteRangeOn, sp.noteRangeLo, sp.noteRangeHi);
    };

    // A preset file landed on a slot: push its ALLOWED NOTES window on its own,
    // without the bulk params write.  StylePlayer owns this window — the engine
    // channel knows nothing about it — so selectChannelPreset cannot carry it,
    // and before this the fold stayed on the PREVIOUS sound's setting until a
    // control was nudged.
    soundsTab.onNoteRangeAdopted = [this, engineChannelFor]
        (int slot, bool isSolo, const SlotParams& sp)
    {
        processor.setChannelNoteRange (engineChannelFor (slot, isSolo),
                                       sp.noteRangeOn, sp.noteRangeLo, sp.noteRangeHi);
    };

    // "SAVE AS DEFAULT" → write the slot's current instrument to
    // instruments_presets\NNN-Name.ins (source .frb + full SlotParams).
    //
    // SOLO slots and DRUM KITS only.  A melodic STYLE slot has no default file
    // any more: its params live in the set, which is per style rather than once
    // per instrument, and giving it a file back would put two owners on the same
    // values — exactly what cancelling .sins was for.  saveChannelAsDefault
    // refuses those, and we say so rather than letting the button look dead.
    soundsTab.onSaveAsDefault = [this, engineChannelFor]
        (int slot, bool isSolo, const SlotParams& sp)
    {
        const int ch = engineChannelFor (slot, isSolo);
        const auto written = processor.saveChannelAsDefault (ch, sp);

        // CONFIRM ONLY WHAT ACTUALLY HAPPENED.  saveChannelAsDefault answers
        // with the file it wrote, or an empty File when it wrote nothing - so
        // the green flash is driven by the write, not by the button press.  A
        // confirmation that appears when the save failed is worse than none: it
        // is the reason someone stops checking.
        if (written != juce::File())
            soundsTab.confirmSaved ("SETTINGS SAVED");

        if (written == juce::File()
            && Betel::SamplePlayerEngine::isStyleChannel (ch)
            && ! processor.isChannelDrum (ch))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon,
                "Style slots are saved with the set",
                "A style slot's sound belongs to the SET now, not to a preset "
                "file, not to a preset file - that is what makes it per style.\n\n"
                "Use FAST SAVE to keep it with this style.");
        }
    };

    // The GAIN slider's own route to the engine.  It bypasses the bulk params
    // push deliberately: a calibration must not be movable by re-pushing a slot
    // for some unrelated reason (see Channel::instrumentGain).
    // A scheduled preset reload landed: bring every slot that shows this sound
    // up to what the file now says, so two slots on the same instrument can
    // never display different numbers.
    processor.onPresetReloaded = [this, engineChannelFor] (int flag, bool isDrumKit)
    {
        juce::ignoreUnused (isDrumKit);
        for (int slot = 0; slot < 8; ++slot)
            for (bool isSolo : { false, true })
            {
                const int ch = engineChannelFor (slot, isSolo);
                if (processor.getChannelInstrumentFlag (ch) != flag) continue;

                SlotParams sp;
                if (processor.getPresetParamsForChannel (ch, sp))
                    soundsTab.adoptPresetParams (isSolo, slot, sp);
            }
    };

    soundsTab.onSweetenerChanged = [this, engineChannelFor]
        (int slot, bool isSolo, const SweetenerParams& sw)
    {
        processor.setChannelSweetener (engineChannelFor (slot, isSolo), sw);
    };

    // SWEETENER GR METERS — the read side of the same route.
    //
    // SweetenerFx has published four gain-reduction figures since the day it
    // shipped and nothing ever painted them, so every threshold, ratio and
    // depth in that block was being set with no feedback but the sound.  The
    // editors now draw four bars; this is where they get their numbers.
    //
    // A PULL, driven from timerCallback: SoundsTab knows which slot's editor is
    // open and returns early when neither is, so a closed editor costs one
    // branch per tick and no engine access at all.
    soundsTab.onQuerySweetenerGr = [this, engineChannelFor]
        (int slot, bool isSolo, float* gr4)
    {
        processor.getChannelSweetenerGr (engineChannelFor (slot, isSolo), gr4);
    };

    soundsTab.onSetIgnorePc = [this] (int slot, bool isSolo, bool ignore)
    {
        processor.setSlotIgnorePc (isSolo, slot, ignore);
    };

    soundsTab.onGetIgnorePc = [this] (int slot, bool isSolo) -> bool
    {
        return processor.isSlotIgnorePc (isSolo, slot);
    };

    soundsTab.onSetBaseUnity = [this, engineChannelFor]
        (int slot, bool isSolo, float baseUnityDb) -> bool
    {
        return processor.setInstrumentBaseUnity (engineChannelFor(slot, isSolo), baseUnityDb);
    };

    soundsTab.onGetBaseUnity = [this, engineChannelFor]
        (int slot, bool isSolo) -> float
    {
        return processor.getInstrumentBaseUnity (engineChannelFor(slot, isSolo));
    };

    soundsTab.onGetPresetParams = [this, engineChannelFor]
        (int slot, bool isSolo, SlotParams& out) -> bool
    {
        return processor.getPresetParamsForChannel (engineChannelFor(slot, isSolo), out);
    };

    soundsTab.onSlotGainChanged = [this, engineChannelFor]
        (int slot, bool isSolo, float gainPercent)
    {
        // THE BASE IS CARRIED THROUGH, not defaulted.
        //
        // The 2-argument form defaults baseUnityDb to 0, so this call used to
        // wipe the calibration a moment after the BASE UNITY dialog set it: type
        // -6 dB, nudge the GAIN slider, and the -6 was gone with nothing on
        // screen to say so.
        const int ch = engineChannelFor (slot, isSolo);
        processor.setChannelInstrumentGainPercent (ch, gainPercent,
                                                   processor.getChannelBaseUnityDb (ch));
    };

    // ── Drum-kit callbacks (Phase 3) ──────────────────────────────────────────
    // Slot DRUMS-mode transition → flip the engine's drum-channel flag for
    // this slot.  After this fires, the engine treats subsequent PCs and
    // noteOns on this channel as drum-mode events (per-key region matching,
    // per-key envelope overrides, no portamento).
    soundsTab.onDrumModeChanged = [this, engineChannelFor]
        (int slot, bool isSolo, bool isDrum)
    {
        processor.setDrumChannel (engineChannelFor(slot, isSolo), isDrum);
    };

    // Hand the host-owned DrumKitRegistry pointer down so the DRUMS instrument
    // row and DrumsPopup can enumerate kits + per-role elements.  Always
    // returns non-null — the registry lives inside BetelgeuseProcessor.
    soundsTab.onGetDrumKitRegistry = [this]() -> Betel::DrumKitRegistry*
    {
        return processor.getDrumKitRegistry();
    };

    // Lets the drum selector read instruments_presets\NNN-*.ins so picking a kit
    // honours a saved "Save as Default" drum preset (matches the PC path).
    soundsTab.onGetInstrumentPresetsFolder = [this]() -> juce::File
    {
        return processor.getInstrumentPresetsFolder();
    };

    // Full DrumKitParams reload onto a channel — fired by the DRUMS cell
    // press and by DrumsPopup's "LOAD KIT" button and per-key element swap.
    soundsTab.onApplyDrumKit = [this, engineChannelFor]
        (int slot, bool isSolo, const DrumKitParams& dk)
    {
        processor.applyDrumKitToChannel (engineChannelFor(slot, isSolo), dk);

        // Style drum / perc slots: lock the slot against subsequent style
        // PCs so the user's drum-kit pick survives mid-track drum PC events
        // (msb 127) the same way melodic substitutions survive theirs.  The
        // dispatcher's slotPcLocked check sits BEFORE the msb==127 branch,
        // so this gate catches both kit-PCs and any program change for the
        // drum slot.  Solo slots never receive style PCs — no lock needed.
        if (! isSolo)
            processor.setSlotReferencePinned (false, slot, /*lock*/ 1);
    };

    // Per-key live edit (slider drag in DrumsPopup).  RT-safe atomic write
    // inside the engine; no sample reload.
    soundsTab.onDrumKeyParamsChanged = [this, engineChannelFor]
        (int slot, bool isSolo, int midiKey, const DrumElementParams& p)
    {
        processor.setDrumKeyParams (engineChannelFor(slot, isSolo), midiKey, p);
    };

    // Kit-wide FX bus knob movement (right-panel sliders in DrumsPopup).
    // RT-safe — pushes the whole DrumKitFxParams struct as atomic stores.
    soundsTab.onKitFxChanged = [this, engineChannelFor]
        (int slot, bool isSolo, const DrumKitFxParams& fx)
    {
        processor.applyDrumKitFx (engineChannelFor(slot, isSolo), fx);
    };

    // Right-click on a key in the DrumsPopup keyboard — audition that sound.
    soundsTab.onAuditionDrumKey = [this, engineChannelFor]
        (int slot, bool isSolo, int midiKey)
    {
        processor.auditionDrumKey (engineChannelFor(slot, isSolo), midiKey);
    };

    // ── Instrument-editor piano strip ────────────────────────────────────────
    // Play the edited slot's engine channel, and report back which notes are
    // sounding on it so the strip can light them up (style notes on a style
    // slot, incoming MIDI on a solo slot).
    soundsTab.onEditorNoteOn = [this, engineChannelFor]
        (int slot, bool isSolo, int note, int velocity)
    {
        processor.editorNoteOn (engineChannelFor(slot, isSolo), note, velocity);
    };

    soundsTab.onEditorNoteOff = [this, engineChannelFor]
        (int slot, bool isSolo, int note)
    {
        processor.editorNoteOff (engineChannelFor(slot, isSolo), note);
    };

    soundsTab.onQuerySoundingNotes = [this, engineChannelFor]
        (int slot, bool isSolo, uint32_t* mask)
    {
        if (mask == nullptr) return;
        processor.getSoundingNotes (engineChannelFor(slot, isSolo), mask);
    };

    // ── MixerTab ↔ engine ─────────────────────────────────────────────────────
    mixerTab.onStyleChannelGainChanged = [this](int slot, float linearGain)
    {
        processor.setChannelVolume (slot, linearGain);
    };
    // Live reflection: the mixer polls this so a style's per-section CC 7 ride
    // (written on the audio thread) moves the visible fader.
    mixerTab.styleGainProvider = [this](int slot)
    {
        return processor.getChannelVolume (slot);
    };
    mixerTab.onSoloChannelGainChanged = [this](int slot, float linearGain)
    {
        processor.setChannelVolume (Betel::SamplePlayerEngine::kNumStyleChannels + slot,
                                    linearGain);
    };
    mixerTab.onMasterGainChanged = [this](float linearGain)
    {
        processor.setMasterVolume (linearGain);
    };
    // MASTER boost tickboxes (0/+3/+6/+9/+12 dB) — applied on top of the master
    // fader inside the processor, so the master-volume CC honours it too.
    mixerTab.onMasterBoostChanged = [this](float boostDb)
    {
        processor.setMasterBoostDb (boostDb);
    };
    // STYLE VOLUME / SOLO VOLUME bus faders (migrated from Settings).
    mixerTab.onStyleBusGainChanged = [this](float linearGain)
    {
        processor.setStyleVolume (linearGain);
    };
    mixerTab.onSoloBusGainChanged = [this](float linearGain)
    {
        processor.setRightHandVolume (linearGain);
    };

    // SOLO BASE UNITY — double-click the SOLO VOLUME fader.
    //
    // Why the right hand needs one at all: the STYLE bus carries BOOST (+19.2 dB
    // at its default) and MAKEUP, and the SOLO bus carries neither.  The two
    // unity detents therefore mean two different loudnesses, and every set was
    // paying for that with fader travel — which is exactly the offset a base
    // unity exists to absorb.
    //
    // GLOBAL, never in a set: it describes this install's balance between the
    // two hands and its sample library, not a property of a song.  MasterSettings
    // writes grex_master.xml the instant it changes, and the startup push above
    // applies it, so it is in force before the first set loads.
    //
    // The fader does NOT move — same rule as the crash box.  The base changes
    // what the position is worth, never the position.
    mixerTab.onSoloBaseUnityRequested = [this]
    {
        const float base = processor.getSoloBaseUnityDb();

        auto* w = new juce::AlertWindow (
            "SOLO BASE UNITY",
            "What the SOLO VOLUME fader's unity detent (127) is worth, in dB.\n"
            "Use it to match the right hand to the band once, for this install.\n"
            "Saved globally, not with the set. Range: -24 to +24 dB.\n"
            "0 dB is the level Grex has always used.",
            juce::MessageBoxIconType::NoIcon);

        w->addTextEditor ("db", juce::String (base, 2), "dB:");
        w->addButton ("SET",    1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, w] (int result)
            {
                std::unique_ptr<juce::AlertWindow> owned (w);
                if (result != 1) return;

                processor.setSoloBaseUnityDb (
                    juce::jlimit (Betel::MasterSettings::kMinSoloBaseUnityDb,
                                  Betel::MasterSettings::kMaxSoloBaseUnityDb,
                                  w->getTextEditorContents ("db").getFloatValue()));

                // Deliberately no mixerTab.setSoloBusGain here: the fader is
                // where the user left it and the base does not move it.
            }), false);
    };

    // ── FINISHER (master-bus chain) ──────────────────────────────────────────
    mixerTab.onFinisherEnabledChanged = [this](bool on)
    {
        processor.setFinisherEnabled (on);
        if (finisherWindow) finisherWindow->setEnabled (on);
    };
    mixerTab.onFinisherEditRequested = [this] { openFinisherWindow(); };
    mixerTab.setFinisherEnabled (processor.getFinisherEnabled());

    // When the user picks an instrument in SoundsTab, push the new name to
    // the matching mixer fader so the label-at-top always reflects what's
    // actually loaded on that slot.
    soundsTab.onInstrumentSelected = [this, engineChannelFor](bool isSolo, int slot, int patch)
    {
        const juce::String name = SoundsTab::instrumentNameForPatch (patch);
        if (isSolo) mixerTab.setSoloSlotLabel  (slot, name);
        else        mixerTab.setStyleSlotLabel (slot, name);

        // Make the selection AUDIBLE, not just visible.
        //
        // Solo slots receive no style PCs, so a direct selectChannelPreset
        // (via processor.setSoloPreset) is enough — anything that happens
        // later (reference-pill override, set restore) re-applies on top.
        //
        // Style melodic slots (BASS / CHORD / PAD / LEAD on engine ch 2..7;
        // drum + perc on ch 0/1 take the onApplyDrumKit path instead and
        // never reach here) are different: a plain selectChannelPreset would
        // get overwritten the moment the style emits its next program change
        // for that channel.  Route through processor.setSlotSubstitution so
        // the slot is BOTH loaded AND locked against subsequent style PCs,
        // and the choice is recorded in slotSubFlag so applyVoiceSetup's
        // re-assertion loop replays it on the next style load.  This is the
        // same path the reference-voice pills use; selectors now share it.
        if (patch < 0) return;
        if (isSolo) processor.setSoloPreset       (slot, patch);
        else        processor.setSlotSubstitution (slot, patch);

        //======================================================================
        // APPLY THE NEW SOUND'S SAVED .ins IMMEDIATELY.
        //
        // Loading the instrument and adopting its saved settings were two
        // separate things, and only the first happened here.  The second waited
        // for the editor window to open, because pushSlotIntoEditor was the one
        // place that read the preset - so SAVE SETTINGS appeared to do nothing
        // until you pressed EDIT, and the sound played on the PREVIOUS
        // instrument's parameters in the meantime.
        //
        // The machinery was already here and already correct: onPresetReloaded
        // does exactly this after a save, through the same two calls. It was
        // simply never reached on a plain selection.
        //
        // getPresetParamsForChannel answers false when the instrument has no
        // .ins, and then nothing is adopted - which is right. An instrument
        // without saved settings must not silently inherit the last one's.
        //======================================================================
        const int ch = engineChannelFor (slot, isSolo);

        SlotParams sp;
        if (processor.getPresetParamsForChannel (ch, sp))
            soundsTab.adoptPresetParams (isSolo, slot, sp);
    };

    // (onReferenceSelected is wired above to the per-instrument substitution
    //  routing -- processor.setSlotSubstitution / setSoloPreset.)

    // ── First-open seed vs. reopen restore ────────────────────────────────────
    // The editor is recreated every time the host opens the plugin window, but
    // the processor persists.  On the FIRST open we seed the engine from the
    // editor defaults + the user's default.bset; on every later open we instead
    // restore the snapshot captured when the window last closed, so reopening
    // the window neither reverts state nor re-cues the running sequencer.
    if (! processor.isEditorInitialized())
    {
        // Push the initial active-solo selection (slot 0) so the very first MIDI
        // events route to a valid channel before the user touches the UI.
        processor.setActiveSoloSlot(0);

        // Commit every slot's editor params onto the engine once (the editor
        // seeds its GoldSliders with notify == false, so without this a fresh
        // instance would run on the engine's own defaults).
        soundsTab.commitAllSlotParamsToEngine();

        // Canonical startup default: a user-saved "default.bset" restores AND
        // commits every per-slot + global setting and loads its style.
        {
            const auto defaultSet = processor.getFolderManager()
                                        .getSetsFolder().getChildFile ("default.bset");
            if (defaultSet.existsAsFile())
                applySetFromFile (defaultSet);
        }

        // Sound-calibration baseline (harmless re-assert if default.bset already
        // applied it) so it is live without any control being moved.
        soundsTab.applySoundCalibration();

        processor.markEditorInitialized();
    }
    else if (processor.getEditorSnapshot().isValid())
    {
        // Reopen: restore the live snapshot.  applyGlobalState's same-style guard
        // skips the style reload (the snapshot names the already-loaded style),
        // so the sequencer keeps playing from where it was.
        applySetPayload (processor.getEditorSnapshot());
    }

    // GUARANTEE a playable style — for EVERY open, not just the first.
    // This used to be an immediate selectFirstStyle() reachable only when no
    // default.bset existed, so it did nothing at all when a default.bset was
    // present but named a style that had since moved, and nothing when the
    // <root>/styles scan hadn't populated the browser yet: the transport came
    // up with nothing to play.  Scheduling it re-checks once the browser is
    // settled, and onNeedsStyle vetoes the arm if a style is live by then — so
    // a good default.bset, or a reopen that kept its style, is never overridden.
    // THE ROOT IS NO LONGER SET HERE.
    //
    // GrexPaths::setRoot now runs inside BetelgeuseProcessor's constructor, by
    // way of rescanFromFolderManager() - the one function that runs both at
    // construction and on every folder relocation.  Setting it here meant a
    // session that never opened the window had no root at all, which is how an
    // offline bounce ended up unregistered and running on default master
    // settings.  See the comment block in that constructor.

    // Crash settings — re-read AFTER the tab pushed its defaults above, so the
    // file wins on an established install and the defaults stand on a fresh
    // one.  The processor already read them at construction; this second read
    // is what puts them on SCREEN, and it is a no-op when the file is absent.
    if (processor.loadCrashSettings())
    {
        refreshCrashFromProcessor();
    }

    // ── MASTER SETTINGS ──────────────────────────────────────────────────────
    //
    // Read BEFORE the panel is seeded and pushed to the processor immediately,
    // so the engine and the button can never disagree about which chord mode is
    // running.  That disagreement is exactly the failure this file exists to
    // stop: playing Fingered while the panel reads 1 FINGER makes every melodic
    // part snap onto plain triad tones, and nothing on screen looks wrong.
    {
        // READ-ONLY HERE.  The processor's constructor already ran
        // checkRegistration(), MasterSettings::load() and the three pushes
        // below - it has to, because a session with no window still plays.
        //
        // What is left for the editor is the half the processor cannot do:
        // putting those values on SCREEN.  The pushes are repeated because they
        // are idempotent and cost two atomic stores, and because that removes
        // any chance of the widget and the engine disagreeing if the file is
        // ever re-read between the two constructors.
        auto& ms = Betel::MasterSettings::get();

        processor.setSoloPitchBendRange (ms.getPitchBendRange());
        processor.pushSoloLowVelBoost();

        // The split the player last set, not the built-in default.  Straight to
        // the tracker so seeding does not write the value it just read back.
        processor.setSplitPointFromProject (ms.getSplitPoint());
        processor.applyStoredSoloBaseUnity();

        processor.getChordTracker().setChordMode (
            ms.isFingeredChord() ? Betel::ChordZoneTracker::ChordMode::Fingered
                                 : Betel::ChordZoneTracker::ChordMode::SingleFinger);
        processor.setTempoSynced (ms.isTempoSynced());

        mainTab.getBtnFingered().setToggleState (ms.isFingeredChord(),
                                                 juce::dontSendNotification);
        selectorTempoType.setSelectedIndex (ms.isTempoSynced() ? 1 : 0, false);
    }

    // Macro presets and the stars are ALSO loaded by the processor now, for the
    // same reason.  Re-read here so a window opened after the user relocated
    // the library shows the new root's files rather than the old root's.
    Betel::GlobalMacros::get().loadFunkey();
    Betel::GlobalMacros::get().loadBigDrums();
    Betel::StyleFavorites::get().load();
    // Style levels start at their hard-coded defaults.  There is no template
    // file any more: three values that every set carries do not need a fourth
    // place to live.
    refreshMacroButtons();

    stylesTab.scheduleAutoArmFirstStyle();

    // Master boost: make the current boost (default +6 dB) LIVE on the engine
    // and light the matching mixer tickbox — for both first-open and reopen.
    // Master volume isn't persisted per-set, so the boost defaults to +6 each
    // run; the processor holds the live value so the master-volume CC honours it
    // too.  setMasterBoostDb(getMasterBoostDb()) is idempotent on reopen (the
    // engine already carries the boost because the processor persisted it).
    processor.setMasterBoostDb (processor.getMasterBoostDb());
    mixerTab.setMasterBoostDb  (processor.getMasterBoostDb());

    // Build the reference-voice pill library from the engine's per-instrument
    // flag library (the processor scanned <root>/sounds in its constructor).
    refreshReferenceLibrary();

    // Jumps tab -> sequencer: push the full config on any user click, and once
    // now so the sequencer starts from what the tab shows.
    jumpsTab.onJumpChanged = [this](Betel::StyleSection, Betel::StyleSection)
    {
        processor.setJumpsConfig (jumpsTab.getConfig());
    };
    processor.setJumpsConfig (jumpsTab.getConfig());

    // ── Right panel: Virtual MIDI keyboard ────────────────────────────────────
    keyboardComponent.setAvailableRange(36, 96);
    keyboardComponent.setOctaveForMiddleC(4);
    addAndMakeVisible(keyboardComponent);

    // ── Right panel: Split knob ───────────────────────────────────────────────
    knobSplit.onChange = [this](double val)
    {
        keyboardComponent.setSplitPoint((int)val);
        processor.setSplitPoint((int)val);
    };
    keyboardComponent.setSplitPoint((int)knobSplit.getValue());
    processor.setSplitPoint((int)knobSplit.getValue());
    addAndMakeVisible(knobSplit);

    addAndMakeVisible(btnHold);
    btnHold.onClick = [this]
    {
        processor.getSequencer().setTransitionHold (btnHold.getToggleState());
    };
    addAndMakeVisible(ledRegistration);
    // GREEN once the licence file has been read and its serial verified.
    //
    // The LED was built, added and given bounds, and setRegistered was never
    // called from anywhere - so it sat red on a registered machine and said
    // nothing.  The folder LED beside it was wired; this one was missed.
    //
    // Driven here for the state at startup and refreshed on the timer below,
    // so registering from the SETTINGS tab turns it green without any extra
    // plumbing between the two pages.
    ledRegistration.setRegistered (RegistrationManager::getInstance().isRegistered());

    lblReg.setText ("Reg", juce::dontSendNotification);
    lblReg.setFont (juce::Font (14.0f, juce::Font::italic));
    lblReg.setColour (juce::Label::textColourId, juce::Colour (0xFFCCCCCC));
    lblReg.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible(lblReg);
    addAndMakeVisible(ledFolderLocator);
    ledFolderLocator.setInterceptsMouseClicks (false, false);  // indicator only

    btnLocateFolder.setButtonText ("LOCATE SYSTEM FOLDER");
    addAndMakeVisible(btnLocateFolder);

    // ── Folder locator LED: GREEN when the root folder resolves, BLINKING
    //    RED when it doesn't.  Clicking it opens a folder chooser; on a
    //    successful pick, processor.rescanFromFolderManager() reloads kits +
    //    styles.  Nothing re-targets a sets folder any more — the SETS browser
    //    is gone, and a set now travels with the style that loads it.
    {
        auto& mgr = processor.getFolderManager();
        ledFolderLocator.setFolderFound (mgr.isRootFolderValid());

        // Manager fires this AFTER it has persisted the new root.
        mgr.onRootFolderChanged = [this]
        {
            auto& m = processor.getFolderManager();
            processor.rescanFromFolderManager();
            rescanStyleBrowser();
            refreshReferenceLibrary();
            ledFolderLocator.setFolderFound (m.isRootFolderValid());
        };

        btnLocateFolder.onClick = [this]
        {
            auto& m = processor.getFolderManager();
            const auto start = m.isRootFolderValid()
                                  ? m.getRootFolder()
                                  : juce::File::getSpecialLocation (
                                        juce::File::userDocumentsDirectory);

            // FileChooser must out-live the async callback — keep it on the
            // heap, captured by the lambda, then deleted inside it.
            auto* chooser = new juce::FileChooser (
                "Locate Betelgeuse folder",
                start);

            chooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectDirectories,
                [this, chooser] (const juce::FileChooser& fc)
                {
                    const auto folder = fc.getResult();
                    if (folder.isDirectory())
                        processor.getFolderManager().setRootFolder (folder);
                    delete chooser;
                });
        };
    }


    setSize(kDesignW, kDesignH);
}

void MainComponent::selectTab(int index)
{
    index = juce::jlimit(0, kNumTabs - 1, index);
    activeTabIndex = index;

    for (int i = 0; i < kNumTabs; ++i)
    {
        tabButtons[i].setActive(i == index);
        tabPages[i]->setVisible(i == index);
    }

    // The global style-search bar belongs to the Styles tab only.
    const bool onStyles = (index == kTabNames.indexOf ("STYLES"));
    styleSearchBox.setVisible (onStyles);
    btnStyleSearch.setVisible (onStyles);
    btnStyleSearchClear.setVisible (onStyles);
}

float MainComponent::getUiScale() const
{
    const float sx = (float)getWidth()  / (float)kDesignW;
    const float sy = (float)getHeight() / (float)kDesignH;
    return juce::jmin(sx, sy);
}

//==============================================================================
// THE SET THAT BELONGS TO A STYLE.
//
//   <root>/styles/Ballad/Analog Ballad.prs   ->   <root>/sets/Ballad/Analog Ballad.bset
//
// The sets tree mirrors the styles tree exactly, so the mapping is the parent
// folder name plus the file's base name — no index, no manifest, nothing to
// keep in step.  Add a style and drop a set beside it and it works; rename
// either and the pair simply stops matching, which is the failure you want
// (it falls back to a plain style load) rather than loading the wrong set.
//==============================================================================
//==============================================================================
//  Grey out the variation buttons this style cannot play.
//
//  A style is under no obligation to carry all fifteen sections.  Two intros
//  instead of three is ordinary; plenty have no break, or one ending.  Those
//  buttons used to look exactly like the working ones and simply did nothing
//  when pressed - performVariation returns early - which reads as a broken
//  plugin rather than as a style that stops at INTRO 2.
//
//  INTRO 4 is permanently unavailable and always was: the 16-button grid is a
//  4x4 panel and there are only three intros to fill it, so index 3 has never
//  had a backing section.  It has simply never looked like it.
//==============================================================================
void MainComponent::refreshVariationAvailability()
{
    using S = Betel::StyleSection;

    const Betel::StyleData* style = processor.getLoadedStyle();

    if (style == nullptr)
    {
        // Nothing loaded: everything back on except the one that never exists,
        // so the panel is not left greyed out between styles.
        mainTab.setAllVariationsAvailable();
        mainTab.setVariationAvailable (3, false);
        return;
    }

    // Mirrors performVariation's indexing exactly.  If that mapping ever moves,
    // this has to move with it - which is why the two comments say the same
    // thing rather than one pointing at the other.
    static const S kSectionForButton[16] =
    {
        S::IntroA, S::IntroB, S::IntroC, S::Count,      // 0-3   INTRO 1-4 (4 has none)
        S::MainA,  S::MainB,  S::MainC,  S::MainD,      // 4-7   VAR 1-4
        S::FillAA, S::FillBB, S::FillCC, S::FillDD,     // 8-11  FILL 1-4
        S::FillBA,                                      // 12    BRAKE
        S::EndingA, S::EndingB, S::EndingC              // 13-15 END 1-3
    };

    for (int i = 0; i < 16; ++i)
    {
        const S sec = kSectionForButton[i];

        const bool ok = (sec != S::Count)
                        && style->getSection (sec).present
                        && style->getSection (sec).endTick > style->getSection (sec).startTick;

        mainTab.setVariationAvailable (i, ok);
    }
}

juce::File MainComponent::setFileForStyle (const juce::String& styleAbsolutePath) const
{
    if (styleAbsolutePath.isEmpty()) return {};

    auto& m = processor.getFolderManager();
    if (! m.isRootFolderValid()) return {};

    const juce::File style (styleAbsolutePath);
    const auto genre = style.getParentDirectory().getFileName();
    if (genre.isEmpty()) return {};

    return m.getSetsFolder()
            .getChildFile (genre)
        // RAW stem, not Betel::displayNameFor — see SetBaker: the set FILE
        // keeps the model code so two styles cannot collide on one .bset.
            .getChildFile (style.getFileNameWithoutExtension() + ".bset");
}

//==============================================================================
// WHAT THE LEFT PANEL CALLS THE THING THAT IS PLAYING.
//
// The set if there is one, the style FILE otherwise — never the name embedded
// in the style, which is the original machine-generated string the whole rename
// pass existed to stop showing people.
//==============================================================================
juce::String MainComponent::currentSetDisplayName() const
{
    if (lastSetFile.existsAsFile())
        return Betel::displayNameFor (lastSetFile);

    const auto stylePath = processor.getLastLoadedStylePath();
    if (stylePath.isNotEmpty())
        return Betel::displayNameFor (juce::File (stylePath));

    return "No Style";
}

//==============================================================================
// CRASH UI <- PROCESSOR.  Every setter here is non-notifying, so seeding the
// panel cannot loop a value back into the processor it just came from.
//==============================================================================
void MainComponent::refreshCrashFromProcessor()
{
    crashTab.setCrashOnTransition (processor.getCrashOnTransition());
    crashTab.setAutoCrashEnabled  (processor.getAutoCrashEnabled());
    crashTab.setAutoCrashEveryN   (processor.getAutoCrashEveryN());
    crashTab.setCrashNotesMask    (processor.getCrashNoteMask());
    crashTab.setCrashVelocity     (processor.getCrashVelocity());
    crashTab.setCrashGainPercent  (processor.getCrashGainPercent());
}

//==============================================================================
// MIXER UI <- PROCESSOR.  Called after a set restores the mixer, so the panel
// shows what the engine is actually running.  Every setter here is the
// non-notifying kind, so nothing loops back into the processor.
//==============================================================================
void MainComponent::refreshMixerFromProcessor()
{
    for (int s = 0; s < 8; ++s)
    {
        mixerTab.setStyleSlotGain (s, processor.getChannelVolume (s));
        mixerTab.setSoloSlotGain  (s, processor.getChannelVolume (
                                        Betel::SamplePlayerEngine::kNumStyleChannels + s));
    }

    mixerTab.setStyleBusGain  (processor.getStyleVolume());
    mixerTab.setSoloBusGain   (processor.getRightHandVolume());
    mixerTab.setMasterGain    (processor.getMasterVolume());
    mixerTab.setMasterBoostDb (processor.getMasterBoostDb());
    mixerTab.setFinisherEnabled (processor.getFinisherEnabled());

    if (finisherWindow != nullptr)
        finisherWindow->setEnabled (processor.getFinisherEnabled());

    // Also here, not only after a browser load: reopening a DAW project restores
    // the processor's style without anything passing through onStyleSelected,
    // so this is the only place a restored style's missing sections get greyed.
    refreshVariationAvailability();
}

//==============================================================================
// THE ONE SET WRITER.  Both SAVE SET and FAST SAVE come here, so the two can
// never save different things — which they used to: the dialog path built its
// own smaller payload and dropped the macro state and the browser selection.
//==============================================================================
bool MainComponent::writeSetToFile (const juce::File& file)
{
    if (file == juce::File()) return false;

    const auto payload = buildSetSnapshot();
    auto xml = payload.createXml();
    if (xml == nullptr) return false;

    file.getParentDirectory().createDirectory();
    if (! xml->writeTo (file)) return false;

    lastSetFile = file;
    return true;
}

void MainComponent::applySetFromFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr) return;

    const auto payload = juce::ValueTree::fromXml (*xml);
    if (! payload.isValid()) return;

    applySetPayload (payload);
    lastSetFile = file;
}

// Apply a full set snapshot (SoundsState + GlobalState + JumpsState + UiState)
// from an in-memory tree.  Shared by applySetFromFile and the editor-reopen
// path, which restores the snapshot captured when the window last closed so a
// close/reopen leaves the running plugin untouched.
void MainComponent::applySetPayload (const juce::ValueTree& payload)
{
    if (! payload.isValid()) return;

    // STYLE LEVELS FIRST, and deliberately so: GlobalState below reloads the
    // style, and the whole load-time gain chain — makeup, drum balance, rhythm
    // ceiling, every CC 7 decision — is computed against whatever the levels say
    // at that moment.  Restoring them afterwards would leave the style voiced
    // with the PREVIOUS style's settings until something forced a reload.
    //
    // An absent block is a set saved before this existed: fromTree falls back to
    // the default template, which is what such a set always effectively had.
    Betel::StyleLevels::get().fromTree (
        payload.getChildWithName (Betel::StyleLevels::kTreeType));
    setEditorTab.refreshFromState();

    // PER-KIT BASE UNITY, AND EARLY FOR THE SAME REASON AS THE LEVELS.
    //
    // GlobalState below reloads the style, and that load is what publishes the
    // kits - applyDrumPresetGain and the sampled-kit path both read this map as
    // they go.  Restoring it afterwards would leave every kit on the previous
    // set's calibration until something forced a reload.
    processor.applyKitUnityState (payload.getChildWithName ("KitUnity"));

    // IGNORE PROGRAM CHANGE, and early for a sharper reason than the levels:
    // GlobalState below reloads the style, and applyVoiceSetup consults these
    // toggles as it decides which slots it may re-voice.  Restored afterwards,
    // the load would have already overwritten the very instruments the toggle
    // exists to protect - and SoundsState further down would then put the
    // user's choice back with no toggle behind it, so the NEXT style change
    // would take it again.
    processor.applyIgnorePcState (payload.getChildWithName ("IgnorePc"));
    soundsTab.refreshIgnorePcButton();

    // SoundsState is applied AFTER the style load, further down.  It used to run
    // HERE, before it — and the load then re-voiced all eight style slots on top
    // of everything it had just restored, which is why saved slot settings kept
    // coming back as the style's own.  The set has to be the last word.
    if (auto global = payload.getChildWithName ("GlobalState"); global.isValid())
    {
        processor.applyGlobalState (global);
    }
    else if (auto link = payload.getChildWithName ("StyleLink"); link.isValid())
    {
        // A FACTORY SET.  It carries no GlobalState on purpose: that block
        // restores transpose, tempo, mutes, chord mode and the solo scale
        // tunings, and a shipped set has no business overwriting the player's
        // session with any of that.  All it claims is "I belong to this style".
        //
        // The link is RELATIVE to <root>/styles, so the pack survives the
        // library being installed anywhere — an absolute path baked at build
        // time would break on every machine but the one that built it.
        const auto rel = link.getProperty ("relPath", juce::String()).toString();
        auto& m = processor.getFolderManager();
        if (rel.isNotEmpty() && m.isRootFolderValid())
        {
            const auto f = m.getStylesFolder().getChildFile (rel);
            if (f.existsAsFile() && f.getFullPathName() != processor.getLastLoadedStylePath())
            {
                juce::String err;
                if (processor.loadStyle (f, err))
                {
                    stylesTab.setSelectedStylePath (f.getFullPathName());
                    soundsTab.applySoundCalibration();
                }
            }
        }
    }

    // Jumps destinations -> tab AND sequencer (setConfig doesn't fire the
    // tab's callback, so push to the processor explicitly).
    if (auto j = payload.getChildWithName ("JumpsState"); j.isValid())
    {
        auto jc = jumpsTab.getConfig();
        for (int i = 0; i < Betel::kNumJumpSources; ++i)
            jc.destinations[(size_t) i] = (Betel::StyleSection)
                (int) j.getProperty ("d" + juce::String (i),
                                     (int) jc.destinations[(size_t) i]);
        jumpsTab.setConfig (jc);
        processor.setJumpsConfig (jc);
    }

    // ── SOUNDS, AFTER THE STYLE LOAD ─────────────────────────────────────────
    //
    // Every slot the set carried — which instrument is on it, its full params,
    // its drum rack, its reference voice — restored and pushed to the engine
    // ON LOAD, with nothing left needing a nudge to take effect.
    //
    // It has to be here rather than before the load: applyVoiceSetup voices all
    // eight style slots during the load, so anything restored earlier is simply
    // overwritten by the style a moment later.
    if (auto sounds = payload.getChildWithName ("SoundsState"); sounds.isValid())
        soundsTab.applyState (sounds);

    // ── IGNORE PROGRAM CHANGE: put the fixed sounds BACK ──────────────────────
    //
    // The toggle itself was restored before the load, and that half only stops
    // the load from re-voicing the slot.  It cannot put back a sound the engine
    // is not currently holding, which is every case where the set is opened into
    // a session holding something else - the slot came up protected and wrong.
    //
    // Here, because it has to be after BOTH the style load and SoundsState: the
    // load would overwrite it, and SoundsState carries the slot's chain, which
    // should settle before the instrument it describes is published.
    processor.restoreIgnorePcSlots();

    // ── AND THE SET GETS THE LAST WORD ───────────────────────────────────────
    //
    // Everything above that publishes an instrument goes through
    // selectChannelPreset, which resets the channel to default ChannelParams
    // for any sound with no saved .ins — including restoreIgnorePcSlots one
    // line up.  So the block that runs LAST wins, and until now that was never
    // SoundsState.  The two-handle band filter was the visible casualty: loaded
    // into the tab, correct on the slider, fully open in the engine until the
    // user touched the channel selector and the editor re-committed it.
    //
    // Params only — see repushAllSlotParams.  Re-selecting instruments here
    // would trip the same reset again.
    soundsTab.repushAllSlotParams();

    // ── USER SFZ ─────────────────────────────────────────────────────────────
    //
    // AFTER the style load, deliberately.  Applied before it, the style's own
    // program changes would be gated away by the override and the slot would
    // never hold the sound the style asked for - so switching the SFZ off later
    // would drop to silence instead of falling back to the style's instrument.
    // Applied here, the slot is voiced by the style first and the SFZ takes it
    // afterwards, which is exactly the state clearSfzOnChannel expects to undo.
    processor.applySfzState (payload.getChildWithName ("SfzState"));

    // AFTER SfzState, and that ordering matters: applySfzParams commits a chain
    // only to a slot whose SFZ is AWAKE, so it has to run once the set has said
    // which slots those are.
    soundsTab.applySfzParams (payload.getChildWithName ("SfzParams"));

    // ── CRASH ────────────────────────────────────────────────────────────────
    //
    // The whole tab, restored and pushed to the panel in one move so the
    // controls read what the engine is actually doing from the moment the set
    // lands — nothing needs touching to take effect.
    processor.applyCrashState (payload.getChildWithName ("CrashState"));
    refreshCrashFromProcessor();

    // ── MIXER, LAST ──────────────────────────────────────────────────────────
    //
    // AFTER the style load above, and that ordering is the whole point:
    // applyVoiceSetup writes every style channel fader from the style's own
    // CC 7, so a mixer restored any earlier would be overwritten a moment later
    // by the load it was supposed to override.  Restored here, the set's fader
    // positions are the last word — which is what IGNORE STYLE VOLUMES needs to
    // mean anything.
    processor.applyMixerState (payload.getChildWithName ("MixerState"));
    refreshMixerFromProcessor();

    const auto uiState = payload.getChildWithName ("UiState");

    // OUTSIDE the validity check, deliberately.  A set with no UiState child
    // used to skip this whole block and leave the macros lit from the previous
    // style; now an absent tree simply answers "off" for both.
    applyMacroEngagedState (uiState);

    if (uiState.isValid())
    {
        selectorSingleMulti.setSelectedIndex (
            (int) uiState.getProperty ("singleMulti", selectorSingleMulti.getSelectedIndex()), true);

        // Re-select the style in the browser WITHOUT reloading it: the style
        // itself is restored by GlobalState above (and on an editor reopen it
        // never left), so this only puts the genre, page, cell highlight and
        // header pill back where they were.
        if (uiState.hasProperty ("styleName"))
            stylesTab.restoreSelection ((int) uiState.getProperty ("stylesGenre", 0),
                                        uiState.getProperty ("styleName").toString());
    }

    // ── Re-sync the visible controls to the restored processor state ──────
    // Knobs / selectors notify so engine + UI converge through one code path
    // (their handlers are idempotent); MainTab toggles are display-only here.
    knobSplit          .setValue ((double) processor.getSplitPoint(),      true);
    knobGlobalSemitone .setValue ((double) processor.getGlobalTranspose(), true);
    knobTempo          .setValue ((double) processor.getManualBPM(),       true);
    // The two MASTER settings are re-seeded from MasterSettings, not from the
    // set — the set no longer carries them, and reading the processor here
    // would be fine but says the wrong thing about who owns the value.
    selectorTempoType  .setSelectedIndex (
        Betel::MasterSettings::get().isTempoSynced() ? 1 : 0, false);
    {
        const float m = processor.getTempoSpeedMult();
        selectorSpeed.setSelectedIndex (m > 1.5f ? 1 : (m < 0.75f ? 2 : 0), true);
    }
    selectorTransition   .setSelectedIndex (processor.getTransitionQuant(), true);
    selectorFillLength   .setSelectedIndex (processor.getFillLength(), true);
    selectorArrangerPiano.setSelectedIndex (processor.isPianoMode() ? 1 : 0, true);
    btnDawStart.setToggleState (processor.getDawStartFollow(), juce::dontSendNotification);
    mainTab.getBtnFingered().setToggleState (
        Betel::MasterSettings::get().isFingeredChord(),
        juce::dontSendNotification);
    {
        const auto mask = processor.getSoloEnableMask();
        for (int i = 0; i < 8; ++i)
        {
            mainTab.getSoloButton (i).setToggleState ((mask >> i) & 1,
                                                      juce::dontSendNotification);
            mainTab.getStyleButton (i).setToggleState (
                ! processor.getStylePlayer().isChannelMuted (i),
                juce::dontSendNotification);
        }
    }

    // Crash tab UI follows the restored processor state.
    refreshCrashFromProcessor();
}

// Build a full set snapshot of the CURRENT editor + processor state, in the
// same shape as a saved .bset.  Used to hand the live state to the processor
// when the window closes so the next open can restore it verbatim.
juce::ValueTree MainComponent::buildSetSnapshot()
{
    juce::ValueTree payload ("BetelSet");
    payload.appendChild (soundsTab.captureState(),       nullptr);
    payload.appendChild (processor.captureGlobalState(), nullptr);
    payload.appendChild (processor.captureMixerState(),  nullptr);
    payload.appendChild (processor.captureCrashState(),  nullptr);
    payload.appendChild (processor.captureKitUnityState(), nullptr);
    payload.appendChild (processor.captureIgnorePcState(), nullptr);
    payload.appendChild (processor.captureSfzState(),    nullptr);
    payload.appendChild (soundsTab.captureSfzParams(),   nullptr);
    payload.appendChild (Betel::StyleLevels::get().toTree(), nullptr);

    {
        juce::ValueTree j ("JumpsState");
        const auto jc = jumpsTab.getConfig();
        for (int i = 0; i < Betel::kNumJumpSources; ++i)
            j.setProperty ("d" + juce::String (i),
                           (int) jc.destinations[(size_t) i], nullptr);
        payload.appendChild (j, nullptr);
    }
    {
        juce::ValueTree u ("UiState");
        u.setProperty ("singleMulti", selectorSingleMulti.getSelectedIndex(), nullptr);
        // Style-browser selection.  The processor keeps the STYLE loaded across
        // an editor close, but the browser is rebuilt from scratch on reopen and
        // used to come back showing genre 0 with nothing picked — the "it forgot
        // my style" bug.  Stored by NAME (plus the genre as a hint) so it also
        // survives a folder reshuffle between sessions in a saved .bset.
        // Macro ENGAGED state travels with a set; the macro PRESETS do not —
        // they are global to the installation and live in their own files, so
        // loading someone else's set cannot silently redefine what Funkey Mode
        // sounds like.
        u.setProperty ("funkeyMode", Betel::GlobalMacros::get().isFunkeyOn(),   nullptr);
        u.setProperty ("bigDrums",   Betel::GlobalMacros::get().isBigDrumsOn(), nullptr);
        u.setProperty ("stylesGenre", stylesTab.getSelectedGenre(), nullptr);
        u.setProperty ("styleName",   stylesTab.getSelectedStyleName(), nullptr);
        payload.appendChild (u, nullptr);
    }
    return payload;
}

// On window close, hand the live state to the processor (which outlives the
// editor).  The next open restores from this instead of re-seeding, so closing
// and reopening the plugin window has no effect on the running plugin.
MainComponent::~MainComponent()
{
    processor.storeEditorSnapshot (buildSetSnapshot());
}

void MainComponent::rescanStyleBrowser()
{
    const auto genres = processor.getStyleGenres();
    stylesTab.setGenres (genres);

    const int n = juce::jmin (genres.size(), StylesTab::kNumGenres);
    for (int i = 0; i < n; ++i)
    {
        const auto entries = processor.getStylesInGenre (genres[i]);
        juce::StringArray names, paths;
        names.ensureStorageAllocated ((int) entries.size());
        paths.ensureStorageAllocated ((int) entries.size());
        for (const auto& e : entries)
        {
            names.add (e.displayName);
            paths.add (e.absolutePath);
        }
        stylesTab.setGenreStyles (i, names, paths);
    }
}

void MainComponent::toggleArabicQuickEditor()
{
    // Toggle: a second press (or the window's close button) dismisses it.
    if (arabicQuickWin != nullptr)
    {
        arabicQuickWin.reset();
        return;
    }

    class QuickWin : public juce::DocumentWindow
    {
    public:
        QuickWin (MainComponent& o)
            : juce::DocumentWindow ("Oriental Scale - Right Hand",
                                    juce::Colour (0xFF181818),
                                    juce::DocumentWindow::closeButton),
              owner (o)
        {
            Betel::applyGrexPopupBehaviour (*this);

            auto* kb = new Betel::ArabicScaleKeyboard();

            // Seed the knobs from the ACTIVE solo channel's current tuning so
            // the window reflects what's already sounding.
            const int seedCh = Betel::SamplePlayerEngine::kNumStyleChannels
                             + owner.processor.getActiveSoloSlot();
            for (int n = 0; n < 12; ++n)
                kb->setCents (n, owner.processor.getChannelScaleTuningCents (seedCh, n));

            // Apply edits to EVERY solo channel -- the whole right hand follows
            // one oriental scale from this quick editor.  (Per-slot fine tuning
            // is still available in the sound-edit panel.)
            kb->onValueChanged = [&o = owner] (int noteClass, float cents)
            {
                for (int s = 0; s < Betel::SamplePlayerEngine::kNumSoloChannels; ++s)
                    o.processor.setChannelScaleTuningCents (
                        Betel::SamplePlayerEngine::kNumStyleChannels + s,
                        noteClass, cents);
            };

            kb->setSize (560, 180);
            setContentOwned (kb, true);
            setUsingNativeTitleBar (true);
            setResizable (false, false);
            centreAroundComponent (&owner, 580, 220);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            // Defer the destruction -- deleting the window from inside its own
            // button callback is asking for trouble.
            juce::MessageManager::callAsync ([&o = owner] { o.arabicQuickWin.reset(); });
        }

    private:
        MainComponent& owner;
    };

    arabicQuickWin = std::make_unique<QuickWin> (*this);
}

void MainComponent::refreshReferenceLibrary()
{
    std::vector<BetelRef::RefCategory>   lib;
    std::vector<BetelRef::RefInstrument> ref, gm;

    for (int flag : processor.getInstrumentFlags())
    {
        const juce::String label = juce::String (flag).paddedLeft ('0', 3)
                                  + "  " + processor.getInstrumentName (flag);
        if (flag >= 200) ref.push_back ({ label, flag });   // reference sounds
        else             gm .push_back ({ label, flag });   // GM 0..127
    }

    if (! ref.empty()) lib.push_back ({ "Reference", std::move (ref) });
    if (! gm .empty()) lib.push_back ({ "GM",        std::move (gm)  });

    BetelRef::setLibrary (std::move (lib));
}

void MainComponent::runStyleSearch()
{
    const auto kw = styleSearchBox.getText().trim();
    const auto results = processor.searchStyles (kw);

    juce::StringArray names, paths;
    names.ensureStorageAllocated ((int) results.size());
    paths.ensureStorageAllocated ((int) results.size());
    for (const auto& r : results)
    {
        names.add (r.displayName);
        paths.add (r.absolutePath);
    }
    stylesTab.showSearchResults (names, paths, kw);

    // Jump to the Styles tab so the results are visible.
    const int stylesIdx = kTabNames.indexOf ("STYLES");
    if (stylesIdx >= 0) selectTab (stylesIdx);
}

void MainComponent::clearStyleSearch()
{
    styleSearchBox.clear();
    stylesTab.clearSearch();
}

juce::Rectangle<int> MainComponent::getUiTargetRect() const
{
    const float s = getUiScale();
    const int w = (int)std::round(kDesignW * s);
    const int h = (int)std::round(kDesignH * s);
    const int x = (getWidth()  - w) / 2;
    const int y = (getHeight() - h) / 2;
    return { x, y, w, h };
}

juce::Rectangle<int> MainComponent::scaleRect(const juce::Rectangle<int>& r) const
{
    const auto target = getUiTargetRect();
    const float s = getUiScale();
    const int x = target.getX() + (int)std::round(r.getX() * s);
    const int y = target.getY() + (int)std::round(r.getY() * s);
    const int w = (int)std::round(r.getWidth()  * s);
    const int h = (int)std::round(r.getHeight() * s);
    return { x, y, w, h };
}

juce::Rectangle<int> MainComponent::scaleRectF(const juce::Rectangle<float>& r) const
{
    const auto target = getUiTargetRect();
    const float s = getUiScale();
    const int x = target.getX() + (int)std::round(r.getX() * s);
    const int y = target.getY() + (int)std::round(r.getY() * s);
    const int w = (int)std::round(r.getWidth()  * s);
    const int h = (int)std::round(r.getHeight() * s);
    return { x, y, w, h };
}

//==============================================================================
// SET MANAGER — the three-way frame.  ONE artwork rectangle
// (x 410.5, y 621, w 89.62, h 113.58) holding three stacked buttons separated
// by two white dividers.  resized() lays the buttons out from these and paint()
// draws the lines from the same numbers, so a change to either follows the
// other automatically.
//
//     113.58 = 3 x 36.53 (buttons) + 2 x 2.0 (dividers)
//==============================================================================
static constexpr float kSetTrioX         = 410.50f;
static constexpr float kSetTrioY         = 621.00f;
static constexpr float kSetTrioW         =  89.62f;
static constexpr float kSetTrioH         = 113.58f;
static constexpr float kSetTrioDividerH  =   2.00f;
static constexpr float kSetTrioButtonH   = (kSetTrioH - 2.0f * kSetTrioDividerH) / 3.0f;

//==============================================================================
// FUNKEY / BIG DRUMS FOLLOW THE SET, AND AN ABSENT ANSWER MEANS OFF.
//
// The engaged state travels in a set's UiState, and a set is per style.  But
// every path that applied it defaulted to the CURRENT value:
//
//     gm.setFunkeyOn ((bool) u.getProperty ("funkeyMode", gm.isFunkeyOn()));
//
// which reads "leave it alone if the set does not mention it".  Three ways a
// set does not mention it, and all three left the toggle lit from the PREVIOUS
// style:
//
//   1. a .bset written before those properties existed
//   2. a factory set with no UiState child at all - the whole block was inside
//      `if (u.isValid())`, so it was skipped entirely
//   3. a style with NO SET, which never reached this code in the first place
//
// That is the "sometimes" in the report: whether the toggle stuck depended on
// what the DESTINATION style's set happened to carry.
//
// A set defines the state.  Silence from it is an answer - OFF - not an
// instruction to keep what the last style wanted.  getProperty on an INVALID
// tree returns the default, so an absent UiState and an empty one land in the
// same place without a special case.
//==============================================================================
void MainComponent::applyMacroEngagedState (const juce::ValueTree& uiState)
{
    auto& gm = Betel::GlobalMacros::get();

    gm.setFunkeyOn   ((bool) uiState.getProperty ("funkeyMode", false));
    gm.setBigDrumsOn ((bool) uiState.getProperty ("bigDrums",   false));

    refreshMacroButtons();          // relight BOTH pairs (left panel + Settings)
    soundsTab.refreshMacroFx();     // and make it audible, not merely unlit
}

void MainComponent::refreshMacroButtons()
{
    const auto& gm = Betel::GlobalMacros::get();
    btnBigDrums  .setActive (gm.isBigDrumsOn());
    btnFunkeyMode.setActive (gm.isFunkeyOn());

    // The GLOBAL SETTINGS page carries the same two switches.  Relighting it
    // here — rather than at each call site — is what keeps the left panel and
    // the settings page from ever disagreeing, including after a set restore.
    settingsTab.refreshMacroState();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (background.isValid())
    {
        const auto target = getUiTargetRect();
        g.drawImage(background, target.toFloat());
    }
    else
    {
        g.setColour(juce::Colours::red);
        g.setFont(16.0f);
        g.drawText("ERROR: assets/back.png not found or failed to load.",
                   getLocalBounds(), juce::Justification::centred);
    }

    // SET MANAGER: the two dividers that split the three-way frame into its
    // three buttons.  Drawn here rather than baked into the artwork so the split
    // stays locked to the button bounds — both come from the same kSetTrio*
    // constants below.  #C2C2C2, not white: a separator that is brighter than
    // the captions it separates pulls the eye to the gap instead of the labels.
    {
        g.setColour (InstrEditStyle::kPanelLabel);
        for (int i = 1; i <= 2; ++i)
        {
            const float y = kSetTrioY + (float) i * kSetTrioButtonH
                                      + (float) (i - 1) * kSetTrioDividerH;
            g.fillRect (scaleRectF ({ kSetTrioX, y, kSetTrioW, kSetTrioDividerH }));
        }
    }

    if (debugLayout)
        drawDebugOverlay(g);
}

void MainComponent::resized()
{
    // ── Header animation (design coords: x=554, y=34, w=977, h=130) ──────────
    headerAnimation.setBounds(scaleRect({ 554, 34, 977, 130 }));

    // ── Left panel: Row 1 ────────────────────────────────────────────────────
    knobTempo         .setBounds(scaleRect({ 102, 106, 123,  88 }));
    selectorSpeed     .setBounds(scaleRect({ 240, 104, 123,  91 }));
    selectorTempoType .setBounds(scaleRect({ 378, 104, 123,  91 }));

    // ── Left panel: Row 2 ────────────────────────────────────────────────────
    btnTap            .setBounds(scaleRect({ 102, 204, 195,  68 }));
    btnResetTempo     .setBounds(scaleRect({ 309, 205, 193,  68 }));

    // ── Left panel: Row 3 ────────────────────────────────────────────────────
    // Four selectors on one row: MODE / SOLO MODE / TRANSITIONS / FILL LENGTH.
    // Narrower than the old three (123 -> 89.76) to make room for the fourth.
    // Float coordinates are kept exact through scaleRect's float overload.
    {
        const float selY = 310.88f, selW = 89.76f, selH = 98.23f;
        selectorArrangerPiano .setBounds(scaleRectF(juce::Rectangle<float>{ 104.00f,   selY, selW, selH }));
        selectorSingleMulti   .setBounds(scaleRectF(juce::Rectangle<float>{ 205.25f,   selY, selW, selH }));
        selectorTransition    .setBounds(scaleRectF(juce::Rectangle<float>{ 309.00f,   selY, selW, selH }));
        selectorFillLength    .setBounds(scaleRectF(juce::Rectangle<float>{ 410.25f,   selY, selW, selH }));
    }

    // ── Left panel: Row 4 ────────────────────────────────────────────────────
    knobGlobalSemitone .setBounds(scaleRect({ 103, 448, 123, 118 }));
    lblPlayedChord     .setBounds(scaleRect({ 363, 443, 127,  46 }));

    // ── Left panel: Row 5 ────────────────────────────────────────────────────
    lblStyleName       .setBounds(scaleRect({ 246, 517, 245,  39 }));

    // ── Left panel: Row 6 ────────────────────────────────────────────────────
    // SET MANAGER — laid out from the artwork's own frames.  Float rects via
    // scaleRectF so the fractional design coords survive at any window scale;
    // rounding them to ints here would drift the buttons off their frames.
    btnLoadSet .setBounds (scaleRectF ({ 107.00f,  621.00f,  89.62f, 47.00f }));
    btnSaveSet .setBounds (scaleRectF ({ 207.56f,  621.00f,  89.62f, 47.00f }));
    btnFastSave.setBounds (scaleRectF ({ 307.38f,  621.00f,  89.62f, 47.00f }));

    // The three-way frame (x 410.5, y 621, w 89.62, h 113.58) is ONE rectangle
    // split into three stacked buttons by two 2 px white dividers, painted in
    // paint().  kSetTrio* below is the single source of truth for both the
    // bounds here and the divider lines there — they cannot drift apart.
    for (int i = 0; i < 3; ++i)
    {
        const float y = kSetTrioY + (float) i * (kSetTrioButtonH + kSetTrioDividerH);
        const juce::Rectangle<float> r { kSetTrioX, y, kSetTrioW, kSetTrioButtonH };
        if      (i == 0) btnComments .setBounds (scaleRectF (r));
        else if (i == 1) btnForgetAll.setBounds (scaleRectF (r));
        else             btnDawStart .setBounds (scaleRectF (r));
    }

    // ── Left panel: Row 7 ────────────────────────────────────────────────────
    lblSetName .setBounds (scaleRectF ({ 109.50f, 684.75f, 287.98f, 51.53f }));

    // Global macros, on the artwork frames beneath the set manager.
    // BIG DRUMS uses the re-measured artwork frame; FUNKEY MODE takes the frame
    // BIG DRUMS vacated.  The two rects must not overlap — 355.75 + 142.49 ends
    // at 498.24, which is past FUNKEY's old 365.25 start, so the pair is swapped
    // rather than just nudged.
    btnBigDrums   .setBounds (scaleRectF ({ 355.75f, 762.97f, 142.49f, 49.07f }));
    btnFunkeyMode .setBounds (scaleRectF ({ 202.25f, 763.47f, 142.50f, 49.07f }));
    lblSetName .setColour (juce::Label::textColourId, InstrEditStyle::kPanelLabel);

    // ── Right panel: Tab selector bar ────────────────────────────────────────
    // Background canvas (design coords): x=558, y=194, w=981, h=70.  The eight
    // buttons sit centred on the canvas' VERTICAL MIDDLE, inset from every
    // canvas border by kEdgePad so the rounded highlights never kiss the art,
    // with kBtnGap of air between neighbours.  Float math end to end (like the
    // Row-3 selectors) so the eight widths distribute evenly at any window
    // scale instead of dumping the integer remainder on the last button.
    {
        const float canvasX  = 558.0f, canvasY = 194.0f;
        const float canvasW  = 981.0f, canvasH = 70.0f;
        const float kEdgePad = 8.0f;   // buttons <-> canvas borders
        const float kBtnGap  = 6.0f;   // between neighbouring buttons

        const float btnH = canvasH - 2.0f * kEdgePad;
        const float btnY = canvasY + (canvasH - btnH) * 0.5f;   // vertical middle
        const float btnW = (canvasW - 2.0f * kEdgePad
                            - (float) (kNumTabs - 1) * kBtnGap) / (float) kNumTabs;

        for (int i = 0; i < kNumTabs; ++i)
        {
            const float tx = canvasX + kEdgePad + (float) i * (btnW + kBtnGap);
            tabButtons[i].setBounds (scaleRectF (juce::Rectangle<float> { tx, btnY, btnW, btnH }));
        }
    }

    // ── Right panel: Tab content canvas ──────────────────────────────────────
    auto canvasBounds = scaleRect({ 585, 289, 918, 435 });
    tabContentContainer.setBounds(canvasBounds);

    for (auto* page : tabPages)
        page->setBounds(tabContentContainer.getLocalBounds());

    // ── Global style-search bar — in the gap below the tab bar ────────────────
    {
        auto bar = scaleRect({ 585, 258, 918, 28 });
        const int gap  = 4;
        const int btnW = juce::jmax(60, bar.getWidth() / 9);
        btnStyleSearchClear.setBounds (bar.removeFromRight (btnW));
        bar.removeFromRight (gap);
        btnStyleSearch.setBounds (bar.removeFromRight (btnW));
        bar.removeFromRight (gap);
        styleSearchBox.setBounds (bar);
    }

    // ── Right panel: Virtual MIDI keyboard ────────────────────────────────────
    auto kbBounds = scaleRect({ 585, 771, 779, 50 });
    keyboardComponent.setBounds(kbBounds);

    // ── MIDI song transport — strip between the content canvas and keyboard ───
    transportBar.setBounds(scaleRect({ 585, 728, 918, 38 }));

    const int numWhiteKeys = 36;
    float keyW = (float)kbBounds.getWidth() / (float)numWhiteKeys;
    keyboardComponent.setKeyWidth(keyW);

    knobSplit       .setBounds(scaleRect({ 1393, 760,  65,  32 }));
    btnHold         .setBounds(scaleRect({ 1458, 762,  60,  60 }));
    btnLocateFolder .setBounds(scaleRect({ 1338,  62, 158,  20 }));
    ledFolderLocator.setBounds(scaleRect({ 1500,  65,  14,  14 }));
    lblReg          .setBounds(scaleRect({ 1430,  88,  66,  18 }));
    ledRegistration .setBounds(scaleRect({ 1500,  91,  14,  14 }));
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'd' || key.getTextCharacter() == 'D')
    {
        debugLayout = !debugLayout;
        repaint();
        return true;
    }
    return false;
}

void MainComponent::drawDebugOverlay(juce::Graphics& g)
{
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(14.0f);
    g.drawText("DEBUG ON (press D to toggle)", 10, 10, 320, 20, juce::Justification::left);

    g.setColour(juce::Colours::lime.withAlpha(0.7f));
    g.drawRect(getUiTargetRect(), 2);

    auto debugRect = [&](juce::Component& c, juce::Colour col)
    {
        if (c.isVisible())
        {
            g.setColour(col.withAlpha(0.8f));
            g.drawRect(c.getBounds(), 1);
        }
    };

    debugRect(headerAnimation,        juce::Colours::cyan);
    debugRect(knobTempo,              juce::Colours::cyan);
    debugRect(selectorSpeed,          juce::Colours::cyan);
    debugRect(selectorTempoType,      juce::Colours::cyan);
    debugRect(btnTap,                 juce::Colours::yellow);
    debugRect(btnResetTempo,          juce::Colours::yellow);
    debugRect(selectorArrangerPiano,  juce::Colours::orange);
    debugRect(selectorSingleMulti,    juce::Colours::orange);
    debugRect(selectorTransition,     juce::Colours::orange);
    debugRect(selectorFillLength,     juce::Colours::orange);
    debugRect(knobGlobalSemitone,     juce::Colours::magenta);
    debugRect(lblPlayedChord,         juce::Colours::magenta);
    debugRect(lblStyleName,           juce::Colours::green);
    debugRect(btnLoadSet,             juce::Colours::red);
    debugRect(btnSaveSet,             juce::Colours::red);
    debugRect(btnFastSave,            juce::Colours::red);
    debugRect(btnComments,            juce::Colours::hotpink);
    debugRect(btnForgetAll,           juce::Colours::hotpink);
    debugRect(btnDawStart,            juce::Colours::hotpink);
    debugRect(lblSetName,             juce::Colours::aquamarine);

    for (auto& tb : tabButtons)
        debugRect(tb, juce::Colours::white);

    debugRect(tabContentContainer,    juce::Colours::yellow);
    debugRect(keyboardComponent,      juce::Colours::lime);
    debugRect(knobSplit,              juce::Colours::cyan);
    debugRect(btnHold,                juce::Colours::orange);
    debugRect(ledRegistration,        juce::Colours::red);
    debugRect(ledFolderLocator,       juce::Colours::red);
}

//==========================================================================
// timerCallback — keep the transport-related UI bits in sync with the live
// sequencer state at ~30 Hz.  Three things are tracked:
//
//   1) PLAY/STOP button.  The sequencer can stop on its own when an ending
//      section finishes, and we want the button to follow.
//   2) SYNC PLAY button.  Auto-disarms on first chord-zone note (one-shot);
//      timer drops the toggle visual when that happens.
//   3) Active section LED.  The 16 variation buttons are one mutually-
//      exclusive selector: exactly one is lit at a time = the section active
//      right now.  The user can click a variButton to queue a change — the
//      sequencer may delay until the next bar boundary — and the lit button
//      moves only when the active section actually changes.  It is never blank:
//      when stopped (or for a section with no button) it falls back to the
//      last-played main variation's button.
//==========================================================================
static int sectionToVariButtonIdx (Betel::StyleSection s) noexcept
{
    switch (s)
    {
        case Betel::StyleSection::IntroA:   return 0;
        case Betel::StyleSection::IntroB:   return 1;
        case Betel::StyleSection::IntroC:   return 2;
        case Betel::StyleSection::MainA:    return 4;
        case Betel::StyleSection::MainB:    return 5;
        case Betel::StyleSection::MainC:    return 6;
        case Betel::StyleSection::MainD:    return 7;
        case Betel::StyleSection::FillAA:   return 8;
        case Betel::StyleSection::FillBB:   return 9;
        case Betel::StyleSection::FillCC:   return 10;
        case Betel::StyleSection::FillDD:   return 11;
        case Betel::StyleSection::FillBA:   return 12;
        case Betel::StyleSection::EndingA:  return 13;
        case Betel::StyleSection::EndingB:  return 14;
        case Betel::StyleSection::EndingC:  return 15;
        default:                            return -1;
    }
}

void MainComponent::timerCallback()
{
    // MASTER SETTINGS: write out anything the setters flagged.  This is the
    // message thread, which is the whole point - a setter may have been called
    // from processBlock (a MIDI CC assigned to SPLIT, say) and file I/O has no
    // business there.  One cheap atomic load when nothing is pending.
    Betel::MasterSettings::get().flushIfDirty();

    auto& seq = processor.getSequencer();

    // Registration LED. Cheap enough to poll: registering happens in the
    // SETTINGS tab, and polling here means that page needs no callback back
    // into MainComponent just to repaint a 14 px circle.
    {
        const bool reg = RegistrationManager::getInstance().isRegistered();
        if (reg != ledRegistration.isRegistered())
            ledRegistration.setRegistered (reg);
    }

    // ── MIDI CC → THE ACTUAL CONTROLS ────────────────────────────────────────
    //
    // A CC arrives on the audio thread and moves the PARAMETER.  Nothing was
    // telling the widget that owns that parameter, so a pedal changed the sound
    // while its knob sat still - and worse, the next touch of that knob snapped
    // the value back to wherever the knob had been left, silently undoing every
    // CC move since.
    //
    // Mirrored here rather than from applyCcToTarget because that runs on the
    // AUDIO thread, where touching a Component is not allowed.  The flag is the
    // handoff.
    //
    // notify = false throughout: these setters would otherwise call straight
    // back into the processor, and a CC stream would drive a feedback loop
    // between the two.  TEMPO is deliberately absent - section 5 below already
    // mirrors it every tick, and doing it twice would fight the SYNCED lock.
    if (processor.consumeCcValueDirty())
    {
        mixerTab.setMasterGain   (processor.getMasterVolume());
        mixerTab.setStyleBusGain (processor.getStyleVolume());
        knobGlobalSemitone.setValue ((double) processor.getGlobalTranspose(), false);
        knobSplit         .setValue ((double) processor.getSplitPoint(),      false);
    }

    // 0) CC learn/assign mirror — refresh the Settings tab when the engine
    //    captures a learned controller, and keep the armed-row highlight live.
    if (processor.consumeCcDirty())
    {
        std::array<int, Betel::CcMap::kNumTargets> live {};
        for (int i = 0; i < SettingsTab::kNumCcTargets; ++i)
        {
            const int n = processor.getCcNumber (i);
            settingsTab.setCcNumber (i, n);
            if (i < Betel::CcMap::kNumTargets) live[(size_t) i] = n;
        }
        // The assignment is captured on the AUDIO thread; the file write happens
        // here, on the message thread, the first time the UI notices.
        Betel::CcMap::get().syncFrom (live);
    }
    settingsTab.setLearning (processor.getCcLearnTarget());

    // ── MIDI song transport: drain the recorder FIFO and keep the bar's lamps
    //    in sync with the recorder/player state (incl. auto-stop at song end).
    {
        auto& rec  = processor.getSongRecorder();
        auto& play = processor.getSongPlayer();
        rec.serviceFifo();
        transportBar.setArmed     (rec.isArmed());
        transportBar.setRecording (rec.isRecording());
        if (play.hasFinished())
            transportBar.setPlaying (false);
        else
            transportBar.setPlaying (play.isPlaying());
    }

    // 1) PLAY / STOP visual ──────────────────────────────────────────────
    const bool engineIsPlaying = seq.isPlaying();
    if (engineIsPlaying != mainTab.getBtnPlayStop().getToggleState())
        mainTab.setPlayStopVisual (engineIsPlaying);

    // 2) SYNC PLAY visual ────────────────────────────────────────────────
    const bool syncArmed = processor.getChordTracker().isSyncStartArmed();
    if (mainTab.getBtnSyncPlay().getToggleState() != syncArmed)
        mainTab.getBtnSyncPlay().setToggleState (syncArmed,
                                                 juce::dontSendNotification);

    // 3) Active section LED ──────────────────────────────────────────────
    // The 16 variation buttons are ONE mutually-exclusive selector: exactly
    // one is lit = the section active right now.  While a section plays its own
    // button is lit; when stopped (or if the section has no button) it falls
    // back to the last-played main variation's button, so a button is never
    // blank and the highlight moves only when the active section changes.
    int activeBtn = sectionToVariButtonIdx (seq.getCurrentSection());
    if (! (engineIsPlaying && processor.hasStyle()) || activeBtn < 0)
        activeBtn = 4 + juce::jlimit (0, 3, (int) seq.getCurrentVariation());
    mainTab.setCurrentlyPlayingVariation (activeBtn);

    // 4) Style metadata strip — push on every change of the loaded style
    //    pointer (set by loadStyle / cleared on style-load failure).
    const Betel::StyleData* cur = processor.getCurrentStyle();
    if (cur != lastSeenStylePtr)
    {
        lastSeenStylePtr = cur;
        if (cur != nullptr)
            mainTab.setStyleInfo (cur->name, cur->originalBPM,
                                  cur->timeSigNum, cur->timeSigDen);
        else
            mainTab.setStyleInfo ({}, 0.0f, 0, 0);

        // (11) Left-panel style display — THE SET'S NAME, not the style's.
        //
        // cur->name is the name written INSIDE the style file, which is the
        // machine-generated original: "16BeatBallad.T160".  Nobody chose it and
        // nobody wants to read it.  What the user picked is the set, whose file
        // name is the curated one — so show that, and fall back to the style
        // FILE's base name (also curated, after the rename) when a style is
        // playing without a set.
        lblStyleName.setText (currentSetDisplayName());

        // The SET MANAGER pill, which has read "No Set" since it was added —
        // nothing ever wrote to it.  Now that a name exists to put there, it
        // says which set is loaded.
        lblSetName.setText (lastSetFile.existsAsFile()
                                ? Betel::displayNameFor (lastSetFile)
                                : juce::String ("No Set"));

        // Keep an open Style Data popup in sync with the newly-loaded style.
        if (styleDataWindow != nullptr && cur != nullptr)
            styleDataWindow->refreshFrom (*cur);

        // Sync the mixer faders to the channels' ACTUAL volumes.  The style's
        // setup just wrote per-channel levels (its CC 7 in as a fader value);
        // without this the faders sit at unity and the first touch jumps the
        // channel to the fader's position ("suddenly wakes up").  Reading the
        // engine's base volume (expression excluded) makes the faders honest and
        // jump-free.  setLinearGain does not notify, so this never loops back.
        {
            for (int s = 0; s < 8; ++s)
            {
                mixerTab.setStyleSlotGain (s, processor.getChannelVolume (s));
                mixerTab.setSoloSlotGain  (s, processor.getChannelVolume (
                                                Betel::SamplePlayerEngine::kNumStyleChannels + s));
            }

            // FINISHER gain-reduction meter — polled on the same UI tick as the
            // faders.  setFinisherGainReductionDb ignores sub-0.1 dB changes and
            // repaints only the meter strip, so this is cheap.
            const float finGr = processor.getFinisherGainReductionDb();
            mixerTab.setFinisherGainReductionDb (finGr);
            if (finisherWindow && finisherWindow->isVisible())
                finisherWindow->setGainReduction (finGr);

            // SWEETENER gain-reduction meters — same tick, same reasoning as
            // the Finisher's above.  refreshSweetenerMeters returns immediately
            // unless a sound editor is actually open, and the panels themselves
            // repaint only the meter strip and only when a bar moved, so a
            // closed editor costs one branch and an open one costs a 18 px
            // repaint.
            soundsTab.refreshSweetenerMeters();

            // THE GM ENVELOPE TABLE NO LONGER RUNS HERE.
            //
            // It used to re-derive attack, decay, sustain, release, amp curve,
            // filter, note range, play mode and octave for all eight style slots
            // from their GM program on EVERY style load — landing on top of
            // whatever .ins preset applyVoiceSetup had applied a moment earlier,
            // which is the same defect applySoundCalibration was fixed for.
            //
            // Those values belong to the SET now (<StyleSlots>, applied in
            // applySetPayload), and the table survives only as the source the
            // set pack is baked from.  See SoundsTab::gmDefaultsInto.
            // STYLE VOLUME fader follows the bus gain, which a style (re)load
            // sets automatically.  setLinearGain does not notify, so this never
            // loops back; manual drags still win until the next style load.
            mixerTab.setStyleBusGain (processor.getStyleVolume());
        }
    }

    // 5) Tempo knob (1/2/3) — in SYNCED the knob shows the live base BPM and
    //    is locked; in FREE it's editable and follows the manual BPM (so TAP
    //    visibly moves it).
    if (processor.isTempoSynced())
    {
        knobTempo.setEnabled (false);
        knobTempo.setValue ((double) processor.getCurrentBaseBPM(),
                            juce::dontSendNotification);
    }
    else
    {
        knobTempo.setEnabled (true);
        const double mb = (double) processor.getManualBPM();
        if (std::abs (knobTempo.getValue() - mb) > 0.5)
            knobTempo.setValue (mb, juce::dontSendNotification);
    }

    // 6) Played-chord display (10).
    {
        const auto c = processor.getChordTracker().getCurrentChord();
        const juce::String txt = chordToDisplayString (c);
        if (txt != lastChordText)
        {
            lastChordText = txt;
            lblPlayedChord.setText (txt);
        }
    }

    // 7) Mirror note-control-driven state back onto the UI so the on-screen
    //    controls follow when a reserved MIDI note (0–35) flips them.
    {
        auto& sp = processor.getStylePlayer();
        for (int s = 0; s < 8; ++s)
        {
            const bool on = ! sp.isChannelMuted (s);   // element ON = not muted
            auto& b = mainTab.getStyleButton (s);
            if (b.getToggleState() != on)
                b.setToggleState (on, juce::dontSendNotification);
        }

        // Solo selection: single-slot routing (mask 0) lights the active slot;
        // otherwise the enable-mask drives the lamps.
        const juce::uint8 mask = processor.getSoloEnableMask();
        const int active = processor.getActiveSoloSlot();
        for (int s = 0; s < 8; ++s)
        {
            const bool on = (mask == 0) ? (s == active)
                                        : ((mask & (1u << s)) != 0);
            auto& b = mainTab.getSoloButton (s);
            if (b.getToggleState() != on)
                b.setToggleState (on, juce::dontSendNotification);

            // Label each solo element with its selected instrument name.
            const juce::String nm = soundsTab.getSoloInstrumentName (s);
            if (b.getButtonText() != nm)
                b.setButtonText (nm);
        }

        // Arranger / Piano selector.
        const int wantPiano = processor.isPianoMode() ? 1 : 0;
        if (selectorArrangerPiano.getSelectedIndex() != wantPiano)
            selectorArrangerPiano.setSelectedIndex (wantPiano, false);
        keyboardComponent.setPianoMode (wantPiano == 1);

        // HOLD lamp.
        const bool held = processor.getSequencer().isTransitionHeld();
        if (btnHold.getToggleState() != held)
            btnHold.setToggleState (held, juce::dontSendNotification);
    }

    // 8) Mirror the ENGINE's actually-loaded instruments onto the Sounds tab,
    //    so whatever selected the voice — style setup, a runtime program
    //    change routed through CASM, a set restore, or a user pick — the tab
    //    shows it as selected.  Every push is guarded by an equality check so
    //    nothing repaints unless the engine state actually changed (no
    //    blinking, no redundant work at 30 Hz).
    {
        constexpr int soloBase = Betel::SamplePlayerEngine::kNumStyleChannels;
        for (int s = 0; s < 8; ++s)
        {
            // ── Style slot s = engine channel s ──────────────────────────
            if (processor.isChannelDrum (s))
            {
                // THE KIT KEY IS THE WITNESS, NOT THE NAME.
                //
                // Watching getChannelDrumKitName alone missed every kit change
                // that came from the runtime PC path: that path publishes with
                // syncName false (audio thread - it cannot write a juce::String),
                // so the name still read the kit BEFORE the change, the mirror
                // saw nothing to do, and the selector kept a stale highlight or
                // went blank.  The key is stamped on EVERY publish path -
                // composed, sampled, fallback, user pick - so nothing gets past
                // it.
                const int kitKey = processor.getChannelKitKey (s);
                if (kitKey >= 0 && soundsTab.getSlotKitKey (false, s) != kitKey)
                    soundsTab.setSlotDrumKit (false, s, kitKey,
                                              processor.getKitNameForKitKey (kitKey));

                // A name change with no key change (a user pick that renames the
                // same kit) still has to land.
                const juce::String kit = processor.getChannelDrumKitName (s);
                if (kit.isNotEmpty() && soundsTab.getSlotDrumKitName (false, s) != kit)
                    soundsTab.setSlotDrumKitName (false, s, kit);

                // ── THE MIXER LABEL IS PUSHED UNCONDITIONALLY, AND CONVERTED ──
                //
                // Two things were wrong here and they compounded.
                //
                // The push sat inside the key branch behind `if (nm.isNotEmpty())`
                // and an else that could not run when the key branch had been
                // taken - so a kit change whose name resolved empty updated the
                // Sounds tab and left the mixer showing the PREVIOUS kit, or
                // nothing.  That is the intermittent blank on DRUMS and PERC.
                //
                // And the engine reports a composed kit by REGISTRY KEY ("000",
                // "016"), not by name.  The Sounds tab runs that through
                // displayNameForKit; the mixer never did, so even when a label
                // did arrive it was a number.  Same conversion, same source of
                // truth, one line.
                {
                    const juce::String shown =
                        SoundsTab::displayNameForKit (soundsTab.getSlotDrumKitName (false, s));
                    if (shown.isNotEmpty()) mixerTab.setStyleSlotLabel (s, shown);
                }
            }
            else
            {
                const int flag = processor.getChannelInstrumentFlag (s);
                if (flag >= 0 && flag < 128
                    && soundsTab.getSlotPatch (false, s) != flag)
                {
                    soundsTab.setSlotPatch (false, s, flag);
                    mixerTab.setStyleSlotLabel (s, SoundsTab::instrumentNameForPatch (flag));

                    // The sound on this slot just changed — and if it carries a
                    // saved voice, the ENGINE already has it while the editor
                    // still holds the previous sound's values.  Adopt it here,
                    // where every instrument change is already detected, so the
                    // editor shows what is playing instead of something stale
                    // that the next knob move would push back over the preset.
                    SlotParams sp;
                    if (processor.getPresetParamsForChannel (s, sp))
                        soundsTab.adoptPresetParams (false, s, sp);
                }
            }

            // ── Solo slot s = engine channel soloBase + s ─────────────────
            const int soloFlag = processor.getChannelInstrumentFlag (soloBase + s);
            if (soloFlag >= 0 && soloFlag < 128
                && soundsTab.getSlotPatch (true, s) != soloFlag)
            {
                soundsTab.setSlotPatch (true, s, soloFlag);
                mixerTab.setSoloSlotLabel (s, SoundsTab::instrumentNameForPatch (soloFlag));

                SlotParams sp;                       // same adoption, right hand
                if (processor.getPresetParamsForChannel (soloBase + s, sp))
                    soundsTab.adoptPresetParams (true, s, sp);
            }
        }
    }
}

//==============================================================================
// MIDI song transport helpers
//==============================================================================

// Snapshot the session state at the moment recording is armed, so the saved
// .grxsong can rebuild the whole performance from A to Z.
GrexSongRecorder::Setup MainComponent::captureSongSetup() const
{
    GrexSongRecorder::Setup s;
    s.stylePath = processor.getLastLoadedStylePath();
    if (auto* st = processor.getCurrentStyle())
        s.tempoBpm = (st->originalBPM > 0.0f) ? (double) st->originalBPM : 120.0;
    s.splitPoint = processor.getSplitPoint();
    s.transpose  = processor.getGlobalTranspose();

    for (int i = 0; i < 8; ++i)
    {
        s.soloPatch [(size_t) i] = soundsTab.getSlotPatch (true,  i);
        s.stylePatch[(size_t) i] = soundsTab.getSlotPatch (false, i);
    }

    const auto jc = jumpsTab.getConfig();
    for (int i = 0; i < 8; ++i)
        s.jumpsDest[(size_t) i] = (int) jc.destinations[(size_t) i];

    for (int i = 0; i < 5; ++i)
        s.ccNumber[(size_t) i] = processor.getCcNumber (i);

    return s;
}

// Restore the captured session before playback so the arranger reproduces the
// performance faithfully when the recorded event stream is re-emitted.
void MainComponent::applySongSetup (const GrexSongRecorder::Setup& s)
{
    if (s.stylePath.isNotEmpty())
    {
        juce::String err;
        if (processor.loadStyle (juce::File (s.stylePath), err))
            soundsTab.applySoundCalibration();
    }

    // FromProject: restoring a song positions the keyboard for that song, but
    // must not redefine where this player's hands live from then on.  See
    // BetelgeuseProcessor::setSplitPointFromProject.
    processor.setSplitPointFromProject (s.splitPoint);
    processor.setGlobalTranspose (s.transpose);

    for (int i = 0; i < 8; ++i)
    {
        soundsTab.setSlotPatch (true,  i, s.soloPatch [(size_t) i]);
        soundsTab.setSlotPatch (false, i, s.stylePatch[(size_t) i]);
    }

    auto jc = jumpsTab.getConfig();
    for (int i = 0; i < 8; ++i)
        jc.destinations[(size_t) i] = (Betel::StyleSection) s.jumpsDest[(size_t) i];
    jumpsTab.setConfig (jc);
    processor.setJumpsConfig (jc);   // setConfig doesn't fire onJumpChanged

    // NOTE: the CC-target *mapping* is not restored here (the processor exposes
    // no public setter for it), but the recorded CC *events* replay correctly.
}


// ======================================================
//  FINISHER editor window
// ======================================================
void MainComponent::openFinisherWindow()
{
    if (! finisherWindow)
    {
        finisherWindow = std::make_unique<FinisherWindow>();

        finisherWindow->onEnabledChanged = [this](bool on)
        {
            processor.setFinisherEnabled (on);
            mixerTab.setFinisherEnabled (on);
        };
        finisherWindow->onAmountChanged = [this](float pct)
        {
            processor.setFinisherAmount (pct);
        };
        finisherWindow->onCharacterChanged = [this](int c)
        {
            // Selecting a character LOADS its preset into the DSP's parameter
            // set, so the window has to re-read every slider afterwards —
            // otherwise it would keep showing the outgoing values.
            processor.setFinisherCharacter (c);
            refreshFinisherWindow();
        };
        finisherWindow->onParamChanged = [this](int id, float v)
        {
            processor.setFinisherParam (id, v);
        };
        finisherWindow->onStageToggled = [this](int stage, bool on)
        {
            processor.setFinisherStageEnabled (stage, on);
        };
        finisherWindow->onClosed = [] { /* window hides itself; kept alive for reuse */ };
    }

    refreshFinisherWindow();
    finisherWindow->openCentredOver (this);
}

void MainComponent::refreshFinisherWindow()
{
    if (! finisherWindow) return;

    finisherWindow->setEnabled   (processor.getFinisherEnabled());
    finisherWindow->setAmount    (processor.getFinisherAmount());
    finisherWindow->setCharacter (processor.getFinisherCharacter());

    for (int i = 0; i < Betel::Finisher::kNumParams; ++i)
        finisherWindow->setParam (i, processor.getFinisherParam (i));

    for (int st = 0; st < Betel::Finisher::kNumStages; ++st)
        finisherWindow->setStageEnabled (st, processor.getFinisherStageEnabled (st));
}



