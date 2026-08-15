#pragma once

#include <JuceHeader.h>
#include <tuple>
#include <vector>
#include "GlobalMacros.h"
#include <BinaryData.h>
#include "SpaceAnimation.h"
#include "InstrEditPanel.h"
#include "InstrEditorWindow.h"
#include "DrumsPopup.h"
#include "DrumKitRegistry.h"
#include "BetelStateXml.h"
#include "InstrumentPreset.h"   // drum .ins lookup for the selector
#include <algorithm>
#include <random>
#include <vector>

// =====================================================================================
//  Reference sound library (scaffold)
//
//  The "reference" voices are an additional, higher-quality sampled library
//  layered on top of the built-in GM source.  Each band slot can override its
//  GM voice with a reference voice via a per-slot pill dropdown in SoundsTab.
//
//  The library itself is produced by a separate sampling task that isn't done
//  yet, so `library()` is currently EMPTY — the pills show only "- none (GM) -"
//  until it's populated.  When the samples are ready, fill the initializer
//  below (categories → instruments, each with a stable integer id) and the
//  pills light up automatically.  Actual playback routing (id → engine voice)
//  is wired separately once the library's storage form is known.
// =====================================================================================
namespace BetelRef
{
    struct RefInstrument { juce::String name; int id; };
    struct RefCategory   { juce::String name; std::vector<RefInstrument> items; };

    // The reference library is populated at runtime by the host from the
    // engine's per-instrument flag library (see MainComponent::refreshReferenceLibrary).
    // Each item's id IS the instrument flag (the leading "NNN" of "NNN-Name.frb").
    // Until populated it stays empty and the pills show only "- none (GM) -".
    inline std::vector<RefCategory>& mutableLibrary()
    {
        static std::vector<RefCategory> lib;
        return lib;
    }
    inline const std::vector<RefCategory>& library() { return mutableLibrary(); }
    inline void setLibrary (std::vector<RefCategory> lib) { mutableLibrary() = std::move (lib); }

    inline juce::String nameForId (int id)
    {
        if (id < 0) return {};
        for (const auto& c : library())
            for (const auto& it : c.items)
                if (it.id == id) return it.name;
        return {};
    }
}

// =====================================================================================
//  ReferencePill — rounded per-slot dropdown that picks a reference voice via a
//  nested category → instrument popup menu.  id -1 = "none" (slot uses GM).
// =====================================================================================
class ReferencePill : public juce::Component
{
public:
    std::function<void(int refId)> onSelect;   // -1 = none (GM)

    void setSelectedId (int id) { selId = id; repaint(); }
    int  getSelectedId() const  { return selId; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        const float r = b.getHeight() * 0.5f;
        const bool  hasRef = (selId >= 0);

        g.setColour (hasRef ? juce::Colour (0xFF2E5E2E) : juce::Colour (0xFF262626));
        g.fillRoundedRectangle (b, r);
        g.setColour (juce::Colours::white.withAlpha (hasRef ? 0.55f : 0.30f));
        g.drawRoundedRectangle (b, r, 1.0f);

        juce::String txt = hasRef ? BetelRef::nameForId (selId) : juce::String ("ref");
        if (txt.isEmpty()) txt = "ref";

        g.setColour (juce::Colours::white.withAlpha (hasRef ? 0.90f : 0.55f));
        g.setFont (juce::Font (juce::jmin (b.getHeight() * 0.52f, 11.0f)));
        g.drawText (txt, getLocalBounds().reduced (8, 0),
                    juce::Justification::centred, true);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;
        menu.addItem (1, "- none (GM) -", true, selId < 0);

        const auto& lib = BetelRef::library();
        if (! lib.empty())
        {
            menu.addSeparator();
            for (const auto& cat : lib)
            {
                juce::PopupMenu sub;
                for (const auto& it : cat.items)
                    sub.addItem (it.id + kIdBase, it.name, true, selId == it.id);
                menu.addSubMenu (cat.name, sub);
            }
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this] (int result)
            {
                if (result == 0) return;                       // dismissed
                const int ref = (result == 1) ? -1 : (result - kIdBase);
                setSelectedId (ref);
                if (onSelect) onSelect (ref);
            });
    }

private:
    static constexpr int kIdBase = 1000;   // menu-item id offset (1 = "none")
    int selId = -1;
};

// =====================================================================================
//  SoundsTab — Canvas 918 × 435
//
//  Slot buttons (8 slots in both modes):
//    Style mode  → DRUMS / PERC / BASS / CHORD 1 / CHORD 2 / PAD / LEAD 1 / LEAD 2
//                  (the 8 fixed roles of a Yamaha-style virtual band — voices are
//                   auto-loaded by the loaded style; the user can mute, adjust
//                   volume, or optionally swap a voice per role.)
//    Solo  mode  → "SOLO 1" .. "SOLO 8"
//
//  Timbre row (17 entries):
//    PIANO / CHROM / ORGAN / GUITAR / BASS / STRINGS / ENSEMB / BRASS / REED /
//    PIPE / SYN.LD / SYN.PAD / SYN.FX / ETHNIC / PERCUS / SFX / DRUMS
//
//  When DRUMS (index 16) is the selected timbre:
//    - the 8 instrument cells show loaded kit names from the DrumKitRegistry
//      (first 8 of however many are scanned) instead of GM instrument names
//    - clicking an instrument cell loads that kit into the slot
//    - clicking EDIT opens DrumsPopup instead of InstrEditorWindow
//    - the slot is registered as a drum channel with the engine, so subsequent
//      Program Change messages route through the engine's drum-kit dispatch
// =====================================================================================

// ── InstrImageDisplay (glow + white smoke + image) ───────────────────────────────────
class InstrImageDisplay : public juce::Component, private juce::Timer
{
public:
    InstrImageDisplay()
    {
        setOpaque(false);
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }
    ~InstrImageDisplay() override { stopTimer(); }

    void set(juce::Image img)
    {
        image = std::move(img);
        smoke.clear();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (!image.isValid()) return;
        const float W = (float)getWidth(), H = (float)getHeight();

        const float cx = W * 0.5f, cy = H * 0.5f;
        const float iw = W * 0.75f, ih = H * 0.75f;
        for (int layer = 5; layer >= 1; --layer)
        {
            const float expand = (float)layer * 5.0f;
            const float alpha  = 0.04f * (float)(6 - layer) / 5.0f;
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.fillEllipse(cx - iw*0.5f - expand, cy - ih*0.5f - expand,
                          iw + expand*2.0f, ih + expand*2.0f);
        }

        for (auto& p : smoke)
        {
            juce::ColourGradient gr(juce::Colours::white.withAlpha(p.alpha),
                                    p.x, p.y,
                                    juce::Colours::white.withAlpha(0.0f),
                                    p.x + p.r, p.y, true);
            g.setGradientFill(gr);
            g.fillEllipse(p.x - p.r, p.y - p.r, p.r * 2.0f, p.r * 2.0f);
        }

        g.drawImageWithin(image, 0, 0, (int)W, (int)H, juce::RectanglePlacement::centred);
    }

private:
    struct SmokeParticle { float x, y, vy, r, alpha; };
    std::vector<SmokeParticle> smoke;
    juce::Image image;
    float spawnTimer = 0;
    std::mt19937 rng { std::random_device{}() };
    std::uniform_real_distribution<float> rnd { 0.f, 1.f };

    void timerCallback() override
    {
        const float W = (float)getWidth(), H = (float)getHeight();
        if (W <= 0 || !image.isValid()) return;

        spawnTimer += 1.0f;
        if (spawnTimer > 2.5f && (int)smoke.size() < 16)
        {
            smoke.push_back({ W * 0.15f + rnd(rng) * W * 0.7f,
                              H * 0.8f + rnd(rng) * H * 0.1f,
                              -(0.25f + rnd(rng) * 0.4f),
                              3.0f + rnd(rng) * 5.0f,
                              0.12f + rnd(rng) * 0.08f });
            spawnTimer = 0;
        }
        for (auto& p : smoke) { p.y += p.vy; p.r += 0.12f; p.alpha -= 0.0025f; }
        smoke.erase(std::remove_if(smoke.begin(), smoke.end(),
            [](const SmokeParticle& p){ return p.alpha <= 0 || p.y < -20; }), smoke.end());
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrImageDisplay)
};

// ── SoundsTab ─────────────────────────────────────────────────────────────────────────
class SoundsTab : public juce::Component
{
public:
    // ── Host bridge callbacks (assigned by MainComponent) ─────────────────────
    std::function<void(bool isSolo, int slot, int patch)>                onInstrumentSelected;
    // "SAVE AS DEFAULT" from the instrument editor — host writes the slot's
    // current instrument to instruments_presets\NNN-Name.ins.
    std::function<void(int slot, bool isSolo, const SlotParams&)>        onSaveAsDefault;

    /** Confirm a SAVE SETTINGS in the editor's header.  Called by the host only
        after the file is genuinely on disk, so an empty text means "it did not
        save" and nothing is shown. */
    void confirmSaved (const juce::String& text)
    {
        if (editorWindow != nullptr && text.isNotEmpty())
            editorWindow->flashSaved (text);
    }
    /** The GAIN slider moved.  Separate from onSlotParamsChanged on purpose:
        the calibration trim is not carried by a bulk push (see
        Channel::instrumentGain), so it needs its own explicit route to the
        engine — and firing only on an actual change means re-pushing a slot for
        any other reason can never disturb it. */
    std::function<void(int slot, bool isSolo, float gainPercent)>        onSlotGainChanged;
    /** Ask the host for the saved voice governing this slot's loaded sound.
        Returns false when none exists. */
    std::function<bool(int slot, bool isSolo, SlotParams&)>              onGetPresetParams;
    /** DEVELOPER: set the base unity for this slot's instrument, in dB.  Writes
        both preset sets and returns true on success. */
    /** IGNORE PROGRAM CHANGE for a style slot - see btnIgnorePc. */
    std::function<void(int slot, bool isSolo, bool ignore)>              onSetIgnorePc;
    std::function<bool(int slot, bool isSolo)>                           onGetIgnorePc;

    std::function<bool(int slot, bool isSolo, float baseUnityDb)>        onSetBaseUnity;
    /** The base unity currently on file for this slot's instrument. */
    std::function<float(int slot, bool isSolo)>                          onGetBaseUnity;
    /** Fires when a slot's reference-voice pill changes. refId -1 = none (the
        slot reverts to its GM voice).  Reference playback routing is wired
        once the reference sample library exists; the selection is stored and
        saved regardless. */
    std::function<void(bool isSolo, int slot, int refId)>                onReferenceSelected;
    std::function<void(int slot)>                                        onActiveSoloSlotChanged;
    std::function<bool(const juce::File& file, const juce::String& code)> onLoadBlobRequested;
    /** The WORLD bank: every instrument flag >= 200, with its name.

        Not a new bank - the engine has always reserved this range ("flags >= 200
        are reference sounds"), and MainComponent::refreshReferenceLibrary has
        always partitioned the library into GM and >=200.  It simply never
        surfaced in the instrument selector, so the only way to reach a World
        sound was to substitute a slot. */
    /** USER SFZ, per slot.  The host owns the file dialog and the engine call;
        the tab only knows which slot is being edited. */
    /** The sampled kits found on disk: msb, lsb, pc, readable label. */
    std::function<std::vector<std::tuple<int,int,int,juce::String>>()>   onGetFullKits;
    /** User picked one from the grid. */
    std::function<void(int slot, bool isSolo, int msb, int lsb, int pc)> onFullKitSelected;
    /** "msb/lsb/pc" of the sampled kit on this slot, or "" when composed. */
    std::function<juce::String(int slot, bool isSolo)>                   onGetFullKitOnSlot;

    std::function<void(int slot, bool isSolo)>                           onSfzLoadRequested;
    std::function<void(int slot, bool isSolo, bool on)>                  onSfzToggled;
    /** name + active, for the slot being edited. */
    std::function<std::pair<juce::String,bool>(int slot, bool isSolo)>   onGetSfzState;

    /** RETIRED - the flat ">= 200" list.  Replaced by the two below, which ask
        per pack and per category.  Left declared so an older wiring still
        compiles; nothing in the tab calls it any more. */
    std::function<std::vector<std::pair<int, juce::String>>()>           onGetWorldInstruments;

    /** Which pack a FLAG belongs to.  Asked of the engine rather than derived
        from the number, because a sound's pack is decided by the folder it was
        found in - see SamplePlayerEngine::setSoundLibraryFolders.  Deriving it
        from a range here would reintroduce exactly the coupling that design
        avoids, and would go wrong the day a pack is renumbered. */
    std::function<int (int flag)>                                        onGetInstrumentPack;

    /** Category names for a pack (1 = WORLD, 2 = ORIENTAL), in display order. */
    std::function<juce::StringArray (int pack)>                          onGetPackCategories;

    /** Every sound in one category of one pack, ascending by flag. */
    std::function<std::vector<std::pair<int, juce::String>> (int pack,
                                                            const juce::String& category)>
                                                                         onGetPackInstruments;

    std::function<std::vector<juce::String>()>                           onGetBlobPresetNames;
    std::function<void(int slot, int presetIndex)>                       onSoloPresetSelected;

    std::function<void(int slot, bool isSolo, const juce::File&)>        onClickFileChosen;
    std::function<void(int slot, bool isSolo, bool enabled, float vol, float decayMs)>
                                                                         onClickParamsChanged;

    /** SWEETENER for a MELODIC slot — its own route for the same reason
        onClickParamsChanged has one: it does not travel on the bulk params push
        (see Channel::ChannelParams), because that push is reset to defaults on
        every instrument change and would switch the block off with it.  Kit
        slots do not use this — theirs rides DrumKitFxParams through
        publishKitFx, which an instrument change never touches. */
    std::function<void(int slot, bool isSolo, const SweetenerParams&)>    onSweetenerChanged;
    std::function<void(int slot, bool isSolo, const SlotParams&)>        onSlotParamsChanged;
    /** A preset file landed on a slot and its ALLOWED NOTES window has to
        reach StylePlayer.  Kept SEPARATE from onSlotParamsChanged: that one
        is the bulk push, which adoptPresetParams deliberately avoids so the
        per-sound calibration trim is never re-written behind the user. */
    std::function<void(int slot, bool isSolo, const SlotParams&)>        onNoteRangeAdopted;

    // ── Drum-kit callbacks (Phase 3 additions) ────────────────────────────────
    std::function<void(int slot, bool isSolo, bool isDrumNow)>           onDrumModeChanged;
    std::function<Betel::DrumKitRegistry*()>                             onGetDrumKitRegistry;
    std::function<juce::File()>                                          onGetInstrumentPresetsFolder;
    std::function<void(int slot, bool isSolo, const DrumKitParams&)>     onApplyDrumKit;
    /** Audition a single drum key on the given slot (right-click in editor). */
    std::function<void(int slot, bool isSolo, int midiKey)>             onAuditionDrumKey;

    /** Hidden octave bias for a slot's engine channel — the bass register rule
        (see applyGmEnvDefaults).  Not a user parameter: the OCTAVE slider still
        reads 0 while the engine plays the shifted register. */
    std::function<void(int slot, bool isSolo, int octaves)>              onOctaveBiasChanged;

    /** Instrument-editor piano strip: play / release a note on the slot, and
        poll which notes are sounding there (so the strip can light them red). */
    std::function<void(int slot, bool isSolo, int note, int velocity)>  onEditorNoteOn;
    std::function<void(int slot, bool isSolo, int note)>                onEditorNoteOff;
    std::function<void(int slot, bool isSolo, uint32_t* mask)>          onQuerySoundingNotes;
    std::function<void(int slot, bool isSolo, int midiKey,
                       const DrumElementParams&)>                        onDrumKeyParamsChanged;

    /** Fires when the user moves any kit-wide FX knob (10-band EQ, sat,
        comp, reverb, delay) in the DrumsPopup.  Host should push these to
        the engine via processor.applyDrumKitFx. */
    std::function<void(int slot, bool isSolo, const DrumKitFxParams&)>   onKitFxChanged;
    //==========================================================================
    // SWEETENER GAIN-REDUCTION METERS
    //
    // The host fills gr4 with { SOFTEN, PEAK, TAME, ROUND } in dB for the
    // engine channel behind (slot, isSolo).  A PULL rather than a push because
    // only this class knows which slot's editor is actually open, and asking
    // the engine for a channel nobody is looking at is work for nothing.
    //==========================================================================
    std::function<void(int slot, bool isSolo, float* gr4)>               onQuerySweetenerGr;

    /** Called from the host's UI timer.  Does nothing at all unless one of the
        two editors is open, so it is safe to call unconditionally every tick -
        which is the point: the host should not have to track which popup is up.

        Both windows are asked when both happen to be visible.  They cannot be
        showing the same slot (one is the drum editor, the other the melodic
        one) but SoundsTab only tracks ONE selection, so whichever is open is
        the one that selection belongs to and the other simply sees the same
        numbers - harmless, and cheaper than a second piece of state to keep in
        step. */
    void refreshSweetenerMeters()
    {
        if (! onQuerySweetenerGr) return;

        const bool drumOpen = (drumsPopup   != nullptr && drumsPopup  ->isVisible());
        const bool instOpen = (editorWindow != nullptr && editorWindow->isVisible());

        if (! drumOpen && ! instOpen) return;

        float gr[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        onQuerySweetenerGr (selectedSlot, soloMode, gr);

        if (drumOpen) drumsPopup  ->setSweetenerGr (gr[0], gr[1], gr[2], gr[3]);
        if (instOpen) editorWindow->setSweetenerGr (gr[0], gr[1], gr[2], gr[3]);
    }

    static juce::String instrumentNameForPatch (int patch)
    {
        const int t = (patch / 8) & 0x0F;
        const int i = patch & 7;
        return juce::String (kInstrNames[t][i]);
    }

    // Current melodic instrument name for a solo slot (0-7), for the Main tab.
    juce::String getSoloInstrumentName (int slot) const
    {
        if (slot < 0 || slot >= 8) return {};
        return instrumentNameForPatch (soloPatch[(size_t) slot]);
    }

    //==========================================================================
    // Full-state snapshot used by the Favorites system.  Captures every per-
    // slot SlotParams (solo + style), the cached patch indices, the drum-mode
    // flags, plus which slot was selected and whether solo-mode was active.
    //==========================================================================
    juce::ValueTree captureState() const
    {
        juce::ValueTree t ("SoundsState");
        t.setProperty ("soloMode",     soloMode,     nullptr);
        t.setProperty ("selectedSlot", selectedSlot, nullptr);

        juce::ValueTree solo ("SoloSlots");
        juce::ValueTree styl ("StyleSlots");
        for (int i = 0; i < 8; ++i)
        {
            auto s = BetelStateXml::saveSlot (soloParams[(size_t) i]);
            s.setProperty ("index",   i,                                 nullptr);
            s.setProperty ("patch",   soloPatch[(size_t) i],             nullptr);
            s.setProperty ("isDrum",  soloIsDrumSlot[(size_t) i],        nullptr);
            s.setProperty ("ref",     soloRef[(size_t) i],               nullptr);
            s.setProperty ("octaveBias", slotOctaveBias[1][(size_t) i],   nullptr);
            solo.appendChild (s, nullptr);

            auto y = BetelStateXml::saveSlot (styleParams[(size_t) i]);
            y.setProperty ("index",   i,                                 nullptr);
            y.setProperty ("patch",   stylePatch[(size_t) i],            nullptr);
            y.setProperty ("isDrum",  styleIsDrumSlot[(size_t) i],       nullptr);
            y.setProperty ("ref",     styleRef[(size_t) i],              nullptr);
            // The octave BIAS is engine state, not a SlotParams field — it is
            // what lifts a bass part a whole octave while the OCTAVE slider
            // still reads 0 — so it rides here rather than being lost.
            y.setProperty ("octaveBias", slotOctaveBias[0][(size_t) i],   nullptr);
            styl.appendChild (y, nullptr);
        }
        t.appendChild (solo, nullptr);
        t.appendChild (styl, nullptr);
        return t;
    }

    /** Restore from a tree previously made by captureState().  Pushes the
        per-slot params back into the engine via the existing callbacks so
        the live audio state matches the UI.  Safe to call with a partial /
        missing tree — missing fields keep their current values. */
    void applyState (const juce::ValueTree& t)
    {
        if (! t.isValid()) return;

        auto solo = t.getChildWithName ("SoloSlots");
        auto styl = t.getChildWithName ("StyleSlots");

        for (int i = 0; i < solo.getNumChildren(); ++i)
        {
            auto s = solo.getChild (i);
            const int idx = (int) s.getProperty ("index", -1);
            if (idx < 0 || idx >= 8) continue;
            // FROZEN: the set may not write this slot.  Everything else about
            // it - patch, octave bias, drum flag - still restores, because those
            // say WHICH SOUND the slot holds; the freeze is about how that sound
            // is voiced, which is the part being worked on.
            if (! isSlotFrozen (idx, true))
                BetelStateXml::loadSlot (s, soloParams[(size_t) idx]);
            slotOctaveBias[1][(size_t) idx] =
                (int) s.getProperty ("octaveBias", slotOctaveBias[1][(size_t) idx]);
            soloPatch[(size_t) idx]      = (int)  s.getProperty ("patch",  soloPatch[(size_t) idx]);
            soloIsDrumSlot[(size_t) idx] = (bool) s.getProperty ("isDrum", soloIsDrumSlot[(size_t) idx]);
            soloRef[(size_t) idx]        = (int)  s.getProperty ("ref",    -1);
        }
        for (int i = 0; i < styl.getNumChildren(); ++i)
        {
            auto y = styl.getChild (i);
            const int idx = (int) y.getProperty ("index", -1);
            if (idx < 0 || idx >= 8) continue;
            if (! isSlotFrozen (idx, false))
                BetelStateXml::loadSlot (y, styleParams[(size_t) idx]);

            // One-time migration: state saved BEFORE the multi-select mono
            // schema (no "monoRetrigNew" property) predates the bass defaults —
            // its saved Poly / A#1 floor would pin the bass forever.  Adopt the
            // new bass defaults once; after the next save the slot carries the
            // new schema and the user's own settings rule again.
            if (idx == 2 && ! y.hasProperty ("monoRetrigNew"))
            {
                auto& bp = styleParams[2];
                bp.playMode         = 1;      // Mono
                bp.monoHoldStolen   = true;   // HOLD STOLEN only
                bp.monoRetrigNew    = false;
                bp.monoRetrigStolen = false;
                bp.sustain          = 1.0f;
                bp.release          = 0.12f;  // slider 4
                bp.noteRangeOn      = true;
                bp.noteRangeLo      = 42;     // F#2 floor
            }
            slotOctaveBias[0][(size_t) idx] =
                (int) y.getProperty ("octaveBias", slotOctaveBias[0][(size_t) idx]);
            stylePatch[(size_t) idx]      = (int)  y.getProperty ("patch",  stylePatch[(size_t) idx]);
            styleIsDrumSlot[(size_t) idx] = (bool) y.getProperty ("isDrum", styleIsDrumSlot[(size_t) idx]);
            styleRef[(size_t) idx]        = (int)  y.getProperty ("ref",    -1);
        }

        soloMode     = (bool) t.getProperty ("soloMode",     soloMode);
        selectedSlot = juce::jlimit (0, 7,
                                     (int) t.getProperty ("selectedSlot", selectedSlot));

        // Drum-ness is now role-fixed (style slots 0/1 only).  Override any
        // legacy "isDrum" values loaded from older states so the engine and
        // UI see one consistent truth.
        syncDrumFlagsFromRole();

        // Re-push per-slot state to the engine so audio matches the UI.
        // For drum slots we use applyDrumKit (re-loads samples) which is
        // heavy but only runs on favourite load (not RT).
        for (int sm = 0; sm < 2; ++sm)
        {
            const bool isSolo = (sm == 0);
            for (int slot = 0; slot < 8; ++slot)
            {
                const auto& params = isSolo ? soloParams[(size_t) slot]
                                            : styleParams[(size_t) slot];
                const bool drum    = isSolo ? soloIsDrumSlot[(size_t) slot]
                                            : styleIsDrumSlot[(size_t) slot];

                if (onDrumModeChanged) onDrumModeChanged (slot, isSolo, drum);

                if (drum)
                {
                    // ONLY push a kit we actually HAVE.  A style-owned drum slot
                    // has no kit stored here: the 30 Hz engine->UI mirror writes
                    // just the kit NAME into drumKit.lastLoadedKit
                    // (setSlotDrumKitName is documented as display-only) and
                    // never the key mapping.  Pushing that name-only shell ran
                    // Channel::loadDrumKit, which clears the preset and
                    // publishes an EMPTY kit — and then onApplyDrumKit pins the
                    // slot against further style PCs, so the drums stayed silent
                    // for the rest of the session.  That is the close/reopen
                    // "drums went away" bug: the editor's own reopen path
                    // restores this snapshot.
                    //
                    // With no mapped keys there is nothing to restore and the
                    // engine's live kit is already correct, so skipping is both
                    // safe and the whole fix.  It also removes the heaviest part
                    // of a reopen (two full kit sample reloads on the message
                    // thread), which is what made the reopen audibly glitch.
                    if (params.drumKit.hasMappedKeys())
                        publishDrumKit (slot, isSolo, params.drumKit);

                    // THE KIT FX WERE NEVER PUSHED HERE.  publishDrumKit sends
                    // the key mapping; the rack — EQ, saturation, compressor,
                    // reverb, delay and their five enables — travels separately
                    // through publishKitFx, and this loop never called it.  So a
                    // set's drum effects loaded into the data, showed correctly
                    // on the toggles, and never reached the engine.
                    //
                    // Unconditional, unlike the kit above: the FX block is
                    // always meaningful even for a slot whose key mapping is
                    // style-owned and therefore not stored here.
                    publishKitFx (slot, isSolo, params.drumKit.fx);
                }
                else
                {
                    const int patch = isSolo ? soloPatch[(size_t) slot]
                                             : stylePatch[(size_t) slot];
                    if (onInstrumentSelected) onInstrumentSelected (isSolo, slot, patch);

                    // Re-apply any reference-voice override for this slot.
                    const int ref = isSolo ? soloRef[(size_t) slot]
                                           : styleRef[(size_t) slot];
                    if (onReferenceSelected) onReferenceSelected (isSolo, slot, ref);
                }

                // Push the full slot params (ADSR / filter / EQ / etc.) and the
                // click sub-set onto the engine.  Note the two callbacks have
                // different signatures: onClickParamsChanged takes the three
                // click scalars explicitly, while onSlotParamsChanged takes
                // the whole SlotParams.
                if (onClickParamsChanged)
                    onClickParamsChanged (slot, isSolo,
                                          params.clickEnabled,
                                          params.clickVolume,
                                          params.clickDecayMs);
                if (onSlotParamsChanged)
                    onSlotParamsChanged (slot, isSolo, params);

                // AND THE HIDDEN OCTAVE BIAS.  onSlotParamsChanged carries
                // octaveOffset (the slider) but NOT the bias, which is engine
                // state and rides on its own callback — only ever fired from
                // commitSlotParamsToEngine, which this loop does not use.  So a
                // set restored the number into the tab and the bass came back an
                // octave adrift from where it was saved.
                if (onOctaveBiasChanged)
                    onOctaveBiasChanged (slot, isSolo,
                                         slotOctaveBias[isSolo ? 1 : 0][(size_t) slot]);
            }
        }

        // ── REDRAW THROUGH THE REAL ENTRY POINTS ─────────────────────────────
        //
        // This used to hand-call a SUBSET of the refreshes, which is why the tab
        // came up showing the wrong half of itself: soloMode was restored as a
        // member but layoutSlots / layoutRefPills / resized never ran, so the
        // SOLO/STYLE buttons said one thing while the cell grid, the pill row and
        // the drum-vs-melodic visibility were still laid out for the other.
        //
        // setMode does the whole job.  It resets selectedSlot to 0, so the
        // restored slot is re-selected straight after.
        const int  restoredSlot = selectedSlot;
        const bool restoredMode = soloMode;

        setMode (restoredMode);
        selectedSlot = restoredSlot;

        syncTimbreToSlot();
        refreshSlotButtons();
        refreshTimbreButtons();
        layoutInstrCells();
        refreshInstrDisplay();
        refreshFullKitCells();
        refreshRefPills();
        reloadInstrImage();
        refreshEditorSlotLabel();
        refreshSfzState();
        pushSlotIntoEditor();
        refreshIgnorePcButton();
        resized();

        if (soloMode && onActiveSoloSlotChanged) onActiveSoloSlotChanged (selectedSlot);

        // A SET HAS SPOKEN for every style slot it carried.  Record that BEFORE
        // calibration runs, because calibration's whole job is to fill in for
        // slots nobody has spoken for — and it was undoing the set: it force-
        // cleared the DRUMS/PERC kit-FX enables the set had just restored, one
        // line after restoring them.
        for (int i = 0; i < styl.getNumChildren(); ++i)
        {
            const int idx = (int) styl.getChild (i).getProperty ("index", -1);
            if (idx >= 0 && idx < 8) styleSlotFromSet[(size_t) idx] = true;
        }

        applySoundCalibration();
    }

    /** Forget that a set governs the style slots — called when a style is loaded
        WITHOUT one, so calibration goes back to filling in for them. */
    void clearStyleSlotOwnership()
    {
        for (auto& b : styleSlotFromSet) b = false;
    }

    /** Push one slot's stored params (ADSR / filter / EQ / FX / octave / pitch
        mod / click) onto the engine WITHOUT reloading the instrument.  The
        editor seeds its GoldSliders with notify == false, so binding a slot
        only *displays* the values; call this whenever the engine must match the
        UI.  Idempotent — applyChannelParams just stores the channel atomics. */
    /** Re-push EVERY slot's params to the engine, and nothing else.

        THE SET HAS TO HAVE THE LAST WORD, and before this it did not.

        `selectChannelPreset` resets a channel to a DEFAULT-CONSTRUCTED
        ChannelParams whenever an instrument arrives with no saved .ins voice —
        correct on its own terms, since a new sound must not inherit the last
        one's envelopes.  But several things legitimately run AFTER SoundsState
        during a set load and reach that same line: restoreIgnorePcSlots
        re-selects the fixed melodic instruments, and any style-driven program
        change does the same.  Each one silently returned the two-handle band
        filter to fully open (20 Hz / 20 kHz), so a set's filter loaded into the
        tab, showed correctly on the slider, and was not what the engine held.
        Touching the channel selector re-seeded the editor and committed it,
        which is why it appeared to arrive late rather than not at all.

        Deliberately NOT a full commit: no onInstrumentSelected, no
        onReferenceSelected, no publishDrumKit.  Re-selecting an instrument here
        would trip the very reset this exists to undo. */
    void repushAllSlotParams()
    {
        for (int sm = 0; sm < 2; ++sm)
        {
            const bool isSolo = (sm == 0);
            for (int slot = 0; slot < 8; ++slot)
            {
                const auto& p = isSolo ? soloParams [(size_t) slot]
                                       : styleParams[(size_t) slot];

                if (onSlotParamsChanged) onSlotParamsChanged (slot, isSolo, p);

                if (onSweetenerChanged && ! roleIsDrum (isSolo, slot))
                    onSweetenerChanged (slot, isSolo, p.sweet);

                // GAIN, AND IT IS THE SAME BUG WITH A DIFFERENT SYMPTOM.
                //
                // gainPercent is deliberately absent from ChannelParams (a bulk
                // push must not be able to move a calibration), so the line
                // above does not carry it — and selectChannelPreset resets the
                // channel to unity on its own account:
                //
                //     setChannelInstrumentGainPercent (channelIndex, 100.0f);
                //
                // followed by the .ins trim only IF the sound has one.  A style
                // instrument with no preset therefore came back at 100 % however
                // the set had it trimmed, and like the band filter it looked
                // fine on the slider.
                //
                // The host pairs this with the channel's CURRENT base unity, so
                // whatever the load path established stays put and only the
                // slot's own trim is restored.
                if (onSlotGainChanged)
                    onSlotGainChanged (slot, isSolo, p.gainPercent);

                // Click travels on its own route too.  Nothing currently resets
                // it, but it is per-slot state the set owns, and this function
                // is meant to be the whole of what the set owns per slot rather
                // than the subset that happens to be broken today.
                if (onClickParamsChanged)
                    onClickParamsChanged (slot, isSolo, p.clickEnabled,
                                          p.clickVolume, p.clickDecayMs);

                if (onOctaveBiasChanged)
                    onOctaveBiasChanged (slot, isSolo,
                                         slotOctaveBias[isSolo ? 1 : 0][(size_t) slot]);
            }
        }
    }

    void commitSlotParamsToEngine (int slot, bool isSolo, const SlotParams& p)
    {
        if (onClickParamsChanged)
            onClickParamsChanged (slot, isSolo, p.clickEnabled, p.clickVolume, p.clickDecayMs);

        // The sweetener rides beside the bulk push, never inside it.
        if (onSweetenerChanged && ! roleIsDrum (isSolo, slot))
            onSweetenerChanged (slot, isSolo, p.sweet);

        // FUNKEY MODE interception.  Every FX push in the plugin funnels through
        // here, so this is the one place the macro has to act.  A COPY is
        // overridden and sent; the slot's own params are never written, so the
        // private FX survive intact and switching the macro off restores them
        // exactly (see GlobalMacros::overrideFx).
        //
        // The GM program comes from the slot's own patch — the flag actually
        // sounding — so a mid-song program change onto or off a covered family
        // takes effect on the next commit, exactly like the per-instrument
        // fader trims.
        const bool drumSlot = roleIsDrum (isSolo, slot);

        if (onSlotParamsChanged)
        {
            const int gmProg = isSolo ? soloPatch [(size_t) juce::jlimit (0, 7, slot)]
                                      : stylePatch[(size_t) juce::jlimit (0, 7, slot)];

            // Gate on the MACRO, not on family coverage: the shared chorus +
            // reverb reaches every melodic channel, so an instrument outside
            // the seven families still has to pass through overrideFx (which
            // leaves its private EQ / wah / phaser / delay alone).
            if (! drumSlot && Betel::GlobalMacros::get().isFunkeyOn())
            {
                SlotParams macroP = p;                       // copy — never the original
                Betel::GlobalMacros::get().overrideFx (macroP, gmProg, drumSlot);
                onSlotParamsChanged (slot, isSolo, macroP);
            }
            else
            {
                onSlotParamsChanged (slot, isSolo, p);
            }
        }

        // BIG DRUMS interception — the drum twin of the Funkey block above.
        //
        // A kit's FX rack does NOT travel to the engine inside SlotParams
        // (applyChannelParams ignores it); it goes out through onKitFxChanged.
        // So the macro has to intervene on its own push, and that push has to
        // happen on EVERY commit rather than only when the rack is edited —
        // that is what makes the toggle work in both directions: switching Big
        // Drums on sends the macro rack, switching it off re-sends the kit's
        // own, and the kit's stored rack is never written either way.
        if (drumSlot) publishKitFx (slot, isSolo, p.drumKit.fx);

        // Hidden octave bias rides along with every commit.  It's cached rather
        // than fired at the point it's computed, because applySoundCalibration()
        // also runs from the CONSTRUCTOR — before the host has wired any
        // callbacks — so a fresh instance would otherwise never receive the bass
        // seed.  commitAllSlotParamsToEngine() (called once after wiring) then
        // flushes every slot's cached value.
        if (onOctaveBiasChanged && slot >= 0 && slot < 8)
            onOctaveBiasChanged (slot, isSolo,
                                 slotOctaveBias[isSolo ? 1 : 0][(size_t) slot]);
    }

    //==========================================================================
    // THE ONLY TWO WAYS A DRUM RACK REACHES THE ENGINE.
    //
    // Everything that publishes a kit or a rack goes through these, and they
    // apply Big Drums on the way out.  That matters because loading a kit
    // pushes the WHOLE DrumKitParams — the kit's own rack included — which
    // would otherwise stomp the macro the instant the user picked a new kit
    // from the SOUNDS tab, leaving the macro looking dead until a slider in its
    // editor happened to re-push it.
    //
    // Both take the caller's struct by const ref and send a COPY, so the slot's
    // stored rack is never written and switching the macro off restores it
    // exactly — the same lossless contract the melodic side has.
    //==========================================================================
    /** BIG DRUMS governs the DRUMS slot only.
    
        PERC is a second, independent kit — in Korg conversions often a full one
        — and a macro named for the drums has no business rewriting its rack.
        Sharing one macro across both also made the two impossible to balance
        against each other, which is most of what a perc part is for. */
    static bool isBigDrumsTarget (int slot, bool isSolo) noexcept
    {
        return ! isSolo && slot == 0;                     // style slot 0 = DRUMS
    }

    void publishDrumKit (int slot, bool isSolo, const DrumKitParams& kit)
    {
        if (! onApplyDrumKit) return;
        DrumKitParams k = kit;                            // copy — never the original
        if (isBigDrumsTarget (slot, isSolo))
            Betel::GlobalMacros::get().overrideDrumFx (k.fx);
        onApplyDrumKit (slot, isSolo, k);
    }

    void publishKitFx (int slot, bool isSolo, const DrumKitFxParams& fx)
    {
        if (! onKitFxChanged) return;
        DrumKitFxParams f = fx;                           // copy — never the original
        if (isBigDrumsTarget (slot, isSolo))
            Betel::GlobalMacros::get().overrideDrumFx (f);
        onKitFxChanged (slot, isSolo, f);
    }

    /** Re-push every slot after a macro is switched on or off — or after a knob
        moves inside a macro editor, which is what makes those edits audible
        live.  The toggle itself does nothing to the audio; this is what makes it
        take effect, and because commitSlotParamsToEngine re-decides
        macro-vs-private per slot (melodic AND drum), it works identically in
        both directions. */
    void refreshMacroFx() { commitAllSlotParamsToEngine(); }

    /** Re-push ONLY the drum rack — what BIG DRUMS actually governs.
    
        A macro toggle used to go through refreshMacroFx, which commits all 16
        slots: envelopes, filter, octave, octave bias, note range, FX, the lot.
        For a macro that touches one kit's effects that is enormous overreach —
        it re-asserts the editor's whole picture over everything the style set,
        and any cached value that has drifted lands on the engine at that
        instant.  Big Drums needs one publish, so it does one. */
    void refreshBigDrums()
    {
        if (roleIsDrum (false, 0))
            publishKitFx (0, false, styleParams[0].drumKit.fx);
    }

    /** Commit every slot's params (both solo and style banks) to the engine.
        Call once after the host has wired the callbacks so a fresh instance has
        the engine matching the editor's seeded defaults without a drag. */
    void commitAllSlotParamsToEngine()
    {
        for (int slot = 0; slot < 8; ++slot)
        {
            commitSlotParamsToEngine (slot, true,  soloParams [(size_t) slot]);
            commitSlotParamsToEngine (slot, false, styleParams[(size_t) slot]);
        }
    }

    // ── GM instrument-type amp-envelope defaults ────────────────────────────
    // Release per GM family, in slider units (× 0.03 s → seconds; R max 3 s):
    //   grand/honky-tonk 30 · e-pianos 15 · harpsi/clavi + chromatic perc 20 ·
    //   organs 15 · acoustic gtr 25 · other gtr 15 · bass 15 · solo strings 15
    //   (timpani 20) · string sections 30 · choir 25 · orch-hit 15 · blown
    //   (brass/reed/pipe) 15 · leads 15 · pads 25 · ethnic 15 · drumish 15 ·
    //   GM FX / SFX 15.
    static int gmReleaseSlider (int prog)
    {
        if (prog == 0 || prog == 1 || prog == 3) return 30;   // grands + honky-tonk
        if (prog == 2 || prog == 4 || prog == 5) return 15;   // electric pianos
        if (prog >=  6 && prog <=  15) return 20;             // harpsi/clavi + chrom.perc
        if (prog >= 16 && prog <=  23) return 15;             // organs
        if (prog == 24 || prog ==  25) return 25;             // acoustic guitars
        if (prog >= 26 && prog <=  31) return 15;             // other guitars
        if (prog >= 32 && prog <=  39) return 4;              // basses (short/tight)
        if (prog >= 40 && prog <=  46) return 15;             // solo strings
        if (prog == 47)                return 20;             // timpani
        if (prog >= 48 && prog <=  51) return 30;             // string sections
        if (prog >= 52 && prog <=  54) return 25;             // choir / voice
        if (prog == 55)                return 15;             // orchestra hit
        if (prog >= 56 && prog <=  79) return 15;             // blown: brass/reeds/pipes
        if (prog >= 80 && prog <=  87) return 15;             // leads
        if (prog >= 88 && prog <=  95) return 25;             // pads
        if (prog >= 104 && prog <= 111) return 15;            // ethnic
        if (prog >= 112 && prog <= 119) return 15;            // drumish
        return 15;                                            // GM FX 96-103 + SFX 120-127
    }

    // Amp decay/release CURVE per GM family (0=Exp 1=Lin 2=Log).  Log for the
    // swell/fade families (string sections 48-51, choir/voice 52-54, synth pads
    // 88-95); Exp — the natural pluck/blown contour, and the app's prior
    // character — for everything else.  Lin is never a default (hand-dialed).
    static int gmAmpCurve (int prog)
    {
        if (prog >= 48 && prog <= 54) return 2;   // string sections + choir -> Log
        if (prog >= 88 && prog <= 95) return 2;   // synth pads            -> Log
        return 0;                                  // Exp
    }

    // Apply the instrument-type amp-envelope defaults to a slot holding a GM
    // voice (progs 0..127 ONLY — reference blobs and kits are untouched):
    // A 0 · D 7 s real (slider 100) · S 0 · R per family.  SlotParams are the
    // authoritative layer, so set them here and commit to the engine.
    // SFF destination channel 11 (the BASS part) maps to style slot 2, and the
    // bass fold floor is C1 — see applyGmEnvDefaults / applySoundCalibration.
    static constexpr int kStyleSlotBass = 2;
    static constexpr int kStyleSlotPad  = 5;   // style slot 5 = "PAD"
    static constexpr int kBassNoteFloor = 24;   // C1  — floor for basses generally
    static constexpr int kGmElecBassFinger = 33; // GM 33 (0-based) = Electric Bass (finger)
    static constexpr int kElecBassFingerFloor = 31; // G1 — see bassNoteFloorFor
    static constexpr int kGmSynthBass1  = 38;   // GM 38 (0-based) = Synth Bass 1
    static constexpr int kGmSynthBass2  = 39;   // GM 39 (0-based) = Synth Bass 2

    // NYLON GUITAR (GM 24) on a STYLE slot: allowed notes [56, 127].
    // Styles use the nylon guitar as a high comping / arpeggio voice, so its
    // window opens at G#3 and runs to the top of the keyboard rather than
    // taking the generic F#2 floor.  Style slots only — a nylon guitar the
    // USER picks for the right hand keeps the full range.
    static constexpr int kGmFiddle        = 110;  // GM 110 (0-based) = Fiddle
    static constexpr int kGmDrawbarOrgan  = 16;   // GM 16  (0-based) = Drawbar Organ
    static constexpr int kGmNylonGuitar   = 24;
    static constexpr int kNylonGuitarLo   = 56;
    static constexpr int kNylonGuitarHi   = 127;
    static constexpr int kSynthBass2Floor = 44; // G#2
    // Bass window CEILING.  SynthesisPanel locks the bass window to exactly 12
    // notes and slides it (applyGapRule, mode 1) — but that runs on the UI's own
    // copy, while the ENGINE gets whatever noteRangeHi the params carry.  With
    // the general A2 ceiling that mismatch was harmless (the engine window was
    // merely wider than the display); at a floor of 44 it would leave the engine
    // a TWO-note window and fold the whole bass line into it.  So Synth Bass 2
    // carries its own ceiling, floor + 11, and the two agree.
    static constexpr int kBassNoteCeil    = 45; // A2  — general bass ceiling
    static constexpr int kSynthBass2Ceil  = kSynthBass2Floor + 11;

    /** Allowed-notes FLOOR for a bass slot, by GM program.  The whole table:
            GM 32 Acoustic Bass            24  C1
            GM 33 Electric Bass (finger)   31  G1
            GM 34 Electric Bass (pick)     24  C1
            GM 35 Fretless Bass            24  C1
            GM 36 Slap Bass 1              24  C1
            GM 37 Slap Bass 2              24  C1
            GM 38 Synth Bass 1             24  C1
            GM 39 Synth Bass 2             44  G#2
        Only the two exceptions are named; everything else — including any
        non-bass program that lands on the bass slot — takes kBassNoteFloor. */
    static int bassNoteFloorFor (int gmProg) noexcept
    {
        if (gmProg == kGmSynthBass2)     return kSynthBass2Floor;
        if (gmProg == kGmElecBassFinger) return kElecBassFingerFloor;
        return kBassNoteFloor;
    }

    /** Matching CEILING for a bass slot — see kBassNoteCeil. */
    static int bassNoteCeilFor (int gmProg) noexcept
    {
        return (gmProg == kGmSynthBass2) ? kSynthBass2Ceil : kBassNoteCeil;
    }

    // Hidden octave bias per slot, [isSolo][slot].  Recomputed by
    // applyGmEnvDefaults on every voice load and flushed by
    // commitSlotParamsToEngine.  Not a user parameter and not serialised — it is
    // fully derived from (slot, GM program), so it can't go stale.
    //==========================================================================
    // Hidden octave bias for a slot, from the GM program sitting on it.
    //
    // A PROPERTY OF THE LOADED PROGRAM, so it has to follow the program no
    // matter who changed it.  It used to be computed only in applyGmEnvDefaults
    // — the UI-driven path — while a STYLE program change went through
    // setSlotPatch, which never touched it.  The stale value then sat in the
    // cache doing nothing until something committed every slot at once, and the
    // only thing that does that is a macro toggle: switching BIG DRUMS on
    // re-sent a +1 or +2 left over from whenever that slot last held a bass, and
    // an unrelated instrument jumped an octave.  Hence "turning on Big Drums
    // shifts some instruments up".
    //==========================================================================
    static int octaveBiasFor (int slot, bool isSolo, int gmProg) noexcept
    {
        const bool bassSlot = (! isSolo && slot == kStyleSlotBass);
        const bool gmBass   = (gmProg >= 32 && gmProg <= 39);
        if (! (bassSlot || gmBass)) return 0;
        return (gmProg == kGmSynthBass1) ? 2 : 1;
    }

    int slotOctaveBias[2][8] {};

    /** Does a SET govern this style slot?  Set by applyState for every slot the
        set carried, cleared by clearStyleSlotOwnership when a style loads bare.
        Read only by applySoundCalibration, which must not overwrite a value the
        user has deliberately saved. */
    bool styleSlotFromSet[8] {};

    //==========================================================================
    // THE SET OWNS THE EIGHT STYLE SLOTS, COMPLETELY.
    //
    // It already did, and I missed it: <SoundsState> has ALWAYS carried the full
    // SlotParams for all sixteen slots through BetelStateXml::saveSlot, not just
    // patch/ref as it looked from the outside.  So the separate <StyleSlots>
    // block added earlier was a SECOND owner of the same values — the exact
    // anti-pattern the whole scope cleanup exists to remove.  It is gone; the
    // baker writes into <SoundsState><StyleSlots> instead.
    //
    // ── THE SPLIT ────────────────────────────────────────────────────────────
    //
    //   STYLE slots (0..7)   the SET decides.  Nothing else does.
    //   SOLO  slots (0..7)   the .ins files decide the SOUND; the set remembers
    //                        WHICH instrument is chosen, and its mixer fader.
    //
    // That is why .sins is gone: a style-side preset file existed to calibrate a
    // sound differently for arrangements than for the right hand, which is
    // exactly the job the set now does — per style, rather than once per
    // instrument for the whole library.
    //==========================================================================

    /** BAKE.  Run the GM table for a style's resolved slot programs and return a
        <SoundsState> carrying ONLY its <StyleSlots> half.

        Solo slots are deliberately absent rather than defaulted: applyState
        iterates whatever children it finds, so an absent SoloSlots means the
        player's right hand is left exactly as it is — which is the whole point
        of the split. */
    static juce::ValueTree bakeStyleSlots (const int styleFlags[8])
    {
        juce::ValueTree state ("SoundsState");
        juce::ValueTree styl  ("StyleSlots");

        for (int i = 0; i < 8; ++i)
        {
            SlotParams p; int bias = 0;
            gmDefaultsInto (p, bias, /*isSolo*/ false, i, styleFlags[i]);

            auto child = BetelStateXml::saveSlot (p);
            child.setProperty ("index",      i,    nullptr);
            child.setProperty ("octaveBias", bias, nullptr);
            styl.appendChild (child, nullptr);
        }

        state.appendChild (styl, nullptr);
        return state;
    }

    //==========================================================================
    // THE GM DEFAULT TABLE — NO LONGER AUTOMATIC.
    //
    // This used to run on every voice pick AND on every style load, silently
    // rewriting the envelope, filter, note range, play mode and octave of every
    // style slot from its GM program number.  Two things were wrong with that:
    // it was invisible (nothing on screen said the values had been decided for
    // you), and it was recurring (it landed on top of whatever a saved .ins had
    // just applied — the same defect already found and fixed in
    // applySoundCalibration, which got a slotHasPreset guard while this did not).
    //
    // The table itself is good work — it encodes real knowledge about how GM
    // families behave in an arranger.  So it is not deleted; it is DEMOTED to
    // what it always should have been: the source the SET PACK is BAKED from.
    // SetBaker runs it once per style, writes the result into that style's
    // .bset as a <StyleSlots> block, and from then on THE SET decides.  Edit
    // any of it per style and nothing recomputes over the top.
    //
    // gmDefaultsInto is PURE — it writes into the SlotParams you hand it and
    // touches no member state and no engine — so the baker can call it 633
    // times over without a style ever being loaded.
    //==========================================================================
    static void gmDefaultsInto (SlotParams& p, int& outOctaveBias,
                                bool isSolo, int slot, int gmProg)
    {
        outOctaveBias = 0;
        if (slot < 0 || slot >= 8 || gmProg < 0 || gmProg > 127) return;

        // ── What counts as "the bass" ────────────────────────────────────────
        // A style's bass part is defined by its SLOT — SFF destination channel 11
        // is ALWAYS the bass, which is style slot 2 — NOT by its GM program
        // number.  Yamaha styles routinely put a non-GM-bass program there:
        // oriental styles in particular use synth basses from the MSB-104 panel
        // bank (e.g. PC 81), nowhere near the GM bass family (32-39).
        //
        // Deciding "is this a bass?" from the program alone meant such a part got
        // the GENERIC treatment, and the damage was severe:
        //   * its allowed-notes floor stayed at F#2 (42) against a ceiling of 45,
        //     i.e. a FOUR-semitone window — foldNote then folded every lower note
        //     UP an octave.  A real oriental style bass runs MIDI 32..48, so
        //     ~46% of its notes jumped an octave and the low end of the whole
        //     mix vanished — which is exactly what a high-pass sounds like;
        //   * sustain fell to 0, so the bass decayed away under the 7 s carpet;
        //   * it stayed polyphonic, so overlapping bass notes stacked up.
        //
        // Slot 2 IS the bass, whatever program is loaded on it.
        const bool bassSlot = (! isSolo && slot == kStyleSlotBass);
        const bool gmBass   = (gmProg >= 32 && gmProg <= 39);
        const bool isBass   = bassSlot || gmBass;

        p.attack  = 0.0f;
        // BASS: full sustain.  Bass lines rely on the held level — the global
        // decay-carpet rule (S 0 + 7 s linear decay) made sustained bass notes
        // sag to ~43% by 4 s and vanish on long endings ("bass stops playing").
        // Everything else keeps S 0.
        p.decay   = 7.0f;
        p.sustain = isBass ? 1.0f : 0.0f;
        p.release  = (float) gmReleaseSlider (gmProg) * 0.03f;
        p.ampCurve = gmAmpCurve (gmProg);

        // Filter family default.  Bright, potentially harsh GM families park
        // HALF-OPEN (filter slider = 50, norm 0.5, ~630 Hz) so they don't scream
        // open at full brightness the moment a style loads them; every other
        // family opens full.  Re-applied on each load like the amp envelope
        // above, so a bright voice replacing one of these on a slot returns to
        // full-open.  Two paths cover both filter types: the CLASSIC cutoff
        // (bass slot only, now that the solo slots run the band filter too — see
        // setBandMode) and the BAND high-cut (style slots 3..7 and every solo
        // slot).  Norm 0.5 == the same ~630 Hz for both, so a family behaves
        // identically whichever path it lands on; the band low-cut stays fully
        // open.
        {
            // Families that park half-open.  Ranges are as specified for the
            // arranger (note: GM "ensemble" formally runs 48-55; 55 = Orchestra
            // Hit is deliberately left out here, so the range is 48-54):
            //     strings      40-46   (solo strings; existing)
            //     ensemble     48-54   (string ens / choir / voice)
            //     brass        56-63
            //     reed         64-71
            //     pipe         72-79
            //     synth lead   80-86   (87 "bass+lead" excluded — see below)
            //     synth pad    88-95
            //     fiddle       110     (single program, not a range)
            //     drawbar organ 16      (single program, STYLE SLOTS ONLY)
            //
            // The lead range fills the gap between pipe and synth pad, and for
            // the same reason as the rest of the list: a lead dropped onto a
            // style's chord / pad / phrase slot screams open at full brightness
            // and fights the parts around it.  87 (Lead 8, "bass+lead") is
            // deliberately EXCLUDED — it is a bass voice, the one
            // correctFlagForBassRole maps lead-family basses away from, and it
            // is not what this rule is about.
            const bool halfOpenFamily =
                   (gmProg >= 40 && gmProg <= 46)     // strings
                || (gmProg >= 48 && gmProg <= 54)     // ensemble
                || (gmProg >= 56 && gmProg <= 63)     // brass
                || (gmProg >= 64 && gmProg <= 71)     // reed
                || (gmProg >= 72 && gmProg <= 79)     // pipe
                || (gmProg >= 80 && gmProg <= 86)     // synth lead (87 excluded)
                || (gmProg >= 88 && gmProg <= 95)     // synth pad
                // FIDDLE (GM 110) — an ethnic-family program, so it sits outside
                // every range above and has to be named on its own.  Same reason
                // as the rest of the list: a bowed fiddle is a bright, nasal
                // voice that cuts hard the moment a style loads it.  Its
                // neighbours in 104..111 (sitar, banjo, koto, kalimba, bagpipe,
                // shanai) are deliberately NOT included — only the fiddle was
                // asked for, and each of those has a different top end.
                || (gmProg == kGmFiddle)
                // DRAWBAR ORGAN (GM 16) — STYLE SLOTS ONLY.
                //
                // As a style part it is a comping voice and its upper harmonics
                // fight the chord and phrase slots, so it takes the same 630 Hz
                // high-cut as the rest of this list.  But a drawbar organ is
                // also one of the most common RIGHT-HAND lead voices, and that
                // cut would gut it — so unlike every other entry here this one
                // is gated to the style.
                //
                // NOTE the inconsistency: the ranges above (including the synth
                // leads and the fiddle) are NOT gated, so they park half-open on
                // a solo slot too.  That is the long-standing behaviour of this
                // table; only the organ is carved out, because it was asked for
                // as a style rule and the solo cost is high.
                || (! isSolo && gmProg == kGmDrawbarOrgan);

            // The PAD SLOT is half-open no matter what lands on it.
            //
            // applySoundCalibration already seeds styleParams[5].filterCutoff to
            // 0.5 — but THIS function runs afterwards, and the family test above
            // pushed it straight back to 1.0 for any program outside these GM
            // ranges.  So a style that put a guitar, organ or (formerly) brass on
            // the pad part got a fully-open filter and the pad stopped sitting
            // back in the mix.
            //
            // Slot 5 IS the pad, whatever instrument the style loads there — the
            // same reasoning as the BASS slot above.  Decided by the SLOT, not by
            // the program number.
            const bool padSlot = (! isSolo && slot == kStyleSlotPad);
            const bool halfOpen = (padSlot || halfOpenFamily);

            // The instrument channel's FILTER is the two-handle band slider:
            //     filterHpNorm = LEFT  handle (high-pass / low-cut)
            //     filterLpNorm = RIGHT handle (low-pass  / high-cut)
            // "50" on the filter slider means the RIGHT handle at 0.5 (~630 Hz),
            // with the LEFT handle left fully open at 0 — which is exactly what
            // the half-open families get.  filterCutoff is the SEPARATE classic
            // filter (bass / solo slots); it is pinned to match so the two can't
            // disagree.
            p.filterCutoff = halfOpen ? 0.5f : 1.0f;   // classic filter  → slider 50
            p.filterHpNorm = 0.0f;                     // band LEFT  handle: open
            p.filterLpNorm = halfOpen ? 0.5f : 1.0f;   // band RIGHT handle: slider 50
        }

        // Allowed-notes floor.  The ceiling is A2 (45), so the floor decides the
        // width of the fold window — get it wrong and foldNote octave-shifts the
        // part instead of merely bounding it.
        //
        // BASS -> C1 (24), matching StylePlayer's own default and its stated
        // intent ("its floor is dropped to C1 so low bass notes, incl. ending
        // roots, aren't folded up").  The previous F#2 (42) gave a FOUR-semitone
        // window and gutted the low end.  Everything non-bass keeps F#2.
        //
        // SYNTH BASS 2 (GM 39) carries its own window [44, 55].  Note that
        // SynthesisPanel locks
        // the BASS window to exactly 12 notes and SLIDES it (applyGapRule, mode
        // 1), so this floor sets where that window sits, not how wide it is.
        //
        // Every bass program's floor now lives in ONE table, bassNoteFloorFor,
        // so adding or retuning one is a single line there rather than another
        // nested conditional here.
        // NYLON GUITAR (GM 24) on a style slot takes its own window, floor AND
        // ceiling — see kNylonGuitarLo/Hi.  Everything else keeps the generic
        // F#2 floor and whatever ceiling the slot already carries.
        const bool nylonStyleVoice = (! isSolo && ! isBass && gmProg == kGmNylonGuitar);

        // BASS IS NO LONGER PENNED IN.
        //
        // The bass slot used to have both edges forced from a table on every
        // program change — floor from bassNoteFloorFor, ceiling from
        // bassNoteCeilFor, which for Synth Bass 2 is floor + 11.  That is a
        // twelve-semitone window the user could set but never keep: the next
        // program change put the table back.  Every other instrument gets a
        // seed and is then left alone, and the bass now works the same way.
        //
        // The floor is still seeded — a bass loaded into thin air needs a
        // sensible register — but the ceiling is left at whatever the slot
        // carries, so a widened window survives.
        p.noteRangeLo = isBass            ? bassNoteFloorFor (gmProg)
                      : nylonStyleVoice   ? kNylonGuitarLo
                                          : 42;
        if (nylonStyleVoice)
        {
            p.noteRangeHi = kNylonGuitarHi;
            p.noteRangeOn = true;      // the window only bites when it is armed
        }

        // BASS: Mono with HOLD STOLEN only (no re-attack on a new note or on the
        // returned note) — the bass part is monophonic on hardware, and letting
        // it run polyphonic lets overlapping style bass notes stack and thicken
        // into mud.  Non-bass voices keep their own mono state.
        if (isBass)
        {
            p.playMode         = 1;      // Mono
            p.monoHoldStolen   = true;
            p.monoRetrigNew    = false;
            p.monoRetrigStolen = false;
        }

        // ── Bass register bias ───────────────────────────────────────────────
        // The bass part is lifted a whole octave as its ZERO state: the engine
        // plays +12 semitones while the OCTAVE slider still reads 0.  That's
        // precisely what Channel::octaveBias exists for — the hook was written
        // and never wired, which is why the bass sat an octave low.
        //
        // Synth Bass 1 (GM 38) sits a further octave down again, so it takes +2.
        // Everything that isn't a bass keeps 0.
        outOctaveBias = octaveBiasFor (slot, isSolo, gmProg);
    }

    /** Run the table for one slot and push the result onto the engine.

        The ONLY callers left are the bake path and an explicit user action —
        nothing applies this automatically any more.  Kept as a named function
        so every caller shares one definition. */
    void applyGmEnvDefaults (bool isSolo, int slot, int gmProg)
    {
        if (slot < 0 || slot >= 8) return;
        auto& p = isSolo ? soloParams[(size_t) slot] : styleParams[(size_t) slot];
        int bias = 0;
        gmDefaultsInto (p, bias, isSolo, slot, gmProg);
        slotOctaveBias[isSolo ? 1 : 0][(size_t) slot] = bias;
        commitSlotParamsToEngine (slot, isSolo, p);   // sends the bias too
    }

    //==========================================================================
    // SOUND CALIBRATION
    //
    // Baseline tweaks that tame the style sound system so a freshly-loaded
    // style (or a restored set) can't surprise with a harsh voice.  Re-asserted
    // at the end of applyState() so it always wins over whatever a saved set
    // pinned; the constructor seeds the same values for the no-set startup path.
    //
    //   * PAD channel (style slot 5): filter cutoff parked at 0.5 (slider "50")
    //     so strings / pads don't scream open at full brightness.
    //   * DRUM channels (style slots 0 = DRUMS, 1 = PERC): every kit-FX stage
    //     (EQ / SAT / COMP / REV / DEL) forced OFF so styles start dry.
    //
    // Everything here is COMMITTED to the engine in-place (no control needs to
    // be physically moved for it to take effect).
    //==========================================================================
    void applySoundCalibration()
    {
        // A SAVED VOICE OUTRANKS THIS.
        //
        // This runs on EVERY style load, and it used to run unconditionally —
        // so a bulk push of the whole PAD slot and a forced-off drum rack
        // landed on top of the .sins / .drm that applyVoiceSetup had applied a
        // moment earlier, wiping it every single time the style was loaded.
        // That is the "Save as Default does nothing" report: the save worked,
        // and this undid it immediately afterwards.
        //
        // These values are a factory safety net for slots the user has not
        // spoken for.  Once a preset exists for the sound sitting on that slot,
        // the user HAS spoken, and the net gets out of the way.

        // PAD: half-open low-pass.
        if (! slotHasPreset (5, false) && ! styleSlotFromSet[5])
        {
            styleParams[5].filterCutoff = 0.5f;
            commitSlotParamsToEngine (5, false, styleParams[5]);
        }

        // THE DRUMS/PERC FORCE-OFF IS GONE TOO.
        //
        // It existed to undo the auto-compressor above — one hidden hand
        // cancelling another.  With nothing switching effects on behind the
        // user's back there is nothing to undo, and blanking the rack here would
        // itself be a third hand: it would silently clear a stage the user had
        // enabled on a style that happens to have no set yet.
        //
        // Every drum effect now has exactly one owner: its toggle, saved in the
        // set.
    }

    SoundsTab()
    {
        setOpaque(false);

        // SOUND CALIBRATION baseline (see applySoundCalibration): the PAD
        // channel starts half-open so styles can't blast bright strings/pads.
        styleParams[5].filterCutoff = 0.5f;

        // Bass (slot 2): the per-slot SlotParams are the authoritative source
        // pushed onto the engine (commitSlotParamsToEngine / onSlotParamsChanged),
        // so the bass defaults MUST live here or the push overrides them.
        //  - Mono by default (hardware bass is monophonic) so overlapping /
        //    over-long style bass notes can't stack into a two-pitch clash.
        //    Flags are independent (multi-select): fall back to a held note,
        //    re-attack each new note, glide the returned note.
        //  - Allowed-notes window ON with the floor at C1 so low bass and ending
        //    roots aren't folded up an octave.
        // SWEETENER: DEFAULT OFF ON EVERY SLOT, KIT SLOTS INCLUDED.
        //
        // It shipped on for DRUMS and PERC and that was the wrong call twice
        // over: a corrective stage that arrives already engaged gives the user
        // no baseline to judge it against, and while ROUND was mis-normalised
        // it meant every kit came up +8 dB hotter than before with no visible
        // cause.  It is a tool now, not a policy — turn it on per slot from the
        // SWEET tab (kits) or the SWEETEN page (melodic).

        styleParams[2].playMode         = 1;     // Mono
        styleParams[2].monoHoldStolen   = true;  // HOLD STOLEN only
        styleParams[2].monoRetrigNew    = false; // no re-attack on a new note
        styleParams[2].monoRetrigStolen = false; // no re-attack on the returned note
        styleParams[2].sustain          = 1.0f;  // bass holds — see applyGmEnvDefaults
        styleParams[2].release          = 0.12f; // slider 4 (short/tight)
        styleParams[2].noteRangeOn = true;
        // C1 floor, NOT F#2.  This value is pushed straight through to
        // StylePlayer (commitSlotParamsToEngine -> setChannelNoteRange), so an
        // F#2 seed here overwrote the player's own correct C1 default and left
        // the bass with a four-semitone fold window.  See applyGmEnvDefaults.
        styleParams[2].noteRangeLo = kBassNoteFloor;   // C1 (24)
        styleParams[2].noteRangeHi = kBassNoteCeil;    // A2 — a CONSTRUCTOR seed only.
        // applyGmEnvDefaults no longer re-forces this on every program change,
        // so widening the window in the editor now sticks.

        // Bass register: +1 octave as the ZERO state (see applyGmEnvDefaults).
        // Cache only — this also runs from the constructor, before any callback
        // exists.  commitAllSlotParamsToEngine() flushes it once the host wires up.
        slotOctaveBias[0][kStyleSlotBass] = 1;

        btnModeSolo .setButtonText("SOLO INSTRUMENTS");
        btnModeStyle.setButtonText("STYLE INSTRUMENTS");
        btnModeSolo .onClick = [this]{ setMode(true);  };
        btnModeStyle.onClick = [this]{ setMode(false); };
        addAndMakeVisible(btnModeSolo);
        addAndMakeVisible(btnModeStyle);

        // ── BANK: GM / WORLD ─────────────────────────────────────────────────
        //
        // Sits between the channel editor and the instrument selector, because
        // that is the order the decision is actually made in: which slot, then
        // which library, then which sound.
        //
        // GM is the 128 program-change addressable sounds, laid out by timbre.
        // GM ships with the plugin; WORLD and ORIENTAL are optional packs, each
        // its own folder under the root.  Which folder a sound was found in
        // decides its pack - see SamplePlayerEngine::setSoundLibraryFolders.
        //
        // (Historic: this row was GM / EXPANSION, and before that GM / WORLD,
        // back when everything non-GM was one undivided flag >= 200 range.)
        // WORLD is everything the library carries at flag >= 200 - the space the
        // engine already reserved for sounds a style cannot ask for by program
        // change, which is exactly what a bouzouki or a kanun is.
        btnBankGm   .setButtonText("GM");
        // EXPANSION, not WORLD: this bank stopped being "ethnic instruments" and
        // became where every added sound pack lands, which is what flag >= 200
        // has always actually meant.  Caption only - the internal name stays
        // btnBankWorld / worldBank so nothing else has to move.
        btnBankWorld   .setButtonText("WORLD");
        btnBankOriental.setButtonText("ORIENTAL");
        for (int i = 0; i < kMaxFullKitCells; ++i)
        {
            auto& b = fullKitButtons[(size_t) i];
            b.onClick = [this, i]
            {
                if (i >= (int) fullKits.size()) return;
                const auto& k = fullKits[(size_t) i];
                if (onFullKitSelected)
                    onFullKitSelected (selectedSlot, soloMode,
                                       std::get<0>(k), std::get<1>(k), std::get<2>(k));
                refreshInstrDisplay();
                reloadInstrImage();
                pushDrumEditorMode();   // an open editor becomes the sampled one
            };
            addChildComponent (b);
        }

        btnBankGm      .onClick = [this]{ setPack(0); };
        btnBankWorld   .onClick = [this]{ setPack(1); };
        btnBankOriental.onClick = [this]{ setPack(2); };
        addAndMakeVisible(btnBankGm);
        addAndMakeVisible(btnBankWorld);
        addAndMakeVisible(btnBankOriental);

        // ── PAGE WITHIN A CATEGORY ───────────────────────────────────────────
        // A category holds as many sounds as the folder holds; the row shows 8.
        // These sit under the cells, in the band the retired reference pills
        // left empty, so nothing else had to move to make room.
        // YELLOW FACE, BLACK SYMBOL.  These are the only controls in the strip
        // that MOVE you somewhere rather than select something, so they read as
        // navigation instead of as two more dark cells among many.
        //
        // textColourOffId AND textColourOnId: JUCE picks the "on" colour while a
        // button is held, and leaving that white would flash the symbol back to
        // invisible for the length of every click.
        btnPagePrev.setButtonText ("-");
        btnPageNext.setButtonText ("+");
        for (auto* b : { &btnPagePrev, &btnPageNext })
        {
            b->setColour (juce::TextButton::buttonColourId,   kPageYellow);
            b->setColour (juce::TextButton::buttonOnColourId, kPageYellow);
            b->setColour (juce::TextButton::textColourOffId,  juce::Colours::black);
            b->setColour (juce::TextButton::textColourOnId,   juce::Colours::black);
            addAndMakeVisible (*b);
        }
        btnPagePrev.onClick = [this]{ stepPackPage (-1); };
        btnPageNext.onClick = [this]{ stepPackPage (+1); };

        pageCaption.setText ("PAGE", juce::dontSendNotification);
        pageCaption.setJustificationType (juce::Justification::centredRight);
        pageCaption.setColour (juce::Label::textColourId, kPageYellow);
        addAndMakeVisible (pageCaption);

        pageLabel.setJustificationType (juce::Justification::centredLeft);
        pageLabel.setColour (juce::Label::textColourId, kPageYellow);
        addAndMakeVisible (pageLabel);

        for (int i = 0; i < 8; ++i)
        {
            slotButtons[i].onClick = [this, i]{ selectSlot(i); };
            addAndMakeVisible(slotButtons[i]);
        }
        for (int i = 0; i < kNumTimbres; ++i)
        {
            timbreButtons[i].setButtonText(kTimbreNames[i]);
            timbreButtons[i].onClick = [this, i]{ selectTimbre(i); };
            addAndMakeVisible(timbreButtons[i]);
        }
        for (int i = 0; i < kMaxInstrCells; ++i)
        {
            instrButtons[i].onClick = [this, i]{ selectInstrument(i); };
            addAndMakeVisible(instrButtons[i]);
        }

        for (int i = 0; i < 8; ++i)
        {
            refPills[i].onSelect = [this, i] (int refId)
            {
                if (soloMode) soloRef[(size_t) i]  = refId;
                else          styleRef[(size_t) i] = refId;
                if (onReferenceSelected) onReferenceSelected (soloMode, i, refId);
            };
            addAndMakeVisible(refPills[i]);
        }

        addAndMakeVisible(spaceAnim);
        addAndMakeVisible(instrDisplay);

        btnEdit.setButtonText("EDIT");
        btnEdit.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xFF222222));
        btnEdit.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btnEdit.onClick = [this]{ toggleEditorWindow(); };
        addAndMakeVisible(btnEdit);

        // IGNORE PROGRAM CHANGE.  Under EDIT because it belongs to the same
        // question - what is on this slot and who decides it.
        //
        // Style slots only: a solo slot never receives a style program change,
        // so a toggle there would promise something it does not do.
        btnIgnorePc.setButtonText("IGNORE PROGRAM CHANGE");
        btnIgnorePc.setTooltip("The style cannot change this slot's instrument. "
                               "Your pick is saved with the set.");
        // THE TEXT carries the state, not the fill: both background colours are
        // the same dark, so switching the toggle lights the label orange and
        // leaves the button itself sitting quietly under EDIT.
        btnIgnorePc.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFF222222));
        btnIgnorePc.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF222222));
        btnIgnorePc.setColour(juce::TextButton::textColourOffId,  juce::Colours::white);
        btnIgnorePc.setColour(juce::TextButton::textColourOnId,   juce::Colour(0xFFCC6600));
        btnIgnorePc.onClick = [this]
        {
            if (soloMode) return;
            const bool now = ! (onGetIgnorePc && onGetIgnorePc(selectedSlot, false));
            if (onSetIgnorePc) onSetIgnorePc(selectedSlot, false, now);
            refreshIgnorePcButton();
        };
        addAndMakeVisible(btnIgnorePc);

        soloPatch .fill(0);
        stylePatch.fill(0);
        soloIsDrumSlot .fill(false);
        styleIsDrumSlot.fill(false);
        soloRef .fill(-1);
        styleRef.fill(-1);
        syncDrumFlagsFromRole();

        setMode(true);
    }

    ~SoundsTab() override = default;

    void setSlotPatch(bool isSolo, int slot, int patch)
    {
        // Keep the hidden octave bias with the program that is actually loaded.
        // This is the STYLE-driven path; without it the cached bias belongs to
        // whatever the UI last selected here, and the next full commit applies
        // it to the wrong instrument.  Cache only — the engine already has the
        // right shift for the note it is playing, and re-pushing here would
        // fight the style.
        if (slot >= 0 && slot < 8)
            slotOctaveBias[isSolo ? 1 : 0][(size_t) slot] =
                octaveBiasFor (slot, isSolo, juce::jlimit (0, 127, patch));

        if (isSolo)  { if (slot >= 0 && slot < 8) soloPatch[slot]  = patch; }
        else         { if (slot >= 0 && slot < 8) stylePatch[slot] = patch; }
        if (soloMode == isSolo) { syncTimbreToSlot(); refreshInstrDisplay(); reloadInstrImage(); refreshEditorSlotLabel(); }
    }

    int getSlotPatch(bool isSolo, int slot) const
    {
        if (isSolo) return (slot >= 0 && slot < 8) ? soloPatch[slot]  : 0;
        else        return (slot >= 0 && slot < 8) ? stylePatch[slot] : 0;
    }

    /** A sound load put a saved voice on this slot — adopt it as the slot's
        params so the editor shows what is actually playing.

        Deliberately does NOT do the BULK commit: the engine already holds the
        channel values (selectChannelPreset applied them), and re-pushing would
        be a wholesale write that the per-sound calibration trim is specifically
        kept out of.  Refreshes the open editor only when it is showing this
        slot.

        THREE THINGS DO NOT TRAVEL THAT WAY, though, and they are the reason a
        freshly loaded preset used to need a control nudged before it took
        effect:

          • ALLOWED NOTES lives in StylePlayer's own atomics, not in the engine
            channel.  selectChannelPreset has never heard of it, so the fold
            window kept whatever the previous sound left behind.
          • The drum kit's FX RACK goes out through onKitFxChanged;
            applyChannelParams ignores drumKit entirely.  So a .drm's effect
            toggles loaded into the UI and stayed inaudible.
          • The metronome CLICK has its own route for the same reason.

        Each is pushed here on its own narrow callback.  That is what makes a
        preset load complete, and it still leaves the bulk push — and the
        calibration trim inside it — untouched. */
    //==========================================================================
    // IGNORE PRESET CHANGES - a per-slot parameter freeze.
    //
    // While a slot is frozen, nothing OUTSIDE the sound editor may write its
    // params: not a set load, not re-selecting the instrument, not a preset
    // reload triggered by somebody else's save.  The user's own edits in the
    // editor still apply - that is the whole point.
    //
    // Voicing a sound used to mean losing it: every set load, and every trip
    // back to the instrument, stamped the .ins or the set over the work in
    // progress.  Freeze, tune until it is right, SAVE SETTINGS, unfreeze.
    //
    // SESSION STATE, deliberately not saved with the set.  It is scaffolding for
    // the act of programming, and a set that came back with slots mysteriously
    // refusing to load would be a worse bug than the one it solves.
    //
    // Indexed [isSolo][slot] to match paramsFor().
    //==========================================================================
    bool paramsFrozen[2][8] {};

    bool isSlotFrozen (int slot, bool isSolo) const
    {
        if (slot < 0 || slot >= 8) return false;
        return paramsFrozen[isSolo ? 1 : 0][(size_t) slot];
    }

    /** Set by the editor's toggle.  Also tells the ENGINE, because a .ins can
        reach the audio without passing through this tab at all - a style's
        program change goes straight to the channel. */
    std::function<void(int slot, bool isSolo, bool ignore)> onSlotFreezeChanged;

    void setSlotFrozen (int slot, bool isSolo, bool frozen)
    {
        if (slot < 0 || slot >= 8) return;
        paramsFrozen[isSolo ? 1 : 0][(size_t) slot] = frozen;

        if (onSlotFreezeChanged) onSlotFreezeChanged (slot, isSolo, frozen);
    }

    void adoptPresetParams (bool isSolo, int slot, const SlotParams& p)
    {
        if (slot < 0 || slot >= 8) return;

        // FROZEN: the .ins does not speak over work in progress.
        if (isSlotFrozen (slot, isSolo)) return;

        // SOLO SLOTS ONLY — see pushSlotIntoEditor.  This fires when the engine
        // reports a new instrument on a slot, and for a style slot that is
        // exactly when a style load would otherwise stamp a .ins over settings
        // the set had just restored.  The .ins is the right hand's authority,
        // not the arrangement's.
        if (! isSolo) return;

        soloParams[(size_t) slot] = p;

        const auto& stored = soloParams[(size_t) slot];

        if (onNoteRangeAdopted)   onNoteRangeAdopted (slot, isSolo, stored);
        if (onSweetenerChanged && ! roleIsDrum (isSolo, slot))
            onSweetenerChanged (slot, isSolo, stored.sweet);
        if (onClickParamsChanged) onClickParamsChanged (slot, isSolo,
                                                        stored.clickEnabled,
                                                        stored.clickVolume,
                                                        stored.clickDecayMs);
        if (roleIsDrum (isSolo, slot))
            publishKitFx (slot, isSolo, stored.drumKit.fx);

        if (editorWindow && editorWindow->isVisible()
            && soloMode && selectedSlot == slot)
            editorWindow->setSlotParams (soloParams[(size_t) slot]);
    }

    //==========================================================================
    // PER-KIT FX DEFAULTS — the bus COMPRESSOR, engaged for the kits whose
    // library samples need it.
    //
    // Values are the DrumsPopup's own 0..100 slider positions, run through the
    // same linear map the popup uses (Map::toEngine), so what is set here is
    // exactly what the sliders will read when the tab is opened:
    //
    //     THRESH 26   RATIO 53   ATK 19   REL 24   MAKE 67   WET 100
    //
    // Applied wherever a kit lands on a slot — the SOUNDS kit selector AND the
    // engine mirror, so a kit a STYLE loads is treated the same as one picked
    // by hand.  Any other kit turns the compressor back off, so switching away
    // does not leave it engaged on a kit that never wanted it.
    //
    // Note applySoundCalibration() force-clears every FX stage on load; these
    // defaults are applied on the KIT change that follows, so they win.
    //==========================================================================
    //==========================================================================
    // NO KIT ARRIVES WITH AN EFFECT ALREADY RUNNING.
    //
    // Electronic and TR-808 used to switch their own COMPRESSOR on the moment
    // they loaded, with a hard-coded threshold, ratio, attack, release, makeup
    // and 100% wet.  Nobody asked for it and nothing on screen said it had
    // happened: the toggle read ON, but the user had not pressed it, and the six
    // values behind it were not theirs either.
    //
    // That is exactly the class of hidden gain stage this whole pass exists to
    // remove.  Every drum effect is now OFF until its toggle is pressed, the
    // toggle is the only thing that turns it on, and what the toggle says is
    // what the bus is doing.  A kit that genuinely wants a compressor gets one
    // saved in its SET, where it is visible, editable and per style.
    //
    // The DrumKitFxParams struct already defaults every stage to false, so a
    // fresh kit needs nothing done to it to arrive clean.
    //==========================================================================

    /** DrumsPopup's slider law, duplicated here so the two cannot drift. */
    static float kitSliderToEngine (float slider, float lo, float hi) noexcept
    {
        return lo + juce::jlimit (0.0f, 100.0f, slider) / 100.0f * (hi - lo);
    }

    /** Mirror the ENGINE's actually-loaded drum kit onto a slot's display
        (the big "<kit> kit" label and the DrumsPopup state).  Programmatic —
        fires no callbacks; repaints only when the value changed AND the slot
        is the one currently shown (so the 30 Hz mirror never causes blinking). */
    void setSlotDrumKitName (bool isSolo, int slot, const juce::String& kitName)
    {
        if (slot < 0 || slot >= 8) return;
        auto& p = isSolo ? soloParams[(size_t) slot] : styleParams[(size_t) slot];
        if (p.drumKit.lastLoadedKit == kitName) return;
        p.drumKit.lastLoadedKit = kitName;

        // The kit CHANGED, and its effects are DELIBERATELY left alone.  This
        // used to seed a compressor here for Electronic / TR-808 — the comment
        // that stood in this spot said so outright, "without the user opening
        // anything".  The rack now belongs to the toggles and to the set; a kit
        // swap changes the samples, not the signal chain over them.

        if (soloMode == isSolo && selectedSlot == slot)
        {
            // Update the kit-cell highlight (not just the canvas) so the orange
            // selection tracks engine-driven kit changes, e.g. a style PC swap.
            if (isCurrentSlotDrum())
            {
                refreshInstrDisplay();
                // ...and the PICTURE, which is keyed off the kit that just
                // changed.  Without this the cells would track a style's kit
                // swap while the image stayed on the previous kit.
                reloadInstrImage();
                // ...and the EDITOR.  This is the mirror, so it is also how the
                // editor learns a kit changed underneath it - a style PC
                // composing over a sampled kit has to take the sampled layout
                // away with it, and the title has to stop naming a kit that is
                // no longer loaded.
                pushDrumEditorMode();
            }
            repaint();
        }
    }

    juce::String getSlotDrumKitName (bool isSolo, int slot) const
    {
        if (slot < 0 || slot >= 8) return {};
        const auto& p = isSolo ? soloParams[(size_t) slot] : styleParams[(size_t) slot];
        return p.drumKit.lastLoadedKit;
    }

    //--------------------------------------------------------------------------
    // WHICH KIT, BY IDENTITY RATHER THAN BY NAME
    //
    // The mirror used to watch getChannelDrumKitName alone, and that name is not
    // a reliable witness: the runtime program-change path publishes with
    // syncName false - it is the audio thread and cannot write a juce::String -
    // so a PC-driven kit change leaves the name reading the PREVIOUS kit.  The
    // mirror saw no change, never refreshed, and the selector kept the old
    // highlight or none at all.  Live only, never saved: it is a copy of what
    // the engine is holding, and the engine is the one that knows.
    //--------------------------------------------------------------------------
    int getSlotKitKey (bool isSolo, int slot) const
    {
        if (slot < 0 || slot >= 8) return -1;
        return (isSolo ? soloKitKey : styleKitKey)[(size_t) slot];
    }

    /** The engine published a kit on this slot.  `name` is what to call it -
        empty when the key names nothing, in which case the stored name stands. */
    void setSlotDrumKit (bool isSolo, int slot, int kitKey, const juce::String& name)
    {
        if (slot < 0 || slot >= 8) return;
        (isSolo ? soloKitKey : styleKitKey)[(size_t) slot] = kitKey;

        // setSlotDrumKitName already does the storing AND the refresh, so route
        // through it whenever there is a name rather than duplicating either.
        if (name.isNotEmpty())
        {
            setSlotDrumKitName (isSolo, slot, name);
            return;
        }

        if (soloMode == isSolo && selectedSlot == slot && isCurrentSlotDrum())
        {
            refreshInstrDisplay();
            reloadInstrImage();
            pushDrumEditorMode();
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        if (animBounds.getHeight() > 0)
        {
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.fillRoundedRectangle(animBounds.toFloat(), 6.0f);
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawRoundedRectangle(animBounds.toFloat(), 6.0f, 1.0f);

            const bool drumSlot = isCurrentSlotDrum();
            if (drumSlot)
            {
                const int margin   = 6;
                const int imgAreaW = (int)(animBounds.getWidth() * 0.32f) - margin * 2;
                const int imgAreaX = animBounds.getRight() - imgAreaW - margin;
                const int labelW   = imgAreaX - animBounds.getX() - margin * 2;
                const float fh     = juce::jmin((float)animBounds.getHeight() * 0.44f, 44.0f);
                g.setColour(juce::Colour(0xFFCC6600));
                g.setFont(juce::Font(fh, juce::Font::bold));

                const auto& dk = currentSlotParams().drumKit;
                // Format: "<KitName> kit" when a kit is loaded, otherwise just
                // "DRUMS".  Resolve the stored identifier (which the engine mirror
                // may have written as a registry key like "000") to its display
                // name so the canvas reads "Standard kit", not "000 kit".
                const juce::String disp = displayNameForKit (dk.lastLoadedKit);
                const juce::String label = disp.isEmpty()
                    ? juce::String ("DRUMS")
                    : disp + " kit";

                g.drawFittedText(label,
                                 animBounds.getX() + margin,
                                 animBounds.getY() + (animBounds.getHeight() - (int)(fh * 2.2f)) / 2,
                                 labelW, (int)(fh * 2.4f),
                                 juce::Justification::centred, 2);
            }
            else
            {
                const int currentPatch = soloMode ? soloPatch[selectedSlot]
                                                  : stylePatch[selectedSlot];
                if (currentPatch >= 0 && currentPatch < 128)
                {
                    const int margin   = 6;
                    const int imgAreaW = (int)(animBounds.getWidth() * 0.32f) - margin * 2;
                    const int imgAreaX = animBounds.getRight() - imgAreaW - margin;
                    const int labelW   = imgAreaX - animBounds.getX() - margin * 2;
                    const float fh     = juce::jmin((float)animBounds.getHeight() * 0.44f, 44.0f);
                    g.setColour(juce::Colour(0xFFCC6600));
                    g.setFont(juce::Font(fh, juce::Font::bold));
                    g.drawFittedText(kFullInstrNames[currentPatch],
                                     animBounds.getX() + margin,
                                     animBounds.getY() + (animBounds.getHeight() - (int)(fh * 2.2f)) / 2,
                                     labelW, (int)(fh * 2.4f),
                                     juce::Justification::centred, 2);
                }
            }
        }

        g.setColour(juce::Colours::white.withAlpha(0.10f));
        for (int divY : sectionDividers)
            g.drawHorizontalLine(divY, 4.0f, (float)(getWidth() - 4));
    }

    void resized() override
    {
        const int W = getWidth(), H = getHeight();
        const int padX = 6, usW = W - padX * 2, gap = 2;
        sectionDividers.clear();

        const int modeH   = juce::jmax(28, (int)(H * 0.082f));
        const int slotH   = juce::jmax(28, (int)(H * 0.082f));
        const int timbreH = juce::jmax(36, (int)(H * 0.105f));
        const int instrH  = juce::jmax(58, (int)(H * 0.150f));
        const int pillH   = juce::jmax(20, (int)(H * 0.050f));
        const int secGap  = juce::jmax(6,  (int)(H * 0.022f));

        int y = juce::jmax(4, (int)(H * 0.010f));

        const int modeW = (usW - gap) / 2;
        btnModeSolo .setBounds(padX,               y, modeW,             modeH);
        btnModeStyle.setBounds(padX + modeW + gap, y, usW - modeW - gap, modeH);
        y += modeH + secGap;
        sectionDividers.add(y - secGap / 2);

        slotRowY = y; slotRowH = slotH;
        layoutSlots();
        y += slotH + secGap;
        sectionDividers.add(y - secGap / 2);

        // Timbre row: 16 melodic categories only (the DRUMS button is gone —
        // drum slots are role-fixed).  Hidden entirely for drum slots, which
        // show just the kit selector below.
        // ── BANK SELECTOR: GM / WORLD ────────────────────────────────────────
        //
        // Between the channel editor above and the instrument selector below,
        // which is the order the choice is actually made in: which slot, which
        // library, which sound.  Hidden on a DRUM slot - a kit is neither GM nor
        // World, and offering the choice there would be offering nothing.
        {
            const bool showBank = ! isCurrentSlotDrum();
            const int  bankH    = juce::jmax (18, timbreH - 2);
            // Three across now, not two.  Same band, same height - the third
            // button comes out of the width the other two had.
            const int  bankW    = juce::jmin (140, (usW - 8) / 3);

            btnBankGm      .setVisible (showBank);
            btnBankWorld   .setVisible (showBank);
            btnBankOriental.setVisible (showBank);

            if (showBank)
            {
                btnBankGm      .setBounds (padX,                     y, bankW, bankH);
                btnBankWorld   .setBounds (padX + (bankW + 4),       y, bankW, bankH);
                btnBankOriental.setBounds (padX + (bankW + 4) * 2,   y, bankW, bankH);
                y += bankH + secGap;   // same gap as every other section
            }
        }

        {
            const bool showTimbres = ! isCurrentSlotDrum();
            const int nT = 16;
            const int cellGap = 2;

            // In a PACK only the categories that exist are shown.
            const int visibleCount = isPackBank() ? juce::jmin (nT, packCategoryNames.size()) : nT;

            // ── DIVIDE BY WHAT IS SHOWN, NOT BY WHAT COULD BE ────────────────
            //
            // This used to divide the width by 16 always and then hide the
            // buttons past visibleCount, so a pack's categories occupied
            // visibleCount/16 of the row and stopped dead: ORIENTAL's six sat in
            // the left 37%, WORLD's ten in the left 62%, with the rest of the
            // canvas empty.  Sixteen is the count of GM FAMILIES, which a pack
            // does not have - so it was never the right divisor there.
            //
            // Widening also makes the labels legible: a category is a word
            // ("accordion", "woodwind"), not a four-letter GM family name, and
            // a 1/16th cell truncated most of them.
            const int laidOut = juce::jmax (1, visibleCount);
            const int cellW   = (usW - (laidOut - 1) * cellGap) / laidOut;

            for (int i = 0; i < kNumTimbres; ++i)
            {
                if (i < nT)
                {
                    // Last visible cell absorbs the integer-division remainder so
                    // the row ends flush with the right edge.
                    const int bw = (i == laidOut - 1) ? (usW - i * (cellW + cellGap))
                                                      : cellW;
                    timbreButtons[i].setBounds(padX + i*(cellW+cellGap), y, bw, timbreH);
                    timbreButtons[i].setVisible(showTimbres && i < visibleCount);
                }
                else
                {
                    timbreButtons[i].setVisible(false);     // DRUMS button removed
                    timbreButtons[i].setBounds(0, 0, 0, 0);
                }
            }
            // THE HIDDEN ROW MUST NOT KEEP ITS HEIGHT.
            //
            // On a drum slot the timbre row is invisible, but y advanced past it
            // anyway - so the kit grid started a full row lower than the slot
            // buttons above it, with nothing in between.  That empty band was
            // the whole "chaos" impression: the grid looked detached from the
            // selector it belongs to.
            if (showTimbres)
            {
                y += timbreH + secGap;
                sectionDividers.add(y - secGap / 2);
            }
            else
            {
                y += secGap;
            }
        }

        // Instrument row: 8 wide cells for melodic timbres, 16 half-width
        // cells when DRUMS is active.  The cell COUNT changes with the
        // selected timbre, hence the explicit handling here AND in selectTimbre.
        // KIT CELLS MATCH THE SLOT BUTTONS.
        //
        // 26 kits in three rows is a lot of screen; at the melodic cell height
        // (58 px) it dominates the tab.  At slotH they read as what they are -
        // another selector row in the same family as DRUMS / PERC / BASS above -
        // and all three rows fit where one melodic row used to.
        instrRowY = y;
        instrRowH = isCurrentSlotDrum() ? slotH : instrH;
        layoutInstrCells();

        // A DRUM SLOT TAKES THREE ROWS, not one: the GM kits on top and the
        // sampled kits on the two below.  The extra height comes out of the
        // canvas beneath, which is the animation strip - it has room to give and
        // nothing below it depends on a fixed top.
        const int cellRows = isCurrentSlotDrum() ? 3 : 1;
        y += instrRowH * cellRows + 2 * (cellRows - 1) + secGap;

        // Reference-voice pills: one per band slot, aligned to the slot columns.
        // The drum/perc view has no GM reference voices (you pick a kit), so the
        // whole row is dropped there and its space handed back to the canvas.
        if (! isCurrentSlotDrum())
        {
            pillRowY = y; pillRowH = pillH;
            // THE REF PILLS ARE RETIRED.
            //
            // They existed as the only route to a >= 200 reference sound, back
            // when nothing else could reach that range.  The WORLD bank browses
            // exactly the same flags with names, pages and the slot's own
            // selector, so a second parallel picker is one mechanism too many -
            // and the one that had no labels.
            for (int i = 0; i < 8; ++i)
            {
                refPills[i].setVisible (false);
                refPills[i].setBounds (0, 0, 0, 0);
            }

            // ── THE PAGE CONTROLS TAKE THE PILLS' PLACE ──────────────────────
            // Same band, and it was already empty.  Left-aligned under the first
            // cells rather than centred, so the eye goes cells -> pager without
            // crossing the whole width.
            {
                const int pw = juce::jmin (34, usW / 12);
                const int ph = juce::jmax (16, pillH - 4);
                const int py = y + (pillH - ph) / 2;
                const int capW = juce::jmax (40, pw + 14);

                // Reading order left to right: PAGE  -  +  3 / 7.  The caption
                // sits before the buttons because it names what they move.
                int px = padX;
                pageCaption.setBounds (px, py, capW, ph);          px += capW + 6;
                btnPagePrev.setBounds (px, py, pw,   ph);          px += pw + 4;
                btnPageNext.setBounds (px, py, pw,   ph);          px += pw + 8;
                pageLabel  .setBounds (px, py, pw * 3, ph);
                refreshPageControls();
            }

            y += pillH + secGap;
            sectionDividers.add(y - secGap / 2);
        }
        else
        {
            pillRowH = 0;
            for (int i = 0; i < 8; ++i)
            {
                refPills[i].setVisible(false);
                refPills[i].setBounds(0, 0, 0, 0);
            }
            // A kit is not paged, and this whole band is gone on a drum slot.
            btnPagePrev.setVisible (false);
            btnPageNext.setVisible (false);
            pageCaption.setVisible (false);
            pageLabel  .setVisible (false);
        }

        const int animH = H - y - 4;
        if (animH > 20)
        {
            animBounds = { padX, y, usW, animH };
            const int margin   = 6;
            const int imgAreaW = (int)(usW * 0.32f) - margin * 2;
            const int imgAreaX = animBounds.getRight() - imgAreaW - margin;
            const int animW    = imgAreaX - padX;

            spaceAnim   .setBounds(padX, y, animW, animH);
            instrDisplay.setBounds(imgAreaX, y + margin, imgAreaW, animH - margin * 2);
            spaceAnim   .setVisible(true);
            instrDisplay.setVisible(true);
            spaceAnim.setAnimating(true);

            btnEdit.setBounds(padX + 2, y + 2, 44, 20);
            btnEdit.setVisible(true);
            btnEdit.toFront(false);

            // Directly under EDIT, same left edge, wider only because the label
            // needs it.  Style slots only - see the constructor.
            // Wide enough for the whole label, and never wider than the panel it
            // sits in - the canvas narrows with the window and a fixed width
            // would run under the instrument picture.
            const int ignW = juce::jlimit(56, 168, animW - 8);
            btnIgnorePc.setBounds(padX + 2, y + 2 + 20 + 3, ignW, 18);
            btnIgnorePc.setVisible(! soloMode);
            btnIgnorePc.toFront(false);
            refreshIgnorePcButton();
        }
        else
        {
            animBounds = {};
            spaceAnim.setVisible(false); instrDisplay.setVisible(false);
            btnEdit.setVisible(false);
            btnIgnorePc.setVisible(false);
        }

        refreshModeButtons(); refreshSlotButtons(); refreshTimbreButtons(); refreshInstrDisplay();
    }

    void visibilityChanged() override { spaceAnim.setAnimating(isVisible()); }

private:
    // ── GM data ───────────────────────────────────────────────────────────────
    static constexpr int kNumTimbres  = 17;
    static constexpr int kDrumsTimbre = 16;  // index of the DRUMS button

    // When DRUMS is the active timbre the row below shows 9 half-width
    // cells instead of the 8 wide cells used for melodic timbres.  The cell
    // labels come from kDrumKitNames and are ordered to match the engine's
    // default PC mapping (see SamplePlayerEngine::populateDefaultDrumKitPCMap):
    //   0..8 → the 9 sampled kits at canonical PCs (0/8/16/24/25/32/40/48/56)
    //
    // NOTE: this count MUST match the engine's nine-kit reality.  Older
    // builds carried 16 names; the extra 7 (Pop/Rock/House/TR-909/Hip-Hop/
    // Arabic/Latin) had no .frb data and registryKeyForKitName had no entry
    // for them, so clicking them silently did nothing.  Keeping the visible
    // list aligned to the engine guarantees PC and the selector are the
    // same code path.
    static constexpr int kMaxInstrCells = 9;
    static constexpr const char* kDrumKitNames [kMaxInstrCells] = {
        "Standard", "Room",  "Power",  "Electronic",
        "TR-808",   "Jazz",  "Brush",  "Orchestra",
        "SFX"
    };

    static constexpr const char* kTimbreNames[kNumTimbres] = {
        "PIANO","CHROM.","ORGAN","GUITAR",
        "BASS","STRINGS","ENSEMB.","BRASS",
        "REED","PIPE","SYN.LD","SYN.PAD",
        "SYN.FX","ETHNIC","PERCUS.","SFX",
        "DRUMS"
    };

    static constexpr const char* kStyleRoleNames[8] = {
        "DRUMS", "PERC", "BASS", "CHORD 1", "CHORD 2", "PAD", "LEAD 1", "LEAD 2"
    };

    // Only 16 melodic timbres here; DRUMS labels are pulled at runtime
    // from the DrumKitRegistry.
    static constexpr const char* kInstrNames[16][8] = {
        { "Ac. Grand","Bright Ac.","Elec. Grand","Honky-tonk","El. Piano 1","El. Piano 2","Harpsichord","Clavinet" },
        { "Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer" },
        { "Drawbar Org.","Percussive","Rock Organ","Church Org.","Reed Organ","Accordion","Harmonica","Bandoneon" },
        { "Ac. Nylon","Ac. Steel","El. Jazz","El. Clean","El. Muted","El. Overdrive","El. Distortion","El. Harmonics" },
        { "Ac. Bass","El. Finger","El. Picked","Fretless","Slap Bass 1","Slap Bass 2","Syn. Bass 1","Syn. Bass 2" },
        { "Violin","Viola","Cello","Contrabass","Tremolo Str.","Pizzicato","Orch. Harp","Timpani" },
        { "Str. Ens. 1","Str. Ens. 2","Syn. Str. 1","Syn. Str. 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit" },
        { "Trumpet","Trombone","Tuba","Muted Trp.","French Horn","Brass Sect.","Syn. Brass 1","Syn. Brass 2" },
        { "Soprano Sax","Alto Sax","Tenor Sax","Bari. Sax","Oboe","English Hn.","Bassoon","Clarinet" },
        { "Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina" },
        { "Lead 1","Lead 2","Lead 3","Lead 4","Lead 5","Lead 6","Lead 7","Lead 8" },
        { "Pad 2 Warm","Pad 3 Poly","Pad 4 Choir","Pad 5 Bowed","Pad 6 Metal","Pad 7 Halo","Pad 8 Sweep","FX 1 Rain" },
        { "FX 2 Soundtrack","FX 3 Crystal","FX 4 Atmosph.","FX 5 Brightness","FX 6 Goblins","FX 7 Echoes","FX 8 Sci-fi","FX 9 New Age" },
        { "Sitar","Banjo","Shamisen","Koto","Kalimba","Bag Pipe","Fiddle","Shanai" },
        { "Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Rev. Cymbal" },
        { "Fret Noise","Breath Noise","Seashore","Bird Tweet","Phone Ring","Helicopter","Applause","Gunshot" }
    };

    static constexpr const char* kFullInstrNames[128] = {
        "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano",
        "Electric Piano 1","Electric Piano 2","Harpsichord","Clavinet",
        "Celesta","Glockenspiel","Music Box","Vibraphone",
        "Marimba","Xylophone","Tubular Bells","Dulcimer",
        "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ",
        "Reed Organ","Accordion","Harmonica","Bandoneon",
        "Acoustic Guitar (nylon)","Acoustic Guitar (steel)","Electric Guitar (jazz)","Electric Guitar (clean)",
        "Electric Guitar (muted)","Electric Guitar (overdrive)","Electric Guitar (distortion)","Electric Guitar (harmonics)",
        "Acoustic Bass","Electric Bass (finger)","Electric Bass (picked)","Electric Bass (fretless)",
        "Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
        "Violin","Viola","Cello","Contrabass",
        "Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
        "String Ensemble 1","String Ensemble 2","Synth Strings 1","Synth Strings 2",
        "Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
        "Trumpet","Trombone","Tuba","Muted Trumpet",
        "French Horn","Brass Section","Synth Brass 1","Synth Brass 2",
        "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax",
        "Oboe","English Horn","Bassoon","Clarinet",
        "Piccolo","Flute","Recorder","Pan Flute",
        "Blown Bottle","Shakuhachi","Whistle","Ocarina",
        "Lead 1","Lead 2","Lead 3","Lead 4","Lead 5","Lead 6","Lead 7","Lead 8",
        "Pad 2 (warm)","Pad 3 (polysynth)","Pad 4 (choir)","Pad 5 (bowed)",
        "Pad 6 (metallic)","Pad 7 (halo)","Pad 8 (sweep)","FX 1 (rain)",
        "FX 2 (soundtrack)","FX 3 (crystal)","FX 4 (atmosphere)","FX 5 (brightness)",
        "FX 6 (goblins)","FX 7 (echoes)","FX 8 (sci-fi)","FX 9 (new age)",
        "Sitar","Banjo","Shamisen","Koto","Kalimba","Bag Pipe","Fiddle","Shanai",
        "Tinkle Bell","Agogo","Steel Drums","Woodblock",
        "Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
        "Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet",
        "Telephone Ring","Helicopter","Applause","Gunshot"
    };

    // ── Button classes ────────────────────────────────────────────────────────
    class ModeButton : public juce::TextButton {
    public:
        void setActive(bool a) { active = a; repaint(); }
        void paintButton(juce::Graphics& g, bool isOver, bool isDown) override {
            auto b = getLocalBounds().toFloat();
            g.setColour(active ? juce::Colour(0xFFCC6600) : isDown ? juce::Colour(0xFF444444) : isOver ? juce::Colour(0xFF333333) : juce::Colour(0xFF222222));
            g.fillRoundedRectangle(b.reduced(0.5f), 5.0f);
            g.setColour(active ? juce::Colour(0xFFCC6600).brighter(0.3f) : juce::Colour(0xFF555555));
            g.drawRoundedRectangle(b.reduced(0.5f), 5.0f, 1.0f);
            g.setColour(active ? juce::Colours::black : juce::Colours::white);
            g.setFont(juce::Font(juce::jmin(b.getHeight() * 0.40f, 13.0f), juce::Font::bold));
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    private: bool active = false;
    };

    class SlotButton : public juce::TextButton {
    public:
        void setActive(bool a) { active = a; repaint(); }
        void paintButton(juce::Graphics& g, bool isOver, bool isDown) override {
            auto b = getLocalBounds().toFloat();
            g.setColour(active ? juce::Colour(0xFFCC6600) : isDown ? juce::Colour(0xFF444444) : isOver ? juce::Colour(0xFF2E2E2E) : juce::Colour(0xFF232323));
            g.fillRoundedRectangle(b.reduced(0.5f), 4.0f);
            g.setColour(juce::Colour(0xFF555555)); g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
            g.setColour(active ? juce::Colours::black : juce::Colours::white);
            g.setFont(juce::Font(juce::jmin(b.getHeight() * 0.38f, 11.0f), juce::Font::bold));
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    private: bool active = false;
    };

    class TimbreButton : public juce::TextButton {
    public:
        void setActive(bool a) { active = a; repaint(); }
        void paintButton(juce::Graphics& g, bool isOver, bool isDown) override {
            auto b = getLocalBounds().toFloat();
            g.setColour(active ? juce::Colour(0xFFCC6600) : isDown ? juce::Colour(0xFF444444) : isOver ? juce::Colour(0xFF2E2E2E) : juce::Colour(0xFF1E1E1E));
            g.fillRoundedRectangle(b.reduced(0.5f), 4.0f);
            g.setColour(active ? juce::Colour(0xFF885500) : juce::Colour(0xFF444444));
            g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
            g.setColour(active ? juce::Colours::black : juce::Colours::white);
            g.setFont(juce::Font(juce::jmin(b.getHeight() * 0.40f, 10.0f), juce::Font::bold));
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    private: bool active = false;
    };

    class InstrButton : public juce::TextButton {
    public:
        void setActive(bool a) { active = a; repaint(); }
        void setProgramNumber(int n) { programNumber = n; repaint(); }   // -1 = hide
        void paintButton(juce::Graphics& g, bool isOver, bool isDown) override {
            auto b = getLocalBounds().toFloat();
            g.setColour(active ? juce::Colour(0xFFCC6600) : isDown ? juce::Colour(0xFF444444) : isOver ? juce::Colour(0xFF2E2E2E) : juce::Colour(0xFF202020));
            g.fillRoundedRectangle(b.reduced(0.5f), 4.0f);
            g.setColour(juce::Colour(0xFF555555)); g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
            g.setColour(active ? juce::Colours::black : juce::Colours::white);
            // RATIO DOUBLED, CAP UNCHANGED.
            //
            // A drum slot lays these cells out at slotH (three rows of kits in
            // the height one melodic row used to take), and 0.18 of that came
            // out around 5 px - half what the DRUMS / PERC / BASS buttons
            // directly above them use, which is the family they are supposed to
            // read as.  0.36 doubles the kit cells and nothing else: a melodic
            // cell is 58 px tall, so it was already pinned at the 10 px cap and
            // stays there.  Font only - no bounds, no layout, nothing resizes.
            g.setFont(juce::Font(juce::jmin(b.getHeight() * 0.36f, 10.0f), juce::Font::bold));
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
            if (programNumber >= 0) {
                g.setColour(active ? juce::Colours::white.withAlpha(0.85f) : juce::Colour(0xFFCC8844));
                g.setFont(juce::Font(juce::jmin(b.getHeight() * 0.16f, 9.0f), juce::Font::bold));
                g.drawText(juce::String(programNumber), getLocalBounds().reduced(4, 2),
                           juce::Justification::topRight);
            }
        }
    private: bool active = false; int programNumber = -1;
    };

    // ── Components ────────────────────────────────────────────────────────────
    ModeButton              btnModeSolo, btnModeStyle;
    ModeButton              btnBankGm, btnBankWorld, btnBankOriental;
    /** Page down / page up within the selected category.  Small, and parked in
        the row the retired reference pills used to occupy - that space was
        already reserved and sat empty. */
    juce::TextButton        btnPagePrev, btnPageNext;
    /** One yellow for the caption, the counter and both button faces - three
        controls that are one thing, so they share one colour.

        inline static, not plain static const: this class lives entirely in a
        header, and a plain static const member would need an out-of-line
        definition in some .cpp to link at all. */
    inline static const juce::Colour kPageYellow { juce::Colour (0xFFE8C33A) };

    /** "PAGE" caption on the left, then the counter.  Two labels rather than one
        string so the caption can stay put while the count changes under it. */
    juce::Label             pageCaption, pageLabel;

    /** Two extra rows of kit cells, shown only on a drum slot. */
    /** NINE PER ROW - the same nine columns the GM row uses.

        26 kits total: 9 GM on top, then 9 and 8 sampled below.  Sharing one
        column grid across all three rows is what makes it read as a matrix
        rather than three unrelated strips, and it costs nothing: the last row
        simply leaves its final cell empty. */
    static constexpr int kFullKitsPerRow  = 9;
    static constexpr int kMaxFullKitCells = kFullKitsPerRow * 2;
    InstrButton             fullKitButtons[kMaxFullKitCells];
    std::vector<std::tuple<int,int,int,juce::String>> fullKits;

    /** WHICH PACK IS BEING BROWSED.  0 = GM, 1 = WORLD, 2 = ORIENTAL, matching
        SamplePlayerEngine::SoundPack.
        
        Was a bool `worldBank`, which could only ever answer "GM or not" - and
        with two optional packs that is no longer the question.  `worldBank` is
        kept as a derived helper below so the many `if (worldBank)` sites that
        mean "not the GM grid" still read correctly. */
    int                     selectedPack = 0;
    bool isPackBank() const { return selectedPack != 0; }

    /** Categories of the current pack, and the page within the current one. */
    juce::StringArray       packCategoryNames;
    int                     packPage = 0;
    /** Cached WORLD list, refreshed on entry so a library rescan is picked up. */
    std::vector<std::pair<int, juce::String>> worldInstruments;
    SlotButton              slotButtons[8];
    TimbreButton            timbreButtons[kNumTimbres];
    InstrButton             instrButtons[kMaxInstrCells];

    // Live mirror of the engine's kit identity per slot - see getSlotKitKey.
    std::array<int, 8>      soloKitKey  { -1,-1,-1,-1,-1,-1,-1,-1 };
    std::array<int, 8>      styleKitKey { -1,-1,-1,-1,-1,-1,-1,-1 };
    SpaceAnimationComponent spaceAnim;
    InstrImageDisplay       instrDisplay;
    juce::TextButton        btnEdit;
    juce::TextButton        btnIgnorePc;

    std::unique_ptr<InstrEditorWindow> editorWindow;
    std::unique_ptr<DrumsPopup>        drumsPopup;

    bool soloMode     = true;
    int  selectedSlot = 0;
    int  selectedTimbre = 0;

    std::array<int,  8>  soloPatch;
    std::array<int,  8>  stylePatch;
    std::array<bool, 8>  soloIsDrumSlot;
    std::array<bool, 8>  styleIsDrumSlot;

    // Per-slot reference-voice override (-1 = none → slot uses its GM voice).
    std::array<int,  8>  soloRef;
    std::array<int,  8>  styleRef;
    ReferencePill        refPills[8];
    int pillRowY = 0, pillRowH = 0;

    std::array<SlotParams, 8>  soloParams;
    std::array<SlotParams, 8>  styleParams;

    //--------------------------------------------------------------------------
    // THE SECOND CHAIN, PER SLOT.
    //
    // Sixteen independent SFZ chains, exactly as asked for - but stored rather
    // than built, because only ONE source on a channel sounds at a time.  Two
    // sets of filters and EQs would be two sets idling; two sets of SETTINGS,
    // with the awake one committed, is the same thing without the silicon.
    //
    // So editing the filter with the SFZ awake shapes the SFZ, switching back
    // restores the blob's own filter untouched, and neither ever overwrites the
    // other.  Both travel in the set.
    //--------------------------------------------------------------------------
    std::array<SlotParams, 8>  sfzSoloParams;
    std::array<SlotParams, 8>  sfzStyleParams;

    int slotRowY = 0, slotRowH = 0;
    int instrRowY = 0, instrRowH = 0;
    juce::Rectangle<int> animBounds;
    juce::Array<int>     sectionDividers;

    // ── Drum-slot helpers ─────────────────────────────────────────────────────
    // Drum-ness is fixed by ROLE: in STYLE mode the first two slots are DRUMS
    // and PERC (drum channels by definition); every SOLO slot is melodic.
    static bool roleIsDrum (bool solo, int slot)
    {
        return (! solo) && (slot == 0 || slot == 1);
    }

    bool isCurrentSlotDrum() const { return roleIsDrum (soloMode, selectedSlot); }

    // Keep the persisted per-slot drum flags in sync with the fixed roles so
    // save/load and the applyState re-push loop route drum kits correctly.
    void syncDrumFlagsFromRole()
    {
        for (int i = 0; i < 8; ++i)
        {
            soloIsDrumSlot [(size_t) i] = roleIsDrum (true,  i);   // always false
            styleIsDrumSlot[(size_t) i] = roleIsDrum (false, i);   // slots 0,1
        }
    }

    /** Is the SFZ the source currently sounding on this slot? */
    bool sfzAwake (int slot, bool isSolo) const
    {
        if (! onGetSfzState || slot < 0 || slot >= 8) return false;
        return onGetSfzState (slot, isSolo).second;
    }

    /** The parameter set that governs whichever source is awake.

        Every read and write in the editor goes through here, so the whole tab
        follows the switch without a single call site having to know about it. */
    SlotParams& paramsFor (int slot, bool isSolo)
    {
        const int i = juce::jlimit (0, 7, slot);
        if (sfzAwake (i, isSolo))
            return isSolo ? sfzSoloParams[(size_t) i] : sfzStyleParams[(size_t) i];
        return isSolo ? soloParams[(size_t) i] : styleParams[(size_t) i];
    }

    const SlotParams& paramsFor (int slot, bool isSolo) const
    {
        const int i = juce::jlimit (0, 7, slot);
        if (sfzAwake (i, isSolo))
            return isSolo ? sfzSoloParams[(size_t) i] : sfzStyleParams[(size_t) i];
        return isSolo ? soloParams[(size_t) i] : styleParams[(size_t) i];
    }

    SlotParams&       currentSlotParams()       { return paramsFor (selectedSlot, soloMode); }
    const SlotParams& currentSlotParams() const { return paramsFor (selectedSlot, soloMode); }

    // ── Image loading ─────────────────────────────────────────────────────────
    static int imageFileNumForPatch(int p)
    {
        if (p == 53)               return 52;
        if (p >= 81 && p <= 87)    return 80;
        if (p >= 89 && p <= 94)    return 88;
        if (p >= 104 && p <= 119)  return p;     // Ethnic + Percussive have their own art
        if (p >= 96 && p <= 127)   return 95;     // others in this range share a generic image
        return p;
    }

    /** The picture for a sound-pack instrument.

        One image per PACK, shared by every sound in it: there are 153 world and
        88 oriental sounds and no artwork per sound, so the pack is the level at
        which a picture means anything.

        SP_ prefixed for the same reason the kits are D_ prefixed - BinaryData is
        a flat symbol space, and SP_WORLD.png mangles to SP_WORLD_png where it
        cannot collide with _000_png or D016_png. */
    static juce::Image loadPackImage (int pack)
    {
        const char* name = (pack == 1) ? "SP_WORLD_png"
                         : (pack == 2) ? "SP_ORIENTAL_png"
                                       : nullptr;
        if (name == nullptr) return {};

        int sz = 0;
        const char* data = BinaryData::getNamedResource (name, sz);
        if (! data || sz == 0) return {};    // pack image not built in
        return juce::ImageFileFormat::loadFrom (data, (size_t) sz);
    }

    static juce::Image loadFromBinaryData(int patch)
    {
        if (patch < 0 || patch > 127) return {};
        const int n = imageFileNumForPatch(patch);
        const juce::String name = juce::String::formatted("_%03d_png", n);
        int sz = 0;
        const char* data = BinaryData::getNamedResource(name.toRawUTF8(), sz);
        if (!data || sz == 0) return {};
        return juce::ImageFileFormat::loadFrom(data, (size_t)sz);
    }

    /** The picture for a sampled drum kit.

        Named D<registry key>.png — D016.png for Power — and NOT 016.png, because
        BinaryData is a flat symbol space and the instrument images already own
        016.png (which mangles to _016_png).  The D prefix keeps the two families
        apart; D016.png mangles to D016_png.

        `kit` may arrive as a display name ("Power") or as the registry key
        ("016") — the engine mirror writes one and the selector the other — so it
        goes through registryKeyForKitName, which normalises both. */
    static juce::Image loadKitFromBinaryData(const juce::String& kit)
    {
        if (kit.isEmpty()) return {};

        // registryKeyForKitName passes an unknown name through lower-cased, so a
        // custom kit yields something that is not a key — getNamedResource then
        // returns nothing and the display clears, which is the right answer.
        const juce::String key = registryKeyForKitName(kit);
        if (key.isEmpty()) return {};

        const juce::String name = "D" + key + "_png";
        int sz = 0;
        const char* data = BinaryData::getNamedResource(name.toRawUTF8(), sz);
        if (!data || sz == 0) return {};      // a custom kit with no picture
        return juce::ImageFileFormat::loadFrom(data, (size_t)sz);
    }

    /** The picture every SAMPLED (unique) kit shares: DUNIQUE.png.

        One image for all of them, and not one per kit, because the sampled kits
        are defined by what is on disk - drop in a folder and it works on next
        scan, no table to edit.  A per-kit picture would put a CMake edit and a
        rebuild in front of that, which is exactly the friction the folder
        naming was designed to remove.

        The D prefix is doing the same job it does for D016.png: BinaryData is a
        flat symbol space, so DUNIQUE.png mangles to DUNIQUE_png and cannot
        collide with the numbered instrument art. */
    static juce::Image loadUniqueKitImage()
    {
        int sz = 0;
        const char* data = BinaryData::getNamedResource("DUNIQUE_png", sz);
        if (!data || sz == 0) return {};      // asset not embedded yet
        return juce::ImageFileFormat::loadFrom(data, (size_t)sz);
    }

    void reloadInstrImage()
    {
        // DRUM SLOTS get their kit's picture.  This used to clear the display
        // and return — the space was reserved and correctly sized, there was
        // simply nothing to put in it.  An unrecognised or custom kit still
        // clears, because loadKitFromBinaryData returns an invalid image and
        // InstrImageDisplay::paint draws nothing for one.
        if (isCurrentSlotDrum())
        {
            // A SAMPLED KIT IS ASKED ABOUT FIRST, and asked of the ENGINE.
            //
            // lastLoadedKit carries a sampled kit's readable label ("Arabic
            // Kit"), which registryKeyForKitName cannot turn into a registry
            // key - so the composed path below answers with an invalid image and
            // the panel goes blank on exactly the kits that are most worth
            // showing.  onGetFullKitOnSlot reads the channel the engine really
            // loaded, so this tracks a style's kit swap as well as a click in
            // the grid.
            const bool uniqueKit = onGetFullKitOnSlot
                                && onGetFullKitOnSlot(selectedSlot, soloMode).isNotEmpty();

            instrDisplay.set(uniqueKit
                                 ? loadUniqueKitImage()
                                 : loadKitFromBinaryData(
                                       currentSlotParams().drumKit.lastLoadedKit));
            repaint();
            return;
        }
        const int p = soloMode ? soloPatch[selectedSlot] : stylePatch[selectedSlot];

        // ── PACK SOUNDS GET THEIR PACK'S PICTURE ─────────────────────────────
        //
        // loadFromBinaryData answers only for 0..127, so every WORLD and
        // ORIENTAL sound used to leave the panel blank - the space was reserved
        // and correctly sized, there was simply nothing in it.
        //
        // The pack is asked of the ENGINE, not inferred from the flag's range:
        // a sound's pack is decided by the folder it was found in, so a number
        // range would be a second, silently divergent answer to a question that
        // already has one.
        //
        // GM is checked first because it is the common case and because an
        // uninstalled pack answers 0 - which then falls through to the GM
        // lookup and correctly draws nothing for an out-of-range flag.
        const int pack = (p > 127 && onGetInstrumentPack) ? onGetInstrumentPack (p) : 0;

        instrDisplay.set (pack != 0 ? loadPackImage (pack)
                                    : loadFromBinaryData (p));
        repaint();
    }

    // ── Editor popup helpers ──────────────────────────────────────────────────
    void toggleEditorWindow()
    {
        if (isCurrentSlotDrum()) { toggleDrumsPopup(); return; }
        toggleMelodicEditorWindow();
    }

    void toggleMelodicEditorWindow()
    {
        if (drumsPopup && drumsPopup->isVisible()) drumsPopup->setVisible(false);

        if (editorWindow && editorWindow->isVisible())
        {
            editorWindow->setVisible(false);
            btnEdit.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF222222));
            return;
        }

        if (!editorWindow)
        {
            editorWindow = std::make_unique<InstrEditorWindow>();
            editorWindow->onSfzLoadRequested = [this]
            {
                if (onSfzLoadRequested) onSfzLoadRequested (selectedSlot, soloMode);
            };
            editorWindow->onSfzToggled = [this] (bool on)
            {
                // NO refreshSfzState here: the window already shows the state the
                // user just chose, and reading the engine back before a file is
                // picked would report "nothing loaded" and undo the click.
                if (onSfzToggled) onSfzToggled (selectedSlot, soloMode, on);
                refreshInstrDisplay();
            };

            editorWindow->onParamsChanged = [this](const SlotParams& p) { saveCurrentSlotParams(p); };
            editorWindow->onBaseUnityRequested = [this] { showBaseUnityDialog(); };

            editorWindow->onFreezeToggled = [this] (bool f)
            {
                // The flag belongs to the SLOT SELECTED WHEN THE BUTTON WAS
                // PRESSED, which is the one the window is showing - so it is
                // read here rather than captured, and a slot change reseeds the
                // button from pushSlotIntoEditor instead of carrying the last
                // slot's state across.
                setSlotFrozen (selectedSlot, soloMode, f);
            };
            editorWindow->onSaveAsDefault = [this](const SlotParams& p)
            {
                // Persist the slot's edited params first, then ask the host to
                // write the .ins for this slot's currently-loaded instrument.
                saveCurrentSlotParams(p);
                if (onSaveAsDefault) onSaveAsDefault(selectedSlot, soloMode, p);
            };
            editorWindow->onClosed = [this]
            {
                btnEdit.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF222222));
            };
            editorWindow->onLoadBlobRequested = [this](const juce::File& f, const juce::String& code) -> bool
            {
                const bool ok = onLoadBlobRequested ? onLoadBlobRequested(f, code) : false;
                if (ok && editorWindow && onGetBlobPresetNames)
                    editorWindow->setPresetList(onGetBlobPresetNames());
                return ok;
            };
            editorWindow->onPresetSelected = [this](int idx, const juce::String& /*name*/)
            {
                if (soloMode && onSoloPresetSelected)
                    onSoloPresetSelected(selectedSlot, idx);
            };
            editorWindow->onClickFileChosen = [this](const juce::File& f)
            {
                if (onClickFileChosen) onClickFileChosen(selectedSlot, soloMode, f);
            };

            // Piano strip.  selectedSlot / soloMode are read at CALL time, not
            // captured, so the strip always follows whatever slot the window is
            // currently editing.
            editorWindow->onKeyboardNoteOn = [this](int note, int vel)
            {
                if (onEditorNoteOn) onEditorNoteOn(selectedSlot, soloMode, note, vel);
            };
            editorWindow->onKeyboardNoteOff = [this](int note)
            {
                if (onEditorNoteOff) onEditorNoteOff(selectedSlot, soloMode, note);
            };
            editorWindow->onQuerySoundingNotes = [this](uint32_t* mask)
            {
                if (onQuerySoundingNotes)
                    onQuerySoundingNotes(selectedSlot, soloMode, mask);
            };
        }

        editorWindow->setSoloMode(soloMode);

        pushSlotIntoEditor();
        if (editorWindow && onGetBlobPresetNames)
            editorWindow->setPresetList(onGetBlobPresetNames());

        editorWindow->openCentredOver(getTopLevelComponent(), 0.8f);
        btnEdit.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFCC6600));
    }

    void toggleDrumsPopup()
    {
        if (editorWindow && editorWindow->isVisible()) editorWindow->setVisible(false);

        if (drumsPopup && drumsPopup->isVisible())
        {
            drumsPopup->setVisible(false);
            btnEdit.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF222222));
            return;
        }

        if (! drumsPopup)
        {
            drumsPopup = std::make_unique<DrumsPopup>();
            drumsPopup->onClosed = [this]
            {
                btnEdit.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF222222));
            };
            // WHICH EDITOR IT IS IS *NOT* DECIDED HERE ANY MORE.
            //
            // It used to be: one read of onGetFullKitOnSlot, inside this
            // construct-once block.  The popup is then reused for every EDIT
            // press on every slot for the life of the plugin, so whichever kind
            // of kit was on the slot the first time you opened it decided the
            // editor for all of them - and since a drum slot starts on a
            // composed kit, the sampled-kit editor effectively never ran.
            // pushDrumEditorMode() now does it on every open instead.
            drumsPopup->onGetDrumKitRegistry = [this]() -> const Betel::DrumKitRegistry*
            {
                return onGetDrumKitRegistry ? onGetDrumKitRegistry() : nullptr;
            };

            drumsPopup->onLoadKit = [this](const juce::String& kitName)
            {
                loadDrumKitIntoCurrentSlot (kitName);
                // The DRUMS popup is the other way to change kit, so the tab's
                // cells and picture have to follow it too — otherwise picking a
                // kit in the popup leaves the tab behind it showing the old one.
                refreshInstrDisplay();
                reloadInstrImage();
            };
            drumsPopup->onKeyElementChanged = [this]
                (int midiKey, const juce::String& kitName, uint8_t roleId)
            {
                if (midiKey < 0 || midiKey >= 128) return;
                auto& dk = currentSlotParams().drumKit;
                auto& entry = dk.keys[(size_t) midiKey];
                entry.sourceKit    = kitName;
                entry.sourceRoleId = roleId;
                // Full reapply so the engine reloads that one key's sample.
                publishDrumKit (selectedSlot, soloMode, dk);
            };
            drumsPopup->onKeyParamsChanged = [this]
                (int midiKey, const DrumElementParams& p)
            {
                if (midiKey < 0 || midiKey >= 128) return;
                auto& dk = currentSlotParams().drumKit;
                dk.keys[(size_t) midiKey] = p;
                // All per-key edits (including the notes 35/36 KICK MIX amount /
                // variant / on-off) are RT-safe atomic writes: the engine drives
                // the kick-mix blend from live per-key atomics with no
                // re-compose, so this stays on the fast path for every key.
                if (onDrumKeyParamsChanged)
                    onDrumKeyParamsChanged (selectedSlot, soloMode, midiKey, p);
            };
            // The drum window gets the same SFZ pair, routed to the same host
            // handlers - a drum slot is just another channel to the engine.
            drumsPopup->onSfzLoadRequested = [this]
            { if (onSfzLoadRequested) onSfzLoadRequested (selectedSlot, soloMode); };
            drumsPopup->onSfzToggled = [this] (bool on)
            {
                if (onSfzToggled) onSfzToggled (selectedSlot, soloMode, on);
            };

            drumsPopup->onBaseUnityRequested = [this] { showBaseUnityDialog(); };

            drumsPopup->onKitGainChanged = [this] (float gainPercent)
            {
                // Calibration is a SlotParams field, so the ordinary commit
                // carries it to the engine and the ordinary .drm / set save
                // carries it to disk — no separate path to keep in step.
                currentSlotParams().gainPercent = gainPercent;
                if (onSlotGainChanged) onSlotGainChanged (selectedSlot, soloMode, gainPercent);
            };

            drumsPopup->onKitFxChanged = [this] (const DrumKitFxParams& fx)
            {
                // Mirror into our slot state so the popup can repopulate on
                // re-open / slot-switch, then forward to the host so the
                // engine atomics are updated in real time (RT-safe).
                // Stored privately either way; publishKitFx decides whether the
                // engine hears this edit or the Big Drums rack that outranks it.
                currentSlotParams().drumKit.fx = fx;
                publishKitFx (selectedSlot, soloMode, fx);
            };
            drumsPopup->onApplyUserKit = [this] (const DrumKitParams& kit)
            {
                // A user-kit file was loaded: store it into this slot (so it
                // saves with the set) and fully re-apply so the engine reloads
                // every key's sample + params + FX.
                currentSlotParams().drumKit = kit;
                publishDrumKit (selectedSlot, soloMode, kit);
            };
            // Piano strip at the bottom of the drum editor — same engine targets
            // as the instrument editor's.  This is the only place MIDI 24..34 is
            // visible, since the key-select keyboard above starts at 35.
            drumsPopup->onKeyboardNoteOn = [this](int note, int vel)
            {
                if (onEditorNoteOn) onEditorNoteOn (selectedSlot, soloMode, note, vel);
            };
            drumsPopup->onKeyboardNoteOff = [this](int note)
            {
                if (onEditorNoteOff) onEditorNoteOff (selectedSlot, soloMode, note);
            };
            drumsPopup->onQuerySoundingNotes = [this](uint32_t* mask)
            {
                if (onQuerySoundingNotes)
                    onQuerySoundingNotes (selectedSlot, soloMode, mask);
            };

            drumsPopup->onAuditionKey = [this] (int midiKey)
            {
                if (onAuditionDrumKey)
                    onAuditionDrumKey (selectedSlot, soloMode, midiKey);
            };
            drumsPopup->onSaveAsDefault = [this]
            {
                // Write the current kit to instruments_presets\NNN-Family.ins
                // (host routes drum slots through the drum branch of save).
                if (onSaveAsDefault)
                    onSaveAsDefault (selectedSlot, soloMode, currentSlotParams());
            };
        }

        if (onGetDrumKitRegistry)
            drumsPopup->setDrumKitRegistry (onGetDrumKitRegistry());

        // BEFORE the kit is pushed: setDrumKitParams rebuilds the component row
        // and re-derives the key set, and both of those read the mode.
        pushDrumEditorMode();

        drumsPopup->setDrumKitParams (currentSlotParams().drumKit);
        drumsPopup->setKitGainPercent (currentSlotParams().gainPercent);
        drumsPopup->setSlotLabel     (buildSlotLabel());
        drumsPopup->openCentredOver  (getTopLevelComponent(), 0.78f);

        btnEdit.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFCC6600));
    }

    //==========================================================================
    // BASE UNITY DIALOG — developer only, reached by LEFT double-click on a GAIN
    // handle.  There is no button, no menu entry and no hint: the gesture is the
    // whole discovery mechanism, which is the point.
    //
    // The value it sets is what "100" means for this instrument from here on,
    // so the trim is returned to the middle when it is applied.
    //==========================================================================
    void showBaseUnityDialog()
    {
        if (! onSetBaseUnity) return;

        const float current = onGetBaseUnity ? onGetBaseUnity (selectedSlot, soloMode) : 0.0f;

        // Say where it lands, because the two are not the same thing.  A KIT's
        // base belongs to the SET and to that one kit - composed and sampled
        // kits hold separate values and neither can reach the other's.  An
        // instrument's base is still a property of the instrument itself.
        const bool drumSlot = isCurrentSlotDrum();
        const juce::String where = drumSlot
            ? juce::String ("Saved in the SET, for this kit alone. Range -40 to +40.")
            : juce::String ("Written to both the .ins and the .sins. Range -40 to +40.");

        auto* w = new juce::AlertWindow ("BASE UNITY",
                                         juce::String (drumSlot ? "Base level for this kit, in dB.\n"
                                                                : "Base level for this instrument, in dB.\n")
                                         + "The GAIN handle's middle (100) will mean exactly this.\n"
                                         + where,
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("db", juce::String (current, 2), "dB:");
        w->addButton ("SAVE",   1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, w] (int result)
            {
                std::unique_ptr<juce::AlertWindow> owned (w);
                if (result != 1) return;

                const float dB = juce::jlimit (-40.0f, 40.0f,
                                     w->getTextEditorContents ("db").getFloatValue());

                if (onSetBaseUnity (selectedSlot, soloMode, dB))
                {
                    // The handle returns to the middle: the base IS the new
                    // meaning of 100, so leaving the trim where it was would
                    // apply the correction twice.
                    //
                    // A DRUM slot deliberately does NOT mirror the value into
                    // SlotParams.  SlotParams travels in the set's SoundsState,
                    // and a kit's base already travels in KitUnity - writing both
                    // would give one number two owners, keyed two different ways
                    // (per slot here, per kit there), and the slot's copy would
                    // start winning the moment a different kit landed on it.
                    if (! isCurrentSlotDrum())
                        currentSlotParams().baseUnityDb = dB;
                    currentSlotParams().gainPercent = 100.0f;
                    pushSlotIntoEditor();
                    if (drumsPopup) drumsPopup->setKitGainPercent (100.0f);
                }
            }), false);
    }

    /** Does a saved preset govern whatever is loaded on this slot right now? */
    bool slotHasPreset (int slot, bool isSolo) const
    {
        SlotParams unused;
        return onGetPresetParams && onGetPresetParams (slot, isSolo, unused);
    }

    void pushSlotIntoEditor()
    {
        if (!editorWindow) return;

        // SOLO SLOTS ONLY.
        //
        // Opening the editor is the moment the user asks "what is this sound set
        // to?", and for the right hand the honest answer is the .ins: that file
        // IS the authority for a solo slot, so adopting it here is right.
        //
        // For a STYLE slot it was the bug.  Every open overwrote the slot from
        // whatever .ins happened to govern that GM flag, so an edit made in the
        // editor survived exactly until the window was closed and reopened —
        // and only on slots whose flag HAD a preset, which is why it looked like
        // one channel misbehaving rather than a general fault.  A PAD playing
        // Drawbar Organ hits it; a PAD playing something with no .ins does not.
        //
        // Style slots belong to the SET now.  What is in styleParams IS the
        // truth — put there by the set on load, or by the user a moment ago —
        // and nothing else may speak over it.
        // The .ins adoption is the BLOB source's business only.  An SFZ is not
        // a library instrument and has no .ins to adopt - overwriting its chain
        // from one would be the same class of bug the style slots had.
        // ── AND THE .ins IS NO LONGER RE-STAMPED HERE ────────────────────────
        //
        // This block used to reload the .ins into soloParams on every open, so
        // opening the editor DISCARDED every edit made since the last SAVE
        // SETTINGS.  Change the delay's time signature or division, close the
        // window, open it again: back to whatever the file said - which reads as
        // "the delay jumped to 1/1" and "the reverb lost its settings", because
        // those controls are the ones you set once and do not re-touch.
        //
        // The reasoning above was sound when it was written: for a solo slot the
        // .ins IS the authority.  What changed is WHEN that authority is
        // applied.  It is now adopted the moment the instrument is selected
        // (MainComponent's onInstrumentSelected -> adoptPresetParams) and again
        // after a save (onPresetReloaded), which are the two moments the file
        // actually becomes the truth.  Re-applying it on every window open added
        // nothing except a third moment that could only destroy work.
        //
        // So the rule now matches the style slots', quoted above: what is in
        // soloParams IS the truth - put there by the .ins on load, or by the
        // user a moment ago - and opening a window only DISPLAYS it.
        // Reseed the freeze button: the flag is per SLOT, so switching slots
        // with the window open must show that slot's state, not the last one's.
        editorWindow->setFrozen (isSlotFrozen (selectedSlot, soloMode));

        editorWindow->setSlotParams (paramsFor (selectedSlot, soloMode));
        // ALLOWED-NOTES scope: style melodic only.  Bass (slot 2) = locked
        // 12-note window; chord/pad/lead = free (>=12); drums & solo = hidden.
        editorWindow->setNoteRangeMode (soloMode || selectedSlot < 2
                                            ? 0
                                            : (selectedSlot == 2 ? 1 : 2));
        // Band filter scope: every MELODIC voice gets the two-thumb BAND filter
        // — the style's own melodic slots (3..7) and now the SOLO slots too, so
        // the two editors present the same controls for the same kind of sound.
        // (They differ only in ALLOWED NOTES, which setNoteRangeMode above keeps
        // hidden for solo: the fold window is a style-arrangement device, not a
        // right-hand one.)
        //
        // Bass (2) and drums (0/1) still keep the classic filter + filter env /
        // LFO — the bass wants its cutoff sweep and a kit is not a "melodic
        // voice" in this sense.  Channel::renderBlock's useBand test mirrors
        // exactly this split, or the controls would be inert.
        editorWindow->setBandMode (soloMode || (selectedSlot >= 3 && selectedSlot <= 7));
        editorWindow->setSlotLabel(buildSlotLabel());

        // setSlotParams only *seeds* the editor (GoldSlider::setValue does not
        // notify), so the engine wouldn't otherwise hear these values until the
        // user nudged a control.  Commit them now so the audio matches the UI.
        //
        // paramsFor, not a local: the local `p` disappeared when the editor push
        // was routed through the awake source, and it has to be the awake set
        // that reaches the engine anyway.
        commitSlotParamsToEngine (selectedSlot, soloMode,
                                  paramsFor (selectedSlot, soloMode));
    }

public:
    static constexpr const char* kSfzParamsType = "SfzParams";

    /** The sixteen SFZ chains, as a set child.

        Separate from <SoundsState> on purpose: that block is the BLOB source's
        state, and a set written before SFZ existed must keep meaning exactly
        what it meant.  An absent block leaves every SFZ chain at its default. */
    juce::ValueTree captureSfzParams() const
    {
        juce::ValueTree t (kSfzParamsType);

        auto add = [&t] (const SlotParams& p, int idx, bool isSolo)
        {
            auto c = BetelStateXml::saveSlot (p);
            c.setProperty ("index", idx,    nullptr);
            c.setProperty ("solo",  isSolo, nullptr);
            t.appendChild (c, nullptr);
        };

        for (int i = 0; i < 8; ++i) add (sfzStyleParams[(size_t) i], i, false);
        for (int i = 0; i < 8; ++i) add (sfzSoloParams [(size_t) i], i, true);
        return t;
    }

    void applySfzParams (const juce::ValueTree& t)
    {
        if (! t.isValid() || ! t.hasType (kSfzParamsType)) return;

        for (int c = 0; c < t.getNumChildren(); ++c)
        {
            const auto child = t.getChild (c);
            if (! child.hasType ("Slot")) continue;

            const int  idx    = (int)  child.getProperty ("index", -1);
            const bool isSolo = (bool) child.getProperty ("solo",  false);
            if (idx < 0 || idx >= 8) continue;

            auto& dst = isSolo ? sfzSoloParams[(size_t) idx] : sfzStyleParams[(size_t) idx];
            BetelStateXml::loadSlot (child, dst, BetelStateXml::SlotLoadContext::Preset);

            // Committed ONLY where the SFZ is the awake source - pushing it to a
            // channel playing its blob would put the wrong chain on the wrong
            // sound.  The other slots take theirs when they wake.
            if (sfzAwake (idx, isSolo))
                commitSlotParamsToEngine (idx, isSolo, dst);
        }
    }

    /** Re-push a slot's FULL DSP state to the engine.

        A normal instrument load ends with applyVoiceSetup -> commitSlotParamsToEngine,
        which is what puts the slot's filter, envelopes, EQ, reverb, delay, drum
        rack and octave bias onto the channel.  Loading an SFZ publishes a voice
        and nothing else - so without this the sound plays through a channel whose
        DSP was never configured, which is exactly "it works but the filter and
        effects do nothing".

        Called by the host after a successful SFZ load. */
    void recommitSlot (int slot, bool isSolo)
    {
        if (slot < 0 || slot >= 8) return;
        commitSlotParamsToEngine (slot, isSolo, paramsFor (slot, isSolo));
        pushSlotIntoEditor();          // the editor shows the chain now in force
    }

    /** Push the edited slot's SFZ state into the editor window.

        Public because the host calls it after a successful LOAD - the file
        dialog and the engine call both live there, so the tab has no other way
        to learn that the slot's sound just changed. */
    void refreshSfzState()
    {
        // BOTH windows - a drum slot has the same override as any other.
        if (drumsPopup != nullptr)
        {
            const auto d = onGetSfzState ? onGetSfzState (selectedSlot, soloMode)
                                         : std::make_pair (juce::String(), false);
            drumsPopup->setSfzState (d.first, d.second);
        }

        if (editorWindow == nullptr) return;
        const auto st = onGetSfzState ? onGetSfzState (selectedSlot, soloMode)
                                      : std::make_pair (juce::String(), false);
        editorWindow->setSfzState (st.first, st.second);
    }

private:
    /** WHICH DRUM EDITOR THIS SLOT GETS.

        A composed GM kit and a sampled kit are two different instruments to
        edit - one is assembled from component folders and has per-element
        PITCH / ROUND ROBIN / LENGTH and a REPLACE, the other is one indivisible
        sound with a GAIN, a filter and an FX rack - and DrumsPopup has carried
        both layouts all along.  What it did not have was any way to CHANGE
        mode: the host wrote it once at construction and never again.

        So this is pushed from every path that can change which kit a slot
        holds - opening the editor, switching slot, switching SOLO/STYLE, and
        picking a kit from either row of the grid.  setFullKitName ignores a
        push that does not change the mode, so calling it often costs nothing. */
    void pushDrumEditorMode()
    {
        if (drumsPopup == nullptr) return;

        const bool uniqueKit = onGetFullKitOnSlot
                            && onGetFullKitOnSlot (selectedSlot, soloMode).isNotEmpty();

        drumsPopup->setFullKitName (uniqueKit
                                      ? getSlotDrumKitName (soloMode, selectedSlot)
                                      : juce::String());
    }

    void refreshEditorSlotLabel()
    {
        if (editorWindow && editorWindow->isVisible())
            editorWindow->setSlotLabel(buildSlotLabel());
        if (drumsPopup && drumsPopup->isVisible())
        {
            pushDrumEditorMode();
            drumsPopup->setSlotLabel    (buildSlotLabel());
            drumsPopup->setDrumKitParams(currentSlotParams().drumKit);
            drumsPopup->setKitGainPercent (currentSlotParams().gainPercent);
        }
    }

    juce::String buildSlotLabel() const
    {
        const juce::String slotName = soloMode
            ? "SOLO " + juce::String(selectedSlot + 1)
            : juce::String(kStyleRoleNames[selectedSlot]);
        if (isCurrentSlotDrum())
        {
            const auto& dk = currentSlotParams().drumKit;
            const juce::String kit = dk.lastLoadedKit.isEmpty()
                ? juce::String ("(no kit)")
                : dk.lastLoadedKit + " kit";
            return "Editing: " + slotName + "  |  " + kit;
        }
        const int p = soloMode ? soloPatch[selectedSlot] : stylePatch[selectedSlot];
        const char* name = (p >= 0 && p < 128) ? kFullInstrNames[p] : "-";
        return "Editing: " + slotName + "  |  " + juce::String(name);
    }

    void saveCurrentSlotParams(const SlotParams& p)
    {
        // Detect a GAIN move before the snapshot is overwritten: the editor
        // reports the whole SlotParams on every knob, and the trim must only be
        // sent when it is the thing that actually changed.
        // THE AWAKE SOURCE'S SET, not the blob's.  Editing the filter while an
        // SFZ is sounding must shape the SFZ and leave the style's own chain
        // exactly where it was - that is what makes the two independent.
        auto& dst = paramsFor (selectedSlot, soloMode);
        const float prevGain = dst.gainPercent;
        dst = p;

        if (p.gainPercent != prevGain && onSlotGainChanged)
            onSlotGainChanged (selectedSlot, soloMode, p.gainPercent);

        if (onClickParamsChanged)
            onClickParamsChanged(selectedSlot, soloMode,
                                 p.clickEnabled, p.clickVolume, p.clickDecayMs);
        if (onSlotParamsChanged)
            onSlotParamsChanged (selectedSlot, soloMode, p);
    }

    // Dropdown shows DISPLAY names ("Standard"); the registry keys sampled kits
    // by numeric folder prefix ("000").  Map the known families; pass anything
    // else through lower-cased so a numeric key or registry name still resolves.
    static juce::String registryKeyForKitName (const juce::String& display)
    {
        static const std::map<juce::String, juce::String> m = {
            { "Standard", "000" }, { "Room",      "008" }, { "Power",     "016" },
            { "Electronic","024" },{ "TR-808",    "025" }, { "Jazz",      "032" },
            { "Brush",    "040" }, { "Orchestra", "048" }, { "SFX",       "056" }
        };
        auto it = m.find (display);
        return it != m.end() ? it->second : display.trim().toLowerCase();
    }

    // PUBLIC because the 30 Hz engine->UI mirror in MainComponent needs it too:
    // the engine reports a composed kit by REGISTRY KEY ("000", "016") and the
    // mixer label has to show a name.  One converter, one source of truth -
    // giving the host its own copy is how the sixteen displayNameFor variants
    // happened.
public:
    // Inverse of registryKeyForKitName: resolve a kit identifier to its DISPLAY
    // name.  The engine reports kits by registry key ("000"…"056") and the 30 Hz
    // engine->UI mirror writes that into lastLoadedKit, so the selector highlight
    // and the canvas label (which compare against / print the display name) must
    // map it back.  Accepts a display name, a registry key, or empty; anything
    // unrecognised (a user/custom kit) passes through unchanged.
    static juce::String displayNameForKit (const juce::String& kit)
    {
        if (kit.isEmpty()) return {};
        for (int i = 0; i < kMaxInstrCells; ++i)
            if (kit == kDrumKitNames[i] || registryKeyForKitName (kDrumKitNames[i]) == kit)
                return kDrumKitNames[i];
        return kit;
    }
private:

    // ── Drum-kit cell helpers ─────────────────────────────────────────────────
    void loadDrumKitIntoCurrentSlot (const juce::String& kitName)
    {
        if (! onGetDrumKitRegistry) return;
        auto* reg = onGetDrumKitRegistry();
        if (reg == nullptr) return;

        const juce::String regKey = registryKeyForKitName (kitName);
        const int          flag   = regKey.getIntValue();

        DrumKitParams dk;

        // 1) "Save as Default" preset for this kit → load the WHOLE saved kit, so
        //    the selector matches the drum program-change path exactly.
        bool loaded = false;
        if (onGetInstrumentPresetsFolder)
        {
            std::map<int, Betel::InstrumentPreset> drumMap;
            Betel::InstrumentPresetIO::scanFolder (onGetInstrumentPresetsFolder(),
                                                   nullptr, &drumMap);
            if (auto it = drumMap.find (flag);
                it != drumMap.end() && it->second.params.drumKit.hasMappedKeys())
            {
                dk     = it->second.params.drumKit;
                loaded = true;
            }
        }

        // 2) Otherwise build from the registry.  Resolve via the numeric key
        //    first, then the raw name, so a name mismatch can't silence the kit.
        if (! loaded)
        {
            auto elems = reg->getElementsForKit (regKey);
            if (elems.empty()) elems = reg->getElementsForKit (kitName.toLowerCase());
            if (elems.empty()) return;   // unknown kit — keep the current one, don't wipe

            dk.lastLoadedKit = regKey;

            auto place = [&dk] (const std::vector<Betel::DrumKitRegistry::ElementHandle>& hs)
            {
                for (const auto& h : hs)
                {
                    if (h.midiKey < 0 || h.midiKey >= 128) continue;
                    auto& entry = dk.keys[(size_t) h.midiKey];
                    entry.sourceKit    = h.kitName;   // element's OWN kit (per-kit / global)
                    entry.sourceRoleId = h.roleId;
                }
            };
            place (elems);
            // Merge GLOBAL components (low_kick / the_lasts) like programChangeDrum.
            for (const auto& gName : reg->getGlobalComponentNames())
                place (reg->getElementsForKit (gName));
        }

        // Label the kit by its DISPLAY name (e.g. "Standard"), not the registry
        // key ("000").  The selector highlight compares against kDrumKitNames and
        // the canvas prints "<Name> kit", so both need the display name here; the
        // numeric program-change is shown on the cell instead.  Samples resolve
        // from each key's own sourceKit, so this label never affects playback.
        dk.lastLoadedKit = kitName;

        // NOTE what is NOT here: no FX seeding.  dk is a fresh DrumKitParams,
        // whose fx block defaults to every stage OFF, so a kit picked from the
        // selector arrives dry and stays that way until a toggle says otherwise.
        currentSlotParams().drumKit = dk;

        publishDrumKit (selectedSlot, soloMode, dk);
        publishKitFx   (selectedSlot, soloMode, dk.fx);

        if (drumsPopup && drumsPopup->isVisible())
        {
            // The slot just moved from a sampled kit to a composed one (or
            // between two composed ones).  Without this the open window keeps
            // the sampled layout over a GM kit - no component row, no REPLACE,
            // and every edit fanned out across all 128 keys.
            pushDrumEditorMode();
            drumsPopup->setDrumKitParams (dk);
        }

        refreshInstrDisplay();
        repaint();
    }

    // ── Selection helpers ─────────────────────────────────────────────────────
    void setMode(bool isSolo)
    {
        soloMode     = isSolo;
        selectedSlot = 0;
        syncTimbreToSlot();
        refreshModeButtons(); refreshBankButtons(); layoutSlots();
        refreshSlotButtons(); refreshTimbreButtons(); refreshInstrDisplay();
        refreshRefPills();
        reloadInstrImage();
        pushSlotIntoEditor();
        refreshIgnorePcButton();   // per slot, and hidden entirely in SOLO
        resized();   // drum-vs-melodic visibility depends on slot

        if (soloMode && onActiveSoloSlotChanged) onActiveSoloSlotChanged(selectedSlot);
    }

    void selectSlot(int i)
    {
        const bool wasDrum = isCurrentSlotDrum();
        selectedSlot = juce::jlimit(0, 7, i);
        syncTimbreToSlot();
        refreshSlotButtons(); refreshTimbreButtons(); refreshInstrDisplay();
        reloadInstrImage();
        pushSlotIntoEditor();
        if (wasDrum != isCurrentSlotDrum())
            resized();   // timbre row visibility flips with slot type

        if (drumsPopup && drumsPopup->isVisible())
        {
            pushDrumEditorMode();        // DRUMS and PERC can differ
            drumsPopup->setSlotLabel     (buildSlotLabel());
            drumsPopup->setDrumKitParams (currentSlotParams().drumKit);
        }

        refreshIgnorePcButton();     // the toggle is per slot

        if (soloMode && onActiveSoloSlotChanged) onActiveSoloSlotChanged(selectedSlot);
    }

    void selectTimbre(int i)
    {
        // Drum slots have no timbre row; ignore stray calls.
        if (isCurrentSlotDrum()) return;

        selectedTimbre = juce::jlimit(0, 15, i);   // 16 melodic categories only

        // ── IN A PACK THIS ROW ONLY BROWSES ──────────────────────────────────
        //
        // The GM branch below rewrites the slot's patch, keeping the same cell
        // in the newly chosen family - which is right for GM, where the grid IS
        // the program number and moving families is picking a sound.
        //
        // A pack category is not a family and its cells are not a fixed grid:
        // the flags are whatever the folder happens to contain.  Rewriting the
        // patch here would jump the slot onto an unrelated sound just for
        // looking at another category, and with an empty category it would jump
        // it onto nothing.  So a category click reloads the page and stops.
        if (isPackBank())
        {
            packPage = 0;
            refreshPackInstruments();
            refreshTimbreButtons();
            layoutInstrCells();
            refreshInstrDisplay();
            refreshPageControls();
            return;
        }

        const int instrIdx = (soloMode ? soloPatch[selectedSlot] : stylePatch[selectedSlot]) % 8;
        int& slotPatch = soloMode ? soloPatch[selectedSlot] : stylePatch[selectedSlot];
        slotPatch = selectedTimbre * 8 + instrIdx;
        if (onInstrumentSelected) onInstrumentSelected(soloMode, selectedSlot, slotPatch);

        refreshTimbreButtons();
        layoutInstrCells();
        refreshInstrDisplay();
        reloadInstrImage();
        refreshEditorSlotLabel();
    }

    /** Switch bank.  DRUM slots ignore it entirely - a kit is neither GM nor
        World, it is a kit - so the selector is hidden there. */
    void setPack (int pack)
    {
        selectedPack = juce::jlimit (0, 2, pack);

        // The category list belongs to the pack, so it is refetched on every
        // switch rather than cached per pack: a user can drop a pack folder in
        // and rescan without the tab holding a stale idea of what is installed.
        packCategoryNames.clear();
        if (isPackBank() && onGetPackCategories)
            packCategoryNames = onGetPackCategories (selectedPack);

        // Category 0, page 0 on entry: a stale index from the previous pack
        // could land past the end of a shorter one.
        selectedTimbre = 0;
        packPage       = 0;
        refreshPackInstruments();

        refreshBankButtons();
        refreshTimbreButtons();
        layoutInstrCells();
        refreshInstrDisplay();
        resized();
        repaint();
    }

    void refreshBankButtons()
    {
        btnBankGm      .setActive (selectedPack == 0);
        btnBankWorld   .setActive (selectedPack == 1);
        btnBankOriental.setActive (selectedPack == 2);
    }

    /** Pull the current category's sounds.  Called on every pack, category and
        rescan change - never cached across them, because the pack folders can
        appear or vanish between two clicks. */
    void refreshPackInstruments()
    {
        worldInstruments.clear();
        if (! isPackBank() || ! onGetPackInstruments) return;

        const juce::String cat = (selectedTimbre >= 0
                               && selectedTimbre < packCategoryNames.size())
                                    ? packCategoryNames[selectedTimbre]
                                    : juce::String();

        worldInstruments = onGetPackInstruments (selectedPack, cat);
        packPage = juce::jlimit (0, juce::jmax (0, packPageCount() - 1), packPage);
    }

    /** How many pages of 8 the current category fills, at least one. */
    int packPageCount() const
    {
        const int n = (int) worldInstruments.size();
        return juce::jmax (1, (n + 7) / 8);
    }

    void stepPackPage (int delta)
    {
        const int last = packPageCount() - 1;
        const int want = packPage + delta;
        if (want < 0 || want > last) return;      // no wrap: the ends are ends

        packPage = want;
        refreshInstrDisplay();
        refreshPageControls();
    }

    /** The +/- pair and the "3 / 7" readout.  Hidden entirely outside a pack and
        on a drum slot - a kit is not paged. */
    void refreshPageControls()
    {
        const bool show = isPackBank() && ! isCurrentSlotDrum();
        const int  last = packPageCount() - 1;

        btnPagePrev.setVisible (show);
        btnPageNext.setVisible (show);
        pageCaption.setVisible (show);
        pageLabel  .setVisible (show);

        if (! show) return;

        // Greyed at the ends rather than wrapping: with 8 cells and a short
        // category, wrapping makes it impossible to tell a full page from a
        // single one.
        btnPagePrev.setEnabled (packPage > 0);
        btnPageNext.setEnabled (packPage < last);
        pageLabel.setText (juce::String (packPage + 1) + " / " + juce::String (last + 1),
                           juce::dontSendNotification);
    }

    void selectInstrument(int i)
    {
        if (isCurrentSlotDrum())
        {
            // Click on a kit cell loads that kit (by fixed name).  The cell
            // index maps 1:1 to kDrumKitNames, so the user always knows which
            // kit they're getting regardless of registry scan state.
            if (i >= 0 && i < kMaxInstrCells)
                loadDrumKitIntoCurrentSlot (juce::String (kDrumKitNames[i]));

            // AND THE PICTURE.  This branch used to refresh the cells and return,
            // which was invisible while drum slots showed no image at all — the
            // moment they got one, the display sat on whichever kit happened to
            // be loaded first and never moved.  The melodic branch below has
            // always called both; this one simply never needed to.
            refreshInstrDisplay();
            reloadInstrImage();
            return;
        }
        int& slotPatch = soloMode ? soloPatch[selectedSlot] : stylePatch[selectedSlot];

        if (isPackBank())
        {
            // PACK cells are the current CATEGORY, 8 per page - so the index is
            // page-relative, not timbre-relative.  The timbre row picks the
            // category now; the +/- pair below the cells picks the page.
            const int idx = packPage * 8 + i;
            if (idx < 0 || idx >= (int) worldInstruments.size()) return;

            slotPatch = worldInstruments[(size_t) idx].first;
            refreshInstrDisplay(); reloadInstrImage();
            refreshEditorSlotLabel();
            if (onInstrumentSelected) onInstrumentSelected(soloMode, selectedSlot, slotPatch);
            return;
        }

        slotPatch      = selectedTimbre * 8 + i;
        applyGmEnvDefaults (soloMode, selectedSlot, slotPatch);   // GM env per family
        refreshInstrDisplay(); reloadInstrImage();
        refreshEditorSlotLabel();
        if (onInstrumentSelected) onInstrumentSelected(soloMode, selectedSlot, slotPatch);
    }

    void syncTimbreToSlot()
    {
        if (isCurrentSlotDrum()) { selectedTimbre = 0; return; }

        // In WORLD the row is pages, not timbres, so there is nothing to sync -
        // dividing a flag of 214 by 8 would land on a meaningless page.
        if (isPackBank()) return;
        const int p = soloMode ? soloPatch[selectedSlot] : stylePatch[selectedSlot];
        selectedTimbre = juce::jlimit(0, 15, p / 8);
    }

    void refreshModeButtons()  { btnModeSolo.setActive(soloMode); btnModeStyle.setActive(!soloMode); }
    void refreshTimbreButtons()
    {
        for (int i = 0; i < kNumTimbres; ++i)
        {
            timbreButtons[i].setActive (i == selectedTimbre);

            // In a PACK the row is a CATEGORY selector, not a timbre selector:
            // these sounds have no GM family, so the labels are the pack's own
            // subfolder names and anything past the end is hidden rather than
            // left as a dead button labelled PIANO.  Paging within a category
            // is the +/- pair below the cells, not this row.
            if (isPackBank())
                timbreButtons[i].setButtonText (i < packCategoryNames.size()
                                                    ? packCategoryNames[i]
                                                    : juce::String());
            else
                timbreButtons[i].setButtonText (kTimbreNames[i]);
        }
    }
    void refreshSlotButtons()  { for (int i = 0; i < 8; ++i) slotButtons[i].setActive(i == selectedSlot); }

    void layoutRefPills()
    {
        if (pillRowH == 0) return;
        // Drum/perc view has no GM reference voices (you pick a kit) — keep the
        // whole pill row hidden no matter which path calls this.
        if (isCurrentSlotDrum())
        {
            for (int i = 0; i < 8; ++i)
            {
                refPills[i].setVisible(false);
                refPills[i].setBounds(0, 0, 0, 0);
            }
            return;
        }
        const int W = getWidth(), padX = 6, usW = W - padX*2, gap = 2;
        const int n = 8;
        const float cw = (float)(usW - (n-1)*gap) / (float)n;
        for (int i = 0; i < n; ++i)
        {
            // Drum slots have no reference voices — hide their pills.
            if (roleIsDrum (soloMode, i))
            {
                refPills[i].setVisible(false);
                refPills[i].setBounds(0, 0, 0, 0);
                continue;
            }
            const int bx = padX + (int)std::round(i * (cw + gap));
            const int bw = (i == n-1) ? (padX + usW - bx) : (int)std::round(cw);
            refPills[i].setBounds(bx, pillRowY, bw, pillRowH);
            refPills[i].setVisible(true);
        }
        refreshRefPills();
    }

    void refreshRefPills()
    {
        for (int i = 0; i < 8; ++i)
            refPills[i].setSelectedId(soloMode ? soloRef[(size_t) i]
                                               : styleRef[(size_t) i]);
    }

    void layoutSlots()
    {
        if (slotRowH == 0) return;
        const int W = getWidth(), padX = 6, usW = W - padX*2, gap = 2;
        const int n = 8;
        const float cw = (float)(usW - (n-1)*gap) / (float)n;
        for (int i = 0; i < n; ++i)
        {
            const int bx = padX + (int)std::round(i * (cw + gap));
            const int bw = (i == n-1) ? (padX + usW - bx) : (int)std::round(cw);
            slotButtons[i].setBounds(bx, slotRowY, bw, slotRowH);
            slotButtons[i].setVisible(true);
            slotButtons[i].setButtonText(soloMode ? juce::String("SOLO ") + juce::String(i + 1)
                                                  : juce::String(kStyleRoleNames[i]));
        }
    }

    // Lays out the instrument row.  Cell count depends on the active timbre:
    // 9 half-width cells when DRUMS is selected (the 9 sampled kits), 8 wide
    // cells otherwise.  Hidden buttons are kept off-canvas so their bounds
    // don't intercept mouse events.
    /** THE DRUM KIT MATRIX - three rows.

            row 0        the nine GM kits, composed from the component folders
            rows 1 and 2 the SAMPLED kits found on disk

        One grid, because from the player's side they are one choice: which kit
        plays.  That they are built two completely different ways - assembled per
        key versus loaded whole - is an implementation fact, and the row split is
        as much as the UI needs to say about it. */
    void layoutInstrCells()
    {
        if (instrRowH == 0) return;
        const int W = getWidth(), padX = 6, usW = W - padX*2, gap = 2;
        const bool drumSlot = isCurrentSlotDrum();

        if (! drumSlot)
        {
            const int n = 8;
            const float cw = (float)(usW - (n-1)*gap) / (float)n;
            for (int i = 0; i < n; ++i)
            {
                const int bx = padX + (int)std::round(i * (cw + gap));
                const int bw = (i == n-1) ? (padX + usW - bx) : (int)std::round(cw);
                instrButtons[i].setBounds(bx, instrRowY, bw, instrRowH);
                instrButtons[i].setVisible(true);
            }
            for (int i = n; i < kMaxInstrCells; ++i)
            {
                instrButtons[i].setVisible(false);
                instrButtons[i].setBounds(0, 0, 0, 0);
            }
            for (auto& b : fullKitButtons) { b.setVisible(false); b.setBounds(0,0,0,0); }
            return;
        }

        // ── row 0: the GM kits ────────────────────────────────────────────────
        const int n = kMaxInstrCells;
        const float cw = (float)(usW - (n-1)*gap) / (float)n;
        for (int i = 0; i < n; ++i)
        {
            const int bx = padX + (int)std::round(i * (cw + gap));
            const int bw = (i == n-1) ? (padX + usW - bx) : (int)std::round(cw);
            instrButtons[i].setBounds(bx, instrRowY, bw, instrRowH);
            instrButtons[i].setVisible(true);
        }

        // ── rows 1 and 2: the sampled kits ────────────────────────────────────
        //
        // Deliberately the SAME cw/gap maths as the row above, so every column
        // lines up down the whole matrix.  n is 9 here too, so the two loops
        // cannot drift apart.
        for (int i = 0; i < kMaxFullKitCells; ++i)
        {
            auto& b = fullKitButtons[(size_t) i];
            if (i >= (int) fullKits.size())
            {
                b.setVisible(false); b.setBounds(0,0,0,0);
                continue;
            }
            const int row = i / kFullKitsPerRow;   // 0 or 1 -> screen rows 1 and 2
            const int col = i % kFullKitsPerRow;
            const int bx  = padX + (int)std::round(col * (cw + gap));
            const int bw  = (col == n-1) ? (padX + usW - bx) : (int)std::round(cw);
            b.setBounds(bx, instrRowY + (row + 1) * (instrRowH + gap), bw, instrRowH);
            b.setVisible(true);
        }
    }

    /** Refill the sampled-kit row labels and light whichever is loaded. */
    void refreshFullKitCells()
    {
        if (! isCurrentSlotDrum()) return;

        if (fullKits.empty() && onGetFullKits) fullKits = onGetFullKits();

        const juce::String live = onGetFullKitOnSlot
                                    ? onGetFullKitOnSlot (selectedSlot, soloMode)
                                    : juce::String();

        for (int i = 0; i < kMaxFullKitCells; ++i)
        {
            auto& b = fullKitButtons[(size_t) i];
            if (i >= (int) fullKits.size()) continue;

            const auto& k = fullKits[(size_t) i];
            b.setButtonText (std::get<3> (k));
            b.setProgramNumber (-1);

            const auto id = juce::String (std::get<0>(k)) + "/"
                          + juce::String (std::get<1>(k)) + "/"
                          + juce::String (std::get<2>(k));
            b.setActive (id == live);
        }
    }

public:
    /** Light the toggle from the ENGINE, never from a local copy.

        Public because a set load restores the eight flags in the processor and
        the button has to catch up - the same reason refreshSfzState is public. */
    void refreshIgnorePcButton()
    {
        const bool on = ! soloMode && onGetIgnorePc && onGetIgnorePc(selectedSlot, false);
        // Toggle state only: the orange lives in textColourOnId, set once in the
        // constructor, so there is one place that decides what "on" looks like.
        btnIgnorePc.setToggleState(on, juce::dontSendNotification);
        btnIgnorePc.repaint();
    }

    /** Repaint the instrument cells - public for the same reason as
        refreshSfzState: an SFZ load happens in the host, not here. */
    void refreshInstrDisplay()
    {
        refreshFullKitCells();

        if (isCurrentSlotDrum())
        {
            // Show the fixed 9-kit list (the engine's composed kits at canonical
            // PCs 0/8/16/24/25/32/40/48/56).  Highlight the loaded kit; if none
            // is loaded yet, default the highlight to the first kit so the
            // selector is never blank.
            //
            // ONE KIT PLAYS, SO ONE CELL LIGHTS.
            //
            // A SAMPLED kit puts its readable label ("Arabic Kit") into
            // lastLoadedKit, and displayNameForKit cannot resolve that to a
            // composed kit - so currentKit came back empty, the "nothing loaded
            // yet" default fired, and cell 0 (Standard) lit UNDERNEATH the lit
            // sampled cell.  Two kits highlighted, one of them a sound that was
            // not playing.  When a sampled kit holds the slot, no GM cell is
            // active and the default is suppressed with it.
            const bool uniqueKit = onGetFullKitOnSlot
                                && onGetFullKitOnSlot (selectedSlot, soloMode).isNotEmpty();

            const juce::String currentKit = displayNameForKit (currentSlotParams().drumKit.lastLoadedKit);
            const bool noneLoaded = currentKit.isEmpty();
            for (int i = 0; i < kMaxInstrCells; ++i)
            {
                const juce::String name (kDrumKitNames[i]);
                instrButtons[i].setButtonText (name);
                instrButtons[i].setActive     (uniqueKit ? false
                                                         : (noneLoaded ? (i == 0)
                                                                       : (name == currentKit)));
                instrButtons[i].setProgramNumber (registryKeyForKitName (name).getIntValue());
            }
            return;
        }

        // AN SFZ HOLDS THE SLOT -> say so on the cells.  The GM/World grids
        // describe what the library offers; while an override is on, none of it
        // is what you are hearing, and leaving a cell lit would be a lie.
        if (onGetSfzState)
        {
            const auto st = onGetSfzState (selectedSlot, soloMode);
            if (st.second && st.first.isNotEmpty())
            {
                for (int i = 0; i < kMaxInstrCells; ++i)
                {
                    instrButtons[i].setButtonText (i == 0 ? st.first : juce::String());
                    instrButtons[i].setProgramNumber (-1);
                    instrButtons[i].setActive (i == 0);
                }
                return;
            }
        }

        if (isPackBank())
        {
            const int cur  = soloMode ? soloPatch[selectedSlot] : stylePatch[selectedSlot];
            const int base = packPage * 8;

            for (int i = 0; i < 8; ++i)
            {
                const int idx = base + i;
                const bool has = idx < (int) worldInstruments.size();

                instrButtons[i].setButtonText (has ? worldInstruments[(size_t) idx].second
                                                   : juce::String());
                instrButtons[i].setProgramNumber (has ? worldInstruments[(size_t) idx].first : -1);
                instrButtons[i].setActive (has && worldInstruments[(size_t) idx].first == cur);
            }
            return;
        }

        const int cur = soloMode ? soloPatch[selectedSlot] : stylePatch[selectedSlot];
        // The active cell is the slot's instrument when we're viewing its timbre;
        // otherwise default to the first cell so a timbre never shows blank.
        const int activeCell = (cur >= 0 && cur / 8 == selectedTimbre) ? (cur % 8) : 0;
        for (int i = 0; i < 8; ++i) {
            const int patch = selectedTimbre * 8 + i;
            instrButtons[i].setButtonText(kInstrNames[selectedTimbre][i]);
            instrButtons[i].setProgramNumber(patch);     // GM program-change value
            instrButtons[i].setActive(i == activeCell);
        }
    }
private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundsTab)
};



