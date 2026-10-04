#pragma once
//==============================================================================
// ModulationPanel.h  —  Modulation tab of the InstrEditorWindow.
//
// Three framed sections:
//   AMP LFO  |  FILTER LFO       (top band)
//   ATTACK GLIDE                 (bottom band, full width, SOLO SLOTS ONLY)
//
// Pitch LFO and Pitch Env were removed — they are overkill for a sample-
// playback arranger voice.  Their SlotParams fields are kept for state
// compatibility but are zeroed on commit (and ignored by the engine), so they
// never modulate pitch.
//
// ATTACK GLIDE is the one thing on this page that DOES touch pitch, and it is
// deliberately not the pitch envelope brought back: an envelope is a shape
// applied to every note for as long as it is held, and this is a fixed-duration
// gesture on the ATTACK only, with its own rules about how often it fires.
//==============================================================================

#include <JuceHeader.h>
#include "InstrEditPanel.h"
#include "SliderNorm.h"   // every slider in the sound editors reads 0..100
#include <cmath>          // std::sqrt / std::lround in the glide mappings

class ModulationPanel : public juce::Component
{
public:
    std::function<void()> onAnythingChanged;

    ModulationPanel()
    {
        auto setup = [this](GoldSlider& s) { addAndMakeVisible(s); s.onChange = notifyFn(); };

        for (auto* s : { &aRate,&aDepth,&aDelay,
                         &fRate,&fDepth,&fDelay })
        { setup(*s); s->setStep (1.0f); }        // 0..100, 101 steps

        wireEnable (aEnable, aOn);
        wireEnable (fEnable, fOn);

        // ── ATTACK GLIDE ─────────────────────────────────────────────────────
        for (auto* sl : { &gDepth,&gTime,&gShape,&gEvery,&gOdds,&gVel })
        { setup(*sl); sl->setStep (1.0f); }

        // Readouts in real units.  A scoop is a musical amount and a duration,
        // and neither is legible as "0..100" - the same reason the release knob
        // reads seconds rather than its own slider position.
        // DEPTH READS IN PITCH, NOT IN SLIDER POSITION, and it switches units at
        // the semitone.  Below 100 cents "0.4 st" tells you nothing you can act
        // on, while "48 c" is a number a musician can aim at; above it, cents
        // stop being legible and semitones are what you think in.
        gDepth .displayFn = [] (float ui)
        {
            const float c = uiToDepthCents (ui);
            return (c < 100.0f) ? (juce::String ((int) std::lround (c)) + " c")
                                : (juce::String (c / 100.0f, 2) + " st");
        };
        gTime  .displayFn = [] (float ui) { return juce::String ((int) uiToTimeMs (ui)) + " ms"; };
        gShape .displayFn = [] (float ui)
        {
            const float k = uiToShapeK (ui);
            const juce::String n = k <= -8.0f ? "SWELL" : k < -2.0f ? "LOG"
                                 : k <=  2.0f ? "LIN"   : k <  9.0f ? "EXP" : "TIGHT";
            return n + " " + juce::String (k, 1);
        };
        // THE TILDE IS THE WHOLE DISTINCTION.  Both sliders read "1 in N" and
        // share one mapping, but EVERY is exact and ODDS is an average - two
        // identical-looking readouts with different meanings is precisely the
        // thing that gets mixed up six months later, so the random one says so.
        gEvery.displayFn = [] (float ui) { return  "1 in " + juce::String (uiToRatioN (ui)); };
        gOdds .displayFn = [] (float ui) { return "~1 in " + juce::String (uiToRatioN (ui)); };

        // The chevron carries the "and everything above" that the number alone
        // does not - the threshold is a floor, not a target.
        gVel  .displayFn = [] (float v)   { return juce::String (">= ") + juce::String ((int) v); };

        const char* modeNames[kNumGlideModes] = { "OFF", "EVERY", "Nth", "RAND", "VEL" };
        for (int i = 0; i < kNumGlideModes; ++i)
        {
            gMode[(size_t) i].setButtonText (modeNames[i]);
            gMode[(size_t) i].onClick = [this, i]
            {
                glideMode = i;
                refreshGlideMode();
                if (onAnythingChanged) onAnythingChanged();
            };
            addAndMakeVisible (gMode[(size_t) i]);
        }
        refreshGlideMode();
    }

    //==========================================================================
    // 0..100 <-> real units.  Static so the readout lambdas can reach them.
    //
    // TIME IS SQUARED, like every other time in the sound editors: the useful
    // part of a scoop lives between about 20 and 120 ms, and a linear 0..500
    // would bury all of it in the bottom quarter of the travel.
    //==========================================================================
    // DEPTH IN CENTS, AND SQUARED, for the same reason TIME is.  A linear
    // 0..12 st put every usable scoop - which lives under about three semitones
    // - in the bottom quarter of the travel, and each slider step was a flat
    // 12 cents, so a 40-cent lean simply was not reachable.  Squared, the first
    // half of the slider covers 0..300 cents at roughly 3 cents a step.
    static constexpr float kMaxDepthCents = 1200.0f;

    static float uiToDepthCents (float ui)
    { const float u = juce::jlimit (0.0f, 100.0f, ui) * 0.01f; return kMaxDepthCents * u * u; }

    static float uiToDepth  (float ui) { return uiToDepthCents (ui) * 0.01f; }   // semitones
    static float depthToUi  (float st)
    {
        const float c = juce::jlimit (0.0f, kMaxDepthCents, st * 100.0f);
        return juce::jlimit (0.0f, 100.0f, 100.0f * std::sqrt (c / kMaxDepthCents));
    }

    static float uiToTimeMs (float ui)
    { const float u = juce::jlimit (0.0f, 100.0f, ui) * 0.01f; return 500.0f * u * u; }
    static float timeMsToUi (float ms)
    { return juce::jlimit (0.0f, 100.0f, 100.0f * std::sqrt (juce::jlimit (0.0f, 500.0f, ms) / 500.0f)); }

    // The SAME law SynthesisPanel's SHAPE knob uses, so +6 reads as EXP on both
    // pages and a number copied from one means the same contour on the other.
    static float uiToShapeK (float ui) { return (juce::jlimit (0.0f, 100.0f, ui) - 50.0f) * 0.4f; }
    static float shapeKToUi (float k)  { return juce::jlimit (0.0f, 100.0f, k / 0.4f + 50.0f); }

    // ── ONE RATIO MAPPING, SHARED BY BOTH MODES ──────────────────────────────
    //
    // 1 in 2 .. 1 in 16: fifteen steps, and the same vocabulary whether the
    // firing is exact (Nth) or averaged (RANDOM).  ODDS used to be a 0..100
    // percentage and that was the wrong shape - above roughly 60% a random
    // glide is indistinguishable from EVERY, so more than half the travel was
    // spent on settings nobody would choose, while the sparse end that actually
    // sounds like an ornament was crammed into the bottom quarter.
    //
    // Fifteen values over 101 slider positions, so every N is reachable and
    // every N round-trips through the step-1 snap.
    static int   uiToRatioN (float ui)
    { return juce::jlimit (2, 16, 2 + (int) std::lround (juce::jlimit (0.0f, 100.0f, ui) * 0.14f)); }
    static float ratioNToUi (int n)
    { return juce::jlimit (0.0f, 100.0f, (float) (juce::jlimit (2, 16, n) - 2) / 0.14f); }

    void loadParams(const SlotParams& p)
    {
        aRate .setValue (Betel::Norm::rateToUi  (p.ampLfoRate));
        aDepth.setValue (Betel::Norm::unitToUi  (p.ampLfoDepth));
        aDelay.setValue (Betel::Norm::delayToUi (p.ampLfoDelay));
        fRate .setValue (Betel::Norm::rateToUi  (p.filtLfoRate));
        fDepth.setValue (Betel::Norm::unitToUi  (p.filtLfoDepth));
        fDelay.setValue (Betel::Norm::delayToUi (p.filtLfoDelay));

        aOn = p.ampLfoEnabled;   refreshEnable(aEnable, aOn);
        fOn = p.filtLfoEnabled;  refreshEnable(fEnable, fOn);

        gDepth .setValue (depthToUi  (p.glideDepth));
        gTime  .setValue (timeMsToUi (p.glideTimeMs));
        gShape .setValue (shapeKToUi (p.glideShapeK));
        gEvery.setValue (ratioNToUi (p.glideEveryN));
        gOdds .setValue (ratioNToUi (p.glideOdds));
        gVel  .setValue ((float) juce::jlimit (1, 127, p.glideVelMin));
        glideMode = juce::jlimit (0, kNumGlideModes - 1, p.glideMode);
        refreshGlideMode();
    }

    void readInto(SlotParams& p) const
    {
        p.ampLfoRate   = Betel::Norm::uiToRate  (aRate .getValue());
        p.ampLfoDepth  = Betel::Norm::uiToUnit  (aDepth.getValue());
        p.ampLfoDelay  = Betel::Norm::uiToDelay (aDelay.getValue());
        p.filtLfoRate  = Betel::Norm::uiToRate  (fRate .getValue());
        p.filtLfoDepth = Betel::Norm::uiToUnit  (fDepth.getValue());
        p.filtLfoDelay = Betel::Norm::uiToDelay (fDelay.getValue());

        p.ampLfoEnabled  = aOn;
        p.filtLfoEnabled = fOn;

        p.glideMode    = glideMode;
        p.glideDepth   = uiToDepth  (gDepth.getValue());
        p.glideTimeMs  = uiToTimeMs (gTime .getValue());
        p.glideShapeK  = uiToShapeK (gShape.getValue());
        p.glideEveryN  = uiToRatioN (gEvery.getValue());
        p.glideOdds    = uiToRatioN (gOdds .getValue());
        p.glideVelMin  = juce::jlimit (1, 127, (int) std::lround (gVel.getValue()));

        // Pitch LFO / pitch env removed — keep them disabled in saved state.
        p.pitchLfoRate = p.pitchLfoDepth = p.pitchLfoDelay = 0.0f;
        p.pEnvA = p.pEnvD = p.pEnvS = p.pEnvR = p.pEnvDepth = 0.0f;
    }

    /** Non-bass style slots (3..7) use the two-thumb band filter, which has no
        single cutoff to modulate, so the FILTER LFO is hidden for them.  Driven
        by InstrEditorWindow per slot. */
    /** ATTACK GLIDE is right-hand only, so a style slot does not get the frame
        at all.  Channel::noteOn refuses it independently - this is the courtesy,
        not the guarantee. */
    void setSoloMode(bool isSolo)
    {
        if (soloMode == isSolo) return;
        soloMode = isSolo;
        refreshGlideMode();
        resized();
        repaint();
    }

    void setBandMode(bool useBand)
    {
        if (bandMode == useBand) return;
        bandMode = useBand;
        fEnable.setVisible (! bandMode);
        for (auto* s : { &fRate,&fDepth,&fDelay }) s->setVisible (! bandMode);
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const char* headers[3] = { "AMP LFO", "FILTER LFO", "ATTACK GLIDE" };
        const auto secs = sectionBounds();
        for (int i = 0; i < 3; ++i)
        {
            if (bandMode   && i == 1) continue;  // band slots have no filter LFO
            if (! soloMode && i == 2) continue;  // style slots have no glide
            InstrEditStyle::paintComponentFrame(g, secs[(size_t) i], headers[i]);
        }
    }

    void resized() override
    {
        const auto secs = sectionBounds();

        auto contentA = InstrEditStyle::componentFrameContent(secs[0]);
        auto contentF = InstrEditStyle::componentFrameContent(secs[1]);

        // Top strip in each frame holds the LFO on/off toggle; sliders fill the rest.
        const int btnH = 22, btnW = 64;
        aEnable.setBounds (contentA.removeFromTop(btnH).removeFromLeft(btnW));
        contentA.removeFromTop(4);
        layoutRow({ &aRate,&aDepth,&aDelay }, contentA);

        if (! bandMode)
        {
            fEnable.setBounds (contentF.removeFromTop(btnH).removeFromLeft(btnW));
            contentF.removeFromTop(4);
            layoutRow({ &fRate,&fDepth,&fDelay }, contentF);
        }

        if (soloMode)
        {
            auto contentG = InstrEditStyle::componentFrameContent(secs[2]);
            auto modeRow  = contentG.removeFromTop(btnH);
            contentG.removeFromTop(4);

            // kNumGlideModes, NOT A LITERAL.  This loop still said 4 when the
            // array went to 5, so VEL was constructed, labelled, wired and made
            // visible - and then never given bounds, which makes a Component
            // zero-sized and therefore invisible.  Nothing warns about that;
            // the button simply is not there.  Every loop over gMode now reads
            // its length from the same constant the array is declared with.
            //
            // The width is derived rather than fixed for the same reason: a
            // hardcoded 58 happens to fit five buttons and would silently run
            // off the end at six.
            const int mg = 4;
            const int mw = juce::jmin (58,
                             (modeRow.getWidth() - (kNumGlideModes - 1) * mg)
                               / kNumGlideModes);

            for (int i = 0; i < kNumGlideModes; ++i)
                gMode[(size_t) i].setBounds (modeRow.getX() + i * (mw + mg),
                                             modeRow.getY(), mw, btnH);

            layoutRow({ &gDepth,&gTime,&gShape,&gEvery,&gOdds,&gVel }, contentG);
        }
    }

private:
    //==========================================================================
    // TWO BANDS, NOT THREE COLUMNS.
    //
    // The glide has five sliders and a four-button mode row; a third of the
    // width would give each slider about 40 px, which is under the GoldSlider's
    // own locked handle diameter.  A full-width bottom band costs the two LFO
    // frames some height - they have three sliders each and can afford it - and
    // is the only arrangement where every control is actually usable.
    //
    // On a STYLE slot the glide band is not reserved at all, so those slots keep
    // the full-height LFO frames they have always had.
    //==========================================================================
    std::array<juce::Rectangle<int>, 3> sectionBounds() const
    {
        const int W = getWidth(), H = getHeight();
        const int pad = 6, gap = 8, vgap = 6;

        const int topH = soloMode ? juce::jmax (60, (H - vgap) * 55 / 100) : H;
        const int botY = topH + vgap;
        const int botH = juce::jmax (0, H - botY);

        const int secW = (W - 2*pad - gap) / 2;
        std::array<juce::Rectangle<int>, 3> r;
        for (int i = 0; i < 2; ++i)
            r[(size_t)i] = { pad + i * (secW + gap), 0, secW, topH };
        r[2] = { pad, botY, W - 2*pad, botH };
        return r;
    }

    /** OFF greys the value sliders; the two conditional ones are inert unless
        their own mode is selected.  Visibly inert rather than hidden: a control
        that vanishes reads as a bug, one that is dimmed reads as "not for this
        mode", which is what it is. */
    void refreshGlideMode()
    {
        for (int i = 0; i < kNumGlideModes; ++i)
        {
            gMode[(size_t) i].setVisible (soloMode);
            InstrEditStyle::styleSquareButton (gMode[(size_t) i], soloMode && glideMode == i);
        }

        const bool live = soloMode && glideMode != 0;

        // setInterceptsMouseClicks IS THE ONE THAT MATTERS.  GoldSlider is a
        // plain Component with its own mouseDrag and never consults isEnabled(),
        // so setEnabled alone would dim a control that still moved when dragged
        // - worse than leaving it lit, because it would look inert and behave
        // live.  setAlpha says it, setInterceptsMouseClicks means it.
        auto set = [] (GoldSlider& sl, bool vis, bool on)
        {
            sl.setVisible (vis);
            sl.setEnabled (on);
            sl.setInterceptsMouseClicks (on, on);
            sl.setAlpha   (on ? 1.0f : 0.35f);
        };

        set (gDepth,  soloMode, live);
        set (gTime,   soloMode, live);
        set (gShape,  soloMode, live);
        set (gEvery,  soloMode, live && glideMode == 2);
        set (gOdds,   soloMode, live && glideMode == 3);
        set (gVel,    soloMode, live && glideMode == 4);
    }

    std::function<void(float)> notifyFn()
    { return [this](float){ if (onAnythingChanged) onAnythingChanged(); }; }

    void wireEnable(juce::TextButton& b, bool& state)
    {
        b.onClick = [this, &b, &state]
        {
            state = ! state;
            refreshEnable(b, state);
            if (onAnythingChanged) onAnythingChanged();
        };
        addAndMakeVisible(b);
    }

    void refreshEnable(juce::TextButton& b, bool state)
    {
        b.setButtonText(state ? "ON" : "OFF");
        InstrEditStyle::styleSquareButton(b, state);
    }

    void layoutRow(std::initializer_list<GoldSlider*> sliders, juce::Rectangle<int> bounds)
    {
        const int n = (int) sliders.size();
        if (n <= 0) return;
        const int g = 4;
        const int w = (bounds.getWidth() - (n - 1) * g) / n;
        int i = 0;
        for (auto* s : sliders)
            s->setBounds(bounds.getX() + i++ * (w + g), bounds.getY(), w, bounds.getHeight());
    }

    // Amp LFO
    juce::TextButton aEnable;  bool aOn = false;
    GoldSlider aRate  { "RATE",  0.0f, 100.0f, 0.0f, "" };
    GoldSlider aDepth { "DEPTH", 0.0f, 100.0f, 0.0f, "" };
    GoldSlider aDelay { "DELAY", 0.0f, 100.0f, 0.0f, "" };

    // Filter LFO
    juce::TextButton fEnable;  bool fOn = false;
    GoldSlider fRate  { "RATE",  0.0f, 100.0f, 0.0f, "" };
    GoldSlider fDepth { "DEPTH", 0.0f, 100.0f, 0.0f, "" };
    GoldSlider fDelay { "DELAY", 0.0f, 100.0f, 0.0f, "" };

    // Attack glide
    static constexpr int kNumGlideModes = 5;   // OFF / EVERY / Nth / RAND / VEL
    std::array<juce::TextButton, kNumGlideModes> gMode;
    int        glideMode = 0;
    GoldSlider gDepth  { "DEPTH",  0.0f, 100.0f, 50.0f, "" };   // 300 c = 3.00 st
    GoldSlider gTime   { "TIME",   0.0f, 100.0f, 35.0f, "" };   // ~61 ms
    GoldSlider gShape  { "SHAPE",  0.0f, 100.0f, 65.0f, "" };   // k +6, EXP
    GoldSlider gEvery  { "EVERY",  0.0f, 100.0f, 14.0f, "" };   // 1 in 4, exact
    GoldSlider gOdds   { "ODDS",   0.0f, 100.0f, 21.0f, "" };   // ~1 in 5, average

    // 1..127 DIRECTLY, not the 0..100 the rest of the row uses.  A velocity
    // threshold is a MIDI number the player already knows from every other
    // velocity display in the plugin; rescaling it to 0..100 would mean the one
    // control whose units are unambiguous was the one that got translated.
    GoldSlider gVel    { "VEL",    1.0f, 127.0f, 100.0f, "" };

    bool soloMode = false;   // hides ATTACK GLIDE on style slots
    bool bandMode = false;   // hides the FILTER LFO on non-bass style slots

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationPanel)
};