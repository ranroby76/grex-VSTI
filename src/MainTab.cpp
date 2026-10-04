


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

    // ── EVERY GAP IS EXACTLY `gap`.  THE BUTTONS ABSORB THE REMAINDER. ───────
    //
    // The old arithmetic rounded each button's LEFT EDGE independently and then
    // gave the last button whatever was left, so the space between any two
    // neighbours came out one pixel wider or narrower than its neighbours' -
    // and at a 2 px gap a one-pixel error is 50%, which is why the rows did not
    // look evenly spaced however carefully they were laid out.
    //
    // Now the width is divided in INTEGERS and the leftover pixels are handed
    // to the first few buttons, one each.  Buttons can differ by 1 px, which
    // nobody sees; gaps cannot differ at all, which everybody does.
    const int totalGaps = gap * (count - 1);
    const int avail     = juce::jmax (count, totalW - totalGaps);
    const int baseW     = avail / count;
    int       remainder = avail % count;

    int bx = x;
    for (int i = 0; i < count; ++i)
    {
        const int bw = baseW + (remainder > 0 ? 1 : 0);
        if (remainder > 0) --remainder;

        items[i]->setBounds(bx, y, bw, h);
        bx += bw + gap;
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
//  THE ROW -> SOLO MASK, in ONE place.
//
//  There were two copies of this loop - the single-mode collapse below and
//  the constructor's rebuildSoloMaskAndNotify - and both read all eight
//  toggles.  SOLO 7 and 8 are hidden now (SoloPairPanel draws them), but the
//  editor mirror still writes every toggle in the row, and legacy routing
//  lights the ACTIVE slot's, which can be 7 or 8.  Read back, that would
//  silently layer the bass or harmony channel onto the right hand the next
//  time any visible solo was pressed - and the collapse could even pick a
//  hidden slot as the one to keep.  The row cannot switch those two on, so it
//  must not be able to write them either.
// =====================================================================================
static bool rowCanWriteSoloSlot (int j) noexcept
{
    return j != LedButton::kManualBassSoloSlot && j != LedButton::kHarmonySoloSlot;
}

static juce::uint8 soloMaskFromRow (const std::array<LedButton, 8>& row) noexcept
{
    juce::uint8 mask = 0;
    for (int j = 0; j < 8; ++j)
        if (rowCanWriteSoloSlot (j) && row[(size_t) j].getToggleState())
            mask |= (juce::uint8) (1u << j);
    return mask;
}

// =====================================================================================
//  setSoloSingleMode
// =====================================================================================
//==============================================================================
// FUNKEY MIX pop-up - a CallOutBox pointing at the "E", holding one slider.
// It is a child of this tab (not a desktop window), which is right for a
// slider: it needs no keyboard, and it closes on a click anywhere else.
//==============================================================================
void MainTab::showFunkeyMix()
{
    auto panel = std::make_unique<FunkeyMixPanel> (funkeyMixValue, [this] (float v)
    {
        funkeyMixValue = v;
        if (onFunkeyMixChanged) onFunkeyMixChanged (v);
    });

    juce::CallOutBox::launchAsynchronously (std::move (panel), btnFunkeyMix.getBounds(), this);
}

void MainTab::setSoloSingleMode(bool isSingle)
{
    soloSingleMode = isSingle;

    if (isSingle)
    {
        int firstOn = -1;
        for (int i = 0; i < 8; ++i)
            if (rowCanWriteSoloSlot (i) && soloButtons[i].getToggleState()) { firstOn = i; break; }

        for (int i = 0; i < 8; ++i)
            soloButtons[i].setToggleState(i == firstOn, juce::dontSendNotification);

        // The collapse changed the effective selection -- push the new mask to
        // the host, otherwise the engine keeps layering the old MULTI set
        // while the UI shows a single instrument.
        if (onSoloMaskChanged) onSoloMaskChanged (soloMaskFromRow (soloButtons));
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
    initSectionLabel(lblSoloOctave,  "SOLO OCTAVE");
    // CENTRED, unlike every other section title.  Those name a whole row and
    // start where the row starts; this one names a single symmetric figure,
    // and a title over a card-five reads as belonging to it only when it sits
    // on the same axis as the centre pad.
    lblSoloOctave.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(lblSolo);
    addAndMakeVisible(lblStyle);
    addAndMakeVisible(lblVariations);
    addAndMakeVisible(lblPlayControl);
    addAndMakeVisible(lblSoloOctave);

    // ── Solo buttons (MIDI ch 17-24) ─────────────────────────────────────────
    auto rebuildSoloMaskAndNotify = [this]()
    {
        if (onSoloMaskChanged) onSoloMaskChanged (soloMaskFromRow (soloButtons));
    };

    for (int i = 0; i < 8; ++i)
    {
        soloButtons[i].setButtonText(kSoloNames[i]);

        // SOLO 1..8, set once and never touched again.
        //
        // The button TEXT is replaced with the loaded instrument's name by the
        // 30 Hz mirror, so it cannot also carry the slot number - and a slot
        // number is the one thing about these eight that never changes. It goes
        // in the corner, where it stays put while the name underneath it moves.
        //
        // Not "CH. 17" like the initial caption: the player thinks in SOLO 1-8,
        // and the MIDI channel is only interesting when wiring something up.
        //
        // ── TWO OF THE EIGHT ARE LAMPS, NOT BUTTONS ──────────────────────
        //
        // SOLO 8 is the HARMONY channel; SOLO 7 is the MANUAL BASS channel.
        // Neither is something you switch on from this row - harmony lives on
        // the feature panel's power switch and manual bass on its own button -
        // and a second control for one state is a second control that can
        // disagree with it. Left pressable they would also be traps: six
        // identical neighbours that layer the lead, and two that quietly do
        // something else.
        //
        // THEY ARE NOT DRAWN AT ALL NOW.  Both buttons are hidden and one
        // SoloPairPanel covers their two cells: a black display titled MANUAL
        // BASS | HARMONY with a lamp and the instrument name in each half.  Two
        // grey buttons that ignored a press, in a row of six that answer one,
        // read as broken; one display reads as a readout.  See SoloPairPanel.
        //
        // The two buttons stay in the array because the row is still laid out
        // as eight equal columns - the panel takes the union of these two
        // cells, so it lands exactly where they were.  They can never enter
        // the solo mask: see soloMaskFromRow.
        const bool isLamp = (i == LedButton::kHarmonySoloSlot
                          || i == LedButton::kManualBassSoloSlot);

        if (isLamp)
        {
            // Hidden below, after the loop.  Non-intercepting and
            // non-toggling as well, so a future setVisible(true) cannot turn
            // either back into something that answers a press.
            soloButtons[i].setMode (LedButton::Mode::Toggle);
            soloButtons[i].setClickingTogglesState (false);
            soloButtons[i].setInterceptsMouseClicks (false, false);
        }
        else
        {
            soloButtons[i].setCornerTag("SOLO " + juce::String(i + 1));
            soloButtons[i].setMode(LedButton::Mode::Toggle);
        }

        // No handler on a lamp. It can never fire (clicks do not reach the
        // component), but an unreachable lambda that writes the solo mask is
        // the kind of thing that becomes reachable again the day someone makes
        // one of these pressable for a moment to test something.
        if (! isLamp)
        {
            soloButtons[i].onClick = [this, i, rebuildSoloMaskAndNotify]
            {
                if (soloSingleMode)
                {
                    for (int j = 0; j < 8; ++j)
                        soloButtons[j].setToggleState(j == i, juce::dontSendNotification);
                }
                rebuildSoloMaskAndNotify();
            };
        }

        addAndMakeVisible(soloButtons[i]);
    }

    // SOLO 7 and 8 are drawn by the panel, which covers both of their cells.
    soloButtons[LedButton::kManualBassSoloSlot].setVisible (false);
    soloButtons[LedButton::kHarmonySoloSlot]   .setVisible (false);
    addAndMakeVisible (soloPair);

    // ── Style buttons (the 8 fixed style roles, default ON) ──────────────────
    for (int i = 0; i < 8; ++i)
    {
        // The ROLE is the thing that never changes, so it goes in the corner -
        // identical reasoning to SOLO 1-8 above. The caption starts as the role
        // too, so a button reads sensibly before any style has loaded, and the
        // 30 Hz mirror replaces it with the instrument name once one has.
        styleButtons[i].setButtonText(kStyleNames[i]);
        styleButtons[i].setCornerTag(kStyleNames[i]);
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

    // ── RECORD / SONG PLAYER ─────────────────────────────────────────────────

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

    btnFunkey.onClick = [this]
    {
        if (onFunkeyToggled) onFunkeyToggled (btnFunkey.getToggleState());
    };

    // FUNKEY MIX - the small "E" laid over FUNKEY's top-left corner.
    btnFunkeyMix.setTooltip ("FUNKEY MIX");
    btnFunkeyMix.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF101418));
    btnFunkeyMix.setColour (juce::TextButton::textColourOffId,  juce::Colour (Betel::Pal::kAccentLight));
    btnFunkeyMix.onClick = [this] { showFunkeyMix(); };

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
    addAndMakeVisible(btnFunkey);
    addAndMakeVisible(btnFunkeyMix);   // AFTER btnFunkey: it must sit on top of it
    addAndMakeVisible(btnCrash);
    addAndMakeVisible(btnFingered);
    addAndMakeVisible(btnRestart);

    // The octave macro only relays - MainComponent picks the slot and owns the
    // write, so this tab never needs to know which solo is selected.
    octavePad.onOctave = [this] (int octaves)
    {
        if (onSoloOctave) onSoloOctave (octaves);
    };
    addAndMakeVisible(octavePad);
}

// =====================================================================================
//  Paint
// =====================================================================================
void MainTab::paint(juce::Graphics& g)
{
    // NO RULE UNDER THE SECTION LABELS.  There used to be a hairline at 18%
    // white below each one.  It was doing the same job as the label itself and
    // the block of buttons under it - both already say "a new section starts
    // here" - and against the grey faces it read as a scratch on the panel
    // rather than as a divider.
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
    // ── THE SIDE INSET ───────────────────────────────────────────────────────
    //
    // 6 px put the outer buttons hard against the tab's own rounded border, so
    // every row read as if it were overflowing the panel it sits in.  18 gives
    // the rows a margin that matches the gap between the panel frame and its
    // title, and it costs each of the eight buttons about 3 px of width - a
    // caption that fitted at 6 still fits at 18.
    //
    // ONE NUMBER FOR EVERY ROW, deliberately: all four rows are laid out
    // through layoutRow with this same padX, so they cannot drift apart.
    const int padX = 18;
    const int usW  = W - 2 * padX;
    // ── ONE GAP, EVERYWHERE ──────────────────────────────────────────────────
    //
    // Between buttons in a row AND between the two variation rows - both use
    // this, so the grid reads as a grid rather than as rows that happen to be
    // near each other.  x1.5 of the old 2 px: enough to separate two grey
    // faces, small enough that no row loses a caption.
    const int gap  = 3;

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

        // THE UNION OF THE TWO HIDDEN CELLS, gap between them included, so the
        // panel is exactly "the width of those two buttons" and its edges stay
        // on the same grid as every other column in the tab.
        soloPair.setBounds (soloButtons[LedButton::kManualBassSoloSlot].getBounds()
                               .getUnion (soloButtons[LedButton::kHarmonySoloSlot].getBounds()));
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
        // The label row is shared with SOLO OCTAVE, so its y is kept and both
        // titles are placed AFTER the buttons - the eighth column's x is only
        // known once layoutRow has run.  See the end of this block.
        const int labelY = y;
        y += labelH + labelPad;

        // EIGHT SLOTS, ON THE VARIATION GRID.  The seven play buttons used to
        // share the full usW between them; they now take the width of a
        // VARIATION button, and the eighth slot holds the solo octave macro.
        //
        // Laid out as an EIGHT-item row through the same layoutRow, with the
        // same padX, usW and gap as the two variation rows above - so every
        // play button sits pixel-exactly under a variation column, the gaps
        // match theirs by construction, and the row starts at the left edge.
        // Computing the narrower width separately would have been one more
        // place for a pixel to drift.
        //
        // The eighth column is exactly one variation button wide, which is
        // exactly three pads of a third of a play button each: the card-five
        // fills it with nothing left over.  See OctaveMacroPad.
        //
        // FUNKEY sits BETWEEN ONPRESS AND CRASH, which is where it belongs
        // rather than where there happened to be room: the row reads left to
        // right as things you do to the arrangement while it plays, and funk
        // character is one of those, not a setting.
        //
        // Every play button is NARROWER than it was (an eighth of the row, not
        // a seventh).  "SYNC PLAY" is two lines and "FINGERED" is the longest
        // single word here - that is the pair to check if a caption ever clips.
        octavePad.setGap (gap);

        juce::Component* items[8] = {
            &btnSyncPlay, &btnPlayStop, &btnOnPress, &btnFunkey,
            &btnCrash,    &btnFingered, &btnRestart, &octavePad
        };
        layoutRow(items, 8, padX, y, usW, btnH, gap);

        // THE "E" ON FUNKEY: its top-left corner, mirroring the lamp in the
        // top-right, and about the lamp's size so the two read as a pair.
        {
            const auto fb   = btnFunkey.getBounds();
            const int  side = juce::jlimit (12, 18, juce::roundToInt ((float) fb.getHeight() * 0.32f));
            btnFunkeyMix.setBounds (fb.getX() + 4, fb.getY() + 4, side, side);
        }

        // ── THE TWO TITLES, one per section of the row ───────────────────────
        //
        // SOLO OCTAVE spans exactly the pad's own width and is drawn CENTRED
        // in it (see the constructor), so its text sits on the matrix's centre
        // line - directly over the 0 pad, since OctaveMacroPad lays the grid
        // out symmetrically about that same line.  Taken from octavePad's
        // bounds rather than recomputed, so it cannot land a pixel off the
        // figure it names.
        //
        // PLAY CONTROL stops one gap short of it instead of spanning the whole
        // row: two labels with overlapping bounds draw fine today, but the day
        // either caption grows they would draw over each other with nothing
        // to say why.
        const int padColX = octavePad.getX();
        lblPlayControl.setBounds (padX,    labelY, juce::jmax (0, padColX - gap - padX), labelH);
        lblSoloOctave .setBounds (padColX, labelY, octavePad.getWidth(),                labelH);
    }
}
