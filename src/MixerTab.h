

#pragma once
//==============================================================================
// MixerTab.h  —  Mixing console for Betelgeuse.
//
// Three framed sections (formation unchanged):
//   • LEFT HAND / STYLE  : 8 horizontal channel faders + STYLE VOLUME under them
//   • RIGHT HAND / SOLO  : 8 horizontal channel faders + SOLO  VOLUME under them
//   • MASTER             : a single VERTICAL fader
//
// EVERY fader uses one normalised LINEAR scale — 255 steps, 0..254, matched
// 1:1 to MIDI CC 7:
//   0 = silence   ·   127 = unity (centre)   ·   254 = unity × 2
// linear gain = value / 127.  The value readout shows the 0..254 integer.
// Style CC 7 writes the STYLE channel faders directly and UNCHANGED — the
// style's raw 0..127 volume IS the fader value, so a style tops out at unity
// and 128..254 is purely the user's headroom.
//
// Channel + bus faders are horizontal (name at LEFT, value at RIGHT); the
// master fader is vertical (name on top, value at bottom).
//==============================================================================

#include <JuceHeader.h>
#include "InstrEditPanel.h"   // for InstrEditStyle::paintComponentFrame

namespace Betel
{
    //==========================================================================
    // MixerFader — 0..200 linear stepped fader, horizontal or vertical
    //==========================================================================
    class MixerFader : public juce::Component
    {
    public:
        std::function<void(float linearGain)> onChange;

        /** Optional. When set, a LEFT double-click fires this instead of
            snapping to unity, and RIGHT double-click keeps the snap.

            Same split the instrument GAIN handle uses, and for the same reason:
            a fader that only had one double-click gesture would have to give up
            "back to unity" to gain a type-in box, and reaching unity again is
            the more common of the two by a wide margin. */
        std::function<void()> onLeftDoubleClick;

        MixerFader()
        {
            setOpaque(false);
            // Cache paint offscreen; under the OpenGL continuous-repaint loop the
            // gradients + AA edges would otherwise re-rasterise (and shimmer)
            // every frame.  Invalidated automatically by repaint() on change.
            setBufferedToImage(true);
        }

        // ── Config ────────────────────────────────────────────────────────────
        void setVertical (bool v)              { vertical = v;  repaint(); }
        void setLabel    (const juce::String& s){ label = s;    repaint(); }

        // ── State ─────────────────────────────────────────────────────────────
        // One normalised linear scale, 255 steps: value 0..254, gain = value/127
        // (0 = silence, 127 = unity at the centre tick, 254 = unity × 2 —
        // matched 1:1 to MIDI CC 7).  Fader position 0..1 = value/254.
        void  setLinearGain (float linear, bool notify = false)
        {
            setStep (juce::roundToInt (juce::jlimit (0.0f, 2.0f, linear) * 127.0f), notify);
        }
        float getLinearGain() const { return (float) step / 127.0f; }
        int   getStepValue()  const { return step; }
        bool  isDragging()    const { return dragging; }

        void paint (juce::Graphics& g) override
        {
            if (vertical) paintVertical (g);
            else          paintHorizontal (g);
        }

        // ── Mouse ─────────────────────────────────────────────────────────────
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = true;
            dragRef  = vertical ? e.y : e.x;
            dragPos  = pos;
            setMouseCursor (vertical ? juce::MouseCursor::UpDownResizeCursor
                                     : juce::MouseCursor::LeftRightResizeCursor);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float span = vertical ? trackSpanV() : trackSpanH();
            if (span < 1.0f) return;
            // Vertical: up = increase (negative dy).  Horizontal: right = increase.
            const float delta = vertical ? (float)(dragRef - e.y) / span
                                         : (float)(e.x - dragRef) / span;
            const float p = juce::jlimit (0.0f, 1.0f, dragPos + delta);
            setStep (juce::roundToInt (p * 254.0f), true);
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            dragging = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (onLeftDoubleClick && ! e.mods.isRightButtonDown())
            {
                onLeftDoubleClick();
                return;
            }
            setStep (127, true);         // centre = unity
        }

    private:
        // ── Value / position ──────────────────────────────────────────────────
        juce::String valueText() const { return juce::String (step); }

        void setStep (int v, bool notify)
        {
            v = juce::jlimit (0, 254, v);
            const float p = (float) v / 254.0f;
            if (v != step) { step = v; pos = p; repaint(); }
            if (notify && onChange) onChange (getLinearGain());
        }

        // ── Geometry helpers (horizontal) ─────────────────────────────────────
        float labelW() const { return juce::jmin ((float) getWidth() * 0.34f, 96.0f); }
        float valueW() const { return juce::jmin ((float) getWidth() * 0.18f, 58.0f); }
        float trackSpanH() const { return (float) getWidth() - labelW() - valueW() - 2.0f * kPad; }
        // ── Geometry helpers (vertical) ───────────────────────────────────────
        float labelH() const { return 20.0f; }
        float valueH() const { return 18.0f; }
        float trackSpanV() const { return (float) getHeight() - labelH() - valueH() - 2.0f * kPad; }

        // ── Painters ──────────────────────────────────────────────────────────
        void paintHorizontal (juce::Graphics& g)
        {
            const float W = (float) getWidth(), H = (float) getHeight();
            if (W < 70.0f || H < 14.0f) return;

            const float lblW = labelW(), valW = valueW();
            const float x0 = lblW + kPad;
            const float w  = trackSpanH();
            if (w < 20.0f) return;
            const float midY = H * 0.5f;

            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.setFont   (juce::Font (13.0f, juce::Font::bold));
            g.drawFittedText (label, 3, 0, (int) lblW - 4, (int) H, juce::Justification::centredLeft, 1);

            const juce::Rectangle<float> track (x0, midY - kTrackT * 0.5f, w, kTrackT);
            paintTrack (g, track, true);

            const float unityX = x0 + w * 0.5f;
            g.setColour (juce::Colour (0xFFE6A059).withAlpha (0.55f));
            g.drawLine (unityX, midY - kTrackT * 0.5f - 4.0f, unityX, midY + kTrackT * 0.5f + 4.0f, 1.0f);

            const float handleX = x0 + pos * w;
            if (pos > 0.001f)
                paintFill (g, juce::Rectangle<float> (x0, track.getY(), handleX - x0, kTrackT), true);

            paintHandle (g, handleX, midY, H * 0.42f);

            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont   (juce::Font (12.0f, juce::Font::bold));
            g.drawFittedText (valueText(), (int)(W - valW), 0, (int) valW - 2, (int) H, juce::Justification::centred, 1);
        }

        void paintVertical (juce::Graphics& g)
        {
            const float W = (float) getWidth(), H = (float) getHeight();
            if (W < 24.0f || H < 60.0f) return;

            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.setFont   (juce::Font (13.0f, juce::Font::bold));
            g.drawFittedText (label, 0, 0, (int) W, (int) labelH(), juce::Justification::centred, 1);

            const float y0 = labelH() + kPad;          // top of track (max)
            const float h  = trackSpanV();
            if (h < 20.0f) return;
            const float midX = W * 0.5f;

            const juce::Rectangle<float> track (midX - kTrackT * 0.5f, y0, kTrackT, h);
            paintTrack (g, track, false);

            const float unityY = y0 + h * 0.5f;
            g.setColour (juce::Colour (0xFFE6A059).withAlpha (0.55f));
            g.drawLine (midX - kTrackT * 0.5f - 4.0f, unityY, midX + kTrackT * 0.5f + 4.0f, unityY, 1.0f);

            const float handleY = y0 + (1.0f - pos) * h;   // pos 1 = top
            if (pos > 0.001f)
                paintFill (g, juce::Rectangle<float> (track.getX(), handleY, kTrackT, track.getBottom() - handleY), false);

            paintHandle (g, midX, handleY, W * 0.40f);

            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont   (juce::Font (12.0f, juce::Font::bold));
            g.drawFittedText (valueText(), 0, (int)(H - valueH()), (int) W, (int) valueH(), juce::Justification::centred, 1);
        }

        void paintTrack (juce::Graphics& g, juce::Rectangle<float> r, bool horiz)
        {
            const float a = horiz ? r.getY() - 1.0f : r.getX() - 1.0f;
            const float b = horiz ? r.getBottom() + 1.0f : r.getRight() + 1.0f;
            juce::ColourGradient grad (juce::Colour (0xFF2E2E2E), horiz ? 0.0f : a, horiz ? a : 0.0f,
                                       juce::Colour (0xFF2E2E2E), horiz ? 0.0f : b, horiz ? b : 0.0f, false);
            grad.addColour (0.5, juce::Colour (0xFF020202));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (r, kTrackT * 0.5f);
            g.setColour (juce::Colour (0xFF000000).withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (0.5f), kTrackT * 0.5f - 0.5f, 0.8f);
        }
        void paintFill (juce::Graphics& g, juce::Rectangle<float> r, bool horiz)
        {
            if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f) return;
            const float a = horiz ? r.getY() - 1.0f : r.getX() - 1.0f;
            const float b = horiz ? r.getBottom() + 1.0f : r.getRight() + 1.0f;
            juce::ColourGradient grad (juce::Colour (0xFFF0B265), horiz ? 0.0f : a, horiz ? a : 0.0f,
                                       juce::Colour (0xFF6E4419), horiz ? 0.0f : b, horiz ? b : 0.0f, false);
            grad.addColour (0.5, juce::Colour (0xFFB87A36));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (r, kTrackT * 0.5f);
        }
        void paintHandle (juce::Graphics& g, float cx, float cy, float radius)
        {
            const float hR = juce::jmin (12.0f, radius);
            g.setColour (juce::Colour (0xFF101010));
            g.fillEllipse (cx - hR, cy - hR, hR * 2.0f, hR * 2.0f);
            g.setColour (juce::Colour (0xFFE6A059).withAlpha (0.55f));
            g.drawEllipse (cx - hR + 0.5f, cy - hR + 0.5f, hR * 2.0f - 1.0f, hR * 2.0f - 1.0f, 1.0f);
            g.setColour (juce::Colour (0xFFD89855));
            g.fillEllipse (cx - kInnerR, cy - kInnerR, kInnerR * 2.0f, kInnerR * 2.0f);
        }

        static constexpr float kTrackT = 10.0f;
        static constexpr float kInnerR = 5.0f;
        static constexpr float kPad    = 6.0f;

        bool         vertical = false;
        juce::String label;
        int          step    = 127;    // 0..254, gain = step/127 (127 = unity)
        float        pos     = 0.5f;   // step/254 — cached for the painters
        float        dragPos = 0.5f;
        int          dragRef = 0;
        bool         dragging = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerFader)
    };

    //==========================================================================
    // MixerTab — the full mixing console
    //==========================================================================
    class MixerTab : public juce::Component,
                     private juce::Timer
    {
    public:
        std::function<void(int slot, float linearGain)> onStyleChannelGainChanged;
        std::function<void(int slot, float linearGain)> onSoloChannelGainChanged;
        std::function<void(float linearGain)>           onMasterGainChanged;
        std::function<void(float boostDb)>              onMasterBoostChanged;    // MASTER boost tickboxes

        // ── FINISHER (master-bus chain) ──────────────────────────────────────
        // The strip carries only the on/off and the "open the editor" button —
        // every parameter lives in FinisherWindow.
        std::function<void(bool)>                       onFinisherEnabledChanged;
        std::function<void()>                           onFinisherEditRequested;
        std::function<void(float linearGain)>           onStyleBusGainChanged;   // STYLE VOLUME
        std::function<void(float linearGain)>           onSoloBusGainChanged;    // SOLO  VOLUME

        // Live fader reflection: the host supplies the style channels' current
        // engine gain; a per-section CC 7 ride (audio thread) then shows up on
        // the visible fader within one poll tick.  A fader being dragged is
        // never overwritten mid-drag; the style's next ride wins afterwards
        // (last writer wins, by design).
        std::function<float(int slot)> styleGainProvider;

        MixerTab()
        {
            setBufferedToImage(true);
            startTimerHz (10);   // live fader reflection of style CC 7 rides

            for (int i = 0; i < 8; ++i)
            {
                addAndMakeVisible (styleFaders[i]);
                styleFaders[i].setLabel (kStyleDefaults[i]);
                styleFaders[i].onChange = [this, i](float)
                {
                    if (onStyleChannelGainChanged)
                        onStyleChannelGainChanged (i, styleFaders[i].getLinearGain());
                };

                addAndMakeVisible (soloFaders[i]);
                soloFaders[i].setLabel ("SOLO " + juce::String (i + 1));
                soloFaders[i].onChange = [this, i](float)
                {
                    if (onSoloChannelGainChanged)
                        onSoloChannelGainChanged (i, soloFaders[i].getLinearGain());
                };
            }

            // ── Bus-volume faders (-10..+10 dB) ───────────────────────────────
            addAndMakeVisible (styleVolumeFader);
            styleVolumeFader.setLabel ("STYLE VOLUME");
            styleVolumeFader.onChange = [this](float)
            {
                if (onStyleBusGainChanged) onStyleBusGainChanged (styleVolumeFader.getLinearGain());
            };

            addAndMakeVisible (soloVolumeFader);
            soloVolumeFader.setLabel ("SOLO VOLUME");
            soloVolumeFader.onLeftDoubleClick = [this]
            { if (onSoloBaseUnityRequested) onSoloBaseUnityRequested(); };
            soloVolumeFader.onChange = [this](float)
            {
                if (onSoloBusGainChanged) onSoloBusGainChanged (soloVolumeFader.getLinearGain());
            };

            // ── Master fader (VERTICAL) ───────────────────────────────────────
            addAndMakeVisible (masterFader);
            masterFader.setVertical (true);
            masterFader.setLabel ("MASTER");
            masterFader.onChange = [this](float)
            {
                if (onMasterGainChanged) onMasterGainChanged (masterFader.getLinearGain());
            };

            // ── FINISHER (master-bus chain) ───────────────────────────────────
            // Two controls in the strip under the master fader: an ON/OFF, and
            // the FINISHER button that opens the editor window.  A thin
            // gain-reduction meter sits beneath them so you can see it working
            // without opening anything.
            // On/off is a small LED, not a labelled button: red = bypassed,
            // green = engaged.  That frees the whole strip width for the
            // FINISHER button, whose label was being truncated to "FINIS...".
            finisherLed.setClickingTogglesState (true);
            finisherLed.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF5A1A1A));
            finisherLed.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF2FA84F));
            finisherLed.onClick = [this]
            {
                if (onFinisherEnabledChanged)
                    onFinisherEnabledChanged (finisherLed.getToggleState());
                repaint();
            };
            addAndMakeVisible (finisherLed);

            finisherBtn.setButtonText ("FINISHER");
            finisherBtn.onClick = [this]
            {
                if (onFinisherEditRequested) onFinisherEditRequested();
            };
            addAndMakeVisible (finisherBtn);

            // ── Master BOOST tickboxes (0 / +3 / +6 / +9 / +12 dB) ────────────
            // Stacked down the right of the master fader, inside the MASTER
            // frame.  Mutually exclusive (a radio group): a single dB boost
            // applied on top of the master fader in the processor.  Default +6.
            for (int i = 0; i < (int) boostButtons.size(); ++i)
            {
                auto& b = boostButtons[(size_t) i];
                b.setButtonText (kBoostLabels[i]);
                b.setRadioGroupId (kBoostRadioGroup);
                b.setClickingTogglesState (true);
                b.setColour (juce::ToggleButton::textColourId,         juce::Colour (0xffd6dde3));
                b.setColour (juce::ToggleButton::tickColourId,         juce::Colour (0xff34c0a8));
                b.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff5a6470));
                b.onClick = [this, i]
                {
                    // Radio group calls onClick for the newly-selected button.
                    if (boostButtons[(size_t) i].getToggleState() && onMasterBoostChanged)
                        onMasterBoostChanged ((float) kBoostDb[i]);
                };
                addAndMakeVisible (b);
            }
            boostButtons[(size_t) kDefaultBoostIndex].setToggleState (true, juce::dontSendNotification);
        }

        // ── Public API ───────────────────────────────────────────────────────
        void setStyleSlotLabel (int slot, const juce::String& nameOrEmpty)
        {
            if (slot < 0 || slot >= 8) return;
            styleFaders[slot].setLabel (nameOrEmpty.isEmpty()
                                          ? juce::String (kStyleDefaults[slot]) : nameOrEmpty);
        }
        void setSoloSlotLabel (int slot, const juce::String& nameOrEmpty)
        {
            if (slot < 0 || slot >= 8) return;
            soloFaders[slot].setLabel (nameOrEmpty.isEmpty()
                                         ? "SOLO " + juce::String (slot + 1) : nameOrEmpty);
        }
        void setStyleSlotGain (int slot, float linearGain)
        {
            if (slot >= 0 && slot < 8) styleFaders[slot].setLinearGain (linearGain);
        }
        void setSoloSlotGain (int slot, float linearGain)
        {
            if (slot >= 0 && slot < 8) soloFaders[slot].setLinearGain (linearGain);
        }
        void setMasterGain   (float linearGain) { masterFader.setLinearGain (linearGain); }
        void setStyleBusGain (float linearGain) { styleVolumeFader.setLinearGain (linearGain); }

        // ── FINISHER: engine → UI ────────────────────────────────────────────
        void setFinisherEnabled (bool on)
        {
            finisherLed.setToggleState (on, juce::dontSendNotification);
            repaint();
        }

        /** Feed the gain-reduction meter.  Called from the UI timer; repaints
            only the meter strip, never the whole console. */
        void setFinisherGainReductionDb (float db)
        {
            if (std::abs (db - finisherGrDb) < 0.1f) return;   // ignore meter jitter
            finisherGrDb = db;
            if (! finisherMeterArea.isEmpty()) repaint (finisherMeterArea);
        }
        void setSoloBusGain  (float linearGain) { soloVolumeFader.setLinearGain (linearGain); }

        /** Double-clicking the SOLO VOLUME fader asks for its BASE UNITY -- what
            the 127 detent is worth in dB.  The mixer does not own the number and
            does not store it; it only reports the gesture. */
        std::function<void()> onSoloBaseUnityRequested;

        /** Light the tickbox matching the processor's current master boost
            (nearest of 0/+3/+6/+9/+12).  Silent — never fires onMasterBoostChanged.
            Used to reflect state when the window is reopened. */
        void setMasterBoostDb (float db)
        {
            const int target = juce::roundToInt (db);
            int best = kDefaultBoostIndex, bestErr = 1 << 30;
            for (int i = 0; i < (int) boostButtons.size(); ++i)
            {
                int e = kBoostDb[i] - target; if (e < 0) e = -e;
                if (e < bestErr) { bestErr = e; best = i; }
            }
            boostButtons[(size_t) best].setToggleState (true, juce::dontSendNotification);
        }

        // ── Drawing ──────────────────────────────────────────────────────────
        void paint (juce::Graphics& g) override
        {
            const auto f = sectionBounds();
            InstrEditStyle::paintComponentFrame (g, f.style,  "LEFT HAND  /  STYLE");
            InstrEditStyle::paintComponentFrame (g, f.solo,   "RIGHT HAND  /  SOLO");
            InstrEditStyle::paintComponentFrame (g, f.master, "MASTER");

            // FINISHER gain-reduction meter: fills RIGHT-to-LEFT, the way a GR
            // meter should, so "more bar" reads as "working harder".  Scaled
            // over 0..kMeterRangeDb; amber until it is leaning on the signal,
            // red past kMeterHotDb so heavy limiting is obvious at a glance.
            if (! finisherMeterArea.isEmpty())
            {
                g.setColour (juce::Colour (0xFF101010));
                g.fillRect (finisherMeterArea);

                if (finisherLed.getToggleState() && finisherGrDb > 0.05f)
                {
                    const float norm = juce::jlimit (0.0f, 1.0f, finisherGrDb / kMeterRangeDb);
                    auto meterF = finisherMeterArea.toFloat();
                    auto bar    = meterF.removeFromRight (meterF.getWidth() * norm);
                    g.setColour (finisherGrDb >= kMeterHotDb ? juce::Colour (0xFFCC3322)
                                                             : juce::Colour (0xFFD4AF37));
                    g.fillRect (bar);
                }
            }
        }

        void resized() override
        {
            const auto f = sectionBounds();
            layoutSide (styleFaders, styleVolumeFader, InstrEditStyle::componentFrameContent (f.style));
            layoutSide (soloFaders,  soloVolumeFader,  InstrEditStyle::componentFrameContent (f.solo));

            // Master frame: vertical fader on the left, the boost tickbox column
            // (0/+3/+6/+9/+12) stacked down the right, inside the same frame.
            // The bottom kFinisherStripH px are reserved for the FINISHER — the
            // fader and the boost column are both shortened to free it.
            auto m = InstrEditStyle::componentFrameContent (f.master);

            auto finStrip = m.removeFromBottom (juce::jmin (kFinisherStripH,
                                                            juce::jmax (0, m.getHeight() / 3)));
            m.removeFromBottom (4);   // breathing room above the strip

            const int boostW = 50, colGap = 6;
            // THE 0 / +3 / +6 / +9 / +12 TICKBOXES ARE GONE.
            //
            // A second gain stage beside the master fader, in fixed 3 dB steps,
            // doing what the fader already does continuously - and it travelled
            // in the set as its own value, so two controls decided one level.
            // The fader takes the whole column back.
            for (auto& b : boostButtons)
            {
                b.setVisible (false);
                b.setBounds (0, 0, 0, 0);
            }
            masterFader.setBounds (m);

            // FINISHER strip: [ on/off ] [ AMOUNT knob ] [ CHARACTER ]
            // with a thin gain-reduction meter drawn under it (see paint()).
            {
                auto row = finStrip.reduced (2, 0);
                finisherMeterArea = row.removeFromBottom (6).reduced (1, 1);
                row.removeFromBottom (3);

                // Small square LED on the left, FINISHER button takes the rest.
                const int led = juce::jmin (18, row.getHeight() - 10);
                auto ledArea = row.removeFromLeft (led + 4);
                finisherLed.setBounds (ledArea.withSizeKeepingCentre (led, led));
                row.removeFromLeft (2);
                finisherBtn.setBounds (row.reduced (1, 4));
            }
        }

    private:
        struct Sections { juce::Rectangle<int> style, solo, master; };

        Sections sectionBounds() const
        {
            const int W = getWidth(), H = getHeight();
            const int pad = 8, gap = 10, masterW = 130;
            const int sideW = (W - 2 * pad - 2 * gap - masterW) / 2;
            const int y = pad, h = H - 2 * pad;
            return { { pad,                              y, sideW,   h },
                     { pad + sideW + gap,                y, sideW,   h },
                     { pad + sideW + gap + sideW + gap,  y, masterW, h } };
        }

        // 8 channel faders stacked, then the bus-volume fader UNDER them (extra
        // separation), all full-width horizontal rows.
        void layoutSide (std::array<MixerFader, 8>& row, MixerFader& bus, juce::Rectangle<int> inner)
        {
            const int gap = 3, busGap = 10;
            const int rowH = (inner.getHeight() - (7 * gap + busGap)) / 9;
            if (rowH < 4) return;
            int yy = inner.getY();
            for (int i = 0; i < 8; ++i)
            {
                row[i].setBounds (inner.getX(), yy, inner.getWidth(), rowH);
                yy += rowH + (i < 7 ? gap : busGap);
            }
            bus.setBounds (inner.getX(), yy, inner.getWidth(), rowH);
        }

        static constexpr const char* kStyleDefaults[8] = {
            "DRUMS", "PERC", "BASS", "CHORD 1", "CHORD 2", "PAD", "LEAD 1", "LEAD 2"
        };

        std::array<MixerFader, 8> styleFaders;
        std::array<MixerFader, 8> soloFaders;
        static constexpr int      kFinisherStripH = 46;    // height freed in the MASTER frame
        static constexpr float    kMeterRangeDb   = 12.0f; // full-scale of the GR meter
        static constexpr float    kMeterHotDb     = 6.0f;  // amber → red above this

        juce::Rectangle<int>      finisherMeterArea;
        juce::TextButton          finisherLed, finisherBtn;
        float                     finisherGrDb = 0.0f;   // meter value, set by the UI timer

        MixerFader                styleVolumeFader;
        MixerFader                soloVolumeFader;
        MixerFader                masterFader;

        // ── Master boost tickboxes (0/+3/+6/+9/+12 dB, radio group, +6 default) ──
        static constexpr int kBoostRadioGroup   = 0x4207;   // unique within this component
        static constexpr int kDefaultBoostIndex = 0;        // 0 dB
        static constexpr int kBoostDb[5]         = { 0, 3, 6, 9, 12 };
        static constexpr const char* kBoostLabels[5] = { "0", "+3", "+6", "+9", "+12" };
        std::array<juce::ToggleButton, 5> boostButtons;

        // Mirror the engine's style-channel gains onto the faders (message
        // thread).  Skips a fader mid-drag; setLinearGain does not notify, so
        // this can never loop back into onStyleChannelGainChanged.
        void timerCallback() override
        {
            if (! styleGainProvider) return;
            for (int i = 0; i < 8; ++i)
            {
                if (styleFaders[(size_t) i].isDragging()) continue;
                const float g = styleGainProvider (i);
                if (std::abs (g - styleFaders[(size_t) i].getLinearGain()) > 0.004f)
                    styleFaders[(size_t) i].setLinearGain (g);
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerTab)
    };
} // namespace Betel

using MixerTab = Betel::MixerTab;
