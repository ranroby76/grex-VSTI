#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "InstrEditPanel.h"    // InstrEditStyle::paintComponentFrame + kPanelLabel

//==============================================================================
//  TutorialTab -- the built-in manual.
//
//  Layout:
//     +-------------------------------------------------------------------+
//     |  [WELCOME][QUICK START][KEYBOARD][TEMPO][SECTIONS][STYLES]         |
//     |  [SOUNDS ][MIXER      ][JUMPS   ][CRASH][SETS    ][SETTINGS]       |
//     |  +-------------------------------------------------------------+  |
//     |  |  <chapter title>                                            |  |
//     |  |  scrolling body text                                        |  |
//     |  +-------------------------------------------------------------+  |
//     +-------------------------------------------------------------------+
//
//  The chapter row uses the same two-state look as the SettingsTab page
//  selector (#1A1A1A unlit, #CC6600 lit, black text when lit), so the nested
//  selector reads as a level BELOW the main tab row rather than a competing
//  copy of it.
//
//  WHY THE TEXT IS DATA AND NOT A PILE OF LABELS.  Every chapter is a plain
//  array of short lines with a one-character prefix -- '#' heading, '-' bullet,
//  anything else a paragraph, empty a spacer.  Adding a chapter is adding an
//  entry to kChapters; nothing about the layout has to be touched.  The parser
//  is four lines and the alternative (a Label per line) would need hand-placed
//  bounds for several hundred labels that still would not wrap.
//
//  Wrapping is done by juce::TextLayout at the viewport's width, so the page
//  reflows at every window scale instead of clipping.  Layouts are rebuilt only
//  on a chapter change or a resize -- never in paint().
//
//  THIS TAB TALKS TO NOTHING.  It has no callbacks and no processor reference
//  by design: a manual that could change the plugin's state would be a manual
//  you cannot safely browse while playing.
//==============================================================================
class TutorialTab : public juce::Component
{
public:
    TutorialTab()
    {
        for (int c = 0; c < getNumChapters(); ++c)
        {
            auto* b = chapterButtons.add (new juce::TextButton());
            b->setButtonText (kChapters[(size_t) c].title);
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF1A1A1A));
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFCC6600));
            b->setColour (juce::TextButton::textColourOffId,  juce::Colours::white.withAlpha (0.75f));
            b->setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
            b->onClick = [this, c] { selectChapter (c); };
            addAndMakeVisible (b);
        }

        viewport.setViewedComponent (&page, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (10);
        addAndMakeVisible (viewport);

        selectChapter (0);
    }

    void paint (juce::Graphics& g) override
    {
        InstrEditStyle::paintComponentFrame (g, bodyFrame(), {});
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);

        // Chapter selector: two rows of six.  Six across keeps every caption on
        // one line at the smallest window scale, which a single row of twelve
        // would not -- and a wrapped caption in a selector reads as two buttons.
        const int rowH = juce::jlimit (22, 34, r.getHeight() / 13);
        auto selArea = r.removeFromTop (rowH * kSelectorRows + kSelectorRowGap);

        for (int row = 0; row < kSelectorRows; ++row)
        {
            auto rowArea = selArea.removeFromTop (rowH);
            if (row + 1 < kSelectorRows) selArea.removeFromTop (kSelectorRowGap);

            const int first = row * kSelectorCols;
            const float bw  = (float) rowArea.getWidth() / (float) kSelectorCols;

            for (int col = 0; col < kSelectorCols; ++col)
            {
                const int idx = first + col;
                if (idx >= chapterButtons.size()) break;

                const juce::Rectangle<float> cell {
                    (float) rowArea.getX() + (float) col * bw, (float) rowArea.getY(),
                    bw, (float) rowArea.getHeight() };

                chapterButtons[idx]->setBounds (cell.toNearestInt().reduced (2, 1));
            }
        }

        r.removeFromTop (6);
        bodyArea = r;

        viewport.setBounds (bodyArea.reduced (14, 12));
        page.setScale (uiScale());
        page.layoutTo (viewport.getMaximumVisibleWidth());
    }

private:
    //==========================================================================
    //  Page -- the scrolled body of one chapter.
    //==========================================================================
    struct Line
    {
        enum class Kind { Heading, Body, Bullet, Space };
        Kind         kind = Kind::Body;
        juce::String text;
    };

    class Page : public juce::Component
    {
    public:
        Page() { setOpaque (false); }

        void setScale (float s) { scale = s; }

        void setLines (const std::vector<Line>* l, int wrapWidth)
        {
            lines = l;
            layoutTo (wrapWidth);
        }

        /** Rebuild every TextLayout for `wrapWidth` and grow to fit.  Called on
            a resize and on a chapter change only -- paint() just draws what is
            already measured, which is what keeps scrolling smooth under the
            plugin's continuous-repaint OpenGL loop. */
        void layoutTo (int wrapWidth)
        {
            entries.clear();

            if (lines == nullptr || wrapWidth < 40)
            {
                // Not a defensive shrug: laying a chapter out at a 1 px wrap
                // width (which is what the constructor's first call sees, before
                // the viewport has been sized) puts every WORD on its own line
                // and costs hundreds of layout passes for a result that resized()
                // throws away a moment later.
                setSize (juce::jmax (1, wrapWidth), 1);
                return;
            }

            const float headFont   = 15.0f * scale;
            const float bodyFont   = 13.5f * scale;
            const float bulletPad  = 16.0f * scale;
            const float headTopPad =  9.0f * scale;
            const float lineGap    =  3.0f * scale;
            const float spaceH     =  7.0f * scale;

            float y = 0.0f;

            for (const auto& ln : *lines)
            {
                Entry e;
                e.kind = ln.kind;

                if (ln.kind == Line::Kind::Space)
                {
                    e.y = y;
                    e.h = spaceH;
                    y += spaceH;
                    entries.push_back (std::move (e));
                    continue;
                }

                const bool  isHead  = (ln.kind == Line::Kind::Heading);
                const bool  isBul   = (ln.kind == Line::Kind::Bullet);
                const float indent  = isBul ? bulletPad : 0.0f;
                const float wrapW   = juce::jmax (40.0f, (float) wrapWidth - indent);

                juce::AttributedString as;
                as.setWordWrap (juce::AttributedString::byWord);
                as.setJustification (juce::Justification::topLeft);
                as.setLineSpacing (1.5f * scale);
                as.append (ln.text,
                           juce::Font (isHead ? headFont : bodyFont,
                                       isHead ? juce::Font::bold : juce::Font::plain),
                           isHead ? juce::Colour (0xFFCC6600)
                                  : juce::Colour (0xFFC2C2C2));

                if (isHead) y += headTopPad;

                e.indent = indent;
                e.layout.createLayout (as, wrapW);
                e.y = y;
                e.h = e.layout.getHeight();

                y += e.h + lineGap;
                entries.push_back (std::move (e));
            }

            setSize (wrapWidth, (int) std::ceil (y) + 4);
        }

        void paint (juce::Graphics& g) override
        {
            const float bulletR = 2.2f * scale;

            // ONLY WHAT IS ON SCREEN.  The plugin runs an OpenGL continuous
            // repaint, so paint() is called every frame; a long chapter is 40+
            // TextLayouts and re-drawing all of them each frame to show the
            // fifteen inside the viewport is pure waste.  Same reasoning as the
            // MixerTab's offscreen cache, cheaper here because the answer is
            // just a clip test.
            const auto clip = g.getClipBounds().toFloat();

            for (auto& e : entries)
            {
                if (e.kind == Line::Kind::Space)  continue;
                if (e.y + e.h < clip.getY())      continue;
                if (e.y > clip.getBottom())       break;

                if (e.kind == Line::Kind::Bullet)
                {
                    g.setColour (juce::Colour (0xFFCC6600));
                    g.fillEllipse (e.indent * 0.35f,
                                   e.y + 6.0f * scale,
                                   bulletR * 2.0f, bulletR * 2.0f);
                }

                e.layout.draw (g, { e.indent, e.y,
                                    (float) getWidth() - e.indent, e.h });
            }
        }

    private:
        struct Entry
        {
            Line::Kind      kind = Line::Kind::Body;
            juce::TextLayout layout;
            float            y = 0.0f, h = 0.0f, indent = 0.0f;
        };

        const std::vector<Line>* lines = nullptr;
        std::vector<Entry>       entries;
        float                    scale = 1.0f;
    };

    //==========================================================================
    struct Chapter
    {
        juce::String      title;
        std::vector<Line> lines;
    };

    //==========================================================================
    //  Chapter text.  '#' heading, '-' bullet, empty line spacer, anything
    //  else a paragraph.  Written as one raw string per chapter so the source
    //  reads the way the page reads.
    //
    //  CONTINUATION LINES ARE JOINED.  The source is hard-wrapped at a readable
    //  width, but the PAGE wraps at whatever the window is, so a source line
    //  break inside a sentence must not become a block break — otherwise the
    //  second half of a bullet renders as an un-indented paragraph of its own.
    //  A new block therefore starts only at '#', at '-', or after a blank line;
    //  every other line is appended to the block above it.
    //==========================================================================
    static std::vector<Line> parse (const juce::String& raw)
    {
        std::vector<Line> out;
        juce::StringArray src;
        src.addLines (raw);

        bool afterBlank = true;   // the first line always starts a block

        for (auto s : src)
        {
            const auto t = s.trim();

            if (t.isEmpty())
            {
                // Collapse runs of blank lines into one spacer.
                if (! out.empty() && out.back().kind != Line::Kind::Space)
                {
                    Line sp;
                    sp.kind = Line::Kind::Space;
                    out.push_back (std::move (sp));
                }
                afterBlank = true;
                continue;
            }

            const bool startsBlock = afterBlank
                                  || t.startsWithChar ('#')
                                  || t.startsWithChar ('-');

            if (! startsBlock && ! out.empty()
                && out.back().kind != Line::Kind::Space)
            {
                out.back().text += " " + t;
                continue;
            }

            Line ln;
            if (t.startsWithChar ('#'))      { ln.kind = Line::Kind::Heading;
                                               ln.text = t.substring (1).trim(); }
            else if (t.startsWithChar ('-')) { ln.kind = Line::Kind::Bullet;
                                               ln.text = t.substring (1).trim(); }
            else                             { ln.kind = Line::Kind::Body;
                                               ln.text = t; }

            out.push_back (std::move (ln));
            afterBlank = false;
        }

        // Trim leading / trailing spacers so a chapter never opens or closes
        // with dead air.
        while (! out.empty() && out.front().kind == Line::Kind::Space)
            out.erase (out.begin());
        while (! out.empty() && out.back().kind == Line::Kind::Space)
            out.pop_back();

        return out;
    }

    void selectChapter (int c)
    {
        currentChapter = juce::jlimit (0, getNumChapters() - 1, c);

        for (int i = 0; i < chapterButtons.size(); ++i)
            chapterButtons[i]->setToggleState (i == currentChapter,
                                               juce::dontSendNotification);

        page.setScale (uiScale());
        page.setLines (&kChapters[(size_t) currentChapter].lines,
                       juce::jmax (1, viewport.getMaximumVisibleWidth()));
        viewport.setViewPosition (0, 0);
        repaint();
    }

    int   getNumChapters() const { return (int) kChapters.size(); }
    float uiScale()        const { return juce::jlimit (0.75f, 1.30f,
                                                        (float) getWidth() / 918.0f); }

    juce::Rectangle<int> bodyFrame() const { return bodyArea; }

    static const std::vector<Chapter> kChapters;

    static constexpr int kSelectorRows   = 2;
    static constexpr int kSelectorCols   = 6;
    static constexpr int kSelectorRowGap = 4;

    juce::OwnedArray<juce::TextButton> chapterButtons;
    juce::Viewport                     viewport;
    Page                               page;
    juce::Rectangle<int>               bodyArea;
    int                                currentChapter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TutorialTab)
};

//==============================================================================
//  THE MANUAL.
//==============================================================================
inline const std::vector<TutorialTab::Chapter> TutorialTab::kChapters =
{
    //--------------------------------------------------------------------------
    { "WELCOME", TutorialTab::parse (R"TXT(
# What Grex is

Grex is an arranger. You play a chord with your left hand and a whole band
plays with you: drums, percussion, bass, chords, pad and two lead parts, all
following the chord you are holding, in the style you picked.

Your right hand stays free to play the melody on your own sounds.

# The screen, in two halves

- LEFT PANEL is the performance side: tempo, transpose, the chord readout, the
  set manager, and at the very bottom the keyboard with its split point.
- RIGHT PANEL is the work side: the tab row and the page it opens. Everything
  you set up ahead of a gig lives here.

# The one rule worth learning first

The keyboard is split in two by the SPLIT point. Everything BELOW the split
is the chord zone, and it drives the band. Everything ABOVE the split is
yours, and it plays your solo sounds. The rest of this manual is detail on
top of that single idea.

# How to use this tutorial

The buttons above are chapters and you can read them in any order. If you are
new, read QUICK START next, then KEYBOARD, then SECTIONS -- those three are
enough to perform with. The rest can wait until you want to change something.
)TXT") },

    //--------------------------------------------------------------------------
    { "QUICK START", TutorialTab::parse (R"TXT(
# Six steps to your first sound

- 1. Open the STYLES tab. Pick a genre from the grid at the top, then pick a
  style from the grid below it. Press LOAD.
- 2. The style name appears on the left panel. Its own set loads with it, so
  the sounds, the mixer and the levels are already set up for that style.
- 3. Hold a chord in the LEFT half of the keyboard. The chord you played is
  shown on the left panel, so you can always check that Grex heard what you
  meant.
- 4. Open the MAIN tab and press PLAY. The band starts on the variation that
  is lit -- VAR 1 unless you changed it.
- 5. Change chords with your left hand. The band follows immediately, in key.
- 6. Play the melody with your right hand.

# The next two things to try

- Press a FILL button while the band plays. It runs a fill and drops you back
  into a variation. This is how you move a song along.
- Press an END button when you want to finish. The band plays an ending and
  stops on its own.

# If you hear nothing

- Check that a style is actually loaded -- the style name on the left panel
  tells you.
- Check that you are playing BELOW the split point. Above it you are playing
  your solo sound, not the chord zone.
- Check the MIXER tab: master, style bus, and the eight style channels.
- Check the MAIN tab's eight style element buttons. An unlit element is muted.
)TXT") },

    //--------------------------------------------------------------------------
    { "KEYBOARD", TutorialTab::parse (R"TXT(
# Split point

The SPLIT knob sits next to the keyboard at the bottom right. It sets the key
where the chord zone ends and your solo range begins. The on-screen keyboard
draws the split, so you can see where the border is.

# ARRANGER and PIANO

- ARRANGER is the normal state: the keyboard is split, the low half feeds the
  band, the high half plays your sound.
- PIANO turns the split off. The whole keyboard plays your solo sound, and the
  band stops being fed. Use it for an intro or a solo passage with no chords.

# 1 FINGER and FINGERED

- 1 FINGER recognises simple shapes: one key gives a major chord, and small
  additions give minor, seventh and minor seventh.
- FINGERED reads real chord voicings, so extended qualities such as maj7, m7b5,
  9ths and 13ths are recognised as you play them.

The button is on the MAIN tab, and the chord readout on the left panel always
shows what Grex decided you played.

# HOLD

HOLD is the button beside the split knob. It freezes the arrangement where it
is: a section that is playing keeps playing, and anything you press while HOLD
is on waits instead of firing. This works on the variations AND on a fill,
intro, break or ending -- a fill held is a fill that vamps until you let go.

Release HOLD and whatever you queued fires on the next transition point.
RESTART deliberately ignores HOLD, because it re-cues rather than moves on.

# Chord memory

Once you have played a chord it stays until you play another one. Lifting your
left hand does not silence the band, so you can take your hand off the chord
zone to reach a control and come straight back.

# SINGLE and MULTI

SOLO MODE decides how many of your own sounds the right hand plays. SINGLE
plays one solo channel. MULTI layers every solo channel you enabled on the
MAIN tab, so you can stack, for example, a piano under a string pad.

# TRANSPOSE

The TRANSPOSE knob on the left panel shifts everything in semitones -- the
band and your solo sounds together -- so you can move a song into a singer's
key without relearning it.
)TXT") },

    //--------------------------------------------------------------------------
    { "TEMPO", TutorialTab::parse (R"TXT(
# Setting the tempo

- The TEMPO knob sets the beats per minute directly.
- TAP sets it by feel: tap the button in time and Grex takes the tempo from
  your taps.
- RESET returns to the tempo the style itself was written at. Every style
  carries its own, so this is your way back after experimenting.

# FREE and SYNCED

- FREE means Grex keeps its own tempo and ignores the host.
- SYNCED means Grex follows your DAW's transport tempo, which is what you want
  when Grex plays alongside other tracks in a project.

# Half and double time

The x1 / x2 / x1/2 selector multiplies the tempo without you touching the
number. x1/2 gives a half-time feel; x2 doubles it. Both keep the style's own
groove intact -- this is not the same as typing a new BPM.

# Starting and stopping

- PLAY / STOP starts and stops the band, and the button caption follows the
  state.
- SYNC PLAY arms a start: the band waits, and the first chord you play in the
  chord zone starts it. You get an entry exactly on your own downbeat.
- ONPRESS is a gate. The band plays only while a chord is held and stops the
  moment you lift, so you can punch the arrangement in and out by hand.
- RESTART re-cues the current section back to its beginning without stopping
  and without changing which section is playing.

# DAW START

The DAW START button on the left panel makes Grex start with your host's
transport, so hitting play in the DAW starts the band with it.
)TXT") },

    //--------------------------------------------------------------------------
    { "SECTIONS", TutorialTab::parse (R"TXT(
# What a style is made of

A style is not one loop. It is a set of sections, and arranging live is simply
choosing which one plays next.

- INTRO 1-3 open a song. Each one is a different length and mood.
- VAR 1-4 are the four main grooves, usually quiet to busy.
- FILL 1-4 are the short run-ups between grooves.
- BRAKE is a break -- a stripped bar that opens the arrangement up.
- END 1-3 close a song and stop the band on their own.

The sixteen pads on the MAIN tab are exactly these sections.

# How to move through a song

Press a variation and Grex plays the fill that leads into it, then lands on
the new groove. You do not have to time the change yourself: Grex waits for
the right musical moment and gets there on the beat.

Press a fill on its own and it runs, then returns you to where you were.

# TRANSITIONS

The TRANSITIONS selector on the left panel decides how often Grex checks for a
press: 1/2, 1/4 or 1/8 of a bar. 1/8 feels instant and forgiving; 1/2 is
tighter and lands on stronger beats. It is a feel setting, not a right answer.

The grid follows the style's own meter, so it behaves the same in 3/4, 6/8 and
7/8 as it does in 4/4.

# FILL LENGTH

A fill never eats more than one bar, so the cost of a transition is the same
in every style. FILL LENGTH chooses how much of that bar you get: "1" plays
the whole bar, "1/2" plays only its second half for a shorter, punchier lift.

# The eight style elements

The MAIN tab's eight buttons -- DRUMS, PERC, BASS, CHORD 1, CHORD 2, PAD,
LEAD 1, LEAD 2 -- mute and unmute the parts of the band. Dropping to drums and
bass for a verse and bringing the rest back for a chorus is one press each way,
and it is the fastest arranging tool in the plugin.

# Playing live: a suggested shape

- Start with SYNC PLAY armed so your first chord is the downbeat.
- Verse on VAR 1 or 2, chorus on VAR 3 or 4.
- Use FILL to change grooves, BRAKE to open a gap before a big entry.
- Use HOLD when you want to stretch a moment: the current section keeps going,
  including a fill or a break, until you let go.
- Finish with an END. Do not just press STOP -- the ending is what makes it
  sound finished.
)TXT") },

    //--------------------------------------------------------------------------
    { "STYLES", TutorialTab::parse (R"TXT(
# The browser

The STYLES tab has two grids. The top one is genres, taken from the folders
inside your styles folder -- so the way you organise the folder is the way the
browser looks. The bottom grid is the styles in the selected genre, arranged
alphabetically across pages.

Click a style to select it, then LOAD. RELOAD re-reads the same style from
disk, which is what you want after editing its set.

# Search

The search bar sits under the tab row and is visible on the STYLES tab. It
looks across the whole library, not just the genre you are browsing, so you
can find a style without remembering where you filed it.

# Favourites

Every cell carries a star. Starring a style marks it as a favourite so your
working set is quick to find in a large library.

# One selection, everywhere

The highlighted cell is the style that is actually loaded, identified by its
full path. Browsing other genres or turning pages never moves that highlight,
so the grid can always be trusted to tell you what is playing.

# Loading while playing

A style cannot be swapped while the band is running -- that would leave notes
hanging and the groove mid-phrase. Stop, load, and start again.

# Every style brings its own set

Loading a style also loads the set saved with it: the sounds on all sixteen
channels, the mixer, the jumps, the crash settings and the style levels. That
is why a freshly loaded style already sounds finished, and why saving a set is
how you keep any change you make.
)TXT") },

    //--------------------------------------------------------------------------
    { "SOUNDS", TutorialTab::parse (R"TXT(
# Sixteen channels

- Eight STYLE channels, one per style element, played by the band.
- Eight SOLO channels, played by your right hand.

The SOUNDS tab is where you choose what each of them plays and how it sounds.

# Choosing a sound

Pick a slot, then pick a voice. Drum slots additionally offer the sampled kits
found in your sounds folder, alongside the built-in composed kits.

If you want a slot to keep the sound YOU chose and ignore what the style asks
for, use IGNORE PROGRAM CHANGE on that slot. Without it, loading a style is
allowed to replace the voice.

# The sound editor

Opening a slot's editor gives you the full instrument: envelope and filter,
then the effects chain -- EQ, chorus, wah, phaser, delay and reverb -- each
with its own on/off.

Two controls are worth knowing by name:

- GAIN is a per-sound level, 0 to 200, where 100 is unity and 200 is twice as
  loud. This is how you calibrate one sound that is too hot or too shy without
  touching the mixer balance.
- ALLOWED NOTES limits the range a sound will play, which is how you stop a
  bass patch from being dragged into an octave it was never sampled for.

# The SWEETENER

The sweetener is a small dynamics block for taking the aggression out of a
sound, especially drums from converted styles. It is three named jobs rather
than a set of engineer's numbers:

- SOFTEN rounds off the attack, or adds attack when you push it the other way.
- TAME pulls down a harsh frequency band only when it gets loud.
- ROUND adds gentle saturation, blended in parallel so definition survives.

It starts switched off on every slot, on purpose: you should hear the sound
raw first and then decide it needs help.

The advanced rack underneath -- EQ, saturation, compressor, reverb, delay --
is still there and unchanged if you prefer to work that way.

# Presets

- A solo sound saves as an .ins preset.
- The same sound inside a style saves as a .sins preset, because a piano under
  your right hand and a piano inside an arrangement want different settings.
- Drum kits save as .drm.

Saving a preset re-applies it to every channel currently holding that
instrument, so you can calibrate by ear without stopping the band.

# Global sound macros

FUNKEY MODE and BIG DRUMS live on the left panel and in Settings. FUNKEY MODE
gives seven instrument families their own global effects chain; BIG DRUMS does
the same for the kit. While a macro is on it replaces the instrument's private
chain; switch it off and the style's own sound returns.
)TXT") },

    //--------------------------------------------------------------------------
    { "MIXER", TutorialTab::parse (R"TXT(
# Three sections

- LEFT HAND / STYLE: the eight band channels, with the STYLE VOLUME bus fader
  under them.
- RIGHT HAND / SOLO: your eight solo channels, with the SOLO VOLUME bus fader
  under them.
- MASTER: one vertical fader for everything.

# One scale everywhere

Every fader reads 0 to 254 on the same linear scale:

- 0 is silence.
- 127 is unity -- the sound at its natural level.
- 254 is unity doubled.

The reason for the odd-looking numbers is honesty: a style writes its own
channel volumes in MIDI, and 0 to 127 is what it writes. Grex shows you that
number unchanged, so the fader reads exactly what the style composer asked
for. Everything from 128 upward is headroom that only you can add.

# Working with it

- Balance the band with the eight style faders.
- Set the band against your right hand with the STYLE and SOLO bus faders.
- Leave MASTER for the overall level into your DAW.

Mixer positions are part of the set, so SAVE SET keeps them with the style.
)TXT") },

    //--------------------------------------------------------------------------
    { "JUMPS", TutorialTab::parse (R"TXT(
# What jumps decide

Some sections are meant to end somewhere. An intro finishes and something has
to follow it; a fill runs and then lands. The JUMPS tab is the table of those
destinations.

Each row is a source -- the three intros, the four fills, and the break. Each
column is where it can land. One destination per row, so the table always has
exactly one answer for every section.

# The defaults, and why they make sense

Out of the box each section lands on its matching variation: INTRO A goes to
MAIN A, FILL BB goes to MAIN B, and so on. That is the behaviour most players
expect, so most of the time this tab needs no attention.

# Why you would change it

- Send every intro to MAIN A so a song always opens in the same groove.
- Send a fill to a BUSIER variation so it builds instead of returning.
- Chain sections: send an intro to another intro to make a longer opening.

# Where it is saved

Jumps are part of the set, so each style can have its own routing. Change the
table, press SAVE SET, and that style will always arrange itself your way.
)TXT") },

    //--------------------------------------------------------------------------
    { "CRASH", TutorialTab::parse (R"TXT(
# Why this tab exists

A crash cymbal is what makes a section change sound intentional. Style files
do not always place them where a player would, so Grex can add them for you.

# The three triggers

- CRASH ON TRANSITION hits a crash when a section change LANDS -- at the end of
  a fill, on the downbeat of the new groove. That is where a drummer plays it:
  the fill is the run-up, and the cymbal marks the arrival, not the departure.
- AUTO CRASH hits one every N cycles of the current variation, for songs that
  want a marker every four or eight bars. The count restarts whenever a new
  variation begins, so a fill in the middle never throws the count off.
- CRASH NOW is a manual hit, and it works whether or not a style is playing.

An ending deliberately gets no automatic crash: a cymbal there rings on past
the final chord.

# Shaping the hit

- VELOCITY is a fixed weight, so every crash lands the same way. It also
  chooses which layer of the cymbal sample speaks, so it changes the character
  of the hit as well as the loudness.
- GAIN is how loud that hit sits in the mix, on the same 0-200 scale as the
  sound gains, with 100 as unity.
- The element buttons choose WHICH cymbals fire, so you can pick a crash, a
  splash or several at once.

SAVE keeps these settings for the whole plugin -- they are your playing habit,
not a property of one style.
)TXT") },

    //--------------------------------------------------------------------------
    { "SETS", TutorialTab::parse (R"TXT(
# What a set is

A set is everything about how a style sounds in YOUR hands: the sounds on all
sixteen channels, the mixer, the jumps table, the style levels, and the global
options saved with a song.

Every style ships with its own set, and loading a style loads it. This is why
you did not have to configure anything to get a good sound in QUICK START.

# The set manager

The three buttons on the left panel:

- LOAD SET opens a set file.
- SAVE SET writes the current state, and asks you where.
- FAST SAVE overwrites the set you are on, with no dialogue. This is the one
  you will use most: change something, hear that it is better, keep it.

The set name is shown underneath so you always know what you are editing.

# COMMENTS

The COMMENTS button opens a free text note saved inside the set. Use it for
the key a song is in, which variation the chorus wants, or anything else you
will not remember at the next gig.

# The SET EDITOR tab

This is where the loaded style's LEVELS live, and they are the controls that
want a playing style to judge against:

- BOOST is an overall level for the whole band.
- MAKEUP is the target Grex normalises each section toward, so a quiet style
  and a loud style arrive at a comparable level.
- IGNORE STYLE VOLUMES tells Grex to disregard the volumes written inside the
  style file and use your mixer positions instead. Converted styles are the
  reason this exists: some of them carry a flat, meaningless mix.

Changes here are part of the set, so SAVE SET is what keeps them.
)TXT") },

    //--------------------------------------------------------------------------
    { "SETTINGS", TutorialTab::parse (R"TXT(
# Four pages

The SETTINGS tab holds CONTROL NOTE MAP, MIDI CC CONTROL, REGISTRATION and
GLOBAL SETTINGS.

# CONTROL NOTE MAP

Grex reserves MIDI notes 0 to 35 as remote controls, so a controller with pads
or a second keyboard can drive the arranger without you touching the screen.
Notes 1 to 8 mute and unmute the style elements, 9 to 14 select solo channels,
15 to 30 fire the sixteen section pads, and 31 to 35 cover play/stop, restart,
hold, arranger/piano and synced play.

The page is a read-only reference. Print it, or leave the tab open while you
program your controller.

# MIDI CC CONTROL

Five things can be driven by a knob or pedal: MASTER VOLUME, STYLE VOLUME,
TEMPO, TRANSPOSE and SPLIT POINT.

Press LEARN on a row, move the control you want to use, and it is assigned.
FORGET clears it. The map is saved with the plugin, not with a set, because it
belongs to your rig rather than to a song.

# REGISTRATION

This page shows your MACHINE ID -- a five-digit number unique to the computer
you are on. Click it to copy it, send it to us, and type the serial you get
back into the box.

Until Grex is registered it goes briefly silent at regular intervals, so you
can hear everything and evaluate it properly, but not perform with it.

# GLOBAL SETTINGS

Three trigger buttons open the global features, each in its own window:

- FUNKEY MODE: one effects chain per instrument family, applied everywhere.
- BIG DRUMS: one effects chain for the whole kit.
- STYLE LEVELS: the level tools described in the SETS chapter.

Also here:

- PITCH BEND RANGE sets how far the wheel bends your solo instruments, from 1
  to 12 semitones.
- BYPASS INSTRUMENT CHAIN skips every channel's filter and post-mix effects so
  you hear the raw instrument. It is a listening tool for calibration, not a
  performance setting.

# Where things are saved

- Set files hold everything that belongs to a SONG.
- The CC map, the crash settings, the global macros and your registration
  belong to the RIG, and are saved once for the whole plugin.
)TXT") },
};
