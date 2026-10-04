

#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the gold accent scheme
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "InstrEditPanel.h"    // InstrEditStyle::paintComponentFrame + kPanelLabel

//==============================================================================
//  TutorialTab -- the built-in manual.
//
//  Layout:
//     +-------------------------------------------------------------------+
//     |  [WELCOME][QUICK START][KEYBOARD][TEMPO][SECTIONS][STYLES][SOUNDS] |
//     |  [MIXER  ][FINISHER   ][JUMPS   ][CRASH][SETS    ][SETTINGS]       |
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
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (Betel::Pal::kAccent));
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

        // Chapter selector: two rows of seven.  Seven across keeps every caption
        // on one line at the smallest window scale, which a single row of
        // fourteen would not -- and a wrapped caption reads as two buttons.
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
                           isHead ? juce::Colour (Betel::Pal::kAccent)
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
                    g.setColour (juce::Colour (Betel::Pal::kAccent));
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
    // SEVEN, not six.  The grid is kSelectorRows x kSelectorCols and the layout
    // loop simply BREAKS when it runs out of buttons - so a chapter past the
    // last slot is not squeezed or wrapped, it silently never gets a button.
    // Adding the FINISHER chapter took the count to 13 against 12 slots, which
    // would have made SETTINGS unreachable with nothing on screen to say why.
    // 2 x 7 = 14 leaves a slot spare for the next one.
    static constexpr int kSelectorCols   = 7;
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

Grex is an arranger. You play a chord with your left hand and a whole band plays
with you: drums, percussion, bass, chords, pad and two lead parts, all following
the chord you are holding, in the style you picked.

Your right hand stays free to play the melody on your own sounds.

The style library is filed into folders by genre, and the STYLES tab shows you
exactly the folders you have. Add one, drop styles into it, and it appears.

# The screen, in two halves

- LEFT PANEL is the performance side: tempo, the global semitone stepper, the
  chord readout, ENERGY, the harmony and split features, the MIDI link box, the
  set manager, and at the very bottom the keyboard with its split point.
- RIGHT PANEL is the work side: the tab row and the page it opens. Everything
  you set up ahead of a gig lives here.

The tab row is MAIN, STYLES, SOUNDS, MIXER, JUMPS, CRASH, SETTINGS and TUTORIAL.

# The one rule worth learning first

The keyboard is split in two by the SPLIT point. Everything BELOW the split is
the chord zone, and it drives the band. Everything ABOVE the split is yours, and
it plays your solo sounds. The rest of this manual is detail on top of that
single idea.

# How to use this tutorial

The buttons above are chapters and you can read them in any order. If you are
new, read QUICK START next, then KEYBOARD, then SECTIONS -- those three are
enough to perform with. The rest can wait until you want to change something.
)TXT") },

    //--------------------------------------------------------------------------
    { "QUICK START", TutorialTab::parse (R"TXT(
# Five steps to your first sound

- 1. Open the STYLES tab. Pick a genre from the row of folder buttons, then
  click a style in the grid. That one click loads it -- there is no separate
  LOAD button to press afterwards.
- 2. The style name appears on the left panel. Its own set loads with it, so the
  sounds, the mixer and the levels are already set up for that style.
- 3. Hold a chord in the LEFT half of the keyboard. The chord is shown on the
  left panel, so you can always check that Grex heard what you meant.
- 4. Open the MAIN tab and press PLAY. The band starts on the section whose lamp
  is lit.
- 5. Change chords with your left hand, and play the melody with your right.

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

The split is YOURS, not the song's. Where your two hands divide follows your
reach and your habit, so it is kept in its own file and remembered for good.
Loading somebody else's set cannot move it, and neither can anything else --
only the knob.

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
shows what was decided.

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

SOLO MODE decides how many of your own sounds the right hand plays. SINGLE plays
one solo channel. MULTI layers every solo channel you enabled on the MAIN tab,
so you can stack, for example, a piano under a string pad.

Your right hand chooses from SIX solo channels. Solo 7 and solo 8 are not
selectable any more: they are lamps, and each belongs to one of the features
below -- solo 8 reads HARMONY, solo 7 reads M.BASS.

# HARMONY

HARMONY adds notes under the top note you are holding, taken from the chord the
band is playing. Choose DUET, TRIO, BLOCK, OCTAVE or 5TH, and set how loud the
added notes sit with the LEVEL slider.

They are real notes played by a real sound, not a pitch shift, and they play on
their own channel -- which is why they are not squashed by anything acting on
your melody.

# MULTI SPLIT

MULTI SPLIT divides the keyboard into THREE zones instead of two: a bass zone at
the bottom, the chord zone above it, and your solos on top. The BASS SPLIT
slider sets the lower boundary, and BASS -> SOLO n chooses which of your solo
sounds the bass zone plays.

# BASS INVERSION and MANUAL BASS

- BASS INVERSION is the switch a single-keyboard player can actually use. The
  style keeps its own bass line and simply follows the lowest note you hold, so
  nothing ever drops out.
- MANUAL BASS hands you the bass line. Everything left of the split plays SOLO 7
  and sends no chords, so the band holds what it had. It turns MULTI SPLIT off,
  since the two want the same part of the keyboard.

# GLOBAL SEMITONE

The GLOBAL SEMITONE control on the left panel shifts everything -- the band and
your solo sounds together -- so you can move a song into a singer's key without
relearning it.

It is a STEPPER, not a knob: one press is one semitone. Press the top half to go
up and the bottom half to go down.
)TXT") },

    //--------------------------------------------------------------------------
    { "TEMPO", TutorialTab::parse (R"TXT(
# Setting the tempo

- The TEMPO knob sets the beats per minute directly.
- TAP sets it by feel: tap the button in time and the tempo is taken from your
  taps.
- RESET returns to the tempo the style itself was written at. Every style
  carries its own, so this is your way back after experimenting.

# What a set remembers about tempo

A set does not store a bare number. It stores your OFFSET from the style's own
tempo. If the style is written at 150 and you play it at 170, the set keeps
"plus 20", not "170".

That is deliberate: the style stays the authority on its own speed, and your
adjustment rides on top. Change the style file later and your set still means
what you meant.

# FREE and SYNCED

- FREE keeps its own tempo and ignores the host.
- SYNCED follows your DAW's transport tempo, which is what you want when the
  band plays alongside other tracks in a project.

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

The DAW START button on the left panel starts the band with your host's
transport, so hitting play in the DAW starts everything together.
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

Press a variation and the fill that leads into it plays, then it lands on the
new groove. You do not have to time the change yourself: it waits for the right
musical moment and gets there on the beat.

Press a fill on its own and it runs, then returns you to where you were.

# TRANSITIONS

The TRANSITIONS selector on the left panel decides how often a press is checked
for: 1/2, 1/4 or 1/8 of a bar. 1/8 feels instant and forgiving; 1/2 is tighter
and lands on stronger beats. It is a feel setting, not a right answer.

The grid follows the style's own meter, so it behaves the same in 3/4, 6/8 and
7/8 as it does in 4/4.

# FILL LENGTH

A fill never eats more than one bar, so the cost of a transition is the same in
every style. FILL LENGTH chooses how much of that bar you get: "1" plays the
whole bar, "1/2" plays only its second half for a shorter, punchier lift.

# The eight style elements

The MAIN tab's eight buttons -- DRUMS, PERC, BASS, CHORD 1, CHORD 2, PAD,
LEAD 1, LEAD 2 -- mute and unmute the parts of the band. Dropping to drums and
bass for a verse and bringing the rest back for a chorus is one press each way,
and it is the fastest arranging tool here.

# Playing live: a suggested shape

- Start with SYNC PLAY armed so your first chord is the downbeat.
- Verse on VAR 1 or 2, chorus on VAR 3 or 4.
- Use FILL to change grooves, BRAKE to open a gap before a big entry.
- Use HOLD when you want to stretch a moment: the current section keeps going,
  including a fill or a break, until you let go.
- Finish with an END. Do not just press STOP -- the ending is what makes it
  sound finished.

# Choosing a section before you start

With the band stopped, press any section button -- INTRO, a variation, FILL,
BREAK or ENDING -- and the next PLAY opens on exactly that section. You are not
limited to starting on a main. Set up the intro you want, then start.

# What the lamp is telling you

The lamp lights the moment you PRESS, not when the change happens. That is
deliberate, and it is how hardware arrangers behave: the button is answering
"I have you", not "it has happened yet".

So between the press and the next quantise point the lamp shows what you ASKED
for while you are still hearing what came before. Nothing is wrong. When the
change lands the lamp does not move -- it simply stops being a promise and
starts being a report.

Press a different button in the meantime and the lamp follows the new one, so
you can always see which section is actually queued.
)TXT") },

    //--------------------------------------------------------------------------
    { "STYLES", TutorialTab::parse (R"TXT(
# The browser

The STYLES tab is one grid of thirty cells, six across and five down, filled
left to right and row by row. Above it is a row of FOLDER buttons -- one per
genre -- and above that the selected-style pill, the page arrows and SEARCH.

# The folders are your folders

The buttons come from the subfolders of your styles folder, and nothing knows
their names in advance. Make a folder called Latin, drop styles into it, rescan,
and a LATIN button appears. A style left loose in the styles folder itself files
under GENERAL.

Choosing a folder only SHOWS you its styles. It never loads anything, and the
lit cell stays lit wherever it is.

# Turning pages

Thirty cells to a page. The arrows either side of the page readout step through
the pages of the folder you are in, and they wrap, so the last page is one press
back from the first. A folder of twelve styles is one page and the arrows go
quiet; a folder of two hundred is seven.

A page with fewer than thirty styles simply shows fewer buttons. The grid keeps
its shape.

# Loading is one click

Click a cell and that style loads. There is no LOAD button to press afterwards
and no RELOAD button.

# Search

The SEARCH button opens a separate window. It has to be a real window rather
than a box on the page: inside a plugin the DAW owns the keyboard, so typing
into a box on the tab would drive the host's transport instead of entering text.

The window holds the search box on top, RESULTS on the left, and LAST SEARCHES
on the right -- your saved keywords, each with a bin to remove it. Click a
result and that style is selected, and the grid turns to its folder and its
page to show you where it lives.

It stays open on purpose. Finding the right style usually means trying two or
three, and a window that closed on the first click would make you re-open it and
re-type for the second.

# Favourites

Every cell carries a star. Starring a style marks it as a favourite so your
working set stays quick to find.

# One selection, everywhere

The lit cell is the style that is actually loaded, identified by its own
reference. Changing folders, turning pages or searching never moves that
highlight, so the grid can always be trusted to tell you what is playing.

The count on the pill is the WHOLE library, not the folder you are looking at --
it answers "how many styles do I have", which is a question about the library.

# Loading while playing

A style cannot be swapped while the band is running -- that would mean revoicing
every slot and rebuilding every kit mid-bar. Stop, load, and start again. The
pill above the grid says so rather than silently doing nothing.

# Every style brings its own set

Loading a style also loads the set saved with it: the sounds on all sixteen
channels, the mixer, the jumps, the crash settings and the style levels. That is
why a freshly loaded style already sounds finished, and why saving a set is how
you keep any change you make.
)TXT") },

    //--------------------------------------------------------------------------
    { "SOUNDS", TutorialTab::parse (R"TXT(
# Sixteen channels

- Eight STYLE channels, one per style element, played by the band.
- Eight SOLO channels, played by your right hand.

The SOUNDS tab is where you choose what each of them plays and how it sounds.

# Choosing a sound

The selector is four vertical columns, each one feeding the next: pick a SLOT,
then PACK, then CATEGORY, then INSTRUMENT, working left to right. Drum slots
also offer the sampled kits found in your sounds folder, alongside the built-in
composed kits.

# The three sound packs

PACK chooses between GM, WORLD and ORIENTAL. GM ships with the plugin and is
always there. WORLD and ORIENTAL are optional downloads that install by dropping
their folder into your system folder and uninstall by deleting it -- nothing is
merged and nothing is left behind.

A pack you have not installed still shows its button. Press it and the category
list is empty, which is the plainest way of saying "not installed".

If you want a slot to keep the sound YOU chose and ignore what the style asks
for, use IGNORE PROGRAM CHANGE on that slot. Without it, loading a style is
allowed to replace the voice.

# The sound editor: four tabs

- SYNTHESIS -- envelopes, filter, allowed notes, and (on solo slots) GAIN.
- MODULATION -- the LFOs, and ATTACK GLIDE.
- EFFECT SENDS -- the five-band EQ, PAN, and three sends.
- INSERTS -- wah, phaser and the sweetener.

# Where the effects actually live

This is the one thing worth reading twice, because it changed.

CHORUS, REVERB and DELAY are no longer per instrument. There is ONE of each per
hand: one rack for the sixteen style channels and one for your eight solos. What
each instrument owns is HOW MUCH of itself it sends there, and those are the
three sliders on the EFFECT SENDS tab.

The rack itself opens from the GLOBAL EFFECTS button under the eight slot
selectors. Open it from any slot in a hand and you see that hand's settings,
because there is only one rack behind them.

WAH, PHASER and SWEETENER went the other way and stayed per instrument, on the
INSERTS tab. They are shapers -- their output replaces the signal rather than
adding to it -- so a parallel send would give you a wah you cannot hear and a
sweetener that only doubles the level. They also want different settings on
every instrument, which a shared rack cannot give.

EQ and PAN are per instrument for the same reason: tilting one voice should not
tilt sixteen.

# The reverb's EARLY REFLECTIONS

The global reverb has an ER control and an ER SIZE beside it. Early reflections
are the first bounces off the walls -- the part that tells your ear how big the
room is, before the tail arrives.

ER starts at zero, so nothing you already saved sounds any different until you
reach for it.

# ATTACK GLIDE

On the MODULATION tab, and on SOLO SLOTS ONLY: a small pitch scoop into the true
note, the way a real player leans into a phrase. The style's own parts already
carry that kind of articulation, which is why it is not offered there.

- OFF, EVERY note, EVERY Nth note, or RANDOM.
- DEPTH is how far it scoops, TIME is how long it takes, SHAPE is the curve.
- EVERY sets the N; ODDS sets the odds in random mode, read as "about 1 in N".

# GAIN, and where a style channel's level lives

GAIN appears on SOLO slots only, 0 to 200 with 100 as unity. Your right hand is
yours to calibrate.

Style slots have no gain slider, and that is the point: the style states its own
level and the mixer adjusts it afterwards. See the MIXER chapter -- everything
about a style channel's loudness is decided there now, including the per-slot
base trim for a part that arrives far too loud or far too quiet.

# ALLOWED NOTES

ALLOWED NOTES decides which part of the keyboard a part is folded into -- how
you stop a bass patch from being dragged into an octave it was never sampled
for.

The one rule is a floor of twelve semitones. A window narrower than an octave
cannot contain every note name, so a folded note could have nowhere legal to
land. Above that floor, every channel can be given whatever window you want.

# The SWEETENER

The sweetener is a small dynamics block for taking the aggression out of a
sound, especially drums from converted styles. It is named jobs rather than
engineer's numbers:

- SOFTEN rounds off the attack, or adds attack when you push it the other way.
- PEAK holds the loudest moments back.
- TAME pulls down a harsh frequency band only when it gets loud.
- ROUND adds gentle saturation, blended in parallel so definition survives.

Each stage has its own button, so you can hear one at a time.

# The drum rack

A drum slot opens its own editor with six pages: EQ, SAT, COMP, SWEET, PAN and
SENDS. Everything above about the global racks applies here too -- the drums
have their own EQ, saturation, compressor, sweetener and pan, and they send to
the same chorus, reverb and delay as the rest of the left hand.

Right-click any key on the editor keyboard to hear that drum on its own.

# FUNKEY MODE

FUNKEY MODE is a performance switch, so it sits on the play-control row between
ONPRESS and CRASH, and it is also reachable from Settings.

It gives SIX instrument families -- chromatic percussion, organ, guitar, synth
lead, ethnic and percussive -- their own WAH and PHASER.

It is a STAGE OF ITS OWN, in front of the instrument:

    STYLE INSTRUMENT  ->  FUNKEY  ->  CHANNEL EFFECTS

So the family's wah and phaser run first and hand an already-wet signal to the
instrument's own chain, which is left completely alone. A slot KEEPS its private
wah and phaser while the macro is on, and the two run in series. Nothing else
changes: the EQ, the sends and everything on the global racks stay as the set
left them.

STYLE CHANNELS ONLY. Your right hand is never funkeyed -- a wah opening and
closing under the melody would be fighting the performance rather than backing
it.

It applies only to the six families named above. A piano, a string pad or a bass
is outside them and passes through untouched.

# Editing a family

The window opens six family buttons, one editor each, and every editor shows two
pages: WAH and PHASER. Nothing else, because nothing else is in this stage.

There is no ON/OFF inside that window. The FUNKEY button on the play-control row
is the only switch. To silence one effect for one family, set its MIX to zero.

# Presets

- A solo sound saves as an .ins preset.
- The same sound inside a style saves as a .sins preset, because a piano under
  your right hand and a piano inside an arrangement want different settings.
- Drum kits save as .drm.

Saving a preset re-applies it to every channel currently holding that
instrument, so you can calibrate by ear without stopping the band.

# The set owns a style channel's voicing

Everything you set on a style channel -- envelopes, filter, EQ, sends, inserts --
belongs to the SET, not to the instrument that happens to be loaded. So when a
style swaps an instrument mid-song, your voicing stays. The new sound arrives
already shaped the way you shaped that channel.
)TXT") },

    //--------------------------------------------------------------------------
    { "MIXER", TutorialTab::parse (R"TXT(
# Three sections

- LEFT HAND / STYLE: the eight band channels, with the STYLE VOLUME bus fader
  under them.
- RIGHT HAND / SOLO: your eight solo channels, with the SOLO VOLUME bus fader
  under them.
- MASTER: one vertical fader for everything. The knob at the right of the tab
  row shows the same value, so the master is reachable without leaving the page
  you are on.

# The rule the whole mixer is built on

THE STYLE DECIDES, AND YOU ADJUST AFTERWARDS.

Whatever level a style writes for a part is that part's base, and nothing argues
with it. Your fader is a separate multiplier applied on top. At the 127 detent
you are hearing exactly what the composer asked for; below it you are trimming
their decision rather than replacing it.

That is why style channels no longer have a gain slider in the sound editor.
There is one number for the style's opinion and one for yours, and they never
compete over the same control.

# One scale everywhere

Every fader reads 0 to 254 on the same linear scale:

- 0 is silence.
- 127 is unity -- exactly what the style asked for.
- 254 is unity doubled.

The odd-looking numbers are honesty: a style writes its channel volumes in MIDI,
and 0 to 127 is what it writes. That number is shown unchanged. Everything from
128 upward is headroom that only you can add.

# Faders on a style load

The faders return to 127 when a style loads, and then the style's set applies
its own. That is on purpose: a fader left over from the last song is a level
nobody chose for this one.

# What unity is worth

Double-click STYLE VOLUME or SOLO VOLUME and you can type a BASE UNITY in dB.
That does not move the fader -- it changes what the fader's unity detent is
WORTH. Use it when 127 is the right position musically but the wrong loudness.

The two are stored differently, on purpose:

- STYLE base unity is saved WITH THE SET. How loud the band should sit is a
  property of the song.
- SOLO base unity is saved GLOBALLY. The balance between your two hands is a
  property of you, and no song should change it.

# SLOT BASE TRIM

Double-click one of the eight STYLE channel faders and you get the same box for
that slot alone: a fixed dB trim, plus or minus 40.

This is the blunt instrument for a part a style sent at 12 or at 127, where the
fader alone cannot reach. It is applied after the style's level and after the
fader, it never changes what the style sends, it is saved with the set, and it
returns to 0 dB when a style loads.

# ENERGY

ENERGY is the tall slider on the left panel, and it is about how HARD the band
plays rather than how loud.

It reads 0 to 200, with 100 meaning "exactly as written". Below 100 the band
plays back, above it the band digs in. It reaches every style channel at once
and is saved with the set.

Use it when a style is right in every way except its attitude -- a ballad that
needs to sit further back behind a singer, or one that needs to lift for a last
chorus.

# Working with it

- Balance the band with the eight style faders.
- Set the band against your right hand with the STYLE and SOLO bus faders.
- Leave MASTER for the overall level into your DAW.

Mixer positions are part of the set, so SAVE SET keeps them with the style.
)TXT") },

    //--------------------------------------------------------------------------
    { "FINISHER", TutorialTab::parse (R"TXT(
# What it is

The FINISHER is the last thing the sound passes through before it leaves the
plugin. Open it from the mixer. It has two tabs: MASTER, the chain on the
finished mix, and DUCKER.

# MASTER

One chain, in order: a high-pass, a low shelf and an air shelf, a bass crossover
with a weight control, stereo width, glue compression, auto level, drive, and a
ceiling limiter with an output trim.

AMOUNT is a dry/wet across the whole chain, so you can dial the entire thing in
and out with one control. Each stage also has its own tick to switch it off on
its own.

GR shows how much the chain is pulling the level down at any moment.

A / B BYPASS compares the finished sound with the raw one. RESET puts every
control back to the factory values.

# DUCKER -- why it exists

The band and your right hand share the master bus. Anything on that bus reacts
to the LOUDEST thing arriving at it, so pushing the style up used to pull your
melody down with it. That is backwards. The melody is the thing that has to stay
audible; the band is what should give way.

The ducker makes the band give way, and it does it BEFORE the two are mixed,
where the band can still be treated on its own.

# How it works

Two points sit on the style. Each one listens to your RIGHT HAND at its own
frequency, and cuts the band there when you play. Play nothing and the band is
untouched. Play a line and a window opens exactly where that line lives.

Listening per point is the whole idea. A low melody note moves the low point and
leaves the high one alone, so the band keeps its brightness while its middle
gets out of the way. A ducker that listened to everything at once would just
pump the whole band up and down.

# Setting a point

Drag a point across to choose its frequency and DOWN to say how deep it may cut.
Down is depth: the further down, the more room it can make.

Depth is a CEILING, not a fixed amount. It says how far the cut is ALLOWED to
go; the dynamics decide how much of that gets used from one moment to the next.

Each point has five more controls:

- Q is how wide the cut is. Low Q takes a broad swathe, high Q takes a narrow
  slice.
- THRESH is how loud your right hand has to be in that band before anything
  happens.
- RATIO is how hard it responds once it is over the threshold.
- ATTACK is how fast the cut arrives. Fast keeps consonants clear; slow is
  gentler.
- RELEASE is how fast the band comes back once you stop.

Double-click a point to set its depth back to zero.

# Reading the display

Two marks per point, and they say different things:

- The hollow RING is where you dragged it. That is your ceiling.
- The filled DOT is the cut being applied RIGHT NOW. At rest it sits at the top
  and the curve is flat. As you play, it falls toward the ring and the curve
  opens under it.

That movement is the ducker working. If the dot never leaves the top, nothing is
ducking.

# The KEY meter

Under each point's name is a KEY bar with a bright tick on it. The bar is how
much of your right hand that point is actually hearing at its frequency. The
tick is its threshold.

This is how you find out why a point is not moving, and there are only two
reasons:

- The bar never reaches the tick. There is not enough of your playing at that
  frequency. Move the point to where your melody really sits, or lower its
  THRESHOLD.
- The bar passes the tick but nothing happens. You have not given the point any
  depth. Drag it down.

# Getting it right

Start gently. A few dB of depth on one point is usually enough -- the ear
notices a hole in the band far more readily than it notices the band being
quieter.

Work with the DUCKER tab open and watch the dots while you play the song you
actually play. A setting that looks right on a held chord often does nothing on
a real melody.

The ducker is saved with the set, so different songs can give way in different
places.
)TXT") },

    //--------------------------------------------------------------------------
    { "JUMPS", TutorialTab::parse (R"TXT(
# What jumps decide

Some sections are meant to end somewhere. An intro finishes and something has to
follow it; a fill runs and then lands. The JUMPS tab is the table of those
destinations.

Each row is a source -- the three intros, the four fills, and the break, eight
in all. Each column is where it can land. One destination per row, so the table
always has exactly one answer for every section.

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

A crash cymbal is what makes a section change sound intentional. Style files do
not always place them where a player would, so they can be added for you.

# The three triggers

- CRASH ON TRANSITION hits a crash when a section change LANDS -- at the end of
  a fill, on the downbeat of the new groove. That is where a drummer plays it:
  the fill is the run-up, and the cymbal marks the arrival, not the departure.
- AUTO CRASH hits one every N cycles of the current variation, for songs that
  want a marker every four or eight bars. The count restarts whenever a new
  variation begins, so a fill in the middle never throws the count off.
- CRASH NOW is a manual hit, and it works whether or not a style is playing.

An ending deliberately gets no automatic crash: a cymbal there rings on past the
final chord.

# Shaping the hit

- VELOCITY is a fixed weight, so every crash lands the same way. It also chooses
  which layer of the cymbal sample speaks, so it changes the character of the
  hit as well as the loudness.
- GAIN is how loud that hit sits in the mix, on the same 0-200 scale as the
  sound gains, with 100 as unity. Double-click its handle to set what that unity
  is worth in dB, without moving the handle.
- The element buttons choose WHICH cymbals fire, so you can pick a crash, a
  splash or several at once.

SAVE keeps these settings for the whole plugin -- they are your playing habit,
not a property of one style.
)TXT") },

    //--------------------------------------------------------------------------
    { "SETS", TutorialTab::parse (R"TXT(
# What a set is

A set is everything about how a style sounds in YOUR hands: the sounds on all
sixteen channels, the mixer and its base trims, the jumps table, the style
levels, ENERGY, the tempo offset, the ducker, and the global options saved with
a song.

Two things are deliberately NOT in it, because they describe your rig rather
than a song: the SPLIT POINT and the SOLO VOLUME base unity. Those follow you
from set to set and are saved on their own.

Every style ships with its own set, and loading a style loads it. This is why
you did not have to configure anything to get a good sound in QUICK START.

# The set manager

The three buttons on the left panel:

- LOAD SET opens a set file.
- SAVE SET writes the current state, and asks you where.
- FAST SAVE overwrites the set you are on, with no dialogue. This is the one you
  will use most: change something, hear that it is better, keep it.

The set name is shown underneath so you always know what you are editing.

# COMMENTS

The COMMENTS button opens a free text note saved inside the set. Use it for the
key a song is in, which variation the chorus wants, or anything else you will
not remember at the next gig.

# The SET EDITOR

The SET EDITOR is a page inside the SETTINGS tab now, alongside CONTROL NOTE MAP
and the rest. It used to be a tab of its own; it is maintenance rather than
performance, so it moved in with the other maintenance.

# BOOST

BOOST is the one level control that lives there: an overall gain for the whole
band, 0 to 100, reaching up to +24 dB on the style bus. The default is 80.

It is the only slider on that panel. FOLLOW PROGRAMMED GAINS is gone, and so are
MAKEUP and IGNORE STYLE VOLUMES before it. All three existed to referee a fight
between the style's levels and yours, and that fight no longer happens: the
style states the base, your fader multiplies it afterwards, and there is nothing
left to blend between. See the MIXER chapter.

# BAKE MISSING and REBUILD ALL

Two buttons on the same page write the starting point for the eight STYLE SLOTS
into style sets, worked out from what each style actually asks for.

- BAKE MISSING fills only the sets that have no slot block yet, and never
  touches one that already has one. This is the everyday button and it is safe
  to press at any time.
- REBUILD ALL overwrites the slot block in EVERY set, including ones you tuned
  by hand. There is no undo.

Either way only the STYLE slots are rewritten. Your solo slots, the mixer, the
jumps, the crash settings, the style levels and the ducker are left alone. The
INFO button on the page says all of this again at the moment of doubt.
)TXT") },

    //--------------------------------------------------------------------------
    { "SETTINGS", TutorialTab::parse (R"TXT(
# Five pages

The SETTINGS tab holds CONTROL NOTE MAP, MIDI ASSIGNING, REGISTRATION, GLOBAL
SETTINGS and SET EDITOR.

# REGISTRATION

The page shows your MACHINE ID -- a number belonging to this computer. Click it
to copy it, send it to us, and type the serial that comes back into the box
below. The lamp in the header turns green and the line under it reads
REGISTERED.

The same lamp is how you check at a glance: green and REGISTERED, or red and NOT
REGISTERED with a note telling you where to go.

Until a valid serial is entered, Grex runs in demo mode -- it plays normally but
the audio mutes briefly every few seconds. Everything else works, so you can set
up a whole rig before registering.

A serial is tied to the machine that asked for it, so a serial from another
computer will be refused.

# CONTROL NOTE MAP

MIDI notes 0 to 35 are reserved as remote controls, so a controller with pads or
a second keyboard can drive the arranger without you touching the screen. Notes
1 to 8 mute and unmute the style elements, 9 to 14 select solo channels, 15 to
30 fire the sixteen section pads, and 31 to 35 cover play/stop, restart, hold,
arranger/piano and synced play.

The page is a read-only reference. Print it, or leave the tab open while you
program your controller.

# MIDI ASSIGNING

Assigning is done by DRAGGING, not by typing, and it happens on the main screen
rather than on this page.

- Move a knob, press a pad, push a pedal. It appears as a chip in the MIDI LINK
  box on the left panel, which keeps the last six sources newest first.
- Drag that chip onto any control in the plugin -- or tap it to arm it and then
  tap the control, which is the easier gesture on a touch screen.
- A small window opens: PUSH, TOGGLE or KNOB, plus CLEAR and CANCEL. CANCEL puts
  the previous assignment back, because a drop replaces whatever else owned that
  source.

Right double-click any assigned control to open the same window again.

A source drives exactly one control. Assigning one that is already in use frees
the older owner rather than leaving two things fighting over it.

This page itself holds the housekeeping: SAVE and LOAD for named map files, and
UNLEARN ALL, which puts every assignment back to the factory control-note map
after asking first. Maps are also saved automatically, so twenty minutes of
mapping is never lost to a closed window.

# GLOBAL SETTINGS

Two buttons and two settings.

- FUNKEY MODE opens the macro editor -- the six instrument families, each with
  its own wah and phaser. See the SOUNDS chapter. The button here only OPENS the
  editor; the switch that engages the macro is on the play-control row.
- RESET SOLO BASE GAIN sets the base gain of every solo instrument back to 0 dB
  and saves every .ins file. It asks first, and there is no undo.
- PITCH BEND RANGE sets how far the wheel bends your solo instruments, from 1 to
  12 semitones.
- LOW VELOCITY RESPONSE lifts your softest playing on the solo channels, 0 to
  100, where 0 is off. It is a correction for a keyboard that plays light, not a
  voicing, which is why it sits here and not in the sound editor.

Both settings apply to the RIGHT HAND only, and both belong to your rig rather
than to a song.

# Where things are saved

- Set files hold everything that belongs to a SONG.
- The split point, the solo base unity, the pitch bend range, the low velocity
  response, the MIDI assignments, the crash settings and the global macros
  belong to the RIG, and are saved once for the whole plugin.
)TXT") },
};




