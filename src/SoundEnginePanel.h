#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "InstrEditPanel.h"     // SlotParams, GoldSlider, InstrEditStyle
#include "OscEngine.h"

//==============================================================================
//  SOUND ENGINE - the first page of the sound editor.
//
//  On the programs the built-in engine plays (GM 38 Synth Bass 1, 39 Synth
//  Bass 2) this page IS the sound source: two oscillators, the Moog ladder and
//  its envelope.  Everything else in the window - SYNTHESIS, MODULATION, the
//  sends and inserts - shapes the engine exactly as it shapes a sample.  On any
//  other program the page says the sound plays samples and edits nothing.
//
//  Stored in SlotParams::engineSpec as oscengine text, EMPTY = the factory
//  sound, so an untouched slot costs a set nothing and follows factory
//  improvements.  The text names the program it was made on: edits made on
//  Synth Bass 1 never land on Synth Bass 2.
//==============================================================================
class SoundEnginePanel : public juce::Component
{
public:
    std::function<void()> onAnythingChanged;

    SoundEnginePanel()
    {
        statusLbl.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        statusLbl.setColour (juce::Label::textColourId, juce::Colour (InstrEditStyle::kPanelLabel));
        addAndMakeVisible (statusLbl);

        noteLbl.setJustificationType (juce::Justification::centred);
        noteLbl.setColour (juce::Label::textColourId, juce::Colour (0xFF8A8A8A));
        noteLbl.setFont (juce::Font (juce::FontOptions (14.0f)));
        noteLbl.setText ("This sound plays samples.  The sound engine plays Synth Bass 1 and Synth Bass 2.",
                         juce::dontSendNotification);
        addChildComponent (noteLbl);

        factoryBtn.setButtonText ("FACTORY");
        factoryBtn.setTooltip ("Back to this sound's factory engine settings");
        factoryBtn.onClick = [this]
        {
            showParams (oscengine::factory (program));
            touched = true;
            notify();
        };
        InstrEditStyle::styleSquareButton (factoryBtn, false);
        addAndMakeVisible (factoryBtn);

        syncBtn.setButtonText ("SYNC");
        syncBtn.setTooltip ("Hard-sync oscillator 2 to oscillator 1");
        syncBtn.onClick = [this]
        {
            syncOn = ! syncOn;
            InstrEditStyle::styleSquareButton (syncBtn, syncOn);
            userEdit();
        };
        InstrEditStyle::styleSquareButton (syncBtn, false);
        addAndMakeVisible (syncBtn);

        for (int i = 0; i < 2; ++i)
        {
            wave [(size_t) i].displayFn = [] (float v) { return juce::String (oscengine::waveName ((int) std::lround (v))); };
            oct  [(size_t) i].displayFn = [] (float v) { const int o = (int) std::lround (v); return (o > 0 ? "+" : "") + juce::String (o); };
            semi [(size_t) i].displayFn = [] (float v) { const int s = (int) std::lround (v); return (s > 0 ? "+" : "") + juce::String (s); };
            fine [(size_t) i].displayFn = [] (float v) { const int c = (int) std::lround (v); return (c > 0 ? "+" : "") + juce::String (c) + " c"; };
            pw   [(size_t) i].displayFn = [] (float v) { return juce::String ((int) std::lround (v)) + " %"; };
        }
        envAmt.displayFn = [] (float v) { const int e = (int) std::lround (v); return (e > 0 ? "+" : "") + juce::String (e); };
        cutoff.displayFn = [] (float v)
        {
            const double hz = 20.0 * std::pow (900.0, (double) v / 100.0);
            return hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + " k" : juce::String ((int) hz) + " Hz";
        };
        auto timeText = [] (float seconds)
        {
            return seconds < 1.0f ? juce::String ((int) std::lround (seconds * 1000.0f)) + " ms"
                                  : juce::String (seconds, 2) + " s";
        };
        envA.displayFn = [timeText] (float v) { return timeText (oscengine::envTimeAttack (v / 100.0f)); };
        envD.displayFn = [timeText] (float v) { return timeText (oscengine::envTimeDecay  (v / 100.0f)); };
        envR.displayFn = [timeText] (float v) { return timeText (oscengine::envTimeDecay  (v / 100.0f)); };

        for (auto* s : allSliders())
        {
            s->setStep (1.0f);
            s->onChange = [this] (float) { userEdit(); };
            addAndMakeVisible (*s);
        }
        setProgram (-1);
    }

    //==========================================================================
    /** The GM program the edited slot plays. */
    void setProgram (int gmProgram)
    {
        program = gmProgram;
        const bool on = oscengine::servesProgram (program);
        for (auto* s : allSliders()) s->setVisible (on);
        syncBtn.setVisible (on);
        factoryBtn.setVisible (on);
        noteLbl.setVisible (! on);
        statusLbl.setText (program == 38 ? "SYNTH BASS 1  -  SOUND ENGINE"
                         : program == 39 ? "SYNTH BASS 2  -  SOUND ENGINE"
                                         : "SOUND ENGINE", juce::dontSendNotification);
        if (on) showSpec (loadedSpec);
        repaint();
    }

    void loadParams (const SlotParams& p)
    {
        loadedSpec = p.engineSpec;
        touched = false;
        if (oscengine::servesProgram (program))
            showSpec (loadedSpec);
    }

    /** Writes the engine settings back - but only when there is something of
        this page's to write.  On a sample program nothing; and the slot's
        existing text is never overwritten by a page that merely SHOWED a
        different program's factory sound. */
    void readInto (SlotParams& p) const
    {
        if (! oscengine::servesProgram (program)) return;
        if (! touched && oscengine::programOf (p.engineSpec.toStdString()) != program) return;
        const auto e = currentParams();
        p.engineSpec = oscengine::isFactory (e) ? juce::String() : juce::String (oscengine::toString (e));
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        if (! oscengine::servesProgram (program)) return;
        g.setColour (juce::Colour (InstrEditStyle::kPanelLabel));
        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
        const char* names[3] = { "OSC 1", "OSC 2", "LADDER" };
        for (int r = 0; r < 3; ++r)
            g.drawText (names[r], rowLabel[(size_t) r], juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto head = r.removeFromTop (28);
        factoryBtn.setBounds (head.removeFromRight (110).reduced (0, 3));
        statusLbl.setBounds (head);
        noteLbl.setBounds (r);
        r.removeFromTop (6);

        const int rowH = r.getHeight() / 3;
        const std::array<std::vector<juce::Component*>, 3> rows =
        {{
            { &wave[0], &oct[0], &semi[0], &fine[0], &pw[0], &level[0], &sub, &noise, &fm },
            { &wave[1], &oct[1], &semi[1], &fine[1], &pw[1], &level[1], &ring, &syncBtn, &drive },
            { &cutoff, &reso, &keyTrack, &envAmt, &velAmt, &envA, &envD, &envS, &envR, &outLevel }
        }};
        for (int i = 0; i < 3; ++i)
        {
            auto row = r.removeFromTop (rowH);
            rowLabel[(size_t) i] = row.removeFromLeft (64);
            const int n = 10;                                   // one grid for all rows
            const int w = row.getWidth() / n;
            for (size_t c = 0; c < rows[(size_t) i].size(); ++c)
            {
                auto cell = row.removeFromLeft (w).reduced (3, 2);
                if (rows[(size_t) i][c] == &syncBtn)
                    syncBtn.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 70), 26));
                else
                    rows[(size_t) i][c]->setBounds (cell);
            }
        }
    }

private:
    void notify() { if (onAnythingChanged) onAnythingChanged(); }

    void userEdit()
    {
        if (loading) return;
        touched = true;
        notify();
    }

    void showSpec (const juce::String& spec)
    {
        oscengine::Params e;
        if (! (oscengine::fromString (spec.toStdString(), e) && e.program == program))
            e = oscengine::factory (program);
        showParams (e);
    }

    void showParams (const oscengine::Params& e)
    {
        const juce::ScopedValueSetter<bool> guard (loading, true);
        for (int i = 0; i < 2; ++i)
        {
            wave [(size_t) i].setValue ((float) e.wave[i]);
            oct  [(size_t) i].setValue ((float) e.oct[i]);
            semi [(size_t) i].setValue ((float) e.semi[i]);
            fine [(size_t) i].setValue (e.fine[i]);
            pw   [(size_t) i].setValue (e.pw[i] * 100.0f);
            level[(size_t) i].setValue (e.level[i] * 100.0f);
        }
        fm.setValue (e.fm * 100.0f);       ring.setValue (e.ring * 100.0f);
        sub.setValue (e.sub * 100.0f);     noise.setValue (e.noise * 100.0f);
        drive.setValue (e.drive * 100.0f);
        cutoff.setValue (e.cutoff * 100.0f);     reso.setValue (e.reso * 100.0f);
        keyTrack.setValue (e.keyTrack * 100.0f); envAmt.setValue (e.envAmt * 100.0f);
        velAmt.setValue (e.velAmt * 100.0f);
        envA.setValue (e.envA * 100.0f);  envD.setValue (e.envD * 100.0f);
        envS.setValue (e.envS * 100.0f);  envR.setValue (e.envR * 100.0f);
        outLevel.setValue (e.outLevel * 100.0f);
        syncOn = e.sync;
        InstrEditStyle::styleSquareButton (syncBtn, syncOn);
    }

    oscengine::Params currentParams() const
    {
        oscengine::Params e = oscengine::factory (program);
        for (int i = 0; i < 2; ++i)
        {
            e.wave [i] = (int) std::lround (wave[(size_t) i].getValue());
            e.oct  [i] = (int) std::lround (oct [(size_t) i].getValue());
            e.semi [i] = (int) std::lround (semi[(size_t) i].getValue());
            e.fine [i] = std::round (fine[(size_t) i].getValue());
            e.pw   [i] = pw   [(size_t) i].getValue() / 100.0f;
            e.level[i] = level[(size_t) i].getValue() / 100.0f;
        }
        e.fm = fm.getValue() / 100.0f;        e.ring = ring.getValue() / 100.0f;
        e.sync = syncOn;
        e.sub = sub.getValue() / 100.0f;      e.noise = noise.getValue() / 100.0f;
        e.drive = drive.getValue() / 100.0f;
        e.cutoff = cutoff.getValue() / 100.0f;      e.reso = reso.getValue() / 100.0f;
        e.keyTrack = keyTrack.getValue() / 100.0f;  e.envAmt = envAmt.getValue() / 100.0f;
        e.velAmt = velAmt.getValue() / 100.0f;
        e.envA = envA.getValue() / 100.0f;  e.envD = envD.getValue() / 100.0f;
        e.envS = envS.getValue() / 100.0f;  e.envR = envR.getValue() / 100.0f;
        e.outLevel = outLevel.getValue() / 100.0f;
        e.program = program;
        // Round through the stored form, so "is it the factory sound" compares
        // exactly what a set would hold.
        oscengine::Params back;
        return oscengine::fromString (oscengine::toString (e), back) ? back : e;
    }

    std::vector<GoldSlider*> allSliders()
    {
        std::vector<GoldSlider*> v;
        for (int i = 0; i < 2; ++i)
            for (auto* s : { &wave[(size_t) i], &oct[(size_t) i], &semi[(size_t) i],
                             &fine[(size_t) i], &pw[(size_t) i], &level[(size_t) i] })
                v.push_back (s);
        for (auto* s : { &fm, &ring, &sub, &noise, &drive, &cutoff, &reso, &keyTrack,
                         &envAmt, &velAmt, &envA, &envD, &envS, &envR, &outLevel })
            v.push_back (s);
        return v;
    }

    int          program    = -1;
    juce::String loadedSpec;
    bool         touched    = false;
    bool         loading    = false;
    bool         syncOn     = false;

    juce::Label      statusLbl, noteLbl;
    juce::TextButton factoryBtn, syncBtn;
    std::array<juce::Rectangle<int>, 3> rowLabel;

    std::array<GoldSlider, 2> wave  {{ { "WAVE",  0.0f,   3.0f,   0.0f, "" }, { "WAVE",  0.0f,   3.0f,   1.0f, "" } }};
    std::array<GoldSlider, 2> oct   {{ { "OCT",  -2.0f,   2.0f,   0.0f, "" }, { "OCT",  -2.0f,   2.0f,  -1.0f, "" } }};
    std::array<GoldSlider, 2> semi  {{ { "SEMI", -12.0f, 12.0f,   0.0f, "" }, { "SEMI", -12.0f, 12.0f,   0.0f, "" } }};
    std::array<GoldSlider, 2> fine  {{ { "FINE", -50.0f, 50.0f,   0.0f, "" }, { "FINE", -50.0f, 50.0f,   0.0f, "" } }};
    std::array<GoldSlider, 2> pw    {{ { "PW",     5.0f, 95.0f,  50.0f, "" }, { "PW",     5.0f, 95.0f,  50.0f, "" } }};
    std::array<GoldSlider, 2> level {{ { "LEVEL",  0.0f, 100.0f, 100.0f, "" }, { "LEVEL", 0.0f, 100.0f, 60.0f, "" } }};
    GoldSlider fm       { "FM",      0.0f, 100.0f,  0.0f, "" };
    GoldSlider ring     { "RING",    0.0f, 100.0f,  0.0f, "" };
    GoldSlider sub      { "SUB",     0.0f, 100.0f,  0.0f, "" };
    GoldSlider noise    { "NOISE",   0.0f, 100.0f,  0.0f, "" };
    GoldSlider drive    { "DRIVE",   0.0f, 100.0f, 20.0f, "" };
    GoldSlider cutoff   { "CUTOFF",  0.0f, 100.0f, 40.0f, "" };
    GoldSlider reso     { "RESO",    0.0f, 100.0f, 20.0f, "" };
    GoldSlider keyTrack { "KEY",     0.0f, 100.0f, 50.0f, "" };
    GoldSlider envAmt   { "ENV",  -100.0f, 100.0f, 40.0f, "" };
    GoldSlider velAmt   { "VEL",     0.0f, 100.0f, 30.0f, "" };
    GoldSlider envA     { "ATTACK",  0.0f, 100.0f,  0.0f, "" };
    GoldSlider envD     { "DECAY",   0.0f, 100.0f, 30.0f, "" };
    GoldSlider envS     { "SUSTAIN", 0.0f, 100.0f,  0.0f, "" };
    GoldSlider envR     { "RELEASE", 0.0f, 100.0f, 20.0f, "" };
    GoldSlider outLevel { "LEVEL",   0.0f, 100.0f, 80.0f, "" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEnginePanel)
};
