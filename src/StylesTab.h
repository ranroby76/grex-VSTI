#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "GlobalMacros.h"   // Betel::displayNameFor

#include <cmath>

#include "StyleFavorites.h"   // the star on every cell

//==============================================================================
//  StylesTab — genre + style browser.
//
//  Layout (full width, stacked):
//     • Genre grid : 2 x 8 = 16 selectors.  Labels come from the sub-folder
//                    names of <root>/styles (the host pushes them via
//                    setGenres()).  Unused slots are hidden.
//     • Header     : page nav  |  selected-style pill  |  STYLE DATA / count.
//     • Style grid : 4 x 8 = 32 cells, paged.  For the active genre the host
//                    pushes the style display names + parallel absolute paths
//                    (setGenreStyles()); the tab arranges them alphabetically
//                    across pages.  Clicking a cell reports the absolute path
//                    via onStyleSelected so the host loads by full path.
//
//  Global search lives outside this tab (a fixed bar under the main menu), so
//  there is no search genre here.
//
//  ── THE SELECTION IS A PATH, NOT AN INDEX ───────────────────────────────────
//
//  It used to be an index into the ACTIVE genre's list, which meant the lit
//  cell was really "cell N of whatever I am looking at".  Browsing to another
//  genre lit its cell 0, and pushing a genre's styles lit cell 0 again, so the
//  grid regularly showed a style highlighted that had never been loaded — and
//  two different genres could each claim to be showing the selection.
//
//  The selection is now the loaded style's ABSOLUTE PATH, which is unique across
//  the whole library.  A cell lights iff its own path matches, so exactly one
//  button in the entire folder tree can be lit at a time, and it stays lit —
//  through page turns, genre browsing and searches — until the user picks a
//  different style.  Browsing genuinely only browses now.
//
//  ── STYLE CHANGES ARE FORBIDDEN WHILE PLAYING ───────────────────────────────
//
//  onIsPlaying lets the host veto a selection.  Swapping the style under a
//  running arrangement means re-voicing every slot and recomposing every kit
//  mid-bar, so the transport has to be stopped first; the tab says so in the
//  pill rather than silently doing nothing.
//==============================================================================
class StylesTab : public juce::Component,
                  private juce::Timer
{
public:
    static constexpr int kNumGenres = 16;            // 2 x 8
    static constexpr int kGenreCols = 8;
    static constexpr int kGenreRows = 2;

    static constexpr int kGridCols  = 8;             // 4 x 8 = 32 styles / page
    static constexpr int kGridRows  = 4;
    static constexpr int kPageSize  = kGridCols * kGridRows;

    // Fired when the user picks a style cell — reports its absolute path.
    std::function<void(const juce::String& absolutePath)> onStyleSelected;

    // Header LOAD.  (RELOAD is gone - a style is re-read whenever it is loaded,
    // so the button only ever repeated what clicking the style already did.)
    std::function<void()> onLoadStyle;


    // Header STYLE DATA — open the metadata popup for the loaded style.
    std::function<void()> onShowStyleData;

    // Host predicate: is the arranger currently running?  A style may only be
    // changed from STOP, so a true answer refuses the selection.  Absent = never
    // playing, i.e. no restriction.
    std::function<bool()> onIsPlaying;

    // A star was toggled.  The tab has already written the flag; this is the
    // host's chance to react (nothing needs to today).
    std::function<void(const juce::String& absolutePath, bool isFavorite)> onFavoriteToggled;

    // Host predicate for the STARTUP auto-arm: "is a style still missing?".
    // Lets the host veto the arm when something else (default.bset, a restored
    // session) already loaded one.  Absent = always arm.
    std::function<bool()> onNeedsStyle;

    //==========================================================================
    // AUTO-ARM — guarantee a style is loaded and ready to play.
    //
    // STARTUP ONLY.  A style is armed when the plugin comes up with none — and
    // nowhere else: not on a folder jump, not on any browsing action.  Changing
    // the selected style is an explicit act (a style button, or loading a set).
    //
    // The arm is DELAYED rather than immediate because the style list arrives
    // asynchronously: at startup the host is still scanning <root>/styles, so an
    // immediate selectFirstStyle() finds genreCount == 0 and silently no-ops —
    // the old behaviour, and why the transport could come up with nothing to
    // play.
    //
    // Any deliberate user action during the wait CANCELS the arm (see
    // onGridButtonClicked and the page buttons), so it can never fight the user
    // or double-load a style.
    //==========================================================================
    static constexpr int kAutoArmDelayMs = 1000;

    /** Startup: after the delay, select the first style of the first non-empty
        genre — unless onNeedsStyle says the host already has one. */
    void scheduleAutoArmFirstStyle (int delayMs = kAutoArmDelayMs)
    {
        startTimer (delayMs);
    }

    void cancelAutoArm() { stopTimer(); }

    int          getSelectedStyleIndex() const { return selectedStyleIndex; }
    juce::String getSelectedStyleName()  const { return selectedStyleName; }
    juce::String getSelectedStylePath()  const { return selectedStylePath; }

    /** Host -> tab: the style that is actually loaded.  The ONE thing that
        decides which cell is lit, anywhere in the library. */
    void setSelectedStylePath (const juce::String& absolutePath)
    {
        selectedStylePath = absolutePath;
        if (absolutePath.isNotEmpty())
            selectedStyleName = Betel::displayNameFor (juce::File (absolutePath));
        refreshGrid();
    }

    /** Restore a previous selection WITHOUT firing onStyleSelected.
        Used by the editor-reopen path: the processor kept the style loaded, so
        re-firing the callback would pointlessly reload it — the tab only needs
        to LOOK right again.  Matching is by NAME (the index and even the genre
        order can shift between sessions if the folders changed), falling back to
        a search across every genre before giving up. */
    void restoreSelection (int genreHint, const juce::String& styleName)
    {
        if (styleName.isEmpty()) return;

        int genre = -1, idx = -1;
        if (genreHint >= 0 && genreHint < genreCount)
            if (const int i = genreStyleNames[genreHint].indexOf (styleName); i >= 0)
            { genre = genreHint; idx = i; }

        if (genre < 0)
            for (int g = 0; g < genreCount && genre < 0; ++g)
                if (const int i = genreStyleNames[g].indexOf (styleName); i >= 0)
                { genre = g; idx = i; }

        if (genre < 0) return;          // style no longer present — leave as is

        cancelAutoArm();                // an explicit restore outranks any pending arm
        inSearchMode       = false;
        selectedGenre      = genre;
        selectedStyleIndex = idx;
        selectedStyleName  = styleName;
        // The path is what actually lights a cell — see the header.  Taken from
        // the same genre list the name was found in, so a restore lights the
        // right button even when two genres hold styles of the same name.
        selectedStylePath  = (idx < genreStylePaths[genre].size())
                                 ? genreStylePaths[genre][idx] : juce::String();
        currentPage        = idx / kPageSize;      // page the selection into view
        for (int i = 0; i < kNumGenres; ++i)
            genreButtons[i].setActive (i == selectedGenre);
        refreshGrid();
    }

    //==========================================================================
    // Host → tab: set the genre labels (folder names).  Up to kNumGenres are
    // shown; the rest are hidden.  Resets the selection to the first genre.
    void setGenres (const juce::StringArray& names)
    {
        genreCount = juce::jmin (kNumGenres, names.size());
        for (int i = 0; i < kNumGenres; ++i)
        {
            const bool used = i < genreCount;
            genreButtons[i].setButtonText (used ? names[i] : juce::String());
            genreButtons[i].setVisible (used);
            if (! used) { genreStyleNames[i].clear(); genreStylePaths[i].clear(); }
        }
        selectGenre (0);
    }

    // Host → tab: push one genre's styles (display names + parallel paths).
    void setGenreStyles (int genreIndex,
                         const juce::StringArray& names,
                         const juce::StringArray& paths)
    {
        if (genreIndex < 0 || genreIndex >= kNumGenres) return;
        genreStyleNames[genreIndex] = names;
        genreStylePaths[genreIndex] = paths;
        // NOTHING is auto-selected here any more.  This used to light cell 0
        // whenever a genre's styles arrived, which is how the grid ended up
        // showing a highlighted style the host had never loaded.  The lit cell
        // is whichever one matches selectedStylePath, and that only changes when
        // the user picks a style or the host restores one.
        if (genreIndex == selectedGenre) refreshGrid();
    }

    void setSelectedStyleName (const juce::String& name)
    {
        selectedStyleName = name;
        repaint();
    }

    // Host → tab: show global-search results in the grid (overrides the genre
    // browse view until cleared or a genre is picked).
    void showSearchResults (const juce::StringArray& names,
                            const juce::StringArray& paths,
                            const juce::String& keyword)
    {
        inSearchMode   = true;
        searchNames    = names;
        searchPaths    = paths;
        searchKeyword  = keyword;
        currentPage        = 0;
        // The selection survives a search: if the loaded style is among the
        // results its cell is still lit, and if it is not, nothing is.
        for (int i = 0; i < kNumGenres; ++i)
            genreButtons[i].setActive (false);
        refreshGrid();
    }

    void clearSearch()
    {
        if (! inSearchMode) return;
        inSearchMode = false;
        searchNames.clear();
        searchPaths.clear();
        searchKeyword.clear();
        selectGenre (selectedGenre);
    }

    int getSelectedGenre() const { return selectedGenre; }

    /** Select the first style of the first non-empty genre and fire
        onStyleSelected, so the host loads a style immediately.  Call once after
        the host has pushed all genres/styles (e.g. just after the startup
        rescan) to make a style available from the first second.  No-op if every
        genre is empty. */
    void selectFirstStyle()
    {
        for (int g = 0; g < genreCount; ++g)
        {
            if (! genreStylePaths[g].isEmpty())
            {
                selectGenre (g);            // selectedGenre = g, selectedStyleIndex = 0
                onGridButtonClicked (0);    // selects style 0 AND fires onStyleSelected
                return;
            }
        }
    }

    //==========================================================================
    StylesTab()
    {
        setOpaque (false);

        for (int i = 0; i < kNumGenres; ++i)
        {
            genreButtons[i].setClickingTogglesState (false);
            // Browsing a folder only SHOWS it.  It deliberately does not arm
            // anything: the selected style changes on an explicit style-button
            // press, on the startup arm below, or when a set is loaded — never
            // as a side effect of looking around.
            genreButtons[i].onClick = [this, i] { selectGenre (i); };
            addChildComponent (genreButtons[i]);
        }

        btnPagePrev.setButtonText ("-");
        btnPageNext.setButtonText ("+");
        btnPagePrev.onClick = [this]
        {
            cancelAutoArm();
            if (currentPage > 0) { --currentPage; refreshGrid(); }
        };
        btnPageNext.onClick = [this]
        {
            cancelAutoArm();
            if (currentPage < getNumPages() - 1) { ++currentPage; refreshGrid(); }
        };
        addAndMakeVisible (btnPagePrev);
        addAndMakeVisible (btnPageNext);
        addAndMakeVisible (lblPage);



        btnStyleData.setButtonText ("STYLE DATA");
        btnStyleData.onClick = [this] { if (onShowStyleData) onShowStyleData(); };
        addAndMakeVisible (btnStyleData);

        lblStyleCount.setJustificationType (juce::Justification::centredRight);
        lblStyleCount.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
        lblStyleCount.setFont (juce::Font (12.0f, juce::Font::bold));
        addAndMakeVisible (lblStyleCount);

        for (int i = 0; i < kPageSize; ++i)
        {
            gridButtons[i].setClickingTogglesState (false);
            gridButtons[i].onClick = [this, i] { onGridButtonClicked (i); };
            gridButtons[i].star.onClick = [this, i] { onStarClicked (i); };
            addAndMakeVisible (gridButtons[i]);
        }

        selectGenre (0);
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xFF1A1A1A));
        g.fillRoundedRectangle (genrePanelBounds.toFloat(), 5.0f);

        g.setColour (juce::Colour (0xFF181818));
        g.fillRoundedRectangle (gridPanelBounds.toFloat(), 5.0f);

        // Selected-style pill.
        auto pill = pillBounds.toFloat();
        g.setColour (juce::Colour (0xFF2A2A2A));
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (juce::Colour (0xFF555555));
        g.drawRoundedRectangle (pill, 6.0f, 1.0f);
        // A refusal outranks the style name for as long as it is showing, and
        // is drawn in red so it cannot be mistaken for the selection.
        g.setColour (notice.isNotEmpty() ? juce::Colour (0xFFE53935) : juce::Colours::white);
        g.setFont (juce::Font (juce::jmin (pill.getHeight() * 0.50f, 13.0f), juce::Font::bold));
        juce::String pillText = notice.isNotEmpty() ? notice : selectedStyleName;
        if (pillText.isEmpty())
            pillText = inSearchMode
                         ? ("Search: \"" + searchKeyword + "\"  ("
                              + juce::String (searchNames.size()) + ")")
                         : juce::String ("No Style Selected");
        g.drawFittedText (pillText, pillBounds.reduced (8, 2),
                          juce::Justification::centredLeft, 1);

        lblPage.setText ("Page " + juce::String (currentPage + 1) + " / "
                             + juce::String (juce::jmax (1, getNumPages())),
                         juce::dontSendNotification);
    }

    void resized() override
    {
        const int W = getWidth(), H = getHeight();
        const int pad = 6, gap = 4;

        // ── Genre band (2 x 8) across the top ────────────────────────────────
        const int genreBandH = juce::jmax (54, (int) (H * 0.20f));
        genrePanelBounds = { 0, 0, W, genreBandH };

        const int gcW = (W - 2 * pad - (kGenreCols - 1) * gap) / kGenreCols;
        const int gcH = (genreBandH - 2 * pad - (kGenreRows - 1) * gap) / kGenreRows;
        for (int i = 0; i < kNumGenres; ++i)
        {
            const int r = i / kGenreCols, c = i % kGenreCols;
            genreButtons[i].setBounds (pad + c * (gcW + gap),
                                       pad + r * (gcH + gap), gcW, gcH);
        }

        // ── Header row ───────────────────────────────────────────────────────
        const int hdrY = genreBandH + gap;
        const int hdrH = juce::jmax (28, (int) (H * 0.072f));

        const int navW  = juce::jmax (22, hdrH);
        const int pageW = 92;
        int hx = pad;
        btnPagePrev.setBounds (hx, hdrY, navW, hdrH);  hx += navW + 2;
        lblPage    .setBounds (hx, hdrY, pageW, hdrH);  hx += pageW + 2;
        btnPageNext.setBounds (hx, hdrY, navW, hdrH);   hx += navW + 6;

        const int countW   = juce::jmax (90, (int) (W * 0.11f));
        const int dataW    = juce::jmax (90, (int) (W * 0.10f));
        const int rightEdge = W - pad;

        // RELOAD used to own the right edge; the counter has taken it, and the
        // pill below grows into the width the button was holding.
        lblStyleCount.setBounds (rightEdge - countW, hdrY, countW, hdrH);
        btnStyleData .setBounds (lblStyleCount.getX() - 6 - dataW, hdrY, dataW, hdrH);

        const int pillX = hx;
        pillBounds = { pillX, hdrY, btnStyleData.getX() - pillX - 6, hdrH };

        // ── Style grid (4 x 8) fills the rest ────────────────────────────────
        const int gridY = hdrY + hdrH + gap;
        const int gridH = H - gridY - pad;
        gridPanelBounds = { 0, gridY - 2, W, gridH + 4 };

        const int cellGap = 3;
        const int cellW = (W - 2 * pad - (kGridCols - 1) * cellGap) / kGridCols;
        const int cellH = (gridH - (kGridRows - 1) * cellGap) / kGridRows;
        for (int r = 0; r < kGridRows; ++r)
            for (int c = 0; c < kGridCols; ++c)
            {
                const int idx = r * kGridCols + c;
                gridButtons[idx].setBounds (pad + c * (cellW + cellGap),
                                            gridY + r * (cellH + cellGap), cellW, cellH);
            }

        refreshGrid();
    }

private:
    // ── Genre selector button ─────────────────────────────────────────────────
    class GenreButton : public juce::TextButton
    {
    public:
        void setActive (bool a) { active = a; repaint(); }
        void paintButton (juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
        {
            auto b = getLocalBounds().toFloat();
            if      (active)        g.setColour (juce::Colour (0xFFCC6600));
            else if (isButtonDown)  g.setColour (juce::Colour (0xFF444444));
            else if (isHighlighted) g.setColour (juce::Colour (0xFF2E2E2E));
            else                    g.setColour (juce::Colour (0xFF222222));
            g.fillRoundedRectangle (b.reduced (1.0f), 4.0f);
            // Black on the amber ON fill, white on the inert one.
            g.setColour (active ? juce::Colours::black : juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::Font (juce::jmin (b.getHeight() * 0.34f, 12.0f), juce::Font::bold));
            g.drawFittedText (getButtonText(), getLocalBounds().reduced (4, 2),
                              juce::Justification::centred, 2);
        }
    private:
        bool active = false;
    };

    // ── FAVOURITE STAR ────────────────────────────────────────────────────────
    //
    // Transparent when off, red when on — exactly that, no plate behind it and
    // no hover fill.  It sits in the top-right corner of a style cell and is a
    // CHILD of that cell, so it follows the cell through every layout and the
    // click lands on the star rather than on the button underneath (JUCE routes
    // a press to the topmost component under the mouse).
    //
    // Off is drawn as an outline rather than nothing at all: an invisible
    // control cannot be discovered, and a hollow star reads as "you may fill
    // this" without competing with the style name beside it.
    //==========================================================================
    class StarButton : public juce::Button
    {
    public:
        StarButton() : juce::Button ("fav") { setWantsKeyboardFocus (false); }

        void setOn (bool o) { on = o; repaint(); }
        bool isOn() const   { return on; }

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            const auto star = makeStar (b.getCentreX(), b.getCentreY(),
                                        juce::jmin (b.getWidth(), b.getHeight()) * 0.5f);

            if (on)
            {
                g.setColour (juce::Colour (0xFFE53935));       // red fill
                g.fillPath (star);
            }
            else
            {
                // Transparent body.  The outline brightens on hover so the
                // control still answers the mouse.
                g.setColour (juce::Colours::white.withAlpha (isOver ? 0.75f : 0.35f));
                g.strokePath (star, juce::PathStrokeType (1.2f));
            }

            if (isDown)
            {
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.strokePath (star, juce::PathStrokeType (1.2f));
            }
        }

    private:
        /** A five-point star, outer radius r, inner radius 0.42r, first point
            straight up. */
        static juce::Path makeStar (float cx, float cy, float r)
        {
            juce::Path p;
            const float inner = r * 0.42f;
            for (int i = 0; i < 10; ++i)
            {
                const float rad = (i % 2 == 0) ? r : inner;
                const float ang = juce::MathConstants<float>::pi * (-0.5f + (float) i * 0.2f);
                const float x = cx + rad * std::cos (ang);
                const float y = cy + rad * std::sin (ang);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            p.closeSubPath();
            return p;
        }

        bool on = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StarButton)
    };

    // ── Style cell ─────────────────────────────────────────────────────────────
    class SmallButton : public juce::TextButton
    {
    public:
        SmallButton() { addAndMakeVisible (star); }

        void setHighlighted (bool h) { highlighted = h; repaint(); }

        /** The star lives in the top-right corner and is sized off the cell, so
            it stays proportionate at every grid size. */
        void resized() override
        {
            const int d = juce::jlimit (12, 20, getHeight() / 3);
            star.setBounds (getWidth() - d - 3, 3, d, d);
        }

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat();
            if      (highlighted) g.setColour (juce::Colour (0xFFCC6600));
            else if (isDown)      g.setColour (juce::Colour (0xFF444444));
            else if (isOver)      g.setColour (juce::Colour (0xFF333333));
            else                  g.setColour (juce::Colour (0xFF252525));
            g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);
            g.setColour (juce::Colour (0xFF555555));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
            // Black on the amber selection — the selected style is the one lit
            // control on this page and white on #CC6600 reads worst of all here,
            // where the label is a long style name at 12 px.
            g.setColour (highlighted ? juce::Colours::black
                                     : juce::Colours::white.withAlpha (0.80f));
            g.setFont (juce::Font (juce::jmin (b.getHeight() * 0.34f, 12.0f), juce::Font::bold));
            // Reserved width on the right so a long style name cannot run under
            // the star.  The text stays centred in what is left, which reads
            // better than centring in the whole cell and colliding.
            auto textArea = getLocalBounds().reduced (3, 2);
            textArea.removeFromRight (star.getWidth() + 2);
            g.drawFittedText (getButtonText(), textArea,
                              juce::Justification::centred, 2);
        }

        /** Public: the tab wires its onClick and drives its state directly.
            A child rather than a painted glyph with a hit test, so JUCE routes
            the press for us and the cell underneath never sees it. */
        StarButton star;

    private:
        bool highlighted = false;
    };

    // ── Nav button (- / +) ───────────────────────────────────────────────────
    class NavButton : public juce::TextButton
    {
    public:
        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (isDown ? juce::Colour (0xFFCC6600)
                                : isOver ? juce::Colour (0xFF444444)
                                         : juce::Colour (0xFF2A2A2A));
            g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);
            g.setColour (juce::Colour (0xFF666666));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
            // Amber here is the held state, so the label follows the same rule.
            g.setColour (isDown ? juce::Colours::black : juce::Colours::white);
            g.setFont (juce::Font (juce::jmin (b.getHeight() * 0.55f, 15.0f), juce::Font::bold));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    // ── Members ───────────────────────────────────────────────────────────────
    GenreButton genreButtons[kNumGenres];
    SmallButton gridButtons[kPageSize];
    NavButton   btnPagePrev, btnPageNext, btnStyleData;
    juce::Label lblPage;
    juce::Label lblStyleCount;   // "N styles" for the current category

    juce::StringArray genreStyleNames[kNumGenres];
    juce::StringArray genreStylePaths[kNumGenres];
    int genreCount = 0;

    // Global-search overlay state.
    bool inSearchMode = false;
    juce::StringArray searchNames, searchPaths;
    juce::String      searchKeyword;

    static constexpr int kNoticeMs = 2200;   // how long the pill holds a refusal

    int selectedGenre      = 0;
    int selectedStyleIndex = -1;       // absolute index within the active list
    int currentPage        = 0;
    juce::String selectedStyleName;
    // THE selection.  Absolute path, so it identifies one style in the whole
    // library rather than one cell in the folder currently on screen.
    juce::String selectedStylePath;
    // Transient message shown in the pill instead of the style name.
    juce::String notice;

    juce::Rectangle<int> genrePanelBounds, gridPanelBounds, pillBounds;

    const juce::StringArray& activeNames() const
        { return inSearchMode ? searchNames : genreStyleNames[selectedGenre]; }
    const juce::StringArray& activePaths() const
        { return inSearchMode ? searchPaths : genreStylePaths[selectedGenre]; }

    // ── Helpers ───────────────────────────────────────────────────────────────
    int getNumPages() const
    {
        const int n = activeNames().size();
        return juce::jmax (1, (n + kPageSize - 1) / kPageSize);
    }

    void selectGenre (int g)
    {
        inSearchMode       = false;
        selectedGenre      = juce::jlimit (0, juce::jmax (0, genreCount - 1), g);
        currentPage        = 0;
        // DELIBERATELY does not touch the selection.  Opening a folder is
        // looking, not choosing; the loaded style keeps its light whether or not
        // it happens to live in the folder now on screen.
        for (int i = 0; i < kNumGenres; ++i)
            genreButtons[i].setActive (i == selectedGenre);
        refreshGrid();
    }

    void refreshGrid()
    {
        const auto& styles = activeNames();
        const int total = styles.size();
        lblStyleCount.setText (juce::String (total) + (total == 1 ? " style" : " styles"),
                               juce::dontSendNotification);
        const auto& paths  = activePaths();
        const int   offset = currentPage * kPageSize;
        for (int i = 0; i < kPageSize; ++i)
        {
            const int absIdx = offset + i;
            const bool has = absIdx < styles.size();
            const juce::String path = (has && absIdx < paths.size()) ? paths[absIdx]
                                                                     : juce::String();

            gridButtons[i].setButtonText (has ? styles[absIdx] : juce::String());
            gridButtons[i].setEnabled (has);

            // ONE lit cell in the whole library — the one whose path is the
            // loaded style's.  Not "cell N of the folder I am looking at".
            gridButtons[i].setHighlighted (path.isNotEmpty()
                                             && path == selectedStylePath);

            gridButtons[i].star.setVisible (has);
            gridButtons[i].star.setOn (path.isNotEmpty()
                                         && Betel::StyleFavorites::get().isFavorite (path));
        }
        repaint();
    }

    void onGridButtonClicked (int slotIndex)
    {
        const int absIdx = currentPage * kPageSize + slotIndex;
        const auto& names = activeNames();
        const auto& paths = activePaths();
        if (absIdx >= names.size()) return;

        // ── STOP-ONLY.  A style change re-voices all eight slots and recomposes
        //    every drum kit; doing that under a running arrangement is a
        //    guaranteed glitch at best.  Refuse, say so, and leave the current
        //    selection exactly where it is.
        if (onIsPlaying && onIsPlaying())
        {
            showNotice ("STOP the arranger to change style");
            return;
        }

        cancelAutoArm();        // the user chose — nothing may choose for them now
        clearNotice();

        selectedStyleIndex = absIdx;
        selectedStyleName  = names[absIdx];
        selectedStylePath  = (absIdx < paths.size()) ? paths[absIdx] : juce::String();
        refreshGrid();

        if (onStyleSelected && selectedStylePath.isNotEmpty())
            onStyleSelected (selectedStylePath);
    }

    /** The star is INDEPENDENT of the selection and of the transport: marking a
        favourite is a note about the library, not a load, so it is allowed at
        any time and never changes which style is playing. */
    void onStarClicked (int slotIndex)
    {
        const int absIdx = currentPage * kPageSize + slotIndex;
        const auto& paths = activePaths();
        if (absIdx >= paths.size()) return;

        const auto path = paths[absIdx];
        if (path.isEmpty()) return;

        const bool now = Betel::StyleFavorites::get().toggle (path);
        gridButtons[slotIndex].star.setOn (now);

        if (onFavoriteToggled) onFavoriteToggled (path, now);
    }

    /** A short message in the pill, in place of the style name.  Used for the
        stop-only refusal — a click that does nothing and says nothing reads as
        a broken button. */
    void showNotice (const juce::String& text)
    {
        notice = text;
        repaint();

        // NOT startTimer: this class's Timer belongs to the startup auto-arm,
        // and borrowing it here would either cancel a pending arm or fire
        // selectFirstStyle when the notice expired.  callAfterDelay is a free
        // one-shot, and the SafePointer covers the tab being destroyed first.
        juce::Component::SafePointer<StylesTab> safe (this);
        juce::Timer::callAfterDelay (kNoticeMs, [safe]
        {
            if (auto* t = safe.getComponent()) t->clearNotice();
        });
    }

    void clearNotice()
    {
        if (notice.isEmpty()) return;
        notice.clear();
        repaint();
    }

    void timerCallback() override
    {
        stopTimer();            // one-shot
        // Host veto: something already loaded a style during the wait.
        if (onNeedsStyle && ! onNeedsStyle()) return;
        selectFirstStyle();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StylesTab)
};
