

#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
//==============================================================================
// GlobalMacrosPanel.h — Settings ▸ GLOBAL SETTINGS.
//
//   ┌─ GLOBAL SETTINGS ──────────────────────────────────────────────────────┐
//   │ [FUNKEY] [RESET GAIN]  .   .   .        │   <- row 0 of a 5 x 3 grid   │
//   │  .    .    .    .    .                  │   <- row 1, free              │
//   │        PITCH BEND        LOW VELOCITY   │   <- row 2                    │
//   └────────────────────────────────────────────────────────────────────────┘
//
//  THE BUTTONS SIT ON A 5 x 3 GRID, FILLED FROM THE TOP LEFT.
//
//  They used to be two big centred doors, sized on the argument that a door
//  which is hard to hit is the whole complaint about the old page.  That was
//  right when there were two of them and nothing else was ever going to arrive.
//  Three is already past what centring reads well, and the page is going to
//  keep collecting them.
//
//  So the geometry is now a GRID rather than a row: five columns, three rows,
//  and a button takes one cell.  Adding a fourth is one more cell rather than a
//  re-centre of everything, the free cells are visibly free, and nothing has to
//  be re-measured when the count changes.
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
#include "StyleLevels.h"     // the GLOBAL style-bus boost lives here
#include "MacroFxWindows.h"
#include "MasterSettings.h"    // PITCH BEND RANGE lives in the master file

class GlobalMacrosPanel : public juce::Component
{
public:
    /** The host owns the macro state: set the GlobalMacros flag, re-push what
        the macro governs, then call refreshFromState(). */
    std::function<void(bool /*on*/)> onFunkeyToggled;

    /** A knob moved inside one of the macro editors. */
    std::function<void()> onMacroFxEdited;


    /** PITCH BEND RANGE moved, in semitones (1..12).  The host pushes it to
        the solo channels; this panel only owns the control. */
    std::function<void(int /*semitones*/)> onPitchBendRangeChanged;

    /** LOW VELOCITY RESPONSE moved, 0..100.  Same deal: the host pushes it to
        the solo channels, this panel only owns the control. */
    std::function<void(int /*amount*/)> onLowVelBoostChanged;

    /** STYLE BOOST moved, in dB.  The host stores, saves and pushes it. */
    std::function<void(float /*db*/)> onStyleBoostChanged;

    /** RESET SOLO BASE GAIN was confirmed.  The panel owns the button and the
        confirmation; the host owns the files.  Fires only on YES - a cancelled
        dialog is silent. */
    std::function<void()> onResetSoloBaseGain;

    // STYLE LEVELS used to be a third door here.  It moved out: the levels
    // belong to the loaded style, not to the installation, so they had no
    // business on a page called GLOBAL SETTINGS.  They live in the SET EDITOR
    // tab now, beside the rest of the set.

    GlobalMacrosPanel()
    {
        setupTrigger (funkeyBtn,  "FUNKEY MODE",  [this] { openFunkey(); });

        // AN ACTION, NOT A DOOR AND NOT A TOGGLE.  It never lights, because
        // there is no state for it to report - it does its work and it is done.
        // setLit(false) once here is what gives it the same unlit colours as
        // the other two rather than the LookAndFeel's default button.
        setupTrigger (resetGainBtn, "RESET SOLO BASE GAIN",
                      [this] { confirmResetSoloBaseGain(); });
        setLit (resetGainBtn, false);

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

        // ── STYLE BOOST ──────────────────────────────────────────────────────
        //
        // ONE NUMBER FOR THE WHOLE INSTALLATION, and that is why it is on this
        // page rather than in the SET EDITOR: it is a property of the rig, like
        // the bend range beside it, not of any style.  It writes grex_boost.xml
        // on every move - see StyleLevels.h - so there is no SAVE to forget.
        //
        // IN dB, 0.0 to 24.0, half-dB steps.  The old 0..100 slider is what let
        // a +19.2 dB boost sit unnoticed for months reading "80", and one unit
        // everywhere is the whole lesson of that.
        sbLabel.setText ("STYLE BOOST", juce::dontSendNotification);
        sbLabel.setJustificationType (juce::Justification::centred);
        sbLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFC2C2C2));
        addAndMakeVisible (sbLabel);

        sbSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        sbSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        styleSettingSlider (sbSlider);
        sbSlider.setRange (0.0, (double) Betel::StyleLevels::kBoostMaxDb, 0.5);
        sbSlider.onValueChange = [this]
        {
            const float db = (float) sbSlider.getValue();
            sbValue.setText (boostText (db), juce::dontSendNotification);

            // THE HOST WRITES IT, not this panel - exactly as LOW VELOCITY
            // RESPONSE does above.  The processor stores the value, saves the
            // file AND pushes it to the style bus; a panel that only stored it
            // is why the slider had no effect at all.
            if (onStyleBoostChanged) onStyleBoostChanged (db);
        };
        addAndMakeVisible (sbSlider);

        sbValue.setJustificationType (juce::Justification::centred);
        sbValue.setColour (juce::Label::textColourId, kSettingYellow);
        addAndMakeVisible (sbValue);

        refreshFromState();
    }

    ~GlobalMacrosPanel() override
    {
        funkeyWin.reset();
        familyWin.reset();
    }

    /** Relight the macro trigger from GlobalMacros, and any open window
        with them.  Called after a toggle from either surface and after a set
        restore, so nothing can disagree about whether a macro is engaged. */
    void refreshFromState()
    {
        const auto& gm = Betel::GlobalMacros::get();
        setLit (funkeyBtn, gm.isFunkeyOn());

        if (funkeyWin != nullptr) funkeyWin->refresh();

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

        const float sb = Betel::StyleLevels::get().boostDb();
        sbSlider.setValue ((double) sb, juce::dontSendNotification);
        sbValue.setText (boostText (sb), juce::dontSendNotification);

        repaint();
    }

    void paint (juce::Graphics& g) override
    { InstrEditStyle::paintComponentFrame (g, getLocalBounds(), "GLOBAL SETTINGS"); }

    void resized() override
    {
        auto inner = InstrEditStyle::componentFrameContent (getLocalBounds());

        // ── THE 5 x 3 GRID ───────────────────────────────────────────────────
        //
        // Derived from the frame rather than fixed, so the cells stay square-ish
        // whatever the tab is resized to and a button never spills into its
        // neighbour.  The inset inside each cell is the gutter: cells touch,
        // buttons do not.
        const int cols = 5, rows = 3;
        const int cellW = inner.getWidth()  / cols;
        const int cellH = inner.getHeight() / rows;

        auto cell = [&] (int col, int row)
        {
            return juce::Rectangle<int> (inner.getX() + col * cellW,
                                         inner.getY() + row * cellH,
                                         cellW, cellH).reduced (7, 10);
        };

        // ROW 0, LEFT TO RIGHT, NO GAP.  The two buttons sit in adjacent cells
        // (0,0) and (1,0); cells (2,0) to (4,0) and the whole of row 1 are
        // deliberately empty - that is the space this layout was for.
        //
        // THE GAP AT (1,0) IS GONE ON PURPOSE.  It was left there because (1,0)
        // is where BIG DRUMS used to be, and dropping a destructive library-wide
        // action into the cell a harmless macro button had just vacated would
        // have put "reset every instrument to 0 dB" under old muscle memory.
        // Big Drums has been gone long enough that the memory it was protecting
        // no longer exists, and the confirmation dialog still stands in front of
        // the action - so the two buttons read as one row of a table now rather
        // than as two strays with a hole between them.
        funkeyBtn   .setBounds (cell (0, 0));
        resetGainBtn.setBounds (cell (1, 0));

        // ── ROW 2: the two settings ──────────────────────────────────────────
        //
        // They keep the bottom of the frame, which is where they already were —
        // a control you READ belongs below the ones you PRESS.  Held to the
        // grid's bottom row now instead of a removeFromBottom, so the buttons
        // and the sliders answer to the same geometry.
        auto lower = juce::Rectangle<int> (inner.getX(), inner.getY() + 2 * cellH,
                                           inner.getWidth(), cellH);

        const int gap = 24;

        // TWO settings side by side, not one across the middle.  Stacking them
        // would push the second off the bottom of the frame; splitting the row
        // gives each the same three-part shape (label / slider / value) the
        // bend range already had.
        const int rowW = juce::jmin (4 * cellW, lower.getWidth());
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

        // ── ROW 1: STYLE BOOST, alone and centred ────────────────────────────
        //
        // Its own row rather than a third of the bottom one: three settings
        // across a row that was already tight for two would leave each too
        // narrow to set by half a dB, and row 1 was empty anyway.
        auto mid = juce::Rectangle<int> (inner.getX(), inner.getY() + cellH,
                                         inner.getWidth(), cellH);
        auto sbRow = mid.withSizeKeepingCentre (juce::jmin (2 * cellW, mid.getWidth()),
                                                juce::jmin (74, mid.getHeight()));
        layOne (sbRow, sbLabel, sbSlider, sbValue);
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
                     on ? juce::Colour (Betel::Pal::kAccent) : juce::Colour (0xFF2A2A2A));

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

    //==========================================================================
    //  RESET SOLO BASE GAIN — CONFIRM FIRST.
    //
    //  It rewrites every .ins in the library and there is no undo, so it asks.
    //  The panel owns the question and the host owns the answer's consequences:
    //  nothing happens here on YES except the callback.
    //
    //  SafePointer rather than a bare `this`: the dialog is async and the user
    //  can change tab, which takes the panel with it.  showAsync would then call
    //  into freed memory - and only sometimes, which is the worst kind.
    //==========================================================================
    void confirmResetSoloBaseGain()
    {
        juce::Component::SafePointer<GlobalMacrosPanel> safe (this);

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions::makeOptionsYesNo (
                juce::MessageBoxIconType::WarningIcon,
                "Reset solo base gain",
                "Set the BASE GAIN of every solo instrument to 0 dB and save "
                "every .ins file?\n\nThis cannot be undone."),
            [safe] (int result)
            {
                if (result != 1)          return;      // 1 = Yes
                if (safe == nullptr)      return;
                if (safe->onResetSoloBaseGain) safe->onResetSoloBaseGain();
            });
    }

    void openFunkey()
    {
        if (funkeyWin == nullptr)
        {
            funkeyWin = std::make_unique<Betel::FunkeyModeWindow>();

            // NO onToggled WIRING ANY MORE.  The window's ON/OFF button is gone
            // and so is the callback: FUNKEY MODE has one switch, the FUNKEY
            // button in the play-control row.  This window is now purely a way
            // in to the six family editors.
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

    /** "+19.2 dB", and the factory value named so it is recognisable. */
    static juce::String boostText (float db)
    {
        juce::String s = "+" + juce::String (db, 1) + " dB";
        if (std::abs (db - Betel::StyleLevels::kForcedBoostDb) < 0.05f) s += "   (factory)";
        return s;
    }

    juce::Label  sbLabel, sbValue;
    juce::Slider sbSlider;

    juce::TextButton funkeyBtn, resetGainBtn;
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
    inline static const juce::Colour kSettingYellow { juce::Colour (Betel::Pal::kNav) };

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalMacrosPanel)
};




