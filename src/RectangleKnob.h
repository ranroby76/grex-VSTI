#pragma once
#include <JuceHeader.h>

// =====================================================================================
//  RectangleKnob
//  Transparent background (back.png provides the bg).
//  Draws only: label text, value text, up/down triangles on each side.
//  Click and drag UP to increase, DOWN to decrease.
// =====================================================================================
class RectangleKnob : public juce::Component
{
public:
    RectangleKnob(const juce::String& label = {},
                  double minVal = 0.0, double maxVal = 127.0,
                  double defaultVal = 0.0, double stepSize = 1.0)
        : labelText(label),
          minValue(minVal), maxValue(maxVal),
          value(defaultVal), defaultValue(defaultVal), step(stepSize)
    {
        setOpaque(false);
        setRepaintsOnMouseActivity(true);
    }

    void setRange(double newMin, double newMax, double newStep = 1.0)
    {
        minValue = newMin;
        maxValue = newMax;
        step = newStep;
        value = juce::jlimit(minValue, maxValue, value);
        repaint();
    }

    void setValue(double newVal, bool notify = true)
    {
        newVal = juce::jlimit(minValue, maxValue, std::round(newVal / step) * step);
        if (newVal != value)
        {
            value = newVal;
            repaint();
            if (notify && onChange)
                onChange(value);
        }
    }

    double getValue() const { return value; }
    void setLabel(const juce::String& text) { labelText = text; repaint(); }

    void setTextColour(juce::Colour c) { textColour = c; repaint(); }
    void setTriangleColour(juce::Colour c) { triColour = c; repaint(); }

    // Pixels of mouse drag per one step increment. Higher = slower/finer.
    void setDragPixelsPerStep(double pixels) { dragPixelsPerStep = pixels; }

    // Force a fixed number of decimal places in the displayed value.
    // -1 (default) keeps the automatic behaviour (integer when step >= 1,
    // otherwise one decimal).
    void setNumDecimals(int n) { decimalsOverride = n; repaint(); }

    std::function<void(double)> onChange;

    // ── Paint ────────────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // NO background fill — back.png provides it

        // Triangle indicators on both sides
        drawTriangles(g, bounds);

        // Label (top portion)
        if (labelText.isNotEmpty())
        {
            auto labelArea = bounds.removeFromTop(bounds.getHeight() * 0.35f);
            g.setColour(textColour);
            g.setFont(juce::Font(juce::jmin(labelArea.getHeight() * 0.7f, 13.0f)));
            g.drawText(labelText, labelArea, juce::Justification::centred, false);
        }

        // Value (center/bottom)
        g.setColour(textColour);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.55f, 28.0f), juce::Font::bold));

        juce::String displayText;
        if (decimalsOverride >= 0)
            displayText = juce::String(value, decimalsOverride);
        else if (step >= 1.0)
            displayText = juce::String((int)value);
        else
            displayText = juce::String(value, 1);

        g.drawText(displayText, bounds, juce::Justification::centred, false);
    }

    // ── Mouse ────────────────────────────────────────────────────────────────
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;          // locked (e.g. tempo SYNCED mode)
        dragStartY = e.y;
        dragStartValue = value;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;          // locked
        const double pxPerStep = e.mods.isShiftDown() ? (dragPixelsPerStep * 4.0) : dragPixelsPerStep;
        const double dy = (double)(dragStartY - e.y);
        const double steps = dy / pxPerStep;
        const double newVal = dragStartValue + steps * step;
        setValue(newVal);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        setValue(defaultValue);
    }

private:
    juce::String labelText;
    double minValue, maxValue, value, defaultValue, step;
    double dragPixelsPerStep = 4.0;
    int dragStartY = 0;
    double dragStartValue = 0.0;
    int decimalsOverride = -1;

    juce::Colour textColour = juce::Colours::white;
    juce::Colour triColour  = juce::Colours::white.withAlpha(0.85f);

    void drawTriangles(juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        const float triSize = juce::jmin(bounds.getWidth() * 0.12f, 8.0f);
        const float centreY = bounds.getCentreY();
        const float margin = 6.0f;

        g.setColour(triColour);

        // Left side
        {
            float lx = bounds.getX() + margin + triSize * 0.5f;
            juce::Path upTri;
            upTri.addTriangle(lx - triSize * 0.5f, centreY - 2.0f,
                              lx + triSize * 0.5f, centreY - 2.0f,
                              lx,                  centreY - 2.0f - triSize);
            g.fillPath(upTri);

            juce::Path downTri;
            downTri.addTriangle(lx - triSize * 0.5f, centreY + 2.0f,
                                lx + triSize * 0.5f, centreY + 2.0f,
                                lx,                  centreY + 2.0f + triSize);
            g.fillPath(downTri);
        }

        // Right side
        {
            float rx = bounds.getRight() - margin - triSize * 0.5f;
            juce::Path upTri;
            upTri.addTriangle(rx - triSize * 0.5f, centreY - 2.0f,
                              rx + triSize * 0.5f, centreY - 2.0f,
                              rx,                  centreY - 2.0f - triSize);
            g.fillPath(upTri);

            juce::Path downTri;
            downTri.addTriangle(rx - triSize * 0.5f, centreY + 2.0f,
                                rx + triSize * 0.5f, centreY + 2.0f,
                                rx,                  centreY + 2.0f + triSize);
            g.fillPath(downTri);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RectangleKnob)
};




