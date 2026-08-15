#pragma once
#include <JuceHeader.h>
#include <array>
#include "InstrEditPanel.h"    // GoldSlider — the plugin's own fader look
                               // (pulls JuceHeader, hence the switch above)

//==============================================================================
//  CrashTab — automatic crash-cymbal triggers.
//
//   • CRASH ON TRANSITION : hit a crash whenever the arranger changes section
//     (fill / variation / break / ending).
//   • AUTO CRASH          : hit a crash every N loop cycles of the current
//     section.
//   • CRASH NOW           : manual one-shot.
//
//  The tab is pure UI; the host wires the callbacks to the processor and pushes
//  the initial state via the setters.
//==============================================================================
class CrashTab : public juce::Component
{
public:
    std::function<void(bool)> onCrashOnTransitionChanged;
    std::function<void(bool)> onAutoCrashEnabledChanged;
    std::function<void(int)>  onAutoCrashEveryNChanged;
    /** The fixed velocity every crash hits at, 1..127. */
    std::function<void(int)>  onCrashVelocityChanged;

    /** The crash's BASE GAIN, 0..200 with 100 = unity.  Velocity picks which
        layer of the cymbal speaks, so it changes the character of the hit as
        well as its level; the gain is how loud that hit sits in the mix. */
    std::function<void(float)> onCrashGainChanged;

    /** Double-click on the GAIN handle — the host opens the dB text box, the
        same gesture the instrument gain uses for BASE UNITY. */
    std::function<void()>      onCrashGainDialogRequested;
    /** SAVE pressed — write every crash setting to the plugin root. */
    std::function<void()>     onSaveSettings;
    std::function<void()>     onManualCrash;
    // Which drum elements the crash fires: bit 0 = 46, 1 = 55, 2 = 56, 3 = 57.
    std::function<void(juce::uint8 mask)> onCrashNotesChanged;

    CrashTab()
    {
        // VELOCITY — a fixed weight, not a range.  Whatever the slider reads is
        // what every crash hits at, so the cymbal lands the same way each time
        // and the number on screen IS the velocity.  1 rather than 0 at the
        // bottom: velocity 0 is a note-off in MIDI, never a silent hit.
        velSlider.setStep (1.0f);
        velSlider.onChange = [this] (float v)
        {
            if (onCrashVelocityChanged) onCrashVelocityChanged ((int) v);
        };
        addAndMakeVisible (velSlider);

        toggleTransition.onClick = [this]
        {
            transitionOn = ! transitionOn;
            refreshToggle (toggleTransition, transitionOn);
            if (onCrashOnTransitionChanged) onCrashOnTransitionChanged (transitionOn);
        };
        addAndMakeVisible (toggleTransition);

        toggleAuto.onClick = [this]
        {
            autoOn = ! autoOn;
            refreshToggle (toggleAuto, autoOn);
            if (onAutoCrashEnabledChanged) onAutoCrashEnabledChanged (autoOn);
        };
        addAndMakeVisible (toggleAuto);

        btnNMinus.setButtonText ("-");
        btnNPlus .setButtonText ("+");
        btnNMinus.onClick = [this] { setEveryN (everyN - 1); };
        btnNPlus .onClick = [this] { setEveryN (everyN + 1); };
        addAndMakeVisible (btnNMinus);
        addAndMakeVisible (btnNPlus);

        lblN.setJustificationType (juce::Justification::centred);
        lblN.setColour (juce::Label::textColourId, juce::Colour (0xFFD4AF37));
        lblN.setColour (juce::Label::backgroundColourId, juce::Colour (0xFF101010));
        lblN.setFont (juce::Font (18.0f, juce::Font::bold));
        addAndMakeVisible (lblN);

        // The bottom strip is two halves of one control: the left FIRES a crash,
        // the right KEEPS the settings that shape it.  Same size and the same
        // rust fill so they read as a pair, with a hairline between them rather
        // than a gap — the divider marks two jobs, not two controls.
        btnCrashNow.setButtonText ("CRASH NOW");
        btnCrashNow.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF7A2E00));
        btnCrashNow.onClick = [this] { if (onManualCrash) onManualCrash(); };
        addAndMakeVisible (btnCrashNow);

        btnSave.setButtonText ("SAVE");
        btnSave.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF7A2E00));
        btnSave.onClick = [this] { if (onSaveSettings) onSaveSettings(); };
        addAndMakeVisible (btnSave);

        // ── BASE GAIN ────────────────────────────────────────────────────────
        //
        // Beside VEL rather than instead of it.  They are not the same control:
        // velocity chooses WHICH layer of the cymbal sample speaks, so turning
        // it down to quieten a crash also makes it a different, softer-struck
        // cymbal.  When the hit is right but too loud for this style, the gain
        // is the honest fix.
        //
        // Double-click opens a dB text box, exactly as the instrument GAIN
        // handle does for BASE UNITY — one gesture, one meaning, everywhere.
        gainSlider.setStep (1.0f);
        gainSlider.onChange = [this] (float v)
        {
            if (seedingGain) return;
            if (onCrashGainChanged) onCrashGainChanged (v);
        };
        gainSlider.onLeftDoubleClick = [this]
        {
            if (onCrashGainDialogRequested) onCrashGainDialogRequested();
        };
        addAndMakeVisible (gainSlider);

        savedMsg.setJustificationType (juce::Justification::centred);
        savedMsg.setFont (juce::Font (13.0f, juce::Font::bold));
        addAndMakeVisible (savedMsg);

        // Vertical multi-selector: which drum element(s) produce the crash.
        // Any combination may be on; default is 57 OPEN D only.
        for (int i = 0; i < 4; ++i)
        {
            noteToggles[(size_t) i].setButtonText (kNoteLabels[i]);
            noteToggles[(size_t) i].onClick = [this, i]
            {
                noteSel[(size_t) i] = ! noteSel[(size_t) i];
                refreshNoteToggle (i);
                if (onCrashNotesChanged) onCrashNotesChanged (notesMask());
            };
            addAndMakeVisible (noteToggles[(size_t) i]);
            refreshNoteToggle (i);
        }

        refreshToggle (toggleTransition, transitionOn);
        refreshToggle (toggleAuto, autoOn);
        refreshN();
    }

    //==========================================================================
    // Engine → UI initial state.
    void setCrashOnTransition (bool b) { transitionOn = b; refreshToggle (toggleTransition, b); }
    void setAutoCrashEnabled  (bool b) { autoOn = b;       refreshToggle (toggleAuto, b); }
    void setAutoCrashEveryN   (int n)  { everyN = juce::jlimit (1, 64, n); refreshN(); }
    void setCrashVelocity     (int v)  { velSlider.setValue ((float) juce::jlimit (1, 127, v)); }

    /** Host -> tab, WITHOUT firing onCrashGainDbChanged.  Used when a set is
        loaded: the value is already on the processor and echoing it back would
        be a pointless round trip. */
    void setCrashGainPercent (float pct)
    {
        seedingGain = true;
        gainSlider.setValue (juce::jlimit (0.0f, 100.0f, pct));
        seedingGain = false;
    }

    float crashGainPercent() const     { return gainSlider.getValue(); }

    /** Report the outcome of SAVE next to the button. */
    void setSaveResult (bool ok)
    {
        savedMsg.setColour (juce::Label::textColourId,
                            ok ? juce::Colour (0xFF00C853) : juce::Colour (0xFFE53935));
        savedMsg.setText (ok ? "saved as the default template" : "SAVE FAILED",
                          juce::dontSendNotification);
    }
    int  crashVelocity() const         { return (int) velSlider.getValue(); }
    void setCrashNotesMask (juce::uint8 m)
    {
        for (int i = 0; i < 4; ++i) { noteSel[(size_t) i] = (m >> i) & 1; refreshNoteToggle (i); }
    }
    juce::uint8 notesMask() const
    {
        juce::uint8 m = 0;
        for (int i = 0; i < 4; ++i) if (noteSel[(size_t) i]) m |= (juce::uint8) (1u << i);
        return m;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF161616));
        const auto cols = columns();
        paintSection (g, cols.first,  "CRASH ON TRANSITION");
        paintSection (g, cols.second, "AUTO CRASH");

        // Hairline between CRASH NOW and SAVE — the same grey sub-pixel line the
        // stacked left-panel buttons and the selectors use, since this is the
        // same idea: a divider inside one control, not a border between two.
        if (separatorH > 0)
        {
            g.setColour (juce::Colour (0xFF9A9A9A).withAlpha (0.30f));
            g.fillRect ((float) separatorX, (float) separatorY + 6.0f,
                        0.6f, (float) separatorH - 12.0f);
        }

        g.setColour (juce::Colour (0xFFAAAAAA));
        g.setFont (juce::Font (13.0f));
        g.drawFittedText ("Crash on every section change\n(fill / variation / break / ending).",
                          cols.first.reduced (16).withTrimmedTop (40).removeFromTop (50),
                          juce::Justification::topLeft, 3);
        g.drawFittedText ("Crash every N loop cycles of the\ncurrent section.",
                          cols.second.reduced (16).withTrimmedTop (40).removeFromTop (50),
                          juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        const auto cols = columns();

        // ── THE TWO ON/OFF TOGGLES ARE SQUARE ────────────────────────────────
        //
        // 44 x 44 rather than 140 wide.  "ON" is three characters; the other
        // 96 px were carrying nothing, and on this page they were carrying it
        // right where the new GAIN fader needed to stand.  Square also matches
        // the +/- steppers below, which are already 44.
        const int kToggleSide = 44;

        auto l = cols.first.reduced (16).withTrimmedTop (96);
        auto row = l.removeFromTop (kToggleSide);
        toggleTransition.setBounds (row.removeFromLeft (kToggleSide));
        row.removeFromLeft (12);

        // Vertical multi-selector right of the toggle (4 stacked note buttons).
        {
            const int stackH = 4 * 30 + 3 * 6;
            const int velW    = 62;
            const int bw = juce::jmin (150, juce::jmax (90, row.getWidth() - velW - 12));

            juce::Rectangle<int> stack (row.getX(), row.getY(), bw, stackH);
            for (int i = 0; i < 4; ++i)
            {
                noteToggles[(size_t) i].setBounds (stack.removeFromTop (30));
                stack.removeFromTop (6);
            }

            // The velocity fader stands to the right of the note selectors and
            // runs the FULL remaining height of the section, not the stack's:
            // it governs whichever of them are lit, and it is the one control
            // here that rewards travel.
            //
            // At the stack's height it was mostly furniture.  GoldSlider
            // reserves kVerticalReserved (88 px: the value text, the label, and
            // the handle's clearance at both ends) before any track is drawn —
            // so a 138 px fader had only ~50 px of actual TRACK, i.e. a 127-step
            // control with a 50 px throw.  That is why it read as too short and
            // dragged too coarsely: height the fader does not have is height the
            // track does not get, and the fixed 88 px does not shrink.
            //
            // Filling the section puts the track near 143 px — close to 3x the
            // usable travel — and costs nothing, because everything below the
            // note stack was empty space inside the same border.
            //
            // Measured from the LIVE bounds rather than hardcoded: the page is
            // resized with the tab canvas, so a fixed number would only be right
            // at the design window size.  The jmax floor keeps the fader from
            // collapsing under the stack if the tab is ever driven very short.
            const int velH = juce::jmax (stackH, l.getBottom() - row.getY());

            // VEL and GAIN side by side, same height.  They are a pair — how
            // hard the crash is struck and how loud that strike sits — and
            // reading one without the other tells you half the story.
            // Both faders start after the note stack and share what is left of
            // the column, so neither can run under the AUTO CRASH panel to the
            // right — which is what the old fixed widths did once GAIN arrived.
            const int fadersX = row.getX() + bw + 12;
            const int avail   = juce::jmax (2 * 40 + 8, l.getRight() - fadersX);
            const int fw      = juce::jmin (velW, (avail - 8) / 2);

            velSlider .setBounds (fadersX,           row.getY(), fw, velH);
            gainSlider.setBounds (fadersX + fw + 8,  row.getY(), fw, velH);
        }

        auto r = cols.second.reduced (16).withTrimmedTop (96);
        toggleAuto.setBounds (r.removeFromTop (kToggleSide).removeFromLeft (kToggleSide));
        r.removeFromTop (14);

        auto stepRow = r.removeFromTop (44);
        const int sw = 44;
        btnNMinus.setBounds (stepRow.removeFromLeft (sw));
        stepRow.removeFromLeft (6);
        lblN.setBounds (stepRow.removeFromLeft (90));
        stepRow.removeFromLeft (6);
        btnNPlus.setBounds (stepRow.removeFromLeft (sw));

        // Bottom strip: CRASH NOW | SAVE, split down the middle, with the save
        // result reported just above so pressing SAVE says something.
        auto strip = getLocalBounds().reduced (10).removeFromBottom (46 + 18);
        savedMsg.setBounds (strip.removeFromTop (18));

        const int half = strip.getWidth() / 2;
        btnCrashNow.setBounds (strip.removeFromLeft (half - 1));
        strip.removeFromLeft (2);                 // the 2 px the separator sits in
        btnSave.setBounds (strip);
        separatorX = btnCrashNow.getRight();
        separatorY = strip.getY();
        separatorH = strip.getHeight();
    }

private:
    std::pair<juce::Rectangle<int>, juce::Rectangle<int>> columns() const
    {
        auto b = getLocalBounds().reduced (10).withTrimmedBottom (56);
        const int leftW = b.getWidth() / 2 - 5;
        auto left = b.removeFromLeft (leftW);
        b.removeFromLeft (10);
        return { left, b };
    }

    void paintSection (juce::Graphics& g, juce::Rectangle<int> rr, const juce::String& title)
    {
        g.setColour (juce::Colour (0xFF0E0E0E));
        g.fillRoundedRectangle (rr.toFloat(), 6.0f);
        g.setColour (juce::Colour (0xFF3A3322));
        g.drawRoundedRectangle (rr.toFloat(), 6.0f, 1.5f);
        g.setColour (juce::Colour (0xFFD4AF37));
        g.setFont (juce::Font (16.0f, juce::Font::bold));
        g.drawText (title, rr.withHeight (30).reduced (12, 4), juce::Justification::centredLeft);
    }

    static void refreshToggle (juce::TextButton& b, bool on)
    {
        b.setButtonText (on ? "ON" : "OFF");
        b.setColour (juce::TextButton::buttonColourId,
                     on ? juce::Colour (0xFFCC6600) : juce::Colour (0xFF2A2A2A));
        // Black on the amber ON fill; white on the inert one.
        b.setColour (juce::TextButton::textColourOnId,
                     on ? juce::Colours::black : juce::Colours::white);
        b.setColour (juce::TextButton::textColourOffId,
                     on ? juce::Colours::black : juce::Colours::white);
        b.repaint();
    }

    void setEveryN (int n)
    {
        everyN = juce::jlimit (1, 64, n);
        refreshN();
        if (onAutoCrashEveryNChanged) onAutoCrashEveryNChanged (everyN);
    }

    void refreshN()
    {
        lblN.setText (juce::String (everyN) + (everyN == 1 ? " cycle" : " cycles"),
                      juce::dontSendNotification);
    }

    void refreshNoteToggle (int i)
    {
        auto& b = noteToggles[(size_t) i];
        const bool on = noteSel[(size_t) i];
        b.setColour (juce::TextButton::buttonColourId,
                     on ? juce::Colour (0xFFCC6600) : juce::Colour (0xFF2A2A2A));
        // Black on the amber ON fill; white on the inert one.
        b.setColour (juce::TextButton::textColourOnId,
                     on ? juce::Colours::black : juce::Colours::white);
        b.setColour (juce::TextButton::textColourOffId,
                     on ? juce::Colours::black : juce::Colours::white);
        b.repaint();
    }

    // Must stay in step with kCrashNotes in Main.cpp — and now names what each
    // key actually is, rather than "OPEN B/C/D".
    static constexpr const char* kNoteLabels[4] = { "49 CRASH 1", "55 SPLASH",
                                                    "52 CHINESE", "57 CRASH 2" };

    juce::TextButton toggleTransition, toggleAuto;
    juce::TextButton btnNMinus, btnNPlus, btnCrashNow, btnSave;
    juce::Label      savedMsg;

    // Geometry of the hairline between the two halves, set in resized().
    int separatorX = 0, separatorY = 0, separatorH = 0;
    std::array<juce::TextButton, 4> noteToggles;
    std::array<bool, 4>             noteSel { false, false, false, true };  // 57 default
    juce::Label      lblN;

    bool transitionOn = false;
    bool autoOn       = false;
    int  everyN       = 4;

    // 110 was the value hardcoded in triggerCrash, so an untouched install
    // sounds exactly as it did before the slider existed.
    GoldSlider velSlider  { "VEL",  1.0f, 127.0f, 110.0f, "" };
    // 0 = silence, 100 = UNITY on the centre detent, 200 = +10 dB.  See
    // BetelgeuseProcessor::getCrashGainLinear for why it is not a dB scale.
    // 0 = silence, 75 = unity, 100 = +6 dB — see
    // BetelgeuseProcessor::setCrashGainPercent for why unity moved off centre.
    GoldSlider gainSlider { "GAIN", 0.0f, 100.0f, 75.0f, "" };
    bool       seedingGain = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrashTab)
};
