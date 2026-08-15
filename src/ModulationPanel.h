#pragma once
//==============================================================================
// ModulationPanel.h  —  Modulation tab of the InstrEditorWindow.
//
// Two framed sections:
//   AMP LFO  |  FILTER LFO
//
// Pitch LFO and Pitch Env were removed — they are overkill for a sample-
// playback arranger voice.  Their SlotParams fields are kept for state
// compatibility but are zeroed on commit (and ignored by the engine), so they
// never modulate pitch.
//==============================================================================

#include <JuceHeader.h>
#include "InstrEditPanel.h"
#include "SliderNorm.h"   // every slider in the sound editors reads 0..100

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
    }

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

        // Pitch LFO / pitch env removed — keep them disabled in saved state.
        p.pitchLfoRate = p.pitchLfoDepth = p.pitchLfoDelay = 0.0f;
        p.pEnvA = p.pEnvD = p.pEnvS = p.pEnvR = p.pEnvDepth = 0.0f;
    }

    /** Non-bass style slots (3..7) use the two-thumb band filter, which has no
        single cutoff to modulate, so the FILTER LFO is hidden for them.  Driven
        by InstrEditorWindow per slot. */
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
        const char* headers[2] = { "AMP LFO", "FILTER LFO" };
        const auto secs = sectionBounds();
        for (int i = 0; i < 2; ++i)
        {
            if (bandMode && i == 1) continue;   // band slots have no filter LFO
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
    }

private:
    std::array<juce::Rectangle<int>, 2> sectionBounds() const
    {
        const int W = getWidth(), H = getHeight();
        const int pad = 6, gap = 8;
        const int secW = (W - 2*pad - gap) / 2;
        std::array<juce::Rectangle<int>, 2> r;
        for (int i = 0; i < 2; ++i)
            r[(size_t)i] = { pad + i * (secW + gap), 0, secW, H };
        return r;
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

    bool bandMode = false;   // hides the FILTER LFO on non-bass style slots

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationPanel)
};