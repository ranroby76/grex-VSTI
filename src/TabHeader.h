#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ===============================================
//  TabHeader Component
//  Displays a simple title text for a tab
// ===============================================
class TabHeader : public juce::Component
{
public:
    TabHeader(const juce::String& title)
        : titleText(title)
    {
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::transparentBlack);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText(titleText, getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::String titleText;
};
