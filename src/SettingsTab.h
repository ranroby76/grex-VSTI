#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

#include "GlobalMacrosPanel.h"
#include "RegistrationManager.h"   // machine-locked serial + demo mode

//==============================================================================
// SettingsTab — reference + control-mapping page, split into sub-pages chosen
// by a horizontal selector at the top:
//
//   • CONTROL NOTE MAP : read-only reference of the reserved MIDI control
//     notes (0–35) and the arranger action each one fires.
//   • MIDI CC CONTROL  : five learnable continuous controllers (master volume,
//     style volume, tempo, transpose, split).  Engine wiring (capture / apply /
//     persistence) is connected by MainComponent through the callbacks below.
//   • REGISTRATION     : placeholder — no licensing backend exists yet.
//   • GLOBAL SETTINGS  : the two global macros — FUNKEY MODE (seven GM-family
//     FX presets) and BIG DRUMS (one global drum rack).  Engage switches plus
//     the buttons that open each editor; the presets themselves live in
//     GlobalMacros and persist to their own files.
//==============================================================================
class SettingsTab : public juce::Component
{
public:
    enum CcTarget { kMasterVol = 0, kStyleVol, kTempo, kTranspose, kSplit, kNumCcTargets };
    enum Page     { kNoteMap = 0, kCc, kRegistration, kGlobal, kNumPages };

    // Fired when the user arms/cancels learn on a target, or clears it.  Wired
    // to the engine by MainComponent.
    std::function<void(int /*target*/)> onLearnRequested;
    std::function<void(int /*target*/)> onForgetRequested;

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
    std::function<void(bool /*on*/)> onBigDrumsToggled;
    std::function<void()>            onMacroFxEdited;

    /** PITCH BEND RANGE moved, in semitones (1..12).  Solo instruments only —
        see BetelgeuseProcessor::setSoloPitchBendRange. */
    std::function<void(int)>         onPitchBendRangeChanged;

    /** LOW VELOCITY RESPONSE moved, 0..100.  Solo instruments only —
        see BetelgeuseProcessor::setSoloLowVelBoost. */
    std::function<void(int)>         onLowVelBoostChanged;

    // onStyleLevelsChanged is gone from this tab.  Style levels are per-STYLE
    // and now live in the SET EDITOR tab, which owns the panel and talks to the
    // host directly.

    SettingsTab()
    {
        // ── Page selector (horizontal) ─────────────────────────────────────
        static const char* pageNames[kNumPages] =
            { "CONTROL NOTE MAP", "MIDI CC CONTROL", "REGISTRATION", "GLOBAL SETTINGS" };

        for (int p = 0; p < kNumPages; ++p)
        {
            pageBtn[p].setButtonText (pageNames[p]);
            pageBtn[p].setClickingTogglesState (false);
            pageBtn[p].setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF1A1A1A));
            pageBtn[p].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFCC6600));
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

        // ── CC-control rows ────────────────────────────────────────────────
        // Named after the CONTROL each one moves, not after the parameter
        // underneath it: the point of this page is "which knob does my pedal
        // grab", and "TRANSPOSE" / "SPLIT POINT" did not match anything
        // written on the left panel.
        static const char* targetNames[kNumCcTargets] =
            { "MASTER VOLUME", "STYLE VOLUME", "TEMPO",
              "GLOBAL SEMI", "SPLIT KEYBOARD" };

        for (int i = 0; i < kNumCcTargets; ++i)
        {
            target[i].setText (targetNames[i], juce::dontSendNotification);
            target[i].setColour (juce::Label::textColourId, juce::Colours::white);
            target[i].setFont (juce::Font (15.0f, juce::Font::bold));
            addAndMakeVisible (target[i]);

            value[i].setJustificationType (juce::Justification::centred);
            value[i].setColour (juce::Label::textColourId, juce::Colour (0xFFD4AF37));
            value[i].setColour (juce::Label::backgroundColourId, juce::Colour (0xFF101010));
            value[i].setFont (juce::Font (15.0f, juce::Font::bold));
            addAndMakeVisible (value[i]);

            learnBtn[i].setButtonText ("LEARN");
            learnBtn[i].onClick = [this, i]
            {
                // Toggle: a second press on the armed row cancels learning.
                const int t = (learningIdx == i) ? -1 : i;
                if (onLearnRequested) onLearnRequested (t);
                setLearning (t);
            };
            addAndMakeVisible (learnBtn[i]);

            forgetBtn[i].setButtonText ("FORGET");
            forgetBtn[i].onClick = [this, i]
            {
                if (onForgetRequested) onForgetRequested (i);
            };
            addAndMakeVisible (forgetBtn[i]);
        }

        // ── Registration ───────────────────────────────────────────────────
        // The placeholder that used to sit here is gone; this page is live.
        auto& rm = RegistrationManager::getInstance();

        regTitle.setText ("REGISTRATION", juce::dontSendNotification);
        regTitle.setJustificationType (juce::Justification::centred);
        regTitle.setColour (juce::Label::textColourId, juce::Colour (0xFFC2C2C2));
        regTitle.setFont (juce::Font (18.0f, juce::Font::bold));
        addAndMakeVisible (regTitle);

        // The ID is READ-ONLY and selectable: the user has to quote it to you
        // accurately, and re-typing a five-digit number off a screen is where
        // support tickets come from.
        regIdLabel.setText ("MACHINE ID", juce::dontSendNotification);
        regIdLabel.setJustificationType (juce::Justification::centred);
        regIdLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF8A8A8A));
        addAndMakeVisible (regIdLabel);

        regIdValue.setText (rm.getMachineIDString(), juce::dontSendNotification);
        regIdValue.setJustificationType (juce::Justification::centred);
        regIdValue.setColour (juce::Label::textColourId, juce::Colour (0xFFCC6600));
        regIdValue.setFont (juce::Font (26.0f, juce::Font::bold));
        regIdValue.setEditable (false);
        // juce::Label has no onClick - it is a display widget, not a button.
        // ClickableLabel below adds one mouseUp override rather than swapping in
        // a TextButton, because the ID has to READ as a value, not as something
        // to press; the copy is a convenience on top, not the point.
        regIdValue.onClicked = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (regIdValue.getText());
            regStatus.setText ("Machine ID copied to clipboard.", juce::dontSendNotification);
        };
        addAndMakeVisible (regIdValue);

        regSerialBox.setTextToShowWhenEmpty ("enter your serial", juce::Colour (0xFF6A6A6A));
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

        // ── Global Settings: the two macro sections ────────────────────────
        macrosPanel.onFunkeyToggled = [this] (bool on)
        { if (onFunkeyModeToggled) onFunkeyModeToggled (on); };
        macrosPanel.onBigDrumsToggled = [this] (bool on)
        { if (onBigDrumsToggled) onBigDrumsToggled (on); };
        macrosPanel.onMacroFxEdited = [this]
        { if (onMacroFxEdited) onMacroFxEdited(); };
        macrosPanel.onPitchBendRangeChanged = [this] (int semis)
        { if (onPitchBendRangeChanged) onPitchBendRangeChanged (semis); };

        macrosPanel.onLowVelBoostChanged = [this] (int amount)
        { if (onLowVelBoostChanged) onLowVelBoostChanged (amount); };
        addAndMakeVisible (macrosPanel);


        // Global Settings: the RIGHT HAND VOLUME and STYLE VOLUME faders that
        // used to live here are gone.  Both buses are owned by the MIXER tab
        // (its STYLE VOLUME / SOLO VOLUME faders drive the same
        // setStyleVolume / setRightHandVolume on the processor), so these were
        // duplicates — and orphaned ones: their callbacks were never wired, so
        // the sliders moved but changed nothing.

        refreshCcLabels();
        selectPage (kNoteMap);
    }

    //==========================================================================
    // Engine → UI: set the controller number assigned to a target (-1 = none).
    void setCcNumber (int targetIdx, int cc)
    {
        if (targetIdx < 0 || targetIdx >= kNumCcTargets) return;
        ccNumber[targetIdx] = cc;
        refreshCcLabels();
    }

    // Engine → UI: mark which target (if any) is currently waiting for a CC.
    void setLearning (int targetIdx)
    {
        learningIdx = targetIdx;
        refreshCcLabels();
    }

    // Bypass-instrument-audio-chain has been retired (the chain is always
    // connected).  Kept as an inert no-op so existing callers — MainComponent's
    // seed + session restore — still compile.
    void setBypassInstrumentChain (bool /*b*/) {}

    /** Relight the macro switches from GlobalMacros.  Called by the host after
        the left-panel BIG DRUMS / FUNKEY MODE buttons flip a flag, and after a
        set restore, so the two surfaces never disagree. */
    void refreshMacroState() { macrosPanel.refreshFromState(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF161616));

        const auto content = contentArea();
        g.setColour (juce::Colour (0xFF0E0E0E));
        g.fillRoundedRectangle (content.toFloat(), 6.0f);
        g.setColour (juce::Colour (0xFF3A3322));
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

        // REGISTRATION placeholder fills the content panel.
        {
            // Centred column, capped width - a five-digit number and one text
            // field stretched across a full-width tab reads as a form to fill
            // in rather than the two-step exchange it actually is.
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

        // MIDI CC rows stacked in the content panel.
        auto rows = content;
        const int rowH = juce::jmin (56, juce::jmax (1, rows.getHeight() / kNumCcTargets));
        for (int i = 0; i < kNumCcTargets; ++i)
        {
            auto row = rows.removeFromTop (rowH).reduced (0, 4);
            const int w = row.getWidth();
            target [i].setBounds (row.removeFromLeft ((int) (w * 0.38f)));
            value  [i].setBounds (row.removeFromLeft ((int) (w * 0.18f)).reduced (2, 6));
            const int bw = (row.getWidth() - 6) / 2;
            learnBtn [i].setBounds (row.removeFromLeft (bw).reduced (2, 6));
            forgetBtn[i].setBounds (row.removeFromRight (bw).reduced (2, 6));
        }

        // GLOBAL SETTINGS page: three trigger buttons, nothing else — every
        // control now lives in the window its feature owns.
        macrosPanel.setBounds (content);
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

        noteMapView.setVisible (note);
        for (int i = 0; i < kNumCcTargets; ++i)
        {
            target  [i].setVisible (cc);
            value   [i].setVisible (cc);
            learnBtn[i].setVisible (cc);
            forgetBtn[i].setVisible (cc);
        }
        for (juce::Component* c : { (juce::Component*) &regTitle,  (juce::Component*) &regIdLabel,
                                    (juce::Component*) &regIdValue, (juce::Component*) &regSerialBox,
                                    (juce::Component*) &regButton, (juce::Component*) &regStatus })
            c->setVisible (reg);
        macrosPanel.setVisible (glob);
        if (glob) macrosPanel.refreshFromState();

        repaint();
    }

    void refreshCcLabels()
    {
        for (int i = 0; i < kNumCcTargets; ++i)
        {
            if (learningIdx == i)
            {
                value[i].setText ("LEARN...", juce::dontSendNotification);
                value[i].setColour (juce::Label::textColourId, juce::Colours::orange);
            }
            else
            {
                value[i].setText (ccNumber[i] >= 0 ? ("CC " + juce::String (ccNumber[i]))
                                                   : juce::String ("-"),
                                  juce::dontSendNotification);
                value[i].setColour (juce::Label::textColourId, juce::Colour (0xFFD4AF37));
            }
            learnBtn[i].setButtonText (learningIdx == i ? "CANCEL" : "LEARN");
        }
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

    juce::Label      target   [kNumCcTargets];
    juce::Label      value    [kNumCcTargets];
    juce::TextButton learnBtn [kNumCcTargets];
    juce::TextButton forgetBtn[kNumCcTargets];

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

    // Global Settings controls: FUNKEY MODE + BIG DRUMS.
    GlobalMacrosPanel macrosPanel;

    // Defaults per spec: vol=CC7, style-vol=CC11, tempo=CC16, transpose=CC17, split=CC18.
    int ccNumber[kNumCcTargets] = { 7, 11, 16, 17, 18 };
    int learningIdx = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsTab)
};



