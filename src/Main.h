


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

#include "FstLibrary.h"
#include "FstLoader.h"
#include "RemoteMap.h"
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
#include "StyleLevels.h"   // the GLOBAL style boost, used by setStyleBoostDb
#include "GlobalMacros.h"  // the funkey mix's live value, used by setFunkeyMix
#include "StyleLoudness.h"   // per-section BS.1770 measurement + corrective trim
#include "BetelFolderManager.h"
#include "EdmKitFiles.h"            // EDM KIT base kits (.dsin)
#include "MasterSettings.h"     // solo base unity + pitch bend live in the master file
#include "Harmonizer.h"          // harmony note maths (pure)
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
    const juce::String getName() const override        { return "Grex Ballada"; }
    bool acceptsMidi() const override                  { return true; }
    bool producesMidi() const override                 { return false; }
    bool isMidiEffect() const override                 { return false; }
    double getTailLengthSeconds() const override       { return 0.0; }
    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram(int) override               {}
    const juce::String getProgramName(int) override    { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    //==========================================================================
    // DAW PROJECT RECALL.
    //
    // These were empty stubs, which meant Grex stored NOTHING in a host project:
    // save, reopen, and every per-set setting was back to default.bset. To a
    // plugin audience that is not a missing feature, it is a broken plugin - and
    // it is the first thing anyone tests.
    //
    // The pieces all existed; none of them were connected. `buildSetSnapshot()`
    // produces the tree, `applySetPayload()` consumes it, and `editorSnapshot`
    // already carried one across an editor close.
    //
    // THE AWKWARD PART is that both of those live on the EDITOR while these two
    // run on the PROCESSOR and must work with no window open. Hence the two
    // callbacks: fresh state when there IS an editor, the stored snapshot when
    // there is not.
    //==========================================================================
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::ValueTree tree;

        if (onCaptureSetTree)
            tree = onCaptureSetTree();          // editor open: current state

        if (! tree.isValid())
            tree = editorSnapshot;              // closed: last known state

        if (! tree.isValid()) return;           // nothing worth saving yet

        // ── DO NOT SAVE A STYLELESS SNAPSHOT ─────────────────────────────────
        //
        // The other half of the trap. A host asking for state before a style has
        // loaded - which it does, on instantiation - would otherwise store a
        // snapshot naming no style, and that snapshot comes back on every open
        // afterwards.
        //
        // Writing nothing is the honest answer to "what state do you have"
        // before there is any: the host stores an empty block, setStateInformation
        // early-returns on it, and startup falls through to default.bset exactly
        // as a fresh instance should.
        if (! hasStyle() && ! tree.getChildWithName ("GlobalState")
                                  .getProperty ("currentStylePath").toString().isNotEmpty())
            return;

        juce::MemoryOutputStream os (dest, false);
        tree.writeToStream (os);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (data == nullptr || sizeInBytes <= 0) return;

        juce::MemoryInputStream is (data, (size_t) sizeInBytes, false);
        auto tree = juce::ValueTree::readFromStream (is);
        if (! tree.isValid() || ! tree.hasType ("BetelSet")) return;

        // Stored FIRST, because the host usually calls this before the editor
        // exists - the first window open reads it from here.
        editorSnapshot = tree;
        projectStateRestored.store (true);

        // And applied straight away when a window IS open, which is what happens
        // when the user loads a preset while looking at the plugin.
        if (onApplySetTree) onApplySetTree (tree);
    }

    /** True once a host has handed us project state.

        THE ONE REAL DECISION IN ALL OF THIS: the project's state beats
        default.bset. A project that reopened into someone's default rather than
        into the song they saved would be the same bug in a politer form. */
    bool hasProjectState() const noexcept { return projectStateRestored.load(); }
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

    //==========================================================================
    // HARMONY VOICE PAIRING - the same latch, for the notes harmony INVENTS.
    //
    // A harmony note has no key of its own. Nothing will ever send a note-off
    // for it, so the ONLY thing that can release it is the note-off of the key
    // that spawned it. That makes this table load-bearing rather than
    // bookkeeping: lose an entry and the voice sustains until the next panic.
    //
    // WHY IT CANNOT BE RECOMPUTED AT NOTE-OFF INSTEAD. The obvious shortcut is
    // to run Harmonizer::compute again on the way out and release whatever it
    // returns. That is wrong, and quietly so: the chord may have moved while
    // the key was held, so the second call returns DIFFERENT notes - it
    // releases voices that were never started and strands the ones that were.
    // What went down has to be what comes up, so it is recorded, not derived.
    //
    // For the same reason a held note NEVER re-pitches when the chord changes.
    // The interval is decided once, at note-on. Re-voicing mid-note sounds like
    // a glitch, not like harmony, and it would put this table out of step with
    // what is actually sounding.
    //
    // harmonyCount IS THE AUTHORITY, not a sentinel value in the note array.
    // Entries past the count are never read, which matters because
    // `int8_t a[128][3] {}` zero-fills to 0 - a valid MIDI note - so a -1
    // sentinel would be a lie at construction even though clearNoteRoutes
    // writes it. Storing the count also means a PARTIAL result releases
    // correctly: two voices where three were asked for, which is what happens
    // at the bottom of the keyboard, releases exactly two.
    //
    // Three slots because Block is the widest type (Harmonizer::kMaxVoices).
    //
    // Audio thread only, like the two routes above.
    //==========================================================================
    static constexpr int kMaxHarmonyVoices = Betel::Harmonizer::kMaxVoices;

    int8_t harmonyNotes [128][kMaxHarmonyVoices] {};
    uint8_t harmonyCount[128] {};

    /** The highest solo note currently held, or -1.  THE MELODY, for harmony's
        purposes: only the top note is harmonised, because harmonising every
        note of a right-hand chord multiplies the voice count and is not what an
        arranger does.

        Audio thread only, like the two route tables. */
    int highestHeldSoloNote = -1;

    void clearNoteRoutes() noexcept
    {
        for (int i = 0; i < 128; ++i)
        {
            noteRouteSolo[i] = 0;
            noteRouteChord[i] = false;
            harmonyCount[i] = 0;
            for (int v = 0; v < kMaxHarmonyVoices; ++v) harmonyNotes[i][v] = -1;
        }
        highestHeldSoloNote = -1;
    }

    /** Take everything a key currently holds and empty the row.
        ONE function for both uses, because they are the same operation: at
        note-off it releases the voices, and at a RESTRIKE it releases the
        previous strike's voices before the new ones are latched.

        Returns the count; the notes are in `out`. */
    int takeHarmony (int phys, int out[kMaxHarmonyVoices]) noexcept
    {
        if (phys < 0 || phys > 127) return 0;

        const int n = juce::jmin ((int) harmonyCount[phys], kMaxHarmonyVoices);
        for (int v = 0; v < n; ++v) out[v] = (int) harmonyNotes[phys][v];

        harmonyCount[phys] = 0;
        return n;
    }

    /** Record what a note-on just spawned.  OVERWRITES - the caller must have
        called takeHarmony first and released whatever came back.

        THIS DOES NOT ACCUMULATE, and that is a deliberate difference from
        noteRouteSolo beside it. That one accumulates safely because it is a
        bitmask of eight fixed slots: striking a key again can only ever re-set
        bits that already exist. Harmony has no such bound - a restrike under a
        MOVED chord produces different notes, so accumulating would need four or
        more rows and the fourth would be silently dropped and left sounding
        forever. Releasing the old strike first is bounded and correct. */
    void latchHarmony (int phys, const Betel::Harmonizer::Result& r) noexcept
    {
        if (phys < 0 || phys > 127) return;

        harmonyCount[phys] = 0;

        for (int i = 0; i < r.count && harmonyCount[phys] < kMaxHarmonyVoices; ++i)
        {
            const int n = r.notes[i];
            if (n < 0 || n > 127) continue;

            bool already = false;
            for (int v = 0; v < harmonyCount[phys]; ++v)
                if (harmonyNotes[phys][v] == (int8_t) n) { already = true; break; }

            if (! already)
                harmonyNotes[phys][harmonyCount[phys]++] = (int8_t) n;
        }
    }

    /** LIVE-SAVED, like the pitch bend range and the low-velocity lift.

        The split used to live only in the project state, so it survived a
        project reload and nothing else: a fresh session always came up at the
        default, and the player set it again every time.  MasterSettings::
        setSplitPoint writes only on a real change, so dragging the knob costs
        one file write when the drag ends on a new note, not one per pixel. */
    //==========================================================================
    // HARMONY - the settings the note dispatch reads.
    //
    // ALL ATOMIC: written by the editor on the message thread, read inside
    // processBlock. Relaxed is right for every one of them - they are
    // independent scalars with nothing hanging off their ordering, and a note
    // landing on the old value for one block is inaudible.
    //
    // THE TARGET SLOT IS FIXED AT 7 (solo 8), by design rather than by
    // shortage. Harmony is right-hand material, so it belongs on the SOLO bus:
    // the ducker keys the style bus FROM the solo bus, so harmony on a style
    // channel would be ducked by the very melody it is harmonising. On solo 8
    // it ducks the band alongside the lead, which is what it should do.
    //==========================================================================
    static constexpr int kHarmonySlot = 7;          // solo 8

    //==========================================================================
    // MULTI SPLIT - THREE ZONES, TWO BOUNDARIES.
    //
    //     note < bassSplit                 BASS zone
    //     bassSplit <= note < splitPoint   CHORD zone (the style)
    //     note >= splitPoint               SOLO zone (the right hand)
    //
    // splitPoint is the boundary Grex has always had; bassSplit is the new one
    // BELOW it. Stated that way round on purpose - a player who never turns
    // multi split on keeps exactly the keyboard they had, because with it off
    // the bass zone has zero width and the test collapses to the old two-way
    // one.
    //
    // The bass zone routes to a SOLO SLOT of its own, so an organ player's
    // pedals or an accordion's left-hand buttons get their own instrument,
    // level and FX. Slot 6 by default: it is the last slot that is not the
    // harmony channel, so the two features do not fight over solo 8.
    //
    // WHAT THE BASS ZONE DOES FOR THE STYLE: it ALSO feeds the chord tracker.
    // A left hand playing a bass line is still telling the band what the chord
    // is, and a wide left hand that suddenly stopped moving the harmony would
    // read as the arranger losing the plot. Manual bass - which silences the
    // style's own bass - is a separate switch and a separate feature.
    //==========================================================================
    static constexpr int kBassSlotDefault = 6;      // solo 7

    /** Which solo slot currently carries the bass zone, or -1 when multi split
        is off.  The four views that tint a slot ask THIS rather than testing
        kBassSlotDefault, so the colour follows the chosen slot instead of being
        painted on slot 7 forever. */
    //==========================================================================
    // MANUAL BASS and BASS TO LOWEST - two switches, not one.
    //
    // MANUAL BASS IS CONDITIONAL, NOT A MUTE - and that is the whole feature.
    //
    // Yamaha's own patent for automatic bass chord accompaniment (US 4,864,907)
    // describes it exactly: where the identified chord ROOT coincides with the
    // bass note being played, the style's authored bass PATTERN plays normally;
    // only where they DISAGREE - or where no chord is recognised at all - does
    // the bass collapse to the note under the finger.
    //
    // So the pattern survives while the player agrees with the chord, and is
    // abandoned only when they deliberately do not. Muting the channel outright
    // would throw the pattern away permanently, which is musically much worse
    // and is why a plain mute felt wrong the moment it was described out loud.
    //
    // BASS INVERSION re-roots the style's bass on the LOWEST note held rather
    // than on the chord root, which is what makes an inversion audible: C/E and
    // a root-position C are the SAME recognised chord - inversion is not part of
    // chord identity - so the bass note has to come from the keyboard.
    //
    // The name is Korg's and so is the two-mode shape. The case that cannot be
    // played any other way is C/B = Cmaj7: renaming gets you C/D as D11 and
    // C/F as Fmaj9, but there is no relabelling that puts a major 7th in the
    // bass without wrecking the harmony above it.
    //
    // BOTH ARE INDEPENDENT SWITCHES on every arranger that ships them - Korg
    // lists Bass Inversion and Manual Bass side by side in its style controls -
    // so neither is gated behind the other or behind multi split here either.
    //==========================================================================
    static constexpr int kStyleBassChannel = 2;     // BASS role

    //==========================================================================
    //  MANUAL BASS IS A ROUTING MODE, NOT ONLY A NOTE SUBSTITUTION.
    //
    //  It used to be the substitution alone - getManualBassNote() handed the
    //  played note to StylePlayer, which swapped it into the style's own bass
    //  pattern.  Nothing routed the keyboard, so the left hand went on feeding
    //  the chord tracker and playing the arranger exactly as before: switching
    //  the mode on changed nothing you could hear from the left hand.
    //
    //  What the mode actually means: the keyboard becomes TWO HANDS with no
    //  chord-only zone in it.  The left hand plays SOLO 7 as a bass instrument
    //  of its own, and it does NOT move the harmony - the band holds whatever
    //  chord it was last given.  See bassZoneFeedsChords() for why that answer
    //  is the opposite of multi split's.
    //
    //  MUTUALLY EXCLUSIVE WITH MULTI SPLIT.  Both claim the left hand and both
    //  claim a bass solo slot, so with the two on at once the zone tests
    //  overlap and whichever branch is written first silently wins.  Turning
    //  one on therefore turns the other off, here rather than in the panel, so
    //  a MIDI assignment or a set load cannot reach an impossible pair.
    //==========================================================================
    void setManualBassEnabled (bool b) noexcept
    {
        manualBassOn.store (b, std::memory_order_relaxed);
        if (b) multiSplitOn.store (false, std::memory_order_relaxed);
    }
    bool isManualBassEnabled() const noexcept
    {
        return manualBassOn.load (std::memory_order_relaxed);
    }

    /** WHICH SOLO SLOT THE LEFT HAND PLAYS, for whichever mode owns it.

        Manual bass is ALWAYS solo 7 and has no cycler; multi split keeps its
        own BASS -> SOLO n choice.  One call answers for both so the router, the
        M.BASS lamp and the four views that tint a slot can never disagree about
        which slot is the bass. */
    int getActiveBassSlot() const noexcept
    {
        if (manualBassOn.load (std::memory_order_relaxed)) return kBassSlotDefault;
        return bassZoneSlot.load (std::memory_order_relaxed);
    }

    /** Does the left hand still tell the band what the chord is?

        MULTI SPLIT : YES.  Its bass zone sits UNDER a chord zone, and a left
                      hand walking a bass line down there is still speaking for
                      the harmony.  Dropping the feed would make a wide left
                      hand stop moving the chords, which reads as the arranger
                      losing the plot rather than as a routing choice.

        MANUAL BASS : NO.  The mode IS a two-hand split with no chord zone in it
                      at all, so there is nothing for the left hand to speak
                      for.  The band holds its last chord and the left hand is
                      free to play anything. */
    bool bassZoneFeedsChords() const noexcept
    {
        return ! manualBassOn.load (std::memory_order_relaxed);
    }

    /** Korg's two settings, kept because the difference is musical, not
        cosmetic: ALWAYS treats the lowest note as the bass unconditionally,
        while OnlyIfNotRoot leaves an ordinary root-position chord alone and
        only re-roots when the player has actually inverted something. */
    enum class BassInversionMode : int { Always = 0, OnlyIfNotRoot = 1 };

    void setBassInversionEnabled (bool b) noexcept
    {
        bassInversionOn.store (b, std::memory_order_relaxed);
    }
    bool isBassInversionEnabled() const noexcept
    {
        return bassInversionOn.load (std::memory_order_relaxed);
    }

    void setBassInversionMode (BassInversionMode m) noexcept
    {
        bassInversionMode.store ((int) m, std::memory_order_relaxed);
    }
    BassInversionMode getBassInversionMode() const noexcept
    {
        return (BassInversionMode) bassInversionMode.load (std::memory_order_relaxed);
    }

    /** The note the style's bass should be rooted on, or -1 to leave it alone.
        AUDIO THREAD. -1 whenever the feature is off or nothing is held, so the
        caller's normal path is one relaxed load and a branch. */
    int getBassRootOverride() const noexcept
    {
        if (! bassInversionOn.load (std::memory_order_relaxed)) return -1;

        const int lowest = chordTracker.getLowestHeldNote();
        if (lowest < 0) return -1;

        if ((BassInversionMode) bassInversionMode.load (std::memory_order_relaxed)
                == BassInversionMode::OnlyIfNotRoot)
        {
            // Root position is not an inversion. Leaving it alone means the
            // style's own bass line keeps its shape for the 90% of chords that
            // are played plainly, and only a deliberate inversion moves it.
            const int rootPc = chordTracker.getCurrentChord().root;
            if ((((lowest % 12) + 12) % 12) == rootPc) return -1;
        }

        return lowest;
    }

    /** MANUAL BASS, resolved: the note the bass should play INSTEAD of its
        pattern, or -1 to let the pattern run.

        The patent's test, verbatim in code: the pattern survives while the
        played bass note agrees with the recognised chord root, and is replaced
        only when it does not - or when no chord has been recognised at all. */
    int getManualBassNote() const noexcept
    {
        if (! manualBassOn.load (std::memory_order_relaxed)) return -1;

        const int lowest = chordTracker.getLowestHeldNote();
        if (lowest < 0) return -1;        // nothing held: the pattern plays on

        const int rootPc = chordTracker.getCurrentChord().root;
        if ((((lowest % 12) + 12) % 12) == rootPc) return -1;   // agrees -> pattern

        return lowest;                    // disagrees -> the finger wins
    }

    int getTintedBassSlot() const noexcept
    {
        // EITHER mode owns a bass slot now, so the tint follows both.  Reading
        // getActiveBassSlot keeps this answer and the router's answer identical
        // by construction.
        if (manualBassOn.load (std::memory_order_relaxed)
            || multiSplitOn.load (std::memory_order_relaxed))
            return getActiveBassSlot();
        return -1;
    }

    void setMultiSplitEnabled (bool b) noexcept
    {
        multiSplitOn.store (b, std::memory_order_relaxed);
        // The other half of the exclusion - see setManualBassEnabled.
        if (b) manualBassOn.store (false, std::memory_order_relaxed);
    }
    bool isMultiSplitEnabled() const noexcept
    {
        return multiSplitOn.load (std::memory_order_relaxed);
    }

    /** THE TWO BOUNDARIES CAN NEVER CROSS.
        Clamped to one below the main split rather than merely to 0..127: if the
        bass boundary reaches or passes it, the CHORD zone has zero or negative
        width and simply vanishes - every note becomes bass or solo and the
        style stops following the left hand at all, with nothing on screen
        saying why. One drag was enough to reach that. */
    void setBassSplitPoint (int midiNote) noexcept
    {
        const int ceiling = juce::jmax (0, getSplitPoint() - 1);
        bassSplitPoint.store (juce::jlimit (0, ceiling, midiNote),
                              std::memory_order_relaxed);
    }

    /** Re-apply the clamp after the MAIN split moves.  The invariant has to hold
        when EITHER boundary changes, not just when the bass one does. */
    void reclampBassSplit() noexcept
    {
        setBassSplitPoint (bassSplitPoint.load (std::memory_order_relaxed));
    }
    int getBassSplitPoint() const noexcept
    {
        return bassSplitPoint.load (std::memory_order_relaxed);
    }

    void setBassZoneSlot (int slot) noexcept
    {
        // Never the harmony slot: it would put the pedals on the harmony voice
        // and the two would overwrite each other's notes.
        if (slot == kHarmonySlot) return;
        bassZoneSlot.store (juce::jlimit (0, 7, slot), std::memory_order_relaxed);
    }
    int getBassZoneSlot() const noexcept
    {
        return bassZoneSlot.load (std::memory_order_relaxed);
    }

    /** True when `note` is in the bass zone.  False whenever multi split is off,
        which is what makes the three-way test collapse to the old two-way one. */
    bool isBassZone (int note) const noexcept
    {
        // MANUAL BASS: the whole left hand, bounded by the MAIN split point.
        // Not bassSplitPoint - that boundary belongs to multi split's THREE
        // zones, and manual bass has only two.  Reusing it would have left the
        // band a chord zone between the two boundaries that the mode says does
        // not exist.  Tested first because the two modes are mutually exclusive
        // (see setManualBassEnabled), so the order is a statement of that
        // rather than a precedence anyone has to remember.
        if (manualBassOn.load (std::memory_order_relaxed))
            return note < getSplitPoint();

        return multiSplitOn.load (std::memory_order_relaxed)
            && note < bassSplitPoint.load (std::memory_order_relaxed);
    }

    void setHarmonyEnabled (bool b) noexcept { harmonyOn.store (b, std::memory_order_relaxed); }
    bool isHarmonyEnabled() const noexcept   { return harmonyOn.load (std::memory_order_relaxed); }

    void setHarmonyType (Betel::Harmonizer::Type t) noexcept
    {
        harmonyType.store ((int) t, std::memory_order_relaxed);
    }
    Betel::Harmonizer::Type getHarmonyType() const noexcept
    {
        return (Betel::Harmonizer::Type) harmonyType.load (std::memory_order_relaxed);
    }

    void setHarmonyBelow (bool b) noexcept { harmonyBelow.store (b, std::memory_order_relaxed); }
    bool isHarmonyBelow() const noexcept   { return harmonyBelow.load (std::memory_order_relaxed); }

    /** 0..100. Harmony velocity as a percentage of the lead's.
        Default 82: at parity the melody stops reading as the melody. */
    void setHarmonyLevel (int pct) noexcept
    {
        harmonyLevel.store (juce::jlimit (0, 100, pct), std::memory_order_relaxed);
    }
    int getHarmonyLevel() const noexcept { return harmonyLevel.load (std::memory_order_relaxed); }

    void setSplitPoint (int midiNote) noexcept
    {
        chordTracker.setSplitPoint (midiNote);
        Betel::MasterSettings::get().setSplitPoint (midiNote);
        reclampBassSplit();
    }

    /** Split point WITHOUT touching the master file - for restoring a project's
        stored value, which must not overwrite the player's own setting.

        The distinction matters because a project carries a split too: opening
        someone else's song should position the keyboard for that song without
        redefining where this player's hands live from then on. */
    void setSplitPointFromProject (int midiNote) noexcept
    {
        chordTracker.setSplitPoint (midiNote);
        reclampBassSplit();
    }
    int  getSplitPoint() const noexcept                { return chordTracker.getSplitPoint(); }
    /** Per-section loudness measurement — readout, target and re-measure. */
    Betel::StyleLoudness&       getStyleLoudness()       noexcept { return styleLoudness; }
    const Betel::StyleLoudness& getStyleLoudness() const noexcept { return styleLoudness; }

    // ── Style transport ───────────────────────────────────────────────────────
    bool loadStyle (const juce::File& file, juce::String& errorMsg);

    //==========================================================================
    // THE STYLE LIBRARY — a FOLDER of .fst files, scanned at open. No blob, no
    // mapping, no packaging layer: the converter writes one file per style and
    // the plugin reads the folder.
    //
    // STYLES ONLY. The SOUND packs (sounds_gm/*.frb) and DRUM KITS
    // (drums/*_kit.frb) are still blobs, still memory-mapped, still opened with
    // AccessCode.h — see `engine` and `drumKitRegistry` below.
    //
    // Opened once at startup; only the TOCs are read, so this costs a few
    // hundred KB and no measurable time. A style's bytes are decrypted the
    // moment it is loaded and never before.
    //==========================================================================
    void openStyleLibrary();

    //==========================================================================
    // EVERY FILE UNDER GrexPaths::root() THAT IS READ ONCE AND CACHED.
    //
    // One method, called from ONE place - the end of rescanFromFolderManager -
    // so that "the root moved" and "re-read the things that live under it" can
    // never drift apart again.
    //
    // The rule this exists to enforce: ANYTHING CACHED AT CONSTRUCTION THAT
    // LIVES UNDER THE ROOT MUST BE INVALIDATED WHEN THE ROOT MOVES.  Every
    // violation has the same signature and the same baffling symptom - it works
    // only after a reload - and the cause is never visible from where it hurts.
    //==========================================================================
    void reloadRootBackedSettings();

    /** Everything after a style is parsed, shared by the file and blob paths.
        `sourceFile` is invalid for a blob load and the loudness cache is skipped
        accordingly - see the definition.

        `styleRef` is the id (or legacy path) the style will be known by, and it
        is needed HERE rather than after the call because the set lookup that
        decides whether CC 7 may touch the faders has to happen BEFORE
        applyVoiceSetup runs. */
    void adoptFreshStyle (std::unique_ptr<Betel::StyleData> fresh,
                          const juce::File& sourceFile,
                          const juce::String& styleRef);

    /** Load by STYLE ID from the library, or by legacy absolute PATH.

        BOTH, deliberately, and that IS the migration. Every set in the field
        stores `currentStylePath` as a real filesystem path; refusing those would
        orphan the entire existing library on the day the blobs ship. So an
        argument that matches a library id loads from the blob, and anything else
        is tried as a file exactly as before.

        Nothing has to be rewritten, nothing is destroyed, and a set saved after
        this point quietly starts carrying an id instead. */
    bool loadStyleByRef (const juce::String& idOrPath, juce::String& errorMsg);

    /** What `currentStylePath` should record for what is loaded now: the ID when
        it came from a blob, the path when it did not. */
    juce::String getCurrentStyleRef() const { return currentStyleRef; }

    const Betel::FstLibrary& getStyleLibrary() const noexcept
    { return styleLibrary; }

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

    //==========================================================================
    // SONG TRANSPORT - what the song window drives.
    //
    // On the PROCESSOR, not the editor, for the reason every startup bug in this
    // project had: a song must be able to play with no window open.
    //==========================================================================
    bool loadSongFile (const juce::File& f)
    {
        GrexSongRecorder::Setup setup;
        std::vector<GrexSongRecorder::Event> evts;
        if (! GrexSongRecorder::loadFromFile (f, setup, evts)) return false;

        songPlayer.setSong (setup, evts);
        songSetup = setup;

        // THE EMBEDDED SET IS THE POINT OF THE FORMAT, so it is applied before a
        // note plays. Without it the song replays against whatever set happens
        // to be loaded, which is the one thing the format exists to prevent.
        if (setup.setSnapshot.isValid() && onApplySetTree)
            onApplySetTree (setup.setSnapshot);

        // The SET no longer carries a split, so the SONG does - and it decides
        // which recorded notes were chord and which were solo.
        if (setup.splitPoint > 0)
            chordTracker.setSplitPoint (setup.splitPoint);

        return true;
    }

    void songPlay()   { songPlayer.setPaused (false); songPlayer.start(); }
    void songStop()   { songPlayer.stop(); }
    void songPause()  { songPlayer.setPaused (! songPlayer.isPaused()); }
    void songSeekBeats (double b) { songPlayer.requestSeek (b); }

    bool   isSongPlaying()        const noexcept { return songPlayer.isPlaying(); }
    bool   isSongPaused()         const noexcept { return songPlayer.isPaused(); }
    double getSongPositionBeats() const noexcept { return songPlayer.getPositionBeats(); }
    double getSongLengthBeats()   const noexcept { return songPlayer.getLengthBeats(); }
    double getSongTempoBpm()      const noexcept { return songSetup.tempoBpm; }

    void setSongChannelMuted (int ch, bool m) { songPlayer.setChannelMuted (ch, m); }

    /** BACKING-TRACK MODE - see GrexSongPlayer::isSuppressedRightHand. */
    void setSongSoloLive (bool live) { songPlayer.setSoloLive (live); }
    bool isSongSoloLive() const      { return songPlayer.isSoloLive(); }
    bool isSongChannelMuted  (int ch) const   { return songPlayer.isChannelMuted (ch); }

    /** Supplied by the editor: applies a <BetelSet> across the tabs.

        Used by BOTH the song loader and DAW project recall - it is one
        operation, and giving it two names would have hidden that. */
    std::function<void (const juce::ValueTree&)> onApplySetTree;

    /** Supplied by the editor: a FRESH snapshot of live state.

        `getStateInformation` needs current state, and `editorSnapshot` is only
        as new as the last editor close - so a project saved with the window open
        would store whatever the state was when it was last shut. */
    std::function<juce::ValueTree()> onCaptureSetTree;
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
    // ═════════════════════════════════════════════════════════════════════════
    //  TEMPO IS AN OFFSET, NOT AN ABSOLUTE.  READ THIS BEFORE EDITING.
    // ═════════════════════════════════════════════════════════════════════════
    //
    //  THE INVARIANT, maintained by every function below:
    //
    //      manualBPM  ==  clamp (styleBaseBPM + userBpmDelta)
    //
    //  THREE VALUES, AND ONLY ONE OF THEM IS SAVED:
    //
    //    styleBaseBPM  the tempo the LOADED STYLE asks for (its originalBPM).
    //                  Owned by the style-load path.  Never saved - the style
    //                  file already holds it and will hand it back next time.
    //
    //    userBpmDelta  what the PLAYER did to it, in BPM.  Style says 150, the
    //                  player wants 170, this holds +20.  THIS IS WHAT THE SET
    //                  SAVES.
    //
    //    manualBPM     the resolved working tempo, and the ONLY one the audio
    //                  thread reads (processBlock, FREE mode).  Derived; never
    //                  a source of truth.
    //
    //  ── WHY THE OFFSET RATHER THAN THE NUMBER ────────────────────────────────
    //
    //  A set used to store the absolute BPM, which meant it stored a number
    //  belonging to the STYLE and then imposed it on whatever style the set
    //  happened to load.  Re-time a style and every set naming it still forced
    //  the old tempo; swap the style behind a set and it played at a speed that
    //  had nothing to do with the new one.  The player's actual intention -
    //  "a bit quicker than written" - was never recorded at all, because it was
    //  baked into a total that could not be taken apart again.
    //
    //  This mirrors the mixer's style x user split: THE STYLE DECIDES, WE ADJUST
    //  AFTER.  The style's own tempo leads, the player's nudge rides on top, and
    //  the two are separable forever after.
    //
    //  ── THE DELTA SURVIVES A STYLE CHANGE.  THAT IS DELIBERATE ───────────────
    //
    //  Load a new style with +20 held and the new style plays at ITS tempo +20,
    //  exactly as a mixer fader stays where the player left it across a style
    //  change.  RESET TEMPO is the escape hatch and now clears the offset
    //  outright, so one button still puts every style back on its own feet.
    //
    //  ── WHERE THE CLAMP LIVES, AND WHY NOT ON THE DELTA ──────────────────────
    //
    //  The 30..300 clamp is applied to the RESOLVED tempo, not to the delta, so
    //  a big offset against a slow style is quietly limited rather than refused.
    //  The delta keeps its full value across that clamp: park a +150 offset on a
    //  60 BPM ballad (resolves to 210) and it still resolves to 300 rather than
    //  240 when a 200 BPM style loads underneath it.  Clamping the stored offset
    //  instead would let the ceiling silently eat the player's setting.
    //
    // resetTempoOverride() drops the offset and snaps back to the loaded style's
    // own tempo (or 120 if no style).
    static constexpr float kMinBPM = 30.0f;
    static constexpr float kMaxBPM = 300.0f;

    void  tapTempo();

    void  resetTempoOverride() noexcept
    {
        userBpmDelta.store (0.0f);
        refreshManualBPMFromDelta();
    }

    /** Re-resolves the working tempo from the style's tempo plus the player's
        offset.  Every writer below ends here, so the invariant holds no matter
        which end was moved. */
    void  refreshManualBPMFromDelta() noexcept
    {
        manualBPM.store (juce::jlimit (kMinBPM, kMaxBPM,
                                       styleBaseBPM.load() + userBpmDelta.load()));
    }

    /** Called by the style-load path with the newly loaded style's originalBPM.
        Re-resolves immediately, so the new style plays at ITS tempo plus
        whatever offset the player is holding. */
    void  setStyleBaseBPM (float bpm) noexcept
    {
        styleBaseBPM.store (bpm > 0.0f ? juce::jlimit (kMinBPM, kMaxBPM, bpm) : 120.0f);
        refreshManualBPMFromDelta();
    }
    float getStyleBaseBPM() const noexcept { return styleBaseBPM.load(); }

    /** THE KNOB'S AND TAP'S ENTRY POINT, AND IT STILL SPEAKS ABSOLUTE BPM.
        The UI shows and sets a tempo, not an offset - asking a player to think
        in deltas would be a worse instrument.  The offset is derived here, which
        is the one place the translation has to happen. */
    void  setManualBPM (float bpm) noexcept
    {
        const float wanted = juce::jlimit (kMinBPM, kMaxBPM, bpm);
        userBpmDelta.store (wanted - styleBaseBPM.load());
        manualBPM   .store (wanted);
    }
    float getManualBPM() const noexcept { return manualBPM.load(); }

    /** THE SET'S ENTRY POINT.  applyGlobalState calls this with the saved
        offset; the resolved tempo falls out of the style already loaded. */
    void  setUserBpmDelta (float delta) noexcept
    {
        // Bounded by the widest possible legal excursion rather than by the BPM
        // range, so a hand-edited or corrupt set cannot park an absurd number
        // here, while every offset a player can actually produce survives.
        const float span = kMaxBPM - kMinBPM;
        userBpmDelta.store (juce::jlimit (-span, span, delta));
        refreshManualBPMFromDelta();
    }
    float getUserBpmDelta() const noexcept { return userBpmDelta.load(); }

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
    void getPadActivity   (int engineChannel, uint32_t out[4]) const;   // EDM KIT pad LEDs

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
    /** RESET: restore the Finisher's factory values.  Replaces "reload the
        selected character", which needed a selector to reload FROM. */
    void  resetFinisherToFactory()         noexcept { finisher.loadFactoryDefaults(); }

    //==========================================================================
    // THE LAST MIDI MESSAGE, packed into ONE int so the read cannot tear.
    //
    //   bits 16..  sequence   (rolls over; only there so a REPEAT of the same
    //                          message is visible - hitting one pad twice has
    //                          to light the monitor twice)
    //   bits 15-14 kind       (1 = pad, 2 = cc, matching RemoteMap::Kind)
    //   bits 13-7  number
    //   bits  6-0  value
    //
    // Four separate atomics would let the reader catch an update halfway and
    // show a CC's number against a note's velocity. One word cannot do that.
    //==========================================================================
    std::atomic<int> lastMidiPacked { 0 };

    void noteLastMidi (int kind, int number, int value) noexcept
    {
        const int seq = (lastMidiSeq.fetch_add (1, std::memory_order_relaxed) + 1) & 0xFFFF;
        lastMidiPacked.store ((seq << 16) | ((kind & 0x3) << 14)
                                          | ((number & 0x7F) << 7)
                                          | (value & 0x7F),
                              std::memory_order_relaxed);
    }

    int getLastMidiPacked() const noexcept
    { return lastMidiPacked.load (std::memory_order_relaxed); }

    /** While armed, an incoming pad or CC is RECORDED and NOT acted on.  Without
        this, arming LEARN and hitting a pad would also fire whatever that pad
        currently drives - which for a variation pad means the arrangement jumps
        while you are trying to set something up. */
    // LEARN IS GONE.  It was an arm-and-wait that swallowed every MIDI message
    // while armed, so forgetting to disarm it made the plugin look deaf with
    // nothing on screen to say why.  Assignment is drag-and-drop now: the
    // source is captured passively by the monitor below and never intercepted,
    // so nothing has to be armed and nothing can be left armed.


    // setFinisherCharacter / getFinisherCharacter are GONE with the selector.
    // See Finisher.h: it was a preset loader, and the sliders it wrote into
    // were always the real owners of those values.
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
                // AN EDM KIT publishes under key 0 - the composed Standard kit's key -
                // so the key cannot bring it back.  SoundsState has just restored it
                // WITH its edits; "restoring key 0" here swapped it for acoustic
                // Standard on every set load.
                if (kit == 0 && engine.channelHoldsEdmKit (s)) continue;
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

    //==========================================================================
    // PLAY REFUSED FOR WANT OF A STYLE.
    //
    // `togglePlayStop` is `else if (hasStyle()) sequencer.start()`, so with no
    // style loaded it did LITERALLY NOTHING: no start, no lamp, no caption
    // change, no sound - from the mouse and from a hardware pad alike, because
    // both routes go through that one function.  A control that does nothing
    // and says nothing is indistinguishable from a broken control, and this has
    // now cost two separate debugging sessions chasing the transport when the
    // fault was upstream, in style loading.
    //
    // Raising a flag here rather than fixing it here is deliberate: the
    // processor must not reach into the browser (it has to work headless, where
    // there IS no browser).  The editor drains this on its 30 Hz mirror and
    // does the recovery - load a style, then start - so pressing PLAY with
    // nothing loaded now PLAYS instead of appearing dead.
    //
    // Relaxed is right: it is a one-bit hint between two threads with no other
    // state hanging off it.
    //==========================================================================
    void notePlayRefusedNoStyle() noexcept
    {
        playRefusedNoStyle.store (true, std::memory_order_relaxed);
    }

    /** True ONCE per refusal.  Consuming it here means a stream of presses
        cannot queue a stream of recoveries. */
    bool consumePlayRefusedNoStyle() noexcept
    {
        return playRefusedNoStyle.exchange (false, std::memory_order_relaxed);
    }
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
    // 11, not 12: an octave is the same note, so the twelfth step only ever
    // duplicated the root a player already had at 0.  Eleven each way covers
    // every key without offering a position that does nothing.
    static constexpr int kMaxTranspose = 11;

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

        // EDM KIT base kits (.dsin) live in the GM pack beside the style presets,
        // so they follow the library root like everything else.
        EdmKitFiles::setBaseKitFolder (folderManager.getGmSoundsFolder()
                                                    .getChildFile ("style_instruments_presets"));
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

    //==========================================================================
    /** ENERGY — style dynamics, 0..100 with 50 = EXACTLY AS AUTHORED.

        The style side's twin of setSoloLowVelBoost: that one owns the global
        velocity-curve term on the eight solo channels, this one owns it on the
        sixteen style channels.  See SamplePlayerEngine::setStyleEnergyCurve for
        why sharing that term is free and why the ordering does not matter.

        0..200 WITH 100 AS NEUTRAL, matching gainPercent.  It was 0..100 with a
        50 detent, and 50-is-neutral reads as "half" on a 0..100 slider however
        the detent is drawn.  100-is-unity is a convention this plugin already
        has and nobody misreads.

        The stored curve is unchanged - only the number on screen moved - so no
        set migrates and nothing sounds different at the same setting.

        WHAT IT CANNOT DO: 0 and 127 are fixed points of the curve, so this
        reshapes the DISTRIBUTION - widening the gap between an ordinary hit and
        an accent, and reaching the kit's softer sample layers - but it never
        lowers the ceiling.  A style that slams still needs the sweetener's PEAK
        stage or a trim.  Two controls, two different jobs. */
    void setStyleEnergy (int energy) noexcept
    {
        const int e = juce::jlimit (0, 200, energy);
        styleEnergy.store (e, std::memory_order_relaxed);
        engine.setStyleEnergyCurve ((float) (e - 100) / 100.0f);
    }

    /** ENERGY as a CURVE OFFSET, which is what the instrument editor has to add
        to a slot's own velocity curve to show what is actually in force.

        The two compose by ADDITION and that is exact, not an approximation:
        VelCurve is out = 127*(in/127)^g with g = 2.5^-curve, so running two
        curves in sequence multiplies the exponents - and multiplying 2.5^-a by
        2.5^-b is 2.5^-(a+b).  ENERGY is therefore a pure offset: every slot
        keeps its voicing relative to every other, and nothing has to be written
        into a slot to make the control work. */
    float getStyleEnergyCurveOffset() const noexcept
    { return (float) (getStyleEnergy() - 100) / 100.0f; }

    int getStyleEnergy() const noexcept
    { return styleEnergy.load (std::memory_order_relaxed); }

    /** Re-push the stored value.  ENERGY is not part of any slot's params, so
        nothing re-asserts it when the style slots are re-voiced; called beside
        the other startup pushes rather than reasoned about. */
    void pushStyleEnergy() noexcept { setStyleEnergy (getStyleEnergy()); }

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
    // The getter takes the FLAG and routes by the pack the engine says that
    // flag came from.  In Ballada that always resolves to GM - there is nowhere
    // else to route to - but the routing is kept rather than short-circuited so
    // this file stays a straight diff against Grex's copy.
    //==========================================================================
    juce::File getInstrumentPresetsFolder (int flag = -1) const
    {
        return folderManager.getPackPresetsFolder (
                   flag < 0 ? 0 : engine.getInstrumentPack (flag));
    }

    /** The read side.  ONE FOLDER PER PACK, merged - a pack ships its sounds
        and their voicing together, so both arrive and leave together. */
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

        // ── A MELODIC STYLE SLOT WRITES NO FILE, BECAUSE THE SET OWNS IT ────
        //
        // melodicPresetFor refuses style channels, so a .ins written from here
        // is never read back on this slot - it is dead weight. That alone would
        // only be waste. The harm is what it does to the RIGHT HAND: the file
        // is keyed by INSTRUMENT, so calibrating the PAD's strings rewrote the
        // base of the solo strings too, and a slot the user never touched
        // changed level. Two slots, one file, one of them not even reading it.
        //
        // The set is the melodic style slot's only voicing authority - the same
        // rule melodicPresetFor, effectiveBaseUnityDb and reassertStyleSlotVoicing
        // already enforce. So this pushes the value to the engine to make it
        // audible now and returns; SoundsTab has already put it into the slot's
        // SlotParams, and SAVE SET is what persists it.
        //
        // No schedulePresetReload either: there is no file to re-read, and
        // reapplyMelodicPreset skips style channels anyway.
        if (Betel::SamplePlayerEngine::isStyleChannel (channelIdx))
        {
            engine.setChannelInstrumentGainPercent (channelIdx, 100.0f, baseUnityDb);
            return true;
        }

        const int flag = engine.getChannelInstrumentFlag (channelIdx);
        if (flag < 0) return false;

        const juce::String name = engine.getInstrumentName (flag);
        const juce::String frb  = engine.getInstrumentFile (flag).getFileName();

        // ── SOLO SCOPE ONLY.  THE STYLE SCOPE IS NOT THIS EDIT'S TO WRITE ───
        //
        // This used to write BOTH files, and that is the same bug the style
        // branch above already documents, running the other way.  Up there:
        // "calibrating the PAD's strings rewrote the base of the solo strings
        // too, and a slot the user never touched changed level."  It was fixed
        // by making a style edit write no file - and the solo side was left
        // writing two.
        //
        // What that cost: setting the right-hand piano to +8 dB wrote a
        // STYLE-scope .ins for the same instrument, SamplePlayerEngine scans
        // those folders into styleInstrumentPresets and pools them, so every
        // style slot holding that sound came up 8 dB louder.  Nothing got
        // quieter; one instrument got louder everywhere, permanently, and every
        // other part had to be raised to catch it.
        //
        // It was unfixable from the UI too.  A style slot's base is owned by the
        // SET - melodicPresetFor refuses style channels - so nothing a user
        // could do to a style slot would ever rewrite that file, and
        // resetAllSoloBaseUnity cleared only the solo copy.  One edit wrote two
        // scopes and one reset cleared one.
        const auto a = Betel::InstrumentPresetIO::writeBaseUnity (
                           getInstrumentPresetsFolder (flag), flag, name, frb,
                           baseUnityDb, /*isDrum*/ false, /*isStyle*/ false);

        rescanInstrumentPresets();

        // Take effect on the edited channel immediately, then let the scheduled
        // reload carry it to every OTHER channel holding the same sound.
        engine.setChannelInstrumentGainPercent (channelIdx, 100.0f, baseUnityDb);
        schedulePresetReload (flag, /*isDrumKit*/ false);

        return a != juce::File();
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

    //==========================================================================
    //  EVERY SOLO INSTRUMENT'S BASE UNITY BACK TO 0 dB, IN ONE PASS.
    //
    //  The single-instrument path above is setInstrumentBaseUnity, and doing a
    //  whole library through it means opening the calibration dialog once per
    //  sound.  This is the same write, applied to every .ins in every installed
    //  pack's instruments_presets folder.
    //
    //  SOLO SET ONLY (.ins).  The .sins half of the split is not touched: a
    //  melodic STYLE slot takes its voicing from the SET rather than from a
    //  preset file (melodicPresetFor refuses style channels outright), so the
    //  style files are not what any solo base gain is read from.
    //
    //  AND IT IS AUDIBLE IMMEDIATELY.  Rewriting the files alone would leave
    //  every currently-loaded sound on the base it was loaded with until the
    //  next program change, so the pass ends by re-reading the folder and
    //  re-applying to each solo channel - the same rescan-then-reapply the
    //  scheduled reload does after a single save, run inline because there is
    //  no burst of edits to coalesce here.
    //
    //  Returns the number of files that actually changed.
    //==========================================================================
    int resetAllSoloBaseUnity()
    {
        int written = 0;

        for (const auto& folder : allInstrumentPresetsFolders())
            written += Betel::InstrumentPresetIO::resetBaseUnityInFolder (folder,
                                                                          /*isStyle*/ false);

        // AND THE STYLE SCOPE, because until now nothing could clear it.  Solo
        // edits used to write style-scope files that no style edit could ever
        // rewrite and this reset did not touch, so a stray calibration was
        // permanent.  Nothing writes them any more - see setInstrumentBaseUnity -
        // but the ones already on disk still have to be removable.
        for (const auto& folder : allStyleInstrumentPresetsFolders())
            written += Betel::InstrumentPresetIO::resetBaseUnityInFolder (folder,
                                                                          /*isStyle*/ true);

        rescanInstrumentPresets();

        for (int sl = 0; sl < Betel::SamplePlayerEngine::kNumSoloChannels; ++sl)
        {
            const int ch   = Betel::SamplePlayerEngine::kNumStyleChannels + sl;
            const int flag = engine.getChannelInstrumentFlag (ch);
            if (flag < 0) continue;

            engine.reapplyMelodicPreset (flag);

            // Let the SOUNDS tab adopt the new value too, or its editor would
            // keep showing the old base until the slot was re-picked.
            if (onPresetReloaded) onPresetReloaded (flag, /*isDrumKit*/ false);
        }

        return written;
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
    /** PASS channelIdx WHENEVER YOU HAVE IT.  A Revo! style's kit is redirected
        to Standard at compose time (see revoFallbackFamily), and that redirect
        is keyed on the CHANNEL'S drum bank — so without the channel this
        returns the kit the PC maps to rather than the one that is sounding.
        Omitting it is only correct where no channel is in hand. */
    juce::String getKitNameForKitKey (int unityKey, int channelIdx = -1) const
    {
        return engine.kitNameForUnityKey (unityKey, channelIdx);
    }

    //==========================================================================
    // PER-KIT BASE UNITY AS A SET BLOCK
    //
    // One property per kit, named "k<unityKey>" because a ValueTree property is
    // a juce::Identifier and cannot hold the '/' a bank/program key would want.
    // Absent block = a set written before this existed, and absent means "no
    // opinion": whatever the .drm or the load path applied stands.
    //==========================================================================
    //==========================================================================
    // SECTION FX — the two shared reverb/delay buses, as a set block.
    //
    // ONE BLOCK FOR BOTH SECTIONS, indexed 0 = LEFT (style + drums), 1 = RIGHT
    // (solo).  These are not per-slot and never were: there is exactly one delay
    // and one reverb behind each section, so a set carries two of each and every
    // slot in a section reads the same values.
    //
    // The per-slot reverb/delay fields still in SoundsState are the SENDS now
    // (see Channel::applyParams) - `reverbWet` and `delayWet` are read, the rest
    // are inert and will fall out of the file on its next save.
    //==========================================================================
    //==========================================================================
    // THE REVERB / DELAY PAGES EDIT THE BUS, NOT THE SLOT
    //
    // EffectsPanel is templated over the params struct and has no idea an engine
    // exists - it reads and writes SlotParams fields and nothing else.  Rather
    // than teach it about section buses, the two fields it exchanges are
    // OVERLAID at the seam: the bus's values are written into the SlotParams
    // copy just before the page is seeded, and read back out of it just after
    // the user moves something.
    //
    // The page therefore shows the same reverb from every slot in a section,
    // which is the requirement, and does it without a single new control.
    //
    // WHAT IS NOT OVERLAID: reverbWet and delayWet.  Those are the per-slot
    // SENDS - the one genuinely per-instrument thing on either page - and
    // overwriting them from the bus would make every slot in a section share one
    // send amount, which is the opposite of the point.
    //
    // `section` is 0 = LEFT (style + drums), 1 = RIGHT (solo).
    //==========================================================================
    /** The section's global effect rack, for the GLOBAL EFFECTS window.

        Handed out whole rather than proxied field by field: the window binds
        EffectsPanel to it through SectionFxBridge, which lives with the window
        because it speaks FamilyFxParams — a UI type this file has no business
        including. */
    Betel::SectionSendFx&       sectionFx (int s)       noexcept { return engine.sectionFx (s); }
    const Betel::SectionSendFx& sectionFx (int s) const noexcept { return engine.sectionFx (s); }

    // readSectionFxInto / writeSectionFxFrom are GONE.
    //
    // They bridged the section rack to a SlotParams so the per-slot REVERB and
    // DELAY pages could edit it.  Those pages are gone — the editor's third tab
    // is EFFECT SENDS now — and the rack is reached through sectionFx() above,
    // which the GLOBAL EFFECTS window binds to EffectsPanel via
    // SectionFxBridge.  One route, one owner.

    //==========================================================================
    // SECTION FX — all six global effects as one set block.
    //
    // 0 = LEFT (style + drums), 1 = RIGHT (solo), three effects each — chorus,
    // reverb and delay.  Wah, phaser and sweetener are not here: they are
    // per-instrument inserts and travel in SoundsState with their slot.  None of it
    // is per-slot: there is exactly one of each behind a section, so a set
    // carries two of each and every slot in a section reads the same values.
    //
    // The per-slot reverb/delay/chorus/wah/phaser fields still in SoundsState
    // are the SENDS now (see Channel::applyParams) — their mix and wet values
    // are read, the rest are inert and fall out of the file on its next save.
    //==========================================================================
    juce::ValueTree captureSectionFxState() const
    {
        using S = Betel::SectionSendFx;
        juce::ValueTree t ("SectionFx");

        for (int i = 0; i < 2; ++i)
        {
            const auto& fx = engine.sectionFx (i);
            juce::ValueTree b (i == 0 ? "Left" : "Right");

            for (int sl = 0; sl < S::kNumSlots; ++sl)
            {
                const juce::String n (S::slotName (sl));
                b.setProperty (n + "On",    fx.enabled[(size_t) sl].load(), nullptr);
                b.setProperty (n + "Level", fx.level  [(size_t) sl].load(), nullptr);
            }

            b.setProperty ("chorusRateHz", fx.chorusRateHz.load(), nullptr);
            b.setProperty ("chorusDepth",  fx.chorusDepth .load(), nullptr);
            b.setProperty ("chorusCentre", fx.chorusCentreMs.load(), nullptr);

            // Wah and phaser are NOT here.  They went back to being per-instrument
            // inserts — a shaper cannot be fed by a send, and they need their own
            // settings per slot — so their parameters live in the slot state with
            // the rest of that instrument, not in this block.
            b.setProperty ("reverbSize",   fx.reverbSize   .load(), nullptr);
            b.setProperty ("reverbDamp",   fx.reverbDamp   .load(), nullptr);
            b.setProperty ("reverbTail",   fx.reverbTail   .load(), nullptr);
            b.setProperty ("reverbPreDelay",fx.reverbPreDelay.load(), nullptr);
            b.setProperty ("reverbHpNorm", fx.reverbHpNorm .load(), nullptr);
            b.setProperty ("reverbLpNorm", fx.reverbLpNorm .load(), nullptr);
            b.setProperty ("reverbErMix",  fx.reverbErMix  .load(), nullptr);
            b.setProperty ("reverbErSize", fx.reverbErSize .load(), nullptr);
            b.setProperty ("reverbAlgo",   fx.reverbAlgo   .load(), nullptr);

            b.setProperty ("delayFeedback",fx.delayFeedback.load(), nullptr);
            b.setProperty ("delayDampHz",  fx.delayDampHz  .load(), nullptr);
            b.setProperty ("delayHpHz",    fx.delayHpHz    .load(), nullptr);
            b.setProperty ("delaySmoothMs",fx.delaySmoothMs.load(), nullptr);
            b.setProperty ("delayTimeSig", fx.delayTimeSig .load(), nullptr);
            b.setProperty ("delayDiv",     fx.delayDiv     .load(), nullptr);

            t.appendChild (b, nullptr);
        }
        return t;
    }

    void applySectionFxState (const juce::ValueTree& t)
    {
        using S = Betel::SectionSendFx;
        if (! t.isValid() || ! t.hasType ("SectionFx")) return;

        for (int i = 0; i < 2; ++i)
        {
            const auto b = t.getChildWithName (i == 0 ? "Left" : "Right");
            if (! b.isValid()) continue;                 // absent means unchanged

            auto& fx = engine.sectionFx (i);
            auto f = [&b] (const char* k, float d) { return (float) (double) b.getProperty (k, d); };
            auto n = [&b] (const char* k, int   d) { return (int)          b.getProperty (k, d); };

            for (int sl = 0; sl < S::kNumSlots; ++sl)
            {
                const juce::String nm (S::slotName (sl));
                fx.enabled[(size_t) sl].store ((bool) b.getProperty (nm + "On",
                                                fx.enabled[(size_t) sl].load()));
                fx.level  [(size_t) sl].store (juce::jlimit (0.0f, 2.0f,
                        (float) (double) b.getProperty (nm + "Level",
                                                        fx.level[(size_t) sl].load())));
            }

            fx.chorusRateHz.store (juce::jlimit (0.01f, 20.0f, f ("chorusRateHz", fx.chorusRateHz.load())));
            fx.chorusDepth .store (juce::jlimit (0.0f,  1.0f,  f ("chorusDepth",  fx.chorusDepth .load())));
            fx.chorusCentreMs.store (juce::jlimit (1.0f, 50.0f, f ("chorusCentre", fx.chorusCentreMs.load())));

            // Wah and phaser: see captureSectionFxState.  A set written before
            // they moved still carries those properties; they are simply not
            // read, which is the same "absent means unchanged" rule this whole
            // block follows.
            fx.reverbSize   .store (juce::jlimit (0.0f, 1.0f, f ("reverbSize",     fx.reverbSize.load())));
            fx.reverbDamp   .store (juce::jlimit (0.0f, 1.0f, f ("reverbDamp",     fx.reverbDamp.load())));
            fx.reverbTail   .store (juce::jlimit (0.0f, 1.0f, f ("reverbTail",     fx.reverbTail.load())));
            fx.reverbPreDelay.store(juce::jlimit (0.0f, 1.0f, f ("reverbPreDelay", fx.reverbPreDelay.load())));
            fx.reverbHpNorm .store (juce::jlimit (0.0f, 1.0f, f ("reverbHpNorm",   fx.reverbHpNorm.load())));
            fx.reverbLpNorm .store (juce::jlimit (0.0f, 1.0f, f ("reverbLpNorm",   fx.reverbLpNorm.load())));

            // A set saved before early reflections existed carries neither
            // property, so both fall back to the live value - which on a fresh
            // load is the 0.0 default, i.e. the reverb exactly as it was.  That
            // is the whole reason the default had to be a no-op.
            fx.reverbErMix  .store (juce::jlimit (0.0f, 1.0f, f ("reverbErMix",    fx.reverbErMix.load())));
            fx.reverbErSize .store (juce::jlimit (0.0f, 1.0f, f ("reverbErSize",   fx.reverbErSize.load())));

            fx.reverbAlgo   .store (juce::jlimit (0, 1,       n ("reverbAlgo",     fx.reverbAlgo.load())));

            fx.delayFeedback.store (juce::jlimit (0.0f, 0.95f, f ("delayFeedback", fx.delayFeedback.load())));

            // Absent in a set written before these existed, so all three fall
            // back to the live value - which on a fresh load is the old
            // hard-coded behaviour.
            fx.delayDampHz  .store (juce::jlimit (200.0f, 20000.0f, f ("delayDampHz",   fx.delayDampHz.load())));
            fx.delayHpHz    .store (juce::jlimit (20.0f,  2000.0f,  f ("delayHpHz",     fx.delayHpHz.load())));
            fx.delaySmoothMs.store (juce::jlimit (1.0f,   500.0f,   f ("delaySmoothMs", fx.delaySmoothMs.load())));
            fx.delayTimeSig .store (juce::jlimit (0, 1,        n ("delayTimeSig",  fx.delayTimeSig.load())));
            fx.delayDiv     .store (juce::jmax  (0,            n ("delayDiv",      fx.delayDiv.load())));
        }
    }

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

            // EDM KIT: a synth kit is saved with the set, never as a sampled
            // kit's default.  "EDM Dance".getIntValue() is 0, so writing it
            // here would overwrite the STANDARD kit's default file.
            if (kit.startsWith ("EDM ")) return {};
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

    //==========================================================================
    // STYLE BUS BASE UNITY -- what the STYLE VOLUME fader's unity detent is worth.
    //
    // PER SET, and that is the difference from the solo base above.  The solo
    // base describes this INSTALL's balance between the player's two hands,
    // which no song changes.  This one describes how loud the band should sit
    // for THIS song, so it travels in MixerState beside the faders.
    //
    // The FADER IS NOT MOVED -- same rule as the solo base and the crash box.
    //==========================================================================
    static constexpr float kDefaultStyleBaseUnityDb =   0.0f;
    static constexpr float kMinStyleBaseUnityDb     = -24.0f;
    static constexpr float kMaxStyleBaseUnityDb     =  24.0f;

    void setStyleBaseUnityDb (float dB)
    {
        const float v = juce::jlimit (kMinStyleBaseUnityDb, kMaxStyleBaseUnityDb, dB);
        styleBaseUnityDb.store (v);
        engine.setStyleBusBaseUnity (std::pow (10.0f, v / 20.0f));
    }

    float getStyleBaseUnityDb() const noexcept { return styleBaseUnityDb.load(); }

    //==========================================================================
    // THE STYLE DUCKER -- lives in the engine because it acts on the style bus
    // before the sum; exposed here so the editor can reach it.  Its window is
    // a TAB of the Finisher, which is a UI grouping and not a signal one.
    //==========================================================================
    Betel::StyleDucker& getDucker() noexcept             { return engine.getDucker(); }
    const Betel::StyleDucker& getDucker() const noexcept { return engine.getDucker(); }
    float getRightHandVolume() const                              { return engine.getSoloBusGain(); }
    /** The master fader gain BEFORE the boost is folded in — i.e. what the
        mixer's master fader shows.  applyMasterOut() multiplies this by the
        boost on its way to the engine, so reading the engine back would return
        the product and a save/restore round trip would compound it. */
    float getMasterVolume  () const noexcept                      { return masterFaderGain.load(); }
    void setChannelVolume  (int channelIdx, float linearGain)     { engine.setChannelVolume(channelIdx, linearGain); }

    /** Every user fader back to 127 - "whatever the style asks for".  Called by
        the EDITOR at the two moments that mean a fresh mix: a set load (just
        before the set applies its own faders) and a bare style load (where no
        set is coming).  Deliberately NOT called per style adopt - see the note
        in adoptFreshStyle. */
    /** THE GLOBAL STYLE BOOST, in dB.  One entry point for every caller - the
        GLOBAL SETTINGS slider and the MIDI remote - because it has to do three
        things in order and doing two of them is what made the slider look dead:
        store the value, write grex_boost.xml, and push it to the style bus. */
    void setStyleBoostDb (float db)
    {
        Betel::StyleLevels::get().setBoostDbAndSave (db);
        stylePlayer.pushStyleBoost();
    }

    float getStyleBoostDb() const { return Betel::StyleLevels::get().boostDb(); }

    void setChannelFunkeyFx (int ch, const Betel::Channel::FunkeyFx& f)
    { engine.setChannelFunkeyFx (ch, f); }

    /** THE FUNKEY MIX, 0..1 - one value for every funkeyed instrument, owned
        by the SET (it travels in the set's UiState; see
        GlobalMacros::kFunkeyMixDefault).  One entry point that does both
        halves: store the live value, then push it to the engine.  Doing only
        the first is how a slider looks dead. */
    void setFunkeyMix (float mix01)
    {
        Betel::GlobalMacros::get().setFunkeyMix (mix01);
        engine.setFunkeyMix (Betel::GlobalMacros::get().funkeyMix());
    }

    float getFunkeyMix() const { return Betel::GlobalMacros::get().funkeyMix(); }

    void resetUserVolumesToUnity() { engine.resetUserVolumesToUnity(); }

    //==========================================================================
    //  WHERE A STYLE'S SET LIVES — ON THE PROCESSOR, NOT THE EDITOR.
    //
    //  This used to be MainComponent::setFileForStyle, and it had to move.  The
    //  editor applies sets, but the editor is not always THERE: a DAW project
    //  reopened with the window closed never ran applySetPayload at all.  That
    //  was survivable while CC 7 seeded every fader on every load and the set
    //  merely overwrote it afterwards.  It stopped being survivable the moment
    //  CC 7 stood down for styles that have a set - the set became the only
    //  thing setting those levels, so the processor has to be able to find and
    //  apply it on its own.
    //
    //  The editor now calls these too, so there is ONE spelling of the rule.
    //==========================================================================

    /** The .bset a style would use, whether or not it exists.  Invalid File if
        the root folder is not set. */
    juce::File setFileForStyleRef (const juce::String& styleRef) const;

    /** The style's set as a tree, or an invalid tree if there is no set file or
        it will not parse. */
    juce::ValueTree setTreeForStyleRef (const juce::String& styleRef) const;

    /** The MixerState node of that set, or an invalid tree.

        THE TEST IS THE NODE, NOT THE FILE.  A .bset can hold only <StyleSlots> -
        applySetPayload handles exactly that case - and a slots-only set must NOT
        suppress CC 7, or every fader would sit wherever the reset left it with
        nothing to state otherwise. */
    juce::ValueTree mixerSetForStyleRef (const juce::String& styleRef) const;

    /** The POST base trim on a style mixer fader, in dB. A third factor on top
        of the style's own level and the fader; it never changes what the style
        sent. Saved in the set, reset to 0 dB on a style load. */
    void  setChannelUserBaseDb (int channelIdx, float db) { engine.setChannelUserBaseDb (channelIdx, db); }
    float getChannelUserBaseDb (int channelIdx) const     { return engine.getChannelUserBaseDb (channelIdx); }
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

    // Per-set STYLE VOLUME base unity in dB.  Held here rather than read back
    // from the engine because the engine stores the LINEAR gain, and a
    // dB -> linear -> dB round trip through a save file drifts.
    std::atomic<float>        styleBaseUnityDb { kDefaultStyleBaseUnityDb };
    std::atomic<int>          lastMidiSeq      { 0 };


    // THE STYLES FOLDER, scanned for loose .fst files. The blob library it
    // replaced is gone entirely - see FstLibrary.h for why the filename is
    // now the style id.
    Betel::FstLibrary styleLibrary;
    juce::String                   currentStyleRef;

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
    std::atomic<bool>                              projectStateRestored { false };
    std::atomic<uint8_t>                           soloEnableMask { 0 };
    // Harmony. See the setters above for why these are relaxed.
    // Multi split. See setMultiSplitEnabled for the zone model.
    // Manual bass / bass to lowest. See setManualBassEnabled.
    std::atomic<bool> manualBassOn      { false };
    std::atomic<bool> bassInversionOn  { false };
    std::atomic<int>  bassInversionMode { (int) BassInversionMode::Always };
    std::atomic<bool> multiSplitOn   { false };
    std::atomic<int>  bassSplitPoint { 48 };      // C3
    std::atomic<int>  bassZoneSlot   { kBassSlotDefault };
    std::atomic<bool> harmonyOn    { false };
    std::atomic<int>  harmonyType  { (int) Betel::Harmonizer::Type::Duet };
    std::atomic<bool> harmonyBelow { true };
    std::atomic<int>  harmonyLevel { 82 };
    std::atomic<int>                               globalTranspose { 0 };
    std::atomic<bool>                              pianoMode { false };

    // Song recorder / player (see GrexSongRecorder.h / GrexSongPlayer.h).
    // perfSongBeats is the monotonic performance beat clock used to timestamp
    // recorded events; it is touched only on the audio thread.
    GrexSongRecorder                               songRecorder;
    GrexSongRecorder::Setup                        songSetup;
    GrexSongPlayer                                 songPlayer;
    double                                         perfSongBeats { 0.0 };

    /** A variation gesture waiting to be written into the song.
        Parked by whichever thread made it, drained by the audio thread - see
        performVariation for why it cannot be recorded where it happens. */
    std::atomic<int>                               pendingUiGesture { -1 };
    std::atomic<bool>                              restartRequested { false };
    // Raised by togglePlayStop when it cannot start for want of a style;
    // drained by the editor's mirror, which does the recovery.  See
    // notePlayRefusedNoStyle above.
    std::atomic<bool> playRefusedNoStyle { false };
    // Tracks the sequencer's playing state across audio blocks so we can flush
    // hung style notes on the playing->stopped edge (audio thread only).
    bool                                           wasSequencerPlaying { false };
    // DAW START -- when enabled, style playback follows the host transport
    // (rising edge starts, falling edge stops).  wasHostPlaying: audio only.
    std::atomic<bool>                              dawStartFollow { false };
    bool                                           wasHostPlaying { false };
    juce::String                                   comments;

    // Dispatch a reserved control-note (0–35) to its action.  Audio thread.
    /** Drive one mapped control.  `value` is 0-127 (a CC value, or 127 for a
        pad). Processor state only — see the comment on the definition. */
    void performRemote (Betel::RemoteId id, int value);

    /** The RELEASE half of RemoteMap::Mode::Push - see performRemoteRelease in
        Main.cpp.  Only stateful controls have anything to give back. */
    void performRemoteRelease (Betel::RemoteId id);

    /** True once the player has assigned ANY pad, which retires the legacy
        0-35 control-note block. See the note dispatch in processBlock. */
    /** Set by the audio thread when a remote moved something; the editor's
        30 Hz mirror consumes it. */
    std::atomic<bool> remoteTouched { false };



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
    //
    // manualBPM is DERIVED from the two below and is the only one processBlock
    // reads - see the tempo block in the public section for the invariant.
    std::atomic<float>                             manualBPM { 120.0f };
    std::atomic<float>                             styleBaseBPM { 120.0f };  // the loaded style's own tempo
    std::atomic<float>                             userBpmDelta { 0.0f };    // the player's nudge, in BPM. SAVED WITH THE SET

    // ENERGY, 0..100, 50 = as authored.  SAVED WITH THE SET, like the tempo
    // nudge and for the same reason: how hard the band plays belongs to the
    // song, not to the installation.
    std::atomic<int>                               styleEnergy  { 100 };
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
