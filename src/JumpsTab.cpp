#include "JumpsTab.h"

// ─── Layout constants ────────────────────────────────────────────────────────
namespace
{
    constexpr int kPad         = 8;
    constexpr int kTitleH      = 20;
    constexpr int kHeaderRowH  = 24;
    constexpr int kRowH        = 34;
    constexpr int kRowGap      = 4;
    constexpr int kSrcLabelW   = 86;
    constexpr int kBtnGap      = 2;
    constexpr int kGroupGap    = 12;   // gap between destination groups (mains|intros|fills|break)
    constexpr int kSrcGroupGap = 8;    // gap between source groups (intros|fills|break)

    constexpr const char* kSourceNames[Betel::kNumJumpSources] = {
        "INTRO A", "INTRO B", "INTRO C",
        "FILL AA", "FILL BB", "FILL CC", "FILL DD",
        "BREAK"
    };

    constexpr const char* kDestNames[Betel::kNumJumpDestinations] = {
        "MAIN A", "MAIN B", "MAIN C", "MAIN D",
        "INTRO A", "INTRO B", "INTRO C",
        "FILL AA", "FILL BB", "FILL CC", "FILL DD",
        "BREAK"
    };

    // Visual section boundaries (a gap follows these indices)
    inline bool isDestGroupBoundary (int c) { return c == 3 || c == 6 || c == 10; }
    inline bool isSrcGroupBoundary  (int r) { return r == 2 || r == 6;            }
}

// ─── Construction ────────────────────────────────────────────────────────────
JumpsTab::JumpsTab()
{
    setOpaque(false);

    lblTitle.setText(juce::String::fromUTF8("JUMPS \xe2\x80\x94 POST-TRANSITION DESTINATIONS"),
                     juce::dontSendNotification);
    lblTitle.setFont(juce::Font(13.5f, juce::Font::bold));
    lblTitle.setColour(juce::Label::textColourId,
                       juce::Colours::white.withAlpha(0.92f));
    lblTitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(lblTitle);

    for (int c = 0; c < kCols; ++c)
    {
        colHeaders[c].setText(kDestNames[c], juce::dontSendNotification);
        colHeaders[c].setFont(juce::Font(10.5f, juce::Font::bold));
        colHeaders[c].setColour(juce::Label::textColourId,
                                juce::Colours::white.withAlpha(0.70f));
        colHeaders[c].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(colHeaders[c]);
    }

    for (int r = 0; r < kRows; ++r)
    {
        rowLabels[r].setText(kSourceNames[r], juce::dontSendNotification);
        rowLabels[r].setFont(juce::Font(12.0f, juce::Font::bold));
        rowLabels[r].setColour(juce::Label::textColourId,
                               juce::Colours::white.withAlpha(0.92f));
        rowLabels[r].setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(rowLabels[r]);

        for (int c = 0; c < kCols; ++c)
        {
            buttons[r][c].setButtonText(kDestNames[c]);
            buttons[r][c].setMode(LedButton::Mode::Selector);
            buttons[r][c].onClick = [this, r, c]
            {
                selectDestination(r, c, /*fireCallback*/ true);
            };
            addAndMakeVisible(buttons[r][c]);
        }

        // Apply the default selection visually.
        selectDestination(r, selectedDest[r], /*fireCallback*/ false);
    }
}

// ─── Selection helpers ───────────────────────────────────────────────────────
void JumpsTab::selectDestination(int sourceRow, int destCol, bool fireCallback)
{
    if (sourceRow < 0 || sourceRow >= kRows) return;
    if (destCol  < 0 || destCol  >= kCols)   return;

    selectedDest[sourceRow] = destCol;

    for (int c = 0; c < kCols; ++c)
        buttons[sourceRow][c].setToggleState(c == destCol,
                                              juce::dontSendNotification);

    if (fireCallback && onJumpChanged)
        onJumpChanged(Betel::kJumpSourceSections[sourceRow],
                      Betel::kJumpDestSections[destCol]);
}

int JumpsTab::sourceRowIndex(Betel::StyleSection s) const
{
    for (int i = 0; i < kRows; ++i)
        if (Betel::kJumpSourceSections[i] == s) return i;
    return -1;
}

int JumpsTab::destColIndex(Betel::StyleSection s) const
{
    for (int i = 0; i < kCols; ++i)
        if (Betel::kJumpDestSections[i] == s) return i;
    return -1;
}

// ─── Public API ──────────────────────────────────────────────────────────────
void JumpsTab::setDestination(Betel::StyleSection source, Betel::StyleSection dest)
{
    const int r = sourceRowIndex(source);
    const int c = destColIndex(dest);
    if (r < 0 || c < 0) return;
    selectDestination(r, c, /*fireCallback*/ false);
}

Betel::StyleSection JumpsTab::getDestination(Betel::StyleSection source) const
{
    const int r = sourceRowIndex(source);
    if (r < 0) return Betel::StyleSection::MainA;
    return Betel::kJumpDestSections[selectedDest[r]];
}

Betel::JumpsConfig JumpsTab::getConfig() const
{
    Betel::JumpsConfig cfg;
    for (int r = 0; r < kRows; ++r)
        cfg.destinations[(size_t) r] = Betel::kJumpDestSections[selectedDest[r]];
    return cfg;
}

void JumpsTab::setConfig(const Betel::JumpsConfig& cfg)
{
    for (int r = 0; r < kRows; ++r)
    {
        const int c = destColIndex(cfg.destinations[(size_t) r]);
        if (c >= 0) selectDestination(r, c, /*fireCallback*/ false);
    }
}

// ─── Paint ───────────────────────────────────────────────────────────────────
void JumpsTab::paint(juce::Graphics& g)
{
    // Background comes from the page-level back.png; we only draw separators.

    // Underline below the title
    if (lblTitle.getBottom() > 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawHorizontalLine(lblTitle.getBottom() + 1,
                             (float) lblTitle.getX(),
                             (float) (getWidth() - kPad));
    }

    // Vertical dividers between destination groups (spanning the grid)
    if (kCols > 0 && ! colHeaders[0].getBounds().isEmpty())
    {
        const int topY = colHeaders[0].getY();
        const int botY = rowLabels[kRows - 1].getBottom();
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        for (int c = 0; c < kCols - 1; ++c)
        {
            if (isDestGroupBoundary(c))
            {
                const int x = (colHeaders[c].getRight()
                             + colHeaders[c + 1].getX()) / 2;
                g.drawVerticalLine(x, (float) topY, (float) botY);
            }
        }
    }

    // Horizontal dividers between source groups (intros/fills/break)
    if (! rowLabels[0].getBounds().isEmpty())
    {
        const int leftX  = rowLabels[0].getX();
        const int rightX = getWidth() - kPad;
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        for (int r = 0; r < kRows - 1; ++r)
        {
            if (isSrcGroupBoundary(r))
            {
                const int y = (rowLabels[r].getBottom()
                             + rowLabels[r + 1].getY()) / 2;
                g.drawHorizontalLine(y, (float) leftX, (float) rightX);
            }
        }
    }
}

// ─── Layout ──────────────────────────────────────────────────────────────────
void JumpsTab::resized()
{
    const int W = getWidth();
    const int H = getHeight();
    if (W < 100 || H < 100) return;

    int y = kPad;

    lblTitle.setBounds(kPad, y, W - 2 * kPad, kTitleH);
    y += kTitleH + 6;

    // Grid horizontal layout: 12 buttons in 4 groups (4|3|4|1), separated by
    // group gaps. Within a group, buttons use the smaller kBtnGap.
    const int gridX = kPad + kSrcLabelW + 4;
    const int gridW = W - gridX - kPad;
    if (gridW <= 0) return;

    const int numLargeGaps = 3;                              // 3 group dividers
    const int numSmallGaps = (kCols - 1) - numLargeGaps;
    const int totalGaps    = numLargeGaps * kGroupGap
                           + numSmallGaps * kBtnGap;
    const int btnW         = juce::jmax(20, (gridW - totalGaps) / kCols);

    std::array<int, kCols> colX {};
    {
        int x = gridX;
        for (int c = 0; c < kCols; ++c)
        {
            colX[(size_t) c] = x;
            x += btnW;
            if (c < kCols - 1)
                x += isDestGroupBoundary(c) ? kGroupGap : kBtnGap;
        }
    }

    // Column headers
    for (int c = 0; c < kCols; ++c)
        colHeaders[c].setBounds(colX[c], y, btnW, kHeaderRowH);
    y += kHeaderRowH + 4;

    // Source rows with group gaps between intros/fills/break
    for (int r = 0; r < kRows; ++r)
    {
        rowLabels[r].setBounds(kPad, y, kSrcLabelW, kRowH);
        for (int c = 0; c < kCols; ++c)
            buttons[r][c].setBounds(colX[c], y, btnW, kRowH);

        y += kRowH + kRowGap;
        if (isSrcGroupBoundary(r))
            y += kSrcGroupGap - kRowGap;  // grow the gap into a group divider
    }
}