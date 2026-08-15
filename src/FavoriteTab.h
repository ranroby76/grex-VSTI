#pragma once
//==============================================================================
// FavoriteTab.h — the SETS tab.
//
// Layout (matches StylesTab — same 4 × 9 paged grid):
//
//   ┌───────────────────────────────────────────────────────────────────────┐
//   │  [-] Page 1/3 [+]   [Selected set pill]          [A-Z]  [NEWEST]      │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │  [Set 1 🗑] [Set 2 🗑] [...] [...]                                    │
//   │  ... 4 cols × 9 rows = 36 cells ...                                   │
//   └───────────────────────────────────────────────────────────────────────┘
//
// WHAT CHANGED, AND WHY.
//
// This was the FAVOURITES tab: it kept its own favorites.xml holding a private
// copy of each saved set, alongside the real set files in <root>/sets.  Two
// stores for one thing — a set saved from the Set List never appeared here, and
// a favourite was invisible to everything else.
//
// It now reads <root>/sets directly.  Every .xml in that folder is a button, so
// whatever exists IS what is listed, and SAVE CURRENT SET is gone: saving a set
// is the Set List's job and duplicating it here is what created the split.
//
// SORTING.  Two buttons, and each is its own toggle: pressing the active one
// reverses it.  A-Z becomes Z-A becomes A-Z; NEWEST becomes OLDEST and back.
// Pressing the inactive one switches to that key and keeps whatever direction
// it was last left in, so the two remember themselves independently.
//
// Interactions:
//   * Click cell body          → "Open '<name>' set?" Yes/No → loads the file.
//   * Click 🗑 icon (top-right)→ confirmation → DELETES the .xml.
//   * Double-click cell body   → rename dialog → renames the .xml.
//==============================================================================

#include <JuceHeader.h>
#include <algorithm>
#include <functional>
#include <vector>
#include "GlobalMacros.h"   // Betel::displayNameFor

class FavoriteTab : public juce::Component
{
public:
    static constexpr int kGridCols = 4;
    static constexpr int kGridRows = 9;
    static constexpr int kPageSize = kGridCols * kGridRows;   // 36

    //==========================================================================
    // Callbacks
    //==========================================================================
    /** Host applies the payload onto its live state.  Called when the user
        confirms "Open '<name>' set?" Yes. */
    std::function<void (const juce::ValueTree& payload)> onApplyFavorite;

    //==========================================================================
    /** Re-target the folder the sets are read from (e.g. when the user changes
        the root via the locator LED).  Rescans immediately.
    
        No early-out on an unchanged path: the FOLDER may be the same while its
        contents are not — a set saved from the Set List has to show up here
        without the user having to move the root and back. */
    void setSetsFolder (const juce::File& newFolder)
    {
        setsFolder = newFolder;
        currentPage = 0;
        lastLoadedName.clear();
        rescanSets();
        refreshGrid();
    }

    juce::File getSetsFolder() const { return setsFolder; }

    /** Re-read the folder without changing it — for use after something else
        writes a set (the Set List's SAVE), so this tab agrees with the disk. */
    void refreshFromDisk() { rescanSets(); refreshGrid(); }

    //==========================================================================
    FavoriteTab()
    {
        setOpaque (false);

        btnPagePrev.setButtonText ("-");
        btnPageNext.setButtonText ("+");
        btnPagePrev.onClick = [this] { if (currentPage > 0) { --currentPage; refreshGrid(); } };
        btnPageNext.onClick = [this]
        {
            const int pages = getNumPages();
            if (currentPage < pages - 1) { ++currentPage; refreshGrid(); }
        };
        addAndMakeVisible (btnPagePrev);
        addAndMakeVisible (btnPageNext);
        addAndMakeVisible (lblPage);

        // Sort keys where SAVE CURRENT SET used to be.  Saving a set belongs to
        // the Set List; this tab only reads.
        btnSortName.onClick = [this] { chooseSort (SortKey::Name); };
        btnSortDate.onClick = [this] { chooseSort (SortKey::Date); };
        addAndMakeVisible (btnSortName);
        addAndMakeVisible (btnSortDate);
        refreshSortButtons();

        for (int i = 0; i < kPageSize; ++i)
        {
            gridButtons[i].setClickingTogglesState (false);
            gridButtons[i].onClick        = [this, i] { onCellClicked (i); };
            gridButtons[i].onTrashClick   = [this, i] { onTrashClicked (i); };
            gridButtons[i].onDoubleClick  = [this, i] { onCellDoubleClicked (i); };
            addAndMakeVisible (gridButtons[i]);
        }

        refreshGrid();
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xFF181818));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);

        // Pill behind the selected-favorite label.
        auto pill = pillBounds.toFloat();
        g.setColour (juce::Colour (0xFF2A2A2A));
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (juce::Colour (0xFF555555));
        g.drawRoundedRectangle (pill, 6.0f, 1.0f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::jmin (pill.getHeight() * 0.50f, 13.0f), juce::Font::bold));
        g.drawFittedText (lastLoadedName.isEmpty() ? "No favorite loaded" : lastLoadedName,
                          pillBounds.reduced (8, 2),
                          juce::Justification::centredLeft, 1);

        const int pages = getNumPages();
        lblPage.setText ("Page " + juce::String (currentPage + 1)
                         + " / " + juce::String (juce::jmax (1, pages)),
                         juce::dontSendNotification);
    }

    void resized() override
    {
        const int W = getWidth();
        const int H = getHeight();
        const int padX = 4, padY = 4, gap = 4;

        // Header
        const int hdrH = juce::jmax (28, (int) (H * 0.073f));
        const int hdrY = padY;

        const int navBtnW   = juce::jmax (22, hdrH);
        const int pageLblW  = 90;
        const int saveBtnW  = juce::jmax (140, (int) (W * 0.175f));

        int hx = padX;
        btnPagePrev   .setBounds (hx, hdrY, navBtnW, hdrH);  hx += navBtnW + 2;
        lblPage       .setBounds (hx, hdrY, pageLblW, hdrH); hx += pageLblW + 2;
        btnPageNext   .setBounds (hx, hdrY, navBtnW, hdrH);  hx += navBtnW + 6;

        const int rightEdge = W - padX;
        // Two sort buttons share the width the single SAVE button had.
        const int sortW = juce::jmax (78, (saveBtnW - 6) / 2);
        btnSortDate.setBounds (rightEdge - sortW,               hdrY, sortW, hdrH);
        btnSortName.setBounds (rightEdge - sortW * 2 - 6,       hdrY, sortW, hdrH);

        pillBounds = { hx, hdrY, btnSortName.getX() - hx - 4, hdrH };

        // Grid
        const int gridY = hdrY + hdrH + gap;
        const int gridH = H - gridY - padY;
        const int gridW = W  - 2 * padX;
        const int gridX = padX;

        const int cellGap = 3;
        const int cellW   = (gridW - (kGridCols - 1) * cellGap) / kGridCols;
        const int cellH   = (gridH - (kGridRows - 1) * cellGap) / kGridRows;

        for (int r = 0; r < kGridRows; ++r)
            for (int c = 0; c < kGridCols; ++c)
            {
                const int idx = r * kGridCols + c;
                gridButtons[idx].setBounds (gridX + c * (cellW + cellGap),
                                            gridY + r * (cellH + cellGap),
                                            cellW, cellH);
            }
    }

private:
    //==========================================================================
    // FavoriteButton — text + small trash icon overlay (top-right).  Trash hits
    // intercept the click so the cell's normal action never fires for them.
    //==========================================================================
    class FavoriteButton : public juce::TextButton
    {
    public:
        std::function<void()> onTrashClick;
        std::function<void()> onDoubleClick;

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            const auto b = getLocalBounds().toFloat();

            // Cell background (subtle hover/press shading).
            g.setColour (isDown ? juce::Colour (0xFF3A2A1A)
                         : isOver ? juce::Colour (0xFF333333)
                                  : juce::Colour (0xFF252525));
            g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);

            g.setColour (juce::Colour (0xFF555555));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

            // Cell text — leave space on the right for the trash icon.
            if (isEnabled())
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.setFont (juce::Font (juce::jmin (b.getHeight() * 0.42f, 12.0f),
                                       juce::Font::bold));
                auto textArea = getLocalBounds().reduced (4, 2);
                textArea.removeFromRight (kTrashSize + 4);
                g.drawFittedText (getButtonText(), textArea,
                                  juce::Justification::centred, 2);

                paintTrashIcon (g);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (trashRect().contains (e.getPosition()))
            {
                trashHit = true;
                if (onTrashClick) onTrashClick();
                return;
            }
            trashHit = false;
            juce::TextButton::mouseDown (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (trashHit) { trashHit = false; return; }
            juce::TextButton::mouseUp (e);
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (trashRect().contains (e.getPosition())) return;
            if (onDoubleClick) onDoubleClick();
        }

    private:
        static constexpr int kTrashSize = 14;
        static constexpr int kTrashPad  = 4;
        bool trashHit = false;

        juce::Rectangle<int> trashRect() const
        {
            return { getWidth() - kTrashSize - kTrashPad,
                     kTrashPad,
                     kTrashSize, kTrashSize };
        }

        void paintTrashIcon (juce::Graphics& g) const
        {
            const auto r = trashRect().toFloat();
            const float x = r.getX();
            const float y = r.getY();
            const float w = r.getWidth();
            const float h = r.getHeight();

            // Soft backdrop circle so the icon stays legible on any cell colour.
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (r.expanded (1.0f));

            const auto stroke = juce::Colours::white.withAlpha (0.85f);
            g.setColour (stroke);

            // Lid handle (the small bump on top)
            g.drawLine (x + w * 0.40f, y + h * 0.18f,
                        x + w * 0.60f, y + h * 0.18f, 1.4f);
            // Top horizontal line of the lid
            g.drawLine (x + w * 0.18f, y + h * 0.30f,
                        x + w * 0.82f, y + h * 0.30f, 1.4f);
            // Bin body (rounded rect open at top)
            juce::Path body;
            body.startNewSubPath (x + w * 0.25f, y + h * 0.32f);
            body.lineTo          (x + w * 0.30f, y + h * 0.85f);
            body.lineTo          (x + w * 0.70f, y + h * 0.85f);
            body.lineTo          (x + w * 0.75f, y + h * 0.32f);
            g.strokePath (body, juce::PathStrokeType (1.2f));
            // Two vertical slots inside
            g.drawLine (x + w * 0.42f, y + h * 0.42f,
                        x + w * 0.42f, y + h * 0.75f, 1.0f);
            g.drawLine (x + w * 0.58f, y + h * 0.42f,
                        x + w * 0.58f, y + h * 0.75f, 1.0f);
        }
    };

    //==========================================================================
    // The set list, read from <root>/sets.
    //==========================================================================
    struct Entry
    {
        juce::File      file;          // the .xml the set came out of
        juce::String    name;          // the SET's name, as the set list stores it
        juce::String    listName;      // which list it belongs to (for the tooltip)
        int             indexInList = -1;  // which <Entry> — names can repeat
        juce::int64     modified = 0;  // the file's mtime — the date sort key
        juce::ValueTree payload;       // SoundsState / GlobalState
    };

    int getCount() const { return (int) entries.size(); }

    int getNumPages() const
    {
        return juce::jmax (1, (getCount() + kPageSize - 1) / kPageSize);
    }

    /** One button per SET, gathered from every set list in the folder.
    
        A file in <root>/sets is a <BetelgeuseSetList> holding N <Entry> children,
        each one a set with its own SoundsState / GlobalState — it is a LIST, not
        a single set.  So the grid enumerates the entries inside those files
        rather than the files themselves: a list with one set yields one button,
        a gig list with twelve yields twelve, and every button recalls exactly the
        set it names.
    
        Parsing happens here rather than on click because enumerating requires it
        anyway — the names live inside the XML.  Once per rescan, not per paint. */
    void rescanSets()
    {
        entries.clear();
        if (! setsFolder.isDirectory()) { applySort(); return; }

        // BOTH extensions.  A set is written as *.bset now — root <BetelSet>,
        // with SoundsState / GlobalState as direct children — and *.xml is the
        // older set-list form.  The shape test below already accepts a BetelSet
        // (it has a SoundsState child); this filter was the only thing hiding
        // them, so the folder had sets in it and the tab listed none.
        for (const auto& f : setsFolder.findChildFiles (juce::File::findFiles,
                                                       false, "*.bset;*.xml"))
        {
            const auto xml = juce::XmlDocument::parse (f);
            if (xml == nullptr) continue;

            const auto tree = juce::ValueTree::fromXml (*xml);
            if (! tree.isValid()) continue;

            const auto listName  = tree.getProperty ("name", Betel::displayNameFor (f))
                                       .toString();
            const auto modifiedMs = f.getLastModificationTime().toMilliseconds();

            //------------------------------------------------------------------
            // TWO SHAPES ARE ACCEPTED, because two exist on disk.
            //
            // A file written by the Set List is a <BetelgeuseSetList> holding
            // <Entry> children — one button per entry.  But a set can also have
            // been written as a BARE PAYLOAD, a single tree carrying SoundsState
            // / GlobalState with no list wrapper, and requiring the wrapper meant
            // every one of those was skipped in silence: the folder had sets in
            // it and the tab showed nothing.
            //
            // So: entries if there are entries, otherwise the file itself as one
            // set named after the file.
            //------------------------------------------------------------------
            const bool looksLikeBarePayload =
                   tree.getChildWithName ("SoundsState").isValid()
                || tree.getChildWithName ("GlobalState").isValid();

            if (looksLikeBarePayload)
            {
                Entry e;
                e.file        = f;
                e.listName    = Betel::displayNameFor (f);
                e.indexInList = -1;              // not inside a list — see rewriteList
                e.name        = Betel::displayNameFor (f);
                e.modified    = modifiedMs;
                e.payload     = tree.createCopy();
                entries.push_back (std::move (e));
                continue;
            }

            for (int i = 0; i < tree.getNumChildren(); ++i)
            {
                const auto child = tree.getChild (i);
                if (! child.hasType ("Entry")) continue;

                Entry e;
                e.file        = f;
                e.listName    = listName;
                e.indexInList = i;
                e.name     = child.getProperty ("name", "Untitled").toString();
                e.modified = modifiedMs;

                // The payload is the Entry's only non-Entry child.
                for (int c = 0; c < child.getNumChildren(); ++c)
                {
                    const auto pc = child.getChild (c);
                    if (pc.getChildWithName ("SoundsState").isValid()
                        || pc.getChildWithName ("GlobalState").isValid()
                        || pc.hasType ("Payload"))
                    { e.payload = pc.createCopy(); break; }
                }
                if (! e.payload.isValid() && child.getNumChildren() > 0)
                    e.payload = child.getChild (0).createCopy();

                entries.push_back (std::move (e));
            }
        }

        applySort();
    }

    //==========================================================================
    // SORTING — two keys, each remembering its own direction.
    //==========================================================================
    enum class SortKey { Name, Date };

    void applySort()
    {
        std::stable_sort (entries.begin(), entries.end(),
            [this] (const Entry& a, const Entry& b)
            {
                if (sortKey == SortKey::Name)
                {
                    const int c = a.name.compareIgnoreCase (b.name);
                    return nameDescending ? (c > 0) : (c < 0);
                }
                return dateDescending ? (a.modified > b.modified)
                                      : (a.modified < b.modified);
            });
    }

    /** Pressing the ACTIVE key reverses it; pressing the other switches key and
        leaves that key's direction as the user last had it. */
    void chooseSort (SortKey k)
    {
        if (sortKey == k)
        {
            if (k == SortKey::Name) nameDescending = ! nameDescending;
            else                    dateDescending = ! dateDescending;
        }
        else
        {
            sortKey = k;
        }

        currentPage = 0;
        applySort();
        refreshSortButtons();
        refreshGrid();
    }

    void refreshSortButtons()
    {
        btnSortName.setButtonText (nameDescending ? "Z-A" : "A-Z");
        btnSortDate.setButtonText (dateDescending ? "NEWEST" : "OLDEST");

        const bool nameOn = (sortKey == SortKey::Name);
        for (auto* pair : { &btnSortName, &btnSortDate })
        {
            const bool on = (pair == &btnSortName) ? nameOn : ! nameOn;
            pair->setColour (juce::TextButton::buttonColourId,
                             on ? juce::Colour (0xFFCC6600) : juce::Colour (0xFF2A2A2A));
            pair->setColour (juce::TextButton::textColourOffId,
                             on ? juce::Colours::black : juce::Colours::white);
            pair->setColour (juce::TextButton::textColourOnId,
                             on ? juce::Colours::black : juce::Colours::white);
        }
    }

    //==========================================================================
    // Grid wiring
    //==========================================================================
    void refreshGrid()
    {
        const int total  = getCount();
        const int offset = currentPage * kPageSize;
        const int pages  = getNumPages();

        if (currentPage >= pages) currentPage = juce::jmax (0, pages - 1);

        for (int i = 0; i < kPageSize; ++i)
        {
            const int idx = offset + i;
            const bool has = idx < total;
            gridButtons[i].setButtonText (has ? entries[(size_t) idx].name : juce::String());
            gridButtons[i].setEnabled    (has);
        }
        repaint();
    }

    int absoluteIndex (int slot) const
    {
        return currentPage * kPageSize + slot;
    }

    //==========================================================================
    // Cell actions
    //==========================================================================
    void onCellClicked (int slot)
    {
        const int idx = absoluteIndex (slot);
        if (idx < 0 || idx >= getCount()) return;
        const Entry& e = entries[(size_t) idx];

        const juce::String name = e.name;

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions::makeOptionsYesNo (
                juce::MessageBoxIconType::QuestionIcon,
                "Open set",
                "Open '" + name + "' set?"),
            [this, name, payload = e.payload.createCopy()] (int result)
            {
                if (result != 1) return;
                if (onApplyFavorite) onApplyFavorite (payload);
                lastLoadedName = name;
                repaint();
            });
    }

    /** Load a set list, hand it to `mutate`, write it back if that returns true.
    
        Delete and rename are edits to ONE <Entry> inside a list now, not to the
        file: the grid lists sets, and a set's file is a gig list that may hold a
        dozen others.  Deleting the file to remove one set would take the rest
        with it. */
    bool rewriteList (const juce::File& f,
                      const std::function<bool (juce::ValueTree&)>& mutate)
    {
        const auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr) return false;

        auto root = juce::ValueTree::fromXml (*xml);
        if (! root.isValid() || ! root.hasType ("BetelgeuseSetList")) return false;
        if (! mutate (root)) return false;

        if (auto out = root.createXml()) return out->writeTo (f, {});
        return false;
    }

    void onTrashClicked (int slot)
    {
        const int idx = absoluteIndex (slot);
        if (idx < 0 || idx >= getCount()) return;
        const auto&        en   = entries[(size_t) idx];
        const juce::String name = en.name;
        const juce::File   file = en.file;
        const juce::String list = en.listName;
        const int          pos  = en.indexInList;

        // The real set is being removed from the real list — there is no private
        // copy to fall back on, so say so.
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions::makeOptionsYesNo (
                juce::MessageBoxIconType::WarningIcon,
                "Delete set",
                (pos < 0 ? "Delete the set file '" + name + "'?"
                         : "Remove '" + name + "' from the set list '" + list + "'?")
                + juce::String ("\n\nThis cannot be undone.")),
            [this, name, file, pos] (int result)
            {
                if (result != 1) return;

                // pos < 0 means the file IS the set (a bare payload), so removing
                // the set means removing the file.
                if (pos < 0)
                    file.deleteFile();
                else
                    rewriteList (file, [pos] (juce::ValueTree& root) -> bool
                    {
                        if (pos < 0 || pos >= root.getNumChildren()) return false;
                        root.removeChild (pos, nullptr);
                        return true;
                    });

                if (lastLoadedName == name) lastLoadedName.clear();
                rescanSets();
                refreshGrid();
            });
    }

    void onCellDoubleClicked (int slot)
    {
        const int idx = absoluteIndex (slot);
        if (idx < 0 || idx >= getCount()) return;

        const juce::String currentName = entries[(size_t) idx].name;
        const juce::File   file        = entries[(size_t) idx].file;
        const int          pos         = entries[(size_t) idx].indexInList;

        auto* aw = new juce::AlertWindow ("Rename set",
                                          "New name for '" + currentName + "':",
                                          juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", currentName, {}, false);
        aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true,
            juce::ModalCallbackFunction::create ([this, aw, file, pos] (int result)
            {
                if (result == 1)
                {
                    const auto newName = aw->getTextEditorContents ("name").trim();
                    if (newName.isNotEmpty())
                    {
                        // A bare payload's name IS its filename, so renaming it
                        // renames the file.
                        if (pos < 0)
                        {
                            // Keep the file's own extension — renaming a .bset
                            // must not turn it into a .xml.
                            const auto target = file.getSiblingFile (
                                                    newName + file.getFileExtension());
                            if (target == file || ! target.existsAsFile())
                                file.moveFileTo (target);
                        }
                        else
                        {
                            rewriteList (file, [pos, newName] (juce::ValueTree& root) -> bool
                            {
                                if (pos < 0 || pos >= root.getNumChildren()) return false;
                                root.getChild (pos).setProperty ("name", newName, nullptr);
                                return true;
                            });
                        }

                        rescanSets();
                        refreshGrid();
                    }
                }
                delete aw;
            }), false);
    }

    //==========================================================================
    // Members
    //==========================================================================
    juce::File          setsFolder;
    std::vector<Entry>  entries;

    SortKey sortKey        = SortKey::Name;
    bool    nameDescending = false;   // A-Z
    bool    dateDescending = true;    // NEWEST first

    int currentPage = 0;
    juce::String lastLoadedName;

    juce::TextButton btnPagePrev, btnPageNext, btnSortName, btnSortDate;
    juce::Label      lblPage;
    FavoriteButton   gridButtons [kPageSize];
    juce::Rectangle<int> pillBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FavoriteTab)
};
