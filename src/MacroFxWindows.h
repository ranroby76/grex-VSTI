#pragma once
//==============================================================================
// MacroFxWindows.h — the two editors behind Settings ▸ GLOBAL SETTINGS.
//
//   FamilyFxWindow    one GM family's six-stage melodic chain (Funkey Mode)
//   BigDrumsFxWindow  the single global drum rack            (Big Drums)
//
// ── WHY THE FAMILY EDITOR IS NOT A NEW EDITOR ────────────────────────────────
//
// FamilyFxWindow embeds the SAME EffectsPanel the Sounds instrument editor uses,
// bound to a Betel::FamilyFxParams instead of a SlotParams (the six stages carry
// identical field names on both types — see GlobalMacros.h, which relies on that
// for its member-for-member push).  PAN is switched off because a family preset
// carries character, not placement.
//
// The consequence that matters: a family preset can never drift from what the
// per-instrument editor offers, because there is only one implementation.  Add a
// stage to EffectsPanel and both pages gain it.
//
// ── LIVE AUDITION ────────────────────────────────────────────────────────────
//
// Every knob move writes straight into the live preset and fires onChanged, so
// the affected family (or the drum rack) is re-pushed to the engine while the
// window is open — you hear the edit on the running arrangement rather than
// after a save/reload cycle.  Nothing is written to disk until SAVE.
//
// ── DISPLAYED TEXT IS PLAIN ASCII ────────────────────────────────────────────
//
// Window titles, headers and frame captions use ASCII only.  The source files
// carry mixed encodings, and a non-ASCII glyph in a juce::String literal comes
// out as mojibake once the file is re-saved by an editor that guesses wrong.
// Comments can hold whatever they like — they are never drawn.
//
// ── SAVE ─────────────────────────────────────────────────────────────────────
//
// SAVE writes the macro's whole file (grex_funkey.xml holds all seven families,
// grex_bigdrums.xml the one rack), so saving from any family window commits the
// lot.  Closing WITHOUT saving leaves the edits live for the session but not on
// disk — the next plugin start reloads the file.
//==============================================================================

#include <JuceHeader.h>
#include "GrexPopupWindow.h"   // stays in front, minimisable, never vanishes
#include <functional>
#include <utility>
#include <vector>

#include "InstrEditPanel.h"     // GoldSlider / InstrEditStyle
#include "EffectsPanel.h"       // reused verbatim by the family editor
#include "GlobalMacros.h"

namespace Betel
{
    //==========================================================================
    // Shared header strip: title on the left, SAVE on the right.
    //==========================================================================
    class MacroEditorHeader : public juce::Component
    {
    public:
        std::function<void()> onSave;

        MacroEditorHeader()
        {
            title.setJustificationType (juce::Justification::centredLeft);
            title.setColour (juce::Label::textColourId, juce::Colour (0xFFCC6600));
            title.setFont (juce::Font (18.0f, juce::Font::bold));
            addAndMakeVisible (title);

            saveBtn.setButtonText ("SAVE");
            InstrEditStyle::styleSquareButton (saveBtn, false);
            saveBtn.onClick = [this] { if (onSave) onSave(); };
            addAndMakeVisible (saveBtn);

            savedMsg.setJustificationType (juce::Justification::centredRight);
            savedMsg.setColour (juce::Label::textColourId, juce::Colour (0xFF00C853));
            savedMsg.setFont (juce::Font (14.0f, juce::Font::bold));
            addAndMakeVisible (savedMsg);
        }

        void setTitle (const juce::String& s)
        { title.setText (s, juce::dontSendNotification); }

        /** Flash a confirmation next to SAVE.  Cleared on the next edit. */
        void setSavedMessage (const juce::String& s)
        { savedMsg.setText (s, juce::dontSendNotification); }

        void resized() override
        {
            auto b = getLocalBounds().reduced (4, 2);
            saveBtn .setBounds (b.removeFromRight (110).reduced (2, 3));
            savedMsg.setBounds (b.removeFromRight (170));
            title   .setBounds (b);
        }

    private:
        juce::Label      title, savedMsg;
        juce::TextButton saveBtn;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroEditorHeader)
    };

    //==========================================================================
    // FamilyFxWindow — Funkey Mode, one family at a time.
    //==========================================================================
    class FamilyFxWindow : public juce::DocumentWindow
    {
    public:
        /** A knob moved: the preset is already updated, re-push it to the
            engine so the edit is audible immediately. */
        std::function<void()> onChanged;
        std::function<void()> onClosed;

        FamilyFxWindow()
            : juce::DocumentWindow ("Funkey Mode - Family FX",
                                    juce::Colour (0xFF1A1A1A),
                                    juce::DocumentWindow::closeButton)
        {
            Betel::applyGrexPopupBehaviour (*this);
            setUsingNativeTitleBar (false);
            setResizable (true, true);
            setResizeLimits (900, 470, 2200, 1200);
            content = new Content (*this);
            setContentOwned (content, true);
            Betel::centreGrexPopupOnScreen (*this, 1120, 560);
        }

        ~FamilyFxWindow() override { clearContentComponent(); }

        /** Bind to a family slot (FunkeyFamilies::Slot) and show. */
        void showForFamily (int familySlot, juce::Component* parent)
        {
            slot = juce::jlimit (0, (int) FunkeyFamilies::Count - 1, familySlot);
            content->bind (slot);
            setName ("Funkey Mode - " + juce::String (FunkeyFamilies::name (slot)));
            placeAndShow (parent);
        }

        int getFamilySlot() const noexcept { return slot; }

        void closeButtonPressed() override
        {
            setVisible (false);
            if (onClosed) onClosed();
        }

    private:
        void placeAndShow (juce::Component* parent)
        {
            if (parent != nullptr)
            {
                const auto pb = parent->getScreenBounds();
                const int w = juce::jlimit (900, 2200, (int) (pb.getWidth()  * 0.80f));
                const int h = juce::jlimit (470, 1200, (int) (pb.getHeight() * 0.72f));
                setBounds (pb.getCentreX() - w / 2, pb.getCentreY() - h / 2, w, h);
            }
            setVisible (true);
            toFront (true);
        }

        //----------------------------------------------------------------------
        class Content : public juce::Component
        {
        public:
            explicit Content (FamilyFxWindow& owner_) : owner (owner_)
            {
                setOpaque (true);

                header.onSave = [this]
                {
                    const bool ok = GlobalMacros::get().saveFunkey();
                    header.setSavedMessage (ok ? "saved to grex_funkey.xml"
                                               : "SAVE FAILED");
                };
                addAndMakeVisible (header);

                fx.onAnythingChanged = [this]
                {
                    if (binding) return;             // seeding, not a user edit
                    fx.readFx (target());
                    header.setSavedMessage ({});     // edits invalidate "saved"
                    if (owner.onChanged) owner.onChanged();
                };
                addAndMakeVisible (fx);
            }

            void bind (int familySlot)
            {
                slot = familySlot;

                // A family preset carries character, not placement — so PAN
                // goes and the other six stages, chorus and reverb included, are
                // all the family's own.
                fx.setPanSelectorVisible (false);

                binding = true;
                fx.loadFx (target());
                binding = false;

                header.setTitle ("FUNKEY MODE  >  "
                                 + juce::String (FunkeyFamilies::name (slot)));
                header.setSavedMessage ({});
            }

            void paint (juce::Graphics& g) override
            { g.fillAll (juce::Colour (0xFF141414)); }

            void resized() override
            {
                auto b = getLocalBounds().reduced (8);
                header.setBounds (b.removeFromTop (34));
                b.removeFromTop (6);
                fx.setBounds (b);
            }

        private:
            /** The family preset this window is bound to. */
            FamilyFxParams& target() const
            { return GlobalMacros::get().family (slot); }

            FamilyFxWindow&   owner;
            MacroEditorHeader header;
            EffectsPanel      fx;
            int  slot    = 0;
            bool binding = false;   // suppresses onAnythingChanged while seeding

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
        };

        Content* content = nullptr;
        int      slot    = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FamilyFxWindow)
    };

    //==========================================================================
    // BigDrumsFxPanel — the drum rack, laid out in the same two-frame language
    // as EffectsPanel (main area + selector strip).
    //
    // MAPPING: every slider is 0..100 with step 1, converted with the SAME
    // formulas DrumsPopup::Map uses — including the piecewise EQ law where 50 is
    // unity, 0 is -60 dB and 100 is +20 dB.  That is deliberate and load-bearing:
    // a number typed into the kit editor and the same number here must mean the
    // same decibel, or a Big Drums preset would not sound like the kit rack it
    // replaces.  DrumsPopup::Map is private to that class, so the formulas are
    // mirrored rather than shared — keep the two in step if either changes.
    //==========================================================================
    class BigDrumsFxPanel : public juce::Component
    {
    public:
        std::function<void()> onAnythingChanged;

        enum Tab { TabEq = 0, TabSat, TabComp, TabRev, TabDel, TabPan, NumTabs };

        BigDrumsFxPanel()
        {
            static const char* tabNames[NumTabs] =
                { "EQ", "SAT", "COMP", "REV", "DEL", "PAN" };

            for (int t = 0; t < NumTabs; ++t)
            {
                tabBtn[t].setButtonText (tabNames[t]);
                tabBtn[t].onClick = [this, t] { setTab (t); };
                addAndMakeVisible (tabBtn[t]);
            }

            // Per-stage enables (PAN has none — it is placement, always active).
            for (int e = 0; e < kNumEnables; ++e)
            {
                enBtn[e].onClick = [this, e]
                {
                    enOn[e] = ! enOn[e];
                    refreshEnable (e);
                    notify();
                };
                addAndMakeVisible (enBtn[e]);
            }

            // ── Delay time source: free ms, or synced to the host BPM ─────────
            syncBtn.onClick = [this] { delSyncOn = ! delSyncOn; refreshDelayRow(); notify(); };
            addAndMakeVisible (syncBtn);

            sig44Btn.setButtonText ("4/4");
            sig34Btn.setButtonText ("3/4");
            sig44Btn.onClick = [this] { setTimeSig (0); };
            sig34Btn.onClick = [this] { setTimeSig (1); };
            addAndMakeVisible (sig44Btn);
            addAndMakeVisible (sig34Btn);

            for (int d = 0; d < 5; ++d)
            {
                divBtn[d].onClick = [this, d] { delDivIdx = d; refreshDelayRow(); notify(); };
                addAndMakeVisible (divBtn[d]);
            }

            for (auto* s : everySlider())
            {
                s->setStep (1.0f);
                s->onChange = [this] (float) { notify(); };
                addAndMakeVisible (*s);
            }

            // ── The rest of the melodic reverb, now that the bus runs it ──────
            revBand.onChange = [this] (float, float) { notify(); };
            addAndMakeVisible (revBand);

            algoBtn.onClick = [this]
            {
                revAlgoIdx = revAlgoIdx == 0 ? 1 : 0;
                refreshAlgoBtn();
                notify();
            };
            addAndMakeVisible (algoBtn);
            refreshAlgoBtn();

            revWet.onLeftDoubleClick = [this] { showWetBaseDialog (true);  };
            delWet.onLeftDoubleClick = [this] { showWetBaseDialog (false); };

            // The rack dry/wet is the one control that gets nudged constantly
            // while balancing, and a 160 px-wide column gave it a stub of
            // vertical travel.  Horizontal turns the strip's full width into
            // travel instead.
            wetS.setHorizontal (true);

            refreshTabButtons();
            for (int e = 0; e < kNumEnables; ++e) refreshEnable (e);
            showTab (TabEq);
        }

        void loadFrom (const BigDrumsFxParams& p)
        {
            seeding = true;

            for (int i = 0; i < 10; ++i) eqS[i].setValue (eqDbToSlider (p.eqGainDb[i]));

            satDrive.setValue (toSlider (p.satDrive, 0.0f, 1.0f));
            satMix  .setValue (toSlider (p.satMix,   0.0f, 1.0f));

            compThresh.setValue (toSlider (p.compThreshDb,  -60.0f,    0.0f));
            compRatio .setValue (toSlider (p.compRatio,       1.0f,   20.0f));
            compAtk   .setValue (toSlider (p.compAttackMs,    0.1f,  200.0f));
            compRel   .setValue (toSlider (p.compReleaseMs,   5.0f, 2000.0f));
            compMakeup.setValue (toSlider (p.compMakeupDb,    0.0f,   24.0f));

            revSize.setValue (toSlider (p.revSize, 0.0f, 1.0f));
            revDamp.setValue (toSlider (p.revDamp, 0.0f, 1.0f));
            revWet .setValue (toSlider (p.revWet,  0.0f, 1.0f));
            revDry .setValue (toSlider (p.revDry,  0.0f, 1.0f));
            revTail.setValue (toSlider (p.revTail, 0.0f, 1.0f));
            revPre .setValue (toSlider (p.revPreDelay, 0.0f, 1.0f));
            revBand.setValues (p.revHpNorm, p.revLpNorm, false /* no notify */);
            revAlgoIdx = juce::jlimit (0, 1, p.revAlgo);
            refreshAlgoBtn();
            revWetBase = p.revWetBase;
            delWetBase = p.delWetBase;

            delSyncOn     = p.delSync;
            delTimeSigIdx = juce::jlimit (0, 1, p.delTimeSig);
            delDivIdx     = juce::jmax  (0,    p.delDiv);
            refreshDelayRow();

            delTime.setValue (toSlider (p.delTimeMs, 1.0f, 2000.0f));
            delFb  .setValue (toSlider (p.delFb,     0.0f,    0.95f));
            delDry .setValue (toSlider (p.delDry,    0.0f,    1.0f));
            delWet .setValue (toSlider (p.delWet,    0.0f,    1.0f));

            panS.setValue (p.pan * 50.0f + 50.0f);
            wetS.setValue (toSlider (p.fxWet, 0.0f, 1.0f));

            enOn[TabEq]   = p.eqEnabled;   enOn[TabSat] = p.satEnabled;
            enOn[TabComp] = p.compEnabled; enOn[TabRev] = p.revEnabled;
            enOn[TabDel]  = p.delEnabled;
            for (int e = 0; e < kNumEnables; ++e) refreshEnable (e);

            seeding = false;
        }

        void readInto (BigDrumsFxParams& p) const
        {
            for (int i = 0; i < 10; ++i) p.eqGainDb[i] = sliderToEqDb (eqS[i].getValue());

            p.satDrive = toEngine (satDrive.getValue(), 0.0f, 1.0f);
            p.satMix   = toEngine (satMix  .getValue(), 0.0f, 1.0f);

            p.compThreshDb  = toEngine (compThresh.getValue(), -60.0f,    0.0f);
            p.compRatio     = toEngine (compRatio .getValue(),   1.0f,   20.0f);
            p.compAttackMs  = toEngine (compAtk   .getValue(),   0.1f,  200.0f);
            p.compReleaseMs = toEngine (compRel   .getValue(),   5.0f, 2000.0f);
            p.compMakeupDb  = toEngine (compMakeup.getValue(),   0.0f,   24.0f);

            p.revSize = toEngine (revSize.getValue(), 0.0f, 1.0f);
            p.revDamp = toEngine (revDamp.getValue(), 0.0f, 1.0f);
            p.revWet  = toEngine (revWet .getValue(), 0.0f, 1.0f);
            p.revDry  = toEngine (revDry .getValue(), 0.0f, 1.0f);
            p.revTail = toEngine (revTail.getValue(), 0.0f, 1.0f);
            p.revPreDelay = toEngine (revPre.getValue(), 0.0f, 1.0f);
            p.revHpNorm   = revBand.getLo();
            p.revLpNorm   = revBand.getHi();
            p.revAlgo     = revAlgoIdx;
            p.revWetBase  = revWetBase;
            p.delWetBase  = delWetBase;

            p.delSync    = delSyncOn;
            p.delTimeSig = delTimeSigIdx;
            p.delDiv     = delDivIdx;
            p.delTimeMs  = toEngine (delTime.getValue(), 1.0f, 2000.0f);
            p.delFb     = toEngine (delFb  .getValue(), 0.0f,    0.95f);
            p.delDry    = toEngine (delDry .getValue(), 0.0f,    1.0f);
            p.delWet    = toEngine (delWet .getValue(), 0.0f,    1.0f);

            p.pan   = (float) (panS.getValue() - 50.0) / 50.0f;
            p.fxWet = toEngine (wetS.getValue(), 0.0f, 1.0f);

            p.eqEnabled   = enOn[TabEq];   p.satEnabled = enOn[TabSat];
            p.compEnabled = enOn[TabComp]; p.revEnabled = enOn[TabRev];
            p.delEnabled  = enOn[TabDel];
        }

        void paint (juce::Graphics& g) override
        {
            static const char* frameTitles[NumTabs] =
                { "10-BAND EQ", "SATURATION", "COMPRESSOR", "REVERB", "DELAY", "PAN" };

            const auto fr = frames();
            InstrEditStyle::paintComponentFrame (g, fr.first,  frameTitles[currentTab]);
            InstrEditStyle::paintComponentFrame (g, fr.second, "RACK");
        }

        void resized() override
        {
            const auto fr = frames();
            auto mainInner  = InstrEditStyle::componentFrameContent (fr.first);
            auto stripInner = InstrEditStyle::componentFrameContent (fr.second);

            // Strip: the six tab buttons, with the global rack WET beneath them
            // as a horizontal fader (short and wide, so the strip's width is
            // the travel).
            auto wetBox = stripInner.removeFromBottom (74);
            stripInner.removeFromBottom (6);
            wetS.setBounds (wetBox);

            const int btnH = (stripInner.getHeight() - (NumTabs - 1) * 4) / NumTabs;
            for (int t = 0; t < NumTabs; ++t)
                tabBtn[t].setBounds (stripInner.getX(),
                                     stripInner.getY() + t * (btnH + 4),
                                     stripInner.getWidth(), btnH);

            // Main area: enable toggle across the top (except PAN), knobs under.
            auto area = mainInner;
            const int enH = 26;
            auto enRow = area.removeFromTop (enH);
            area.removeFromTop (8);
            for (int e = 0; e < kNumEnables; ++e)
                enBtn[e].setBounds (enRow.getX(), enRow.getY(), 90, enH);

            // DELAY gets one extra row above its knobs: SYNC, the time
            // signature pair, and the divisions — the same set the melodic
            // delay offers, so a kit and an instrument can be locked to the
            // same musical value.
            if (currentTab == TabRev)
            {
                auto row = area.removeFromTop (26);
                area.removeFromTop (8);
                algoBtn.setBounds (row.removeFromLeft (104).reduced (0, 1));
            }

            if (currentTab == TabDel)
            {
                auto row = area.removeFromTop (26);
                area.removeFromTop (8);

                syncBtn .setBounds (row.removeFromLeft (76).reduced (0, 1));
                row.removeFromLeft (6);
                sig44Btn.setBounds (row.removeFromLeft (52).reduced (0, 1));
                sig34Btn.setBounds (row.removeFromLeft (52).reduced (0, 1));
                row.removeFromLeft (10);

                const auto& divs = divTable();
                const int nd = (int) divs.size();
                const int dw = juce::jmax (34, row.getWidth() / juce::jmax (1, nd));
                for (int d = 0; d < 5; ++d)
                {
                    if (d < nd)
                    {
                        divBtn[d].setButtonText (divs[(size_t) d]);
                        divBtn[d].setBounds (row.getX() + d * dw, row.getY(),
                                             dw - 2, row.getHeight());
                    }
                    else divBtn[d].setBounds (0, 0, 0, 0);
                }
            }

            layoutRow (area, { &eqS[0], &eqS[1], &eqS[2], &eqS[3], &eqS[4],
                               &eqS[5], &eqS[6], &eqS[7], &eqS[8], &eqS[9] }, 10);
            layoutRow (area, { &satDrive, &satMix }, 2);
            layoutRow (area, { &compThresh, &compRatio, &compAtk, &compRel, &compMakeup }, 5);
            // BAND is a two-thumb widget, not a GoldSlider, so it gets its own
            // column off the right of the REV row before layoutRow divides up
            // what is left.
            if (currentTab == TabRev)
            {
                const int bandW = juce::jmax (56, area.getWidth() / 8);
                revBand.setBounds (area.removeFromRight (bandW).reduced (2, 0));
            }
            layoutRow (area, { &revSize, &revDamp, &revWet, &revDry, &revTail, &revPre }, 6);
            layoutRow (area, { &delTime, &delFb, &delDry, &delWet }, 4);

            // PAN sits alone, centred at a third of the width.
            const int pw = juce::jmax (60, area.getWidth() / 3);
            panS.setBounds (area.getX() + (area.getWidth() - pw) / 2,
                            area.getY(), pw, area.getHeight());
        }

    private:
        //----------------------------------------------------------------------
        // Mirror of DrumsPopup::Map — see the class note above.
        //----------------------------------------------------------------------
        static float toSlider (float engineVal, float lo, float hi)
        {
            if (hi == lo) return 0.0f;
            return juce::jlimit (0.0f, 100.0f, 100.0f * (engineVal - lo) / (hi - lo));
        }
        static float toEngine (float sliderVal, float lo, float hi)
        {
            return lo + juce::jlimit (0.0f, 100.0f, sliderVal) / 100.0f * (hi - lo);
        }
        static float sliderToEqDb (float v)
        {
            v = juce::jlimit (0.0f, 100.0f, v);
            return v <= 50.0f ? -60.0f + (v / 50.0f) * 60.0f
                              : ((v - 50.0f) / 50.0f) * 20.0f;
        }
        static float eqDbToSlider (float db)
        {
            return db <= 0.0f ? juce::jlimit (0.0f,  50.0f,  50.0f + db * (50.0f / 60.0f))
                              : juce::jlimit (50.0f, 100.0f, 50.0f + db * (50.0f / 20.0f));
        }

        std::vector<GoldSlider*> everySlider()
        {
            std::vector<GoldSlider*> v {
                &satDrive, &satMix,
                &compThresh, &compRatio, &compAtk, &compRel, &compMakeup,
                &revSize, &revDamp, &revWet, &revDry, &revTail, &revPre,
                &delTime, &delFb, &delDry, &delWet,
                &panS, &wetS
            };
            for (int i = 0; i < 10; ++i) v.push_back (&eqS[i]);
            return v;
        }

        std::pair<juce::Rectangle<int>, juce::Rectangle<int>> frames() const
        {
            const int W = getWidth(), H = getHeight();
            const int pad = 6, gap = 8, stripW = 160;
            const int mainW = W - stripW - 2 * pad - gap;
            return { { pad, 0, mainW, H }, { pad + mainW + gap, 0, stripW, H } };
        }

        void layoutRow (juce::Rectangle<int> area,
                        std::vector<GoldSlider*> sliders, int perRow)
        {
            const int n = (int) sliders.size();
            if (n == 0) return;
            const int pr = juce::jmax (1, perRow);
            const int w  = area.getWidth() / pr;
            for (int i = 0; i < n; ++i)
                sliders[(size_t) i]->setBounds (area.getX() + i * w, area.getY(),
                                                w - 4, area.getHeight());
        }

        void setTab (int t)
        {
            currentTab = juce::jlimit (0, (int) NumTabs - 1, t);
            refreshTabButtons();
            showTab (currentTab);
            resized();
            repaint();
        }

        void showTab (int t)
        {
            for (int i = 0; i < 10; ++i) eqS[i].setVisible (t == TabEq);

            satDrive.setVisible (t == TabSat);  satMix.setVisible (t == TabSat);

            const bool comp = (t == TabComp);
            compThresh.setVisible (comp); compRatio.setVisible (comp);
            compAtk   .setVisible (comp); compRel  .setVisible (comp);
            compMakeup.setVisible (comp);

            const bool rev = (t == TabRev);
            revSize.setVisible (rev); revDamp.setVisible (rev); revWet.setVisible (rev);
            revDry .setVisible (rev); revTail.setVisible (rev); revPre.setVisible (rev);
            revBand.setVisible (rev); algoBtn.setVisible (rev);

            const bool del = (t == TabDel);
            delTime.setVisible (del); delFb.setVisible (del);
            delDry .setVisible (del); delWet.setVisible (del);
            syncBtn .setVisible (del);
            sig44Btn.setVisible (del);
            sig34Btn.setVisible (del);
            for (int d = 0; d < 5; ++d)
                divBtn[d].setVisible (del && d < (int) divTable().size());
            if (del) refreshDelayRow();

            panS.setVisible (t == TabPan);

            // One enable toggle is on screen at a time — the active stage's.
            for (int e = 0; e < kNumEnables; ++e) enBtn[e].setVisible (e == t);
        }

        /** Lit = PLATE.  Unlit = HALL/ROOM, which is where every existing
            preset already sits, so nothing moves by being drawn. */
        void refreshAlgoBtn()
        {
            algoBtn.setButtonText (revAlgoIdx == 1 ? "PLATE" : "HALL");
            InstrEditStyle::styleSquareButton (algoBtn, revAlgoIdx == 1);
        }

        //----------------------------------------------------------------------
        // WET BASE GAIN - LEFT double-click on either WET slider.  Same wording,
        // same range and same gesture as EffectsPanel's and the kit rack's: one
        // control, three places, one meaning.
        //----------------------------------------------------------------------
        void showWetBaseDialog (bool isReverb)
        {
            const float current = isReverb ? revWetBase : delWetBase;

            auto* w = new juce::AlertWindow (
                isReverb ? "REVERB WET BASE" : "DELAY WET BASE",
                juce::String (isReverb ? "What full WET is worth on the Big Drums reverb.\n"
                                       : "What full WET is worth on the Big Drums delay.\n")
                    + "100 = the slider means what it says. 50 = half.\n"
                    + "Saved with the Big Drums preset. Range 0 to 200.",
                juce::MessageBoxIconType::NoIcon);

            w->addTextEditor ("pct", juce::String (current * 100.0f, 1), "%:");
            w->addButton ("SAVE",   1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            juce::Component::SafePointer<BigDrumsFxPanel> safe (this);

            w->enterModalState (true, juce::ModalCallbackFunction::create (
                [safe, w, isReverb] (int result) mutable
                {
                    std::unique_ptr<juce::AlertWindow> owned (w);
                    if (result != 1) return;

                    auto* pnl = safe.getComponent();
                    if (pnl == nullptr) return;

                    const float v = juce::jlimit (0.0f, 200.0f,
                                        w->getTextEditorContents ("pct").getFloatValue()) * 0.01f;

                    if (isReverb) pnl->revWetBase = v;
                    else          pnl->delWetBase = v;

                    pnl->notify();
                }), false);
        }

        const std::vector<juce::String>& divTable() const
        {
            static const std::vector<juce::String> k44 { "1/1","1/2","1/4","1/8","1/16" };
            static const std::vector<juce::String> k34 { "1/1","1/3","1/6","1/12" };
            return delTimeSigIdx == 0 ? k44 : k34;
        }

        void setTimeSig (int ts)
        {
            delTimeSigIdx = juce::jlimit (0, 1, ts);
            delDivIdx     = 0;              // tables differ; index 0 is valid in both
            refreshDelayRow();
            resized();
            notify();
        }

        /** Light the delay row and make the inactive time source visibly inert:
            with SYNC on the free TIME knob does nothing, with SYNC off the
            divisions do — showing both live would promise two time sources at
            once. */
        void refreshDelayRow()
        {
            syncBtn.setButtonText (delSyncOn ? "SYNC" : "FREE");
            InstrEditStyle::styleSquareButton (syncBtn, delSyncOn);
            InstrEditStyle::styleSquareButton (sig44Btn, delTimeSigIdx == 0);
            InstrEditStyle::styleSquareButton (sig34Btn, delTimeSigIdx == 1);

            const int nd = (int) divTable().size();
            delDivIdx = juce::jlimit (0, juce::jmax (0, nd - 1), delDivIdx);
            for (int d = 0; d < 5; ++d)
                InstrEditStyle::styleSquareButton (divBtn[d], d == delDivIdx);

            setInert (delTime, delSyncOn);
            for (auto* b : { &sig44Btn, &sig34Btn }) setInert (*b, ! delSyncOn);
            for (int d = 0; d < 5; ++d) setInert (divBtn[d], ! delSyncOn);
        }

        static void setInert (juce::Component& c, bool inert)
        {
            c.setAlpha (inert ? 0.35f : 1.0f);
            c.setInterceptsMouseClicks (! inert, ! inert);
        }

        void refreshTabButtons()
        {
            for (int t = 0; t < NumTabs; ++t)
                InstrEditStyle::styleSquareButton (tabBtn[t], t == currentTab);
        }

        void refreshEnable (int e)
        {
            enBtn[e].setButtonText (enOn[e] ? "ON" : "OFF");
            InstrEditStyle::styleSquareButton (enBtn[e], enOn[e]);
        }

        void notify()
        {
            if (seeding) return;
            if (onAnythingChanged) onAnythingChanged();
        }

        static constexpr int kNumEnables = 5;   // EQ / SAT / COMP / REV / DEL

        juce::TextButton tabBtn[NumTabs];
        juce::TextButton enBtn[kNumEnables];

        // Delay time source.
        juce::TextButton syncBtn, sig44Btn, sig34Btn, divBtn[5];
        bool             delSyncOn     = false;
        int              delTimeSigIdx = 0;
        int              delDivIdx     = 2;
        bool             enOn [kNumEnables] { false, false, false, false, false };
        int              currentTab = TabEq;
        bool             seeding    = false;

        GoldSlider eqS[10] {
            { "31",  0.0f, 100.0f, 50.0f, "" }, { "62",  0.0f, 100.0f, 50.0f, "" },
            { "125", 0.0f, 100.0f, 50.0f, "" }, { "250", 0.0f, 100.0f, 50.0f, "" },
            { "500", 0.0f, 100.0f, 50.0f, "" }, { "1k",  0.0f, 100.0f, 50.0f, "" },
            { "2k",  0.0f, 100.0f, 50.0f, "" }, { "4k",  0.0f, 100.0f, 50.0f, "" },
            { "8k",  0.0f, 100.0f, 50.0f, "" }, { "16k", 0.0f, 100.0f, 50.0f, "" }
        };

        GoldSlider satDrive   { "DRIVE",  0.0f, 100.0f,   0.0f, "" };
        GoldSlider satMix     { "MIX",    0.0f, 100.0f, 100.0f, "" };

        GoldSlider compThresh { "THRESH", 0.0f, 100.0f, 100.0f, "" };
        GoldSlider compRatio  { "RATIO",  0.0f, 100.0f,   0.0f, "" };
        GoldSlider compAtk    { "ATK",    0.0f, 100.0f,   2.0f, "" };
        GoldSlider compRel    { "REL",    0.0f, 100.0f,   2.0f, "" };
        GoldSlider compMakeup { "MAKEUP", 0.0f, 100.0f,   0.0f, "" };

        GoldSlider revSize    { "SIZE",   0.0f, 100.0f,  50.0f, "" };
        GoldSlider revDamp    { "DAMP",   0.0f, 100.0f,  50.0f, "" };
        GoldSlider revWet     { "WET",    0.0f, 100.0f,   0.0f, "" };
        // DRY WAS MISSING FROM THIS RACK ENTIRELY, and that was a live bug -
        // BigDrumsFxParams had no revDry while the kit rack did, and
        // overrideDrumFx copies by name, so engaging Big Drums silently reset
        // every kit's reverb dry to 1.0.
        GoldSlider revDry     { "DRY",    0.0f, 100.0f, 100.0f, "" };
        GoldSlider revTail    { "TAIL",   0.0f, 100.0f,  50.0f, "" };
        GoldSlider revPre     { "PRE",    0.0f, 100.0f,   0.0f, "" };
        BandFilterSlider revBand { "SEND BAND" };
        juce::TextButton algoBtn;          // HALL/ROOM (FDN) <-> PLATE
        int              revAlgoIdx = 0;

        GoldSlider delTime    { "TIME",   0.0f, 100.0f,  12.0f, "" };
        GoldSlider delFb      { "FB",     0.0f, 100.0f,  32.0f, "" };
        GoldSlider delDry     { "DRY",    0.0f, 100.0f, 100.0f, "" };
        GoldSlider delWet     { "WET",    0.0f, 100.0f,   0.0f, "" };

        // What full WET travel is worth.  LEFT double-click on the WET handle.
        float revWetBase = 1.0f;
        float delWetBase = 0.5f;

        GoldSlider panS       { "PAN",    0.0f, 100.0f,  50.0f, "" };
        GoldSlider wetS       { "RACK",   0.0f, 100.0f, 100.0f, "" };   // rack dry/wet

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigDrumsFxPanel)
    };

    //==========================================================================
    // FunkeyModeWindow — the macro's own window.
    //
    // Everything that used to sit on the GLOBAL SETTINGS page: the engage
    // switch and the seven family buttons.  The page could not give it room
    // without crowding the other two macros, and a macro with seven editors
    // behind it deserves a window rather than a strip.
    //
    // A family button opens FamilyFxWindow on top of this one — two levels, but
    // each level is one decision: WHICH family, then WHAT it sounds like.
    //==========================================================================
    class FunkeyModeWindow : public juce::DocumentWindow
    {
    public:
        std::function<void(bool)> onToggled;      // engage switch moved
        std::function<void(int)>  onFamilyPicked; // open that family's editor
        std::function<void()>     onClosed;

        FunkeyModeWindow()
            : juce::DocumentWindow ("Funkey Mode", juce::Colour (0xFF1A1A1A),
                                    juce::DocumentWindow::closeButton)
        {
            Betel::applyGrexPopupBehaviour (*this);
            setUsingNativeTitleBar (false);
            setResizable (true, true);
            setResizeLimits (620, 260, 1600, 700);
            content = new Content (*this);
            setContentOwned (content, true);
            Betel::centreGrexPopupOnScreen (*this, 760, 320);
        }

        ~FunkeyModeWindow() override { clearContentComponent(); }

        void showCentredOver (juce::Component* parent)
        {
            content->refresh();
            if (parent != nullptr)
            {
                const auto pb = parent->getScreenBounds();
                setBounds (pb.getCentreX() - 380, pb.getCentreY() - 160, 760, 320);
            }
            setVisible (true);
            toFront (true);
        }

        void refresh() { if (content != nullptr) content->refresh(); }

        void closeButtonPressed() override
        {
            setVisible (false);
            if (onClosed) onClosed();
        }

    private:
        class Content : public juce::Component
        {
        public:
            explicit Content (FunkeyModeWindow& owner_) : owner (owner_)
            {
                setOpaque (true);

                header.setTitle ("FUNKEY MODE");
                header.onSave = [this]
                {
                    const bool ok = GlobalMacros::get().saveFunkey();
                    header.setSavedMessage (ok ? "saved to grex_funkey.xml" : "SAVE FAILED");
                };
                addAndMakeVisible (header);

                onBtn.onClick = [this]
                {
                    const bool next = ! GlobalMacros::get().isFunkeyOn();
                    if (owner.onToggled) owner.onToggled (next);
                    refresh();
                };
                addAndMakeVisible (onBtn);

                for (int f = 0; f < FunkeyFamilies::Count; ++f)
                {
                    famBtn[f].setButtonText (shortName (f));
                    InstrEditStyle::styleSquareButton (famBtn[f], false);
                    famBtn[f].onClick = [this, f]
                    { if (owner.onFamilyPicked) owner.onFamilyPicked (f); };
                    addAndMakeVisible (famBtn[f]);
                }

                hint.setText ("Each family button opens its own six-stage FX chain. "
                              "SAVE writes all seven.",
                              juce::dontSendNotification);
                hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
                hint.setFont (juce::Font (13.0f, juce::Font::plain));
                addAndMakeVisible (hint);

                refresh();
            }

            void refresh()
            {
                const bool on = GlobalMacros::get().isFunkeyOn();
                onBtn.setButtonText (on ? "ON" : "OFF");
                onBtn.setColour (juce::TextButton::buttonColourId,
                                 on ? juce::Colour (0xFFCC6600) : juce::Colour (0xFF2A2A2A));
                onBtn.setColour (juce::TextButton::textColourOffId,
                                 on ? juce::Colours::black : juce::Colours::white.withAlpha (0.75f));
                repaint();
            }

            void paint (juce::Graphics& g) override
            { g.fillAll (juce::Colour (0xFF141414)); }

            void resized() override
            {
                auto b = getLocalBounds().reduced (10);
                header.setBounds (b.removeFromTop (34));
                b.removeFromTop (8);

                auto row = b.removeFromTop (44);
                onBtn.setBounds (row.removeFromLeft (150));
                row.removeFromLeft (12);
                hint.setBounds (row);

                b.removeFromTop (12);

                const int n  = FunkeyFamilies::Count;
                const int bw = (b.getWidth() - (n - 1) * 6) / n;
                const int bh = juce::jmin (b.getHeight(), 96);
                for (int f = 0; f < n; ++f)
                    famBtn[f].setBounds (b.getX() + f * (bw + 6), b.getY(), bw, bh);
            }

        private:
            static juce::String shortName (int slot)
            {
                switch (slot)
                {
                    case FunkeyFamilies::Piano:      return "PIANO";
                    case FunkeyFamilies::ChromPerc:  return "CHR.PERC";
                    case FunkeyFamilies::Organ:      return "ORGAN";
                    case FunkeyFamilies::Guitar:     return "GUITAR";
                    case FunkeyFamilies::SynthLead:  return "SYN.LEAD";
                    case FunkeyFamilies::Ethnic:     return "ETHNIC";
                    case FunkeyFamilies::Percussive: return "PERCUSS.";
                    default:                         return "?";
                }
            }

            FunkeyModeWindow& owner;
            MacroEditorHeader header;
            juce::TextButton  onBtn;
            juce::TextButton  famBtn[FunkeyFamilies::Count];
            juce::Label       hint;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
        };

        Content* content = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FunkeyModeWindow)
    };

    //==========================================================================
    // BigDrumsFxWindow
    //==========================================================================
    class BigDrumsFxWindow : public juce::DocumentWindow
    {
    public:
        std::function<void()>     onChanged;
        std::function<void(bool)> onToggled;   // engage switch moved in from the page
        std::function<void()>     onClosed;

        BigDrumsFxWindow()
            : juce::DocumentWindow ("Big Drums - Global Drum FX",
                                    juce::Colour (0xFF1A1A1A),
                                    juce::DocumentWindow::closeButton)
        {
            Betel::applyGrexPopupBehaviour (*this);
            setUsingNativeTitleBar (false);
            setResizable (true, true);
            setResizeLimits (900, 470, 2200, 1200);
            content = new Content (*this);
            setContentOwned (content, true);
            Betel::centreGrexPopupOnScreen (*this, 1120, 560);
        }

        ~BigDrumsFxWindow() override { clearContentComponent(); }

        void showCentredOver (juce::Component* parent)
        {
            content->bind();
            content->refreshEngage();

            if (parent != nullptr)
            {
                const auto pb = parent->getScreenBounds();
                const int w = juce::jlimit (900, 2200, (int) (pb.getWidth()  * 0.80f));
                const int h = juce::jlimit (470, 1200, (int) (pb.getHeight() * 0.72f));
                setBounds (pb.getCentreX() - w / 2, pb.getCentreY() - h / 2, w, h);
            }
            setVisible (true);
            toFront (true);
        }

        void closeButtonPressed() override
        {
            setVisible (false);
            if (onClosed) onClosed();
        }

        void refresh() { if (content != nullptr) content->refreshEngage(); }

    private:
        class Content : public juce::Component
        {
        public:
            explicit Content (BigDrumsFxWindow& owner_) : owner (owner_)
            {
                setOpaque (true);

                onBtn.onClick = [this]
                {
                    const bool next = ! GlobalMacros::get().isBigDrumsOn();
                    if (owner.onToggled) owner.onToggled (next);
                    refreshEngage();
                };
                addAndMakeVisible (onBtn);

                header.setTitle ("BIG DRUMS  >  GLOBAL DRUM FX RACK");
                header.onSave = [this]
                {
                    const bool ok = GlobalMacros::get().saveBigDrums();
                    header.setSavedMessage (ok ? "saved to grex_bigdrums.xml"
                                               : "SAVE FAILED");
                };
                addAndMakeVisible (header);

                rack.onAnythingChanged = [this]
                {
                    rack.readInto (GlobalMacros::get().bigDrums());
                    header.setSavedMessage ({});
                    if (owner.onChanged) owner.onChanged();
                };
                addAndMakeVisible (rack);
            }

            void bind()
            {
                rack.loadFrom (GlobalMacros::get().bigDrums());
                header.setSavedMessage ({});
            }

            /** The engage switch lives here rather than on the settings page, so
                turning the macro on and hearing what it does are one gesture in
                one place. */
            void refreshEngage()
            {
                const bool on = GlobalMacros::get().isBigDrumsOn();
                onBtn.setButtonText (on ? "ON" : "OFF");
                onBtn.setColour (juce::TextButton::buttonColourId,
                                 on ? juce::Colour (0xFFCC6600) : juce::Colour (0xFF2A2A2A));
                onBtn.setColour (juce::TextButton::textColourOffId,
                                 on ? juce::Colours::black : juce::Colours::white.withAlpha (0.75f));
                repaint();
            }

            void paint (juce::Graphics& g) override
            { g.fillAll (juce::Colour (0xFF141414)); }

            void resized() override
            {
                auto b = getLocalBounds().reduced (8);

                auto top = b.removeFromTop (34);
                onBtn.setBounds (top.removeFromLeft (110));
                top.removeFromLeft (10);
                header.setBounds (top);

                b.removeFromTop (6);
                rack.setBounds (b);
            }

        private:
            BigDrumsFxWindow& owner;
            juce::TextButton  onBtn;
            MacroEditorHeader header;
            BigDrumsFxPanel   rack;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
        };

        Content* content = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigDrumsFxWindow)
    };
} // namespace Betel



