
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme

#include <JuceHeader.h>
#include "GrexPopupWindow.h"  // stays in front, minimisable, never vanishes
#include "SearchHistory.h"

//==============================================================================
//  StyleSearchWindow — search the style library from a REAL OS WINDOW.
//
//  ── WHY THIS IS A WINDOW AND NOT A PANEL IN THE TAB ─────────────────────────
//
//  Because a plugin editor does not own the keyboard.  The DAW does.  The search
//  box used to live in the styles tab, and typing into it drove the host's
//  transport instead of entering text - the same report AssignPopup.h records
//  when its own type-a-CC-number box was deleted for exactly this reason.
//
//  A juce::DocumentWindow with setUsingNativeTitleBar(true) is a REAL top-level
//  OS window. Windows gives it keyboard focus directly and the host never gets a
//  say. That is why the Comments window's editor has always accepted typing
//  while everything inside the plugin's own canvas could not.
//
//  THIS MUST NOT BECOME A CallOutBox. A CallOutBox is a child of the plugin
//  editor, so it would look exactly like this and fail exactly like the inline
//  box did - which is the worst possible outcome, because it would appear fixed.
//
//  ── WHAT IT DOES ────────────────────────────────────────────────────────────
//
//     search row : text box | SEARCH | CLEAR
//     left  60%  : RESULTS - one selector per hit, scrolling
//     right 40%  : LAST SEARCHES - saved keywords, each with a trashbin
//
//  Clicking a result selects that style in the styles tab AND turns the grid to
//  its page. The window STAYS OPEN: finding a style you like usually means
//  trying two or three, and a window that closed on the first click would make
//  the player re-open it and re-type to hear the second.
//==============================================================================
namespace Betel
{

class StyleSearchContent : public juce::Component
{
public:
    /** Ask the host to run a search. Results come back via setResults(). */
    std::function<void (const juce::String& keyword)> onSearch;

    /** A result was clicked - select this style ref and page the grid to it. */
    std::function<void (const juce::String& styleRef)> onResultChosen;

    /** Host predicate: refuse the selection while the arranger runs. Same rule
        the grid enforces - a style change re-voices eight slots and recomposes
        every kit, which cannot happen under a running arrangement. */
    std::function<bool()> onIsPlaying;

    StyleSearchContent()
    {
        // ── Search row ───────────────────────────────────────────────────────
        searchBox.setTextToShowWhenEmpty ("Type a style name...", juce::Colour (0xFF777777));
        searchBox.setColour (juce::TextEditor::backgroundColourId,     juce::Colour (0xFF101010));
        searchBox.setColour (juce::TextEditor::textColourId,           juce::Colours::white);
        searchBox.setColour (juce::TextEditor::outlineColourId,        juce::Colour (0xFF555555));
        searchBox.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (Pal::kAccent));
        searchBox.setFont (juce::Font (juce::FontOptions (16.0f)));
        searchBox.setReturnKeyStartsNewLine (false);
        searchBox.onReturnKey = [this] { runSearch(); };
        addAndMakeVisible (searchBox);

        btnSearch.setButtonText ("SEARCH");
        btnSearch.onClick = [this] { runSearch(); };
        addAndMakeVisible (btnSearch);

        btnClear.setButtonText ("CLEAR");
        btnClear.onClick = [this]
        {
            searchBox.clear();
            setResults ({}, {}, {});
            // FOCUS GOES BACK TO THE BOX. Clearing is nearly always the first
            // half of "search for something else", and making the player click
            // the box again to type is a step that exists for no reason.
            searchBox.grabKeyboardFocus();
        };
        addAndMakeVisible (btnClear);

        // ── Headings ─────────────────────────────────────────────────────────
        auto initHeading = [this] (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centredLeft);
            l.setColour (juce::Label::textColourId, juce::Colour (Pal::kAccentSoft));
            l.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            addAndMakeVisible (l);
        };
        initHeading (lblResults, "RESULTS");
        initHeading (lblHistory, "LAST SEARCHES");

        lblStatus.setJustificationType (juce::Justification::centredLeft);
        lblStatus.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        lblStatus.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (lblStatus);

        // ── The two scrolling columns ────────────────────────────────────────
        // Viewports, because either list can outrun the window: "ballad" matches
        // most of a ballad library, and the history holds twenty.
        resultsView.setViewedComponent (&resultsList, false);
        resultsView.setScrollBarsShown (true, false);
        addAndMakeVisible (resultsView);

        historyView.setViewedComponent (&historyList, false);
        historyView.setScrollBarsShown (true, false);
        addAndMakeVisible (historyView);

        resultsList.onRowClicked = [this] (int i) { chooseResult (i); };

        historyList.onRowClicked   = [this] (int i) { replaySearch (i); };
        historyList.onRowDeleted   = [this] (int i) { deleteSearch (i); };

        refreshHistory();
    }

    /** Host -> window: the hits for the last search. */
    void setResults (const juce::StringArray& names,
                     const juce::StringArray& refs,
                     const juce::String& keyword)
    {
        resultNames = names;
        resultRefs  = refs;
        lastKeyword = keyword;

        resultsList.setRows (names);
        resultsView.setViewPosition (0, 0);

        if (keyword.isEmpty())        setStatus ({});
        else if (names.isEmpty())     setStatus ("No style matches \"" + keyword + "\".");
        else                          setStatus (juce::String (names.size())
                                                 + (names.size() == 1 ? " match" : " matches")
                                                 + " for \"" + keyword + "\".");
        resized();
    }

    /** Re-read the saved keywords and rebuild the right-hand column. */
    void refreshHistory()
    {
        historyList.setRows (SearchHistory::get().keywords());
        historyView.setViewPosition (0, 0);
        resized();
    }

    /** The window calls this when it is shown, so the caret is already in the
        box - the entire point of this window is that it can take typing. */
    void focusSearchBox() { searchBox.grabKeyboardFocus(); }

    void setStatus (const juce::String& text)
    {
        lblStatus.setText (text, juce::dontSendNotification);
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF1A1A1A));

        g.setColour (juce::Colour (0xFF141414));
        g.fillRoundedRectangle (resultsPanel.toFloat(), 5.0f);
        g.fillRoundedRectangle (historyPanel.toFloat(), 5.0f);

        // The divider is drawn rather than implied by the gap: two dark panels
        // side by side on a dark window read as one wide panel with a seam.
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine (dividerX, (float) resultsPanel.getY(),
                            (float) resultsPanel.getBottom());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);

        // ── Search row ───────────────────────────────────────────────────────
        auto row = r.removeFromTop (34);
        const int btnW = 84;
        btnClear .setBounds (row.removeFromRight (btnW));
        row.removeFromRight (6);
        btnSearch.setBounds (row.removeFromRight (btnW));
        row.removeFromRight (8);
        searchBox.setBounds (row);

        r.removeFromTop (6);
        lblStatus.setBounds (r.removeFromTop (18));
        r.removeFromTop (6);

        // ── 60 / 40.  Results get the larger share because a style name is
        //    long and a search keyword is short by nature. ────────────────────
        const int gap      = 10;
        const int resultsW = (int) ((r.getWidth() - gap) * 0.60f);

        resultsPanel = r.removeFromLeft (resultsW);
        r.removeFromLeft (gap);
        historyPanel = r;
        dividerX = resultsPanel.getRight() + gap / 2;

        auto lay = [] (juce::Rectangle<int> panel, juce::Label& heading,
                       juce::Viewport& view, RowList& list)
        {
            auto inner = panel.reduced (6);
            heading.setBounds (inner.removeFromTop (16));
            inner.removeFromTop (2);
            view.setBounds (inner);
            list.setSize (view.getMaximumVisibleWidth(), list.preferredHeight());
        };

        lay (resultsPanel, lblResults, resultsView, resultsList);
        lay (historyPanel, lblHistory, historyView, historyList);
    }

private:
    //==========================================================================
    //  TRASHBIN — drawn, not an image.
    //
    //  A path costs nothing, scales with the row and needs no BinaryData entry,
    //  which matters for one 14 px glyph. Lid, body, two ribs.
    //==========================================================================
    class TrashButton : public juce::Button
    {
    public:
        TrashButton() : juce::Button ("delete") { setWantsKeyboardFocus (false); }

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (3.0f);
            const float w = b.getWidth(), h = b.getHeight();
            const float x = b.getX(),     y = b.getY();

            g.setColour (isDown ? juce::Colour (0xFFE53935)
                                : isOver ? juce::Colour (0xFFE53935).withAlpha (0.85f)
                                         : juce::Colours::white.withAlpha (0.40f));

            const float lidY  = y + h * 0.18f;
            const float bodyY = lidY + h * 0.10f;

            g.drawLine (x, lidY, x + w, lidY, 1.4f);                       // lid
            g.drawLine (x + w * 0.36f, y + h * 0.04f,
                        x + w * 0.64f, y + h * 0.04f, 1.4f);               // handle
            g.drawRoundedRectangle (x + w * 0.14f, bodyY,
                                    w * 0.72f, h - (bodyY - y) - h * 0.04f,
                                    1.5f, 1.3f);                           // body
            g.drawLine (x + w * 0.38f, bodyY + h * 0.14f,
                        x + w * 0.38f, y + h * 0.86f, 1.1f);               // ribs
            g.drawLine (x + w * 0.62f, bodyY + h * 0.14f,
                        x + w * 0.62f, y + h * 0.86f, 1.1f);
        }
    };

    //==========================================================================
    //  A vertical list of selectors, optionally each with a trashbin.
    //
    //  Buttons rather than a ListBox: the rows carry two different controls and
    //  a hover state per control, which a ListBoxModel would have to hand-roll
    //  anyway - and the counts here are tens, not thousands, so the one thing a
    //  ListBox buys (recycling rows) buys nothing.
    //==========================================================================
    class RowList : public juce::Component
    {
    public:
        std::function<void (int)> onRowClicked;
        std::function<void (int)> onRowDeleted;   // absent = no trashbins

        static constexpr int kRowH = 26;
        static constexpr int kGap  = 3;

        void setRows (const juce::StringArray& labels)
        {
            rows.clear();

            for (int i = 0; i < labels.size(); ++i)
            {
                auto* r = rows.add (new Row());
                r->button.setButtonText (labels[i]);
                r->button.onClick = [this, i] { if (onRowClicked) onRowClicked (i); };
                addAndMakeVisible (r->button);

                if (onRowDeleted != nullptr)
                {
                    r->trash.onClick = [this, i] { if (onRowDeleted) onRowDeleted (i); };
                    addAndMakeVisible (r->trash);
                }
            }

            setSize (juce::jmax (10, getWidth()), preferredHeight());
            resized();
        }

        int preferredHeight() const
        {
            return juce::jmax (kRowH, rows.size() * (kRowH + kGap));
        }

        void resized() override
        {
            const bool withTrash = (onRowDeleted != nullptr);
            int y = 0;

            for (auto* r : rows)
            {
                auto line = juce::Rectangle<int> (0, y, getWidth(), kRowH);
                if (withTrash)
                {
                    r->trash.setBounds (line.removeFromRight (kRowH));
                    line.removeFromRight (2);
                }
                r->button.setBounds (line);
                y += kRowH + kGap;
            }
        }

    private:
        struct RowButton : public juce::TextButton
        {
            void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
            {
                auto b = getLocalBounds().toFloat();
                g.setColour (isDown ? juce::Colour (Pal::kAccent)
                                    : isOver ? juce::Colour (0xFF333333)
                                             : juce::Colour (0xFF232323));
                g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);
                g.setColour (juce::Colour (0xFF444444));
                g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

                // Black while held, matching every other accent fill in the
                // product - see BalladaPalette.h.
                g.setColour (isDown ? juce::Colours::black
                                    : juce::Colours::white.withAlpha (0.85f));
                g.setFont (juce::Font (juce::FontOptions (13.0f)));
                g.drawFittedText (getButtonText(), getLocalBounds().reduced (8, 2),
                                  juce::Justification::centredLeft, 1);
            }
        };

        struct Row
        {
            RowButton    button;
            TrashButton  trash;
        };

        juce::OwnedArray<Row> rows;
    };

    //==========================================================================
    class FlatButton : public juce::TextButton
    {
    public:
        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (isDown ? juce::Colour (Pal::kAccent)
                                : isOver ? juce::Colour (0xFF444444)
                                         : juce::Colour (0xFF2A2A2A));
            g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);
            g.setColour (juce::Colour (0xFF666666));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (isDown ? juce::Colours::black : juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    //==========================================================================
    void runSearch()
    {
        const auto kw = searchBox.getText().trim();

        // AN EMPTY BOX IS A CLEAR, NOT A SEARCH. Running it would save a blank
        // row to the history and empty the results with nothing on screen
        // explaining which keyword did it.
        if (kw.isEmpty()) { setResults ({}, {}, {}); return; }

        // SAVED BEFORE THE SEARCH RUNS, not after, and NOT conditionally on
        // finding something. A search that found nothing is still a search the
        // player made, and a history that silently drops the misses is a history
        // that lies about what was tried.
        SearchHistory::get().add (kw);
        refreshHistory();

        if (onSearch) onSearch (kw);
    }

    void chooseResult (int index)
    {
        if (index < 0 || index >= resultRefs.size()) return;

        // THE SAME STOP-ONLY RULE THE GRID ENFORCES. Without this the search
        // window would be a way around a restriction that exists because
        // swapping a style mid-bar re-voices eight slots and recomposes every
        // kit - a guaranteed glitch at best.
        if (onIsPlaying && onIsPlaying())
        {
            setStatus ("STOP the arranger to change style.");
            return;
        }

        if (onResultChosen) onResultChosen (resultRefs[index]);

        setStatus ("Loaded: " + resultNames[index]);
    }

    void replaySearch (int index)
    {
        const auto terms = SearchHistory::get().keywords();
        if (index < 0 || index >= terms.size()) return;

        searchBox.setText (terms[index], juce::dontSendNotification);
        runSearch();          // promotes it back to the top of the history too
    }

    void deleteSearch (int index)
    {
        const auto terms = SearchHistory::get().keywords();
        if (index < 0 || index >= terms.size()) return;

        SearchHistory::get().remove (terms[index]);
        refreshHistory();
    }

    juce::TextEditor searchBox;
    FlatButton       btnSearch, btnClear;
    juce::Label      lblResults, lblHistory, lblStatus;

    juce::Viewport   resultsView, historyView;
    RowList          resultsList, historyList;

    juce::StringArray resultNames, resultRefs;
    juce::String      lastKeyword;

    juce::Rectangle<int> resultsPanel, historyPanel;
    int dividerX = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleSearchContent)
};

//==============================================================================
//  The window itself.  A REAL desktop window - see the file header.
//==============================================================================
class StyleSearchWindow : public juce::DocumentWindow
{
public:
    std::function<void()> onClose;          // fired by the title bar's X

    StyleSearchWindow()
        : juce::DocumentWindow ("Style Search", juce::Colour (0xFF1A1A1A),
                                juce::DocumentWindow::closeButton)
    {
        Betel::applyGrexPopupBehaviour (*this);

        // NATIVE TITLE BAR, AND IT IS NOT COSMETIC. This is what makes the
        // window a real OS window that Windows will hand keyboard focus to,
        // which is the entire reason this feature is a window at all. The X the
        // player presses to close is this bar's X.
        setUsingNativeTitleBar (true);

        content = new StyleSearchContent();
        setContentOwned (content, true);

        centreWithSize (660, 460);
        setResizable (true, false);
        setResizeLimits (520, 340, 1400, 1000);
        setVisible (true);

        // The caret starts in the box. Anything else would make the player click
        // once before typing in a window that exists only so they can type.
        content->focusSearchBox();
    }

    StyleSearchContent& getContent() { return *content; }

    void closeButtonPressed() override
    {
        if (onClose) onClose();     // the host resets its unique_ptr, deleting us
    }

private:
    // Owned by the DocumentWindow (setContentOwned), so this is an observer.
    StyleSearchContent* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleSearchWindow)
};

} // namespace Betel
