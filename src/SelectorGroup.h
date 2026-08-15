

#pragma once
#include <JuceHeader.h>

#include "InstrEditPanel.h"   // InstrEditStyle::kPanelLabel

// =====================================================================================
//  SelectorGroup
//  Transparent background (back.png provides the bg).
//  Draws only: selection highlight, separator lines, text labels.
// =====================================================================================
class SelectorGroup : public juce::Component
{
public:
    SelectorGroup(const juce::StringArray& options, int defaultIndex = 0)
        : labels(options), selectedIndex(juce::jlimit(0, options.size() - 1, defaultIndex))
    {
        setOpaque(false);
        setRepaintsOnMouseActivity(true);
    }

    void setOptions(const juce::StringArray& options, int defaultIndex = 0)
    {
        labels = options;
        selectedIndex = juce::jlimit(0, labels.size() - 1, defaultIndex);
        repaint();
    }

    int getSelectedIndex() const { return selectedIndex; }

    juce::String getSelectedText() const
    {
        if (selectedIndex >= 0 && selectedIndex < labels.size())
            return labels[selectedIndex];
        return {};
    }

    void setSelectedIndex(int idx, bool notify = true)
    {
        idx = juce::jlimit(0, labels.size() - 1, idx);
        if (idx != selectedIndex)
        {
            selectedIndex = idx;
            repaint();
            if (notify && onChange)
                onChange(selectedIndex);
        }
    }

    void setColours(juce::Colour text, juce::Colour selBg, juce::Colour selText)
    {
        textColour = text;
        selectedBg = selBg;
        selectedText = selText;
        repaint();
    }

    std::function<void(int)> onChange;

    // ── Paint ────────────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const int n = labels.size();
        if (n == 0) return;

        // NO background fill — back.png provides it

        const float itemH = bounds.getHeight() / (float)n;
        const float fontSize = juce::jmin(itemH * 0.50f, 14.0f);

        for (int i = 0; i < n; ++i)
        {
            auto itemRect = juce::Rectangle<float>(
                bounds.getX(), bounds.getY() + i * itemH,
                bounds.getWidth(), itemH);

            if (i == selectedIndex)
            {
                auto selRect = itemRect.reduced(3.0f, 1.5f);
                g.setColour(selectedBg);
                g.fillRoundedRectangle(selRect, 8.0f);
            }

            // Separator between items.
            //
            // Grey rather than white, and a sub-pixel fillRect rather than a
            // 1-px line: these divide segments of ONE control, so the divider
            // should read as a hairline inside it, not as a border between
            // separate buttons.  drawHorizontalLine cannot go under 1 px;
            // fillRect anti-aliases to a genuinely lighter line.
            if (i > 0)
            {
                g.setColour(juce::Colour(0xFF9A9A9A).withAlpha(0.30f));
                g.fillRect(bounds.getX() + 6.0f,
                           bounds.getY() + (float) i * itemH,
                           bounds.getWidth() - 12.0f,
                           0.6f);
            }

            // Text
            g.setColour(i == selectedIndex ? selectedText : textColour);
            g.setFont(juce::Font(fontSize, juce::Font::bold));
            g.drawText(labels[i], itemRect, juce::Justification::centred, false);
        }
    }

    // ── Mouse ────────────────────────────────────────────────────────────────
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int n = labels.size();
        if (n == 0) return;

        const float itemH = (float)getHeight() / (float)n;
        int clickedIndex = (int)(e.position.y / itemH);
        clickedIndex = juce::jlimit(0, n - 1, clickedIndex);

        setSelectedIndex(clickedIndex);
    }

private:
    juce::StringArray labels;
    int selectedIndex = 0;

    // #C2C2C2, matching every other caption on the left panel — see
    // InstrEditStyle::kPanelLabel for why it is not pure white.
    juce::Colour textColour   = InstrEditStyle::kPanelLabel;
    juce::Colour selectedBg   = juce::Colour(0xFFCC6600);
    // BLACK on the amber selection — white on #CC6600 is a poor read, and black
    // is what "engaged" looks like on every other lit control in the plugin.
    juce::Colour selectedText = juce::Colours::black;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelectorGroup)
};
