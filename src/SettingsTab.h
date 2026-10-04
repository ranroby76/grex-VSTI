
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

#include "GlobalMacrosPanel.h"
#include "SetEditorTab.h"
#include "RegistrationManager.h"   // machine-locked serial + demo mode

//==============================================================================
// SettingsTab — reference + control-mapping page, split into sub-pages chosen
// by a horizontal selector at the top:
//
//   • CONTROL NOTE MAP : read-only reference of the reserved MIDI control
//     notes (0–35) and the arranger action each one fires.
//   • MIDI ASSIGNING   : SAVE / LOAD for the MIDI assign map (.grexmidi in
//     <root>/midi maps).  The five learnable CC rows that used to be this page
//     are gone: assignment is drag-and-drop from the MIDI LINK canvas now, and
//     a second modal way of doing it - one that could only reach five fixed
//     targets - could only ever disagree with the real one.  The ENGINE keeps
//     those five CC targets and their defaults; only the way they change moved.
//   • REGISTRATION     : machine-locked serial + demo mode; the page is live.
//     The machine ID is read off the screen, quoted to us, and the serial that
//     comes back is typed in here.
//   • GLOBAL SETTINGS  : the two global macros — FUNKEY MODE (seven GM-family
//     FX presets).  Engage switch plus
//     the buttons that open each editor; the presets themselves live in
//     GlobalMacros and persist to their own files.
//   • SET EDITOR       : bake the library's sets, rebuild them, edit style
//     levels.  It sits here rather than in the main tab row - a maintenance
//     surface should not cost a permanent slot beside MAIN, STYLES and MIXER.
//==============================================================================
class SettingsTab : public juce::Component
{
public:
    enum CcTarget { kMasterVol = 0, kStyleVol, kTempo, kTranspose, kSplit, kNumCcTargets };
    // THE ENUM IS THE AUTHORITY for the button row, the page count and the
    // visibility switch, so an entry here adds all three at once.
    //
    // kRegistration is back - Grex is paid.  SET EDITOR STAYS WHERE BALLADA PUT
    // IT: it moved out of the main tab row because it is a maintenance surface
    // (bake the library's sets, rebuild them, edit style levels) and was costing
    // a permanent slot beside MAIN, STYLES and MIXER, which are played.  That
    // reasoning holds in Grex too, so it is not moved back.
    enum Page     { kNoteMap = 0, kCc, kRegistration, kGlobal, kSetEditor, kNumPages };

    //==========================================================================
    // MIDI ASSIGNING page.  Two buttons, and that is the whole page.
    //
    // WHAT WAS HERE: five rows of LEARN / FORGET for the fixed continuous
    // targets.  Assignment does not work that way any more - a source is
    // captured passively by the MIDI LINK canvas and dragged onto whatever it
    // should drive - so a second, modal, five-row-only way of assigning things
    // was a duplicate that could only ever disagree with the real one.
    //
    // What a settings page CAN usefully own is the FILE side of that map, which
    // the canvas offers only through a right-click nobody would find.
    //==========================================================================
    std::function<void()> onSaveMidiMap;
    std::function<void()> onLoadMidiMap;

    /** Put every assignment back to the factory control-note map.  The host
        confirms first - it auto-saves, so there is no undo behind it. */
    std::function<void()> onUnlearnAllMidi;

    /** Fired after a registration ATTEMPT, successful or not.  The header keeps
        its own lamp and status flag, and a page that changed the licence
        without saying so would leave those two reading the old answer until the
        editor was reopened. */
    std::function<void()> onRegistrationChanged;

    // Global Settings: bypass instrument audio chain toggle.  When true,
    // every channel's filter + post-mix FX are skipped; only amp ADSR / amp
    // LFO / pitch ADSR / pitch LFO / velocity / volume / pan survive.
    // Includes drum channels.  Wired by MainComponent to the processor's
    // setBypassInstrumentChain.
    std::function<void(bool /*bypass*/)> onBypassInstrumentChainChanged;

    // ── Global macros (GLOBAL SETTINGS page) ──────────────────────────────
    // Forwarded straight from GlobalMacrosPanel.  The host owns the state: it
    // sets the GlobalMacros flag, re-commits every slot so the change is
    // audible, and relights BOTH this page and the left-panel macro buttons.
    std::function<void(bool /*on*/)> onFunkeyModeToggled;

    /** RESET SOLO BASE GAIN, already confirmed by the panel's own dialog.  The
        host does the work: every .ins in the library back to 0 dB base. */
    std::function<void()> onResetSoloBaseGain;
    std::function<void()>            onMacroFxEdited;

    /** PITCH BEND RANGE moved, in semitones (1..12).  Solo instruments only —
        see BetelgeuseProcessor::setSoloPitchBendRange. */
    std::function<void(int)>         onPitchBendRangeChanged;

    /** LOW VELOCITY RESPONSE moved, 0..100.  Solo instruments only —
        see BetelgeuseProcessor::setSoloLowVelBoost. */
    std::function<void(int)>         onLowVelBoostChanged;
    std::function<void(float)>       onStyleBoostChanged;

    // onStyleLevelsChanged is gone from this tab.  Style levels are per-STYLE
    // and now live in the SET EDITOR tab, which owns the panel and talks to the
    // host directly.

    SettingsTab()
    {
        // ── Page selector (horizontal) ─────────────────────────────────────
        static const char* pageNames[kNumPages] =
            { "CONTROL NOTE MAP", "MIDI ASSIGNING", "REGISTRATION",
              "GLOBAL SETTINGS", "SET EDITOR" };

        for (int p = 0; p < kNumPages; ++p)
        {
            pageBtn[p].setButtonText (pageNames[p]);
            pageBtn[p].setClickingTogglesState (false);
            pageBtn[p].setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF1A1A1A));
            pageBtn[p].setColour (juce::TextButton::buttonOnColourId, juce::Colour (Betel::Pal::kAccent));
            pageBtn[p].setColour (juce::TextButton::textColourOffId,  juce::Colours::white.withAlpha (0.75f));
            pageBtn[p].setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
            pageBtn[p].onClick = [this, p] { selectPage (p); };
            addAndMakeVisible (pageBtn[p]);
        }

        // ── Note-map reference (read-only) ─────────────────────────────────
        noteMapView.setMultiLine (true);
        noteMapView.setReadOnly (true);
        noteMapView.setScrollbarsShown (true);
        noteMapView.setCaretVisible (false);
        noteMapView.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF101010));
        noteMapView.setColour (juce::TextEditor::textColourId,       juce::Colour (0xFFE8E0C8));
        noteMapView.setColour (juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);
        noteMapView.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
        noteMapView.setText (buildNoteMapText(), juce::dontSendNotification);
        addAndMakeVisible (noteMapView);

        // ── MIDI ASSIGNING page ────────────────────────────────────────────
        assignBlurb.setText ("Assignments are made by dragging a source from "
                             "the MIDI LINK panel onto any control.\n"
                             "Save the current map to a file, or load one back.",
                             juce::dontSendNotification);
        assignBlurb.setJustificationType (juce::Justification::centredTop);
        assignBlurb.setColour (juce::Label::textColourId,
                               juce::Colours::white.withAlpha (0.65f));
        assignBlurb.setFont (juce::Font (juce::FontOptions (14.0f)));
        addAndMakeVisible (assignBlurb);

        auto initAssignBtn = [this] (juce::TextButton& b, const juce::String& t)
        {
            b.setButtonText (t);
            b.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF1A1A1A));
            b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            addAndMakeVisible (b);
        };
        initAssignBtn (saveMapBtn, "SAVE MIDI MAP");
        initAssignBtn (loadMapBtn, "LOAD MIDI MAP");
        initAssignBtn (unlearnBtn, "UNLEARN ALL");

        // Set apart from the other two on purpose. Save and load are reversible
        // and this is not, so it does not sit flush beside them wearing the
        // same colour - the row above is housekeeping, this one throws work
        // away.
        unlearnBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF3A1010));
        unlearnBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xFFFF9A9A));

        saveMapBtn.onClick = [this] { if (onSaveMidiMap)    onSaveMidiMap();    };
        loadMapBtn.onClick = [this] { if (onLoadMidiMap)    onLoadMidiMap();    };
        unlearnBtn.onClick = [this] { if (onUnlearnAllMidi) onUnlearnAllMidi(); };

        // ── Registration ───────────────────────────────────────────────────
        {
            auto& rm = RegistrationManager::getInstance();

            regTitle.setText ("REGISTRATION", juce::dontSendNotification);
            regTitle.setJustificationType (juce::Justification::centred);
            regTitle.setColour (juce::Label::textColourId, juce::Colour (0xFFC2C2C2));
            regTitle.setFont (juce::Font (18.0f, juce::Font::bold));
            addAndMakeVisible (regTitle);

            // The ID is READ-ONLY and copyable: the user has to quote it to you
            // accurately, and re-typing a number off a screen is where support
            // tickets come from.
            regIdLabel.setText ("MACHINE ID", juce::dontSendNotification);
            regIdLabel.setJustificationType (juce::Justification::centred);
            regIdLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF8A8A8A));
            addAndMakeVisible (regIdLabel);

            regIdValue.setText (rm.getMachineIDString(), juce::dontSendNotification);
            regIdValue.setJustificationType (juce::Justification::centred);
            regIdValue.setColour (juce::Label::textColourId, juce::Colour (Betel::Pal::kAccent));
            regIdValue.setFont (juce::Font (26.0f, juce::Font::bold));
            regIdValue.setEditable (false);
            // juce::Label has no onClick - it is a display widget, not a button.
            // ClickableLabel below adds one mouseUp override rather than swapping
            // in a TextButton, because the ID has to READ as a value, not as
            // something to press; the copy is a convenience on top, not the point.
            regIdValue.onClicked = [this]
            {
                juce::SystemClipboard::copyTextToClipboard (regIdValue.getText());
                regStatus.setText ("Machine ID copied to clipboard.",
                                   juce::dontSendNotification);
            };
            addAndMakeVisible (regIdValue);

            regSerialBox.setTextToShowWhenEmpty ("enter your serial",
                                                 juce::Colour (0xFF6A6A6A));
            regSerialBox.setJustification (juce::Justification::centred);
            regSerialBox.setFont (juce::Font (20.0f));
            regSerialBox.onReturnKey = [this] { attemptRegistration(); };
            addAndMakeVisible (regSerialBox);

            regButton.setButtonText ("REGISTER");
            regButton.onClick = [this] { attemptRegistration(); };
            addAndMakeVisible (regButton);

            regStatus.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (regStatus);

            refreshRegistrationState();
        }

        // ── Global Settings: the macro section ─────────────────────────────
        macrosPanel.onFunkeyToggled = [this] (bool on)
        { if (onFunkeyModeToggled) onFunkeyModeToggled (on); };
        macrosPanel.onMacroFxEdited = [this]
        { if (onMacroFxEdited) onMacroFxEdited(); };
        macrosPanel.onPitchBendRangeChanged = [this] (int semis)
        { if (onPitchBendRangeChanged) onPitchBendRangeChanged (semis); };

        macrosPanel.onLowVelBoostChanged = [this] (int amount)
        { if (onLowVelBoostChanged) onLowVelBoostChanged (amount); };

        macrosPanel.onStyleBoostChanged = [this] (float db)
        { if (onStyleBoostChanged) onStyleBoostChanged (db); };

        macrosPanel.onResetSoloBaseGain = [this]
        { if (onResetSoloBaseGain) onResetSoloBaseGain(); };
        addAndMakeVisible (macrosPanel);


        // Global Settings: the RIGHT HAND VOLUME and STYLE VOLUME faders that
        // used to live here are gone.  Both buses are owned by the MIXER tab
        // (its STYLE VOLUME / SOLO VOLUME faders drive the same
        // setStyleVolume / setRightHandVolume on the processor), so these were
        // duplicates — and orphaned ones: their callbacks were never wired, so
        // the sliders moved but changed nothing.

        // addChildComponent, not addAndMakeVisible: selectPage below owns
        // visibility for every page, and a page that shows itself would be
        // painted over the selected one until the first page press.
        addChildComponent (setEditorTab);

        selectPage (kNoteMap);
    }

    // The CC-learn mirrors are retired with the rows they fed.  Kept as inert
    // no-ops - same treatment as setBypassInstrumentChain below - so the host's
    // 30 Hz mirror and its startup seed still compile while the engine keeps
    // its five fixed CC targets working exactly as before.
    void setCcNumber (int /*targetIdx*/, int /*cc*/) {}
    void setLearning (int /*targetIdx*/) {}

    // Bypass-instrument-audio-chain has been retired (the chain is always
    // connected).  Kept as an inert no-op so existing callers — MainComponent's
    // seed + session restore — still compile.
    void setBypassInstrumentChain (bool /*b*/) {}

    /** Relight the macro switches from GlobalMacros.  Called by the host after
        the FUNKEY MODE button flips a flag, and after a
        set restore, so the two surfaces never disagree. */
    void refreshMacroState() { macrosPanel.refreshFromState(); }

    /** The SET EDITOR page.  MainComponent owns every callback on it - the
        baker, the style-name provider, the levels hook - because those need the
        processor and the style library, neither of which a settings page has
        any business knowing about.  So the page LIVES here and is WIRED there. */
    SetEditorTab& getSetEditorTab() noexcept { return setEditorTab; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF161616));

        const auto content = contentArea();
        g.setColour (juce::Colour (0xFF0E0E0E));
        g.fillRoundedRectangle (content.toFloat(), 6.0f);
        g.setColour (juce::Colour (Betel::Pal::kTintPanel));
        g.drawRoundedRectangle (content.toFloat(), 6.0f, 1.5f);

    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10);

        // Horizontal selector row — all buttons identical size.
        auto selRow = b.removeFromTop (kSelectorH);
        const int bw = selRow.getWidth() / kNumPages;
        for (int p = 0; p < kNumPages; ++p)
        {
            juce::Rectangle<int> r (selRow.getX() + p * bw, selRow.getY(),
                                    bw, selRow.getHeight());
            pageBtn[p].setBounds (r.reduced (3, 2));
        }

        auto content = contentArea().reduced (12);

        // CONTROL NOTE MAP fills the content panel.
        noteMapView.setBounds (content);

        // REGISTRATION: a centred column with a capped width - a machine number
        // and one text field stretched across a full-width tab reads as a form
        // to fill in rather than the two-step exchange it actually is.
        {
            auto col = content.withSizeKeepingCentre (juce::jmin (420, content.getWidth() - 40),
                                                      juce::jmin (300, content.getHeight()));
            regTitle    .setBounds (col.removeFromTop (30));
            col.removeFromTop (14);
            regIdLabel  .setBounds (col.removeFromTop (18));
            regIdValue  .setBounds (col.removeFromTop (36));
            col.removeFromTop (22);
            regSerialBox.setBounds (col.removeFromTop (34).reduced (30, 0));
            col.removeFromTop (10);
            regButton   .setBounds (col.removeFromTop (32).reduced (110, 0));
            col.removeFromTop (14);
            regStatus   .setBounds (col.removeFromTop (40));
        }

        // MIDI ASSIGNING: a line of explanation and two buttons, centred in the
        // content panel rather than stretched across it - two controls spread
        // over a full-width page read as a page missing its other eight.
        {
            auto col = content.reduced (juce::jmax (0, (content.getWidth() - 420) / 2), 0);
            col.removeFromTop (juce::jmax (0, col.getHeight() / 5));

            assignBlurb.setBounds (col.removeFromTop (56));
            col.removeFromTop (18);

            auto row = col.removeFromTop (40);
            const int bw = (row.getWidth() - 16) / 2;
            saveMapBtn.setBounds (row.removeFromLeft  (bw));
            loadMapBtn.setBounds (row.removeFromRight (bw));

            col.removeFromTop (26);
            unlearnBtn.setBounds (col.removeFromTop (36).reduced (bw / 3, 0));
        }

        // GLOBAL SETTINGS page: three trigger buttons, nothing else — every
        // control now lives in the window its feature owns.
        macrosPanel.setBounds (content);

        // SET EDITOR fills the content panel.  It draws its own frame inside
        // this one, which is what every other page here does.
        setEditorTab.setBounds (content);
    }

private:
    static constexpr int kSelectorH = 36;

    juce::Rectangle<int> contentArea() const
    {
        auto b = getLocalBounds().reduced (10);
        b.removeFromTop (kSelectorH + 6);   // selector row + gap
        return b;
    }

    void selectPage (int p)
    {
        currentPage = juce::jlimit (0, (int) kNumPages - 1, p);

        for (int i = 0; i < kNumPages; ++i)
            pageBtn[i].setToggleState (i == currentPage, juce::dontSendNotification);

        const bool note = (currentPage == kNoteMap);
        const bool cc   = (currentPage == kCc);
        const bool reg  = (currentPage == kRegistration);
        const bool glob = (currentPage == kGlobal);
        const bool sete = (currentPage == kSetEditor);

        noteMapView.setVisible (note);
        assignBlurb.setVisible (cc);
        saveMapBtn .setVisible (cc);
        loadMapBtn .setVisible (cc);
        unlearnBtn .setVisible (cc);
        for (juce::Component* c : { (juce::Component*) &regTitle,
                                    (juce::Component*) &regIdLabel,
                                    (juce::Component*) &regIdValue,
                                    (juce::Component*) &regSerialBox,
                                    (juce::Component*) &regButton,
                                    (juce::Component*) &regStatus })
            c->setVisible (reg);

        // Re-read the licence every time the page is opened: registering in one
        // plugin instance leaves every other instance's page showing the old
        // answer until it is looked at again.
        if (reg) refreshRegistrationState();

        macrosPanel.setVisible (glob);
        if (glob) macrosPanel.refreshFromState();

        setEditorTab.setVisible (sete);
        // Re-read the levels every time the page is opened.  It is the only page
        // here showing state another surface can change while it is hidden - a
        // style load rewrites the levels underneath it - so a stale reading
        // would be the normal case rather than the rare one.
        if (sete) setEditorTab.refreshFromState();

        repaint();
    }

    static juce::String buildNoteMapText()
    {
        juce::StringArray L;
        L.add ("Reserved MIDI notes 0-35 trigger arranger");
        L.add ("controls (note-on only).  Note 0 is unused.");
        L.add ("");
        L.add ("STYLE ELEMENTS  (toggle mute)");
        const char* el[8] = { "DRUMS","PERC","BASS","CHORD 1","CHORD 2","PAD","LEAD 1","LEAD 2" };
        for (int i = 0; i < 8; ++i)
            L.add ("  " + pad (juce::String (i + 1)) + el[i]);
        L.add ("");
        L.add ("SOLO CHANNELS  (select)");
        for (int i = 0; i < 6; ++i)
            L.add ("  " + pad (juce::String (i + 9)) + "Solo channel " + juce::String (i + 1));
        L.add ("");
        L.add ("VARIATION PADS  (trigger)");
        const char* vr[16] = { "INTRO 1","INTRO 2","INTRO 3","INTRO 4",
                               "VAR 1","VAR 2","VAR 3","VAR 4",
                               "FILL 1","FILL 2","FILL 3","FILL 4",
                               "END 1","END 2","END 3","BREAK" };
        const int notes[16] = { 15,16,17,18, 19,20,21,22, 23,24,25,26, 27,28,29, 30 };
        for (int i = 0; i < 16; ++i)
            L.add ("  " + pad (juce::String (notes[i])) + vr[i]);
        L.add ("");
        L.add ("TRANSPORT / MODES");
        L.add ("  " + pad ("31") + "PLAY / STOP        (toggle)");
        L.add ("  " + pad ("32") + "RESTART            (trigger)");
        L.add ("  " + pad ("33") + "HOLD               (toggle)");
        L.add ("  " + pad ("34") + "ARRANGER / PIANO   (toggle)");
        L.add ("  " + pad ("35") + "SYNCED PLAY        (arm)");
        return L.joinIntoString ("\n");
    }

    static juce::String pad (juce::String n) { return (n + "   ").substring (0, 5); }

    int currentPage = kNoteMap;

    juce::TextButton pageBtn[kNumPages];

    juce::TextEditor noteMapView;

    juce::Label      assignBlurb;
    juce::TextButton saveMapBtn, loadMapBtn, unlearnBtn;

    /** A Label that can be clicked.  juce::Label deliberately has no onClick;
        this is the smallest thing that adds one without turning the machine ID
        into a button. */
    struct ClickableLabel : public juce::Label
    {
        std::function<void()> onClicked;
        void mouseUp (const juce::MouseEvent&) override { if (onClicked) onClicked(); }
    };

    juce::Label       regTitle, regIdLabel, regStatus;
    ClickableLabel    regIdValue;
    juce::TextEditor  regSerialBox;
    juce::TextButton  regButton;

    void attemptRegistration()
    {
        auto& rm = RegistrationManager::getInstance();

        if (rm.tryRegister (regSerialBox.getText()))
        {
            regSerialBox.clear();
            refreshRegistrationState();
            regStatus.setText ("Registered. Thank you.", juce::dontSendNotification);
        }
        else
        {
            // Deliberately vague.  "Wrong serial" and "wrong machine" are the
            // same message here: telling the user WHICH check failed tells an
            // attacker the same thing, and the honest answer for a support call
            // is the machine ID above either way.
            regStatus.setColour (juce::Label::textColourId, juce::Colour (0xFFCC4433));
            regStatus.setText ("That serial is not valid for this machine.",
                               juce::dontSendNotification);
        }

        // EITHER WAY.  A failed attempt still has to reach the header, because
        // the licence file may have been deleted since the lamp was last drawn
        // and this is the moment somebody is looking at the answer.
        if (onRegistrationChanged) onRegistrationChanged();
    }

    void refreshRegistrationState()
    {
        auto& rm = RegistrationManager::getInstance();
        const bool on = rm.isRegistered();

        regStatus.setColour (juce::Label::textColourId,
                             on ? juce::Colour (0xFF66AA55) : juce::Colour (0xFF8A8A8A));
        regStatus.setText (on ? "REGISTERED"
                              : "Demo mode - audio mutes briefly every 18 seconds.",
                           juce::dontSendNotification);

        regSerialBox.setEnabled (! on);
        regButton   .setEnabled (! on);
        regIdValue  .setText (rm.getMachineIDString(), juce::dontSendNotification);
    }

    // Global Settings controls: FUNKEY MODE.
    GlobalMacrosPanel macrosPanel;
    SetEditorTab      setEditorTab;

    // The CC-number cache and the armed-row index went with the rows they drew.
    // kNumCcTargets stays: the ENGINE still has its five fixed continuous
    // targets and the host counts them by this name.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsTab)
};



