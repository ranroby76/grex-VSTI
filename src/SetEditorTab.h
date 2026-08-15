
#pragma once
//==============================================================================
// SetEditorTab.h — the SET EDITOR tab.
//
//   ┌───────────────────────────────────────────────────────────────────────┐
//   │  ┌─ STYLE LEVELS - <loaded style> ─────────────────────────────────┐  │
//   │  │   [BOOST]   [MAKEUP]   [IGNORE STYLE VOLUMES]                   │  │
//   │  └─────────────────────────────────────────────────────────────────┘  │
//   └───────────────────────────────────────────────────────────────────────┘
//
// WHAT THIS TAB USED TO BE, AND WHY IT IS NOT ANY MORE.
//
// It was the SETS tab: a 4 x 9 grid of buttons, one per .xml in <root>/sets,
// with open / rename / delete and two sort keys.  That browser existed because
// a set was a thing you went and FETCHED — one saved song among many, unrelated
// to whatever style was loaded.
//
// Sets are not that any more.  Every style ships with its own set, and the
// Styles tab loads it; there is nothing left to browse for, because picking the
// style already picked the set.  A grid of set files beside a grid of style
// files would just be the same list twice.
//
// So the tab keeps its slot in the header and changes job: it is now where you
// EDIT the set that is currently loaded.  Today that means the style-bus levels,
// which is the part of a set that has to be judged against a playing style and
// therefore wants a whole tab rather than a window on top of one.
//
// Everything here writes into Betel::StyleLevels, i.e. into the loaded style's
// values.  Saving is SAVE SET, which is on the main frame and belongs there:
// the levels are one block inside the set payload, not a file of their own.
//==============================================================================

#include <JuceHeader.h>
#include <functional>

#include "InstrEditPanel.h"      // InstrEditStyle
#include "StyleLevelsPanel.h"

class SetEditorTab : public juce::Component
{
public:
    /** A level moved.  The host re-applies the current style so the change is
        audible without reloading it by hand. */
    std::function<void()> onLevelsChanged;

    /** The loaded style's name, for the panel's frame title. */
    std::function<juce::String()> styleNameProvider;

    /** BAKE ALL SETS — write the GM starting point into every set in the pack.
        The host owns the baker; this is just the door.  `overwrite` is the
        REBUILD variant, which replaces blocks that already exist. */
    std::function<void (bool overwrite)> onBakeAllSets;

    /** Stop a bake in progress. */
    std::function<void()> onCancelBake;

    SetEditorTab()
    {
        setOpaque (false);

        levels.onLevelsChanged = [this] { if (onLevelsChanged) onLevelsChanged(); };
        addAndMakeVisible (levels);

        // ── BAKE ─────────────────────────────────────────────────────────────
        //
        // Two buttons rather than one with a modifier: FILL GAPS is the safe
        // everyday action and REBUILD destroys hand tuning, and a destructive
        // action should never be one careless click away from a harmless one.
        bakeBtn.setButtonText ("BAKE MISSING");
        InstrEditStyle::styleSquareButton (bakeBtn, false);
        bakeBtn.onClick = [this] { if (onBakeAllSets) onBakeAllSets (false); };
        addAndMakeVisible (bakeBtn);

        rebakeBtn.setButtonText ("REBUILD ALL");
        InstrEditStyle::styleSquareButton (rebakeBtn, false);
        rebakeBtn.onClick = [this]
        {
            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::WarningIcon,
                "Rebuild every set?",
                "This overwrites the slot block in ALL sets with the GM starting "
                "point, discarding any per-style tuning you have saved.\n\n"
                "BAKE MISSING only fills sets that have none.",
                "Rebuild", "Cancel", nullptr,
                juce::ModalCallbackFunction::create ([this] (int r)
                {
                    if (r == 1 && onBakeAllSets) onBakeAllSets (true);
                }));
        };
        addAndMakeVisible (rebakeBtn);

        cancelBtn.setButtonText ("STOP");
        InstrEditStyle::styleSquareButton (cancelBtn, false);
        cancelBtn.onClick = [this] { if (onCancelBake) onCancelBake(); };
        cancelBtn.setVisible (false);
        addAndMakeVisible (cancelBtn);

        bakeHint.setText ("Writes each style's GM starting point into its set. "
                          "Style slots only - solo slots stay with their .ins files.",
                          juce::dontSendNotification);
        bakeHint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        bakeHint.setFont (juce::Font (13.0f, juce::Font::plain));
        addAndMakeVisible (bakeHint);

        bakeStatus.setJustificationType (juce::Justification::centredLeft);
        bakeStatus.setFont (juce::Font (13.0f, juce::Font::bold));
        bakeStatus.setColour (juce::Label::textColourId, juce::Colour (0xFF00C853));
        addAndMakeVisible (bakeStatus);
    }

    /** Host -> tab: bake progress and completion. */
    void setBakeProgress (int done, int total)
    {
        bakeStatus.setColour (juce::Label::textColourId, juce::Colour (0xFFCC6600));
        bakeStatus.setText (juce::String (done) + " / " + juce::String (total),
                            juce::dontSendNotification);
        setBakeRunning (true);
    }

    void setBakeResult (const juce::String& text, bool ok)
    {
        bakeStatus.setColour (juce::Label::textColourId,
                              ok ? juce::Colour (0xFF00C853) : juce::Colour (0xFFE53935));
        bakeStatus.setText (text, juce::dontSendNotification);
        setBakeRunning (false);
    }

    void setBakeRunning (bool running)
    {
        bakeBtn  .setEnabled (! running);
        rebakeBtn.setEnabled (! running);
        cancelBtn.setVisible (running);
    }

    /** Re-seed every control from whatever is now in force.  Called after a set
        load or a style change — the values belong to the style, so a tab left
        showing the previous style's numbers would push those stale numbers back
        onto the engine at the next slider move. */
    void refreshFromState()
    {
        if (styleNameProvider) levels.setStyleName (styleNameProvider());
        levels.refreshFromState();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xFF181818));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);

        // The panel is a fixed-height block pinned to the top rather than
        // stretched to fill: the sliders have a natural size and a 900 px tall
        // BOOST knob would be absurd.  The room below is deliberately empty —
        // it is where the rest of the set's editable state will go.
        // Three controls in one row - the block is half what it was.
        // Tall enough for the slider block plus its hint and no more - the
        // frame used to run well past its own content, which is what made the
        // empty lower half so obvious.
        const int panelH = juce::jmin (r.getHeight(), 245);
        levels.setBounds (r.removeFromTop (panelH));

        r.removeFromTop (12);

        auto row = r.removeFromTop (32);
        bakeBtn   .setBounds (row.removeFromLeft (150).reduced (0, 2));
        row.removeFromLeft (8);
        rebakeBtn .setBounds (row.removeFromLeft (150).reduced (0, 2));
        row.removeFromLeft (8);
        cancelBtn .setBounds (row.removeFromLeft (90).reduced (0, 2));
        row.removeFromLeft (12);
        bakeStatus.setBounds (row.removeFromLeft (160));

        r.removeFromTop (4);
        bakeHint.setBounds (r.removeFromTop (20));
    }

private:
    StyleLevelsPanel levels;
    juce::TextButton bakeBtn, rebakeBtn, cancelBtn;
    juce::Label      bakeHint, bakeStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetEditorTab)
};
