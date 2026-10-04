
#pragma once

#include <JuceHeader.h>
#include "InstrEditPanel.h"    // GoldSlider, SlotParams, SweetenerParams
#include "SoundsFx.h"          // SectionSendFx::Slot — the send ordering
#include "BalladaPalette.h"

//==============================================================================
//  EffectSendsPanel.h — the instrument editor's third tab, replacing EFFECTS.
//
//  ── WHY THE OLD TAB HAD TO GO ────────────────────────────────────────────────
//
//  Three of its eight pages edited effects that no longer exist per instrument.
//  Chorus, reverb and delay are one instance per SECTION now, shared by every
//  channel in it — so a per-slot page for any of them would have offered a knob
//  that either changed nothing, or changed it for all sixteen instruments
//  without saying so.  Both are worse than not offering it.
//
//  Wah, phaser and sweetener went the other way: they are shapers, they stayed
//  per-instrument, and they live on the INSERTS tab beside this one.
//
//  What survives is what is genuinely per-instrument:
//
//      TOP     EQ (five bands) and PAN — corrective, and nobody wants one
//              instrument's tilt applied to the whole hand.
//
//      BOTTOM  Three sends — HOW MUCH of this instrument reaches each global
//              effect.  This is the per-instrument half of a global effect, and
//              the only per-instrument thing left about them.
//
//  ── THE SENDS ARE NOT NEW STATE ──────────────────────────────────────────────
//
//  Each send slider writes a field that already existed and already persisted:
//
//      CHORUS  -> chorusMix        REVERB  -> reverbWet        DELAY -> delayWet
//
//  Channel::applyParams already reads exactly these and turns them into sends,
//  which is why every existing .ins and .bset arrived with sensible amounts the
//  moment the racks went in.  Nothing here adds a saved field, and nothing here
//  needs a migration.
//
//  ── A SEND OF ZERO IS "OFF" ──────────────────────────────────────────────────
//
//  applyParams gates each send on the slot's matching `enabled` flag, and those
//  flags used to be the per-page power buttons that no longer exist.  Rather
//  than strand them at whatever an old preset happened to store — where a slot
//  with a healthy chorusMix and a stale chorusEnabled=false would look connected
//  and be silent — this panel DERIVES them: enabled = (send > 0).
//
//  One slider, one meaning, and no invisible second condition.
//==============================================================================

class EffectSendsPanel : public juce::Component
{
public:
    std::function<void()> onAnythingChanged;

    EffectSendsPanel()
    {
        // GoldSlider::onChange is std::function<void(float)>, not void().
        auto notify = [this] (float) { if (onAnythingChanged) onAnythingChanged(); };

        eqHeader.setText ("EQ  +  PAN", juce::dontSendNotification);
        eqHeader.setJustificationType (juce::Justification::centredLeft);
        eqHeader.setColour (juce::Label::textColourId,
                            juce::Colour (Betel::Pal::kAccentLight));
        addAndMakeVisible (eqHeader);

        sendHeader.setText ("SENDS TO GLOBAL EFFECTS  (chorus / reverb / delay)", juce::dontSendNotification);
        sendHeader.setJustificationType (juce::Justification::centredLeft);
        sendHeader.setColour (juce::Label::textColourId,
                              juce::Colour (Betel::Pal::kAccentLight));
        addAndMakeVisible (sendHeader);

        for (auto* s : { &eqGain[0], &eqGain[1], &eqGain[2], &eqGain[3], &eqGain[4],
                         &panSlider })
        {
            addAndMakeVisible (*s);
            s->onChange = notify;
        }

        for (int i = 0; i < kNumSends; ++i)
        {
            addAndMakeVisible (sends[i]);
            sends[i].onChange = notify;
        }
    }

    //── SlotParams exchange, matching the panel it replaces ───────────────────
    void loadParams (const SlotParams& p)
    {
        for (int i = 0; i < 5; ++i)
            eqGain[i].setValue (kEqRange.toUi (p.eqGain[i]));

        panSlider.setValue (p.pan * 50.0f + 50.0f);

        setSend (kChorus, p.chorusMix);
        setSend (kReverb, p.reverbWet);
        setSend (kDelay,  p.delayWet);
    }

    void readInto (SlotParams& p) const
    {
        for (int i = 0; i < 5; ++i)
            p.eqGain[i] = kEqRange.toNative (eqGain[i].getValue());

        p.pan = (panSlider.getValue() - 50.0f) / 50.0f;

        p.chorusMix = getSend (kChorus);
        p.reverbWet = getSend (kReverb);
        p.delayWet  = getSend (kDelay);


        // DERIVED, not stored — see the header.  A send with nothing behind it
        // is off, and there is no second switch to forget.
        p.chorusEnabled = p.chorusMix > 0.0f;
        p.reverbEnabled = p.reverbWet > 0.0f;
        p.delayEnabled  = p.delayWet  > 0.0f;
        // wahEnabled / phaserEnabled / sweet.enabled are NOT touched here — they
        // belong to the INSERTS tab, which owns those three outright.

        // The bases scaled a per-slot insert's wet.  A send has no base — the
        // ceiling is the rack's own level now — so they are pinned at unity
        // rather than left to scale the send by whatever a preset happened to
        // carry.
        p.reverbWetBase = 1.0f;
        p.delayWetBase  = 1.0f;
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);

        // Half and half.  The two blocks answer different questions — what this
        // instrument sounds like, and how much of it goes elsewhere — so they
        // get equal room rather than one being squeezed under the other.
        auto top = r.removeFromTop (r.getHeight() / 2);
        r.removeFromTop (8);

        eqHeader.setBounds (top.removeFromTop (18));
        layoutRow (top, { &eqGain[0], &eqGain[1], &eqGain[2], &eqGain[3], &eqGain[4],
                          &panSlider });

        sendHeader.setBounds (r.removeFromTop (18));
        std::vector<GoldSlider*> sv;
        for (int i = 0; i < kNumSends; ++i) sv.push_back (&sends[i]);
        layoutRow (r, sv);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (10);
        const int half = r.getHeight() / 2;

        g.setColour (juce::Colour (Betel::Pal::kAccent).withAlpha (0.25f));
        g.drawHorizontalLine (r.getY() + half + 4, (float) r.getX(),
                              (float) r.getRight());
    }

private:
    // Ordering matches SectionSendFx::Slot so the two cannot drift apart.
    // Three, matching SectionSendFx::Slot.  Wah, phaser and sweetener are
    // per-instrument INSERTS again — a shaper cannot be fed by a send — and
    // live on the editor's INSERTS tab with their own settings.
    enum { kChorus = 0, kReverb, kDelay, kNumSends };

    // ── LOG TAPER, NOT LINEAR ────────────────────────────────────────────
    //
    // A linear send puts every usable amount in the bottom of the travel —
    // 20% of a reverb is already a lot — so the top four fifths read as
    // "drowned" and the useful range is a nudge off zero.  The same curve the
    // chorus mix knob uses: half rotation is -20 dB.
    void  setSend (int i, float v)
    {
        sends[i].setValue (juce::jlimit (0.0f, 100.0f,
                                         Betel::logKnobTaperInv (v) * 100.0f));
    }

    // getValue() is non-const on GoldSlider, so these accessors cannot be const
    // either.  readInto drops const with them rather than casting
    // it away, which would be the same thing said less honestly.
    float getSend (int i) const
    {
        return Betel::logKnobTaper (juce::jlimit (0.0f, 1.0f, sends[i].getValue() / 100.0f));
    }

    static void layoutRow (juce::Rectangle<int> area, const std::vector<GoldSlider*>& v)
    {
        if (v.empty() || area.getWidth() <= 0) return;

        const int gap = 6;
        const int w   = (area.getWidth() - gap * ((int) v.size() - 1)) / (int) v.size();

        int x = area.getX();
        for (auto* s : v)
        {
            s->setBounds (x, area.getY(), w, area.getHeight());
            x += w + gap;
        }
    }

    // ±12 dB across 0..100, 50 = flat — the same span the old EQ page used.
    struct Range
    {
        float lo, hi;
        float toUi     (float n) const { return (n - lo) / (hi - lo) * 100.0f; }
        float toNative (float u) const { return lo + (u / 100.0f) * (hi - lo); }
    };
    static constexpr Range kEqRange { -12.0f, 12.0f };

    juce::Label eqHeader, sendHeader;

    GoldSlider eqGain[5] {
        { "LO",  0.0f, 100.0f, 50.0f, "" }, { "MLO", 0.0f, 100.0f, 50.0f, "" },
        { "MID", 0.0f, 100.0f, 50.0f, "" }, { "MHI", 0.0f, 100.0f, 50.0f, "" },
        { "HI",  0.0f, 100.0f, 50.0f, "" }
    };
    GoldSlider panSlider { "PAN", 0.0f, 100.0f, 50.0f, "" };

    GoldSlider sends[kNumSends] {
        { "CHORUS", 0.0f, 100.0f, 0.0f, "" },
        { "REVERB", 0.0f, 100.0f, 0.0f, "" },
        { "DELAY",  0.0f, 100.0f, 0.0f, "" }
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectSendsPanel)
};
