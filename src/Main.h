#pragma once
//==============================================================================
// Main.h
//
// Declares BetelgeuseProcessor so that MainComponent.cpp (and any other
// translation unit) can take a reference to it without having to drag in the
// editor / GL plumbing from Main.cpp.
//
// The processor owns:
//   - the SamplePlayerEngine (24 channels: 16 style + 8 solo)
//   - the MidiKeyboardState that the virtual on-screen keyboard injects into
//   - an "active solo slot" atomic — every incoming MIDI event (DAW + virtual
//     keyboard) is routed to engine channel (kNumStyleChannels + activeSoloSlot)
//   - a DrumKitRegistry instance (Phase 3) — scanned from a kits folder at
//     construction time and handed to the engine via setDrumKitRegistry, so
//     drum-mode channels can resolve PCs and load kits without any further
//     plumbing from the host
//
// Access code:
//   All Betelgeuse sound packs (main library + drum kits) are signed with the
//   fixed code "111222".  The InstrEditorWindow no longer exposes a textbox;
//   loadBlob() and scanDrumKitFolder() default to that code so callers may
//   pass an empty string without issue.
//==============================================================================

#include <JuceHeader.h>
#include <tuple>
#include <vector>
#include <atomic>
#include <memory>
#include <vector>
#include "SamplePlayerEngine.h"
#include "DrumKitRegistry.h"
#include "InstrEditPanel.h"     // DrumKitParams / DrumElementParams
#include "InstrumentPreset.h"   // .ins default-preset read/write
#include "StyleData.h"
#include "StyleSequencer.h"
#include <cmath>

#include "Finisher.h"
#include "StylePlayer.h"
#include "StyleLoudness.h"   // per-section BS.1770 measurement + corrective trim
#include "BetelFolderManager.h"
#include "MasterSettings.h"     // solo base unity + pitch bend live in the master file
#include "GrexSongRecorder.h"   // MIDI performance capture (phase 1)
#include "GrexSongPlayer.h"     // MIDI performance playback (phase 2)

class BetelgeuseProcessor : public juce::AudioProcessor
{
public:
    // Fixed access code embedded in every Betelgeuse sound pack and drum-kit
    // blob.  Centralised here so there's exactly one definition.
    static constexpr const char* kFixedAccessCode = "111222";

    BetelgeuseProcessor();
    ~BetelgeuseProcessor() override
    {
        *aliveFlag = false;

        // LAST CHANCE TO WRITE.  The UI timer normally flushes, but it only runs
        // while an editor is open - a change made from a MIDI CC in a window-less
        // session would otherwise never reach disk.  Destruction is the message
        // thread, so writing here is safe.
        Betel::MasterSettings::get().flushIfDirty();
    }

    // ── AudioProcessor surface ────────────────────────────────────────────────
    const juce::String getName() const override        { return "Grex"; }
    bool acceptsMidi() const override                  { return true; }
    bool producesMidi() const override                 { return false; }
    bool isMidiEffect() const override                 { return false; }
    double getTailLengthSeconds() const override       { return 0.0; }
    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram(int) override               {}
    const juce::String getProgramName(int) override    { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override     {}
    void setStateInformation(const void*, int) override       {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void prepareToPlay(double sampleRate, int blockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                    { return true; }

    // ── Shared state used by MainComponent / SoundsTab ────────────────────────
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    void setActiveSoloSlot(int slot) noexcept
    {
        activeSoloSlot.store(juce::jlimit(0, Betel::SamplePlayerEngine::kNumSoloChannels - 1, slot));
    }
    int  getActiveSoloSlot() const noexcept            { return activeSoloSlot.load(); }

    //==========================================================================
    // WHERE EACH HELD KEY WAS SENT.  Stuck-note insurance.
    //
    // processBlock decided a note's destination from three LIVE values -
    // pianoMode, the split point, and the solo mask / activeSoloSlot - and then
    // decided it AGAIN, from those same live values, when the note-off arrived.
    // Change any of them while a key is down and the two answers differ, so the
    // note-off is delivered somewhere the note-on never went and the voice it
    // was meant to release sustains forever.
    //
    // All three change during ordinary playing: picking another right-hand voice
    // moves activeSoloSlot, the SOLO ELEMENTS row rewrites the mask, and
    // PIANO/ARRANGER moves the split. Which is why it felt random and why it
    // showed up under fast playing - the faster the phrase, the more keys are
    // down when something moves.
    //
    // So the route is LATCHED at note-on and replayed at note-off, exactly as
    // Channel already latches transposition in keyDownXpose. Same bug, one
    // layer up.
    //
    // ACCUMULATED with |=, not overwritten: a key struck again before its first
    // note-off (which is what fast playing IS) would otherwise leave the first
    // strike's channel unreleased. Releasing every channel the key ever reached
    // can at worst stop a note early; the alternative hangs it.
    //
    // Audio thread only - both are read and written inside processBlock.
    //==========================================================================
    uint8_t noteRouteSolo  [128] {};   // bit per solo slot that got the note-on
    bool    noteRouteChord [128] {};   // the note-on went to chord recognition

    void clearNoteRoutes() noexcept
    {
        for (int i = 0; i < 128; ++i) { noteRouteSolo[i] = 0; noteRouteChord[i] = false; }
    }

    /** LIVE-SAVED, like the pitch bend range and the low-velocity lift.

        The split used to live only in the project state, so it survived a
        project reload and nothing else: a fresh session always came up at the
        default, and the player set it again every time.  MasterSettings::
        setSplitPoint writes only on a real change, so dragging the knob costs
        one file write when the drag ends on a new note, not one per pixel. */
    void setSplitPoint (int midiNote) noexcept
    {
        chordTracker.setSplitPoint (midiNote);
        Betel::MasterSettings::get().setSplitPoint (midiNote);
    }

    /** Split point WITHOUT touching the master file - for restoring a project's
        stored value, which must not overwrite the player's own setting.

        The distinction matters because a project carries a split too: opening
        someone else's song should position the keyboard for that song without
        redefining where this player's hands live from then on. */
    void setSplitPointFromProject (int midiNote) noexcept
    {
        chordTracker.setSplitPoint (midiNote);
    }
    int  getSplitPoint() const noexcept                { return chordTracker.getSplitPoint(); }
    /** Per-section loudness measurement — readout, target and re-measure. */
    Betel::StyleLoudness&       getStyleLoudness()       noexcept { return styleLoudness; }
    const Betel::StyleLoudness& getStyleLoudness() const noexcept { return styleLoudness; }

    // ── Style transport ───────────────────────────────────────────────────────
    bool loadStyle (const juce::File& file, juce::String& errorMsg);

    // ── Bypass instrument audio chain (Global Settings toggle) ────────────────
    // When true, only the amp ADSR + channel volume survive.  Filter, amp LFO,
    // pitch ENV/LFO, velocity gain, channel pan, clicks, and every post-mix
    // FX stage (5-band EQ, Chorus, Wah, Phaser, Delay, Reverb, drum FX bus)
    // are bypassed.  Channel volume stays connected so the mixer faders (and
    // CC 7) keep balancing the instruments.  Applies to drum channels too.
    // Effect is immediate; the next audio block reflects the new state.  See
    // chainBypassed in Channel.h for the precise list of what survives.
    void setBypassInstrumentChain (bool b) noexcept
    {
        bypassInstrumentChainAtomic.store (b);
        engine.setBypassInstrumentChain (b);
    }
    bool getBypassInstrumentChain() const noexcept
    {
        return bypassInstrumentChainAtomic.load();
    }

    // ── Styles-folder scanning (used by StylesTab's SEARCH genre) ─────────────
    //
    // `stylesFolders` is a host-managed list of directories the processor
    // scans recursively for every Yamaha SFF style variant (.sty .prs .bcs
    // .sst .pst .pcs .fps .scp .aus) when the user runs a keyword search.
    // Defaults to ~/Documents/Betelgeuse/Styles (added in the constructor);
    // call addStylesFolder() to register additional search roots.
    struct StyleSearchResult
    {
        juce::String displayName;    // file name without extension
        juce::String absolutePath;
    };

    void addStylesFolder (const juce::File& folder);
    void clearStylesFolders() { stylesFolders.clear(); }
    const std::vector<juce::File>& getStylesFolders() const { return stylesFolders; }

    /** Recursively scan every registered styles folder for Yamaha SFF
        style files (.sty .prs .bcs .sst .pst .pcs .fps .scp .aus) whose
        filename (sans extension) contains `keyword` (case-insensitive).
        Empty keyword returns every file in every folder.  Results are
        sorted alphabetically by display name. */
    std::vector<StyleSearchResult> searchStyles (const juce::String& keyword) const;

    /** Genre names = the immediate sub-folder names of <root>/styles, sorted
        alphabetically (case-insensitive). */
    juce::StringArray getStyleGenres() const;

    /** Every Yamaha SFF style file directly inside <root>/styles/<genre>,
        sorted alphabetically by display name. */
    std::vector<StyleSearchResult> getStylesInGenre (const juce::String& genre) const;

    /** Path of the most-recently-loaded style file (empty if none).  Used by
        the Favorites system to recall which style was up when a set was
        saved. */
    juce::String getLastLoadedStylePath() const { return lastLoadedStylePath; }

    // ── Editor-session snapshot (see members below) ───────────────────────────
    // The editor calls these to decide between a first-time engine seed and a
    // state-preserving restore when the plugin window is (re)opened.
    bool                  isEditorInitialized()  const { return editorInitialized; }
    void                  markEditorInitialized()       { editorInitialized = true; }
    void                  storeEditorSnapshot (juce::ValueTree t) { editorSnapshot = std::move (t); }
    const juce::ValueTree& getEditorSnapshot() const    { return editorSnapshot; }

    // ── MIDI performance recorder / player (record-stop-save / load-apply-play).
    //    Owned here so processBlock can capture input and inject playback; the
    //    editor drives arm/stop/save/load/applySetup.
    GrexSongRecorder& getSongRecorder() noexcept { return songRecorder; }
    GrexSongPlayer&   getSongPlayer()   noexcept { return songPlayer;   }

    /** Capture / restore the host-side global state used by the Favorites
        system (active solo slot, split point, the loaded style file path,
        master volume).  Per-slot params live in SoundsTab — capture those
        separately via soundsTab.captureState(). */
    juce::ValueTree captureGlobalState() const;
    void            applyGlobalState (const juce::ValueTree& gs);

    // ── Root folder management ────────────────────────────────────────────────
    BetelFolderManager&       getFolderManager()       { return folderManager; }
    const BetelFolderManager& getFolderManager() const { return folderManager; }

    /** Re-scan the drum-kit registry + the styles-folder list from whatever
        the folder manager currently reports as the root.  Call from the UI
        after the user picks a new root folder. */
    void rescanFromFolderManager();

    // ── Style-playback accessors (transport UI lives in MainComponent) ────────
    Betel::StyleSequencer&    getSequencer()    noexcept { return sequencer; }
    Betel::StylePlayer&       getStylePlayer()  noexcept { return stylePlayer; }
    Betel::ChordZoneTracker&  getChordTracker() noexcept { return chordTracker; }

    /** True iff a style file is currently loaded — used by the UI to enable
        / disable transport buttons. */
    bool hasStyle() const noexcept { return currentStyle != nullptr; }

    /** Read-only access to the currently-loaded style.  Returns nullptr if
        nothing is loaded.  Used by the UI to surface metadata (name, BPM,
        time signature) after a successful loadStyle(). */
    const Betel::StyleData* getCurrentStyle() const noexcept { return currentStyle.get(); }

    // ── Solo-routing mask (driven by SOLO ELEMENTS in MainTab) ────────────────
    //
    // 8 bits, one per solo slot.  When the mask is non-zero, processBlock
    // routes every above-split MIDI event to ALL slots whose bit is set
    // (layered play).  When the mask is zero, the legacy single-slot
    // routing via activeSoloSlot is used so the default behaviour is
    // unchanged for users who never touch the SOLO ELEMENTS row.
    void setSoloEnableMask (uint8_t mask) noexcept
    {
        const uint8_t oldMask = soloEnableMask.exchange (mask);
        if (oldMask == mask) return;

        // Release voices on slots that just LEFT the routing set: notes held
        // across the change would otherwise never receive their note-off
        // (note-offs only reach the NEW selection) and hang forever.
        const int base = Betel::SamplePlayerEngine::kNumStyleChannels;
        if (oldMask == 0)
        {
            // Legacy single-slot routing was active; flush it if it's not part
            // of the new selection.
            const int act = activeSoloSlot.load();
            if (mask != 0 && (mask & (1u << act)) == 0)
                engine.allNotesOff (base + act);
        }
        else
        {
            const uint8_t removed = (uint8_t) (oldMask & ~mask);
            for (int s = 0; s < 8; ++s)
                if (removed & (1u << s))
                    engine.allNotesOff (base + s);
        }
    }
    uint8_t getSoloEnableMask() const         noexcept { return soloEnableMask.load(); }

    // ── Tempo system (left-panel TEMPO knob + FREE/SYNCED + speed + TAP) ──────
    //
    //   SYNCED : sequencer follows the host clock (or the style's original
    //            BPM in standalone).  The TEMPO knob shows that value and is
    //            locked.
    //   FREE   : sequencer uses the manual BPM set by the knob or by TAP.
    //   Speed  : ×1 / ×2 / ׽ multiplier applied on top of whichever base wins.
    //
    // resetTempoOverride() snaps the manual BPM back to the loaded style's
    // original tempo (or 120 if no style).
    void  tapTempo();
    void  resetTempoOverride() noexcept
    {
        manualBPM.store (currentStyle != nullptr && currentStyle->originalBPM > 0.0f
                             ? currentStyle->originalBPM : 120.0f);
    }

    void  setManualBPM (float bpm) noexcept
        { manualBPM.store (juce::jlimit (30.0f, 300.0f, bpm)); }
    float getManualBPM() const noexcept { return manualBPM.load(); }

    void  setTempoSynced (bool synced) noexcept { tempoSyncedToHost.store (synced); }
    bool  isTempoSynced() const noexcept        { return tempoSyncedToHost.load(); }

    void  setTempoSpeedMult (float m) noexcept  { tempoSpeedMult.store (m); }
    float getTempoSpeedMult() const noexcept    { return tempoSpeedMult.load(); }

    /** The base BPM (before the speed multiplier) the engine resolved on the
        last processBlock — used by the UI to display the live tempo on the
        knob while SYNCED. */
    float getCurrentBaseBPM() const noexcept { return currentBaseBPM.load(); }

    void setComments (const juce::String& c) { comments = c; }
    juce::String getComments() const         { return comments; }

    /** Audition a single drum key on an engine channel (one-shot). */
    void auditionDrumKey (int engineChannel, int midiKey);

    // ── Instrument-editor piano strip ────────────────────────────────────────
    /** Play / release a note on an engine channel from the editor's on-screen
        keyboard.  Unlike auditionDrumKey (a one-shot), these are a real held
        note pair, so melodic voices sustain until the key is let go. */
    void editorNoteOn  (int engineChannel, int midiNote, int velocity);
    void editorNoteOff (int engineChannel, int midiNote);

    /** Notes currently sounding on an engine channel (128-bit mask, 4 x 32).
        The piano strip polls this to light up whatever the style (or the solo
        MIDI input) is playing on the slot being edited. */
    void getSoundingNotes (int engineChannel, uint32_t out[4]) const;

    /** Fire a one-shot crash-cymbal hit on the DRUMS style slot. */
    void triggerCrash();

    // ── Crash tab settings ────────────────────────────────────────────────────
    void setJumpsConfig (const Betel::JumpsConfig& cfg) noexcept { sequencer.setJumpsConfig (cfg); }
    void setTransitionQuant (int mode) noexcept                  { sequencer.setTransitionQuant (mode); }

    void setFillLength      (int mode) noexcept                  { sequencer.setFillLength (mode); }

    // ── FINISHER (master-bus chain, zero latency) ─────────────────────────────
    // Applied at the very END of processBlock, so it sits after the master fader
    // and the master BOOST.  That ordering is deliberate: the boost now feeds
    // INTO the finisher's ceiling, which turns it from a clipping hazard into a
    // usable loudness control.
    void  setFinisherEnabled   (bool b)    noexcept { finisher.setEnabled (b); }
    bool  getFinisherEnabled   () const    noexcept { return finisher.isEnabled(); }
    void  setFinisherAmount    (float pct) noexcept { finisher.setAmount (pct); }
    float getFinisherAmount    () const    noexcept { return finisher.getAmount(); }
    void  setFinisherCharacter (int c)     noexcept { finisher.setCharacter (c); }
    int   getFinisherCharacter () const    noexcept { return finisher.getCharacter(); }
    float getFinisherGainReductionDb() const noexcept { return finisher.getGainReductionDb(); }

    void  setFinisherParam        (int id, float v) noexcept { finisher.setParam (id, v); }
    float getFinisherParam        (int id) const    noexcept { return finisher.getParam (id); }
    void  setFinisherStageEnabled (int s, bool on)  noexcept { finisher.setStageEnabled (s, on); }
    bool  getFinisherStageEnabled (int s) const     noexcept { return finisher.getStageEnabled (s); }
    void setDawStartFollow  (bool b)  noexcept                   { dawStartFollow.store (b); }
    bool getDawStartFollow  () const  noexcept                   { return dawStartFollow.load(); }
    int  getTransitionQuant () const  noexcept                   { return sequencer.getTransitionQuant(); }
    int  getFillLength      () const  noexcept                   { return sequencer.getFillLength(); }

    void setCrashNoteMask (juce::uint8 m) noexcept { crashNoteMask.store (m); }
    juce::uint8 getCrashNoteMask() const noexcept  { return crashNoteMask.load(); }

    void setCrashOnTransition (bool b) noexcept { crashOnTransition.store (b); }
    bool getCrashOnTransition() const noexcept  { return crashOnTransition.load(); }
    void setAutoCrashEnabled  (bool b) noexcept { autoCrashEnabled.store (b); }
    bool getAutoCrashEnabled() const noexcept   { return autoCrashEnabled.load(); }
    void setAutoCrashEveryN   (int n) noexcept  { autoCrashEveryN.store (juce::jmax (1, n)); }

    /** FIXED crash velocity, 1..127.  Every crash — transition, auto, or CRASH
        NOW — hits at exactly this value: the cymbal is a punctuation mark, and a
        punctuation mark that varies in weight is a different mark. */
    void setCrashVelocity (int v) noexcept { crashVelocity.store (juce::jlimit (1, 127, v)); }
    int  getCrashVelocity() const noexcept { return crashVelocity.load(); }

    /** CRASH BASE GAIN, as a 0..200 position with 100 = UNITY.

        Separate from the velocity on purpose: velocity picks WHICH layer of the
        cymbal sample speaks, so using it as a volume control changes the
        character of the hit as well as its level.  A crash that is right in feel
        but too loud for one style needs a gain, not a softer strike.

        ── THE SCALE ────────────────────────────────────────────────────────────

            0    silence
           75    unity (0 dB) — the default
          100    +6 dB

        Was 0..200 with unity at dead centre and +10 dB at the top.  That put
        three quarters of the travel BELOW unity and gave a boost range nothing
        ever needed: the crash is being trimmed DOWN against a style far more
        often than pushed up, and a hundred positions of attenuation against a
        hundred of boost is the wrong division of a fader.  Unity at 75 puts the
        working range where the work happens and leaves a quarter of the throw
        for the +6 dB that is actually reachable on a cymbal without it clipping
        the mix.

        Two straight segments rather than one dB range, because a single dB scale
        cannot put silence at one end and unity part-way up: silence is -inf dB
        and has no position on it.  BELOW 75 the position is the linear gain
        rescaled (37.5 -> 0.5), ABOVE it the position is dB.  Unity therefore
        lands exactly on the 75 detent, which is the point. */
    void setCrashGainPercent (float pct) noexcept
    { crashGainPercent.store (juce::jlimit (0.0f, 100.0f, pct)); }

    /** Unity, as a position on the scale above.  Named rather than written as
        75 in six places, because the whole point of this change was that the
        number moved. */
    static constexpr float kCrashUnityPct   = 75.0f;
    static constexpr float kCrashMaxBoostDb =  6.0f;

    float getCrashGainPercent() const noexcept { return crashGainPercent.load(); }

    //==========================================================================
    // CRASH BASE UNITY — what the 75 detent is WORTH, in dB.
    //
    // The fader says WHERE you are relative to unity.  This says what unity
    // itself sounds like.  Two different jobs that used to be crossed:
    // double-clicking the GAIN handle opened a dB box that fed setCrashGainDb,
    // which converted the figure into a POSITION and moved the handle.  So the
    // one control meant to calibrate the sound moved the control you had
    // already set, and there was no way to say "this cymbal sample is 4 dB hot"
    // without also throwing away your fader position.
    //
    // Now they are separate, exactly as they are for an instrument (see
    // setInstrumentBaseUnity / SamplePlayerEngine::instrumentGainLinear, which
    // this deliberately mirrors down to the +/-40 dB clamp):
    //
    //     output = base  x  whatever the fader position is worth
    //
    // The position multiplier is 1.0 at 75, so the handle parked on unity gives
    // exactly the base and nothing else.  Move the base and every position
    // moves with it, keeping their relationship: if 75 is now -4 dB, then 37.5
    // is still half of it and 100 is still +6 dB above it.
    //
    // A property of the SAMPLE rather than of the song, which is why it saves
    // into grex_crash.xml alongside the rest of the crash block.
    //==========================================================================
    void setCrashBaseUnityDb (float dB) noexcept
    { crashBaseUnityDb.store (juce::jlimit (-40.0f, 40.0f, dB)); }

    float getCrashBaseUnityDb() const noexcept { return crashBaseUnityDb.load(); }

    /** What the fader POSITION alone is worth, base excluded: 0..1 below the
        unity detent, up to +6 dB above it.  Split out of getCrashGainLinear so
        the base has exactly one place to be applied. */
    float getCrashPositionGain() const noexcept
    {
        const float p = crashGainPercent.load();
        if (p <= kCrashUnityPct) return p / kCrashUnityPct;      // 0 .. 1
        const float dB = ((p - kCrashUnityPct) / (100.0f - kCrashUnityPct))
                         * kCrashMaxBoostDb;
        return std::pow (10.0f, dB / 20.0f);                     // 0 .. +6 dB
    }

    /** Linear multiplier for the crash hit, for whoever renders it.  Base and
        position, and the only place the two combine. */
    float getCrashGainLinear() const noexcept
    {
        const float pos = getCrashPositionGain();

        // Silence stays silence.  Without this a base of +6 dB would lift the
        // bottom of the fader off zero, and a fader whose zero is not silent is
        // broken however good the arithmetic behind it is.
        if (pos <= 0.0f) return 0.0f;

        return juce::Decibels::decibelsToGain (
                   juce::jlimit (-40.0f, 40.0f, crashBaseUnityDb.load())) * pos;
    }

    /** The RESULTING level in dB — base and position together, i.e. what you
        actually hear.  Silence reports -100. */
    float getCrashGainDb() const noexcept
    {
        const float g = getCrashGainLinear();
        return g <= 1.0e-5f ? -100.0f : 20.0f * std::log10 (g);
    }

    /** Read the crash gain out of a state tree, converting a LEGACY value.

        The scale moved (0..200 unity-at-100 -> 0..100 unity-at-75), and the two
        overlap: a stored 75 means 0.75 linear on the old scale and UNITY on the
        new one, so the number alone cannot say which it is.  Hence a new
        property name.  `crashGainPct2` is read as-is; a tree carrying only the
        old `crashGainPct` is converted, which is the only way an existing set
        comes back at the level it was saved at. */
    static float readCrashGainPct (const juce::ValueTree& t, float fallback)
    {
        if (t.hasProperty ("crashGainPct2"))
            return (float) (double) t.getProperty ("crashGainPct2", (double) fallback);

        if (! t.hasProperty ("crashGainPct")) return fallback;

        const float old = juce::jlimit (0.0f, 200.0f,
                              (float) (double) t.getProperty ("crashGainPct", 100.0));

        // Old below unity was linear gain / 100; old above unity was 0.1 dB per
        // step.  Both land on the same audible level on the new scale, and the
        // old +10 dB ceiling clamps to the new +6.
        if (old <= 100.0f) return old * kCrashUnityPct * 0.01f;

        const float dB = juce::jlimit (0.0f, kCrashMaxBoostDb, (old - 100.0f) * 0.1f);
        return kCrashUnityPct + dB * (100.0f - kCrashUnityPct) / kCrashMaxBoostDb;
    }

    /** Set the fader POSITION so the resulting level lands on `dB`.

        The exact inverse of getCrashGainDb, which means it has to take the base
        back out first — otherwise this and its getter would disagree by exactly
        the base the moment anyone calibrated a cymbal.

        NOTE THAT THIS MOVES THE HANDLE, and is therefore NOT what the GAIN
        handle's double-click box calls: that box sets the BASE and leaves the
        handle alone.  Kept for CC / automation, where "put the crash at -6 dB"
        genuinely does mean move the fader. */
    void setCrashGainDb (float dB) noexcept
    {
        if (dB <= -99.0f) { setCrashGainPercent (0.0f); return; }

        const float posDb = dB - juce::jlimit (-40.0f, 40.0f, crashBaseUnityDb.load());
        const float g     = std::pow (10.0f, posDb / 20.0f);

        setCrashGainPercent (
            g <= 1.0f ? g * kCrashUnityPct
                      : kCrashUnityPct + juce::jlimit (0.0f, kCrashMaxBoostDb, posDb)
                                         * (100.0f - kCrashUnityPct) / kCrashMaxBoostDb);
    }

    //==========================================================================
    // CRASH SETTINGS — their own file in the plugin root, like the macros.
    //
    // These five already ride inside a saved SET, but a set is a performance:
    // load someone else's and you inherit their crash choices.  How the cymbal
    // behaves is a property of the INSTALLATION — it belongs with grex_funkey
    // and grex_levels, read once at start, independent of whatever set is open.
    //
    // The set copy is left alone deliberately, and this file is applied AFTER
    // the tab's defaults at startup, so on a fresh install the tab defaults
    // stand and on an established one the file wins.
    //==========================================================================
    bool saveCrashSettings() const
    {
        juce::ValueTree t ("GrexCrash");
        t.setProperty ("crashOnTransition", crashOnTransition.load(), nullptr);
        t.setProperty ("autoCrashEnabled",  autoCrashEnabled .load(), nullptr);
        t.setProperty ("autoCrashEveryN",   autoCrashEveryN  .load(), nullptr);
        t.setProperty ("crashNoteMask",     (int) crashNoteMask.load(), nullptr);
        t.setProperty ("crashVelocity",     crashVelocity    .load(), nullptr);
        t.setProperty ("crashGainPct2",     (double) crashGainPercent.load(), nullptr);
        t.setProperty ("crashBaseUnityDb", (double) crashBaseUnityDb.load(), nullptr);

        const auto f = Betel::GrexPaths::crashSettings();
        f.getParentDirectory().createDirectory();
        if (auto xml = t.createXml()) return xml->writeTo (f);
        return false;
    }

    /** Returns false when there is no file yet, so the caller can leave the
        UI's own defaults in charge instead of overwriting them with nothing. */
    bool loadCrashSettings()
    {
        const auto xml = juce::XmlDocument::parse (Betel::GrexPaths::crashSettings());
        if (xml == nullptr) return false;
        const auto t = juce::ValueTree::fromXml (*xml);
        if (! t.isValid() || ! t.hasType ("GrexCrash")) return false;

        crashOnTransition.store ((bool) t.getProperty ("crashOnTransition", crashOnTransition.load()));
        autoCrashEnabled .store ((bool) t.getProperty ("autoCrashEnabled",  autoCrashEnabled .load()));
        setAutoCrashEveryN ((int) t.getProperty ("autoCrashEveryN", autoCrashEveryN.load()));
        setCrashNoteMask ((juce::uint8) (int) t.getProperty ("crashNoteMask",
                                                            (int) crashNoteMask.load()));
        setCrashVelocity ((int) t.getProperty ("crashVelocity", crashVelocity.load()));
        setCrashGainPercent (readCrashGainPct (t, crashGainPercent.load()));
        setCrashBaseUnityDb ((float) (double) t.getProperty (
                                 "crashBaseUnityDb", (double) crashBaseUnityDb.load()));
        return true;
    }
    int  getAutoCrashEveryN() const noexcept    { return autoCrashEveryN.load(); }

    // ── Program-change control (manifest-pool precedence: pill > Fixed > PC) ──
    // Runtime override of the current style's GM-Controlled / Fixed mode.  Each
    // style reverts to its own header flag on the next load.
    void setStyleFixedMode (bool fixed) noexcept { stylePlayer.setGmControlled (! fixed); }
    bool isStyleFixedMode() const noexcept       { return ! stylePlayer.isGmControlled(); }
    int  getSlotStyleFlag (int slot) const noexcept { return stylePlayer.getSlotStyleFlag (slot); }

    // A pinned reference-voice pill freezes that style slot against the style's
    // program changes.  Solo slots receive no style PCs, so they're ignored.
    void setSlotReferencePinned (bool isSolo, int slot, int refId) noexcept
    {
        if (! isSolo) stylePlayer.setSlotPcLocked (slot, refId >= 0);
    }

    // ── IGNORE PROGRAM CHANGE, per style slot ────────────────────────────────
    // Solo slots receive no style PCs, so the toggle is meaningless there and
    // the setter ignores them rather than pretending to store something.
    void setSlotIgnorePc (bool isSolo, int slot, bool ignore) noexcept
    {
        if (! isSolo) stylePlayer.setSlotPcIgnored (slot, ignore);
    }
    bool isSlotIgnorePc (bool isSolo, int slot) const noexcept
    {
        return isSolo ? false : stylePlayer.isSlotPcIgnored (slot);
    }

    /** The eight toggles as a set child, AND what each fixed slot was fixed TO.

        The flag alone is not enough, and that was the bug: it stops a style
        load re-voicing the slot, but it cannot put back a sound the engine is
        not currently holding.  Load a set into a fresh session and the slot
        held whatever the previous style left there - protected, and wrong.

        A drum slot stores its KIT KEY, which is the only identity a SAMPLED kit
        has: SlotParams carries drumKit.lastLoadedKit, and for a sampled kit that
        is a label ("Pop Latin Kit"), not something anything can reload from.
        A melodic slot stores its instrument flag.

        Absent block = a set written before this existed, and absent means every
        slot follows the style, which is what such a set always effectively
        said. */
    juce::ValueTree captureIgnorePcState() const
    {
        juce::ValueTree t ("IgnorePc");
        // kNumUserStyleSlots, not a literal 8: every SFF source channel is routed
        // onto one of these slots by mapSourceToEngineChannel, so this loop IS
        // the whole style - and if that count ever moves, this moves with it.
        for (int s = 0; s < Betel::StylePlayer::kNumUserStyleSlots; ++s)
        {
            const bool drum = engine.isChannelDrum (s);
            t.setProperty (ignPcId ("s", s), stylePlayer.isSlotPcIgnored (s), nullptr);
            t.setProperty (ignPcId ("k", s),
                           drum ? engine.kitUnityKeyForChannel (s) : -1, nullptr);
            t.setProperty (ignPcId ("f", s),
                           drum ? -1 : engine.getChannelInstrumentFlag (s), nullptr);
        }
        return t;
    }

    void applyIgnorePcState (const juce::ValueTree& t)
    {
        fixedKitKey.fill (-1);
        fixedFlag  .fill (-1);

        // ── AN ABSENT BLOCK MEANS EVERY SLOT FOLLOWS THE STYLE ───────────────
        //
        // captureIgnorePcState says exactly that in its own comment, but this
        // used to `return` here after clearing only fixedKitKey / fixedFlag -
        // and those are what a fixed slot was fixed TO, not the toggles.  The
        // toggles live in StylePlayer and were left exactly as the PREVIOUS set
        // had them.
        //
        // That is worse than merely sticky.  The slot stayed protected from the
        // style while the memory of what to protect it WITH had just been wiped,
        // so it held whatever happened to be there and the style could not
        // correct it - a lock with nothing behind it.
        //
        // So the loop runs either way and simply reads false out of a missing
        // block: setSlotPcIgnored is called for all eight slots on every path.
        const bool haveBlock = t.isValid() && t.hasType ("IgnorePc");

        for (int s = 0; s < Betel::StylePlayer::kNumUserStyleSlots; ++s)
        {
            const bool ign = haveBlock
                          && (bool) t.getProperty (ignPcId ("s", s), false);

            stylePlayer.setSlotPcIgnored (s, ign);
            if (! ign) continue;                       // nothing pinned to restore

            fixedKitKey[(size_t) s] = (int) t.getProperty (ignPcId ("k", s), -1);
            fixedFlag  [(size_t) s] = (int) t.getProperty (ignPcId ("f", s), -1);
        }
    }

    /** Publish what each fixed slot was fixed to.  Call AFTER the style load -
        see captureIgnorePcState for why the toggle alone cannot do this. */
    void restoreIgnorePcSlots()
    {
        for (int s = 0; s < Betel::StylePlayer::kNumUserStyleSlots; ++s)
        {
            if (! stylePlayer.isSlotPcIgnored (s)) continue;

            const int kit = fixedKitKey[(size_t) s];
            if (kit >= 0)
            {
                engine.setDrumChannel (s, true);

                if ((kit & Betel::SamplePlayerEngine::kFullKitUnityBit) != 0)
                {
                    loadFullKitDirect (s, (kit >> 16) & 0xFF,
                                          (kit >>  8) & 0xFF,
                                           kit        & 0xFF);
                }
                else
                {
                    // preload THEN publish, the pair applyVoiceSetup uses: publish
                    // alone is a pointer swap into a pool that may be cold, and
                    // publishDrumKitWithName is the form that syncs the UI name.
                    engine.preloadDrumKitForChannel (s, kit);
                    engine.publishDrumKitWithName   (s, kit);
                }
                continue;
            }

            const int flag = fixedFlag[(size_t) s];
            if (flag >= 0)
            {
                engine.setDrumChannel (s, false);
                engine.selectChannelPreset (s, flag);
            }
        }
    }

    // ── Performance actions (callable from UI or note-control dispatch) ───────
    //
    // These centralise the section/transport logic so the on-screen buttons
    // and the reserved MIDI control-notes (0–35) drive identical behaviour.
    void performVariation (int variButtonIdx);   // 0-15, same indexing as MainTab
    void togglePlayStop();
    void toggleSyncPlay();
    void togglePianoMode() noexcept { pianoMode.store (! pianoMode.load()); }
    void toggleHold();
    void requestRestart() noexcept { restartRequested.store (true); }

    /** Toggle a style element's mute (slot 0-7 = DRUMS..LEAD 2). */
    void toggleStyleElement (int slot);
    /** Select a single solo slot (0-7) and switch to single-slot routing. */
    void selectSoloSlot (int slot) noexcept
    {
        if (slot < 0 || slot >= 8) return;
        activeSoloSlot.store (slot);
        soloEnableMask.store (0);   // 0 = legacy single-slot routing via activeSoloSlot
    }

    // ── Global transpose (left-panel SEMITONE knob) ───────────────────────────
    //
    // Shifts the SOUNDING pitch of everything the player enters: solo notes
    // are transposed directly, and chord-zone notes are transposed before
    // recognition so the band follows into the new key.  The zone boundary
    // (split point) stays on the physical keys.  Range clamped to ±24.
    // SEMITONE knob / ccTranspose.  Range is +/-kMaxTranspose in ALL THREE
    // places that touch it — the knob, this clamp and the CC mapping — which
    // previously disagreed: the knob offered +/-12 while this clamp and the CC
    // reached +/-24, so a CC could drive the engine somewhere the knob could
    // not display and the next knob touch snapped it back.
    //
    // The value is pushed straight to the engine, where every channel applies
    // it inside its own note-on shift.  That is what makes the knob move the
    // STYLE parts and not just the solo voices, and it is why turning it while
    // notes are held is safe (Channel latches the shift per note).
    static constexpr int kMaxTranspose = 12;

    void setGlobalTranspose (int semis) noexcept
    {
        const int v = juce::jlimit (-kMaxTranspose, kMaxTranspose, semis);
        globalTranspose.store (v);
        engine.setGlobalTransposeSemis (v);
    }
    int  getGlobalTranspose() const noexcept { return globalTranspose.load(); }

    // ── Arranger / Piano mode (left-panel selector) ───────────────────────────
    //
    // Arranger: below-split notes drive chord recognition (accompaniment on).
    // Piano:    the entire keyboard plays the solo voice(s); no chord zone,
    //           so the band ignores left-hand input.
    void setPianoMode (bool on) noexcept { pianoMode.store (on); }
    bool isPianoMode() const noexcept    { return pianoMode.load(); }

    void startStylePlayback()                      { sequencer.start(); }
    void stopStylePlayback()
    {
        sequencer.stop();
        stylePlayer.allNotesOff();
    }
    bool isStylePlaying() const noexcept           { return sequencer.isPlaying(); }
    /** Unused today; defaults to the DIRECT jump, matching the VAR buttons.
        Anything that wants the fill on the way asks for it explicitly. */
    void selectStyleVariation (Betel::StyleVariation v, bool viaFill = false)
    { sequencer.selectVariation (v, viaFill); }
    void triggerStyleIntro    (int idx)            { sequencer.triggerIntro (idx); }
    void triggerStyleEnding   (int idx)            { sequencer.triggerEnding (idx); }
    void triggerStyleFill()                        { sequencer.triggerFill(); }
    void triggerStyleBreak()                       { sequencer.triggerBreak(); }
    const Betel::StyleData* getLoadedStyle() const noexcept { return currentStyle.get(); }

    /** Re-run the loaded style's voice setup.
    
        The style-bus boost, the loudness makeup and the PERC / BASS fader trims
        are all decided ONCE, at style load — so moving any of them in Settings
        changes nothing until the setup runs again.  This is what makes those
        sliders audible while a style is playing instead of only after the user
        reloads it by hand.
    
        Message thread.  Safe to call at any time: applyVoiceSetup is what a
        style load already does, and re-running it on the same style is
        idempotent apart from the levels it recomputes. */
    void reapplyCurrentStyle()
    {
        // refreshLevels, NOT applyVoiceSetup: the full setup reloads every
        // instrument and republishes every drum kit, which is inaudible once but
        // brutal on every tick of a dragged slider.  Only the levels can have
        // changed here, and only the levels are re-pushed.
        if (currentStyle != nullptr)
            stylePlayer.refreshLevels (*currentStyle);
    }

    // ── Engine pass-throughs for blob & preset selection ──────────────────────
    /** Loads a Betelgeuse sound pack.  If `accessCode` is empty the fixed
        Betelgeuse code (kFixedAccessCode = "111222") is used.  Callers that
        truly need to override the code can still pass one explicitly. */
    bool loadBlob(const juce::File& file, const juce::String& accessCode = {})
    {
        const juce::String code = accessCode.isEmpty() ? juce::String (kFixedAccessCode)
                                                       : accessCode;
        lastBlobAccessCode = code;
        return engine.loadBlob(file, code);
    }
    bool isBlobLoaded() const                          { return engine.isBlobLoaded(); }
    std::vector<juce::String> getBlobPresetNames() const
    {
        return engine.getBlobPresetNames();
    }
    void setSoloPreset(int slot, int presetIndex)
    {
        const int idx = juce::jlimit(0, Betel::SamplePlayerEngine::kNumSoloChannels - 1, slot);
        engine.selectChannelPreset(Betel::SamplePlayerEngine::kNumStyleChannels + idx, presetIndex);
    }

    // ── Per-instrument sound library (Grex small-blob model) ──────────────────
    /** Rescan every installed pack, then the presets that belong to them.

        Was setSoundLibraryFolder(folder) - one folder, one library.  A caller
        no longer picks the folder: the three pack roots are the folder manager's
        to know, and a pack the user has not installed is simply absent.

        Empty accessCode -> fixed Betelgeuse code. */
    void rescanSoundLibrary (const juce::String& accessCode = {})
    {
        const juce::String code = accessCode.isEmpty() ? juce::String (kFixedAccessCode)
                                                       : accessCode;

        engine.setSoundLibraryFolders (folderManager.getGmSoundsFolder(),
                                       folderManager.getWorldSoundsFolder(),
                                       folderManager.getOrientalSoundsFolder(),
                                       code);

        // Each pack carries its own instruments_presets, so the preset scan has
        // to follow the library scan and cover all three.
        engine.setInstrumentPresetFolders      (allInstrumentPresetsFolders());
        engine.setStyleInstrumentPresetFolders (allStyleInstrumentPresetsFolders());
    }
    int              getSoundLibraryCount() const   { return engine.getSoundLibraryCount(); }
    std::vector<int> getInstrumentFlags()   const   { return engine.getInstrumentFlags(); }
    juce::String     getInstrumentName (int flag) const { return engine.getInstrumentName (flag); }

    // ── THE OPTIONAL SOUND PACKS ─────────────────────────────────────────────
    // pack: 0 = GM, 1 = WORLD, 2 = ORIENTAL (SamplePlayerEngine::SoundPack).
    // Both return empty for a pack the user has not installed, which is a
    // normal state and not an error.
    /** Which pack a sound came from - 0 GM, 1 WORLD, 2 ORIENTAL.  Answers GM
        for a flag the library does not know, which is what an empty slot and a
        plain GM sound both want. */
    int getInstrumentPack (int flag) const { return engine.getInstrumentPack (flag); }

    juce::StringArray getPackCategories (int pack) const
    {
        return engine.getPackCategories (pack);
    }

    std::vector<std::pair<int, juce::String>>
        getPackInstruments (int pack, const juce::String& category) const
    {
        return engine.getPackInstruments (pack, category);
    }
    bool             hasInstrumentFlag (int flag) const { return engine.hasInstrumentFlag (flag); }

    // ── Per-slot instrument substitution (style slot 0..7) ────────────────────
    // Override what a style slot plays and ignore the style's program changes;
    // persisted in the set (GlobalState).  flag < 0 clears the override.
    void setSlotSubstitution   (int styleSlot, int flag) { stylePlayer.setSlotSubstitution (styleSlot, flag); }
    void clearSlotSubstitution (int styleSlot)           { stylePlayer.clearSlotSubstitution (styleSlot); }
    int  getSlotSubstitution   (int styleSlot) const     { return stylePlayer.getSlotSubstitution (styleSlot); }

    // Per-channel allowed-notes window (octave-fold applied to a style slot's
    // final pitch at dispatch time).  Forwards to StylePlayer; the window is
    // owned per slot in SlotParams and persisted with the slot state.  Solo /
    // out-of-range channels are ignored by StylePlayer::setNoteRange.
    void setChannelNoteRange (int engineCh, bool on, int lo, int hi) noexcept
    {
        stylePlayer.setNoteRange (engineCh, on, lo, hi);
    }

    /** Hidden octave bias for a channel (see Channel::octaveBias): the engine
        plays +N octaves while the instrument's OCTAVE slider still reads 0. */
    void setChannelOctaveBias (int engineCh, int octaves) noexcept
    {
        engine.setChannelOctaveBias (engineCh, octaves);
    }

    /** PITCH BEND RANGE — SOLO CHANNELS ONLY.
        
        The wheel already reaches nothing else: processBlock routes
        isPitchWheel() through forEachSoloChannel, so the style slots have never
        seen it.  This pushes the range to the same set of channels, which keeps
        the setting and the signal path describing the same thing.
        
        The style's OWN pitch-bend events (0xE0 in the style data) are a
        different animal and still play on style slots at StylePlayer's fixed
        kStylePitchBendSemis — that is the composer's writing, not the player's
        wheel, and this slider deliberately does not touch it. */
    void setSoloPitchBendRange (int semitones)
    {
        const float s = (float) juce::jlimit (1, 12, semitones);

        // ALL EIGHT solo channels, not just the enabled ones.  processBlock's
        // forEachSoloChannel walks the live solo MASK, which changes while the
        // player works; the RANGE is a property of the wheel and has to be
        // already correct on a slot the moment it is switched on.  Setting it
        // on a silent channel costs one atomic store.
        for (int i = 0; i < 8; ++i)
            engine.setChannelPitchBendRange (
                Betel::SamplePlayerEngine::kNumStyleChannels + i, s);
    }

    /** LOW VELOCITY RESPONSE — SOLO CHANNELS ONLY, for the same reason the
        bend range is.

        A style slot's velocities are AUTHORED: they came out of the style file
        the way its writer meant them, and lifting them would rewrite the
        arrangement's dynamics rather than compensate for anybody's keyboard.
        The complaint this answers is about notes the PLAYER strikes, so it
        reaches exactly the channels the player strikes.

        All eight, enabled or not, for the reason given above: the setting is a
        property of the keyboard and has to be already correct on a slot the
        moment it is switched on. */
    void setSoloLowVelBoost (int amount)
    {
        Betel::MasterSettings::get().setLowVelBoost (amount);
        const float curve = Betel::MasterSettings::get().getLowVelCurve();

        for (int i = 0; i < 8; ++i)
            engine.setChannelGlobalVelCurve (
                Betel::SamplePlayerEngine::kNumStyleChannels + i, curve);
    }

    /** Re-push whatever the master file currently says.  Called at startup
        beside the other master pushes, so a saved value is live before the
        first note rather than after the first slider move. */
    void pushSoloLowVelBoost()
    {
        const float curve = Betel::MasterSettings::get().getLowVelCurve();
        for (int i = 0; i < 8; ++i)
            engine.setChannelGlobalVelCurve (
                Betel::SamplePlayerEngine::kNumStyleChannels + i, curve);
    }

    /** IGNORE PRESET CHANGES for one slot.  slot 0..7; isSolo picks which bank.

        Pushed straight to the channel so it covers every route a preset can
        take into the audio - set restore, style program change, runtime PC,
        instrument selector - not just the two the sound editor knows about. */
    void setSlotIgnorePresetParams (int slot, bool isSolo, bool ignore)
    {
        const int s = juce::jlimit (0, 7, slot);
        const int ch = isSolo ? Betel::SamplePlayerEngine::kNumStyleChannels + s : s;
        engine.setChannelIgnorePresetParams (ch, ignore);
    }

    void setChannelSweetener (int channelIdx, const SweetenerParams& sw)
    {
        engine.setChannelSweetener (channelIdx, sw);
    }

    /** Live sweetener gain reduction for one channel — { SOFTEN, PEAK, TAME,
        ROUND } in dB.  Polled by the editor's meters on the UI timer, exactly
        as the Finisher's GR meter is. */
    void getChannelSweetenerGr (int channelIdx, float* gr4) const
    {
        engine.getChannelSweetenerGr (channelIdx, gr4);
    }

    void applyChannelParams (int channelIdx, const Betel::Channel::ChannelParams& p)
    {
        engine.applyChannelParams (channelIdx, p);
    }

    //==========================================================================
    // TWO-HANDLE BAND FILTER — per channel, and immune to program changes.
    //
    // Takes the 0..1 normalised edges the UI and the set both speak, and maps
    // them with the same log law everything else uses (20 * 1000^norm), so the
    // slider position, the .bset property and the engine cutoff always agree.
    //
    // Called from exactly two places: the sound editor when the handles move,
    // and the set restore that re-pushes every slot.  A style program change
    // reaches applyChannelParams, never this, which is the whole point — the
    // filter is a property of the CHANNEL STRIP, not of the sound that happens
    // to be loaded on it.
    //==========================================================================
    void setChannelBandFilterNorm (int channelIdx, float hpNorm, float lpNorm)
    {
        const auto toHz = [] (float n)
        { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, n)); };

        engine.setChannelBandFilter (channelIdx, toHz (hpNorm), toHz (lpNorm));
    }

    /** The edges currently in force on a channel, back in 0..1 — the inverse of
        the law above, for anything that needs to read the strip rather than the
        editor's copy. */
    float getChannelBandFilterHpNorm (int channelIdx) const
    {
        return juce::jlimit (0.0f, 1.0f,
                   std::log (juce::jmax (20.0f, engine.getChannelBandFilterHpHz (channelIdx))
                             / 20.0f) / std::log (1000.0f));
    }

    float getChannelBandFilterLpNorm (int channelIdx) const
    {
        return juce::jlimit (0.0f, 1.0f,
                   std::log (juce::jmax (20.0f, engine.getChannelBandFilterLpHz (channelIdx))
                             / 20.0f) / std::log (1000.0f));
    }

    // ── Drum-kit registry (Phase 3) ───────────────────────────────────────────
    Betel::DrumKitRegistry*       getDrumKitRegistry()       { return &drumKitRegistry; }
    const Betel::DrumKitRegistry* getDrumKitRegistry() const { return &drumKitRegistry; }

    /** Scan a folder for *_kit.frb blobs and add them to the catalog.  Empty
        accessCode → use the fixed Betelgeuse code ("111222"). */
    int scanDrumKitFolder (const juce::File& folder, const juce::String& accessCode = {})
    {
        const juce::String code = accessCode.isEmpty() ? juce::String (kFixedAccessCode)
                                                       : accessCode;

        // The SAMPLED-KIT folders live in the same place, one level down:
        //   sounds/drums/126-000-036_Arabic Kit/
        // Scanned here so both catalogs are always built from the same root and
        // cannot disagree about where the drums are.
        engine.getFullKitMap().scan (folder);

        return drumKitRegistry.scanFolder (folder, code);
    }

    //==========================================================================
    // THE SAMPLED KIT GRID (Sounds tab)
    //==========================================================================

    /** Every sampled kit on disk: msb, lsb, pc, readable label. */
    std::vector<std::tuple<int,int,int,juce::String>> getFullKitList() const
    {
        std::vector<std::tuple<int,int,int,juce::String>> out;
        for (const auto& e : engine.getFullKitMap().all())
            out.push_back ({ e.msb, e.lsb, e.pc, Betel::FullKitMap::prettyLabel (e) });
        return out;
    }

    /** "msb/lsb/pc" of the sampled kit on this channel, "" when composed. */
    juce::String getFullKitOnChannel (int channelIdx) const
    { return engine.getFullKitOnChannel (channelIdx); }

    /** THE USER picked a kit from the grid, so load it unconditionally.

        The DRUMS-versus-PERC rule exists to stop a STYLE's request replacing a
        composed kit behind the user's back; an explicit choice is never that. */
    bool loadFullKitDirect (int channelIdx, int msb, int lsb, int pc)
    {
        return engine.loadFullKitOnChannel (channelIdx, msb, lsb, pc,
                                            juce::String (kFixedAccessCode));
    }

    /** Full sampled kit for this request, loaded whole - or false if the policy
        says composed, or the folder holds nothing readable. */
    bool tryLoadFullKit (int channelIdx, int msb, int lsb, int pc, bool isPercSlot)
    {
        if (! engine.getFullKitMap().shouldLoadFull (msb, lsb, pc, isPercSlot))
            return false;
        return engine.loadFullKitOnChannel (channelIdx, msb, lsb, pc,
                                            juce::String (kFixedAccessCode));
    }

    /** Which COMPOSED kit answers a request nothing else can.  -1 means "no
        opinion, use the existing nearest-family logic". */
    int composedFallbackPc (int msb, int lsb, int pc) const
    {
        // Betel-qualified: BetelgeuseProcessor is global, FullKitMap is not.
        const int fb = Betel::FullKitMap::composedFallbackPc (msb, lsb, pc);
        if (fb != Betel::FullKitMap::kFallbackArabic) return fb;

        // The Arabic family: answer with Arabic if the library has it, else let
        // the existing percussion handling decide.  Never a bank-127 drum kit.
        return engine.getFullKitMap().find (126, 0, 36) != nullptr ? 36 : -1;
    }

    void setDrumChannel (int channelIdx, bool b)            { engine.setDrumChannel (channelIdx, b); }

    // ── Engine-state mirror (read by MainComponent's UI timer) ──────────────
    int  getChannelInstrumentFlag (int channelIdx) const { return engine.getChannelInstrumentFlag (channelIdx); }
    bool isChannelDrum            (int channelIdx) const { return engine.isChannelDrum (channelIdx); }
    juce::String getChannelDrumKitName (int channelIdx) const { return engine.getChannelDrumKitName (channelIdx); }

    // instruments_presets lives beside the sounds folder
    // (<project>\Grex VSTI\instruments_presets).  SOLO set: .ins + .drm.
    //==========================================================================
    // PRESETS NOW LIVE INSIDE THE PACK THEY DESCRIBE.
    //
    // This used to derive one shared folder from the library's parent.  With
    // three packs that answer is wrong in both directions: a WORLD sound's
    // preset must be WRITTEN into the world pack (so deleting the pack takes
    // its voicing with it), and READING has to cover all three at once.
    //
    // So the getter takes the FLAG and routes by the pack the engine says that
    // flag came from.  A negative flag - "no particular instrument" - answers
    // GM, which is the pack that is always installed.
    //==========================================================================
    juce::File getInstrumentPresetsFolder (int flag = -1) const
    {
        return folderManager.getPackPresetsFolder (
                   flag < 0 ? 0 : engine.getInstrumentPack (flag));
    }

    /** All three, for the read side. */
    std::vector<juce::File> allInstrumentPresetsFolders() const
    {
        return { folderManager.getPackPresetsFolder (0),
                 folderManager.getPackPresetsFolder (1),
                 folderManager.getPackPresetsFolder (2) };
    }

    std::vector<juce::File> allStyleInstrumentPresetsFolders() const
    {
        return { folderManager.getPackStylePresetsFolder (0),
                 folderManager.getPackStylePresetsFolder (1),
                 folderManager.getPackStylePresetsFolder (2) };
    }

    // style_instruments_presets sits beside it.  STYLE set: .sins (melodic
    // only — kits are not split, see InstrumentPreset.h).  Created lazily by
    // the first style-side "Save as Default"; absent until then, which is why
    // a style channel falls back to the .ins.
    juce::File getStyleInstrumentPresetsFolder (int flag = -1) const
    {
        return folderManager.getPackStylePresetsFolder (
                   flag < 0 ? 0 : engine.getInstrumentPack (flag));
    }

    /** Developer base unity for the instrument on this channel, in dB.
    
        Written to BOTH sets — instruments_presets\NNN.ins AND
        style_instruments_presets\NNN.sins — because a base is one value per
        INSTRUMENT, not per context: it describes how hot the sample itself is,
        which does not change because a style is playing it.  Writing one file
        would let the two drift and the correction would depend on where the
        sound happened to load.
    
        Drum kits write only the .drm, which is their single set. */
    bool setInstrumentBaseUnity (int channelIdx, float baseUnityDb)
    {
        // ── DRUM KITS: THE SET OWNS IT, AND EACH KIT HAS ITS OWN ────────────
        //
        // This used to write a .drm, resolving the kit with
        // getChannelDrumKitName().getIntValue().  Two things were wrong with
        // that and they compounded:
        //
        //   * a SAMPLED kit's name is a label, so "Pop Latin Kit".getIntValue()
        //     is 0 - every sampled kit calibrated itself into 000 Standard's
        //     .drm and read Standard's value back.  One number for seventeen
        //     kits, and it belonged to a composed one.
        //   * a .drm is global, so a calibration made for one song followed the
        //     kit into every other song that used it.
        //
        // Now: keyed per kit (composed by PC, sampled by bank/program, and the
        // two key spaces cannot overlap), held in the engine, and written out
        // with the set.  No file, no rescan, no scheduled reload - the set is
        // the owner and SAVE SET is what persists it.
        if (engine.isChannelDrum (channelIdx))
        {
            const int unityKey = engine.kitUnityKeyForChannel (channelIdx);
            if (unityKey < 0) return false;              // nothing published yet

            engine.setKitUnityDb (unityKey, baseUnityDb);

            // EVERY channel holding this same kit, not just the edited one - a
            // style routinely puts one kit on DRUMS and PERC, and calibrating
            // one while the other kept the old base is the split-brain the live
            // reload exists to prevent.
            for (int ch = 0; ch < Betel::SamplePlayerEngine::kNumChannels; ++ch)
                if (engine.isChannelDrum (ch)
                    && engine.kitUnityKeyForChannel (ch) == unityKey)
                    engine.applyKitUnityToChannel (ch, unityKey);

            return true;
        }

        const int flag = engine.getChannelInstrumentFlag (channelIdx);
        if (flag < 0) return false;

        const juce::String name = engine.getInstrumentName (flag);
        const juce::String frb  = engine.getInstrumentFile (flag).getFileName();

        const auto a = Betel::InstrumentPresetIO::writeBaseUnity (
                           getInstrumentPresetsFolder (flag), flag, name, frb,
                           baseUnityDb, /*isDrum*/ false, /*isStyle*/ false);
        const auto b = Betel::InstrumentPresetIO::writeBaseUnity (
                           getStyleInstrumentPresetsFolder (flag), flag, name, frb,
                           baseUnityDb, /*isDrum*/ false, /*isStyle*/ true);

        rescanInstrumentPresets();

        // Take effect on the edited channel immediately, then let the scheduled
        // reload carry it to every OTHER channel holding the same sound.
        engine.setChannelInstrumentGainPercent (channelIdx, 100.0f, baseUnityDb);
        schedulePresetReload (flag, /*isDrumKit*/ false);

        return a != juce::File() && b != juce::File();
    }

    /** The base unity currently on file for this channel's instrument, so the
        dialog opens showing what is actually in force. */
    float getInstrumentBaseUnity (int channelIdx) const
    {
        SlotParams sp;

        // Drum channels read the SET's per-kit map, which is the owner now.  A
        // legacy .drm base is still honoured at LOAD time (see
        // applyDrumPresetGain) but is not what the dialog edits, so it is not
        // what the dialog shows.
        if (engine.isChannelDrum (channelIdx))
        {
            float db = 0.0f;
            engine.getKitUnityDb (engine.kitUnityKeyForChannel (channelIdx), db);
            return db;
        }

        if (getPresetParamsForChannel (channelIdx, sp)) return sp.baseUnityDb;
        const int flag = engine.getChannelInstrumentFlag (channelIdx);
        if (flag >= 0 && engine.presetParamsForAnyScope (channelIdx, flag, sp))
            return sp.baseUnityDb;
        return 0.0f;
    }

    /** The saved voice governing whatever is loaded on this channel, if any.
        Lets the editor display what the engine is really playing. */
    bool getPresetParamsForChannel (int channelIdx, SlotParams& out) const
    {
        const int flag = engine.getChannelInstrumentFlag (channelIdx);
        if (flag < 0) return false;
        return engine.presetParamsFor (channelIdx, flag, out);
    }

    //==========================================================================
    // LIVE CALIBRATION RELOAD.
    //
    // A save is only half of calibrating by ear — the other half is HEARING it,
    // on every channel playing that sound, without stopping.  So a save is
    // followed by a short delay, a re-scan of both preset folders, and a re-push
    // to every channel currently holding that instrument.
    //
    // The delay exists because the write and the read are the same file: the XML
    // has to be closed and flushed before a re-scan can see it, and a save is
    // usually one of several (a knob moved, then another) — coalescing them into
    // one reload keeps a burst of edits from triggering a burst of re-scans.
    //
    // Scheduled, not immediate, so the button returns at once and the audio
    // thread never waits on a file.
    //==========================================================================
    static constexpr int kPresetReloadDelayMs = 500;

    /** After a save: re-read from disk and apply to every channel holding this
        sound.  isDrumKit selects which set the flag belongs to. */
    void schedulePresetReload (int flag, bool isDrumKit)
    {
        if (flag < 0) return;

        auto alive = aliveFlag;                       // survives a closed editor
        juce::Timer::callAfterDelay (kPresetReloadDelayMs,
            [this, alive, flag, isDrumKit]
            {
                if (! *alive) return;                 // processor went away

                rescanInstrumentPresets();            // pick the file up off disk

                if (isDrumKit) engine.reapplyDrumPreset    (flag);
                else           engine.reapplyMelodicPreset (flag);

                // Let the editor catch up: every slot showing this sound should
                // now display what the file says, not what it said before.
                if (onPresetReloaded) onPresetReloaded (flag, isDrumKit);
            });
    }

    /** Fired after a scheduled reload has been applied to the engine. */
    std::function<void(int flag, bool isDrumKit)> onPresetReloaded;

    /** Per-sound calibration trim for one engine channel, as a percent of unity
        (100 = unity).  Driven by the editor's GAIN slider; sound loads set it
        themselves from the governing preset. */
    void setChannelInstrumentGainPercent (int channelIdx, float gainPercent)
    {
        engine.setChannelInstrumentGainPercent (channelIdx, gainPercent);
    }

    /** The same, keeping a base the caller does not want to disturb.  The GAIN
        slider goes through here: the 2-argument form defaults the base to 0, so
        one nudge of the slider used to wipe the calibration the double-click had
        just set. */
    void setChannelInstrumentGainPercent (int channelIdx, float gainPercent,
                                          float baseUnityDb)
    {
        engine.setChannelInstrumentGainPercent (channelIdx, gainPercent, baseUnityDb);
    }

    float getChannelBaseUnityDb (int channelIdx) const
    {
        return engine.getChannelBaseUnityDb (channelIdx);
    }

    /** Which kit a drum channel is holding, and what to call it.  The UI mirror
        watches the KEY, not the name - see kitNameForUnityKey. */
    int getChannelKitKey (int channelIdx) const noexcept
    {
        return engine.kitUnityKeyForChannel (channelIdx);
    }
    juce::String getKitNameForKitKey (int unityKey) const
    {
        return engine.kitNameForUnityKey (unityKey);
    }

    //==========================================================================
    // PER-KIT BASE UNITY AS A SET BLOCK
    //
    // One property per kit, named "k<unityKey>" because a ValueTree property is
    // a juce::Identifier and cannot hold the '/' a bank/program key would want.
    // Absent block = a set written before this existed, and absent means "no
    // opinion": whatever the .drm or the load path applied stands.
    //==========================================================================
    juce::ValueTree captureKitUnityState() const
    {
        juce::ValueTree t ("KitUnity");
        for (const auto& kv : engine.allKitUnity())
            t.setProperty (juce::Identifier ("k" + juce::String (kv.first)),
                           (double) kv.second, nullptr);
        return t;
    }

    void applyKitUnityState (const juce::ValueTree& t)
    {
        if (! t.isValid() || ! t.hasType ("KitUnity")) return;

        engine.clearKitUnity();
        for (int i = 0; i < t.getNumProperties(); ++i)
        {
            const auto id = t.getPropertyName (i);
            const auto s  = id.toString();
            if (! s.startsWith ("k")) continue;
            engine.setKitUnityDb (s.substring (1).getIntValue(),
                                  (float) (double) t.getProperty (id));
        }

        // Anything already loaded takes its new base at once, trim untouched.
        for (int ch = 0; ch < Betel::SamplePlayerEngine::kNumChannels; ++ch)
            if (engine.isChannelDrum (ch))
                engine.applyKitUnityToChannel (ch, engine.kitUnityKeyForChannel (ch));
    }

    /** Re-scan both preset folders.  Called after a "Save as Default" so the
        file just written governs the very next load instead of only taking
        effect at the next restart. */
    void rescanInstrumentPresets()
    {
        engine.setInstrumentPresetFolders      (allInstrumentPresetsFolders());
        engine.setStyleInstrumentPresetFolders (allStyleInstrumentPresetsFolders());
    }

    /** "Save as Default": write the channel's currently-loaded instrument —
        source .frb name + the supplied SlotParams snapshot — to whichever set
        this CHANNEL belongs to:

            style channel (0..15)  -> style_instruments_presets\NNN-Name.sins
            solo  channel (16..23) -> instruments_presets\NNN-Name.ins

        So saving from a style slot calibrates the sound for arrangements, and
        saving the same sound from a solo slot calibrates it for the right hand,
        without either overwriting the other.  Drum kits are never split — they
        always write instruments_presets\NNN-Family.drm.

        Returns the written file (invalid on failure). */
    juce::File saveChannelAsDefault (int channelIdx, const SlotParams& params)
    {
        if (engine.isChannelDrum (channelIdx))
        {
            // Drum default preset: the whole kit lives in params.drumKit.  Key
            // it by the kit's numeric flag ("000" -> 0) so a matching program
            // change reloads it; name it by family for a readable file name.
            juce::String kit = params.drumKit.lastLoadedKit;
            if (kit.isEmpty()) kit = engine.getChannelDrumKitName (channelIdx);
            const int flag = kit.getIntValue();

            const auto out = Betel::InstrumentPresetIO::write (
                                 getInstrumentPresetsFolder (flag),
                                 flag, drumFamilyName (flag),
                                 /*isDrum*/ true, /*frb*/ {}, params,
                                 /*isStyle*/ false);
            rescanInstrumentPresets();
            schedulePresetReload (flag, /*isDrumKit*/ true);
            return out;
        }

        const int flag = engine.getChannelInstrumentFlag (channelIdx);
        if (flag < 0) return {};                       // nothing loaded on this channel

        const juce::String name = engine.getInstrumentName (flag);
        const juce::File   frb  = engine.getInstrumentFile (flag);

        // .sins IS CANCELLED — a melodic STYLE slot has no default file any
        // more.  Its params live in the SET, which is per style and therefore
        // strictly better at the job the .sins was invented for; writing one
        // here would put a second owner back on the same values, and writing a
        // plain .ins instead would let a style-slot edit silently redefine the
        // right hand's version of that instrument.
        //
        // FAST SAVE is the save for a style slot.  Refuse here and say so.
        if (Betel::SamplePlayerEngine::isStyleChannel (channelIdx))
            return {};

        const auto out = Betel::InstrumentPresetIO::write (
                             getInstrumentPresetsFolder (flag),
                             flag, name, /*isDrum*/ false,
                             frb.getFileName(), params,
                             /*isStyle*/ false);
        rescanInstrumentPresets();
        schedulePresetReload (flag, /*isDrumKit*/ false);
        return out;
    }

    /** Readable family name for a drum kit flag (matches the default PC map). */
    static juce::String drumFamilyName (int flag)
    {
        switch (flag)
        {
            case 0:  return "Standard";
            case 8:  return "Room";
            case 16: return "Power";
            case 24: return "Electronic";
            case 25: return "TR-808";
            case 32: return "Jazz";
            case 40: return "Brush";
            case 48: return "Orchestra";
            case 56: return "SFX";
            default: return "Kit";
        }
    }

    void applyDrumKitToChannel (int channelIdx, const DrumKitParams& kit)
    {
        engine.applyDrumKitToChannel (channelIdx, kit);
    }

    void setDrumKeyParams (int channelIdx, int midiKey, const DrumElementParams& p)
    {
        engine.setDrumKeyParams (channelIdx, midiKey, p);
    }

    /** Push the kit-wide FX bus params onto a drum channel. */
    void applyDrumKitFx (int channelIdx, const DrumKitFxParams& fx)
    {
        engine.applyDrumKitFx (channelIdx, fx);
    }

    /** Live per-key FX-send update — RT-safe. */
    void setDrumKeyFxSend (int channelIdx, int midiKey, float sendNorm)
    {
        engine.setDrumKeyFxSend (channelIdx, midiKey, sendNorm);
    }

    void setDrumKitPCMapping    (int pcValue, const juce::String& kitName) { engine.setDrumKitPCMapping (pcValue, kitName); }
    void removeDrumKitPCMapping (int pcValue)                              { engine.removeDrumKitPCMapping (pcValue); }
    void clearAllDrumKitPCMappings()                                       { engine.clearAllDrumKitPCMappings(); }
    juce::String getDrumKitNameForPC (int pcValue) const                   { return engine.getDrumKitNameForPC (pcValue); }

    // Arabic / microtonal scale tuning (per channel).
    void  setChannelScaleTuningCents (int channelIdx, int noteClass, float cents) { engine.setChannelScaleTuningCents (channelIdx, noteClass, cents); }
    float getChannelScaleTuningCents (int channelIdx, int noteClass) const        { return engine.getChannelScaleTuningCents (channelIdx, noteClass); }
    void  clearChannelScaleTuning    (int channelIdx)                             { engine.clearChannelScaleTuning (channelIdx); }

    // Click library (per-channel transient attack layer).
    bool loadClickSample   (int channelIdx, const juce::File& f)  { return engine.loadClickSample(channelIdx, f); }
    void clearClickSample  (int channelIdx)                       { engine.clearClickSample(channelIdx); }
    void setClickEnabled   (int channelIdx, bool enabled)         { engine.setClickEnabled(channelIdx, enabled); }
    void setClickVolume    (int channelIdx, float volume)         { engine.setClickVolume(channelIdx, volume); }
    void setClickDecayMs   (int channelIdx, float decayMs)        { engine.setClickDecayMs(channelIdx, decayMs); }

    // Mixer
    // Master volume now passes through a master BOOST stage: the mixer's master
    // fader (and the master-volume CC) set the PRE-boost gain here, and
    // applyMasterOut() multiplies in the currently selected boost (0/+3/+6/+9/
    // +12 dB) before it reaches the engine.  Both the fader and the CC honour
    // the boost, and it survives the editor being recreated because the boost
    // lives on the processor, not the UI.
    void setMasterVolume   (float linearGain)
    {
        masterFaderGain.store (juce::jmax (0.0f, linearGain));
        applyMasterOut();
    }
    /** Master output boost in dB (default +6), applied on top of the master
        fader — see setMasterVolume / applyMasterOut.  Driven by the mixer's
        master tickboxes (0/+3/+6/+9/+12). */
    void setMasterBoostDb  (float db)
    {
        masterBoostDb.store (db);
        applyMasterOut();
    }
    float getMasterBoostDb () const noexcept                       { return masterBoostDb.load(); }
    void setStyleVolume    (float linearGain)                     { engine.setStyleBusGain(linearGain); }
    float getStyleVolume   () const                               { return engine.getStyleBusGain(); }
    void setRightHandVolume(float linearGain)                     { engine.setSoloBusGain(linearGain); }

    //==========================================================================
    // SOLO BUS BASE UNITY -- what the SOLO VOLUME fader's unity detent is worth.
    //
    // A GLOBAL setting, stored in grex_master.xml and never in a set: it
    // describes this install's balance between the two hands, which does not
    // change because a different song was loaded.  MasterSettings owns the
    // number; this pushes it at the engine.
    //
    // The fader is untouched -- setSoloBusGain still carries exactly what the
    // mixer shows, and the two are multiplied once at render.
    //==========================================================================
    void setSoloBaseUnityDb (float dB)
    {
        Betel::MasterSettings::get().setSoloBaseUnityDb (dB);
        engine.setSoloBusBaseUnity (Betel::MasterSettings::get().getSoloBaseUnityGain());
    }

    float getSoloBaseUnityDb() const
    { return Betel::MasterSettings::get().getSoloBaseUnityDb(); }

    /** Push the stored figure at the engine without rewriting the file.  Called
        once at startup, right after MasterSettings::load. */
    void applyStoredSoloBaseUnity()
    { engine.setSoloBusBaseUnity (Betel::MasterSettings::get().getSoloBaseUnityGain()); }
    float getRightHandVolume() const                              { return engine.getSoloBusGain(); }
    /** The master fader gain BEFORE the boost is folded in — i.e. what the
        mixer's master fader shows.  applyMasterOut() multiplies this by the
        boost on its way to the engine, so reading the engine back would return
        the product and a save/restore round trip would compound it. */
    float getMasterVolume  () const noexcept                      { return masterFaderGain.load(); }
    void setChannelVolume  (int channelIdx, float linearGain)     { engine.setChannelVolume(channelIdx, linearGain); }
    float getChannelVolume (int channelIdx) const                 { return engine.getChannelVolume(channelIdx); }

    //==========================================================================
    // USER SFZ — per channel, and it travels in the SET.
    //
    // A loaded SFZ is a decision about THIS song's sound, exactly like a slot's
    // instrument or its mixer fader, so it belongs with them.  Without this the
    // feature would be a per-session novelty: load your bouzouki, save the set,
    // reopen tomorrow and it is gone.
    //
    // Stored as an absolute path plus the bare file name.  The path is what
    // normally resolves; the name is the fallback, looked up in <root>/sfz, so
    // a set shared with someone else - or your own library moved to another
    // drive - still finds the instrument if they have it.
    //==========================================================================
    juce::File sfzFallbackFolder() const
    { return Betel::GrexPaths::root().getChildFile ("sfz"); }

    bool loadSfzOnChannel (int channelIdx, const juce::File& f,
                           juce::String& errorOut, juce::String& nameOut)
    { return engine.loadSfzOnChannel (channelIdx, f, errorOut, nameOut); }

    void clearSfzOnChannel (int channelIdx) { engine.clearSfzOnChannel (channelIdx); }

    /** The toggle: put the channel to sleep on its blob source, or wake the SFZ
        back up.  Neither loses anything - see SamplePlayerEngine. */
    void sleepSfzOnChannel (int channelIdx) { engine.sleepSfzOnChannel (channelIdx); }
    void wakeSfzOnChannel  (int channelIdx) { engine.wakeSfzOnChannel  (channelIdx); }
    bool hasSfzVoice (int channelIdx) const { return engine.hasSfzVoice (channelIdx); }
    bool         isSfzActive (int channelIdx) const { return engine.isSfzActive (channelIdx); }
    juce::String getSfzName  (int channelIdx) const { return engine.getSfzName (channelIdx); }
    juce::String getSfzPath  (int channelIdx) const { return engine.getSfzPath (channelIdx); }

    /** Drop every user SFZ.

        Called on a style change: the override holds its slot against program
        changes, so without this the previous song's instrument would survive
        into the next style and the arrangement could never take the slot back.
        A set restores its own afterwards - which is also why nothing that is not
        playing is ever stored. */
    void clearAllSfz()
    {
        for (int ch = 0; ch < Betel::SamplePlayerEngine::kNumChannels; ++ch)
            if (engine.isSfzActive (ch)) engine.clearSfzOnChannel (ch);
    }

    juce::ValueTree captureSfzState() const
    {
        juce::ValueTree t ("SfzState");

        auto add = [&] (int ch)
        {
            if (! engine.isSfzActive (ch)) return;
            juce::ValueTree c ("Sfz");
            c.setProperty ("ch",   ch,                    nullptr);
            c.setProperty ("path", engine.getSfzPath (ch), nullptr);
            c.setProperty ("file", juce::File (engine.getSfzPath (ch)).getFileName(), nullptr);
            t.appendChild (c, nullptr);
        };

        // Only the sixteen the user can reach: eight style slots and eight solo.
        for (int i = 0; i < Betel::StylePlayer::kNumUserStyleSlots; ++i) add (i);
        for (int i = 0; i < Betel::SamplePlayerEngine::kNumSoloChannels; ++i)
            add (Betel::SamplePlayerEngine::kNumStyleChannels + i);

        return t;
    }

    /** Restore the set's SFZ assignments.

        A channel NOT named in the block is cleared, so loading a set that has
        no SFZ on CHORD 1 actually removes one left over from the previous set
        rather than leaving it holding the slot for ever. */
    void applySfzState (const juce::ValueTree& t)
    {
        if (! t.isValid() || ! t.hasType ("SfzState")) return;

        std::array<bool, Betel::SamplePlayerEngine::kNumChannels> wanted {};

        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            const auto c = t.getChild (i);
            if (! c.hasType ("Sfz")) continue;

            const int ch = (int) c.getProperty ("ch", -1);
            if (ch < 0 || ch >= Betel::SamplePlayerEngine::kNumChannels) continue;

            juce::File f (c.getProperty ("path", juce::String()).toString());
            if (! f.existsAsFile())
            {
                const auto nm = c.getProperty ("file", juce::String()).toString();
                if (nm.isNotEmpty()) f = sfzFallbackFolder().getChildFile (nm);
            }
            if (! f.existsAsFile()) continue;   // missing library: leave the slot on its style sound

            juce::String err, name;
            if (engine.loadSfzOnChannel (ch, f, err, name))
                wanted[(size_t) ch] = true;
        }

        for (int ch = 0; ch < Betel::SamplePlayerEngine::kNumChannels; ++ch)
            if (! wanted[(size_t) ch] && engine.isSfzActive (ch))
                engine.clearSfzOnChannel (ch);
    }

    //==========================================================================
    // CRASH SNAPSHOT — the whole tab, as a set child.
    //
    // The crash used to live only in grex_crash.xml, one setting for the entire
    // library.  It is not one setting for the entire library: whether a style
    // wants a crash on every transition, which cymbals it uses, how hard and how
    // loud, is a property of THAT arrangement.  A ballad and a rock shuffle want
    // different answers and there is no third answer that suits both.
    //
    // grex_crash.xml stays, demoted to the same role grex_levels.xml has: the
    // TEMPLATE a style gets before anyone has tuned it.
    //==========================================================================
    juce::ValueTree captureCrashState() const
    {
        juce::ValueTree t ("CrashState");
        t.setProperty ("crashOnTransition", crashOnTransition.load(),      nullptr);
        t.setProperty ("autoCrashEnabled",  autoCrashEnabled .load(),      nullptr);
        t.setProperty ("autoCrashEveryN",   autoCrashEveryN  .load(),      nullptr);
        t.setProperty ("crashNoteMask",     (int) crashNoteMask.load(),    nullptr);
        t.setProperty ("crashVelocity",     crashVelocity    .load(),      nullptr);
        t.setProperty ("crashGainPct2",     (double) crashGainPercent.load(), nullptr);
        t.setProperty ("crashBaseUnityDb", (double) crashBaseUnityDb.load(), nullptr);
        return t;
    }

    /** ABSENT MEANS UNCHANGED, like every other apply here — so a set written
        before the crash travelled leaves the crash exactly as it is. */
    void applyCrashState (const juce::ValueTree& t)
    {
        if (! t.isValid() || ! t.hasType ("CrashState")) return;

        crashOnTransition.store ((bool) t.getProperty ("crashOnTransition",
                                                       crashOnTransition.load()));
        autoCrashEnabled .store ((bool) t.getProperty ("autoCrashEnabled",
                                                       autoCrashEnabled.load()));
        setAutoCrashEveryN ((int) t.getProperty ("autoCrashEveryN", autoCrashEveryN.load()));
        setCrashNoteMask ((juce::uint8) (int) t.getProperty ("crashNoteMask",
                                                             (int) crashNoteMask.load()));
        setCrashVelocity ((int) t.getProperty ("crashVelocity", crashVelocity.load()));
        setCrashGainPercent (readCrashGainPct (t, crashGainPercent.load()));
        setCrashBaseUnityDb ((float) (double) t.getProperty (
                                 "crashBaseUnityDb", (double) crashBaseUnityDb.load()));
    }

    //==========================================================================
    // MIXER SNAPSHOT — the fader positions, as a set child.
    //
    // The mixer was the one panel saved NOWHERE: not in the set, not in any
    // settings file.  That was survivable while every style channel level came
    // from the style's own CC 7, because the style rewrote them on each load
    // anyway.  It stopped being survivable with IGNORE STYLE VOLUMES, whose
    // whole premise is that the levels belong to the user — and a level that
    // belongs to the user has to survive closing the plugin.
    //
    // Restored AFTER the style loads, deliberately: applyVoiceSetup writes every
    // style channel fader from the style's CC 7, so a mixer restored before it
    // would be overwritten by the very next load.  See applySetPayload.
    //==========================================================================
    juce::ValueTree captureMixerState() const;
    void            applyMixerState (const juce::ValueTree& ms);

    // ── MIDI CC control (Settings tab) ────────────────────────────────────────
    //
    // Five learnable continuous controllers, in the SettingsTab::CcTarget order:
    //   0 master volume, 1 style volume, 2 tempo, 3 transpose, 4 split point.
    // A controller number of -1 means "unassigned".
    enum CcTarget { ccMasterVol = 0, ccStyleVol, ccTempo, ccTranspose, ccSplit, kNumCcTargets };

    void setCcLearnTarget (int target) noexcept
        { ccLearnTarget.store (target >= 0 && target < kNumCcTargets ? target : -1); }
    int  getCcLearnTarget() const noexcept { return ccLearnTarget.load(); }

    /** True once since the last call if a CC moved one of the five targets.
        The editor polls this and mirrors the live values into the widgets. */
    bool consumeCcValueDirty() noexcept { return ccValueDirty.exchange (false); }

    void forgetCc (int target) noexcept
        { if (target >= 0 && target < kNumCcTargets) ccNumber[(size_t) target].store (-1); }

    int  getCcNumber (int target) const noexcept
        { return (target >= 0 && target < kNumCcTargets) ? ccNumber[(size_t) target].load() : -1; }

    /** Push a stored assignment in.  Used at startup by CcMap, which is the
        persistent owner of the map — the processor holds the LIVE copy the
        audio thread reads, and this is how the two are put in step. */
    void setCcNumber (int target, int cc) noexcept
    {
        if (target < 0 || target >= kNumCcTargets) return;
        ccNumber[(size_t) target].store ((cc >= 0 && cc <= 127) ? cc : -1);
    }

    /** Set true by the audio thread when a learn assignment lands, so the UI
        timer can refresh the display.  Reading clears it. */
    bool consumeCcDirty() noexcept { return ccDirty.exchange (false); }

private:
    // Master output = master fader gain × master boost (dB→linear).  Kept in one
    // place so the master fader, the master-volume CC and the boost tickboxes
    // all compose into the single value the engine reads.
    void applyMasterOut() noexcept
    {
        engine.setMasterVolume (masterFaderGain.load()
                                * juce::Decibels::decibelsToGain (masterBoostDb.load()));
    }

    Betel::Finisher           finisher;
    Betel::SamplePlayerEngine engine;

    /** Property names for the IgnorePc block - a ValueTree property is a
        juce::Identifier, so they have to start with a letter. */
    static juce::Identifier ignPcId (const char* prefix, int slot)
    {
        return juce::Identifier (juce::String (prefix) + juce::String (slot));
    }

    // What each IGNORE-PROGRAM-CHANGE slot was fixed to, read out of the set and
    // held until restoreIgnorePcSlots can publish it after the style load.
    std::array<int, 8> fixedKitKey { -1,-1,-1,-1,-1,-1,-1,-1 };
    std::array<int, 8> fixedFlag   { -1,-1,-1,-1,-1,-1,-1,-1 };

    // Liveness token for scheduled work.  juce::Timer::callAfterDelay cannot be
    // cancelled, so anything it captures must be able to tell that the processor
    // has since been destroyed.
    std::shared_ptr<bool> aliveFlag { std::make_shared<bool> (true) };
    Betel::DrumKitRegistry    drumKitRegistry;
    juce::String              lastBlobAccessCode { kFixedAccessCode };

    juce::MidiKeyboardState   keyboardState;
    std::atomic<int>          activeSoloSlot { 0 };

    // Master boost stage (mixer master tickboxes: 0/+3/+6/+9/+12 dB; default +6).
    std::atomic<float>        masterFaderGain { 1.0f };   // pre-boost gain (fader / CC)
    std::atomic<float>        masterBoostDb   { 0.0f };   // selected boost, default 0 dB

    // ── Style pipeline ────────────────────────────────────────────────────────
    Betel::StyleSequencer                       sequencer;

    // Per-section loudness measurement.  Listens to the rendered output, learns
    // what each section of the loaded style actually sounds like, and hands back
    // a corrective trim for the style bus.  Results are cached per style file in
    // grex_loudness.xml, so a section is only ever measured uncorrected once.
    Betel::StyleLoudness                        styleLoudness;
    int                                         lastMeasuredSection = -1;
    Betel::StylePlayer                          stylePlayer { engine };
    Betel::ChordZoneTracker                     chordTracker;
    std::unique_ptr<Betel::StyleData>           currentStyle;
    std::vector<std::unique_ptr<Betel::StyleData>> retiredStyles;
    std::vector<juce::File>                        stylesFolders;
    juce::String                                   lastLoadedStylePath;
    BetelFolderManager                             folderManager;

    // Editor-session persistence.  The editor (MainComponent) is destroyed and
    // recreated every time the plugin window is closed/reopened in the host,
    // but the processor — and everything it owns (style, sequencer position,
    // engine sounds) — lives on.  The editor seeds the engine once on the first
    // open; on every later open it restores the snapshot it saved when it last
    // closed, so re-opening the window never reverts state or re-cues playback.
    bool                                           editorInitialized = false;
    juce::ValueTree                                editorSnapshot;
    std::atomic<uint8_t>                           soloEnableMask { 0 };
    std::atomic<int>                               globalTranspose { 0 };
    std::atomic<bool>                              pianoMode { false };

    // Song recorder / player (see GrexSongRecorder.h / GrexSongPlayer.h).
    // perfSongBeats is the monotonic performance beat clock used to timestamp
    // recorded events; it is touched only on the audio thread.
    GrexSongRecorder                               songRecorder;
    GrexSongPlayer                                 songPlayer;
    double                                         perfSongBeats { 0.0 };
    std::atomic<bool>                              restartRequested { false };
    // Tracks the sequencer's playing state across audio blocks so we can flush
    // hung style notes on the playing->stopped edge (audio thread only).
    bool                                           wasSequencerPlaying { false };
    // DAW START -- when enabled, style playback follows the host transport
    // (rising edge starts, falling edge stops).  wasHostPlaying: audio only.
    std::atomic<bool>                              dawStartFollow { false };
    bool                                           wasHostPlaying { false };
    juce::String                                   comments;

    // Dispatch a reserved control-note (0–35) to its action.  Audio thread.
    void handleControlNote (int note);

    //==========================================================================
    // MIDI CC CONTROL - NOTHING IS ASSIGNED UNTIL THE USER ASSIGNS IT.
    //
    // ccNumber[t] = the controller bound to target t; -1 means none, and none is
    // now the default for every target.
    //
    // These used to default to master=CC7, style=CC11, tempo=CC16,
    // transpose=CC17, split=CC18 - five live bindings the user never made.  CC7
    // is Channel Volume and CC11 is Expression: both are standard messages that
    // hardware emits on its own, on a patch change, when a slider is nudged, or
    // from a pedal that is simply plugged in.  So a controller could drive
    // Grex's master volume or style volume with nobody having asked it to, and
    // nothing on screen would say why the mix moved.
    //
    // CcMap (grex_cc_map.xml) already defaulted to all -1 and is the thing that
    // describes the rig.  The two disagreed, and the DEFAULTS won in exactly the
    // sessions nobody was watching - see the constructor, where the map is now
    // loaded.
    //
    // THE RULE: a CC reaches a destination only if the user bound it in
    // SETTINGS -> MIDI CC CONTROL, or it is a reserved control note (0-35),
    // which is a note and not a CC.  handleControllerMessage is the single
    // choke point and it matches nothing by default.
    //==========================================================================
    std::array<std::atomic<int>, kNumCcTargets> ccNumber
        { { {-1}, {-1}, {-1}, {-1}, {-1} } };
    std::atomic<int>  ccLearnTarget { -1 };  // target awaiting a CC, or -1

    // Raised by applyCcToTarget, cleared by the editor.  A CC moves the
    // PARAMETER directly on the audio thread; without this the on-screen
    // control it belongs to never learned it had moved, so a pedal changed the
    // sound while its knob sat still - and the next touch of that knob snapped
    // the value back to wherever the knob had been left.
    std::atomic<bool> ccValueDirty { false };
    std::atomic<bool> ccDirty       { false };

    // ── Crash tab state ─────────────────────────────────────────────────────
    std::atomic<bool> crashOnTransition { false };
    // Which drum elements a crash fires: bit 0 = note 46, 1 = 55, 2 = 56,
    // 3 = 57.  Default: 57 (OPEN D) only.
    std::atomic<juce::uint8> crashNoteMask { 0b1000 };
    std::atomic<bool> autoCrashEnabled  { false };
    std::atomic<float> crashGainPercent  { 75.0f };   // 0=silence, 75=unity, 100=+6dB
    // What the 75 detent is worth, in dB.  0 = the sample as sampled.
    std::atomic<float> crashBaseUnityDb  { 0.0f };
    std::atomic<int>  autoCrashEveryN   { 4 };
    std::atomic<int>  crashVelocity     { 110 };   // the old hardcoded value
    int               autoCrashCounter  = 0;   // audio-thread loop accumulator

    // Audio thread: route a controller message to learn-capture or apply.
    void handleControllerMessage (int ccNum, int value);
    void applyCcToTarget (int target, int value);

    // Tap-tempo + tempo-mode state (message thread writes, audio thread reads).
    std::atomic<float>                             manualBPM { 120.0f };
    std::atomic<bool>                              tempoSyncedToHost { false }; // FREE default (matches selector)
    std::atomic<float>                             tempoSpeedMult { 1.0f };
    std::atomic<float>                             currentBaseBPM { 120.0f };   // last resolved base, for UI
    static constexpr int                           kTapBufferSize = 4;
    std::array<double, kTapBufferSize>             tapTimestamps {};
    int                                            tapCount = 0;
    std::vector<Betel::EmittedStyleEvent>       emittedEvents;

    // Style interpretation mode has been removed — there is one CASM path.

    // Global "bypass instrument audio chain" flag — default off.  Mirror of
    // the per-channel atomics inside SamplePlayerEngine so the processor can
    // serialise / restore it without a round-trip through the engine.
    std::atomic<bool>                           bypassInstrumentChainAtomic { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BetelgeuseProcessor)
};



