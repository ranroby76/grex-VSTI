#pragma once
#include <JuceHeader.h>

// =============================================================================
//  GrexPopupWindow.h
//
//  ONE BEHAVIOUR FOR EVERY WINDOW THE PLUGIN POPS UP.
//
//  Grex opens nine of them -- the instrument editor, the drum kit editor, the
//  Finisher, Style Data, Comments, the oriental scale keyboard, and the three
//  macro FX windows -- and each was built with its own constructor arguments.
//  They therefore behaved differently from one another for no reason anybody
//  chose, and they all shared two problems:
//
//    * THEY SANK BEHIND THE HOST.  A DocumentWindow opened from a plugin is a
//      sibling of the DAW's window, not a child of it.  Click the DAW's
//      arrangement, or its own plugin rack, and the editor you were working in
//      goes behind it -- indistinguishable from having vanished, and the
//      instinctive fix (press EDIT again) closes it instead of raising it.
//
//    * THERE WAS NOWHERE TO PUT THEM.  Every window was built with
//      DocumentWindow::closeButton alone, so the only way to clear the screen
//      was to close the window and lose your place in it.
//
//  applyGrexPopupBehaviour fixes both, in the two lines that matter:
//
//    * setAlwaysOnTop -- the window stays above the host, so it disappears
//      only when the user closes it or minimises it.
//    * a MINIMISE BUTTON beside the close button, so "get this off my screen"
//      and "throw this away" stop being the same gesture.
//
//  WHY MINIMISE IS SAFE HERE, since minimising is the one thing that looks
//  like the disappearance this file exists to prevent: JUCE's TopLevelWindow
//  puts ComponentPeer::windowAppearsOnTaskbar in its desktop style flags by
//  default, so a minimised popup has a real taskbar button and comes back with
//  one click.  It is gone because it was SENT away, and it is one click from
//  returning -- which is the distinction that matters.
//
//  Call it from the constructor, BEFORE the window is first made visible: the
//  button set is baked into the peer's style flags when the window is added to
//  the desktop, so changing it afterwards would need the peer recreating.
// =============================================================================
namespace Betel
{
    inline void applyGrexPopupBehaviour (juce::DocumentWindow& w)
    {
        w.setTitleBarButtonsRequired (juce::DocumentWindow::minimiseButton
                                    | juce::DocumentWindow::closeButton,
                                      false);

       #if ! JUCE_MAC
        w.setAlwaysOnTop (true);
       #endif

        //======================================================================
        // WHY macOS DOES NOT GET THE TOPMOST FLAG, AND WHY THAT IS NOT A
        // REGRESSION THERE.
        //
        // The whole justification above is Windows behaviour.  On Windows a
        // top-level window is its own thing in the Z order, so clicking the DAW
        // buries the editor and setAlwaysOnTop is the only way to keep it.
        //
        // macOS raises windows BY APPLICATION.  Every window the plugin opens
        // belongs to the host's process, so clicking the host brings the whole
        // set forward together and the editor comes with it.  The problem the
        // flag solves does not exist there.
        //
        // What the flag WOULD cost on macOS is worse than what it buys, twice:
        //
        //   * a floating window sits above EVERY application, not just the
        //     host.  Cmd-Tab to Safari and the drum editor is still pinned over
        //     it, which on a Mac reads as a plugin that has lost its mind.
        //
        //   * AppKit will not miniaturise a window whose level is above
        //     NSNormalWindowLevel.  Press the minimise button and the window
        //     either ignores it or goes away WITHOUT landing in the Dock - and
        //     unlike Windows there is no taskbar to fetch it back from.  That
        //     is precisely the vanishing this file exists to prevent, caused by
        //     this file's own fix.
        //
        // So the minimise button is kept on every platform and the topmost flag
        // is Windows/Linux only.  Read the taskbar paragraph above as saying
        // "taskbar or Dock" - the Dock only receives the window because it is
        // at a normal level, which is the point of the guard.
        //======================================================================
    }

    //==========================================================================
    // OPEN CENTRED, BUT NEVER BIGGER THAN THE SCREEN.
    //
    // Every popup called centreWithSize with a fixed pair of numbers chosen on
    // a 1600x853 Windows desktop, and none of them checked whether the machine
    // could show it.  On the laptops this plugin will actually meet on macOS
    // that overflows: a 13" MacBook Air is 1280x800 POINTS, and the drum editor
    // asks for 1400x710 - wider than the whole screen, so it opens hanging off
    // both edges with its own resize corner off-screen too.  The instrument
    // editor's 1280x792 is the same story once the menu bar and Dock are taken
    // off the top and bottom.
    //
    // userArea rather than totalArea is doing real work here: on macOS it
    // already excludes the menu bar and the Dock, and on Windows the taskbar,
    // so this asks "what can actually be shown" rather than "how big is the
    // panel".  The extra margin leaves the title bar reachable instead of
    // flush against the edge.
    //
    // The window stays RESIZABLE to its full declared limits - this only picks
    // the size it OPENS at, so nothing is taken away from a big monitor.
    //==========================================================================
    inline void centreGrexPopupOnScreen (juce::Component& w, int wantW, int wantH)
    {
        constexpr int kMargin  = 40;
        constexpr int kFloorW  = 320;
        constexpr int kFloorH  = 240;

        int openW = wantW;
        int openH = wantH;

        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = display->userArea;
            openW = juce::jmin (openW, area.getWidth()  - kMargin);
            openH = juce::jmin (openH, area.getHeight() - kMargin);
        }

        w.centreWithSize (juce::jmax (kFloorW, openW), juce::jmax (kFloorH, openH));
    }

    //==========================================================================
    // NATIVE DIALOGS OPENED FROM AN ALWAYS-ON-TOP POPUP.
    //
    // Three of these windows launch a native FileChooser of their own -- the
    // SFZ load in the instrument editor, the two kit loads in the drum editor,
    // and the Style Data export.  A native chooser is an OS window that does
    // not know about JUCE's topmost flag, so an always-on-top popup can cover
    // the very dialog it just opened.  The user sees a plugin that has frozen.
    //
    // JUCE handles this for its OWN modal windows -- AlertWindow raises itself
    // to always-on-top when any always-on-top window exists -- but it cannot do
    // it for a dialog the operating system owns.
    //
    // So the popup steps aside for the length of the chooser and takes its
    // place back afterwards.  Two calls rather than a scope guard because
    // launchAsync returns immediately: the restore belongs in the completion
    // callback, not at the end of the launching function.
    //
    // The restore takes a SafePointer because the window may have been closed
    // while the chooser was open, which is exactly the case a raw `this` would
    // turn into a crash.
    //
    // BOTH ARE NO-OPS ON macOS, because there is no topmost flag to suspend
    // there.  They are still CALLED from all four sites unguarded: keeping the
    // platform test in one place means a call site cannot be the thing that
    // forgets it, and restoring a flag that was never set costs nothing.
    //==========================================================================
    inline void suspendGrexPopupTopmost (juce::Component* w)
    {
       #if JUCE_MAC
        juce::ignoreUnused (w);
       #else
        if (w != nullptr)
            w->setAlwaysOnTop (false);
       #endif
    }

    inline void restoreGrexPopupTopmost (juce::Component::SafePointer<juce::Component> w)
    {
        if (auto* c = w.getComponent())
        {
           #if ! JUCE_MAC
            c->setAlwaysOnTop (true);
           #endif
            c->toFront (false);      // false: taking the topmost flag back must
                                     // not steal focus from whatever the user
                                     // clicked once the dialog closed.  On macOS
                                     // this is the whole job: raise it, nothing
                                     // more.
        }
    }
}



