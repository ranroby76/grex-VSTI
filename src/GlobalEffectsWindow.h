

#pragma once

#include <JuceHeader.h>
#include "EffectsPanel.h"
#include "GlobalMacros.h"

//==============================================================================
//  GlobalEffectsWindow.h — the three global effects for ONE section.
//
//  ── WHY THIS IS NOT A NEW EDITOR ─────────────────────────────────────────────
//
//  It is EffectsPanel, bound to a section rack instead of a slot.  That panel
//  was already built to be bound to something other than a slot — the Funkey
//  family editor does exactly this through Betel::FamilyFxParams — so a second
//  implementation would have been a second layout to keep in step, and they
//  would have drifted the first time a control moved.
//
//  The selector list is trimmed to the three that are global — chorus, reverb
//  and delay.  EQ and PAN are hidden because they stay per-instrument: a shared
//  EQ would mean tilting one voice tilts sixteen, and a shared pan is a
//  contradiction in terms.  Wah, phaser and sweetener are hidden for a different
//  reason — they are shapers and went back to being per-instrument INSERTS.
//
//  ── ONE WINDOW PER HAND ──────────────────────────────────────────────────────
//
//  Section 0 is LEFT (the 16 style channels, drums and perc); section 1 is RIGHT
//  (the 8 solo channels).  The two are independent racks, so opening the window
//  from either hand's button shows that hand's settings — and shows the SAME
//  settings from every slot in that hand, because there is only one rack behind
//  them.
//
//  ── THE CHORUS PAGE MEANS SOMETHING NEW ──────────────────────────────────────
//
//  The global chorus is juce::dsp::Chorus now, whose controls really are RATE
//  and DEPTH — so the panel's existing chorusRate / chorusDepth fields mean
//  exactly what they say again, and the sliders need no relabelling.
//
//  Only MIX is shown, as you asked: it is the ceiling the per-instrument sends
//  reach.  Rate and depth sit at JUCE's documented classic-chorus values and are
//  mapped here so a control could be added later without touching the bridge.
//
//      chorusRate  0..1  ->  0.1 - 3.0 Hz   (the 0.8 Hz default sits at 0.24)
//      chorusDepth 0..1  ->  depth 0..1      (0.45 default)
//==============================================================================

namespace Betel
{
    /** Maps between a section rack and the FamilyFxParams the panel speaks.

        Free functions rather than members of either side: SectionSendFx is
        audio-thread DSP and has no business knowing about a UI struct, and
        FamilyFxParams belongs to the macro system. */
    struct SectionFxBridge
    {
        // ── THE CHORUS CONTROL RANGES ────────────────────────────────────────
        //
        // Chosen so the whole slider is useful, not so the numbers are round.
        //
        //   RATE   0.1 - 3.0 Hz.  Classic chorus lives 0.3-2; above 3 it is a
        //          rotary/vibrato effect, below 0.1 nothing appears to move.
        //          The 0.8 Hz default sits at 24% of travel.
        //   DEPTH  0 - 1 straight through, which is what juce::dsp::Chorus takes.
        //   MIX    the bus level — the ceiling the per-instrument sends reach.
        //
        // CENTRE DELAY is fixed at 7.5 ms and not exposed: JUCE's docs name 7-8
        // ms as the classic chorus range, and moving it is how a chorus turns
        // into a flanger by accident.
        static constexpr float kRateMinHz = 0.1f;
        static constexpr float kRateMaxHz = 3.0f;

        static float rateToUi   (float hz) { return juce::jlimit (0.0f, 1.0f,
                                    (hz - kRateMinHz) / (kRateMaxHz - kRateMinHz)); }
        static float rateFromUi (float x)  { return kRateMinHz
                                    + juce::jlimit (0.0f, 1.0f, x) * (kRateMaxHz - kRateMinHz); }

        // ── EARLY REFLECTIONS TRAVEL BESIDE FamilyFxParams, NOT INSIDE IT ────
        //
        // Every field on FamilyFxParams has to exist on SlotParams as well, because
        // EffectsPanel's loadFx/readFx are one templated pair over both.  ER exists
        // only on a section rack, so putting it in the shared struct would give
        // every slot and every Funkey family two parameters they neither own nor
        // save.  These two carry it instead, and EffectsPanel::loadEr / readEr are
        // outside the templated pair to match.
        static void readEr (const SectionSendFx& fx, float& erMix, float& erSize)
        {
            erMix  = juce::jlimit (0.0f, 1.0f, fx.reverbErMix .load());
            erSize = juce::jlimit (0.0f, 1.0f, fx.reverbErSize.load());
        }

        static void writeEr (float erMix, float erSize, SectionSendFx& fx)
        {
            fx.reverbErMix .store (juce::jlimit (0.0f, 1.0f, erMix));
            fx.reverbErSize.store (juce::jlimit (0.0f, 1.0f, erSize));
        }

        static void read (const SectionSendFx& fx, FamilyFxParams& p)
        {
            using S = SectionSendFx;

            // ── ONE CONTROL: MIX IS THE CEILING ──────────────────────────
            //
            // The effective amount on any instrument is send x level, so level
            // is the most the send can reach.  That is the whole control.
            //
            // Through the log taper, which is what that curve was in the patch
            // for: chorus amounts live low, and a linear knob would put all of
            // them in the top fifth of the travel.
            p.chorusEnabled = fx.enabled[S::kChorus].load();
            p.chorusMix     = logKnobTaperInv (fx.level[S::kChorus].load());

            // DRIFT AND WIDTH ARE FIXED, not absent.  They stay atomics at the
            // patch's 4.15 / 0.442, stay in the saved set, and stay settable in
            // code — so wanting less drift one day is a one-line change rather
            // than a re-port.  They are simply not exposed, and their sliders
            // are hidden in EffectsPanel so nothing offers a knob that does
            // nothing.  Seeded here only so readFx cannot write stale values.
            p.chorusRate    = rateToUi (fx.chorusRateHz.load());
            p.chorusDepth   = juce::jlimit (0.0f, 1.0f, fx.chorusDepth.load());

            // EVERY EFFECT'S MIX/WET IS ITS BUS LEVEL.
            //
            // These used to be pinned at 1.0 on the way in and ignored on the
            // way out, which is why every WET and MIX slider in this window
            // moved and did nothing.  They are the rack's output level — the
            // ceiling the per-instrument sends reach — and they belong to the
            // bus exactly as the chorus MIX does.
            p.reverbEnabled  = fx.enabled[S::kReverb].load();
            p.reverbSize     = fx.reverbSize    .load();
            p.reverbDamp     = fx.reverbDamp    .load();
            p.reverbTail     = fx.reverbTail    .load();
            p.reverbPreDelay = fx.reverbPreDelay.load();
            p.reverbHpNorm   = fx.reverbHpNorm  .load();
            p.reverbLpNorm   = fx.reverbLpNorm  .load();
            p.reverbWet      = logKnobTaperInv (fx.level[S::kReverb].load());
            p.reverbDry      = 0.0f;
            p.reverbWetBase  = 1.0f;

            p.delayEnabled  = fx.enabled[S::kDelay].load();
            p.delayFeedback = fx.delayFeedback.load();
            p.delayTimeSig  = fx.delayTimeSig .load();
            p.delayDiv      = fx.delayDiv     .load();

            // *** THESE THREE WERE WRITTEN BUT NEVER READ ***
            //
            // write() has always stored delayDampHz / delayHpHz / delaySmoothMs
            // into the rack; read() did not load them back.  So the panel opened
            // showing FamilyFxParams' own defaults (5000 / 20 / 40) whatever the
            // rack actually held, and the next write stamped those defaults over
            // the saved values.  A brightened delay survived until the window was
            // opened and anything at all was touched, then silently reverted.
            //
            // A read/write pair that does not cover the same fields is the same
            // class of fault as a SlotParams field missing from the converter:
            // it fails silently and looks like the value was never saved.
            p.delayDampHz   = fx.delayDampHz  .load();
            p.delayHpHz     = fx.delayHpHz    .load();
            p.delaySmoothMs = fx.delaySmoothMs.load();
            p.delayWet      = logKnobTaperInv (fx.level[S::kDelay].load());
            p.delayDry      = 0.0f;
            p.delayWetBase  = 1.0f;

            // EQ is not on this rack.  Left at whatever the struct defaults to;
            // the selector is hidden, so nothing reads it back.
        }

        static void write (const FamilyFxParams& p, SectionSendFx& fx)
        {
            using S = SectionSendFx;

            fx.enabled[S::kChorus].store (p.chorusEnabled);
            fx.level  [S::kChorus].store (juce::jlimit (0.0f, 1.0f,
                                                        logKnobTaper (p.chorusMix)));

            // RATE AND DEPTH ARE WRITTEN BACK NOW.  They were held fixed while
            // the chorus had no controls; with real sliders in front of them
            // they are the whole point.
            fx.chorusRateHz.store (juce::jlimit (kRateMinHz, kRateMaxHz,
                                                 rateFromUi (p.chorusRate)));
            fx.chorusDepth .store (juce::jlimit (0.0f, 1.0f, p.chorusDepth));

            fx.enabled[S::kReverb].store (p.reverbEnabled);
            fx.level  [S::kReverb].store (juce::jlimit (0.0f, 1.0f, logKnobTaper (p.reverbWet)));
            fx.reverbSize    .store (juce::jlimit (0.0f, 1.0f, p.reverbSize));
            fx.reverbDamp    .store (juce::jlimit (0.0f, 1.0f, p.reverbDamp));
            fx.reverbTail    .store (juce::jlimit (0.0f, 1.0f, p.reverbTail));
            fx.reverbPreDelay.store (juce::jlimit (0.0f, 1.0f, p.reverbPreDelay));
            fx.reverbHpNorm  .store (juce::jlimit (0.0f, 1.0f, p.reverbHpNorm));
            fx.reverbLpNorm  .store (juce::jlimit (0.0f, 1.0f, p.reverbLpNorm));

            fx.enabled[S::kDelay].store (p.delayEnabled);
            fx.level  [S::kDelay].store (juce::jlimit (0.0f, 1.0f, logKnobTaper (p.delayWet)));
            fx.delayFeedback.store (juce::jlimit (0.0f, 0.95f, p.delayFeedback));
            fx.delayDampHz  .store (juce::jlimit (200.0f, 20000.0f, p.delayDampHz));
            fx.delayHpHz    .store (juce::jlimit (20.0f,  2000.0f,  p.delayHpHz));
            fx.delaySmoothMs.store (juce::jlimit (1.0f,   500.0f,   p.delaySmoothMs));
            fx.delayTimeSig .store (juce::jlimit (0, 1, p.delayTimeSig));
            fx.delayDiv     .store (juce::jmax  (0,     p.delayDiv));
        }

    };
}

//==============================================================================
class GlobalEffectsContent : public juce::Component
{
public:
    /** Both hooks are supplied by MainComponent, which owns the processor.
        This component knows only its section index. */
    std::function<void (int section, Betel::FamilyFxParams&)>       onRead;
    std::function<void (int section, const Betel::FamilyFxParams&)> onWrite;

    /** The early-reflections pair, on their own route - see SectionFxBridge.
        Both are optional: without them the ER sliders simply never move, which
        is the correct degraded behaviour rather than a crash. */
    std::function<void (int section, float& erMix, float& erSize)> onReadEr;
    std::function<void (int section, float  erMix, float  erSize)> onWriteEr;

    GlobalEffectsContent()
    {
        addAndMakeVisible (fx);

        // THE SIX GLOBALS, AND ONLY THOSE.  EQ and PAN stay per-instrument, so
        // showing them here would offer a control that writes nowhere.
        // THE THREE THAT WORK IN PARALLEL.  Wah, phaser and sweetener are
        // per-instrument inserts again — they need per-instrument settings, and
        // a shaper cannot be fed by a send at all — so they live in the
        // instrument editor, not here.  EQ and PAN never belonged here either.
        fx.setSelectorsVisible ({ EffectsPanel::SelChorus,
                                  EffectsPanel::SelReverb,
                                  EffectsPanel::SelDelay });

        fx.onAnythingChanged = [this] { pushToRack(); };
    }

    void setSection (int s)
    {
        section = juce::jlimit (0, 1, s);
        pullFromRack();
    }

    int  getSection() const noexcept { return section; }

    /** RE-SEED FROM THE RACK.  Call whenever the rack is written by anything
        other than this window — a set load, a style change, a future macro.

        This is not optional.  pushToRack() reads EVERY widget back on any
        change, enable buttons included, and writes the lot to the rack.  If
        the widgets are stale — because the rack moved and nobody told them —
        the first slider you touch silently restores the old state of every
        other control on the page, which from the outside looks like the
        toggles switching each other off. */
    void refreshFromRack() { pullFromRack(); }

    void resized() override { fx.setBounds (getLocalBounds().reduced (8)); }

    void paint (juce::Graphics& g) override
    {
        // The same flat near-black every other popup in the plugin uses
        // (MacroFxWindows does likewise) rather than a palette name — the
        // palette has no background constant, and inventing one here would put
        // the definition in the wrong file.
        g.fillAll (juce::Colour (0xFF141414));
    }

private:
    void pullFromRack()
    {
        if (onRead)
        {
            Betel::FamilyFxParams p;
            onRead (section, p);
            fx.loadFx (p);
        }

        if (onReadEr)
        {
            float erMix = 0.0f, erSize = 0.5f;
            onReadEr (section, erMix, erSize);
            fx.loadEr (erMix, erSize);
        }
    }

    void pushToRack()
    {
        if (onWrite)
        {
            Betel::FamilyFxParams p;
            // Read-modify-write: readFx only touches the fields the panel owns,
            // so seeding from the rack first keeps everything else intact.
            if (onRead) onRead (section, p);
            fx.readFx (p);
            onWrite (section, p);
        }

        if (onWriteEr)
        {
            float erMix = 0.0f, erSize = 0.5f;
            fx.readEr (erMix, erSize);
            onWriteEr (section, erMix, erSize);
        }
    }

    EffectsPanel fx;
    int          section = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalEffectsContent)
};

//==============================================================================
class GlobalEffectsWindow : public juce::DocumentWindow
{
public:
    GlobalEffectsWindow (const juce::String& title)
        : juce::DocumentWindow (title,
                                juce::Colour (0xFF141414),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content = new GlobalEffectsContent(), true);
        setResizable (true, false);
        centreWithSize (900, 560);
    }

    GlobalEffectsContent& getContent() noexcept { return *content; }

    // Hidden rather than deleted: the window keeps its size and page between
    // openings, and MainComponent owns its lifetime.
    void closeButtonPressed() override { setVisible (false); }

    // Every time it comes back, re-read the rack.  Anything could have moved
    // it while it was hidden, and stale widgets are what write stale state.
    void visibilityChanged() override
    {
        if (isVisible() && content != nullptr)
            content->refreshFromRack();
    }

private:
    GlobalEffectsContent* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalEffectsWindow)
};
