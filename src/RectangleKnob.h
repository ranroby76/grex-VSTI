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

    //==========================================================================
    // STEPPER MODE — PRESS to step, no caption, value on the vertical centre.
    //
    // A knob and a stepper are the same STATE with two input methods, so this is
    // a mode on the existing widget rather than a second class.  Everything that
    // already talks to a RectangleKnob - setValue, getValue, onChange, the MIDI
    // remote hub, the 30 Hz mirror - keeps working untouched, which is the whole
    // reason not to fork it.
    //
    // ONE PRESS = ONE STEP, AND DRAGGING IS OFF.  Both halves of that matter: a
    // control that steps on click and slides on drag gives two different results
    // for what feels like one gesture, and on a narrow box the drag wins by
    // accident constantly.  A transpose is a discrete musical decision, and an
    // accidental +2 in a live set is a wrong key.
    //
    // Double-click still returns to the default, because that is true of every
    // control in the plugin and breaking it here would be the surprise.
    //==========================================================================
    void setStepperMode (bool s) { stepperMode = s; repaint(); }
    bool isStepperMode() const   { return stepperMode; }

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

        if (stepperMode) { paintStepper (g, bounds); return; }

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

        if (stepperMode)
        {
            // THIRDS, NOT HALVES: the middle band is dead on purpose.  With two
            // live halves every press lands on one or the other, so a press
            // aimed at the readout - which is what a number invites you to
            // touch - would silently change it.
            const float third = (float) getHeight() / 3.0f;
            if      ((float) e.y <  third)        setValue (value + step);
            else if ((float) e.y >= third * 2.0f) setValue (value - step);
            return;
        }

        dragStartY = e.y;
        dragStartValue = value;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;          // locked
        if (stepperMode)   return;          // press-to-step only; see setStepperMode
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
        // A STEPPER MUST NOT RESET ON A FAST SECOND PRESS, and this is exactly
        // what went wrong: JUCE delivers mouseDown, mouseUp, mouseDown,
        // mouseDoubleClick for two quick clicks, so pressing up twice stepped to
        // +2 and was then thrown back to the default by the double-click that
        // followed.  Repeated pressing IS the normal gesture here, which makes
        // double-click-to-default not merely unhelpful but actively wrong: the
        // second press has already stepped via mouseDown, so ignoring this
        // leaves every click worth exactly one step, however fast they come.
        if (stepperMode) return;
        setValue(defaultValue);
    }

private:
    juce::String labelText;
    double minValue, maxValue, value, defaultValue, step;
    double dragPixelsPerStep = 4.0;
    int dragStartY = 0;
    double dragStartValue = 0.0;
    int decimalsOverride = -1;
    bool stepperMode = false;

    juce::Colour textColour = juce::Colours::white;
    juce::Colour triColour  = juce::Colours::white.withAlpha(0.85f);

    /** Up triangle / value / down triangle in equal thirds, so this and the hit
        test in mouseDown cannot disagree about where the press bands are. */
    void paintStepper (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        const float third = bounds.getHeight() / 3.0f;
        const float cx    = bounds.getCentreX();
        const float triW  = juce::jmin (bounds.getWidth() * 0.30f, 18.0f);
        const float triH  = juce::jmin (third * 0.45f, 11.0f);

        g.setColour (triColour);

        const float upCy = bounds.getY() + third * 0.5f;
        juce::Path up;
        up.addTriangle (cx - triW * 0.5f, upCy + triH * 0.5f,
                        cx + triW * 0.5f, upCy + triH * 0.5f,
                        cx,               upCy - triH * 0.5f);
        g.fillPath (up);

        const float dnCy = bounds.getBottom() - third * 0.5f;
        juce::Path dn;
        dn.addTriangle (cx - triW * 0.5f, dnCy - triH * 0.5f,
                        cx + triW * 0.5f, dnCy - triH * 0.5f,
                        cx,               dnCy + triH * 0.5f);
        g.fillPath (dn);

        auto textArea = bounds.withY (bounds.getY() + third).withHeight (third);
        g.setColour (textColour);
        g.setFont (juce::Font (juce::jmin (third * 0.85f, 26.0f), juce::Font::bold));

        juce::String t;
        if      (decimalsOverride >= 0) t = juce::String (value, decimalsOverride);
        else if (step >= 1.0)           t = juce::String ((int) value);
        else                            t = juce::String (value, 1);

        g.drawText (t, textArea, juce::Justification::centred, false);
    }

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