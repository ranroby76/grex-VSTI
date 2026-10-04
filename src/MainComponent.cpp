

#include "MainComponent.h"
#include "SongWindowContent.h"
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

// SET EDITOR is absent on purpose — it is now a page inside SETTINGS.  This
// array is the authority for the button row, the page array and kNumTabs, so
// the entry had to go from all three together.
static const juce::StringArray kTabNames {
    "MAIN", "STYLES", "SOUNDS", "MIXER", "JUMPS", "CRASH", "SETTINGS",
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

//==============================================================================
//  WHICH SOLO THE OCTAVE MACRO MEANS - "the selected solo", from the player's
//  side of the screen.
//
//  Not simply getActiveSoloSlot().  In single-solo mode the MAIN tab's SOLO
//  buttons set the solo MASK, and setSoloEnableMask does not move the active
//  slot - so the slot your right hand is playing and the slot the SOUNDS tab
//  last selected can be two different slots.  Aimed at the active slot alone,
//  a press would re-pitch an instrument you are not hearing.
//
//      mask 0           legacy single-slot routing: the active slot IS the
//                       one playing.
//      one bit set      that slot - it is the one lit, and the one sounding.
//      several bits     solos layered: the active slot if it is among them
//                       (the one open for editing), else the lowest enabled.
//
//  Used by BOTH the press and the 30 Hz mirror, so the pads always show the
//  slot a press would change.
//==============================================================================
static int selectedSoloForOctave (const BetelgeuseProcessor& p)
{
    const juce::uint8 mask   = p.getSoloEnableMask();
    const int         active = p.getActiveSoloSlot();

    if (mask == 0)                       return active;
    if ((mask & (mask - 1)) == 0)        // exactly one bit
    {
        for (int s = 0; s < 8; ++s)
            if (mask & (1u << s))        return s;
    }
    if (mask & (1u << active))           return active;
    for (int s = 0; s < 8; ++s)
        if (mask & (1u << s))            return s;
    return active;
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

    // ORDER MATTERS HERE. Both readouts are added AFTER the animation so they
    // paint over it rather than under it - a z-order kept by construction
    // cannot be undone by the next component somebody adds.
    addAndMakeVisible(lblPlayedChord);

    // ── THE THREE FEATURES ───────────────────────────────────────────────────
    addAndMakeVisible(featurePanel);

    featurePanel.onHarmonyEnabled = [this] (bool on)
    {
        processor.setHarmonyEnabled (on);
    };
    featurePanel.onHarmonyType = [this] (Betel::Harmonizer::Type t)
    {
        processor.setHarmonyType (t);
    };
    featurePanel.onHarmonyLevel = [this] (int pct)
    {
        processor.setHarmonyLevel (pct);
    };

    featurePanel.onMultiSplitEnabled = [this] (bool on)
    {
        processor.setMultiSplitEnabled (on);
    };
    featurePanel.onBassSplitPoint = [this] (int note)
    {
        processor.setBassSplitPoint (note);
    };
    featurePanel.onBassZoneSlot = [this] (int slot)
    {
        processor.setBassZoneSlot (slot);
    };

    featurePanel.onBassInversionEnabled = [this] (bool on)
    {
        processor.setBassInversionEnabled (on);
    };
    featurePanel.onManualBassEnabled = [this] (bool on)
    {
        processor.setManualBassEnabled (on);
    };

    // Seed FROM the processor rather than pushing the panel's own defaults into
    // it: on a reopened editor the processor already holds the session's state,
    // and pushing would overwrite it with whatever the widgets constructed at.
    featurePanel.setHarmonyState (processor.isHarmonyEnabled(),
                                  processor.getHarmonyType(),
                                  processor.getHarmonyLevel());
    featurePanel.setMultiSplitState (processor.isMultiSplitEnabled(),
                                     processor.getBassSplitPoint(),
                                     processor.getBassZoneSlot());
    featurePanel.setBassState (processor.isBassInversionEnabled(),
                               processor.isManualBassEnabled());

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
    // ITS FORE WAS TOUCHING THE PAINTED PANEL, top and bottom.  Three items
    // share the black rect here, so each segment is a third of the height the
    // two-item groups give theirs, and the shared proportional inset that
    // leaves those looking right leaves this one looking flush.
    //
    // 0.05 per side = 10% off the height.  Tuned by eye in two passes of 5%:
    // the first (0.025) still left the three fills reading as touching the
    // panel's top and bottom edges.  Asked for per instance so the other five
    // groups are untouched - see SelectorGroup.
    selectorSpeed.setExtraVerticalInset (0.05f);
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

    // GLOBAL SEMITONE IS A STEPPER NOW.  One press, one semitone - see
    // RectangleKnob::setStepperMode for why dragging is switched off with it.
    knobGlobalSemitone.setStepperMode (true);

    // ── ENERGY ───────────────────────────────────────────────────────────────
    addAndMakeVisible (energySlider);
    energySlider.onChange = [this] (double v)
    {
        processor.setStyleEnergy ((int) v);

        // Keep an OPEN editor honest.  Its velocity display shows the slot's
        // curve plus ENERGY, so moving this while the window is up has to move
        // the readout with it or the number on screen stops being the number in
        // force - which is the whole point of showing the sum.
        soundsTab.refreshEnergyOffset (processor.getStyleEnergyCurveOffset());
    };

    soundsTab.onGetEnergyCurveOffset = [this]
    { return processor.getStyleEnergyCurveOffset(); };

    // ── MACRO MASTER VOLUME ──────────────────────────────────────────────────
    //
    // ONE VALUE, TWO WIDGETS.  The processor owns it; this knob and the mixer's
    // master fader both write it, and each pushes the other SILENTLY so a move
    // cannot bounce off the far callback and re-enter this one.
    addAndMakeVisible (knobMasterVol);
    knobMasterVol.onChange = [this] (double pct)
    {
        const float g = (float) (pct * 0.01);
        processor.setMasterVolume (g);
        mixerTab.setMasterGain (g);      // MixerFader::setLinearGain does not notify
    };

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

    // lblPlayedChord is added near the top of this ctor.  It is NOT in the
    // header any more - resized() puts it beside the set pill in Row 7.

    // ── Left panel: Row 6 ────────────────────────────────────────────────────
    // ── Global macros ────────────────────────────────────────────────────────
    // Toggle the flag, re-commit every slot (that is what makes it audible —
    // see SoundsTab::refreshMacroFx), then relight the button.  The macro
    // presets themselves are global and live in their own files; only the
    // ENGAGED state travels with a set.
    mainTab.onFunkeyToggled = [this] (bool on)
    {
        Betel::GlobalMacros::get().setFunkeyOn (on);
        soundsTab.refreshMacroFx();
        refreshMacroButtons();
    };
    refreshMacroButtons();

    // THE FUNKEY MIX - the "E" on the FUNKEY button.  Per SET, i.e. per style:
    // it is saved in the set's UiState and put back by applyMacroEngagedState.
    // The pop-up opens at the live value, and every move goes straight through
    // the processor's one setter.
    mainTab.setFunkeyMixValue (processor.getFunkeyMix() * 100.0f);
    mainTab.onFunkeyMixChanged = [this] (float v0to100)
    {
        processor.setFunkeyMix (v0to100 / 100.0f);
    };

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
        // FAST SAVE WRITES THE SET OF THE STYLE THAT IS LOADED.  lastSetFile is the
        // last set loaded or saved, and it is the target only while it still
        // belongs to that style.  Loading a style that has no set of its own used
        // to leave the PREVIOUS style's set as the target, and FAST SAVE wrote
        // this style's whole session into it - 12-8 Ballad's set ended up holding
        // Club Dance 1, EDM kit and all, and the ballad then loaded it as its own.
        auto belongsTo = [this] (const juce::File& setFile, const juce::String& style)
        {
            if (style.isEmpty()) return true;                   // no style: nothing to disagree with
            if (setFile == setFileForStyle (style)) return true; // the style's own set
            // A set saved under another name for this same style counts too: the
            // style it says it holds - the id, or an older set's path - by name.
            if (auto xml = juce::XmlDocument::parse (setFile))
            {
                juce::String held;
                if (auto* link = xml->getChildByName ("StyleLink"))  held = link->getStringAttribute ("styleId");
                if (held.isEmpty())
                    if (auto* gs = xml->getChildByName ("GlobalState")) held = gs->getStringAttribute ("currentStylePath");
                auto nameOf = [] (const juce::String& ref)
                {
                    return ref.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false)
                              .upToLastOccurrenceOf (".fgt", false, true);
                };
                return held.isNotEmpty() && nameOf (held).equalsIgnoreCase (nameOf (style));
            }
            return false;
        };

        const auto current = processor.getLastLoadedStylePath();
        juce::File target;
        if (lastSetFile.existsAsFile() && belongsTo (lastSetFile, current))
            target = lastSetFile;

        if (target == juce::File())
            target = setFileForStyle (current);

        if (target == juce::File())
            target = setFileForStyle (pendingStylePath);

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

    // setEditorTab is NOT here — settingsTab is its parent now and shows it as
    // a page.  Listing it would give it two parents and a second layout pass.
    tabPages = { &mainTab, &stylesTab, &soundsTab, &mixerTab,
                 &jumpsTab, &crashTab, &settingsTab,
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
            if (processor.hasStyle())
            {
                seq.start();
            }
            else
            {
                // NOT A SILENT REFUSAL ANY MORE.  Snapping the lamp back and
                // saying nothing is what made a style-loading failure look like
                // a broken PLAY button, twice.  Raise the same flag the MIDI
                // route raises so ONE recovery serves both, then let the mirror
                // run it - doing it inline would put a style load inside a
                // button callback.
                mainTab.setPlayStopVisual (false);
                processor.notePlayRefusedNoStyle();
            }
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

    // THE SOLO OCTAVE MACRO.  The pad asks; the model decides.  The pads light
    // from the model on the next mirror tick, never from the click itself.
    mainTab.onSoloOctave = [this] (int octaves)
    {
        soundsTab.setSoloOctave (selectedSoloForOctave (processor), octaves);
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
        stylesTab.setSelectedStylePath (absolutePath, displayNameForStyleRef (absolutePath));

        // No LOAD button anymore — selecting a style loads it immediately.
        if (pendingStylePath.isEmpty()) return;

        // LEVELS ARE PER STYLE.  Start from the default template rather than
        // inheriting whatever the previous style was tuned to — otherwise one
        // style's "pull the perc down" would silently follow the user around
        // the whole library.  The style's own set overwrites this a moment
        // later if it has one.
        //
        // *** THIS DOES NOT TOUCH THE STYLE BOOST, AND MUST NOT. ***
        //
        // The boost is GLOBAL now, owned by grex_boost.xml, and applyValues()
        // no longer writes it — see the block above applyValues in
        // StyleLevels.h.  It used to, and that is exactly why a boost tuned on
        // the GLOBAL SETTINGS slider came back at the factory value the moment
        // the next style was picked: the slider saved correctly and this line
        // threw the number away a fraction of a second later.
        //
        // What is left here is styleVolFollow, which is retired and read by
        // nothing; the call is kept so the per-set field still resets per set.
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
                        // ── THE CLICKED REF GOES IN AS-IS WHEN IT IS AN ID ──
                        //
                        // THIS IS THE "I HAD TO CLICK FOUR STYLES BEFORE ONE
                        // PLAYED" BUG.  `pendingStylePath` is a STYLE ID now
                        // ("01-ballad/grand-diva"), not a filename, and feeding
                        // an id to juce::File(...).getRelativePathFrom() is
                        // meaningless - it treats the id as a relative path
                        // against the process's working directory and hands
                        // back something that resolves to nothing.
                        //
                        // The applier then failed to load it, and because this
                        // branch RETURNS below, the bare-style fallback further
                        // down was never reached either.  So clicking any style
                        // whose set is a FACTORY set loaded nothing and said
                        // nothing.  Clicking around eventually landed on a user
                        // set (which carries GlobalState and takes the branch
                        // above) or on a style with no set at all - which is
                        // exactly what "it started working after four styles"
                        // looks like from the outside.
                        //
                        // styleId is the property the reader prefers, so an id
                        // goes in as an id.  Only a genuine file still gets the
                        // relative-path treatment, which is what that maths was
                        // always for.
                        const juce::File asFile (pendingStylePath);
                        if (asFile.existsAsFile())
                            link.setProperty ("relPath",
                                              asFile.getRelativePathFrom (
                                                  processor.getFolderManager().getStylesFolder()),
                                              nullptr);
                        else
                            link.setProperty ("styleId", pendingStylePath, nullptr);
                    }

                    applySetPayload (payload);
                    lastSetFile = setFile;
                    setEditorTab.refreshFromState();

                    // ── THE RULE: NEVER LEAVE THE BROWSER WITHOUT A STYLE ────
                    //
                    // Grex has had one invariant since the very first build -
                    // there is never a moment with no style loaded - and the
                    // move to set-driven loading quietly broke it, because the
                    // SET became the thing that loads the style and a set can
                    // fail to resolve one.
                    //
                    // The click is the authority on WHICH style, not the set.
                    // So if the set did not end up on the style that was
                    // actually clicked, load it directly. Cheap, and it makes
                    // the set responsible only for the settings around a style,
                    // which is all it was ever meant to own.
                    if (! processor.hasStyle()
                        || processor.getLastLoadedStylePath() != pendingStylePath)
                    {
                        // ON SCREEN, not in a file.  The file diagnostics are
                        // gone; a set whose style will not load is exactly the
                        // case where the player needs telling, and the STYLES
                        // tab already owns a transient notice for it.
                        juce::String err;
                        if (! processor.loadStyleByRef (pendingStylePath, err))
                            stylesTab.showNotice ("SET APPLIED - ITS STYLE WOULD NOT LOAD");
                    }

                    soundsTab.applySoundCalibration();
                    refreshVariationAvailability();
                    return;
                }
            }
        }

        // NO SET FOR THIS STYLE — load the style bare, on the template levels.
        // Hand the style slots back to calibration: nothing owns them now.
        soundsTab.clearStyleSlotOwnership();

        // Nothing declares FUNKEY for this style, so nothing asks for it: an
        // invalid tree means off.  Without this the toggle stayed lit from
        // whatever style was loaded before, which is the third and last of the
        // sticky paths.
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
        // pendingStylePath now holds a STYLE ID from the browser, or a legacy
        // path out of an old set. loadStyleByRef takes either.
        if (! processor.loadStyleByRef (pendingStylePath, err))
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Could not load style",
                err.isEmpty() ? juce::String ("Unknown error") : err);
        else
        {
            soundsTab.applySoundCalibration();   // force PAD cutoff + drum FX onto the engine

            // NO SET IS COMING on this path, so the faders are reset here - the
            // fourth of the sticky-state clears this branch already performs
            // (macros and IGNORE PC are just above).  AFTER the load, so a
            // re-adopt inside loadStyleByRef cannot land on top of it.
            //
            // UNLESS THE PROCESSOR ALREADY APPLIED A SET'S MIXER.  adoptFreshStyle
            // now resets and restores the levels itself when the style has a set,
            // precisely so a closed editor does not leave them unset - and this
            // reset would wipe exactly that work.  The CC 7 seed is the same
            // test, so asking the player is asking the one authority.
            if (! processor.getStylePlayer().isStyleCc7Suppressed())
                processor.resetUserVolumesToUnity();

            refreshMixerFromProcessor();
        }

        refreshVariationAvailability();
    };

    // STOP-ONLY style changes.  The tab asks before it acts on a click, and
    // says so in the pill when the answer is no — see StylesTab::onGridButtonClicked.
    stylesTab.onIsPlaying = [this] { return processor.isStylePlaying(); };

    // Startup auto-arm veto: only arm when nothing has loaded a style yet.
    stylesTab.onNeedsStyle = [this] { return ! processor.hasStyle(); };

    // THE PAGER AND THE PAGE READOUT ARE BACK, INSIDE THE TAB.  They live in
    // StylesTab's top row now rather than on a header line of their own, so
    // nothing here wires them - see StylesTab::resized.
    //
    // THE STYLE DATA POPUP IS STILL GONE.  StyleDataWindow.h sits in the old
    // Grex tree and has not been brought across; it is a metadata inspector for
    // a style file, which is a developer tool rather than something a player
    // reaches for mid-set.  If it comes back it comes back as its own decision.

    // ── Style search — A REAL WINDOW, BECAUSE THE DAW OWNS THE KEYBOARD ──────
    //
    // The tab's SEARCH button opens StyleSearchWindow; everything else about
    // searching happens in there. See StyleSearchWindow.h for why it has to be a
    // top-level OS window and not a panel or a CallOutBox.
    //
    // ALREADY-OPEN IS A RAISE, NOT A SECOND WINDOW. Pressing SEARCH twice used
    // to be the sort of thing that leaves two windows fighting over one history
    // file; this brings the existing one forward and puts the caret back in the
    // box instead.
    stylesTab.onSearchWindowRequested = [this]
    {
        if (styleSearchWindow != nullptr)
        {
            styleSearchWindow->setVisible (true);
            styleSearchWindow->toFront (true);
            styleSearchWindow->getContent().focusSearchBox();
            return;
        }

        styleSearchWindow = std::make_unique<Betel::StyleSearchWindow>();
        auto& c = styleSearchWindow->getContent();

        c.onSearch = [this] (const juce::String& kw)
        {
            const auto results = processor.searchStyles (kw);
            juce::StringArray names, refs;
            names.ensureStorageAllocated ((int) results.size());
            refs .ensureStorageAllocated ((int) results.size());
            for (const auto& r : results) { names.add (r.displayName); refs.add (r.absolutePath); }

            if (styleSearchWindow != nullptr)
                styleSearchWindow->getContent().setResults (names, refs, kw);
        };

        // THE TAB DOES THE SELECTING, not this lambda. selectStyleByRef runs the
        // same path a real cell click takes - stop-only refusal, auto-arm
        // cancel, onStyleSelected - so choosing from the window and choosing
        // from the grid cannot drift into behaving differently.
        c.onResultChosen = [this] (const juce::String& ref)
        {
            if (! stylesTab.selectStyleByRef (ref) && styleSearchWindow != nullptr)
                styleSearchWindow->getContent()
                    .setStatus ("That style is no longer in the library.");
        };

        c.onIsPlaying = [this] { return processor.isStylePlaying(); };

        styleSearchWindow->onClose = [this] { styleSearchWindow.reset(); };
    };

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

        // THE LIBRARY, not the folder. The folder holds .grxbld blobs now, so
        // a directory scan for *.sty found nothing and reported "no styles
        // found" while the browser was listing all 898 from those same blobs.
        // THE TEMPLATE IS A LIVE SNAPSHOT, taken the same way a save is taken.
        //
        // The baker is headless and cannot ask the engine anything, so without
        // this it can only write <StyleSlots> — and applySetPayload treats every
        // absent block as "leave it alone", which is why a freshly baked set
        // inherited the previous style's mixer, crash, jumps and levels instead
        // of describing an instrument of its own.
        //
        // Compiling a table of defaults into SetBaker would have been a second
        // copy of buildSetSnapshot that drifts the first time a parameter is
        // added.  One capture path instead: whatever a SAVE writes, a BAKE
        // writes.
        //
        // NOTE FOR ANYONE READING A SUPPORT REPORT: the session this is taken
        // from becomes the baseline for every set the run touches.
        if (! setBaker->start (processor.getStyleLibrary(), m.getSetsFolder(),
                               overwrite, buildSetSnapshot()))
            setEditorTab.setBakeResult ("no styles in the library", false);
        else
            setEditorTab.setBakeProgress (0, setBaker->total());
    };

    setEditorTab.onCancelBake = [this] { if (setBaker) setBaker->cancel(); };

    // ── SettingsTab MIDI ASSIGNING page -> the map file ──────────────────────
    //
    // The CC LEARN / FORGET wiring that used to be here is gone with the rows it
    // drove.  Assignment is drag-and-drop now, and a second modal way of doing
    // it - one that could only reach five fixed targets - could only ever
    // disagree with the real one.  The engine keeps those five CC targets and
    // their defaults; what moved is only how they get changed.
    //
    // Both buttons call the SAME helpers the canvas's right-click menu uses, so
    // there is one save path and one load path however you reach them.
    settingsTab.onSaveMidiMap    = [this] { promptSaveMidiMap();    };
    settingsTab.onLoadMidiMap    = [this] { promptLoadMidiMap();    };
    settingsTab.onUnlearnAllMidi = [this] { promptUnlearnAllMidi(); };

    // The REGISTRATION page changed the licence - push the answer into the
    // header lamp and its status flag, which keep their own copy of it.
    settingsTab.onRegistrationChanged = [this] { refreshRegistrationIndicator(); };
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

    soundsTab.onGetInstrumentName = [this] (int flag)
    {
        return processor.getInstrumentName (flag);
    };

    soundsTab.onGetPackInstruments = [this] (int pack, const juce::String& category)
    {
        return processor.getPackInstruments (pack, category);
    };

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
    // Identical handling to the play-control row's FUNKEY button — deliberately
    // so: the two surfaces are two views of the same flag, and
    // refreshMacroButtons() relights both, so flipping either moves the other.
    settingsTab.onFunkeyModeToggled = [this] (bool on)
    {
        Betel::GlobalMacros::get().setFunkeyOn (on);
        soundsTab.refreshMacroFx();
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

    // STYLE BOOST is global: the processor stores it, writes grex_boost.xml and
    // pushes it to the style bus in one call.
    settingsTab.onStyleBoostChanged = [this] (float db)
    {
        processor.setStyleBoostDb (db);
    };

    // RESET SOLO BASE GAIN.  The panel has already asked and been told yes, so
    // this just does it — and then SAYS SO.  A batch that rewrites a few hundred
    // files and returns in silence reads exactly like a button that does
    // nothing, which is the same complaint the .ins save receipt exists for.
    settingsTab.onResetSoloBaseGain = [this]
    {
        const int n = processor.resetAllSoloBaseUnity();

        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "Reset solo base gain",
            n > 0 ? juce::String (n) + (n == 1 ? " instrument preset was reset to 0 dB."
                                               : " instrument presets were reset to 0 dB.")
                  : juce::String ("Nothing to do - every solo instrument was already at 0 dB."));
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

    // ── RECORD, from the panel's own split button ────────────────────────────
    //
    // ONE BUTTON, THREE STATES, and pressing it means the opposite thing in
    // each: idle arms, armed disarms, recording stops and saves. A separate
    // stop button would be dead two thirds of the time.
    addAndMakeVisible (btnRecord);
    addAndMakeVisible (btnSongPlayer);

    // Indicators only.  They sit above the buttons rather than over them, but a
    // lamp that swallowed a click would be a dead spot in the artwork if the
    // areas were ever nudged into each other.
    ledSongRecord.setInterceptsMouseClicks (false, false);
    ledSongPlay  .setInterceptsMouseClicks (false, false);
    addAndMakeVisible (ledSongRecord);
    addAndMakeVisible (ledSongPlay);

    setRecordState (RecordState::Idle);
    setSongActive  (false);

    btnRecord.onClick = [this]
    {
        auto& rec = processor.getSongRecorder();

        if (rec.isRecording())
        {
            rec.stop();
            setRecordState (RecordState::Idle);
            promptSaveSong();
            return;
        }

        if (rec.isArmed())                    // armed but never latched - cancel
        {
            rec.stop();
            setRecordState (RecordState::Idle);
            return;
        }

        processor.getSongPlayer().stop();
        rec.arm (captureSongSetup());
        setRecordState (RecordState::Armed);
    };

    btnSongPlayer.onClick = [this] { openSongWindow(); };

    // A song's embedded set is applied through the SAME path a set file takes,
    // so nothing about a song load is a special case downstream.
    processor.onApplySetTree = [this] (const juce::ValueTree& setTree)
    {
        if (setTree.isValid()) applySetPayload (setTree);
    };

    // Fresh state whenever the host asks for it. Without this, a project saved
    // with the window open would store whatever the state was at the last window
    // CLOSE - which for someone who never closes it means the state at startup.
    processor.onCaptureSetTree = [this] { return buildSetSnapshot(); };

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
        // THE ROOT THE USER ACTUALLY CHOSE, not a hard-coded folder name.  Grex
        // wrote "Documents/Grex VSTI" here as a literal, which is a half-truth
        // - the root is relocatable, and a player who moved it would have had
        // songs saved somewhere they never chose.
        const auto dir = processor.getFolderManager().getRootFolder();
        auto* chooser = new juce::FileChooser ("Save song",
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
        const auto dir = processor.getFolderManager().getRootFolder();
        auto* chooser = new juce::FileChooser ("Load song", dir, "*.grxsong");
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

    // FUNKEY MODE's own stage — STYLE INSTRUMENT -> FUNKEY -> CHANNEL EFFECTS.
    // The tab sends the FAMILY PRESET (or nullptr for bypass); the conversion to
    // the engine struct happens here, the one place that legitimately knows both
    // types.
    soundsTab.onFunkeyFxChanged = [this, engineChannelFor]
        (int slot, bool isSolo, const Betel::FamilyFxParams* fam)
    {
        Betel::Channel::FunkeyFx fk;          // all-off by default = bypass
        if (fam != nullptr)
            Betel::GlobalMacros::toFunkeyFx (*fam, fk);

        processor.setChannelFunkeyFx (engineChannelFor (slot, isSolo), fk);
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

    // RESTORE THE STYLE'S OWN VOICE on a style slot. clearSlotSubstitution
    // drops the lock, hands the bank identity back to the style, invalidates
    // the redundancy triple so the next authored PC is genuinely re-applied,
    // and reloads the slot's setup voice immediately - so it takes effect on
    // the spot rather than at the next program change.
    soundsTab.onRestoreStyleInstrument = [this] (int slot)
    {
        processor.clearSlotSubstitution (slot);

        // The tab caches the patch it thinks the slot holds; without this the
        // cell grid and the button captions would keep showing the pick that
        // was just given up.
        // getSlotStyleFlag is the STYLE'S OWN voice for that slot - the exact
        // value clearSlotSubstitution just re-selected on the engine. -1 means
        // no style is loaded, in which case there is nothing to show and the
        // cached patch is left alone.
        const int styleFlag = processor.getSlotStyleFlag (slot);
        if (styleFlag >= 0)
            soundsTab.setSlotPatch (false, slot, styleFlag);
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

    // ── THE SECTION FX BUS, SEEN FROM THE SOUND EDITOR ───────────────────────
    //
    // SoundsTab owns the editor and knows which slot is open; the processor owns
    // the two buses.  These two hooks join them, and they are the entire reason
    // the REVERB and DELAY pages can show one shared effect from every slot
    // without EffectsPanel knowing a bus exists.
    //
    // isSolo IS the section: the right hand is the 8 solo channels, everything
    // else - style melodic, bass, drums and perc - is the left.
    // ── GLOBAL EFFECTS ───────────────────────────────────────────────────────
    //
    // SoundsTab knows which hand is showing; the processor owns the two racks.
    // This joins them, and it is the only place the window is created — lazily,
    // because most sessions never open it and an EffectsPanel is not cheap.
    //
    // ONE window for both hands, retargeted by setSection() rather than
    // duplicated: two would drift apart in size, page and position, and the
    // player would have to learn which was which.
    soundsTab.onOpenGlobalEffects = [this] (bool isSolo)
    {
        const int section = isSolo ? 1 : 0;

        if (globalFxWindow == nullptr)
        {
            globalFxWindow = std::make_unique<GlobalEffectsWindow> ("GLOBAL EFFECTS");
            auto& c = globalFxWindow->getContent();

            c.onRead = [this] (int sec, Betel::FamilyFxParams& p)
                { Betel::SectionFxBridge::read (processor.sectionFx (sec), p); };

            c.onWrite = [this] (int sec, const Betel::FamilyFxParams& p)
                { Betel::SectionFxBridge::write (p, processor.sectionFx (sec)); };

            // EARLY REFLECTIONS, on their own pair of hooks.  They do not ride
            // FamilyFxParams because that struct is shared with SlotParams and
            // the Funkey families, none of which have an ER stage - see the note
            // on SectionFxBridge::readEr.
            c.onReadEr = [this] (int sec, float& erMix, float& erSize)
                { Betel::SectionFxBridge::readEr (processor.sectionFx (sec), erMix, erSize); };

            c.onWriteEr = [this] (int sec, float erMix, float erSize)
                { Betel::SectionFxBridge::writeEr (erMix, erSize, processor.sectionFx (sec)); };

        }

        globalFxWindow->setName (isSolo ? "GLOBAL EFFECTS - RIGHT HAND"
                                        : "GLOBAL EFFECTS - LEFT HAND");
        globalFxWindow->getContent().setSection (section);
        globalFxWindow->setVisible (true);
        globalFxWindow->toFront (true);
    };


    soundsTab.onSlotGainChanged = [this, engineChannelFor]
        (int slot, bool isSolo, float gainPercent, float baseUnityDb)
    {
        // THE BASE ARRIVES WITH THE GAIN, and is no longer looked up here.
        //
        // Two bugs lived in that lookup. The first was the 2-argument form,
        // which defaulted the base to 0 and wiped the calibration a moment
        // after the dialog set it. The second outlived the fix: the 3-argument
        // form called getChannelBaseUnityDb, which resolves through the .ins —
        // and a melodic STYLE slot has no .ins, so it quietly returned 0 dB and
        // the SET's own base never reached the engine at all. The percent came
        // back on a set load and its partner did not.
        //
        // SoundsTab now answers "whose base is this" once, in
        // effectiveBaseUnityDb, and sends the answer. The host's job is to push
        // the pair, not to re-derive half of it from a file a style slot is not
        // allowed to consult.
        processor.setChannelInstrumentGainPercent (engineChannelFor (slot, isSolo),
                                                   gainPercent, baseUnityDb);
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

    // EDM KIT pad LEDs: keys hit plus synth voices still ringing.
    soundsTab.onQueryPadActivity = [this, engineChannelFor]
        (int slot, bool isSolo, uint32_t* mask)
    {
        if (mask == nullptr) return;
        processor.getPadActivity (engineChannelFor(slot, isSolo), mask);
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

        // AND THE SOUND EDITOR FOLLOWS.  Solo is the one place the two are the
        // same number - the style never writes a solo slot - so the fader and
        // the editor's GAIN are two views of one value rather than two
        // authorities over it. SoundsTab's own guard stops the return hop.
        soundsTab.setSoloGainFromMixer (slot, linearGain);
    };
    // ...and the other direction: the editor's GAIN moves the mixer fader.
    soundsTab.onSoloGainToMixer = [this] (int slot, float linearGain)
    {
        processor.setChannelVolume (Betel::SamplePlayerEngine::kNumStyleChannels + slot,
                                    linearGain);
        mixerTab.setSoloSlotGain (slot, linearGain);
    };

    mixerTab.onMasterGainChanged = [this](float linearGain)
    {
        processor.setMasterVolume (linearGain);
        knobMasterVol.setValue ((double) linearGain * 100.0, false);   // silent
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
            "0 dB is the level Grex ships with.",
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

    // STYLE VOLUME base unity - the same gesture as SOLO VOLUME above, but the
    // answer goes somewhere else: PER SET, in MixerState, because it says how
    // loud the band should sit for THIS song.  The solo base stays global
    // because it describes the player's two hands, which no song changes.
    //
    // The fader does NOT move - same rule as the solo base and the crash box.
    // ── PER-SLOT POST BASE TRIM, left double-click on a style fader ──────────
    //
    // The blunt instrument for a style part sent at 12 or at 127, where the
    // fader alone cannot reach. POST, like everything else on this side: it
    // multiplies what is heard and never touches what the style sent.
    mixerTab.onStyleSlotBaseDbRequested = [this] (int slot)
    {
        if (slot < 0 || slot >= 8) return;

        const float base = processor.getChannelUserBaseDb (slot);

        auto* w = new juce::AlertWindow (
            "SLOT BASE TRIM",
            "A fixed dB trim on this style slot, applied AFTER the style's own\n"
            "level and after the fader - for a part sent far too loud or too quiet.\n"
            "It does not change what the style sends. Saved WITH THE SET, and\n"
            "reset to 0 dB when a style loads. Range: -40 to +40 dB.",
            juce::MessageBoxIconType::NoIcon);

        w->addTextEditor ("db", juce::String (base, 2), "dB:");
        w->addButton ("SET",    1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, w, slot] (int result)
            {
                std::unique_ptr<juce::AlertWindow> owned (w);
                if (result != 1) return;

                processor.setChannelUserBaseDb (
                    slot, juce::jlimit (-40.0f, 40.0f,
                                        w->getTextEditorContents ("db").getFloatValue()));
            }), false);
    };

    mixerTab.onStyleBaseUnityRequested = [this]
    {
        const float base = processor.getStyleBaseUnityDb();

        auto* w = new juce::AlertWindow (
            "STYLE BASE UNITY",
            "What the STYLE VOLUME fader's unity detent (127) is worth, in dB.\n"
            "Use it to set how loud the band sits for this song.\n"
            "Saved WITH THE SET, unlike the solo base. Range: -24 to +24 dB.\n"
            "0 dB is the level Grex ships with.",
            juce::MessageBoxIconType::NoIcon);

        w->addTextEditor ("db", juce::String (base, 2), "dB:");
        w->addButton ("SET",    1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, w] (int result)
            {
                std::unique_ptr<juce::AlertWindow> owned (w);
                if (result != 1) return;

                processor.setStyleBaseUnityDb (
                    juce::jlimit (BetelgeuseProcessor::kMinStyleBaseUnityDb,
                                  BetelgeuseProcessor::kMaxStyleBaseUnityDb,
                                  w->getTextEditorContents ("db").getFloatValue()));
            }), false);
    };

    // ── MIDI ASSIGNMENT ──────────────────────────────────────────────────────
    //
    // NO FILE YET MEANS A NEW INSTALL, so the historic control-note table gets
    // seeded as real entries and written out. From then on the file is the only
    // authority - including when it is deliberately empty, because CLEAR ALL
    // has to be able to mean cleared. Seeding on every launch instead would
    // resurrect thirty-five assignments the player had just removed.
    {
        const auto mapFile = Betel::GrexPaths::root().getChildFile ("grex_remote_map.xml");
        if (mapFile.existsAsFile())
        {
            Betel::RemoteMap::get().load (mapFile);
        }
        else
        {
            Betel::RemoteMap::get().seedFactoryDefaults();
            Betel::RemoteMap::get().save (mapFile);
        }
    }

    remoteHub.isArmed        = [this] { return armedKind != Betel::RemoteMap::Kind::None; };
    remoteHub.getArmedSource = [this] { return std::make_pair (armedKind, armedNumber); };

    midiLinkCanvas.getLastMidiPacked = [this] { return processor.getLastMidiPacked(); };
    midiLinkCanvas.onSourceArmed     = [this] (Betel::RemoteMap::Kind k, int n)
    {
        setArmedMidiSource (k, n);
    };
    midiLinkCanvas.onMenuRequested   = [this] { showMidiMapMenu(); };
    addAndMakeVisible (midiLinkCanvas);

    remoteHub.onAssignRequested = [this] (Betel::RemoteId id, juce::Component& over,
                                          Betel::RemoteMap::Kind k, int number)
    {
        openAssignPopup (id, over, k, number);
    };

    attachRemoteControls();

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
        // Library name first - the third and last place a GM program lookup was
        // naming World and Oriental sounds after whatever GM keeps at the same
        // program number.
        const auto libName = processor.getInstrumentName (patch);
        const juce::String name = libName.isNotEmpty()
                                    ? libName
                                    : SoundsTab::instrumentNameForPatch (patch);
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

        // The harmony slot's INSTRUMENT, which the line above does not carry -
        // see seedHarmonyInstrument. Harmless when a set follows and sends its
        // own; the point is the case where none does.
        soundsTab.seedHarmonyInstrument();

        // ── WHAT THE FIRST WINDOW OPENS INTO ─────────────────────────────
        //
        // PROJECT STATE WINS. The host calls setStateInformation before the
        // editor exists, so by the time we get here the snapshot is already
        // waiting - and reopening a saved project into somebody's default.bset
        // instead of into the song they saved is the same bug the empty stubs
        // were, only politer about it.
        //
        // default.bset stays as the canonical startup default for every other
        // case: a fresh instance, or a host that saved no state.
        if (processor.hasProjectState() && processor.getEditorSnapshot().isValid())
            applySetPayload (processor.getEditorSnapshot());

        // ── AND THEN GUARANTEE A STYLE, WHICHEVER PATH RAN ───────────────
        //
        // THIS IS NOT BELT AND BRACES, IT IS THE FIX FOR A TRAP I SHIPPED.
        //
        // The branch above used to be an `else`, so project state REPLACED
        // default.bset. The moment a host saved a snapshot taken before a style
        // was loaded, that styleless snapshot came back on every subsequent
        // open, default.bset was never consulted again, and Grex had no style
        // at all - at which point PLAY does nothing, because togglePlayStop is
        // `else if (hasStyle())`. No start, no lamp, no text change, and a
        // hardware button hits the same wall because both go through that one
        // function.
        //
        // Self-perpetuating too: with no style there was nothing better to save
        // next time either.
        //
        // Asking hasStyle() afterwards makes it a GUARANTEE rather than a hope -
        // it does not care which path ran or why one failed, only that the
        // instrument ends up playable.
        if (! processor.hasStyle())
        {
            const auto defaultSet = processor.getFolderManager()
                                        .getSetsFolder().getChildFile ("default.bset");
            if (defaultSet.existsAsFile())
                applySetFromFile (defaultSet);
        }

        // ── AND IF default.bset DID NOT DELIVER ONE EITHER ───────────────
        //
        // It very often cannot: default.bset is a FACTORY set, its StyleLink is
        // a path relative to <root>/styles, and since the library moved into
        // .grxbld blobs there is no file at the end of that path.  The set
        // applies, every level and voicing in it lands, and no style loads -
        // silently, because applySetPayload's StyleLink branch discards the
        // error.  Those sets need re-baking; until they are, this is what
        // stands between the user and a plugin that cannot play.
        //
        // Asking a SECOND time is the point.  The block above answers "did the
        // snapshot give us a style"; this answers "did anything at all", and
        // only the second question is the one the transport cares about.
        //
        // Immediate, not deferred: the 1 s auto-arm below is a backstop for a
        // browser that has not finished populating, and a backstop that any
        // restoreSelection could cancel is not a guarantee.
        if (! processor.hasStyle())
            stylesTab.selectFirstStyle();

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
    // -1 until multi split says otherwise: no purple band on a two-zone
    // keyboard, where it would mark a boundary that does nothing.
    keyboardComponent.setBassSplitPoint (-1);
    keyboardComponent.setOctaveForMiddleC(4);
    addAndMakeVisible(keyboardComponent);

    // ── Right panel: Split knob ───────────────────────────────────────────────
    knobSplit.onChange = [this](double val)
    {
        keyboardComponent.setSplitPoint((int)val);
        processor.setSplitPoint((int)val);
    };
    // SEED THE KNOB FROM THE PROCESSOR, NEVER THE PROCESSOR FROM THE KNOB.
    //
    // These two lines used to read the other way round:
    //     processor.setSplitPoint ((int) knobSplit.getValue());
    // and knobSplit is constructed as { 36.0, 127.0, 60.0 }, so its value at
    // this point is ALWAYS the hard-coded 60 - it has never been told what the
    // player actually saved.  The seed further up this constructor had already
    // loaded the real split out of MasterSettings, and this line then threw it
    // away and pushed 60 straight back through setSplitPoint, which persists.
    // So the saved split was destroyed on EVERY editor open, and re-saved as
    // 60: the value looked like it never stuck no matter how it was stored.
    //
    // notify = false because the knob and the processor already agree here.
    knobSplit.setValue ((double) processor.getSplitPoint(), false);
    keyboardComponent.setSplitPoint (processor.getSplitPoint());
    addAndMakeVisible(knobSplit);

    addAndMakeVisible(btnHold);
    btnHold.onClick = [this]
    {
        processor.getSequencer().setTransitionHold (btnHold.getToggleState());
    };
    // ── THE REGISTRATION LAMP IS LIVE IN GREX ────────────────────────────────
    //
    // Ballada hid all of this - it is free, so a permanently green lamp would
    // only prompt the question it exists to answer.  Grex is paid, so the lamp,
    // the flag under it and the line telling an unregistered player where to go
    // are all shown.
    //
    // THE LAMP SHARES THE FOLDER LOCATOR'S Y.  Two status lamps at two different
    // heights read as two unrelated parts; on one line they read as a row of
    // indicators, which is what they are.  See resized().
    //
    // THE LAMP IS DRIVEN BY THE LICENCE, not by anything on screen: it reads
    // RegistrationManager::isRegistered(), which is the same answer the audio
    // thread's demo mute reads.  SettingsTab's REGISTRATION page calls back into
    // refreshRegistrationIndicator() after every attempt, so the two never
    // disagree.
    addAndMakeVisible (ledRegistration);
    ledRegistration.setInterceptsMouseClicks (false, false);   // indicator only

    lblReg.setFont (juce::Font (13.0f, juce::Font::bold));
    lblReg.setJustificationType (juce::Justification::centredRight);
    lblReg.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (lblReg);

    lblRegHint.setFont (juce::Font (11.0f, juce::Font::italic));
    lblRegHint.setColour (juce::Label::textColourId, juce::Colour (0xFFCCCCCC));
    lblRegHint.setJustificationType (juce::Justification::centredRight);
    lblRegHint.setInterceptsMouseClicks (false, false);
    addChildComponent (lblRegHint);      // shown only while unregistered

    refreshRegistrationIndicator();
    addAndMakeVisible(ledFolderLocator);
    ledFolderLocator.setInterceptsMouseClicks (false, false);  // indicator only

    btnLocateFolder.setButtonText ("LOCATE SYSTEM FOLDER");

    // YELLOW FORE, BLACK TEXT.  This is the one control that means "the plugin
    // cannot find its library" - it has to be the loudest thing in the corner,
    // and the pool-blue palette has nothing that shouts.  Hence a literal here
    // rather than a Pal:: constant: it is deliberately OUTSIDE the accent
    // system, and deriving it from kBase would only tie it to a scheme it is
    // supposed to stand apart from.
    //
    // Black text, not white: on a bright yellow, white is barely legible while
    // black reads at better than 13:1.
    //
    // Only buttonColourId is set - JUCE's default LookAndFeel derives the hover
    // and pressed shades from it, so one colour covers all three states.  Both
    // text ids are set even though this is not a toggle, so the caption cannot
    // go white if it ever becomes one.
    {
        constexpr juce::uint32 kLocateYellow = 0xFFF2C200;

        btnLocateFolder.setColour (juce::TextButton::buttonColourId,
                                   juce::Colour (kLocateYellow));
        btnLocateFolder.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
        btnLocateFolder.setColour (juce::TextButton::textColourOnId,  juce::Colours::black);
    }

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

            // THE ONE CASE WHERE EVERYTHING WORKS AND STILL WILL NOT LAST.
            // The root is set and the library has loaded, so every other signal
            // on screen says success - but the locator did not reach disk, and
            // the next launch will come up on the default folder again. Said
            // here because there is no later moment at which it becomes
            // visible: by the time the user notices, they are relaunching and
            // this session is gone.
            //
            // Raised AFTER rescanStyleBrowser so it outranks that function's own
            // empty-library notice: if both are true, the reason it will not
            // stick is the more useful of the two.
            if (! m.isLocatorPersisted())
                stylesTab.showNotice ("FOLDER SET, BUT COULD NOT BE SAVED FOR NEXT TIME");
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

    // The search bar used to be shown and hidden here. It lives inside
    // StylesTab now, so it appears and disappears with the tab itself and needs
    // no rule of its own.
}

void MainComponent::refreshRegistrationIndicator()
{
    const bool reg = RegistrationManager::getInstance().isRegistered();

    ledRegistration.setRegistered (reg);

    lblReg.setText (reg ? "REGISTERED" : "NOT REGISTERED", juce::dontSendNotification);
    lblReg.setColour (juce::Label::textColourId,
                      reg ? juce::Colour (0xFF00CC00) : juce::Colour (0xFFE53935));

    // THE HINT IS SHOWN ONLY WHEN IT HAS SOMETHING TO SAY.  A permanent line
    // telling a registered player where to register is noise, and noise in a
    // header is what teaches people to stop reading it.
    lblRegHint.setText ("Please register in SETTINGS > REGISTRATION",
                        juce::dontSendNotification);
    lblRegHint.setVisible (! reg);
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
    // ── DELEGATED TO THE PROCESSOR ───────────────────────────────────────────
    //
    // The body that used to be here moved to BetelgeuseProcessor::
    // setFileForStyleRef, because the processor now has to find a style's set
    // WITHOUT an editor: CC 7 no longer seeds the faders when a set owns them,
    // so a project reopened with this window closed would otherwise have
    // nothing setting those levels at all.  See Main.h.
    //
    // Kept as a member rather than replaced at every call site so the editor
    // reads the same as it did, and so there is exactly one place that knows
    // sets are named by Betel::setFileNameForStyle.
    return processor.setFileForStyleRef (styleAbsolutePath);
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
    knobMasterVol.setValue (processor.getMasterVolume() * 100.0, false);
    energySlider .setValue ((double) processor.getStyleEnergy(),  false);
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

    auto payload = buildSetSnapshot();
    BetelStateXml::roundNumbersTo2Decimals (payload);    // 2 decimals, never "0.5999999642372131"
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
    //
    // THE BOOST IS NOT IN THAT TEMPLATE ANY MORE.  fromTree routes through
    // applyValues, which no longer writes the boost at all — so a set can
    // neither carry one nor reset one, whatever an older .bset happens to hold.
    // See the block above applyValues in StyleLevels.h for why the guarantee
    // lives there rather than at this call site.
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
        // styleId first, relPath second. A set baked since the blobs carries an
        // id; one from before carries a path. Preferring the id means a re-baked
        // set resolves instantly and an old one still gets its chance.
        const auto styleId = link.getProperty ("styleId", juce::String()).toString();
        const auto rel     = styleId.isNotEmpty()
                                ? styleId
                                : link.getProperty ("relPath", juce::String()).toString();
        auto& m = processor.getFolderManager();
        if (rel.isNotEmpty() && m.isRootFolderValid())
        {
            // A FACTORY SET'S StyleLink IS A RELATIVE PATH, and after the move
            // to blobs there is no file at the end of it. loadStyleByRef takes
            // it anyway - a set re-baked since the blobs will hold an ID here,
            // and an older one will simply fail to resolve, which is the honest
            // outcome: those sets need re-baking, and pretending otherwise would
            // load the wrong style rather than none.
            const auto f = m.getStylesFolder().getChildFile (rel);
            const auto ref = f.existsAsFile() ? f.getFullPathName() : rel;

            if (ref.isNotEmpty() && ref != processor.getLastLoadedStylePath())
            {
                juce::String err;
                if (processor.loadStyleByRef (ref, err))
                {
                    stylesTab.setSelectedStylePath (ref, displayNameForStyleRef (ref));
                    soundsTab.applySoundCalibration();
                }
                else
                {
                    // THE FAILURE USED TO GO NOWHERE.  `err` was filled in and
                    // dropped on the floor, so an orphaned factory-set link -
                    // the expected outcome for every set baked before the blobs
                    // - produced a set that applied perfectly and a plugin with
                    // no style, with nothing anywhere saying why.
                    //
                    // ON SCREEN rather than in grex_log.txt, so that "PLAY is
                    // broken" becomes "this set names a style that no longer
                    // resolves" without the player having to open a file.
                    stylesTab.showNotice ("SET LINKS A STYLE THAT NO LONGER RESOLVES");
                    juce::ignoreUnused (err);
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

    // The two shared FX buses.  Order does not matter here - nothing else reads
    // them - but they go in with the other engine-wide blocks rather than with
    // the per-slot state, because that is what they are.
    processor.applySectionFxState (payload.getChildWithName ("SectionFx"));

    // THE WINDOW MUST HEAR ABOUT THIS.  It re-reads the rack only when it is
    // shown; a set arriving while it is open would otherwise leave it showing
    // the previous state, and its next slider touch would write that stale
    // state — every enable included — straight back over what the set just
    // loaded.  See GlobalEffectsContent::refreshFromRack.
    if (globalFxWindow != nullptr && globalFxWindow->isVisible())
        globalFxWindow->getContent().refreshFromRack();
    refreshCrashFromProcessor();

    // ── MIXER, LAST ──────────────────────────────────────────────────────────
    //
    // AFTER the style load above, and that ordering is the whole point:
    // applyVoiceSetup writes every style channel fader from the style's own
    // CC 7, so a mixer restored any earlier would be overwritten a moment later
    // by the load it was supposed to override.  Restored here, the set's fader
    // positions are the last word — which is what IGNORE STYLE VOLUMES needs to
    // mean anything.
    // ── UNITY FIRST, THEN THE SET'S OWN FADERS ───────────────────────────────
    //
    // Adjacent to the restore ON PURPOSE.  The reset used to live in
    // adoptFreshStyle, where it ran on every style adopt - and a style gets
    // adopted more than once per user action, so a late adopt wiped a mixer the
    // set had already restored.  Here there is no window between the two: unity
    // clears the previous style's trims, and the set's values land immediately
    // after and win.
    //
    // It runs even when the set carries no MixerState, which is the case that
    // needs it most: applyMixerState treats an absent value as "leave it
    // alone", so without this an old set would silently inherit the last
    // style's faders.
    processor.resetUserVolumesToUnity();
    processor.applyMixerState (payload.getChildWithName ("MixerState"));
    refreshMixerFromProcessor();

    // The DUCKER travels inside MixerState, so its widgets are stale the
    // instant that returns.  Guarded because the window is built lazily — a
    // player who has never opened the Finisher still gets the set's ducking,
    // it just has no page to update yet.
    if (finisherWindow) refreshFinisherWindow();

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
        // never left), so this only puts the page, cell highlight and pill back
        // where they were.
        if (uiState.hasProperty ("styleName"))
            stylesTab.restoreSelection (uiState.getProperty ("styleName").toString());
    }

    // ── Re-sync the visible controls to the restored processor state ──────
    // Knobs / selectors notify so engine + UI converge through one code path
    // (their handlers are idempotent); MainTab toggles are display-only here.
    // knobSplit is deliberately ABSENT.  The set no longer carries a split, so
    // there is nothing here to re-sync to: the knob already shows the player's
    // own split and must keep showing it across a set load.  Re-seeding it from
    // the processor would read the same value back and change nothing today,
    // but it would put a writer for a MASTER-owned value inside the set-load
    // path, which is exactly where this went wrong before.
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
    payload.appendChild (processor.captureSectionFxState(), nullptr);
    // ── <SfzParams> IS NO LONGER WRITTEN ─────────────────────────────────────
    //
    // It was 698 KB of EVERY set - 41% of the file - and byte-identical in all of
    // them. Measured across three saved sets: two different styles and an edited
    // vs unedited pair all produced the same sha. It varies with nothing.
    //
    // The reason is not a bug. `sfzStyleParams` / `sfzSoloParams` are a SECOND,
    // parallel set of SlotParams that governs a slot only while an SFZ is the
    // awake source on it. Nothing populates them until someone edits a slot in
    // that state, so in a library where no SFZ has ever been loaded they stay at
    // their constructed defaults forever - and 898 sets carried the same 626 MB
    // of them between them.
    //
    // READING IS DELIBERATELY LEFT IN PLACE (applySfzParams still runs on the
    // load path), so every set already on disk keeps working exactly as before.
    // This line stops NEW sets carrying the block; it does not invalidate old
    // ones.
    //
    // IF SFZ SLOTS EVER MATTER, PUT THIS BACK - it is one line, and the capture
    // and apply sides are both still here and still correct:
    //     payload.appendChild (soundsTab.captureSfzParams(), nullptr);
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
        // THE FUNKEY MIX belongs to the set too - that is its whole point: each
        // style keeps its own blend.  Stored as the slider shows it, 0..100.
        u.setProperty ("funkeyMix",  juce::roundToInt (Betel::GlobalMacros::get().funkeyMix() * 100.0f),
                       nullptr);
        u.setProperty ("stylesPage", stylesTab.getSelectedPage(), nullptr);
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

    // ── CLEAR EVERY CALLBACK THAT CAPTURED `this` ────────────────────────────
    //
    // The PROCESSOR outlives the editor, and these two hold `this`. A host that
    // saves its project after the window is closed - which is exactly what
    // happens when you shut the plugin window and then hit Save - would call
    // straight into a destroyed object.
    //
    // Cleared AFTER the snapshot above, so the last thing they do is the thing
    // they were for. getStateInformation then falls back to that snapshot, which
    // is what it is designed to do with no editor present.
    processor.onCaptureSetTree = nullptr;
    processor.onApplySetTree   = nullptr;
}

void MainComponent::rescanStyleBrowser()
{
    // ONE FLAT LIST PLUS A PARALLEL CATEGORY ARRAY, NOT A LIST PER CATEGORY.
    //
    // The tab does its own filing: it is the thing that knows which folder is on
    // screen and which page of it, so handing it the whole library once and a
    // category per style is both fewer calls and one authority for the split.
    //
    // The genre loop is still walked rather than replaced by a straight
    // allEntries() call, because getStylesInGenre is what SORTS — and a browser
    // whose page 1 is alphabetical only by accident of directory order is a
    // browser nobody can find anything in.
    juce::StringArray names, paths, groups;

    for (const auto& genre : processor.getStyleGenres())
        for (const auto& e : processor.getStylesInGenre (genre))
        {
            names .add (e.displayName);
            paths .add (e.absolutePath);
            groups.add (genre);
        }

    stylesTab.setLibrary (names, paths, groups);

    // AN EMPTY LIBRARY IS SAID OUT LOUD, not just written to a file.  This runs
    // after a relocate as well as at startup, so pointing the locator at a
    // folder with no blob in it now answers on screen in the moment rather than
    // leaving a browser that is blank for no stated reason.
    if (names.isEmpty())
        stylesTab.showNotice ("NO STYLES - CHECK THE SYSTEM FOLDER");
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

// runStyleSearch lived here. It read a keyword, called processor.searchStyles
// and pushed the hits into the tab's grid. All three moved into the search
// window's onSearch lambda in the constructor, because the results are shown
// there now and a host-side function that only one lambda called was one more
// place for the two to disagree.

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
// FUNKEY FOLLOWS THE SET, AND AN ABSENT ANSWER MEANS OFF.
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

    // THE FUNKEY MIX, by the same rule: what the set says, and the default when
    // it says nothing - an older set, a set with no UiState, or no set at all.
    {
        const int mix100 = juce::jlimit (0, 100, (int) uiState.getProperty (
            "funkeyMix", juce::roundToInt (Betel::GlobalMacros::kFunkeyMixDefault * 100.0f)));
        processor.setFunkeyMix ((float) mix100 / 100.0f);
        mainTab.setFunkeyMixValue ((float) mix100);
    }

    refreshMacroButtons();          // relight BOTH pairs (left panel + Settings)
    soundsTab.refreshMacroFx();     // and make it audible, not merely unlit
}

void MainComponent::refreshMacroButtons()
{
    const auto& gm = Betel::GlobalMacros::get();
    mainTab.setFunkeyVisual (gm.isFunkeyOn());

    // The GLOBAL SETTINGS page carries the same switch.  Relighting it here —
    // rather than at each call site — is what keeps the play-control row and the
    // settings page from ever disagreeing, including after a set restore.
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
    selectorSpeed     .setBounds(scaleRect ({ 240, 104, 123,  91 }));
    // 378 -> 376.5: THE WHOLE GROUP SAT RIGHT OF ITS PAINTED PANEL, not just
    // the fore.  Measured off a screen capture, the gold fill AND the SYNCED
    // caption both centred 1.5 device px right of the panel's own centre,
    // while the SPEED group beside it centred to within half a pixel - so the
    // artwork and the bounds disagreed by about 1.5 design px and the fix is
    // the rect, not the fill.  Float bounds, because the correction is smaller
    // than one design pixel and rounding it away would put it straight back.
    selectorTempoType .setBounds(scaleRectF({ 376.5f, 104.0f, 123.0f, 91.0f }));

    // ── Left panel: Row 2 ────────────────────────────────────────────────────
    // ROW 2, to Rob's measurements.  He gave them as w,x,h,y; scaleRect takes
    // x,y,w,h, hence the reordering:
    //     TAP    w121 x105 h67 y204      ends at 226
    //     RESET  w123 x240 h67 y204      ends at 363
    //     CANVAS w123 x377 h67 y204      ends at 500
    // 14 px between each, and the row finishes flush with the panel.
    btnTap            .setBounds(scaleRect({ 105, 204, 121,  67 }));
    btnResetTempo     .setBounds(scaleRect({ 240, 204, 123,  67 }));
    midiLinkCanvas    .setBounds(scaleRect({ 377, 204, 123,  67 }));

    // ── RECORD SONG / PLAY SONG ──────────────────────────────────────────────
    //
    // ONE PAINTED PILL IN back.png, SPLIT LEFT AND RIGHT.  Left half RECORD,
    // right half PLAY.  The buttons are InvisibleButton and draw nothing at all:
    // the pill, both captions and the divider between them are already in the
    // artwork, so anything drawn here would be a second button on top of it.
    //
    // The two lamps sit ABOVE the pill, each roughly over the half it reports on
    // - the round one over RECORD, the triangle over PLAY - in the windows the
    // artwork leaves for them.
    //
    // All four rects come straight from the Photoshop file, in the (w, x, h, y)
    // order they were given in.  They are the only numbers to change if the
    // artwork moves; nothing below is derived from anything else.
    {
        constexpr float kPillW = 262.0f,  kPillX = 239.5f,  kPillH = 38.0f,  kPillY = 771.5f;
        constexpr float kRecW  =  11.0f,  kRecX  = 299.0f,  kRecH  = 11.0f,  kRecY  = 756.0f;
        constexpr float kPlayW =  12.0f,  kPlayX = 435.22f, kPlayH = 11.16f, kPlayY = 754.09f;

        // Split on the exact centre in DESIGN units and round once, so the two
        // halves cannot drift apart by a pixel at some window sizes and not at
        // others - which is what splitting after scaling would do.
        const float halfW = kPillW * 0.5f;

        btnRecord    .setBounds (scaleRectF ({ kPillX,         kPillY, halfW, kPillH }));
        btnSongPlayer.setBounds (scaleRectF ({ kPillX + halfW, kPillY, halfW, kPillH }));

        ledSongRecord.setBounds (scaleRectF ({ kRecX,  kRecY,  kRecW,  kRecH  }));
        ledSongPlay  .setBounds (scaleRectF ({ kPlayX, kPlayY, kPlayW, kPlayH }));
    }

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
    //
    // ROWS 4 AND 5 ARE NOW DELIBERATELY EMPTY apart from the knob. The CHORD
    // readout that was at { 363, 443, 127, 46 } and the STYLE name that was at
    // { 246, 517, 245, 39 } both moved to the header - see the header block at
    // the end of this function. Left clear on purpose, for the features that
    // need the room; do not backfill it with something small.
    // Rob's coords are w,x,h,y; scaleRectF takes x,y,w,h.  One row - y 446,
    // h 121 - three controls left to right, none of them touching:
    //     semitone  w 64.48  x 100.75    -> 100.75 .. 165.23
    //     features  w 263    x 177       -> 177.00 .. 440.00   (see below)
    //     energy    w 61     x 448       -> 448.00 .. 509.00
    // The semitone box SHRINKS from 123 wide to free that strip.
    knobGlobalSemitone.setBounds (scaleRectF ({ 100.75f, 446.0f, 64.48f, 121.0f }));
    energySlider      .setBounds (scaleRectF ({ 448.00f, 446.0f, 61.00f, 121.0f }));

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

    // ── Left panel: Row 7 - THE SET AND CHORD READOUTS ───────────────────────
    //
    // The pill was 287.98 wide and held the set name alone.  It NARROWS to
    // 187.34 and the chord takes the strip that frees on its right, which is
    // what replaces the two header readouts.
    //
    // TEXT ONLY - both frames are painted into main_back.png, so nothing is
    // drawn here.  BetelTextLabel already draws Justification::centred, so
    // "aligned to the centre" needs no extra work.
    //
    // Rob's coords are w,x,h,y; scaleRectF takes x,y,w,h.  scaleRectF rather
    // than scaleRect so the fractions survive to ONE rounding at the end, the
    // same reason the kSetTrio* block above uses it.
    //     set    w187.34  x108.84  h52.02  y684
    //     chord  w 89.53  x306.72  h52.02  y684
    lblSetName    .setBounds (scaleRectF ({ 108.84f, 684.0f, 187.34f, 52.02f }));
    lblPlayedChord.setBounds (scaleRectF ({ 306.72f, 684.0f,  89.53f, 52.02f }));

    // The two artwork frames beneath the set manager are EMPTY now: Big Drums
    // was removed and Funkey moved to the play-control row.  Left free rather
    // than filled with the next thing that needs a home - a control put
    // somewhere because a rectangle existed is how a panel stops making sense.
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

        // NINE SLOTS FOR EIGHT BUTTONS: the last one is the MASTER VOLUME knob.
        //
        // Sized as one more tab rather than from a hand-measured rectangle, so
        // the knob stays aligned with the row at every window scale and a ninth
        // tab could still be added without re-measuring anything - the divisor
        // is the only number that would change.
        const int   slots = kNumTabs + 1;
        const float btnH  = canvasH - 2.0f * kEdgePad;
        const float btnY  = canvasY + (canvasH - btnH) * 0.5f;   // vertical middle
        const float btnW  = (canvasW - 2.0f * kEdgePad
                             - (float) (slots - 1) * kBtnGap) / (float) slots;

        for (int i = 0; i < kNumTabs; ++i)
        {
            const float tx = canvasX + kEdgePad + (float) i * (btnW + kBtnGap);
            tabButtons[i].setBounds (scaleRectF (juce::Rectangle<float> { tx, btnY, btnW, btnH }));
        }

        const float mx = canvasX + kEdgePad + (float) kNumTabs * (btnW + kBtnGap);
        knobMasterVol.setBounds (scaleRectF (juce::Rectangle<float> { mx, btnY, btnW, btnH }));
    }

    // ── Right panel: Tab content canvas ──────────────────────────────────────
    auto canvasBounds = scaleRect({ 585, 289, 918, 435 });
    tabContentContainer.setBounds(canvasBounds);

    for (auto* page : tabPages)
        page->setBounds(tabContentContainer.getLocalBounds());

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
    // ── FOLDER LOCATOR: LED ON TOP, BUTTON UNDERNEATH ────────────────────────
    //
    // Moved to the header's TOP-LEFT corner.  Both are LEFT-aligned on the same
    // x now - the LED on top, the button directly beneath it with one LED-height
    // of air between.  The gap is written as kLed rather than as 14 so it stays
    // a "LED size" gap if the LED is ever resized.
    //
    // ANCHORED TO THE HEADER, NOT TO ABSOLUTE NUMBERS.  kHdrX/kHdrY mirror the
    // headerAnimation rect above, so if the banner is ever moved or resized the
    // pair travels with it instead of drifting off its corner.  If that rect
    // changes, these two constants change with it - they are the only coupling.
    //
    // The registration lamp sits at the FAR RIGHT of the same banner, on the
    // SAME Y as the locator lamp - see the block after this one.  The two are
    // the only status lamps in the header and they now read as one row.
    {
        constexpr float kHdrX = 554.0f, kHdrY = 34.0f;   // == headerAnimation origin
        constexpr float kInset = 12.0f;
        constexpr float kLed  = 14.0f;
        constexpr float kBtnW = 158.0f, kBtnH = 20.0f;

        const float x = kHdrX + kInset;
        const float y = kHdrY + kInset;

        ledFolderLocator.setBounds (scaleRectF ({ x, y, kLed, kLed }));
        btnLocateFolder .setBounds (scaleRectF ({ x, y + kLed + kLed, kBtnW, kBtnH }));
    }

    // ── THE HEADER READOUTS ARE GONE ─────────────────────────────────────────
    //
    // The header used to carry a CHORD readout at x573 y63 and a STYLE readout
    // at x573 y120, both inside frames painted into main_back.png.  Rob has
    // rebuilt that artwork, so BOTH are removed as a feature here.
    //
    // `lblStyleName` IS DELETED OUTRIGHT rather than moved: it displayed
    // currentSetDisplayName(), which is the SET name - the same thing the
    // lblSetName pill beside the LOAD / SAVE / RELOAD trio already shows.  Two
    // widgets for one value is how they end up disagreeing.
    //
    // `lblPlayedChord` SURVIVES and moves down into the space freed by
    // narrowing that pill - see Row 7 below, where both are placed together.

    // ── THE THREE FEATURES ───────────────────────────────────────────────────
    // Rob's coords are w,x,h,y = 263,177,121,446; scaleRect takes x,y,w,h.
    // Shifted LEFT from x 238, which is what frees the far end of this band for
    // ENERGY; the three features now sit between the narrowed semitone stepper
    // and that slider.
    featurePanel.setBounds (scaleRect ({ 177, 446, 263, 121 }));
    // ── THE REGISTRATION LAMP AND ITS TWO LINES ──────────────────────────────
    //
    // Anchored to the SAME kHdrY + kInset as ledFolderLocator, so the two lamps
    // sit on one line across the banner.  The numbers are written as the header
    // origin plus an inset for exactly the reason the locator pair is - if the
    // banner moves, both travel with it.
    //
    // RIGHT-ALIGNED, not centred under the lamp.  The banner ends at x 1531 and
    // the lamp's right edge is 1514; a label centred on a 14 px lamp would need
    // to start past the edge to fit "NOT REGISTERED", so the text is right-
    // aligned to the lamp's right edge and grows leftwards into the banner.
    {
        constexpr float kHdrX = 554.0f, kHdrY = 34.0f;   // == headerAnimation origin
        constexpr float kInset = 12.0f;
        constexpr float kLed  = 14.0f;
        constexpr float kRight = kHdrX + 977.0f - kInset - 5.0f;   // 1514

        const float y = kHdrY + kInset;                  // == the locator lamp's y

        ledRegistration.setBounds (scaleRectF ({ kRight - kLed, y, kLed, kLed }));
        lblReg         .setBounds (scaleRectF ({ kRight - 170.0f, y + kLed + 4.0f, 170.0f, 16.0f }));
        lblRegHint     .setBounds (scaleRectF ({ kRight - 300.0f, y + kLed + 22.0f, 300.0f, 14.0f }));
    }
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
    debugRect(midiLinkCanvas,         juce::Colours::yellow);
    debugRect(selectorArrangerPiano,  juce::Colours::orange);
    debugRect(selectorSingleMulti,    juce::Colours::orange);
    debugRect(selectorTransition,     juce::Colours::orange);
    debugRect(selectorFillLength,     juce::Colours::orange);
    debugRect(knobGlobalSemitone,     juce::Colours::magenta);
    debugRect(featurePanel,           juce::Colours::magenta);
    debugRect(lblPlayedChord,         juce::Colours::magenta);
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
//==============================================================================
/** The browser's label for a style ref.

    The grid is labelled from the blob's TOC, so the ONLY name that can match a
    cell is the TOC's. Deriving one from the ref - which is an id, not a path -
    is what stopped cells lighting. */
juce::String MainComponent::displayNameForStyleRef (const juce::String& ref) const
{
    if (const auto* e = processor.getStyleLibrary().find (ref))
        return e->displayName;

    // A legacy path out of an old set: fall back to the old derivation.
    return Betel::displayNameFor (juce::File (ref));
}

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

    // The registration-LED poll that used to sit here is gone with the licence
    // check - see the ctor.  Nothing can change that state in a free build, so
    // polling it 30 times a second was work to confirm a constant.

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

        // Re-read from the processor, which is what keeps the master knob and
        // the mixer fader showing one number however either was moved - and
        // covers the routes neither callback sees, like a set load or a CC.
        knobMasterVol.setValue (processor.getMasterVolume() * 100.0, false);
        energySlider .setValue ((double) processor.getStyleEnergy(),  false);

        knobGlobalSemitone.setValue ((double) processor.getGlobalTranspose(), false);
        knobSplit         .setValue ((double) processor.getSplitPoint(),      false);
    }

    // 0) CC learn/assign mirror — refresh the Settings tab when the engine
    //    captures a learned controller, and keep the armed-row highlight live.
    // STILL DRAINED, even though no page shows it now: the flag has to be
    // consumed or it stays raised forever, and the file write is what keeps
    // grex_cc_map.xml in step with the engine. Only the row refresh went.
    if (processor.consumeCcDirty())
    {
        std::array<int, Betel::CcMap::kNumTargets> live {};
        for (int i = 0; i < SettingsTab::kNumCcTargets; ++i)
        {
            const int n = processor.getCcNumber (i);
            if (i < Betel::CcMap::kNumTargets) live[(size_t) i] = n;
        }
        // The assignment is captured on the AUDIO thread; the file write happens
        // here, on the message thread, the first time the UI notices.
        Betel::CcMap::get().syncFrom (live);
    }

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

    // 0) PLAY pressed with nothing loaded — recover before the visual below,
    //    so a successful recovery lights the lamp on this same tick rather
    //    than blinking off and on again.
    if (processor.consumePlayRefusedNoStyle())
        recoverNoStyleAndPlay();

    // 0b) HARMONY — mirrored so a MIDI-assigned toggle or a set load shows on
    //     the panel. setHarmonyState never calls back, so this cannot loop.
    featurePanel.setHarmonyState (processor.isHarmonyEnabled(),
                                  processor.getHarmonyType(),
                                  processor.getHarmonyLevel());
    featurePanel.setMultiSplitState (processor.isMultiSplitEnabled(),
                                     processor.getBassSplitPoint(),
                                     processor.getBassZoneSlot());
    featurePanel.setBassState (processor.isBassInversionEnabled(),
                               processor.isManualBassEnabled());

    // THE KEYBOARD'S PURPLE BAND. Asked every tick rather than pushed from the
    // panel, because the split can also move from a MIDI assignment or a set
    // load - neither of which passes through the panel at all.
    keyboardComponent.setBassSplitPoint (processor.isMultiSplitEnabled()
                                             ? processor.getBassSplitPoint()
                                             : -1);

    //==========================================================================
    // GAIN-REDUCTION METERS - FINISHER AND SWEETENER.
    //
    // THESE WERE INSIDE `if (cur != lastSeenStylePtr)`, the block that runs
    // ONCE WHEN A NEW STYLE LOADS. Their own comment said "polled on the same
    // UI tick as the faders", which is what the author intended and what the
    // nesting quietly prevented: they were read exactly once per style load -
    // at a moment when nothing is sounding, so both read zero - and then never
    // again until the next style change.
    //
    // Not broken meters. Meters asked for a number once an hour.
    //
    // The fader sync they were nested with genuinely IS style-load work: the
    // style's setup writes per-channel levels and the faders have to catch up.
    // That stays where it is. A meter is the opposite - it is only ever
    // interesting while audio is running.
    //
    // Both are cheap enough for 30 Hz by construction:
    // setFinisherGainReductionDb ignores sub-0.1 dB changes, and
    // refreshSweetenerMeters returns on its first line unless a sound editor is
    // actually open. An open one repaints an 18 px strip and only when a bar
    // has moved.
    //==========================================================================
    {
        const float finGr = processor.getFinisherGainReductionDb();
        mixerTab.setFinisherGainReductionDb (finGr);
        if (finisherWindow && finisherWindow->isVisible())
            finisherWindow->setGainReduction (finGr);

        soundsTab.refreshSweetenerMeters();
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
    // THE LAMP ANSWERS "WHAT DID I ASK FOR", NOT "WHAT IS SOUNDING".
    //
    // This read getCurrentSection(), so a queued press could not light its
    // button until the transition actually happened — up to a whole quantise
    // unit later, and longer under HOLD.  Press ENDING and the panel went on
    // showing the variation still playing, which reads as the press not having
    // registered at all.  It was reported as the ending having "strange
    // latency": the audio was on time, the LED was the thing lying.
    //
    // A hardware arranger lights the lamp on the PRESS and holds it lit through
    // the section, so the sequence a player sees is press -> lit -> it happens
    // -> still lit -> section ends.  getQueuedOrCurrentSection gives exactly
    // that: the queued section while one is pending, the playing section
    // otherwise — and because the transition enters the very section that was
    // queued, the LED never moves at the changeover. It simply stops being a
    // promise and starts being a report, with no visible seam.
    const auto queuedSec = seq.getQueuedOrCurrentSection();

    // A press made while STOPPED is a real queued press and must light too.
    // The start path now opens playback ON the section that was armed (see
    // StyleSequencer's pendingStart handler), so arming INTRO or ENDING with
    // the transport stopped genuinely decides what the next PLAY will do — and
    // the lamp has to say so, or the stopped case is the one place the panel
    // goes back to lying. Without this the fallback below would overwrite it
    // with the current variation's main every frame.
    const bool haveQueued = (queuedSec != seq.getCurrentSection());

    int activeBtn = sectionToVariButtonIdx (queuedSec);
    if (activeBtn < 0
        || (! haveQueued && ! (engineIsPlaying && processor.hasStyle())))
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
        //
        // THE PILL INHERITS THE HEADER'S DERIVATION.  It used to write
        // displayNameFor(lastSetFile) or a bare "No Set", while the header label
        // it replaces used currentSetDisplayName() - which falls back to the
        // loaded STYLE's name when no set exists, and only then to "No Style".
        // Keeping the weaker of the two would have been a silent regression
        // dressed up as a layout change.
        lblSetName.setText (currentSetDisplayName());

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

            // THE TWO GR METERS USED TO SIT HERE, and that is why neither ever
            // moved. See the block after this `if` closes.

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

        // THE OCTAVE PADS FOLLOW THE SELECTED SOLO'S OWN OCTAVE.  One read of
        // the model covers every way it can change - a pad, the editor's OCT
        // slider, picking another solo, a set load, an .ins adopt - so none of
        // those paths needs to remember to tell the pads.
        mainTab.setSoloOctaveDisplay (
            soundsTab.getSoloOctave (selectedSoloForOctave (processor)));

        // THE MANUAL BASS | HARMONY DISPLAY that replaced SOLO 7 and 8.
        //
        // These two lamps had NO driver before this: they were configured to
        // follow setPlaying(), and nothing ever called it, so they never lit.
        //
        //   MANUAL BASS  lit when the left hand is playing solo 7 as a bass -
        //                asked through getTintedBassSlot(), the same answer the
        //                purple tint and the router use.  That is manual bass
        //                always, and multi split while its zone sits on solo 7;
        //                both are the "M. BASS" slot everywhere else in the UI.
        //   HARMONY      lit when harmony is switched on.
        //
        // The names come from the same source as the six buttons beside them.
        mainTab.setSoloPairState (
            processor.getTintedBassSlot() == LedButton::kManualBassSoloSlot,
            soundsTab.getSoloInstrumentName (LedButton::kManualBassSoloSlot),
            processor.isHarmonyEnabled(),
            soundsTab.getSoloInstrumentName (LedButton::kHarmonySoloSlot));

        for (int s = 0; s < 8; ++s)
        {
            const bool on = (mask == 0) ? (s == active)
                                        : ((mask & (1u << s)) != 0);
            auto& b = mainTab.getSoloButton (s);
            if (b.getToggleState() != on)
                b.setToggleState (on, juce::dontSendNotification);

            // Label each solo element with its selected instrument name -
            // EXCEPT slot 8, which is the harmony plate.
            //
            // THIS IS WHY IT WAS NOT SAYING "HARMONY": the caption was set once
            // in MainTab's constructor and then overwritten by this mirror on
            // the very first 30 Hz tick, with whatever instrument the harmony
            // channel happens to hold. A fixed caption cannot survive a loop
            // that rewrites it thirty times a second, so the loop has to know
            // about it.
            if (s != BetelgeuseProcessor::kHarmonySlot)
            {
                const juce::String nm = soundsTab.getSoloInstrumentName (s);
                if (b.getButtonText() != nm)
                    b.setButtonText (nm);
            }

            // THE BASS ZONE'S SLOT WEARS ITS COLOUR, wherever it currently is.
            // Asked of the processor rather than compared against a constant,
            // so moving the zone to another slot moves the tint with it -
            // otherwise slot 7 would stay purple after the zone had left it,
            // which is worse than no tint at all.
            const int tintSlot = processor.getTintedBassSlot();
            if (s != BetelgeuseProcessor::kHarmonySlot)
            {
                b.setTintColour (s == tintSlot
                                     ? Betel::Harmonizer::bassZone()
                                     : juce::Colours::transparentBlack);

                // M. BASS beside the slot number, so the corner tag says both
                // WHICH slot it is and WHAT it is doing - the number alone
                // cannot explain why that one button is purple.
                b.setCornerTag (s == tintSlot
                                    ? "SOLO " + juce::String (s + 1) + "  M. BASS"
                                    : "SOLO " + juce::String (s + 1));
            }

            // ── AND THE STYLE ROW THE SAME WAY ───────────────────────────
            //
            // The eight style buttons showed their ROLE and never named the
            // sound loaded on them - MainTab::setStyleInstrumentName existed
            // for exactly this and nothing ever called it. Mirrored here rather
            // than at the patch-change site so it cannot go stale: a slot can
            // take a new sound from a set load, a song, a favourite or a preset
            // adopt, and only some of those pass through a patch-change hook.
            //
            // The full name is set; LedButton shortens it at paint time.
            //
            // MOVED TO THE END OF STEP 8, and that move IS the drum fix.  This
            // loop runs BEFORE the mirror that writes the engine's kit name into
            // the Sounds tab, so on a drum slot it was reading a value that the
            // same function had not written yet - the button was fed the PREVIOUS
            // refresh's answer, and on the first refresh after a style or set
            // load there is no previous answer at all.
        }

        // ARMED -> RECORDING happens in the AUDIO thread, when the first
        // gesture latches t=0. Nothing tells the editor, so the button would sit
        // on "ARMED" through an entire take without this - the one state where
        // being wrong actually costs the user something.
        refreshRecordButton();

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
                                              processor.getKitNameForKitKey (kitKey, s));

                // A name change with no key change (a user pick that renames the
                // same kit) still has to land.
                const juce::String kit = processor.getChannelDrumKitName (s);
                if (kit.isNotEmpty() && soundsTab.getSlotDrumKitName (false, s) != kit)
                    soundsTab.setSlotDrumKitName (false, s, kit);

                // CRASH TAB: the kit on the DRUMS slot names the four crash
                // options - an EDM kit's own crash pads.  A no-op unless the
                // kit or its edits changed.
                if (s == 0)
                    crashTab.setDrumsKit (soundsTab.getSlotDrumKitName (false, 0),
                                          soundsTab.getSlotEdmEdits (false, 0));

                // ── THE MAIN TAB BUTTON, FROM THE ENGINE, NOT THE MIRROR ─────
                //
                // Named straight off getChannelDrumKitName rather than through
                // the Sounds tab, because the mirror above only runs while
                // isChannelDrum(s) holds and only updates on a CHANGE.  Reading
                // the engine directly means the DRUMS and PERC buttons cannot be
                // one refresh behind, and cannot be left showing a stale name if
                // the mirror is ever skipped.
                //
                // displayNameForKit, NOT prettyName - and swapping those two is
                // what put "000" on the button.
                //
                // The engine reports a COMPOSED kit by REGISTRY KEY ("000",
                // "016"), and a SAMPLED full kit by its folder name. prettyName
                // only removes underscores, so it passed the key straight
                // through: a name on some slots, a bare number on others,
                // depending on which kind of kit the style happened to load.
                //
                // displayNameForKit is the project's ONE converter for exactly
                // this - its own comment says "one converter, one source of
                // truth - giving the host its own copy is how the sixteen
                // displayNameFor variants happened", and that is precisely the
                // mistake this line was making. The MIXER label two branches
                // below already used it; the button was the odd one out.
                if (kit.isNotEmpty())
                    mainTab.setStyleInstrumentName (s, SoundsTab::displayNameForKit (kit));

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
                // ── THE MELODIC HALF OF THE MAIN TAB BUTTON NAME ─────────────
                //
                // Unconditional and every refresh, the same as the drum half
                // above.  It used to live in an earlier loop; both halves are
                // here now so the two can never disagree about which slot they
                // are naming, and so a slot that flips between drum and melodic
                // is renamed by exactly one of them.
                {
                    const juce::String sn = soundsTab.getStyleInstrumentName (s);
                    if (sn.isNotEmpty())
                        mainTab.setStyleInstrumentName (s, sn);
                }

                const int flag = processor.getChannelInstrumentFlag (s);
                if (flag >= 0 && flag < 128
                    && soundsTab.getSlotPatch (false, s) != flag)
                {
                    soundsTab.setSlotPatch (false, s, flag);
                    // Library name first for the same reason as the solo
                    // buttons: a GM program lookup cannot name a World or
                    // Oriental sound.
                    {
                        const auto nm = processor.getInstrumentName (flag);
                        mixerTab.setStyleSlotLabel (s, nm.isNotEmpty()
                                                        ? nm
                                                        : SoundsTab::instrumentNameForPatch (flag));
                    }

                    // The sound on this slot just changed — and if it carries a
                    // saved voice, the ENGINE already has it while the editor
                    // still holds the previous sound's values.  Adopt it here,
                    // where every instrument change is already detected, so the
                    // editor shows what is playing instead of something stale
                    // that the next knob move would push back over the preset.
                    SlotParams sp;
                    if (processor.getPresetParamsForChannel (s, sp))
                        soundsTab.adoptPresetParams (false, s, sp);

                    // AND RE-STATE WHAT THE SET SAYS THIS SLOT SOUNDS LIKE.
                    //
                    // Deliberately OUTSIDE the getPresetParamsForChannel guard
                    // above.  That guard means "the incoming sound has a saved
                    // .ins", and a melodic style slot normally has none - which
                    // is precisely the case where selectChannelPreset has just
                    // stamped default ChannelParams and a 100% trim over the
                    // slot.  Gating the repair on the same condition that makes
                    // the repair unnecessary is how this stayed invisible.
                    //
                    // adoptPresetParams above still returns early for style
                    // slots and should: the .ins is the right hand's authority,
                    // not the arrangement's.  This is the other half - the set
                    // is the style slot's authority.
                    soundsTab.reassertStyleSlotVoicing (s);
                }
            }

            // ── Solo slot s = engine channel soloBase + s ─────────────────
            const int soloFlag = processor.getChannelInstrumentFlag (soloBase + s);
            if (soloFlag >= 0 && soloFlag < 128
                && soundsTab.getSlotPatch (true, s) != soloFlag)
            {
                soundsTab.setSlotPatch (true, s, soloFlag);
                {
                    const auto nm = processor.getInstrumentName (soloFlag);
                    mixerTab.setSoloSlotLabel (s, nm.isNotEmpty()
                                                    ? nm
                                                    : SoundsTab::instrumentNameForPatch (soloFlag));
                }

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
    // ── THE WHOLE SET GOES IN THE SONG ───────────────────────────────────────
    //
    // buildSetSnapshot() is the same tree SAVE SET writes, so a song carries
    // everything a set does: the mixer, the ducker, the finisher, per-slot
    // voicing, kit unity, crash, jumps, levels and the macros. The thin fields
    // below were never going to cover that, and a song that replays against
    // whatever set happens to be loaded is not a recording of anything.
    //
    // ~980 KB against a performance of ~50 KB, so the file is this size either
    // way - and what it buys is a song that cannot change when a set is edited
    // months later.
    GrexSongRecorder::Setup s;

    // buildSetSnapshot is non-const because it reads live UI state; this method
    // is const, so the cast rather than loosening either signature.
    s.setSnapshot = const_cast<MainComponent*> (this)->buildSetSnapshot();
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
        if (processor.loadStyleByRef (s.stylePath, err))
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
//==============================================================================
// WHICH CONTROLS CAN BE ASSIGNED.
//
// The tabs register their own (see MainTab::attachRemotes) so the id stays
// beside the widget. What is left is the LEFT PANEL, which MainComponent owns
// directly.
//
// SET LOAD / SAVE / FAST SAVE ARE DELIBERATELY ABSENT. A stray CC that changes a
// level is a wrong note; a stray CC that overwrites a set file is lost work.
//==============================================================================
void MainComponent::attachRemoteControls()
{
    using R = Betel::RemoteId;

    mainTab.attachRemotes     (remoteHub);
    mixerTab.attachRemotes    (remoteHub);
    setEditorTab.attachRemotes (remoteHub);

    remoteHub.attach (knobTempo,          R::Tempo);
    remoteHub.attach (energySlider,       R::StyleEnergy);
    remoteHub.attach (knobGlobalSemitone, R::Transpose);
    remoteHub.attach (knobSplit,          R::SplitPoint);

    // HOLD and ARRANGER/PIANO were MISSED on the first pass and caught by the
    // id-coverage check, not by reading: both sit on the left panel rather than
    // in MainTab with the other transport controls, so scanning the tab found
    // everything except them. Two of the most performance-critical controls in
    // the plugin, and the two the eye skips.
    remoteHub.attach (btnHold,               R::Hold);
    remoteHub.attach (selectorArrangerPiano, R::PianoMode);

    remoteHub.attach (btnComments,   R::Comments);
    remoteHub.attach (btnForgetAll,  R::OrientalScale);
    remoteHub.attach (btnDawStart,   R::DawStart);
    // FUNKEY is attached inside MainTab::attachRemotes now, with the rest of the
    // play-control row.
}

//==============================================================================
// The popup is created fresh per opening and handed to a CalloutBox, which owns
// and deletes it. Keeping one long-lived instance would mean remembering to
// disarm LEARN on every path that could close it; a short-lived one disarms in
// its own destructor path and cannot be left armed by accident.
//==============================================================================
void MainComponent::openAssignPopup (Betel::RemoteId id, juce::Component& over,
                                     Betel::RemoteMap::Kind k, int number)
{
    // THE DROP ALREADY HAPPENED.  This window is not a confirmation - the
    // assignment is live the moment openFor runs - it is where the mode gets
    // chosen and where CANCEL can put back whatever was there before.
    auto popup = std::make_unique<Betel::AssignPopup>();
    auto* raw  = popup.get();
    raw->openFor (id, k, number);
    raw->onMapChanged = [this] { saveRemoteMap(); };

    auto& box = juce::CallOutBox::launchAsynchronously (
                    std::move (popup),
                    getLocalArea (&over, over.getLocalBounds()),
                    this);
    box.setDismissalMouseClicksAreAlwaysConsumed (true);

    raw->onCloseRequested = [safe = juce::Component::SafePointer<juce::CallOutBox> (&box)]
    {
        if (safe != nullptr) safe->dismiss();
    };

    // The drop consumed the armed source, if that is how it got here.
    setArmedMidiSource (Betel::RemoteMap::Kind::None, -1);
}

void MainComponent::saveRemoteMap() const
{
    // AUTO-SAVE, ALWAYS.  The map belongs to the RIG, not to a song, so it has
    // to survive a close without anybody remembering to save it - somebody
    // spending twenty minutes mapping a controller and losing it to a closed
    // window is not an acceptable outcome. The named files below are presets on
    // top of this, for people with more than one controller.
    Betel::RemoteMap::get().save (
        Betel::GrexPaths::root().getChildFile ("grex_remote_map.xml"));
}

/** Which chip the canvas has armed, if any - the touch path.  One owner for the
    value, read by the hub's hitTest and drawn by the canvas, so the two cannot
    disagree about whether an assignment is in progress. */
void MainComponent::setArmedMidiSource (Betel::RemoteMap::Kind k, int number)
{
    armedKind   = k;
    armedNumber = number;
    midiLinkCanvas.setArmedSource (k, number);

    // Repaint every attached target together so the armed state cannot be half
    // on: while armed, every one of them swallows ordinary clicks.
    //
    // The dashed "waiting for a target" outline this used to drive is GONE (see
    // AssignPopup.h) - the HOVER highlight now carries that job, so the repaint
    // still matters: a target that missed the repaint would not light on hover
    // either, and would then swallow a click with no feedback at all.
    remoteHub.repaintTargets();
}

//==============================================================================
// SAVE / LOAD A NAMED MIDI MAP.
//
// Files live in <root>/midi maps beside the auto-saved grex_remote_map.xml, so
// one rig can keep a map per controller and switch between them.  Deliberately
// NOT in the set: a set travels with a song, and which hardware is plugged in
// has nothing to do with which song is being played.
//==============================================================================
//==============================================================================
// SAVE / LOAD THE MIDI ASSIGN MAP.
//
// Two entry points reach these - the SETTINGS > MIDI ASSIGNING buttons and the
// MIDI LINK canvas's right-click menu - and they must not be two
// implementations. A save that wrote somewhere the load did not look would be
// the kind of fault nobody finds until a map goes missing.
//
// Files live in <root>/midi maps, beside the auto-saved grex_remote_map.xml, so
// one rig can keep a map per controller. Deliberately NOT in the set: a set
// travels with a song, and which hardware is plugged in has nothing to do with
// which song is being played.
//==============================================================================
juce::File MainComponent::midiMapsFolder() const
{
    auto dir = Betel::GrexPaths::root().getChildFile ("midi maps");
    dir.createDirectory();
    return dir;
}

void MainComponent::promptSaveMidiMap()
{
    const auto dir = midiMapsFolder();

    midiMapChooser = std::make_unique<juce::FileChooser> (
                         "Save MIDI map", dir.getChildFile ("My Controller.grexmidi"),
                         "*.grexmidi");
    midiMapChooser->launchAsync (juce::FileBrowserComponent::saveMode
                               | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;

        // The extension is added rather than assumed: a name typed without one
        // would save a file the LOAD dialog's *.grexmidi filter then hides.
        Betel::RemoteMap::get().save (
            f.hasFileExtension ("grexmidi") ? f : f.withFileExtension ("grexmidi"));
    });
}

void MainComponent::promptLoadMidiMap()
{
    midiMapChooser = std::make_unique<juce::FileChooser> (
                         "Load MIDI map", midiMapsFolder(), "*.grexmidi");
    midiMapChooser->launchAsync (juce::FileBrowserComponent::openMode
                               | juce::FileBrowserComponent::canSelectFiles,
                                 [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File() || ! f.existsAsFile()) return;

        Betel::RemoteMap::get().load (f);

        // The loaded map becomes the LIVE map, so it also becomes the auto-saved
        // one. Without this the next launch would quietly come back on the map
        // that was just replaced, and the load would look like it had not
        // happened at all.
        saveRemoteMap();
    });
}

//==============================================================================
// UNLEARN ALL.
//
// Confirmed first, because the map AUTO-SAVES: the moment this runs, the file
// on disk is overwritten and a controller somebody spent twenty minutes mapping
// is gone with no undo behind it. A destructive action reachable in one click
// from a settings page needs the second click.
//
// Back to FACTORY, not to empty - see RemoteMap::resetToFactoryDefaults for why
// an empty map is no longer a state anybody would want to land in.
//==============================================================================
void MainComponent::promptUnlearnAllMidi()
{
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon,
        "Unlearn all MIDI assignments",
        "Every assignment goes back to the factory control-note map.\n\n"
        "This is saved immediately and cannot be undone. Save the current map "
        "to a file first if you want to keep it.",
        "UNLEARN ALL",
        "CANCEL",
        this,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result == 0) return;              // cancelled

            Betel::RemoteMap::get().resetToFactoryDefaults();
            saveRemoteMap();

            // A source armed against a control that no longer holds it would
            // leave the arm live with nothing to drop - every attached target
            // still swallowing clicks, for an assignment that cannot complete.
            setArmedMidiSource (Betel::RemoteMap::Kind::None, -1);
        }));
}

void MainComponent::showMidiMapMenu()
{
    auto dir = Betel::GrexPaths::root().getChildFile ("midi maps");
    dir.createDirectory();

    juce::PopupMenu m;
    m.addItem (1, "Save MIDI map as...");
    m.addItem (2, "Load MIDI map...");
    m.addSeparator();
    // Same wording and same action as the SETTINGS button. It used to say
    // "Clear all assignments" and it used to mean it - emptying the map, which
    // now leaves the instrument with no hardware control whatsoever.
    m.addItem (3, "Unlearn all (back to factory)");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (midiLinkCanvas),
                     [this, dir] (int r)
    {
        if (r == 1)      promptSaveMidiMap();
        else if (r == 2) promptLoadMidiMap();
        else if (r == 3) promptUnlearnAllMidi();
    });
}

//==============================================================================
// THE SONG WINDOW
//==============================================================================
void MainComponent::openSongWindow()
{
    if (songWindow != nullptr)
    {
        songWindow->toFront (true);
        return;
    }

    class Holder : public juce::DocumentWindow
    {
    public:
        Holder (MainComponent& o)
            : juce::DocumentWindow ("Song", juce::Colour (0xFF1A1A1A),
                                    juce::DocumentWindow::closeButton),
              owner (o)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new SongWindowContent (owner.processor), true);
            setResizable (true, true);
            setResizeLimits (520, 360, 1100, 900);

            // ALWAYS ON TOP, as asked. A transport you cannot see is a transport
            // you cannot stop, and this one is the only way out of song mode.
            setAlwaysOnTop (true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { owner.closeSongWindow(); }

    private:
        MainComponent& owner;
    };

    songWindow = std::make_unique<Holder> (*this);

    // ── THE BAND GOES DEAD, THE RIGHT HAND DOES NOT ──────────────────────────
    //
    // This was setEnabled(false) on the whole component. Right about the band -
    // a click that changes something the song is about to replay over looks
    // like the song ignoring you - and wrong about the player, because it took
    // the SOLO SELECTORS with it. Choosing which slots the keyboard reaches is
    // exactly what you want to do over a backing track.
    //
    // Selective from the start rather than disable-then-re-enable: JUCE's
    // isEnabled() walks up the hierarchy, so a child cannot undo a disabled
    // parent.
    mainTab.setSongMode (true);
    setSongModeLocksRecord (true);   // btnRecord is ours now, not MainTab's
    setSongActive (true);
}

void MainComponent::closeSongWindow()
{
    processor.getSongPlayer().stop();
    songWindow.reset();

    mainTab.setSongMode (false);
    setSongModeLocksRecord (false);   // btnRecord is ours now, not MainTab's
    setSongActive (false);
}

//==============================================================================
void MainComponent::promptSaveSong()
{
    if (processor.getSongRecorder().getEventCount() <= 0) return;

    auto& m = processor.getFolderManager();
    const auto dir = m.isRootFolderValid() ? m.getRootFolder().getChildFile ("songs")
                                           : juce::File::getSpecialLocation (
                                                 juce::File::userDocumentsDirectory);
    dir.createDirectory();

    songChooser = std::make_unique<juce::FileChooser> (
                      "Save song", dir.getChildFile ("Untitled.grxsong"), "*.grxsong");

    songChooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.getFullPathName().isNotEmpty())
            processor.getSongRecorder().saveToFile (
                f.hasFileExtension ("grxsong") ? f
                                               : f.withFileExtension ("grxsong"));
    });
}

//==============================================================================
// PLAY WITH NOTHING LOADED: LOAD SOMETHING, THEN PLAY.
//
// Raised by BetelgeuseProcessor::togglePlayStop (the MIDI / assigned-pad route)
// and by the mouse handler, drained here on the 30 Hz mirror.
//
// WHY THE RECOVERY IS ON THIS SIDE: the style browser is an editor component.
// The processor must be able to refuse headlessly - an offline render with no
// style genuinely has nothing to play and must not go hunting for one - so the
// processor only ever reports, and the recovery happens where a browser exists.
//
// WHY RECOVER AT ALL RATHER THAN JUST WARN: this is an arranger.  There is no
// useful state in which it holds no style, and every route that is supposed to
// guarantee one has now failed in the field at least once (a styleless project
// snapshot; a factory set whose StyleLink was orphaned by the move to blobs).
// The transport button is where the user finds out, so the transport button is
// where it gets fixed.
//==============================================================================
void MainComponent::recoverNoStyleAndPlay()
{
    auto& seq = processor.getSequencer();

    // Something loaded a style between the press and this tick.  Honour the
    // press rather than dropping it - the user asked for PLAY.
    if (processor.hasStyle())
    {
        if (! seq.isPlaying()) seq.start();
        return;
    }

    // selectFirstStyle fires onStyleSelected synchronously, which is the whole
    // load path - style, set, levels, calibration.  So by the time it returns,
    // hasStyle() is the honest answer to whether it worked.
    if (stylesTab.selectFirstStyle() && processor.hasStyle())
    {
        seq.start();
        return;
    }

    // NOTHING TO LOAD.  Say so where the user is looking and write it down.
    // An empty library is a real state - a fresh install with no styles folder,
    // or a root pointing somewhere wrong - and it is the one case where PLAY
    // genuinely cannot do anything.  It must still not be silent.
    stylesTab.showNotice ("NO STYLES FOUND - CHECK THE ROOT FOLDER");

    // Guarded exactly like the search path's own jump: indexOf returns -1 if
    // the row is ever renamed, and selectTab would index the button array with
    // it.
    if (const int stylesIdx = kTabNames.indexOf ("STYLES"); stylesIdx >= 0)
        selectTab (stylesIdx);
}

void MainComponent::setRecordState (RecordState st)
{
    recordState = st;

    // THE LAMP IS THE WHOLE INDICATION NOW.  The button is invisible and the
    // caption is in the artwork, so there is nothing else left to say which of
    // the three states this is - see SongRecordLED for why armed and recording
    // must stay distinguishable.
    switch (st)
    {
        case RecordState::Recording: ledSongRecord.setState (SongRecordLED::State::Recording); break;
        case RecordState::Armed:     ledSongRecord.setState (SongRecordLED::State::Armed);     break;
        default:                     ledSongRecord.setState (SongRecordLED::State::Off);       break;
    }
}

void MainComponent::refreshRecordButton()
{
    auto& rec = processor.getSongRecorder();
    const auto want = rec.isRecording() ? RecordState::Recording
                    : rec.isArmed()     ? RecordState::Armed
                                        : RecordState::Idle;
    if (getRecordState() != want)
        setRecordState (want);
}

void MainComponent::openFinisherWindow()
{
    if (! finisherWindow)
    {
        finisherWindow = std::make_unique<FinisherWindow> (processor.getDucker());

        finisherWindow->onEnabledChanged = [this](bool on)
        {
            processor.setFinisherEnabled (on);
            mixerTab.setFinisherEnabled (on);
        };
        finisherWindow->onAmountChanged = [this](float pct)
        {
            processor.setFinisherAmount (pct);
        };
        // RESET restores the factory values and pushes every slider back.  The
        // window asks; it does not own the numbers.  That is the same division
        // the CHARACTER selector used to blur - it wrote values the sliders
        // already owned, which is why it needed a CUSTOM state to admit the
        // sliders had moved on.
        // Any ducker control moved.  The page has already written the value
        // into the engine (it holds the live object); this exists so the SET
        // knows it is dirty and so nothing else has to poll.
        finisherWindow->onDuckerChanged = [this]
        {
            // Deliberately empty of pushes: DuckerPage writes straight to the
            // engine object, which is the same object MixerState reads when the
            // set is saved.  A second copy here would be a second owner.
        };

        finisherWindow->onResetRequested = [this]
        {
            processor.resetFinisherToFactory();
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

    for (int i = 0; i < Betel::Finisher::kNumParams; ++i)
        finisherWindow->setParam (i, processor.getFinisherParam (i));

    for (int st = 0; st < Betel::Finisher::kNumStages; ++st)
        finisherWindow->setStageEnabled (st, processor.getFinisherStageEnabled (st));

    // The DUCKER page reads the engine directly, but its SLIDERS are widgets
    // and widgets need seeding - after a set load they would otherwise show the
    // previous song's numbers over the new song's sound.
    finisherWindow->refreshDucker();
}
