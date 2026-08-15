#pragma once
//==============================================================================
// ArabicScaleKeyboard.h
//
// A compact one-octave piano keyboard (7 white + 5 black keys) where each key
// is a 3-state toggle for the pitch class's tuning offset:
//
//   click cycles:  0¢  →  -50¢ (half-flat / koron)  →  +50¢ (half-sharp / sori)  →  0¢
//
// Visual indication:
//   • 0¢   : key in its base colour, no overlay
//   • -50¢ : amber overlay + "♭½" label
//   • +50¢ : aqua  overlay + "♯½" label
//
// The component is self-contained — no JUCE keyboard listener, no MIDI input.
// It calls onValueChanged(noteClass, cents) whenever the user clicks a key.
//
// Usage:
//   ArabicScaleKeyboard kb;
//   kb.onValueChanged = [&](int n, float c) { engine.setScaleTuningCents(n, c); };
//   kb.setCents(4, -50.0f);     // E half-flat
//   addAndMakeVisible(kb);
//==============================================================================

#include <JuceHeader.h>
#include <array>
#include <functional>

namespace Betel
{
    class ArabicScaleKeyboard : public juce::Component
    {
    public:
        // Cycle order: 0  →  -50  →  +50  →  0
        static constexpr float kHalfFlatCents  = -50.0f;
        static constexpr float kHalfSharpCents = +50.0f;

        ArabicScaleKeyboard()
        {
            for (int i = 0; i < 12; ++i) cents[(size_t) i] = 0.0f;
            setInterceptsMouseClicks(true, false);
            setOpaque(false);
        }

        std::function<void(int noteClass, float cents)> onValueChanged;

        // Programmatic setters (e.g. for preset recall)
        void setCents(int noteClass, float c)
        {
            if (noteClass < 0 || noteClass >= 12) return;
            cents[(size_t) noteClass] = c;
            if (onValueChanged) onValueChanged(noteClass, c);
            repaint();
        }
        float getCents(int noteClass) const
        {
            if (noteClass < 0 || noteClass >= 12) return 0.0f;
            return cents[(size_t) noteClass];
        }
        void resetAll()
        {
            for (int i = 0; i < 12; ++i)
            {
                cents[(size_t) i] = 0.0f;
                if (onValueChanged) onValueChanged(i, 0.0f);
            }
            repaint();
        }

        // ── Drawing ───────────────────────────────────────────────────────────
        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
            const float whiteW = bounds.getWidth() / 7.0f;
            const float h      = bounds.getHeight();
            const float blackW = whiteW * 0.62f;
            const float blackH = h * 0.62f;

            // --- White keys first (background layer) ---
            for (int i = 0; i < 7; ++i)
            {
                const int noteClass = kWhiteNotes[i];
                const auto rect = juce::Rectangle<float>(bounds.getX() + i * whiteW,
                                                          bounds.getY(),
                                                          whiteW - 1.0f, h);
                drawKey(g, rect, noteClass, /*isBlack=*/false);
            }

            // --- Black keys overlaid ---
            for (int i = 0; i < 7; ++i)
            {
                const int blackNote = kBlackAfterWhite[i];
                if (blackNote < 0) continue;
                const float cx = bounds.getX() + (i + 1) * whiteW;
                const auto rect = juce::Rectangle<float>(cx - blackW * 0.5f,
                                                          bounds.getY(),
                                                          blackW, blackH);
                drawKey(g, rect, blackNote, /*isBlack=*/true);
            }
        }

        // ── Mouse ─────────────────────────────────────────────────────────────
        void mouseDown(const juce::MouseEvent& e) override
        {
            const int hit = hitTestKey(e.position);
            if (hit < 0) return;
            cycleKey(hit);
        }

    private:
        // Pitch classes for the 7 white keys (C major)
        static constexpr int kWhiteNotes[7]      = { 0, 2, 4, 5, 7, 9, 11 };
        // Black-key pitch class that sits AFTER each white key (or -1 if none)
        static constexpr int kBlackAfterWhite[7] = { 1, 3, -1, 6, 8, 10, -1 };

        std::array<float, 12> cents {};

        void cycleKey(int noteClass)
        {
            const float c = cents[(size_t) noteClass];
            float next = 0.0f;
            if      (std::abs(c) < 0.1f)               next = kHalfFlatCents;
            else if (std::abs(c - kHalfFlatCents) < 0.1f) next = kHalfSharpCents;
            else                                       next = 0.0f;
            cents[(size_t) noteClass] = next;
            if (onValueChanged) onValueChanged(noteClass, next);
            repaint();
        }

        int hitTestKey(juce::Point<float> p) const
        {
            const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
            const float whiteW = bounds.getWidth() / 7.0f;
            const float h      = bounds.getHeight();
            const float blackW = whiteW * 0.62f;
            const float blackH = h * 0.62f;

            // Check black keys first (they sit on top)
            for (int i = 0; i < 7; ++i)
            {
                const int blackNote = kBlackAfterWhite[i];
                if (blackNote < 0) continue;
                const float cx = bounds.getX() + (i + 1) * whiteW;
                const auto rect = juce::Rectangle<float>(cx - blackW * 0.5f,
                                                          bounds.getY(),
                                                          blackW, blackH);
                if (rect.contains(p)) return blackNote;
            }
            // Then white keys
            for (int i = 0; i < 7; ++i)
            {
                const auto rect = juce::Rectangle<float>(bounds.getX() + i * whiteW,
                                                          bounds.getY(),
                                                          whiteW - 1.0f, h);
                if (rect.contains(p)) return kWhiteNotes[i];
            }
            return -1;
        }

        void drawKey(juce::Graphics& g, juce::Rectangle<float> rect,
                     int noteClass, bool isBlack)
        {
            const float c       = cents[(size_t) noteClass];
            const bool  isFlat  = std::abs(c - kHalfFlatCents)  < 0.1f;
            const bool  isSharp = std::abs(c - kHalfSharpCents) < 0.1f;
            const bool  active  = isFlat || isSharp;

            // Base fill
            juce::Colour base = isBlack
                ? juce::Colour::fromRGB(28, 28, 32)
                : juce::Colour::fromRGB(236, 232, 218);
            g.setColour(base);
            g.fillRoundedRectangle(rect, 2.0f);

            // Active overlay
            if (isFlat)
            {
                g.setColour(juce::Colour::fromRGB(196, 132, 61).withAlpha(0.85f)); // amber bronze
                g.fillRoundedRectangle(rect, 2.0f);
            }
            else if (isSharp)
            {
                g.setColour(juce::Colour::fromRGB(80, 160, 180).withAlpha(0.85f)); // aqua
                g.fillRoundedRectangle(rect, 2.0f);
            }

            // Border
            g.setColour(juce::Colour::fromRGB(15, 15, 18));
            g.drawRoundedRectangle(rect, 2.0f, 0.8f);

            // Label
            if (active)
            {
                const juce::String label = isFlat ? juce::String::fromUTF8("\xe2\x99\xad\xc2\xbd")  // ♭½
                                                  : juce::String::fromUTF8("\xe2\x99\xaf\xc2\xbd"); // ♯½
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::jmin(rect.getHeight() * 0.30f, 11.0f),
                                     juce::Font::bold));
                g.drawText(label, rect, juce::Justification::centred);
            }

            // Note name (C, D, etc.) drawn small at the bottom of white keys
            if (! isBlack)
            {
                static const char* const noteLabels[12] = {
                    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
                };
                g.setColour(active ? juce::Colours::white.withAlpha(0.85f)
                                    : juce::Colour::fromRGB(80, 80, 80));
                g.setFont(juce::jmin(rect.getHeight() * 0.18f, 9.0f));
                auto labelArea = rect.removeFromBottom(rect.getHeight() * 0.25f);
                g.drawText(noteLabels[noteClass], labelArea, juce::Justification::centred);
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArabicScaleKeyboard)
    };
} // namespace Betel
