

#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme

#include <JuceHeader.h>
#include "RemoteMap.h"

namespace Betel
{

//==============================================================================
// ASSIGN POPUP - opened by DROPPING a MIDI source onto a control.
//==============================================================================
//
// WHAT THIS USED TO BE, AND WHY IT IS NOT THAT ANY MORE.  The old panel asked
// you to type a CC or note number, with a LEARN button beside it.  Both are
// gone:
//
//   - THE TEXT BOX needed the keyboard, and a plugin editor does not reliably
//     own the keyboard.  The DAW does.  That is the whole "I'm typing and it's
//     controlling the DAW" report, and no amount of grabKeyboardFocus fixes a
//     host that never yields.
//   - LEARN was a modal arm-and-wait that swallowed every MIDI message while it
//     was armed.  Leaving it armed made the plugin look deaf, with nothing on
//     screen to explain why.
//   - THE PAD / CC SELECTOR is now redundant: the source arrives WITH its kind
//     attached, because it came from a message the hardware actually sent.
//
// So the source is settled before this window ever opens, and the only question
// left is HOW it should drive the control - which is three buttons and a
// cancel.  See RemoteMap::Mode for what the three mean.
//==============================================================================
class AssignPopup : public juce::Component
{
public:
    /** Fires when the assignment changes, so the host can save the map. */
    std::function<void()> onMapChanged;

    /** Close me.  The host owns the CallOutBox, so it does the dismissing. */
    std::function<void()> onCloseRequested;

    static constexpr int kW = 218;
    static constexpr int kH = 196;

    AssignPopup()
    {
        auto initLabel = [this] (juce::Label& l, float size, float alpha,
                                 juce::Justification j)
        {
            l.setJustificationType (j);
            l.setColour (juce::Label::textColourId,
                         juce::Colours::white.withAlpha (alpha));
            l.setFont (juce::Font (juce::FontOptions (size)));
            addAndMakeVisible (l);
        };

        initLabel (title,  15.0f, 0.95f, juce::Justification::centred);
        initLabel (source, 17.0f, 1.00f, juce::Justification::centred);
        initLabel (hint,   11.0f, 0.55f, juce::Justification::centred);

        // X TOP-LEFT, as asked for originally and kept: it is the one control
        // whose position should never move between panels.
        btnClose.setButtonText ("X");
        btnClose.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible (btnClose);

        auto initMode = [this] (juce::TextButton& b, const juce::String& t,
                                RemoteMap::Mode m)
        {
            b.setButtonText (t);
            b.setClickingTogglesState (false);
            b.onClick = [this, m] { applyMode (m); };
            addAndMakeVisible (b);
        };
        initMode (btnPush,   "PUSH",   RemoteMap::Mode::Push);
        initMode (btnToggle, "TOGGLE", RemoteMap::Mode::Toggle);
        initMode (btnKnob,   "KNOB",   RemoteMap::Mode::Knob);

        // CLEAR IS ALSO UN-ASSIGN, and it always was.  Kept because the only
        // other way to free a control would be to drop something else on it,
        // which is not the same thing.
        btnClear.setButtonText ("CLEAR");
        btnClear.onClick = [this]
        {
            RemoteMap::get().clear (target);
            refresh();
            if (onMapChanged) onMapChanged();
        };
        addAndMakeVisible (btnClear);

        btnCancel.setButtonText ("CANCEL");
        btnCancel.onClick = [this]
        {
            // CANCEL PUTS BACK WHAT WAS THERE BEFORE THE DROP.  A drop is a
            // destructive act - it can evict whatever else owned that source -
            // so "cancel" has to mean the map is as it was, not merely that the
            // window closed.
            RemoteMap::get().assign (target, entryKind, entryNumber, entryMode);
            if (onMapChanged) onMapChanged();
            if (onCloseRequested) onCloseRequested();
        };
        addAndMakeVisible (btnCancel);

        setSize (kW, kH);
    }

    /** Point the panel at a control that has just had `k`/`number` dropped on
        it.  The assignment is made IMMEDIATELY - the window is here to choose
        the mode and to offer a way back, not to confirm the drop. */
    void openFor (RemoteId r, RemoteMap::Kind k, int number)
    {
        target = r;

        // Remember the PREVIOUS state first: CANCEL restores exactly this.
        const auto prev = RemoteMap::get().get (r);
        entryKind   = prev.kind;
        entryNumber = prev.number;
        entryMode   = prev.mode;

        if (k != RemoteMap::Kind::None && number >= 0)
            RemoteMap::get().assign (r, k, number, RemoteMap::defaultModeFor (r));

        refresh();
        if (onMapChanged) onMapChanged();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF1A1A1A));
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawRect (getLocalBounds(), 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);

        auto top = r.removeFromTop (22);
        btnClose.setBounds (top.removeFromLeft (22));
        title.setBounds (top);

        r.removeFromTop (4);
        source.setBounds (r.removeFromTop (24));
        hint  .setBounds (r.removeFromTop (14));
        r.removeFromTop (6);

        // The three modes get one row each: they are the reason the panel
        // exists, and a row of three 60 px buttons reads worse than three full
        // width ones at a glance.
        const int rowH = 26;
        btnPush  .setBounds (r.removeFromTop (rowH)); r.removeFromTop (3);
        btnToggle.setBounds (r.removeFromTop (rowH)); r.removeFromTop (3);
        btnKnob  .setBounds (r.removeFromTop (rowH)); r.removeFromTop (6);

        auto bottom = r.removeFromTop (rowH);
        btnClear .setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 3));
        btnCancel.setBounds (bottom.removeFromRight (bottom.getWidth() - 3));
    }

private:
    void applyMode (RemoteMap::Mode m)
    {
        // A mode with nothing assigned would be a setting with no subject.
        if (RemoteMap::get().get (target).kind == RemoteMap::Kind::None) return;
        RemoteMap::get().setMode (target, m);
        refresh();
        if (onMapChanged) onMapChanged();
    }

    void refresh()
    {
        const auto a = RemoteMap::get().get (target);

        title .setText (remoteLabel (target), juce::dontSendNotification);
        source.setText (RemoteMap::get().describe (target), juce::dontSendNotification);

        // KNOB IS NOT OFFERED ON A BUTTON AND PUSH/TOGGLE NOT ON A FADER.
        // Offering a setting that cannot work is worse than offering fewer:
        // a note has no value to give a fader, and a fader has no press.
        const bool continuous = remoteIsContinuous (target);
        btnPush  .setEnabled (! continuous);
        btnToggle.setEnabled (! continuous);
        btnKnob  .setEnabled (continuous);

        const auto mark = [&a] (RemoteMap::Mode m)
        {
            return a.kind != RemoteMap::Kind::None && a.mode == m;
        };
        auto tint = [this] (juce::TextButton& b, bool on)
        {
            b.setColour (juce::TextButton::buttonColourId,
                         on ? juce::Colour (Betel::Pal::kAccentDeep) : juce::Colour (0xFF2A2A2A));
        };
        tint (btnPush,   mark (RemoteMap::Mode::Push));
        tint (btnToggle, mark (RemoteMap::Mode::Toggle));
        tint (btnKnob,   mark (RemoteMap::Mode::Knob));

        hint.setText (continuous ? "continuous control"
                                 : "PUSH = while held   TOGGLE = latching",
                      juce::dontSendNotification);
        repaint();
    }

    RemoteId        target      = RemoteId::kNumRemoteIds;
    RemoteMap::Kind entryKind   = RemoteMap::Kind::None;
    int             entryNumber = -1;
    RemoteMap::Mode entryMode   = RemoteMap::Mode::Toggle;

    juce::Label      title, source, hint;
    juce::TextButton btnClose, btnPush, btnToggle, btnKnob, btnClear, btnCancel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssignPopup)
};

//==============================================================================
// THE DROP TARGET.
//
// One invisible child per assignable control, parented to the control so it
// moves and resizes with it for free.  The widget classes stay untouched - five
// unrelated ones with no common base beyond juce::Component - which is what
// keeps the wiring at one line per control and stops this drifting out of step
// with them.
//
// hitTest() IS THE WHOLE TRICK, and it is the same one the old right-click
// shield used, but now with an honest condition.  A component that is present
// answers hitTest true; this one answers true only while a MIDI source is
// actually looking for a home:
//
//     drag in progress, or a chip armed by tap -> present, catches the drop
//     any other time                           -> absent, and every left click,
//                                                 drag, hover and double-click
//                                                 reaches the control unchanged
//
// The old shield lied whenever the right button was down, which is why a right
// click anywhere in the plugin went nowhere.  This one is invisible unless you
// are mid-assignment.
//==============================================================================
class RemoteDropTarget : public juce::Component,
                         public juce::DragAndDropTarget
{
public:
    std::function<void (RemoteMap::Kind, int)> onSourceDropped;
    std::function<bool()>                      isArmed;

    /** RIGHT DOUBLE-CLICK on the control: open the popup with no new source, to
        change the mode or to CLEAR the assignment.  Without this there is no way
        to un-assign anything at all - dropping is the only other route in, and
        a drop can only ever add. */
    std::function<void()> onEditRequested;

    RemoteDropTarget()
    {
        setInterceptsMouseClicks (true, false);
        setWantsKeyboardFocus (false);
    }

    ~RemoteDropTarget() override
    {
        if (auto* p = getParentComponent())
            p->removeMouseListener (this);
    }

    /** Listen on the PARENT rather than hit-testing for it.
        The old shield made hitTest return true whenever the right button was
        down, which is why a right click anywhere in the plugin went nowhere: a
        component that claims the hit consumes the whole gesture. A mouse
        listener SEES the parent's events without taking them, so the control
        keeps working normally and only the right double-click is acted on. */
    void listenTo (juce::Component& parent)
    {
        parent.removeMouseListener (this);   // idempotent - attach() may re-point
        parent.addMouseListener (this, false);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown() && onEditRequested)
            onEditRequested();
    }

    bool hitTest (int, int) override
    {
        if (isArmed && isArmed()) return true;

        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
            return c->isDragAndDropActive();

        return false;
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.isObject()
            && d.description.getDynamicObject()->hasProperty ("midiLinkKind");
    }

    void itemDragEnter (const SourceDetails&) override { over = true;  repaint(); }
    void itemDragExit  (const SourceDetails&) override { over = false; repaint(); }

    void itemDropped (const SourceDetails& d) override
    {
        over = false;
        repaint();

        if (! isInterestedInDragSource (d)) return;

        auto* o = d.description.getDynamicObject();
        const auto k = (RemoteMap::Kind) (int) o->getProperty ("midiLinkKind");
        const int  n = (int) o->getProperty ("midiLinkNumber");
        if (onSourceDropped) onSourceDropped (k, n);
    }

    /** The TOUCH path: a chip was tapped to arm it, this control was then
        tapped, and hitTest let the tap land here instead of on the control. */
    void mouseUp (const juce::MouseEvent&) override
    {
        if (isArmed && isArmed() && onSourceDropped)
            onSourceDropped (RemoteMap::Kind::None, -1);   // host reads the armed source
    }

    void mouseDown (const juce::MouseEvent&) override {}
    void mouseDrag (const juce::MouseEvent&) override {}

    void paint (juce::Graphics& g) override
    {
        // THE DASHED "WAITING FOR A TARGET" OUTLINE IS GONE.
        //
        // It drew an amber dashed rounded-rect on EVERY attached control the
        // whole time a source was armed.  Removed on request: it read as visual
        // noise across the main page and the left panel, and it could not sit
        // symmetrically on its control - this overlay is fitted to the parent
        // and the stroke was inset 1 px then drawn 1.2 px wide, so half of it
        // fell outside the inset and the weight looked different on each edge.
        //
        // WHAT IT WAS FOR, so the reasoning is not lost: while a source is
        // armed every one of these overlays hit-tests true, so every attached
        // control stops responding to ordinary clicks.  The outline existed so
        // that state could never be entered invisibly.
        //
        // The HOVER highlight below is deliberately KEPT and now carries that
        // job alone: point at any attached control while armed and it lights
        // amber, so the modal state still announces itself the moment you go
        // near a target - just on the one control under the pointer instead of
        // on all of them at once.  The MIDI link canvas also still shows which
        // chip is armed.
        if (! over) return;
        g.setColour (juce::Colour (Betel::Pal::kAccentDeep).withAlpha (0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        g.setColour (juce::Colour (Betel::Pal::kAccentDeep));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.5f);
    }

    void parentSizeChanged()      override { fitToParent(); }
    void parentHierarchyChanged() override { fitToParent(); }

private:
    void fitToParent()
    {
        if (auto* p = getParentComponent())
        {
            setBounds (p->getLocalBounds());
            toFront (false);
        }
    }

    bool over = false;
};

//==============================================================================
// THE ATTACH MECHANISM.  Signature unchanged from the right-click version, so
// every existing attach() call site in MainTab / MixerTab / StyleLevelsPanel /
// SetEditorTab / MainComponent keeps working untouched - only the class behind
// them changed.
//==============================================================================
class RemoteAssignHub
{
public:
    /** Opens the popup over `c`.  Kind::None / -1 means EDIT: no new source,
        just show what is assigned so it can be re-moded or cleared. */
    std::function<void (RemoteId, juce::Component&, RemoteMap::Kind, int)> onAssignRequested;

    /** Is a chip currently armed by tap?  Supplied by the editor. */
    std::function<bool()>                     isArmed;
    /** What is armed, for the tap path. */
    std::function<std::pair<RemoteMap::Kind,int>()> getArmedSource;

    void attach (juce::Component& c, RemoteId r)
    {
        for (auto* t : targets)
            if (t->getParentComponent() == &c) { point (*t, c, r); return; }

        auto* t = targets.add (new RemoteDropTarget());
        c.addAndMakeVisible (t);
        t->setBounds (c.getLocalBounds());
        t->toFront (false);
        t->listenTo (c);
        point (*t, c, r);
    }

private:
    void point (RemoteDropTarget& t, juce::Component& c, RemoteId r)
    {
        t.isArmed = [this] { return isArmed && isArmed(); };

        t.onSourceDropped = [this, &c, r] (RemoteMap::Kind k, int n)
        {
            // A tap arrives with no payload: take the armed source instead.
            if (k == RemoteMap::Kind::None && getArmedSource)
            {
                const auto a = getArmedSource();
                k = a.first;
                n = a.second;
            }
            if (onAssignRequested) onAssignRequested (r, c, k, n);
        };

        t.onEditRequested = [this, &c, r]
        {
            if (onAssignRequested)
                onAssignRequested (r, c, RemoteMap::Kind::None, -1);
        };
    }

    /** Repaint every target, so the armed outline appears and clears
        everywhere at the same moment rather than on whatever happens to be
        redrawn next. */
public:
    void repaintTargets()
    {
        for (auto* t : targets) t->repaint();
    }

private:
    juce::OwnedArray<RemoteDropTarget> targets;
};

} // namespace Betel




