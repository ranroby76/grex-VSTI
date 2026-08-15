#pragma once
//==============================================================================
// InstrEditorWindow.h
//
// Floating window hosting the per-slot instrument editor.
//
// The window no longer owns its own BlobReader — pressing LOAD SOUND PACK now
// fires `onLoadBlobRequested(file, accessCode)`, which the host (SoundsTab →
// MainComponent → BetelgeuseProcessor) forwards to the engine. The host then
// pushes the new preset list back in via `setPresetList(...)`.
//
// Access code:
//   The visible access-code textbox has been removed.  Betelgeuse sound packs
//   are all signed with the fixed code "111222", which getAccessCode() returns
//   unconditionally.  The textbox member is kept for ABI continuity but is
//   never added to the component tree and is zero-sized in the layout so it
//   uses no screen space.
//==============================================================================

#include <JuceHeader.h>
#include <BinaryData.h>
#include <vector>
#include <cmath>               // std::ceil on the slot-label text width
#include "GrexPopupWindow.h"   // stays in front, minimisable, never vanishes
#include "InstrEditPanel.h"
#include "SynthesisPanel.h"
#include "ModulationPanel.h"
#include "EffectsPanel.h"

class InstrEditorWindow : public juce::DocumentWindow
{
public:
    static constexpr int kDesignW = 1280;
    // The piano strip along the bottom costs kKeyboardH (64) + an 8 px gap.  The
    // window grew by exactly that, so the tab content area keeps the SAME height
    // it had before the strip existed (720-148-12 = 560  ==  792-148-12-64-8).
    // That matters: SynthesisPanel's MONO MODE column hands the portamento slider
    // whatever is left under the three sub-buttons, and GoldSlider's horizontal
    // mode needs ~64 px for its value/track/label stack.  Squeeze the column and
    // the slider collapses into the buttons above it.
    static constexpr int kDesignH = 792;   // was 720  (+64 strip, +8 gap)
    static constexpr int kMinW    = 800;
    static constexpr int kMinH    = 612;   // was 540  (+72, same content floor)

    // ── Host callbacks ────────────────────────────────────────────────────────
    std::function<void(const SlotParams&)> onParamsChanged;
    std::function<void(const SlotParams&)> onSaveAsDefault;   // "SAVE AS DEFAULT" pressed

    /** IGNORE PRESET CHANGES toggled.  The host owns the flag - it is a property
        of the SLOT, so it has to outlive this window. */
    std::function<void(bool)>              onFreezeToggled;

    //==========================================================================
    // USER SFZ - the parallel sound source for this slot.
    //
    // SFZ ON/OFF holds the slot: while it is lit the loaded .sfz plays and the
    // style's program changes are ignored, so an arrangement cannot take the
    // sound back mid-song.  Off, the slot returns to whatever instrument the
    // style last asked for.
    //==========================================================================
    std::function<void()>     onSfzLoadRequested;   // "LOAD SFZ" pressed
    std::function<void(bool)> onSfzToggled;         // the ON/OFF switch moved
    /** Left double-click on the GAIN handle — the developer base-unity dialog.
        Deliberately not handled here: this window has no idea which slot or
        which file it is editing on disk, and SoundsTab does. */
    std::function<void()>                  onBaseUnityRequested;
    std::function<void()>                  onClosed;
    std::function<void(int presetIndex, const juce::String& presetName)> onPresetSelected;

    // Returns true if the host accepted the blob; when true the host is
    // expected to push the new preset list in via setPresetList().
    std::function<bool(const juce::File& file, const juce::String& accessCode)> onLoadBlobRequested;

    // Click library — the user picked a file in the CLICK tab. The host
    // should load it into the engine for the currently-edited channel
    // (and persist the path in the matching SlotParams via onParamsChanged).
    std::function<void(const juce::File&)>  onClickFileChosen;

    // ── Piano strip (bottom of the window) ───────────────────────────────────
    // The user played a key: host sounds it on the engine channel behind the
    // slot currently being edited.  Held note, so melodic voices sustain.
    std::function<void(int note, int velocity)>  onKeyboardNoteOn;
    std::function<void(int note)>                onKeyboardNoteOff;

    // Polled at 30 Hz: host fills a 4 x uint32 (128-bit) mask of the notes
    // sounding on the edited channel, so the strip can light them up in red.
    std::function<void(uint32_t*)>               onQuerySoundingNotes;

    /** Tells the editor whether the currently-edited slot is solo or style.
        Forwarded to EffectsPanel, which keeps the flag but no longer gates any
        control on it (the per-slot ARABIC SCALE selector was removed — the
        oriental scale is a global, left-panel feature). */
    /** Host -> window: which SFZ is loaded on this slot and whether it is on. */
    void setSfzState (const juce::String& name, bool active)
    {
        if (content != nullptr) content->setSfzState (name, active);
    }

    void setSoloMode(bool isSolo)
    {
        if (content != nullptr) content->setSoloMode(isSolo);
    }

    /** Seed IGNORE PRESET CHANGES from the host's per-slot flag.

        THREE layers forward this, and missing the top one is what C2039 was
        reporting: InstrEditorWindow -> Content -> DesignCanvas.  The canvas owns
        the button, Content forwards, and the window is what the host actually
        holds - so a method that stops at Content is invisible to SoundsTab.

        Null-guarded like every other forward here: content is built with the
        window and a host call can land before it exists. */
    void setFrozen (bool f)
    {
        if (content != nullptr) content->setFrozen (f);
    }

    /** Green two-second confirmation in the header.  See DesignCanvas::flashSaved. */
    void flashSaved (const juce::String& text)
    {
        if (content != nullptr) content->flashSaved (text);
    }

    bool isFrozen() const
    {
        return content != nullptr && content->isFrozen();
    }

    /** Host -> window: what the SWEETENER is doing right now on the edited
        channel, in dB of gain reduction per stage.  Pushed from the host's UI
        timer exactly as the Finisher's GR meter is; cheap enough to send every
        tick because EffectsPanel drops anything that does not move a bar. */
    void setSweetenerGr (float softenDb, float peakDb, float tameDb, float roundDb)
    {
        if (content != nullptr) content->setSweetenerGr (softenDb, peakDb, tameDb, roundDb);
    }

    /** Tells the editor whether the current slot uses the two-thumb BAND filter
        (non-bass style slots 3..7) instead of the classic filter.  SynthesisPanel
        swaps the FILTER controls and hides FILTER ENV; ModulationPanel hides the
        FILTER LFO. */
    void setBandMode(bool useBand)
    {
        if (content != nullptr) content->setBandMode(useBand);
    }

    InstrEditorWindow()
        : juce::DocumentWindow("Instrument Editor",
                               juce::Colour(0xFF101010),
                               juce::DocumentWindow::closeButton,
                               true)
    {
        Betel::applyGrexPopupBehaviour(*this);
        setUsingNativeTitleBar(false);
        setResizable(true, false);
        setResizeLimits(kMinW, kMinH, 4000, 2800);

        if (auto icon = loadIconImage(); icon.isValid())
            setIcon(icon);

        auto* c = new Content(*this);
        content = c;
        setContentOwned(c, false);

        // 1280x792 is wider AND taller than a 13" MacBook Air's usable area
        // (1280x800 points, less the menu bar and Dock), so a fixed
        // centreWithSize opened it hanging off every edge with its own resize
        // corner out of reach.  Clamped to the screen; the resize limits set
        // above are untouched, so a large monitor loses nothing.
        Betel::centreGrexPopupOnScreen(*this, kDesignW, kDesignH);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        if (onClosed) onClosed();
    }

    void openCentredOver(juce::Component* reference, float ratio)
    {
        if (reference != nullptr && reference->isShowing())
        {
            auto refBounds = reference->getScreenBounds();
            int w = juce::jmax(kMinW, (int)(refBounds.getWidth()  * ratio));
            int h = juce::jmax(kMinH, (int)(refBounds.getHeight() * ratio));
            setBounds(refBounds.getCentreX() - w / 2,
                      refBounds.getCentreY() - h / 2, w, h);
        }
        setVisible(true);
        toFront(true);
    }

    void setSlotParams(const SlotParams& p)
    {
        if (content != nullptr) content->setSlotParams(p);
    }

    // 0 = hidden (solo / drums), 1 = bass (gap locked at 12), 2 = other melodic.
    void setNoteRangeMode(int mode)
    {
        if (content != nullptr) content->setNoteRangeMode(mode);
    }

    void setSlotLabel(const juce::String& text)
    {
        if (content != nullptr) content->setSlotLabel(text);
    }

    // Pushed in by the host after a successful load.
    void setPresetList(const std::vector<juce::String>& names)
    {
        if (content != nullptr) content->setPresetList(names);
    }

private:
    //==========================================================================
    // ICO parser — extracts the largest PNG sub-image and decodes via JUCE.
    //==========================================================================
    static juce::Image loadIconImage()
    {
        int size = 0;
        const char* raw = BinaryData::getNamedResource("icon_ico", size);
        if (raw == nullptr || size < 22) return {};

        auto* p = reinterpret_cast<const juce::uint8*>(raw);
        if (p[0] != 0 || p[1] != 0 || p[2] != 1 || p[3] != 0) return {};
        const int count = (int) p[4] | ((int) p[5] << 8);
        if (count <= 0 || count > 64) return {};

        int bestOffset = -1, bestSize = 0, bestDim = 0;
        for (int i = 0; i < count; ++i)
        {
            const int entryOff = 6 + i * 16;
            if (entryOff + 16 > size) break;

            auto* e = p + entryOff;
            const int w        = e[0] == 0 ? 256 : (int) e[0];
            const int dataSize = (int) e[8]  | ((int) e[9]  << 8)
                               | ((int) e[10] << 16) | ((int) e[11] << 24);
            const int dataOff  = (int) e[12] | ((int) e[13] << 8)
                               | ((int) e[14] << 16) | ((int) e[15] << 24);

            if (dataOff <= 0 || dataSize < 8 || dataOff + dataSize > size) continue;

            auto* img = p + dataOff;
            const bool isPng = img[0] == 0x89 && img[1] == 0x50
                            && img[2] == 0x4E && img[3] == 0x47;
            if (isPng && w >= bestDim)
            {
                bestDim    = w;
                bestOffset = dataOff;
                bestSize   = dataSize;
            }
        }

        if (bestOffset < 0) return {};
        return juce::ImageFileFormat::loadFrom(raw + bestOffset, (size_t) bestSize);
    }

    //==========================================================================
    // DesignCanvas
    //==========================================================================
    class DesignCanvas : public juce::Component
    {
    public:
        std::function<void(int)>                          onTabSelected;
        std::function<void()>                             onPanelChanged;
        std::function<void()>                             onBaseUnityRequested;
        std::function<void()>                             onSaveDefault;       // "SAVE AS DEFAULT"
        std::function<void(bool)>                         onFreezeToggled;     // "IGNORE PRESET CHANGES"
        std::function<void()>                             onSfzLoad;
        std::function<void(bool)>                         onSfzToggle;
        std::function<void()>                             onLoadBlobClicked;
        std::function<void(int)>                          onPresetSelectionChanged;
        std::function<void(const juce::File&)>            onClickFileChosen;

        // Piano strip along the bottom.
        std::function<void(int note, int velocity)>       onKeyboardNoteOn;
        std::function<void(int note)>                     onKeyboardNoteOff;
        std::function<void(uint32_t*)>                    onQuerySounding;

        /** Tells the EFFECTS tab whether the slot being edited is solo or style.

            Also decides whether SAVE SETTINGS exists at all.

            SOLO slots keep it: a right-hand sound is chosen once and wanted the
            same way in every song, so it has a file of its own -
            instruments_presets\\NNN-Name.ins - and that file is the only way to
            make a program change reload it verbatim.

            STYLE slots do not: a style slot's sound lives in the SET, which is
            per style rather than once per instrument, so the button would have
            nothing to write.  Hidden rather than left there to refuse.

            It was briefly retired from BOTH editors, which took the .ins save
            away from the solo side along with the style side it was aimed at. */
        void setSoloMode(bool isSolo)
        {
            effectsPanel.setSoloMode(isSolo);

            saveDefaultBtn.setVisible (isSolo);
            resized();          // the header row re-flows around it
        }

        /** Seeded by the host on every open and slot change - the flag belongs to
            the SLOT, not to the window, so a freeze survives closing the editor
            and reopening it on the same slot. */
        void setFrozen (bool f)
        {
            if (frozen == f) return;
            frozen = f;
            refreshFreezeBtn();
        }

        bool isFrozen() const noexcept { return frozen; }

        /** Show the green confirmation for two seconds.  Called only after the
            file has actually been written - a confirmation that appears when the
            write failed is worse than no confirmation at all. */
        void flashSaved (const juce::String& text)
        {
            savedLabel.setText (text, juce::dontSendNotification);
            savedLabel.setVisible (true);

            const int token = ++savedFlashToken;
            juce::Component::SafePointer<DesignCanvas> safe (this);

            juce::Timer::callAfterDelay (2000, [safe, token]() mutable
            {
                if (auto* c = safe.getComponent())
                    if (c->savedFlashToken == token)      // not superseded
                        c->savedLabel.setVisible (false);
            });
        }

        /** Lit while frozen, using the same square-button styling every other
            engaged toggle in this editor uses - forgetting it is on is the one
            way this button can cost work instead of saving it, so it has to read
            as ON from across the room. */
        void refreshFreezeBtn()
        {
            InstrEditStyle::styleSquareButton (freezeBtn, frozen);
        }

        /** Band filter (non-bass style slots): forwarded to the SYNTHESIS and
            MODULATION tabs so they swap / hide the classic filter controls. */
        void setBandMode(bool useBand)
        {
            synthPanel.setBandMode(useBand);
            modPanel  .setBandMode(useBand);
        }

        /** Live sweetener gain reduction — straight through to the EFFECTS
            page, which owns the four bars and decides whether to repaint. */
        void setSweetenerGr (float softenDb, float peakDb, float tameDb, float roundDb)
        {
            effectsPanel.setSweetenerGr (softenDb, peakDb, tameDb, roundDb);
        }

        DesignCanvas()
        {
            // Sound packs / presets are no longer loaded from this window — the
            // instrument cells and reference-voice pills handle loading now.

            slotLabel.setText("Currently editing: \xe2\x80\x94", juce::dontSendNotification);
            slotLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFCC6600));
            slotLabel.setFont(juce::Font(14.0f, juce::Font::bold));
            slotLabel.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(slotLabel);

            const juce::String tabLabels[kNumTabs] =
                { "SYNTHESIS", "MODULATION", "EFFECTS" };
            for (int i = 0; i < kNumTabs; ++i)
            {
                tabBtns[i].setButtonText(tabLabels[i]);
                tabBtns[i].onClick = [this, i]{ selectTab(i); };
                addAndMakeVisible(tabBtns[i]);
            }

            // "SAVE AS DEFAULT" — writes the current voice (source .frb name +
            // the full SlotParams snapshot) to instruments_presets\NNN-Name.ins
            // so the matching program change / selector reloads it verbatim.
            saveDefaultBtn.setButtonText("SAVE SETTINGS");
            saveDefaultBtn.onClick = [this]{ if (onSaveDefault) onSaveDefault(); };

            // ── IGNORE PRESET CHANGES ─────────────────────────────────────────
            //
            // A programming aid: while it is lit, nothing outside this window may
            // write this slot's parameters.  Loading a set, selecting the sound
            // again, a preset reload after somebody else's save - all of them
            // leave the slot exactly where the user left it.
            //
            // SAVE SETTINGS still works while frozen, and that is the point of
            // the pair: it reads what is ON SCREEN, so the frozen state is what
            // gets written to the .ins.  Freeze, tune, save, unfreeze - instead
            // of losing the work to the next set load and starting again.
            //
            // addAndMakeVisible, unlike SAVE SETTINGS beside it: this one is for
            // STYLE slots too.  A style slot's params come from the set, which is
            // exactly the thing worth freezing while voicing one.
            freezeBtn.setButtonText("IGNORE PRESET CHANGES");
            freezeBtn.onClick = [this]
            {
                frozen = ! frozen;
                refreshFreezeBtn();
                if (onFreezeToggled) onFreezeToggled (frozen);
            };
            addAndMakeVisible(freezeBtn);
            refreshFreezeBtn();
            // addChildComponent, NOT addAndMakeVisible — the same trap the SFZ
            // LOAD button documents below.  setSoloMode owns this button's
            // visibility, and starting visible would show it for a moment on a
            // style slot, which is the one place it must never appear.
            addChildComponent(saveDefaultBtn);

            // ── SAVE CONFIRMATION ────────────────────────────────────────────
            // Writing a .ins is silent and instant: without a word back, the
            // only way to know it worked was to close the window and reopen it.
            //
            // Parked in the gap between IGNORE PRESET CHANGES and the SFZ pair,
            // so it never overlaps a control however long the instrument name
            // gets - and hidden until there is something to say, so the header
            // does not carry a permanently empty slot.
            savedLabel.setText ("SETTINGS SAVED", juce::dontSendNotification);
            savedLabel.setJustificationType (juce::Justification::centred);
            savedLabel.setColour (juce::Label::textColourId, kSavedGreen);
            savedLabel.setInterceptsMouseClicks (false, false);
            addChildComponent (savedLabel);

            // ── USER SFZ ──────────────────────────────────────────────────────
            // Two controls, on every slot: LOAD picks the file, SFZ switches it
            // in and out.  Deliberately separate - loading is a decision you make
            // once, switching is one you make while listening, and one button
            // doing both would make A/B comparison a file dialog every time.
            sfzLoadBtn.setButtonText("LOAD");
            styleSfzLoad(false);
            sfzLoadBtn.onClick = [this]{ if (onSfzLoad) onSfzLoad(); };
            // addChildComponent, NOT addAndMakeVisible - the latter sets visible
            // TRUE and undid the line above it.
            addChildComponent(sfzLoadBtn);

            // THE TOGGLE CARRIES THE CAPTION and is ALWAYS enabled.  It used to
            // be disabled until a file was loaded, while LOAD only appeared once
            // the toggle was on - so neither could ever be reached.  The toggle
            // is the way in; LOAD is what it reveals.
            sfzToggleBtn.setButtonText("LOAD YOUR OWN SFZ");
            sfzToggleBtn.setClickingTogglesState(true);
            styleSfzToggle(false);
            sfzToggleBtn.onClick = [this]
            {
                sfzWanted = sfzToggleBtn.getToggleState();
                styleSfzToggle(sfzWanted);
                sfzLoadBtn.setVisible(sfzWanted);
                if (onSfzToggle) onSfzToggle(sfzWanted);
                resized();
            };
            addAndMakeVisible(sfzToggleBtn);

            sfzLabel.setJustificationType(juce::Justification::centredLeft);
            sfzLabel.setFont(juce::Font(12.0f));
            sfzLabel.setColour(juce::Label::textColourId,
                               InstrEditStyle::kPanelLabel.withAlpha(0.75f));
            addAndMakeVisible(sfzLabel);

            synthPanel.onBaseUnityRequested = [this]
            { if (onBaseUnityRequested) onBaseUnityRequested(); };

            synthPanel   .onAnythingChanged = [this]{ if (onPanelChanged) onPanelChanged(); };
            modPanel     .onAnythingChanged = [this]{ if (onPanelChanged) onPanelChanged(); };
            effectsPanel .onAnythingChanged = [this]{ if (onPanelChanged) onPanelChanged(); };

            effectsPanel.onClickFileChosen = [this](const juce::File& f)
            {
                if (onClickFileChosen) onClickFileChosen(f);
            };

            addChildComponent(synthPanel);
            addChildComponent(modPanel);
            addChildComponent(effectsPanel);

            pianoStrip.onNoteOn = [this](int n, int v)
            {
                if (onKeyboardNoteOn) onKeyboardNoteOn(n, v);
            };
            pianoStrip.onNoteOff = [this](int n)
            {
                if (onKeyboardNoteOff) onKeyboardNoteOff(n);
            };
            pianoStrip.onQuerySounding = [this](uint32_t* mask)
            {
                if (onQuerySounding) onQuerySounding(mask);
            };
            addAndMakeVisible(pianoStrip);

            selectTab(0);
        }

        void setSlotParams(const SlotParams& p)
        {
            synthPanel   .loadParams(p);
            modPanel     .loadParams(p);
            effectsPanel .loadParams(p);
        }

        void setNoteRangeMode(int mode) { synthPanel.setNoteRangeMode(mode); }

        void collectInto(SlotParams& p) const
        {
            synthPanel   .readInto(p);
            modPanel     .readInto(p);
            effectsPanel .readInto(p);
        }

        void setSlotLabel(const juce::String& t)
        { slotLabel.setText(t, juce::dontSendNotification); }

        void setPresetList(const std::vector<juce::String>&) {}   // presets removed

        // Hardcoded for Betelgeuse — the textbox is no longer visible.
        juce::String getAccessCode() const { return "111222"; }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xFF101010));
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.drawHorizontalLine(kHeaderBottomY, 0.0f, (float) getWidth());
            g.drawHorizontalLine(kTabBarBottomY, 0.0f, (float) getWidth());
        }

        void resized() override
        {
            const int pad = 16;
            const int W = getWidth();

            // Access label + textbox are not laid out — zero-size them to make
            // sure they never affect layout even if some path makes them visible.
            accessLabel     .setBounds(0, 0, 0, 0);
            accessCodeEditor.setBounds(0, 0, 0, 0);

            // ── HEADER ROW: title + SAVE SETTINGS left, SFZ hard right ────────
            //
            // The SFZ pair sits on the SAME baseline as the orange "Editing:"
            // title, at the right edge.  Both answer "what am I hearing" - the
            // title names the slot, the buttons say whether a user instrument
            // has taken it - so they belong on one line, not stacked.
            //
            // SAVE SETTINGS joins that line on the LEFT, immediately after the
            // title text.  On a STYLE slot it is hidden (see setSoloMode) and
            // the title takes the lot - reserving a gap for a control that is
            // not there would just look like something failed to draw.
            const int rowY = 50, rowH = 26;

            int right = W - pad;

            const int togW = 170;
            if (sfzLoadBtn.isVisible())
            {
                const int loadW = 110;
                sfzLoadBtn.setBounds(right - loadW, rowY, loadW, rowH);
                right -= loadW + 4;
            }
            sfzToggleBtn.setBounds(right - togW, rowY, togW, rowH);
            right -= togW + 10;

            // The loaded file's name, between the title and the buttons - it can
            // have whatever width is left and shortens rather than pushing.
            const int nameW = juce::jlimit(0, 220, right - pad - 160);
            sfzLabel.setBounds(right - nameW, rowY + 2, nameW, rowH - 4);
            right -= nameW + 8;

            // ── TITLE, THEN SAVE SETTINGS, ON THE SAME LINE ─────────────────
            //
            // The button sits immediately after the "Currently editing: SOLO n |
            // Name" text rather than at the right edge, because the right edge
            // belongs to the SFZ pair and a save button parked among them reads
            // as part of the SFZ controls.  Next to the slot name it reads as
            // what it is: save THIS slot's sound.
            //
            // The label is sized to its own text so the button follows the name
            // instead of floating at a fixed offset, and is clamped so a long
            // instrument name shortens rather than pushing the button off.
            const int saveW = saveDefaultBtn.isVisible() ? 130 : 0;
            const int saveGap = saveDefaultBtn.isVisible() ? 8 : 0;

            // IGNORE PRESET CHANGES sits immediately right of SAVE SETTINGS, and
            // on a style slot - where SAVE SETTINGS is hidden - it simply takes
            // that place instead.  Wider than SAVE because the caption is.
            const int freezeW   = 190;
            const int freezeGap = 8;

            // TEXT WIDTH.  Font::getStringWidthFloat was REMOVED in JUCE 8, and
            // its replacement static moved around between point releases, so
            // measure with a GlyphArrangement instance instead -- addLineOfText
            // and getBoundingBox have been stable across every JUCE version this
            // project has ever built against.
            const int textW = [this]
            {
                const auto text = slotLabel.getText();
                if (text.isEmpty()) return 0;

                juce::GlyphArrangement ga;
                ga.addLineOfText (slotLabel.getFont(), text, 0.0f, 0.0f);
                return (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth()) + 6;
            }();

            const int labelRoom = juce::jmax (60, right - pad - saveW - saveGap);
            const int labelW    = juce::jlimit (60, labelRoom, textW);

            slotLabel.setBounds(pad, 52, labelW, 22);

            int cursorX = pad + labelW;

            if (saveDefaultBtn.isVisible())
            {
                cursorX += saveGap;
                saveDefaultBtn.setBounds(cursorX, rowY, saveW, rowH);
                cursorX += saveW;
            }
            else
            {
                saveDefaultBtn.setBounds(0, 0, 0, 0);
            }

            // Clamped so it never runs under the SFZ pair on the right: the
            // header is a fixed width and a long instrument name has already
            // eaten into it by the time we get here.
            const int freezeX   = cursorX + freezeGap;
            const int freezeFit = juce::jmax (0, right - freezeX);
            const int freezeUsed = juce::jmin (freezeW, freezeFit);
            freezeBtn.setBounds(freezeX, rowY, freezeUsed, rowH);

            // CENTRED IN WHATEVER IS LEFT, between the last button and the SFZ
            // pair.  Computed from the buttons' real extents rather than a fixed
            // x, so it stays clear of them when a long instrument name has
            // pushed everything right.  Below ~90 px there is no room for the
            // text and it is simply not shown - better absent than clipped or
            // overlapping.
            {
                const int freeL = freezeX + freezeUsed + 12;
                const int freeR = right - 12;
                const int freeW = freeR - freeL;

                if (freeW >= 90)
                    savedLabel.setBounds (freeL, rowY, freeW, rowH);
                else
                    savedLabel.setBounds (0, 0, 0, 0);
            }

            const int tabY = 92;
            const int tabH = 42;
            const int gap  = 8;
            const int tabW = (W - 2 * pad - (kNumTabs - 1) * gap) / kNumTabs;
            for (int i = 0; i < kNumTabs; ++i)
                tabBtns[i].setBounds(pad + i * (tabW + gap), tabY, tabW, tabH);

            // Piano strip: hard against the bottom, spanning the FULL width
            // (no padding -- it's meant to read as one continuous keyboard).
            pianoStrip.setBounds(0, getHeight() - kKeyboardH, W, kKeyboardH);

            const int contentY = kTabBarBottomY + 8;
            const int contentH = getHeight() - contentY - 12 - kKeyboardH - 8;
            const juce::Rectangle<int> contentBounds(pad, contentY, W - 2 * pad, contentH);
            synthPanel   .setBounds(contentBounds);
            modPanel     .setBounds(contentBounds);
            effectsPanel .setBounds(contentBounds);
        }

    private:
        void selectTab(int idx)
        {
            activeTab = juce::jlimit(0, kNumTabs - 1, idx);
            for (int i = 0; i < kNumTabs; ++i)
                InstrEditStyle::styleSquareButton(tabBtns[i], i == activeTab);

            synthPanel   .setVisible(activeTab == 0);
            modPanel     .setVisible(activeTab == 1);
            effectsPanel .setVisible(activeTab == 2);

            if (onTabSelected) onTabSelected(activeTab);
        }

        static constexpr int kNumTabs        = 3;
        static constexpr int kHeaderBottomY  = 80;
        static constexpr int kTabBarBottomY  = 140;
        static constexpr int kKeyboardH      = 64;   // design-space px ("short keys")

        juce::Label      accessLabel;          // hidden — kept for ABI continuity
        juce::TextEditor accessCodeEditor;     // hidden — kept for ABI continuity
        juce::Label      slotLabel;

        juce::TextButton tabBtns[kNumTabs];
        juce::TextButton saveDefaultBtn;
        juce::TextButton freezeBtn;
        bool             frozen = false;

        juce::Label      savedLabel;
        /** Bumped on every flash.  The hide is a delayed callback, so without a
            token a second save inside the two seconds would be blanked early by
            the FIRST save's timer - the confirmation would vanish while the user
            was still looking at it. */
        int              savedFlashToken = 0;

        inline static const juce::Colour kSavedGreen { juce::Colour (0xFF57D46A) };
        juce::TextButton sfzLoadBtn, sfzToggleBtn;

        /** ORANGE CAPTION, both states.

            styleSquareButton puts BLACK on the amber fill, which is right for a
            control whose lit state is the amber itself.  This one reads as a
            status light - "an SFZ is holding this slot" - so the word stays
            orange either way and the fill carries the state. */
        /** GREEN while empty, ORANGE once a file is loaded. */
        void styleSfzLoad (bool loaded)
        {
            const auto fill = loaded ? juce::Colour (0xFFCC6600) : juce::Colour (0xFF1E7A3C);
            sfzLoadBtn.setColour (juce::TextButton::buttonColourId,   fill);
            sfzLoadBtn.setColour (juce::TextButton::buttonOnColourId, fill);
            sfzLoadBtn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            sfzLoadBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        }

        void styleSfzToggle (bool on)
        {
            const auto amber = juce::Colour (0xFFCC6600);
            const auto text  = on ? amber : juce::Colours::white;
            sfzToggleBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
            sfzToggleBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF3A2A14));
            sfzToggleBtn.setColour (juce::TextButton::textColourOffId, text);
            sfzToggleBtn.setColour (juce::TextButton::textColourOnId,  text);
            sfzToggleBtn.repaint();
        }

        bool sfzWanted = false;
        juce::Label      sfzLabel;

    public:
        /** Host -> canvas: what is loaded, and whether it is switched in.

            An empty name means nothing is loaded, so the toggle is disabled -
            a switch with nothing behind it is worse than no switch. */
        void setSfzState (const juce::String& name, bool active)
        {
            // INTENT, not just engine state - see DrumsPopup::setSfzState.
            // A REFRESH IS A SLOT CHANGE - see DrumsPopup::setSfzState.
            sfzWanted = active;
            const bool on = active;

            sfzToggleBtn.setToggleState (on, juce::dontSendNotification);
            styleSfzToggle (on);

            sfzLoadBtn.setVisible (on);
            // GREEN until something is loaded, ORANGE once it is.
            styleSfzLoad (name.isNotEmpty());
            sfzLoadBtn.setButtonText (name.isEmpty() ? juce::String ("LOAD") : name);

            sfzLabel.setText (active && name.isNotEmpty() ? name : juce::String(),
                              juce::dontSendNotification);
            resized();
        }

    private:
        int              activeTab = 0;

        SynthesisPanel   synthPanel;
        ModulationPanel  modPanel;
        EffectsPanel     effectsPanel;
        PianoStrip       pianoStrip;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DesignCanvas)
    };

    //==========================================================================
    // Content — proportional scaling + corner grip
    //
    // No longer owns a BlobReader: pressing LOAD SOUND PACK runs the file
    // chooser and then asks the host (via owner.onLoadBlobRequested) to do the
    // actual loading. The host pushes the resulting preset list back in via
    // InstrEditorWindow::setPresetList.
    //==========================================================================
    class Content : public juce::Component
    {
    public:
        explicit Content(InstrEditorWindow& owner_) : owner(owner_)
        {
            canvas.onTabSelected             = [](int){};
            canvas.onPanelChanged            = [this]{ propagateParams(); };
            canvas.onBaseUnityRequested      = [this]
            { if (owner.onBaseUnityRequested) owner.onBaseUnityRequested(); };
            canvas.onSaveDefault             = [this]
            {
                canvas.collectInto(currentParams);
                if (owner.onSaveAsDefault) owner.onSaveAsDefault(currentParams);
            };
            canvas.onFreezeToggled           = [this] (bool f)
            { if (owner.onFreezeToggled) owner.onFreezeToggled (f); };
            canvas.onSfzLoad                 = [this]
            { if (owner.onSfzLoadRequested) owner.onSfzLoadRequested(); };
            canvas.onSfzToggle               = [this] (bool on)
            { if (owner.onSfzToggled) owner.onSfzToggled (on); };
            canvas.onLoadBlobClicked         = [this]{ doLoadBlob(); };
            canvas.onPresetSelectionChanged  = [this](int idx)
            {
                if (idx < 0) return;
                if (owner.onPresetSelected)
                    owner.onPresetSelected(idx, juce::String("Preset ") + juce::String(idx + 1));
            };
            canvas.onClickFileChosen = [this](const juce::File& f)
            {
                if (owner.onClickFileChosen) owner.onClickFileChosen(f);
            };
            canvas.onKeyboardNoteOn = [this](int n, int v)
            {
                if (owner.onKeyboardNoteOn) owner.onKeyboardNoteOn(n, v);
            };
            canvas.onKeyboardNoteOff = [this](int n)
            {
                if (owner.onKeyboardNoteOff) owner.onKeyboardNoteOff(n);
            };
            canvas.onQuerySounding = [this](uint32_t* mask)
            {
                if (owner.onQuerySoundingNotes) owner.onQuerySoundingNotes(mask);
            };

            addAndMakeVisible(canvas);

            cornerResizer = std::make_unique<juce::ResizableCornerComponent>(
                &owner, owner.getConstrainer());
            addAndMakeVisible(*cornerResizer);
        }

        /** Forwarded by InstrEditorWindow::setSoloMode. */
        void setSoloMode(bool isSolo) { canvas.setSoloMode(isSolo); }

        void flashSaved (const juce::String& text) { canvas.flashSaved (text); }

        /** Seed IGNORE PRESET CHANGES from the host's per-slot flag. */
        void setFrozen (bool f) { canvas.setFrozen (f); }
        bool isFrozen() const   { return canvas.isFrozen(); }

        /** Forwarded by InstrEditorWindow::setSfzState. */
        void setSfzState(const juce::String& name, bool active)
        { canvas.setSfzState(name, active); }

        /** Forwarded by InstrEditorWindow::setBandMode. */
        void setBandMode(bool useBand) { canvas.setBandMode(useBand); }

        /** Forwarded by InstrEditorWindow::setSweetenerGr. */
        void setSweetenerGr (float softenDb, float peakDb, float tameDb, float roundDb)
        { canvas.setSweetenerGr (softenDb, peakDb, tameDb, roundDb); }

        void setSlotParams(const SlotParams& p)
        {
            currentParams = p;
            canvas.setSlotParams(p);
        }

        // 0 = hidden (solo / drums), 1 = bass (locked window), 2 = other melodic.
        void setNoteRangeMode(int mode) { canvas.setNoteRangeMode(mode); }

        void setSlotLabel(const juce::String& t) { canvas.setSlotLabel(t); }

        void setPresetList(const std::vector<juce::String>& names)
        {
            canvas.setPresetList(names);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xFF080808));
        }

        void resized() override
        {
            const int W = getWidth();
            const int H = getHeight();

            const float sx = (float) W / (float) kDesignW;
            const float sy = (float) H / (float) kDesignH;
            const float s  = juce::jmin(sx, sy);
            const int scaledW = (int)((float) kDesignW * s);
            const int scaledH = (int)((float) kDesignH * s);
            const int dx = (W - scaledW) / 2;
            const int dy = (H - scaledH) / 2;

            canvas.setBounds(0, 0, kDesignW, kDesignH);
            canvas.setTransform(juce::AffineTransform::scale(s).translated((float) dx, (float) dy));

            const int grip = 18;
            if (cornerResizer != nullptr)
                cornerResizer->setBounds(W - grip, H - grip, grip, grip);
        }

    private:
        void propagateParams()
        {
            canvas.collectInto(currentParams);
            if (owner.onParamsChanged) owner.onParamsChanged(currentParams);
        }

        void doLoadBlob()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Choose a sound pack",
                juce::File(),
                "*.frb;*.blob");

            // Fixed access code for all Betelgeuse sound packs.
            const auto code = canvas.getAccessCode();   // returns "111222"

            // Step aside for the OS dialog and take the topmost flag back
            // when it closes -- see GrexPopupWindow.h.
            Betel::suspendGrexPopupTopmost (&owner);
            juce::Component::SafePointer<juce::Component> back (&owner);

            fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
                [this, code, back](const juce::FileChooser& fc)
                {
                    Betel::restoreGrexPopupTopmost (back);

                    auto file = fc.getResult();
                    if (!file.existsAsFile()) return;

                    const bool ok = owner.onLoadBlobRequested
                                  ? owner.onLoadBlobRequested(file, code)
                                  : false;

                    if (!ok)
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Sound pack failed to load",
                            "The engine rejected the file (unsupported format or wrong build).");
                });
        }

        InstrEditorWindow& owner;
        DesignCanvas       canvas;
        SlotParams         currentParams;

        std::unique_ptr<juce::ResizableCornerComponent> cornerResizer;
        std::unique_ptr<juce::FileChooser>              fileChooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Content)
    };

    Content* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrEditorWindow)
};


