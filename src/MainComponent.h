


#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
#include "AssignPopup.h"
#include "MidiLinkCanvas.h"
#include <JuceHeader.h>

#include "RectangleKnob.h"
#include "InstrEditPanel.h"   // InstrEditStyle::kPanelLabel
#include "SelectorGroup.h"
#include "FinisherWindow.h"
#include "SpaceAnimation.h"
#include "FeaturePanel.h"

// Tabs
#include "MainTab.h"
#include "StylesTab.h"
#include "StyleSearchWindow.h"
#include "SoundsTab.h"
#include "MixerTab.h"
#include "SetEditorTab.h"
#include "MasterSettings.h"   // player-wide chord mode + tempo free/synced
#include "CcMap.h"            // the MIDI CC learn map, its own global preset
#include "SetBaker.h"         // writes the GM starting point into the whole set pack
#include "JumpsTab.h"
#include "CrashTab.h"
#include "SettingsTab.h"
#include "GlobalEffectsWindow.h"
#include "TutorialTab.h"     // the built-in manual, last in the tab row
#include "GrexPopupWindow.h" // stays in front, minimisable, never vanishes
#include "GrexSongRecorder.h"
#include "GrexSongPlayer.h"
#include "GrexTransportBar.h"
#include "FolderLocatorLED.h"

class BetelgeuseProcessor;  // forward-declared; full def comes via Main.h in .cpp

// =====================================================================================
//  BetelButton – transparent overlay, only draws text (back.png has the bg)
// =====================================================================================
class BetelButton : public juce::TextButton
{
public:
    BetelButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void setTextColour(juce::Colour c) { textCol_ = c; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (isButtonDown || getToggleState())
            g.setColour(juce::Colour(0x30FFFFFF));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        g.setColour(textCol_);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.35f, 14.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

private:
    juce::Colour textCol_ = InstrEditStyle::kPanelLabel;
};

// =====================================================================================
//  BlinkButton – transparent normally, blinks orange for 200ms on click
// =====================================================================================
class BlinkButton : public juce::TextButton, private juce::Timer
{
public:
    BlinkButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void setTextColour(juce::Colour c) { textCol_ = c; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool /*isButtonDown*/) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (blinking)
            g.setColour(juce::Colour(Betel::Pal::kAccent));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        g.setColour(blinking ? juce::Colours::black : textCol_);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.35f, 14.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        blinking = true;
        repaint();
        startTimer(200);
        juce::TextButton::mouseDown(e);
    }

    /** Hold the amber for longer than the 200 ms press blink.  Used to confirm
        FAST SAVE: the whole point of that button is that nothing opens, so the
        button itself has to be the receipt. */
    void flash(int ms)
    {
        blinking = true;
        repaint();
        startTimer(ms);
    }

private:
    void timerCallback() override
    {
        blinking = false;
        stopTimer();
        repaint();
    }

    bool blinking = false;
    juce::Colour textCol_ = InstrEditStyle::kPanelLabel;
};

// =====================================================================================
//  Separator between the three stacked buttons that read as one control
//  (COMMENTS / ORIENTAL SCALE / DAW START).
//
//  Grey rather than white and drawn as a sub-pixel fillRect rather than a
//  1-px line: it is a hairline dividing parts of ONE group, not a border
//  between three controls, and at white/0.5 it read as the latter.
// =====================================================================================
inline void drawGroupSeparator (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (juce::Colour (0xFF9A9A9A).withAlpha (0.30f));
    g.fillRect (bounds.getX() + 6.0f, bounds.getY(), bounds.getWidth() - 12.0f, 0.6f);
}

// =====================================================================================
//  BlinkSeparatorButton – blinks orange on click + hairline separator on top edge
// =====================================================================================
class BlinkSeparatorButton : public juce::TextButton, private juce::Timer
{
public:
    BlinkSeparatorButton(const juce::String& text = {}) : juce::TextButton(text) {}

    void setTextColour(juce::Colour c) { textCol_ = c; repaint(); }
    void setDrawTopSeparator(bool draw) { drawTopSep = draw; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool /*isButtonDown*/) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (blinking)
            g.setColour(juce::Colour(Betel::Pal::kAccent));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        if (drawTopSep) drawGroupSeparator(g, bounds);

        g.setColour(blinking ? juce::Colours::black : textCol_);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.35f, 13.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        blinking = true;
        repaint();
        startTimer(200);
        juce::TextButton::mouseDown(e);
    }

private:
    void timerCallback() override
    {
        blinking = false;
        stopTimer();
        repaint();
    }

    bool blinking = false;
    bool drawTopSep = false;
    juce::Colour textCol_ = InstrEditStyle::kPanelLabel;
};

// =====================================================================================
//  DawStartButton – toggle, fully transparent when off, orange fill when on
// =====================================================================================
class DawStartButton : public juce::TextButton
{
public:
    DawStartButton(const juce::String& text = {}) : juce::TextButton(text)
    {
        setClickingTogglesState(true);
    }

    void setTextColour(juce::Colour c) { textCol_ = c; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (getToggleState())
            g.setColour(juce::Colour(Betel::Pal::kAccent));
        else if (isButtonDown)
            g.setColour(juce::Colour(0x30FFFFFF));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        drawGroupSeparator(g, bounds);

        // BLACK on the lit (orange) state — see TabButton.
        g.setColour(getToggleState() ? juce::Colours::black : textCol_);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.35f, 13.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

private:
    juce::Colour textCol_ = InstrEditStyle::kPanelLabel;
};

// =====================================================================================
//  TabButton – tab selector button (full gold when active, not dimmed)
// =====================================================================================
class TabButton : public juce::TextButton
{
public:
    TabButton(const juce::String& text = {}) : juce::TextButton(text)
    {
        setClickingTogglesState(false);
    }

    void setActive(bool active) { isActive = active; repaint(); }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (isActive)
            g.setColour(juce::Colour(Betel::Pal::kAccent));
        else if (isButtonDown)
            g.setColour(juce::Colour(0x40FFFFFF));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x18FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        // MINUS 5%, ABOUT THE CENTRE - the same trim the left-panel selectors
        // got, so the two blue selection rectangles in the plugin still read as
        // one treatment.
        //
        // Applied to the ONE fill rather than only to the active state: this is
        // a single rectangle drawn in whichever colour the state chose, and
        // shrinking only the blue one would leave the hover highlight larger
        // than the selection it is previewing.
        //
        // The 10 px corner radius is deliberately NOT scaled with it - 5% of it
        // is half a pixel, and changing the shape for that is not worth it.
        g.fillRoundedRectangle(bounds.reduced (bounds.getWidth()  * 0.025f,
                                               bounds.getHeight() * 0.025f), 10.0f);

        // BLACK on the lit (gold) state: white on #D4AF37 is a poor read, and
        // black is what "engaged" looks like everywhere else in the plugin.
        g.setColour(isActive ? juce::Colours::black : juce::Colours::white.withAlpha(0.7f));
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.40f, 14.0f), juce::Font::bold));
        // drawFittedText, not drawText: these labels can be two lines ("FUNKEY /
        // MODE"), and drawText renders a single one.
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(3),
                         juce::Justification::centred, 2);
    }

private:
    bool isActive = false;
};

// =====================================================================================
//  BetelTextLabel – text only, no background
// =====================================================================================
class BetelTextLabel : public juce::Component
{
public:
    BetelTextLabel(const juce::String& text = {}, float fontSize = 14.0f,
                   juce::Colour textCol = InstrEditStyle::kPanelLabel)
        : displayText(text), fSize(fontSize), colour(textCol)
    {
        setOpaque(false);
    }

    void setText(const juce::String& t) { displayText = t; repaint(); }
    juce::String getText() const { return displayText; }
    void setFontSize(float s) { fSize = s; repaint(); }
    void setTextColour(juce::Colour c) { colour = c; repaint(); }

    void paint(juce::Graphics& g) override
    {
        g.setColour(colour);
        g.setFont(juce::Font(fSize, juce::Font::bold));
        g.drawFittedText(displayText, getLocalBounds().reduced(4), juce::Justification::centred, 2);
    }

private:
    juce::String displayText;
    float fSize;
    juce::Colour colour;
};

// =====================================================================================
//  HoldButton – toggle, NO text, orange LED circle
// =====================================================================================
class HoldButton : public juce::TextButton
{
public:
    HoldButton() : juce::TextButton(juce::String())
    {
        setClickingTogglesState(true);
    }

    void paintButton(juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        if (isButtonDown)
            g.setColour(juce::Colour(0x30FFFFFF));
        else if (isHighlighted)
            g.setColour(juce::Colour(0x15FFFFFF));
        else
            g.setColour(juce::Colour(0x00000000));

        g.fillRoundedRectangle(bounds, 10.0f);

        const float ledDiameter = juce::jmin(bounds.getWidth() * 0.3f, bounds.getHeight() * 0.25f);
        const float ledX = bounds.getCentreX() - ledDiameter * 0.5f;
        const float ledY = bounds.getY() + bounds.getHeight() * 0.15f;

        g.setColour(getToggleState() ? juce::Colour(Betel::Pal::kAccent) : juce::Colour(0xFF666666));
        g.fillEllipse(ledX, ledY, ledDiameter, ledDiameter);

        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillEllipse(ledX + ledDiameter * 0.2f, ledY + ledDiameter * 0.15f,
                       ledDiameter * 0.35f, ledDiameter * 0.35f);
    }
};

// =====================================================================================
//  InvisibleButton – a hit area and nothing else.
//
//  The RECORD SONG / PLAY SONG captions, the pill they sit in and the divider
//  between them are all painted into back.png.  Anything drawn here would be a
//  second button on top of the artwork's, so paintButton is deliberately empty.
//
//  It is still a real juce::Button: it clicks, it can be disabled, and it takes
//  part in the usual mouse routing.  It simply has no appearance of its own.
// =====================================================================================
class InvisibleButton : public juce::Button
{
public:
    InvisibleButton() : juce::Button ({}) {}

    void paintButton (juce::Graphics&, bool, bool) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InvisibleButton)
};

// =====================================================================================
//  SongRecordLED – the round lamp over RECORD SONG.
//
//  THREE STATES ON ONE LAMP, and the middle one is the reason this blinks at all.
//  ARMED IS NOT RECORDING: the recorder waits for the first gesture to latch
//  t = 0, so there is a real window where the player thinks tape is moving and it
//  is not.  With the caption now painted into the artwork, this lamp is the ONLY
//  thing that can tell them apart:
//
//      Idle       off
//      Armed      steady red   - "I have you, waiting for your first note"
//      Recording  blinking red - "tape is moving"
//
//  The timer only runs while recording, so an idle or armed lamp costs nothing.
// =====================================================================================
class SongRecordLED : public juce::Component, private juce::Timer
{
public:
    SongRecordLED() { setOpaque (false); }

    enum class State { Off = 0, Armed, Recording };

    void setState (State s)
    {
        if (state == s) return;
        state = s;

        if (state == State::Recording)
        {
            phase = true;
            startTimer (450);       // slow enough to read as a pulse, not a flicker
        }
        else
        {
            stopTimer();
            phase = true;           // armed shows the lamp solid
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (state == State::Off || ! phase) return;

        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto  circle = bounds.withSizeKeepingCentre (d, d);

        g.setColour (juce::Colour (0xFFE01414));
        g.fillEllipse (circle);

        // The same offset highlight FolderLocatorLED uses, so the two lamps read
        // as the same family of part rather than as two different components.
        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.fillEllipse (circle.reduced (d * 0.25f).translated (-d * 0.08f, -d * 0.08f));
    }

private:
    void timerCallback() override { phase = ! phase; repaint(); }

    State state = State::Off;
    bool  phase = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongRecordLED)
};

// =====================================================================================
//  SongPlayLED – the triangular lamp over PLAY SONG.  Green while a song is
//  loaded or playing, dark otherwise.
//
//  Drawn as a right-pointing triangle rather than a circle because the artwork
//  cuts a triangular window for it; a round lamp would show its corners.
// =====================================================================================
class SongPlayLED : public juce::Component
{
public:
    SongPlayLED() { setOpaque (false); }

    void setOn (bool shouldBeOn)
    {
        if (on == shouldBeOn) return;
        on = shouldBeOn;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (! on) return;

        // FILLS THE BOUNDS EXACTLY.  The old reduced(0.5f) took half a pixel
        // off every side, which on a lamp this small is most of the margin
        // between fitting the artwork's triangular window and sitting inside
        // it with a dark rim showing all the way round.  The bounds come
        // straight from the Photoshop file now, so they are the shape - there
        // is nothing left for an inset to protect against.
        auto b = getLocalBounds().toFloat();

        // THE THREE VERTICES.  A triangle is fully determined by its box once
        // the point's position is fixed, so there is no angle to set directly:
        // base down the LEFT edge, point on the RIGHT.  That is an up-pointing
        // triangle rotated +90 degrees, which is how the artwork is built.
        //
        // At the shipped 12 x 11.16, the point works out at 49.9 degrees and
        // the two base corners at 65.05 each.
        //
        // kApexY is where the point sits DOWN the right edge, 0.5 = halfway.
        // Named rather than written as getCentreY() because the artwork's own
        // window looks like it may carry the point slightly above centre - if
        // it still does not sit right, this is the one number to change.
        const float apexY = b.getY() + b.getHeight() * kApexY;

        juce::Path tri;
        tri.startNewSubPath (b.getX(),     b.getY());
        tri.lineTo          (b.getRight(), apexY);
        tri.lineTo          (b.getX(),     b.getBottom());
        tri.closeSubPath();

        g.setColour (juce::Colour (0xFF00CC00));
        g.fillPath (tri);
    }

private:
    bool on = false;

    /** See paint(). 0.5 = the point halfway down the right edge. */
    static constexpr float kApexY = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongPlayLED)
};

// =====================================================================================
//  RegistrationLED – rounded circle, green = registered, red = unregistered
// =====================================================================================
class RegistrationLED : public juce::Component
{
public:
    RegistrationLED() { setOpaque(false); }

    void setRegistered(bool reg) { registered = reg; repaint(); }
    bool isRegistered() const { return registered; }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        auto circle = bounds.withSizeKeepingCentre(diameter, diameter);

        g.setColour(registered ? juce::Colour(0xFF00CC00) : juce::Colour(0xFFCC0000));
        g.fillEllipse(circle);

        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.fillEllipse(circle.reduced(diameter * 0.25f).translated(-diameter * 0.08f, -diameter * 0.08f));
    }

private:
    bool registered = false;
};

// =====================================================================================
//  SplitKnob – compact knob with left-aligned triangle pair + value
// =====================================================================================
class SplitKnob : public juce::Component
{
public:
    SplitKnob(double minVal = 36.0, double maxVal = 127.0, double defaultVal = 60.0)
        : minValue(minVal), maxValue(maxVal), value(defaultVal), defaultValue(defaultVal)
    {
        setOpaque(false);
        setRepaintsOnMouseActivity(true);
    }

    void setValue(double newVal, bool notify = true)
    {
        newVal = juce::jlimit(minValue, maxValue, std::round(newVal));
        if (newVal != value)
        {
            value = newVal;
            repaint();
            if (notify && onChange)
                onChange(value);
        }
    }

    double getValue() const { return value; }

    std::function<void(double)> onChange;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        const float triSize = juce::jmin(bounds.getHeight() * 0.25f, 6.0f);
        const float centreY = bounds.getCentreY();
        const float lx = bounds.getX() + 10.0f + triSize * 0.5f;

        g.setColour(InstrEditStyle::kPanelLabel.withAlpha(0.85f));

        juce::Path upTri;
        upTri.addTriangle(lx - triSize * 0.5f, centreY - 1.5f,
                          lx + triSize * 0.5f, centreY - 1.5f,
                          lx,                  centreY - 1.5f - triSize);
        g.fillPath(upTri);

        juce::Path downTri;
        downTri.addTriangle(lx - triSize * 0.5f, centreY + 1.5f,
                            lx + triSize * 0.5f, centreY + 1.5f,
                            lx,                  centreY + 1.5f + triSize);
        g.fillPath(downTri);

        auto textArea = bounds.withLeft(lx + triSize + 2.0f);
        g.setColour(InstrEditStyle::kPanelLabel);
        g.setFont(juce::Font(juce::jmin(bounds.getHeight() * 0.60f, 16.0f), juce::Font::bold));
        g.drawText(juce::String((int)value), textArea, juce::Justification::centred, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStartY = e.y;
        dragStartValue = value;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const double pxPerStep = e.mods.isShiftDown() ? 8.0 : 3.0;
        const double dy = (double)(dragStartY - e.y);
        setValue(dragStartValue + dy / pxPerStep);
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
    double minValue, maxValue, value, defaultValue;
    int dragStartY = 0;
    double dragStartValue = 0.0;
};

// =====================================================================================
//  EnergySlider — STYLE DYNAMICS, vertical, 0..200 with 100 = as authored.
//
//  Pull it down and the band plays pianissimo; push it up and it digs in.  The
//  centre is the important part: this rides on top of writing that is already
//  correct, so 50 has to be a genuine no-op and has to be findable without
//  looking - hence the detent tick and the double-click.
//
//  Nothing here draws a frame or a caption.  The artwork carries both, like
//  every other control on this panel; the track, the fill and the handle are the
//  only things that move, so they are the only things painted.
// =====================================================================================
class EnergySlider : public juce::Component
{
public:
    EnergySlider() { setOpaque (false); setRepaintsOnMouseActivity (true); }

    void setValue (double v, bool notify = true)
    {
        v = juce::jlimit (0.0, 200.0, std::round (v));
        if (v == value) return;
        value = v;
        repaint();
        if (notify && onChange) onChange (value);
    }

    double getValue() const { return value; }

    std::function<void (double)> onChange;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f, 6.0f);
        if (b.getHeight() < 24.0f) return;

        auto readout = b.removeFromBottom (juce::jmin (b.getHeight() * 0.20f, 18.0f));

        const float cx      = b.getCentreX();
        const float trackW  = 5.0f;
        const float handleH = 11.0f;
        const float top     = b.getY()      + handleH * 0.5f;
        const float bot     = b.getBottom() - handleH * 0.5f;
        const float span    = juce::jmax (1.0f, bot - top);
        const float centreY = top + span * 0.5f;

        // 200 at the TOP, so up is more energy.
        const float y = bot - (float) (value / 200.0) * span;

        g.setColour (juce::Colour (Betel::Pal::kAccent).withAlpha (0.22f));
        g.fillRoundedRectangle (cx - trackW * 0.5f, top - 1.0f, trackW, span + 2.0f, trackW * 0.5f);

        // The 100 detent, always drawn: neutral has to be findable at a glance.
        g.setColour (InstrEditStyle::kPanelLabel.withAlpha (0.55f));
        g.fillRect (cx - 7.0f, centreY - 0.5f, 14.0f, 1.0f);

        // FILL FROM THE CENTRE, not from the bottom.  This is a deviation from
        // neutral in one of two directions; a bottom-up fill would read as an
        // amount, which is the wrong mental model for it.
        if (std::abs (value - 100.0) > 0.5)
        {
            g.setColour (juce::Colour (Betel::Pal::kAccent));
            g.fillRoundedRectangle (cx - trackW * 0.5f,
                                    juce::jmin (y, centreY),
                                    trackW,
                                    std::abs (y - centreY),
                                    trackW * 0.5f);
        }

        // A WHITE CIRCLE, like every other handle in the plugin.  A control that
        // looks unlike its neighbours reads as a different KIND of control, and
        // this one is an ordinary fader.
        const float rad = juce::jmin (handleH * 0.85f, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillEllipse (cx - rad - 1.0f, y - rad - 1.0f, (rad + 1.0f) * 2.0f, (rad + 1.0f) * 2.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (cx - rad, y - rad, rad * 2.0f, rad * 2.0f);

        g.setColour (InstrEditStyle::kPanelLabel);
        g.setFont (juce::Font (juce::jmin (readout.getHeight(), 15.0f), juce::Font::bold));
        g.drawText (juce::String ((int) value), readout, juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartY     = e.y;
        dragStartValue = value;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Twice the range in the same travel, so the coarse step doubles too -
        // otherwise the rescale would silently halve how far one drag moves it.
        const double pxPerStep = e.mods.isShiftDown() ? 2.0 : 0.6;
        setValue (dragStartValue + (double) (dragStartY - e.y) / pxPerStep);
    }

    void mouseUp (const juce::MouseEvent&) override
    { setMouseCursor (juce::MouseCursor::NormalCursor); }

    /** Straight back to neutral - the gesture people reach for on a control
        whose whole point is a no-op centre. */
    void mouseDoubleClick (const juce::MouseEvent&) override { setValue (100.0); }

private:
    double value = 100.0;
    int    dragStartY = 0;
    double dragStartValue = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnergySlider)
};

// =====================================================================================
//  Custom MidiKeyboard: dark gray white keys, grayish-white black keys
// =====================================================================================
class BetelMidiKeyboard : public juce::MidiKeyboardComponent
{
public:
    BetelMidiKeyboard(juce::MidiKeyboardState& state, Orientation orientation)
        : juce::MidiKeyboardComponent(state, orientation) {}

    void setSplitPoint(int noteNumber)
    {
        splitNote = noteNumber;
        repaint();
    }

    // In piano mode the whole keyboard is one melodic range — no chord zone,
    // so the bronze split tint is suppressed.
    void setPianoMode(bool p)
    {
        pianoMode = p;
        repaint();
    }

    //==========================================================================
    // THE BASS ZONE, drawn ONLY when multi split is actually on.
    //
    // `bassNote < 0` means no third zone, and every note below the main split
    // stays bronze exactly as before - which is what a player who never turns
    // multi split on must keep seeing. A purple band that appeared on a
    // two-zone keyboard would be describing a boundary that does nothing.
    //
    // The colour is Harmonizer::bassZone at the same 0x50 alpha the bronze
    // uses, so the two tints read as the same KIND of marking rather than as
    // one wash and one highlight.
    //==========================================================================
    void setBassSplitPoint(int noteNumber)
    {
        if (bassNote == noteNumber) return;
        bassNote = noteNumber;
        repaint();
    }

private:
    /** The tint for a key, or transparent when it is in the solo zone.
        One function for both key colours - a white key and a black key in the
        same zone must never disagree about which zone that is. */
    juce::Colour zoneTintFor(int midiNoteNumber) const
    {
        if (pianoMode) return juce::Colours::transparentBlack;

        if (bassNote >= 0 && midiNoteNumber < bassNote)
            return Betel::Harmonizer::bassZone().withAlpha(0.31f);

        if (midiNoteNumber < splitNote)
            return juce::Colour(Betel::Pal::kSplitZoneTint);

        return juce::Colours::transparentBlack;
    }

public:

    void drawWhiteNote(int midiNoteNumber, juce::Graphics& g,
                       juce::Rectangle<float> area,
                       bool isDown, bool isOver,
                       juce::Colour /*lineColour*/, juce::Colour /*textColour*/) override
    {
        auto col = juce::Colour(0xFF3A3A3A);
        if (isDown)       col = col.brighter(0.3f);
        else if (isOver)  col = col.brighter(0.1f);

        g.setColour(col);
        g.fillRect(area);

        if (const auto tint = zoneTintFor(midiNoteNumber); ! tint.isTransparent())
        {
            g.setColour(tint);
            g.fillRect(area);
        }

        g.setColour(juce::Colour(0xFF2A2A2A));
        g.drawRect(area, 1.0f);
    }

    void drawBlackNote(int midiNoteNumber, juce::Graphics& g,
                       juce::Rectangle<float> area,
                       bool isDown, bool isOver,
                       juce::Colour /*noteFillColour*/) override
    {
        auto col = juce::Colour(0xFFBBBBBB);
        if (isDown)       col = col.darker(0.3f);
        else if (isOver)  col = col.darker(0.1f);

        g.setColour(col);
        g.fillRect(area);

        if (const auto tint = zoneTintFor(midiNoteNumber); ! tint.isTransparent())
        {
            g.setColour(tint);
            g.fillRect(area);
        }

        g.setColour(juce::Colour(0xFF999999));
        g.drawRect(area, 1.0f);
    }

private:
    int  splitNote = 60;
    int  bassNote  = -1;        // < 0 = multi split off, no purple band
    bool pianoMode = false;
};

// =====================================================================================
//  CommentsWindow — free-floating notepad for the current set.  Text is held in
//  the processor and written out when the set/state is saved.
// =====================================================================================
class CommentsWindow : public juce::DocumentWindow
{
public:
    std::function<void(const juce::String&)> onSave;   // fired by SAVE button
    std::function<void()>                    onClose;  // fired when window closes

    explicit CommentsWindow (const juce::String& initialText)
        : juce::DocumentWindow ("Comments", juce::Colour (0xFF1A1A1A),
                                juce::DocumentWindow::closeButton)
    {
        Betel::applyGrexPopupBehaviour (*this);
        setUsingNativeTitleBar (true);
        auto* c = new Content (initialText);
        c->onSave  = [this] (const juce::String& t) { if (onSave)  onSave (t);  };
        setContentOwned (c, true);
        centreWithSize (440, 320);
        setResizable (true, false);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClose) onClose();
    }

private:
    struct Content : public juce::Component
    {
        juce::TextEditor editor;
        juce::TextButton clearBtn { "CLEAR ALL" }, saveBtn { "SAVE" };
        std::function<void(const juce::String&)> onSave;

        explicit Content (const juce::String& init)
        {
            editor.setMultiLine (true);
            editor.setReturnKeyStartsNewLine (true);
            editor.setScrollbarsShown (true);
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF101010));
            editor.setColour (juce::TextEditor::textColourId,       juce::Colours::white);
            editor.setText (init, juce::dontSendNotification);
            addAndMakeVisible (editor);

            for (auto* b : { &clearBtn, &saveBtn })
            {
                b->setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF2A2A2A));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                addAndMakeVisible (*b);
            }
            clearBtn.onClick = [this] { editor.clear(); editor.grabKeyboardFocus(); };
            saveBtn .onClick = [this] { if (onSave) onSave (editor.getText()); };
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            auto bottom = r.removeFromBottom (32);
            editor.setBounds (r.withTrimmedBottom (6));
            clearBtn.setBounds (bottom.removeFromLeft (120));
            bottom.removeFromLeft (8);
            saveBtn .setBounds (bottom.removeFromLeft (120));
        }
    };
};

// =====================================================================================
//  MainComponent
// =====================================================================================
class MainComponent : public juce::Component,
                      public  juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    //==========================================================================
    // RECORD / SONG PLAYER - moved here from MainTab.
    //
    // They live in a painted area on the LEFT PANEL now, split LEFT and RIGHT:
    // left half RECORD, right half PLAY.  That is outside MainTab's canvas, so
    // they had to become children of this component; the behaviour that drove
    // them was already here, wired through mainTab's callbacks.
    //==========================================================================

    /** Three states, and ARMED IS NOT RECORDING - the recorder waits for the
        first gesture to latch t=0, so there is a real window where the player
        thinks they are rolling and are not.  Dark red + white + "STOP" is the
        only one of the three that means tape is moving. */
    enum class RecordState { Idle = 0, Armed, Recording };

    void setRecordState (RecordState st);
    RecordState getRecordState() const noexcept { return recordState; }

    /** Lit while a song is loaded or playing, so the panel still says so with
        the song window closed. */
    void setSongActive (bool on) { ledSongPlay.setOn (on); }

    /** MainTab::setSongMode used to disable btnRecord itself.  The button is
        ours now, so the same signal comes through here. */
    void setSongModeLocksRecord (bool on) { btnRecord.setEnabled (! on); }

    explicit MainComponent(BetelgeuseProcessor& proc);
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void timerCallback() override;          // 30 Hz transport-sync poll
    const Betel::StyleData* lastSeenStylePtr = nullptr;
    juce::String            lastChordText;

    static constexpr int kDesignW = 1600;
    static constexpr int kDesignH = 853;

    BetelgeuseProcessor&     processor;
    juce::MidiKeyboardState& keyboardState;  // reference into processor

    juce::Image background;
    bool debugLayout = false;

    // ── Header animation ──────────────────────────────────────────────────────
    SpaceAnimationComponent headerAnimation;

    // HARMONY / MULTI SPLIT / MANUAL BASS, in the band the chord and style
    // readouts vacated. See FeaturePanel.h for the layout Rob specified.
    Betel::FeaturePanel featurePanel;

    // The CHORD and STYLE readouts sit over this animation. NO PLATE BEHIND
    // THEM: their frames are painted into main_back.png, so anything drawn
    // here would sit on top of Rob's artwork and hide it. Safe to draw
    // straight over: SpaceAnimationComponent is setOpaque(false) and its
    // paint() only adds stars and orbs, never a background fill.

    // ── Left panel controls ───────────────────────────────────────────────────
    RectangleKnob         knobTempo { "TEMPO", 30.0, 300.0, 120.0, 1.0 };
    SelectorGroup         selectorSpeed { {"x1","x2","x1/2"}, 0 };
    SelectorGroup         selectorTempoType { {"FREE","SYNCED"}, 0 };

    BlinkButton           btnTap        { "TAP" };
    BlinkButton           btnResetTempo { "RESET\nTEMPO" };

    // Shares row 2 with TAP and RESET TEMPO, which gave up width for it.  See
    // MidiLinkCanvas.h for why assignment starts from a live source rather than
    // from a typed number.
    Betel::MidiLinkCanvas midiLinkCanvas;

    SelectorGroup         selectorArrangerPiano { {"ARRANGER","PIANO"}, 0 };
    SelectorGroup         selectorSingleMulti   { {"SINGLE","MULTI"}, 0 };
    SelectorGroup         selectorTransition    { {"1/2","1/4","1/8"}, 0 };
    SelectorGroup         selectorFillLength    { {"1","1/2"}, 0 };

    // FINISHER editor — created lazily the first time the mixer's FINISHER
    // button is pressed, like SoundsTab's drumsPopup.
    std::unique_ptr<FinisherWindow> finisherWindow;
    void openFinisherWindow();
    void refreshFinisherWindow();

    // NO CAPTION and a STEPPER, not a knob.  The empty label is deliberate: the
    // artwork carries the wording now, and setStepperMode would not draw it.
    // +/-11 rather than +/-12: at 12 you are back on the same note an octave
    // away, so the twelfth step only duplicated what 0 already gave.  Matches
    // BetelgeuseProcessor::kMaxTranspose, which is what the CC and the remote
    // both scale to - three routes, one limit.
    RectangleKnob         knobGlobalSemitone { "", -11.0, 11.0, 0.0, 1.0 };

    // ENERGY — style dynamics, in the strip the features row vacated.
    EnergySlider          energySlider;
    BetelTextLabel        lblPlayedChord     { "C", 20.0f };

    // 19.5 = 13 x 1.5.  This is the one line on the panel that answers "what am
    // I playing", so it reads from across a room rather than matching the
    // captions around it.

    BlinkButton           btnLoadSet  { "LOAD\nSET" };
    BlinkButton           btnSaveSet  { "SAVE\nSET" };
    // RE-LOAD became FAST SAVE.  Re-loading the last set reverted unsaved
    // edits, which is a rescue for a mistake; saving them without a dialog is
    // something you want after every good one.  The second is worth a front-
    // panel button, the first was not.
    BlinkButton           btnFastSave { "FAST\nSAVE" };

    // Set file I/O (LOAD SET / SAVE SET / FAST SAVE).
    std::unique_ptr<juce::FileChooser> setChooser;
    juce::File                         lastSetFile;

    /** The set that belongs to a style: <root>/sets/<genre>/<style name>.bset,
        mirroring the styles tree exactly.  Selecting a style loads THIS rather
        than the raw style file whenever it exists. */
    juce::File setFileForStyle (const juce::String& styleAbsolutePath) const;

    /** Grey out the variation buttons the LOADED style has no section for.
        Called after every style load; see the definition for the mapping. */
    void refreshVariationAvailability();

    /** Write the current state to a .bset.  Shared by SAVE SET and FAST SAVE so
        the two can never drift into saving different things. */
    bool writeSetToFile (const juce::File& file);

    /** Pull every mixer control back from the processor after a set load.  The
        per-slot faders are re-synced by the timer when the style pointer moves,
        but the buses, the master, the boost and the Finisher switch are not
        polled by anything — without this they would sit at the previous song's
        positions while the engine ran the new one's. */
    void refreshMixerFromProcessor();

    /** Pull every crash control back from the processor after a set load. */
    void refreshCrashFromProcessor();

    /** The name the left panel shows for what is playing: the SET's file
        name, falling back to the style FILE's — never the machine-generated
        string stored inside the style. */
    juce::String currentSetDisplayName() const;

    /** The set-pack baker.  Owned here because it outlives a single button
        press — it steps through the library on a timer so the editor stays
        responsive.  Constructed with the processor's bank map, which is what
        resolves a style's slot programs. */
    std::unique_ptr<Betel::SetBaker> setBaker;
    void applySetFromFile (const juce::File& file);
    void applySetPayload  (const juce::ValueTree& payload);



    juce::ValueTree buildSetSnapshot();

    BlinkSeparatorButton  btnComments  { "COMMENTS" };
    BlinkSeparatorButton  btnForgetAll { "ORIENTAL\nSCALE" };   // quick Arabic-scale editor
    DawStartButton        btnDawStart  { "DAW\nSTART" };

    BetelTextLabel        lblSetName { "No Set", 13.0f };

    // ── Global macros ─────────────────────────────────────────────────────────
    // BIG DRUMS IS GONE, and FUNKEY MODE moved to MainTab's play-control row -
    // it is a performance switch you reach for mid-song, not a settings toggle,
    // and the two artwork frames under the set manager are free again.
    //
    // What is left here is the relight: MainTab and the GLOBAL SETTINGS page
    // both carry the switch, so one function pushes the flag out to both and
    // neither can drift.
    void refreshMacroButtons();

    /** Put FUNKEY MODE where the given UiState says, defaulting OFF when it does
        not say — then relight the button and make the result audible.  Pass an
        invalid tree to mean "nothing declares it", which is a bare style load.
        See the definition for why the default is false rather than the current
        value. */
    void applyMacroEngagedState (const juce::ValueTree& uiState);

    // ── Right panel ───────────────────────────────────────────────────────────
    // NINE tabs since TUTORIAL was added.  Nothing else needs touching for a
    // new tab: resized() divides the selector canvas by kNumTabs in float math,
    // so the nine buttons redistribute evenly by themselves, and selectTab
    // works off the same count.  TUTORIAL goes LAST on purpose — appending
    // leaves every existing tab index where it was.
    static constexpr int kNumTabs = 8;
    std::array<TabButton, kNumTabs> tabButtons;

    // MASTER VOLUME, on the right end of the tab bar.  THE SAME VALUE as the
    // mixer's master fader, not a copy: both write the processor and each pushes
    // the other SILENTLY, so a move on either cannot bounce back and re-enter
    // its own callback.  Deliberately NOT attached to RemoteId::MasterVolume -
    // the mixer fader already owns that id, and one id must mean one widget.
    RectangleKnob         knobMasterVol { "MASTER", 0.0, 200.0, 100.0, 1.0 };
    int activeTabIndex = 0;

    juce::Component tabContentContainer;
    MainTab      mainTab;
    StylesTab    stylesTab;
    SoundsTab    soundsTab;
    MixerTab     mixerTab;
    JumpsTab     jumpsTab;
    CrashTab     crashTab;
    SettingsTab  settingsTab;
    TutorialTab  tutorialTab;

    // ── SET EDITOR IS A PAGE OF SettingsTab NOW, NOT A TAB ───────────────────
    //
    // It is no longer a member here and no longer appears in kTabNames or
    // tabPages; SettingsTab constructs and lays it out.
    //
    // THIS REFERENCE EXISTS SO THE WIRING DID NOT HAVE TO MOVE.  MainComponent
    // owns every callback on that page - the baker, the style-name provider,
    // the levels hook - because they need the processor and the style library,
    // which a settings page has no business knowing about.  Binding the name to
    // the page's real home keeps all of that code reading exactly as it did,
    // and a reference cannot drift out of sync the way a second instance would.
    //
    // DECLARED AFTER settingsTab ON PURPOSE: members initialise in declaration
    // order, so binding this any earlier would reference a tab that has not been
    // constructed yet.
    SetEditorTab& setEditorTab { settingsTab.getSetEditorTab() };

    // The GLOBAL EFFECTS window.  One instance serving both hands — it is
    // retargeted by setSection() on open rather than duplicated, so the two
    // hands cannot drift apart in size, page or position.
    std::unique_ptr<GlobalEffectsWindow> globalFxWindow;
    std::array<juce::Component*, kNumTabs> tabPages {};

    // ── MIDI song transport (record / play) ───────────────────────────────────
    GrexTransportBar transportBar;
    GrexSongRecorder::Setup captureSongSetup() const;   // snapshot for arm()

    //==========================================================================
    // SONG WINDOW.  Always on top and it DISABLES THE MAIN GUI while open -
    // song mode owns the band, so leaving the panel clickable underneath would
    // let a click change something the song is about to replay over.
    //
    // The MIDI keyboard is untouched by that: input is handled processor-side
    // and never passes through this component, so the right hand stays live
    // exactly as a backing track needs.
    //==========================================================================
    void openSongWindow();
    void closeSongWindow();
    void promptSaveSong();
    void refreshRecordButton();

    /** PLAY was pressed with no style loaded: load one and start.
        Drains BetelgeuseProcessor::consumePlayRefusedNoStyle - see the comment
        block on togglePlayStop for why the recovery lives on this side. */
    void recoverNoStyleAndPlay();

    std::unique_ptr<juce::DocumentWindow> songWindow;
    std::unique_ptr<juce::FileChooser>    songChooser;
    void applySongSetup (const GrexSongRecorder::Setup&); // restore before play

    // ── Style search ─────────────────────────────────────────────────────────
    // A real top-level window, so it can take keyboard input the plugin editor
    // cannot get from the host. Created on demand by the tab's SEARCH button and
    // destroyed by its own X — see the wiring in the constructor.
    std::unique_ptr<Betel::StyleSearchWindow> styleSearchWindow;

    BetelMidiKeyboard keyboardComponent;
    SplitKnob         knobSplit { 36.0, 127.0, 60.0 };

    //==========================================================================
    // MIDI ASSIGNMENT.  The hub owns nothing but listeners; the popup is created
    // per opening and handed to a CalloutBox, which owns and deletes it.
    //==========================================================================
    Betel::RemoteAssignHub remoteHub;

    /** The browser's label for a style ref - from the blob TOC, not from the
        ref itself, which is an id and not a path. */
    juce::String displayNameForStyleRef (const juce::String& ref) const;

    void attachRemoteControls();
    void openAssignPopup (Betel::RemoteId id, juce::Component& over,
                          Betel::RemoteMap::Kind k, int number);
    void saveRemoteMap() const;

    /** The MIDI-assign file menu: named maps live beside the auto-saved one so
        one rig can keep a map per controller. */
    void showMidiMapMenu();
    juce::File midiMapsFolder() const;
    void promptSaveMidiMap();
    void promptLoadMidiMap();
    void promptUnlearnAllMidi();
    void setArmedMidiSource (Betel::RemoteMap::Kind k, int number);

    // The TAP-TO-ARM source, for touch screens where dragging across the window
    // is awkward.  Kind::None means nothing is armed.
    Betel::RemoteMap::Kind armedKind   = Betel::RemoteMap::Kind::None;
    int                    armedNumber = -1;
    std::unique_ptr<juce::FileChooser> midiMapChooser;
    HoldButton        btnHold;
    RegistrationLED   ledRegistration;
    // THE STATUS FLAG UNDER THE LAMP, and the line that says what to do about
    // it.  A lamp alone answers "is it registered" only for someone who already
    // knows the lamp is about registration; the words are what make an
    // unregistered install self-explaining.  lblRegHint is shown ONLY when the
    // answer is no - a permanent "you are registered, register here" is noise.
    juce::Label       lblReg;        // REGISTERED / NOT REGISTERED
    juce::Label       lblRegHint;    // where to go when it is the latter
    FolderLocatorLED  ledFolderLocator;
    juce::TextButton  btnLocateFolder;

    // ── Helpers ───────────────────────────────────────────────────────────────
    /** Push RegistrationManager's answer into the lamp and the two labels.
        Called at construction and after any attempt to register, so the header
        never disagrees with the licence file. */
    void  refreshRegistrationIndicator();
    float getUiScale() const;
    juce::Rectangle<int> getUiTargetRect() const;
    juce::Rectangle<int> scaleRect(const juce::Rectangle<int>& r) const;
    // Float variant — keeps sub-pixel design coordinates exact until the single
    // rounding at the end, instead of truncating them at the call site.
    juce::Rectangle<int> scaleRectF(const juce::Rectangle<float>& r) const;

    void selectTab(int index);
    // Quick Arabic/oriental scale editor (ORIENTAL SCALE button) -- pops the
    // ArabicScaleKeyboard in its own window; edits apply to all solo channels.
    std::unique_ptr<juce::DocumentWindow> arabicQuickWin;

    // The two halves of the left-panel record/play area, plus their lamps.
    // The buttons are INVISIBLE - pill, captions and divider are all in
    // back.png - so all the state lives on the two LEDs.
    InvisibleButton  btnRecord, btnSongPlayer;
    SongRecordLED    ledSongRecord;
    SongPlayLED      ledSongPlay;
    RecordState      recordState = RecordState::Idle;
    void toggleArabicQuickEditor();
    void drawDebugOverlay(juce::Graphics& g);

    // ── Styles browser state ──────────────────────────────────────────────────
    // Selecting a style cell caches its absolute path here; LOAD / RELOAD load
    // it via processor.loadStyle.
    juce::String pendingStylePath;

    // Scan <root>/styles sub-folders into the genre + style grids.
    void rescanStyleBrowser();
    // Rebuilds the reference-voice pill library from the engine's per-instrument
    // flag library (Reference = flags >= 200, GM = flags 0..127).
    void refreshReferenceLibrary();

    std::unique_ptr<CommentsWindow> commentsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
