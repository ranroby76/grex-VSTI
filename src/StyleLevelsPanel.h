
#pragma once
//==============================================================================
// StyleLevelsPanel.h — the STYLE LEVELS block of the SET EDITOR.
//
//   +- STYLE LEVELS - <the loaded style> ------------------------------------+
//   |    [ BOOST ]        [ MAKEUP ]        [ IGNORE STYLE VOLUMES ]         |
//   |    Per style - live. SAVE SET keeps it with this style.                |
//   +------------------------------------------------------------------------+
//
// Three controls.  That is the whole panel.
//
// ── WHY SO FEW ───────────────────────────────────────────────────────────────
//
// It used to carry ten: three role-fader percentages and four switches for
// automatic corrections, on top of these three.  Every one of those was a gain
// stage the user could not see on the mixer, quietly moving levels underneath
// the faders they had just set.
//
// The role percentages are the MIXER's job now, the per-slot shaping is the
// SOUND EDITOR's, and the automatic corrections are gone rather than switched
// off and left as a trap.  What is left is the two things that act on the style
// BUS as a whole - how hard it is driven, and how much its sections are glued
// together - plus the one decision that has to be made before either of them
// means anything: whose instrument volumes are we using.
//
// ── THE SCALE ────────────────────────────────────────────────────────────────
//
// Both sliders read 0..100 in 101 steps, 0 = no effect, 100 = full effect.  The
// same scale on both, because they answer the same question: how much of this?
//
//   BOOST   0 -> untouched         100 -> +24 dB on the style bus
//   MAKEUP  0 -> normalisation off 100 -> sections fully levelled
//
// MAKEUP is a DEPTH.  At 100 every section is driven to the same loudness; at
// 30 the quiet ones travel 30% of the way toward the loud ones.  That is what
// makes it behave like glue rather than a leveller - and 0 is its off switch,
// which is why there is no separate AUTO MAKEUP button beside it.
//
// ── NO DEFAULTS BUTTONS ──────────────────────────────────────────────────────
//
// DEFAULTS and SAVE AS DEFAULT are gone with grex_levels.xml.  Every set carries
// these three values, so a template file was a fourth owner of something that
// already had one too many.  The defaults are compiled in: BOOST 80, MAKEUP 20.
//==============================================================================

#include <JuceHeader.h>
#include <functional>
#include <vector>

#include "InstrEditPanel.h"     // GoldSlider / InstrEditStyle
#include "StyleLevels.h"

class StyleLevelsPanel : public juce::Component
{
public:
    /** A value changed.  The host re-applies the style so the new level is
        audible without the user having to reload it by hand. */
    std::function<void()> onLevelsChanged;

    StyleLevelsPanel()
    {
        // 101 steps across 0..100 - whole numbers, so a value can be read off,
        // written down and dialled again exactly.
        boost .setStep (1.0f);
        makeup.setStep (1.0f);

        // HORIZONTAL.  These two read as amounts on a scale, not as faders in a
        // mixer strip - and a horizontal track shows the 0..100 span at a glance
        // where a vertical one makes you read the number to know where you are.
        boost .setHorizontal (true);
        makeup.setHorizontal (true);

        for (auto* s : allSliders())
        {
            addAndMakeVisible (*s);
            s->onChange = [this] (float) { push(); };
        }

        ignoreCc7Btn.setButtonText ("IGNORE STYLE VOLUMES");
        ignoreCc7Btn.setClickingTogglesState (true);
        InstrEditStyle::styleSquareButton (ignoreCc7Btn, false);
        ignoreCc7Btn.onClick = [this]
        {
            InstrEditStyle::styleSquareButton (ignoreCc7Btn, ignoreCc7Btn.getToggleState());
            push();
        };
        addAndMakeVisible (ignoreCc7Btn);

        hint.setText ("Per style - live. SAVE SET keeps it with this style.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, InstrEditStyle::kPanelLabel.withAlpha (0.70f));
        hint.setFont (juce::Font (13.0f, juce::Font::plain));
        addAndMakeVisible (hint);

        refreshFromState();
    }

    /** The style these levels belong to, for the frame title. */
    void setStyleName (const juce::String& name)
    {
        styleName = name;
        repaint();
    }

    /** Pull every control from the live settings - after a load or a set change. */
    void refreshFromState()
    {
        auto& sl = Betel::StyleLevels::get();
        seeding = true;

        boost .setValue (sl.boostValue());
        makeup.setValue (sl.makeupValue());

        ignoreCc7Btn.setToggleState (sl.ignoreCc7(), juce::dontSendNotification);
        InstrEditStyle::styleSquareButton (ignoreCc7Btn, sl.ignoreCc7());

        seeding = false;
    }

    void paint (juce::Graphics& g) override
    {
        const auto title = styleName.isNotEmpty() ? "STYLE LEVELS - " + styleName.toUpperCase()
                                                  : juce::String ("STYLE LEVELS");
        InstrEditStyle::paintComponentFrame (g, getLocalBounds(), title);
    }

    void resized() override
    {
        auto inner = InstrEditStyle::componentFrameContent (getLocalBounds());

        // ── ONE BLOCK, TWO COLUMNS ───────────────────────────────────────────
        //
        // Everything is laid out inside a block whose height is the SLIDER STACK,
        // not the frame.  That is the whole fix: the toggle used to be centred in
        // a column spanning the full frame height while the sliders occupied only
        // its top, so the two centred on different axes and the button floated -
        // and capped at 56 px it looked lost beside a 200 px stack.
        //
        // Now the toggle spans the block exactly: its top meets BOOST's top and
        // its bottom meets MAKEUP's bottom, so the three read as one unit.
        const int rowGap = 14;
        const int hintH  = 22;

        const int rowH   = juce::jlimit (52, 76,
                                         (inner.getHeight() - rowGap - hintH - 10) / 2);
        const int blockH = rowH * 2 + rowGap;

        auto block = inner.removeFromTop (juce::jmin (blockH, inner.getHeight()));

        // Right column: a third of the width, clamped so it neither crowds the
        // sliders on a narrow window nor sprawls on a wide one.
        const int togW = juce::jlimit (200, 300, block.getWidth() / 3);
        auto togCell = block.removeFromRight (togW);
        block.removeFromRight (16);
        ignoreCc7Btn.setBounds (togCell);

        boost .setBounds (block.removeFromTop (rowH));
        block.removeFromTop (rowGap);
        makeup.setBounds (block.removeFromTop (rowH));

        // Directly under the block rather than pinned to the frame's bottom edge,
        // which is what left the dead band below MAKEUP.
        inner.removeFromTop (10);
        hint.setBounds (inner.removeFromTop (hintH));
    }

private:
    std::vector<GoldSlider*> allSliders() { return { &boost, &makeup }; }

    /** IGNORE STYLE VOLUMES greys nothing now - there is nothing left on this
        panel that depends on the style's CC 7.  BOOST and MAKEUP both act on the
        finished style bus, whichever levels fed it. */
    void push()
    {
        if (seeding) return;

        auto& sl = Betel::StyleLevels::get();
        sl.setBoost     (boost .getValue());
        sl.setMakeup    (makeup.getValue());
        sl.setIgnoreCc7 (ignoreCc7Btn.getToggleState());

        if (onLevelsChanged) onLevelsChanged();
    }

    // 0 = no effect, 100 = full effect, on both.  Defaults compiled in.
    GoldSlider boost  { "BOOST",  0.0f, 100.0f, Betel::StyleLevels::kDefaultBoost,  "" };
    GoldSlider makeup { "MAKEUP", 0.0f, 100.0f, Betel::StyleLevels::kDefaultMakeup, "" };

    juce::TextButton ignoreCc7Btn;
    juce::Label      hint;
    juce::String     styleName;
    bool             seeding = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleLevelsPanel)
};

// The StyleLevelsWindow that used to live here is gone.  The panel is mounted
// directly in the SET EDITOR tab now (see SetEditorTab.h), so a floating window
// over the top of it would be a second door into the same room - and the tab is
// the better one, because levels have to be judged against a playing style and
// a window sitting over the transport gets in the way of that.
