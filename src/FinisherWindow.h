#pragma once
//==============================================================================
// FinisherWindow.h  —  the FINISHER editor popup.
//
// Opened by the FINISHER button in the mixer's MASTER frame.  Follows the same
// pattern as DrumsPopup / InstrEditorWindow: a juce::DocumentWindow with a
// non-native title bar, owned content, and an onClosed callback.
//
// NON-MODAL on purpose — you can keep playing while you dial it in, which is
// the whole point of a live tool.
//
// ── The model ───────────────────────────────────────────────────────────────
//   CHARACTER  loads a preset INTO the sliders, so the window always shows the
//              values that are actually running.  Nothing is hidden.
//   AMOUNT     a DRY/WET blend between the untouched input and the whole
//              tone-and-dynamics chain.  It no longer scales each stage's
//              depth: the sliders are absolute, so what a slider says is what
//              the wet path does, and AMOUNT decides how much of it you hear.
//              The ceiling stays fully wet at every setting, so AMOUNT 0 is
//              the limiter alone rather than a bypass.  It is still the one
//              control you touch mid-song.
//   Sliders    editable.  Touching one flips CHARACTER to CUSTOM, so it is
//              always obvious the preset is no longer being followed.
//              RESET TO CHARACTER reloads it.
//   Toggles    bypass individual stages, for A/B-ing whether a stage is
//              actually earning its place.
//
// Every row is built from Betel::Finisher's own parameter metadata
// (paramName / paramRange / paramSuffix / stageForParam), so a range can never
// drift out of sync between this window and the DSP.
//==============================================================================

#include <JuceHeader.h>
#include "GrexPopupWindow.h"   // stays in front, minimisable, never vanishes
#include "Finisher.h"

class FinisherWindow : public juce::DocumentWindow
{
public:
    // ── UI → engine ──────────────────────────────────────────────────────────
    std::function<void(bool)>              onEnabledChanged;
    std::function<void(float)>             onAmountChanged;
    std::function<void(int)>               onCharacterChanged;
    std::function<void(int, float)>        onParamChanged;    // (paramId, value)
    std::function<void(int, bool)>         onStageToggled;    // (stageId, on)
    std::function<void()>                  onClosed;

    FinisherWindow()
        : juce::DocumentWindow ("Finisher",
                                juce::Colour (0xFF1A1A1A),
                                juce::DocumentWindow::closeButton)
    {
        Betel::applyGrexPopupBehaviour (*this);
        setUsingNativeTitleBar (false);
        setResizable (true, true);
        setResizeLimits (620, 470, 1200, 900);
        content = new Content (*this);
        setContentOwned (content, true);
        Betel::centreGrexPopupOnScreen (*this, 720, 520);
    }

    ~FinisherWindow() override { clearContentComponent(); }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClosed) onClosed();
    }

    void openCentredOver (juce::Component* parent)
    {
        if (parent != nullptr)
        {
            const auto pb = parent->getScreenBounds();
            setBounds (pb.getCentreX() - getWidth()  / 2,
                       pb.getCentreY() - getHeight() / 2,
                       getWidth(), getHeight());
        }
        setVisible (true);
        toFront (true);
    }

    // ── engine → UI ──────────────────────────────────────────────────────────
    void setEnabled       (bool on)          { content->setEnabledState (on); }
    void setAmount        (float pct)        { content->setAmountValue (pct); }
    void setCharacter     (int c)            { content->setCharacterValue (c); }
    void setParam         (int id, float v)  { content->setParamValue (id, v); }
    void setStageEnabled  (int s, bool on)   { content->setStageValue (s, on); }
    void setGainReduction (float db)         { content->setGr (db); }

private:
    //==========================================================================
    class Content : public juce::Component
    {
    public:
        explicit Content (FinisherWindow& o) : owner (o)
        {
            // ── Master row ────────────────────────────────────────────────────
            onBtn.setButtonText ("ON");
            onBtn.setClickingTogglesState (true);
            // Lit ORANGE when engaged.  Without this the button looked identical
            // on and off, which made "is it even running?" unanswerable.
            onBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
            onBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFCC6600));
            onBtn.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xFF999999));
            onBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
            onBtn.onClick = [this]
            {
                if (owner.onEnabledChanged) owner.onEnabledChanged (onBtn.getToggleState());
                repaint();
            };
            addAndMakeVisible (onBtn);

            // Labelled DRY/WET because that is now literally what it does —
            // it blends the chain against the untouched input rather than
            // scaling each stage's depth.  See the note in Finisher::resolve().
            amountLabel.setText ("AMOUNT (DRY/WET)", juce::dontSendNotification);
            styleName (amountLabel);
            addAndMakeVisible (amountLabel);

            styleSlider (amountSlider);
            amountSlider.setRange (0.0, 100.0, 1.0);
            amountSlider.setValue (50.0, juce::dontSendNotification);
            amountSlider.onValueChange = [this]
            {
                amountValue.setText (juce::String ((int) amountSlider.getValue()) + " %",
                                     juce::dontSendNotification);
                if (owner.onAmountChanged) owner.onAmountChanged ((float) amountSlider.getValue());
            };
            addAndMakeVisible (amountSlider);

            styleValue (amountValue);
            amountValue.setText ("50 %", juce::dontSendNotification);
            addAndMakeVisible (amountValue);

            charLabel.setText ("CHARACTER", juce::dontSendNotification);
            styleName (charLabel);
            addAndMakeVisible (charLabel);

            // CHARACTER as a HORIZONTAL row of radio buttons.  SelectorGroup
            // stacks its options vertically (itemH = height / n), so six of them
            // in a 26 px row collapsed into unreadable 4 px stripes — and its
            // mouseDown ignores isEnabled(), so it stayed clickable when nothing
            // else was.  Plain TextButtons in a radio group avoid both problems.
            for (int i = 0; i < kNumChars; ++i)
            {
                auto& b = charBtn[(size_t) i];
                b.setButtonText (kCharNames[i]);
                b.setClickingTogglesState (true);
                b.setRadioGroupId (kCharRadioGroup);
                b.setConnectedEdges (((i > 0) ? juce::Button::ConnectedOnLeft : 0)
                                   | ((i < kNumChars - 1) ? juce::Button::ConnectedOnRight : 0));
                b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
                b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFCC6600));
                b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xFF999999));
                b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
                b.onClick = [this, i]
                {
                    if (! charBtn[(size_t) i].getToggleState()) return;   // only the newly-lit one
                    if (owner.onCharacterChanged) owner.onCharacterChanged (i);
                };
                addAndMakeVisible (b);
            }
            charBtn[0].setToggleState (true, juce::dontSendNotification);

            // ── One row per parameter, built from the DSP's own metadata ──────
            for (int i = 0; i < Betel::Finisher::kNumParams; ++i)
            {
                auto& r = rows[(size_t) i];
                r.paramId = i;
                r.stageId = Betel::Finisher::stageForParam (i);

                // Only the FIRST parameter of a stage carries the stage toggle;
                // the second (EQ air, BASS weight) shares the row above's.
                r.ownsToggle = (r.stageId >= 0) && (i == firstParamOfStage (r.stageId));

                if (r.ownsToggle)
                {
                    r.toggle.setToggleState (true, juce::dontSendNotification);
                    r.toggle.setColour (juce::ToggleButton::tickColourId,
                                        juce::Colour (0xFFD4AF37));
                    r.toggle.setColour (juce::ToggleButton::tickDisabledColourId,
                                        juce::Colour (0xFF666666));
                    const int stage = r.stageId;
                    r.toggle.onClick = [this, stage]
                    {
                        const bool on = stageToggleState (stage);
                        if (owner.onStageToggled) owner.onStageToggled (stage, on);
                    };
                    addAndMakeVisible (r.toggle);
                }

                r.name.setText (Betel::Finisher::paramName (i), juce::dontSendNotification);
                styleName (r.name);
                addAndMakeVisible (r.name);

                float lo, hi, st;
                Betel::Finisher::paramRange (i, lo, hi, st);
                styleSlider (r.slider);
                r.slider.setRange ((double) lo, (double) hi, (double) st);
                r.slider.setValue ((double) lo, juce::dontSendNotification);
                const int pid = i;
                r.slider.onValueChange = [this, pid]
                {
                    auto& rr = rows[(size_t) pid];
                    rr.value.setText (formatValue (pid, (float) rr.slider.getValue()),
                                      juce::dontSendNotification);
                    // Touching a slider means we are no longer following the
                    // preset verbatim — say so, rather than silently lying.
                    if (! suppressCustom)
                        setCharacterValue (Betel::Finisher::kCustom);
                    if (owner.onParamChanged) owner.onParamChanged (pid, (float) rr.slider.getValue());
                };
                addAndMakeVisible (r.slider);

                styleValue (r.value);
                addAndMakeVisible (r.value);
            }

            // ── Footer ────────────────────────────────────────────────────────
            bypassBtn.setButtonText ("A / B BYPASS");
            bypassBtn.setClickingTogglesState (true);
            bypassBtn.onClick = [this]
            {
                // Momentary compare: flips the engine off without disturbing the
                // ON button's own state, so you can hear it in and out fast.
                const bool bypassed = bypassBtn.getToggleState();
                if (owner.onEnabledChanged)
                    owner.onEnabledChanged (bypassed ? false : onBtn.getToggleState());
            };
            addAndMakeVisible (bypassBtn);

            resetBtn.setButtonText ("RESET TO CHARACTER");
            resetBtn.onClick = [this]
            {
                // Reload whatever character is showing.  If we are on CUSTOM
                // there is nothing to reload, so fall back to CLEAN.
                int c = selectedCharacter();
                if (c == Betel::Finisher::kCustom) c = Betel::Finisher::kClean;
                setCharacterValue (c);
                if (owner.onCharacterChanged) owner.onCharacterChanged (c);
            };
            addAndMakeVisible (resetBtn);

            setSize (720, 520);
        }

        //======================================================================
        void setEnabledState (bool on)
        {
            // NOTE: the controls are deliberately left ENABLED whether or not the
            // finisher is running.  Gating them behind the ON state made the whole
            // window dead on open (it opens with the effect off), which read as
            // "nothing works".  You can now dial a patch in while it is bypassed
            // and hear it the moment you switch it on.
            onBtn.setToggleState (on, juce::dontSendNotification);
            repaint();
        }

        void setAmountValue (float pct)
        {
            amountSlider.setValue ((double) pct, juce::dontSendNotification);
            amountValue.setText (juce::String ((int) pct) + " %", juce::dontSendNotification);
        }

        void setCharacterValue (int c)
        {
            if (c < 0 || c >= kNumChars) return;
            charBtn[(size_t) c].setToggleState (true, juce::dontSendNotification);
        }

        /** Engine → slider.  suppressCustom keeps a programmatic refresh from
            flipping the selector to CUSTOM the way a user drag would. */
        void setParamValue (int id, float v)
        {
            if (id < 0 || id >= Betel::Finisher::kNumParams) return;
            auto& r = rows[(size_t) id];
            suppressCustom = true;
            r.slider.setValue ((double) v, juce::dontSendNotification);
            r.value.setText (formatValue (id, v), juce::dontSendNotification);
            suppressCustom = false;
        }

        void setStageValue (int stage, bool on)
        {
            for (auto& r : rows)
                if (r.ownsToggle && r.stageId == stage)
                    r.toggle.setToggleState (on, juce::dontSendNotification);
            repaint();
        }

        void setGr (float db)
        {
            if (std::abs (db - grDb) < 0.1f) return;
            grDb = db;
            repaint (meterArea);
        }

        //======================================================================
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xFF1A1A1A));

            g.setColour (juce::Colour (0xFF2E2E2E));
            g.drawLine ((float) kPad, (float) masterH,
                        (float) (getWidth() - kPad), (float) masterH, 1.0f);

            // Gain-reduction meter: fills right-to-left, amber → red when it is
            // working hard, so heavy limiting is obvious without reading a number.
            if (! meterArea.isEmpty())
            {
                g.setColour (juce::Colour (0xFF0C0C0C));
                g.fillRect (meterArea);

                if (onBtn.getToggleState() && grDb > 0.05f)
                {
                    const float norm = juce::jlimit (0.0f, 1.0f, grDb / kMeterRangeDb);
                    auto mf  = meterArea.toFloat();
                    auto bar = mf.removeFromRight (mf.getWidth() * norm);
                    g.setColour (grDb >= kMeterHotDb ? juce::Colour (0xFFCC3322)
                                                     : juce::Colour (0xFFD4AF37));
                    g.fillRect (bar);
                }

                g.setColour (juce::Colour (0xFF999999));
                g.setFont (juce::Font (12.0f, juce::Font::bold));
                g.drawText ("GR", meterArea.withX (kPad).withWidth (26),
                            juce::Justification::centredLeft, false);
                g.drawText (juce::String (grDb, 1) + " dB",
                            meterArea.withX (meterArea.getRight() + 6).withWidth (60),
                            juce::Justification::centredLeft, false);
            }
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (kPad);

            // ── Master block ─────────────────────────────────────────────────
            {
                auto row = b.removeFromTop (kRowH);
                onBtn       .setBounds (row.removeFromLeft (60).reduced (0, 3));
                row.removeFromLeft (8);
                amountLabel .setBounds (row.removeFromLeft (kNameW));
                amountValue .setBounds (row.removeFromRight (kValueW));
                amountSlider.setBounds (row.reduced (6, 4));

                b.removeFromTop (6);
                auto crow = b.removeFromTop (kRowH);
                charLabel.setBounds (crow.removeFromLeft (60 + 8 + kNameW));
                crow.removeFromLeft (6);
                const int cw = crow.getWidth() / kNumChars;
                for (int i = 0; i < kNumChars; ++i)
                    charBtn[(size_t) i].setBounds (crow.getX() + i * cw, crow.getY() + 2,
                                                   cw, crow.getHeight() - 4);

                b.removeFromTop (6);
                auto mrow = b.removeFromTop (16);
                mrow.removeFromLeft (30);          // "GR" caption, drawn in paint()
                mrow.removeFromRight (66);         // dB readout, drawn in paint()
                meterArea = mrow.reduced (0, 5);

                masterH = b.getY();
                b.removeFromTop (10);
            }

            // ── Chain rows ───────────────────────────────────────────────────
            for (auto& r : rows)
            {
                auto row = b.removeFromTop (kRowH);
                auto tog = row.removeFromLeft (26);
                if (r.ownsToggle) r.toggle.setBounds (tog.reduced (2, 6));
                row.removeFromLeft (4);
                r.name .setBounds (row.removeFromLeft (kNameW));
                r.value.setBounds (row.removeFromRight (kValueW));
                r.slider.setBounds (row.reduced (6, 5));
                b.removeFromTop (2);
            }

            // ── Footer ───────────────────────────────────────────────────────
            auto foot = b.removeFromBottom (kRowH);
            const int half = (foot.getWidth() - 8) / 2;
            bypassBtn.setBounds (foot.removeFromLeft  (half).reduced (0, 3));
            resetBtn .setBounds (foot.removeFromRight (half).reduced (0, 3));
        }

    private:
        struct Row
        {
            int              paramId = 0;
            int              stageId = -1;
            bool             ownsToggle = false;
            juce::ToggleButton toggle;
            juce::Label      name, value;
            juce::Slider     slider;
        };

        static int firstParamOfStage (int stage) noexcept
        {
            for (int i = 0; i < Betel::Finisher::kNumParams; ++i)
                if (Betel::Finisher::stageForParam (i) == stage) return i;
            return -1;
        }

        int selectedCharacter() const
        {
            for (int i = 0; i < kNumChars; ++i)
                if (charBtn[(size_t) i].getToggleState()) return i;
            return Betel::Finisher::kClean;
        }

        bool stageToggleState (int stage) const
        {
            for (const auto& r : rows)
                if (r.ownsToggle && r.stageId == stage) return r.toggle.getToggleState();
            return true;
        }

        static juce::String formatValue (int id, float v)
        {
            float lo, hi, st;
            Betel::Finisher::paramRange (id, lo, hi, st);
            const int decimals = (st < 1.0f) ? ((st < 0.1f) ? 2 : 1) : 0;
            return juce::String (v, decimals) + Betel::Finisher::paramSuffix (id);
        }

        static void styleName (juce::Label& l)
        {
            l.setColour (juce::Label::textColourId, juce::Colour (0xFFCCCCCC));
            l.setFont (juce::Font (13.0f, juce::Font::bold));
        }

        static void styleValue (juce::Label& l)
        {
            l.setJustificationType (juce::Justification::centredRight);
            l.setColour (juce::Label::textColourId, juce::Colour (0xFFD4AF37));
            l.setFont (juce::Font (13.0f, juce::Font::bold));
        }

        static void styleSlider (juce::Slider& s)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            s.setColour (juce::Slider::trackColourId,      juce::Colour (0xFFCC6600));
            s.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF0C0C0C));
            s.setColour (juce::Slider::thumbColourId,      juce::Colour (0xFFD4AF37));
        }

        static constexpr int   kPad    = 14;
        static constexpr int   kRowH   = 30;
        static constexpr int   kNameW  = 116;
        static constexpr int   kValueW = 62;
        static constexpr float kMeterRangeDb = 12.0f;
        static constexpr float kMeterHotDb   = 6.0f;

        FinisherWindow&  owner;

        juce::TextButton onBtn, bypassBtn, resetBtn;
        juce::Label      amountLabel, amountValue, charLabel;
        juce::Slider     amountSlider;
        static constexpr int kNumChars      = Betel::Finisher::kNumCharacters;   // incl. CUSTOM
        static constexpr int kCharRadioGroup = 0x5F1;
        static constexpr const char* kCharNames[6] =
            { "CLEAN", "WARM", "BRIGHT", "FAT", "LIVE", "CUSTOM" };

        std::array<juce::TextButton, (size_t) kNumChars> charBtn;

        std::array<Row, (size_t) Betel::Finisher::kNumParams> rows;

        juce::Rectangle<int> meterArea;
        int   masterH       = 0;
        float grDb          = 0.0f;
        bool  suppressCustom = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

    Content* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FinisherWindow)
};



