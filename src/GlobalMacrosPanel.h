#pragma once
//==============================================================================
// GlobalMacrosPanel.h — Settings ▸ GLOBAL SETTINGS.
//
//   ┌─ GLOBAL SETTINGS ──────────────────────────────────────────────────────┐
//   │                                                                        │
//   │            [ FUNKEY MODE ]              [ BIG DRUMS ]                  │
//   │                                                                        │
//   └────────────────────────────────────────────────────────────────────────┘
//
// THREE BUTTONS AND NOTHING ELSE.
//
// This page used to carry the controls themselves: an engage switch and seven
// family buttons for Funkey, another switch and an editor button for Big Drums,
// then five sliders for the levels.  Three features sharing one tab's width
// meant none of them had room — the sliders were too narrow to read and the
// family buttons too small to label properly.
//
// Each feature now owns a window, and the page owns the doors.  Everything that
// belongs to a feature — its engage switch, its editors, its SAVE — lives with
// that feature, so turning a macro on and hearing what it does is one gesture
// in one place instead of two controls a tab apart.
//
// The windows are created once and reopened, not rebuilt, so each keeps its
// size and position across visits.
//==============================================================================

#include <JuceHeader.h>
#include <functional>
#include <memory>

#include "InstrEditPanel.h"     // InstrEditStyle
#include "GlobalMacros.h"
#include "MacroFxWindows.h"
#include "MasterSettings.h"    // PITCH BEND RANGE lives in the master file

class GlobalMacrosPanel : public juce::Component
{
public:
    /** The host owns the macro state: set the GlobalMacros flag, re-push what
        the macro governs, then call refreshFromState(). */
    std::function<void(bool /*on*/)> onFunkeyToggled;
    std::function<void(bool /*on*/)> onBigDrumsToggled;

    /** A knob moved inside one of the macro editors. */
    std::function<void()> onMacroFxEdited;

    /** PITCH BEND RANGE moved, in semitones (1..12).  The host pushes it to
        the solo channels; this panel only owns the control. */
    std::function<void(int /*semitones*/)> onPitchBendRangeChanged;

    /** LOW VELOCITY RESPONSE moved, 0..100.  Same deal: the host pushes it to
        the solo channels, this panel only owns the control. */
    std::function<void(int /*amount*/)> onLowVelBoostChanged;

    // STYLE LEVELS used to be a third door here.  It moved out: the levels
    // belong to the loaded style, not to the installation, so they had no
    // business on a page called GLOBAL SETTINGS.  They live in the SET EDITOR
    // tab now, beside the rest of the set.

    GlobalMacrosPanel()
    {
        setupTrigger (funkeyBtn,  "FUNKEY MODE",  [this] { openFunkey(); });
        setupTrigger (drumsBtn,   "BIG DRUMS",    [this] { openBigDrums(); });

        // ── PITCH BEND RANGE ─────────────────────────────────────────────
        // A real setting rather than a door, so it sits ON the page instead of
        // behind a button.  Integer steps 1..12: a bend range is a whole number
        // of semitones on every instrument that has ever had one, and a
        // continuous slider would only invite 3.7.
        pbLabel.setText ("PITCH BEND RANGE", juce::dontSendNotification);
        pbLabel.setJustificationType (juce::Justification::centred);
        pbLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFC2C2C2));
        addAndMakeVisible (pbLabel);

        pbSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        pbSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        styleSettingSlider (pbSlider);
        pbSlider.setRange (Betel::MasterSettings::kMinPitchBendRange,
                           Betel::MasterSettings::kMaxPitchBendRange, 1.0);
        pbSlider.onValueChange = [this]
        {
            const int semis = (int) pbSlider.getValue();
            pbValue.setText (juce::String ("+/- ") + juce::String (semis)
                             + (semis == 1 ? " SEMITONE" : " SEMITONES"),
                             juce::dontSendNotification);
            Betel::MasterSettings::get().setPitchBendRange (semis);
            if (onPitchBendRangeChanged) onPitchBendRangeChanged (semis);
        };
        addAndMakeVisible (pbSlider);

        pbValue.setJustificationType (juce::Justification::centred);
        pbValue.setColour (juce::Label::textColourId, kSettingYellow);
        addAndMakeVisible (pbValue);

        // ── LOW VELOCITY RESPONSE ────────────────────────────────────────
        // A keyboard correction, not an instrument voicing - which is why it
        // belongs on this page beside the bend range and not in the sound
        // editor.  0 is off, and integer steps because a percentage of lift is
        // a figure you nudge until it feels right, not one you calculate.
        lvLabel.setText ("LOW VELOCITY RESPONSE", juce::dontSendNotification);
        lvLabel.setJustificationType (juce::Justification::centred);
        lvLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFC2C2C2));
        addAndMakeVisible (lvLabel);

        lvSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        lvSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        styleSettingSlider (lvSlider);
        lvSlider.setRange (Betel::MasterSettings::kMinLowVelBoost,
                           Betel::MasterSettings::kMaxLowVelBoost, 1.0);
        lvSlider.onValueChange = [this]
        {
            const int amount = (int) lvSlider.getValue();
            lvValue.setText (lowVelText (amount), juce::dontSendNotification);
            // The host writes MasterSettings inside setSoloLowVelBoost, so this
            // panel deliberately does NOT - two writers of one setting is how
            // the value and the sound end up disagreeing.
            if (onLowVelBoostChanged) onLowVelBoostChanged (amount);
        };
        addAndMakeVisible (lvSlider);

        lvValue.setJustificationType (juce::Justification::centred);
        lvValue.setColour (juce::Label::textColourId, kSettingYellow);
        addAndMakeVisible (lvValue);

        refreshFromState();
    }

    ~GlobalMacrosPanel() override
    {
        funkeyWin.reset();
        familyWin.reset();
        drumsWin .reset();
    }

    /** Relight the two macro triggers from GlobalMacros, and any open window
        with them.  Called after a toggle from either surface and after a set
        restore, so nothing can disagree about whether a macro is engaged. */
    void refreshFromState()
    {
        const auto& gm = Betel::GlobalMacros::get();
        setLit (funkeyBtn, gm.isFunkeyOn());
        setLit (drumsBtn,  gm.isBigDrumsOn());

        if (funkeyWin != nullptr) funkeyWin->refresh();
        if (drumsWin  != nullptr) drumsWin ->refresh();

        // dontSendNotification: this is a REFRESH, and letting it fire onChange
        // would write the value straight back to MasterSettings on every set
        // restore for no reason.
        const int semis = Betel::MasterSettings::get().getPitchBendRange();
        pbSlider.setValue ((double) semis, juce::dontSendNotification);
        pbValue.setText (juce::String ("+/- ") + juce::String (semis)
                         + (semis == 1 ? " SEMITONE" : " SEMITONES"),
                         juce::dontSendNotification);

        const int lv = Betel::MasterSettings::get().getLowVelBoost();
        lvSlider.setValue ((double) lv, juce::dontSendNotification);
        lvValue.setText (lowVelText (lv), juce::dontSendNotification);

        repaint();
    }

    void paint (juce::Graphics& g) override
    { InstrEditStyle::paintComponentFrame (g, getLocalBounds(), "GLOBAL SETTINGS"); }

    void resized() override
    {
        auto inner = InstrEditStyle::componentFrameContent (getLocalBounds());

        // One row of three, centred and generously sized: these are doors, and a
        // door that is hard to hit is the whole complaint about the old page.
        // TWO doors now, not three — STYLE LEVELS moved to the SET EDITOR tab.
        // Widened to match, so the page does not look like it lost something.
        // Doors on top, the setting underneath — a control you READ belongs
        // below the two you PRESS, not competing with them for the centre.
        auto lower = inner.removeFromBottom (juce::jmin (86, inner.getHeight() / 3));

        const int gap = 24;
        const int bw  = juce::jmin (280, (inner.getWidth() - gap) / 2);
        const int bh  = juce::jmin (90,  inner.getHeight() - 20);
        const int x0  = inner.getCentreX() - (2 * bw + gap) / 2;
        const int y   = inner.getCentreY() - bh / 2;

        funkeyBtn.setBounds (x0,            y, bw, bh);
        drumsBtn .setBounds (x0 + bw + gap, y, bw, bh);

        // TWO settings side by side now, not one across the middle.  Stacking
        // them would push the second off the bottom of the frame; splitting the
        // row gives each the same three-part shape (label / slider / value) the
        // bend range already had.
        const int rowW = juce::jmin (2 * bw + gap, lower.getWidth());
        auto row = lower.withSizeKeepingCentre (rowW, juce::jmin (74, lower.getHeight()));

        auto left  = row.removeFromLeft ((row.getWidth() - gap) / 2);
        row.removeFromLeft (gap);
        auto right = row;

        auto layOne = [] (juce::Rectangle<int> r, juce::Label& lab,
                          juce::Slider& sl, juce::Label& val)
        {
            lab.setBounds (r.removeFromTop (20));
            val.setBounds (r.removeFromBottom (18));
            sl .setBounds (r.reduced (4, 2));
        };

        layOne (left,  pbLabel, pbSlider, pbValue);
        layOne (right, lvLabel, lvSlider, lvValue);
    }

private:
    void setupTrigger (juce::TextButton& b, const juce::String& text,
                       std::function<void()> onClick)
    {
        b.setButtonText (text);
        b.onClick = std::move (onClick);
        addAndMakeVisible (b);
    }

    /** A trigger for an ENGAGED macro is lit, so the page still answers "is this
        on?" at a glance without carrying the switch itself. */
    static void setLit (juce::TextButton& b, bool on)
    {
        b.setColour (juce::TextButton::buttonColourId,
                     on ? juce::Colour (0xFFCC6600) : juce::Colour (0xFF2A2A2A));

        // UNCHANGED, and deliberately so: these two ARE the reference the rest
        // of the left panel was matched to.  A plain TextButton's caption goes
        // through the LookAndFeel and lands at #C2C2C2, and that measured value
        // is what InstrEditStyle::kPanelLabel now carries everywhere else.
        // Black when lit stays — it is the read on amber, not a colour scheme.
        b.setColour (juce::TextButton::textColourOffId,
                     on ? juce::Colours::black : juce::Colours::white);
        b.setColour (juce::TextButton::textColourOnId,
                     on ? juce::Colours::black : juce::Colours::white);
    }

    void openFunkey()
    {
        if (funkeyWin == nullptr)
        {
            funkeyWin = std::make_unique<Betel::FunkeyModeWindow>();
            funkeyWin->onToggled = [this] (bool on)
            {
                if (onFunkeyToggled) onFunkeyToggled (on);
                refreshFromState();
            };
            funkeyWin->onFamilyPicked = [this] (int slot) { openFamily (slot); };
        }
        funkeyWin->showCentredOver (getTopLevelComponent());
    }

    void openFamily (int slot)
    {
        if (familyWin == nullptr)
        {
            familyWin = std::make_unique<Betel::FamilyFxWindow>();
            familyWin->onChanged = [this] { if (onMacroFxEdited) onMacroFxEdited(); };
        }
        familyWin->showForFamily (slot, getTopLevelComponent());
    }

    void openBigDrums()
    {
        if (drumsWin == nullptr)
        {
            drumsWin = std::make_unique<Betel::BigDrumsFxWindow>();
            drumsWin->onChanged = [this] { if (onMacroFxEdited) onMacroFxEdited(); };
            drumsWin->onToggled = [this] (bool on)
            {
                if (onBigDrumsToggled) onBigDrumsToggled (on);
                refreshFromState();
            };
        }
        drumsWin->showCentredOver (getTopLevelComponent());
    }

    juce::TextButton funkeyBtn, drumsBtn;
    juce::Label      pbLabel, pbValue;
    juce::Slider     pbSlider;
    juce::Label      lvLabel, lvValue;
    juce::Slider     lvSlider;

    //==========================================================================
    // ONE YELLOW FOR BOTH SETTINGS.
    //
    // These sliders carried JUCE's stock LookAndFeel colours, which are blue -
    // the one hue that appears nowhere else on this surface, so the two real
    // controls on the page looked like they had been borrowed from another
    // program.  Yellow is what the rest of Grex uses for a value you read.
    //
    // FOUR colour ids, not one: JUCE draws a linear slider from thumb, track and
    // background separately, and setting only the thumb leaves a blue track
    // behind it.  textBoxOutline is set because a NoTextBox slider still paints
    // that outline in some LookAndFeels.
    //==========================================================================
    inline static const juce::Colour kSettingYellow { juce::Colour (0xFFE8C33A) };

    static void styleSettingSlider (juce::Slider& sl)
    {
        sl.setColour (juce::Slider::thumbColourId,           kSettingYellow);
        sl.setColour (juce::Slider::trackColourId,           kSettingYellow);
        sl.setColour (juce::Slider::backgroundColourId,      juce::Colour (0xFF2A2A2A));
        sl.setColour (juce::Slider::textBoxOutlineColourId,  juce::Colours::transparentBlack);
    }

    /** "OFF" reads better than "0 %" for a setting whose whole point is that it
        does nothing until you move it. */
    static juce::String lowVelText (int amount)
    {
        return amount <= 0 ? juce::String ("OFF")
                           : juce::String ("+") + juce::String (amount) + " %";
    }

    std::unique_ptr<Betel::FunkeyModeWindow>  funkeyWin;
    std::unique_ptr<Betel::FamilyFxWindow>    familyWin;
    std::unique_ptr<Betel::BigDrumsFxWindow>  drumsWin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalMacrosPanel)
};



