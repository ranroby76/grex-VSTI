
#include "MainTab.h"

// ── Static name tables ────────────────────────────────────────────────────────

static const juce::StringArray kSoloNames {
    "CH. 17", "CH. 18", "CH. 19", "CH. 20",
    "CH. 21", "CH. 22", "CH. 23", "CH. 24"
};

static const juce::StringArray kStyleNames {
    "DRUMS",   "PERC",    "BASS",    "CHORD 1",
    "CHORD 2", "PAD",     "LEAD 1",  "LEAD 2"
};

static const juce::StringArray kVariNames {
    "INTRO 1", "INTRO 2", "INTRO 3", "INTRO 4",
    "VAR 1",   "VAR 2",   "VAR 3",   "VAR 4",
    "FILL 1",  "FILL 2",  "FILL 3",  "FILL 4",
    "BRAKE",   "END 1",   "END 2",   "END 3"
};

// =====================================================================================
//  Helpers
// =====================================================================================
void MainTab::initSectionLabel(juce::Label& lbl, const juce::String& text)
{
    lbl.setText(text, juce::dontSendNotification);
    lbl.setFont(juce::Font(12.5f, juce::Font::bold));
    lbl.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    lbl.setJustificationType(juce::Justification::centredLeft);
    lbl.setOpaque(false);
}

void MainTab::layoutRow(juce::Component** items, int count,
                        int x, int y, int totalW, int h, int gap)
{
    if (count <= 0) return;
    const int totalGaps = gap * (count - 1);
    const float btnW = (float)(totalW - totalGaps) / (float)count;

    for (int i = 0; i < count; ++i)
    {
        const int bx = x + (int)std::round(i * (btnW + (float)gap));
        const int bw = (i == count - 1) ? (x + totalW - bx) : (int)std::round(btnW);
        items[i]->setBounds(bx, y, bw, h);
    }
}

// =====================================================================================
//  setSelectedVariation
// =====================================================================================
void MainTab::setSelectedVariation(int i, bool /*notify*/)
{
    i = juce::jlimit(0, 15, i);
    selectedVariation = i;
    for (int j = 0; j < 16; ++j)
        variButtons[j].setToggleState(j == i, juce::dontSendNotification);
}

// =====================================================================================
//  setSoloSingleMode
// =====================================================================================
void MainTab::setSoloSingleMode(bool isSingle)
{
    soloSingleMode = isSingle;

    if (isSingle)
    {
        int firstOn = -1;
        for (int i = 0; i < 8; ++i)
            if (soloButtons[i].getToggleState()) { firstOn = i; break; }

        for (int i = 0; i < 8; ++i)
            soloButtons[i].setToggleState(i == firstOn, juce::dontSendNotification);

        // The collapse changed the effective selection -- push the new mask to
        // the host, otherwise the engine keeps layering the old MULTI set
        // while the UI shows a single instrument.
        juce::uint8 mask = 0;
        for (int j = 0; j < 8; ++j)
            if (soloButtons[j].getToggleState()) mask |= (juce::uint8) (1u << j);
        if (onSoloMaskChanged) onSoloMaskChanged (mask);
    }
}

// =====================================================================================
//  Constructor
// =====================================================================================
MainTab::MainTab()
{
    setOpaque(false);

    initSectionLabel(lblSolo,        "SOLO ELEMENTS");
    initSectionLabel(lblStyle,       "STYLE ELEMENTS ON \\ OFF");
    initSectionLabel(lblVariations,  "VARIATIONS");
    initSectionLabel(lblPlayControl, "PLAY CONTROL");

    addAndMakeVisible(lblSolo);
    addAndMakeVisible(lblStyle);
    addAndMakeVisible(lblVariations);
    addAndMakeVisible(lblPlayControl);

    // ── Solo buttons (MIDI ch 17-24) ─────────────────────────────────────────
    auto rebuildSoloMaskAndNotify = [this]()
    {
        juce::uint8 mask = 0;
        for (int j = 0; j < 8; ++j)
            if (soloButtons[j].getToggleState()) mask |= (juce::uint8) (1u << j);
        if (onSoloMaskChanged) onSoloMaskChanged (mask);
    };

    for (int i = 0; i < 8; ++i)
    {
        soloButtons[i].setButtonText(kSoloNames[i]);
        soloButtons[i].setMode(LedButton::Mode::Toggle);

        soloButtons[i].onClick = [this, i, rebuildSoloMaskAndNotify]
        {
            if (soloSingleMode)
            {
                for (int j = 0; j < 8; ++j)
                    soloButtons[j].setToggleState(j == i, juce::dontSendNotification);
            }
            rebuildSoloMaskAndNotify();
        };

        addAndMakeVisible(soloButtons[i]);
    }

    // ── Style buttons (the 8 fixed style roles, default ON) ──────────────────
    for (int i = 0; i < 8; ++i)
    {
        styleButtons[i].setButtonText(kStyleNames[i]);
        styleButtons[i].setToggleState(true, juce::dontSendNotification);

        styleButtons[i].onClick = [this, i]
        {
            if (onStyleElementToggled)
                onStyleElementToggled (i, styleButtons[i].getToggleState());
        };

        addAndMakeVisible(styleButtons[i]);
    }

    // ── Variation buttons (selector) ──────────────────────────────────────────
    for (int i = 0; i < 16; ++i)
    {
        variButtons[i].setButtonText(kVariNames[i]);
        variButtons[i].setMode(LedButton::Mode::Toggle);
        // The LED follows the PLAYING state only, never the click: pressing a
        // variation queues the switch, and the button lights only once the
        // sequencer has actually moved to it (setCurrentlyPlayingVariation()
        // from the host's timer).  For VAR 1-4 exactly one is always lit (the
        // current main variation); intro / fill / break / ending light only
        // while their section is the one playing right now.
        variButtons[i].setLedFollowsPlayingOnly(true);

        // Update the visual selection AND notify the host so it can dispatch
        // the appropriate StyleSequencer command (intro / variation / fill /
        // break / ending).  The translation from button-index to section
        // lives in MainComponent so MainTab stays UI-only.
        variButtons[i].onClick = [this, i]
        {
            setSelectedVariation(i);
            if (onVariationClicked) onVariationClicked(i);
        };

        addAndMakeVisible(variButtons[i]);
    }

    setSelectedVariation(4);             // VAR 1 (internal selection bookkeeping)
    setCurrentlyPlayingVariation(4);     // VAR 1 lit immediately (last-played default)

    // ── Play controls ─────────────────────────────────────────────────────────
    btnFingered.setAlternateText("FINGERED");

    btnPlayStop.onClick = [this]
    {
        const bool wantsPlay = btnPlayStop.getToggleState();
        btnPlayStop.setButtonText (wantsPlay ? "STOP" : "PLAY");
        if (onPlayStopToggled) onPlayStopToggled (wantsPlay);
    };

    btnSyncPlay.onClick = [this]
    {
        if (onSyncPlayToggled) onSyncPlayToggled (btnSyncPlay.getToggleState());
    };

    btnOnPress.onClick = [this]
    {
        // ONPRESS = Sync-Stop: when toggled on, lifting all chord-zone keys
        // halts the band.  Off by default; user-armable.
        if (onSyncStopToggled) onSyncStopToggled (btnOnPress.getToggleState());
    };

    btnCrash.onClick = [this]
    {
        if (onCrashClicked) onCrashClicked();
    };

    btnRestart.onClick = [this]
    {
        if (onRestartClicked) onRestartClicked();
    };

    btnFingered.onClick = [this]
    {
        // Toggle off (default) = SINGLE FINGER mode; toggle on = FINGERED.
        // The button text already swaps via setAlternateText("FINGERED").
        if (onChordModeToggled)
            onChordModeToggled (btnFingered.getToggleState());
    };

    // The style-info status strip that used to sit here is GONE.  It repeated
    // the style name, the tempo and the time signature - all three of which the
    // left panel already shows, larger and in one place - so it was a second
    // copy of the same facts competing for the eye above the transport.

    addAndMakeVisible(btnSyncPlay);
    addAndMakeVisible(btnPlayStop);
    addAndMakeVisible(btnOnPress);
    addAndMakeVisible(btnCrash);
    addAndMakeVisible(btnFingered);
    addAndMakeVisible(btnRestart);
}

// =====================================================================================
//  Paint
// =====================================================================================
void MainTab::paint(juce::Graphics& g)
{
    auto drawSep = [&](juce::Label& lbl)
    {
        const int y = lbl.getBottom() + 1;
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawHorizontalLine(y, (float)lbl.getX(), (float)(lbl.getX() + lbl.getWidth()));
    };

    drawSep(lblSolo);
    drawSep(lblStyle);
    drawSep(lblVariations);
    drawSep(lblPlayControl);
}

// =====================================================================================
//  Layout  (918 x 435 canvas)
//
//  Section heights are expressed as fractions of H so the layout is
//  fully proportional and compresses gracefully at smaller heights.
// =====================================================================================
void MainTab::resized()
{
    const int W    = getWidth();
    const int H    = getHeight();
    const int padX = 6;
    const int usW  = W - 2 * padX;
    const int gap  = 2;

    const int labelH   = juce::jmax(14, (int)(H * 0.042f));   // ~18px
    const int btnH     = juce::jmax(48, (int)(H * 0.143f));   // ~62px  (sub-label fits)
    const int varBtnH  = juce::jmax(36, (int)(H * 0.101f));   // ~44px
    const int secGap   = juce::jmax(8,  (int)(H * 0.032f));   // ~14px
    const int labelPad = juce::jmax(2,  (int)(H * 0.007f));   // ~3px

    // Total height of the stacked control block, used to centre it vertically
    // within the working zone (4 section labels, 3 full-height button rows,
    // 2 variation rows, 3 section gaps + 1 inter-variation gap).
    const int totalContentH = 4 * (labelH + labelPad)
                            + 3 * btnH
                            + 2 * varBtnH + gap
                            + 3 * secGap;

    int y = juce::jmax(2, (H - totalContentH) / 2);

    // ── SOLO ELEMENTS ────────────────────────────────────────────────────────
    lblSolo.setBounds(padX, y, usW, labelH);
    y += labelH + labelPad;

    {
        juce::Component* items[8];
        for (int i = 0; i < 8; ++i) items[i] = &soloButtons[i];
        layoutRow(items, 8, padX, y, usW, btnH, gap);
    }
    y += btnH + secGap;

    // ── STYLE ELEMENTS ───────────────────────────────────────────────────────
    lblStyle.setBounds(padX, y, usW, labelH);
    y += labelH + labelPad;

    {
        juce::Component* items[8];
        for (int i = 0; i < 8; ++i) items[i] = &styleButtons[i];
        layoutRow(items, 8, padX, y, usW, btnH, gap);
    }
    y += btnH + secGap;

    // ── VARIATIONS ───────────────────────────────────────────────────────────
    //
    // Full width again.  These two rows and the PLAY CONTROL row below used to
    // give up a column down their right to the FILL METHOD selector; with the
    // selector gone (GRID is now the only timing there is) the width returns to
    // the buttons that were paying for it.
    lblVariations.setBounds(padX, y, usW, labelH);
    y += labelH + labelPad;

    {
        juce::Component* row1[8];
        for (int i = 0; i < 8; ++i) row1[i] = &variButtons[i];
        layoutRow(row1, 8, padX, y, usW, varBtnH, gap);
        y += varBtnH + gap;

        juce::Component* row2[8];
        for (int i = 0; i < 8; ++i) row2[i] = &variButtons[8 + i];
        layoutRow(row2, 8, padX, y, usW, varBtnH, gap);
        y += varBtnH + secGap;
    }

    // ── PLAY CONTROL ─────────────────────────────────────────────────────────
    {
        // The label takes the full width now the status strip is gone.
        lblPlayControl.setBounds (padX, y, usW, labelH);
        y += labelH + labelPad;

        juce::Component* items[6] = {
            &btnSyncPlay, &btnPlayStop, &btnOnPress,
            &btnCrash,    &btnFingered, &btnRestart
        };
        layoutRow(items, 6, padX, y, usW, btnH, gap);
    }
}
