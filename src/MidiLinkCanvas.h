
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme

#include <JuceHeader.h>
#include "RemoteMap.h"

namespace Betel
{

//==============================================================================
// MidiLinkCanvas.h  -  the DRAG SOURCE for MIDI assignment.
//==============================================================================
//
// WHAT IT REPLACES.  The old assign popup asked you to TYPE a CC or note
// number, with LEARN beside it as a rescue.  Both were wrong the same way:
// typing needs the keyboard and a plugin editor does not reliably own the
// keyboard - the DAW does - and LEARN was a modal arm-and-wait that swallowed
// every MIDI message while armed, so forgetting to disarm made the plugin look
// deaf with nothing on screen to explain why.
//
// HOW IT WORKS NOW.  Move any control on the hardware.  Its number appears
// here.  Drag it onto any control in Grex and drop it.  No keyboard anywhere in
// the loop and nothing that can be left armed.
//
// ONE SOURCE AT A TIME, SAMPLE AND HOLD.  Whatever arrived last is what is
// shown and what gets dragged; the next message replaces it.  A queue of recent
// sources was tried and dropped - with six chips on a 123 px panel the question
// "which one am I dragging" is a question the panel should never make you ask.
// Hold means it does NOT clear itself, so you can move a knob, take your hand
// off the hardware, and the source is still there when you reach the mouse.
//
// TOUCH.  Dragging across the window with a finger is awkward, so the panel can
// also be TAPPED to arm the source; the next tap on any control assigns it.
//==============================================================================
class MidiLinkCanvas : public juce::Component,
                       private juce::Timer
{
public:
    /** Polled for the processor's packed last-MIDI word.  Supplied by the
        editor so this class needs no processor reference of its own. */
    std::function<int()> getLastMidiPacked;

    /** The panel was TAPPED rather than dragged: arm the held source, and let
        the next control tap take it.  Kind::None means "disarm". */
    std::function<void (RemoteMap::Kind, int)> onSourceArmed;

    /** Right-click: the save / load / clear map menu.  It lives here because
        this panel IS the MIDI-assign feature, so it is where a user would look
        for its files. */
    std::function<void()> onMenuRequested;

    MidiLinkCanvas()
    {
        setInterceptsMouseClicks (true, false);
        startTimerHz (25);
    }

    /** Host -> panel: is the held source currently armed by tap? */
    void setArmedSource (RemoteMap::Kind k, int number)
    {
        const bool a = (k != RemoteMap::Kind::None && k == heldKind && number == heldNumber);
        if (a == armed) return;
        armed = a;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xFF141414));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (armed ? juce::Colour (Betel::Pal::kAccentDeep)
                           : juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, armed ? 1.5f : 1.0f);

        auto body = getLocalBounds().reduced (3);

        // ABOVE the value.
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawFittedText ("midi control link", body.removeFromTop (13),
                          juce::Justification::centred, 1);

        // BELOW it.  Taken off the bottom before the value is drawn so the
        // value always gets the whole middle, whatever the panel height is.
        auto hintR = body.removeFromBottom (13);
        g.drawFittedText ("drag & drop to any control", hintR,
                          juce::Justification::centred, 1);

        // THE VALUE.
        const bool have = (heldKind != RemoteMap::Kind::None && heldNumber >= 0);
        g.setColour (have ? juce::Colours::white.withAlpha (armed ? 1.0f : 0.9f)
                          : juce::Colours::white.withAlpha (0.25f));
        g.setFont (juce::Font (juce::FontOptions (have ? 19.0f : 15.0f)));
        g.drawFittedText (have ? label() : "--", body, juce::Justification::centred, 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            dragStarted = true;                  // suppress tap-to-arm below
            if (onMenuRequested) onMenuRequested();
            return;
        }
        dragStarted = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // 6 px separates a drag from a tap on a touch screen, where a tap
        // always travels a little.
        if (dragStarted || heldKind == RemoteMap::Kind::None) return;
        if (e.getDistanceFromDragStart() < 6) return;

        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            // The payload is the SOURCE, not the control: the drop target knows
            // which control it belongs to, and this knows nothing about controls
            // at all. Keeps the two halves independent.
            juce::var payload (new juce::DynamicObject());
            payload.getDynamicObject()->setProperty ("midiLinkKind",   (int) heldKind);
            payload.getDynamicObject()->setProperty ("midiLinkNumber", heldNumber);

            dragStarted = true;
            c->startDragging (payload, this);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragStarted || heldKind == RemoteMap::Kind::None) return;
        if (onSourceArmed)
            onSourceArmed (armed ? RemoteMap::Kind::None : heldKind,
                           armed ? -1 : heldNumber);
    }

private:
    juce::String label() const
    {
        return (heldKind == RemoteMap::Kind::Pad ? "PAD " : "CC ") + juce::String (heldNumber);
    }

    void timerCallback() override
    {
        if (! getLastMidiPacked) return;

        const int packed = getLastMidiPacked();
        if (packed == lastPacked) return;        // nothing new since the last tick
        lastPacked = packed;

        // seq(16) | kind(2) | number(7) | value(7) - see noteLastMidi in Main.h.
        const int kind   = (packed >> 14) & 0x3;
        const int number = (packed >>  7) & 0x7F;
        if (kind != (int) RemoteMap::Kind::Pad && kind != (int) RemoteMap::Kind::Cc)
            return;

        if (heldKind == (RemoteMap::Kind) kind && heldNumber == number)
            return;                              // same source still moving

        // A NEW SOURCE REPLACES THE OLD AND DISARMS.  Arming is a promise about
        // one specific control; silently re-pointing it at whatever moved last
        // would assign something the player never chose.
        heldKind   = (RemoteMap::Kind) kind;
        heldNumber = number;

        if (armed)
        {
            armed = false;
            if (onSourceArmed) onSourceArmed (RemoteMap::Kind::None, -1);
        }
        repaint();
    }

    RemoteMap::Kind heldKind   = RemoteMap::Kind::None;
    int             heldNumber = -1;

    int  lastPacked  = 0;
    bool dragStarted = false;
    bool armed       = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLinkCanvas)
};

} // namespace Betel




