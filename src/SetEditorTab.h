



#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
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

    /** What the two bake buttons do, in the words someone needs BEFORE pressing
        one. Kept next to the buttons rather than in the tutorial because the
        moment of doubt is here, not there. */
    void showBakeInfo()
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::NoIcon,
            "About baking",
            "Every style ships with a SET - the sounds, the mixer, the jumps, the "
            "levels. BAKING writes the starting point for the eight STYLE SLOTS "
            "into those sets, worked out from what each style actually asks for.\n\n"

            "BAKE MISSING\n"
            "Fills only the sets that have no slot block yet. It never touches a "
            "set that already has one, so nothing you have tuned can be lost. "
            "This is the everyday button, and it is safe to press at any time.\n\n"

            "REBUILD ALL\n"
            "Overwrites the slot block in EVERY set, including the ones you have "
            "already tuned by hand. Any per-style voicing you saved into a style "
            "slot is replaced by the computed starting point. There is no undo.\n\n"

            "What survives a REBUILD\n"
            "Only the STYLE slots are rewritten. Your eight SOLO slots, the mixer, "
            "the jumps table, the crash settings, the style levels and the ducker "
            "are all left exactly as they were.\n\n"

            "When you would want REBUILD ALL\n"
            "After changing something about how slots are worked out, or if a "
            "batch of sets was baked from a library that has since been fixed. If "
            "neither of those is true, you want BAKE MISSING.",
            "Close");
    }

    SetEditorTab()
    {
        setOpaque (false);

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
                "This overwrites the style-slot block in EVERY set with the "
                "computed starting point, discarding any per-style voicing you "
                "have saved. There is no undo.\n\n"
                "We hope you have read the \"i\" beside these buttons and know "
                "what you are doing.\n\n"
                "If you only want to fill the sets that have nothing yet, that is "
                "BAKE MISSING, and it cannot lose anything.",
                "Rebuild", "Cancel", nullptr,
                juce::ModalCallbackFunction::create ([this] (int r)
                {
                    if (r == 1 && onBakeAllSets) onBakeAllSets (true);
                }));
        };
        addAndMakeVisible (rebakeBtn);

        // ── THE "i" ──────────────────────────────────────────────────────────
        //
        // These two buttons are the only controls in the plugin that can reach
        // into files the user is not looking at and change them in bulk, and
        // their names do not say so. "REBUILD ALL" sounds like a refresh.
        //
        // The explanation is deliberately BEFORE the warning rather than inside
        // it: a confirm box is read as an obstacle and clicked through, but an
        // "i" is read by someone who has chosen to find out, which is the only
        // moment the words actually land.
        infoBtn.setButtonText ("i");
        InstrEditStyle::styleSquareButton (infoBtn, false);
        infoBtn.onClick = [this] { showBakeInfo(); };

        addAndMakeVisible (infoBtn);

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
        bakeStatus.setColour (juce::Label::textColourId, juce::Colour (Betel::Pal::kAccent));
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
        // NOTHING TO REFRESH ANY MORE.  The STYLE LEVELS panel is gone with its
        // one control - BOOST is a compiled constant now, see StyleLevels.h.
        //
        // styleNameProvider is kept as a PUBLIC member and deliberately unused
        // here: MainComponent still assigns it, and this tab is where the rest
        // of a set's editable state is meant to land, which will want the
        // style's name the moment anything arrives.  It costs a std::function
        // and says what this tab is for.
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xFF181818));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    }

    /** Nothing on this tab takes a MIDI assignment now that the BOOST slider is
        gone.  Kept as a no-op so SettingsTab's forwarding does not have to know,
        and RemoteId::StyleBoost keeps its number - an id is a persistence key,
        and removing one silently re-points every assignment above it. */
    void attachRemotes (Betel::RemoteAssignHub& hub) { juce::ignoreUnused (hub); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);

        // THE STYLE LEVELS FRAME IS GONE, and with it the 245 px it reserved.
        // BOOST was the only control left in it and BOOST is now a constant, so
        // the frame was a titled rectangle around nothing.  The BAKE row moves
        // to the top of the tab rather than sitting a quarter of the way down a
        // panel that is not there.

        auto row = r.removeFromTop (32);

        bakeBtn   .setBounds (row.removeFromLeft (150).reduced (0, 2));
        row.removeFromLeft (8);
        rebakeBtn .setBounds (row.removeFromLeft (150).reduced (0, 2));
        row.removeFromLeft (6);
        // Square, and between the two buttons it explains rather than off at the
        // end - it has to be in the eye-line of someone reaching for REBUILD.
        infoBtn   .setBounds (row.removeFromLeft (28).reduced (0, 2));
        row.removeFromLeft (8);
        cancelBtn .setBounds (row.removeFromLeft (90).reduced (0, 2));
        row.removeFromLeft (12);
        bakeStatus.setBounds (row.removeFromLeft (160));

        r.removeFromTop (4);
        bakeHint.setBounds (r.removeFromTop (20));

    }

private:
    juce::TextButton bakeBtn, rebakeBtn, cancelBtn, infoBtn;
    juce::Label      bakeHint, bakeStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetEditorTab)
};




