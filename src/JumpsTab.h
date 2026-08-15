#pragma once
//==============================================================================
// JumpsTab.h
//
// The Jumps menu — defines what variation the sequencer should jump to once a
// transition variation (intro/fill/break) finishes a cycle loop.
//
// Layout:
//                 │ MAIN A │ MAIN B │ MAIN C │ MAIN D ║ INTRO A │ INTRO B │ INTRO C ║ FILL AA │ FILL BB │ FILL CC │ FILL DD ║ BREAK │
//   INTRO A       │   ●    │   ○    │   ○    │   ○    ║    ○    │    ○    │    ○    ║    ○    │    ○    │    ○    │    ○    ║   ○   │
//   INTRO B       │   ○    │   ●    │   ○    │   ○    ║    ○    │    ○    │    ○    ║    ○    │    ○    │    ○    │    ○    ║   ○   │
//   INTRO C       │   ○    │   ○    │   ●    │   ○    ║    ○    │    ○    │    ○    ║    ○    │    ○    │    ○    │    ○    ║   ○   │
//   FILL AA       │   ●    │   ○    │   ○    │   ○    ║ …
//   …             │
//   BREAK         │   ●    │   ○    │   ○    │   ○    ║ …
//
// Each row is radio-button-style — exactly one destination is selected at any
// time, and clicking a different one moves the selection there.
//==============================================================================

#include <JuceHeader.h>
#include <array>
#include <functional>

#include "MainTab.h"        // for LedButton (reused as selector buttons)
#include "StyleData.h"      // for Betel::StyleSection
#include "JumpsConfig.h"    // for kNumJumpSources / kNumJumpDestinations

class JumpsTab : public juce::Component
{
public:
    JumpsTab();
    ~JumpsTab() override = default;

    // ── Per-source API ────────────────────────────────────────────────────────
    Betel::StyleSection getDestination (Betel::StyleSection source) const;
    void                setDestination (Betel::StyleSection source,
                                        Betel::StyleSection dest);

    // ── Bulk (used by the Set serialiser) ────────────────────────────────────
    Betel::JumpsConfig getConfig() const;
    void               setConfig (const Betel::JumpsConfig& cfg);

    // Fired when the user clicks a destination button (programmatic setters
    // via setDestination/setConfig do not fire this).
    std::function<void(Betel::StyleSection source, Betel::StyleSection dest)>
        onJumpChanged;

    void paint   (juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int kRows = Betel::kNumJumpSources;
    static constexpr int kCols = Betel::kNumJumpDestinations;

    int  sourceRowIndex   (Betel::StyleSection s) const;
    int  destColIndex     (Betel::StyleSection s) const;
    void selectDestination (int sourceRow, int destCol, bool fireCallback);

    juce::Label                                     lblTitle;
    std::array<juce::Label, kCols>                  colHeaders;
    std::array<juce::Label, kRows>                  rowLabels;
    std::array<std::array<LedButton, kCols>, kRows> buttons;

    // Default mapping (intros → matching mains, fills → matching mains,
    // break → main A) lives here so the visual reflects it on first show.
    std::array<int, kRows> selectedDest { 0, 1, 2,    0, 1, 2, 3,    0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JumpsTab)
};
