
#pragma once
#include "BalladaPalette.h"   // Betel::Pal - the pool-blue accent scheme
//==============================================================================
// DrumsPopup.h  —  Phase 3.3 rework.
//
// Layout (1400×650 default):
//
//   ┌───────────────────────────────────────────────────────────────────────┐
//   │ KIT: [Standard ▾]     Editing: SOLO 1 | DRUMS | Standard              │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │  Per-key element selectors — every button is "E <kitAbbrev>"          │
//   │  [E Std][E Pop][E Rk][E Rk][E Std]...                                 │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │  49-key drum keyboard (MIDI 35..83 — B0 to B4)                        │
//   ├──────────────────────────────────────────┬────────────────────────────┤
//   │ PER-KEY  (selected: Kick C1)             │ [EQ][SAT][COMP][REV][DEL] │ ← FX tabs
//   │ ┌──────────────────────────────────────┐ │ ┌────────────────────────┐│
//   │ │ [Gain][Pitch][RR][Atk][Dec][Rel][FX] │ │ │  10-BAND EQ            ││
//   │ │   GoldSliders (0..100, step 1)       │ │ │  [s][s][s][s][s][s]... ││
//   │ │                                      │ │ │                        ││
//   │ └──────────────────────────────────────┘ │ └────────────────────────┘│
//   └──────────────────────────────────────────┴────────────────────────────┘
//
// Highlights of this rework:
//   * All slider controls are GoldSliders (matches the InstrEditorWindow
//     instrument-controls aesthetic) with step = 1 → 0..100 with 101 steps.
//   * Per-effect tabs replace the previous vertical stack.  Only the active
//     effect's controls are visible; each effect sits inside an
//     InstrEditStyle-framed rectangle with its own header.
//   * Title strip uses ASCII separators only (no Unicode middle-dots) so it
//     never renders as garbage on fonts missing those glyphs.
//   * Per-key element-selector buttons are prefixed with "E " so the user
//     knows what they do at a glance (E = Edit Element).
//==============================================================================

#include <JuceHeader.h>
#include <algorithm>            // std::find / std::remove on the key sets
#include <cmath>               // std::abs on floats - the GR meter's change test
#include "GrexPopupWindow.h"   // stays in front, minimisable, never vanishes
#include "InstrEditPanel.h"      // DrumKitParams / DrumElementParams / DrumKitFxParams / GoldSlider / InstrEditStyle
#include "DrumKitRegistry.h"
#include "DrumElementRoles.h"
#include "ChorusConvolver.h"  // Betel::logKnobTaper / logKnobTaperInv — the send curve
#include "InstrumentPreset.h"   // SAVE KIT / LOAD KIT share the .drm format
#include "BetelStateXml.h"     // user-kit (*.bdk) file serialization
#include "DrumSplitEq.h"       // the EQ's slider rule: silence .. unity .. +24 dB

class DrumsPopup : public juce::DocumentWindow
{
public:
    //==========================================================================
    // Callbacks
    //==========================================================================
    /** Read-only access to the drum registry.

        The popup could not previously ask a single structural question about a
        kit - which keys a component covers, what else could go in that folder -
        because it had no handle on the catalog at all.  Every edit was therefore
        stuck at one MIDI key, which is exactly the limitation the component
        selector removes. */
    /** FULL-KIT MODE.

        A sampled kit is one indivisible instrument - it has no component
        folders, no per-role key groups, and no other kit to swap a part with.
        So the editor drops everything that only means something for a COMPOSED
        kit: the component row, REPLACE, and the per-element PITCH / ROUND ROBIN
        / LENGTH sliders.  What is left edits the whole kit at once: GAIN, the
        two-handle filter, and the FX rack.

        Empty string = composed, and the editor behaves as it always has. */
    juce::String fullKitName;

    std::function<const Betel::DrumKitRegistry*()>                        onGetDrumKitRegistry;

    std::function<void(const juce::String& kitName)>                     onLoadKit;
    std::function<void(int midiKey, const juce::String& kitName,
                       uint8_t roleId)>                                  onKeyElementChanged;
    std::function<void(int midiKey, const DrumElementParams& params)>    onKeyParamsChanged;
    std::function<void(const DrumKitFxParams& fx)>                       onKitFxChanged;
    /** Per-sound calibration trim for this kit, as a percent of unity (100 =
        unity).  Deliberately NOT part of DrumKitFxParams: it must survive a Big
        Drums macro replacing the whole rack, because a calibration is not an
        effect. */
    std::function<void(float gainPercent)>                               onKitGainChanged;
    /** Left double-click on the kit GAIN handle — the base-unity dialog. */
    std::function<void()>                                                onBaseUnityRequested;
    /** Fires when a user-kit file is loaded — the whole kit must be applied to
        the engine (re-resolving samples), not just incremental params. */
    std::function<void(const DrumKitParams& kit)>                        onApplyUserKit;
    /** Right-click on a key in the editor keyboard — audition that drum sound. */
    std::function<void(int midiKey)>                                     onAuditionKey;

    // ── Piano strip (bottom of the window) ───────────────────────────────────
    // Plays the drum slot, and shows what the STYLE is playing on it — including
    // MIDI 24..34, which the key-select keyboard above (starting at 35) hides.
    std::function<void(int note, int velocity)>                          onKeyboardNoteOn;
    std::function<void(int note)>                                        onKeyboardNoteOff;
    std::function<void(uint32_t*)>                                       onQuerySoundingNotes;
    /** USER SFZ - the same pair the instrument editors carry.

        A drum SFZ replaces the whole kit, so it only behaves if the patch
        follows the GM drum map: the cells here address MIDI keys, and a patch
        that maps them elsewhere will simply not sound on the ones it skipped. */
    std::function<void()>     onSfzLoadRequested;
    std::function<void(bool)> onSfzToggled;

    std::function<void()>                                                onSaveAsDefault; // "SAVE AS DEFAULT"
    std::function<void()>                                                onClosed;

    DrumsPopup()
        : juce::DocumentWindow ("Drums Kit Editor",
                                juce::Colour (0xFF1A1A1A),
                                juce::DocumentWindow::closeButton)
    {
        Betel::applyGrexPopupBehaviour (*this);
        setUsingNativeTitleBar (false);
        setResizable (true, true);
        // The piano strip costs kStripH (54) + a 6 px gap; the window grew by
        // exactly 60 so the editor above it keeps the height it always had.
        setResizeLimits (1000, 620, 2200, 1400);   // min H was 560 (+60)
        content = new Content (*this);
        setContentOwned (content, true);
        // 1400 wide is more than a 13" MacBook Air has in total (1280 points),
        // so this used to open off both edges there.  Clamped to the usable
        // screen; the 2200x1400 resize ceiling above is unchanged.
        Betel::centreGrexPopupOnScreen (*this, 1400, 710);   // was 650 (+60)
    }

    ~DrumsPopup() override { clearContentComponent(); }

    void openCentredOver (juce::Component* parent, float scale)
    {
        if (parent != nullptr)
        {
            const auto pb = parent->getScreenBounds();
            const int w = juce::jlimit (1000, 2200, (int) (pb.getWidth()  * scale));
            const int h = juce::jlimit (620,  1400, (int) (pb.getHeight() * scale));
            setBounds (pb.getCentreX() - w / 2, pb.getCentreY() - h / 2, w, h);
        }
        setVisible (true);
        toFront (true);
    }

    void setDrumKitRegistry (Betel::DrumKitRegistry* r) { content->setRegistry (r); }
    void setDrumKitParams   (const DrumKitParams& kit)  { content->setKitState (kit); }
    /** Seed the kit trim.  It lives in SlotParams, not DrumKitParams, so the
        host pushes it separately from setDrumKitParams. */
    void setKitGainPercent  (float gainPercent)         { content->setKitGainPercent (gainPercent); }
    void setSlotLabel       (const juce::String& s)     { content->setSlotLabel (s); }

    /** WHICH OF THE TWO EDITORS TO BE.

        Empty = a COMPOSED kit, and the window is the component editor it has
        always been.  Non-empty = a SAMPLED kit, named, and the window drops
        everything that only means something for a composed one.

        Must be pushed on EVERY open and on every path that can change which
        kind of kit a slot holds - assigning fullKitName directly, which is what
        the host used to do at construction time, latches the editor for the
        life of the plugin. */
    void setFullKitName (const juce::String& kitName)
    {
        if (content != nullptr) content->setFullKit (kitName);
        else                    fullKitName = kitName;
    }

    /** Host -> window: forwarded to the content. */
    void setSfzState (const juce::String& name, bool active)
    {
        if (content != nullptr) content->setSfzState (name, active);
    }

    /** Host -> window: what the SWEETENER is doing on the edited drum slot, in
        dB of gain reduction per stage { SOFTEN, PEAK, TAME, ROUND }.  Pushed
        from the host's UI timer; the content drops anything that does not move
        a bar, so sending it every tick is cheap. */
    void setSweetenerGr (float softenDb, float peakDb, float tameDb, float roundDb)
    {
        if (content != nullptr) content->setSweetenerGr (softenDb, peakDb, tameDb, roundDb);
    }

    void closeButtonPressed() override
    {
        setVisible (false);
        if (onClosed) onClosed();
    }

private:
    //==========================================================================
    // Keyboard range + fixed 9-kit list (must match SoundsTab::kDrumKitNames
    // and SamplePlayerEngine's default PC map at PCs 0/8/16/24/25/32/40/48/56).
    //==========================================================================
    static constexpr int kKeyFirst = 35;       // B0 — extra-low kick
    static constexpr int kKeyCount = 49;       // 4 octaves + one extra (B0..B4)

    static constexpr int kNumKits = 9;
    static constexpr const char* kKitNames [kNumKits] = {
        "Standard", "Room",  "Power",  "Electronic",
        "TR-808",   "Jazz",  "Brush",  "Orchestra",
        "SFX"
    };
    /** Registry keys, parallel to kKitNames.  The registry catalogs a kit by the
        NUMERIC prefix of its files (kick/016_power.frb -> "016"), while the UI
        and DrumKitParams::lastLoadedKit carry the display name.  Both spellings
        are in circulation, so the lookup below accepts either. */
    static constexpr const char* kKitKeys [kNumKits] = {
        "000", "008", "016", "024",
        "025", "032", "040", "048",
        "056"
    };

    /** Display name or registry key in, registry key out. */
    static juce::String registryKeyLike (const juce::String& kit)
    {
        const auto k = kit.trim();
        for (int i = 0; i < kNumKits; ++i)
            if (k.equalsIgnoreCase (kKitNames[i]) || k.equalsIgnoreCase (kKitKeys[i]))
                return kKitKeys[i];
        return k.toLowerCase();      // a global component names itself
    }

    static constexpr const char* kKitAbbrev [kNumKits] = {
        "Std", "Rm",  "Pwr", "El",
        "808", "Jz",  "Br",  "Or",
        "SFX"
    };

    //==========================================================================
    // 0..100 ↔ engine-domain mapping helpers.
    //
    // Every UI slider in this popup is a GoldSlider with range 0..100 and
    // step 1 (so 101 discrete positions).  These free functions convert to
    // and from the actual engine units stored in DrumKitParams.
    //==========================================================================
    struct Map
    {
        // Linear two-way map between [0..100] and [lo..hi].
        static float toSlider (float engineVal, float lo, float hi)
        {
            if (hi == lo) return 0.0f;
            return juce::jlimit (0.0f, 100.0f,
                                 100.0f * (engineVal - lo) / (hi - lo));
        }
        static float toEngine (float sliderVal, float lo, float hi)
        {
            return lo + juce::jlimit (0.0f, 100.0f, sliderVal) / 100.0f * (hi - lo);
        }

        // EQ bands: THE SLIDER RULE, one definition for every drum EQ slider
        // (DrumSplitEq.h) - the bottom half is silence .. unity, the top half
        // unity .. +24 dB.
        //   slider  0   -> the band muted
        //   slider  50  -> unity
        //   slider  100 -> +24 dB
        // Each slider is the volume of its own band - the EQ is a real split.
        static float sliderToEqDb (float v)
        {
            return Betel::DrumSplitEq::dbFromPosition (juce::jlimit (0.0f, 100.0f, v) / 100.0f);
        }
        static float eqDbToSlider (float db)
        {
            return 100.0f * Betel::DrumSplitEq::positionFromDb (db);
        }

        // Per-key Pitch:  slider 0..100  ↔  -24..+24 semitones (unity at 50)
        static float sliderToPitch (float v) { return ((v - 50.0f) / 50.0f) * 24.0f; }
        static float pitchToSlider (float st){ return 50.0f + (st / 24.0f) * 50.0f; }

        // Per-key Gain:  slider 0..100  ↔  0..2 linear (unity at 50)
        static float sliderToGain (float v) { return v * 0.02f; }
        static float gainToSlider (float g) { return g * 50.0f; }

        // Per-key Round-Robin mode: 4 discrete states quantised from 0..100.
        // 0..24 → 0, 25..49 → 1, 50..74 → 2, 75..100 → 3
        // RR IS AN AMOUNT NOW, so these are the identity.  They used to fold
        // the slider's 0..100 into a 4-position MODE, which is why pushing it
        // high felt like it should do more than it did - three quarters of the
        // travel selected the same mode as the quarter before it, and no mode
        // was implemented at render anyway.
        static int   sliderToRr (float v) { return juce::jlimit (0, 100, (int) std::round (v)); }
        static float rrToSlider (int rr)  { return juce::jlimit (0.0f, 100.0f, (float) rr); }
    };

    //==========================================================================
    // FIVE PAGES, AND THE LAST ONE IS THE WAY OUT OF THE KIT.
    //
    // SWEET, REV and DEL are gone from this rack.  All three are GLOBAL effects
    // now — one instance per section, shared by every channel in it — so a
    // per-kit page for any of them would either change nothing or change it for
    // the whole hand without saying so.
    //
    // What remains is what is genuinely per-kit: EQ, saturation, compression
    // and pan.  SENDS is how the kit reaches everything else.
    enum FxTab { TabEq = 0, TabSat, TabComp, TabSweet, TabPan, TabSends, NumFxTabs };
    static constexpr const char* kFxTabNames [NumFxTabs] = {
        "EQ", "SAT", "COMP", "SWEET", "PAN", "SENDS"
    };
    static constexpr const char* kFxFrameTitles [NumFxTabs] = {
        "10-BAND EQ", "SATURATION", "COMPRESSOR", "SWEETENER", "PAN",
        "SENDS TO GLOBAL EFFECTS  (chorus / reverb / delay)"
    };

    //==========================================================================
    // Content
    //==========================================================================
    class Content : public juce::Component, private juce::Timer
    {
    public:
        explicit Content (DrumsPopup& owner_) : owner (owner_)
        {
            setOpaque (true);

            // ── User-kit file preset manager (SAVE KIT / LOAD KIT) ────────────
            for (auto* b : { &saveKitBtn, &loadKitBtn, &saveDefaultBtn })
            {
                b->setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF2A2A2A));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                addAndMakeVisible (*b);
            }
            saveKitBtn .setButtonText ("SAVE KIT");
            loadKitBtn .setButtonText ("LOAD KIT");

            // ── Component replacement droplist ────────────────────────────────
            // Hidden by default and revealed by selectComponent ONLY when the
            // folder actually holds more than one file.  clap/
            // hold one each, so they get no droplist at all - an editor should
            // not offer a choice the library cannot honour.
            for (int i = 0; i < kMaxComponents; ++i)
            {
                auto& b = componentButtons[i];
                b.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF2A2A2A));
                b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                b.onClick = [this, i]
                {
                    if (i < (int) componentNames.size())
                        selectComponent (componentNames[(size_t) i]);
                };
                b.setVisible (false);
                addAndMakeVisible (b);
            }

            // ── TWO-HANDLE FILTER ────────────────────────────────────────────
            // Beside GAIN / PITCH / ROUND ROBIN, the same control the melodic
            // editors carry.  A fast reducer: thin a boomy kick or take the fizz
            // off a hat with two thumbs, instead of spending five EQ bands on it.
            bandFilter.onChange = [this] (float, float) { writeSliderToKey(); };
            addAndMakeVisible (bandFilter);

            // ── USER SFZ, top-right ───────────────────────────────────────────
            sfzLoadBtn.setButtonText ("LOAD");
            styleSfzLoad (false);
            sfzLoadBtn.onClick = [this] { if (owner.onSfzLoadRequested) owner.onSfzLoadRequested(); };
            // addChildComponent, NOT addAndMakeVisible - the latter sets visible
            // TRUE and undid the line above it, which is why LOAD was on screen
            // from the start.
            addChildComponent (sfzLoadBtn);

            // THE TOGGLE CARRIES THE CAPTION and is ALWAYS enabled.  It used to
            // be disabled until a file was loaded, while LOAD only appeared once
            // the toggle was on - so neither could ever be reached.  The toggle
            // is the way in; LOAD is what it reveals.
            sfzToggleBtn.setButtonText ("LOAD YOUR OWN SFZ");
            sfzToggleBtn.setClickingTogglesState (true);
            styleSfzToggle (false);
            sfzToggleBtn.onClick = [this]
            {
                sfzWanted = sfzToggleBtn.getToggleState();
                styleSfzToggle (sfzWanted);
                sfzLoadBtn.setVisible (sfzWanted);
                if (owner.onSfzToggled) owner.onSfzToggled (sfzWanted);
                resized();
            };
            addAndMakeVisible (sfzToggleBtn);

            componentSwapBtn.setButtonText ("REPLACE");
            componentSwapBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF2A2A2A));
            componentSwapBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            componentSwapBtn.onClick = [this] { openComponentSwapMenu(); };
            componentSwapBtn.setVisible (false);
            addAndMakeVisible (componentSwapBtn);
            saveKitBtn.onClick = [this] { saveUserKit(); };
            loadKitBtn.onClick = [this] { loadUserKit(); };
            // Writes the current kit to instruments_presets\NNN-Family.drm so the
            // matching drum program change reloads it verbatim.
            saveDefaultBtn.onClick = [this] { if (owner.onSaveAsDefault) owner.onSaveAsDefault(); };

            // ── Per-key element-selector buttons (49 buttons, "E …" labels) ──
            for (int k = 0; k < kKeyCount; ++k)
            {
                auto& b = perKeyKitButtons[k];
                b.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF2A2A2A));
                b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                b.onClick = [this, k] { openPerKeyKitMenu (kKeyFirst + k); };
                addAndMakeVisible (b);
            }

            // ── Per-key GoldSliders ───────────────────────────────────────────
            // All sliders are 0..100 with step 1 (101 positions).  Hosting
            // GoldSlider directly gives us the bronze handle / dark groove
            // look identical to the InstrEditorWindow's instrument controls.
            mountPerKeySlider (gainSlider,    "GAIN");
            mountPerKeySlider (pitchSlider,   "PITCH");
            mountPerKeySlider (rrSlider,      "RR");

            // VELOCITY CURVE, sharing RR's column.  RR gives up half its height
            // for it - the round-robin control is a small integer count and
            // never needed a full-height fader, while a curve is unreadable
            // without a picture.
            //
            // It writes through keysToEdit() like every other per-element
            // control, which is what makes it PER CATEGORY on a composed kit
            // (the selected component's keys) and GLOBAL on a sampled one (all
            // 128).  No branch anywhere.
            addAndMakeVisible (velCurveBox);
            velCurveBox.onChange = [this](float) { writeSliderToKey(); };
            mountPerKeySlider (lengthSlider,  "LENGTH");

            // Default-value double-click positions (engine values).
            gainSlider   .setValue (Map::gainToSlider (1.0f),   juce::dontSendNotification);
            pitchSlider  .setValue (Map::pitchToSlider (0.0f),  juce::dontSendNotification);
            rrSlider     .setValue (Map::rrToSlider (0),        juce::dontSendNotification);
            lengthSlider .setValue (100.0f,                            juce::dontSendNotification);

            gainSlider   .onChange = [this](float){ writeSliderToKey(); };
            pitchSlider  .onChange = [this](float){ writeSliderToKey(); };
            rrSlider     .onChange = [this](float){ writeSliderToKey(); };
            lengthSlider .onChange = [this](float){ writeSliderToKey(); };

            // ── Kick MIX controls (per-key; visible only on kick keys 35/36) ──
            mountPerKeySlider (kickMixSlider, "MIX");
            kickMixSlider.setValue (50.0f, juce::dontSendNotification);
            kickMixSlider.onChange = [this](float){ writeSliderToKey(); };

            kickMixEnableBtn.setClickingTogglesState (true);
            kickMixEnableBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
            kickMixEnableBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF2E8B2E));
            kickMixEnableBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            kickMixEnableBtn.setColour (juce::TextButton::textColourOffId,  juce::Colours::grey);
            kickMixEnableBtn.setToggleState (false, juce::dontSendNotification);   // default OFF (opt-in)
            kickMixEnableBtn.onClick = [this] { refreshKickMixControls(); writeSliderToKey(); };
            addAndMakeVisible (kickMixEnableBtn);

            kickMixVariantBtn.setClickingTogglesState (true);   // off = EDM, on = WOOD
            kickMixVariantBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
            kickMixVariantBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (Betel::Pal::kAccentShade));
            kickMixVariantBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
            kickMixVariantBtn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            kickMixVariantBtn.onClick = [this] { refreshKickMixControls(); writeSliderToKey(); };
            addAndMakeVisible (kickMixVariantBtn);

            // Master rack WET fader (global dry↔FX blend for the whole kit).
            mountFxSlider (fxWetSlider, "WET");
            fxWetSlider.setValue (100.0f, juce::dontSendNotification);   // fully through the rack
            fxWetSlider.onChange = [this](float){ writeFxFromUi(); };


            // ── FX tab bar (5 tabs) ───────────────────────────────────────────
            for (int t = 0; t < NumFxTabs; ++t)
            {
                tabButtons[t].setButtonText (kFxTabNames[t]);
                tabButtons[t].onClick = [this, t] { selectFxTab (t); };
                addAndMakeVisible (tabButtons[t]);
            }

            // ── FX sliders — one set per tab, all 0..100 step-1 GoldSliders ───
            // The name argument is unused by mountFxSlider — the label lives on
            // the slider itself — so it is passed empty rather than fetched from
            // a Component name GoldSlider never sets.
            for (int i = 0; i < kNumSends; ++i)
                mountFxSlider (sendSliders[i], {});

            mountFxSlider (swMixSlider,    "MIX");
            mountFxSlider (swDepthSlider,  "DEPTH");
            mountFxSlider (swWindowSlider, "WINDOW");
            mountFxSlider (swTameSlider,   "TAME");
            mountFxSlider (swFreqSlider,   "FREQ");
            mountFxSlider (swDriveSlider,  "DRIVE");
            mountFxSlider (swRndSlider,    "AMOUNT");
            mountFxSlider (swCeilSlider,   "CEILING");
            mountFxSlider (swRatioSlider,  "RATIO");
            for (auto* b : { &swSoftBtn, &swPeakBtn, &swTameBtn, &swRoundBtn })
            {
                b->setClickingTogglesState (true);
                b->onClick = [this] { styleSweetStages(); writeFxFromUi(); };
                addAndMakeVisible (*b);
            }
            swSoftBtn .setButtonText ("SOFTEN");
            swPeakBtn .setButtonText ("PEAK");
            swTameBtn .setButtonText ("TAME");
            swRoundBtn.setButtonText ("ROUND");
            styleSweetStages();

            mountFxSlider (satDriveSlider, "DRIVE");
            mountFxSlider (satMixSlider,   "MIX");

            mountFxSlider (compThreshSlider,  "THRESH");
            mountFxSlider (compRatioSlider,   "RATIO");
            mountFxSlider (compAttackSlider,  "ATK");
            mountFxSlider (compReleaseSlider, "REL");
            mountFxSlider (compMakeupSlider,  "MAKE");

            mountFxSlider (revSizeSlider, "SIZE");
            mountFxSlider (revDampSlider, "DAMP");
            mountFxSlider (revWetSlider,  "WET");
            mountFxSlider (revDrySlider,  "DRY");
            mountFxSlider (revTailSlider, "TAIL");
            mountFxSlider (revPreSlider,  "PRE");

            revBand.onChange = [this] (float, float) { writeFxFromUi(); };
            addAndMakeVisible (revBand);

            revAlgoBtn.onClick = [this]
            {
                revAlgoIdx = revAlgoIdx == 0 ? 1 : 0;
                refreshRevAlgoBtn();
                writeFxFromUi();
            };
            addAndMakeVisible (revAlgoBtn);
            refreshRevAlgoBtn();

            // Base-gain boxes, same gesture as the GAIN handle above and as the
            // melodic WET sliders: LEFT double-click, no button, no hint.
            revWetSlider.onLeftDoubleClick = [this] { showWetBaseDialog (true);  };
            delWetSlider.onLeftDoubleClick = [this] { showWetBaseDialog (false); };

            // Kit calibration trim.  Sits beside the rack WET (always visible on
            // every FX tab) because it is not one stage among six — it is how
            // loud this kit is against every other sound in the library.
            kitGainSlider.setStep (1.0f);
            kitGainSlider.onLeftDoubleClick = [this]
            { if (owner.onBaseUnityRequested) owner.onBaseUnityRequested(); };
            kitGainSlider.onChange = [this] (float v)
            {
                if (updatingFromState) return;
                if (owner.onKitGainChanged) owner.onKitGainChanged (v);
            };
            addAndMakeVisible (kitGainSlider);

            mountFxSlider (delTimeSlider, "TIME");
            mountFxSlider (delFbSlider,   "FB");
            mountFxSlider (delDrySlider,  "DRY");
            mountFxSlider (delWetSlider,  "WET");

            // ── Delay time source ─────────────────────────────────────────────
            // The kit delay used to run on free ms only, so a kit echo drifted
            // against every tempo-synced melodic delay in the arrangement.  It
            // now offers the same SYNC / time-signature / division set the
            // instrument editor does, and both resolve through the same
            // division tables in Channel, so a 1/8 here lands where a 1/8 there
            // does.
            delSyncBtn.onClick = [this] { delSyncOn = ! delSyncOn; refreshDelayRow(); writeFxFromUi(); };
            addAndMakeVisible (delSyncBtn);

            delSig44Btn.setButtonText ("4/4");
            delSig34Btn.setButtonText ("3/4");
            delSig44Btn.onClick = [this] { setDelTimeSig (0); };
            delSig34Btn.onClick = [this] { setDelTimeSig (1); };
            addAndMakeVisible (delSig44Btn);
            addAndMakeVisible (delSig34Btn);

            for (int d = 0; d < 5; ++d)
            {
                delDivBtns[d].onClick = [this, d]
                { delDivIdx = d; refreshDelayRow(); writeFxFromUi(); };
                addAndMakeVisible (delDivBtns[d]);
            }

            static const char* eqLabels [10] = {
                "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
            };
            for (int i = 0; i < 10; ++i)
            {
                mountFxSlider (eqSliders[i], eqLabels[i]);
                eqSliders[i].setValue (50.0f, juce::dontSendNotification);  // unity
            }

            // Wire FX onChange handlers (one shared callback re-reads ALL UI).
            // 26, not 23: TAIL, PRE and the delay DRY joined the rack when
            // it took over the melodic reverb and delay engines.  std::array
            // needs the count spelled out, so this one still has to be kept in
            // step by hand; the nonEq array below now deduces its own.
            const std::array<GoldSlider*, 26> fxKnobs {
                &swMixSlider, &swDepthSlider, &swWindowSlider, &swTameSlider,
                &swFreqSlider, &swDriveSlider, &swRndSlider,
                &swCeilSlider, &swRatioSlider,
                &satDriveSlider, &satMixSlider,
                &compThreshSlider, &compRatioSlider, &compAttackSlider,
                &compReleaseSlider, &compMakeupSlider,
                &revSizeSlider, &revDampSlider, &revWetSlider, &revDrySlider,
                &revTailSlider, &revPreSlider,
                &delTimeSlider,  &delFbSlider,  &delDrySlider, &delWetSlider
            };
            for (auto* k : fxKnobs) k->onChange = [this](float){ writeFxFromUi(); };

            // THE SEND SLIDERS NEED THIS TOO.  They are not in fxKnobs — that
            // array is a hand-kept std::array with its count spelled out — so
            // without their own wiring they moved and nothing happened, which
            // is why the kit stayed dry however far they were pushed.
            for (auto& sl : sendSliders) sl.onChange = [this](float){ writeFxFromUi(); };
            for (auto& s : eqSliders) s.onChange = [this](float){ writeFxFromUi(); };

            // PAN tab: single fader, 0..100 with 50 = centre -> pan -1..+1.
            mountFxSlider (panSlider, "PAN");
            panSlider.onChange = [this](float){ writeFxFromUi(); };

            for (int t = 0; t < NumFxTabs; ++t)
            {
                auto& eb = fxEnableBtns[t];
                eb.setClickingTogglesState (true);
                eb.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
                eb.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF2E8B2E));
                eb.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
                eb.setColour (juce::TextButton::textColourOffId,  juce::Colours::grey);
                eb.onClick = [this, t] { refreshFxEnableBtn (t); writeFxFromUi(); };
                addAndMakeVisible (eb);
            }

            // ── Piano strip along the bottom (MIDI 24..96) ───────────────────
            // The key-select keyboard above starts at 35, so the style's lowest
            // drum / percussion hits (24..34) are invisible there.  This strip
            // shows them — and lets the kit be played straight from the window.
            pianoStrip.onNoteOn = [this](int n, int v)
            {
                if (owner.onKeyboardNoteOn) owner.onKeyboardNoteOn (n, v);
            };
            pianoStrip.onNoteOff = [this](int n)
            {
                if (owner.onKeyboardNoteOff) owner.onKeyboardNoteOff (n);
            };
            pianoStrip.onQuerySounding = [this](uint32_t* mask)
            {
                if (owner.onQuerySoundingNotes) owner.onQuerySoundingNotes (mask);
            };
            addAndMakeVisible (pianoStrip);

            selectFxTab (TabEq);

            // Initial kick-mix state (default selectedKey = 36 = kick, so the
            // controls start visible).  Layout is finalised by resized().
            refreshKickMixControls();
            updateKickMixVisibility();

            startTimerHz (10);   // refresh header text in case state mutates
        }

        //----------------------------------------------------------------------
        void setRegistry (Betel::DrumKitRegistry* r) { registry = r; }

        // A "shared" / global drum component is one loaded onto every kit and so
        // cannot be swapped per-kit: the engine pins clap to MIDI 39
        // (Channel::loadDrumKit), and the registry exposes any other global
        // folders (e.g. the_lasts = upper percussion) via isGlobal.  Their
        // per-key element-selector buttons are meaningless, so they are hidden.
        //
        // MIDI 35 CAME OFF THIS LIST when the sub-kick moved inside the kick
        // blobs.  It is a real per-kit element now, so hiding its selector would
        // be withholding a choice that genuinely exists.
        bool isSharedComponentKey (int midiKey) const
        {
            if (midiKey == 39) return true;                    // forced global
            if (registry != nullptr)
                for (const auto& gName : registry->getGlobalComponentNames())
                    for (const auto& h : registry->getElementsForKit (gName))
                        if (h.midiKey == midiKey) return true;
            return false;
        }

        // Kick keys carry the supplemental EDM/WOOD kick-mix blend (MIDI 35 &
        // 36 only).  Every other key ignores the kickMix* fields, so the Kick
        // MIX controls are hidden unless one of these keys is selected.
        static bool isKickMixKey (int midiKey) noexcept
        {
            return midiKey == 35 || midiKey == 36;
        }

        void setKitState (const DrumKitParams& kit)
        {
            kitState = kit;

            // Rebuild the row first: a library rescan can add or drop folders,
            // and selecting into a stale list would light the wrong button.
            rebuildComponentRow();

            // Re-derive the component's key set against the NEW kit: the same
            // folder can cover different keys from one kit to the next, so a
            // stale list would edit the wrong drums.
            selectComponent (selectedComponent.isNotEmpty() ? selectedComponent
                                                            : juce::String ("kick"));

            refreshPerKeyButtonLabels();
            refreshSelectedKeySliders();
            refreshFxSlidersFromState();
            repaint();
        }

        //----------------------------------------------------------------------
        // WHICH EDITOR THIS IS - and it can change under an open window.
        //
        // The mode used to be written straight into owner.fullKitName once, by
        // the host, at the moment the popup was CONSTRUCTED.  The popup is a
        // lazily-created member that is then reused for every EDIT press on
        // every slot for the life of the plugin, so whichever kind of kit
        // happened to be on the slot the first time you opened it decided the
        // editor for all of them.  In practice a drum slot starts on a composed
        // kit, so the sampled-kit editor below effectively never ran.
        //
        // Going back to COMPOSED needs its own work: layoutPerKeySliders() hides
        // PITCH / ROUND ROBIN / LENGTH in full-kit mode and nothing ever set
        // them visible again, so a window that had once shown a sampled kit
        // stayed stripped for every GM kit after it - and keysToEdit() kept
        // fanning every edit across all 128 keys.
        //----------------------------------------------------------------------
        void setFullKit (const juce::String& kitName)
        {
            if (owner.fullKitName == kitName) return;   // nothing to reshape
            owner.fullKitName = kitName;

            const bool composed = kitName.isEmpty();

            // The per-ELEMENT controls exist only on a composed kit.  Full-kit
            // mode hides them in layoutPerKeySliders(); this is the other half.
            pitchSlider .setVisible (composed);
            rrSlider    .setVisible (composed);
            lengthSlider.setVisible (composed);

            // The component row, REPLACE and the card title are all derived from
            // the mode, and selectComponent rebuilds the key set with them - so
            // neither editor can leave state behind for the other.
            rebuildComponentRow();
            selectComponent (selectedComponent.isNotEmpty() ? selectedComponent
                                                            : juce::String ("kick"));
            updateKickMixVisibility();   // also reflows the slider columns
            resized();
            repaint();
        }

        //----------------------------------------------------------------------
        // SAVE KIT / LOAD KIT now use .drm — the SAME file the SAVE AS DEFAULT
        // button writes.
        //
        // The old .bdk held a bare <DrumKit> tree, so the two buttons produced
        // two formats for one thing: a .bdk could not be dropped into
        // instruments_presets to become a kit's default, and a .drm could not be
        // loaded here.  The .drm wrapper is a superset — it carries the same
        // DrumKit inside a <Slot>, plus the flag, the gain and the base unity —
        // so folding SAVE KIT into it loses nothing and removes the duality.
        //
        // .bdk files still LOAD (see loadUserKit); only writing is unified.
        //----------------------------------------------------------------------
        void saveUserKit()
        {
            fileChooser = std::make_unique<juce::FileChooser> (
                "Save kit", juce::File(), "*.drm");

            // Step aside for the OS dialog and take the topmost flag back when
            // it closes -- see GrexPopupWindow.h.
            Betel::suspendGrexPopupTopmost (&owner);
            juce::Component::SafePointer<juce::Component> back (&owner);

            fileChooser->launchAsync (
                juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
                [this, back] (const juce::FileChooser& fc)
                {
                    Betel::restoreGrexPopupTopmost (back);

                    auto file = fc.getResult();
                    if (file == juce::File()) return;
                    if (file.getFileExtension().isEmpty())
                        file = file.withFileExtension ("drm");

                    // The kit travels inside a full SlotParams, exactly as SAVE
                    // AS DEFAULT writes it, so the result is loadable by both.
                    SlotParams p;
                    p.drumKit = kitState;

                    const int flag = kitState.lastLoadedKit.getIntValue();
                    Betel::InstrumentPresetIO::writeToFile (file, flag,
                                                     kitState.lastLoadedKit,
                                                     /*isDrum*/ true, /*frb*/ {}, p);
                });
        }

        void loadUserKit()
        {
            fileChooser = std::make_unique<juce::FileChooser> (
                "Load kit", juce::File(), "*.drm;*.bdk");

            // Step aside for the OS dialog and take the topmost flag back when
            // it closes -- see GrexPopupWindow.h.
            Betel::suspendGrexPopupTopmost (&owner);
            juce::Component::SafePointer<juce::Component> back (&owner);

            fileChooser->launchAsync (
                juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
                [this, back] (const juce::FileChooser& fc)
                {
                    Betel::restoreGrexPopupTopmost (back);

                    auto file = fc.getResult();
                    if (file == juce::File() || ! file.existsAsFile()) return;

                    DrumKitParams loaded;

                    // .drm first — a full preset wrapper.  Falls back to the old
                    // bare <DrumKit> so existing .bdk files still open.
                    const auto preset = Betel::InstrumentPresetIO::read (file);
                    if (preset.ok && preset.isDrum && preset.params.drumKit.hasMappedKeys())
                    {
                        loaded = preset.params.drumKit;
                    }
                    else
                    {
                        auto xml = juce::XmlDocument::parse (file);
                        if (xml == nullptr) return;
                        auto tree = juce::ValueTree::fromXml (*xml);
                        if (! BetelStateXml::loadDrumKit (tree, loaded)) return;
                    }

                    setKitState (loaded);                          // refresh editor UI
                    if (owner.onApplyUserKit)
                        owner.onApplyUserKit (loaded);             // apply to engine
                });
        }

        void setKitGainPercent (float gainPercent)
        {
            const bool wasUpdating = updatingFromState;
            updatingFromState = true;                  // seeding, not a user move
            kitGainSlider.setValue (juce::jlimit (0.0f, 200.0f, gainPercent),
                                    juce::dontSendNotification);
            updatingFromState = wasUpdating;
        }

        void setSlotLabel (const juce::String& s)
        {
            slotLabel = s;
            repaint();
        }

        //----------------------------------------------------------------------
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xFF161616));

            // Top-bar status text (ASCII separators only — no Unicode!).
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::Font (13.5f, juce::Font::bold));
            g.drawText (slotLabel.isEmpty() ? juce::String ("Editing: (drum slot)")
                                            : slotLabel,
                        statusRect, juce::Justification::centredLeft, true);

            // The keyboard backdrop and paintKeyboard() used to be drawn here.
            // Both are retired with the key picker: the component row above says
            // what is being edited, and a 49-key strip that no longer selects
            // anything would only invite clicks that do nothing.

            // Per-key card with its own framed rectangle + header.
            InstrEditStyle::paintComponentFrame (g, perKeyCardRect,
                                                 perKeyHeader());

            // Active FX frame.
            InstrEditStyle::paintComponentFrame (g, fxFrameRect,
                                                 kFxFrameTitles[activeTab]);

            if (activeTab == TabSweet)
                paintSweetenerMeters (g);
        }

        //======================================================================
        // SWEETENER GAIN-REDUCTION METERS
        //
        // The identical four bars the instrument editor's EFFECTS page draws,
        // in the identical order (SOFTEN, PEAK, TAME, ROUND) with the identical
        // range and colours.  Two panels driving the SAME SweetenerFx through
        // the SAME SweetenerParams have no business metering it differently -
        // the same reasoning that put the nine sliders in one row here.
        //
        // Drawn rather than built from Components: no interaction, and this
        // window is already carrying a lot of children.
        //======================================================================
        void paintSweetenerMeters (juce::Graphics& g)
        {
            if (sweetMeterArea.isEmpty()) return;

            static const char* kNames[kNumSweetMeters] = { "SOFT", "PEAK", "TAME", "RND" };

            const int gap  = 6;
            const int cell = juce::jmax (30, (sweetMeterArea.getWidth() - (kNumSweetMeters - 1) * gap)
                                                 / kNumSweetMeters);

            // The block enable gates every stage, so a switched-off block shows
            // empty troughs instead of a frozen last reading.
            const bool live = fxEnableBtns[TabSweet].getToggleState();

            for (int i = 0; i < kNumSweetMeters; ++i)
            {
                juce::Rectangle<int> cellR (sweetMeterArea.getX() + i * (cell + gap),
                                            sweetMeterArea.getY(), cell,
                                            sweetMeterArea.getHeight());

                auto labelR = cellR.removeFromLeft (34);
                auto barR   = cellR;

                g.setColour (juce::Colour (0xFF8A8A8A));
                g.setFont (juce::Font (10.0f, juce::Font::bold));
                g.drawText (kNames[i], labelR, juce::Justification::centredLeft, false);

                g.setColour (juce::Colour (0xFF0C0C0C));
                g.fillRect (barR);

                const float db = live ? grDb[i] : 0.0f;

                if (db > 0.05f)
                {
                    const float norm = juce::jlimit (0.0f, 1.0f, db / kSweetMeterRangeDb);
                    auto fill = barR.toFloat().withWidth (barR.getWidth() * norm);

                    g.setColour (db >= kSweetMeterHotDb ? juce::Colour (0xFFCC3322)
                                                        : juce::Colour (Betel::Pal::kAccentBright));
                    g.fillRect (fill);
                }

                g.setColour (juce::Colour (0xFF2E2E2E));
                g.drawRect (barR, 1);
            }
        }

        /** Host -> content, on the UI timer.  Peak hold with a slow decay, for
            the same reason EffectsPanel uses one: the audio thread reports a
            per-BLOCK peak, and a bar following that raw at 30 Hz flickers
            rather than reads. */
        void setSweetenerGr (float softenDb, float peakDb, float tameDb, float roundDb)
        {
            const float in[kNumSweetMeters] = { softenDb, peakDb, tameDb, roundDb };
            bool moved = false;

            for (int i = 0; i < kNumSweetMeters; ++i)
            {
                const float v = juce::jlimit (0.0f, kSweetMeterRangeDb, in[i]);
                const float next = (v > grDb[i]) ? v
                                                 : grDb[i] + (v - grDb[i]) * 0.25f;

                if (std::abs (next - grDb[i]) > 0.02f) { grDb[i] = next; moved = true; }
            }

            if (moved && activeTab == TabSweet && ! sweetMeterArea.isEmpty())
                repaint (sweetMeterArea);
        }

        void resized() override
        {
            const int pad = 8;
            auto r = getLocalBounds().reduced (pad);

            // ── Top bar ───────────────────────────────────────────────────────
            const int topH = 28;
            auto top = r.removeFromTop (topH);
            saveKitBtn.setBounds (top.removeFromLeft (80));
            top.removeFromLeft (4);
            loadKitBtn.setBounds (top.removeFromLeft (80));
            top.removeFromLeft (4);
            // SAVE AS DEFAULT is gone: a kit's sound belongs to the SET now, and
            // a second place to save it was a second owner of the same values.
            saveDefaultBtn.setVisible (false);
            saveDefaultBtn.setBounds (0, 0, 0, 0);

            // SFZ pair hard right on the header row, same place and same order
            // as the instrument editors - one habit, two windows.
            if (sfzLoadBtn.isVisible())
            {
                sfzLoadBtn.setBounds (top.removeFromRight (110));
                top.removeFromRight (4);
            }
            sfzToggleBtn.setBounds (top.removeFromRight (170));
            top.removeFromRight (10);

            statusRect = top;

            r.removeFromTop (6);

            // ── COMPONENT SELECTOR ROW ────────────────────────────────────────
            //
            // Replaces the 49-key element picker and the keyboard strip under
            // it.  A keyboard asks "which MIDI note", which is the wrong
            // question: the answer people actually want is "the toms" or "the
            // metal", and reaching that through six separate keys meant six
            // separate edits that had to be kept in step by hand.
            //
            // One button per component FOLDER, so the row is exactly what the
            // library contains - no fixed list to drift out of step with disk.
            const bool fullKit = owner.fullKitName.isNotEmpty();

            if (fullKit)
            {
                // No component row: there are no components.
                for (auto& b : componentButtons) { b.setVisible (false); b.setBounds (0,0,0,0); }
                componentSwapBtn.setVisible (false);
                componentSwapBtn.setBounds (0, 0, 0, 0);
            }
            else
            {
                const int compH = 30;
                auto compRow = r.removeFromTop (compH);

                const int nComp = juce::jmax (1, (int) componentNames.size());
                const int cellW = compRow.getWidth() / nComp;

                for (int i = 0; i < kMaxComponents; ++i)
                {
                    if (i >= (int) componentNames.size())
                    {
                        componentButtons[i].setVisible (false);
                        componentButtons[i].setBounds (0, 0, 0, 0);
                        continue;
                    }
                    componentButtons[i].setVisible (true);
                    componentButtons[i].setBounds (compRow.getX() + i * cellW,
                                                   compRow.getY(),
                                                   cellW - 3, compH);
                }
            }

            r.removeFromTop (4);

            // REPLACE sits under the row, and only when the selected folder
            // actually holds more than one file.
            if (! fullKit)
            {
                auto swapRow = r.removeFromTop (24);
                componentSwapBtn.setBounds (swapRow.removeFromLeft (150));
            }

            // The keyboard is gone; nothing paints into kbRect any more.
            kbRect = {};

            r.removeFromTop (8);

            // The old per-key buttons stay in the class but are never shown -
            // the code that drives them is the same code the component row now
            // calls, so deleting them would be a bigger change than retiring
            // them.
            for (int k = 0; k < kKeyCount; ++k)
            {
                perKeyKitButtons[k].setVisible (false);
                perKeyKitButtons[k].setBounds (0, 0, 0, 0);
            }

            // ── Piano strip: full width, hard against the bottom ──────────────
            pianoStrip.setBounds (r.removeFromBottom (kStripH));
            r.removeFromBottom (6);

            // ── Bottom area: left = per-key sliders, right = FX panel ─────────
            auto bottom = r;
            const int rightW = juce::jmax (560, (int) (bottom.getWidth() * 0.48f));
            auto rightArea = bottom.removeFromRight (rightW);
            bottom.removeFromRight (8);

            perKeyCardRect = bottom;
            layoutPerKeySliders();

            // FX tabs + framed effect content.
            const int tabH = 28;
            auto tabRow = rightArea.removeFromTop (tabH);
            const int tabBtnW = tabRow.getWidth() / NumFxTabs;
            for (int t = 0; t < NumFxTabs; ++t)
            {
                const int x = tabRow.getX() + t * tabBtnW;
                const int w = (t == NumFxTabs - 1)
                                 ? (tabRow.getRight() - x)
                                 : tabBtnW - 2;
                tabButtons[t].setBounds (x, tabRow.getY(), w, tabH);
            }

            rightArea.removeFromTop (4);
            fxFrameRect = rightArea;
            layoutActiveFxTab();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int k = keyAtPoint (e.getPosition());
            if (k >= 0)
            {
                selectedKey = k;
                refreshSelectedKeySliders();
                repaint();

                // Right-click auditions the key's drum sound for fast editing.
                if (e.mods.isRightButtonDown() && owner.onAuditionKey)
                    owner.onAuditionKey (k);
            }
        }

    private:
        //----------------------------------------------------------------------
        // Setup helpers
        //----------------------------------------------------------------------
        void mountPerKeySlider (GoldSlider& s, const juce::String& /*name*/)
        {
            s.setStep (1.0f);                                   // 101 discrete steps
            addAndMakeVisible (s);
        }
        //----------------------------------------------------------------------
        // Delay time source (mirrors BigDrumsFxPanel, which mirrors the melodic
        // delay — one musical vocabulary across all three).
        //----------------------------------------------------------------------
        const std::vector<juce::String>& delDivTable() const
        {
            static const std::vector<juce::String> k44 { "1/1","1/2","1/4","1/8","1/16" };
            static const std::vector<juce::String> k34 { "1/1","1/3","1/6","1/12" };
            return delTimeSig == 0 ? k44 : k34;
        }

        void setDelTimeSig (int ts)
        {
            delTimeSig = juce::jlimit (0, 1, ts);
            delDivIdx  = 0;                 // index 0 is valid in both tables
            refreshDelayRow();
            resized();
            writeFxFromUi();
        }

        void showDelayRow (bool vis)
        {
            delSyncBtn .setVisible (vis);
            delSig44Btn.setVisible (vis);
            delSig34Btn.setVisible (vis);
            for (int d = 0; d < 5; ++d)
                delDivBtns[d].setVisible (vis && d < (int) delDivTable().size());
        }

        /** Light the row and make the INACTIVE time source visibly inert — with
            SYNC on the free TIME knob does nothing, with SYNC off the divisions
            do, and showing both live would promise two time sources at once. */
        void refreshDelayRow()
        {
            delSyncBtn.setButtonText (delSyncOn ? "SYNC" : "FREE");
            InstrEditStyle::styleSquareButton (delSyncBtn,  delSyncOn);
            InstrEditStyle::styleSquareButton (delSig44Btn, delTimeSig == 0);
            InstrEditStyle::styleSquareButton (delSig34Btn, delTimeSig == 1);

            const int nd = (int) delDivTable().size();
            delDivIdx = juce::jlimit (0, juce::jmax (0, nd - 1), delDivIdx);
            for (int d = 0; d < 5; ++d)
                InstrEditStyle::styleSquareButton (delDivBtns[d], d == delDivIdx);

            setInert (delTimeSlider, delSyncOn);
            setInert (delSig44Btn, ! delSyncOn);
            setInert (delSig34Btn, ! delSyncOn);
            for (int d = 0; d < 5; ++d) setInert (delDivBtns[d], ! delSyncOn);
        }

        static void setInert (juce::Component& c, bool inert)
        {
            c.setAlpha (inert ? 0.35f : 1.0f);
            c.setInterceptsMouseClicks (! inert, ! inert);
        }

        void mountFxSlider (GoldSlider& s, const juce::String& /*name*/)
        {
            s.setStep (1.0f);
            addAndMakeVisible (s);
        }

        /** HALL/ROOM is the FDN, PLATE is the Dattorro tank.  Lit = PLATE, so
            the unlit state is the one every existing kit is already on. */
        void refreshRevAlgoBtn()
        {
            revAlgoBtn.setButtonText (revAlgoIdx == 1 ? "PLATE" : "HALL");
            InstrEditStyle::styleSquareButton (revAlgoBtn, revAlgoIdx == 1);
        }

        //----------------------------------------------------------------------
        // WET BASE GAIN - LEFT double-click on either WET slider.
        //
        // Identical in wording and range to EffectsPanel's box, deliberately:
        // this is the same control on the same two effects, and a number learned
        // on the instrument editor has to mean the same thing here.
        //----------------------------------------------------------------------
        void showWetBaseDialog (bool isReverb)
        {
            const float current = isReverb ? revWetBase : delWetBase;

            auto* w = new juce::AlertWindow (
                isReverb ? "REVERB WET BASE" : "DELAY WET BASE",
                juce::String (isReverb ? "What full WET is worth on the kit reverb.\n"
                                       : "What full WET is worth on the kit delay.\n")
                    + "100 = the slider means what it says. 50 = half.\n"
                    + "Saved with this kit. Range 0 to 200.",
                juce::MessageBoxIconType::NoIcon);

            w->addTextEditor ("pct", juce::String (current * 100.0f, 1), "%:");
            w->addButton ("SAVE",   1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

            // SafePointer: the drum editor can be closed while the box is open.
            juce::Component::SafePointer<Content> safe (this);

            w->enterModalState (true, juce::ModalCallbackFunction::create (
                [safe, w, isReverb] (int result) mutable
                {
                    std::unique_ptr<juce::AlertWindow> owned (w);
                    if (result != 1) return;

                    auto* c = safe.getComponent();
                    if (c == nullptr) return;

                    const float v = juce::jlimit (0.0f, 200.0f,
                                        w->getTextEditorContents ("pct").getFloatValue()) * 0.01f;

                    if (isReverb) c->revWetBase = v;
                    else          c->delWetBase = v;

                    c->writeFxFromUi();
                }), false);
        }

        //----------------------------------------------------------------------
        // Layout: per-key sliders (left card)
        //----------------------------------------------------------------------
        void layoutPerKeySliders()
        {
            auto content = InstrEditStyle::componentFrameContent (perKeyCardRect);

            const bool kick = isKickMixKey (selectedKey);

            // On a kick key, reserve a short strip along the bottom of the card
            // for the two kick-mix buttons (MIX ON/OFF + EDM/WOOD).
            juce::Rectangle<int> btnStrip;
            if (kick)
            {
                content.removeFromBottom (6);
                btnStrip = content.removeFromBottom (26);
            }

            // 4 sliders normally; kick keys add the MIX amount as a 5th column.
            // The two-handle FILTER takes a column of its own at the end - it is
            // the same width as a slider and belongs with them, being the fourth
            // thing you reach for after gain, pitch and round robin.
            // FULL KIT: GAIN and the band filter only.  PITCH, ROUND ROBIN and
            // LENGTH are per-ELEMENT controls - they need a key group to act on,
            // and a sampled kit has none.
            const bool fullKit = owner.fullKitName.isNotEmpty();

            GoldSlider* sliders [5] = {
                &gainSlider, &pitchSlider, &rrSlider, &lengthSlider, &kickMixSlider
            };
            const int n    = fullKit ? 1 : (kick ? 5 : 4);
            const int cols = n + 1;                      // + the band filter

            if (fullKit)
                for (auto* sl : { &pitchSlider, &rrSlider, &lengthSlider, &kickMixSlider })
                { sl->setVisible (false); sl->setBounds (0, 0, 0, 0); }
                // velCurveBox is NOT in that list: a sampled kit has no elements
                // to shape but still has a velocity response worth curving.

            const int gap = 4;
            const int cellW = juce::jmax (40, (content.getWidth() - (cols - 1) * gap) / cols);

            // RR's COLUMN IS SHARED WITH THE VELOCITY CURVE.
            //
            // Round robin is a small integer count and never needed a
            // full-height fader; a curve is unreadable without a picture.  So
            // the curve takes the upper half of that cell and RR the lower.
            // On a FULL KIT there is no RR column at all, so the curve moves to
            // the top of the GAIN cell instead - a sampled kit still wants one
            // global curve even though it has no per-element controls.
            const int rrIndex = 2;

            for (int i = 0; i < n; ++i)
            {
                const int sx = content.getX() + i * (cellW + gap);
                auto cell = juce::Rectangle<int> (sx, content.getY(),
                                                  cellW, content.getHeight());

                if (! fullKit && i == rrIndex)
                {
                    const int half = (cell.getHeight() - 4) / 2;
                    velCurveBox.setBounds (cell.withHeight (half));
                    cell = cell.withTrimmedTop (half + 4);
                }
                sliders[i]->setBounds (cell);
            }

            if (fullKit)
            {
                // One curve for the whole kit, above GAIN.
                auto cell = juce::Rectangle<int> (content.getX(), content.getY(),
                                                  cellW, content.getHeight());
                const int half = (cell.getHeight() - 4) / 2;
                velCurveBox.setBounds (cell.withHeight (half));
                gainSlider .setBounds (cell.withTrimmedTop (half + 4));
            }
            velCurveBox.setVisible (true);

            bandFilter.setBounds (content.getX() + n * (cellW + gap),
                                  content.getY(), cellW, content.getHeight());

            if (kick && ! btnStrip.isEmpty())
            {
                const int gap2 = 8;
                const int bw   = juce::jmax (60, (btnStrip.getWidth() - gap2) / 2);
                kickMixEnableBtn .setBounds (btnStrip.removeFromLeft (bw));
                btnStrip.removeFromLeft (gap2);
                kickMixVariantBtn.setBounds (btnStrip.removeFromLeft (bw));
            }
        }

        //----------------------------------------------------------------------
        // Kick-mix control visibility + state (per-key; kick keys 35/36 only)
        //----------------------------------------------------------------------
        void updateKickMixVisibility()
        {
            const bool kick = isKickMixKey (selectedKey);
            kickMixSlider    .setVisible (kick);
            kickMixEnableBtn .setVisible (kick);
            kickMixVariantBtn.setVisible (kick);
            layoutPerKeySliders();   // reflow 4- vs 5-column + button strip
        }

        //----------------------------------------------------------------------
        // SWEETENER STAGE TOGGLES: ORANGE WHEN ON.
        //
        // These were the case InstrEditStyle::styleSquareButton's own comment
        // warns about and nothing here was calling it: a button with
        // setClickingTogglesState(true) is drawn from buttonOnColourId once its
        // toggle state is set, and that id was left at the LookAndFeel default.
        // So SOFTEN / PEAK / TAME / ROUND changed nothing visible when switched
        // - the only way to know a stage was on was to read the set file.
        //
        // Must be called after any setToggleState made with
        // dontSendNotification, since that deliberately skips onClick.
        //----------------------------------------------------------------------
        void styleSweetStages()
        {
            for (auto* b : { &swSoftBtn, &swPeakBtn, &swTameBtn, &swRoundBtn })
                InstrEditStyle::styleSquareButton (*b, b->getToggleState());
        }

        void refreshKickMixControls()
        {
            const bool on = kickMixEnableBtn.getToggleState();
            kickMixEnableBtn .setButtonText (on ? "MIX ON" : "MIX OFF");
            kickMixVariantBtn.setButtonText (kickMixVariantBtn.getToggleState() ? "WOOD" : "EDM");

            // Grey + disable the amount and the variant while the blend is off.
            kickMixSlider    .setEnabled (on);
            kickMixSlider    .setInterceptsMouseClicks (on, on);
            kickMixSlider    .setAlpha   (on ? 1.0f : 0.45f);
            kickMixVariantBtn.setEnabled (on);
        }

        //----------------------------------------------------------------------
        // Layout: only the active FX tab's controls
        //----------------------------------------------------------------------
        void layoutActiveFxTab()
        {
            auto content = InstrEditStyle::componentFrameContent (fxFrameRect);
            auto fxTopStrip = content.removeFromTop (22);
            content.removeFromTop (4);
            fxEnableBtns[activeTab].setBounds (fxTopStrip.removeFromLeft (80));

            // ── THE FOUR SWEETENER STAGE ENABLES ────────────────────────────
            //
            // These were made visible by selectFxTab and then never given
            // bounds by anything, so they have been sitting at zero size since
            // they were added: SOFTEN / PEAK / TAME / ROUND could not be
            // switched at all from this panel, and the sliders under them were
            // moving stages that were off.
            //
            // The top strip is where they belong - beside the block enable,
            // above the row of sliders they gate - and it is the formation the
            // instrument editor now shares.
            {
                const bool sweet = (activeTab == TabSweet);
                juce::TextButton* stages[4] = { &swSoftBtn, &swPeakBtn, &swTameBtn, &swRoundBtn };

                if (sweet)
                {
                    auto strip = fxTopStrip.reduced (6, 0);
                    const int gap = 4;
                    const int w   = juce::jmax (44, (strip.getWidth() - 3 * gap) / 4);
                    int x = strip.getX();
                    for (auto* b : stages)
                    {
                        b->setBounds (x, strip.getY(), w, strip.getHeight());
                        x += w + gap;
                    }
                }
                else
                {
                    for (auto* b : stages) b->setBounds (0, 0, 0, 0);
                }
            }

            // Master WET fader + the kit GAIN trim — always visible, fixed
            // columns on the right, whichever FX tab is showing.
            const int wetW = 58;
            auto gainCol = content.removeFromRight (wetW);
            content.removeFromRight (8);
            kitGainSlider.setBounds (gainCol);
            auto wetCol = content.removeFromRight (wetW);
            content.removeFromRight (10);
            // THE RACK'S WET BELONGS TO THE RACK, NOT TO THE SENDS.
            //
            // It is the dry/wet of the per-kit chain — EQ, saturation,
            // compression — and on the SENDS page it sits beside three sliders
            // it has no effect on whatsoever, which reads as a fourth send that
            // does nothing.  Hidden there and nowhere else.
            fxWetSlider.setVisible (activeTab != TabSends);
            fxWetSlider.setBounds (wetCol);

            auto laySliders = [&] (std::initializer_list<GoldSlider*> sliders)
            {
                const int n = (int) sliders.size();
                if (n <= 0) return;
                const int gap = 4;
                const int cellW = juce::jmax (40, (content.getWidth() - (n - 1) * gap) / n);

                int i = 0;
                for (auto* s : sliders)
                {
                    s->setBounds (content.getX() + i * (cellW + gap),
                                  content.getY(),
                                  cellW,
                                  content.getHeight());
                    ++i;
                }
            };

            // ── THE GR METER STRIP ──────────────────────────────────────────
            //
            // Only the SWEETEN page has one, so it is carved off BEFORE
            // laySliders runs and only on that tab - every other tab keeps the
            // full height it has always had.  Remembered as a rect because
            // setSweetenerGr repaints exactly this area and nothing else.
            if (activeTab == TabSweet)
            {
                const int stripH = juce::jmin (18, juce::jmax (0, content.getHeight() - 60));

                if (stripH >= 10)
                {
                    sweetMeterArea = content.removeFromBottom (stripH);
                    content.removeFromBottom (4);
                }
                else
                {
                    // Squeezed too small for both - the sliders win.  An empty
                    // rect switches the meters off cleanly.
                    sweetMeterArea = {};
                }
            }
            else
            {
                sweetMeterArea = {};
            }

            switch (activeTab)
            {
                case TabEq:
                    laySliders ({ &eqSliders[0], &eqSliders[1], &eqSliders[2], &eqSliders[3],
                                  &eqSliders[4], &eqSliders[5], &eqSliders[6], &eqSliders[7],
                                  &eqSliders[8], &eqSliders[9] });
                    break;

                case TabSat:
                    laySliders ({ &satDriveSlider, &satMixSlider });
                    break;

                case TabComp:
                    laySliders ({ &compThreshSlider, &compRatioSlider, &compAttackSlider,
                                  &compReleaseSlider, &compMakeupSlider });
                    break;

                case TabSweet:
                    laySliders ({ &swMixSlider, &swDepthSlider, &swWindowSlider,
                                  &swTameSlider, &swFreqSlider, &swDriveSlider,
                                  &swRndSlider, &swCeilSlider, &swRatioSlider });
                    break;

                case TabSends:
                    laySliders ({ &sendSliders[0], &sendSliders[1], &sendSliders[2] });
                    break;

                case TabPan:
                    laySliders ({ &panSlider });
                    break;

                default: break;
            }
        }

        void refreshFxEnableBtn (int t)
        {
            fxEnableBtns[t].setButtonText (fxEnableBtns[t].getToggleState() ? "ON" : "OFF");
        }

        void selectFxTab (int idx)
        {
            activeTab = juce::jlimit (0, (int) NumFxTabs - 1, idx);

            // Highlight active tab button.
            for (int t = 0; t < NumFxTabs; ++t)
            {
                const bool on = (t == activeTab);
                tabButtons[t].setColour (juce::TextButton::buttonColourId,
                                         on ? juce::Colour (Betel::Pal::kAccent)
                                            : juce::Colour (0xFF2A2A2A));
                // Black on the amber fill — see InstrEditStyle::styleSquareButton.
                tabButtons[t].setColour (juce::TextButton::textColourOffId,
                                         on ? juce::Colours::black : juce::Colours::white);
                tabButtons[t].setColour (juce::TextButton::textColourOnId,
                                         on ? juce::Colours::black : juce::Colours::white);
            }

            // Hide every FX slider, then show only the active tab's.
            for (auto& s : eqSliders) s.setVisible (false);
            // SIZE DEDUCED, deliberately.  This was a fixed [23] and adding
            // sliders above turned it into a C2078 rather than anything that
            // reads like the actual mistake.  Empty brackets cannot go stale.
            GoldSlider* nonEq [] = {
                &swMixSlider, &swDepthSlider, &swWindowSlider, &swTameSlider,
                &swFreqSlider, &swDriveSlider, &swRndSlider,
                &swCeilSlider, &swRatioSlider,
                &satDriveSlider, &satMixSlider,
                &compThreshSlider, &compRatioSlider, &compAttackSlider,
                &compReleaseSlider, &compMakeupSlider,
                &revSizeSlider, &revDampSlider, &revWetSlider, &revDrySlider,
                &revTailSlider, &revPreSlider,
                &delTimeSlider,  &delFbSlider,  &delDrySlider, &delWetSlider
            };
            for (auto* s : nonEq) s->setVisible (false);
            for (auto* b : { &swSoftBtn, &swPeakBtn, &swTameBtn, &swRoundBtn }) b->setVisible (false);
            panSlider.setVisible (false);
            revBand   .setVisible (false);
            revAlgoBtn.setVisible (false);
            showDelayRow (false);          // only the DEL tab shows the time row

            switch (activeTab)
            {
                case TabEq:   for (auto& s : eqSliders) s.setVisible (true); break;
                case TabSat:  satDriveSlider.setVisible (true); satMixSlider.setVisible (true); break;
                case TabComp: compThreshSlider.setVisible (true); compRatioSlider.setVisible (true);
                              compAttackSlider.setVisible (true); compReleaseSlider.setVisible (true);
                              compMakeupSlider.setVisible (true); break;
                case TabSweet:
                    for (auto* sl : { &swMixSlider, &swDepthSlider, &swWindowSlider,
                                      &swTameSlider, &swFreqSlider, &swDriveSlider,
                                      &swRndSlider, &swCeilSlider,
                                      &swRatioSlider }) sl->setVisible (true);
                    for (auto* b : { &swSoftBtn, &swPeakBtn, &swTameBtn,
                                     &swRoundBtn }) b->setVisible (true);
                    break;
                case TabPan:  panSlider.setVisible (true); break;
                case TabSends:
                    for (auto& s : sendSliders) s.setVisible (true);
                    break;
                default: break;
            }

            for (int t = 0; t < NumFxTabs; ++t) fxEnableBtns[t].setVisible (false);
            if (activeTab != TabPan && activeTab != TabSends)
                fxEnableBtns[activeTab].setVisible (true);
            
            layoutActiveFxTab();
            repaint();
        }

        //----------------------------------------------------------------------
        // Per-key element menu
        //----------------------------------------------------------------------
        void openPerKeyKitMenu (int midiKey)
        {
            const auto& e = kitState.keys[(size_t) midiKey];
            const uint8_t currentRole = e.sourceRoleId;
            const uint8_t roleForLookup = currentRole != 0
                ? currentRole
                : (uint8_t) Betel::DrumRoles::getGMRoleForMidiKey (midiKey);

            juce::PopupMenu menu;
            menu.addSectionHeader ("Source kit for "
                                   + keyName (midiKey)
                                   + " - "
                                   + juce::String (Betel::DrumRoles::getName (roleForLookup)));

            for (int i = 0; i < kNumKits; ++i)
            {
                const bool active = (e.sourceKit == kKitNames[i]);
                menu.addItem (1000 + i, kKitNames[i], true, active);
            }
            menu.addSeparator();
            menu.addItem (1, "(no source - silence this key)");

            const int btnIdx = midiKey - kKeyFirst;
            menu.showMenuAsync (juce::PopupMenu::Options()
                                    .withTargetComponent (&perKeyKitButtons[btnIdx]),
                                [this, midiKey, roleForLookup] (int chosen)
            {
                if (chosen == 0) return;
                if (chosen == 1)
                {
                    if (owner.onKeyElementChanged)
                        owner.onKeyElementChanged (midiKey, juce::String(), 0);
                    auto& key = kitState.keys[(size_t) midiKey];
                    key.sourceKit.clear();
                    key.sourceRoleId = 0;
                    refreshPerKeyButtonLabels();
                    repaint();
                    return;
                }
                const int kitIdx = chosen - 1000;
                if (kitIdx < 0 || kitIdx >= kNumKits) return;
                const juce::String newKit (kKitNames[kitIdx]);

                if (owner.onKeyElementChanged)
                    owner.onKeyElementChanged (midiKey, newKit, roleForLookup);

                auto& key = kitState.keys[(size_t) midiKey];
                key.sourceKit    = newKit;
                key.sourceRoleId = roleForLookup;
                refreshPerKeyButtonLabels();
                repaint();
            });
        }

        //----------------------------------------------------------------------
        //----------------------------------------------------------------------
        // COMPONENTS
        //
        // A component folder is not one drum.  tom/ is six (41, 43, 45, 47, 48,
        // 50); metal/ is about ten hats, rides and crashes.  Pressing TOM and
        // dragging GAIN moves ALL of them together - one control for the family,
        // which is what makes a ten-button selector worth having over a 49-key
        // keyboard.
        //
        // selectedKey stays the ANCHOR: it is the first key of the selected
        // component, and everything downstream (kick-mix visibility, audition,
        // the slider read-back) keeps working off it unchanged.  Only the WRITE
        // fans out.
        //----------------------------------------------------------------------

        /** Every MIDI key the given component covers in the loaded kit.

            Asked of the registry rather than inferred from role IDs: the folder
            is the truth, a role table would be a guess that drifts the first
            time a kit is rebuilt. */
        std::vector<int> keysForComponent (const juce::String& component) const
        {
            std::vector<int> out;
            if (component.isEmpty() || ! owner.onGetDrumKitRegistry) return out;

            const auto* reg = owner.onGetDrumKitRegistry();
            if (reg == nullptr) return out;

            const juce::String kitKey =
                DrumsPopup::registryKeyLike (kitState.lastLoadedKit);

            auto gather = [&out] (const std::vector<Betel::DrumKitRegistry::ElementHandle>& hs)
            {
                for (const auto& h : hs)
                    if (h.midiKey >= 0 && h.midiKey < 128
                        && std::find (out.begin(), out.end(), h.midiKey) == out.end())
                        out.push_back (h.midiKey);
            };

            const juce::String folder = folderForPage (component);

            // ── THE LOW-ZONE PAGES, ANSWERED BY RANGE ────────────────────────
            if (isLowZonePage (component))
            {
                const auto range = lowZoneRangeFor (component);

                for (const auto& f : lowZoneFolders())
                {
                    // Kit-filtered first, then unfiltered - the same two-step the
                    // folder path below uses, and for the same reason: a GLOBAL
                    // component is catalogued under its own stem, not the kit's,
                    // so the filtered query matches nothing for it.
                    gather (reg->getElementsForComponent (f, kitKey));
                    gather (reg->getElementsForComponent (f));
                }

                out.erase (std::remove_if (out.begin(), out.end(),
                                           [&range] (int k)
                                           { return k < range.first || k > range.second; }),
                           out.end());

                // ── THE FALLBACK IS FOR 23..34 ONLY, AND THAT IS NOT AN
                //    OVERSIGHT ────────────────────────────────────────────────
                //
                // xgLowKeySubstitute in Channel.cpp clones 25..34 from donors
                // already in the kit, so those keys sound on a library with no
                // low-zone blob at all and the page must be able to reach them.
                //
                // NOTHING clones 13..22.  Those keys exist only if revo_first or
                // gm_first is installed, so an empty UNDER GM page is the honest
                // answer rather than a bug - the same rule LOW KICK follows for
                // note 35.  Listing keys that carry no sample would give the
                // player sliders that write nowhere.
                if (out.empty() && component.trim().equalsIgnoreCase ("lower_gm"))
                    for (int k = range.first; k <= range.second; ++k)
                        out.push_back (k);

                // KEY 33 IS SOFT KICK'S, NOT LOWER GM'S.  It sits inside this
                // page's range, and an install still carrying the old the_second
                // would even supply it from a low-zone folder - but it has its
                // own page now, and one key under two pages is two sliders
                // disagreeing about one drum.  Dropped from both the gathered
                // keys and the no-blob fallback above.
                out.erase (std::remove (out.begin(), out.end(),
                                        Betel::DrumKitRegistry::kSoftKickKey),
                           out.end());

                std::sort (out.begin(), out.end());
                return out;
            }

            gather (reg->getElementsForComponent (folder, kitKey));

            // GLOBAL COMPONENTS DO NOT CARRY THE KIT'S NAME.
            //
            // A per-kit file (kick/000_standard.frb) is catalogued as kit "000",
            // but a global (the_last/the_lasts.frb) is catalogued under its own
            // stem - "the_lasts" - because it belongs to every kit rather than
            // one.  Filtering those by kitKey therefore matches nothing, and the
            // component would come back with no keys at all.
            //
            // That is not a cosmetic miss: with no keys the editor falls back to
            // the anchor key, so every category shows the SAME values and an
            // edit lands on whatever key the anchor happens to be rather than on
            // the drums you are looking at.  Retry unfiltered.
            if (out.empty())
                gather (reg->getElementsForComponent (folder));

            // ── THE PINNED GLOBALS ARE NOT IN THE CATALOG AT ALL ──────────────
            //
            // clap/ and kickmix/ are `continue`d by the registry scan and loaded
            // separately onto FIXED keys, so they own no ElementHandle and both
            // queries above come back empty however they are filtered.  Their
            // keys are known constants - the loader pins them - so name them
            // below.
            //
            // low_kick/ used to be in that list.  It is not any more: note 35
            // comes out of the kick blob with the rest of the kit, so it has a
            // real ElementHandle and needs no constant.
            // ── THE XG LOW KEYS BELONG TO THE LOW-ZONE COMPONENTS ─────────────
            //
            // Keys 25..34 used to carry no samples of their own: xgLowKeySubstitute
            // cloned each from a DONOR already in the kit, so each one was listed
            // on the page that owned its donor - 25..29/31 on SNARE, 30/32/34 on
            // STICK, 33 on KICK.
            //
            // They are supplied for real now - the_second over 23..34, and
            // revo_first overriding 25..28 on a Revo! kit - as GLOBALS merged
            // into every kit.  So they are their own family and appear on their
            // own page (LOWER GM).  Listing them on the donor pages as well
            // would put one key under two sets of channel controls, which is how
            // two sliders end up disagreeing about one drum.
            //
            // THE FALLBACK IS KEPT AND IS NOT A DUPLICATE.  A library with no
            // low-zone blob still gets the clones from composeDrumKit, and the
            // query above returns nothing for this page - so the keys are added
            // here only when the registry did not already supply them.
            {
                const auto c = component.trim().toLowerCase();
                auto addAll = [&out] (std::initializer_list<int> ks)
                {
                    for (int k : ks)
                        if (std::find (out.begin(), out.end(), k) == out.end())
                            out.push_back (k);
                };

                // the_first's fallback used to live here, listing all of
                // 13..34.  It has moved into the low-zone branch above, which
                // returns before this point and applies it to LOWER GM only -
                // see the note there on why 13..22 gets none.
                juce::ignoreUnused (addAll);
            }

            //------------------------------------------------------------------
            // SPLIT THE ONE KICK FOLDER INTO THREE PAGES.
            //
            // Everything above answered "what keys does kick/ carry", which is
            // now all three kicks at once -- the re-sampled kit blobs hold 35
            // and 36 in a single SFZ, and the two key-33 blobs sit beside them.
            // The three drums want their own channel controls, so each page
            // keeps only its own share:
            //
            //   SOFT KICK -> 33 alone (the family's soft or EDM kick)
            //   LOW KICK  -> 35 alone
            //   KICK      -> everything else the kit's kick file carries (36)
            //
            // Doing it here rather than in the registry keeps the catalog
            // honest about what the file contains, and puts the split in the one
            // place that cares -- the editor.
            //------------------------------------------------------------------
            {
                const auto c = component.trim().toLowerCase();
                if (c == "low_kick")
                {
                    const bool present = std::find (out.begin(), out.end(), kLowKickKey)
                                             != out.end();
                    // If this kit's blob has no 35 the page shows "no keys" and
                    // edits nothing, which is the truth.  It must NOT fall back
                    // to a hard-coded { 35 }: that is what the old pinned global
                    // guaranteed, and guaranteeing a key that carries no sample
                    // is how a slider ends up writing nowhere.
                    out.clear();
                    if (present) out.push_back (kLowKickKey);
                }
                else if (c == "soft_kick")
                {
                    // KEY 33 SOUNDS ON THIS KIT IF ANY OF THREE THINGS SUPPLIES
                    // IT: a key-33 blob (33_soft_kick / 33_edm_kick), an older
                    // low-zone blob that still carries 33, or - failing both -
                    // composeDrumKit's clone of the kit's own kick, which needs
                    // the kit to HAVE a kick.  `out` holds exactly that at this
                    // point: the keys this kit's kick/ files map.
                    //
                    // Same honesty rule as LOW KICK: if nothing can make 33
                    // sound the page shows "no keys" rather than a slider that
                    // writes nowhere.  Which of the two blobs a kit gets is not
                    // this page's business - the engine picks by kit family, and
                    // the editor writes per-KEY parameters, so an edit to 33
                    // lands on whichever sample is there.
                    bool sounds33 = ! out.empty();
                    for (const auto& gName : reg->getGlobalComponentNames())
                    {
                        if (sounds33) break;
                        for (const auto& h : reg->getElementsForKit (gName))
                            if (h.midiKey == Betel::DrumKitRegistry::kSoftKickKey)
                            { sounds33 = true; break; }
                    }

                    out.clear();
                    if (sounds33) out.push_back (Betel::DrumKitRegistry::kSoftKickKey);
                }
                else if (c == "kick")
                {
                    // KICK owns 36 and whatever else the kit's kick file maps -
                    // never 35 (LOW KICK's) and never 33 (SOFT KICK's).  33 can
                    // reach this list on the unfiltered retry above, because the
                    // two key-33 blobs live in kick/ as globals.
                    out.erase (std::remove_if (out.begin(), out.end(),
                                               [] (int k)
                                               {
                                                   return k == kLowKickKey
                                                       || k == Betel::DrumKitRegistry::kSoftKickKey;
                                               }),
                               out.end());
                }
                else if (out.empty() && c == "clap")
                {
                    out = { 39 };   // still a pinned global
                }
                // The low-zone components need no case here: revo_first,
                // gm_first and the_second are catalogued like any other, and
                // both low-zone PAGES return from the branch further up before
                // ever reaching this block.
            }

            std::sort (out.begin(), out.end());
            return out;
        }

        /** Select a component: remember it, anchor on its lowest key, and show
            the replacement droplist ONLY when the folder holds more than one
            file.  A folder with a single file - clap/ typically - offers a choice
            that does not exist, so it offers none. */
        /** Ask the registry what folders exist and label the row from them.

            Called whenever a kit lands, so a library rescan is picked up without
            the popup having to be reopened. */
        /** The row, in PLAYING ORDER with the names the user asked for.

            Fixed rather than whatever the folder scan happens to return: the
            order runs bottom of the kit to top - under-GM, kicks, then the
            middle, then cymbals and toms, then everything above GM - which is
            how a kit is thought about, not how a directory sorts.

            kickmix is deliberately absent.  It is not an element: it is the
            supplemental layer blended into the two kick keys, and it already has
            its own MIX control on the kick page. */
        struct ComponentEntry { const char* folder; const char* label; };

        //----------------------------------------------------------------------
        // PAGE ID vs FOLDER.
        //
        // These used to be the same string, because every page was a folder.
        // LOW KICK broke that: the re-sampled kick blobs carry note 35 and note
        // 36 in one SFZ, so both kick pages now read the SAME folder and are
        // told apart by KEY instead.
        //
        // The page id stays "low_kick" so nothing that remembers a selected
        // page has to change, and everything that asks the registry a question
        // -- what elements, are there alternatives -- resolves to the folder
        // first.
        //----------------------------------------------------------------------
        static juce::String folderForPage (const juce::String& pageId)
        {
            // SOFT KICK joins LOW KICK here: its two blobs live in kick/ too, so
            // every registry question the page asks goes to that folder.
            const auto p = pageId.trim();
            return (p.equalsIgnoreCase ("low_kick") || p.equalsIgnoreCase ("soft_kick"))
                       ? juce::String ("kick")
                       : pageId;
        }

        //----------------------------------------------------------------------
        // THE TWO LOW-ZONE PAGES ARE KEY RANGES, NOT FOLDERS.
        //
        // UNDER GM used to be one page over one folder, the_first, covering the
        // whole of 13..34.  That folder is gone: the span is now served by three
        // components, and WHICH of them supplies a key depends on the kit.
        //
        //     13..22   revo_first | gm_first   (kit-dependent)
        //     23..24   the_second               (common)
        //     25..28   revo_first overrides the_second
        //     29..34   the_second               (common)
        //
        // A page tied to a FOLDER cannot express that.  It would have to pick
        // revo_first or gm_first without knowing which kit is loaded, and it
        // would show 25..28 on whichever page owned revo_first rather than
        // alongside the neighbouring keys they sound with.
        //
        // So these two pages ask a different question - WHICH KEYS IN MY RANGE
        // does this kit actually map - and gather across every low-zone folder
        // to answer it.  That is exact rather than approximate: revo_first and
        // gm_first are mutually exclusive within one kit (resolveDrumKitParams
        // merges one and skips the other), so a union of the two can never show
        // a key twice or a key the kit does not have.
        //
        // It also makes the edit land correctly with no further thought: the
        // editor writes per-KEY parameters, so an edit to key 13 reaches
        // whichever component supplied 13, without the page ever having to know
        // which one that was.
        //
        // the_first is still in the folder list so a library that has NOT been
        // re-blobbed keeps both pages working off the old single component.
        //----------------------------------------------------------------------
        static constexpr int kUnderGmLoKey = 13, kUnderGmHiKey = 22;
        static constexpr int kLowerGmLoKey = 23, kLowerGmHiKey = 34;

        static bool isLowZonePage (const juce::String& pageId)
        {
            const auto p = pageId.trim().toLowerCase();
            return p == "under_gm" || p == "lower_gm";
        }

        static std::pair<int, int> lowZoneRangeFor (const juce::String& pageId)
        {
            return pageId.trim().equalsIgnoreCase ("under_gm")
                     ? std::pair<int, int> { kUnderGmLoKey, kUnderGmHiKey }
                     : std::pair<int, int> { kLowerGmLoKey, kLowerGmHiKey };
        }

        /** Every component that can supply a key below 35, newest first.  Order
            is immaterial - the gather de-duplicates by key - but keeping the
            live three ahead of legacy the_first documents which is which. */
        static const std::vector<juce::String>& lowZoneFolders()
        {
            static const std::vector<juce::String> f {
                "revo_first", "gm_first", "the_second", "the_first"
            };
            return f;
        }

        /** MIDI 35: the sub-kick, and the ONLY key the LOW KICK page owns.  KICK
            takes every other key its folder carries.  Splitting on one number
            rather than on the role tag is deliberate -- the two kicks live in one
            SFZ and may well share a role, but they can never share a key. */
        static constexpr int kLowKickKey = 35;

        static const std::vector<ComponentEntry>& componentOrder()
        {
            // UNDER GM AND LOWER GM ARE REAL PAGES.
            //
            // The sub-GM keys used to be excluded from the row entirely: their
            // blob was skipped by the scan and everything in 25..34 was a CLONE
            // of a donor that KICK, SNARE or STICK already owned.  Both halves of
            // that changed - the skip went, and real blobs now supply the span -
            // so those keys belong to their own pages rather than being borrowed
            // from three others.  They lead the row because they own the lowest
            // keys in the kit.
            //
            // TWO PAGES, NOT ONE, because the span is not uniform: 13..22 is
            // kit-dependent (revo_first or gm_first) and 23..34 is shared, with
            // revo_first overriding 25..28 on a Revo! kit.  See the note above
            // keysForComponent on why both are keyed by RANGE and not by folder.
            //
            // xgLowKeySubstitute in Channel.cpp is untouched and still stands as
            // the fallback for a library with no low-zone blob: composeDrumKit
            // skips any key a real element already maps, so installing the blobs
            // simply switches the clones off.
            //
            // LOW KICK AND KICK ARE TWO PAGES OVER ONE FOLDER.  Both resolve to
            // kick/, split by key: LOW KICK owns 35, KICK owns the rest.  They
            // are kept apart because they are two drums with two sets of
            // channel controls, which is the whole point -- but they are now one
            // FILE, so the REPLACE droplist on either page swaps both at once.
            // SOFT KICK IS THE THIRD PAGE OVER kick/.  Key 33 has its own two
            // blobs there (33_soft_kick / 33_edm_kick, one chosen per kit
            // family), so it is a drum with its own channel controls rather than
            // one of LOWER GM's XG fillers.  It sits before LOW KICK because 33
            // is below 35, which keeps the row in playing order.
            static const std::vector<ComponentEntry> order = {
                { "under_gm",  "UNDER GM" },
                { "lower_gm",  "LOWER GM" },
                { "soft_kick", "SOFT KICK" },
                { "low_kick",  "LOW KICK" },
                { "kick",      "KICK"     },
                { "stick",     "STICK"    },
                { "snare",     "SNARE"    },
                { "clap",      "CLAP"     },
                { "metal",     "CYMBALS"  },
                { "tom",       "TOMS"     },
                { "the_last",  "REST GM"  }
            };
            return order;
        }

        void rebuildComponentRow()
        {
            componentNames.clear();

            std::vector<juce::String> present;
            if (owner.onGetDrumKitRegistry)
                if (const auto* reg = owner.onGetDrumKitRegistry())
                    present = reg->getComponentNames();

            auto have = [&present] (const juce::String& f)
            {
                for (const auto& p : present) if (p.equalsIgnoreCase (f)) return true;
                return false;
            };

            for (const auto& e : componentOrder())
            {
                if ((int) componentNames.size() >= kMaxComponents) break;
                // clap is a pinned global the scan skips, so it is shown whether
                // or not the folder walk reported it.  LOW KICK is no longer in
                // that category: it is a view onto kick/, so it appears exactly
                // when kick/ does -- which is also what makes it disappear
                // honestly if the kick folder is missing, instead of offering a
                // page with nothing behind it.
                const juce::String f (e.folder);

                // A low-zone PAGE is present when ANY component that can feed
                // its range is installed - it is not a folder, so asking for one
                // by name would hide both pages on every library.
                const bool present =
                    isLowZonePage (f)
                        ? std::any_of (lowZoneFolders().begin(), lowZoneFolders().end(),
                                       [&have] (const juce::String& g) { return have (g); })
                        : have (folderForPage (f));

                if (! present && f != "clap") continue;

                componentButtons[(int) componentNames.size()].setButtonText (e.label);
                componentNames.push_back (f);
            }
        }

        void refreshComponentButtons()
        {
            for (int i = 0; i < kMaxComponents; ++i)
            {
                if (i >= (int) componentNames.size()) continue;
                InstrEditStyle::styleSquareButton (
                    componentButtons[i],
                    componentNames[(size_t) i].equalsIgnoreCase (selectedComponent));
            }
        }

        void selectComponent (const juce::String& component)
        {
            selectedComponent = component;
            componentKeys     = keysForComponent (component);

            if (! componentKeys.empty())
                selectedKey = anchorKeyFor (componentKeys);

            bool hasAlts = false;
            if (owner.onGetDrumKitRegistry)
                if (const auto* reg = owner.onGetDrumKitRegistry())
                    hasAlts = reg->componentHasAlternatives (folderForPage (component));

            // NO REPLACE ON SOFT KICK.  The page reads kick/, which does have
            // alternatives - but those are the KITS' kick files (35 + 36), and a
            // swap started here would re-source those two drums from a page that
            // shows neither.  Key 33's own sample is picked by kit family, not by
            // the user, so there is nothing on this page to swap.
            if (component.trim().equalsIgnoreCase ("soft_kick"))
                hasAlts = false;

            componentSwapBtn.setVisible (hasAlts);

            refreshComponentButtons();
            refreshSelectedKeySliders();

            // Say what is actually being edited.  "TOM - 6 keys" is the
            // difference between trusting the fan-out and wondering whether the
            // slider did anything.
            slotLabel = owner.fullKitName.isNotEmpty()
                          ? "EDITING DRUMS | " + owner.fullKitName.toUpperCase()
                          : selectedComponent.toUpperCase()
                              + (componentKeys.empty()
                                    ? juce::String (" - no keys in this kit")
                                    : " - " + juce::String ((int) componentKeys.size()) + " keys");
            resized();
            repaint();
        }

        //----------------------------------------------------------------------
        // THE KEY A PAGE OPENS ON.
        //
        // This used to be keys.front(), i.e. the LOWEST key -- which put every
        // page that owns an XG clone onto the clone instead of onto the drum the
        // page is named after.  The clones are 25..34, borrowed from a donor by
        // xgLowKeySubstitute, so:
        //
        //     KICK   opened on 33 (XG soft kick)  instead of 36
        //     SNARE  opened on 25 (brush tap)     instead of 38
        //     STICK  opened on 30 (castanet)      instead of 37
        //
        // For KICK that is why the EDM/WOOD kick-mix controls vanished: they are
        // shown only while a kick-mix key (35 or 36) is selected, and 33 is
        // neither.  The controls were reachable ONLY from the LOW KICK page,
        // which anchored on 35 -- so the feature looked like it belonged to the
        // low kick and disappeared everywhere else.
        //
        // For SNARE and STICK it was the quieter version of the same fault: the
        // page opened showing a brush tap's values, and the first slider move
        // landed on a drum the user was not looking at.
        //
        // Keys arrive sorted, so the first one at 35 or above is the lowest key
        // the component really owns.  A page with nothing but clones keeps
        // front() rather than losing its anchor entirely.
        //----------------------------------------------------------------------
        static int anchorKeyFor (const std::vector<int>& keys)
        {
            for (const int k : keys)
                if (k >= 35) return k;
            return keys.front();
        }

        /** The keys an edit should land on.  Falls back to the anchor alone, so
            a component the registry knows nothing about still edits something
            rather than silently editing nothing. */
        std::vector<int> keysToEdit() const
        {
            // FULL KIT: the whole thing at once.  There are no components to
            // narrow it down to, and a per-key edit on a sampled kit would be
            // editing one region of an instrument the user thinks of as one
            // sound.
            if (owner.fullKitName.isNotEmpty())
            {
                std::vector<int> all;
                for (int k = 0; k < 128; ++k)
                    if (kitState.keys[(size_t) k].gain > 0.0f || true) all.push_back (k);
                return all;
            }

            if (! componentKeys.empty()) return componentKeys;

            // NOTHING resolved for this component.  Editing the anchor key would
            // silently move a drum the user is not looking at, which is worse
            // than doing nothing - so do nothing, and let the empty label say so.
            return {};
        }

        //----------------------------------------------------------------------
        // Per-key sliders <-> state
        //----------------------------------------------------------------------
        void refreshSelectedKeySliders()
        {
            updatingFromState = true;
            const auto& e = kitState.keys[(size_t) selectedKey];
            gainSlider   .setValue (Map::gainToSlider  (e.gain),                          juce::dontSendNotification);
            pitchSlider  .setValue (Map::pitchToSlider (e.pitch),                         juce::dontSendNotification);
            rrSlider     .setValue (Map::rrToSlider    (e.roundRobinAmount),              juce::dontSendNotification);
            velCurveBox  .setCurve (e.velCurve, juce::dontSendNotification);
            bandFilter   .setValues (e.filterHpNorm, e.filterLpNorm, false /* no notify */);
            lengthSlider .setValue (e.length,                                             juce::dontSendNotification);

            // Kick MIX — loaded for every key (harmless on non-kick keys; the
            // controls are hidden there anyway).
            kickMixSlider    .setValue       (e.kickMixAmount,        juce::dontSendNotification);
            kickMixEnableBtn .setToggleState (e.kickMixEnabled,       juce::dontSendNotification);
            kickMixVariantBtn.setToggleState (e.kickMixVariant == 1,  juce::dontSendNotification);
            updatingFromState = false;

            refreshKickMixControls();
            updateKickMixVisibility();
        }

        void writeSliderToKey()
        {
            if (updatingFromState) return;

            // EVERY key in the selected component, not just the anchor.  One
            // GAIN for all six toms - see the note above keysForComponent.
            for (const int k : keysToEdit())
            {
                if (k < 0 || k >= 128) continue;
                auto& e = kitState.keys[(size_t) k];

                e.gain           = Map::sliderToGain  (gainSlider   .getValue());
                e.pitch          = Map::sliderToPitch (pitchSlider  .getValue());
                e.roundRobinAmount = Map::sliderToRr  (rrSlider     .getValue());
                e.velCurve       = velCurveBox.getCurve();
                e.filterHpNorm   = bandFilter.getLo();
                e.filterLpNorm   = bandFilter.getHi();
                e.length         = lengthSlider .getValue();
                // Attack fixed at lowest (instant); every key plays its full sample.
                e.attack         = 0.0f;
                e.release        = 0.0f;
                e.fullLength     = true;

                // Kick MIX - written only on the two kick keys so every other
                // key keeps the struct defaults untouched (the engine ignores
                // kickMix* elsewhere).
                if (isKickMixKey (k))
                {
                    e.kickMixEnabled = kickMixEnableBtn.getToggleState();
                    e.kickMixAmount  = kickMixSlider.getValue();
                    e.kickMixVariant = kickMixVariantBtn.getToggleState() ? 1 : 0;
                }

                if (owner.onKeyParamsChanged)
                    owner.onKeyParamsChanged (k, e);
            }
        }

        /** The replacement droplist for the selected component.  Built from the
            FOLDER, so it lists exactly the kits that actually have a file there
            - never a kit whose folder is missing that component.

            LOW KICK resolves to kick/ like the KICK page does, so both offer the
            same list -- and picking from either moves BOTH kicks, because after
            the re-sample they are two keys in one file.  That is stated in the
            menu header rather than hidden: a swap that quietly changed a drum on
            another page would be worse than a swap that says it will. */
        void openComponentSwapMenu()
        {
            if (! owner.onGetDrumKitRegistry) return;
            const auto* reg = owner.onGetDrumKitRegistry();
            if (reg == nullptr) return;

            const auto folder = folderForPage (selectedComponent);
            const auto kits   = reg->getKitsForComponent (folder);
            if (kits.size() < 2) return;      // nothing to choose between

            const auto currentKit = registryKeyLike (kitState.lastLoadedKit);
            const bool sharedKick = folder.equalsIgnoreCase ("kick");

            juce::PopupMenu menu;
            menu.addSectionHeader (sharedKick
                                     ? juce::String ("Replace KICK + LOW KICK with")
                                     : "Replace " + selectedComponent.toUpperCase()
                                           + " with");

            for (int i = 0; i < (int) kits.size(); ++i)
            {
                juce::String label = kits[(size_t) i];
                for (int k = 0; k < kNumKits; ++k)
                    if (label.equalsIgnoreCase (kKitKeys[k])) { label = kKitNames[k]; break; }

                menu.addItem (2000 + i, label, true,
                              kits[(size_t) i].equalsIgnoreCase (currentKit));
            }

            menu.showMenuAsync (juce::PopupMenu::Options()
                                    .withTargetComponent (&componentSwapBtn),
                                [this, kits] (int chosen)
            {
                const int idx = chosen - 2000;
                if (idx < 0 || idx >= (int) kits.size()) return;
                swapComponentToKit (kits[(size_t) idx]);
            });
        }

        /** Swap the WHOLE component to another kit's version of it - every key
            it covers, in one move.  This is what the droplist under each
            component button does. */
        void swapComponentToKit (const juce::String& newKitKey)
        {
            // WHICH KEYS MOVE.  Normally the page's own, but the two kick pages
            // share one file: swapping from LOW KICK while leaving 36 on the old
            // kit would ask the engine for half of each blob, which the file
            // layout cannot deliver.  So a kick swap moves the folder's whole key
            // set regardless of which of the two pages started it.
            std::vector<int> keys = keysToEdit();

            if (folderForPage (selectedComponent).equalsIgnoreCase ("kick"))
            {
                for (const int k : keysForComponent ("kick"))
                    if (std::find (keys.begin(), keys.end(), k) == keys.end())
                        keys.push_back (k);
                for (const int k : keysForComponent ("low_kick"))
                    if (std::find (keys.begin(), keys.end(), k) == keys.end())
                        keys.push_back (k);

                // AND NEVER KEY 33.  A kit's kick file carries 35 and 36 only;
                // re-sourcing 33 onto it would ask for an element the file does
                // not have and silence the soft kick.  SOFT KICK hides REPLACE,
                // and KICK never lists 33, so this cannot normally be reached -
                // it is here so that no path through a kick swap ever can.
                keys.erase (std::remove (keys.begin(), keys.end(),
                                         Betel::DrumKitRegistry::kSoftKickKey),
                            keys.end());
            }

            for (const int k : keys)
            {
                if (k < 0 || k >= 128) continue;
                auto& key = kitState.keys[(size_t) k];

                const uint8_t role = key.sourceRoleId != 0
                    ? key.sourceRoleId
                    : (uint8_t) Betel::DrumRoles::getGMRoleForMidiKey (k);

                key.sourceKit    = newKitKey;
                key.sourceRoleId = role;

                if (owner.onKeyElementChanged)
                    owner.onKeyElementChanged (k, newKitKey, role);
            }
            refreshSelectedKeySliders();
            repaint();
        }

        //----------------------------------------------------------------------
        // FX sliders <-> state
        //----------------------------------------------------------------------
        void refreshFxSlidersFromState()
        {
            updatingFromState = true;

            fxWetSlider.setValue (Map::toSlider (kitState.fx.fxWet, 0.0f, 1.0f), juce::dontSendNotification);

            // The sends.  revWet / delWet / sweet.mix are the fields that always
            // existed — a kit that had reverb arrives with a reverb send.
            setSendVal (kSendChorus, kitState.fx.chorusSend);
            setSendVal (kSendReverb, kitState.fx.revWet);
            setSendVal (kSendDelay,  kitState.fx.delWet);

            // EQ — piecewise mapping centred on unity (dB 0 → slider 50).
            for (int i = 0; i < 10; ++i)
                eqSliders[i].setValue (Map::eqDbToSlider (kitState.fx.eqGainDb[i]),
                                       juce::dontSendNotification);

            satDriveSlider.setValue (Map::toSlider (kitState.fx.satDrive,     0.0f, 1.0f),  juce::dontSendNotification);
            satMixSlider  .setValue (Map::toSlider (kitState.fx.satMix,       0.0f, 1.0f),  juce::dontSendNotification);

            compThreshSlider .setValue (Map::toSlider (kitState.fx.compThreshDb,  -60.0f, 0.0f),    juce::dontSendNotification);
            compRatioSlider  .setValue (Map::toSlider (kitState.fx.compRatio,     1.0f,   20.0f),   juce::dontSendNotification);
            compAttackSlider .setValue (Map::toSlider (kitState.fx.compAttackMs,  0.1f,   200.0f),  juce::dontSendNotification);
            compReleaseSlider.setValue (Map::toSlider (kitState.fx.compReleaseMs, 5.0f,   2000.0f), juce::dontSendNotification);
            compMakeupSlider .setValue (Map::toSlider (kitState.fx.compMakeupDb,  0.0f,   24.0f),   juce::dontSendNotification);

            revSizeSlider.setValue (Map::toSlider (kitState.fx.revSize, 0.0f, 1.0f), juce::dontSendNotification);
            revDampSlider.setValue (Map::toSlider (kitState.fx.revDamp, 0.0f, 1.0f), juce::dontSendNotification);
            revWetSlider .setValue (Map::toSlider (kitState.fx.revWet,  0.0f, 1.0f), juce::dontSendNotification);
            revDrySlider .setValue (Map::toSlider (kitState.fx.revDry,  0.0f, 1.0f), juce::dontSendNotification);
            revTailSlider.setValue (Map::toSlider (kitState.fx.revTail, 0.0f, 1.0f), juce::dontSendNotification);
            revPreSlider .setValue (Map::toSlider (kitState.fx.revPreDelay, 0.0f, 1.0f), juce::dontSendNotification);
            revBand.setValues (kitState.fx.revHpNorm, kitState.fx.revLpNorm, false /* no notify */);
            revAlgoIdx = juce::jlimit (0, 1, kitState.fx.revAlgo);
            refreshRevAlgoBtn();
            revWetBase = kitState.fx.revWetBase;
            delWetBase = kitState.fx.delWetBase;

            delSyncOn  = kitState.fx.delSync;
            delTimeSig = juce::jlimit (0, 1, kitState.fx.delTimeSig);
            delDivIdx  = juce::jmax  (0,    kitState.fx.delDiv);
            refreshDelayRow();

            delTimeSlider.setValue (Map::toSlider (kitState.fx.delTimeMs, 1.0f, 2000.0f), juce::dontSendNotification);
            delFbSlider  .setValue (Map::toSlider (kitState.fx.delFb,     0.0f, 0.95f),   juce::dontSendNotification);
            delDrySlider .setValue (Map::toSlider (kitState.fx.delDry,    0.0f, 1.0f),    juce::dontSendNotification);
            delWetSlider .setValue (Map::toSlider (kitState.fx.delWet,    0.0f, 1.0f),    juce::dontSendNotification);

            panSlider    .setValue (kitState.fx.pan * 50.0f + 50.0f,                          juce::dontSendNotification);

            {
                const auto& sw = kitState.fx.sweet;
                swMixSlider   .setValue (Map::toSlider (sw.mix,         0.0f,    1.0f),    juce::dontSendNotification);
                swDepthSlider .setValue (Map::toSlider (sw.softenDepth, -1.0f,   1.0f),    juce::dontSendNotification);
                swWindowSlider.setValue (Map::toSlider (sw.softenMs,    1.0f,  150.0f),    juce::dontSendNotification);
                swCeilSlider  .setValue (Map::toSlider (sw.peakCeilDb,  3.0f,   24.0f),    juce::dontSendNotification);
                swRatioSlider .setValue (PeakRatio::toUi01 (sw.peakRatio) * 100.0f,        juce::dontSendNotification);
                swPeakBtn .setToggleState (sw.peakOn,   juce::dontSendNotification);
                swTameSlider  .setValue (Map::toSlider (sw.tameDepthDb, 0.0f,   12.0f),    juce::dontSendNotification);
                swFreqSlider  .setValue (Map::toSlider (sw.tameFreqHz,  1500.0f, 8000.0f), juce::dontSendNotification);
                swDriveSlider .setValue (Map::toSlider (sw.roundDrive,  0.0f,    1.0f),    juce::dontSendNotification);
                swRndSlider   .setValue (Map::toSlider (sw.roundMix,    0.0f,    1.0f),    juce::dontSendNotification);
                swSoftBtn .setToggleState (sw.softenOn, juce::dontSendNotification);
                swTameBtn .setToggleState (sw.tameOn,   juce::dontSendNotification);
                swRoundBtn.setToggleState (sw.roundOn,  juce::dontSendNotification);
                styleSweetStages();   // dontSendNotification skips onClick
            }

            // ORDER FOLLOWS THE FxTab ENUM: Eq, Sat, Comp, Pan, Sends.  PAN and
            // SENDS have no enable button — pan is always in effect, and a send
            // of zero is already off — so both are false and their buttons stay
            // hidden (see the guard in selectFxTab).
            const bool fxEn[NumFxTabs] = { kitState.fx.eqEnabled,
                                           kitState.fx.satEnabled,
                                           kitState.fx.compEnabled,
                                           kitState.fx.sweet.enabled,
                                           false, false };
            for (int t = 0; t < NumFxTabs; ++t)
            {
                fxEnableBtns[t].setToggleState (fxEn[t], juce::dontSendNotification);
                refreshFxEnableBtn (t);
            }

            updatingFromState = false;
        }

        void writeFxFromUi()
        {
            if (updatingFromState) return;

            for (int i = 0; i < 10; ++i)
                kitState.fx.eqGainDb[i] = Map::sliderToEqDb (eqSliders[i].getValue());

            kitState.fx.satDrive = Map::toEngine (satDriveSlider.getValue(), 0.0f, 1.0f);
            kitState.fx.satMix   = Map::toEngine (satMixSlider  .getValue(), 0.0f, 1.0f);

            kitState.fx.compThreshDb  = Map::toEngine (compThreshSlider .getValue(), -60.0f, 0.0f);
            kitState.fx.compRatio     = Map::toEngine (compRatioSlider  .getValue(), 1.0f,   20.0f);
            kitState.fx.compAttackMs  = Map::toEngine (compAttackSlider .getValue(), 0.1f,   200.0f);
            kitState.fx.compReleaseMs = Map::toEngine (compReleaseSlider.getValue(), 5.0f,   2000.0f);
            kitState.fx.compMakeupDb  = Map::toEngine (compMakeupSlider .getValue(), 0.0f,   24.0f);

            kitState.fx.revSize = Map::toEngine (revSizeSlider.getValue(), 0.0f, 1.0f);
            kitState.fx.revDamp = Map::toEngine (revDampSlider.getValue(), 0.0f, 1.0f);
            kitState.fx.revWet  = Map::toEngine (revWetSlider .getValue(), 0.0f, 1.0f);
            kitState.fx.revDry  = Map::toEngine (revDrySlider .getValue(), 0.0f, 1.0f);
            kitState.fx.revTail = Map::toEngine (revTailSlider.getValue(), 0.0f, 1.0f);
            kitState.fx.revPreDelay = Map::toEngine (revPreSlider.getValue(), 0.0f, 1.0f);
            kitState.fx.revHpNorm   = revBand.getLo();
            kitState.fx.revLpNorm   = revBand.getHi();
            kitState.fx.revAlgo     = revAlgoIdx;
            kitState.fx.revWetBase  = revWetBase;
            kitState.fx.delWetBase  = delWetBase;

            kitState.fx.delSync    = delSyncOn;
            kitState.fx.delTimeSig = delTimeSig;
            kitState.fx.delDiv     = delDivIdx;
            kitState.fx.delTimeMs  = Map::toEngine (delTimeSlider.getValue(), 1.0f, 2000.0f);
            kitState.fx.delFb     = Map::toEngine (delFbSlider  .getValue(), 0.0f, 0.95f);
            kitState.fx.delDry    = Map::toEngine (delDrySlider .getValue(), 0.0f, 1.0f);
            kitState.fx.delWet    = Map::toEngine (delWetSlider .getValue(), 0.0f, 1.0f);
            kitState.fx.pan       = (panSlider.getValue() - 50.0f) / 50.0f;
            kitState.fx.fxWet     = Map::toEngine (fxWetSlider.getValue(), 0.0f, 1.0f);

            {
                auto& sw = kitState.fx.sweet;

                // sw.mix IS NOT SET HERE.  swMixSlider belongs to the SWEET page,
                // which no longer exists — the widget survives only because the
                // sweetener's other controls are read from its siblings — and it
                // still sits at its construction default of 100.  Reading it
                // wrote a full-strength mix on every write-back, and the enable
                // derived from it below then latched the sweetener ON.  Toggling
                // the EQ was enough to trigger it.
                //
                // The mix is the SEND now, set further down with the other five.
                sw.softenDepth = Map::toEngine (swDepthSlider .getValue(), -1.0f,   1.0f);
                sw.softenMs    = Map::toEngine (swWindowSlider.getValue(), 1.0f,  150.0f);
                sw.peakCeilDb  = Map::toEngine (swCeilSlider  .getValue(), 3.0f,   24.0f);
                sw.peakRatio   = PeakRatio::toNative ((float) swRatioSlider.getValue() * 0.01f);
                sw.peakOn      = swPeakBtn .getToggleState();
                sw.tameDepthDb = Map::toEngine (swTameSlider  .getValue(), 0.0f,   12.0f);
                sw.tameFreqHz  = Map::toEngine (swFreqSlider  .getValue(), 1500.0f, 8000.0f);
                sw.roundDrive  = Map::toEngine (swDriveSlider .getValue(), 0.0f,    1.0f);
                sw.roundMix    = Map::toEngine (swRndSlider   .getValue(), 0.0f,    1.0f);
                sw.softenOn    = swSoftBtn .getToggleState();
                sw.tameOn      = swTameBtn .getToggleState();
                sw.roundOn     = swRoundBtn.getToggleState();
            }

            kitState.fx.eqEnabled   = fxEnableBtns[TabEq ].getToggleState();
            kitState.fx.satEnabled  = fxEnableBtns[TabSat].getToggleState();
            kitState.fx.compEnabled = fxEnableBtns[TabComp].getToggleState();
            // ── THE THREE SENDS ──────────────────────────────────────────────
            //
            // revWet and delWet are the fields that already existed and already
            // persisted; the chorus send is the new one.  Each enable is DERIVED
            // from its send, so one slider means one thing and there is no second
            // switch to leave in the wrong position.
            kitState.fx.revWet     = getSendVal (kSendReverb);
            kitState.fx.delWet     = getSendVal (kSendDelay);
            kitState.fx.chorusSend = getSendVal (kSendChorus);

            kitState.fx.revEnabled   = kitState.fx.revWet    > 0.0f;
            kitState.fx.delEnabled   = kitState.fx.delWet    > 0.0f;

            // AFTER the send is set, not before.  Deriving this while sw.mix
            // still held a stale slider reading is what switched the sweetener
            // on by itself.
            // The sweetener is an INSERT again, so its mix is its own control on
            // the SWEET page and its enable is that page's button — not derived
            // from a send that no longer exists.
            kitState.fx.sweet.mix     = Map::toEngine (swMixSlider.getValue(), 0.0f, 1.0f);
            kitState.fx.sweet.enabled = fxEnableBtns[TabSweet].getToggleState();

            // A send has no base — the ceiling is the rack's own level now.
            kitState.fx.revWetBase  = 1.0f;
            kitState.fx.delWetBase  = 1.0f;

            if (owner.onKitFxChanged) owner.onKitFxChanged (kitState.fx);
        }

        //----------------------------------------------------------------------
        // Per-key kit-button labels.  Format: "E <abbrev>" or "E -".
        //----------------------------------------------------------------------
        void refreshPerKeyButtonLabels()
        {
            for (int k = 0; k < kKeyCount; ++k)
            {
                const int mk = kKeyFirst + k;
                if (isSharedComponentKey (mk))
                {
                    perKeyKitButtons[k].setVisible (false);
                    continue;
                }
                const auto& e = kitState.keys[(size_t) mk];
                int kitIdx = -1;
                for (int i = 0; i < kNumKits; ++i)
                    if (e.sourceKit == kKitNames[i]) { kitIdx = i; break; }

                auto& b = perKeyKitButtons[k];
                if (kitIdx >= 0)
                {
                    b.setButtonText (juce::String ("E ") + kKitAbbrev[kitIdx]);
                    b.setColour (juce::TextButton::buttonColourId,
                                 juce::Colour (Betel::Pal::kTintPanelDeep));
                }
                else
                {
                    b.setButtonText ("E -");
                    b.setColour (juce::TextButton::buttonColourId,
                                 juce::Colour (0xFF2A2A2A));
                }
            }
        }

        //----------------------------------------------------------------------
        // Keyboard rendering (49 keys, 35..83)
        //----------------------------------------------------------------------
        void paintKeyboard (juce::Graphics& g)
        {
            if (kbRect.getWidth() < kKeyCount) return;

            const float cellW = (float) kbRect.getWidth() / (float) kKeyCount;
            const float baseY = (float) kbRect.getY() + 2.0f;
            const float baseH = (float) kbRect.getHeight() - 4.0f;

            for (int i = 0; i < kKeyCount; ++i)
            {
                const int mk = kKeyFirst + i;
                const auto& e = kitState.keys[(size_t) mk];
                const bool hasElement = (! e.sourceKit.isEmpty()) && (e.sourceRoleId != 0);
                const bool isBlack    = isBlackKey (mk);
                const bool isSelected = (mk == selectedKey);

                juce::Colour fill;
                if (isSelected)        fill = juce::Colour (Betel::Pal::kAccent);   // edited key — orange
                else if (isBlack)      fill = juce::Colour (0xFF141414);   // black key
                else                   fill = juce::Colour (0xFFE8E8E8);   // white key

                const float x = (float) kbRect.getX() + (float) i * cellW;
                g.setColour (fill);
                g.fillRect (x + 0.5f, baseY, juce::jmax (1.0f, cellW - 1.0f), baseH);

                // Mapped keys keep the white/black look but get a small role-tinted
                // dot near the top, so the editor still shows which keys carry a
                // drum element without recolouring the whole key.
                if (hasElement && ! isSelected)
                {
                    const float d = juce::jmin (cellW - 3.0f, 5.0f);
                    if (d > 1.0f)
                    {
                        g.setColour (roleColour (e.sourceRoleId));
                        g.fillEllipse (x + (cellW - d) * 0.5f, baseY + 3.0f, d, d);
                    }
                }

                if (mk % 12 == 0)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.35f));
                    g.drawLine (x, baseY, x, baseY + baseH, 1.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.65f));
                    g.setFont (juce::Font (10.0f));
                    g.drawText ("C" + juce::String (mk / 12 - 1),
                                (int) x + 2, (int) (baseY + baseH - 14),
                                (int) (cellW * 4.0f), 14, juce::Justification::left, false);
                }
                else if (mk == kKeyFirst)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.65f));
                    g.setFont (juce::Font (10.0f));
                    g.drawText ("B0", (int) x + 2, (int) (baseY + baseH - 14),
                                (int) (cellW * 3.0f), 14, juce::Justification::left, false);
                }
            }
        }

        //----------------------------------------------------------------------
        int keyAtPoint (juce::Point<int> p) const
        {
            if (! kbRect.contains (p)) return -1;
            const float cellW = (float) kbRect.getWidth() / (float) kKeyCount;
            const int idx = (int) ((float)(p.x - kbRect.getX()) / juce::jmax (0.001f, cellW));
            const int clamped = juce::jlimit (0, kKeyCount - 1, idx);
            return kKeyFirst + clamped;
        }

        // Header text for the per-key card.  ASCII separators only — the old
        // Unicode middle-dot caused glyph-fallback gibberish on some fonts.
        juce::String perKeyHeader() const
        {
            // FULL KIT: one instrument, not 128 keys.  Naming a key here was the
            // composed editor's header showing through - GAIN, the filter and
            // the FX rack all land on the whole kit, so a key name, a GM role
            // and a source folder are three things that are not true.
            if (owner.fullKitName.isNotEmpty())
                return "WHOLE KIT - " + owner.fullKitName.toUpperCase();

            const auto& e = kitState.keys[(size_t) selectedKey];
            juce::String s = "Key " + keyName (selectedKey);
            const juce::String gm = gmDrumName (selectedKey);
            if (gm.isNotEmpty()) s += " - " + gm;
            s += " (MIDI " + juce::String (selectedKey) + ")";
            if (e.sourceRoleId != 0)
                s += "   |   " + juce::String (Betel::DrumRoles::getName (e.sourceRoleId));
            if (! e.sourceKit.isEmpty())
                s += "   |   source: " + e.sourceKit;
            return s;
        }

        // Canonical General MIDI Level 1 percussion key name for MIDI 35..81
        // (channel-10 map).  Returns "" for keys outside the GM range.
        static juce::String gmDrumName (int midi)
        {
            switch (midi)
            {
                case 35: return "Acoustic Bass Drum";
                case 36: return "Bass Drum 1";
                case 37: return "Side Stick";
                case 38: return "Acoustic Snare";
                case 39: return "Hand Clap";
                case 40: return "Electric Snare";
                case 41: return "Low Floor Tom";
                case 42: return "Closed Hi-Hat";
                case 43: return "High Floor Tom";
                case 44: return "Pedal Hi-Hat";
                case 45: return "Low Tom";
                case 46: return "Open Hi-Hat";
                case 47: return "Low-Mid Tom";
                case 48: return "Hi-Mid Tom";
                case 49: return "Crash Cymbal 1";
                case 50: return "High Tom";
                case 51: return "Ride Cymbal 1";
                case 52: return "Chinese Cymbal";
                case 53: return "Ride Bell";
                case 54: return "Tambourine";
                case 55: return "Splash Cymbal";
                case 56: return "Cowbell";
                case 57: return "Crash Cymbal 2";
                case 58: return "Vibraslap";
                case 59: return "Ride Cymbal 2";
                case 60: return "Hi Bongo";
                case 61: return "Low Bongo";
                case 62: return "Mute Hi Conga";
                case 63: return "Open Hi Conga";
                case 64: return "Low Conga";
                case 65: return "High Timbale";
                case 66: return "Low Timbale";
                case 67: return "High Agogo";
                case 68: return "Low Agogo";
                case 69: return "Cabasa";
                case 70: return "Maracas";
                case 71: return "Short Whistle";
                case 72: return "Long Whistle";
                case 73: return "Short Guiro";
                case 74: return "Long Guiro";
                case 75: return "Claves";
                case 76: return "Hi Wood Block";
                case 77: return "Low Wood Block";
                case 78: return "Mute Cuica";
                case 79: return "Open Cuica";
                case 80: return "Mute Triangle";
                case 81: return "Open Triangle";
                default: return {};
            }
        }

        static bool isBlackKey (int midi)
        {
            const int pc = ((midi % 12) + 12) % 12;
            return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        }

        static juce::String keyName (int midi)
        {
            static const char* names [12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            const int pc = ((midi % 12) + 12) % 12;
            const int oct = midi / 12 - 1;
            return juce::String (names[(size_t) pc]) + juce::String (oct);
        }

        static juce::Colour roleColour (uint8_t roleId)
        {
            const float h = std::fmod ((float) roleId * 0.137f, 1.0f);
            return juce::Colour::fromHSV (h, 0.55f, 0.75f, 1.0f);
        }

        void timerCallback() override
        {
            repaint (perKeyCardRect);
        }

        //----------------------------------------------------------------------
        // Members
        //----------------------------------------------------------------------
        DrumsPopup&  owner;
        Betel::DrumKitRegistry* registry = nullptr;

        DrumKitParams kitState;
        juce::String  slotLabel;
        int           selectedKey = 36;   // ANCHOR key of the selected component (C1)

        // ── COMPONENT SELECTION ──────────────────────────────────────────────
        // The component is what the user picks; selectedKey above is simply its
        // lowest key, kept so every existing per-key path keeps working.
        juce::String     selectedComponent;
        std::vector<int> componentKeys;      // every key the component covers
        juce::TextButton sfzLoadBtn, sfzToggleBtn;

        /** GREEN while empty, ORANGE once a file is loaded. */
        void styleSfzLoad (bool loaded)
        {
            const auto fill = loaded ? juce::Colour (Betel::Pal::kAccent) : juce::Colour (0xFF1E7A3C);
            sfzLoadBtn.setColour (juce::TextButton::buttonColourId,   fill);
            sfzLoadBtn.setColour (juce::TextButton::buttonOnColourId, fill);
            sfzLoadBtn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
            sfzLoadBtn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        }

        /** ORANGE CAPTION when on, plain white when off - so the lit state is
            unmistakable rather than a barely-tinted fill. */
        void styleSfzToggle (bool on)
        {
            const auto amber = juce::Colour (Betel::Pal::kAccent);
            const auto text  = on ? amber : juce::Colours::white;
            sfzToggleBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF2A2A2A));
            sfzToggleBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF3A2A14));
            sfzToggleBtn.setColour (juce::TextButton::textColourOffId, text);
            sfzToggleBtn.setColour (juce::TextButton::textColourOnId,  text);
            sfzToggleBtn.repaint();
        }

        bool sfzWanted = false;

    public:
        /** Host -> popup: what is loaded on this drum slot and whether it is on. */
        void setSfzState (const juce::String& name, bool active)
        {
            // INTENT, not just engine state.  The toggle means "I want to use my
            // own sound here"; `active` means "one is loaded and playing".  They
            // differ for exactly the moment that matters - the toggle is on, no
            // file picked yet - and reading only `active` there switched the
            // toggle straight back off before the user could reach LOAD.
            // A REFRESH IS A SLOT CHANGE, so the intent resets to the engine's
            // truth here.  The toggle's own click deliberately does NOT trigger a
            // refresh - otherwise turning it on would immediately read back
            // "nothing loaded" and switch itself off again.
            sfzWanted = active;
            const bool on = active;

            sfzToggleBtn.setToggleState (on, juce::dontSendNotification);
            styleSfzToggle (on);

            sfzLoadBtn.setVisible (on);
            // GREEN until something is loaded, ORANGE once it is - so the button
            // says whether it is still asking or already answered.
            styleSfzLoad (name.isNotEmpty());
            sfzLoadBtn.setButtonText (name.isEmpty() ? juce::String ("LOAD") : name);
            resized();
        }

    private:
        juce::TextButton componentSwapBtn;   // replacement droplist - hidden when
                                             // the folder holds only one file

        // One button per component FOLDER, built from the registry so the row is
        // whatever the library actually holds.  12 is headroom over the ten
        // folders that exist today.
        static constexpr int kMaxComponents = 12;
        juce::TextButton componentButtons[kMaxComponents];
        std::vector<juce::String> componentNames;
        bool          updatingFromState = false;

        juce::TextButton saveKitBtn { "SAVE KIT" };
        juce::TextButton loadKitBtn { "LOAD KIT" };
        juce::TextButton saveDefaultBtn { "SAVE AS DEFAULT" };
        std::unique_ptr<juce::FileChooser> fileChooser;
        juce::TextButton perKeyKitButtons [kKeyCount];

        // FX tab buttons + active tab index
        juce::TextButton tabButtons [NumFxTabs];
        // Per-stage ON/OFF toggles (one per FX tab; only the active tab's is shown).
        juce::TextButton fxEnableBtns[NumFxTabs];

        int              activeTab = TabEq;

        // Per-key sliders (left card)
        BandFilterSlider bandFilter { "FILTER" };
    GoldSlider gainSlider    { "GAIN",  0.0f, 100.0f, 50.0f };
        GoldSlider pitchSlider   { "PITCH", 0.0f, 100.0f, 50.0f };
        GoldSlider rrSlider      { "RR",    0.0f, 100.0f, 0.0f  };
        GoldSlider lengthSlider  { "LENGTH",0.0f, 100.0f, 100.0f };
        GoldSlider fxWetSlider   { "WET",   0.0f, 100.0f, 100.0f };  // global rack dry↔wet


        // FX sliders (right card, one set per tab)
        GoldSlider eqSliders [10] {
            { "31",  0.0f, 100.0f, 50.0f }, { "62",  0.0f, 100.0f, 50.0f },
            { "125", 0.0f, 100.0f, 50.0f }, { "250", 0.0f, 100.0f, 50.0f },
            { "500", 0.0f, 100.0f, 50.0f }, { "1k",  0.0f, 100.0f, 50.0f },
            { "2k",  0.0f, 100.0f, 50.0f }, { "4k",  0.0f, 100.0f, 50.0f },
            { "8k",  0.0f, 100.0f, 50.0f }, { "16k", 0.0f, 100.0f, 50.0f }
        };
        // ── SWEETENER (see SweetenerFx.h) ────────────────────────────────
        // Every stage keeps its own enable and its own controls: SOFTEN and
        // TAME and ROUND fix three different problems and do not co-vary, so a
        // single AMOUNT would mean over-treating two of them to fix one.
        GoldSlider swMixSlider    { "MIX",    0.0f, 100.0f, 100.0f };
        GoldSlider swDepthSlider  { "DEPTH",  0.0f, 100.0f,  50.0f };  // bipolar, 50 = neutral
        GoldSlider swWindowSlider { "WINDOW", 0.0f, 100.0f,  24.0f };
        GoldSlider swTameSlider   { "TAME",   0.0f, 100.0f,   0.0f };
        GoldSlider swFreqSlider   { "FREQ",   0.0f, 100.0f,  38.0f };
        GoldSlider swDriveSlider  { "DRIVE",  0.0f, 100.0f,   0.0f };
        GoldSlider swRndSlider    { "AMOUNT", 0.0f, 100.0f, 100.0f };
        VelCurveDisplay velCurveBox;

        GoldSlider swCeilSlider   { "CEILING",0.0f, 100.0f,  43.0f };  // 43 -> 12 dB
        GoldSlider swRatioSlider  { "RATIO",  0.0f, 100.0f,   0.0f };  //  0 -> 1:1
        juce::TextButton swSoftBtn, swPeakBtn, swTameBtn, swRoundBtn;

        GoldSlider satDriveSlider   { "DRIVE",  0.0f, 100.0f, 0.0f };
        GoldSlider satMixSlider     { "MIX",    0.0f, 100.0f, 100.0f };

        GoldSlider compThreshSlider { "THRESH", 0.0f, 100.0f, 100.0f };
        GoldSlider compRatioSlider  { "RATIO",  0.0f, 100.0f, 0.0f };
        GoldSlider compAttackSlider { "ATK",    0.0f, 100.0f, 5.0f };
        GoldSlider compReleaseSlider{ "REL",    0.0f, 100.0f, 5.0f };
        GoldSlider compMakeupSlider { "MAKE",   0.0f, 100.0f, 0.0f };

        GoldSlider revSizeSlider { "SIZE", 0.0f, 100.0f, 50.0f };
        GoldSlider revDampSlider { "DAMP", 0.0f, 100.0f, 50.0f };
        GoldSlider revWetSlider  { "WET",  0.0f, 100.0f,   0.0f };
        // DRY at 100 by default - the kit is untouched until asked.
        GoldSlider revDrySlider  { "DRY",  0.0f, 100.0f, 100.0f };
        // ── THE REST OF THE MELODIC REVERB, now that this rack runs it ───────
        // The bus swapped juce::Reverb for Betel::ReverbFx, which is the engine
        // the instrument editor drives.  These four are the controls that engine
        // has always had and this page never exposed, so the kit rack could only
        // reach a corner of it.
        GoldSlider revTailSlider { "TAIL", 0.0f, 100.0f,  50.0f };   // RT60
        GoldSlider revPreSlider  { "PRE",  0.0f, 100.0f,   0.0f };   // 0..200 ms
        // Two-handle band on the reverb SEND - wet only, dry untouched.  Same
        // control and same question as EffectsPanel's: what the REVERB hears.
        BandFilterSlider revBand { "SEND BAND" };
        juce::TextButton revAlgoBtn;      // HALL/ROOM (FDN) <-> PLATE
        int              revAlgoIdx = 0;

        // Delay time source (SYNC / 4-4 3-4 / divisions).
        juce::TextButton delSyncBtn, delSig44Btn, delSig34Btn, delDivBtns[5];
        bool             delSyncOn  = false;
        int              delTimeSig = 0;
        int              delDivIdx  = 2;

        // Per-kit calibration trim, 0..200 with 100 = UNITY.  Stored in
        // SlotParams::gainPercent — outside the rack, so Big Drums cannot swap
        // it away with the effects.
        GoldSlider kitGainSlider { "GAIN", 0.0f, 200.0f, 100.0f, "" };

        GoldSlider delTimeSlider { "TIME", 0.0f, 100.0f, 12.0f };
        GoldSlider delFbSlider   { "FB",   0.0f, 100.0f, 30.0f };
        // DRY is explicit now: the bus runs Betel::StereoDelayFx, which takes
        // dry and wet separately instead of summing against an implicit 1.0.
        GoldSlider delDrySlider  { "DRY",  0.0f, 100.0f, 100.0f };
        GoldSlider delWetSlider  { "WET",  0.0f, 100.0f, 0.0f };

        // What full WET travel is worth on each effect.  LEFT double-click on
        // the WET handle opens the box; defaults match the melodic side and
        // differ from each other for the reason given in DrumKitFxParams.
        float revWetBase = 1.0f;
        float delWetBase = 0.5f;
        GoldSlider panSlider     { "PAN",  0.0f, 100.0f, 50.0f };

        // ── THE THREE SENDS ──────────────────────────────────────────────────
        //
        // Three, matching SectionSendFx::Slot — chorus, reverb, delay.  Order
        // matches that enum so the page and the rack cannot drift apart.  All
        // three default to 0: a kit that has never been dialled is dry, not
        // dropped into effects nobody asked for.
        //
        // The sweetener is a per-kit INSERT again (its own page below) — a
        // shaper cannot be fed by a send.
        enum { kSendChorus = 0, kSendReverb, kSendDelay, kNumSends };

        GoldSlider sendSliders [kNumSends] {
            { "CHORUS", 0.0f, 100.0f, 0.0f },
            { "REVERB", 0.0f, 100.0f, 0.0f },
            { "DELAY",  0.0f, 100.0f, 0.0f }
        };

        // ── LOG TAPER, NOT LINEAR ────────────────────────────────────────────
        //
        // A linear send puts every usable amount in the bottom of the travel:
        // 20% of a reverb is already a lot, so the top four fifths of the
        // slider are all "drowned" and the useful range is a nudge off zero.
        //
        // The same curve the mix knob uses — half rotation is -20 dB — so a
        // send at the middle contributes a tenth, and fine control lives where
        // the amounts actually are.
        float getSendVal (int i) const
        {
            return Betel::logKnobTaper (juce::jlimit (0.0f, 1.0f,
                                            sendSliders[i].getValue() * 0.01f));
        }
        void  setSendVal (int i, float v)
        {
            sendSliders[i].setValue (juce::jlimit (0.0f, 100.0f,
                                        Betel::logKnobTaperInv (v) * 100.0f),
                                     juce::dontSendNotification);
        }

        // ── Kick MIX controls (per-key, notes 35 & 36 only) ───────────────────
        // Equal-power blend between the kit's own kick and a supplemental EDM or
        // WOOD kick.  Shown in the per-key card only when a kick key is selected;
        // wired to DrumElementParams::kickMix{Enabled,Amount,Variant} through the
        // normal writeSliderToKey() / onKeyParamsChanged path.
        GoldSlider       kickMixSlider     { "MIX", 0.0f, 100.0f, 50.0f };
        juce::TextButton kickMixEnableBtn  { "MIX OFF" };  // toggles kickMixEnabled (default OFF)
        juce::TextButton kickMixVariantBtn { "EDM" };      // EDM (0) / WOOD (1)

        static constexpr int kStripH = 54;   // piano strip height ("short keys")
        PianoStrip pianoStrip;

        // Cached rects from resized()
        juce::Rectangle<int> statusRect, kbRect;
        juce::Rectangle<int> perKeyCardRect;
        juce::Rectangle<int> fxFrameRect;

        // ── Sweetener GR meters ──────────────────────────────────────────────
        // Same range and same HOT point as EffectsPanel's, deliberately: 12 dB
        // is the span the four stages actually work in, and 6 dB is where a
        // stage stops shaping and starts squashing.  If either number ever
        // moves it has to move in BOTH files or a setting learned on one panel
        // stops reading the same on the other.
        static constexpr int   kNumSweetMeters    = 4;
        static constexpr float kSweetMeterRangeDb = 12.0f;
        static constexpr float kSweetMeterHotDb   =  6.0f;

        float grDb[kNumSweetMeters] { 0.0f, 0.0f, 0.0f, 0.0f };
        juce::Rectangle<int> sweetMeterArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

    Content* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsPopup)
};
