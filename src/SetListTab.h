#pragma once
//==============================================================================
// SetListTab.h
//
// Live-performance ordered sequence of saved sets.  Distinct from FavoriteTab:
//
//   FavoriteTab — unordered library, random access via a grid.
//   SetListTab  — multiple named ordered lists living in <root>/sets/,
//                 played sequentially via PREV / NEXT during a show.
//
// Layout (canvas roughly 1600 × 435):
//
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │  [Set list ▾]  [NEW]  [DELETE]                      <set list name>  │   header
//   ├──────────────────────────────────────────────────────────────────────┤
//   │  [Entry 1🗑] [Entry 2🗑] [Entry 3🗑] [Entry 4🗑]                       │
//   │  ... 4 cols × 8 rows = 32 cells per page ...                          │   grid
//   ├──────────────────────────────────────────────────────────────────────┤
//   │   [- Page 1/3 +]      [PREV]   3 / 12   [NEXT]      [ADD CURRENT]    │   footer
//   └──────────────────────────────────────────────────────────────────────┘
//
// Interactions:
//   * Cell click           → "Open '<name>' set?" Yes/No → applies on yes.
//   * Trash icon click     → "Remove this entry?"   Yes/No → deletes + saves.
//   * Cell double-click    → modal rename dialog.
//   * PREV / NEXT          → load adjacent entry (clamps at ends; no wrap).
//   * ADD CURRENT          → name prompt → captures host state → appends.
//   * NEW                  → name prompt → creates empty set list file.
//   * DELETE               → confirmation → deletes the .xml file.
//   * Dropdown             → switch between *.xml files in <root>/sets/.
//
// Persistence: every change auto-saves to the current set list's .xml file:
//
//   <BetelgeuseSetList version="1" name="...">
//     <Entry id="..." name="...">
//       <Payload>
//         <SoundsState .../>
//         <GlobalState .../>
//       </Payload>
//     </Entry>
//     ...
//   </BetelgeuseSetList>
//
// The `Payload` tree is the same `FavoritePayload` shape MainComponent builds
// for the FavoriteTab, so the host's capture/apply wiring is identical.
//==============================================================================

#include <JuceHeader.h>
#include "GlobalMacros.h"   // Betel::displayNameFor

class SetListTab : public juce::Component
{
public:
    static constexpr int kGridCols = 4;
    static constexpr int kGridRows = 8;
    static constexpr int kPageSize = kGridCols * kGridRows;   // 32

    //==========================================================================
    // Host callbacks (wired by MainComponent)
    //==========================================================================
    std::function<juce::ValueTree()>                     onCaptureCurrentSet;
    std::function<void (const juce::ValueTree& payload)> onApplyEntry;

    //==========================================================================
    SetListTab()
    {
        setOpaque (false);

        // ── Header row ───────────────────────────────────────────────────────
        addAndMakeVisible (setListSelector);
        setListSelector.onChange = [this] { onSelectorChanged(); };

        btnNew.setButtonText ("NEW");
        btnNew.onClick = [this] { promptNewSetList(); };
        addAndMakeVisible (btnNew);

        btnDelete.setButtonText ("DELETE");
        btnDelete.onClick = [this] { promptDeleteSetList(); };
        addAndMakeVisible (btnDelete);

        addAndMakeVisible (lblHeaderName);
        lblHeaderName.setFont (juce::Font (14.0f, juce::Font::bold));
        lblHeaderName.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        lblHeaderName.setJustificationType (juce::Justification::centredRight);

        // ── Grid ─────────────────────────────────────────────────────────────
        for (int i = 0; i < kPageSize; ++i)
        {
            gridButtons[i].setClickingTogglesState (false);
            gridButtons[i].onClick        = [this, i] { onCellClicked (i); };
            gridButtons[i].onTrashClick   = [this, i] { onTrashClicked (i); };
            gridButtons[i].onDoubleClick  = [this, i] { onCellDoubleClicked (i); };
            addAndMakeVisible (gridButtons[i]);
        }

        // ── Footer ───────────────────────────────────────────────────────────
        btnPagePrev.setButtonText ("-");
        btnPageNext.setButtonText ("+");
        btnPagePrev.onClick = [this] { if (currentPage > 0) { --currentPage; refreshGrid(); } };
        btnPageNext.onClick = [this]
        {
            if (currentPage < getNumPages() - 1) { ++currentPage; refreshGrid(); }
        };
        addAndMakeVisible (btnPagePrev);
        addAndMakeVisible (btnPageNext);
        addAndMakeVisible (lblPage);

        btnPrev.setButtonText ("< PREV");
        btnPrev.onClick = [this] { navigateBy (-1); };
        addAndMakeVisible (btnPrev);

        addAndMakeVisible (lblPosition);
        lblPosition.setJustificationType (juce::Justification::centred);
        lblPosition.setFont (juce::Font (13.0f, juce::Font::bold));
        lblPosition.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));

        btnNext.setButtonText ("NEXT >");
        btnNext.onClick = [this] { navigateBy (+1); };
        addAndMakeVisible (btnNext);

        btnAddCurrent.setButtonText ("ADD CURRENT");
        btnAddCurrent.onClick = [this] { promptAddCurrent(); };
        addAndMakeVisible (btnAddCurrent);

        refreshGrid();   // initial empty state
    }

    /** Re-target the folder where set list .xml files live.  Rescans the
        folder, picks the first found list (if any), and reloads the grid. */
    void setSetsFolder (const juce::File& folder)
    {
        setsFolder = folder;
        rescanFolder();
    }

    juce::File getSetsFolder() const { return setsFolder; }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xFF181818));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);

        // Subtle separators between header / grid / footer.
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine (headerSeparatorY, 6.0f, (float) (getWidth() - 6));
        g.drawHorizontalLine (footerSeparatorY, 6.0f, (float) (getWidth() - 6));

        lblPage.setText ("Page " + juce::String (currentPage + 1)
                         + " / " + juce::String (juce::jmax (1, getNumPages())),
                         juce::dontSendNotification);
    }

    void resized() override
    {
        const int W = getWidth();
        const int H = getHeight();
        const int padX = 6, padY = 4, gap = 4;

        // ── Header ────────────────────────────────────────────────────────────
        const int hdrH    = juce::jmax (28, (int) (H * 0.07f));
        const int comboW  = juce::jmax (180, (int) (W * 0.22f));
        const int btnW    = juce::jmax (66,  (int) (W * 0.06f));

        int hx = padX;
        setListSelector.setBounds (hx, padY, comboW, hdrH);   hx += comboW + gap;
        btnNew         .setBounds (hx, padY, btnW,   hdrH);   hx += btnW   + gap;
        btnDelete      .setBounds (hx, padY, btnW,   hdrH);   hx += btnW   + gap;

        const int rightEdge = W - padX;
        lblHeaderName.setBounds (hx, padY, rightEdge - hx, hdrH);

        headerSeparatorY = padY + hdrH + 2;

        // ── Footer ────────────────────────────────────────────────────────────
        const int footerH = juce::jmax (32, (int) (H * 0.085f));
        const int footerY = H - padY - footerH;

        const int navBtnW    = juce::jmax (24, footerH);
        const int pageLblW   = 90;
        const int prevNextW  = juce::jmax (84, (int) (W * 0.08f));
        const int positionW  = 90;
        const int addBtnW    = juce::jmax (130, (int) (W * 0.12f));

        int fx = padX;
        btnPagePrev .setBounds (fx, footerY, navBtnW,   footerH); fx += navBtnW   + 2;
        lblPage     .setBounds (fx, footerY, pageLblW,  footerH); fx += pageLblW  + 2;
        btnPageNext .setBounds (fx, footerY, navBtnW,   footerH);

        // Centre the prev/position/next cluster.
        const int clusterW = prevNextW + positionW + prevNextW + 2 * gap;
        const int clusterX = (W - clusterW) / 2;
        int cx = clusterX;
        btnPrev    .setBounds (cx, footerY, prevNextW, footerH); cx += prevNextW + gap;
        lblPosition.setBounds (cx, footerY, positionW, footerH); cx += positionW + gap;
        btnNext    .setBounds (cx, footerY, prevNextW, footerH);

        btnAddCurrent.setBounds (rightEdge - addBtnW, footerY, addBtnW, footerH);

        footerSeparatorY = footerY - 2;

        // ── Grid (fills space between header and footer) ─────────────────────
        const int gridY = headerSeparatorY + gap;
        const int gridH = footerSeparatorY - gridY - gap;
        const int gridW = W - 2 * padX;
        const int gridX = padX;

        const int cellGap = 3;
        const int cellW = (gridW - (kGridCols - 1) * cellGap) / kGridCols;
        const int cellH = (gridH - (kGridRows - 1) * cellGap) / kGridRows;

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
    // EntryButton — text + trash icon overlay (top-right) + currently-playing
    // highlight (green border) for the entry the host last loaded.
    //==========================================================================
    class EntryButton : public juce::TextButton
    {
    public:
        std::function<void()> onTrashClick;
        std::function<void()> onDoubleClick;

        void setIsCurrent (bool b) { if (b != isCurrent) { isCurrent = b; repaint(); } }

        void paintButton (juce::Graphics& g, bool isOver, bool isDown) override
        {
            const auto b = getLocalBounds().toFloat();
            g.setColour (isDown ? juce::Colour (0xFF3A2A1A)
                         : isOver ? juce::Colour (0xFF333333)
                                  : juce::Colour (0xFF252525));
            g.fillRoundedRectangle (b.reduced (0.5f), 4.0f);

            g.setColour (isCurrent ? juce::Colour (0xFF00CC44)
                                   : juce::Colour (0xFF555555));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, isCurrent ? 2.0f : 1.0f);

            if (isEnabled())
            {
                g.setColour (juce::Colours::white.withAlpha (0.88f));
                g.setFont (juce::Font (juce::jmin (b.getHeight() * 0.36f, 13.0f),
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
            if (trashRect().contains (e.getPosition())) { trashHit = true; if (onTrashClick) onTrashClick(); return; }
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
        bool isCurrent = false;

        juce::Rectangle<int> trashRect() const
        {
            return { getWidth() - kTrashSize - kTrashPad, kTrashPad, kTrashSize, kTrashSize };
        }

        void paintTrashIcon (juce::Graphics& g) const
        {
            const auto r = trashRect().toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (r.expanded (1.0f));
            const auto stroke = juce::Colours::white.withAlpha (0.85f);
            g.setColour (stroke);
            const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
            g.drawLine (x + w * 0.40f, y + h * 0.18f, x + w * 0.60f, y + h * 0.18f, 1.4f);
            g.drawLine (x + w * 0.18f, y + h * 0.30f, x + w * 0.82f, y + h * 0.30f, 1.4f);
            juce::Path body;
            body.startNewSubPath (x + w * 0.25f, y + h * 0.32f);
            body.lineTo          (x + w * 0.30f, y + h * 0.85f);
            body.lineTo          (x + w * 0.70f, y + h * 0.85f);
            body.lineTo          (x + w * 0.75f, y + h * 0.32f);
            g.strokePath (body, juce::PathStrokeType (1.2f));
            g.drawLine (x + w * 0.42f, y + h * 0.42f, x + w * 0.42f, y + h * 0.75f, 1.0f);
            g.drawLine (x + w * 0.58f, y + h * 0.42f, x + w * 0.58f, y + h * 0.75f, 1.0f);
        }
    };

    //==========================================================================
    // In-memory model
    //==========================================================================
    struct Entry
    {
        juce::String   id;
        juce::String   name;
        juce::ValueTree payload;
    };

    juce::File             setsFolder;
    juce::File             currentFile;     // the .xml currently loaded (empty = none)
    juce::String           currentName;     // user-visible name of the loaded list
    std::vector<Entry>     entries;
    int                    currentEntryIdx = -1;
    int                    currentPage = 0;

    int  getCount() const { return (int) entries.size(); }
    int  getNumPages() const { return juce::jmax (1, (getCount() + kPageSize - 1) / kPageSize); }
    int  absoluteIndex (int slot) const { return currentPage * kPageSize + slot; }

    //==========================================================================
    // Folder & file I/O
    //==========================================================================
    void rescanFolder()
    {
        setListSelector.clear (juce::dontSendNotification);

        if (! setsFolder.isDirectory())
        {
            entries.clear();
            currentFile = juce::File();
            currentName.clear();
            currentEntryIdx = -1;
            refreshHeader();
            refreshGrid();
            return;
        }

        const auto files = setsFolder.findChildFiles (
            juce::File::findFiles | juce::File::ignoreHiddenFiles,
            false, "*.xml");

        int id = 1;
        for (const auto& f : files)
            setListSelector.addItem (Betel::displayNameFor (f), id++);

        if (setListSelector.getNumItems() > 0)
        {
            setListSelector.setSelectedId (1, juce::dontSendNotification);
            loadFromFile (files[0]);
        }
        else
        {
            entries.clear();
            currentFile = juce::File();
            currentName.clear();
            currentEntryIdx = -1;
            refreshHeader();
            refreshGrid();
        }
    }

    void loadFromFile (const juce::File& f)
    {
        entries.clear();
        currentFile = f;
        currentEntryIdx = -1;
        currentPage = 0;

        if (! f.existsAsFile()) { currentName.clear(); refreshHeader(); refreshGrid(); return; }

        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr) { currentName.clear(); refreshHeader(); refreshGrid(); return; }

        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid() || ! tree.hasType ("BetelgeuseSetList"))
        { currentName.clear(); refreshHeader(); refreshGrid(); return; }

        currentName = tree.getProperty ("name", Betel::displayNameFor (f)).toString();

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto e = tree.getChild (i);
            if (! e.hasType ("Entry")) continue;
            Entry x;
            x.id      = e.getProperty ("id",   juce::Uuid().toString()).toString();
            x.name    = e.getProperty ("name", "Untitled").toString();
            x.payload = e.getChildWithName ("Payload").createCopy();
            entries.push_back (std::move (x));
        }

        refreshHeader();
        refreshGrid();
    }

    void saveCurrent()
    {
        if (currentFile == juce::File()) return;

        juce::ValueTree root ("BetelgeuseSetList");
        root.setProperty ("version", 1,           nullptr);
        root.setProperty ("name",    currentName, nullptr);
        for (const auto& e : entries)
        {
            juce::ValueTree x ("Entry");
            x.setProperty ("id",   e.id,   nullptr);
            x.setProperty ("name", e.name, nullptr);
            x.appendChild (e.payload.createCopy(), nullptr);
            root.appendChild (x, nullptr);
        }
        if (auto x = root.createXml())
            x->writeTo (currentFile, {});
    }

    void onSelectorChanged()
    {
        const auto id = setListSelector.getSelectedId();
        if (id <= 0) return;
        const auto picked = setListSelector.getItemText (id - 1);
        const auto f = setsFolder.getChildFile (picked + ".xml");
        if (f.existsAsFile()) loadFromFile (f);
    }

    //==========================================================================
    // Header / footer / grid refresh
    //==========================================================================
    void refreshHeader()
    {
        lblHeaderName.setText (currentName.isEmpty() ? "No set list loaded" : currentName,
                                juce::dontSendNotification);

        // Sync dropdown selection (if a matching item exists)
        if (setListSelector.getNumItems() > 0 && currentName.isNotEmpty())
        {
            for (int i = 0; i < setListSelector.getNumItems(); ++i)
                if (setListSelector.getItemText (i) == Betel::displayNameFor (currentFile))
                {
                    setListSelector.setSelectedItemIndex (i, juce::dontSendNotification);
                    break;
                }
        }
    }

    void refreshGrid()
    {
        const int total = getCount();
        const int offset = currentPage * kPageSize;
        const int pages = getNumPages();
        if (currentPage >= pages) currentPage = juce::jmax (0, pages - 1);

        for (int i = 0; i < kPageSize; ++i)
        {
            const int idx = offset + i;
            const bool has = idx < total;
            gridButtons[i].setButtonText (has ? entries[(size_t) idx].name : juce::String());
            gridButtons[i].setEnabled    (has);
            gridButtons[i].setIsCurrent  (has && idx == currentEntryIdx);
        }

        if (total > 0 && currentEntryIdx >= 0)
            lblPosition.setText (juce::String (currentEntryIdx + 1) + " / " + juce::String (total),
                                  juce::dontSendNotification);
        else if (total > 0)
            lblPosition.setText ("- / " + juce::String (total), juce::dontSendNotification);
        else
            lblPosition.setText ("- / -", juce::dontSendNotification);

        repaint();
    }

    //==========================================================================
    // Cell + navigation actions
    //==========================================================================
    void applyEntryAt (int idx, bool silent = false)
    {
        if (idx < 0 || idx >= getCount()) return;
        currentEntryIdx = idx;
        // Jump grid page to wherever the entry lives so it's visible.
        currentPage = idx / kPageSize;
        if (! silent && onApplyEntry)
            onApplyEntry (entries[(size_t) idx].payload);
        refreshGrid();
    }

    void onCellClicked (int slot)
    {
        const int idx = absoluteIndex (slot);
        if (idx < 0 || idx >= getCount()) return;
        const juce::String name = entries[(size_t) idx].name;

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions::makeOptionsYesNo (
                juce::MessageBoxIconType::QuestionIcon,
                "Open set",
                "Open '" + name + "' set?"),
            [this, idx] (int result) { if (result == 1) applyEntryAt (idx); });
    }

    void onTrashClicked (int slot)
    {
        const int idx = absoluteIndex (slot);
        if (idx < 0 || idx >= getCount()) return;
        const juce::String name = entries[(size_t) idx].name;

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions::makeOptionsYesNo (
                juce::MessageBoxIconType::WarningIcon,
                "Remove entry",
                "Remove this entry?\n\n'" + name + "'"),
            [this, idx] (int result)
            {
                if (result != 1) return;
                if (idx < 0 || idx >= getCount()) return;
                entries.erase (entries.begin() + idx);
                if (currentEntryIdx == idx)        currentEntryIdx = -1;
                else if (currentEntryIdx > idx)    --currentEntryIdx;
                saveCurrent();
                refreshGrid();
            });
    }

    void onCellDoubleClicked (int slot)
    {
        const int idx = absoluteIndex (slot);
        if (idx < 0 || idx >= getCount()) return;
        const juce::String oldName = entries[(size_t) idx].name;

        auto* aw = new juce::AlertWindow ("Rename entry",
                                           "New name for '" + oldName + "':",
                                           juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", oldName, {}, false);
        aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true,
            juce::ModalCallbackFunction::create ([this, aw, idx] (int r)
            {
                if (r == 1)
                {
                    const auto n = aw->getTextEditorContents ("name").trim();
                    if (n.isNotEmpty() && idx >= 0 && idx < getCount())
                    {
                        entries[(size_t) idx].name = n;
                        saveCurrent();
                        refreshGrid();
                    }
                }
                delete aw;
            }), false);
    }

    void navigateBy (int delta)
    {
        if (getCount() == 0) return;
        if (currentEntryIdx < 0) { applyEntryAt (0); return; }   // first PREV/NEXT loads entry 0
        const int next = juce::jlimit (0, getCount() - 1, currentEntryIdx + delta);
        if (next != currentEntryIdx) applyEntryAt (next);
    }

    //==========================================================================
    // Set list management (NEW / DELETE / ADD CURRENT)
    //==========================================================================
    void promptNewSetList()
    {
        if (! setsFolder.isDirectory())
        {
            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions::makeOptionsOk (
                    juce::MessageBoxIconType::WarningIcon,
                    "No sets folder",
                    "Locate the Betelgeuse folder first using the LED in the header."),
                nullptr);
            return;
        }

        auto* aw = new juce::AlertWindow ("New set list",
                                           "Name for the new set list:",
                                           juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", "My Set List", {}, false);
        aw->addButton ("CREATE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true,
            juce::ModalCallbackFunction::create ([this, aw] (int r)
            {
                if (r == 1)
                {
                    const auto n = aw->getTextEditorContents ("name").trim();
                    if (n.isNotEmpty()) createNewSetList (n);
                }
                delete aw;
            }), false);
    }

    void createNewSetList (juce::String name)
    {
        // Make the filename safe and unique.
        juce::String safe = name.removeCharacters ("\\/:*?\"<>|").trim();
        if (safe.isEmpty()) safe = "Untitled";
        juce::File f = setsFolder.getChildFile (safe + ".xml");
        int n = 2;
        while (f.existsAsFile())
            f = setsFolder.getChildFile (safe + " " + juce::String (n++) + ".xml");

        entries.clear();
        currentEntryIdx = -1;
        currentPage = 0;
        currentFile = f;
        currentName = name;
        saveCurrent();
        rescanFolder();

        // Select the newly-created list in the dropdown.
        for (int i = 0; i < setListSelector.getNumItems(); ++i)
            if (setListSelector.getItemText (i) == Betel::displayNameFor (f))
            {
                setListSelector.setSelectedItemIndex (i, juce::dontSendNotification);
                loadFromFile (f);
                break;
            }
    }

    void promptDeleteSetList()
    {
        if (currentFile == juce::File()) return;
        const auto name = currentName.isEmpty() ? Betel::displayNameFor (currentFile)
                                                 : currentName;
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions::makeOptionsYesNo (
                juce::MessageBoxIconType::WarningIcon,
                "Delete set list",
                "Delete the set list '" + name + "'?\nThis cannot be undone."),
            [this] (int r)
            {
                if (r != 1) return;
                if (currentFile.existsAsFile()) currentFile.deleteFile();
                rescanFolder();
            });
    }

    void promptAddCurrent()
    {
        if (! onCaptureCurrentSet) return;
        if (currentFile == juce::File())
        {
            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions::makeOptionsOk (
                    juce::MessageBoxIconType::WarningIcon,
                    "No set list",
                    "Create or pick a set list before adding entries."),
                nullptr);
            return;
        }

        auto* aw = new juce::AlertWindow ("Add current set",
                                           "Name for this entry:",
                                           juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", suggestEntryName(), {}, false);
        aw->addButton ("ADD",    1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true,
            juce::ModalCallbackFunction::create ([this, aw] (int r)
            {
                if (r == 1)
                {
                    const auto n = aw->getTextEditorContents ("name").trim();
                    if (n.isNotEmpty()) addCurrent (n);
                }
                delete aw;
            }), false);
    }

    void addCurrent (const juce::String& name)
    {
        Entry e;
        e.id      = juce::Uuid().toString();
        e.name    = name;
        e.payload = onCaptureCurrentSet();
        if (! e.payload.isValid()) e.payload = juce::ValueTree ("EmptyPayload");
        entries.push_back (std::move (e));
        saveCurrent();
        // Jump to the page containing the newly-added entry so the user sees it.
        currentPage = (getCount() - 1) / kPageSize;
        refreshGrid();
    }

    juce::String suggestEntryName() const
    {
        int n = (int) entries.size() + 1;
        while (true)
        {
            const juce::String c = "Entry " + juce::String (n);
            const bool clash = std::any_of (entries.begin(), entries.end(),
                [&] (const Entry& x) { return x.name == c; });
            if (! clash) return c;
            ++n;
        }
    }

    //==========================================================================
    // UI members
    //==========================================================================
    juce::ComboBox    setListSelector;
    juce::TextButton  btnNew, btnDelete;
    juce::Label       lblHeaderName;

    EntryButton       gridButtons [kPageSize];

    juce::TextButton  btnPagePrev, btnPageNext;
    juce::Label       lblPage;
    juce::TextButton  btnPrev, btnNext, btnAddCurrent;
    juce::Label       lblPosition;

    int headerSeparatorY = 36;
    int footerSeparatorY = 380;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetListTab)
};
