#pragma once
#include <JuceHeader.h>

//==============================================================================
// GrexTransportBar.h
//
// DAW-style transport for the Grex MIDI song recorder / player.  Classic
// transport iconography (record dot, play triangle, stop square) plus SAVE /
// LOAD, drawn as vectors so they stay crisp at any host scale.
//
// Visual language (matches Fanan/Grex: dark + single accent, with the sacred
// transport meter colours):
//   • panel / button fill : near-black greys
//   • REC active          : red    (#D50000)
//   • PLAY active          : green  (#00C853)
//   • accent / hover       : amber  (#CC6600)
//
// The host owns playback; this bar just fires callbacks.  Drive its lamps with
// setArmed/setRecording/setPlaying from the editor timer.
//==============================================================================
class GrexTransportBar : public juce::Component
{
public:
    enum class Icon { Record, Play, Stop, Save, Load };

    // Callbacks — wired by MainComponent.
    std::function<void()> onRecord;   // arm / begin capture
    std::function<void()> onStop;     // stop capture or playback
    std::function<void()> onPlay;     // load+play (or play loaded song)
    std::function<void()> onSave;     // save captured song to .grxsong
    std::function<void()> onLoad;     // load a .grxsong

    GrexTransportBar()
    {
        auto add = [this] (TransportButton& b, Icon ic, juce::Colour accent,
                           std::function<void()>& cb)
        {
            b.icon   = ic;
            b.accent = accent;
            b.onClick = [&cb] { if (cb) cb(); };
            addAndMakeVisible (b);
        };

        add (btnRec,  Icon::Record, kRed,   onRecord);
        add (btnPlay, Icon::Play,   kGreen, onPlay);
        add (btnStop, Icon::Stop,   kAmber, onStop);
        add (btnSave, Icon::Save,   kAmber, onSave);
        add (btnLoad, Icon::Load,   kAmber, onLoad);

        status.setJustificationType (juce::Justification::centredLeft);
        status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.80f));
        status.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
        addAndMakeVisible (status);
        updateStatus();
    }

    //── State from the host (drives the lamps + status readout) ────────────────
    void setArmed (bool b)     { armed = b;     btnRec.active = (armed || recording); refresh(); }
    void setRecording (bool b) { recording = b; btnRec.active = (armed || recording); refresh(); }
    void setPlaying (bool b)   { playing = b;   btnPlay.active = playing;             refresh(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xFF101010));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (0xFF3A3322));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 6);
        const int btn = juce::jmin (b.getHeight(), 40);
        const int gap = 6;

        auto place = [&] (TransportButton& tb) { tb.setBounds (b.removeFromLeft (btn)); b.removeFromLeft (gap); };
        place (btnRec);
        place (btnPlay);
        place (btnStop);
        b.removeFromLeft (gap * 2);          // group divider before file ops
        place (btnSave);
        place (btnLoad);
        b.removeFromLeft (gap * 2);
        status.setBounds (b);                // remaining width = status readout
    }

private:
    static constexpr juce::uint32 kRedRaw   = 0xFFD50000;
    static constexpr juce::uint32 kGreenRaw = 0xFF00C853;
    static constexpr juce::uint32 kAmberRaw = 0xFFCC6600;
    inline static const juce::Colour kRed   { kRedRaw };
    inline static const juce::Colour kGreen { kGreenRaw };
    inline static const juce::Colour kAmber { kAmberRaw };

    //── A single square transport button that paints a vector icon ─────────────
    struct TransportButton : public juce::Button
    {
        TransportButton() : juce::Button ({}) { setClickingTogglesState (false); }

        Icon         icon   = Icon::Stop;
        juce::Colour accent { kAmberRaw };
        bool         active = false;

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.5f);

            // Button body — darker normally, lifts on hover/down, glows accent
            // when active (recording / playing).
            juce::Colour body (0xFF1C1C1C);
            if (down)       body = body.brighter (0.10f);
            else if (over)  body = body.brighter (0.18f);

            g.setColour (body);
            g.fillRoundedRectangle (r, 5.0f);

            const auto edge = active ? accent : juce::Colour (0xFF3A3A3A);
            g.setColour (active ? accent.withAlpha (0.9f) : edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, active ? 1.6f : 1.0f);

            // Icon colour: accent when active, otherwise a soft white that
            // brightens on hover.
            const auto ink = active ? accent
                                    : juce::Colours::white.withAlpha (over ? 0.95f : 0.78f);
            paintIcon (g, r, ink);
        }

        void paintIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
        {
            auto c = r.getCentre();
            const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.38f;
            g.setColour (ink);

            switch (icon)
            {
                case Icon::Record:
                    g.fillEllipse (c.x - s, c.y - s, s * 2.0f, s * 2.0f);
                    break;

                case Icon::Play:
                {
                    juce::Path p;
                    p.addTriangle (c.x - s * 0.85f, c.y - s,
                                   c.x - s * 0.85f, c.y + s,
                                   c.x + s,         c.y);
                    g.fillPath (p);
                    break;
                }

                case Icon::Stop:
                    g.fillRoundedRectangle (c.x - s, c.y - s, s * 2.0f, s * 2.0f, 1.5f);
                    break;

                case Icon::Save:
                {
                    // Down arrow into a tray = "save / write".
                    juce::Path arrow;
                    arrow.addLineSegment ({ c.x, c.y - s, c.x, c.y + s * 0.25f }, s * 0.36f);
                    arrow.addTriangle (c.x - s * 0.7f, c.y + s * 0.05f,
                                       c.x + s * 0.7f, c.y + s * 0.05f,
                                       c.x,            c.y + s * 0.8f);
                    g.fillPath (arrow);
                    g.fillRoundedRectangle (c.x - s, c.y + s * 0.75f, s * 2.0f, s * 0.34f, 1.0f);
                    break;
                }

                case Icon::Load:
                {
                    // Folder outline = "open / load".
                    juce::Path f;
                    const float w = s * 1.9f, h = s * 1.4f;
                    const float x = c.x - w * 0.5f, y = c.y - h * 0.5f;
                    f.startNewSubPath (x, y + h);
                    f.lineTo (x, y + h * 0.28f);
                    f.lineTo (x + w * 0.40f, y + h * 0.28f);
                    f.lineTo (x + w * 0.52f, y);
                    f.lineTo (x + w,         y);
                    f.lineTo (x + w,         y + h);
                    f.closeSubPath();
                    g.strokePath (f, juce::PathStrokeType (juce::jmax (1.4f, s * 0.22f)));
                    break;
                }
            }
        }
    };

    void refresh() { updateStatus(); repaint(); }

    void updateStatus()
    {
        juce::String t = "READY";
        juce::Colour c = juce::Colours::white.withAlpha (0.55f);
        if      (recording) { t = "\xe2\x97\x8f RECORDING"; c = kRed;   }
        else if (armed)     { t = "ARMED \xe2\x80\x94 waiting for first action"; c = kAmber; }
        else if (playing)   { t = "\xe2\x96\xb6 PLAYING";   c = kGreen; }
        status.setText (t, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, c);
    }

    TransportButton btnRec, btnPlay, btnStop, btnSave, btnLoad;
    juce::Label     status;

    bool armed = false, recording = false, playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrexTransportBar)
};
