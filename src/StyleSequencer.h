#pragma once
//==============================================================================
// StyleSequencer.h
//
// The "band leader". Owns the transport, the current section, the current
// variation, and any pending section change. Each audio block, advances the
// playhead and produces a list of MIDI events to be dispatched by the engine.
//
//   • No engine wiring here — we just produce EmittedStyleEvent records.
//   • No chord transposition here either — each emitted event carries its
//     CASM rule so the next layer (step 3) can do the NTR/NTT math.
//
// Threading model:
//   • Message thread:  setStyle / prepareToPlay / releaseResources
//   • Any thread:      transport + section commands (queued atomically,
//                      applied at the next renderBlock)
//   • Audio thread:    renderBlock
//==============================================================================

#include <JuceHeader.h>
#include "StyleData.h"
#include "JumpsConfig.h"
#include <atomic>
#include <vector>
#include <array>

namespace Betel
{
    enum class StyleVariation : int { A = 0, B = 1, C = 2, D = 3 };

    enum class StyleTransport : int { Stopped, Playing };

    // ─────────────────────────────────────────────────────────────────────────
    // One MIDI event due in the current audio block, with its CASM rule
    // attached for later transposition/routing.
    // ─────────────────────────────────────────────────────────────────────────
    struct EmittedStyleEvent
    {
        int               sampleOffset = 0;  // 0 .. blockSize within this block
        const StyleEvent* event        = nullptr;
        const CasmEntry*  casm         = nullptr;  // null = no CASM for this src ch

        // Boundary marker (event == nullptr).  Emitted by renderBlock at every
        // loop-wrap and section change, at the sample offset of the boundary.
        // StylePlayer releases every still-sounding style note when it sees this
        // — so a note that sustains past a section's phrase end (or across a
        // mid-section fill / variation / break / intro / ending) can never be
        // orphaned into a stuck note.  The load-time seam clamp only covers a
        // self-looping main's end; this covers every reset, at runtime.
        bool              flushNotes   = false;

        /** True when this boundary is a real SECTION CHANGE, false when it is a
            main looping back on itself.
        
            StylePlayer re-asserts every slot's SInt setup voice at a boundary, so
            a section-local revoicing cannot leak into the NEXT section.  On a
            self-loop that is wrong: the section's own mid-phrase program change
            has already fired, and restoring the SInt undoes it — then the PC
            fires again on the next pass.  The result is an instrument flipping
            between the SInt voice and the section's voice once per cycle, which
            is what "it keeps switching between the previous and the new
            instrument" is.  The flush still happens either way; only the voice
            re-assert is gated on this. */
        bool              sectionChanged = false;

        /** SPLIT method: change section WITHOUT damping what is sounding.
        
            The patent this follows makes the point plainly — a chord backing
            note damped halfway through its duration is what sounds wrong about a
            mid-bar switch, not the switch itself.  A real Yamaha answers that by
            holding the melodic parts on the main until the bar line and moving
            only the rhythm; this engine reads ONE section for all parts, so it
            cannot do that literally.  What it can do is let the sustained notes
            ring across the splice instead of cutting them, which removes the
            same artefact for the same reason.
        
            Honest about what it is: an approximation of the patent's rhythm /
            melodic split, not the split itself. */
        bool              keepSounding = false;
    };

    // ─────────────────────────────────────────────────────────────────────────
    class StyleSequencer
    {
    public:
        StyleSequencer();
        ~StyleSequencer() = default;

        // ── Lifecycle (message thread) ────────────────────────────────────────
        void setStyle (const StyleData* style);
        const StyleData* getStyle() const noexcept { return stylePtr.load(); }
        void prepareToPlay (double sampleRate);
        void releaseResources();

        // ── Transport (any thread) ────────────────────────────────────────────
        void start();
        void stop();
        bool isPlaying() const noexcept { return transport.load() == StyleTransport::Playing; }

        // ── Section commands (any thread) ─────────────────────────────────────
        /** Choose the variation to move to.

            viaFill = false (a VAR button): CUT STRAIGHT THERE at the next quant
            point.  Pressing VAR 3 means "be on VAR 3", and routing that through
            VAR 3's fill first made the arrangement arrive somewhere the player
            did not ask for and a bar late.

            viaFill = true (a FILL button): play the destination's fill and land
            on its main - the musical transition, which is now what the FILL
            buttons are FOR and the only thing that reaches it.  Before this the
            two buttons did the same thing whenever the target differed from the
            current variation. */
        void selectVariation (StyleVariation v, bool viaFill = false);
        void triggerIntro    (int idx);   // 0=A, 1=B, 2=C; honored only when stopped
        void triggerEnding   (int idx);   // 0=A, 1=B, 2=C
        void triggerFill();
        void triggerBreak();

        // Re-cue the CURRENT section to tick 0 without changing the selected
        // section/variation or stopping.  Used by the RESTART control.
        void restart() noexcept { pendingRestart.store (true); }

        // ── Transition HOLD (any thread) ──────────────────────────────────────
        // While held, queued user transitions (intro/fill/break/variation
        // change/ending) are not applied at the bar boundary — they stay
        // pending and fire once hold releases.
        //
        // HOLD also freezes the section that is ALREADY RUNNING.  A main keeps
        // looping, as it always did; a fill / intro / break / ending now loops
        // too instead of handing over to its destination at its own end, so it
        // keeps playing its role for as long as HOLD is on.  Releasing HOLD lets
        // the next end-of-section run normally: the transition lands where it
        // was going, and a held ending finally stops the transport.
        //
        // RESTART deliberately bypasses hold.
        void setTransitionHold (bool held) noexcept { transitionHeld.store (held); }
        bool isTransitionHeld() const noexcept      { return transitionHeld.load(); }

        // ── Tempo ─────────────────────────────────────────────────────────────
        void   setHostBPM (double bpm) noexcept { hostBPM.store (bpm); }
        double getHostBPM() const noexcept      { return hostBPM.load(); }

        // ── Jumps (post-transition destinations from the Jumps tab) ──────────
        // Where to land after each transition section (3 intros, 4 fills,
        // break) finishes.  Written on the message thread, read on the audio
        // thread at transition entry / section end.
        void setJumpsConfig (const JumpsConfig& cfg) noexcept
        {
            for (int i = 0; i < kNumJumpSources; ++i)
                jumpDest[(size_t) i].store ((int) cfg.destinations[(size_t) i]);
        }

        // ── Transition quantisation (TRANS selector: "1/2" / "1/4" / "1/8") ──
        // The grid on which queued transitions (fill / break / variation /
        // ending) are allowed to latch.  METER-AWARE: derived from the style's
        // beat (barTicks / timeSigNum), so it always divides the bar a whole
        // number of times in ANY meter — 4/4, 3/4, 6/8, 9/8, 5/4, 7/8.
        //   0 = coarse : two beats, or the whole BAR when the meter has an odd
        //                beat count (two beats cannot divide an odd bar evenly).
        //   1 = one beat.
        //   2 = half a beat (most responsive).
        // In 4/4 these are exactly a 1/2, 1/4 and 1/8 note — which is what the
        // selector labels say.  See renderBlock for the full rationale.
        void setTransitionQuant (int mode) noexcept { transitionQuant.store (mode); }
        int  getTransitionQuant() const noexcept    { return transitionQuant.load(); }

        // ── Fill length (FILL LENGTH selector: "1" / "1/2") ──────────────────
        // 0 = "1"  : play the whole fill, from its start to its end.
        // 1 = "1/2": enter the fill at its MIDWAY point and play only the second
        //            half.  The fill still ends where it always did, so it lands
        //            on exactly the same beat and hands over to the destination
        //            section unchanged — it just starts later, which is the
        //            short, punchy fill an arranger player usually wants.
        // Applies to the four variation fills (Fill In AA/BB/CC/DD).  The BREAK
        // (Fill In BA) is left whole — it is a different musical gesture.
        void setFillLength (int mode) noexcept { fillLength.store (mode); }
        int  getFillLength() const noexcept    { return fillLength.load(); }

        // ── Audio thread ──────────────────────────────────────────────────────
        void renderBlock (int blockSizeSamples,
                          std::vector<EmittedStyleEvent>& outEvents);

        // ── Query (read-only) ─────────────────────────────────────────────────
        StyleSection   getCurrentSection()   const noexcept;
        StyleVariation getCurrentVariation() const noexcept;
        double         getCurrentTickInSection() const noexcept { return localTick.load(); }
        int            getCurrentBarInSection()  const noexcept;  // 1-based
        juce::String   describeState() const;

        // Crash-tab support.  Each audio block the host consumes these to fire
        // a crash on a real section transition and/or every N loop cycles.
        int consumeTransitionEvents() noexcept { return transitionEvents.exchange (0); }

        /** LANDINGS: a transition section handing back to a main.
        
            transitionEvents counts the moment a transition is ENTERED — the
            instant FILL is pressed.  That is the wrong moment for a crash: a
            crash marks the arrival, not the departure.  A fill is a run-up, and
            the cymbal belongs on the downbeat it runs up to.
        
            This counts the other end: the boundary where a fill / intro / break
            finishes and the chosen main takes over. */
        int consumeLandingEvents() noexcept { return landingEvents.exchange (0); }

        /** MAIN ENTRIES: a var 1-4 section just STARTED playing.
        
            Raised whenever enterSection targets a main — a landing out of a
            fill / intro / break, a direct cut between variations, or a start.
            A self-loop does NOT raise one: looping is not starting.
        
            This is the auto-crash's zero point.  The counter is measured from
            the moment a variation begins, so a fill in the middle does not add
            to the count — it ends the old count and begins a new one. */
        int consumeMainEntryEvents() noexcept { return mainEntryEvents.exchange (0); }

        //======================================================================
        // TRANSITION TIMING — one behaviour, no selector.
        //
        // A queued transition fires on the TRANS quantise grid (the 1/2 / 1/4 /
        // 1/8 selector, meter-aware — see renderBlock), and the target section
        // always starts from its own tick 0.
        //
        // Three other timings used to live here (Instant, Split, NextBar).  They
        // all fired relative to the nearest LOCAL boundary — the current block or
        // the next bar line — with no regard for where the variation was inside
        // its loop cycle, so a press early in an 8-bar main abandoned the phrase
        // six bars short and handed the fill whatever scraps of a bar remained.
        // Instant and Split compounded it by JOINING the fill at the outgoing
        // bar's phase, which discarded the fill's opening in proportion to how
        // late in the bar you pressed.  Removed rather than kept as options: a
        // timing you would never choose is not a choice.
        //======================================================================
        int consumeLoopEvents()       noexcept { return loopEvents.exchange (0); }

    private:
        // Sentinels for "what to do at end of current section"
        static constexpr int kAfterLoop = -1;    // loop self (mains) or fall back to main
        static constexpr int kAfterStop = -2;    // stop transport

        static StyleSection mainSectionFor (StyleVariation v) noexcept;
        static StyleSection fillSectionFor (StyleVariation v) noexcept;
        static bool         isMainSection  (StyleSection s)  noexcept;
        static bool         isVariationFill (StyleSection s) noexcept;   // Fill AA..DD (not BA)

        // Jumps helpers (audio thread).  jumpDestFor: the configured landing
        // section after transition `src`.  afterFor: the nextAfterCurrent value
        // to use when ENTERING `s` — mains loop, endings stop, transitions get
        // their configured destination (so chained jumps work).
        StyleSection jumpDestFor (StyleSection src) const noexcept;
        int          afterFor    (StyleSection s)   const noexcept;

        void enterSection (StyleSection s, int afterCurrent);
        void onSectionEndReached();
        bool applyPendingAtBarBoundary();   // true if a transition was applied

        /** Diagnostic line per transition — see StyleSequencer.cpp. */
        void logTransition (const char* what, int section,
                            double lt, double len) const;
        void processPendingCommands();

        // ── State ─────────────────────────────────────────────────────────────
        std::atomic<const StyleData*> stylePtr        { nullptr };

        std::atomic<StyleTransport>   transport       { StyleTransport::Stopped };
        std::atomic<int>              currentSection  { (int) StyleSection::MainA };
        std::atomic<int>              currentVariation{ (int) StyleVariation::A };
        std::atomic<int>              nextAfterCurrent{ kAfterLoop };

        // Per-source jump destinations, indexed per Betel::kJumpSourceSections;
        // values are (int) StyleSection.  Seeded with JumpsConfig defaults in
        // the constructor body (reset()).
        std::array<std::atomic<int>, kNumJumpSources> jumpDest {};

        // TRANS selector mode: 0 = x1 (section boundary), 1 = x1/2 (+ middle).
        std::atomic<int> transitionQuant { 0 };
        std::atomic<int> fillLength      { 0 };   // subdivides the one-bar cap:
                                                  // 0 = the capped window whole,
                                                  // 1 = its second half
        std::atomic<double>           localTick       { 0.0 };
        size_t                        eventCursor     { 0 };   // audio-thread-only

        // Tick this section was ENTERED on, and therefore the tick a HOLD
        // re-cues it to.  Audio-thread-only: written by enterSection, read by
        // onSectionEndReached, both of which run inside renderBlock.
        //
        // Not simply 0, because a variation fill is entered on the TAIL of its
        // one-bar capped window (see enterSection).  Re-cueing a held fill to 0
        // would play the build-up bar the cap exists to skip, so the held loop
        // would sound like a different fill from the one the press produced.
        double                        sectionEntryTick { 0.0 };

        // Pending command flags (set from any thread, processed in renderBlock)
        std::atomic<bool> pendingStart     { false };
        std::atomic<bool> pendingStop      { false };
        std::atomic<int>  pendingIntro     { -1 };
        std::atomic<int>  pendingEnding    { -1 };
        std::atomic<int>  pendingVariation { -1 };

        // Did the pending variation come from a FILL button or a VAR button?
        // Only the FILL route plays a fill on the way; see selectVariation.
        std::atomic<bool> pendingVariationViaFill { false };
        std::atomic<bool> pendingFill      { false };
        std::atomic<bool> pendingBreak     { false };
        std::atomic<bool> pendingRestart   { false };
        std::atomic<bool> transitionHeld   { false };

        // Consumed by the host (crash tab).  Incremented on a real section
        // transition / on each section loop respectively.
        std::atomic<int>  transitionEvents  { 0 };
        std::atomic<int>  landingEvents      { 0 };
        std::atomic<int>  mainEntryEvents     { 0 };
        std::atomic<int>  loopEvents        { 0 };

        // Tempo + sample rate
        std::atomic<double> hostBPM    { 120.0 };
        double              sampleRate { 48000.0 };

        // Audio-thread-only.  Set true whenever a loop-wrap or section change
        // resets the event cursor; renderBlock consumes it on the next loop
        // iteration to drop a flushNotes marker at the boundary's sample offset.
        bool                boundaryReset { false };
        bool                boundaryWasSectionChange { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleSequencer)
    };
} // namespace Betel
