

#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme

#include <JuceHeader.h>
#include "Main.h"

//==============================================================================
// SongWindowContent — the transport for a recorded performance.
//
// A MIDI FILE PLAYER AND A MIXER, which is what Rob called it and what it is:
// the events replay into the same dispatch a live performance uses, so the band
// rebuilds itself rather than being played back, and the sixteen mutes act on
// the channels that band is using.
//
//==============================================================================
// THE SIXTEEN MUTES ARE THE POINT, NOT A CONVENIENCE.
//
// Eight style + eight solo IS Grex's channel model, so they cost nothing new.
// And muting the solo half is "minus one": the recording contains the ORIGINAL
// player's right hand, and turning it off is what makes a song you recorded into
// a backing track you can play over. Every arranger workstation has that switch;
// here it falls out of the channel layout for free.
//
// THE LIVE TOGGLES WIN over whatever mute state the song's set carries. They are
// a performance control, like a fader - which also means they are deliberately
// NOT written back into the song.
//==============================================================================
class SongWindowContent : public juce::Component,
                          private juce::Timer
{
public:
    explicit SongWindowContent (BetelgeuseProcessor& p) : processor (p)
    {
        title.setText ("SONG", juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (Betel::Pal::kAccentBright));
        title.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title);

        fileLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF999999));
        fileLabel.setFont (juce::FontOptions (12.0f));
        fileLabel.setText ("no song loaded", juce::dontSendNotification);
        addAndMakeVisible (fileLabel);

        // ── transport ────────────────────────────────────────────────────────
        auto setupBtn = [this] (juce::TextButton& b, const char* t, juce::Colour on)
        {
            b.setButtonText (t);
            b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
            b.setColour (juce::TextButton::buttonOnColourId, on);
            b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xFFCCCCCC));
            b.setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
            addAndMakeVisible (b);
        };
        setupBtn (btnLoad,   "LOAD",   juce::Colour (Betel::Pal::kAccent));
        setupBtn (btnPlay,   "PLAY",   juce::Colour (0xFF2E8B2E));
        setupBtn (btnPause,  "PAUSE",  juce::Colour (Betel::Pal::kAccent));
        setupBtn (btnStop,   "STOP",   juce::Colour (Betel::Pal::kAccent));
        setupBtn (btnRewind, "REWIND", juce::Colour (Betel::Pal::kAccent));

        btnLoad  .onClick = [this] { chooseSong(); };
        btnPlay  .onClick = [this] { play(); };
        btnPause .onClick = [this] { pause(); };
        btnStop  .onClick = [this] { stop(); };
        btnRewind.onClick = [this] { rewind(); };

        // ── position ─────────────────────────────────────────────────────────
        position.setSliderStyle (juce::Slider::LinearHorizontal);
        position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        position.setRange (0.0, 1.0, 0.0001);
        position.setColour (juce::Slider::trackColourId,      juce::Colour (Betel::Pal::kAccent));
        position.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF0C0C0C));
        position.setColour (juce::Slider::thumbColourId,      juce::Colour (Betel::Pal::kAccentBright));
        position.onDragStart = [this] { scrubbing = true;  };
        position.onDragEnd   = [this] { scrubbing = false; seekToSliderPosition(); };
        addAndMakeVisible (position);

        posLabel.setColour (juce::Label::textColourId, juce::Colour (Betel::Pal::kAccentBright));
        posLabel.setFont (juce::FontOptions (12.0f));
        posLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (posLabel);

        // ── tempo ────────────────────────────────────────────────────────────
        tempoLabel.setText ("TEMPO", juce::dontSendNotification);
        tempoLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFAAAAAA));
        tempoLabel.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (tempoLabel);

        // THE TIMELINE IS IN BEATS, so this is not time-stretching - the band is
        // re-sequenced at the new tempo and follows exactly. The recorded right
        // hand moves with it, which for a backing track is what is wanted.
        tempo.setSliderStyle (juce::Slider::LinearHorizontal);
        tempo.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
        tempo.setRange (40.0, 240.0, 1.0);
        tempo.setValue (120.0, juce::dontSendNotification);
        tempo.setColour (juce::Slider::trackColourId,      juce::Colour (Betel::Pal::kAccent));
        tempo.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF0C0C0C));
        tempo.setColour (juce::Slider::thumbColourId,      juce::Colour (Betel::Pal::kAccentBright));
        tempo.onValueChange = [this]
        {
            processor.setManualBPM ((float) tempo.getValue());
        };
        addAndMakeVisible (tempo);

        // ── the sixteen mutes ────────────────────────────────────────────────
        muteHeader.setText ("MUTE   -   band on the left, your hand on the right",
                            juce::dontSendNotification);
        muteHeader.setColour (juce::Label::textColourId, juce::Colour (0xFF808080));
        muteHeader.setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (muteHeader);

        static const char* kStyleRole[8] = { "DRUMS","PERC","BASS","CHORD 1",
                                             "CHORD 2","PAD","LEAD 1","LEAD 2" };
        for (int i = 0; i < 16; ++i)
        {
            auto& b = mutes[(size_t) i];
            const bool isSolo = (i >= 8);
            b.setButtonText (isSolo ? "SOLO " + juce::String (i - 7)
                                    : juce::String (kStyleRole[i]));
            b.setClickingTogglesState (true);

            // LIT MEANS MUTED, which is the opposite of a mixer's mute LED being
            // off when the channel is fine - but here the button IS the mute, and
            // an unlit row reading "everything is playing" is the state a player
            // wants to recognise at a glance.
            b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF1E1E1E));
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF8B0000));
            b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xFFAAAAAA));
            b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            b.onClick = [this, i] { applyMute (i); };
            addAndMakeVisible (b);
        }

        setSize (620, 470);
        startTimerHz (12);
        refreshTransport();
    }

    ~SongWindowContent() override { stopTimer(); }

    //==========================================================================
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xFF1A1A1A)); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);

        auto top = r.removeFromTop (26);
        title.setBounds (top.removeFromLeft (70));
        fileLabel.setBounds (top);
        r.removeFromTop (10);

        auto row = r.removeFromTop (32);
        const int bw = (row.getWidth() - 4 * 6) / 5;
        for (auto* b : { &btnLoad, &btnPlay, &btnPause, &btnStop, &btnRewind })
        {
            b->setBounds (row.removeFromLeft (bw));
            row.removeFromLeft (6);
        }

        r.removeFromTop (10);
        auto posRow = r.removeFromTop (24);
        posLabel.setBounds (posRow.removeFromRight (110));
        position.setBounds (posRow);

        r.removeFromTop (8);
        auto tRow = r.removeFromTop (24);
        tempoLabel.setBounds (tRow.removeFromLeft (56));
        tempo.setBounds (tRow);

        r.removeFromTop (14);
        muteHeader.setBounds (r.removeFromTop (16));
        r.removeFromTop (4);

        // Two rows of eight: the band above, the right hand below, which is the
        // same split the instrument itself has.
        const int rowH = juce::jlimit (26, 44, (r.getHeight() - 8) / 2);
        for (int half = 0; half < 2; ++half)
        {
            auto band = r.removeFromTop (rowH);
            if (half == 0) r.removeFromTop (8);

            const int cw = (band.getWidth() - 7 * 4) / 8;
            for (int i = 0; i < 8; ++i)
            {
                mutes[(size_t) (half * 8 + i)].setBounds (band.removeFromLeft (cw));
                band.removeFromLeft (4);
            }
        }
    }

private:
    //==========================================================================
    void chooseSong()
    {
        auto& m = processor.getFolderManager();
        const auto dir = m.isRootFolderValid()
                            ? m.getRootFolder().getChildFile ("songs")
                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        chooser = std::make_unique<juce::FileChooser> ("Load song", dir, "*.grxsong");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;

            if (processor.loadSongFile (f))
            {
                fileLabel.setText (f.getFileNameWithoutExtension(),
                                   juce::dontSendNotification);
                position.setValue (0.0, juce::dontSendNotification);
                tempo.setValue (processor.getSongTempoBpm(), juce::dontSendNotification);
            }
            else
            {
                fileLabel.setText ("could not load that song", juce::dontSendNotification);
            }
            refreshTransport();
        });
    }

    void play()   { processor.songPlay();   refreshTransport(); }
    void stop()   { processor.songStop();   refreshTransport(); }

    /** PAUSE MUST SILENCE WHAT IS SOUNDING. A note held at the pause point has
        no note-off coming while the clock is frozen, so it would hang until
        something else happened to stop it. Resume deliberately does NOT
        re-trigger them: a short gap is correct and is what every sequencer
        does. */
    void pause()  { processor.songPause();  refreshTransport(); }

    void rewind() { processor.songSeekBeats (0.0); position.setValue (0.0, juce::dontSendNotification); refreshTransport(); }

    void seekToSliderPosition()
    {
        const double len = processor.getSongLengthBeats();
        if (len <= 0.0) return;
        processor.songSeekBeats (position.getValue() * len);
    }

    /** A mute is a live performance control and beats whatever the song's own
        set said - so it is applied straight to the engine and never written
        back into the song. */
    void applyMute (int index)
    {
        const bool muted = mutes[(size_t) index].getToggleState();
        processor.setSongChannelMuted (index, muted);
    }

    void refreshTransport()
    {
        const bool loaded  = processor.getSongLengthBeats() > 0.0;
        const bool playing = processor.isSongPlaying();

        btnPlay  .setEnabled (loaded);
        btnPause .setEnabled (loaded);
        btnStop  .setEnabled (loaded);
        btnRewind.setEnabled (loaded);

        btnPlay .setToggleState (playing,  juce::dontSendNotification);
        btnPause.setToggleState (processor.isSongPaused(), juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const double len = processor.getSongLengthBeats();
        const double pos = processor.getSongPositionBeats();

        if (! scrubbing && len > 0.0)
            position.setValue (juce::jlimit (0.0, 1.0, pos / len),
                               juce::dontSendNotification);

        auto barsBeats = [] (double beats)
        {
            const int bar  = (int) (beats / 4.0) + 1;
            const int beat = (int) std::fmod (beats, 4.0) + 1;
            return juce::String (bar) + "." + juce::String (beat);
        };

        posLabel.setText (len > 0.0 ? barsBeats (pos) + "  /  " + barsBeats (len)
                                    : juce::String ("--"),
                          juce::dontSendNotification);
        refreshTransport();
    }

    BetelgeuseProcessor& processor;

    juce::Label      title, fileLabel, posLabel, tempoLabel, muteHeader;
    juce::TextButton btnLoad, btnPlay, btnPause, btnStop, btnRewind;
    juce::Slider     position, tempo;
    std::array<juce::TextButton, 16> mutes;
    bool             scrubbing = false;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongWindowContent)
};




