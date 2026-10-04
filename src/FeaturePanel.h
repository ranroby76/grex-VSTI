
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme

#include <JuceHeader.h>
#include "Harmonizer.h"

namespace Betel
{

//==============================================================================
// StatusLED  -  the red/green dot, lifted out of FolderLocatorLED.
//
// Same circle, same two colours, same offset white highlight, so a third and
// fourth dot on the surface look like they belong rather than like a new
// invention. Lifted rather than copied because there were already TWO of these
// (folder locator, registration) and a third copy is where a set of "identical"
// widgets starts drifting apart.
//==============================================================================
class StatusLED : public juce::Component
{
public:
    StatusLED() { setInterceptsMouseClicks (false, false); }

    void setOn (bool o)
    {
        if (on == o) return;
        on = o;
        repaint();
    }
    bool isOn() const noexcept { return on; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        const auto  c = b.withSizeKeepingCentre (d, d);

        g.setColour (on ? juce::Colour (0xFF00CC00) : juce::Colour (0xFFCC0000));
        g.fillEllipse (c);

        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.fillEllipse (c.reduced (d * 0.25f).translated (-d * 0.08f, -d * 0.08f));
    }

private:
    bool on = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusLED)
};

//==============================================================================
// PowerButton  -  the IEC power symbol from every remote control.
//
// A juce::Path rather than an image: it scales with the window like every other
// control here and stays crisp at any size, and an image would need a second
// asset for the on state.
//==============================================================================
class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button ("power")
    {
        setClickingTogglesState (true);
    }

    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        const auto  c = b.withSizeKeepingCentre (d, d);

        const bool on = getToggleState();

        g.setColour (isDown ? juce::Colour (0xFF303030)
                            : isOver ? juce::Colour (0xFF262626)
                                     : juce::Colour (0xFF1C1C1C));
        g.fillEllipse (c);

        const auto glyph = on ? juce::Colour (0xFF00CC00)
                              : juce::Colours::white.withAlpha (0.35f);
        g.setColour (glyph);
        g.drawEllipse (c.reduced (0.75f), 1.2f);

        // The broken ring: an arc with a gap at the TOP, and a bar through it.
        const auto  r  = c.reduced (d * 0.26f);
        const float cx = r.getCentreX(), cy = r.getCentreY();
        const float rad = r.getWidth() * 0.5f;

        juce::Path ring;
        ring.addCentredArc (cx, cy, rad, rad, 0.0f,
                            juce::degreesToRadians (35.0f),
                            juce::degreesToRadians (325.0f),
                            true);

        juce::Path bar;
        bar.startNewSubPath (cx, cy - rad * 1.15f);
        bar.lineTo         (cx, cy - rad * 0.10f);

        const juce::PathStrokeType stroke (juce::jmax (1.4f, d * 0.085f),
                                           juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded);
        g.strokePath (ring, stroke);
        g.strokePath (bar,  stroke);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerButton)
};

//==============================================================================
// FeaturePanel  -  HARMONY / MULTI SPLIT / MANUAL BASS.
//==============================================================================
//
// THE TABLE THAT IS MISSING THREE OF ITS BORDERS. Rob's design: three stacked
// selectors on the left, and the ONLY long border is the vertical rule down the
// right edge of that column. No left border, no top, no bottom. The internal
// separators between cells start from nothing on the left and die into the
// rule. It reads as a table without being boxed in.
//
// TWO STATES PER FEATURE, TWO SIGNALS. A selector being highlighted means "this
// is the one you are editing". The LED at its top-left means "this one is
// running" - red off, green on. They are independent on purpose: harmony can be
// playing while multi split is on screen, and one signal could not say both.
//
// HARMONY IS FIRST AND IS THE DEFAULT SELECTION, because it is the one that
// gets used. The selection is UI state and is deliberately NOT persisted - it
// always opens on Harmony rather than wherever somebody last poked.
//==============================================================================
class FeaturePanel : public juce::Component
{
public:
    enum Feature { kHarmony = 0, kMultiSplit = 1, kBass = 2, kNumFeatures = 3 };

    // ── Harmony -> processor ────────────────────────────────────────────────
    std::function<void (bool)>                  onHarmonyEnabled;
    std::function<void (Harmonizer::Type)>      onHarmonyType;
    std::function<void (int)>                   onHarmonyLevel;

    // ── Multi split -> processor ────────────────────────────────────────────
    std::function<void (bool)>                  onMultiSplitEnabled;
    std::function<void (int)>                   onBassSplitPoint;
    std::function<void (int)>                   onBassZoneSlot;

    // ── Bass -> processor.  TWO INDEPENDENT SWITCHES, as on every arranger
    //    that ships them: Korg lists Bass Inversion and Manual Bass side by
    //    side in its style controls, neither gated behind the other.
    std::function<void (bool)>                  onBassInversionEnabled;
    std::function<void (bool)>                  onManualBassEnabled;

    FeaturePanel()
    {
        for (int i = 0; i < kNumFeatures; ++i)
        {
            addAndMakeVisible (leds[i]);
            leds[i].setOn (false);
        }

        addAndMakeVisible (harmonyPower);
        harmonyPower.onClick = [this]
        {
            if (onHarmonyEnabled) onHarmonyEnabled (harmonyPower.getToggleState());
            leds[kHarmony].setOn (harmonyPower.getToggleState());
        };

        btnType.setButtonText (Harmonizer::typeName (Harmonizer::Type::Duet));
        btnType.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1C1C1C));
        btnType.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        btnType.onClick = [this]
        {
            // CYCLES rather than opening a list. Five items is short enough that
            // tapping through them is faster than a menu, and it works the same
            // with a finger as with a mouse.
            typeIdx = (typeIdx + 1) % Harmonizer::kNumTypes;
            btnType.setButtonText (Harmonizer::typeName ((Harmonizer::Type) typeIdx));
            if (onHarmonyType) onHarmonyType ((Harmonizer::Type) typeIdx);
        };
        addAndMakeVisible (btnType);

        sLevel.setSliderStyle (juce::Slider::LinearHorizontal);
        sLevel.setTextBoxStyle (juce::Slider::TextBoxRight, false, 34, 16);
        sLevel.setRange (0.0, 100.0, 1.0);
        sLevel.setValue (82.0, juce::dontSendNotification);
        sLevel.setColour (juce::Slider::trackColourId,        juce::Colour (Betel::Pal::kAccentDeep));
        sLevel.setColour (juce::Slider::backgroundColourId,   juce::Colour (0xFF141414));
        sLevel.setColour (juce::Slider::thumbColourId,        juce::Colours::white);
        sLevel.setColour (juce::Slider::textBoxTextColourId,  juce::Colours::white);
        sLevel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        // Same latch, same reason - setHarmonyState is mirrored from the same
        // 30 Hz tick and writes this slider too.  It has not been reported yet
        // only because the harmony level has no clamp pushing back against the
        // drag; the overwrite is identical.
        sLevel.onDragStart = [this] { levelDragging = true;  };
        sLevel.onDragEnd   = [this] { levelDragging = false; };
        sLevel.onValueChange = [this]
        {
            if (onHarmonyLevel) onHarmonyLevel ((int) sLevel.getValue());
        };
        addAndMakeVisible (sLevel);

        // ── MULTI SPLIT ─────────────────────────────────────────────────────
        addAndMakeVisible (splitPower);
        splitPower.onClick = [this]
        {
            if (onMultiSplitEnabled) onMultiSplitEnabled (splitPower.getToggleState());
            leds[kMultiSplit].setOn (splitPower.getToggleState());
        };

        sBassSplit.setSliderStyle (juce::Slider::LinearHorizontal);
        sBassSplit.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 16);
        sBassSplit.setRange (0.0, 127.0, 1.0);
        sBassSplit.setValue (48.0, juce::dontSendNotification);
        sBassSplit.setColour (juce::Slider::trackColourId,      Harmonizer::bassZone());
        sBassSplit.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF141414));
        sBassSplit.setColour (juce::Slider::thumbColourId,      juce::Colours::white);
        sBassSplit.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        sBassSplit.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        // NUMBERS, NOT NOTE NAMES. Grex speaks MIDI note numbers everywhere
        // else a split is set, and one control answering in "C3" while the
        // knob beside it answers in 48 is worse than either convention on its
        // own.
        sBassSplit.onValueChange = [this]
        {
            if (onBassSplitPoint) onBassSplitPoint ((int) sBassSplit.getValue());
            repaint();      // the BASS caption names this boundary
        };
        //======================================================================
        //  THE DRAG LATCH - WHY THIS SLIDER FOUGHT THE MOUSE.
        //
        //  The editor's 30 Hz mirror calls setMultiSplitState() every tick and
        //  writes processor.getBassSplitPoint() back into this slider.  While
        //  the user is DRAGGING, that is the model overwriting the widget the
        //  user currently has hold of - thirty times a second.
        //
        //  It bites hardest near the main split, because setBassSplitPoint
        //  clamps to splitPoint - 1: drag the thumb up to the boundary and the
        //  processor answers with a SMALLER number, which the next tick stuffs
        //  back under the cursor.  The thumb jumps backwards while the mouse
        //  moves forwards, which is the "fighting on drag".
        //
        //  So the mirror is suspended for the duration of the gesture.  The
        //  widget owns the value while the mouse is down; the model owns it
        //  every other moment, including the first tick after the button is
        //  released, which is when any clamp finally shows on screen.
        //
        //  A latch rather than isMouseButtonDown(): a drag that leaves the
        //  slider's bounds keeps mouse capture but does not always keep that
        //  predicate, and a mirror that resumes mid-gesture is the whole bug.
        //======================================================================
        sBassSplit.onDragStart = [this] { bassSplitDragging = true;  };
        sBassSplit.onDragEnd   = [this] { bassSplitDragging = false; };
        addAndMakeVisible (sBassSplit);

        // WHICH SLOT THE BASS ZONE PLAYS. Cycles rather than opening a list -
        // seven choices, and the same reasoning as the harmony TYPE button.
        //
        // SLOT 8 IS SKIPPED, not merely refused by the processor: offering a
        // choice that is silently corrected is worse than not offering it.
        btnBassSlot.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1C1C1C));
        btnBassSlot.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        btnBassSlot.onClick = [this]
        {
            bassSlotIdx = (bassSlotIdx + 1) % 7;      // 0..6, never 7
            btnBassSlot.setButtonText ("BASS -> SOLO " + juce::String (bassSlotIdx + 1));
            if (onBassZoneSlot) onBassZoneSlot (bassSlotIdx);
            repaint();      // the BASS caption names this slot
        };
        btnBassSlot.setButtonText ("BASS -> SOLO " + juce::String (bassSlotIdx + 1));
        addAndMakeVisible (btnBassSlot);

        // ── BASS ────────────────────────────────────────────────────────────
        //
        // The POWER button is BASS INVERSION, because that is the one a single
        // keyboard player can actually use: the style keeps its bass line and
        // simply follows the lowest note held, so nothing ever drops out.
        // MANUAL BASS sits beside it as the second switch - it is the pedal
        // board feature, and on one keyboard it is a technique, not a setting.
        addAndMakeVisible (bassPower);
        bassPower.onClick = [this]
        {
            if (onBassInversionEnabled) onBassInversionEnabled (bassPower.getToggleState());
            refreshBassLed();
        };

        btnManualBass.setButtonText ("MANUAL BASS");
        btnManualBass.setClickingTogglesState (true);
        btnManualBass.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF1C1C1C));
        btnManualBass.setColour (juce::TextButton::buttonOnColourId, Harmonizer::bassZone());
        btnManualBass.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        btnManualBass.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        btnManualBass.onClick = [this]
        {
            if (onManualBassEnabled) onManualBassEnabled (btnManualBass.getToggleState());
            refreshBassLed();
        };
        addAndMakeVisible (btnManualBass);

        showFeature (kHarmony);
    }

    /** Engine -> UI for the bass tab.  Never calls back. */
    void setBassState (bool inversionOn, bool manualOn)
    {
        if (bassPower.getToggleState() != inversionOn)
            bassPower.setToggleState (inversionOn, juce::dontSendNotification);

        if (btnManualBass.getToggleState() != manualOn)
            btnManualBass.setToggleState (manualOn, juce::dontSendNotification);

        refreshBassLed();
    }

    /** Engine -> UI for multi split.  Never calls back, like setHarmonyState. */
    void setMultiSplitState (bool on, int bassSplit, int bassSlot)
    {
        if (splitPower.getToggleState() != on)
            splitPower.setToggleState (on, juce::dontSendNotification);

        leds[kMultiSplit].setOn (on);

        // NOT while the user has hold of it - see the drag latch above.
        if (! bassSplitDragging && (int) sBassSplit.getValue() != bassSplit)
            sBassSplit.setValue ((double) bassSplit, juce::dontSendNotification);

        const int idx = juce::jlimit (0, 6, bassSlot);
        if (idx != bassSlotIdx)
        {
            bassSlotIdx = idx;
            btnBassSlot.setButtonText ("BASS -> SOLO " + juce::String (idx + 1));
        }

        // The BASS tab's caption quotes BOTH of these, and this is the path a
        // SET LOAD comes in on - without the repaint the caption keeps naming
        // the previous set's boundary and slot.
        repaint();
    }

    /** Engine -> UI.  Called from the editor's mirror; never fires callbacks
        back at the engine, which would be a loop. */
    void setHarmonyState (bool on, Harmonizer::Type t, int level)
    {
        if (harmonyPower.getToggleState() != on)
            harmonyPower.setToggleState (on, juce::dontSendNotification);

        leds[kHarmony].setOn (on);

        if ((int) t != typeIdx)
        {
            typeIdx = (int) t;
            btnType.setButtonText (Harmonizer::typeName (t));
        }

        // NOT while the user has hold of it - see the drag latch above.
        if (! levelDragging && (int) sLevel.getValue() != level)
            sLevel.setValue ((double) level, juce::dontSendNotification);
    }

    /** ONE LED FOR TWO SWITCHES: the tab is green when EITHER bass feature is
        on, because the LED answers "is this tab doing something", not "which
        of its switches is set". */
    void refreshBassLed()
    {
        leds[kBass].setOn (bassPower.getToggleState() || btnManualBass.getToggleState());
    }

    void showFeature (int f)
    {
        selected = juce::jlimit (0, kNumFeatures - 1, f);

        const bool h = (selected == kHarmony);
        harmonyPower.setVisible (h);
        btnType     .setVisible (h);
        sLevel      .setVisible (h);

        const bool bs = (selected == kBass);
        bassPower    .setVisible (bs);
        btnManualBass.setVisible (bs);

        const bool ms = (selected == kMultiSplit);
        splitPower .setVisible (ms);
        sBassSplit .setVisible (ms);
        btnBassSlot.setVisible (ms);

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto col = r.removeFromLeft (kColW);

        const float cellH = (float) getHeight() / (float) kNumFeatures;

        for (int i = 0; i < kNumFeatures; ++i)
        {
            const auto cell = juce::Rectangle<float> (0.0f, cellH * (float) i,
                                                      (float) kColW, cellH);

            if (i == selected)
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.fillRect (cell);
            }

            g.setColour (i == selected ? juce::Colours::white
                                       : juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (i == selected ? 11.5f : 11.0f, juce::Font::bold));
            g.drawFittedText (kNames[i],
                              cell.toNearestInt().withTrimmedLeft (16).reduced (2, 0),
                              juce::Justification::centredLeft, 1);

            // INTERNAL SEPARATORS ONLY - between the cells, never above the
            // first or below the last. They start from nothing on the left and
            // die into the vertical rule on the right, which is the whole look.
            if (i > 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.drawHorizontalLine ((int) cell.getY(), 6.0f, (float) kColW);
            }
        }

        // THE ONE LONG BORDER.
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.drawVerticalLine (kColW, 0.0f, (float) getHeight());

        if (selected == kBass)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawFittedText ("BASS INVERSION",
                              r.withTrimmedLeft (44).removeFromTop (32),
                              juce::Justification::centredLeft, 1);

            // WHAT MANUAL BASS ACTUALLY DOES, said on screen.
            //
            // It is the only switch here whose effect is invisible until you
            // play - so without this line the honest reading of the button is
            // "nothing happened". Three facts, no prose: the keyboard becomes
            // two hands, the left one is solo 7, and it stops moving chords.
            //
            // NO NUMBER IN IT, deliberately. The boundary is the GLOBAL split
            // the SPLIT knob owns, not the bass split one tab over, and
            // printing a figure here would invite reading it as a second
            // setting that lives on this panel. "THE SPLIT" points at the one
            // control that actually decides it.
            //
            // Anchored on the BUTTON'S OWN BOUNDS rather than on another strip
            // measured off `r`: the caption belongs under that control, and a
            // second independent measurement is how a caption ends up floating
            // somewhere else the next time the rows move.
            const auto under = btnManualBass.getBounds()
                                   .withTrimmedTop (btnManualBass.getHeight())
                                   .withHeight (30);

            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.drawFittedText ("ON: LEFT OF THE SPLIT PLAYS SOLO 7"
                              "\nNO CHORDS FROM THE LEFT HAND"
                              "\nTURNS MULTI SPLIT OFF",
                              under, juce::Justification::centredTop, 3);
        }
        else if (selected != kHarmony && selected != kMultiSplit)
        {
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("not built yet",
                              r.reduced (8), juce::Justification::centred, 1);
        }
    }

    void resized() override
    {
        const int cellH = getHeight() / kNumFeatures;

        for (int i = 0; i < kNumFeatures; ++i)
            leds[i].setBounds (4, cellH * i + 5, kLedSize, kLedSize);

        auto panel = getLocalBounds().withTrimmedLeft (kColW + 5).reduced (4, 6);

        // POWER + LEVEL on the top row, TYPE UNDER THE SLIDER and exactly as
        // wide - slider track AND its value box, since the slider's bounds
        // already include the text box. Lining the two up means the type name
        // reads as belonging to the control above it rather than floating
        // beside the power switch, and it gets a far bigger touch target than
        // the 74 px it had.
        auto row1 = panel.removeFromTop (32);
        harmonyPower.setBounds (row1.removeFromLeft (32));
        row1.removeFromLeft (6);
        sLevel.setBounds (row1);

        panel.removeFromTop (6);

        // ROW 2 SPANS THE WHOLE PANEL, power button's left edge to the slider
        // value's right edge.
        //
        // It used to start at the slider's X, which left a dead notch under the
        // power toggle and pushed the button hard against the right wall - the
        // row read as misaligned rather than as a second row. Taking the full
        // width also centres the caption over the row instead of over the
        // slider alone.
        auto row2 = panel.removeFromTop (26);

        const int rowX = harmonyPower.getX();
        const int rowW = juce::jmax (40, sLevel.getRight() - rowX);
        btnType.setBounds (row2.withX (rowX).withWidth (rowW));

        // MULTI SPLIT shares row 1's shape so the two tabs do not jump when you
        // switch between them: power button in the same place, its one control
        // filling the same width.
        splitPower .setBounds (harmonyPower.getBounds());
        sBassSplit .setBounds (sLevel.getBounds());
        btnBassSlot.setBounds (btnType.getBounds());

        // BASS shares the same two-row shape as the other tabs.
        bassPower    .setBounds (harmonyPower.getBounds());
        btnManualBass.setBounds (btnType.getBounds());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x >= kColW) return;

        const int cellH = juce::jmax (1, getHeight() / kNumFeatures);
        showFeature (juce::jlimit (0, kNumFeatures - 1, e.y / cellH));
    }

    int getSelectedFeature() const noexcept { return selected; }

private:
    static constexpr int kColW    = 90;
    static constexpr int kLedSize = 9;

    static constexpr const char* kNames[kNumFeatures] =
        { "HARMONY", "MULTI SPLIT", "BASS" };

    int selected = kHarmony;
    int typeIdx  = (int) Harmonizer::Type::Duet;

    StatusLED        leds[kNumFeatures];
    PowerButton      harmonyPower;
    PowerButton      splitPower;
    juce::Slider     sBassSplit;
    juce::TextButton btnBassSlot;
    PowerButton      bassPower;
    juce::TextButton btnManualBass;
    int              bassSlotIdx = 6;          // solo 7, matching the processor

    // Set between onDragStart and onDragEnd.  While either is true the 30 Hz
    // mirror leaves that slider alone - message thread only, so a plain bool.
    bool             bassSplitDragging = false;
    bool             levelDragging     = false;
    juce::TextButton btnType;
    juce::Slider     sLevel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeaturePanel)
};

} // namespace Betel
