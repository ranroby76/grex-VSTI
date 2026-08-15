#pragma once
//==============================================================================
// ArabicScalePopup.h
//
// NOTE: filename retained for source-tree continuity; this file no longer
// declares a popup window. The Arabic scale editor is now embedded inline in
// the EFFECTS tab alongside CLICK / EQ / REVERB / DELAY.
//
// Contents:
//   • ArabicKeyKnob          — a single rectangular drag-knob (white or black)
//                              showing note name + cents value. Drag up/down
//                              snaps live to one of {-50, 0, +50} cents.
//   • ArabicKeyboardKnobs    — the 12 knobs arranged in piano formation:
//                              7 white (naturals) along the bottom row,
//                              5 black (sharps) on the upper half between the
//                              correct white pairs.
//
// The host (EffectsPanel) embeds ArabicKeyboardKnobs and reacts to
// onValueChanged(noteClass, cents) to push the new value to the engine via
// the existing callback chain.
//==============================================================================

#include <JuceHeader.h>
#include <array>
#include <functional>

namespace Betel
{
    //==========================================================================
    // ArabicKeyKnob — one rectangular drag-knob for a single chromatic note
    //==========================================================================
    class ArabicKeyKnob : public juce::Component
    {
    public:
        static constexpr float kHalfFlatCents  = -50.0f;
        static constexpr float kHalfSharpCents = +50.0f;

        std::function<void (float cents)> onValueChanged;

        ArabicKeyKnob (int noteClassIn, const juce::String& labelIn, bool blackKey)
            : noteClass (noteClassIn), label (labelIn), isBlack (blackKey)
        {
            setOpaque (false);
        }

        void  setCents (float c, bool notify = false)
        {
            cents = snap (juce::jlimit (kHalfFlatCents, kHalfSharpCents, c));
            repaint();
            if (notify && onValueChanged) onValueChanged (cents);
        }
        float getCents() const     { return cents; }
        int   getNoteClass() const { return noteClass; }

        // ── Drawing ──────────────────────────────────────────────────────────
        void paint (juce::Graphics& g) override
        {
            const float W = (float) getWidth();
            const float H = (float) getHeight();
            if (W < 6.0f || H < 20.0f) return;

            const juce::Colour bg     = isBlack ? juce::Colour (0xFF1E1E22)
                                                : juce::Colour (0xFFEFEAD8);
            const juce::Colour text   = isBlack ? juce::Colours::white.withAlpha (0.95f)
                                                : juce::Colour (0xFF202020);
            const juce::Colour border = juce::Colour (0xFF0A0A0A);

            // Body
            const juce::Rectangle<float> r (0.5f, 0.5f, W - 1.0f, H - 1.0f);
            g.setColour (bg);
            g.fillRoundedRectangle (r, 3.0f);

            // Subtle vertical shading so the knobs read as 3-D
            {
                juce::ColourGradient sg (
                    juce::Colours::white.withAlpha (isBlack ? 0.05f : 0.20f),
                    r.getCentreX(), r.getY(),
                    juce::Colours::black.withAlpha (isBlack ? 0.45f : 0.18f),
                    r.getCentreX(), r.getBottom(), false);
                g.setGradientFill (sg);
                g.fillRoundedRectangle (r, 3.0f);
            }

            // Highlight on hover / drag — a thin amber ring
            if (dragging || hovered)
            {
                g.setColour (juce::Colour (0xFFE6A059).withAlpha (dragging ? 0.85f : 0.45f));
                g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.5f);
            }
            else
            {
                g.setColour (border);
                g.drawRoundedRectangle (r, 3.0f, 0.8f);
            }

            // Note name (top)
            const float labelH = juce::jmin (16.0f, H * 0.30f);
            g.setColour (text.withAlpha (0.75f));
            g.setFont (juce::Font (juce::jmin (12.0f, labelH * 0.85f), juce::Font::plain));
            g.drawFittedText (label,
                              juce::Rectangle<float> (0.0f, 2.0f, W, labelH).toNearestInt(),
                              juce::Justification::centred, 1);

            // Numerical value (centre / lower half)
            const juce::Rectangle<float> valRect (0.0f, labelH, W, H - labelH);
            g.setColour (text);
            g.setFont (juce::Font (juce::jmin (22.0f, valRect.getHeight() * 0.55f),
                                   juce::Font::bold));
            g.drawFittedText (formatCents (),
                              valRect.toNearestInt(),
                              juce::Justification::centred, 1);
        }

        // ── Mouse ────────────────────────────────────────────────────────────
        void mouseEnter (const juce::MouseEvent&) override { hovered = true;  repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { hovered = false; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragStartCents = cents;
            dragStartY     = e.y;
            dragging       = true;
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            // Map full vertical drag (component height) to the full ±50 range.
            const float h = juce::jmax (1.0f, (float) getHeight());
            const float dCents = (float) (dragStartY - e.y) / h * 100.0f;
            const float raw    = juce::jlimit (kHalfFlatCents, kHalfSharpCents,
                                               dragStartCents + dCents);
            cents = snap (raw);
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            dragging = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint();
            if (onValueChanged) onValueChanged (cents);
        }
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            setCents (0.0f, true);
        }

    private:
        static float snap (float c)
        {
            if (c >  25.0f) return  50.0f;
            if (c < -25.0f) return -50.0f;
            return 0.0f;
        }
        juce::String formatCents() const
        {
            const int v = (int) cents;
            if (v == 0)  return "0";
            if (v >  0)  return "+" + juce::String (v);
            return juce::String (v);   // already has minus sign
        }

        int          noteClass;
        juce::String label;
        bool         isBlack;
        float        cents          = 0.0f;
        float        dragStartCents = 0.0f;
        int          dragStartY     = 0;
        bool         dragging       = false;
        bool         hovered        = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArabicKeyKnob)
    };

    //==========================================================================
    // ArabicKeyboardKnobs — 12 knobs laid out as a piano keyboard
    //
    //   ┌──┐    ┌──┐         ┌──┐  ┌──┐  ┌──┐
    //   │C#│    │D#│         │F#│  │G#│  │A#│       ← 5 upper (black bg)
    //   └──┘    └──┘         └──┘  └──┘  └──┘
    //   ┌────┬────┬────┬────┬────┬────┬────┐
    //   │ C  │ D  │ E  │ F  │ G  │ A  │ B  │        ← 7 lower (white bg)
    //   └────┴────┴────┴────┴────┴────┴────┘
    //==========================================================================
    class ArabicKeyboardKnobs : public juce::Component
    {
    public:
        std::function<void (int noteClass, float cents)> onValueChanged;

        ArabicKeyboardKnobs()
        {
            for (int i = 0; i < 7; ++i)
            {
                const int nc = kWhiteNotes[i];
                whiteKnobs[i] = std::make_unique<ArabicKeyKnob> (
                    nc, kNoteNames[nc], /*black=*/false);
                whiteKnobs[i]->onValueChanged = [this, nc](float c)
                {
                    if (onValueChanged) onValueChanged (nc, c);
                };
                addAndMakeVisible (*whiteKnobs[i]);
            }
            for (int i = 0; i < 5; ++i)
            {
                const int nc = kBlackNotes[i];
                blackKnobs[i] = std::make_unique<ArabicKeyKnob> (
                    nc, kNoteNames[nc], /*black=*/true);
                blackKnobs[i]->onValueChanged = [this, nc](float c)
                {
                    if (onValueChanged) onValueChanged (nc, c);
                };
                addAndMakeVisible (*blackKnobs[i]);
            }
        }

        void setCents (int noteClass, float cents)
        {
            if (auto* k = knobForNote (noteClass)) k->setCents (cents, false);
        }
        float getCents (int noteClass) const
        {
            if (auto* k = knobForNote (noteClass)) return k->getCents();
            return 0.0f;
        }
        void resetAll()
        {
            for (auto& k : whiteKnobs) if (k) k->setCents (0.0f, true);
            for (auto& k : blackKnobs) if (k) k->setCents (0.0f, true);
        }

        void paint (juce::Graphics& g) override
        {
            // Soft dark backdrop framing the keyboard
            g.fillAll (juce::Colour (0xFF0A0A0A));
        }

        void resized() override
        {
            const auto bounds = getLocalBounds().reduced (6);
            const float W = (float) bounds.getWidth();
            const float H = (float) bounds.getHeight();
            if (W <= 0.0f || H <= 0.0f) return;

            const float whiteW = W / 7.0f;
            const float blackW = whiteW * 0.65f;
            const float blackH = H * 0.55f;

            // White knobs across the bottom (full height of inner area).
            for (int i = 0; i < 7; ++i)
            {
                const float x = bounds.getX() + i * whiteW;
                whiteKnobs[i]->setBounds (juce::Rectangle<float> (
                    x + 2.0f, (float) bounds.getY(),
                    whiteW - 4.0f, H).toNearestInt());
            }
            // Black knobs occupy the upper portion, centred over the gap to the
            // next white in the natural keyboard layout.
            // Indices into whiteKnobs after which a black knob appears:
            //   C->C#  (after 0), D->D# (after 1), F->F# (after 3),
            //   G->G# (after 4), A->A# (after 5)
            const int afterWhite[5] = { 0, 1, 3, 4, 5 };
            for (int i = 0; i < 5; ++i)
            {
                const float centreX = bounds.getX()
                                    + (afterWhite[i] + 1) * whiteW;
                blackKnobs[i]->setBounds (juce::Rectangle<float> (
                    centreX - blackW * 0.5f, (float) bounds.getY(),
                    blackW, blackH).toNearestInt());
            }
        }

    private:
        ArabicKeyKnob* knobForNote (int noteClass) const
        {
            for (int i = 0; i < 7; ++i)
                if (kWhiteNotes[i] == noteClass) return whiteKnobs[i].get();
            for (int i = 0; i < 5; ++i)
                if (kBlackNotes[i] == noteClass) return blackKnobs[i].get();
            return nullptr;
        }

        static constexpr int kWhiteNotes[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr int kBlackNotes[5] = { 1, 3, 6, 8, 10 };
        static constexpr const char* kNoteNames[12] = {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
        };

        std::array<std::unique_ptr<ArabicKeyKnob>, 7> whiteKnobs;
        std::array<std::unique_ptr<ArabicKeyKnob>, 5> blackKnobs;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArabicKeyboardKnobs)
    };
} // namespace Betel
