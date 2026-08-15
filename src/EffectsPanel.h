#pragma once
//==============================================================================
// EffectsPanel.h  —  Effects tab of the InstrEditorWindow.
//
// Layout (two framed sections):
//   ┌──────────────────────────────────────┬──────────────┐
//   │              EQ                      │   EFFECTS    │
//   │  (or CHORUS / WAH / PHASER /         │   ┌────────┐ │
//   │   DELAY / REVERB / PAN)              │   │EQ      │ │
//   │                                      │   │CHORUS  │ │
//   │   Selected effect's controls         │   │WAH     │ │
//   │                                      │   │ ...    │ │
//   └──────────────────────────────────────┴──────────────┘
//
// All 7 selectors (EQ / CHORUS / WAH / PHASER / DELAY / REVERB / PAN) are
// first-class, unconditionally enabled, and each render their own UI inline in
// the main area whenever it is the active selection.
//
// ── NORMALISED CONTROLS: 0..100, 101 STEPS ───────────────────────────────────
//
// EVERY slider on this page is a 0..100 integer control.  One scale across the
// whole chain means a Funkey-Mode family preset can be dialled in and compared
// without translating between dB, Hz and 0..1 in the head, and two effects'
// settings are directly comparable at a glance.
//
// The DSP and SlotParams are UNCHANGED — they keep their native units.  The
// FxRange table below is the ONLY place the mapping lives: loadParams()
// converts native → 0..100 on the way in, readInto() converts back on the way
// out.  Because the grid is 1% of each span, a value that arrives from an .ins
// preset off-grid is re-quantised the first time the page writes it back (a wah
// base of 400 Hz reads back as ~406.6 Hz).  That is the intended cost of a
// uniform grid, and it is bounded by one step in every case.
//
// ── REUSED BY THE FUNKEY-MODE FAMILY EDITOR ──────────────────────────────────
//
// loadFx()/readFx() are templated on anything carrying the six stages under the
// same field names — SlotParams and Betel::FamilyFxParams both do, by design
// (see GlobalMacros.h).  So the Funkey family editor is not a second
// implementation of this page: it IS this page, bound to a family preset
// instead of a slot, with the PAN selector switched off through
// setSelectorsVisible() trimming the list to what that preset actually owns.
// One editor, one layout, no drift.
//
// ── ARABIC SCALE: REMOVED ────────────────────────────────────────────────────
//
// The per-slot ARABIC SCALE selector is gone.  The oriental scale is a GLOBAL
// feature (left panel), already applied to the sounding solo channels, so the
// per-slot copy only ever fought it.  SlotParams::scaleTuningCents and the
// onArabicScaleChanged callback chain were removed with it; the engine-side
// per-channel tuning API (setChannelScaleTuningCents) stays, because the global
// keyboard drives it.
//==============================================================================

#include <JuceHeader.h>
#include "InstrEditPanel.h"
#include <algorithm>
#include <cmath>       // std::abs on floats - the GR meter's change test
#include <vector>

class EffectsPanel : public juce::Component
{
public:
    std::function<void()> onAnythingChanged;

    /** Selector indices, in strip order.  Public so a host can trim the strip
        to a subset with setSelectorsVisible(). */
    enum Selector { SelEq = 0, SelChorus, SelWah, SelPhaser, SelDelay, SelReverb, SelPan,
                    SelSweeten };

    static constexpr int kNumEffectSlots = 8;   // + SWEETEN

    EffectsPanel()
    {
        const juce::String labels[kNumEffectSlots] =
            { "EQ", "CHORUS", "WAH", "PHASER", "DELAY", "REVERB", "PAN", "SWEETEN" };

        for (int i = 0; i < kNumEffectSlots; ++i)
        {
            selectorBtns[i].setButtonText(labels[i]);
            selectorBtns[i].onClick = [this, i] { setSelectedEffect(i); };
            addAndMakeVisible(selectorBtns[i]);
        }

        // ── EQ ────────────────────────────────────────────────────────────────
        for (int i = 0; i < 5; ++i)
        {
            addAndMakeVisible(eqGain[i]);
            eqGain[i].onChange = notifyFn();
            addAndMakeVisible(eqFreq[i]);
            eqFreq[i].onFreqChanged = [this](float){ if (onAnythingChanged) onAnythingChanged(); };
        }

        // ── Reverb ────────────────────────────────────────────────────────────
        for (auto* s : { &rSize,&rDamp,&rWet,&rDry,&rTail,&rPre }) { addAndMakeVisible(*s); s->onChange = notifyFn(); }

        // ── Pan ───────────────────────────────────────────────────────────────
        addAndMakeVisible(panSlider); panSlider.onChange = notifyFn();

        // ── Delay ─────────────────────────────────────────────────────────────
        btnSig44.setButtonText("4/4");
        btnSig34.setButtonText("3/4");
        btnSig44.onClick = [this]{ setTimeSig(0); };
        btnSig34.onClick = [this]{ setTimeSig(1); };
        addAndMakeVisible(btnSig44);
        addAndMakeVisible(btnSig34);
        for (int i = 0; i < 5; ++i)
        {
            addAndMakeVisible(delayDivBtns[i]);
            delayDivBtns[i].onClick = [this, i] {
                delayDivIdx = i; refreshDelayButtons();
                if (onAnythingChanged) onAnythingChanged();
            };
        }
        addAndMakeVisible(dFB);  dFB.onChange  = notifyFn();
        addAndMakeVisible(dDry); dDry.onChange = notifyFn();
        addAndMakeVisible(dWet); dWet.onChange = notifyFn();

        // ── WET BASE GAIN, on both wet sliders ────────────────────────────────
        //
        // Same gesture as the GAIN handle's base-unity box: LEFT double-click,
        // no button, no menu entry, no hint.  A right double-click still resets
        // the slider, so the two gestures do not collide.
        //
        // A MULTIPLIER, not dB, because that is how the question gets asked in
        // practice - "make full wet worth half" - and because the wet sliders
        // read 0..100 rather than in dB the way the GAIN handle does.
        dWet.onLeftDoubleClick = [this] { showWetBaseDialog (false); };
        rWet.onLeftDoubleClick = [this] { showWetBaseDialog (true);  };

        // ── Insert FX (chorus / wah / phaser) ─────────────────────────────────
        wireFxEnable (swBtn,      swOn);                    // block: ON / OFF
        wireFxEnable (swPeakBtn,  swPeakOn,  "PEAK");
        wireFxEnable (swSoftBtn,  swSoftOn,  "SOFTEN");
        wireFxEnable (swTameBtn,  swTameOn,  "TAME");
        wireFxEnable (swRoundBtn, swRoundOn, "ROUND");
        for (auto* sl : { &swMix, &swDepth, &swWindow, &swCeil, &swRatio,
                          &swTameDb, &swFreq, &swDrive, &swRndMix })
        { addAndMakeVisible (*sl); sl->onChange = notifyFn(); }

        wireFxEnable (chBtn, chOn);
        wireFxEnable (wahBtn, wahOn);
        wireFxEnable (phBtn, phOn);
        wireFxEnable (eqBtn,  eqOn);
        wireFxEnable (revBtn, revOn);
        wireFxEnable (delBtn, delOn);

        for (auto* s : { &chRate, &chDepth, &chMix,
                         &wSens, &wRate, &wLfo, &wFreq, &wQ, &wMix,
                         &phRate, &phDepth, &phFB, &phMix })
        {
            addAndMakeVisible(*s);
            s->onChange = notifyFn();
        }

        // 101 discrete steps on EVERY slider of the page — the single grid the
        // whole effects chain (and the Funkey-Mode family presets) is tuned on.
        for (auto* s : everySlider())
            s->setStep(1.0f);

        refreshSelectorButtons();
        refreshDelayButtons();
        showEffect(0);
    }

    /** The host wires this to its file-loader: panel says "user picked this
        file", host calls engine.loadClickSample(channelIdx, file) and stores
        the path into its persisted SlotParams. */
    std::function<void(const juce::File&)> onClickFileChosen;

    void loadParams(const SlotParams& p)
    {
        loadFx (p);
        loadSweetener (p.sweet);
        // PAN is already a 0..100 control end to end (50 = centre).
        panSlider.setValue(p.pan * 50.0f + 50.0f);
    }

    void readInto(SlotParams& p) const
    {
        readFx (p);
        readSweetener (p.sweet);
        p.pan = (float) (panSlider.getValue() - 50.0) / 50.0f;
    }

    /** The six FX stages only — no pan.  Templated so the Funkey family editor
        drives this very panel with a Betel::FamilyFxParams; the field names are
        identical on both types by design. */
    template <typename FxT>
    void loadFx(const FxT& p)
    {
        for (int i = 0; i < 5; ++i)
        {
            eqGain[i].setValue(kEqGainRange.toUi (p.eqGain[i]));
            eqFreq[i].setHz(p.eqFreq[i]);
        }
        rSize.setValue(kUnitRange.toUi (p.reverbSize));
        rDamp.setValue(kUnitRange.toUi (p.reverbDamp));
        rWet .setValue(kUnitRange.toUi (p.reverbWet));
        rDry .setValue(kUnitRange.toUi (p.reverbDry));
        rTail.setValue(kUnitRange.toUi (p.reverbTail));
        rBand.setValues (p.reverbHpNorm, p.reverbLpNorm, false /* no notify */);
        rPre .setValue(kUnitRange.toUi (p.reverbPreDelay));

        timeSig     = p.delayTimeSig;
        delayDivIdx = p.delayDiv;
        dFB .setValue(kDelayFbRange.toUi (p.delayFeedback));
        dDry.setValue(kUnitRange   .toUi (p.delayDry));
        dWet.setValue(kUnitRange   .toUi (p.delayWet));
        delayWetBase  = p.delayWetBase;
        reverbWetBase = p.reverbWetBase;

        // A preset can change the SIGNATURE, and the row's column widths depend
        // on it - four buttons across in 3/4, five in 4/4.  refreshDelayButtons
        // fixes labels and visibility; only a layout pass fixes the geometry, so
        // both are needed and loadParams used to do neither properly.
        refreshDelayButtons();
        resized();

        chOn = p.chorusEnabled;
        chRate .setValue(kUnitRange.toUi (p.chorusRate));
        chDepth.setValue(kUnitRange.toUi (p.chorusDepth));
        chMix  .setValue(kUnitRange.toUi (p.chorusMix));

        wahOn = p.wahEnabled;
        wSens.setValue(kUnitRange    .toUi (p.wahSensitivity));
        wRate.setValue(kWahRateRange .toUi (p.wahRate));
        wLfo .setValue(kUnitRange    .toUi (p.wahLfoDepth));
        wFreq.setValue(kWahFreqRange .toUi (p.wahBaseHz));
        wQ   .setValue(kUnitRange    .toUi (p.wahQ));
        wMix .setValue(kUnitRange    .toUi (p.wahMix));

        phOn = p.phaserEnabled;
        phRate .setValue(kUnitRange.toUi (p.phaserRate));
        phDepth.setValue(kUnitRange.toUi (p.phaserDepth));
        phFB   .setValue(kUnitRange.toUi (p.phaserFeedback));
        phMix  .setValue(kUnitRange.toUi (p.phaserMix));

        refreshFxEnable(chBtn, chOn);  refreshFxEnable(wahBtn, wahOn);
        refreshFxEnable(phBtn, phOn);
        eqOn  = p.eqEnabled;  revOn = p.reverbEnabled;  delOn = p.delayEnabled;
        refreshFxEnable(eqBtn, eqOn);  refreshFxEnable(revBtn, revOn);
        refreshFxEnable(delBtn, delOn);
    }

    template <typename FxT>
    void readFx(FxT& p) const
    {
        for (int i = 0; i < 5; ++i)
        {
            p.eqGain[i] = kEqGainRange.toNative (eqGain[i].getValue());
            p.eqFreq[i] = eqFreq[i].getHz();
        }
        p.reverbSize     = kUnitRange.toNative (rSize.getValue());
        p.reverbDamp     = kUnitRange.toNative (rDamp.getValue());
        p.reverbWet      = kUnitRange.toNative (rWet .getValue());
        p.reverbDry      = kUnitRange.toNative (rDry .getValue());
        p.reverbTail     = kUnitRange.toNative (rTail.getValue());
        p.reverbHpNorm   = rBand.getLo();
        p.reverbLpNorm   = rBand.getHi();
        p.reverbPreDelay = kUnitRange.toNative (rPre .getValue());

        p.delayTimeSig  = timeSig;
        p.delayDiv      = delayDivIdx;
        p.delayFeedback = kDelayFbRange.toNative (dFB .getValue());
        p.delayDry      = kUnitRange   .toNative (dDry.getValue());
        p.delayWet      = kUnitRange   .toNative (dWet.getValue());
        p.delayWetBase  = delayWetBase;
        p.reverbWetBase = reverbWetBase;
        p.eqEnabled     = eqOn;
        p.reverbEnabled = revOn;
        p.delayEnabled  = delOn;

        p.chorusEnabled  = chOn;
        p.chorusRate     = kUnitRange.toNative (chRate .getValue());
        p.chorusDepth    = kUnitRange.toNative (chDepth.getValue());
        p.chorusMix      = kUnitRange.toNative (chMix  .getValue());

        p.wahEnabled     = wahOn;
        p.wahSensitivity = kUnitRange    .toNative (wSens.getValue());
        p.wahRate        = kWahRateRange .toNative (wRate.getValue());
        p.wahLfoDepth    = kUnitRange    .toNative (wLfo .getValue());
        p.wahBaseHz      = kWahFreqRange .toNative (wFreq.getValue());
        p.wahQ           = kUnitRange    .toNative (wQ   .getValue());
        p.wahMix         = kUnitRange    .toNative (wMix .getValue());

        p.phaserEnabled  = phOn;
        p.phaserRate     = kUnitRange.toNative (phRate .getValue());
        p.phaserDepth    = kUnitRange.toNative (phDepth.getValue());
        p.phaserFeedback = kUnitRange.toNative (phFB   .getValue());
        p.phaserMix      = kUnitRange.toNative (phMix  .getValue());
    }

    /** SWEETENER load / read.

        Deliberately OUTSIDE the templated loadFx/readFx pair.  Those two are
        shared with the Funkey family editor through Betel::FamilyFxParams, and
        adding fields there would silently give every macro family a sweetener
        it does not own or persist.  The slot path calls these; the macro path
        does not, and hides the selector through setSelectorsVisible. */
    void loadSweetener (const SweetenerParams& sw)
    {
        swOn      = sw.enabled;
        swPeakOn  = sw.peakOn;
        swSoftOn  = sw.softenOn;
        swTameOn  = sw.tameOn;
        swRoundOn = sw.roundOn;

        swMix   .setValue (kUnitRange    .toUi (sw.mix));
        swDepth .setValue (kSwDepthRange .toUi (sw.softenDepth));
        swWindow.setValue (kSwWindowRange.toUi (sw.softenMs));
        swCeil  .setValue (kSwCeilRange  .toUi (sw.peakCeilDb));
        swRatio .setValue (PeakRatio::toUi01 (sw.peakRatio) * 100.0f);
        swTameDb.setValue (kSwTameRange  .toUi (sw.tameDepthDb));
        swFreq  .setValue (kSwFreqRange  .toUi (sw.tameFreqHz));
        swDrive .setValue (kUnitRange    .toUi (sw.roundDrive));
        swRndMix.setValue (kUnitRange    .toUi (sw.roundMix));

        refreshFxEnable (swBtn,      swOn);
        refreshFxEnable (swPeakBtn,  swPeakOn,  "PEAK");
        refreshFxEnable (swSoftBtn,  swSoftOn,  "SOFTEN");
        refreshFxEnable (swTameBtn,  swTameOn,  "TAME");
        refreshFxEnable (swRoundBtn, swRoundOn, "ROUND");
    }

    void readSweetener (SweetenerParams& sw) const
    {
        sw.enabled     = swOn;
        sw.peakOn      = swPeakOn;
        sw.softenOn    = swSoftOn;
        sw.tameOn      = swTameOn;
        sw.roundOn     = swRoundOn;

        sw.mix         = kUnitRange    .toNative (swMix   .getValue());
        sw.softenDepth = kSwDepthRange .toNative (swDepth .getValue());
        sw.softenMs    = kSwWindowRange.toNative (swWindow.getValue());
        sw.peakCeilDb  = kSwCeilRange  .toNative (swCeil  .getValue());
        sw.peakRatio   = PeakRatio::toNative ((float) swRatio.getValue() * 0.01f);
        sw.tameDepthDb = kSwTameRange  .toNative (swTameDb.getValue());
        sw.tameFreqHz  = kSwFreqRange  .toNative (swFreq  .getValue());
        sw.roundDrive  = kUnitRange    .toNative (swDrive .getValue());
        sw.roundMix    = kUnitRange    .toNative (swRndMix.getValue());
    }

    /** Show only the listed selectors, in strip order.  An empty list restores
        all seven.  Used by the macro editors: a Funkey family preset carries no
        placement (no PAN), and the shared glue preset owns only CHORUS and
        REVERB — showing stages it does not write would promise an edit that
        never reaches the engine.

        If the active page is one that goes away, the selection falls back to
        the first visible selector, so the main area is never left showing a
        control that has no button. */
    void setSelectorsVisible (const std::vector<int>& which)
    {
        for (int i = 0; i < kNumEffectSlots; ++i)
            selectorShown[i] = which.empty()
                             || std::find (which.begin(), which.end(), i) != which.end();

        if (! selectorShown[selectedEffect])
            for (int i = 0; i < kNumEffectSlots; ++i)
                if (selectorShown[i]) { setSelectedEffect (i); break; }

        refreshSelectorButtons();
        resized();
        repaint();
    }

    /** Convenience for the common case — drop PAN, keep the rest. */
    void setPanSelectorVisible (bool shouldShow)
    {
        if (shouldShow) setSelectorsVisible ({});
        else            setSelectorsVisible ({ SelEq, SelChorus, SelWah,
                                               SelPhaser, SelDelay, SelReverb });
    }

    /** Tells the panel whether the currently-edited slot is solo or style.
        Nothing on this page is gated on it any more (the ARABIC SCALE selector
        that used to be was removed), but the flag is kept so the
        InstrEditorWindow / SoundsTab call chain is unchanged. */
    void setSoloMode(bool isSolo)
    {
        soloMode = isSolo;
        refreshSelectorButtons();
    }

    //==========================================================================
    // SWEETENER GAIN-REDUCTION METERS
    //
    // Four bars under the slider row, one per stage, in the same left-to-right
    // order as the sliders: SOFTEN, PEAK, TAME, ROUND.
    //
    // WHY THEY EXIST.  Every one of these stages is invisible when it is set
    // correctly - that is the whole point of them - so without a meter the only
    // feedback is the sound, and the only way to tell "TAME is doing nothing"
    // from "TAME is doing exactly the right amount" is to switch it off and
    // compare.  SweetenerFx has published these four figures since the day it
    // shipped and nothing ever painted them, so the block was being tuned by
    // ear with no feedback at all.
    //
    // PEAK HOLD WITH A SLOW DECAY, not the instantaneous value: the audio
    // thread reports the peak reduction over a whole block, and at 30 Hz a bar
    // following that raw is a flicker rather than a reading.  Decaying toward
    // the live value means a stage that catches one transient per bar still
    // shows what it caught.
    //==========================================================================
    void setSweetenerGr (float softenDb, float peakDb, float tameDb, float roundDb)
    {
        const float in[kNumSweetMeters] = { softenDb, peakDb, tameDb, roundDb };
        bool moved = false;

        for (int i = 0; i < kNumSweetMeters; ++i)
        {
            const float v = juce::jlimit (0.0f, kSweetMeterRangeDb, in[i]);

            // Rise instantly, fall slowly.  A reduction meter that fell as fast
            // as it rose would read as noise.
            const float next = (v > grDb[i]) ? v
                                             : grDb[i] + (v - grDb[i]) * 0.25f;

            if (std::abs (next - grDb[i]) > 0.02f) { grDb[i] = next; moved = true; }
        }

        // Repaint ONLY the meter strip, and only when a bar actually moved.
        // This runs on the UI timer for as long as the window is open, so it
        // must not drag the whole page through a repaint 30 times a second.
        if (moved && selectedEffect == SelSweeten && ! sweetMeterArea.isEmpty())
            repaint (sweetMeterArea);
    }

    void paint(juce::Graphics& g) override
    {
        const auto frames = sectionBounds();
        const char* names[kNumEffectSlots] = { "5-BAND EQ", "CHORUS", "AUTO-WAH",
                                               "PHASER", "DELAY", "REVERB", "PAN",
                                               "SWEETENER" };
        const juce::String mainTitle =
            (selectedEffect >= 0 && selectedEffect < kNumEffectSlots) ? names[selectedEffect]
                                                                      : juce::String("EFFECT");
        InstrEditStyle::paintComponentFrame(g, frames.first,  mainTitle);
        InstrEditStyle::paintComponentFrame(g, frames.second, "EFFECTS");

        if (selectedEffect == SelSweeten)
            paintSweetenerMeters (g);
    }

    /** The four GR bars.  Drawn here rather than as four child Components: they
        carry no interaction, and four extra Components in a page that is
        already dense costs more than eight lines of paint code. */
    void paintSweetenerMeters (juce::Graphics& g)
    {
        if (sweetMeterArea.isEmpty()) return;

        static const char* kNames[kNumSweetMeters] = { "SOFT", "PEAK", "TAME", "RND" };

        const int gap  = 6;
        const int cell = juce::jmax (30, (sweetMeterArea.getWidth() - (kNumSweetMeters - 1) * gap)
                                             / kNumSweetMeters);

        // The block enable gates all four stages, so a switched-off block draws
        // empty troughs rather than a frozen last reading.
        const bool live = swOn;

        for (int i = 0; i < kNumSweetMeters; ++i)
        {
            juce::Rectangle<int> cellR (sweetMeterArea.getX() + i * (cell + gap),
                                        sweetMeterArea.getY(), cell,
                                        sweetMeterArea.getHeight());

            auto labelR = cellR.removeFromLeft (34);
            auto barR   = cellR;

            g.setColour (juce::Colour (0xFF8A8A8A));
            g.setFont (juce::Font (10.0f, juce::Font::bold));
            g.drawText (kNames[i], labelR, juce::Justification::centredLeft, false);

            g.setColour (juce::Colour (0xFF0C0C0C));
            g.fillRect (barR);

            const float db = live ? grDb[i] : 0.0f;

            if (db > 0.05f)
            {
                const float norm = juce::jlimit (0.0f, 1.0f, db / kSweetMeterRangeDb);
                auto fill = barR.toFloat().withWidth (barR.getWidth() * norm);

                // Amber while it is working, red once it is working hard - the
                // same two colours and the same meaning as the Finisher's GR
                // meter, so a reading learned on one page transfers.
                g.setColour (db >= kSweetMeterHotDb ? juce::Colour (0xFFCC3322)
                                                    : juce::Colour (0xFFD4AF37));
                g.fillRect (fill);
            }

            g.setColour (juce::Colour (0xFF2E2E2E));
            g.drawRect (barR, 1);
        }
    }

    void resized() override
    {
        const auto frames = sectionBounds();
        const auto mainInner  = InstrEditStyle::componentFrameContent(frames.first);
        const auto stripInner = InstrEditStyle::componentFrameContent(frames.second);

        layoutEQ(mainInner);
        layoutReverb(mainInner);
        layoutDelay(mainInner);
        layoutChorus(mainInner);
        layoutWah(mainInner);
        layoutPhaser(mainInner);
        layoutPan(mainInner);
        layoutSweetener(mainInner);

        // Only the shown selectors take a row, so a trimmed strip stays flush
        // to the top of the frame instead of leaving holes where the hidden
        // buttons used to be.
        const int nSel = numVisibleSelectors();
        const int btnH = (stripInner.getHeight() - (nSel - 1) * 4) / juce::jmax (1, nSel);
        int row = 0;
        for (int i = 0; i < kNumEffectSlots; ++i)
        {
            if (! selectorShown[i]) continue;
            selectorBtns[i].setBounds(stripInner.getX(),
                                      stripInner.getY() + row * (btnH + 4),
                                      stripInner.getWidth(), btnH);
            ++row;
        }
    }

private:
    //==========================================================================
    // 0..100 <-> native conversion.
    //
    // One entry per distinct native span used on this page.  Anything that is
    // already 0..1 shares kUnitRange, so adding a control to an existing effect
    // needs no new range.
    //==========================================================================
    struct FxRange
    {
        float lo, hi;

        float toUi (float native) const noexcept
        {
            if (hi <= lo) return 0.0f;
            return juce::jlimit (0.0f, 100.0f, (native - lo) / (hi - lo) * 100.0f);
        }

        float toNative (float ui) const noexcept
        {
            return lo + juce::jlimit (0.0f, 100.0f, ui) * 0.01f * (hi - lo);
        }
    };

    static constexpr FxRange kUnitRange     {  0.0f,    1.0f };   // every 0..1 control
    static constexpr FxRange kEqGainRange   { -12.0f,  12.0f };   // dB  (50 = 0 dB)
    static constexpr FxRange kDelayFbRange  {  0.0f,    0.95f };  // feedback ceiling
    static constexpr FxRange kWahRateRange  {  0.05f,   8.0f };   // Hz
    static constexpr FxRange kWahFreqRange  { 80.0f, 1500.0f };   // Hz
    static constexpr FxRange kSwDepthRange  { -1.0f,    1.0f };   // bipolar, 50 = 0
    static constexpr FxRange kSwWindowRange {  1.0f,  150.0f };   // ms
    static constexpr FxRange kSwTameRange   {  0.0f,   12.0f };   // dB of duck
    static constexpr FxRange kSwFreqRange   { 1500.0f, 8000.0f }; // Hz
    static constexpr FxRange kSwCeilRange   {    3.0f,   24.0f }; // dB over body
    // kSwRatioRange is gone: the ratio is LOG-mapped through PeakRatio, not
    // linear through an FxRange.  See the note beside that namespace.

    /** Every slider on the page, in one list, so the 101-step grid is applied
        in exactly one place and a newly-added control cannot be forgotten. */
    std::vector<GoldSlider*> everySlider()
    {
        std::vector<GoldSlider*> v {
            &rSize, &rDamp, &rWet, &rDry, &rTail, &rPre, &panSlider, &dFB, &dDry, &dWet,
            &chRate, &chDepth, &chMix,
            &wSens, &wRate, &wLfo, &wFreq, &wQ, &wMix,
            &phRate, &phDepth, &phFB, &phMix
        };
        for (int i = 0; i < 5; ++i) v.push_back (&eqGain[i]);
        return v;
    }

    std::pair<juce::Rectangle<int>, juce::Rectangle<int>> sectionBounds() const
    {
        const int W = getWidth(), H = getHeight();
        const int pad = 6, gap = 8;
        const int stripW = 160;
        const int mainW  = W - stripW - 2 * pad - gap;
        const juce::Rectangle<int> mainFrame  { pad,             0, mainW,  H };
        const juce::Rectangle<int> stripFrame { pad + mainW + gap, 0, stripW, H };
        return { mainFrame, stripFrame };
    }

    void layoutEQ(juce::Rectangle<int> inner)
    {
        eqBtn.setBounds(inner.removeFromTop(24).removeFromLeft(70));
        inner.removeFromTop(4);
        const int freqH = 20;
        const int gainH = inner.getHeight() - freqH - 4;
        const int bw    = (inner.getWidth() - 4 * 4) / 5;
        for (int i = 0; i < 5; ++i)
        {
            const int bx = inner.getX() + i * (bw + 4);
            eqGain[i].setBounds(bx, inner.getY(), bw, gainH);
            eqFreq[i].setBounds(bx, inner.getY() + gainH + 4, bw, freqH);
        }
    }

    void layoutSweetener (juce::Rectangle<int> inner)
    {
        // ── THE DRUM PANEL'S FORMATION, PORTED ──────────────────────────────
        //
        // This used to stack four stage rows, each with its enable on the left
        // and its own two sliders beside it.  The reasoning was that grouping a
        // stage with its controls makes ownership readable -- but in practice it
        // turned nine controls into four unequal blocks with four different
        // slider widths and a column of buttons cutting down the middle, while
        // the drum editor showed the identical nine as one even row.  Two
        // panels driving the SAME SweetenerFx through the SAME SweetenerParams
        // with the SAME ranges have no business looking like different effects.
        //
        // So: one row of nine, ordered by stage -- MIX, then SOFTEN's pair, then
        // PEAK's, TAME's, ROUND's -- with the four stage enables in the strip
        // above, beside the block enable.  Identical to the drum panel, which is
        // what makes a setting learned in one transfer to the other.
        auto top = inner.removeFromTop (24);
        swBtn.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (8);

        {
            const int gap = 4;
            const int w   = juce::jmax (44, (top.getWidth() - 3 * gap) / 4);
            int x = top.getX();
            for (auto* b : { &swSoftBtn, &swPeakBtn, &swTameBtn, &swRoundBtn })
            {
                b->setBounds (x, top.getY(), w, top.getHeight());
                x += w + gap;
            }
        }

        inner.removeFromTop (6);

        // ── THE GR METER STRIP ──────────────────────────────────────────────
        //
        // Taken off the BOTTOM, so the nine sliders keep the shape they had and
        // only lose the height the strip costs.  It is remembered as a rect
        // because setSweetenerGr repaints exactly this area 30 times a second
        // and nothing else.
        {
            const int stripH = juce::jmin (18, juce::jmax (0, inner.getHeight() - 60));

            if (stripH >= 10)
            {
                sweetMeterArea = inner.removeFromBottom (stripH);
                inner.removeFromBottom (4);
            }
            else
            {
                // Window squeezed too small for both - the sliders win.  An
                // empty rect switches the meters off cleanly: paint skips them
                // and setSweetenerGr stops repainting.
                sweetMeterArea = {};
            }
        }

        // Stage order, left to right, so the row reads the way the signal flows.
        GoldSlider* row[] = { &swMix,
                              &swDepth, &swWindow,     // SOFTEN
                              &swCeil,  &swRatio,      // PEAK
                              &swTameDb, &swFreq,      // TAME
                              &swDrive, &swRndMix };   // ROUND

        const int n    = (int) (sizeof (row) / sizeof (row[0]));
        const int gap  = 4;
        const int cell = juce::jmax (40, (inner.getWidth() - (n - 1) * gap) / n);

        for (int i = 0; i < n; ++i)
            row[i]->setBounds (inner.getX() + i * (cell + gap),
                               inner.getY(), cell, inner.getHeight());
    }

    void layoutPan(juce::Rectangle<int> inner)
    {
        // Single centred pan fader (~1/3 width).  50 = centre.
        const int w = juce::jmax (60, inner.getWidth() / 3);
        panSlider.setBounds (inner.getX() + (inner.getWidth() - w) / 2,
                             inner.getY(), w, inner.getHeight());
    }

    void layoutReverb(juce::Rectangle<int> inner)
    {
        revBtn.setBounds(inner.removeFromTop(24).removeFromLeft(70));
        inner.removeFromTop(4);
        const int g = 6;
        // Six faders on top, the send band underneath - a two-handle slider
        // reads as a frequency AXIS and wants width, not a column.
        auto band = inner.removeFromBottom (juce::jmax (34, inner.getHeight() / 3));
        inner.removeFromBottom (4);

        const int sw = (inner.getWidth() - 5 * g) / 6;
        rSize.setBounds(inner.getX(),                 inner.getY(), sw, inner.getHeight());
        rTail.setBounds(inner.getX() + (sw + g),      inner.getY(), sw, inner.getHeight());
        rDamp.setBounds(inner.getX() + (sw + g) * 2,  inner.getY(), sw, inner.getHeight());
        rWet .setBounds(inner.getX() + (sw + g) * 3,  inner.getY(), sw, inner.getHeight());
        rDry .setBounds(inner.getX() + (sw + g) * 4,  inner.getY(), sw, inner.getHeight());
        rPre .setBounds(inner.getX() + (sw + g) * 5,  inner.getY(),
                        inner.getWidth() - (sw + g) * 5, inner.getHeight());
        rBand.setBounds (band);
    }

    void layoutDelay(juce::Rectangle<int> inner)
    {
        delBtn.setBounds(inner.removeFromTop(24).removeFromLeft(70));
        inner.removeFromTop(4);
        const int rowH = 26;
        const int divH = 24;
        const int sigW = (inner.getWidth() - 6) / 2;
        btnSig44.setBounds(inner.getX(),                inner.getY(), sigW,                       rowH);
        btnSig34.setBounds(inner.getX() + sigW + 6,     inner.getY(), inner.getWidth() - sigW - 6, rowH);

        const auto& divs = (timeSig == 0) ? kDivs44 : kDivs34;
        const int nd = (int)divs.size();
        const int dw = inner.getWidth() / juce::jmax(1, nd);
        // BOUNDS ONLY.  Text and visibility belong to refreshDelayButtons, which
        // is reached from every path that can change the signature - resized()
        // is not.
        for (int i = 0; i < nd; ++i)
            delayDivBtns[i].setBounds(inner.getX() + i * dw, inner.getY() + rowH + 4, dw - 1, divH);

        const int fbY = inner.getY() + rowH + 4 + divH + 6;
        const int fbH = inner.getHeight() - (fbY - inner.getY());
        // Three across now (FB / DRY / WET) rather than two.  The reverb row
        // reads SIZE DAMP TAIL WET DRY; this keeps WET and DRY adjacent so the
        // pair reads the same way on both effects.
        const int g3 = 6;
        const int w3 = (inner.getWidth() - g3 * 2) / 3;
        dFB .setBounds(inner.getX(),                  fbY, w3, fbH);
        dDry.setBounds(inner.getX() + (w3 + g3),      fbY, w3, fbH);
        dWet.setBounds(inner.getX() + (w3 + g3) * 2,  fbY,
                       inner.getWidth() - (w3 + g3) * 2, fbH);
    }

    void setSelectedEffect(int idx)
    {
        selectedEffect = juce::jlimit(0, kNumEffectSlots - 1, idx);
        refreshSelectorButtons();
        showEffect(selectedEffect);

        // Re-lay out after the visibility change.  Switching TO the delay had to
        // place a row whose column count depends on the time signature, and the
        // buttons were still carrying whichever widths the last signature left -
        // which is how a 4/4-width button ended up straddling a 3/4 row.
        resized();
        repaint();
    }

    void showEffect(int idx)
    {
        // Order: 0 EQ, 1 CHORUS, 2 WAH, 3 PHASER, 4 DELAY, 5 REVERB, 6 PAN
        const bool showEq = (idx == 0);
        for (int i = 0; i < 5; ++i) { eqGain[i].setVisible(showEq); eqFreq[i].setVisible(showEq); }
        eqBtn.setVisible(showEq);

        setFxGroupVisible(idx == 1, chBtn,  { &chRate, &chDepth, &chMix });
        setFxGroupVisible(idx == 2, wahBtn, { &wSens, &wRate, &wLfo, &wFreq, &wQ, &wMix });
        setFxGroupVisible(idx == 3, phBtn,  { &phRate, &phDepth, &phFB, &phMix });

        const bool showDl = (idx == 4);
        delBtn.setVisible(showDl);
        btnSig44.setVisible(showDl); btnSig34.setVisible(showDl);
        dFB.setVisible(showDl); dDry.setVisible(showDl); dWet.setVisible(showDl);

        // The division row's visibility is NOT set here - refreshDelayButtons
        // owns it, because only it knows how many buttons this signature uses.
        // Called on the way out as well as the way in so that leaving DELAY
        // hides the row rather than leaving it drawn over the next effect.
        refreshDelayButtons();

        const bool showRv = (idx == 5);
        revBtn.setVisible(showRv);
        rSize.setVisible(showRv); rDamp.setVisible(showRv);
        rDry.setVisible(showRv);  rTail.setVisible(showRv);
        rBand.setVisible(showRv);
        rWet.setVisible(showRv);  rPre.setVisible(showRv);

        panSlider.setVisible(idx == 6);

        const bool showSw = (idx == SelSweeten);
        swBtn.setVisible (showSw);
        swSoftBtn.setVisible (showSw); swPeakBtn.setVisible (showSw);
        swTameBtn.setVisible (showSw); swRoundBtn.setVisible (showSw);
        for (auto* sl : { &swMix, &swDepth, &swWindow, &swCeil, &swRatio,
                          &swTameDb, &swFreq, &swDrive, &swRndMix })
            sl->setVisible (showSw);
    }

    void refreshSelectorButtons()
    {
        for (int i = 0; i < kNumEffectSlots; ++i)
        {
            InstrEditStyle::styleSquareButton(selectorBtns[i], i == selectedEffect);
            selectorBtns[i].setAlpha (1.0f);
            selectorBtns[i].setEnabled(true);
            selectorBtns[i].setVisible(selectorShown[i]);
        }
    }

    void setTimeSig(int ts)
    {
        timeSig = ts;
        delayDivIdx = 0;
        refreshDelayButtons();
        resized();
        if (onAnythingChanged) onAnythingChanged();
    }

    //--------------------------------------------------------------------------
    // THE DIVISION ROW HAS ONE OWNER NOW: TEXT, LIT STATE AND VISIBILITY.
    //
    // It used to own only the lit state.  The TEXT and the VISIBILITY were set
    // in resized(), which is the only place that knew 4/4 has five divisions and
    // 3/4 has four - and two of the three paths that change the row never
    // reached resized():
    //
    //   * loadParams     - a preset saying 3/4 relit the buttons but left the
    //                      4/4 labels and 4/4 positions on screen, which is the
    //                      "no proper redraw on start" half.
    //   * setSelectedEffect - worse: it did
    //                      `for (i < 5) delayDivBtns[i].setVisible(showDl)`,
    //                      showing ALL FIVE regardless of signature.  In 3/4 the
    //                      fifth button had no business existing, so it appeared
    //                      wearing its stale 4/4 label ("1/16") at its stale
    //                      5-wide position - straddling and hiding the real
    //                      fourth button, 1/12.  That is the button "breaking
    //                      out" in the screenshot.
    //
    // Putting all three here means a caller cannot relight the row and forget to
    // relabel or re-hide it.  resized() keeps only the BOUNDS, which is the one
    // thing that genuinely needs geometry.
    //--------------------------------------------------------------------------
    void refreshDelayButtons()
    {
        InstrEditStyle::styleSquareButton(btnSig44, timeSig == 0);
        InstrEditStyle::styleSquareButton(btnSig34, timeSig == 1);

        const auto& divs = (timeSig == 0) ? kDivs44 : kDivs34;
        const int   nd   = (int) divs.size();
        const bool  showDl = (selectedEffect == 4);

        for (int i = 0; i < 5; ++i)
        {
            const bool used = (i < nd);

            if (used)
            {
                delayDivBtns[i].setButtonText (divs[(size_t) i]);
                InstrEditStyle::styleSquareButton (delayDivBtns[i], i == delayDivIdx);
            }

            // A button the signature does not use is hidden AND parked at zero
            // size.  Hiding alone left it holding stale bounds, and any later
            // blanket setVisible(true) brought it back exactly where it should
            // not be.
            delayDivBtns[i].setVisible (showDl && used);
            if (! used) delayDivBtns[i].setBounds (0, 0, 0, 0);
        }
    }

    // ── FX helpers ────────────────────────────────────────────────────────────
    //--------------------------------------------------------------------------
    // FX ENABLES.  `caption` empty means the button says ON / OFF, which is
    // right for a toggle sitting directly above the section it gates - the
    // heading beside it already says WHICH effect.
    //
    // The four SWEETENER stage toggles pass their NAME instead.  They sit in a
    // row of four with nothing else to identify them, so four buttons all
    // reading "ON" said only how many stages were on, never which - and the
    // names set at construction were being overwritten here on the first
    // refresh, so they never survived to be seen.  On/off is carried by the
    // orange fill, which styleSquareButton was already applying.
    //--------------------------------------------------------------------------
    void wireFxEnable(juce::TextButton& b, bool& state, juce::String caption = {})
    {
        b.onClick = [this, &b, &state, caption]
        {
            state = ! state;
            refreshFxEnable(b, state, caption);
            if (onAnythingChanged) onAnythingChanged();
        };
        addAndMakeVisible(b);
    }

    void refreshFxEnable(juce::TextButton& b, bool state, juce::String caption = {})
    {
        b.setButtonText(caption.isNotEmpty() ? caption
                                             : juce::String (state ? "ON" : "OFF"));
        InstrEditStyle::styleSquareButton(b, state);
    }

    void setFxGroupVisible(bool vis, juce::TextButton& b, std::vector<GoldSlider*> knobs)
    {
        b.setVisible(vis);
        for (auto* k : knobs) k->setVisible(vis);
    }

    // Enable toggle across the top, knobs in a perRow grid beneath.  Bounds are
    // always set (visibility is owned by showEffect) so a freshly-selected
    // effect already has correct geometry.
    void layoutFxKnobs(juce::Rectangle<int> inner, juce::TextButton& enable,
                       std::vector<GoldSlider*> knobs, int perRow)
    {
        const int enH = 26;
        enable.setBounds(inner.getX(), inner.getY(), inner.getWidth(), enH);

        auto area = inner.withTrimmedTop(enH + 8);
        const int n = (int) knobs.size();
        if (n == 0) return;
        const int pr   = juce::jmax(1, perRow);
        const int rows = (n + pr - 1) / pr;
        const int kw   = area.getWidth()  / pr;
        const int kh   = area.getHeight() / juce::jmax(1, rows);
        for (int i = 0; i < n; ++i)
        {
            const int r = i / pr, c = i % pr;
            knobs[(size_t) i]->setBounds(area.getX() + c * kw, area.getY() + r * kh,
                                         kw - 4, kh - 4);
        }
    }

    void layoutChorus(juce::Rectangle<int> inner)
    { layoutFxKnobs(inner, chBtn,  { &chRate, &chDepth, &chMix }, 3); }

    void layoutWah(juce::Rectangle<int> inner)
    { layoutFxKnobs(inner, wahBtn, { &wSens, &wRate, &wLfo, &wFreq, &wQ, &wMix }, 3); }

    void layoutPhaser(juce::Rectangle<int> inner)
    { layoutFxKnobs(inner, phBtn,  { &phRate, &phDepth, &phFB, &phMix }, 4); }

    std::function<void(float)> notifyFn()
    { return [this](float){ if (onAnythingChanged) onAnythingChanged(); }; }

    static inline const std::vector<juce::String> kDivs44 { "1/1","1/2","1/4","1/8","1/16" };
    static inline const std::vector<juce::String> kDivs34 { "1/1","1/3","1/6","1/12" };

    int numVisibleSelectors() const
    {
        int n = 0;
        for (int i = 0; i < kNumEffectSlots; ++i) if (selectorShown[i]) ++n;
        return juce::jmax (1, n);
    }

    juce::TextButton selectorBtns[kNumEffectSlots];
    int  selectedEffect = 0;
    bool selectorShown[kNumEffectSlots] { true, true, true, true, true, true, true, true };

    // ── Sweetener GR meters ──────────────────────────────────────────────────
    // 12 dB of range because that is the span the four stages actually work in:
    // SOFTEN and PEAK are corrective and rarely pass 6, TAME is capped at 12 by
    // its own parameter, and a bar scaled to 24 would leave every honest
    // reading in the first eighth of its travel.  HOT at 6 dB, where a stage
    // stops shaping and starts squashing.
    static constexpr int   kNumSweetMeters    = 4;
    static constexpr float kSweetMeterRangeDb = 12.0f;
    static constexpr float kSweetMeterHotDb   =  6.0f;

    float grDb[kNumSweetMeters] { 0.0f, 0.0f, 0.0f, 0.0f };
    juce::Rectangle<int> sweetMeterArea;

    // Kept for API parity with InstrEditorWindow / SoundsTab; nothing on the
    // page is gated on it since the ARABIC SCALE selector was removed.
    bool soloMode = false;

    //==========================================================================
    // WET BASE GAIN BOX - LEFT double-click on either WET slider.
    //
    // The value is what full travel on that slider is worth: 100 = the slider
    // means what it says, 50 = full wet is half gain.  Up to 200 because this
    // is a calibration and a quiet source can legitimately need more than unity.
    //
    // The slider is deliberately NOT re-centred afterwards, unlike the GAIN
    // handle's base-unity box.  That one is setting the reference the trim is
    // measured against, so leaving the trim off-centre would apply the
    // correction twice.  This one is voicing an effect: the user is listening
    // while they type, and moving their slider out from under them would undo
    // the thing they were judging.
    //==========================================================================
    void showWetBaseDialog (bool isReverb)
    {
        const float current = isReverb ? reverbWetBase : delayWetBase;

        auto* w = new juce::AlertWindow (
            isReverb ? "REVERB WET BASE" : "DELAY WET BASE",
            juce::String (isReverb ? "What full WET is worth on the reverb.\n"
                                   : "What full WET is worth on the delay.\n")
                + "100 = the slider means what it says. 50 = half.\n"
                + "Saved with this sound. Range 0 to 200.",
            juce::MessageBoxIconType::NoIcon);

        w->addTextEditor ("pct", juce::String (current * 100.0f, 1), "%:");
        w->addButton ("SAVE",   1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        // SafePointer, not a captured `this`: the sound editor can be closed
        // while this box is open, and a raw pointer - or a captured reference to
        // one of the members - would then be written through after the panel
        // died.  Same guard the popup windows use around their file choosers.
        juce::Component::SafePointer<EffectsPanel> safe (this);

        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [safe, w, isReverb] (int result) mutable
            {
                std::unique_ptr<juce::AlertWindow> owned (w);
                if (result != 1) return;

                auto* panel = safe.getComponent();
                if (panel == nullptr) return;

                const float v = juce::jlimit (0.0f, 200.0f,
                                    w->getTextEditorContents ("pct").getFloatValue()) * 0.01f;

                if (isReverb) panel->reverbWetBase = v;
                else          panel->delayWetBase  = v;

                if (panel->onAnythingChanged) panel->onAnythingChanged();
            }), false);
    }

    // ── Sliders — ALL 0..100, 101 steps (see FxRange above for native spans) ──
    GoldSlider eqGain[5] {
        { "LO",  0.0f, 100.0f, 50.0f, "" }, { "MLO", 0.0f, 100.0f, 50.0f, "" },
        { "MID", 0.0f, 100.0f, 50.0f, "" }, { "MHI", 0.0f, 100.0f, 50.0f, "" },
        { "HI",  0.0f, 100.0f, 50.0f, "" }
    };
    FreqLabel eqFreq[5];

    GoldSlider rSize { "SIZE", 0.0f, 100.0f, 50.0f, "" };
    GoldSlider rDamp { "DAMP", 0.0f, 100.0f, 50.0f, "" };
    GoldSlider rWet  { "WET",  0.0f, 100.0f,  0.0f, "" };
    // DRY defaults to 100: a reverb is a SEND, so the source is untouched
    // until the user decides otherwise.  See ReverbFx::process.
    GoldSlider rDry  { "DRY",  0.0f, 100.0f, 100.0f, "" };
    GoldSlider rTail { "TAIL", 0.0f, 100.0f,  50.0f, "" };   // RT60 0.45..8 s
    // The reverb SEND band — wet only.  Same control as the channel filter,
    // different question: that one shapes what YOU hear, this one shapes what
    // the REVERB hears.  See ReverbFx::processFdn.
    BandFilterSlider rBand { "SEND BAND" };
    GoldSlider rPre  { "PRE",  0.0f, 100.0f,  0.0f, "" };

    // PAN: 50 = centre, maps to pan -1..+1.
    GoldSlider panSlider { "PAN", 0.0f, 100.0f, 50.0f, "" };

    juce::TextButton btnSig44, btnSig34;
    juce::TextButton delayDivBtns[5];
    int timeSig = 0;
    int delayDivIdx = 2;
    GoldSlider dFB  { "FB",  0.0f, 100.0f, 32.0f, "" };   // 32 -> 0.30 native
    // DRY defaults to 100 for the same reason rDry does: the source is
    // untouched until the user says otherwise, and WET only adds on top.
    GoldSlider dDry { "DRY", 0.0f, 100.0f, 100.0f, "" };
    GoldSlider dWet { "WET", 0.0f, 100.0f,  0.0f, "" };

    // What full travel on each WET slider is worth.  Stored with the slot, set
    // by the LEFT double-click box below.  Defaults differ on purpose - see
    // SlotParams, where the reasoning lives.
    float delayWetBase  = 0.5f;
    float reverbWetBase = 1.0f;

    // ── Chorus / Auto-Wah / Phaser ────────────────────────────────────────────
    // EQ / Reverb / Delay on/off toggles (share the wireFxEnable/refreshFxEnable
    // helpers used by the chorus/wah/phaser toggles; default OFF).
    juce::TextButton eqBtn;   bool eqOn  = false;
    juce::TextButton revBtn;  bool revOn = false;
    juce::TextButton delBtn;  bool delOn = false;

    // ── SWEETENER ────────────────────────────────────────────────────────────
    // Three stages, each with its OWN enable and its OWN controls, plus a
    // block enable and a parallel MIX.  Ganging them behind a single AMOUNT
    // was the first sketch and it was wrong: the three fix three different
    // problems and they do not co-vary, so one knob means over-treating two
    // stages to fix one.
    //
    // DEPTH on SOFTEN is BIPOLAR - 50 is neutral, below softens the attack and
    // above sharpens it, so the same stage puts life back into a dull kit.
    // FREQ on TAME matters for the same reason: a harsh snare sits near 4 kHz,
    // brass near 2.5 kHz, a hat near 7 kHz, and one fixed frequency cannot
    // serve all three.
    juce::TextButton swBtn;      bool swOn      = false;   // whole block
    juce::TextButton swPeakBtn;  bool swPeakOn  = true;
    juce::TextButton swSoftBtn;  bool swSoftOn  = true;
    juce::TextButton swTameBtn;  bool swTameOn  = true;
    juce::TextButton swRoundBtn; bool swRoundOn = true;

    GoldSlider swMix    { "MIX",   0.0f, 100.0f, 100.0f, "" };
    GoldSlider swDepth  { "DEPTH", 0.0f, 100.0f,  50.0f, "" };  // bipolar, 50 = neutral
    GoldSlider swWindow { "WINDOW",0.0f, 100.0f,  24.0f, "" };  // 24 -> ~8 ms
    GoldSlider swTameDb { "TAME",  0.0f, 100.0f,   0.0f, "" };
    GoldSlider swFreq   { "FREQ",  0.0f, 100.0f,  38.0f, "" };  // 38 -> ~4 kHz
    GoldSlider swDrive  { "DRIVE", 0.0f, 100.0f,   0.0f, "" };
    GoldSlider swRndMix { "AMOUNT",0.0f, 100.0f, 100.0f, "" };
    GoldSlider swCeil   { "CEILING",0.0f, 100.0f, 43.0f, "" };  // 43 -> 12 dB
    GoldSlider swRatio  { "RATIO",  0.0f, 100.0f,  0.0f, "" };  //  0 -> 1:1 (off)

    juce::TextButton chBtn;  bool chOn  = false;
    GoldSlider chRate  { "RATE",  0.0f, 100.0f, 50.0f, "" };
    GoldSlider chDepth { "DEPTH", 0.0f, 100.0f, 50.0f, "" };
    GoldSlider chMix   { "MIX",   0.0f, 100.0f,  0.0f, "" };

    juce::TextButton wahBtn; bool wahOn = false;
    GoldSlider wSens { "SENS", 0.0f, 100.0f, 50.0f, "" };
    GoldSlider wRate { "RATE", 0.0f, 100.0f, 12.0f, "" };   // 12 -> ~1.0 Hz
    GoldSlider wLfo  { "LFO",  0.0f, 100.0f,  0.0f, "" };
    GoldSlider wFreq { "FREQ", 0.0f, 100.0f, 23.0f, "" };   // 23 -> ~406 Hz
    GoldSlider wQ    { "Q",    0.0f, 100.0f, 60.0f, "" };
    GoldSlider wMix  { "MIX",  0.0f, 100.0f,  0.0f, "" };

    juce::TextButton phBtn;  bool phOn  = false;
    GoldSlider phRate  { "RATE",  0.0f, 100.0f, 40.0f, "" };
    GoldSlider phDepth { "DEPTH", 0.0f, 100.0f, 80.0f, "" };
    GoldSlider phFB    { "FB",    0.0f, 100.0f, 50.0f, "" };
    GoldSlider phMix   { "MIX",   0.0f, 100.0f,  0.0f, "" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsPanel)
};