#pragma once
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

class SynthesisPanel : public juce::Component
{
public:
    std::function<void()> onAnythingChanged;

    SynthesisPanel()
    {
        for (auto* s : { &sA,&sD,&sSus,&sR,&sGain }) { addAndMakeVisible(*s); s->onChange = notifyFn(); }
        // 201 whole-number steps, 100 = unity.  Integer detents mean "this one
        // needed 85" is a value you can dial again on the next sound and compare
        // against, and it reads in the same units as the mixer fader's own
        // notion of unity.
        sGain.setStep (1.0f);
        // Left double-click on the GAIN handle = the developer's base-unity
        // dialog.  Forwarded up; the panel does not own the window.
        sGain.onLeftDoubleClick = [this] { if (onBaseUnityRequested) onBaseUnityRequested(); };

        // Amp decay/release CURVE selector — header row above the A/D/S/R sliders.
        const juce::String ampCurveLabels[3] = { "EXP", "LIN", "LOG" };
        for (int i = 0; i < 3; ++i)
        {
            ampCurveBtns[i].setButtonText(ampCurveLabels[i]);
            ampCurveBtns[i].onClick = [this, i] { setAmpCurve(i); };
            addAndMakeVisible(ampCurveBtns[i]);
        }

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
        sRange.setColour (juce::Slider::trackColourId,      juce::Colour (0xFFB87A36));
        sRange.setColour (juce::Slider::thumbColourId,      juce::Colour (0xFFF0B265));
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
        refreshAmpCurveButtons();
        refreshRangeEnabled();
        updateRangeLabel();
    }

    /** Left double-click on GAIN.  Wired by InstrEditorWindow. */
    std::function<void()> onBaseUnityRequested;

    void loadParams(const SlotParams& p)
    {
        sA  .setValue (Betel::Norm::envToUi (p.attack, Betel::Norm::kAmpAtkMaxS));
        sD  .setValue (p.decay   / 7.0f * 100.0f);      // already 0..100
        sSus.setValue (Betel::Norm::unitToUi (p.sustain));
        sR  .setValue (p.release / 3.0f * 100.0f);      // already 0..100
        sGain.setValue(p.gainPercent);
        ampCurve = juce::jlimit(0, 2, p.ampCurve);

        sCut.setValue(p.filterCutoff * 100.0f); sRes.setValue(p.filterReson * 100.0f);
        sKey.setValue(p.filterKeytrack);
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
        refreshAmpCurveButtons();
        repaint();
    }

    void readInto(SlotParams& p) const
    {
        p.attack  = Betel::Norm::uiToEnv (sA.getValue(), Betel::Norm::kAmpAtkMaxS);
        p.decay   = sD.getValue() * 0.07f;
        p.sustain = Betel::Norm::uiToUnit (sSus.getValue());
        p.release = sR.getValue() * 0.03f;
        p.gainPercent = sGain.getValue();
        p.ampCurve = ampCurve;

        p.filterCutoff = sCut.getValue() / 100.0f; p.filterReson = sRes.getValue() / 100.0f;
        p.filterKeytrack = sKey.getValue();
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

    /** Non-bass style slots (3..7) use the two-thumb BAND filter instead of the
        classic CUT/RES/KEY + type, and drop the FILTER ENV section (a band has
        no single cutoff to sweep).  Driven by InstrEditorWindow per slot. */
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
            const int btnRowH = 22;
            const int btnW     = (inner.getWidth() - 4) / 3;
            for (int i = 0; i < 3; ++i)
                ampCurveBtns[i].setBounds(inner.getX() + i * (btnW + 2),
                                          inner.getY(), btnW, btnRowH);
            // Sliders take the space below the curve header (shortened).
            // GAIN joins the amp row because that is where level lives, and it
            // puts the per-sound trim on the first page of the editor — the
            // point of it is quick A/B calibration against the last sound.
            layoutSliderRow({ &sA,&sD,&sSus,&sR,&sGain },
                            inner.withTrimmedTop(btnRowH + 6));
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

    // Enforce the gap rule for the active mode on (noteRangeLo, noteRangeHi).
    // A "12-note window" means hi-lo == 11 semitones (one per pitch class).
    //   mode 1 (bass)  : span LOCKED to exactly 11.
    //   mode 2 (others): span ≥ 11 (may be wider).
    // loFixed = keep lo and push hi; otherwise keep hi and pull lo.
    void applyGapRule (bool loFixed)
    {
        const int kSpan = 11;   // 12-note window
        noteRangeLo = juce::jlimit (0, 127, noteRangeLo);
        noteRangeHi = juce::jlimit (0, 127, noteRangeHi);
        if (noteRangeHi < noteRangeLo) { const int t = noteRangeLo; noteRangeLo = noteRangeHi; noteRangeHi = t; }

        if (noteRangeMode == 1)                    // bass: exact span
        {
            if (loFixed) noteRangeHi = noteRangeLo + kSpan;
            else         noteRangeLo = noteRangeHi - kSpan;
        }
        else if (noteRangeHi - noteRangeLo < kSpan) // others: minimum span
        {
            if (loFixed) noteRangeHi = noteRangeLo + kSpan;
            else         noteRangeLo = noteRangeHi - kSpan;
        }

        // Clamp the (possibly shifted) pair back inside 0..127 keeping span.
        if (noteRangeHi > 127) { noteRangeHi = 127; noteRangeLo = juce::jmax (0,   noteRangeHi - kSpan); }
        if (noteRangeLo < 0)   { noteRangeLo = 0;   noteRangeHi = juce::jmin (127, noteRangeLo + kSpan); }
    }

    // Two-thumb slider moved.  Bass (mode 1) keeps a locked 12-note window —
    // the un-dragged thumb follows.  Other slots (mode 2) may widen freely but
    // not narrow below 12 notes: at the floor the dragged thumb stops and the
    // other stays put.  Result is written back and the readout refreshed.
    void onRangeChanged()
    {
        int lo = (int) std::lround (sRange.getMinValue());
        int hi = (int) std::lround (sRange.getMaxValue());
        const int  kSpan   = 11;                   // 12-note window
        const bool loMoved = (lo != prevLo);

        if (noteRangeMode == 1)                    // bass: locked span, window slides
        {
            if (loMoved) hi = lo + kSpan;
            else         lo = hi - kSpan;
        }
        else if (hi - lo < kSpan)                  // others: floor — stop the dragged thumb
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
    void setAmpCurve(int c)    { ampCurve = c;   refreshAmpCurveButtons(); if (onAnythingChanged) onAnythingChanged(); }
    void setPlayMode(int m)    { playMode = m;   refreshMonoButtons();   if (onAnythingChanged) onAnythingChanged(); }
    void toggleMonoFlag(int i) // 0=HoldStolen 1=RetrigNew 2=RetrigStolen — independent
    {
        if (i == 0)      monoHoldStolen   = ! monoHoldStolen;
        else if (i == 1) monoRetrigNew    = ! monoRetrigNew;
        else             monoRetrigStolen = ! monoRetrigStolen;
        refreshMonoButtons();
        if (onAnythingChanged) onAnythingChanged();
    }

    void refreshAmpCurveButtons()
    {
        for (int i = 0; i < 3; ++i)
            InstrEditStyle::styleSquareButton(ampCurveBtns[i], ampCurve == i);
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

    void layoutSliderRow(std::initializer_list<GoldSlider*> sliders, juce::Rectangle<int> bounds)
    {
        const int n = (int) sliders.size();
        if (n <= 0) return;
        const int g = 4;
        const int w = (bounds.getWidth() - (n - 1) * g) / n;
        int i = 0;
        for (auto* s : sliders)
            s->setBounds(bounds.getX() + i++ * (w + g), bounds.getY(), w, bounds.getHeight());
    }

    // D and R read 0..100 and map to REAL durations: D 100 = 7 s (linear time
    // peak->sustain), R 100 = 3 s (linear time current-level->silence).
    GoldSlider sA   { "A", 0.0f, 100.0f,   0.0f, "" };
    GoldSlider sD   { "D", 0.0f, 100.0f, 100.0f, "" };
    GoldSlider sSus { "S", 0.0f, 100.0f,   0.0f, "" };
    GoldSlider sR   { "R", 0.0f, 100.0f,  15.0f, "" };

    // Per-sound calibration trim, 0..200 with 100 = UNITY.  Saved with the
    // voice (.ins / .sins / .drm / set), so once a sound is levelled it stays
    // levelled everywhere it loads — the style's own CC 7 and the mixer fader
    // are untouched by it.
    GoldSlider sGain { "GAIN", 0.0f, 200.0f, 100.0f, "" };

    GoldSlider sCut { "CUT", 0.0f, 100.0f, 100.0f, "" };   // 0..100 display; param stays 0..1
    GoldSlider sRes { "RES", 0.0f, 100.0f, 0.0f,   "" };   // 0..100 display; param stays 0..1
    GoldSlider sKey { "KEY", 0.0f, 100.0f, 0.0f, "" };
    juce::TextButton ampCurveBtns[3];
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
    int ampCurve = 0;   // 0=Exp 1=Lin 2=Log
    int playMode = 0;
    bool monoHoldStolen = true, monoRetrigNew = false, monoRetrigStolen = false;
    GoldSlider sPort { "PORT", 0.0f, 100.0f, 22.0f, "" };   // 22 -> ~100 ms
    GoldSlider sOct  { "OCT",  -3.0f, 3.0f, 0.0f, "" };   // 7 steps, -3..+3

    int noteRangeMode = 0;   // 0 hidden / 1 bass-locked / 2 free melodic

    juce::TextButton btnRangeOn;                 // ALLOWED NOTES on/off toggle
    juce::Slider     sRange;                     // two-thumb [lo,hi] window
    juce::Label      rangeLabel;                 // note-name readout
    bool noteRangeOn = false;
    int  noteRangeLo = 34, noteRangeHi = 45;     // default A#1..A2 (12 notes)
    int  prevLo = 34, prevHi = 45;               // last committed thumbs

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthesisPanel)
};
