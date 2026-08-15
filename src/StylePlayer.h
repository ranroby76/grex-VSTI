#pragma once
//==============================================================================
// StylePlayer.h
//
// Step 4 of the engine pipeline. Two collaborators live in here:
//
//   1) ChordZoneTracker — receives every incoming MIDI noteOn/noteOff that
//      falls BELOW the keyboard split point, maintains the set of currently
//      held chord-zone notes, and re-runs ChordRecognizer on every change.
//      Chord-hold semantics: when the user lifts all chord-zone keys the last
//      recognised chord is retained (matches Yamaha "Memory ON" default).
//
//      Sync-start: when armSyncStart() has been called, the tracker fires
//      onSyncStartTriggered() on the first noteOn that lands in an empty
//      buffer, then disarms itself.  The callback is invoked on the audio
//      thread; the host should only do thread-safe things there (e.g.
//      sequencer.start() — which just sets an atomic).
//
//   2) StylePlayer — consumes the EmittedStyleEvent stream from
//      StyleSequencer::renderBlock() and turns each event into a real engine
//      call.  For note events, NoteTransposer applies the CASM-driven
//      transposition.  For control changes, program changes, and pitch
//      bends, the dispatcher routes them straight to the destination
//      engine channel.  Voice setup (bank + PC + volume) is applied when
//      a style is loaded.
//
//      Channel mapping — IMPORTANT:
//        Yamaha SFF places its 8 source channels on MIDI ch 9-16 (zero-based
//        8-15) — RHY1, RHY2, BASS, CHD1, CHD2, PAD, PHR1, PHR2.  In
//        Betelgeuse, the 8 user-visible style slots in SoundsTab are
//        engine channels 0-7 (DRUMS / PERC / BASS / CHORD 1 / CHORD 2 /
//        PAD / LEAD 1 / LEAD 2 in MainTab's order).  So every destination
//        channel computed from a style event (whether straight from the
//        event or routed through CASM dstChannel) is mapped via
//        mapSourceToEngineChannel() before reaching the engine.
//
//      Mute mask — channel-level enable/disable controlled by the 8 STYLE
//      ELEMENTS ON/OFF toggles in MainTab.  When a bit in muteMask is set,
//      note-on / CC / PC / pitch-bend events for that channel are dropped;
//      note-offs still pass through so any held notes get released
//      cleanly.  Calling setChannelMute(ch, true) also issues an immediate
//      engine.allNotesOff(ch) for the target channel.
//
// Threading: ChordZoneTracker and StylePlayer are designed to be called
// from the audio thread only.  No allocations occur on note events; the
// held-notes array is fixed-size and the dispatch loop only reads
// pointers that the sequencer's atomics already guarantee valid.  The
// mute mask, sync-start flag, and the sync-start callback are written
// from the message thread (UI clicks) and read from the audio thread.
//==============================================================================

#include <JuceHeader.h>
#include "GlobalMacros.h"
#include "StyleLevels.h"    // section boost / makeup target / role fader trims
#include "PerfMonitor.h"
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>

#include "SamplePlayerEngine.h"
#include "StyleData.h"
#include "StyleSequencer.h"
#include "ChordTransposer.h"
#include "BankProgramMap.h"

namespace Betel
{
    //==========================================================================
    // ChordZoneTracker
    //==========================================================================
    class ChordZoneTracker
    {
    public:
        ChordZoneTracker() { reset(); }

        void setSplitPoint (int midiNote) noexcept
        {
            splitPoint.store (juce::jlimit (0, 127, midiNote));
        }
        int  getSplitPoint() const noexcept { return splitPoint.load(); }

        bool isChordZone (int midiNote) const noexcept
        {
            return midiNote < splitPoint.load();
        }

        // ── Chord recognition mode (FINGERED vs SINGLE_FINGER) ────────────────
        //
        // FINGERED: full chord recognition — user plays the actual chord notes
        // (Cmaj = C-E-G, Cmin = C-Eb-G, Cmaj7 = C-E-G-B, etc.).  Recognizer
        // scores every (root, quality) combination and picks the best fit.
        //
        // SINGLE_FINGER ("1 Finger"): the JUST-PRESSED chord-zone key is the
        // chord root.  Pressing the SAME root again flips the quality
        // major <-> minor (tap = major; release and tap again = minor; again =
        // major; and so on); pressing a DIFFERENT root starts fresh at major.
        // The chord latches (sample-and-hold) until the next press, so
        // releasing keys never changes it.  4-note+ held chords are NOT
        // promoted to FINGERED here — single-finger is purely the tap toggle.
        enum class ChordMode : uint8_t { Fingered = 0, SingleFinger = 1 };

        void      setChordMode (ChordMode m) noexcept { chordMode.store ((uint8_t) m); }
        ChordMode getChordMode() const       noexcept { return (ChordMode) chordMode.load(); }

        // ── Sync-start ────────────────────────────────────────────────────────
        //
        // Set the callback once at construction time (from MainComponent).
        // Then arm/disarm via armSyncStart() / disarmSyncStart() from UI.
        // The first chord-zone noteOn that arrives with the buffer empty
        // fires the callback and clears the armed flag (one-shot).
        std::function<void()> onSyncStartTriggered;

        void armSyncStart()    noexcept { syncStartArmed.store (true);  }
        void disarmSyncStart() noexcept { syncStartArmed.store (false); }
        bool isSyncStartArmed() const noexcept { return syncStartArmed.load(); }

        // ── Sync-stop ─────────────────────────────────────────────────────────
        //
        // Mirror of sync-start.  When armed and a chord-zone noteOff empties
        // the held buffer, the callback fires and the flag clears.  Yamaha's
        // "Sync Stop" semantics: lifting all chord-zone keys halts the band.
        std::function<void()> onSyncStopTriggered;

        void armSyncStop()    noexcept { syncStopArmed.store (true);  }
        void disarmSyncStop() noexcept { syncStopArmed.store (false); }
        bool isSyncStopArmed() const noexcept { return syncStopArmed.load(); }

        // ── Gate mode (ONPRESS) ───────────────────────────────────────────────
        // When active, the style plays only while a chord is held: every
        // empty→held transition starts it, every held→empty transition stops
        // it.  Unlike sync-start/stop these are not one-shot — they re-arm
        // automatically each press/release cycle.
        void setGateMode (bool on) noexcept { gateModeActive.store (on); }
        bool isGateMode() const noexcept    { return gateModeActive.load(); }

        /** Add a note to the chord zone. No-op if the note is already held
            or the buffer is full (16 simultaneous notes is far more than
            any chord we need to recognise). */
        void noteOn (int midiNote)
        {
            for (int i = 0; i < numHeld; ++i)
                if (held[(size_t) i] == midiNote) return;       // already in
            if (numHeld >= (int) held.size()) return;          // full

            const bool wasEmpty = (numHeld == 0);
            held[(size_t) numHeld++] = midiNote;

            // ── SINGLE-FINGER: last-note priority (note stealing) ─────────────
            // In single-finger mode the JUST-PRESSED key is ALWAYS the new chord
            // root, even when previous chord keys are still held — so a legato
            // chord change (press the new key before fully releasing the old)
            // switches immediately instead of being decided by whichever held
            // key happens to be highest.  Pressing the SAME root again flips
            // major ↔ minor; a different root starts fresh at major.  (Fingered
            // mode keeps full recompute() recognition over all held notes.)
            if (getChordMode() == ChordMode::SingleFinger)
            {
                const int root = ((midiNote % 12) + 12) % 12;
                if (root == sfToggleRoot)
                    sfToggleMinor = ! sfToggleMinor;
                else
                {
                    sfToggleRoot  = root;
                    sfToggleMinor = false;
                }
                currentChord.store (packChord ({ root,
                                                 sfToggleMinor ? ChordQuality::Min
                                                               : ChordQuality::Maj }));
            }
            else
            {
                recompute();
            }

            // Fire the start trigger when the buffer goes from empty→held if
            // EITHER a one-shot sync-start was armed OR gate (ONPRESS) mode is
            // active.  In gate mode the trigger stays live so every fresh
            // chord press re-starts the style.
            if (wasEmpty && (syncStartArmed.exchange (false) || gateModeActive.load()))
            {
                if (onSyncStartTriggered) onSyncStartTriggered();
            }
        }

        void noteOff (int midiNote)
        {
            for (int i = 0; i < numHeld; ++i)
            {
                if (held[(size_t) i] == midiNote)
                {
                    held[(size_t) i] = held[(size_t) (numHeld - 1)];
                    --numHeld;

                    // True sample-and-hold: releasing keys NEVER re-recognises
                    // the chord.  It is latched on note-on and held verbatim
                    // until the next chord is pressed, so lifting fingers — even
                    // one at a time and unevenly — can no longer collapse a held
                    // triad through its half-released two- and one-note partials
                    // (the old recompute-on-release "ruined the chord" bug).
                    if (numHeld == 0
                        && (syncStopArmed.exchange (false) || gateModeActive.load()))
                    {
                        if (onSyncStopTriggered) onSyncStopTriggered();
                    }
                    return;
                }
            }
        }

        void reset() noexcept
        {
            numHeld = 0;
            sfToggleRoot  = -1;
            sfToggleMinor = false;
            // Default to CMaj so the band has something coherent to play
            // even when the user hasn't yet pressed any chord-zone keys.
            currentChord.store (packChord ({ 0, ChordQuality::Maj }));
        }

        Chord getCurrentChord() const noexcept
        {
            return unpackChord (currentChord.load());
        }

    private:
        void recompute()
        {
            std::vector<int> snapshot;
            snapshot.reserve ((size_t) numHeld);
            for (int i = 0; i < numHeld; ++i)
                snapshot.push_back (held[(size_t) i]);
            const Chord c = (getChordMode() == ChordMode::SingleFinger)
                              ? ChordRecognizer::recognizeSingleFinger (snapshot)
                              : ChordRecognizer::recognize             (snapshot);
            if (c.quality != ChordQuality::Unknown)
                currentChord.store (packChord (c));
        }

        // Pack/unpack Chord into a single 16-bit atomic for lock-free read.
        static uint16_t packChord  (Chord c) noexcept
        {
            return (uint16_t) (((uint8_t) c.quality << 8) | (uint8_t) (c.root & 0x0F));
        }
        static Chord    unpackChord (uint16_t v) noexcept
        {
            Chord c;
            c.root    = (int) (v & 0x0F);
            c.quality = (ChordQuality) ((v >> 8) & 0xFF);
            return c;
        }

        std::array<int, 16>    held;
        int                    numHeld     = 0;
        // SINGLE-FINGER toggle state: last single-pressed root (pitch class,
        // -1 = none yet) and its current maj/min flip.  Audio-thread-only,
        // like `held` — no atomics needed.
        int                    sfToggleRoot  = -1;
        bool                   sfToggleMinor = false;
        std::atomic<int>       splitPoint { 60 };   // Middle C — adjustable from UI
        std::atomic<uint16_t>  currentChord { 0 };  // CMaj
        std::atomic<bool>      syncStartArmed { false };
        std::atomic<bool>      syncStopArmed  { false };
        std::atomic<bool>      gateModeActive { false };   // ONPRESS
        std::atomic<uint8_t>   chordMode      { 0 };   // 0 = Fingered (default)
    };

    //==========================================================================
    // StylePlayer
    //==========================================================================
    class StylePlayer
    {
    public:
        // Number of user-visible style slots = engine channels 0..7
        static constexpr int kNumUserStyleSlots = 8;

        // Engine slot order: DRUMS, PERC, BASS, CHORD1, CHORD2, PAD, LEAD1, LEAD2.
        static constexpr int kBassEngineSlot = 2;   // BASS — target of the allowed-notes fold
        static constexpr int kPadEngineSlot  = 5;   // PAD  — glides immediately on chord change

        explicit StylePlayer (SamplePlayerEngine& eng) : engine (eng)
        {
            for (auto& s : slotSubFlag) s.store (-1);

            // Allowed-notes defaults: every slot seeded to A#1..A2 (a valid
            // 12-note window) so toggling one on starts from a sane range.
            // Only BASS (slot 2) is ON by default, and its floor is dropped to
            // C1 so low bass notes (incl. ending roots) aren't folded up.
            for (int i = 0; i < kNumUserStyleSlots; ++i)
            {
                noteRangeLo[(size_t) i].store (i == kBassEngineSlot ? 24 : 34);  // 24 = C1
                noteRangeHi[(size_t) i].store (45);
                noteRangeOn[(size_t) i].store (i == kBassEngineSlot);
            }

            // The bass part is monophonic on hardware — run the bass slot in Mono
            // by default so overlapping/over-long style bass notes can't stack
            // into a two-pitch clash.  Independent flags: fall back to a held
            // note on release, and give each new note a fresh attack (articulate
            // bass); the returned note glides (no re-attack).
            engine.getChannel (kBassEngineSlot)
                  .setMonoConfig (/*mono*/ true, /*holdStolen*/ true,
                                  /*retrigNew*/ true, /*retrigStolen*/ false);

            for (auto& row : lastStyleCC) row.fill (-1);   // CC "changed" gate
            lastStylePB.fill (-1);                          // PB "changed" gate

            musicalLo.fill (-1);   // no style loaded yet — see applyVoiceSetup
            musicalHi.fill (-1);
        }

        // Style flag currently on a slot: 0..127 = GM voice, >=200 = reference
        // blob, -1 = empty.  Lets the UI apply GM instrument-type envelope
        // defaults to the slots a freshly-loaded style populated.
        int getSlotStyleFlag (int slot) const noexcept
        {
            return (slot >= 0 && slot < kNumUserStyleSlots)
                     ? slotStyleFlag[(size_t) slot] : -1;
        }

        // ── Per-channel "allowed notes" window ────────────────────────────
        // Each melodic style slot (0..kNumUserStyleSlots-1) can fold its final
        // sounding pitch into a register window [lo, hi].  Set live from the
        // per-instrument sound editor (SynthesisPanel "ALLOWED NOTES"); read on
        // the audio thread by foldNote().  The window is always ≥ 12 notes
        // (hi-lo ≥ 11) so the octave-fold always converges.  Bass (slot 2) is
        // ON by default at A#1..A2; the other slots default OFF.
        void setNoteRange (int engineCh, bool on, int lo, int hi) noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return;
            lo = juce::jlimit (0, 127, lo);
            hi = juce::jlimit (0, 127, hi);
            if (hi < lo) { const int t = lo; lo = hi; hi = t; }
            if (hi - lo < 11) hi = juce::jmin (127, lo + 11);   // enforce 12-note floor
            if (hi - lo < 11) lo = juce::jmax (0,  hi - 11);
            noteRangeLo[(size_t) engineCh].store (lo);
            noteRangeHi[(size_t) engineCh].store (hi);
            noteRangeOn[(size_t) engineCh].store (on);
        }

        /** Push every event in this block into the engine after applying
            NTR/NTT.  Should be called from the audio thread, AFTER
            StyleSequencer has filled `events` for the current block.

            Chord changes are deferred to the nearest 1/8-note boundary
            ONLY WHILE THE SEQUENCER IS ADVANCING.  When ticks aren't
            moving (paused, stopped, pre-roll, transport reset) there's
            no musical timeline to defer against, so chord changes commit
            immediately — otherwise the chord would never propagate to
            the dispatch path and the user would see "chord switching not
            working" because `lastDispatchChord` stays stuck at whatever
            was latched the last time ticks actually moved.

            Parameters:
              • events           - emitted events for this block
              • currentChord     - chord the user is holding right now
              • currentTick      - sequencer's tick position at end-of-block
                                   (relative to the current section)
              • ticksPerEighth   - the style's 1/8-note grid in ticks
                                   (typically TPQ / 2) */
        void dispatchBlock (const std::vector<EmittedStyleEvent>& events,
                            const Chord& currentChord,
                            int currentTick,
                            int ticksPerEighth,
                            bool fingeredMode)
        {
            // Latch the chord-recognition mode for this block.  dispatchOne and
            // retriggerForChordChange (both audio-thread, invoked below) read it
            // to choose between the FINGERED chord-mute selection and the
            // SINGLE-FINGER major/minor bucket reduction.  Audio-thread only, so
            // a plain bool needs no synchronisation.
            dispatchFingeredMode = fingeredMode;

            // Refill this block's drum-compose budget, then first spend it on any
            // pool misses parked in earlier blocks so a deferred kit lands as
            // soon as possible (still at most kMaxDrumComposesPerBlock decodes
            // per block).  Cheap pooled swaps below are never gated by this.
            drumComposeBudget = kMaxDrumComposesPerBlock;
            for (int ch = 0; ch < kNumUserStyleSlots && drumComposeBudget > 0; ++ch)
            {
                const int pc = pendingDrumPc[(size_t) ch];
                if (pc < 0) continue;
                pendingDrumPc[(size_t) ch] = -1;
                --drumComposeBudget;
                engine.programChangeDrum (ch, pc);   // composes + pools + swaps
            }

            // Immediate chord response.  The held chord is committed the moment
            // it changes — no 1/8-note deferral — so subtle / quick chord
            // changes are never coalesced or dropped while waiting for a musical
            // boundary.  This does NOT touch the sequencer's timeline: the
            // sequencer keeps ticking in perfect sync; a chord change only
            // latches the chord that new style notes transpose against and
            // glides the PAD's sustains (retriggerForChordChange) — everything
            // else answers the new chord on its next authored note-on.
            pendingChord = currentChord;

            const bool firstCall    = ! haveLastChord;
            const bool chordChanged = firstCall
                                    || ! (pendingChord == lastDispatchChord);

            if (chordChanged)
            {
                // Deferred switch (real-arranger feel): sounding notes keep
                // their pitch and end at their natural note-offs; only the PAD
                // slot's long sustains glide to the new chord immediately so a
                // held pad can't ring the old harmony for bars.  Skipped on the
                // very first chord — nothing is sounding yet.
                if (! firstCall)
                    retriggerForChordChange (pendingChord);

                lastDispatchChord = pendingChord;
                haveLastChord     = true;

                // ── One-shot CASM-miss diagnostic (temporary) ───────────────
                // Arm on every committed chord change.  The next dispatchBlock
                // that actually carries events tallies how many emitted events
                // came back with a matched CASM rule vs casm==nullptr (the
                // silent "no transposition" path) and writes ONE summary line
                // to grex_log.txt, then disarms.  Remove once chord switching
                // is confirmed wired.
                diagCasmLogArmed = true;
            }
            lastDispatchTick = currentTick;       // retained for diagnostics only
            juce::ignoreUnused (ticksPerEighth);  // deferral grid no longer used

            // ── One-shot CASM-miss diagnostic (temporary) ───────────────────
            // If a chord change just committed, report on the first populated
            // block: per-source-channel, how many emitted note/CC events found
            // a CASM rule vs came back null.  A null CASM means dispatchOne
            // plays the RAW recorded pitch with NO chord transposition — so a
            // high casmNull count here is the direct cause of "chord won't
            // switch".  Logged once per chord change (infrequent), then off.
            if (diagCasmLogArmed && ! events.empty())
            {
                int matched = 0, missed = 0;
                uint16_t matchChMask = 0, nullChMask = 0;
                for (const auto& e : events)
                {
                    if (e.event == nullptr) continue;
                    const int srcCh = (int) (e.event->channel & 0x0F);
                    if (e.casm != nullptr) { ++matched; matchChMask |= (uint16_t) (1u << srcCh); }
                    else                   { ++missed;  nullChMask  |= (uint16_t) (1u << srcCh); }
                }
                if (matched + missed > 0)
                {
                    // AUDIO THREAD — must not touch a file.  This used to call
                    // grexLog(), which OPENS, WRITES and CLOSES grex_log.txt;
                    // and because the flag below is re-armed on every chord
                    // change, it ran once per chord change rather than once
                    // ever.  The perf log caught the cost: a 14.5 ms block
                    // whose DSP stages summed to 124 us — i.e. >99% of it was
                    // the thread BLOCKED in that write, not computing.
                    //
                    // The counts still reach the log, via PerfMonitor's
                    // lock-free marker; the reporter thread formats and writes
                    // them.  diagChordName / diagChannelMask are deliberately
                    // NOT called here — they build juce::Strings, which
                    // allocate.
                    auto& perf = PerfMonitor::get();
                    perf.audioMark (PerfMonitor::AudioMarkId::ChordSwitch, matched + missed);
                    if (missed > 0)
                        perf.audioMark (PerfMonitor::AudioMarkId::CasmMiss, missed);
                    diagCasmLogArmed = false;
                }
            }

            // Dispatch every event in this block against the latched chord
            // (lastDispatchChord) — now always the chord the user is holding
            // right now, applied immediately.
            for (const auto& e : events)
            {
                // Boundary marker from the sequencer (loop-wrap / section change):
                // release every note still sounding from the section we just
                // left before this block's new note-ons arrive.  allNotesOff()
                // touches only the 8 style slots (not the player's solo voices)
                // and clears the active-note tracker, so a note that outlasts its
                // phrase end — or spans a fill / variation / break / intro /
                // ending — can never be stranded as a stuck note.
                //
                // SOFT flush: release only the notes still HELD (the tracker) —
                // each with its channel's natural release — and leave voices
                // already ringing their release tails alone.  The old hard
                // allNotesOff() (5 ms cut on every voice) guillotined authored
                // tails that cross the wrap: end-of-section bell/EP notes died
                // at the boundary instead of ringing ("very short, not
                // ringing").  Then re-assert every slot's SInt setup voice as
                // the section baseline, so a section-local revoicing (an intro
                // parking a DRUM KIT on a phrase slot, never restored by the
                // style) can't leak into the next section — its own bank/PC
                // events, dispatched right after this marker, re-apply it
                // whenever the incoming section really wants it.
                if (e.flushNotes)
                {
                    // SPLIT leaves what is sounding to ring across the splice —
                    // see EmittedStyleEvent::keepSounding.
                    if (! e.keepSounding)
                        flushHeldStyleNotes();

                    // Only on a real SECTION CHANGE.  On a self-loop the section
                    // has already applied its own bank/PC events, and restoring
                    // the SInt on top of them undoes the revoicing the style
                    // asked for — which the section then re-applies on the next
                    // pass, flipping the instrument once per cycle.
                    if (e.sectionChanged)
                        reassertSetupVoices();

                    continue;
                }
                if (e.event == nullptr) continue;
                dispatchOne (*e.event, e.casm, lastDispatchChord);
            }
        }

        /** Applied at style-load time: program-change + volume setup for
            every source channel that the style file declares. */
        //======================================================================
        // Recompute and re-push ONLY the levels.
        //
        // applyVoiceSetup does the whole job: it selects instruments, publishes
        // drum kits, resets note ranges and recomputes levels.  Running it on
        // every tick of a Settings slider meant reloading every instrument and
        // republishing every kit while audio was playing — which is exactly the
        // "really strong noises" a dragged slider produced.
        //
        // This does the last part alone.  It touches no instrument, no kit and
        // no note range: it walks the same source -> destination resolution,
        // recomputes the two dynamic drum factors and the loudness makeup, and
        // writes fader / auto-level / bus gain.  All of those are plain atomic
        // stores the audio thread picks up on the next block, so it is silent by
        // construction and safe to call as fast as a mouse can move.
        //======================================================================
        void refreshLevels (const StyleData& style)
        {
            drumBalanceFactor   = gatedDrumBalanceFactor (style);
            rhythmCeilingFactor = gatedRhythmCeilingFactor (style, drumBalanceFactor);

            slotLoadGain.fill (0.0f);

            for (int srcCh = 0; srcCh < (int) style.voices.size(); ++srcCh)
            {
                const auto& v = style.voices[(size_t) srcCh];
                if (! v.isUsed()) continue;

                const int engineCh = resolveDestChannel (style, srcCh);
                if (engineCh < 0 || engineCh >= kNumUserStyleSlots) continue;

                const int   rawVol = (v.volume >= 0) ? v.volume : kDefaultStyleCc7;
                const int   cc7    = effectiveStyleCc7 (engineCh, rawVol);
                const bool  ignore = StyleLevels::get().ignoreCc7();

                // IGNORE CC 7: leave the fader exactly where the user (or the
                // set) left it and neutralise the auto-level.  Writing unity to
                // the fader would be wrong — that is a level decision too, and
                // the whole point of the switch is that this slot's level is
                // not ours to make.
                const float fdr    = ignore ? engine.getChannelVolume (engineCh)
                                            : styleCc7ToFader (cc7);
                const float aut    = styleCc7ToAutoLevel (engineCh, rawVol,
                                                          drumBalanceFactor,
                                                          rhythmCeilingFactor,
                                                          bassIsSynthBass2 (engineCh));
                if (! ignore) engine.setChannelVolume (engineCh, fdr);
                engine.setChannelAutoLevel (engineCh, aut);

                const float cal = engine.presetGainLinearFor (
                                      engineCh, slotSoundingFlag[(size_t) engineCh]);
                slotLoadGain[(size_t) engineCh] = fdr * aut * cal;
            }

            const float makeup = computeStyleMakeupGain (style, slotLoadGain);
            engine.setStyleMakeupGain (StyleLevels::get().sectionBoost() * makeup);
        }

        void applyVoiceSetup (const StyleData& style)
        {
            // Spike diagnostics: a style load decodes every voice the style
            // needs and composes its kits, so it is the biggest single
            // message-thread burst there is.  Timing it here brackets all of
            // that, and the individual DECODE lines nest inside.
            Betel::PerfMonitor::Scoped perfScope ("STYLELOAD", style.name);

            grexLog ("=== applyVoiceSetup ===", true);
            grexLog ("soundLibraryCount=" + juce::String (engine.getSoundLibraryCount()));

            //------------------------------------------------------------------
            // CHORDAL SLOTS GO POLY FOR THIS STYLE.
            //
            // The BASS slot is Mono with a legato glide, which is what a bass
            // LINE wants and the opposite of what a chord wants: two notes on
            // one mono voice do not sound together, the second steals the first
            // and glides to it.  An all-piano style whose "bass" is a two-voice
            // left hand therefore swept an octave on every beat.
            //
            // Cleared for EVERY slot first: the flag describes the style that is
            // loading, so the one before it must not leave anything behind.
            // The user's own Mono setting is never written - see
            // Channel::setStyleForcedPoly - so it returns by itself the moment a
            // style that plays lines is loaded.
            //------------------------------------------------------------------
            for (int ch = 0; ch < kNumUserStyleSlots; ++ch)
                engine.setChannelStyleForcedPoly (ch, false);

            for (int rawDst = 0; rawDst < 16; ++rawDst)
            {
                if (! style.destChordal[(size_t) rawDst]) continue;

                const int ch = mapSourceToEngineChannel (rawDst);
                if (ch < 0 || ch >= kNumUserStyleSlots) continue;

                engine.setChannelStyleForcedPoly (ch, true);
                grexLog ("chordal dst" + juce::String (rawDst)
                         + " -> ch" + juce::String (ch) + "  FORCED POLY for this style");
            }

            if (auto* reg = engine.getDrumKitRegistry())
            {
                grexLog ("drumKitCount=" + juce::String (reg->getKitCount())
                         + "  kit000Elements=" + juce::String ((int) reg->getElementsForKit ("000").size()));
                juce::String g;
                for (const auto& n : reg->getGlobalComponentNames()) g += n + " ";
                grexLog ("drumGlobals=" + g);
            }
            else grexLog ("drumKitRegistry=NULL");

            // Re-sync the bank map with how many presets the blob exposes.
            bankMap.setNumBlobPresets (engine.numBlobPresets());

            // GM-Controlled vs Fixed comes from the style header.
            gmControlled.store (style.gmControlled);

            // Wipe the running bank/PC cache — every style-load starts fresh.
            for (int ch = 0; ch < kNumUserStyleSlots; ++ch)
            {
                runningBankMsb[(size_t) ch] = -1;
                runningBankLsb[(size_t) ch] = -1;
                slotStyleFlag[(size_t) ch]  = -1;
                slotSoundingFlag[(size_t) ch] = -1;
                slotLoadGain [(size_t) ch]  = 0.0f;        // …and its makeup weight
                rhythmLocked[(size_t) ch]   = false;       // drum-part lock (set below)
                pendingDrumPc[(size_t) ch]  = -1;          // clear parked kit misses
                lastStyleCC  [(size_t) ch].fill (-1);      // new style: CCs send fresh
                setupBankMsb [(size_t) ch] = -1;           // setup-voice snapshot
                setupBankLsb [(size_t) ch] = -1;
                setupPc      [(size_t) ch] = -1;
                setupExpression[(size_t) ch] = -1;         // …and its CC 11
                lastStylePB  [(size_t) ch] = -1;           // …and pitch bend too
                // AUTO-LEVEL carries the GM CC 7 law plus the per-slot
                // calibration trims (see styleCc7ToAutoLevel).  Cleared here so
                // a slot this style doesn't use can't inherit the previous
                // style's factor — it is as stateful as expression.
                engine.setChannelAutoLevel (ch, 1.0f);
                // The style OWNS the style faders now: its CC 7 is a MIDI
                // command to the visible fader (setup below writes each present
                // channel's level; per-section rides move it live).  Default
                // absent channels to unity so a part missing from this style
                // doesn't inherit the previous style's fader.
                engine.setChannelVolume (ch, 1.0f);
            }

            // A style "has a CASM" if any present section declares routing rules.
            styleHasCasm = false;
            for (const auto& sec : style.sections)
                if (sec.present && ! sec.casm.empty()) { styleHasCasm = true; break; }

            // ── Per-slot MUSICAL REGISTER (drives the MegaVoice fallback) ─────
            //
            // The register each style slot actually PLAYS in: the min/max of every
            // authored note BELOW the articulation floor, routed through the same
            // CASM the dispatcher uses.  THIS -- not the sampled key span of
            // whichever instrument happens to be loaded -- is what a MegaVoice
            // articulation key must be folded into.
            //
            // Folding into the instrument's sampled span was wrong because a
            // guitar blob is mapped far above C7: the articulation keys were
            // already "in range", so nothing folded and they sounded 2-4 OCTAVES
            // above the part.  And no OCTAVE slider can fix that, because the
            // required shift differs PER KEY.  CountryBallad_T151, LEAD 1
            // (musical register 48..83):
            //
            //     key  98 -> 74   2 octaves down
            //     key 108 -> 72   3 octaves down
            //     key 123 -> 75   4 octaves down
            //
            // One global slider cannot be -2, -3 and -4 at once -- and it clamps
            // at +/-3, so -4 was never even reachable.  Folding each key
            // individually into the part's own register hands every one of them
            // exactly the shift it needs, and leaves the slider free at 0.
            //
            // Message thread, once per style load; the audio thread only reads.
            for (int i = 0; i < kNumUserStyleSlots; ++i)
            {
                musicalLo[(size_t) i] = -1;
                musicalHi[(size_t) i] = -1;
            }

            for (const auto& sec : style.sections)
            {
                if (! sec.present) continue;

                for (const auto idx : sec.eventIdx)
                {
                    if (idx >= style.events.size()) continue;
                    const auto& ev = style.events[idx];

                    if ((ev.status & 0xF0) != 0x90 || ev.data2 == 0) continue;   // note-on only
                    const int n = (int) ev.data1;
                    if (n >= kMegaVoiceArticulationFloor) continue;              // not musical

                    // Same routing gate as dispatchOne: an unmapped low channel
                    // (the SInt mirror) or a phantom part never sounds, so it must
                    // not colour the register either.
                    const int        srcCh = (int) (ev.channel & 0x0F);
                    const CasmEntry* c     = sec.findCasm ((uint8_t) srcCh);
                    if (c == nullptr && (srcCh < 8 || styleHasCasm)) continue;

                    const int dst = mapSourceToEngineChannel (c != nullptr ? (int) c->dstChannel
                                                                           : srcCh);
                    if (dst < 0 || dst >= kNumUserStyleSlots) continue;

                    int& lo = musicalLo[(size_t) dst];
                    int& hi = musicalHi[(size_t) dst];
                    if (lo < 0 || n < lo) lo = n;
                    if (hi < 0 || n > hi) hi = n;
                }
            }

            // ── Drum auto-balance (DRUMS slot only) ──────────────────────────
            drumBalanceFactor   = gatedDrumBalanceFactor (style);
            rhythmCeilingFactor = gatedRhythmCeilingFactor (style, drumBalanceFactor);

            //
            // Channels 8..15 are the standard SFF part channels.  Channels
            // 0..7 are EITHER a vestigial SInt setup mirror (no CASM rule —
            // skip them) OR real chord-quality SOURCE VARIANTS routed to a
            // destination via CASM (multi-record styles).  The routed variants
            // carry the only instrument many harmony destinations ever get —
            // if we skip them, those dst slots stay empty and the part is
            // SILENT even though the dispatcher routes notes to it.  So pool a
            // low channel whenever it has a CASM rule.
            //
            // CRITICAL — route through CASM like the dispatcher does: at
            // runtime every event (notes, bank-selects, PCs) lands on
            // casm->dstChannel, NOT the raw source channel.  Pool the voices on
            // every destination this source can reach, or a runtime PC's
            // pooled-swap silently misses and the part keeps the wrong voice.
            for (int srcCh = 0; srcCh < 16; ++srcCh)
            {
                if (srcCh < 8 && ! sourceHasCasm (style, srcCh)) continue;  // mirror — skip
                int dests[kNumUserStyleSlots];
                const int numDests = collectDestChannels (style, srcCh, dests);
                for (const auto& spec : style.channelManifest[(size_t) srcCh])
                {
                    // Drum bank (MSB 127): the kit is owned by the drum
                    // registry, not the melodic flag library.  Pre-warm it into
                    // each destination's drum pool so the runtime program change
                    // is an atomic pointer swap, not a 128-key sample decode on
                    // the audio thread (the CPU spike at style transitions).
                    if (spec.bankMsb == 127)
                    {
                        for (int d = 0; d < numDests; ++d)
                        {
                            // IGNORE PROGRAM CHANGE, CHECKED HERE TOO.
                            //
                            // Everything else in this loop only PRE-WARMS a pool,
                            // which is harmless on a fixed slot.  onLoadFullKit
                            // does not: it publishes.  Unguarded, the style's
                            // sampled kit landed on the slot during this pass -
                            // before the setup loop further down ever reached its
                            // own guard - so a fixed drum slot came back holding
                            // the style's kit and looked like the toggle did
                            // nothing.
                            if (dests[d] >= 0 && dests[d] < kNumUserStyleSlots
                                && slotPcIgnored[(size_t) dests[d]].load())
                                continue;

                            // A SAMPLED KIT, IF THE POLICY SAYS SO.
                            //
                            // FullKitMap decides: no folder means composed, and
                            // the four kits GM already covers well (Standard 1/2,
                            // Electro, TR-808) stay composed on the DRUMS slot so
                            // the shared metal, the kick mix, the low-key
                            // substitution and every per-element edit survive.
                            // Slot 1 is PERC, where a whole sampled kit is
                            // additive rather than a replacement.
                            if (onLoadFullKit
                                && onLoadFullKit (dests[d], 127, spec.bankLsb,
                                                  spec.program, dests[d] == 1))
                                continue;

                            const int fb = onComposedFallbackPc
                                             ? onComposedFallbackPc (127, spec.bankLsb,
                                                                     spec.program)
                                             : -1;
                            engine.preloadDrumKitForChannel (dests[d],
                                                             fb >= 0 ? fb : spec.program);
                        }
                        continue;
                    }

                    // Bank 126 (percussion / SFX kits) landing on a rhythm
                    // destination is a DRUM part too — pre-warm it as one.  It
                    // used to fall through to the melodic branch below, where
                    // bankMap.resolve(126, 0, 78) returns GM 78 = Whistle: the
                    // kit was never pooled, so the setup had to decode it inline.
                    // Its PC is bank-126 numbering, hence percBankKitPc().
                    {
                        bool anyRhythmDest = false;
                        for (int d = 0; d < numDests; ++d)
                            if (dests[d] == 0 || dests[d] == 1) anyRhythmDest = true;

                        if (spec.bankMsb == 126 && anyRhythmDest)
                        {
                            for (int d = 0; d < numDests; ++d)
                            {
                                if (dests[d] != 0 && dests[d] != 1) continue;

                                // Same publish-not-prewarm guard as the bank-127
                                // branch above.
                                if (slotPcIgnored[(size_t) dests[d]].load()) continue;

                                // Bank 126 has no GM equivalent by definition, so
                                // a folder here always wins.
                                if (onLoadFullKit
                                    && onLoadFullKit (dests[d], 126, spec.bankLsb,
                                                      spec.program, dests[d] == 1))
                                    continue;

                                // No folder.  ETHNIC NEVER FALLS INTO A DRUM KIT -
                                // a snare where a doum should be is worse than the
                                // sound being absent - so the fallback answers with
                                // Arabic when the library has it, and otherwise
                                // leaves the existing percussion handling to decide.
                                const int fb = onComposedFallbackPc
                                                 ? onComposedFallbackPc (126, spec.bankLsb,
                                                                         spec.program)
                                                 : -1;
                                engine.preloadDrumKitForChannel (
                                    dests[d], fb >= 0 ? fb : percBankKitPc (spec.program));
                            }
                            continue;
                        }
                    }
                    for (int d = 0; d < numDests; ++d)
                    {
                        // THE NAME IS RESOLVED PER DESTINATION, INSIDE THIS LOOP.
                        //
                        // It used to be hoisted above the loop, so one source
                        // feeding two slots pre-warmed both under whichever name
                        // happened to be found first.  That is worse than a
                        // cosmetic error here: the pool is keyed on the resolved
                        // FLAG, and at runtime dispatchOne resolves the same
                        // program with the section's OWN name - so a mismatch
                        // means the pooled swap finds nothing and the audio
                        // thread decodes inline.
                        const char* vName = casmVoiceNameFor (style, srcCh, dests[d]);
                        const int   pi    = bankMap.resolve (spec.bankMsb, spec.bankLsb,
                                                             spec.program, vName);

                        // Bass-role correction is applied PER DESTINATION and
                        // here as well as at setup: pooling keys on the flag, so
                        // pre-warming the raw flag while the setup selects the
                        // corrected one would miss the pool and force a decode
                        // (and a runtime pooled swap would find nothing).
                        const int f = (dests[d] == kBassEngineSlot)
                                        ? BankProgramMap::correctFlagForBassRole (pi, vName)
                                        : pi;
                        engine.preloadChannelPreset (dests[d], f);
                    }
                }
            }

            // Voice setup runs LOW channels first (0..7), then HIGH (8..15), so
            // when a destination is fed by both a routed low-channel variant
            // AND a main high-channel part, the high-channel "main" voice wins
            // (it's applied last).  Destinations fed ONLY by low-channel
            // variants — the harmony parts in most multi-record styles — still
            // get a published instrument instead of falling silent.  Low
            // channels with no CASM rule are the SInt mirror and are skipped.

            // Which style slots this style actually assigns an instrument to.
            // Everything left untouched is put in the EMPTY state below.
            bool slotTouched[kNumUserStyleSlots] = {};

            //------------------------------------------------------------------
            // WHICH SOURCE DEFINES A DESTINATION'S VOICE.
            //
            // A destination can be fed by several sources — that is the SFF
            // chord-quality variant mechanism — but a channel holds exactly ONE
            // preset.  This loop lets every source write its destination's
            // voice, so with conflicting SInts the LAST one silently won, which
            // is an ordering accident rather than a decision.
            //
            // Measured on 6_8_Brush_Bld_S838, destination 12 (CHORD2):
            //     src  0   bank 0/115 PC 26  -> Electric Guitar (jazz)
            //     src  1   bank 0/113 PC  0  -> Acoustic Grand Piano
            //     src 12   bank 8/0   PC 16  <- the DESTINATION's own SInt
            // Three different answers for one channel; the engine took src 1
            // purely because it came last.
            //
            // SFF says which is right: MIDI channels 9..16 ARE the eight
            // destination channels and their SInt declares each destination's
            // voice.  Channels 1..8 are extra source tracks routed INTO a
            // destination — variants that play THROUGH the destination's voice,
            // not declarations of their own.
            //
            // So a source may set the destination voice only when it IS that
            // destination (srcCh >= 8), or when the destination declares none
            // of its own.  Everything else still routes its NOTES normally; it
            // just no longer overwrites the instrument.
            //------------------------------------------------------------------
            // RESOLVE THE HIGH CHANNEL THE SAME WAY ITS NOTES ARE RESOLVED.
            //
            // This asks "does a destination declare its own voice", so it has to
            // ask about the slot the high channel actually LANDS on, which is
            // resolveDestChannel — the very function the loop below uses for the
            // low channels.  Using the static mapSourceToEngineChannel here made
            // the two disagree the moment a style's CASM routed its high
            // channels away from their natural slots:
            //
            //   src12..15 map STATICALLY to slots 4..7, but this style's CASM
            //   sends them to slots 2 and 3.  Slots 4..7 were therefore reported
            //   as declaring their own voice while nothing ever set one, so the
            //   low-channel variants routed there had their setup skipped and
            //   the slots came up EMPTY — gains 0.0000, silent, while drums and
            //   bass (slots 0..3, correctly resolved) played.
            //
            // Reopening the editor "fixed" it only because that re-selects an
            // instrument for every slot from the UI's own cache.
            auto destDeclaresOwnVoice = [&style] (int engineCh) -> bool
            {
                for (int c = 8; c < (int) style.voices.size(); ++c)
                    if (resolveDestChannel (style, c) == engineCh
                        && style.voices[(size_t) c].isUsed())
                        return true;
                return false;
            };

            for (int srcCh = 0; srcCh < (int) style.voices.size(); ++srcCh)
            {
                if (srcCh < 8 && ! sourceHasCasm (style, srcCh)) continue;  // mirror — skip

                const auto& v = style.voices[(size_t) srcCh];

                // A SOURCE WITH NOTES BUT NO DECLARED VOICE.
                //
                // isUsed() means "the setup bar declared a bank or a program
                // for this channel".  Skipping when it does not is right for a
                // channel that plays nothing — but some styles ship a routed
                // source that HAS notes and declares no voice at all, expecting
                // the destination's own setup to supply one.  When that
                // destination has no high-channel counterpart either, nothing
                // ever writes it: the slot is never touched, drops into the
                // EMPTY state, and the part is silent even though the
                // dispatcher is faithfully routing notes to it.
                //
                // AnalogBallad_S321 (Tyros 2) is the case that exposed it.  Its
                // PHRASE1 destination is fed only by source channels 6 and 7,
                // which carry 57 and 22 notes in Intro C / Ending C and declare
                // neither bank nor program.  No high channel resolves to
                // PHRASE1 either — the CASM sends ch15 to CHORD2 instead — so
                // nothing ever set that slot up and the part never sounded.
                //
                // The CASM knows what was wanted: those two entries are named
                // "Piano" and "Piano m".  bankMap.resolve already consults the
                // CASM voice name, so an undeclared channel that HAS a name is
                // resolved from the name rather than skipped.  One with neither
                // a voice nor a name is still skipped — nothing to resolve
                // from, and a guess would be worse than silence.
                if (! v.isUsed() && casmVoiceNameFor (style, srcCh) == nullptr)
                    continue;

                // Translate the SFF source channel onto the engine slot the
                // DISPATCHER will actually fire this part's events at: the
                // section CASM's dstChannel when one exists for this source,
                // else the source channel itself.  Setting the voice up on the
                // raw source slot while the notes land on the CASM destination
                // is exactly the "wrong instruments / silent drums" failure.
                const int engineCh = resolveDestChannel (style, srcCh);

                // A routed low-channel variant does NOT get to declare the
                // destination's instrument when that destination declares its
                // own — see the note above.  Its notes still play, through the
                // destination's voice, which is the whole point of a variant.
                // IGNORE PROGRAM CHANGE: the user has fixed this slot's
                // instrument, and a style LOAD is the loudest program change of
                // the lot - guarding only the runtime PCs would let every style
                // change walk straight over the choice.  slotTouched is set so
                // the EMPTY-slot sweep below does not then unload the very
                // instrument being protected.
                if (engineCh >= 0 && engineCh < kNumUserStyleSlots
                    && slotPcIgnored[(size_t) engineCh].load())
                {
                    slotTouched[(size_t) engineCh] = true;
                    grexLog ("setup src" + juce::String (srcCh) + "->ch"
                             + juce::String (engineCh) + "  VOICE SKIPPED (ignore PC)");
                    continue;
                }

                const bool mayDeclareVoice = (srcCh >= 8)
                                          || ! destDeclaresOwnVoice (engineCh);
                if (! mayDeclareVoice)
                {
                    grexLog ("setup src" + juce::String (srcCh) + "->ch"
                             + juce::String (engineCh)
                             + "  VOICE SKIPPED (destination declares its own; "
                               "routed variant plays through it)");
                    continue;
                }

                // A channel is a DRUM channel whenever its setup bank-select is
                // MSB 127 -- even if no explicit program change was sent in the
                // setup bar (default to kit 0 / Standard).  The old guard
                // (v.program >= 0) skipped bank-only drum channels entirely, so
                // they fell through to melodic flag 0.
                // GUARD: a part can also be a drum part even if its *captured*
                // setup voice is non-127 -- some styles set the kit up correctly
                // in the SInt bar (bank 127) but then stamp a melodic / SFX bank
                // (e.g. 126) on the drums in every section.  If ANY voice in the
                // channel's manifest uses bank 127, treat it as a drum part and
                // LOCK it, using that bank-127 program for the kit.
                // BANK 126 on a rhythm destination: SFF reserves dst 9 (engine 0,
                // DRUMS) and dst 8 (engine 1, PERC) for percussion.  Yamaha voices
                // the SubRhythm / additional-percussion part out of bank MSB 126
                // (the "SFX / additional drum" bank), so its WHOLE setup is 126,
                // not 127 (e.g. the Tyros / oriental styles' "AddDrum" part).
                // Treat bank 126 as a rhythm bank too, but ONLY on those two
                // rhythm slots -- so a bank-126 SFX voice sitting on a melodic
                // slot is never misread as a kit.  The kit itself resolves through
                // SamplePlayerEngine::resolveDrumKitParams, whose nearest-family
                // fallback maps the non-standard PC onto the closest sampled kit,
                // so the part plays percussion instead of falling silent.
                const bool rhythmDest = (engineCh == 0 || engineCh == 1);
                const bool percBank   = (rhythmDest && v.bankMsb == 126);
                bool rhythmRole  = (v.bankMsb == 127) || percBank;

                // The kit PC must be read in the numbering of the bank it came
                // from.  A bank-126 PC is NOT a drum-bank PC — see percBankKitPc.
                //
                // THE RAW REQUEST IS KEPT ALONGSIDE THE COMPOSED ONE.
                //
                // forcedKitPc is the COMPOSED answer, and for bank 126 that is
                // percBankKitPc, which returns 0 for everything - so by the time
                // the kit is published, "126/0/43 Pop Latin" and "126/0/35
                // Arabic" have both become "000 Standard" and there is nothing
                // left to look a sampled kit up by.  kitBankMsb/Lsb/RawPc carry
                // the style's actual request through to the FullKitMap query
                // below.
                int  kitBankMsb  = v.bankMsb;
                int  kitBankLsb  = v.bankLsb;
                int  kitRawPc    = (v.program >= 0) ? v.program : 0;
                int  forcedKitPc = kitRawPc;
                if (percBank) forcedKitPc = percBankKitPc (forcedKitPc);

                if (! rhythmRole)
                    for (const auto& spec : style.channelManifest[(size_t) srcCh])
                        if (spec.bankMsb == 127
                            || (rhythmDest && spec.bankMsb == 126))
                        {
                            rhythmRole  = true;
                            const int rawPc = (spec.program >= 0) ? spec.program : 0;
                            kitBankMsb  = spec.bankMsb;
                            kitBankLsb  = spec.bankLsb;
                            kitRawPc    = rawPc;
                            forcedKitPc = (spec.bankMsb == 126) ? percBankKitPc (rawPc)
                                                                : rawPc;
                            break;
                        }

                if (rhythmRole)
                {
                    if (engineCh >= 0 && engineCh < kNumUserStyleSlots)
                        slotTouched[(size_t) engineCh] = true;
                    engine.setDrumChannel (engineCh, true);
                    // THE SAMPLED KIT FIRST - THIS IS WHERE IT WAS BEING LOST.
                    //
                    // The manifest loop above is FullKitMap-aware and had already
                    // LOADED the sampled kit onto this slot.  Then this line ran
                    // and published the composed fallback straight over it, so a
                    // style asking for 126/0/43 heard 000 Standard - the sampled
                    // kit was loaded and destroyed inside one call to this
                    // function, which is why nothing in the logs looked wrong.
                    //
                    // Asked in the style's own numbering, not forcedKitPc's:
                    // FullKitMap keys on the real bank and program, and by
                    // forcedKitPc every bank-126 kit has already collapsed to 0.
                    bool kitOk = (onLoadFullKit
                                  && onLoadFullKit (engineCh, kitBankMsb, kitBankLsb,
                                                    kitRawPc, engineCh == 1));

                    // No sampled kit for this request: compose as before.  Already
                    // pre-warmed by the manifest loop, so it is a pointer swap
                    // that also syncs the UI kit name.
                    if (! kitOk)
                        kitOk = engine.publishDrumKitWithName (engineCh, forcedKitPc);
                    rhythmLocked  [(size_t) engineCh] = true;     // pin to drum path
                    slotStyleFlag [(size_t) engineCh] = -1;
                    slotSoundingFlag[(size_t) engineCh] = -1;   // drum slot: no melodic flag
                    // Pin the running + redundancy caches to the drum bank (127),
                    // NOT the captured setup bank: a section that later resends a
                    // non-127 bank/PC must read as "different" so the playback
                    // guard sees it and ignores it instead of demoting the part.
                    runningBankMsb[(size_t) engineCh] = 127;
                    runningBankLsb[(size_t) engineCh] = 0;
                    lastAppliedMsb[(size_t) engineCh] = 127;
                    lastAppliedLsb[(size_t) engineCh] = 0;
                    lastAppliedPc [(size_t) engineCh] = forcedKitPc;
                    setupBankMsb  [(size_t) engineCh] = 127;
                    setupBankLsb  [(size_t) engineCh] = 0;
                    setupPc       [(size_t) engineCh] = forcedKitPc;
                    grexLog ("setup src" + juce::String (srcCh) + "->ch" + juce::String (engineCh)
                             + "  DRUM(locked)  req=" + juce::String (kitBankMsb)
                             + "/" + juce::String (kitBankLsb)
                             + "/" + juce::String (kitRawPc)
                             + "  forcedKitPc=" + juce::String (forcedKitPc)
                             + " setupBank=" + juce::String (v.bankMsb)
                             + " sampled=" + juce::String ((int) engine.isFullKitOnChannel (engineCh))
                             + " loaded=" + juce::String ((int) kitOk));
                }
                else if (v.program >= 0)
                {
                    // GUARD — load-time twin of the runtime PC demotion guard:
                    // a slot already drum-locked THIS load must not be
                    // overwritten by a LATER source that resolves onto the same
                    // engine slot with a melodic voice (e.g. a stray piano on a
                    // CASM-less src 9 falling back to slot 0 would replace the
                    // kit and clear the drum flag).  Keep the kit; skip this
                    // source's setup (voice AND volume) for the slot entirely.
                    if (engineCh >= 0 && engineCh < kNumUserStyleSlots
                        && rhythmLocked[(size_t) engineCh])
                    {
                        grexLog ("setup src " + juce::String (srcCh)
                                 + "  melodic voice IGNORED - slot "
                                 + juce::String (engineCh) + " is drum-locked");
                        continue;
                    }
                    if (engineCh >= 0 && engineCh < kNumUserStyleSlots)
                        slotTouched[(size_t) engineCh] = true;
                    engine.setDrumChannel (engineCh, false);
                    const char* vName  = casmVoiceNameFor (style, srcCh, engineCh);
                    const int   rawIdx = bankMap.resolve (v.bankMsb, v.bankLsb,
                                                          v.program, vName);
                    // BASS destination: map a non-bass program into the GM bass
                    // family — see BankProgramMap::correctFlagForBassRole.  The
                    // CORRECTED flag is what gets stored in slotStyleFlag, so
                    // everything keyed off the slot's GM program downstream
                    // (SoundsTab::applyGmEnvDefaults — allowed-notes floor,
                    // octave bias, mono contract — and the mixer/SOUNDS label)
                    // sees the bass the part actually is.
                    const int presetIdx = (engineCh == kBassEngineSlot)
                                            ? BankProgramMap::correctFlagForBassRole (rawIdx, vName)
                                            : rawIdx;
                    engine.selectChannelPreset (engineCh, presetIdx);
                    slotStyleFlag[(size_t) engineCh] = presetIdx;
                    slotSoundingFlag[(size_t) engineCh] = presetIdx;
                    runningBankMsb[(size_t) engineCh] = v.bankMsb;
                    runningBankLsb[(size_t) engineCh] = v.bankLsb;
                    // Prime the PC redundancy cache — see drum branch above.
                    lastAppliedMsb[(size_t) engineCh] = v.bankMsb;
                    lastAppliedLsb[(size_t) engineCh] = v.bankLsb;
                    lastAppliedPc [(size_t) engineCh] = v.program;
                    setupBankMsb  [(size_t) engineCh] = v.bankMsb;
                    setupBankLsb  [(size_t) engineCh] = v.bankLsb;
                    setupPc       [(size_t) engineCh] = v.program;
                    grexLog ("setup src" + juce::String (srcCh) + "->ch" + juce::String (engineCh)
                             + "  MELODIC msb=" + juce::String (v.bankMsb)
                             + " lsb=" + juce::String (v.bankLsb)
                             + " prog=" + juce::String (v.program)
                             + " -> flag=" + juce::String (presetIdx)
                             + (presetIdx != rawIdx
                                  ? (" (bass-role corrected from " + juce::String (rawIdx) + ")")
                                  : juce::String())
                             + " inLibrary=" + juce::String ((int) engine.hasInstrumentFlag (presetIdx)));
                }
                else
                {
                    grexLog ("setup ch" + juce::String (engineCh)
                             + "  USED-but-no-prog  bankMsb=" + juce::String (v.bankMsb)
                             + " lsb=" + juce::String (v.bankLsb));
                }
                {
                    // CC 7 IS OPTIONAL IN THE SInt.  A style that never sends it
                    // for a part used to skip this whole block, which left that
                    // slot exactly where the per-style reset above put it —
                    // fader 1.0 (=127, UNITY) with auto-level 1.0 — i.e. the
                    // loudest any style part can be, with EVERY calibration
                    // bypassed: the GM volume law, the per-instrument trims, the
                    // BASS boost, the PERC trim, the drum balance and the rhythm
                    // ceiling.  EasyBallad's French Horn (bank 104 / PC 60, no
                    // CC 7 at all) is exactly that case, which is why it loaded
                    // at 127 despite the brass/reed trim.
                    //
                    // A missing CC 7 means "the GM default", not "full power":
                    // the MIDI spec puts channel volume at 100, and that is what
                    // the hardware would be sitting at.  So substitute 100 and
                    // run the normal path — the part gets the law and all its
                    // trims like any other.
                    const int rawVol = (v.volume >= 0) ? v.volume : kDefaultStyleCc7;

                    // PURE CC 7 → FADER, 1:1.  The mixer scale is 0..254 with
                    // gain = value/127, so the style's raw 0..127 volume IS the
                    // fader value: 127 = unity, and a style can never push a
                    // fader past unity — everything above 127 is user headroom.
                    // No anchor, no template, no ratios.
                    //
                    // The one exception is the PERC slot, which is trimmed to a
                    // fraction of what the style asks for — see
                    // styleCc7ToAutoLevel.
                    //
                    // NO AUTOMATIC DRUM TRIM RUNS ANY MORE — neither slot.
                    //
                    // drumBalanceFactor and rhythmCeilingFactor still arrive as
                    // arguments below, but they come from gatedDrumBalanceFactor
                    // and gatedRhythmCeilingFactor, which both return 1.0.  The
                    // measurements are still computed and still logged; nothing
                    // applies them.  See the retirement note above those two.
                    //
                    // So what reaches a style slot's level is the GM square law
                    // and nothing else, and the kit's balance is the user's
                    // mixer faders, which travel in the set.
                    //
                    // The GM volume law lands on the channel's AUTO-LEVEL, never
                    // on the fader.  The fader is the composer's stated number
                    // and nothing else, so the mixer keeps showing exactly what
                    // the style file (and the Style Data window) reports.
                    const int   cc7    = effectiveStyleCc7 (engineCh, rawVol);
                    const bool  ignore = StyleLevels::get().ignoreCc7();
                    // IGNORE CC 7 — see refreshLevels above.  The fader keeps
                    // the user's number; only the auto-level is touched, and
                    // that goes to unity.
                    const float fdr = ignore ? engine.getChannelVolume (engineCh)
                                             : styleCc7ToFader (cc7);
                    const float aut = styleCc7ToAutoLevel (engineCh, rawVol,
                                                           drumBalanceFactor,
                                                           rhythmCeilingFactor,
                                                           bassIsSynthBass2 (engineCh));
                    if (! ignore) engine.setChannelVolume (engineCh, fdr);
                    engine.setChannelAutoLevel (engineCh, aut);
                    // Remember the slot's resulting linear gain so the per-style
                    // loudness makeup can weigh each note by the level it will
                    // actually sound at.
                    if (engineCh >= 0 && engineCh < kNumUserStyleSlots)
                    {
                        // x the SOUND's own calibration.  Without this the
                        // loudness makeup weighs each note by the level the
                        // STYLE asked for rather than the level it will be heard
                        // at, so every instrument that gets calibrated pushes
                        // the makeup further out of step with the real mix.
                        const float cal = engine.presetGainLinearFor (
                                              engineCh,
                                              slotSoundingFlag[(size_t) engineCh]);
                        slotLoadGain[(size_t) engineCh] = fdr * aut * cal;
                    }
                }
                // EXPRESSION (CC 11) — the style's SECOND gain input, now
                // honoured.  It is a separate multiplier from the fader, and
                // that separation is the point: CC 7 is the part's mix level
                // (the fader the user sees and can override), CC 11 is the
                // composer's performance shape ON that level — a part parked
                // below its fader in the setup, ridden per section, faded out
                // across an ending.  Forcing unity here discarded all of it.
                //
                // Same GM law family as CC 7 (see styleCc7ToAutoLevel); a channel
                // the setup doesn't mention resets to unity so nothing leaks in
                // from the previously-loaded style.
                engine.setChannelExpression (engineCh, styleExpressionToGain (v.expression));
                if (engineCh >= 0 && engineCh < kNumUserStyleSlots)
                    setupExpression[(size_t) engineCh] = v.expression;   // restore point for allNotesOff
                // OTHER GAIN-SHAPING CCs ARE STILL FILTERED — deliberately:
                //  • Reverb send (CC 91): NOT applied — was a write-only value
                //    (no reverb bus in the render path); now fully ignored.
                //  • Chorus send (CC 93): ignored (no per-channel chorus).
                // PAN: the style's CC 10 is deliberately ignored — every channel
                // stays equal-power centre.  Panning is reserved for the user's
                // own per-channel pan control (default centre); the style is not
                // allowed to move it.  (channelPan defaults to 0 and nothing else
                // sets it, so all channels render dead-centre.)
            }

            // ── User substitutions vs. a NEW style ───────────────────────────
            //
            // A manual instrument pick locks that slot against the style's
            // program changes (setSlotSubstitution).  That is right for the
            // style it was made on — and wrong for the next one: carrying it
            // across meant picking one guitar once left that channel deaf to
            // every later style's PCs, permanently.
            //
            // So the lock is scoped to the style it was made on.  Re-applying
            // the SAME style keeps it (editor reopen, set restore, a reload
            // after an edit); loading a DIFFERENT one releases every slot back
            // to that style's own voices.  A set that stores substitutions
            // restores them right after loadStyle, so the set path is unharmed.
            {
                const juce::String id = style.sourceFile != juce::File()
                                      ? style.sourceFile.getFullPathName()
                                      : style.name;
                if (id != lastAppliedStyleId)
                {
                    lastAppliedStyleId = id;
                    for (int ch = 0; ch < kNumUserStyleSlots; ++ch)
                    {
                        slotSubFlag [(size_t) ch].store (-1);
                        slotPcLocked[(size_t) ch].store (false);
                    }
                }
            }

            // Re-assert any substitutions that survived the check above, so a
            // set's saved overrides survive a style (re)load regardless of the
            // order in which load happens.
            for (int ch = 0; ch < kNumUserStyleSlots; ++ch)
            {
                const int sub = slotSubFlag[(size_t) ch].load();
                if (sub < 0) continue;
                engine.setDrumChannel (ch, false);
                engine.selectChannelPreset (ch, sub);
                slotPcLocked[(size_t) ch].store (true);
            }

            // ── EMPTY slots ──────────────────────────────────────────────────
            // Any style slot this style's setup never assigned an instrument to
            // (and no user substitution claims) goes to the EMPTY state: the
            // engine channel is fully unloaded — no instrument, no RAM held by
            // the previous style's decodes, and every MIDI stream to it is
            // ignored (noteOn / render bail on the null preset).  This kills
            // the ghost slot that kept showing + holding the previous style's
            // voice (e.g. a leftover Flute on LEAD 2 in a style that has no
            // Phrase 2 part at all).
            for (int ch = 0; ch < kNumUserStyleSlots; ++ch)
            {
                if (slotTouched[(size_t) ch])                continue;
                if (slotSubFlag[(size_t) ch].load() >= 0)    continue;  // user override owns it
                engine.clearChannelInstrument (ch);
                slotStyleFlag [(size_t) ch] = -1;
                slotSoundingFlag[(size_t) ch] = -1;
                rhythmLocked  [(size_t) ch] = false;
                runningBankMsb[(size_t) ch] = -1;
                runningBankLsb[(size_t) ch] = -1;
                lastAppliedMsb[(size_t) ch] = -1;
                lastAppliedLsb[(size_t) ch] = -1;
                lastAppliedPc [(size_t) ch] = -1;
                setupBankMsb  [(size_t) ch] = -1;
                setupBankLsb  [(size_t) ch] = -1;
                setupPc       [(size_t) ch] = -1;
                setupExpression[(size_t) ch] = -1;
                // An unused slot must not inherit the PREVIOUS style's
                // expression — that multiplier is stateful and would silently
                // hold this slot down for the whole session.
                engine.setChannelExpression (ch, 1.0f);
                engine.setChannelAutoLevel  (ch, 1.0f);   // …and its auto-level
                grexLog ("setup slot " + juce::String (ch) + "  EMPTY (unused by this style)");
            }

            // STYLE VOLUME stays at UNITY on load — it is entirely the user's.
            // The MAKEUP is a separate multiplier and is now actually computed
            // (see computeStyleMakeupGain): it evens out the very wide
            // style-to-style loudness spread so one STYLE VOLUME setting holds
            // across the library instead of needing a nudge per style.
            const float makeup = computeStyleMakeupGain (style, slotLoadGain);
            const float busOut = StyleLevels::get().sectionBoost() * makeup;
            engine.setStyleBusGain    (1.0f);
            engine.setStyleMakeupGain (busOut);

            // LEVEL DIAGNOSTIC.  Prints the whole style-bus gain chain and every
            // slot's load-time gain, so "the style is too quiet" can be answered
            // from data instead of inference.  If this line is MISSING from
            // grex_log.txt after loading a style, the build is not running this
            // file at all — which is itself the answer.
            {
                juce::String g;
                for (int ch = 0; ch < kNumUserStyleSlots; ++ch)
                    g += juce::String (ch) + "=" + juce::String (slotLoadGain[(size_t) ch], 4) + " ";
                grexLog ("LEVELS: styleBus=1.000  makeup=" + juce::String (makeup, 3)
                         + ((makeup >= kStyleMakeupMax - 1.0e-4f
                             || makeup <= kStyleMakeupMin + 1.0e-4f) ? " [CLAMPED]" : "")
                         + "  sectionBoost=" + juce::String (kStyleSectionBoost, 3)
                         + "  -> styleBusTotal=" + juce::String (busOut, 3)
                         + " (" + juce::String (20.0f * std::log10 (juce::jmax (1.0e-6f, busOut)), 1) + " dB)");
                grexLog ("LEVELS: slot gains (fader x autoLevel)  " + g);
                grexLog ("LEVELS: drumBalance=" + juce::String (drumBalanceFactor, 3)
                         + "  rhythmCeiling=" + juce::String (rhythmCeilingFactor, 3)
                         + " (" + juce::String (20.0f * std::log10 (juce::jmax (1.0e-6f, rhythmCeilingFactor)), 1)
                         + " dB)");
            }
        }

        //======================================================================
        // Per-style loudness MAKEUP.
        //
        // This multiplier has existed on the style bus from the start, and its
        // declaration says it is "set once at style load to normalise the wide
        // loudness gaps between styles" — but the only line that ever wrote it
        // wrote 1.0, so the normalisation never happened.  Measured across the
        // library the gap is real and large: taking the 95th percentile of the
        // summed simultaneous note amplitude over the Main sections,
        //
        //     80sDisco      +1.7 dB
        //     6-8Orchestral +1.5 dB
        //     Dosari        -4.9 dB
        //
        // — so a STYLE VOLUME set to sit right under the right hand for one
        // style is 6..7 dB wrong for the next.  That is what makes "the style is
        // quieter than the solo voice" feel inconsistent as well as true.
        //
        // The metric is the one computeDrumBalanceFactor uses, for the same
        // reason: SIMULTANEOUS notes add on the bus, so per-tick summed
        // amplitude — each note's slot gain times its velocity gain — is what
        // the ear actually meets.  p95 rather than the peak, so one stray
        // fortissimo stab can't set the level for a whole style; and rather than
        // the median, because peaks are what run out of headroom.
        //
        // Conservative by construction: a dead-band leaves ordinary styles at
        // exactly 1.0, and the clamp bounds the correction to +/-6 dB so a
        // mis-measured style can never be wildly re-levelled.  It never touches
        // the per-channel faders, so the mixer keeps showing literal CC 7.
        //======================================================================
        // FIXED style-section offset, on top of the per-style normalisation.
        // The whole style bus (slots 0..7) is lifted by this much so the backing
        // sits where it should against the right-hand voice, which runs at unity
        // with nothing attenuating it.  Separate from the makeup on purpose: the
        // makeup answers "is THIS style level with the others", this answers "is
        // the style section level with the solo section", and only one of them
        // should change when a style loads.
        //
        // HEADROOM: this is a large lift.  Measured style p95 summed amplitude
        // is around 1.2 (already above unity), so +15 dB puts peaks near +16 dB.
        // The master fader / BOOST has to come down to match, and the Finisher's
        // ceiling will be doing real work.  Lower this constant first if the mix
        // clips rather than pulling individual channels back down.
        // DEFAULT only — the live value is a setting, see StyleLevels.
        static constexpr float kStyleSectionBoostDb = 15.0f;
        static constexpr float kStyleSectionBoost   = 5.623413f;   // 10^(15/20)

        // TARGET calibrated against 90sPopBallad, judged right by ear: its log
        // read makeup 0.703 at this target, i.e. p95 1.707 x 0.703 = 1.20.
        // Since delivered level = p95 x (T/p95) x boost = T x boost, T alone
        // sets the level every style is normalised to.  It was briefly 0.68,
        // derived from a log that turned out to belong to a DIFFERENT style —
        // that made every style 4.9 dB quieter than the one actually approved.
        static constexpr float kStyleMakeupTarget = 1.20f;    // p95 summed amplitude
        // Range wide enough to actually reach the target.  The old +/-6 dB was
        // far too tight for the real spread of authored CC 7: 90sPopBallad
        // wanted x3.55 (+11 dB) and was truncated to x2.00, landing 5 dB under
        // every style that fitted inside the clamp — the normaliser silently
        // creating the very inconsistency it exists to remove.
        static constexpr float kStyleMakeupMin    = 0.25f;    // -12.0 dB
        static constexpr float kStyleMakeupMax    = 6.00f;    // +15.6 dB
        //
        // NO DEAD-BAND.  There used to be one (leave "ordinary" styles at
        // exactly 1.0), and it put a cliff in the middle of the range: the
        // second style measured p95 1.707, a hair outside the 1.70 edge, so it
        // was corrected — at 1.69 it would have been left alone and come out
        // 3 dB louder.  Normalisation has to be continuous or near-identical
        // styles land on opposite sides of a coin flip.

        static float computeStyleMakeupGain (const StyleData& style,
                                             const std::array<float, kNumUserStyleSlots>& slotGain)
        {
            std::map<int, float> perTick;       // absolute tick -> summed amplitude

            for (const auto& sec : style.sections)
            {
                if (! sec.present) continue;
                if (sec.id < StyleSection::MainA || sec.id > StyleSection::MainD) continue;

                for (const auto idx : sec.eventIdx)
                {
                    if (idx >= style.events.size()) continue;
                    const auto& ev = style.events[idx];
                    if ((ev.status & 0xF0) != 0x90 || ev.data2 == 0) continue;   // note-on only

                    const int srcCh = (int) (ev.channel & 0x0F);
                    const CasmEntry* c = sec.findCasm ((uint8_t) srcCh);
                    if (c == nullptr && (srcCh < 8 || styleHasCasmStatic (style))) continue;

                    const int dst = mapSourceToEngineChannel (c != nullptr ? (int) c->dstChannel
                                                                           : srcCh);
                    if (dst < 0 || dst >= kNumUserStyleSlots) continue;

                    // Velocity gain, matching Channel::startVoice exactly: drums
                    // are linear, melodic voices take the compressed curve.
                    const float uv = (float) ev.data2 / 127.0f;
                    const bool  rhythm = (dst == kDrumsEngineSlot || dst == kPercEngineSlot);
                    const float vg = rhythm ? uv : uv * (1.4f - 0.7f * uv);

                    perTick[ev.tick] += slotGain[(size_t) dst] * vg;
                }
            }

            std::vector<float> sums;
            sums.reserve (perTick.size());
            for (const auto& kv : perTick)
                if (kv.second > 0.0f) sums.push_back (kv.second);
            if (sums.size() < 8) return 1.0f;       // too little data to judge
            std::sort (sums.begin(), sums.end());

            const float k  = 0.95f * (float) (sums.size() - 1);
            const int   lo = (int) std::floor (k);
            const int   hi = juce::jmin (lo + 1, (int) sums.size() - 1);
            const float p95 = sums[(size_t) lo]
                            + (sums[(size_t) hi] - sums[(size_t) lo]) * (k - (float) lo);

            if (p95 <= 1.0e-6f) return 1.0f;

            // MAKEUP IS A DEPTH, NOT A TARGET.
            //
            // It used to be the loudness the section was driven to, with a
            // separate on/off switch beside it.  Now it is 0..100 for "how much
            // of that normalisation do you want", and 0 IS the off switch — one
            // control instead of two, and the same 0-100 scale as BOOST.
            //
            // Blending toward 1.0 rather than scaling the target is what makes
            // it behave like glue: at 30 a quiet section travels 30% of the way
            // toward the loud ones, instead of everything being flattened onto
            // one number.
            const float depth = StyleLevels::get().makeupDepth();
            if (depth <= 0.0f) return 1.0f;

            const float full = juce::jlimit (kStyleMakeupMin, kStyleMakeupMax,
                                             StyleLevels::kMakeupTarget / p95);
            return 1.0f + (full - 1.0f) * depth;
        }

        /** Boundary flush: release ONLY the still-held style notes (tracker
            entries) with each channel's natural release.  Voices already in
            release keep ringing their tails across the boundary. */
        void flushHeldStyleNotes()
        {
            for (int i = 0; i < activeNoteCount; ++i)
                engine.noteOff (activeNotes[(size_t) i].ch,
                                activeNotes[(size_t) i].destNote);
            activeNoteCount = 0;
        }

        /** Return every slot to its SInt setup voice (cheap no-op via the
            lastApplied cache when nothing changed).  Locks and pins win. */
        void reassertSetupVoices()
        {
            if (! gmControlled.load()) return;          // Fixed mode: leave voices
            for (int ch = 0; ch < kNumUserStyleSlots; ++ch)
            {
                const int msb = setupBankMsb[(size_t) ch];
                const int lsb = setupBankLsb[(size_t) ch];
                const int pc  = setupPc     [(size_t) ch];
                if (pc < 0) continue;                    // no setup voice
                // Re-seed the running bank so the next authored PC resolves
                // against the setup bank, exactly as at style load.
                runningBankMsb[(size_t) ch] = msb;
                runningBankLsb[(size_t) ch] = lsb;
                if (lastAppliedMsb[(size_t) ch] == msb
                    && lastAppliedLsb[(size_t) ch] == lsb
                    && lastAppliedPc [(size_t) ch] == pc)
                    continue;                            // already at setup
                if (slotPcLocked [(size_t) ch].load())   continue;   // user pin
                if (slotPcIgnored[(size_t) ch].load())   continue;   // user toggle
                lastAppliedMsb[(size_t) ch] = msb;
                lastAppliedLsb[(size_t) ch] = lsb;
                lastAppliedPc [(size_t) ch] = pc;
                if (rhythmLocked[(size_t) ch])
                {
                    // Same rule as dispatchOne: honour the drum bank (127) and
                    // the percussion/SFX bank (126); ignore anything else.
                    if      (msb == 127) applyRuntimeDrumPc (ch, pc);
                    else if (msb == 126) applyRuntimeDrumPc (ch, percBankKitPc (pc));
                    continue;
                }
                if (msb == 127) { applyRuntimeDrumPc (ch, pc); continue; }
                const int presetIdx = bankMap.resolve (msb, lsb, pc, nullptr);
                engine.selectChannelPooledPreset (ch, presetIdx);
                // AUDIO THREAD (reassertSetupVoices runs from dispatchBlock's
                // boundary handling) — same reason as the chord-switch marker
                // above: no file, no string building.  The slot number is the
                // useful part and it fits in the marker's int.
                PerfMonitor::get().audioMark (PerfMonitor::AudioMarkId::BoundaryRestore, ch);
            }
        }

        /** Silence every style slot.  Call when stopping playback or
            switching styles to avoid stuck notes. */
        void allNotesOff()
        {
            for (int ch = 0; ch < kNumUserStyleSlots; ++ch)
            {
                engine.allNotesOff (ch);
                // Expression is STATEFUL, unlike notes: an ending ramps it down
                // to a fade-out and the style never sends anything to bring it
                // back, so a stop mid-ending would leave that part quiet for
                // every section after it.  Rewind to the value the style's own
                // setup asked for (unity when it asked for nothing), which is
                // exactly where the next start should begin.
                engine.setChannelExpression (ch,
                    styleExpressionToGain (setupExpression[(size_t) ch]));
            }
            activeNoteCount = 0;
        }

        // ── MIDI event trace (debugging) ────────────────────────────────────
        // A lock-free capture of every note-on the dispatcher processes, with
        // its source provenance and outcome, so the played stream can be diffed
        // against the style file's note dump (StyleDataWindow "NOTES EXPORT").
        // Armed/disarmed from the UI; while armed, dispatchOne appends a fixed
        // POD record per note event with no allocation, lock, or file-I/O on the
        // audio thread.  Read back and formatted off-thread for export.
        struct MidiTraceRec
        {
            uint32_t seq;          // capture order (0-based)
            uint32_t tick;         // source event tick (maps to the style file)
            uint8_t  srcCh;        // source MIDI channel 0..15
            uint8_t  srcData1;     // note number
            uint8_t  srcData2;     // velocity
            uint8_t  dstCh;        // engine slot 0..7, or 0xFF if dropped pre-routing
            uint8_t  dstNote;      // sounding note after transpose, or 0xFF
            uint8_t  result;       // TraceResult
            uint8_t  chordRoot;    // 0..11
            uint8_t  chordQual;    // ChordQuality value
            uint8_t  casmMatched;  // 1 if a CASM rule applied, else 0
            uint8_t  pad;
        };
        enum TraceResult : uint8_t
        {
            TR_PLAY = 0, TR_DROP_MUTED, TR_DROP_VARIANT, TR_DROP_TRANSPOSE,
            TR_DROP_PHANTOM, TR_NOTE_OFF
        };
        static constexpr int kTraceCap = 100000;

        /** Arm/disarm capture.  Arming resets the buffer so each run is clean. */
        void setTraceArmed (bool b) noexcept
        {
            if (b) traceWriteIdx.store (0, std::memory_order_relaxed);
            traceArmed.store (b, std::memory_order_release);
        }
        bool isTraceArmed() const noexcept { return traceArmed.load (std::memory_order_acquire); }

        /** Records captured (saturates at kTraceCap).  Read only when disarmed —
            the audio thread must not be writing while the reader formats. */
        int getTraceCount() const noexcept
        {
            const size_t n = traceWriteIdx.load (std::memory_order_acquire);
            return (int) juce::jmin (n, (size_t) kTraceCap);
        }
        /** True if capture overflowed the buffer (some events were lost). */
        bool traceOverflowed() const noexcept
        {
            return traceWriteIdx.load (std::memory_order_acquire) > (size_t) kTraceCap;
        }
        const MidiTraceRec& getTraceRec (int i) const noexcept { return traceBuf[(size_t) i]; }

        // ── Channel mute mask (driven by STYLE ELEMENTS ON/OFF in MainTab) ──
        //
        // Setting a bit drops note-ons / CC / PC / pitch-bend on that engine
        // channel; note-offs still pass through.  Mute-set also issues an
        // immediate allNotesOff(ch) so held notes don't hang.
        void setChannelMute (int engineCh, bool muted) noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return;
            const uint16_t bit = (uint16_t) (1u << engineCh);
            const uint16_t old = muteMask.load();
            const uint16_t neu = muted ? (uint16_t) (old |  bit)
                                       : (uint16_t) (old & ~bit);
            if (neu == old) return;
            muteMask.store (neu);
            if (muted) engine.allNotesOff (engineCh);
        }

        bool isChannelMuted (int engineCh) const noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return false;
            return (muteMask.load() & (uint16_t) (1u << engineCh)) != 0;
        }

        BankProgramMap& getBankMap()  noexcept { return bankMap; }

        // ── Program-change precedence (pill > Fixed > style PCs) ──────────────
        // gmControlled=false freezes every slot to its initial instrument.
        void setGmControlled (bool b) noexcept { gmControlled.store (b); }
        bool isGmControlled() const noexcept   { return gmControlled.load(); }

        //---------------------------------------------------------------------
        // IGNORE PROGRAM CHANGE - the user's own, per style slot.
        //
        // Deliberately NOT slotPcLocked.  That flag is machinery: the
        // substitution path raises it, and loading a different style clears it
        // along with the substitutions it belongs to.  This one is a user
        // decision that lives in the set, so it must survive exactly the thing
        // that clears the other - a style change is the loudest program change
        // there is, and surviving it is the entire point of the toggle.
        //
        // Read from the audio thread (dispatchOne) and the message thread
        // (applyVoiceSetup); atomics, no other synchronisation needed.
        //---------------------------------------------------------------------
        void setSlotPcIgnored (int engineCh, bool ignored) noexcept
        {
            if (engineCh >= 0 && engineCh < kNumUserStyleSlots)
                slotPcIgnored[(size_t) engineCh].store (ignored);
        }
        bool isSlotPcIgnored (int engineCh) const noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return false;
            return slotPcIgnored[(size_t) engineCh].load();
        }

        // A pinned slot (user reference-voice pill) ignores the style's PCs.
        void setSlotPcLocked (int engineCh, bool locked) noexcept
        {
            if (engineCh >= 0 && engineCh < kNumUserStyleSlots)
                slotPcLocked[(size_t) engineCh].store (locked);
        }
        bool isSlotPcLocked (int engineCh) const noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return false;
            return slotPcLocked[(size_t) engineCh].load();
        }

        // -- Per-slot instrument substitution (set-level override) -----------
        // Message thread.  Pins style slot `engineCh` (0..7) to instrument
        // `flag` (any melodic flag, including >= 200 reference sounds): decodes
        // + publishes it now and locks out the style's program changes so the
        // user's choice survives transitions.  flag < 0 clears the override.
        void setSlotSubstitution (int engineCh, int flag)
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return;
            if (flag < 0) { clearSlotSubstitution (engineCh); return; }
            slotSubFlag[(size_t) engineCh].store (flag);
            engine.setDrumChannel (engineCh, false);   // a substituted slot is melodic
            engine.selectChannelPreset (engineCh, flag);
            slotPcLocked[(size_t) engineCh].store (true);

            // The channel now holds an ORDINARY GM voice the user picked, so the
            // MegaVoice rules must stop applying to it.
            //
            // They would otherwise keep applying, because the 0xC0 handler
            // returns on slotPcLocked BEFORE it reaches the line that updates
            // lastAppliedMsb — so a locked slot keeps whatever bank the style
            // last put there, forever.  On a slot whose style voice was a
            // MegaVoice (MSB 8 — ordinary for CHORD / PHRASE guitar parts),
            // isMegaVoiceArticulation stayed true for the user's voice, and
            // every source note >= C7 — a fret-noise / dead-note TRIGGER, not a
            // pitch — was chord-snapped and played as a real note.  On a guitar
            // those triggers are noise; on a piano they are extra chord tones,
            // thickening and hardening every chord the part plays.
            // megaVoiceMusicalVelocity kept rewriting velocities on the same
            // stale basis.
            //
            // -1 is "generic GM, bank not set", which is what a user pick is.
            // Lsb/Pc go to the -2 "nothing applied" sentinel so the redundancy
            // cache cannot later skip a genuine program change against a
            // half-stale triple.
            lastAppliedMsb[(size_t) engineCh] = -1;
            lastAppliedLsb[(size_t) engineCh] = -2;
            lastAppliedPc [(size_t) engineCh] = -2;
        }

        // Message thread.  Removes the override and restores the style's own
        // voice (if one was recorded), re-enabling the style's program changes.
        void clearSlotSubstitution (int engineCh)
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return;
            slotSubFlag[(size_t) engineCh].store (-1);
            slotPcLocked[(size_t) engineCh].store (false);

            // Hand the bank identity back to the style: restore the slot's setup
            // bank (so MegaVoice classification is right again the moment the
            // style's own voice is back) and invalidate the rest of the
            // redundancy triple, so the next authored PC is genuinely re-applied
            // rather than skipped against the substitution's stale entry.
            lastAppliedMsb[(size_t) engineCh] = setupBankMsb[(size_t) engineCh];
            lastAppliedLsb[(size_t) engineCh] = -2;
            lastAppliedPc [(size_t) engineCh] = -2;

            const int styleFlag = slotStyleFlag[(size_t) engineCh];
            if (styleFlag >= 0)
                engine.selectChannelPreset (engineCh, styleFlag);
        }

        int getSlotSubstitution (int engineCh) const noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return -1;
            return slotSubFlag[(size_t) engineCh].load();
        }

    private:
        //----------------------------------------------------------------------
        // Map a raw style source channel (0..15) onto a user-visible engine
        // style channel (0..7).  Yamaha styles use ch 8..15 — subtract 8.
        // Any incoming channel outside 8..15 is clamped into 0..7.
        //----------------------------------------------------------------------
        // TEMP diagnostics: append a line to D:\\workspace\\BetelgeuseArranger\\grex_log.txt.
        // reset=true overwrites the file (start of a fresh style load).
        static void grexLog (const juce::String& line, bool reset = false)
        {
            const juce::File f = GrexPaths::styleLog();   // plugin root — see GrexPaths
            if (reset) f.replaceWithText (line + juce::newLine);
            else       f.appendText     (line + juce::newLine);
        }

        // ── One-shot CASM-miss diagnostic helpers (temporary) ───────────────
        // Pretty-print the held chord and a 16-bit source-channel set for the
        // grex_log.txt summary written from dispatchBlock.  Remove together
        // with diagCasmLogArmed once chord switching is confirmed.
        static juce::String diagChordName (const Chord& c)
        {
            static const char* n[12] =
                { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int r = ((c.root % 12) + 12) % 12;
            return juce::String (n[r]) + " q" + juce::String ((int) c.quality);
        }
        static juce::String diagChannelMask (uint16_t mask)
        {
            juce::String s;
            for (int ch = 0; ch < 16; ++ch)
                if (mask & (uint16_t) (1u << ch))
                    s << (s.isEmpty() ? "" : ",") << juce::String (ch);
            return s.isEmpty() ? juce::String ("-") : s;
        }

        static int mapSourceToEngineChannel (int srcCh) noexcept
        {
            // SFF roles: Ch9 (srcCh 8) = SubRhythm/percussion, Ch10 (srcCh 9) =
            // main Rhythm (the mandatory drum-kit channel).  The whole UI —
            // SoundsTab slot 0, mixer fader 0, MainTab "DRUMS" element, the
            // crash trigger on engine 0 — assumes engine channel 0 holds the
            // MAIN drum kit.  A plain srcCh-8 map would put the perc channel on
            // engine 0 and the drums on engine 1 (the swap the user saw), so
            // route the main rhythm to engine 0 and the sub-rhythm to engine 1.
            // Both load-time setup and the runtime dispatcher resolve through
            // here, so a kit and its notes always land on the same channel.
            if (srcCh == 8) return 1;   // Ch9  SubRhythm/perc -> engine 1 (PERC)
            if (srcCh == 9) return 0;   // Ch10 main Rhythm    -> engine 0 (DRUMS)
            if (srcCh >= 10 && srcCh <= 15) return srcCh - 8;   // Ch11..16 -> 2..7
            return juce::jlimit (0, kNumUserStyleSlots - 1, srcCh);
        }

        //----------------------------------------------------------------------
        // CASM-aware destination resolution.  The dispatcher routes every
        // runtime event through casm->dstChannel when the playing section has a
        // CASM rule for the event's source channel, so the load-time setup MUST
        // target the same destination(s) or instruments/kits land on slots the
        // notes never reach.
        //----------------------------------------------------------------------
        /** True if ANY present section carries a CASM rule for this source
            channel.  Used to tell a real CASM-routed source variant (channels
            0..7 in multi-record styles) apart from the vestigial SInt setup
            mirror (channels 0..7 with no routing) so applyVoiceSetup sets up
            the former and skips the latter. */
        static bool sourceHasCasm (const StyleData& style, int srcCh) noexcept
        {
            for (const auto& sec : style.sections)
                if (sec.present && sec.findCasm ((uint8_t) srcCh) != nullptr)
                    return true;
            return false;
        }

        /** The engine slot this source channel's events will land on: the first
            present section's CASM dstChannel for it, else the source itself. */
    public:
        //======================================================================
        // BAKE SUPPORT — resolve a style's eight slot programs WITHOUT loading it.
        //
        // Everything the resolution needs is static and engine-free: the CASM
        // destination map, the CASM voice name, the bank lookup and the bass-role
        // correction.  So the set baker can parse 633 style files and work out
        // what voice each slot would end up with, without decoding a single
        // sample or composing a single kit — which is the difference between a
        // few seconds and most of an hour.
        //
        // This MIRRORS the melodic branch of applyVoiceSetup exactly, including
        // the undeclared-source-with-a-CASM-name rescue and the bass-role
        // correction, so a baked value is the value a real load would produce.
        // Drum slots come back as -1: they have no GM program, and the GM table
        // ignores anything outside 0..127 anyway.
        //======================================================================
        static void resolveStyleSlotFlags (const StyleData& style,
                                           BankProgramMap& bankMap,
                                           int outFlags[kNumUserStyleSlots]) noexcept
        {
            for (int i = 0; i < kNumUserStyleSlots; ++i) outFlags[i] = -1;

            for (int srcCh = 0; srcCh < (int) style.voices.size(); ++srcCh)
            {
                const auto& v = style.voices[(size_t) srcCh];

                // Admission test first, and it asks the ANY-destination
                // question: a source that declares nothing AND carries no CASM
                // name anywhere is not a part.
                if (! v.isUsed() && casmVoiceNameFor (style, srcCh) == nullptr) continue;

                const int engineCh = resolveDestChannel (style, srcCh);
                if (engineCh < 0 || engineCh >= kNumUserStyleSlots) continue;

                // ...and only now the name that belongs to THIS destination.
                const char* vName = casmVoiceNameFor (style, srcCh, engineCh);

                // Drum destinations carry no melodic program — leave them at -1
                // rather than resolving their kit PC into the melodic table.
                if (engineCh == kDrumsEngineSlot || engineCh == kPercEngineSlot) continue;

                if (v.program < 0 && vName == nullptr) continue;

                const int rawIdx = bankMap.resolve (v.bankMsb, v.bankLsb,
                                                    v.program, vName);
                outFlags[engineCh] = (engineCh == kBassEngineSlot)
                                       ? BankProgramMap::correctFlagForBassRole (rawIdx, vName)
                                       : rawIdx;
            }
        }

    private:
    public:
        /** Host -> player: load a full sampled kit if the policy allows.
            Returns true when it did, so the caller skips the composed path. */
        std::function<bool (int engineCh, int msb, int lsb, int pc, bool isPercSlot)>
            onLoadFullKit;

        /** Host -> player: which COMPOSED kit answers a request nothing else
            can.  -1 means "no opinion, use the existing nearest-family logic". */
        std::function<int (int msb, int lsb, int pc)> onComposedFallbackPc;

    private:
        static int resolveDestChannel (const StyleData& style, int srcCh) noexcept
        {
            for (const auto& sec : style.sections)
            {
                if (! sec.present) continue;
                if (const auto* c = sec.findCasm ((uint8_t) srcCh))
                    return mapSourceToEngineChannel ((int) c->dstChannel);
            }
            return mapSourceToEngineChannel (srcCh);
        }

        /** Every DISTINCT engine slot this source can reach across all present
            sections (different sections may carry different CASM routings).
            Writes up to kNumUserStyleSlots entries into `out`; returns count.
            Always contains at least one entry (the source-mapped fallback). */
        static int collectDestChannels (const StyleData& style, int srcCh,
                                        int (&out)[kNumUserStyleSlots]) noexcept
        {
            int n = 0;
            auto add = [&] (int engineCh)
            {
                for (int i = 0; i < n; ++i) if (out[i] == engineCh) return;
                if (n < kNumUserStyleSlots) out[n++] = engineCh;
            };
            bool anyCasm = false;
            for (const auto& sec : style.sections)
            {
                if (! sec.present) continue;
                if (const auto* c = sec.findCasm ((uint8_t) srcCh))
                {
                    anyCasm = true;
                    add (mapSourceToEngineChannel ((int) c->dstChannel));
                }
            }
            if (! anyCasm)
                add (mapSourceToEngineChannel (srcCh));
            return n;
        }

        /** The author-written CASM voice label for a source channel (e.g.
            "Bass", "Strings", "Choir", "Trumpet"), taken from the first present
            section that routes it.  Returned to BankProgramMap::resolve as the
            voice-name fallback so a Yamaha MegaVoice (bank-8) part whose exact
            (LSB, PC) isn't in the explicit table resolves to the RIGHT GM family
            from its name instead of silently collapsing to piano.  nullptr when
            no section names this source. */
        /** The CASM voice name for a source, FOR THE DESTINATION IT FEEDS.

            The destination argument is the whole point.  This used to take the
            source alone and return the first name it found anywhere in the
            style, and that is wrong the moment one source feeds more than one
            destination - which converted styles do constantly, because they
            re-voice per section.  Measured on a Roland 70s Pop conversion: 12 of
            its 16 sources carry MORE THAN ONE name across the file, and a single
            first-hit lookup kept one and threw the rest away.  The worst case
            there was source 10, which is percussion (`Std.Kit1`) into the PERC
            slot in one section and a bass (`MellowFi`) into BASS in ten - and the
            melodic name won, so the percussion pre-warm resolved a drum part
            through a melodic name.

            The name is not cosmetic: bankMap.resolve uses it to disambiguate a
            (bank, program) pair, and correctFlagForBassRole reads it to decide
            whether a part is a bass at all.  A wrong name is a wrong instrument.

            Destination-scoped, the same file drops from 12 ambiguous sources to
            5 ambiguous (source, destination) pairs out of 30 - and those five
            are genuine per-section re-voicing, which the RUNTIME path already
            handles correctly because dispatchOne reads casm->voiceName from the
            section in hand.

            engineDst < 0 asks the old question - "any name for this source" -
            which is still the right one for a presence test. */
        static const char* casmVoiceNameFor (const StyleData& style, int srcCh,
                                             int engineDst = -1) noexcept
        {
            // PASS 1: the name this source carries where it feeds THIS slot.
            if (engineDst >= 0)
                for (const auto& sec : style.sections)
                {
                    if (! sec.present) continue;
                    for (const auto& c : sec.casm)
                        if (c.srcChannel == (uint8_t) srcCh
                            && c.voiceName[0] != 0
                            && mapSourceToEngineChannel ((int) c.dstChannel) == engineDst)
                            return c.voiceName;
                }

            // PASS 2: any name for this source.  Reached by presence tests, and
            // by a destination this source never actually feeds.
            for (const auto& sec : style.sections)
                if (sec.present)
                    if (const auto* c = sec.findCasm ((uint8_t) srcCh))
                        if (c->voiceName[0] != 0)
                            return c->voiceName;
            return nullptr;
        }

        // ── Active-note tracker helpers (audio thread only) ─────────────────
        int findActiveNote (int ch, int srcNote) const noexcept
        {
            for (int i = 0; i < activeNoteCount; ++i)
                if (activeNotes[(size_t) i].ch == ch
                    && activeNotes[(size_t) i].srcNote == srcNote)
                    return i;
            return -1;
        }

        // ── Bass "allowed notes" register lock ────────────────────────────
        // Fold a note destined for the BASS slot into the user's 12-note
        // window [lo, hi].  Applied to the FINAL sounding pitch (after chord
        // transposition) so the part stays in the chosen register for every
        // chord.  A ≥12-note window holds at least one representative of each
        // pitch class, so the fold always converges.  Disabled slots (toggle
        // off) and non-style channels pass through untouched.
        int foldNote (int dstEngineCh, int note) const noexcept
        {
            if (dstEngineCh < 0 || dstEngineCh >= kNumUserStyleSlots) return note;
            if (! noteRangeOn[(size_t) dstEngineCh].load())          return note;
            const int lo = noteRangeLo[(size_t) dstEngineCh].load();
            const int hi = noteRangeHi[(size_t) dstEngineCh].load();
            if (hi <= lo) return note;
            while (note < lo) note += 12;
            while (note > hi) note -= 12;
            if (note < lo)   note  = lo;   // guard (window ≥ 12 keeps this unused)
            return note;
        }

        void trackNoteOn (int ch, int srcNote, int destNote, int vel,
                          const CasmEntry* casm) noexcept
        {
            // FIFO, instance-correct: every note-on gets its OWN entry — the
            // same (ch, srcNote) can legitimately sound several times at once
            // (authored same-pitch overlaps, or an old-chord instance ringing
            // under the deferred switch while the new chord re-strikes that
            // source note).  Each authored note-off then releases exactly ONE
            // instance — the OLDEST (findActiveNote returns the first match) —
            // so overlaps survive and a re-strike never chops the previous
            // instance.  The former single-slot overwrite (+ forced release of
            // the old pitch) was exactly that chop.
            // FULL: evict the OLDEST entry — releasing it for real — instead of
            // dropping the new one on the floor.  The old code silently skipped
            // the store while the caller still sent engine.noteOn, so that note
            // could never be found by dispatchNoteOff: its voice sustained
            // forever and its key stayed lit on the piano strip.  Worse, the
            // tracker only empties via note-off or the boundary flush, so once
            // it pinned at capacity EVERY subsequent note leaked the same way —
            // one overflow cascaded into the "many stuck notes" state.  Evicting
            // bounds the damage to the single oldest note, which is by far the
            // most likely one to have lost its note-off already.
            if (activeNoteCount >= kMaxActiveNotes)
            {
                engine.noteOff (activeNotes[0].ch, activeNotes[0].destNote);
                removeActiveAt (0);
            }

            if (activeNoteCount < kMaxActiveNotes)
                activeNotes[(size_t) activeNoteCount++] = { ch, srcNote, destNote, vel, casm };
        }

        void removeActiveAt (int idx) noexcept
        {
            // ORDERED erase (shift-left), not swap-with-last: the tracker is
            // FIFO per (ch, srcNote) — findActiveNote's first match must stay
            // the OLDEST instance so each authored note-off releases the pitch
            // of ITS OWN instance, in order, across same-pitch overlaps.
            for (int i = idx; i < activeNoteCount - 1; ++i)
                activeNotes[(size_t) i] = activeNotes[(size_t) (i + 1)];
            --activeNoteCount;
        }

        /** Note-off that targets the ACTUAL sounding pitch (from the tracker),
            so a chord change between a note's on and off can't strand it as a
            stuck note.  Falls back to live transposition if it wasn't tracked. */
        void dispatchNoteOff (int dstEngineCh, int srcNote,
                              const CasmEntry* casm, const Chord& chord)
        {
            const int idx = findActiveNote (dstEngineCh, srcNote);
            if (idx >= 0)
            {
                engine.noteOff (dstEngineCh, activeNotes[(size_t) idx].destNote);
                removeActiveAt (idx);
                return;
            }
            if (casm != nullptr)
            {
                // Pick path based on current load mode; both paths return the
                // same Result struct, so the caller is mode-agnostic.
                const auto r = NoteTransposer::applyAuto (srcNote, chord, *casm);
                if (r.shouldPlay) engine.noteOff (dstEngineCh, foldNote (dstEngineCh, r.destNote));
            }
            else
                engine.noteOff (dstEngineCh, foldNote (dstEngineCh, srcNote));
        }

        /** MIDI note of pitch-class `pc` (0..11) nearest to `reference`, within
            +/-6 semitones — used to force a held note to the new chord's root
            for the *_TO_ROOT retrigger rules. */
        static int nearestPitchClass (int pc, int reference) noexcept
        {
            pc = ((pc % 12) + 12) % 12;
            int n = (reference / 12) * 12 + pc;
            if (n - reference >  6) n -= 12;
            if (n - reference < -6) n += 12;
            return n;
        }

        /** Re-voice every still-sounding style note to a new chord, honouring
            each channel's RetriggerRule (RTR) from the style file — the real-
            time equivalent of a hardware arranger's note-transition handling:

              STOP                cut the note (silence until the next note-on)
              PITCH_SHIFT         glide the voice to the new chord tone, NO re-attack
              PITCH_SHIFT_TO_ROOT glide the voice to the new chord's root, NO re-attack
              RETRIGGER           re-strike the note at the new chord tone
              RETRIGGER_TO_ROOT   re-strike the note at the new chord's root
              NOTE_GENERATOR      treated as PITCH_SHIFT

            The smooth (PITCH_SHIFT) path re-pitches the sounding voice in place,
            so it avoids the "style restart" click a blanket re-attack produced.
            Notes whose pitch doesn't change are left untouched.  Untransposed
            parts (drums / Bypass) ignore the chord and are skipped.

            In Betelgeuse multi-source dispatch, a held note from a variant that
            no longer plays for the new chord is dropped first, so the new
            winner's events own the next note-on cleanly. */
        void retriggerForChordChange (const Chord& newChord)
        {
            // DEFERRED chord switch (real-arranger feel).  Sounding style notes
            // are NOT re-pitched or dropped on a chord change: they keep their
            // old-chord pitch and end at their natural, authored note-offs
            // (dispatchNoteOff releases the TRACKED pitch, and note-offs bypass
            // the winner gate, so nothing strands).  New note-ons transpose
            // against the latched new chord — the switch lands on the next
            // played notes, so the groove never feels restarted.
            //
            // One exception: the PAD slot.  Pads hold whole-bar sustains that
            // would otherwise ring the OLD harmony deep into the new chord, so
            // the pad's sounding notes glide to the new chord immediately
            // (retune in place — envelope and sample position intact, no
            // re-attack).  The boundary flush still clears everything at loop
            // wraps and section changes, and the mono bass re-articulates on
            // its very next note anyway.
            for (int i = 0; i < activeNoteCount; ++i)
            {
                auto& a = activeNotes[(size_t) i];
                if (a.casm == nullptr)        continue;   // untransposed — ignores the chord
                if (a.ch != kPadEngineSlot)   continue;   // defer: next note-on answers
                if (isChannelMuted (a.ch))    continue;   // muted — already silenced

                // Resolve the pad note's pitch under the new chord.  If the new
                // chord drops it (e.g. the maj7 drop rule), release it — it has
                // no place in the new harmony.
                const auto r = NoteTransposer::applyAuto (a.srcNote, newChord, *a.casm);
                const int newDest = r.shouldPlay ? foldNote (a.ch, r.destNote) : -1;

                if (newDest < 0)
                {
                    engine.noteOff (a.ch, a.destNote);
                    removeActiveAt (i); --i;
                    continue;
                }
                if (newDest == a.destNote)    continue;   // unchanged — no glitch

                if (engine.retuneNote (a.ch, a.destNote, newDest))
                    a.destNote = newDest;                 // smooth glide, envelope intact
                else
                {
                    engine.noteOff (a.ch, a.destNote);    // glide fallback: re-strike
                    engine.noteOn  (a.ch, newDest, a.vel);
                    a.destNote = newDest;
                }
            }
        }

        //======================================================================
        // Bank 126 is Yamaha's "SFX Kit" bank.  Despite the name it holds every
        // ethnic and Latin PERCUSSION kit as well as the SFX/noise ones:
        //
        //    0,1 SFX Kit1/2      35,36 ArabicKit2/1     64 ArabicMixKit
        //    2,3 NewSFXKit1/2    37    KhaligiKit       65 KhaligiMixKit
        //    8   NoisesKit       38    IranianKit       66 IranianMixKit
        //    49  CymbalKit       40    CubanKit         67 TurkishKit
        //    109 VocalEffectsKit 43,44 PopLatinKit1/2   114 IndianKit
        //    110 GospelAdLibs    45    PopPercKit       124 ChineseKit
        //                        119   BrazilianKit
        //
        // It numbers its kits COMPLETELY differently from the drum bank (127),
        // so a bank-126 PC must never be handed to the drum-bank tables —
        // drumKitPCMap / nearestDrumFamily are bank-127 numbering and mis-land
        // them badly:
        //      PC 78 -> "032" Jazz          PC 40 (CubanKit)     -> "040" Brush
        //      PC 8  (NoisesKit) -> "008" Room
        //      PC 64 (ArabicMixKit)         -> "024" Electro
        // An oriental style's percussion was playing a JAZZ kit for the whole
        // song because of exactly that.
        //
        // Grex ships no percussion / oriental kit, so every bank-126 kit maps to
        // STANDARD.  That isn't arbitrary: GM's upper drum range (60..81) IS real
        // hand percussion — congas, bongos, timbales, agogo, cabasa, maracas,
        // claves, guiro, woodblock, triangle — so a percussion part at least
        // lands on percussion.  The lower keys (35..59) will hit kick/snare/toms
        // rather than darbuka/riq; that needs a real oriental kit, not a table.
        //
        // If such a kit is ever added, THIS is the single place to re-point.
        // Returns a bank-127 PC for SamplePlayerEngine::resolveDrumKitParams.
        //======================================================================
        static int percBankKitPc (int /*bank126Pc*/) noexcept
        {
            return 0;      // drumKitPCMap[0] == "000" Standard
        }

        //======================================================================
        // Style CC 7 -> engine channel gain  (GM volume law + per-slot trims).
        //
        // CC 7 is NOT linear amplitude.  GM (and every Yamaha arranger) maps
        //
        //     gain = (v / 127)^2         — the spec's 40·log10(v/127) dB curve
        //
        // so a composer's fader of 55 means amplitude 0.188 (−14.5 dB), not
        // 0.433.  Every style ever authored is balanced against that law;
        // reading CC 7 linearly (the old code) inflated every deliberately-
        // quiet part, and the lower the authored fader the bigger the error:
        // +2.1 dB at 100, +5.1 dB at 71, +7.3 dB at 55, +11 dB at 36.  That is
        // exactly what made hot-take sections (the 80sDisco Intro B/C chord
        // channels, authored around CC 7 55/71) blast.  Squaring HERE — the
        // one funnel both the SInt setup and the per-section CC 7 ride flow
        // through — restores the composer's mix for the entire style library
        // at once.
        //
        // NOTHING IS TRIMMED ON TOP OF THIS ANY MORE.
        //
        // This block used to list a PERC ×0.50, a BASS ×1.35, a DRUMS ×2 boost
        // and the per-style auto-balance.  Every one of them has been removed:
        // effectiveStyleCc7 now ends in a bare `return styleVolume`, and the two
        // automatic drum factors are gated to unity.  The square law below is
        // the whole of what a style's stated volume becomes.
        //
        // Left as a note rather than deleted because the list read as current
        // long after the code went, and it produced a wrong diagnosis of why a
        // hot converted style hits hard: the answer is that NOTHING corrects it,
        // not that some trim is correcting it badly.
        //
        // Only what the STYLE writes is scaled.  The faders stay normal user
        // controls: move one by hand and it does exactly what it says, and the
        // ride remains last-writer-wins as before.  A fader position now shows
        // the LAW-CORRECT gain (CC 7 55 parks the slider low, like the
        // hardware's real output level) — headroom above it is the user's.
        //======================================================================
        static constexpr int kPercEngineSlot    = 1;     // style slot 1 = "PERC"
        // kBassEngineSlot (= 2) is already declared above, with the note-fold.
        // BASS auto-level multiplier.  Combined with the x2 FADER doubling in
        // effectiveStyleCc7, the bass lands at x4.00 total (+12.0 dB) over what
        // the style's CC 7 asks for — 2.0 here x 2.0 on the fader.
        //
        // It was x8.1 (405 here): the original x1.35 "runs thin" trim, x3 for
        // register/monophony, then x2 on the fader.  Measured against the two
        // reference logs that put the bass ~21 dB above the average melodic
        // slot where both composers had written it ~3 dB above — far past what
        // the structural argument below justifies, because that argument was
        // derived from a PER-NOTE gain comparison which under-counts the
        // polyphonic parts (they stack 3..4 notes, the bass never does).
        //
        // The structural reasons are still real, and are what x4 pays for:
        // 6-8Orchestral the bass's raw per-note gain is −16.9 dB, which is
        // ABOVE CHORD1 (−24.3), CHORD2 (−21.0) and PHRASE1 (−19.1) and level
        // with PAD.  On paper it is one of the loudest parts; in the room it
        // disappears, for two reasons the gain table cannot see:
        //
        //   REGISTER — the style authors bass around MIDI 29 (~44 Hz), one and
        //   a half to two octaves below every other part (medians 60..76).  At
        //   40..60 Hz the ear needs roughly 10..20 dB more level than at 1 kHz
        //   for the same loudness, so equal electrical gain is nowhere near
        //   equal audibility.
        //
        //   MONOPHONY — the bass slot is Mono: exactly ONE voice, ever.  The
        //   chord parts are polyphonic and stack 3..4 simultaneous notes, which
        //   sums to 10..12 dB on the bus.  A per-note gain comparison flatters
        //   the bass by that whole margin (108 bass notes here against CHORD1's
        //   809).
        //
        //   Both are structural, so they apply to every style, not this one —
        //   which is why this belongs in the calibration and not on a fader.
        //
        // At x4 the bass slot only passes unity gain on its own once a style
        // authors CC 7 above 64 (it was 45 at x8.1), so it no longer eats the
        // style bus's headroom by itself.

        //======================================================================
        // Drum auto-balance calibration (DRUMS slot 0 only — see
        // computeDrumBalanceFactor and the fader application in applyVoiceSetup).
        //
        // "Hotness" = the 95th-percentile of the per-tick SUMMED velocity-gain of
        // the DRUMS part over the Main sections.  It measures the summed signal,
        // not single notes, because simultaneous hits ADD on the bus — the reason
        // a dense kit clips where a loud-but-sparse one doesn't.  Reference
        // numbers (measured):
        //     sparse "normal" style   p95 ~ 0.45 .. 0.55   -> inside band, no change
        //     9-8 Balkan              p95 ~ 2.16           -> x0.50  (-6.0 dB)
        //     Cucek                   p95 ~ 1.38           -> x0.65  (-3.7 dB)
        //
        // EXTREME-ONLY: inside the dead-band the factor is exactly 1.0, so an
        // ordinary style keeps the composer's CC 7 balance untouched.  Outside it,
        // the factor scales the DRUMS fader back toward the nearest edge, clamped
        // so it can never do anything drastic.  All four numbers are here to tune
        // by ear.
        static constexpr int   kDrumsEngineSlot      = 0;      // style slot 0 = "DRUMS"
        static constexpr float kDrumBalanceLo        = 0.60f;  // below -> lift (cold)
        static constexpr float kDrumBalanceHi        = 0.90f;  // above -> cut  (hot)
        //======================================================================
        // RHYTHM CEILING — caps how far the kit may sit ABOVE the rest of the
        // style, on top of the density balance below.
        //
        // The density balance measures the drum part ALONE, so it cannot see a
        // style whose kit is simply authored louder than everything around it.
        // EMC / Style Works conversions of Korg styles do exactly that: the
        // 6/8 Ballad reference writes BOTH rhythm parts at CC 7 90 while every
        // melodic part sits at 49..56.  Measured rhythm-to-melodic ratio after
        // today's balance:
        //
        //     Korg 6/8 Ballad  +2.5 dB      <- the complaint
        //     6-8Orchestral    -1.0 dB      <- reference (approved by ear)
        //     90sPopBallad     -6.6 dB
        //     Dosari           -8.2 dB
        //     80sDisco         -9.3 dB
        //
        // So the ratio is measured and, if it exceeds the reference, the rhythm
        // slots are scaled down to it.  Deliberately a CEILING, not a
        // normalisation: a style whose kit sits politely below the reference is
        // left exactly as its composer wrote it.  Applied to DRUMS *and* PERC,
        // because in these conversions PERC is a second full kit, not
        // percussion — cutting only DRUMS would move half the problem.
        //
        // kRhythmCeilingRatio is the one dial.  0.891 = -1.0 dB = 6-8Orchestral.
        // Note that 6-8Orchestral is the HOT END of the native styles (the
        // other three cluster at -6.6..-9.3 dB); lowering this constant toward
        // 0.47 (-6.6 dB, 90sPopBallad) would pull the whole library tighter,
        // at the cost of also cutting 6-8Orchestral by 5.6 dB.
        //======================================================================
        static constexpr float kRhythmCeilingRatio = 0.891f;   // -1.0 dB
        static constexpr float kRhythmCeilingMin   = 0.35f;    // never cut more than -9.1 dB

        static constexpr float kDrumBalanceMinFactor = 0.50f;  // never quieter than -6 dB
        static constexpr float kDrumBalanceMaxFactor = 2.00f;  // never louder  than +6 dB

        // DRUMS output boost, applied to the FINAL drums fader value (the
        // style's CC 7 x the auto-balance factor) in BOTH places the style
        // writes it — the SInt setup AND the per-section CC 7 ride — so a
        // mid-song ride can't walk it back.
        //
        // NOW 1.0 (no boost).  It was 2.0, added while CC 7 was still read
        // LINEARLY — back then the law itself was under-reading the kit and the
        // x2 was compensating for it.  With the real GM law now applied
        // law, that compensation double-counts: measured on 80sDisco it left
        // the drums +1.0 dB ABOVE their old level while every melodic part fell
        // 5..25 dB, which is the "kit hits like a hammer, everything else is
        // quiet" balance.  This is the one dial to raise if a genre pass wants
        // the kit hotter — but raise it against the corrected law, not to
        // compensate for a broken one.
        // Ceiling on the auto-level multiplier.  The FADER is separately bounded
        // by the style's own CC 7 (never above 127 = unity), so this only bounds
        // the calibration factors — drumBalance can reach 2.0, and nothing may
        // push a slot beyond +6 dB of what its fader shows.
        // Headroom for the largest legitimate auto-level: the BASS trim (×4.05
        // at CC 7 127).  It was 2.0, which would have silently CLIPPED the bass
        // boost on any style whose bass fader sits above CC 7 ~63 — the boost
        // would have appeared to work on quiet styles and quietly stopped
        // scaling on loud ones.  Nothing else comes close: drums cap at
        // balance ×2.0, PERC at ×0.5, every other slot at ×1.0.
        static constexpr float kAutoLevelMax = 4.5f;

        //======================================================================
        // Per-INSTRUMENT fader trims — voices authored hot across most
        // libraries, trimmed wherever a STYLE loads them:
        //
        //     GM 56..71  the whole GM BRASS and REED block, to a QUARTER
        //                (-12 dB): 56 Trumpet, 57 Trombone, 58 Tuba,
        //                59 Muted Trumpet, 60 French Horn, 61 Brass Section,
        //                62/63 Synth Brass 1-2, 64..67 Saxes, 68 Oboe,
        //                69 English Horn, 70 Bassoon, 71 Clarinet
        //     GM 53      Voice Oohs, to a QUARTER (-12 dB)
        //     GM 88      Pad 2 (warm), to a HALF (-6 dB)
        //     GM 105     Banjo, to 1/3.5 (-10.9 dB) — that library sample is
        //                recorded hot, so a style CC 7 of 70 parks it at 20
        //
        // NOTE: GM 61 Brass Section sits INSIDE this range, so it takes the
        // quarter trim rather than the half it once had on its own.  If it
        // should go back to -6 dB, give it an explicit case ahead of the range
        // test above.  (The per-instrument trim table itself is gone — see
        // the note where instrumentFaderMul used to be.)
        //
        // The BASS slot returns before this trim (it has its own calibration),
        // so GM 58 Tuba — which correctFlagForBassRole deliberately passes
        // through as a legitimate bass voice for polka / brass-band styles — is
        // untouched when it IS the bass, and trimmed only when a style puts it
        // on a melodic slot.
        //
        // Unlike the PERC / BASS trims (which are per-SLOT and live in the
        // auto-level, invisible to the user), these are deliberately applied to
        // the FADER: the value the mixer shows is the trimmed one.  A style CC 7
        // of 61 on Pad 2 parks the slider at 31 (30.5, rounded to nearest), and
        // the gain moves with it because gain = fader x auto-level.
        //
        // Keyed on the SOUNDING flag, so the trim follows a mid-song program
        // change onto or off one of these voices, on whichever slot it lands.
        //
        // Style-driven loads only.  A user who picks one of these by hand, or
        // moves the fader afterwards, is not overridden — this is a calibration
        // of what the style asked for, not a cap.
        //======================================================================

        // Multipliers rather than integer percents: the banjo's 1/3.5 has no
        // exact percent (28.571...), and rounding it to 29 would drift on some
        // CC 7 values.

        /** Fader multiplier for the instrument a slot is sounding; 1.0 leaves
            the style's CC 7 alone.  Most specific test first, so adding a
            single-instrument exception ahead of a range is a one-line change. */
        // instrumentFaderMul removed — per-instrument level now lives in the
        // sound's own GAIN trim / base unity, not in a hardcoded table.

        // GM 39 "Synth Bass 2" runs at ONE TENTH of the gain the other basses
        // get (x4 / 10 = x0.4).  Calibrated, not guessed: with the part in its
        // correct register the divisor of 3 it carried before still needed the
        // fader pulled from 65 down to 20 by hand to sit in the mix, and 10
        // reproduces that to within 0.2 dB.
        //
        // The reason it needs so much more trim than the register argument
        // alone suggests: kSynthBass2Floor folds this voice's part two octaves
        // UP to correct a library sample recorded two octaves low, and the
        // resampled result is far hotter than the other basses.
        //
        // Placed entirely in the AUTO-LEVEL, "behind the scenes": the fader is
        // left showing the composer's own CC 7, undoubled, so the mixer never
        // implies a level the style did not ask for.
        static constexpr int kSynthBass2Flag       = 39;

        /** True when this slot is sounding GM 39.  Keyed on the SOUNDING flag,
            so it follows a mid-song program change onto or off Synth Bass 2 the
            same way the Brass Section trim does. */
        bool bassIsSynthBass2 (int engineCh) const noexcept
        {
            return engineCh == kBassEngineSlot
                && slotSoundingFlag[(size_t) engineCh] == kSynthBass2Flag;
        }

        /** The CC 7 value a style slot should actually park its fader on, after
            any per-instrument trim.  Keyed on the flag the slot is SOUNDING, so
            it tracks a mid-song program change as well as the initial setup. */
        int effectiveStyleCc7 (int engineCh, int styleVolume) const noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return styleVolume;

            // The per-INSTRUMENT trims that used to live here are gone — Voice
            // Oohs, the brass/reed block, Pad 2 warm, Banjo, and the BASS x2.
            // They were hardcoded corrections for a handful of the 128
            // instruments, reachable only from a style and editable only by
            // recompiling; the GAIN slider and the base-unity dialog do that
            // job per sound now, visibly, and save it with the preset.
            //
            // ── PERC IS NOT HALVED.  IT WAS, AND THE ×0.50 IS GONE TOO. ──────
            //
            // A long block here used to describe a PERC ×0.50 applied on the
            // visible fader, with the reasoning that Korg and Ketron
            // conversions carry a second full kit on that slot and write it
            // hot.  The observation is still true — measured p90 drum velocity
            // is 125-127 on Ketron conversions against 85 on a native Yamaha
            // style — but the trim itself went with the rest of the per-role
            // percentages, and the comment stayed behind describing machinery
            // that is no longer here.
            //
            // THE PER-ROLE FADER PERCENTAGES ARE GONE.
            //
            // PERC, BASS and DRUMS each used to take a settable share of the
            // style's own CC 7 here.  That is precisely what the eight mixer
            // faders do — and the mixer travels in the set, so the user's number
            // survives, where a hidden percentage could only be inferred by ear.
            // Two mechanisms for one job is what this whole pass exists to end.
            //
            // The style's stated volume now passes through untouched, and every
            // balance decision after it belongs to the mixer or the sound editor.
            return styleVolume;
        }

        //======================================================================
        // MegaVoice articulation keys — FALLBACK (play them, don't drop them).
        //
        // Yamaha's MegaVoice bank (MSB 8) parks the instrument's ARTICULATIONS at
        // the top of the keyboard -- fret noise, dead notes, slides, hammer-ons,
        // harmonics -- and chooses between them by VELOCITY.  They are TRIGGERS,
        // not pitches: nothing musical is meant to sound at note 117.
        //
        // Grex ships no such samples.  Played as literal pitches they screech,
        // because Channel::findRegion() has nothing mapped up there: it borrows
        // the nearest region it can find -- which can be TWO OCTAVES below -- and
        // stretches it up to reach the note.  It is not a corner case: in
        // CountryBlues_T151, 26% of the LEAD channel is articulation keys
        // (96..117) against a musical range that only reaches 67.  (It also
        // explains why nudging the OCTAVE slider to +1 seemed to "fix" the pitch:
        // it pushed those notes past 127, where every fallback misses and they
        // fall SILENT.  The slider was muting the noise, not correcting it.)
        //
        // FALLBACK, instead of the previous outright drop: hand the key to the
        // instrument the slot ACTUALLY has loaded and let it play it as an
        // ordinary note, at its own natural octave --
        //
        //   1. SNAP to a tone of the held chord.  The pitch of an articulation
        //      key carries no musical information (117 means "slide", not "F8"),
        //      and its zone is usually NTR=RootFixed / NTT=Bypass, which does not
        //      transpose -- so left literal it would sit on a fixed pitch against
        //      every chord and clash.  Snapped, it can only ever land on a chord
        //      tone: it reads as an extra ghost / harmonic, never as a wrong note.
        //   2. FOLD by octaves into the register THE PART ITSELF PLAYS IN --
        //      musicalLo/musicalHi, gathered per slot at style load from every
        //      authored note below the floor (see applyVoiceSetup).  Each key
        //      gets exactly the shift it needs (2, 3 or 4 octaves in the same
        //      style), so the articulation lands BESIDE the part, not above it,
        //      and the OCTAVE slider stays free at 0.
        //
        //      NOT the instrument's sampled key span: a guitar blob is mapped
        //      well above C7, so the keys were already "in range", nothing folded,
        //      and they sounded 2-4 octaves too high.  The sampled span survives
        //      only as foldToInstrumentRange(), the fallback for a slot whose part
        //      has no musical notes at all.
        //
        // 96 (C7) is Yamaha's own boundary: CountryRock_T151's CASM puts its high
        // RootFixed/Bypass zone at exactly 96..127, and every MegaVoice style
        // examined keeps its articulations at 96 and above while the musical part
        // stays well below.
        //
        // Two switches, in case the result wants tuning by ear:
        //   kMegaVoiceArticulationsPlay = false  -> restore the old behaviour
        //                                          (drop the keys, silence).
        //   kMegaVoiceArticulationsSnap = false  -> play them at their literal
        //                                          folded pitch, no chord snap.
        //======================================================================
        static constexpr int  kMegaVoiceBankMsb           = 8;
        static constexpr int  kMegaVoiceArticulationFloor = 96;   // C7

        //======================================================================
        // MegaVoice ARTICULATION VELOCITIES — the velocity twin of the
        // articulation-KEY rule above.
        //
        // A MegaVoice does not use velocity purely as loudness: the upper part
        // of the range SELECTS A PLAYING TECHNIQUE (dead note, mute, slide,
        // harmonic, fret noise).  Composers therefore park a part at a fixed
        // high velocity to choose a technique, not to ask for a fortissimo.
        //
        // Grex substitutes an ORDINARY GM voice for the MegaVoice, and on an
        // ordinary sampled voice velocity IS loudness — and also picks the
        // velocity LAYER, so a high value is both louder AND brighter/harder.
        // Played literally, an articulation-band note becomes an aggressive
        // strum.  Measured on 001_Acoustic_Bld_3_4: its CHORD2 guitar has 75 of
        // 101 notes at exactly velocity 100 and 14 at exactly 13 — two discrete
        // spikes, unmistakably a switch rather than a dynamic curve — and it
        // played as a hard strum through a soft acoustic ballad.
        //
        // A survey of the reference styles shows the rule separates cleanly.
        // Percentage of a MegaVoice source's notes at velocity >= 96:
        //
        //     001_Acoustic  src0   80%   <- the complaint
        //     90sPopBallad  src5   84%   <- same pathology, unreported
        //     next highest         30%
        //     typical               0..13%
        //
        // So the band is remapped rather than dropped (dropping loses the part)
        // and rather than blanket-compressed (that would flatten the genuine
        // dynamics of MegaVoice parts playing in the musical band — e.g. the
        // SAME style's CHORD1, a smooth 3..58 curve on the same instrument,
        // which sounds correct today and must not move).
        //
        // The map is linear and order-preserving, so relative accents inside
        // the band survive; it just lands them where an ordinary voice reads
        // them as playing rather than shouting.  Velocity 100 -> 52.
        //
        //   kMegaVelArticulationFloor = 128 -> disables this entirely.
        //======================================================================
        static constexpr int kMegaVelArticulationFloor = 96;
        static constexpr int kMegaVelMusicalLo         = 40;
        static constexpr int kMegaVelMusicalHi         = 72;

        /** Velocity a MegaVoice articulation-band note should actually sound
            at.  Values below the floor are musical playing and pass through
            untouched. */
        static int megaVoiceMusicalVelocity (int vel) noexcept
        {
            if (vel < kMegaVelArticulationFloor) return vel;

            const int span = 127 - kMegaVelArticulationFloor;
            if (span <= 0) return kMegaVelMusicalLo;

            const float t = (float) (vel - kMegaVelArticulationFloor) / (float) span;
            return juce::jlimit (1, 127,
                                 kMegaVelMusicalLo
                                   + juce::roundToInt (t * (float) (kMegaVelMusicalHi
                                                                    - kMegaVelMusicalLo)));
        }

        /** True when this destination slot is currently sounding a voice that
            came from the MegaVoice bank — the same test isMegaVoiceArticulation
            uses, so key- and velocity-articulations agree on scope. */
        bool isMegaVoiceChannel (int engineCh) const noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return false;
            return lastAppliedMsb[(size_t) engineCh] == kMegaVoiceBankMsb;
        }
        static constexpr bool kMegaVoiceArticulationsPlay = true;
        static constexpr bool kMegaVoiceArticulationsSnap = true;

        bool isMegaVoiceArticulation (int engineCh, int srcNote) const noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return false;
            if (lastAppliedMsb[(size_t) engineCh] != kMegaVoiceBankMsb) return false;
            return srcNote >= kMegaVoiceArticulationFloor;
        }

        /** Octave-fold `note` so that, AFTER the destination channel adds its own
            octave shift inside Channel::noteOn (OCTAVE slider + hidden bias), the
            key lands inside the loaded instrument's mapped register -- i.e. on a
            region that really exists.  Instruments with no content (or a mapping
            narrower than an octave) pass through / clamp; nothing is invented. */
        int foldToInstrumentRange (int engineCh, int note) const noexcept
        {
            int lo = 0, hi = 127;
            engine.getChannelKeyRange (engineCh, lo, hi);
            if (hi < lo) return note;                        // impossible — defensive only

            // Work in the PRE-octave domain: the engine will add this back on.
            const int oct = engine.getChannelOctaveShift (engineCh) * 12;
            lo -= oct;
            hi -= oct;

            // A window narrower than 12 semitones can't hold every pitch class,
            // so the fold wouldn't converge — clamp into it instead.
            if (hi - lo < 11) return juce::jlimit (0, 127, juce::jlimit (lo, hi, note));

            while (note > hi) note -= 12;
            while (note < lo) note += 12;
            return juce::jlimit (0, 127, note);
        }

        /** Fold `note` by octaves into the register the destination slot's part
            actually PLAYS in for the loaded style (musicalLo/musicalHi).  This is
            what puts an articulation key beside the part instead of 2-4 octaves
            above it -- per key, so keys needing -2, -3 and -4 octaves all land.

            NO octave pre-compensation here, unlike foldToInstrumentRange: the
            register is in the AUTHORED (pre-octave) domain, exactly like the
            musical notes we hand the engine, so the channel's OCTAVE slider then
            shifts the articulation and the part together -- which is the whole
            point.  A slot whose part has no musical notes at all falls back to
            the instrument's sampled span. */
        int foldToMusicalRange (int engineCh, int note) const noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return note;

            int lo = musicalLo[(size_t) engineCh];
            int hi = musicalHi[(size_t) engineCh];
            if (lo < 0 || hi < lo) return foldToInstrumentRange (engineCh, note);

            // A window narrower than an octave can't hold every pitch class, so
            // the fold wouldn't converge — widen it before folding.
            if (hi - lo < 11)
            {
                hi = juce::jmin (127, lo + 11);
                lo = juce::jmax (0,   hi - 11);
            }

            while (note > hi) note -= 12;
            while (note < lo) note += 12;
            return juce::jlimit (0, 127, note);
        }

        /** The final sounding key for a MegaVoice articulation: snapped onto the
            held chord (so it can never clash — the snap moves it at most 6
            semitones and preserves its pitch class through the fold), then folded
            into the part's own musical register (so it can never sit octaves above
            the music).  See the block comment above. */
        int megaVoiceFallbackNote (int engineCh, int note, const Chord& chord) const noexcept
        {
            if (kMegaVoiceArticulationsSnap)
                note = NoteTransposer::snapToChordTone (note, chord);
            return foldToMusicalRange (engineCh, note);
        }

        /** Style CC 11 value -> linear expression multiplier.
            Applied LINEARLY, deliberately.  GM specifies the same square curve
            for CC 11 as for CC 7, but here it multiplies a fader that has ALREADY
            been through that curve, and squaring twice over-attenuates: on
            80sDisco the PAD (CC 7 52, CC 11 86) lost 3.4 dB it shouldn't have,
            on top of the fader's own correct drop.  Linear keeps expression
            doing its real job — the composer's ride shape — without re-applying
            a law the fader already carries.
            A "not set" value (-1, the StyleVoiceSetup default) returns unity, so
            a channel whose setup never mentions CC 11 is unaffected.  No
            per-slot trims here: those belong to the mix level (the fader), and
            applying them twice would double-trim. */
        static float styleExpressionToGain (int styleExpression) noexcept
        {
            if (styleExpression < 0) return 1.0f;              // not set -> unity
            return (float) juce::jlimit (0, 127, styleExpression) / 127.0f;
        }

        /** Style CC 7 -> FADER VALUE (normalised, 1.0 = 127 = unity).
            The composer's number, UNCHANGED — this is what the mixer shows and
            what the fader scale documents ("the style's raw 0..127 volume IS
            the fader value").  The GM law does NOT belong here: baking it into
            the fader made the slider read the AMPLITUDE instead of the CC 7
            value, so an oriental style declaring VOL 45 showed 22.  The law
            lives in styleCc7ToAutoLevel below. */
        // The mixer scale is 0..254 (gain = value / 127, so 127 = unity and 254
        // = x2).  A style's own CC 7 is 7-bit and can never exceed 127, but the
        // EFFECTIVE value we park on the fader can — the BASS doubling below is
        // exactly that case — so the clamp is the fader's real ceiling, not 127.
        // Clamping at 127 here would have silently capped the bass boost for any
        // style whose bass sits above CC 7 63.
        static constexpr int kFaderValueMax = 254;

        // What a part means when its SInt carries no CC 7 at all.  The MIDI /
        // GM default for channel volume is 100 — NOT 127.  Treating "absent" as
        // full scale is what let an un-volumed part load louder than anything
        // the style actually asked for.
        static constexpr int kDefaultStyleCc7 = 100;

        static float styleCc7ToFader (int styleVolume) noexcept
        {
            return (float) juce::jlimit (0, kFaderValueMax, styleVolume) / 127.0f;
        }

        /** Style CC 7 -> the channel's AUTO-LEVEL multiplier: everything that
            shapes the part's gain WITHOUT moving the visible fader.

            Engine gain = fader x expression x autoLevel, so returning (v/127)
            here makes the audible law (v/127)^2 — the GM / Yamaha CC 7 volume
            curve — while the fader still reads v.  The per-slot trims and the
            drums' balance/boost ride along here for the same reason: they are
            calibration, not the composer's stated level, and the user should
            not see the fader move to a number the style never asked for.

                PERC (slot 1)  x0.50   (-6.0 dB — runs hot against the kit)
                BASS (slot 2)  x1.35   (+2.6 dB — runs thin; reference-honed)

            Audibly identical to the previous all-in-the-fader arithmetic; only
            the split between "what you see" and "what you hear" changed. */
        static float styleCc7ToAutoLevel (int engineCh, int styleVolume,
                                          float drumBalance   = 1.0f,
                                          float rhythmCeiling = 1.0f,
                                          bool  synthBass2    = false) noexcept
        {
            // IGNORE CC 7 (per style, from the set).  Everything below this
            // line is derived from the style's stated channel volume — the GM
            // square law, the dynamic drum trims, the lot — so when the style's
            // instrument volumes are being ignored the whole function collapses
            // to unity and the part's level is the user's mixer fader alone.
            if (StyleLevels::get().ignoreCc7())
            {
                juce::ignoreUnused (engineCh, styleVolume, drumBalance,
                                    rhythmCeiling, synthBass2);
                return 1.0f;
            }

            float level = styleCc7ToFader (styleVolume);      // -> GM square law

            //------------------------------------------------------------------
            // ONLY THE DYNAMIC TRIMS SURVIVE HERE.
            //
            // The fixed per-ROLE multipliers are gone — BASS x4, Synth Bass 2
            // /10, PERC x0.50 and the (already inert) DRUMS boost.  Every one of
            // them was a guess at how loud one KIND of sound should be, applied
            // to whatever instrument a style happened to put on that slot, and
            // invisible to whoever was listening.  The per-sound GAIN trim and
            // base unity do that job per INSTRUMENT now, on screen and saved
            // with the sound, so the same guess made twice can only fight
            // itself.
            //
            // drumBalance and rhythmCeiling stay: they are not levels for a
            // sound, they are MEASUREMENTS of the loaded kit's density made per
            // style, and no preset can carry them because they depend on what
            // the style plays rather than on what the kit is.
            //------------------------------------------------------------------
            if (engineCh == kPercEngineSlot || engineCh == kDrumsEngineSlot)
                level *= rhythmCeiling;

            if (engineCh == kDrumsEngineSlot)
                level *= drumBalance;

            juce::ignoreUnused (synthBass2);

            return juce::jlimit (0.0f, kAutoLevelMax, level);
        }

        //======================================================================
        // Per-style DRUMS auto-balance factor.  Message thread, once per style
        // load (applyVoiceSetup); the audio thread only ever reads the cached
        // result.  Returns a multiplier for the DRUMS fader — see the calibration
        // constants above.
        //
        //   1. Bin every DRUMS note-on in the Main sections by tick, summing its
        //      velocity-gain (vel/127) with any others on the same tick.  This is
        //      the instantaneous drum-bus amplitude the mix will actually see.
        //   2. Take the 95th percentile of those per-tick sums (not the max — one
        //      freak stack shouldn't set the level).
        //   3. Dead-band: inside [Lo, Hi] return 1.0 (leave ordinary styles as the
        //      composer set them).  Above Hi, scale down toward Hi; below Lo,
        //      scale up toward Lo.  Clamp to [MinFactor, MaxFactor].
        //
        // DRUMS only: the DRUMS part is fed by source ch10 -> engine slot 0 (see
        // mapSourceToEngineChannel).  PERC is not measured at all — which used
        // to be defensible because PERC had its own fixed trim, and no longer
        // is, because that trim is gone.  Worth knowing if this is ever
        // re-enabled: converted styles routinely put the MAIN kit on PERC (one
        // Ketron file measured 598 of its 636 kit hits there), so a DRUMS-only
        // measurement can miss the loud slot entirely.
        //======================================================================
        /** Rhythm-to-melodic ceiling factor — see the constants above.  Runs at
            style load on the message thread, after computeDrumBalanceFactor so
            the density balance is already folded into the drums' weight. */
        //======================================================================
        // THE TWO AUTOMATIC DRUM FACTORS ARE RETIRED.
        //
        // computeDrumBalanceFactor and computeRhythmCeilingFactor still exist —
        // they encode real measurements about how a style's kit sits against its
        // melodic parts, and that knowledge is worth keeping — but nothing
        // applies them any more.  Both wrappers return unity.
        //
        // They went for the same reason the role percentages did: with eight
        // mixer faders in the set and a sound editor per slot, the user sets the
        // kit's level directly, and an automatic trim riding underneath a fader
        // someone deliberately moved argues with them rather than helping.
        //
        // Kept as functions rather than deleted so the members and the LEVELS
        // diagnostic line still read, and so restoring either is one line.
        //======================================================================
        static float gatedDrumBalanceFactor (const StyleData& style)
        {
            juce::ignoreUnused (style);
            return 1.0f;
        }

        float gatedRhythmCeilingFactor (const StyleData& style, float drumBalance)
        {
            juce::ignoreUnused (style, drumBalance);
            return 1.0f;
        }

        /** Non-static since it now consults the engine for per-sound calibration:
            the melodic reference it measures the kit against is exactly what the
            user's gain trims move. */
        float computeRhythmCeilingFactor (const StyleData& style, float drumBalance)
        {
            // 1) Each slot's linear gain from the manifest, mirroring
            //    styleCc7ToAutoLevel's trims so the weights match what will
            //    actually be heard.
            std::array<float, kNumUserStyleSlots> g {};
            for (int srcCh = 0; srcCh < (int) style.voices.size(); ++srcCh)
            {
                const auto& v = style.voices[(size_t) srcCh];
                if (! v.isUsed() || v.volume < 0) continue;
                const int dst = resolveDestChannel (style, srcCh);
                if (dst < 0 || dst >= kNumUserStyleSlots) continue;
                if (g[(size_t) dst] > 0.0f) continue;          // first writer wins
                const float u = (float) juce::jlimit (0, 127, v.volume) / 127.0f;
                float gain = u * u;
                // Mirrors styleCc7ToAutoLevel, which now applies only the two
                // DYNAMIC rhythm trims — the fixed role multipliers are gone.
                if (dst == kDrumsEngineSlot) gain *= drumBalance;

                // Melodic destinations carry the user's calibration.  The kit
                // slots deliberately do NOT: this factor exists to cut the KIT
                // against the rest, and folding the kit's own trim into both
                // sides of that comparison would cancel the very thing it
                // measures.
                if (dst != kDrumsEngineSlot && dst != kPercEngineSlot)
                {
                    const char* vName = casmVoiceNameFor (style, srcCh, dst);
                    const int   raw   = bankMap.resolve (v.bankMsb, v.bankLsb,
                                                         v.program, vName);
                    const int   flag  = (dst == kBassEngineSlot)
                            ? BankProgramMap::correctFlagForBassRole (raw, vName)
                            : raw;
                    gain *= engine.presetGainLinearFor (dst, flag);
                }

                g[(size_t) dst] = gain;
            }

            // 2) Per-tick amplitude, accumulated PER SOURCE.  Several sources
            //    can feed one destination as chord-quality variants and only
            //    ONE of them ever fires, so they are collapsed by MAX below —
            //    summing them would over-count a multi-take style (on the Korg
            //    reference it inflates the kit's measured density from 3.07 to
            //    5.35, i.e. 4.8 dB of phantom level).
            std::map<int, std::map<int, float>> perTickSrc;   // tick -> src -> amp
            for (const auto& sec : style.sections)
            {
                if (! sec.present) continue;
                if (sec.id < StyleSection::MainA || sec.id > StyleSection::MainD) continue;

                for (const auto idx : sec.eventIdx)
                {
                    if (idx >= style.events.size()) continue;
                    const auto& ev = style.events[idx];
                    if ((ev.status & 0xF0) != 0x90 || ev.data2 == 0) continue;

                    const int srcCh = (int) (ev.channel & 0x0F);
                    const CasmEntry* c = sec.findCasm ((uint8_t) srcCh);
                    if (c == nullptr && (srcCh < 8 || styleHasCasmStatic (style))) continue;

                    const int dst = mapSourceToEngineChannel (c != nullptr ? (int) c->dstChannel
                                                                           : srcCh);
                    if (dst < 0 || dst >= kNumUserStyleSlots) continue;

                    const float uv = (float) ev.data2 / 127.0f;
                    const bool  rhythm = (dst == kDrumsEngineSlot || dst == kPercEngineSlot);
                    const float vg = rhythm ? uv : uv * (1.4f - 0.7f * uv);
                    perTickSrc[ev.tick][srcCh] += g[(size_t) dst] * vg;
                }
            }

            // 3) Collapse variants by MAX per slot, then split rhythm / melodic.
            std::vector<float> rh, me;
            rh.reserve (perTickSrc.size());
            me.reserve (perTickSrc.size());
            for (const auto& tk : perTickSrc)
            {
                std::array<float, kNumUserStyleSlots> best {};
                for (const auto& kv : tk.second)
                {
                    const int dst = resolveDestChannel (style, kv.first);
                    if (dst < 0 || dst >= kNumUserStyleSlots) continue;
                    best[(size_t) dst] = juce::jmax (best[(size_t) dst], kv.second);
                }
                float r = 0.0f, m = 0.0f;
                for (int s = 0; s < kNumUserStyleSlots; ++s)
                    ((s == kDrumsEngineSlot || s == kPercEngineSlot) ? r : m) += best[(size_t) s];
                if (r > 0.0f) rh.push_back (r);
                if (m > 0.0f) me.push_back (m);
            }
            if (rh.size() < 8 || me.size() < 8) return 1.0f;   // too little to judge

            auto p95 = [] (std::vector<float>& v)
            {
                std::sort (v.begin(), v.end());
                const float k  = 0.95f * (float) (v.size() - 1);
                const int   lo = (int) std::floor (k);
                const int   hi = juce::jmin (lo + 1, (int) v.size() - 1);
                return v[(size_t) lo] + (v[(size_t) hi] - v[(size_t) lo]) * (k - (float) lo);
            };
            const float rp = p95 (rh), mp = p95 (me);
            if (rp <= 1.0e-6f || mp <= 1.0e-6f) return 1.0f;

            const float ratio = rp / mp;
            if (ratio <= kRhythmCeilingRatio) return 1.0f;     // already polite — leave alone

            return juce::jmax (kRhythmCeilingMin, kRhythmCeilingRatio / ratio);
        }

        static float computeDrumBalanceFactor (const StyleData& style)
        {
            std::map<int, float> perTick;   // absolute tick -> summed velocity-gain

            for (const auto& sec : style.sections)
            {
                if (! sec.present) continue;
                // Main A-D only (enum MainA..MainD = 0..3) — fills / intros /
                // endings are atypical bursts and would skew the level.
                if (sec.id < StyleSection::MainA || sec.id > StyleSection::MainD) continue;

                for (const auto idx : sec.eventIdx)
                {
                    if (idx >= style.events.size()) continue;
                    const auto& ev = style.events[idx];

                    if ((ev.status & 0xF0) != 0x90 || ev.data2 == 0) continue;   // note-on only

                    const int srcCh = (int) (ev.channel & 0x0F);
                    const CasmEntry* c = sec.findCasm ((uint8_t) srcCh);
                    if (c == nullptr && (srcCh < 8 || styleHasCasmStatic (style))) continue;

                    const int dst = mapSourceToEngineChannel (c != nullptr ? (int) c->dstChannel
                                                                           : srcCh);
                    if (dst != kDrumsEngineSlot) continue;   // DRUMS slot only

                    perTick[ev.tick] += (float) ev.data2 / 127.0f;
                }
            }

            if (perTick.size() < 4) return 1.0f;   // too little data to judge — leave it

            std::vector<float> sums;
            sums.reserve (perTick.size());
            for (const auto& kv : perTick) sums.push_back (kv.second);
            std::sort (sums.begin(), sums.end());

            // 95th percentile (linear interpolation).
            const float k  = 0.95f * (float) (sums.size() - 1);
            const int   lo = (int) std::floor (k);
            const int   hi = juce::jmin (lo + 1, (int) sums.size() - 1);
            const float p95 = sums[(size_t) lo] + (sums[(size_t) hi] - sums[(size_t) lo]) * (k - (float) lo);

            float factor = 1.0f;
            if      (p95 > kDrumBalanceHi) factor = kDrumBalanceHi / p95;   // hot  -> cut
            else if (p95 < kDrumBalanceLo) factor = kDrumBalanceLo / p95;   // cold -> lift

            return juce::jlimit (kDrumBalanceMinFactor, kDrumBalanceMaxFactor, factor);
        }

        // computeDrumBalanceFactor is static, so it can't read the styleHasCasm
        // member — recompute the same test locally over the style it's handed.
        static bool styleHasCasmStatic (const StyleData& style) noexcept
        {
            for (const auto& sec : style.sections)
                if (sec.present && ! sec.casm.empty()) return true;
            return false;
        }

        // Apply a runtime drum Program-Change with the per-block compose
        // budget.  A kit already pooled on the channel is published immediately
        // (pointer swap — the common case, never delayed).  A pool miss (which
        // would decode 128 samples on this thread) is run only if the block's
        // budget allows; otherwise it is parked in pendingDrumPc and retried at
        // the start of a later block, so several un-warmed kits can't all decode
        // in one boundary block.  Audio thread.
        void applyRuntimeDrumPc (int engineCh, int pc)
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots) return;

            // A SAMPLED KIT ON THIS SLOT IS NEVER REPLACED FROM HERE.
            //
            // This path is the AUDIO thread, and loading a sampled kit is file
            // I/O - so the only two things it could do are keep the kit or
            // compose over it.  Composing over it is the worse of the two by a
            // long way: a style re-sends its SInt kit at boundaries, and for
            // bank 126 percBankKitPc answers 0 for every program, so one
            // boundary would turn a Pop Latin or Arabic kit into 000 Standard
            // and it would never come back.
            //
            // The cost, stated plainly: a style that genuinely swaps to a
            // DIFFERENT kit mid-song keeps the sampled one it started with.  That
            // is still better than what the composed answer would give it, which
            // is Standard either way.
            if (engine.isFullKitOnChannel (engineCh)) return;

            if (engine.isChannelDrumKitPooled (engineCh, pc))
            {
                engine.programChangeDrum (engineCh, pc);   // pooled -> pointer swap
                pendingDrumPc[(size_t) engineCh] = -1;      // supersedes any parked miss
                return;
            }

            if (drumComposeBudget > 0)
            {
                --drumComposeBudget;
                engine.programChangeDrum (engineCh, pc);   // composes + pools + swaps
                pendingDrumPc[(size_t) engineCh] = -1;
            }
            else
            {
                pendingDrumPc[(size_t) engineCh] = pc;      // defer to a later block
            }
        }

        void dispatchOne (const StyleEvent& ev,
                          const CasmEntry*  casm,
                          const Chord&      chord)
        {
            const uint8_t cmd = ev.status & 0xF0;

            // Channels 0..7 come in two very different flavours:
            //
            //   (a) Vestigial SInt setup MIRROR — duplicate program changes,
            //       expression and other automation with NO CASM rule.  These
            //       fold onto engine slots 0..7 (the same slots the real parts
            //       use) and, if dispatched, hijack the real parts' instrument
            //       and fight their volume.  They must be dropped.
            //
            //   (b) Real chord-quality SOURCE VARIANTS — multi-record styles
            //       (most modern ones) record each harmony part several times,
            //       once per chord quality (Maj7 / Dom7 / MinMaj7 / ...), place
            //       those takes on channels 0..7, and route them to the part's
            //       destination (8..15) via CASM.  These carry a CASM rule and
            //       MUST be dispatched — the multi-source winner gate below
            //       then picks the right take for the held chord.
            //       Dropping them silences every routed harmony part and kills
            //       chord switching on the whole style.
            //
            // The CASM rule is the discriminator: a low channel with no rule is
            // the mirror (drop it); a low channel WITH a rule is a real routed
            // variant (let it through).
            // Unmapped low channels (0..7) are the vestigial SInt mirror -- always
            // junk.  When the style declares a CASM, an unmapped channel >= 8 is a
            // phantom part too (leftover MIDI the CASM never lists) -- drop it so
            // its notes can't pile extra voices / CPU onto a real engine slot.
            // With no CASM at all, keep the fallback so every channel still plays.
            if (casm == nullptr && ((int) (ev.channel & 0x0F) < 8 || styleHasCasm))
            {
                if (cmd == 0x90 && ev.data2 > 0)
                    traceNote (ev, casm, chord, TR_DROP_PHANTOM, -1, -1);
                return;
            }

            //------------------------------------------------------------------
            // A NOTE-OFF MUST NEVER BE GATED BY THE VARIANT SELECTOR.
            //
            // The gate below decides which SOURCE TAKE is right for the chord
            // being held RIGHT NOW.  That question is meaningless for a
            // note-off: the note it releases was started earlier, under
            // whatever chord was held THEN, by whichever take won at THAT
            // moment.  Change chord mid-phrase and the winner changes with it —
            // so the take that started the note is now a loser, its note-off is
            // dropped by the gate, and the note is never released.
            //
            // 70sDisco1_T161 is the case that exposed it: three takes feed the
            // bass destination (ch1 "Bass C", ch2 "Bass B", ch11 "Bass A"), and
            // the trace shows the winner handing over from ch11 to ch2 the
            // instant the chord moves from C major to A minor.  Every handover
            // stranded a note.
            //
            // On the BASS that is as bad as it gets.  The slot is MONO, so the
            // stranded note keeps its entry in Channel::heldNotes, monoHoldStolen
            // then falls back to that stale pitch on later releases, and the
            // orphaned voice sustains at 1.0 — bass sustain — fighting every
            // new note through the glide / crossfade path.  Fast passages pile
            // up more strandings, which is why it got worse the busier the line.
            //
            // Handled here, ABOVE the gate, so it cannot be filtered.  Safe for
            // an unmatched off: dispatchNoteOff's findActiveNote returns -1 and
            // the fallback release lands on a channel with nothing to release.
            // The note-off arms still inside the switch below are now a
            // redundant guard rather than the live path.
            //------------------------------------------------------------------
            if (cmd == 0x80 || (cmd == 0x90 && ev.data2 == 0))
            {
                const int offRawDst = (casm != nullptr) ? (int) casm->dstChannel
                                                        : (int) ev.channel;
                dispatchNoteOff (mapSourceToEngineChannel (offRawDst),
                                 (int) ev.data1, casm, chord);
                return;
            }

            // Dispatch gate — UNIFIED multi-source variant selector.
            //
            // A destination fed by more than one source recording (e.g. a Maj7
            // take AND a dedicated minor take routed to the same slot in an
            // intro / ending) must fire the RIGHT take for the held chord, or
            // the wrong quality plays (a major recording root-shifted over a
            // minor chord sounds major).  The old strict path resolved this by
            // pruning to one (major-family) source, which produced exactly that
            // wrong-quality artifact on minor chords; the selector below is the
            // single path now.
            //
            // Each CASM entry carries an 8-bit `isWinnerForBucket` mask and a
            // 34-entry `firesForFingeredQuality` table, both filled at style
            // load from the file's ORIGINAL per-source chord tag + chord-mute
            // bitmap.  FINGERED play honours the chord-mute bitmap (matching
            // voicings for the held quality layer; a gap falls back to the
            // broadest comp, never silent); SINGLE-FINGER reduces to the
            // major/minor bucket winner.  Single-source destinations trivially
            // win every bucket, so they behave exactly as before.
            if (casm != nullptr)
            {
                if (dispatchFingeredMode)
                {
                    // FINGERED: honour the file's chord-mute bitmap so the
                    // matching voicing(s) for the held quality layer; an
                    // unknown quality falls back to the bucket winner.
                    const int q = (int) chord.quality;
                    const bool fires = (q >= 0 && q < 34)
                        ? casm->firesForFingeredQuality[(size_t) q]
                        : casm->isWinnerForBucket[
                              (size_t) NoteTransposer::betelgeuseQualityBucket (chord.quality)];
                    if (! fires)
                    {
                        if (cmd == 0x90 && ev.data2 > 0)
                            traceNote (ev, casm, chord, TR_DROP_VARIANT, -1, -1);
                        return;
                    }
                }
                else
                {
                    // SINGLE-FINGER: major/minor reduction via the buckets.
                    const int bucket
                        = NoteTransposer::betelgeuseQualityBucket (chord.quality);
                    if (! casm->isWinnerForBucket[(size_t) bucket])
                    {
                        if (cmd == 0x90 && ev.data2 > 0)
                            traceNote (ev, casm, chord, TR_DROP_VARIANT, -1, -1);
                        return;
                    }
                }
            }

            // Pick the raw destination channel from CASM if present, else
            // mirror the source channel, then translate onto the user slot.
            const int rawDst = (casm != nullptr)
                ? (int) casm->dstChannel
                : (int) ev.channel;
            const int dstEngineCh = mapSourceToEngineChannel (rawDst);

            const bool muted = isChannelMuted (dstEngineCh);

            switch (cmd)
            {
                case 0x90:  // Note On (vel 0 is a Note Off)
                {
                    if (ev.data2 == 0)
                    {
                        // Implicit note-off — pass through even when muted.
                        dispatchNoteOff (dstEngineCh, (int) ev.data1, casm, chord);
                        return;
                    }
                    if (muted)
                    {
                        traceNote (ev, casm, chord, TR_DROP_MUTED, dstEngineCh, -1);
                        return;       // drop note-on on muted channel
                    }

                    // MegaVoice articulation key (>= C7 on an MSB-8 channel): a
                    // TRIGGER, not a pitch.  No longer dropped — megaVoiceFallbackNote()
                    // snaps it onto the held chord and folds it into the loaded
                    // instrument's own mapped register, so the SELECTED voice plays
                    // it as an ordinary note at its natural octave (see the block
                    // comment on isMegaVoiceArticulation).  The fold is applied to
                    // the FINAL pitch, and trackNoteOn records that exact key — so
                    // the authored note-off releases what actually sounded and the
                    // articulation can never hang.
                    const bool megaArt =
                        isMegaVoiceArticulation (dstEngineCh, (int) ev.data1);

                    // Velocity twin of the above: on a MegaVoice slot the upper
                    // velocity band is an articulation SELECTOR, so remap it
                    // into the musical range before it reaches an ordinary
                    // voice.  See megaVoiceMusicalVelocity.
                    const int outVel = isMegaVoiceChannel (dstEngineCh)
                                         ? megaVoiceMusicalVelocity ((int) ev.data2)
                                         : (int) ev.data2;

                    if (megaArt && ! kMegaVoiceArticulationsPlay)
                    {
                        traceNote (ev, casm, chord, TR_DROP_MUTED, dstEngineCh, -1);
                        return;       // switch is off — old behaviour: silence it
                    }

                    if (casm != nullptr)
                    {
                        const auto r = NoteTransposer::applyAuto ((int) ev.data1, chord, *casm);
                        if (r.shouldPlay)
                        {
                            int outNote = foldNote (dstEngineCh, r.destNote);
                            if (megaArt)
                                outNote = megaVoiceFallbackNote (dstEngineCh, outNote, chord);
                            engine.noteOn (dstEngineCh, outNote, outVel);
                            trackNoteOn (dstEngineCh, (int) ev.data1, outNote,
                                         outVel, casm);
                            traceNote (ev, casm, chord, TR_PLAY, dstEngineCh, outNote);
                        }
                        else
                            traceNote (ev, casm, chord, TR_DROP_TRANSPOSE, dstEngineCh, -1);
                    }
                    else
                    {
                        int outNote = foldNote (dstEngineCh, (int) ev.data1);
                        if (megaArt)
                            outNote = megaVoiceFallbackNote (dstEngineCh, outNote, chord);
                        engine.noteOn (dstEngineCh, outNote, outVel);
                        trackNoteOn (dstEngineCh, (int) ev.data1, outNote,
                                     outVel, nullptr);
                        traceNote (ev, nullptr, chord, TR_PLAY, dstEngineCh, outNote);
                    }
                    return;
                }

                case 0x80:  // Note Off — always process
                {
                    dispatchNoteOff (dstEngineCh, (int) ev.data1, casm, chord);
                    return;
                }

                case 0xB0:  // Control Change
                {
                    if (muted) return;
                    const int cc  = (int) ev.data1;
                    const int val = (int) ev.data2;

                    // Filter style control CCs EXCEPT bank-select (needed by the
                    // next PC), Main Volume (CC 7) and Expression (CC 11).
                    // CC 7 is honoured because voice-switching styles pair every
                    // per-section program change with a balancing CC 7; dropping
                    // it leaves the new voice at the wrong level (sharp jumps).
                    // CC 11 is honoured because it is the composer's performance
                    // shape — per-section swells and ending fade-outs — and it
                    // rides the engine's own expression multiplier, so it never
                    // fights the user's fader.  Pan (CC 10) and the send CCs
                    // stay filtered.
                    switch (cc)
                    {
                        case 0:     // Bank Select MSB — caches for next PC
                            runningBankMsb[(size_t) dstEngineCh] = val;
                            return;

                        case 32:    // Bank Select LSB — caches for next PC
                            runningBankLsb[(size_t) dstEngineCh] = val;
                            return;

                        case 7:     // Main Volume — per-section level ride
                        {
                            if (dstEngineCh < 0 || dstEngineCh >= kNumUserStyleSlots)
                                return;
                            // PURE CC 7 → FADER, 1:1 (same rule as the setup):
                            // the ride value lands as the fader value, gain =
                            // val/127.  Bounded by MIDI itself — a style can
                            // reach 127 (unity) at most.  Identical repeats are
                            // swallowed by the changed-gate; last writer wins
                            // between a manual fader move and the next ride.
                            if (! styleCCChanged (dstEngineCh, 7, val))
                                return;            // identical repeat — swallowed

                            // PERC is trimmed here too, not just at setup: a style
                            // that re-sends CC 7 every section would otherwise walk
                            // the level straight back up.  The changed-gate above
                            // still compares the RAW style value, so the trim can
                            // never make two different rides look identical.
                            //
                            // DRUMS gets the same treatment as the setup — the
                            // auto-balance factor and the boost constant — so a
                            // mid-song ride lands on exactly the same scale as
                            // the load-time value instead of silently reverting
                            // the drums to the raw 1:1 level.  drumBalanceFactor
                            // is written once per style load (message thread) and
                            // only read here, as its comment already provides for.
                            //
                            // Split exactly like the setup: the composer's number
                            // to the FADER (so the visible slider tracks the ride
                            // in the style's own units), the GM law and the trims
                            // to the AUTO-LEVEL.
                            // Brass Section is trimmed on the ride too, or a
                            // style that re-sends CC 7 each section would walk
                            // the halved fader straight back up.
                            engine.setChannelVolume    (dstEngineCh,
                                styleCc7ToFader (effectiveStyleCc7 (dstEngineCh, val)));
                            engine.setChannelAutoLevel (dstEngineCh,
                                styleCc7ToAutoLevel (dstEngineCh, val, drumBalanceFactor,
                                                     rhythmCeilingFactor,
                                                     bassIsSynthBass2 (dstEngineCh)));
                            return;
                        }

                        case 11:    // Expression — the composer's performance shape
                        {
                            if (dstEngineCh < 0 || dstEngineCh >= kNumUserStyleSlots)
                                return;
                            // Rides land on the engine's expression multiplier,
                            // NOT the fader: the fader stays the user's control
                            // (and the mix level the style set with CC 7), while
                            // this carries the per-section swells and the ending
                            // fade-outs the style authors.  Same GM square law
                            // as the setup value.
                            //
                            // NOT changed-gated: an expression fade is a dense
                            // ramp of single-step values (an ending walks 126
                            // down to 60 one unit at a time), so every value is
                            // already distinct and the write is a single atomic
                            // store — gating would only cost a lookup.
                            engine.setChannelExpression (dstEngineCh,
                                                         styleExpressionToGain (val));
                            return;
                        }

                        default:
                            return;     // filtered — other style control CCs ignored
                    }
                }

                case 0xC0:  // Program Change — bank-aware via running CC 0 / 32
                {
                    if (muted) return;

                    // Precedence: a pinned slot (reference-voice pill) wins, then
                    // a Fixed style freezes the instrument; otherwise honour the
                    // style's program change.
                    if (slotPcLocked  [(size_t) dstEngineCh].load()) return;
                    if (slotPcIgnored [(size_t) dstEngineCh].load()) return;   // user toggle
                    if (! gmControlled.load())                    return;

                    const int msb = runningBankMsb[(size_t) dstEngineCh];
                    const int lsb = runningBankLsb[(size_t) dstEngineCh];
                    const int pc  = (int) ev.data1;

                    // PC redundancy cache — drum kit reload + preset lookup are
                    // the two heaviest stages of section transitions.  When the
                    // (msb, lsb, pc) triple matches what's already loaded on
                    // this channel, the new PC is a no-op: skip the work.
                    // Section transitions often re-stamp the same instrument
                    // setup the channel already had; the cache turns those into
                    // a 3-int compare instead of a drum-kit rebuild.
                    //
                    // Cache primed by applyVoiceSetup at style-load so even the
                    // first section's PCs hit (if the bar-1 SInt setup matched
                    // what the style sent at Main A start, which is typical).
                    if (lastAppliedMsb[(size_t) dstEngineCh] == msb
                        && lastAppliedLsb[(size_t) dstEngineCh] == lsb
                        && lastAppliedPc [(size_t) dstEngineCh] == pc)
                        return;

                    lastAppliedMsb[(size_t) dstEngineCh] = msb;
                    lastAppliedLsb[(size_t) dstEngineCh] = lsb;
                    lastAppliedPc [(size_t) dstEngineCh] = pc;

                    // GUARD: a rhythm-locked slot (a drum part per its SInt /
                    // manifest) must never be demoted to a melodic voice.  Honour
                    // a real drum-kit change (bank 127); ignore any non-127 bank
                    // (e.g. SFX bank 126) so a mis-authored style can't swap the
                    // drums for a fretless bass mid-song.
                    if (rhythmLocked[(size_t) dstEngineCh])
                    {
                        // Honour a real kit change: the drum bank (127) and the
                        // percussion/SFX bank (126) — a style legitimately swaps
                        // its percussion kit per section (this is how oriental
                        // styles work, re-sending their kit at every boundary).
                        // Everything else is still ignored, so a mis-authored
                        // style can't demote the drums to a fretless bass.
                        if (msb == 127)
                            applyRuntimeDrumPc (dstEngineCh, pc);
                        else if (msb == 126)
                            applyRuntimeDrumPc (dstEngineCh, percBankKitPc (pc));
                        return;
                    }

                    if (msb == 127)
                    {
                        // Drum bank: swap the kit by PC.
                        applyRuntimeDrumPc (dstEngineCh, pc);
                        return;
                    }
                    const char* vName   = (casm != nullptr) ? casm->voiceName : nullptr;
                    const int   rawIdx  = bankMap.resolve (msb, lsb, pc, vName);
                    // Same bass-role correction as the setup and the preload —
                    // all three must agree or a mid-song PC would swap the slot
                    // back to the uncorrected (lead / piano) voice, and the
                    // pooled lookup would miss the flag the preload warmed.
                    const int   presetIdx = (dstEngineCh == kBassEngineSlot)
                                              ? BankProgramMap::correctFlagForBassRole (rawIdx, vName)
                                              : rawIdx;
                    engine.selectChannelPooledPreset (dstEngineCh, presetIdx);
                    slotSoundingFlag[(size_t) dstEngineCh] = presetIdx;
                    return;
                }

                case 0xE0:  // Pitch Bend — dropped while muted
                {
                    if (muted) return;
                    const int lsb = (int) ev.data1;
                    const int msb = (int) ev.data2;
                    const int bend14 = (msb << 7) | lsb;
                    if (dstEngineCh >= 0 && dstEngineCh < kNumUserStyleSlots)
                    {
                        if (lastStylePB[(size_t) dstEngineCh] == bend14)
                            return;                    // identical repeat — swallowed
                        lastStylePB[(size_t) dstEngineCh] = bend14;
                    }
                    const float n = ((float) bend14 - 8192.0f) / 8192.0f;
                    engine.pitchBend (dstEngineCh, juce::jlimit (-1.0f, 1.0f, n));
                    return;
                }

                default:    // Channel Aftertouch (0xD0), etc. — ignored
                    return;
            }
        }

        SamplePlayerEngine& engine;
        BankProgramMap      bankMap;
        std::atomic<uint16_t> muteMask { 0 };

        // Chord-recognition mode latched at the top of each dispatchBlock from
        // the caller's ChordZoneTracker mode.  true = FINGERED (honour each
        // variant's chord-mute bitmap via firesForFingeredQuality); false =
        // SINGLE-FINGER (major/minor reduction via isWinnerForBucket).  Written
        // and read only on the audio thread, so a plain bool is sufficient.
        bool                dispatchFingeredMode = false;


        // ── "Changed" gate for style CCs that reach the engine ──────────────
        // Last value actually sent, per (style slot, CC#).  A style CC is
        // forwarded only when its value DIFFERS from the last one sent —
        // Yamaha sections re-send their CC rides on every loop / section
        // re-entry, and without this gate those identical repeats keep
        // re-writing the target (e.g. stomping a manual fader move with the
        // same CC 7 every cycle).  Once a value passed, the same value is
        // swallowed until a different one arrives.  -1 = nothing sent yet.
        std::array<std::array<int, 128>, kNumUserStyleSlots> lastStyleCC;

        // Same gate for pitch bend (absolute 14-bit state; styles re-stamp the
        // 8192 centre reset at every section start / loop re-entry).
        std::array<int, kNumUserStyleSlots> lastStylePB;

        bool styleCCChanged (int engineCh, int cc, int val) noexcept
        {
            if (engineCh < 0 || engineCh >= kNumUserStyleSlots
                || cc < 0 || cc > 127)
                return true;                       // out of scope — never gate
            int& last = lastStyleCC[(size_t) engineCh][(size_t) cc];
            if (last == val) return false;         // identical repeat — gate it
            last = val;
            return true;
        }

        // Per-style-slot "allowed notes" window.  Read on the audio thread by
        // foldNote; written from the per-instrument sound editor via
        // setNoteRange.  Bass (slot 2) defaults ON at A#1 (34) → A2 (45); the
        // other slots default OFF (lo/hi seeded to the same window so a freshly
        // toggled slot starts from a valid 12-note range).
        std::array<std::atomic<bool>, kNumUserStyleSlots> noteRangeOn {};
        std::array<std::atomic<int>,  kNumUserStyleSlots> noteRangeLo {};
        std::array<std::atomic<int>,  kNumUserStyleSlots> noteRangeHi {};

        // PC redundancy cache — last (msb, lsb, pc) actually applied per
        // engine channel.  When a runtime PC event re-stamps the same triple,
        // dispatchOne short-circuits before calling programChangeDrum /
        // selectChannelPooledPreset, both of which can do drum-kit / preset
        // lookups that take 50-200 µs each.  Section transitions in a
        // typical SFF file emit 5-15 PCs concentrated in the first audio
        // block of the new section; without the cache, all of them did real
        // work on every variation press, spiking the audio thread.  Seed
        // values of -2 are sentinel "never set" (-1 is already a valid
        // bank-not-set value for runningBankMsb/Lsb, so we use a different
        // sentinel to ensure the first PC after style-load always misses
        // and runs).
        // Running Bank Select state per style slot (CC 0 / CC 32 cached until
        // the next Program Change consumes them).  -1 = no bank byte seen yet
        // since load, so the PC resolves with the setup bank.
        std::array<int, kNumUserStyleSlots> runningBankMsb {{ -1, -1, -1, -1, -1, -1, -1, -1 }};
        std::array<int, kNumUserStyleSlots> runningBankLsb {{ -1, -1, -1, -1, -1, -1, -1, -1 }};

        // SInt setup voice per slot — the BASELINE every boundary re-asserts.
        // A section that revoices a part (e.g. an intro putting a kit on a
        // phrase slot) owns it only within itself: at each loop wrap / section
        // change the slot returns to its setup voice, and the incoming
        // section's own bank/PC events (dispatched right after the boundary
        // marker) re-apply its revoicing.  -1 = no setup voice captured.
        std::array<int, kNumUserStyleSlots> setupBankMsb {{ -1, -1, -1, -1, -1, -1, -1, -1 }};
        std::array<int, kNumUserStyleSlots> setupBankLsb {{ -1, -1, -1, -1, -1, -1, -1, -1 }};
        std::array<int, kNumUserStyleSlots> setupPc      {{ -1, -1, -1, -1, -1, -1, -1, -1 }};
        // Setup CC 11 per slot (-1 = the style never set one).  Expression is
        // STATEFUL — an ending walks it down to a fade-out and no event ever
        // brings it back — so allNotesOff() restores from here on every stop /
        // style switch.  Without that, one ending would leave the part quiet
        // for the rest of the session.
        std::array<int, kNumUserStyleSlots> setupExpression {{ -1, -1, -1, -1, -1, -1, -1, -1 }};

        std::array<int, kNumUserStyleSlots> lastAppliedMsb {{ -2, -2, -2, -2, -2, -2, -2, -2 }};
        std::array<int, kNumUserStyleSlots> lastAppliedLsb {{ -2, -2, -2, -2, -2, -2, -2, -2 }};
        std::array<int, kNumUserStyleSlots> lastAppliedPc  {{ -2, -2, -2, -2, -2, -2, -2, -2 }};

        // Per-block expensive-load budget (the "spread the swaps" insurance).
        // Pooled kit swaps (the common case after pre-warming) are pointer
        // swaps and ALWAYS apply immediately — never delayed, so the part never
        // plays the wrong kit.  Only a pool MISS (a kit the style manifest
        // didn't pre-warm, which decodes on the audio thread) is rationed: at
        // most kMaxDrumComposesPerBlock per block, with any extra deferred to a
        // later block via pendingDrumPc.  This keeps several un-warmed kits from
        // all decoding in the single block at a section boundary.  With the
        // manifest pre-warm in place this budget is essentially never hit.
        static constexpr int kMaxDrumComposesPerBlock = 1;
        int drumComposeBudget = 0;   // reset each dispatchBlock
        std::array<int, kNumUserStyleSlots> pendingDrumPc {{ -1, -1, -1, -1, -1, -1, -1, -1 }};

        // Rhythm-role lock — true for any engine slot the style set up as a drum
        // part (setup bank 127, or ANY voice in its manifest uses bank 127).  A
        // locked slot is pinned to the drum-kit path: a later section that stamps
        // a non-127 bank (e.g. SFX bank 126) on it is ignored, so a mis-authored
        // style can never demote the drums to a melodic GM voice (the "drums turn
        // into a roaring fretless bass" failure).
        std::array<bool, kNumUserStyleSlots> rhythmLocked {{ false, false, false, false,
                                                             false, false, false, false }};

        // True when the loaded style declares a CASM.  When set, a source channel
        // with no CASM rule is a phantom part (leftover MIDI the style never lists
        // as a part) and is dropped at dispatch, so a mis-authored style can't
        // inject extra voices / CPU through channels outside its CASM.
        bool styleHasCasm = false;

        // ── Per-slot musical register of the loaded style ────────────────────
        // The lowest / highest AUTHORED note below kMegaVoiceArticulationFloor
        // that dispatches to each style slot; -1 = the slot's part has none.
        // Written once per style load by applyVoiceSetup (message thread), read
        // by foldToMusicalRange (audio thread) — same lifecycle and the same
        // plain-int treatment as runningBankMsb / slotStyleFlag / rhythmLocked.
        std::array<int, kNumUserStyleSlots> musicalLo {};
        std::array<int, kNumUserStyleSlots> musicalHi {};

        // Per-style DRUMS auto-balance multiplier, computed once at style load by
        // computeDrumBalanceFactor and applied to the DRUMS fader in
        // applyVoiceSetup.  1.0 = no correction (the normal case).
        float drumBalanceFactor = 1.0f;
        // Rhythm-to-melodic ceiling — see computeRhythmCeilingFactor.  Written once
        // per style load on the message thread, read by the audio thread exactly
        // like drumBalanceFactor above.
        float rhythmCeilingFactor = 1.0f;

        // Program-change precedence state.  gmControlled is set per style at
        // applyVoiceSetup; slotPcLocked is set by the host when a slot's
        // reference-voice pill is pinned.
        std::atomic<bool> gmControlled { true };
        std::array<std::atomic<bool>, kNumUserStyleSlots> slotPcLocked {};

        // User-owned, set-persisted - see setSlotPcIgnored.  Never cleared by a
        // style load, unlike slotPcLocked directly above it.
        std::array<std::atomic<bool>, kNumUserStyleSlots> slotPcIgnored {};

        // Per-slot instrument substitution (user override of the style's voice).
        // slotSubFlag[ch] >= 0 -> ignore the style's PCs for this slot and play
        // this instrument flag instead (persisted in the set).  slotStyleFlag[ch]
        // remembers the style's own setup flag so a cleared substitution can be
        // restored.  Both are -1 = none.  slotSubFlag is initialised to -1 in the
        // constructor (std::atomic has no brace-initialiser in std::array).
        std::array<std::atomic<int>, kNumUserStyleSlots> slotSubFlag {};

        // Identity of the style applyVoiceSetup last ran on.  Message thread
        // only.  Its whole job is to tell "the same style again" from "a new
        // style", which is what scopes a user substitution to the style it was
        // made on — see the block in applyVoiceSetup.
        juce::String lastAppliedStyleId;
        std::array<int, kNumUserStyleSlots>              slotStyleFlag {{ -1, -1, -1, -1, -1, -1, -1, -1 }};
        // The flag each style slot is CURRENTLY sounding — updated by the setup
        // AND by every runtime program change, so per-instrument calibration
        // (effectiveStyleCc7) tracks a mid-song voice swap.  Deliberately
        // separate from slotStyleFlag, which means "the SInt voice to restore
        // when a substitution is cleared" and must not drift with PCs.
        std::array<int, kNumUserStyleSlots>              slotSoundingFlag {{ -1, -1, -1, -1, -1, -1, -1, -1 }};
        // Each slot's linear gain (fader x auto-level) as of the style's setup —
        // the weight computeStyleMakeupGain gives that slot's notes.
        std::array<float, kNumUserStyleSlots>            slotLoadGain {};

        // ── Active-note tracker (audio thread only) ─────────────────────────
        // Every sounding style note, so a chord change can re-pitch held notes
        // immediately and note-offs can target the actual sounding pitch.
        struct ActiveStyleNote
        {
            int ch = -1, srcNote = -1, destNote = -1, vel = 0;
            const CasmEntry* casm = nullptr;
        };
        static constexpr int kMaxActiveNotes = 256;
        std::array<ActiveStyleNote, kMaxActiveNotes> activeNotes {};
        int   activeNoteCount = 0;

        // MIDI event trace buffer (see public API).  Audio thread appends via
        // traceNote(); the message thread reads it for export when disarmed.
        std::array<MidiTraceRec, (size_t) kTraceCap> traceBuf {};
        std::atomic<size_t> traceWriteIdx { 0 };
        std::atomic<bool>   traceArmed    { false };

        // Append one record if armed.  Lock-free and audio-thread safe: an
        // atomic fetch-add reserves a slot, then a plain POD store fills it.
        // Events past kTraceCap are counted (for the overflow flag) but not
        // stored.  Called only for note-on / note-off events.
        inline void traceNote (const StyleEvent& ev, const CasmEntry* casm,
                               const Chord& chord, uint8_t result,
                               int dstCh, int dstNote) noexcept
        {
            if (! traceArmed.load (std::memory_order_relaxed)) return;
            const size_t i = traceWriteIdx.fetch_add (1, std::memory_order_relaxed);
            if (i >= (size_t) kTraceCap) return;
            MidiTraceRec& r = traceBuf[i];
            r.seq         = (uint32_t) i;
            r.tick        = (uint32_t) ev.tick;
            r.srcCh       = (uint8_t) (ev.channel & 0x0F);
            r.srcData1    = ev.data1;
            r.srcData2    = ev.data2;
            r.dstCh       = (dstCh   < 0) ? (uint8_t) 0xFF : (uint8_t) dstCh;
            r.dstNote     = (dstNote < 0) ? (uint8_t) 0xFF : (uint8_t) dstNote;
            r.result      = result;
            r.chordRoot   = (uint8_t) chord.root;
            r.chordQual   = (uint8_t) chord.quality;
            r.casmMatched = (uint8_t) (casm != nullptr ? 1 : 0);
            r.pad         = 0;
        }
        Chord lastDispatchChord { 0, ChordQuality::Maj };
        bool  haveLastChord = false;

        // Chord-deferral state: held chord queued for application at the
        // next 1/8 boundary.  pendingChord is overwritten on every
        // dispatchBlock call; lastDispatchTick is the sequencer tick at
        // end-of-last-block, used to detect when this block crossed a
        // boundary.  Starting both at "0 / Maj" matches lastDispatchChord's
        // initial state so a fresh session doesn't glitch on first dispatch.
        Chord pendingChord    { 0, ChordQuality::Maj };
        int   lastDispatchTick = 0;

        // ── One-shot CASM-miss diagnostic state (temporary) ─────────────────
        // Set true when a committed chord change occurs; the next dispatchBlock
        // that carries events logs a CASM match/null summary and clears it.
        // Remove together with the diagChordName / diagChannelMask helpers and
        // the logging block in dispatchBlock once chord switching is confirmed.
        bool  diagCasmLogArmed = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StylePlayer)
    };
} // namespace Betel
