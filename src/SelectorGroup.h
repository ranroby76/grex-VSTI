
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
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

        // ── TOP TRIM: THE FIRST ITEM'S FORE SAT TOO HIGH ─────────────────────
        //
        // The selected fill was inset only 1.5 px plus 2.5% from the top of its
        // segment, so on item 0 it landed within about 2 px of the painted
        // panel's own top edge and read as touching it - noticeably tighter
        // than the TEMPO box, TAP and RESET TEMPO beside it.
        //
        // Trimmed from BOUNDS rather than from the selected rect, so the
        // separators and the label text move with it and the segments stay
        // equal.  Trimming only the TOP leaves the bottom edge exactly where it
        // was: withTrimmedTop reduces the height by the same amount it moves the
        // origin, so the last item still ends on the original boundary and only
        // the north edge changes.
        //
        // 3 px matches the horizontal inset already used on the selected rect
        // below, so the fill now clears its panel by the same margin on the left,
        // the right and the top.
        bounds = bounds.withTrimmedTop (kTopTrim);

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

                // MINUS 5%, ON BOTH AXES, ABOUT THE CENTRE.  Taken off the rect
                // AFTER the fixed 3 / 1.5 inset rather than by enlarging that
                // inset: the fixed part is a margin in pixels and has to stay
                // the same at every window scale, where this part is a
                // proportion of the segment and has to grow with it.  Written
                // as 2.5% per side, which is what reduced() takes.
                selRect = selRect.reduced (selRect.getWidth()  * 0.025f,
                                           selRect.getHeight() * 0.025f);

                // PER-INSTANCE EXTRA HEIGHT TRIM, off by default.
                //
                // Six controls share this class and they do not all have the
                // same amount of room: the artwork's black panel is one size,
                // but a 3-item group gives each segment a third of it and a
                // 2-item group a half, so the same proportional inset leaves
                // the taller segment looking generous and the shorter one
                // looking like it touches the panel edge.
                //
                // So the extra is asked for by the OWNER rather than baked in
                // here - changing the shared number would move all six, and
                // four of them are already sitting right.
                if (extraVInset > 0.0f)
                    selRect = selRect.reduced (0.0f, selRect.getHeight() * extraVInset);

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

    /** Extra height taken off the selection fill, as a PROPORTION PER SIDE of
        the already-inset rect.  0 = the shared default; 0.025f takes 5% off
        the height, 0.05f takes 10%.

        The value each group actually uses lives at its CALL SITE, not here -
        a number quoted in this comment would go stale the first time a group
        was retuned, which is exactly what happened to the TEMPO SPEED value. */
    void setExtraVerticalInset (float proportionPerSide)
    {
        extraVInset = juce::jlimit (0.0f, 0.25f, proportionPerSide);
        repaint();
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
    /** Design-space px shaved off the TOP of the whole group before the items
        are measured.  See paint(). */
    static constexpr float kTopTrim = 3.0f;

    /** See setExtraVerticalInset. */
    float extraVInset = 0.0f;

    juce::Colour selectedBg   = juce::Colour(Betel::Pal::kAccent);
    // BLACK on the amber selection — white on #CC6600 is a poor read, and black
    // is what "engaged" looks like on every other lit control in the plugin.
    juce::Colour selectedText = juce::Colours::black;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelectorGroup)
};
