
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme

#include <JuceHeader.h>
#include "StyleDucker.h"

namespace Betel
{

//==============================================================================
// DUCKER CURVE — two draggable bell points over a log-frequency grid.
//
// THE ONE IDEA THIS DISPLAY HAS TO GET ACROSS: what you SET and what is
// HAPPENING are two different things, and both matter at once.
//
//   • You DRAG a handle to say how deep the cut may go.  That is a ceiling.
//   • The dynamics decide how much of it is used from moment to moment.
//
// So the handle stays where you put it — a hollow ring at your depth, the
// promise — and a filled dot rides between 0 dB and that ring showing the cut
// actually being applied right now.  At rest the dot sits at the top and the
// curve is flat; as the right hand pushes into the band, the dot falls toward
// the ring and the curve opens under it.  That is the pumping.
//
// Drawing only the live curve would hide the setting; drawing only the setting
// would hide the behaviour, and the behaviour is the entire reason the thing
// exists.
//
// X IS LOGARITHMIC because pitch is.  A linear frequency axis puts nine tenths
// of the musically interesting range in the first tenth of the width, and makes
// a point at 200 Hz impossible to place accurately.
//==============================================================================
class DuckerEditor : public juce::Component,
                     private juce::Timer
{
public:
    /** Fires when a point is dragged.  freq in Hz, depth in dB as a POSITIVE
        number (how far down the handle sits). */
    std::function<void(int band, float freqHz, float depthDb)> onPointMoved;

    /** Fires when a point is clicked, so the host can show that point's
        controls.  Also fires on drag start. */
    std::function<void(int band)> onPointSelected;

    explicit DuckerEditor (StyleDucker& d) : ducker (d)
    {
        setOpaque (false);
        // 30 Hz is enough for the pump to read as motion rather than as steps,
        // and cheap: this repaints one small panel and only while the Finisher
        // window is open.
        startTimerHz (30);
    }

    ~DuckerEditor() override { stopTimer(); }

    int  getSelectedBand() const noexcept { return selected; }
    void setSelectedBand (int b) noexcept
    {
        selected = juce::jlimit (0, StyleDucker::kNumBands - 1, b);
        repaint();
    }

    /** Called after the host changes freq/depth from anywhere else. */
    void refresh() { repaint(); }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (juce::Colour (0xFF0C0C0C));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (0xFF2A2A2A));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        auto plot = r.reduced (kAxisW * 0.5f, 8.0f);
        plot.removeFromLeft  (kAxisW * 0.5f);
        plot.removeFromBottom (kAxisH);

        drawGrid (g, plot);

        // ── The two curves ───────────────────────────────────────────────────
        // TARGET first and dim: it is context, not the reading.  LIVE on top and
        // bright, because that is the thing that moves and the eye should go to
        // whatever is moving.
        drawCurve (g, plot, false, juce::Colour (Betel::Pal::kCurveGhost), 1.0f);
        drawCurve (g, plot, true,  juce::Colour (Betel::Pal::kAccentBright), 1.8f);

        for (int b = 0; b < StyleDucker::kNumBands; ++b)
            drawPoint (g, plot, b);

        drawAxisLabels (g, r, plot);
    }

    void resized() override { repaint(); }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hit = bandAtPosition (e.position);
        if (hit < 0) return;

        selected = hit;
        dragging = hit;
        if (onPointSelected) onPointSelected (hit);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;

        const auto plot = plotArea();
        if (plot.isEmpty()) return;

        const float hz = xToFreq (juce::jlimit (plot.getX(), plot.getRight(), e.position.x), plot);
        const float db = yToDepth (juce::jlimit (plot.getY(), plot.getBottom(), e.position.y), plot);

        ducker.setFreq    (dragging, hz);
        ducker.setDepthDb (dragging, db);

        if (onPointMoved) onPointMoved (dragging, hz, db);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

    /** Double-click a point to park it back at zero depth — the fastest way to
        take a band out of play without hunting for its enable. */
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hit = bandAtPosition (e.position);
        if (hit < 0) return;

        ducker.setDepthDb (hit, 0.0f);
        if (onPointMoved) onPointMoved (hit, ducker.getFreq (hit), 0.0f);
        repaint();
    }

private:
    //==========================================================================
    void timerCallback() override
    {
        // REPAINT ONLY WHEN SOMETHING MOVED.  A dynamic display that repaints
        // unconditionally burns a frame's worth of work 30 times a second to
        // draw the same picture, and this panel sits inside a plugin editor
        // that is already spending its budget elsewhere.
        bool changed = false;
        for (int b = 0; b < StyleDucker::kNumBands; ++b)
        {
            const float gr = ducker.getGainReductionDb (b);
            if (std::abs (gr - lastGr[(size_t) b]) > 0.05f)
            {
                lastGr[(size_t) b] = gr;
                changed = true;
            }
        }
        if (changed) repaint();
    }

    juce::Rectangle<float> plotArea() const
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        auto plot = r.reduced (kAxisW * 0.5f, 8.0f);
        plot.removeFromLeft  (kAxisW * 0.5f);
        plot.removeFromBottom (kAxisH);
        return plot;
    }

    //==========================================================================
    // Axis mapping.  One pair of functions, used by both the drawing and the
    // hit testing, so a point can never be drawn somewhere the mouse cannot
    // find it.
    //==========================================================================
    static float freqToNorm (float hz)
    {
        const float lo = std::log10 (StyleDucker::kMinFreqHz);
        const float hi = std::log10 (StyleDucker::kMaxFreqHz);
        return juce::jlimit (0.0f, 1.0f,
                             (std::log10 (juce::jmax (1.0f, hz)) - lo) / (hi - lo));
    }

    static float normToFreq (float n)
    {
        const float lo = std::log10 (StyleDucker::kMinFreqHz);
        const float hi = std::log10 (StyleDucker::kMaxFreqHz);
        return std::pow (10.0f, lo + juce::jlimit (0.0f, 1.0f, n) * (hi - lo));
    }

    static float freqToX (float hz, juce::Rectangle<float> p)
    { return p.getX() + freqToNorm (hz) * p.getWidth(); }

    static float xToFreq (float x, juce::Rectangle<float> p)
    { return normToFreq (p.getWidth() > 0.0f ? (x - p.getX()) / p.getWidth() : 0.0f); }

    /** dB is 0 at the TOP and -kMaxDepthDb at the bottom: only cuts exist here,
        so the whole vertical range is spent on the thing the control does. */
    static float depthToY (float depthDb, juce::Rectangle<float> p)
    {
        const float n = juce::jlimit (0.0f, 1.0f, depthDb / StyleDucker::kMaxDepthDb);
        return p.getY() + n * p.getHeight();
    }

    static float yToDepth (float y, juce::Rectangle<float> p)
    {
        const float n = p.getHeight() > 0.0f ? (y - p.getY()) / p.getHeight() : 0.0f;
        return juce::jlimit (0.0f, StyleDucker::kMaxDepthDb, n * StyleDucker::kMaxDepthDb);
    }

    //==========================================================================
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> p)
    {
        g.setColour (juce::Colour (0xFF1C1C1C));

        static const float decades[] = { 50.f, 100.f, 200.f, 500.f, 1000.f,
                                         2000.f, 5000.f, 10000.f };
        for (float f : decades)
        {
            const float x = freqToX (f, p);
            g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
        }

        for (int db = 6; db <= 24; db += 6)
        {
            const float y = depthToY ((float) db, p);
            g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
        }

        // 0 dB is the reference the whole display hangs off, so it is brighter
        // than the rest of the grid rather than one line among many.
        g.setColour (juce::Colour (0xFF3A3A3A));
        g.drawHorizontalLine ((int) depthToY (0.0f, p), p.getX(), p.getRight());
    }

    /** The summed response of both bells, sampled across the width.
        `live` picks the CURRENT reduction; otherwise the dragged depth. */
    void drawCurve (juce::Graphics& g, juce::Rectangle<float> p,
                    bool live, juce::Colour col, float thickness)
    {
        if (p.getWidth() < 4.0f) return;

        juce::Path path;
        const int steps = juce::jmax (16, (int) p.getWidth());

        for (int i = 0; i <= steps; ++i)
        {
            const float n  = (float) i / (float) steps;
            const float hz = normToFreq (n);

            float totalDb = 0.0f;
            for (int b = 0; b < StyleDucker::kNumBands; ++b)
            {
                if (! ducker.isBandEnabled (b)) continue;

                const float depth = live ? ducker.getGainReductionDb (b)
                                         : ducker.getDepthDb (b);
                if (depth <= 0.001f) continue;

                totalDb += depth * bellShape (hz, ducker.getFreq (b), ducker.getQ (b));
            }

            const float x = p.getX() + n * p.getWidth();
            const float y = depthToY (totalDb, p);
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }

        g.setColour (col);
        g.strokePath (path, juce::PathStrokeType (thickness));
    }

    /** Normalised bell magnitude, 1 at centre, falling either side.  This is a
        DRAWING approximation of the filter, not the filter itself: it only has
        to look like what is heard, and a log-domain gaussian on the same Q does
        that convincingly at a fraction of the cost of evaluating the biquad's
        transfer function per pixel. */
    static float bellShape (float hz, float centreHz, float q)
    {
        const float octaves = std::log2 (juce::jmax (1.0f, hz)
                                         / juce::jmax (1.0f, centreHz));
        const float width   = juce::jmax (0.05f, 1.0f / juce::jmax (0.1f, q));
        const float t       = octaves / width;
        return std::exp (-0.5f * t * t);
    }

    void drawPoint (juce::Graphics& g, juce::Rectangle<float> p, int b)
    {
        const bool on    = ducker.isBandEnabled (b);
        const float hz   = ducker.getFreq (b);
        const float set  = ducker.getDepthDb (b);
        const float liveGr = ducker.getGainReductionDb (b);

        const float x     = freqToX (hz, p);
        const float ySet  = depthToY (set, p);
        const float yLive = depthToY (liveGr, p);

        const bool sel = (b == selected);
        const auto base = on ? juce::Colour (Betel::Pal::kAccentBright) : juce::Colour (0xFF5A5A5A);

        // The RING is the ceiling you dragged to.
        g.setColour (base.withAlpha (sel ? 0.95f : 0.55f));
        g.drawEllipse (x - kHandleR, ySet - kHandleR, kHandleR * 2.0f, kHandleR * 2.0f,
                       sel ? 2.0f : 1.4f);

        // The stem ties the two together, so the eye reads "this dot belongs to
        // that ring" rather than seeing four unrelated marks.
        if (std::abs (ySet - yLive) > 1.0f)
        {
            g.setColour (base.withAlpha (0.28f));
            g.drawLine (x, yLive, x, ySet, 1.0f);
        }

        // The FILLED DOT is what is happening right now.
        g.setColour (base.withAlpha (on ? 1.0f : 0.5f));
        g.fillEllipse (x - kLiveR, yLive - kLiveR, kLiveR * 2.0f, kLiveR * 2.0f);

        g.setColour (juce::Colour (0xFFCCCCCC));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (b + 1),
                    juce::Rectangle<float> (x - 12.0f, ySet - 26.0f, 24.0f, 14.0f),
                    juce::Justification::centred, false);
    }

    void drawAxisLabels (juce::Graphics& g, juce::Rectangle<float> r,
                         juce::Rectangle<float> p)
    {
        g.setColour (juce::Colour (0xFF777777));
        g.setFont (juce::FontOptions (10.0f));

        struct Tick { float hz; const char* label; };
        static const Tick ticks[] = { { 100.f, "100" }, { 500.f, "500" },
                                      { 1000.f, "1k" }, { 5000.f, "5k" },
                                      { 10000.f, "10k" } };
        for (auto& t : ticks)
        {
            const float x = freqToX (t.hz, p);
            g.drawText (t.label,
                        juce::Rectangle<float> (x - 18.0f, p.getBottom() + 2.0f, 36.0f, 12.0f),
                        juce::Justification::centred, false);
        }

        for (int db = 6; db <= 24; db += 6)
            g.drawText ("-" + juce::String (db),
                        juce::Rectangle<float> (r.getX() + 2.0f,
                                                depthToY ((float) db, p) - 6.0f,
                                                kAxisW - 4.0f, 12.0f),
                        juce::Justification::centredLeft, false);
    }

    /** Hit test against the RING, because that is the thing you are aiming at —
        the live dot moves on its own and grabbing it would mean the target
        jumping out from under the cursor. */
    int bandAtPosition (juce::Point<float> pos) const
    {
        const auto p = plotArea();
        int best = -1;
        float bestDist = kHitR;

        for (int b = 0; b < StyleDucker::kNumBands; ++b)
        {
            const float x = freqToX (ducker.getFreq (b), p);
            const float y = depthToY (ducker.getDepthDb (b), p);
            const float d = pos.getDistanceFrom ({ x, y });
            if (d < bestDist) { bestDist = d; best = b; }
        }
        return best;
    }

    static constexpr float kAxisW   = 30.0f;
    static constexpr float kAxisH   = 14.0f;
    static constexpr float kHandleR = 7.0f;
    static constexpr float kLiveR   = 4.0f;
    static constexpr float kHitR    = 18.0f;

    StyleDucker& ducker;
    int   selected = 0;
    int   dragging = -1;
    std::array<float, (size_t) StyleDucker::kNumBands> lastGr { { 0.0f, 0.0f } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuckerEditor)
};

} // namespace Betel




