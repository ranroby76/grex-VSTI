#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
//==============================================================================
// SynthesisPanel.h  —  Synthesis tab of the InstrEditorWindow.
//
// Four framed sections (rounded 20 px, 1 px white outline, dark-gray fill):
//   AMP ENV.  |  FILTER  |  FILTER ENV.  |  MONO MODE
//==============================================================================

#include <JuceHeader.h>
#include "InstrEditPanel.h"
#include "SliderNorm.h"   // every slider in the sound editors reads 0..100
#include <cmath>
#include <vector>         // the amp row is built, not braced - see layoutSliderRow

class SynthesisPanel : public juce::Component
{
public:
    std::function<void()> onAnythingChanged;

    SynthesisPanel()
    {
        for (auto* s : { &sA,&sD,&sSus,&sR,&sCurve,&sGain,&sVar }) { addAndMakeVisible(*s); s->onChange = notifyFn(); }
        sVar.setStep (1.0f);   // 0..100, whole numbers
        // 201 whole-number steps, 100 = unity.  Integer detents mean "this one
        // needed 85" is a value you can dial again on the next sound and compare
        // against, and it reads in the same units as the mixer fader's own
        // notion of unity.
        sGain.setStep (1.0f);
        // Left double-click on the GAIN handle = the developer's base-unity
        // dialog.  Forwarded up; the panel does not own the window.
        sGain.onLeftDoubleClick = [this] { if (onBaseUnityRequested) onBaseUnityRequested(); };

        // ── THE ENVELOPE KNOBS READ WHAT THEY MEAN ───────────────────────────
        //
        // D and R hold 0..100 because the drag is linear and their scale is not,
        // so without this the number under them is a knob position rather than a
        // time.  That is how a release of "70" went from 2.1 s to 629 ms in a
        // rebuild with nothing on screen to notice it by.
        sD.displayFn = [] (float ui) { return timeText (expUiToTime (ui, kDecMinS, kDecMaxS)); };
        sR.displayFn = [] (float ui) { return timeText (expUiToTime (ui, kRelMinS, kRelMaxS)); };

        // SHAPE reads its FAMILIAR NAME and its exponent.  The old EXP/LIN/LOG
        // buttons were three points on this knob; naming them here gives back
        // the one thing those buttons really offered — somewhere to aim — while
        // keeping the ninety-eight positions between them.
        sCurve.displayFn = [] (float ui)
        {
            const float k = sliderToCurveK (ui);
            const juce::String name = k <= -8.0f ? "SWELL"
                                    : k <  -2.0f ? "LOG"
                                    : k <=  2.0f ? "LIN"
                                    : k <   9.0f ? "EXP"
                                                 : "TIGHT";
            return name + " " + juce::String (k, 1);
        };

        // The EXP / LIN / LOG selector that used to sit above the A/D/S/R row is
        // GONE.  Those were three fixed points on the SHAPE knob beside them —
        // +6, 0, -4 — so once the knob existed they were three ways of typing a
        // number you can already dial, and a control that only ever agrees with
        // another control is one to delete.

        addAndMakeVisible(sCut); sCut.onChange = notifyFn();
        addAndMakeVisible(sRes); sRes.onChange = notifyFn();
        addAndMakeVisible(sKey); sKey.onChange = notifyFn();
        sCut.setStep(1.0f); sRes.setStep(1.0f); sKey.setStep(1.0f);   // 0..100, 101 steps

        const juce::String filtTypeLabels[4] = { "LP", "HP", "BP", "NT" };
        for (int i = 0; i < 4; ++i)
        {
            filtTypeBtns[i].setButtonText(filtTypeLabels[i]);
            filtTypeBtns[i].onClick = [this, i] { setFilterType(i); };
            addAndMakeVisible(filtTypeBtns[i]);
        }

        for (auto* s : { &fA,&fD,&fS,&fR,&fAmt }) { addAndMakeVisible(*s); s->onChange = notifyFn(); }

        // Band filter — hidden child, revealed by setBandMode() for non-bass
        // style slots.  onChange writes both edges through readInto -> commit.
        addChildComponent (bandSlider);
        bandSlider.onChange = [this](float, float) { if (onAnythingChanged) onAnythingChanged(); };

        addAndMakeVisible (velCurveBox);
        velCurveBox.onChange = [this](float) { if (onAnythingChanged) onAnythingChanged(); };

        btnPoly.setButtonText("POLY");
        btnMono.setButtonText("MONO");
        btnPoly.onClick = [this]{ setPlayMode(0); };
        btnMono.onClick = [this]{ setPlayMode(1); };
        addAndMakeVisible(btnPoly);
        addAndMakeVisible(btnMono);

        const juce::String monoSubLabels[3] = { "HOLD STOLEN", "RETRIG NEW", "RETRIG STOLEN" };
        for (int i = 0; i < 3; ++i)
        {
            monoSubBtns[i].setButtonText(monoSubLabels[i]);
            monoSubBtns[i].onClick = [this, i] { toggleMonoFlag(i); };   // multi-select
            addAndMakeVisible(monoSubBtns[i]);
        }

        sPort.setHorizontal(true);                // wide-and-short layout
        addAndMakeVisible(sPort);
        sPort.onChange = notifyFn();

        sOct.setStep(1.0f);                       // snap to whole octaves

        // EVERY remaining slider on this panel is now 0..100 in 101 steps.
        // sGain (0..200, 100 = unity) and sOct (-3..+3) are deliberately not,
        // and keep their own steps above -- see SliderNorm.h.
        for (auto* s : { &sA, &sD, &sSus, &sR, &fA, &fD, &fS, &fR, &fAmt, &sPort })
            s->setStep (1.0f);
        sOct.setHorizontal(true);                 // 7 discrete steps need horizontal room
        addAndMakeVisible(sOct);
        sOct.onChange = notifyFn();

        // ── ALLOWED NOTES (per-channel register window) ──────────────────────
        // Toggle + a two-thumb [lo,hi] slider.  Added as hidden child comps;
        // setNoteRangeMode() reveals them for melodic style slots only.
        btnRangeOn.setClickingTogglesState (true);
        btnRangeOn.onClick = [this]
        {
            noteRangeOn = btnRangeOn.getToggleState();
            refreshRangeEnabled();
            if (onAnythingChanged) onAnythingChanged();
        };
        addChildComponent (btnRangeOn);

        sRange.setSliderStyle (juce::Slider::TwoValueHorizontal);
        sRange.setRange (0.0, 127.0, 1.0);
        sRange.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        sRange.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF2E2E2E));
        sRange.setColour (juce::Slider::trackColourId,      juce::Colour (Betel::Pal::kAccent));
        sRange.setColour (juce::Slider::thumbColourId,      juce::Colour (Betel::Pal::kAccentLight));
        sRange.setMinAndMaxValues ((double) noteRangeLo, (double) noteRangeHi,
                                   juce::dontSendNotification);
        sRange.onValueChange = [this] { onRangeChanged(); };
        addChildComponent (sRange);

        rangeLabel.setJustificationType (juce::Justification::centred);
        rangeLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFFFCC44));
        rangeLabel.setFont (juce::Font (13.0f, juce::Font::bold));
        addChildComponent (rangeLabel);

        refreshFilterButtons();
        refreshMonoButtons();
        refreshRangeEnabled();
        updateRangeLabel();
    }

    /** Left double-click on GAIN.  Wired by InstrEditorWindow. */
    std::function<void()> onBaseUnityRequested;

    void loadParams(const SlotParams& p)
    {
        sA  .setValue (Betel::Norm::envToUi (p.attack, Betel::Norm::kAmpAtkMaxS));
        sD  .setValue (expTimeToUi (p.decay,   kDecMinS, kDecMaxS));
        sSus.setValue (Betel::Norm::unitToUi (p.sustain));
        sR  .setValue (expTimeToUi (p.release, kRelMinS, kRelMaxS));
        sGain.setValue(p.gainPercent);
        sCurve.setValue (curveKToSlider (p.ampCurveK));

        sCut.setValue(p.filterCutoff * 100.0f); sRes.setValue(p.filterReson * 100.0f);
        sKey.setValue(p.filterKeytrack);
        sVar.setValue((float) juce::jlimit (0, 100, p.variationAmount));
        filterType = juce::jlimit(0, 3, p.filterType);

        velCurveBox.setCurve (p.velCurve, juce::dontSendNotification);
        fA  .setValue (Betel::Norm::envToUi (p.fEnvA, Betel::Norm::kFiltAtkMaxS));
        fD  .setValue (Betel::Norm::envToUi (p.fEnvD, Betel::Norm::kFiltDecMaxS));
        fS  .setValue (Betel::Norm::unitToUi (p.fEnvS));
        fR  .setValue (Betel::Norm::envToUi (p.fEnvR, Betel::Norm::kFiltRelMaxS));
        fAmt.setValue (Betel::Norm::unitToUi (p.fEnvAmount));

        bandSlider.setValues (p.filterHpNorm, p.filterLpNorm, false /* no notify */);

        playMode         = juce::jlimit(0, 1, p.playMode);
        monoHoldStolen   = p.monoHoldStolen;
        monoRetrigNew    = p.monoRetrigNew;
        monoRetrigStolen = p.monoRetrigStolen;
        sPort.setValue (Betel::Norm::portaToUi (p.portamentoTime));
        sOct.setValue((float) juce::jlimit(-3, 3, p.octaveOffset));

        // Allowed-notes window
        noteRangeOn = p.noteRangeOn;
        noteRangeLo = juce::jlimit (0, 127, p.noteRangeLo);
        noteRangeHi = juce::jlimit (0, 127, p.noteRangeHi);
        if (noteRangeHi < noteRangeLo) { const int t = noteRangeLo; noteRangeLo = noteRangeHi; noteRangeHi = t; }
        applyGapRule (true /*loFixed*/);          // coerce to the current mode's gap rule
        btnRangeOn.setToggleState (noteRangeOn, juce::dontSendNotification);
        sRange.setMinAndMaxValues ((double) noteRangeLo, (double) noteRangeHi, juce::dontSendNotification);
        prevLo = noteRangeLo; prevHi = noteRangeHi;
        refreshRangeEnabled();
        updateRangeLabel();

        refreshFilterButtons();
        refreshMonoButtons();
        repaint();
    }

    void readInto(SlotParams& p) const
    {
        p.attack  = Betel::Norm::uiToEnv (sA.getValue(), Betel::Norm::kAmpAtkMaxS);
        p.decay   = expUiToTime (sD.getValue(), kDecMinS, kDecMaxS);
        p.sustain = Betel::Norm::uiToUnit (sSus.getValue());
        p.release = expUiToTime (sR.getValue(), kRelMinS, kRelMaxS);
        p.gainPercent = sGain.getValue();
        // ampCurve (the old 0/1/2) is deliberately left alone: it is still in the
        // save format so an older build could read the file, and it is still what
        // an older FILE is read through, but nothing sets it from here any more.
        p.ampCurveK = sliderToCurveK (sCurve.getValue());

        p.filterCutoff = sCut.getValue() / 100.0f; p.filterReson = sRes.getValue() / 100.0f;
        p.filterKeytrack = sKey.getValue();
        p.variationAmount = juce::jlimit (0, 100, (int) std::lround (sVar.getValue()));
        p.filterType = filterType;

        p.velCurve = velCurveBox.getCurve();
        // uiToEnvMin, not uiToEnv: these three were declared with a 0.001 s
        // floor, and a squared law reaches 0 exactly.  A zero-length filter
        // stage is a click, not an envelope.
        p.fEnvA = Betel::Norm::uiToEnvMin (fA.getValue(), Betel::Norm::kFiltAtkMaxS);
        p.fEnvD = Betel::Norm::uiToEnvMin (fD.getValue(), Betel::Norm::kFiltDecMaxS);
        p.fEnvS = Betel::Norm::uiToUnit   (fS.getValue());
        p.fEnvR = Betel::Norm::uiToEnvMin (fR.getValue(), Betel::Norm::kFiltRelMaxS);
        p.fEnvAmount = Betel::Norm::uiToUnit (fAmt.getValue());

        p.filterHpNorm = bandSlider.getLo();
        p.filterLpNorm = bandSlider.getHi();

        p.playMode         = playMode;
        p.monoHoldStolen   = monoHoldStolen;
        p.monoRetrigNew    = monoRetrigNew;
        p.monoRetrigStolen = monoRetrigStolen;
        p.portamentoTime = Betel::Norm::uiToPorta (sPort.getValue());
        p.octaveOffset = (int) std::lround(sOct.getValue());

        p.noteRangeOn = noteRangeOn;
        p.noteRangeLo = noteRangeLo;
        p.noteRangeHi = noteRangeHi;
    }

    //==========================================================================
    // THE PER-SOUND GAIN IS A SOLO-ONLY CONTROL NOW.
    //
    // Under the gain model a STYLE slot's level is the style's own decision,
    // taken whole, with the mixer fader as the one adjustment on top.  A GAIN
    // trim living in the sound editor was a second authority over the same
    // level - which is what produced a night of "the set's gain keeps
    // reverting", because a per-sound reset kept flattening a value the set
    // owned and the editor kept putting back.
    //
    // Solo keeps it: nothing competes there, so a per-sound trim is exactly
    // what it claims to be.
    //==========================================================================
    void setGainVisible (bool shouldShow)
    {
        if (gainVisible == shouldShow) return;
        gainVisible = shouldShow;
        sGain.setVisible (gainVisible);
        resized();
    }

    /** Non-bass style slots (3..7) use the two-thumb BAND filter instead of the
        classic CUT/RES/KEY + type, and drop the FILTER ENV section (a band has
        no single cutoff to sweep).  Driven by InstrEditorWindow per slot. */
    /** ENERGY's curve offset, so the velocity display reads what is actually in
        force rather than only what this slot stores.  Style slots only - ENERGY
        does not touch the solo bank. */
    void setEnergyOffset (float curveOffset)
    { velCurveBox.setEnergyOffset (curveOffset); }

    void setBandMode(bool useBand)
    {
        if (bandMode == useBand) return;
        bandMode = useBand;
        applyFilterModeVisibility();
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto secs = sectionBounds();

        // COLUMN 1 NOW CARRIES BOTH FILTER FRAMES, STACKED.
        //
        // They used to own a column each, at full height, with vertical
        // sliders.  Halved and stacked they fit in one — which is what freed
        // column 2 for the velocity curve.  Neither filter section ever needed
        // the height: three sliders and a button row, and four sliders, both of
        // which read fine horizontally.
        InstrEditStyle::paintComponentFrame(g, secs[0], "AMP ENV. + GAIN");

        juce::Rectangle<int> filtBox, fEnvBox;
        filterSplit (filtBox, fEnvBox);
        InstrEditStyle::paintComponentFrame(g, filtBox, "FILTER");
        if (! bandMode)   // a band has no cutoff to sweep
            InstrEditStyle::paintComponentFrame(g, fEnvBox, "FILTER ENV.");

        InstrEditStyle::paintComponentFrame(g, secs[2], "VELOCITY CURVE");

        juce::Rectangle<int> monoBox, rangeBox, octBox;
        columnSplit(monoBox, rangeBox, octBox);
        InstrEditStyle::paintComponentFrame(g, monoBox, "MONO MODE");
        if (noteRangeMode != 0)
            InstrEditStyle::paintComponentFrame(g, rangeBox, "ALLOWED NOTES");
        InstrEditStyle::paintComponentFrame(g, octBox,  "OCTAVE");
    }

    void resized() override
    {
        const auto secs = sectionBounds();

        // ── AMP ENV ──────────────────────────────────────────────────────────
        {
            auto inner = InstrEditStyle::componentFrameContent(secs[0]);

            // The 22 px button header that used to sit above this row is gone
            // with the EXP / LIN / LOG buttons, so the sliders get the whole
            // frame — SHAPE reads its name in its own readout now.
            //
            // GAIN joins the amp row because that is where level lives, and it
            // puts the per-sound trim on the first page of the editor — the
            // point of it is quick A/B calibration against the last sound.
            // VAR joins the AMP row rather than the FILTER row, and not for
            // space: the filter row is replaced wholesale by the two-thumb band
            // slider on style slots 3..7, so a control parked there would simply
            // vanish on five of the eight style sounds. The amp row is the one
            // that is always present.
            // GAIN LEAVES THE ROW ENTIRELY when hidden, rather than being made
            // invisible in place - a reserved gap would read as a missing
            // control on every style slot.  The remaining five share the width.
            std::vector<GoldSlider*> ampRow { &sA, &sD, &sSus, &sR, &sCurve };
            if (gainVisible) ampRow.push_back (&sGain);
            ampRow.push_back (&sVar);

            layoutSliderRow(ampRow, inner);
        }

        // ── FILTER (classic CUT/RES/KEY + type)  or  BAND (two-thumb) ────────
        juce::Rectangle<int> filtBox, fEnvBox;
        filterSplit (filtBox, fEnvBox);
        {
            auto inner = InstrEditStyle::componentFrameContent(filtBox);
            if (bandMode)
            {
                bandSlider.setBounds (inner);
            }
            else
            {
                const int btnRowH = 26;
                const int sliderH = inner.getHeight() - btnRowH - 8;
                const int slotW   = (inner.getWidth() - 8) / 3;
                sCut.setBounds(inner.getX(),                    inner.getY(), slotW, sliderH);
                sRes.setBounds(inner.getX() + slotW + 4,        inner.getY(), slotW, sliderH);
                sKey.setBounds(inner.getX() + (slotW + 4) * 2,  inner.getY(),
                               inner.getWidth() - (slotW + 4) * 2, sliderH);

                const int btnW = (inner.getWidth() - 6) / 4;
                const int btnY = inner.getY() + sliderH + 6;
                for (int i = 0; i < 4; ++i)
                    filtTypeBtns[i].setBounds(inner.getX() + i * (btnW + 2), btnY, btnW, btnRowH);
            }
        }

        // ── FILTER ENV (classic filter only — band has no cutoff to sweep) ──
        if (! bandMode)
            layoutSliderRow({ &fA,&fD,&fS,&fR,&fAmt },
                            InstrEditStyle::componentFrameContent(fEnvBox));

        // ── VELOCITY CURVE — the column the stack above paid for ────────────
        {
            auto inner = InstrEditStyle::componentFrameContent(secs[2]);
            // Square-ish and centred: a curve read against its own diagonal is
            // only honest when the box is square, so the shape on screen is the
            // shape of the function.
            const int side = juce::jmin (inner.getWidth(), inner.getHeight() - 4);
            velCurveBox.setBounds (inner.withSizeKeepingCentre (side, side));
        }

        // ── MONO MODE (upper) + ALLOWED NOTES (mid) + OCTAVE (lower) ─────────
        {
            juce::Rectangle<int> monoBox, rangeBox, octBox;
            columnSplit(monoBox, rangeBox, octBox);

            auto inner = InstrEditStyle::componentFrameContent(monoBox);
            const int x = inner.getX(), y = inner.getY(), w = inner.getWidth(), h = inner.getHeight();

            const int rowH  = 28;
            const int polyW = (w - 6) / 2;
            btnPoly.setBounds(x,             y, polyW,         rowH);
            btnMono.setBounds(x + polyW + 6, y, w - polyW - 6, rowH);

            const int subY = y + rowH + 8;
            const int subH = 24;
            for (int i = 0; i < 3; ++i)
                monoSubBtns[i].setBounds(x, subY + i * (subH + 4), w, subH);

            const int portY = subY + 3 * (subH + 4) + 8;
            const int portH = h - (portY - y) - 4;
            // Horizontal slider: full frame width, capped height (~80 px is
            // plenty for the value-text / track / label stack — GoldSlider's
            // horizontal mode reserves ~64 px and the rest is breathing room).
            const int portBarH = juce::jlimit(0, 80, portH);
            const int portBarY = portY + juce::jmax(0, (portH - portBarH) / 2);
            sPort.setBounds(x, portBarY, w, portBarH);

            // ALLOWED NOTES frame: toggle (top), range slider (mid), note-name
            // readout (bottom).  Only laid out / shown for melodic style slots.
            if (noteRangeMode != 0)
            {
                auto ri = InstrEditStyle::componentFrameContent(rangeBox);
                const int rx = ri.getX(), ry = ri.getY(), rw = ri.getWidth();
                const int togH   = 26;
                const int labH   = 20;
                btnRangeOn.setBounds(rx, ry, rw, togH);
                rangeLabel.setBounds(rx, ri.getBottom() - labH, rw, labH);
                const int slY = ry + togH + 8;
                const int slH = juce::jmax(24, (ri.getBottom() - labH - 6) - slY);
                sRange.setBounds(rx, slY, rw, slH);
            }

            // Octave slider: full inner width of the lower frame, vertically
            // centred so the seven discrete steps each get the full inner
            // width to spread across.
            auto octInner = InstrEditStyle::componentFrameContent(octBox);
            const int octBarH = juce::jlimit(0, 80, octInner.getHeight());
            const int octBarY = octInner.getY()
                              + juce::jmax(0, (octInner.getHeight() - octBarH) / 2);
            sOct.setBounds(octInner.getX(), octBarY, octInner.getWidth(), octBarH);
        }
    }

    // Split the 4th column into MONO MODE (top), an optional ALLOWED NOTES
    // frame (middle, only when a melodic style slot is loaded), and OCTAVE
    // (bottom).  Shared by paint() and resized() so the two always agree.
    void columnSplit(juce::Rectangle<int>& monoBox,
                     juce::Rectangle<int>& rangeBox,
                     juce::Rectangle<int>& octBox) const
    {
        auto sec = sectionBounds()[3];
        const int gap  = 8;
        const int octH = juce::jlimit(108, 150, sec.getHeight() / 4);
        octBox = sec.removeFromBottom(octH);
        sec.removeFromBottom(gap);

        if (noteRangeMode != 0)
        {
            const int rangeH = juce::jlimit(120, 168, sec.getHeight() * 2 / 5);
            rangeBox = sec.removeFromBottom(rangeH);
            sec.removeFromBottom(gap);
        }
        else
        {
            rangeBox = {};
        }
        monoBox = sec;
    }

    // Allowed-notes window scope for the slot currently in the editor:
    //   0 = hidden  (solo / drums — frame not shown, column = MONO + OCTAVE)
    //   1 = bass    (window LOCKED to exactly 12 notes; thumbs slide together)
    //   2 = other melodic (window free, but ≥ 12 notes — can widen, not narrow)
    void setNoteRangeMode (int mode)
    {
        noteRangeMode = juce::jlimit (0, 2, mode);

        const bool show = (noteRangeMode != 0);
        btnRangeOn.setVisible (show);
        sRange    .setVisible (show);
        rangeLabel.setVisible (show);

        // Re-coerce the current window to the new mode's gap rule.
        applyGapRule (true /*loFixed*/);
        sRange.setMinAndMaxValues ((double) noteRangeLo, (double) noteRangeHi,
                                   juce::dontSendNotification);
        prevLo = noteRangeLo; prevHi = noteRangeHi;

        refreshRangeEnabled();
        updateRangeLabel();
        resized();
        repaint();
    }

    int getNoteRangeMode() const noexcept { return noteRangeMode; }

private:
    // MIDI note number → name, e.g. 34 → "A#1" (middle C = C4).
    static juce::String noteName (int n)
    {
        static const char* nm[12] = { "C","C#","D","D#","E","F",
                                      "F#","G","G#","A","A#","B" };
        n = juce::jlimit (0, 127, n);
        return juce::String (nm[n % 12]) + juce::String (n / 12 - 1);
    }

    // Enforce the gap rule on (noteRangeLo, noteRangeHi).
    // A "12-note window" means hi-lo == 11 semitones (one per pitch class).
    //   ALL slots, bass included: span >= 11, free to be wider.
    // loFixed = keep lo and push hi; otherwise keep hi and pull lo.
    //
    // THE BASS USED TO BE LOCKED TO EXACTLY 11 and it no longer is.
    //
    // The lock came from the octave-fold: a window narrower than 12 semitones
    // cannot contain every pitch class, so a folded note could have nowhere
    // legal to land and the fold would not converge.  That argument justifies
    // a FLOOR of 11 - which is still enforced, here and again in
    // StylePlayer::setNoteRange - but it never justified a CEILING.  A bass
    // free to use two octaves folds perfectly well; it simply folds less often.
    //
    // Locking it also made the bass the one channel whose ALLOWED NOTES control
    // behaved unlike every other channel's, which is a worse cost than it looks:
    // a control that moves differently in one place teaches the player not to
    // trust it anywhere.
    void applyGapRule (bool loFixed)
    {
        const int kSpan = 11;   // 12-note window, now a FLOOR everywhere
        noteRangeLo = juce::jlimit (0, 127, noteRangeLo);
        noteRangeHi = juce::jlimit (0, 127, noteRangeHi);
        if (noteRangeHi < noteRangeLo) { const int t = noteRangeLo; noteRangeLo = noteRangeHi; noteRangeHi = t; }

        if (noteRangeHi - noteRangeLo < kSpan)     // every slot: minimum span
        {
            if (loFixed) noteRangeHi = noteRangeLo + kSpan;
            else         noteRangeLo = noteRangeHi - kSpan;
        }

        // Clamp the (possibly shifted) pair back inside 0..127 keeping span.
        if (noteRangeHi > 127) { noteRangeHi = 127; noteRangeLo = juce::jmax (0,   noteRangeHi - kSpan); }
        if (noteRangeLo < 0)   { noteRangeLo = 0;   noteRangeHi = juce::jmin (127, noteRangeLo + kSpan); }
    }

    // Two-thumb slider moved.  EVERY slot, bass included, may widen freely but
    // not narrow below 12 notes: at the floor the dragged thumb stops and the
    // other stays put.  Result is written back and the readout refreshed.
    void onRangeChanged()
    {
        int lo = (int) std::lround (sRange.getMinValue());
        int hi = (int) std::lround (sRange.getMaxValue());
        const int  kSpan   = 11;                   // 12-note window
        const bool loMoved = (lo != prevLo);

        // ONE RULE FOR EVERY SLOT.  The bass used to slide a locked 12-note
        // window here; now it hits the same floor as the rest and is free above
        // it.  See applyGapRule for why the floor stays and the ceiling went.
        if (hi - lo < kSpan)                       // floor - stop the dragged thumb
        {
            if (loMoved) lo = hi - kSpan;
            else         hi = lo + kSpan;
        }

        // Keep the pair inside MIDI range while preserving the span.
        if (lo < 0)   { lo = 0;   if (hi - lo < kSpan) hi = lo + kSpan; }
        if (hi > 127) { hi = 127; if (hi - lo < kSpan) lo = hi - kSpan; }
        if (lo < 0) lo = 0;

        noteRangeLo = lo; noteRangeHi = hi;
        sRange.setMinAndMaxValues ((double) lo, (double) hi, juce::dontSendNotification);
        prevLo = lo; prevHi = hi;
        updateRangeLabel();
        if (onAnythingChanged) onAnythingChanged();
    }

    void refreshRangeEnabled()
    {
        btnRangeOn.setButtonText (noteRangeOn ? "ON" : "OFF");
        InstrEditStyle::styleSquareButton (btnRangeOn, noteRangeOn);
        sRange.setEnabled (noteRangeOn);
        sRange.setAlpha   (noteRangeOn ? 1.0f : 0.45f);
        rangeLabel.setAlpha (noteRangeOn ? 1.0f : 0.5f);
    }

    void updateRangeLabel()
    {
        // Raw MIDI note NUMBERS, not names.  The note-name convention here was
        // C1 = 24 (see noteName: n/12 - 1), which reads a full octave lower than
        // the Yamaha/GM convention people expect (where C1 = 36) — so a "C1"
        // label was a constant source of confusion.  Numbers are unambiguous.
        rangeLabel.setText (juce::String (noteRangeLo) + "  -  " + juce::String (noteRangeHi)
                                + "   (" + juce::String (noteRangeHi - noteRangeLo + 1) + " notes)",
                            juce::dontSendNotification);
    }

    /** Column 1, split into FILTER over FILTER ENV.

        In BAND mode the filter takes the whole column: the two-thumb slider has
        no envelope to pair with, and giving it the full height back is better
        than leaving an empty frame under it. */
    void filterSplit (juce::Rectangle<int>& filt, juce::Rectangle<int>& fEnv) const
    {
        auto col = sectionBounds()[1];
        if (bandMode) { filt = col; fEnv = {}; return; }

        const int gap = 6;
        const int h   = (col.getHeight() - gap) / 2;
        filt = col.withHeight (h);
        fEnv = col.withTrimmedTop (h + gap);
    }

    std::array<juce::Rectangle<int>, 4> sectionBounds() const
    {
        const int W = getWidth(), H = getHeight();
        const int pad = 6, gap = 8;
        const int secW = (W - 2*pad - 3*gap) / 4;
        std::array<juce::Rectangle<int>, 4> r;
        for (int i = 0; i < 4; ++i)
            r[(size_t)i] = { pad + i * (secW + gap), 0, secW, H };
        return r;
    }

    std::function<void(float)> notifyFn()
    { return [this](float){ if (onAnythingChanged) onAnythingChanged(); }; }

    void setFilterType(int t)  { filterType = t; refreshFilterButtons(); if (onAnythingChanged) onAnythingChanged(); }
    // The buttons are SHORTCUTS now: each jumps the knob to its exact point.
    void setPlayMode(int m)    { playMode = m;   refreshMonoButtons();   if (onAnythingChanged) onAnythingChanged(); }
    void toggleMonoFlag(int i) // 0=HoldStolen 1=RetrigNew 2=RetrigStolen — independent
    {
        if (i == 0)      monoHoldStolen   = ! monoHoldStolen;
        else if (i == 1) monoRetrigNew    = ! monoRetrigNew;
        else             monoRetrigStolen = ! monoRetrigStolen;
        refreshMonoButtons();
        if (onAnythingChanged) onAnythingChanged();
    }

    void refreshFilterButtons()
    {
        for (int i = 0; i < 4; ++i)
            InstrEditStyle::styleSquareButton(filtTypeBtns[i], filterType == i);
    }

    // Band mode shows the two-thumb band slider and hides the classic filter
    // controls + the FILTER ENV sliders; classic mode does the reverse.
    void applyFilterModeVisibility()
    {
        bandSlider.setVisible (bandMode);
        sCut.setVisible (! bandMode);
        sRes.setVisible (! bandMode);
        sKey.setVisible (! bandMode);
        for (auto& b : filtTypeBtns)              b.setVisible (! bandMode);
        for (auto* s : { &fA,&fD,&fS,&fR,&fAmt }) s->setVisible (! bandMode);
    }

    void refreshMonoButtons()
    {
        InstrEditStyle::styleSquareButton(btnPoly, playMode == 0);
        InstrEditStyle::styleSquareButton(btnMono, playMode == 1);

        const bool monoActive = (playMode == 1);
        const bool flags[3] = { monoHoldStolen, monoRetrigNew, monoRetrigStolen };
        for (int i = 0; i < 3; ++i)
        {
            InstrEditStyle::styleSquareButton(monoSubBtns[i], monoActive && flags[i]);
            monoSubBtns[i].setAlpha(monoActive ? 1.0f : 0.4f);
        }
        sPort.setAlpha(monoActive ? 1.0f : 0.4f);
    }

    // TWO OVERLOADS ON PURPOSE.  The amp row is BUILT at runtime now, because
    // GAIN drops out of it on a style slot and an initializer_list cannot be
    // conditional - so it needs the template.  But a BRACED LIST cannot deduce
    // a template parameter (it has no type until it is bound), so the other
    // call sites, which are still written out literally, need the original
    // signature kept alongside.  Removing it is what broke the FILTER row.
    template <typename Range>
    void layoutSliderRow(const Range& sliders, juce::Rectangle<int> bounds)
    {
        const int n = (int) sliders.size();
        if (n <= 0) return;
        const int g = 4;
        const int w = (bounds.getWidth() - (n - 1) * g) / n;
        int i = 0;
        for (auto* s : sliders)
            s->setBounds(bounds.getX() + i++ * (w + g), bounds.getY(), w, bounds.getHeight());
    }

    void layoutSliderRow(std::initializer_list<GoldSlider*> sliders, juce::Rectangle<int> bounds)
    {
        layoutSliderRow<std::initializer_list<GoldSlider*>>(sliders, bounds);
    }

    // ── D AND R ARE EXPONENTIAL, AND THAT IS THE WHOLE POINT ─────────────────
    //
    // They were linear: R spanned 0-3 s across 100 steps, so EVERYTHING under
    // 150 ms lived in knob positions 1 to 5.  Five steps out of a hundred for
    // the entire tight-release region, which is why the knob felt like it did
    // nothing and then did everything.
    //
    // Three decades each instead, so equal knob movement is equal RATIO — the
    // way time is actually heard:
    //
    //      R  5 ms .. 5 s     0-20 SFZ-tight | 30-40 tight | 50-60 natural
    //                         70-80 long     | 90-100 swell
    //      D  5 ms .. 20 s    decay legitimately runs long, so it gets more top
    //
    // 5 ms at the bottom rather than 0 because below ~3 ms a note-off CLICKS.
    // The envelope will happily do 0 and it sounds like a fault.
    //
    // NOTHING MIGRATES.  SlotParams still stores SECONDS; only the knob's curve
    // changed.  Every existing preset keeps its exact time and simply shows at
    // a different position — today's 450 ms lands on 65, 900 ms on 75.
    static constexpr float kRelMinS = 0.005f, kRelMaxS = 5.0f;
    static constexpr float kDecMinS = 0.005f, kDecMaxS = 20.0f;

    /** ms under a second, seconds above — the same rule GoldSlider's own "s"
        unit follows, so the two can never disagree. */
    static juce::String timeText (float sec)
    {
        return sec < 1.0f ? juce::String ((int) std::lround (sec * 1000.0f)) + "ms"
                          : juce::String (sec, 2) + "s";
    }

    static float expUiToTime (float ui, float lo, float hi)
    {
        return lo * std::pow (hi / lo, juce::jlimit (0.0f, 100.0f, ui) * 0.01f);
    }
    static float expTimeToUi (float t, float lo, float hi)
    {
        if (t <= lo) return 0.0f;
        return juce::jlimit (0.0f, 100.0f,
                             100.0f * std::log (t / lo) / std::log (hi / lo));
    }
    GoldSlider sA   { "A", 0.0f, 100.0f,   0.0f, "" };
    GoldSlider sD   { "D", 0.0f, 100.0f, 100.0f, "" };
    GoldSlider sSus { "S", 0.0f, 100.0f,   0.0f, "" };
    GoldSlider sR   { "R", 0.0f, 100.0f,  15.0f, "" };

    // ── THE DECAY/RELEASE SHAPE, AS A KNOB ───────────────────────────────────
    //
    // 50 is linear.  Below it the level holds and then drops (swell — strings,
    // choir, pads); above it the fall is front-loaded and the tail leaves
    // sooner, which is what a tight release is.
    //
    //      k = (slider - 50) * 0.4      so  40 = Log(-4), 50 = Lin(0), 65 = Exp(+6)
    //
    // The three legacy shapes are exact points on this, which is why the EXP /
    // LIN / LOG buttons still work: they now just jump the knob.
    GoldSlider sCurve { "SHAPE", 0.0f, 100.0f, 65.0f, "" };

    static float sliderToCurveK (float ui)  { return (juce::jlimit (0.0f, 100.0f, ui) - 50.0f) * 0.4f; }
    static float curveKToSlider (float k)   { return juce::jlimit (0.0f, 100.0f, k / 0.4f + 50.0f); }

    // Per-sound calibration trim, 0..200 with 100 = UNITY.  Saved with the
    // voice (.ins / .sins / .drm / set), so once a sound is levelled it stays
    // levelled everywhere it loads — the style's own CC 7 and the mixer fader
    // are untouched by it.
    GoldSlider sGain { "GAIN", 0.0f, 200.0f, 100.0f, "" };
    bool       gainVisible = true;

    GoldSlider sCut { "CUT", 0.0f, 100.0f, 100.0f, "" };   // 0..100 display; param stays 0..1
    GoldSlider sRes { "RES", 0.0f, 100.0f, 0.0f,   "" };   // 0..100 display; param stays 0..1
    GoldSlider sKey { "KEY", 0.0f, 100.0f, 0.0f, "" };

    // Melodic pseudo round robin depth. Lives in the AMP row because that row
    // survives band mode; see the layout comment. 0 = off, and off is default.
    GoldSlider sVar { "VAR", 0.0f, 100.0f, 0.0f, "" };
    juce::TextButton filtTypeBtns[4];
    int filterType = 0;

    // Band filter (non-bass style slots 3..7): shown by setBandMode() in place
    // of CUT/RES/KEY + type, and it drops the FILTER ENV section.  Writes
    // filterHpNorm / filterLpNorm.  Bass / solo slots keep the classic filter.
    BandFilterSlider bandSlider { "FILTER" };

    // Drag to bend, double-click for linear.  See the VelCurve namespace for
    // why one bipolar number replaces the eight curves a hardware panel shows.
    VelCurveDisplay  velCurveBox;
    bool             bandMode = false;

    GoldSlider fA   { "A",   0.0f, 100.0f,  4.0f, "" };   //  4 -> ~8 ms
    GoldSlider fD   { "D",   0.0f, 100.0f, 20.0f, "" };   // 20 -> ~200 ms
    GoldSlider fS   { "S",   0.0f, 100.0f, 50.0f, "" };
    GoldSlider fR   { "R",   0.0f, 100.0f, 27.0f, "" };   // 27 -> ~300 ms
    GoldSlider fAmt { "AMT", 0.0f, 100.0f,  0.0f, "" };

    juce::TextButton btnPoly, btnMono;
    juce::TextButton monoSubBtns[3];
    int playMode = 0;
    bool monoHoldStolen = true, monoRetrigNew = false, monoRetrigStolen = false;
    GoldSlider sPort { "PORT", 0.0f, 100.0f, 22.0f, "" };   // 22 -> ~100 ms
    GoldSlider sOct  { "OCT",  -3.0f, 3.0f, 0.0f, "" };   // 7 steps, -3..+3

    // 0 = the ALLOWED NOTES row is hidden entirely (drum slots, solo slots).
    // 1 and 2 both mean VISIBLE AND FREELY EDITABLE now: 1 used to mean
    // "bass, span locked to 12" and no longer does, so the two behave
    // identically and only (mode != 0) is ever tested.  The distinction is kept
    // because SoundsTab still passes 1 for the bass slot and a caller passing a
    // value the enum no longer honours is better than a silent renumber.
    int noteRangeMode = 0;

    juce::TextButton btnRangeOn;                 // ALLOWED NOTES on/off toggle
    juce::Slider     sRange;                     // two-thumb [lo,hi] window
    juce::Label      rangeLabel;                 // note-name readout
    bool noteRangeOn = false;
    int  noteRangeLo = 34, noteRangeHi = 45;     // default A#1..A2 (12 notes)
    int  prevLo = 34, prevHi = 45;               // last committed thumbs

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthesisPanel)
};