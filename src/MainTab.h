
#pragma once
#include <JuceHeader.h>

// =====================================================================================
//  LedButton
//  Toggle or Push button with a LED bar on top.
//  LED: dark red when off, shiny red when on (override via setLedColourOn)
// =====================================================================================
class LedButton : public juce::TextButton
{
public:
    enum class Mode { Toggle, Push, Selector };

    LedButton(const juce::String& text = {}, Mode mode = Mode::Toggle)
        : juce::TextButton(text), buttonMode(mode)
    {
        setClickingTogglesState(mode == Mode::Toggle || mode == Mode::Selector);
    }

    void setMode(Mode m)
    {
        buttonMode = m;
        setClickingTogglesState(m == Mode::Toggle || m == Mode::Selector);
    }

    void setSubLabel(const juce::String& s) { subLabel = s; repaint(); }
    void setLedColourOn(juce::Colour c)     { ledOn = c;    repaint(); }
    void setAlternateText(const juce::String& onText) { altText = onText; repaint(); }

    /** Currently-playing pip — used by MainTab to show which variation
        the StyleSequencer is actually playing right now (distinct from
        the user's most-recent click which sets the toggle state). */
    void setPlaying(bool p) { if (p == isPlaying) return; isPlaying = p; repaint(); }
    bool getPlaying() const noexcept { return isPlaying; }

    /** When set, ALL visual highlight state (background, border, LED bar,
        altText) follows `isPlaying` rather than the toggle/click state.
        This is what variation buttons want: clicking a button queues the
        switch but the button itself only lights up once the sequencer has
        actually moved to that section.  Default false preserves the
        toggle-driven behaviour used by Play/Stop, Sync, Solo, etc. */
    void setLedFollowsPlayingOnly(bool b) { if (b == ledFollowsPlayingOnly) return; ledFollowsPlayingOnly = b; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        const float ledH = juce::jmax(5.0f, bounds.getHeight() * 0.10f);

        const bool toggleOn    = (buttonMode == Mode::Push) ? isButtonDown : getToggleState();

        // UNAVAILABLE = the style has no such section.  Drawn dim and, crucially,
        // its LED can never light: the button stays on screen so the panel keeps
        // its shape, but it reads as absent rather than as merely unlit.
        const bool avail = isEnabled();

        // ── LED ONLY, FOR VARIATION BUTTONS ─────────────────────────────────
        //
        // The background used to follow the CLICK while the LED followed what
        // was actually playing.  Two indicators for one question: after a
        // switch the old section kept a bright panel while the new one had the
        // lit LED, so the brightest thing on screen was the variation that had
        // just STOPPED.  A player reads brightness first and the small red bar
        // second, so the panel was actively telling them the wrong section.
        //
        // Now ledFollowsPlayingOnly means exactly that - the LED is the whole
        // indication and the background never highlights.  Hover and press
        // feedback below are untouched; those answer "am I about to click
        // this", not "what is playing".
        const bool highlightOn = ledFollowsPlayingOnly ? false : toggleOn;
        const bool ledOnState  = avail && (ledFollowsPlayingOnly ? isPlaying : toggleOn);

        // altText has to follow whatever this button uses as its lit state, or
        // a variation button would never show it now that highlightOn is false.
        const bool textLit     = ledFollowsPlayingOnly ? ledOnState : highlightOn;

        // Background
        if (! avail)
            g.setColour(juce::Colour(0xFF151515));      // sunk below the panel
        else if (highlightOn)
            g.setColour(juce::Colour(0xFF3A3A3A));
        else if (isButtonDown)
            g.setColour(juce::Colour(0xFF2E2E2E));
        else if (isHighlighted)
            g.setColour(juce::Colour(0xFF2A2A2A));
        else
            g.setColour(juce::Colour(0xFF1E1E1E));

        g.fillRoundedRectangle(bounds.reduced(0.5f), 4.0f);

        // Border
        g.setColour(! avail      ? juce::Colour(0xFF2A2A2A)
                   : highlightOn ? juce::Colour(0xFF666666)
                                 : juce::Colour(0xFF444444));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        // LED bar
        auto ledArea = juce::Rectangle<float>(
            bounds.getX() + 3.0f, bounds.getY() + 2.5f,
            bounds.getWidth() - 6.0f, ledH - 1.5f);

        // Two dim levels: an available-but-silent LED still reads as a lamp
        // that could light, an unavailable one has to read as no lamp at all.
        const juce::Colour ledDim = avail ? juce::Colour(0xFF400000)
                                          : juce::Colour(0xFF1C1010);
        const juce::Colour ledLit = ledOn;
        g.setColour(ledOnState ? ledLit : ledDim);
        g.fillRoundedRectangle(ledArea, 1.5f);

        if (ledOnState)
        {
            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.fillRoundedRectangle(ledArea.reduced(0.5f, 0.5f).withHeight(ledArea.getHeight() * 0.45f), 1.0f);
        }

        // Text
        juce::String displayText = (textLit && altText.isNotEmpty()) ? altText : getButtonText();
        g.setColour(juce::Colours::white.withAlpha(avail ? 0.92f : 0.28f));

        if (subLabel.isNotEmpty())
        {
            auto top    = bounds.withY(bounds.getY() + ledH + 2.0f)
                                .withHeight(bounds.getHeight() * 0.40f);
            auto bottom = juce::Rectangle<float>(bounds.getX(),
                                                  top.getBottom(),
                                                  bounds.getWidth(),
                                                  bounds.getBottom() - top.getBottom() - 2.0f);
            g.setFont(juce::Font(juce::jmin(top.getHeight() * 0.65f, 13.0f), juce::Font::bold));
            g.drawFittedText(displayText, top.toNearestInt().reduced(2), juce::Justification::centred, 1);
            g.setFont(juce::Font(juce::jmin(bottom.getHeight() * 0.55f, 11.0f)));
            g.setColour(juce::Colours::white.withAlpha(avail ? 0.65f : 0.22f));
            g.drawFittedText(subLabel, bottom.toNearestInt().reduced(2), juce::Justification::centred, 2);
        }
        else
        {
            auto textBounds = bounds.withY(bounds.getY() + ledH + 2.0f)
                                    .withHeight(bounds.getHeight() - ledH - 4.0f);
            g.setFont(juce::Font(juce::jmin(textBounds.getHeight() * 0.45f, 14.0f), juce::Font::bold));
            g.drawFittedText(displayText, textBounds.toNearestInt().reduced(2), juce::Justification::centred, 2);
        }

        // (Currently-playing green pip removed.  Variation buttons now use
        //  the red LED bar as the sole "currently playing" indicator via
        //  setLedFollowsPlayingOnly(true); other LedButton uses are
        //  unaffected.)
    }

private:
    Mode buttonMode = Mode::Toggle;
    juce::Colour ledOn { 0xFFFF2020 };
    juce::String subLabel;
    juce::String altText;
    bool isPlaying = false;
    bool ledFollowsPlayingOnly = false;
};


// =====================================================================================
//  MainTab — Canvas: 918 x 435 (design coords)
//
//  This tab is the arranger's panel.  It owns the 16 variation buttons
//  (3 intros + 4 mains + 4 fills + break + 3 endings, plus one spare),
//  the 8 solo selectors, the 8 style-element on/off buttons, and the
//  play-control row (sync-play, play/stop, on-press, crash, fingered,
//  restart).
//
//  Wiring to the engine is OUTSIDE this class: MainTab fires generic
//  callbacks and MainComponent translates them into StyleSequencer
//  commands.  This keeps MainTab UI-only and engine-agnostic.
//
//  Variation-button index layout (kVariNames in the .cpp):
//       0..3   INTRO 1-4     (only 1-3 map to IntroA/B/C; index 3 is a
//                             spare with no underlying section)
//       4..7   VAR 1-4       → Main A/B/C/D
//       8..11  FILL 1-4      → Fill AA/BB/CC/DD
//       12     BRAKE         → FillBA (the "Break" section)
//       13..15 END 1-3       → Ending A/B/C
// =====================================================================================
class MainTab : public juce::Component
{
public:
    MainTab();
    ~MainTab() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ── Transport callbacks (wired by MainComponent) ──────────────────────────
    //
    // onVariationClicked fires AFTER setSelectedVariation() has updated the
    // LED row, so the host doesn't have to worry about visual state.
    std::function<void(int variIndex)> onVariationClicked;
    std::function<void(bool wantsPlay)> onPlayStopToggled;
    std::function<void(bool on)>        onSyncPlayToggled;
    std::function<void()>               onCrashClicked;
    std::function<void()>               onRestartClicked;

    /** Fires when a STYLE ELEMENTS ON/OFF button is clicked.  `on` is the
        new toggle state of that button (true = element ON / unmuted). */
    std::function<void(int slotIndex, bool on)> onStyleElementToggled;

    /** Fires whenever any SOLO ELEMENTS button changes state, passing the
        full 8-bit enable mask (bit N = slot N).  The host routes upper-
        keyboard input to all enabled slots; when mask is 0 it falls back
        to the legacy single-slot routing via activeSoloSlot. */
    std::function<void(juce::uint8 mask)> onSoloMaskChanged;

    /** Fires when ONPRESS (sync-stop) toggles. */
    std::function<void(bool on)> onSyncStopToggled;

    /** Fires when FINGERED / 1 FINGER toggles.  `fingeredMode` = true means
        full-chord recognition; false means Yamaha single-finger mode. */
    std::function<void(bool fingeredMode)> onChordModeToggled;

    /** NO-OP, kept so the host's per-frame call site does not have to change.

        The status strip it used to write to is gone - it repeated the style
        name, tempo and time signature that the left panel already shows. */
    void setStyleInfo (const juce::String&, float, int, int) {}

    /** Drives the variation-button LEDs as ONE mutually-exclusive selector:
        exactly one of the 16 buttons is lit — the section that is active
        right now — and every other LED is off.  The host guarantees a valid
        index is always passed (never -1), so a button is never blank: while a
        section plays its own button is lit; when stopped the last-played main
        variation's button stays lit.  The LED moves only when the active
        section actually changes. */
    void setCurrentlyPlayingVariation (int activeBtn) noexcept
    {
        for (int i = 0; i < 16; ++i)
            variButtons[i].setPlaying (i == activeBtn);
    }

    // Called by MainComponent when Single/Multi selector changes
    void setSoloSingleMode(bool isSingle);

    // Set instrument name shown below channel label
    // Solo channels use MIDI ch 17-24 (indices 0-7)
    void setSoloInstrumentName(int index, const juce::String& name)
    {
        jassert(index >= 0 && index < 8);
        soloButtons[index].setSubLabel(name);
    }

    // Style channels use 8 roles: DRUMS / PERC / BASS / CHORD 1 / CHORD 2 /
    // PAD / LEAD 1 / LEAD 2 (matching SoundsTab::kStyleRoleNames).
    void setStyleInstrumentName(int index, const juce::String& name)
    {
        jassert(index >= 0 && index < 8);
        styleButtons[index].setSubLabel(name);
    }

    // ── Accessors for engine hookup ───────────────────────────────────────────
    LedButton& getSoloButton(int i)  { jassert(i >= 0 && i < 8);  return soloButtons[i]; }
    LedButton& getStyleButton(int i) { jassert(i >= 0 && i < 8);  return styleButtons[i]; }
    LedButton& getVariButton(int i)  { jassert(i >= 0 && i < 16); return variButtons[i]; }
    LedButton& getBtnSyncPlay()      { return btnSyncPlay; }
    LedButton& getBtnPlayStop()      { return btnPlayStop; }
    LedButton& getBtnOnPress()       { return btnOnPress;  }
    LedButton& getBtnCrash()         { return btnCrash;    }
    LedButton& getBtnFingered()      { return btnFingered; }
    LedButton& getBtnRestart()       { return btnRestart;  }

    int  getSelectedVariation() const { return selectedVariation; }
    void setSelectedVariation(int i, bool notify = false);

    /** Which of the 16 variation buttons the loaded style can actually play.

        A style is not obliged to carry all fifteen sections - two intros
        instead of three is common, and plenty have no break or only one
        ending.  Those buttons used to look identical to the working ones and
        simply did nothing when pressed, which reads as a broken plugin rather
        than as a style that stops at INTRO 2.

        setEnabled does double duty: it blocks the click AND is what
        LedButton::paintButton reads to draw the dimmed state, so availability
        has one owner instead of a flag that could disagree with the button. */
    void setVariationAvailable (int idx, bool available)
    {
        if (idx < 0 || idx >= 16) return;
        if (variButtons[(size_t) idx].isEnabled() == available) return;
        variButtons[(size_t) idx].setEnabled (available);
        variButtons[(size_t) idx].repaint();
    }

    /** No style loaded: everything is playable again, so the panel does not sit
        greyed out between loads. */
    void setAllVariationsAvailable()
    {
        for (int i = 0; i < 16; ++i) setVariationAvailable (i, true);
    }

    /** Force-set the PLAY/STOP visual state without firing the callback.
        Used by the host when the sequencer transport changes from a non-UI
        source (e.g. external MIDI start). */
    void setPlayStopVisual (bool playing) noexcept
    {
        btnPlayStop.setToggleState (playing, juce::dontSendNotification);
        btnPlayStop.setButtonText  (playing ? "STOP" : "PLAY");
    }

private:
    // ── Section labels ────────────────────────────────────────────────────────
    juce::Label lblSolo, lblStyle, lblVariations, lblPlayControl;

    // ── Solo elements (8) — MIDI ch 17-24 ────────────────────────────────────
    std::array<LedButton, 8>  soloButtons;
    bool soloSingleMode = true;

    // ── Style elements (8) — the 8 fixed Yamaha-arranger roles ───────────────
    // DRUMS / PERC / BASS / CHORD 1 / CHORD 2 / PAD / LEAD 1 / LEAD 2
    std::array<LedButton, 8> styleButtons;

    // ── Variations (16 selector buttons, always one active) ───────────────────
    std::array<LedButton, 16> variButtons;
    int selectedVariation = 4;    // default: VAR 1 (index 4)

    // ── Play controls ─────────────────────────────────────────────────────────
    LedButton btnSyncPlay { "SYNC\nPLAY",  LedButton::Mode::Toggle };
    LedButton btnPlayStop { "PLAY",        LedButton::Mode::Toggle };
    LedButton btnOnPress  { "ONPRESS",     LedButton::Mode::Toggle };
    LedButton btnCrash    { "CRASH",       LedButton::Mode::Push   };
    LedButton btnFingered { "1 FINGER",    LedButton::Mode::Toggle };
    LedButton btnRestart  { "RESTART",     LedButton::Mode::Push   };

    // ── Layout helpers ────────────────────────────────────────────────────────
    void layoutRow(juce::Component** items, int count, int x, int y, int w, int h, int gap);
    static void initSectionLabel(juce::Label& lbl, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainTab)
};
