#pragma once
//==============================================================================
// FolderLocatorLED.h
//
// A pure status LED for the system-folder locator — identical circle design to
// RegistrationLED.  Green when the folder is found, red when it isn't.  No
// glyph, no blink, no mouse interaction (the LOCATE SYSTEM FOLDER button does
// the locating).
//==============================================================================

#include <JuceHeader.h>

class FolderLocatorLED : public juce::Component
{
public:
    FolderLocatorLED() { setOpaque (false); }

    void setFolderFound (bool found)
    {
        if (folderFound == found) return;
        folderFound = found;
        repaint();
    }
    bool isFolderFound() const { return folderFound; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto  circle   = bounds.withSizeKeepingCentre (diameter, diameter);

        g.setColour (folderFound ? juce::Colour (0xFF00CC00) : juce::Colour (0xFFCC0000));
        g.fillEllipse (circle);

        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.fillEllipse (circle.reduced (diameter * 0.25f)
                              .translated (-diameter * 0.08f, -diameter * 0.08f));
    }

private:
    bool folderFound = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FolderLocatorLED)
};