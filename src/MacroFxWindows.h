
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
//==============================================================================
// MacroFxWindows.h — the editors behind Settings ▸ GLOBAL SETTINGS.
//
//   FunkeyModeWindow  the engage switch plus the six family buttons
//   FamilyFxWindow    one GM family's WAH and PHASER
//
// BIG DRUMS WAS REMOVED, editors and preset file included.  It replaced the
// loaded kit's whole FX rack with one global one, which is the opposite of what
// a kit that was voiced for a style needs.
//
// ── WHY THE FAMILY EDITOR IS NOT A NEW EDITOR ────────────────────────────────
//
// FamilyFxWindow embeds the SAME EffectsPanel the Sounds instrument editor uses,
// bound to a Betel::FamilyFxParams instead of a SlotParams (the stages carry
// identical field names on both types — see GlobalMacros.h, which relies on that
// for its member-for-member push).  Only WAH and PHASER are shown, because those
// are the only two stages the macro copies; PAN is off because a family preset
// carries character, not placement.
//
// The consequence that matters: a family preset can never drift from what the
// per-instrument editor offers, because there is only one implementation.  Add a
// stage to EffectsPanel and both pages gain it.
//
// ── LIVE AUDITION ────────────────────────────────────────────────────────────
//
// Every knob move writes straight into the live preset and fires onChanged, so
// the affected family (or the drum rack) is re-pushed to the engine while the
// window is open — you hear the edit on the running arrangement rather than
// after a save/reload cycle.  Nothing is written to disk until SAVE.
//
// ── DISPLAYED TEXT IS PLAIN ASCII ────────────────────────────────────────────
//
// Window titles, headers and frame captions use ASCII only.  The source files
// carry mixed encodings, and a non-ASCII glyph in a juce::String literal comes
// out as mojibake once the file is re-saved by an editor that guesses wrong.
// Comments can hold whatever they like — they are never drawn.
//
// ── SAVE ─────────────────────────────────────────────────────────────────────
//
// SAVE writes the macro's whole file (grex_funkey.xml holds all seven families,
// grex_bigdrums.xml the one rack), so saving from any family window commits the
// lot.  Closing WITHOUT saving leaves the edits live for the session but not on
// disk — the next plugin start reloads the file.
//==============================================================================

#include <JuceHeader.h>
#include "GrexPopupWindow.h"   // stays in front, minimisable, never vanishes
#include <functional>
#include <utility>
#include <vector>

#include "InstrEditPanel.h"     // GoldSlider / InstrEditStyle
#include "EffectsPanel.h"       // reused verbatim by the family editor
#include "GlobalMacros.h"

namespace Betel
{
    //==========================================================================
    // Shared header strip: title on the left, SAVE on the right.
    //==========================================================================
    class MacroEditorHeader : public juce::Component
    {
    public:
        std::function<void()> onSave;

        MacroEditorHeader()
        {
            title.setJustificationType (juce::Justification::centredLeft);
            title.setColour (juce::Label::textColourId, juce::Colour (Betel::Pal::kAccent));
            title.setFont (juce::Font (18.0f, juce::Font::bold));
            addAndMakeVisible (title);

            saveBtn.setButtonText ("SAVE");
            InstrEditStyle::styleSquareButton (saveBtn, false);
            saveBtn.onClick = [this] { if (onSave) onSave(); };
            addAndMakeVisible (saveBtn);

            savedMsg.setJustificationType (juce::Justification::centredRight);
            savedMsg.setColour (juce::Label::textColourId, juce::Colour (0xFF00C853));
            savedMsg.setFont (juce::Font (14.0f, juce::Font::bold));
            addAndMakeVisible (savedMsg);
        }

        void setTitle (const juce::String& s)
        { title.setText (s, juce::dontSendNotification); }

        /** Flash a confirmation next to SAVE.  Cleared on the next edit. */
        void setSavedMessage (const juce::String& s)
        { savedMsg.setText (s, juce::dontSendNotification); }

        void resized() override
        {
            auto b = getLocalBounds().reduced (4, 2);
            saveBtn .setBounds (b.removeFromRight (110).reduced (2, 3));
            savedMsg.setBounds (b.removeFromRight (170));
            title   .setBounds (b);
        }

    private:
        juce::Label      title, savedMsg;
        juce::TextButton saveBtn;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroEditorHeader)
    };

    //==========================================================================
    // FamilyFxWindow — Funkey Mode, one family at a time.
    //==========================================================================
    class FamilyFxWindow : public juce::DocumentWindow
    {
    public:
        /** A knob moved: the preset is already updated, re-push it to the
            engine so the edit is audible immediately. */
        std::function<void()> onChanged;
        std::function<void()> onClosed;

        FamilyFxWindow()
            : juce::DocumentWindow ("Funkey Mode - Family FX",
                                    juce::Colour (0xFF1A1A1A),
                                    juce::DocumentWindow::closeButton)
        {
            Betel::applyGrexPopupBehaviour (*this);
            setUsingNativeTitleBar (false);
            setResizable (true, true);
            setResizeLimits (900, 470, 2200, 1200);
            content = new Content (*this);
            setContentOwned (content, true);
            Betel::centreGrexPopupOnScreen (*this, 1120, 560);
        }

        ~FamilyFxWindow() override { clearContentComponent(); }

        /** Bind to a family slot (FunkeyFamilies::Slot) and show. */
        void showForFamily (int familySlot, juce::Component* parent)
        {
            slot = juce::jlimit (0, (int) FunkeyFamilies::Count - 1, familySlot);
            content->bind (slot);
            setName ("Funkey Mode - " + juce::String (FunkeyFamilies::name (slot)));
            placeAndShow (parent);
        }

        int getFamilySlot() const noexcept { return slot; }

        void closeButtonPressed() override
        {
            setVisible (false);
            if (onClosed) onClosed();
        }

    private:
        void placeAndShow (juce::Component* parent)
        {
            if (parent != nullptr)
            {
                const auto pb = parent->getScreenBounds();
                const int w = juce::jlimit (900, 2200, (int) (pb.getWidth()  * 0.80f));
                const int h = juce::jlimit (470, 1200, (int) (pb.getHeight() * 0.72f));
                setBounds (pb.getCentreX() - w / 2, pb.getCentreY() - h / 2, w, h);
            }
            setVisible (true);
            toFront (true);
        }

        //----------------------------------------------------------------------
        class Content : public juce::Component
        {
        public:
            explicit Content (FamilyFxWindow& owner_) : owner (owner_)
            {
                setOpaque (true);

                header.onSave = [this]
                {
                    const bool ok = GlobalMacros::get().saveFunkey();
                    header.setSavedMessage (ok ? "saved to grex_funkey.xml"
                                               : "SAVE FAILED");
                };
                addAndMakeVisible (header);

                fx.onAnythingChanged = [this]
                {
                    if (binding) return;             // seeding, not a user edit
                    fx.readFx (target());
                    header.setSavedMessage ({});     // edits invalidate "saved"
                    if (owner.onChanged) owner.onChanged();
                };
                addAndMakeVisible (fx);
            }

            void bind (int familySlot)
            {
                slot = familySlot;

                // WAH AND PHASER ONLY, because those are the only two stages
                // GlobalMacros::overrideFx actually copies.  Showing EQ, chorus,
                // delay or reverb here would be worse than clutter: they would
                // edit, save and reload perfectly and never once be heard, which
                // is the most expensive kind of control to ship.
                fx.setSelectorsVisible ({ EffectsPanel::SelWah, EffectsPanel::SelPhaser });

                // *** DO NOT ADD setPanSelectorVisible(false) BACK HERE. ***
                //
                // It is not a separate switch for PAN.  It calls
                // setSelectorsVisible({SelEq, SelChorus, SelWah, SelPhaser,
                // SelDelay, SelReverb}) - so it THREW AWAY the wah+phaser list
                // above and put EQ, chorus, delay and reverb straight back on
                // this page.  The list above already excludes PAN and SWEETEN by
                // not naming them, which makes the second call redundant as well
                // as wrong.
                //
                // THE PER-STAGE ON/OFF BUTTONS INSIDE WAH AND PHASER STAY.  They
                // are the family's own controls and a family with wah but no
                // phaser needs them.  It is the window-level ON/OFF that had to
                // go - see FunkeyModeWindow below.

                binding = true;
                fx.loadFx (target());
                binding = false;

                header.setTitle ("FUNKEY MODE  >  "
                                 + juce::String (FunkeyFamilies::name (slot)));
                header.setSavedMessage ({});
            }

            void paint (juce::Graphics& g) override
            { g.fillAll (juce::Colour (0xFF141414)); }

            void resized() override
            {
                auto b = getLocalBounds().reduced (8);
                header.setBounds (b.removeFromTop (34));
                b.removeFromTop (6);
                fx.setBounds (b);
            }

        private:
            /** The family preset this window is bound to. */
            FamilyFxParams& target() const
            { return GlobalMacros::get().family (slot); }

            FamilyFxWindow&   owner;
            MacroEditorHeader header;
            EffectsPanel      fx;
            int  slot    = 0;
            bool binding = false;   // suppresses onAnythingChanged while seeding

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
        };

        Content* content = nullptr;
        int      slot    = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FamilyFxWindow)
    };

    //==========================================================================
    class FunkeyModeWindow : public juce::DocumentWindow
    {
    public:
        // NO ENGAGE SWITCH HERE.  The onToggled callback is GONE with the ON/OFF
        // button that drove it: FUNKEY MODE has exactly one switch, the FUNKEY
        // button in the play-control row, and a second one in this window looks
        // like the master and is not.  Someone who finds it OFF concludes the
        // macro is broken, which is exactly what happened.
        //
        // The per-stage ON/OFF buttons inside a family's WAH and PHASER pages
        // are a different thing and they stay - those choose what the FAMILY is,
        // not whether the macro runs.
        std::function<void(int)>  onFamilyPicked; // open that family's editor
        std::function<void()>     onClosed;

        FunkeyModeWindow()
            : juce::DocumentWindow ("Funkey Mode", juce::Colour (0xFF1A1A1A),
                                    juce::DocumentWindow::closeButton)
        {
            Betel::applyGrexPopupBehaviour (*this);
            setUsingNativeTitleBar (false);
            setResizable (true, true);
            setResizeLimits (620, 260, 1600, 700);
            content = new Content (*this);
            setContentOwned (content, true);
            Betel::centreGrexPopupOnScreen (*this, 760, 320);
        }

        ~FunkeyModeWindow() override { clearContentComponent(); }

        void showCentredOver (juce::Component* parent)
        {
            content->refresh();
            if (parent != nullptr)
            {
                const auto pb = parent->getScreenBounds();
                setBounds (pb.getCentreX() - 380, pb.getCentreY() - 160, 760, 320);
            }
            setVisible (true);
            toFront (true);
        }

        void refresh() { if (content != nullptr) content->refresh(); }

        void closeButtonPressed() override
        {
            setVisible (false);
            if (onClosed) onClosed();
        }

    private:
        class Content : public juce::Component
        {
        public:
            explicit Content (FunkeyModeWindow& owner_) : owner (owner_)
            {
                setOpaque (true);

                header.setTitle ("FUNKEY MODE");
                header.onSave = [this]
                {
                    const bool ok = GlobalMacros::get().saveFunkey();
                    header.setSavedMessage (ok ? "saved to grex_funkey.xml" : "SAVE FAILED");
                };
                addAndMakeVisible (header);

                for (int f = 0; f < FunkeyFamilies::Count; ++f)
                {
                    famBtn[f].setButtonText (shortName (f));
                    InstrEditStyle::styleSquareButton (famBtn[f], false);
                    famBtn[f].onClick = [this, f]
                    { if (owner.onFamilyPicked) owner.onFamilyPicked (f); };
                    addAndMakeVisible (famBtn[f]);
                }

                hint.setText ("Each family button opens its own WAH and PHASER. "
                              "Everything else stays the instrument's own. "
                              "SAVE writes all six.  Switch the macro on and off "
                              "with the FUNKEY button on the main tab.",
                              juce::dontSendNotification);
                hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
                hint.setFont (juce::Font (13.0f, juce::Font::plain));
                addAndMakeVisible (hint);

                refresh();
            }

            /** Kept as a no-op repaint: the window has nothing left that
                reflects the engaged state, but the constructor and any future
                caller can still ask for a refresh without knowing that. */
            void refresh() { repaint(); }

            void paint (juce::Graphics& g) override
            { g.fillAll (juce::Colour (0xFF141414)); }

            void resized() override
            {
                auto b = getLocalBounds().reduced (10);
                header.setBounds (b.removeFromTop (34));
                b.removeFromTop (8);

                // The hint takes the whole row the ON/OFF button used to share.
                hint.setBounds (b.removeFromTop (44));

                b.removeFromTop (12);

                const int n  = FunkeyFamilies::Count;
                const int bw = (b.getWidth() - (n - 1) * 6) / n;
                const int bh = juce::jmin (b.getHeight(), 96);
                for (int f = 0; f < n; ++f)
                    famBtn[f].setBounds (b.getX() + f * (bw + 6), b.getY(), bw, bh);
            }

        private:
            static juce::String shortName (int slot)
            {
                switch (slot)
                {
                    case FunkeyFamilies::ChromPerc:  return "CHR.PERC";
                    case FunkeyFamilies::Organ:      return "ORGAN";
                    case FunkeyFamilies::Guitar:     return "GUITAR";
                    case FunkeyFamilies::SynthLead:  return "SYN.LEAD";
                    case FunkeyFamilies::Ethnic:     return "ETHNIC";
                    case FunkeyFamilies::Percussive: return "PERCUSS.";
                    default:                         return "?";
                }
            }

            FunkeyModeWindow& owner;
            MacroEditorHeader header;
            juce::TextButton  famBtn[FunkeyFamilies::Count];
            juce::Label       hint;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
        };

        Content* content = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FunkeyModeWindow)
    };

} // namespace Betel



