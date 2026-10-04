




#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
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
#include "DuckerEditor.h"
#include "GrexPopupWindow.h"   // stays in front, minimisable, never vanishes
#include "Finisher.h"

class FinisherWindow : public juce::DocumentWindow
{
public:
    // ── UI → engine ──────────────────────────────────────────────────────────
    std::function<void(bool)>              onEnabledChanged;
    std::function<void(float)>             onAmountChanged;
    std::function<void(int, float)>        onParamChanged;    // (paramId, value)
    std::function<void(int, bool)>         onStageToggled;    // (stageId, on)

    /** RESET pressed.  The host restores the factory values and pushes them
        back; the window does not own the numbers and must not invent them. */
    std::function<void()>                  onResetRequested;
    std::function<void()>                  onClosed;

    /** Any DUCKER control moved.  The host pushes the value at the engine and
        saves it with the set; the window owns none of it. */
    std::function<void()>                  onDuckerChanged;

    /** THE DUCKER IS PASSED IN, not owned and not copied.
        Its editor has to read `getGainReductionDb` at 30 Hz to animate, so a
        snapshot would be a still photograph of a thing whose whole point is
        that it moves.  The window holds a reference to the live object. */
    explicit FinisherWindow (Betel::StyleDucker& d)
        : juce::DocumentWindow ("Finisher",
                                juce::Colour (0xFF1A1A1A),
                                juce::DocumentWindow::closeButton)
    {
        Betel::applyGrexPopupBehaviour (*this);
        setUsingNativeTitleBar (false);
        setResizable (true, true);
        // Eleven chain rows plus the master block, the footer and the padding
        // come to roughly 516 px of CONTENT.  The old 470 minimum could not fit
        // that and the old 520 default only just could, which is why the footer
        // was the thing that lost.
        setResizeLimits (620, 560, 1200, 980);
        content = new Content (*this, d);
        setContentOwned (content, true);
        Betel::centreGrexPopupOnScreen (*this, 720, 600);
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
    void setParam         (int id, float v)  { content->setParamValue (id, v); }
    void setStageEnabled  (int s, bool on)   { content->setStageValue (s, on); }
    void setGainReduction (float db)         { content->setGr (db); }

    /** Re-seed the DUCKER page from the engine — after a set load, or on open. */
    void refreshDucker()                     { content->refreshDuckerPage(); }

private:
    //==========================================================================
    //==========================================================================
    // THE DUCKER PAGE — the curve, plus the five controls the drag cannot carry.
    //
    // FREQ and DEPTH are the drag; everything else is a slider, because they
    // have no natural position on a frequency/gain plane.  Trying to encode Q as
    // handle width or ratio as colour would be clever and unreadable.
    //==========================================================================
    class DuckerPage : public juce::Component,
                       private juce::Timer
    {
    public:
        DuckerPage (FinisherWindow& o, Betel::StyleDucker& d)
            : owner (o), ducker (d), editor (d)
        {
            onBtn.setButtonText ("DUCKER ON");
            onBtn.setClickingTogglesState (true);
            onBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
            onBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Betel::Pal::kAccent));
            onBtn.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xFF999999));
            onBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
            onBtn.onClick = [this]
            {
                ducker.setEnabled (onBtn.getToggleState());
                notify();
            };
            addAndMakeVisible (onBtn);

            hint.setText ("Drag a point: across = frequency, down = how deep it may duck. "
                          "Double-click a point to zero it.",
                          juce::dontSendNotification);
            hint.setJustificationType (juce::Justification::centredLeft);
            hint.setColour (juce::Label::textColourId, juce::Colour (0xFF808080));
            hint.setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (hint);

            editor.onPointMoved = [this] (int, float, float) { notify(); refreshReadouts(); };
            editor.onPointSelected = [this] (int b) { selected = b; refreshReadouts(); };
            addAndMakeVisible (editor);

            for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
            {
                auto& col = cols[(size_t) b];
                const int band = b;

                col.title.setText ("POINT " + juce::String (b + 1), juce::dontSendNotification);
                col.title.setColour (juce::Label::textColourId, juce::Colour (0xFFCCCCCC));
                col.title.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                addAndMakeVisible (col.title);

                col.on.setButtonText ("ON");
                col.on.setClickingTogglesState (true);
                col.on.setToggleState (true, juce::dontSendNotification);
                col.on.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
                col.on.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Betel::Pal::kAccent));
                col.on.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xFF999999));
                col.on.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
                col.on.onClick = [this, band]
                {
                    ducker.setBandEnabled (band, cols[(size_t) band].on.getToggleState());
                    editor.refresh();
                    notify();
                };
                addAndMakeVisible (col.on);

                auto setupRow = [this, band] (Ctl& c, const char* name,
                                              double lo, double hi, double step,
                                              std::function<void(int, float)> apply)
                {
                    c.name.setText (name, juce::dontSendNotification);
                    c.name.setColour (juce::Label::textColourId, juce::Colour (0xFFAAAAAA));
                    c.name.setFont (juce::FontOptions (11.0f));
                    addAndMakeVisible (c.name);

                    c.value.setColour (juce::Label::textColourId, juce::Colour (Betel::Pal::kAccentBright));
                    c.value.setFont (juce::FontOptions (11.0f));
                    c.value.setJustificationType (juce::Justification::centredRight);
                    addAndMakeVisible (c.value);

                    c.slider.setSliderStyle (juce::Slider::LinearHorizontal);
                    c.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                    c.slider.setRange (lo, hi, step);
                    c.slider.setColour (juce::Slider::trackColourId,      juce::Colour (Betel::Pal::kAccent));
                    c.slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF0C0C0C));
                    c.slider.setColour (juce::Slider::thumbColourId,      juce::Colour (Betel::Pal::kAccentBright));
                    c.slider.onValueChange = [this, band, &c, apply]
                    {
                        if (seeding) return;
                        apply (band, (float) c.slider.getValue());
                        refreshReadouts();
                        editor.refresh();
                        notify();
                    };
                    addAndMakeVisible (c.slider);
                };

                setupRow (col.q,      "Q",       Betel::StyleDucker::kMinQ,
                                                 Betel::StyleDucker::kMaxQ, 0.05,
                          [this] (int i, float v) { ducker.setQ (i, v); });
                setupRow (col.thresh, "THRESH",  Betel::StyleDucker::kMinThreshDb,
                                                 Betel::StyleDucker::kMaxThreshDb, 0.5,
                          [this] (int i, float v) { ducker.setThresholdDb (i, v); });
                setupRow (col.ratio,  "RATIO",   Betel::StyleDucker::kMinRatio,
                                                 Betel::StyleDucker::kMaxRatio, 0.1,
                          [this] (int i, float v) { ducker.setRatio (i, v); });
                setupRow (col.attack, "ATTACK",  Betel::StyleDucker::kMinAttackMs,
                                                 Betel::StyleDucker::kMaxAttackMs, 0.5,
                          [this] (int i, float v) { ducker.setAttackMs (i, v); });
                setupRow (col.release,"RELEASE", Betel::StyleDucker::kMinRelMs,
                                                 Betel::StyleDucker::kMaxRelMs, 5.0,
                          [this] (int i, float v) { ducker.setReleaseMs (i, v); });
            }

            refreshFromDucker();

            // 20 Hz is plenty for a level meter and half the editor's rate:
            // this one only has to answer "is the key over the line", which is
            // a slower question than "where is the curve right now".
            startTimerHz (20);
        }

        ~DuckerPage() override { stopTimer(); }

        /** Pull EVERY control from the ducker.  Called on open and after a set
            load, so the page can never show a value the engine is not using. */
        void refreshFromDucker()
        {
            seeding = true;
            onBtn.setToggleState (ducker.isEnabled(), juce::dontSendNotification);

            for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
            {
                auto& col = cols[(size_t) b];
                col.on     .setToggleState (ducker.isBandEnabled (b), juce::dontSendNotification);
                col.q      .slider.setValue (ducker.getQ           (b), juce::dontSendNotification);
                col.thresh .slider.setValue (ducker.getThresholdDb (b), juce::dontSendNotification);
                col.ratio  .slider.setValue (ducker.getRatio       (b), juce::dontSendNotification);
                col.attack .slider.setValue (ducker.getAttackMs    (b), juce::dontSendNotification);
                col.release.slider.setValue (ducker.getReleaseMs   (b), juce::dontSendNotification);
            }
            seeding = false;

            refreshReadouts();
            editor.refresh();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xFF888888));
            g.setFont (juce::FontOptions (11.0f));
            for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
                g.drawText (freqText (b), cols[(size_t) b].freqArea,
                            juce::Justification::centredLeft, false);

            for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
                paintKeyMeter (g, b);
        }

        //======================================================================
        // THE KEY METER — what the SOLO is delivering to this band's detector,
        // against the threshold it has to beat.
        //
        // This is the answer to "why is that point not moving".  A band that
        // never ducks has exactly two possible causes and they want opposite
        // responses: no DEPTH set (drag the point down), or no ENERGY in the
        // band (move the point to where the melody actually lives, or lower the
        // threshold).  Without a key reading those are indistinguishable, and
        // the player is left changing things at random.
        //
        // Scale is -60..0 dB, matching the threshold's own range, so the tick
        // and the bar are always measured in the same units.
        //======================================================================
        void paintKeyMeter (juce::Graphics& g, int b)
        {
            const auto area = cols[(size_t) b].keyArea;
            if (area.isEmpty()) return;

            auto bar = area.withTrimmedLeft (34.0f).withTrimmedRight (52.0f)
                           .reduced (0.0f, 4.0f);

            g.setColour (juce::Colour (0xFFAAAAAA));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText ("KEY", area.withWidth (32.0f),
                        juce::Justification::centredLeft, false);

            g.setColour (juce::Colour (0xFF0C0C0C));
            g.fillRect (bar);

            const float lvl  = ducker.getSidechainLevelDb (b);
            const float thr  = ducker.getThresholdDb (b);
            const float nLvl = juce::jlimit (0.0f, 1.0f, (lvl + 60.0f) / 60.0f);
            const float nThr = juce::jlimit (0.0f, 1.0f, (thr + 60.0f) / 60.0f);

            // OVER the threshold is the whole point, so it is the state that
            // gets the bright colour rather than the one that gets a warning.
            const bool over = lvl > thr;
            g.setColour (over ? juce::Colour (Betel::Pal::kAccent) : juce::Colour (0xFF3A5A3A));
            g.fillRect (bar.withWidth (bar.getWidth() * nLvl));

            const float tx = bar.getX() + bar.getWidth() * nThr;
            g.setColour (juce::Colour (Betel::Pal::kAccentBright));
            g.drawLine (tx, bar.getY() - 2.0f, tx, bar.getBottom() + 2.0f, 1.6f);

            g.setColour (over ? juce::Colour (Betel::Pal::kAccent) : juce::Colour (0xFF777777));
            g.drawText (lvl <= -119.0f ? juce::String ("--")
                                       : juce::String ((int) lvl) + " dB",
                        area.withTrimmedLeft (area.getWidth() - 50.0f),
                        juce::Justification::centredRight, false);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (kPad);

            auto top = r.removeFromTop (28);
            onBtn.setBounds (top.removeFromLeft (110).reduced (0, 2));
            top.removeFromLeft (10);
            hint.setBounds (top);

            r.removeFromTop (8);

            // The two control columns are reserved from the BOTTOM before the
            // editor takes the rest - the same rule the master page's footer
            // needed.  A curve one row short still reads; a control row zero
            // pixels tall does not.
            const int colH = kTitleH + kKeyH + 4 + 5 * (kCtlH + 4);
            auto ctlArea = r.removeFromBottom (colH);
            r.removeFromBottom (8);

            editor.setBounds (r);

            const int gap = 14;
            const int colW = (ctlArea.getWidth() - gap) / 2;
            for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
            {
                auto c = (b == 0) ? ctlArea.removeFromLeft (colW)
                                  : ctlArea.removeFromRight (colW);
                auto& col = cols[(size_t) b];

                auto head = c.removeFromTop (kTitleH);
                col.on.setBounds (head.removeFromRight (54).reduced (0, 2));
                head.removeFromRight (6);
                col.title.setBounds (head.removeFromLeft (66));
                col.freqArea = head.toFloat();

                col.keyArea = c.removeFromTop (kKeyH).toFloat();
                c.removeFromTop (4);

                for (Ctl* ctl : { &col.q, &col.thresh, &col.ratio,
                                  &col.attack, &col.release })
                {
                    auto row = c.removeFromTop (kCtlH);
                    ctl->name .setBounds (row.removeFromLeft (58));
                    ctl->value.setBounds (row.removeFromRight (54));
                    ctl->slider.setBounds (row.reduced (4, 3));
                    c.removeFromTop (4);
                }
            }
        }

    private:
        struct Ctl { juce::Label name, value; juce::Slider slider; };
        struct Col
        {
            juce::Label      title;
            juce::TextButton on;
            juce::Rectangle<float> freqArea, keyArea;
            Ctl q, thresh, ratio, attack, release;
        };

        void notify() { if (owner.onDuckerChanged) owner.onDuckerChanged(); }

        juce::String freqText (int b) const
        {
            const float hz = ducker.getFreq (b);
            const float depth = ducker.getDepthDb (b);
            juce::String f = hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                                           : juce::String ((int) hz) + " Hz";
            return f + "   -" + juce::String (depth, 1) + " dB";
        }

        void timerCallback() override
        {
            // Repaint only on a real change, and only the meter strips.
            bool changed = false;
            for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
            {
                const float k = ducker.getSidechainLevelDb (b);
                if (std::abs (k - lastKey[(size_t) b]) > 0.5f)
                {
                    lastKey[(size_t) b] = k;
                    changed = true;
                }
            }
            if (! changed) return;

            for (auto& c : cols)
                if (! c.keyArea.isEmpty())
                    repaint (c.keyArea.getSmallestIntegerContainer());
        }

        void refreshReadouts()
        {
            for (int b = 0; b < Betel::StyleDucker::kNumBands; ++b)
            {
                auto& col = cols[(size_t) b];
                col.q      .value.setText (juce::String (ducker.getQ (b), 2),                juce::dontSendNotification);
                col.thresh .value.setText (juce::String ((int) ducker.getThresholdDb (b)) + " dB", juce::dontSendNotification);
                col.ratio  .value.setText (juce::String (ducker.getRatio (b), 1) + ":1",     juce::dontSendNotification);
                col.attack .value.setText (juce::String (ducker.getAttackMs (b), 1) + " ms", juce::dontSendNotification);
                col.release.value.setText (juce::String ((int) ducker.getReleaseMs (b)) + " ms", juce::dontSendNotification);
            }
            repaint();
        }

        static constexpr int kPad    = 12;
        static constexpr int kTitleH = 22;
        static constexpr int kCtlH   = 22;
        static constexpr int kKeyH   = 18;

        FinisherWindow&      owner;
        Betel::StyleDucker&  ducker;
        Betel::DuckerEditor  editor;
        juce::TextButton     onBtn;
        juce::Label          hint;
        std::array<Col, (size_t) Betel::StyleDucker::kNumBands> cols;
        int  selected = 0;
        bool seeding  = false;
        std::array<float, (size_t) Betel::StyleDucker::kNumBands> lastKey { { -120.0f, -120.0f } };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuckerPage)
    };

    class Content : public juce::Component
    {
    public:
        Content (FinisherWindow& o, Betel::StyleDucker& d)
            : owner (o), ducker (d), duckPage (o, d)
        {
            // ── TAB ROW ──────────────────────────────────────────────────────
            //
            // Two pages, and switching is pure VISIBILITY: every widget for both
            // pages exists all the time and only one set is shown.  Building
            // and destroying a page on each switch would throw away the
            // ducker editor's animation state and its selection every time the
            // player looked at the master chain.
            auto styleTab = [this] (juce::TextButton& b, const char* text)
            {
                b.setButtonText (text);
                b.setClickingTogglesState (true);
                b.setRadioGroupId (0x5F2);
                b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF1E1E1E));
                b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Betel::Pal::kAccent));
                b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xFF999999));
                b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
                addAndMakeVisible (b);
            };
            styleTab (tabMaster, "MASTER");
            styleTab (tabDucker, "DUCKER");

            tabMaster.onClick = [this] { showPage (0); };
            tabDucker.onClick = [this] { showPage (1); };
            tabMaster.setToggleState (true, juce::dontSendNotification);

            addChildComponent (duckPage);

            // ── Master row ────────────────────────────────────────────────────
            onBtn.setButtonText ("ON");
            onBtn.setClickingTogglesState (true);
            // Lit ORANGE when engaged.  Without this the button looked identical
            // on and off, which made "is it even running?" unanswerable.
            onBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
            onBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Betel::Pal::kAccent));
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
            // ── AMOUNT IS RETIRED ────────────────────────────────────────────
            //
            // It was a dry/wet across the tone-and-dynamics section, and there is
            // no longer a tone section to blend against - PUSH into the ceiling
            // is the loudness control now, and it is an ordinary parameter row.
            //
            // The widgets stay DECLARED because setAmountValue() below is still
            // called by the host on a set load, and finAmount is still written to
            // every .bset.  They are simply never added to the component, so they
            // take no space and draw nothing.  (Deleting them here is what broke
            // the build the last time this window was cut down: amountLabel
            // shared a declaration line with a control that was still in use.)

            amountSlider.setRange (0.0, 100.0, 1.0);
            amountSlider.setValue (50.0, juce::dontSendNotification);
            amountSlider.onValueChange = [this]
            {
                amountValue.setText (juce::String ((int) amountSlider.getValue()) + " %",
                                     juce::dontSendNotification);
                if (owner.onAmountChanged) owner.onAmountChanged ((float) amountSlider.getValue());
            };

            amountValue.setText ("50 %", juce::dontSendNotification);


            // CHARACTER as a HORIZONTAL row of radio buttons.  SelectorGroup
            // stacks its options vertically (itemH = height / n), so six of them
            // in a 26 px row collapsed into unreadable 4 px stripes — and its
            // mouseDown ignores isEnabled(), so it stayed clickable when nothing
            // else was.  Plain TextButtons in a radio group avoid both problems.

            // ── One row per parameter, built from the DSP's own metadata ──────
            for (int i = 0; i < Betel::Finisher::kNumParams; ++i)
            {
                auto& r = rows[(size_t) i];
                r.paramId = i;
                r.stageId = Betel::Finisher::stageForParam (i);

                // ── RETIRED PARAMETERS GET NO ROW ────────────────────────────
                //
                // The enum still holds them - it has to, because finParam<i> is
                // saved by index and renumbering would corrupt every set - but
                // the DSP pins them neutral, so a control here would be a knob
                // that moves and does nothing.  paramIsLive() is the single
                // definition of what is real, and it lives beside the DSP.
                if (! Betel::Finisher::paramIsLive (i))
                    continue;

                // Only the FIRST parameter of a stage carries the stage toggle;
                // the second (EQ air, BASS weight) shares the row above's.
                r.ownsToggle = (r.stageId >= 0) && (i == firstParamOfStage (r.stageId));

                if (r.ownsToggle)
                {
                    r.toggle.setToggleState (true, juce::dontSendNotification);
                    r.toggle.setColour (juce::ToggleButton::tickColourId,
                                        juce::Colour (Betel::Pal::kAccentBright));
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

                // pGlue is the RETIRED 0-100 amount: live so the COMP stage
                // keeps its on/off tick, but with no slider of its own.  The
                // four real controls - threshold, ratio, attack, release - are
                // separate parameters and DO build rows, automatically, because
                // this loop is driven by the DSP's own metadata.
                if (! Betel::Finisher::paramHasSlider (i))
                    continue;

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

            resetBtn.setButtonText ("RESET");
            resetBtn.onClick = [this]
            {
                // RESET restores the factory sound directly now.  It used to
                // "reload whatever character is showing", which meant RESET's
                // meaning depended on a selector - and on CUSTOM it had to
                // invent one.  One button, one outcome.
                if (owner.onResetRequested) owner.onResetRequested();
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

        /** Re-seed the DUCKER page from the engine.  PUBLIC because the window
            calls it on the host's behalf — it sat below `private:` on the first
            pass and the compiler was right to refuse. */
        void refreshDuckerPage() { duckPage.refreshFromDucker(); }

        /** Switch pages.  Visibility only - both pages stay built, so the
            ducker editor keeps its selection and its animation across a trip to
            the master chain and back. */
        void showPage (int page)
        {
            currentPage = page;
            const bool master = (page == 0);

            onBtn       .setVisible (master);
            bypassBtn   .setVisible (master);
            resetBtn    .setVisible (master);
            for (auto& r : rows)
            {
                if (! Betel::Finisher::paramIsLive (r.paramId)) continue;
                if (r.ownsToggle) r.toggle.setVisible (master);
                r.name  .setVisible (master);
                r.value .setVisible (master);
                if (Betel::Finisher::paramHasSlider (r.paramId))
                    r.slider.setVisible (master);
            }

            duckPage.setVisible (! master);
            if (! master) duckPage.refreshFromDucker();

            resized();
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

            // The master chrome - the GR meter and its caption - belongs to the
            // master page.  Painting it under the DUCKER would show a meter for
            // a chain that page is not displaying.
            if (currentPage != 0) return;

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
                                                     : juce::Colour (Betel::Pal::kAccentBright));
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

            // ── TAB ROW, always, before either page gets any space ───────────
            {
                auto tabs = b.removeFromTop (26);
                const int tw = juce::jlimit (90, 160, tabs.getWidth() / 4);
                tabMaster.setBounds (tabs.removeFromLeft (tw));
                tabs.removeFromLeft (4);
                tabDucker.setBounds (tabs.removeFromLeft (tw));
                b.removeFromTop (8);
            }

            if (currentPage == 1)
            {
                duckPage.setBounds (b);
                return;
            }

            // ── FOOTER FIRST, AND THAT ORDER IS THE FIX ──────────────────────
            //
            // The two buttons used to be laid out LAST, from whatever `b` had
            // left after the master block and all eleven chain rows had taken
            // their share off the top.  removeFromBottom cannot take height
            // that is not there: at the default window size the rows finish
            // roughly a row short of the bottom, so the footer got whatever
            // slack remained - a few pixels - and the buttons drew as slivers.
            //
            // Reserving it here means the footer is guaranteed its full height
            // whatever else happens, and the chain rows absorb the shortfall
            // instead - which is the right way round, because a row that is two
            // pixels short still reads as a row while a button that is two
            // pixels tall does not read as anything.
            auto foot = b.removeFromBottom (kRowH);
            b.removeFromBottom (8);

            // ── Master block ─────────────────────────────────────────────────
            {
                auto row = b.removeFromTop (kRowH);
                onBtn.setBounds (row.removeFromLeft (60).reduced (0, 3));
                // The rest of this row was AMOUNT and is now empty on purpose -
                // the ON button keeps its own line rather than being crowded up
                // against the chain rows.

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
                // A retired parameter must take NO VERTICAL SPACE.  Leaving the
                // removeFromTop in would reserve a gap for a row that is never
                // drawn, which reads as a broken panel rather than a tidy one.
                if (! Betel::Finisher::paramIsLive (r.paramId)) continue;

                auto row = b.removeFromTop (kRowH);
                auto tog = row.removeFromLeft (26);
                if (r.ownsToggle) r.toggle.setBounds (tog.reduced (2, 6));
                row.removeFromLeft (4);
                r.name .setBounds (row.removeFromLeft (kNameW));

                if (Betel::Finisher::paramHasSlider (r.paramId))
                {
                    r.value.setBounds (row.removeFromRight (kValueW));
                    r.slider.setBounds (row.reduced (6, 5));
                }
                b.removeFromTop (2);
            }

            // ── Footer ───────────────────────────────────────────────────────
            // `foot` was reserved at the TOP of this function - see the comment
            // there.  Nothing between here and there may take from it.
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
            l.setColour (juce::Label::textColourId, juce::Colour (Betel::Pal::kAccentBright));
            l.setFont (juce::Font (13.0f, juce::Font::bold));
        }

        static void styleSlider (juce::Slider& s)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            s.setColour (juce::Slider::trackColourId,      juce::Colour (Betel::Pal::kAccent));
            s.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF0C0C0C));
            s.setColour (juce::Slider::thumbColourId,      juce::Colour (Betel::Pal::kAccentBright));
        }

        static constexpr int   kPad    = 14;
        static constexpr int   kRowH   = 30;
        static constexpr int   kNameW  = 116;
        static constexpr int   kValueW = 62;
        static constexpr float kMeterRangeDb = 12.0f;
        static constexpr float kMeterHotDb   = 6.0f;

        FinisherWindow&      owner;
        Betel::StyleDucker&  ducker;
        DuckerPage           duckPage;
        juce::TextButton     tabMaster, tabDucker;
        int                  currentPage = 0;

        juce::TextButton onBtn, bypassBtn, resetBtn;

        // amountLabel and amountValue used to share a declaration line with
        // charLabel.  Removing the character selector took the whole line, and
        // with it two members that had nothing to do with it - the AMOUNT
        // caption and its readout.  Declared on their own line now, so the next
        // deletion cannot reach them by accident.
        juce::Label      amountLabel, amountValue;
        juce::Slider     amountSlider;


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




