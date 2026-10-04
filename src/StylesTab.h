
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the gold accent scheme
#include <juce_gui_basics/juce_gui_basics.h>
#include "GlobalMacros.h"   // Betel::displayNameFor

#include <cmath>

#include "StyleFavorites.h"   // the star on every cell

//==============================================================================
//  StylesTab — the Grex style browser.
//
//  Layout (full width, stacked, everything inside the tab canvas):
//     • Top row      : selected-style pill + count | ◀ | page | ▶ | SEARCH
//     • Category row : one button per SUBFOLDER of <root>/styles
//     • Style grid   : 6 x 5 = 30 cells, a clean matrix with no tail.
//
//  ── THE CATEGORY ROW IS THE FOLDER TREE, NOT A GENRE TABLE ──────────────────
//
//  Ballada shipped one genre, so its band of buttons was repurposed as a page
//  row - one button per page of a flat library.  Grex ships the full library
//  filed into subfolders, so the band goes back to being what it looks like: one
//  button per category, and the category list comes from the folders that
//  actually exist.  Nothing here knows the names in advance; add a folder, drop
//  styles in it, rescan, and a button appears.
//
//  THE PAGER MOVED UP INTO THE TOP ROW to pay for it.  The pill was running the
//  whole width for a style name that is rarely more than half of it, so it is
//  shortened and the freed space carries ◀ / page / ▶ - which is a pager that
//  costs one row instead of one row PER SIX PAGES, and that is what makes room
//  for the category row without the grid losing a cell.
//
//  ── PAGE COUNT IS DATA-DRIVEN, PER CATEGORY ─────────────────────────────────
//
//  Pages are counted from the ACTIVE CATEGORY's style count, never assumed and
//  never global: switching to a folder of 12 styles gives one page, switching to
//  one of 200 gives seven.  There is no page ceiling any more - the old kMaxPages
//  existed because each page needed a button of its own, and two arrows do not
//  care how many pages they step through.
//
//  ── THE SELECTION IS A REF, NOT AN INDEX ────────────────────────────────────
//
//  A cell lights iff its own style ref matches the loaded one, so exactly one
//  button in the whole library can be lit and it stays lit through page turns,
//  category changes and searches.  Browsing genuinely only browses.
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
    // ── THE PAGE SHAPE ───────────────────────────────────────────────────────
    // 6 x 5 = 30, a plain matrix.
    //
    // A SHORT PAGE KEEPS THE MATRIX AND SHOWS FEWER BUTTONS.  The cells are
    // laid out on the same 6 x 5 grid whatever the page holds; the ones with no
    // style behind them are HIDDEN, not drawn empty and disabled.  A dead cell
    // that looks like a live one is a button that invites the press which does
    // nothing, and twenty-nine of them on the last page of a category is a page
    // that reads as broken.
    //
    // FILLED LEFT TO RIGHT, ROW BY ROW.  Cell 0 is top-left, cell 5 ends the
    // first row, cell 6 starts the second at the left edge.  Stated because it
    // is the one thing a reader cannot infer from the constants.
    static constexpr int kPageCols  = 6;
    static constexpr int kPageRows  = 5;
    static constexpr int kPageSize  = kPageCols * kPageRows;                // 30

    // Sizes the category-button array only.  The number SHOWN comes from the
    // folders that were scanned.
    //
    // 24 = FOUR ROWS OF SIX.  Past that the band would take more height than the
    // grid it is filing, which is the point at which the tree needs flattening
    // rather than the band needs growing - so passing it is REPORTED in the pill
    // rather than silently dropping folders.
    static constexpr int kMaxCategories = 24;

    // Fired when the user picks a style cell — reports its style ref (a style id
    // in the library, or a legacy absolute path).
    std::function<void(const juce::String& styleRef)> onStyleSelected;

    // Host predicate: is the arranger currently running?  A style may only be
    // changed from STOP, so a true answer refuses the selection.  Absent = never
    // playing, i.e. no restriction.
    std::function<bool()> onIsPlaying;

    // A star was toggled.  The tab has already written the flag; this is the
    // host's chance to react (nothing needs to today).
    std::function<void(const juce::String& styleRef, bool isFavorite)> onFavoriteToggled;

    // Host predicate for the STARTUP auto-arm: "is a style still missing?".
    // Lets the host veto the arm when something else (default.bset, a restored
    // session) already loaded one.  Absent = always arm.
    std::function<bool()> onNeedsStyle;

    // ── SEARCH IS A WINDOW, AND THE TAB ONLY OPENS IT ────────────────────────
    //
    // The inline text box that used to sit here is gone, and not for layout
    // reasons: a plugin editor does not own the keyboard, the DAW does, and
    // typing into that box drove the host's transport instead of entering text.
    // See StyleSearchWindow.h - the fix is a real OS window, which is the only
    // thing Windows will hand keyboard focus to directly.
    //
    // THE PAGE READOUT BELOW IS A DRAWN FIELD, NOT AN EDITOR, for exactly the
    // same reason.  It shows "3 / 7"; the arrows are how you move.  Making it
    // typeable would reintroduce the fault the search box was moved out to
    // escape.
    std::function<void()> onSearchWindowRequested;

    //==========================================================================
    // AUTO-ARM — guarantee a style is loaded and ready to play.
    //
    // STARTUP ONLY.  A style is armed when the plugin comes up with none — and
    // nowhere else: not on a page turn, not on a category change, not on any
    // browsing action.  Changing the selected style is an explicit act (a style
    // button, or loading a set).
    //
    // The arm is DELAYED rather than immediate because the style list arrives
    // asynchronously: at startup the host is still scanning the folder, so an
    // immediate selectFirstStyle() finds an empty list and silently no-ops —
    // which is why the transport could come up with nothing to play.
    //
    // Any deliberate user action during the wait CANCELS the arm, so it can
    // never fight the user or double-load a style.
    //==========================================================================
    static constexpr int kAutoArmDelayMs = 1000;

    /** Startup: after the delay, select the first style — unless onNeedsStyle
        says the host already has one. */
    void scheduleAutoArmFirstStyle (int delayMs = kAutoArmDelayMs)
    {
        startTimer (delayMs);
    }

    void cancelAutoArm() { stopTimer(); }

    int          getSelectedStyleIndex() const { return selectedStyleIndex; }
    juce::String getSelectedStyleName()  const { return selectedStyleName; }
    juce::String getSelectedStylePath()  const { return selectedStylePath; }

    /** Which page is on screen.  Persisted in UiState so reopening the editor
        lands where the player left it. */
    int getSelectedPage() const { return currentPage; }

    /** Which category is on screen, by NAME rather than index — an index shifts
        the moment a folder is added or removed, and a saved index would then
        reopen on the wrong folder. */
    juce::String getSelectedCategory() const
    {
        return juce::isPositiveAndBelow (activeCategory, categories.size())
                 ? categories[activeCategory] : juce::String();
    }

    /** Host -> tab: the style that is actually loaded.  The ONE thing that
        decides which cell is lit, anywhere in the library.

        `ref` is a STYLE ID (`Latin/Bossa Nova`) or a legacy file path.

        `displayName` MUST be supplied for an id, and here is why it broke once:
        this used to derive the name with `displayNameFor (juce::File (ref))`,
        which for an id yields the text after the last slash cut at the first dot.
        The grid's cells are labelled from the library, so the two never matched
        and no cell ever lit.  A click selected something the grid could not show
        as selected, which is exactly what a click-twice-to-work bug feels like.

        Empty displayName keeps the old File-derived behaviour for a legacy
        path, which is still what an old set hands over. */
    void setSelectedStylePath (const juce::String& ref,
                               const juce::String& displayName = {})
    {
        selectedStylePath = ref;

        if (displayName.isNotEmpty())
            selectedStyleName = displayName;
        else if (ref.isNotEmpty())
            selectedStyleName = Betel::displayNameFor (juce::File (ref));

        refreshGrid();
    }

    /** Restore a previous selection WITHOUT firing onStyleSelected.
        Used by the editor-reopen path: the processor kept the style loaded, so
        re-firing the callback would pointlessly reload it — the tab only needs
        to LOOK right again.  Matching is by NAME, because an index can shift
        between sessions if the library changed. */
    void restoreSelection (const juce::String& styleName)
    {
        if (styleName.isEmpty()) return;

        const int idx = styleNames.indexOf (styleName);
        if (idx < 0) return;                // style no longer present — leave as is

        // ── CANCEL THE ARM ONLY IF A STYLE IS ACTUALLY LOADED ───────────────
        //
        // THIS WAS A CANCEL-THEN-FAIL HOLE.  restoreSelection deliberately does
        // NOT load anything - it only makes the tab LOOK right again, because
        // the reopen path assumes the processor kept its style.  When that
        // assumption is false (a project whose snapshot named a style that no
        // longer resolves) this cancelled the one remaining backstop and left
        // the grid showing a lit cell for a style that was never loaded.
        //
        // Everything downstream then looked healthy while hasStyle() stayed
        // false - and PLAY does nothing at all in that state.
        if (onNeedsStyle == nullptr || ! onNeedsStyle())
            cancelAutoArm();

        selectedStyleIndex = idx;
        selectedStyleName  = styleName;
        selectedStylePath  = (idx < stylePaths.size()) ? stylePaths[idx] : juce::String();

        revealLibraryIndex (idx);           // its category, then its page
        refreshGrid();
    }

    /** Select a style BY REF and turn the grid to its category and page, exactly
        as if the player had found the cell and clicked it.

        This is what the search window calls when a result is chosen.  It goes
        through the same onGridButtonClicked path as a real click rather than
        setting the selection directly, so the stop-only refusal, the auto-arm
        cancel and the onStyleSelected callback all fire once and in one order.
        A second path that "just selects" is how two ways of choosing a style
        drift into behaving differently. */
    bool selectStyleByRef (const juce::String& ref)
    {
        if (ref.isEmpty()) return false;

        const int idx = stylePaths.indexOf (ref);
        if (idx < 0) return false;

        revealLibraryIndex (idx);
        refreshGrid();                       // the cell must exist before it is clicked

        const int slot = activeIdx.indexOf (idx) - currentPage * kPageSize;
        if (! juce::isPositiveAndBelow (slot, kPageSize)) return false;

        onGridButtonClicked (slot);
        return true;
    }

    //==========================================================================
    /** Host → tab: THE WHOLE LIBRARY, three parallel arrays.

        `groups` is the category (subfolder) each style belongs to.  It may be
        shorter than the other two or empty; anything unnamed falls into one
        default category, so a caller that has no filing to offer still works.

        NOTHING is auto-selected.  Pushing styles used to light cell 0, which is
        how the grid ended up showing a highlighted style the host had never
        loaded.  The lit cell is whichever matches selectedStylePath, and that
        only changes when the user picks a style or the host restores one. */
    void setLibrary (const juce::StringArray& names,
                     const juce::StringArray& paths,
                     const juce::StringArray& groups)
    {
        styleNames  = names;
        stylePaths  = paths;
        styleGroups = groups;

        rebuildCategories();

        // KEEP THE CATEGORY THE PLAYER WAS LOOKING AT across a rescan, by name.
        // A rescan happens when the root folder moves or a style is added, and
        // snapping back to the first folder every time would throw away the one
        // piece of navigation state the player set by hand.
        const int keep = categories.indexOf (categoryBeforeRescan);
        activeCategory = keep >= 0 ? keep : 0;

        rebuildActive();
        refreshCategoryButtons();
        refreshGrid();
    }

    /** Flat overload — one category, everything in it.  Kept so a caller with no
        folder information (and the search window's tests) still compiles. */
    void setStyles (const juce::StringArray& names, const juce::StringArray& paths)
    {
        setLibrary (names, paths, {});
    }

    void setSelectedStyleName (const juce::String& name)
    {
        selectedStyleName = name;
        repaint();
    }

    /** Select the first style and fire onStyleSelected, so the host loads a
        style immediately.  No-op if the library is empty.

        LOOKS PAST THE ACTIVE CATEGORY.  If the folder on screen is empty but
        another holds styles, it switches to the first that does — "arm
        something" is the guarantee, and refusing because the player happened to
        leave an empty folder selected would break it for no reason.

        RETURNS FALSE rather than no-opping in silence: "the library is empty"
        and "I loaded one" used to be the same answer — void — so a caller
        relying on this as a last-resort guarantee could not tell the guarantee
        had not been met, and the instrument came up with nothing to play and
        nothing to say about it. */
    bool selectFirstStyle()
    {
        if (stylePaths.isEmpty()) return false;

        if (activeIdx.isEmpty())
        {
            revealLibraryIndex (0);
            if (activeIdx.isEmpty()) return false;
        }

        currentPage = 0;
        refreshCategoryButtons();
        refreshGrid();
        onGridButtonClicked (0);      // selects the first style AND fires the callback
        return true;
    }

    //==========================================================================
    StylesTab()
    {
        setOpaque (false);

        for (int i = 0; i < kMaxCategories; ++i)
        {
            categoryButtons[i].setClickingTogglesState (false);
            // Choosing a folder only SHOWS styles.  It deliberately does not arm
            // anything: the selected style changes on an explicit style-button
            // press, on the startup arm, or when a set is loaded — never as a
            // side effect of looking around.
            categoryButtons[i].onClick = [this, i] { selectCategory (i); };
            addChildComponent (categoryButtons[i]);
        }

        for (int i = 0; i < kPageSize; ++i)
        {
            gridButtons[i].setClickingTogglesState (false);
            gridButtons[i].onClick      = [this, i] { onGridButtonClicked (i); };
            gridButtons[i].star.onClick = [this, i] { onStarClicked (i); };
            addChildComponent (gridButtons[i]);
        }

        // ── The pager: two arrows either side of a drawn readout ─────────────
        btnPagePrev.setButtonText (juce::CharPointer_UTF8 ("\xe2\x97\x80"));   // ◀
        btnPageNext.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"));   // ▶
        btnPagePrev.onClick = [this] { stepPage (-1); };
        btnPageNext.onClick = [this] { stepPage (+1); };
        addAndMakeVisible (btnPagePrev);
        addAndMakeVisible (btnPageNext);

        // ── The one search control left: a button that opens the window ──────
        btnSearch.setButtonText ("SEARCH");
        btnSearch.onClick = [this] { if (onSearchWindowRequested) onSearchWindowRequested(); };
        addAndMakeVisible (btnSearch);

        refreshCategoryButtons();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xFF1A1A1A));
        g.fillRoundedRectangle (headerPanelBounds.toFloat(), 5.0f);

        g.setColour (juce::Colour (0xFF181818));
        g.fillRoundedRectangle (gridPanelBounds.toFloat(), 5.0f);

        // ── Selected-style pill ──────────────────────────────────────────────
        //
        // KEPT, and not as decoration: showNotice() draws here, and that is the
        // only way the stop-only refusal and the host's "no styles found" ever
        // reach the player.  It is SHORTER than it was because the pager now
        // shares the row - see resized().
        auto pill = pillBounds.toFloat();
        g.setColour (juce::Colour (0xFF2A2A2A));
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (juce::Colour (0xFF555555));
        g.drawRoundedRectangle (pill, 6.0f, 1.0f);

        // A refusal outranks the style name for as long as it is showing, and
        // is drawn in red so it cannot be mistaken for the selection.
        g.setColour (notice.isNotEmpty() ? juce::Colour (0xFFE53935) : juce::Colours::white);
        g.setFont (juce::Font (juce::jmin (pill.getHeight() * 0.50f, 13.0f), juce::Font::bold));

        juce::String pillText = notice.isNotEmpty() ? notice
                                                    : Betel::prettyName (selectedStyleName);
        if (pillText.isEmpty())
            pillText = "No Style Selected";

        // The count rides on the pill's right edge rather than owning a label of
        // its own — it is the same fact about the same list, and a second
        // control for it was one more thing to lay out and keep in sync.
        //
        // IT COUNTS THE WHOLE LIBRARY, not the folder on screen.  "how many
        // styles do I have" is the question it answers, and a number that
        // changed every time a category button was pressed would answer a
        // different one nobody asked.
        auto textArea = pillBounds.reduced (8, 2);
        const int total = styleNames.size();
        const juce::String countText = juce::String (total) + (total == 1 ? " style" : " styles");

        g.drawFittedText (pillText, textArea.withTrimmedRight (76),
                          juce::Justification::centredLeft, 1);

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::Font (juce::jmin (pill.getHeight() * 0.42f, 11.0f), juce::Font::bold));
        g.drawFittedText (countText, textArea, juce::Justification::centredRight, 1);

        // ── The page readout, between the two arrows ─────────────────────────
        //
        // DRAWN, NOT AN EDITOR.  See onSearchWindowRequested above: a text field
        // inside a plugin editor does not reliably get the keyboard on Windows,
        // and this one has nothing to gain from typing that the arrows do not
        // already give.
        auto box = pageBoxBounds.toFloat();
        g.setColour (juce::Colour (0xFF121212));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (juce::Colour (0xFF555555));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::Font (juce::jmin (box.getHeight() * 0.48f, 12.0f), juce::Font::bold));
        g.drawFittedText (juce::String (currentPage + 1) + " / " + juce::String (getNumPages()),
                          pageBoxBounds, juce::Justification::centred, 1);
    }

    void resized() override
    {
        const int W = getWidth(), H = getHeight();
        const int pad = 6, gap = 4;

        // ── Top row: pill | ◀ | page | ▶ | SEARCH ────────────────────────────
        //
        // The pager is sized first and the pill takes whatever is left, so a
        // narrow editor shortens the NAME rather than squeezing the controls
        // into something unclickable.
        const int rowH    = juce::jmax (26, (int) (H * 0.068f));
        const int btnW    = juce::jlimit (70, 110, W / 9);
        const int arrowW  = juce::jlimit (24, 34, W / 26);
        const int boxW    = juce::jlimit (52, 78, W / 14);
        const int pagerW  = arrowW * 2 + boxW + gap * 2;

        int x = W - pad - btnW;
        btnSearch.setBounds (x, pad, btnW, rowH);

        x -= gap + arrowW;
        btnPageNext.setBounds (x, pad, arrowW, rowH);

        x -= gap + boxW;
        pageBoxBounds = { x, pad, boxW, rowH };

        x -= gap + arrowW;
        btnPagePrev.setBounds (x, pad, arrowW, rowH);

        pillBounds = { pad, pad,
                       juce::jmax (140, W - 2 * pad - btnW - pagerW - 2 * gap), rowH };

        // ── Category row(s) ──────────────────────────────────────────────────
        //
        // THE HEADER GROWS WITH THE CATEGORY COUNT, AND THAT IS A FIX, NOT A
        // FLOURISH.  The band this replaced sized itself for exactly ONE row
        // while placing button 7 onwards on a second row below it - so the
        // moment the count passed six, the seventh button was drawn on top of
        // the first row of style cells.
        //
        // A LIBRARY WITH ONE CATEGORY GETS NO BAND AT ALL.  One full-width
        // button that selects the only thing there is costs a row of height and
        // answers nothing; the grid takes that height instead.
        const int catY   = pad + rowH + gap;
        const int catH   = juce::jmax (26, (int) (H * 0.078f));
        const int nCats  = getNumVisibleCategories();
        const int rows   = nCats >= 2 ? (nCats + kPageCols - 1) / kPageCols : 0;

        headerPanelBounds = { 0, 0, W,
                              rows > 0 ? catY + rows * catH + (rows - 1) * gap + pad / 2
                                       : pad + rowH + pad / 2 };

        // ── THE CATEGORY ROW FILLS THE WIDTH IT STARTS IN ────────────────────
        //
        // Divided by the number of buttons ACTUALLY ON THE ROW, so three folders
        // run from pad to W - pad instead of huddling in the left half and
        // reading as a row that failed to load.  Capped at kPageCols so a
        // library big enough to wrap keeps every row the same cell width rather
        // than stretching a short last row across the whole band.
        const int perRow = juce::jlimit (1, kPageCols, juce::jmax (1, nCats));
        const int cbW    = (W - 2 * pad - (perRow - 1) * gap) / perRow;

        for (int i = 0; i < kMaxCategories; ++i)
        {
            const int r = i / perRow, c = i % perRow;
            categoryButtons[i].setBounds (pad + c * (cbW + gap),
                                          catY + r * (catH + gap), cbW, catH);
        }

        // ── Style grid: kPageRows full rows, no tail ─────────────────────────
        const int gridY = headerPanelBounds.getBottom() + gap;
        const int gridH = H - gridY - pad;
        gridPanelBounds = { 0, gridY - 2, W, gridH + 4 };

        const int cellGap = 3;
        const int cellW = (W - 2 * pad - (kPageCols - 1) * cellGap) / kPageCols;
        const int cellH = (gridH - (kPageRows - 1) * cellGap) / kPageRows;

        // ROW BY ROW, LEFT TO RIGHT.  idx = r * kPageCols + c, so cell 6 is the
        // left of the second row rather than the top of the second column.
        for (int r = 0; r < kPageRows; ++r)
            for (int c = 0; c < kPageCols; ++c)
            {
                const int idx = r * kPageCols + c;
                gridButtons[idx].setBounds (pad + c * (cellW + cellGap),
                                            gridY + r * (cellH + cellGap), cellW, cellH);
            }

        refreshGrid();
    }

private:
    // ── Band button — one per category ────────────────────────────────────────
    class BandButton : public juce::TextButton
    {
    public:
        void setActive (bool a) { active = a; repaint(); }
        void paintButton (juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
        {
            auto b = getLocalBounds().toFloat();
            if      (active)        g.setColour (juce::Colour (Betel::Pal::kAccent));
            else if (isButtonDown)  g.setColour (juce::Colour (0xFF444444));
            else if (isHighlighted) g.setColour (juce::Colour (0xFF2E2E2E));
            else                    g.setColour (juce::Colour (0xFF222222));
            g.fillRoundedRectangle (b.reduced (1.0f), 4.0f);
            // Black on the gold ON fill, white on the inert one.
            g.setColour (active ? juce::Colours::black : juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::Font (juce::jmin (b.getHeight() * 0.46f, 13.0f), juce::Font::bold));
            g.drawFittedText (getButtonText(), getLocalBounds().reduced (4, 2),
                              juce::Justification::centred, 1);
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

        /** THE FILLED-STAR COLOUR.  ONE CONSTANT - change it here and nowhere
            else.  DARK red rather than the bright 0xFFE53935 it was: the star
            sits on a cell that is gold when the style is the loaded one, and a
            light red on gold read as a smudge rather than a mark.

            Deliberately NOT the same red as the pill's refusal text, which is
            still 0xFFE53935 and should stay bright - that one is a warning and
            wants to catch the eye; this one is a bookmark and should not. */
        static constexpr juce::uint32 kStarOn = 0xFF8B0000;

        void setOn (bool o) { on = o; repaint(); }
        bool isOn() const   { return on; }

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            const auto star = makeStar (b.getCentreX(), b.getCentreY(),
                                        juce::jmin (b.getWidth(), b.getHeight()) * 0.5f);

            if (on)
            {
                g.setColour (juce::Colour (kStarOn));
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
            if      (highlighted) g.setColour (juce::Colour (Betel::Pal::kAccent));
            else if (isDown)      g.setColour (juce::Colour (0xFF444444));
            else if (isOver)      g.setColour (juce::Colour (0xFF333333));
            else                  g.setColour (juce::Colour (0xFF252525));
            g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);
            g.setColour (juce::Colour (0xFF555555));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
            // Black on the gold selection — the selected style is the one lit
            // control on this page and white on gold reads worst of all here,
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

    // ── Flat action button (SEARCH, and the two pager arrows) ────────────────
    class FlatButton : public juce::TextButton
    {
    public:
        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (isDown ? juce::Colour (Betel::Pal::kAccent)
                                : isOver ? juce::Colour (0xFF444444)
                                         : juce::Colour (0xFF2A2A2A));
            g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);
            g.setColour (juce::Colour (0xFF666666));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
            // Gold here is the held state, so the label follows the same rule.
            g.setColour (! isEnabled() ? juce::Colours::white.withAlpha (0.25f)
                                       : isDown ? juce::Colours::black
                                                : juce::Colours::white);
            g.setFont (juce::Font (juce::jmin (b.getHeight() * 0.44f, 12.0f), juce::Font::bold));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    // ── Members ───────────────────────────────────────────────────────────────
    BandButton  categoryButtons[kMaxCategories];
    SmallButton gridButtons[kPageSize];

    FlatButton  btnSearch, btnPagePrev, btnPageNext;

    // THE LIBRARY, flat and parallel.  styleGroups may be shorter than the other
    // two; groupAt() answers the default category for anything it does not cover.
    juce::StringArray styleNames, stylePaths, styleGroups;

    // Derived from styleGroups, in the order they should be drawn.
    juce::StringArray categories;

    // Indices into the flat arrays for the category on screen.  Rebuilt on every
    // category change rather than filtered at draw time, so the page maths is
    // plain arithmetic on one array instead of a scan per cell.
    juce::Array<int> activeIdx;

    static constexpr int kNoticeMs = 2200;   // how long the pill holds a refusal

    /** The default category for a style whose group is empty or absent.  Matches
        FstLibrary::kUncategorised so the two never disagree about what a loose
        style is called. */
    static constexpr const char* kDefaultCategory = "GENERAL";

    int selectedStyleIndex = -1;       // absolute index within the whole library
    int activeCategory     = 0;
    int currentPage        = 0;
    juce::String selectedStyleName;
    // THE selection.  A style ref, so it identifies one style in the whole
    // library rather than one cell on the page currently on screen.
    juce::String selectedStylePath;
    // Which category was on screen when the last rescan arrived, so it can be
    // restored by name.
    juce::String categoryBeforeRescan;
    // Transient message shown in the pill instead of the style name.
    juce::String notice;

    juce::Rectangle<int> headerPanelBounds, gridPanelBounds, pillBounds, pageBoxBounds;

    // How many rows of category buttons the last layout was built for.  Compared
    // in refreshCategoryButtons so a library that crosses a row boundary re-lays
    // out.
    int lastCategoryRows = 0;

    // ── Helpers ───────────────────────────────────────────────────────────────
    juce::String groupAt (int i) const
    {
        if (juce::isPositiveAndBelow (i, styleGroups.size()) && styleGroups[i].isNotEmpty())
            return styleGroups[i];
        return kDefaultCategory;
    }

    int getNumVisibleCategories() const
    {
        return juce::jmin (categories.size(), kMaxCategories);
    }

    int getNumPages() const
    {
        const int n = activeIdx.size();
        return juce::jmax (1, (n + kPageSize - 1) / kPageSize);
    }

    /** The categories present, in first-appearance order.  The library arrives
        sorted by group then name, so that order is already alphabetical and
        sorting again here would only be a second opinion about the same list. */
    void rebuildCategories()
    {
        categories.clear();
        for (int i = 0; i < styleNames.size(); ++i)
        {
            const auto g = groupAt (i);
            if (! categories.contains (g)) categories.add (g);
        }

        // OVERFLOW IS REPORTED, NOT SWALLOWED.  Past kMaxCategories there is no
        // button to reach a folder with, and styles that exist, count toward the
        // total and cannot be opened are a fault nobody would look for here.
        if (categories.size() > kMaxCategories)
            showNotice ("MORE THAN " + juce::String (kMaxCategories)
                        + " STYLE FOLDERS - SOME ARE UNREACHABLE");
    }

    void rebuildActive()
    {
        activeIdx.clearQuick();
        if (categories.isEmpty()) return;

        activeCategory = juce::jlimit (0, categories.size() - 1, activeCategory);
        const auto want = categories[activeCategory];

        for (int i = 0; i < styleNames.size(); ++i)
            if (groupAt (i) == want)
                activeIdx.add (i);

        categoryBeforeRescan = want;
        currentPage = juce::jlimit (0, getNumPages() - 1, currentPage);
    }

    /** Show exactly as many category buttons as the library needs, and light the
        one on screen.  Hidden rather than disabled: a dead button in a row of
        live ones invites the press that does nothing. */
    void refreshCategoryButtons()
    {
        const int n = getNumVisibleCategories();
        const bool banded = n >= 2;

        for (int i = 0; i < kMaxCategories; ++i)
        {
            const bool show = banded && i < n;
            if (show) categoryButtons[i].setButtonText (categories[i]);
            categoryButtons[i].setVisible (show);
            categoryButtons[i].setActive  (i == activeCategory);
        }

        // A CHANGE IN ROW COUNT MOVES THE GRID, so the layout has to be redone.
        // setLibrary is the only caller that can change it, and it arrives long
        // after the last resized() - without this the header would be sized for
        // whatever the category count was when the tab was last laid out.
        const int rows = banded ? (n + kPageCols - 1) / kPageCols : 0;
        if (rows != lastCategoryRows)
        {
            lastCategoryRows = rows;
            if (getWidth() > 0 && getHeight() > 0) resized();
        }
    }

    void selectCategory (int c)
    {
        if (! juce::isPositiveAndBelow (c, categories.size())) return;

        cancelAutoArm();
        activeCategory = c;
        currentPage    = 0;
        // DELIBERATELY does not touch the selection.  Opening a folder is
        // looking, not choosing; the loaded style keeps its light whether or not
        // it happens to live in the folder now on screen.
        rebuildActive();
        refreshCategoryButtons();
        refreshGrid();
    }

    void stepPage (int delta)
    {
        cancelAutoArm();
        const int n = getNumPages();
        // WRAPS.  With two arrows and no page buttons, a pager that stops dead
        // at either end makes the last page of a long folder a several-press
        // journey from the first.
        currentPage = (currentPage + delta % n + n) % n;
        refreshGrid();
        repaint();                       // the readout is painted, not a child
    }

    /** Put the library index `i` on screen: switch to its category, then turn to
        the page holding it.  Does NOT select it — callers that select do so
        immediately afterwards through their own path. */
    void revealLibraryIndex (int i)
    {
        if (! juce::isPositiveAndBelow (i, styleNames.size())) return;

        const int c = categories.indexOf (groupAt (i));
        if (c >= 0) activeCategory = c;

        rebuildActive();
        refreshCategoryButtons();

        const int within = activeIdx.indexOf (i);
        currentPage = within >= 0 ? within / kPageSize : 0;
    }

    void refreshGrid()
    {
        const int offset = currentPage * kPageSize;

        for (int i = 0; i < kPageSize; ++i)
        {
            const int slot = offset + i;
            const bool has = slot < activeIdx.size();
            const int  abs = has ? activeIdx[slot] : -1;

            // HIDDEN, NOT BLANK.  See the note on kPageSize: an empty cell that
            // still looks like a cell is a button that does nothing.
            gridButtons[i].setVisible (has);
            if (! has) continue;

            const juce::String path = (abs < stylePaths.size()) ? stylePaths[abs]
                                                                : juce::String();

            // The ARRAY keeps the real name, because selectedStyleName is
            // matched against it by indexOf and the display name is half of the
            // .bset path (setFileNameForStyle(displayName)).  Prettied at the
            // draw, never in the store.
            gridButtons[i].setButtonText (Betel::prettyName (styleNames[abs]));
            gridButtons[i].setEnabled (true);

            // ONE lit cell in the whole library — the one whose ref is the
            // loaded style's.  Not "cell N of the page I am looking at".
            gridButtons[i].setHighlighted (path.isNotEmpty()
                                             && path == selectedStylePath);

            gridButtons[i].star.setVisible (true);
            gridButtons[i].star.setOn (path.isNotEmpty()
                                         && Betel::StyleFavorites::get().isFavorite (path));
        }

        // The arrows go dead on a single-page folder rather than disappearing:
        // a control that vanishes reads as a fault, one that greys reads as
        // "nothing to step to".
        const bool multi = getNumPages() > 1;
        btnPagePrev.setEnabled (multi);
        btnPageNext.setEnabled (multi);

        repaint();
    }

    void onGridButtonClicked (int slotIndex)
    {
        const int slot = currentPage * kPageSize + slotIndex;
        if (! juce::isPositiveAndBelow (slot, activeIdx.size())) return;

        const int absIdx = activeIdx[slot];
        if (! juce::isPositiveAndBelow (absIdx, styleNames.size())) return;

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
        selectedStyleName  = styleNames[absIdx];
        selectedStylePath  = (absIdx < stylePaths.size()) ? stylePaths[absIdx]
                                                          : juce::String();
        refreshGrid();

        if (onStyleSelected && selectedStylePath.isNotEmpty())
            onStyleSelected (selectedStylePath);
    }

    /** The star is INDEPENDENT of the selection and of the transport: marking a
        favourite is a note about the library, not a load, so it is allowed at
        any time and never changes which style is playing. */
    void onStarClicked (int slotIndex)
    {
        const int slot = currentPage * kPageSize + slotIndex;
        if (! juce::isPositiveAndBelow (slot, activeIdx.size())) return;

        const int absIdx = activeIdx[slot];
        if (! juce::isPositiveAndBelow (absIdx, stylePaths.size())) return;

        const auto path = stylePaths[absIdx];
        if (path.isEmpty()) return;

        const bool now = Betel::StyleFavorites::get().toggle (path);
        gridButtons[slotIndex].star.setOn (now);

        if (onFavoriteToggled) onFavoriteToggled (path, now);
    }

public:
    /** A short message in the pill, in place of the style name.  Used for the
        stop-only refusal — a click that does nothing and says nothing reads as
        a broken button.

        PUBLIC because the HOST raises notices too, not only the tab's own
        refusal: MainComponent::recoverNoStyleAndPlay says "no styles found"
        here when PLAY is pressed and the library has nothing to offer. */
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

private:
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
