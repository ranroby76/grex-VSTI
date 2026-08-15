#pragma once
#include <JuceHeader.h>

// NOTE:
// This panel must remain visually transparent.
// back.png already contains all panel graphics.
// This file only places controls; no background fills.

class BetelSidePanel : public juce::Component
{
public:
    BetelSidePanel()
    {
        setOpaque(false);

        // Tempo
        addAndMakeVisible(tempoLabel);
        tempoLabel.setText("TEMPO", juce::dontSendNotification);
        tempoLabel.setJustificationType(juce::Justification::centred);
        tempoLabel.setColour(juce::Label::textColourId, juce::Colours::black);

        addAndMakeVisible(tempoValue);
        tempoValue.setText("80", juce::dontSendNotification);
        tempoValue.setJustificationType(juce::Justification::centred);
        tempoValue.setColour(juce::Label::textColourId, juce::Colours::black);
        tempoValue.setFont(juce::Font(24.0f, juce::Font::bold));

        // Buttons (placeholders; we will skin later)
        addAndMakeVisible(btnTap);
        addAndMakeVisible(btnReset);

        addAndMakeVisible(btnArranger);
        addAndMakeVisible(btnPiano);

        addAndMakeVisible(btnSingle);
        addAndMakeVisible(btnMulti);

        addAndMakeVisible(btnLoadSet);
        addAndMakeVisible(btnSaveSet);
        addAndMakeVisible(btnReload);

        setupButton(btnTap, "TAP");
        setupButton(btnReset, "RESET");
        setupButton(btnArranger, "ARRANGER");
        setupButton(btnPiano, "PIANO");
        setupButton(btnSingle, "SINGLE");
        setupButton(btnMulti, "MULTI");
        setupButton(btnLoadSet, "LOAD\nSET");
        setupButton(btnSaveSet, "SAVE\nSET");
        setupButton(btnReload, "RE-\nLOAD");

        // Transparent buttons by default so you only see text until we add skins
        makeButtonTransparent(btnTap);
        makeButtonTransparent(btnReset);
        makeButtonTransparent(btnArranger);
        makeButtonTransparent(btnPiano);
        makeButtonTransparent(btnSingle);
        makeButtonTransparent(btnMulti);
        makeButtonTransparent(btnLoadSet);
        makeButtonTransparent(btnSaveSet);
        makeButtonTransparent(btnReload);
    }

    void paint(juce::Graphics&) override
    {
        // DO NOT draw background here.
        // back.png is the background.
    }

    void resized() override
    {
        // Initial approximations (we will refine after first run).
        const int W = getWidth();

        tempoLabel.setBounds(60, 55, W - 120, 22);
        tempoValue.setBounds(75, 82, W - 150, 44);

        btnTap.setBounds(45, 150, (W - 90) / 2, 44);
        btnReset.setBounds(45 + (W - 90) / 2 + 8, 150, (W - 90) / 2 - 8, 44);

        btnArranger.setBounds(55, 255, (W - 110) / 2, 42);
        btnPiano.setBounds(55 + (W - 110) / 2 + 10, 255, (W - 110) / 2 - 10, 42);

        btnSingle.setBounds(55 + (W - 110) / 2 + 10, 300, (W - 110) / 2 - 10, 42);
        btnMulti.setBounds(55 + (W - 110) / 2 + 10, 345, (W - 110) / 2 - 10, 42);

        // Bottom utility buttons cluster (left)
        const int bottomY = getHeight() - 120;
        btnLoadSet.setBounds(25, bottomY, 85, 58);
        btnSaveSet.setBounds(120, bottomY, 85, 58);
        btnReload.setBounds(215, bottomY, 70, 58);
    }

private:
    juce::Label tempoLabel, tempoValue;

    juce::TextButton btnTap, btnReset;
    juce::TextButton btnArranger, btnPiano;
    juce::TextButton btnSingle, btnMulti;

    juce::TextButton btnLoadSet, btnSaveSet, btnReload;

    void setupButton(juce::TextButton& b, const juce::String& text)
    {
        b.setButtonText(text);
        b.setClickingTogglesState(false);
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    }

    void makeButtonTransparent(juce::TextButton& b)
    {
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BetelSidePanel)
};