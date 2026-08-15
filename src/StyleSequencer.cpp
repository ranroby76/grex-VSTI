#include "StyleSequencer.h"
#include "GlobalMacros.h"      // GrexPaths::styleLog — the transition diagnostic
#include <algorithm>
#include <cmath>

namespace Betel
{
    // =========================================================================
    //  Construction / lifecycle
    // =========================================================================
    StyleSequencer::StyleSequencer() = default;

    void StyleSequencer::setStyle (const StyleData* style)
    {
        // Reset all state to a known idle position. The audio thread will pick
        // up the new pointer on the next renderBlock.
        stylePtr.store (style);

        transport       .store (StyleTransport::Stopped);
        currentSection  .store ((int) StyleSection::MainA);
        setJumpsConfig (JumpsConfig());   // default IntroN->MainN, FillNN->MainN, Break->MainA
        currentVariation.store ((int) StyleVariation::A);
        nextAfterCurrent.store (kAfterLoop);
        localTick       .store (0.0);
        eventCursor   = 0;
        sectionEntryTick = 0.0;

        pendingStart   .store (false);
        pendingStop    .store (false);
        pendingIntro   .store (-1);
        pendingEnding  .store (-1);
        pendingVariation.store (-1);
        pendingVariationViaFill.store (false);
        pendingFill    .store (false);
        pendingBreak   .store (false);
    }

    void StyleSequencer::prepareToPlay (double sr) { sampleRate = sr; }
    void StyleSequencer::releaseResources()         { stop(); }

    // =========================================================================
    //  Transport + command setters (any thread — atomic only)
    // =========================================================================
    void StyleSequencer::start() { pendingStart.store (true); }
    void StyleSequencer::stop()  { pendingStop .store (true); }

    void StyleSequencer::selectVariation (StyleVariation v, bool viaFill)
    {
        pendingVariationViaFill.store (viaFill);

        if (transport.load() == StyleTransport::Stopped)
        {
            // Not running yet — set the variation that an upcoming start() will use.
            currentVariation.store ((int) v);
            currentSection  .store ((int) mainSectionFor (v));
            nextAfterCurrent.store (kAfterLoop);
            localTick       .store (0.0);
        }
        else
        {
            pendingVariation.store ((int) v);
        }
    }

    void StyleSequencer::triggerIntro (int idx)
    {
        if (idx < 0 || idx > 2) return;
        pendingIntro.store (idx);
    }

    void StyleSequencer::triggerEnding (int idx)
    {
        if (idx < 0 || idx > 2) return;
        pendingEnding.store (idx);
    }

    void StyleSequencer::triggerFill()  { pendingFill .store (true); }
    void StyleSequencer::triggerBreak() { pendingBreak.store (true); }

    // =========================================================================
    //  Query
    // =========================================================================
    StyleSection   StyleSequencer::getCurrentSection()   const noexcept
    {
        return (StyleSection) currentSection.load();
    }

    StyleVariation StyleSequencer::getCurrentVariation() const noexcept
    {
        return (StyleVariation) currentVariation.load();
    }

    int StyleSequencer::getCurrentBarInSection() const noexcept
    {
        const auto* s = stylePtr.load();
        if (s == nullptr) return 1;
        const int barTicks = s->ticksPerQuarter * s->timeSigNum * 4
                                 / std::max (1, s->timeSigDen);   // meter-aware bar (3/4, 6/8, 4/4…)
        if (barTicks <= 0) return 1;
        return 1 + (int) (localTick.load() / (double) barTicks);
    }

    juce::String StyleSequencer::describeState() const
    {
        juce::String s;
        s << "Transport : " << (isPlaying() ? "Playing" : "Stopped") << "\n"
          << "Section   : " << getStyleSectionName (getCurrentSection()) << "\n"
          << "Variation : Main " << (char) ('A' + (int) getCurrentVariation()) << "\n"
          << "Bar       : " << getCurrentBarInSection() << "\n"
          << "LocalTick : " << juce::String (localTick.load(), 1) << "\n";
        return s;
    }

    // =========================================================================
    //  Internal helpers
    // =========================================================================
    StyleSection StyleSequencer::mainSectionFor (StyleVariation v) noexcept
    {
        switch (v)
        {
            case StyleVariation::A: return StyleSection::MainA;
            case StyleVariation::B: return StyleSection::MainB;
            case StyleVariation::C: return StyleSection::MainC;
            case StyleVariation::D: return StyleSection::MainD;
        }
        return StyleSection::MainA;
    }

    StyleSection StyleSequencer::fillSectionFor (StyleVariation v) noexcept
    {
        switch (v)
        {
            case StyleVariation::A: return StyleSection::FillAA;
            case StyleVariation::B: return StyleSection::FillBB;
            case StyleVariation::C: return StyleSection::FillCC;
            case StyleVariation::D: return StyleSection::FillDD;
        }
        return StyleSection::FillAA;
    }

    bool StyleSequencer::isMainSection (StyleSection s) noexcept
    {
        return s == StyleSection::MainA || s == StyleSection::MainB
            || s == StyleSection::MainC || s == StyleSection::MainD;
    }

    bool StyleSequencer::isVariationFill (StyleSection s) noexcept
    {
        // The four variation fills only.  Fill In BA (the BREAK) is deliberately
        // excluded — see setFillLength.
        return s == StyleSection::FillAA || s == StyleSection::FillBB
            || s == StyleSection::FillCC || s == StyleSection::FillDD;
    }

    StyleSection StyleSequencer::jumpDestFor (StyleSection src) const noexcept
    {
        for (int i = 0; i < kNumJumpSources; ++i)
            if (kJumpSourceSections[i] == src)
                return (StyleSection) jumpDest[(size_t) i].load();
        // Not a configurable transition (shouldn't happen) — fall back to the
        // current variation's main, mirroring the old hardcoded behaviour.
        return mainSectionFor (getCurrentVariation());
    }

    int StyleSequencer::afterFor (StyleSection s) const noexcept
    {
        if (isMainSection (s))                    return kAfterLoop;
        if (s == StyleSection::EndingA || s == StyleSection::EndingB
                                       || s == StyleSection::EndingC)
                                                  return kAfterStop;
        return (int) jumpDestFor (s);   // transition -> its configured landing
    }

    /** One line per transition into grex_log.txt.  Message/audio thread — a plain
        append.  localTick is where the outgoing section had reached when the
        transition fired, and len is the target section's own length: together
        they say how much of the phrase was left on the table, which is the
        measurement the phrase-anchored timing will be built from. */
    void StyleSequencer::logTransition (const char* what, int section,
                                        double lt, double len) const
    {
        juce::String d;
        d << "[trans] " << what
          << " target="    << section
          << " localTick=" << juce::String (lt, 1)
          << " targetLen=" << juce::String (len, 1);
        GrexPaths::styleLog().appendText (d + juce::newLine);
    }

    void StyleSequencer::enterSection (StyleSection s, int afterCurrent)
    {
        currentSection  .store ((int) s);
        nextAfterCurrent.store (afterCurrent);

        // A var 1-4 just started.  Every path into a main comes through here —
        // landing out of a fill / intro / break, a direct cut between
        // variations, or a start — while a self-loop does not, because it never
        // calls enterSection.  So this is precisely "a variation began playing",
        // which is where the auto-crash count restarts.
        if (isMainSection (s))
            mainEntryEvents.fetch_add (1, std::memory_order_relaxed);

        // Every section starts from its own tick 0 unless the ONE-BAR CAP below
        // moves it.  (The bar-relative JOIN that used to sit here belonged to
        // Instant / Split: it entered the target at the outgoing bar's phase,
        // which is why a late press left almost none of the fill to play.  Both
        // methods are gone, so the plain entry is unconditional.)
        double startTick = 0.0;

        //----------------------------------------------------------------------
        // ONE-BAR CAP — a variation fill never plays more than its LAST BAR.
        //
        // A 2-bar fill costs two bars of the phrase it interrupts, and the cost
        // varies per style, so the player can never know what a press will do
        // without knowing the style.  Capping makes the price of a transition
        // CONSTANT — exactly one bar, every style, every meter — which is what
        // makes it learnable.
        //
        // The tail is what survives, for the same reason the fill's ending is
        // the part that matters: a 2-bar fill is a build in bar 1 and the
        // approach to the downbeat in bar 2.  Dropping the build keeps the
        // function; dropping the approach would destroy it.
        //
        // FILL LENGTH then subdivides whatever the cap left:
        //     "1"    the whole capped window  (a 1-bar fill plays entire)
        //     "1/2"  its second half          (short and punchy)
        // Uniform at last, which the old rule was not: `len * 0.5` happened to
        // give the last BAR of a 2-bar fill but only half a bar of a 1-bar one,
        // so the same setting meant two different things.
        //
        // Variation fills only — isVariationFill excludes Fill In BA, so the
        // BREAK keeps its authored length.  Intros and endings are meant to be
        // long and are untouched.
        //
        // eventCursor stays at 0 on purpose: renderBlock's emit loop walks past
        // every event earlier than localTick WITHOUT emitting it (the
        // `evLt >= lt` guard), so the skipped head is silent and the cursor
        // self-corrects on the first block.
        //----------------------------------------------------------------------
        if (isVariationFill (s))
        {
            if (const auto* style = stylePtr.load())
            {
                const auto&  sec = style->getSection (s);
                const double len = (double) (sec.endTick - sec.startTick);

                // Meter-aware bar, matching renderBlock's own formula so the cap
                // lands on a real bar line in 3/4, 6/8 and 7/8 as well as 4/4.
                const double barTicks = (double) (style->ticksPerQuarter
                                                  * style->timeSigNum * 4
                                                  / std::max (1, style->timeSigDen));

                if (len > 0.0 && barTicks > 0.0)
                {
                    double window = std::min (len, barTicks);
                    if (fillLength.load() == 1) window *= 0.5;
                    startTick = len - window;          // always the fill's TAIL
                }
            }
        }

        localTick     .store (startTick);
        sectionEntryTick = startTick;   // where a HOLD re-cues this section to
        eventCursor   = 0;
        boundaryReset = true;   // see renderBlock — flush sounding style notes
        boundaryWasSectionChange = true;   // a real change: re-assert setup voices
    }

    void StyleSequencer::onSectionEndReached()
    {
        const auto* style = stylePtr.load();
        if (style == nullptr) return;

        // TRANS quantisation: the section boundary is the canonical point where
        // queued transitions take effect (x1 and x1/2 alike).  Only fires while
        // looping a MAIN -- transition sections keep their pre-decided
        // destination, and HOLD still freezes everything.  What HOLD does to
        // the section already running is handled immediately below.
        if (applyPendingAtBarBoundary())
            return;

        //----------------------------------------------------------------------
        // HOLD ON A TRANSITION SECTION
        //
        // HOLD used to freeze only the QUEUE: pendings stayed pending, but a
        // fill / intro / break / ending that was ALREADY RUNNING still reached
        // its end and handed over to its destination — or stopped the
        // transport, in the ending's case.  So pressing HOLD during a
        // transition did nothing you could hear: the section left anyway, and
        // the hold only started meaning something once a main was back
        // underneath it.
        //
        // Now the running section is held too.  A non-main re-cues to its own
        // entry point and keeps playing its role until HOLD is released, at
        // which point the next end-of-section runs the ordinary path below and
        // the transition lands (or the ending stops) exactly as it would have.
        //
        // The re-cue goes to sectionEntryTick rather than tick 0 so a variation
        // fill repeats the one-bar capped window it actually entered on — it
        // vamps the bar you are hearing, not the build-up bar the cap dropped.
        // Intros, breaks and endings enter at 0, so for them the two are the
        // same thing.
        //
        // THE COUNTERS ARE DELIBERATELY LEFT ALONE:
        //   * no landingEvents — nothing landed, and a crash on every held loop
        //     is precisely the machine-gun that moving the crash onto the
        //     landing was meant to stop;
        //   * no mainEntryEvents — no main was entered, so the auto-crash's
        //     zero point must not move;
        //   * no loopEvents — that counter means "a VARIATION completed a
        //     cycle", and the auto-crash depends on transitions never raising
        //     one ("only main self-loops raise loopEvents").
        //
        // The length guard is not decoration.  renderBlock reaches this
        // function through a `continue` in its own loop, so re-cueing a section
        // whose entry point sits at or past its end would spin the audio
        // thread.  When the guard cannot be satisfied HOLD simply does not
        // apply to the section and the normal path runs.
        //----------------------------------------------------------------------
        if (transitionHeld.load() && ! isMainSection (getCurrentSection()))
        {
            const auto&  heldSec = style->getSection (getCurrentSection());
            const double heldLen = (double) (heldSec.endTick - heldSec.startTick);

            if (heldSec.present && heldLen > 0.0 && sectionEntryTick < heldLen - 0.5)
            {
                localTick.store (sectionEntryTick);
                eventCursor   = 0;
                boundaryReset = true;              // flush whatever is still sounding
                boundaryWasSectionChange = false;  // same section — its program
                                                   // changes are already in force
                return;
            }
        }

        const int after = nextAfterCurrent.load();

        if (after == kAfterStop)
        {
            // An ENDING finishing counts as a landing.  It is the one transition
            // that arrives nowhere — kAfterStop returns before the landing
            // counters below — so without this an ending could never fire the
            // crash, by construction, no matter what the Crash tab said.
            //
            // The cymbal rings out over the stop rather than being cut with the
            // style: triggerCrash plays it straight on the drum channel and
            // StylePlayer never tracks it, so the playing->stopped flush in the
            // processor (which releases TRACKED style notes only) leaves it
            // alone.  That is the intended sound — the final crash decaying
            // after the last hit.
            landingEvents.fetch_add (1, std::memory_order_relaxed);
            transport.store (StyleTransport::Stopped);
            return;
        }

        if (after == kAfterLoop)
        {
            // Loop ourselves if we're a main; otherwise fall back to the
            // current variation's main (defensive — shouldn't normally happen).
            if (isMainSection (getCurrentSection()))
            {
                localTick   .store (0.0);
                eventCursor = 0;
                boundaryReset = true;   // self-loop wrap — flush sounding notes
                // NOT a section change: the section's own program changes are
                // already in force and must not be undone.  See
                // EmittedStyleEvent::sectionChanged.
                boundaryWasSectionChange = false;
                loopEvents.fetch_add (1, std::memory_order_relaxed);   // one cycle done
            }
            else
            {
                // A transition section with no explicit destination still LANDS
                // on its variation's main, so it counts like any other.
                landingEvents.fetch_add (1, std::memory_order_relaxed);
                enterSection (mainSectionFor (getCurrentVariation()), kAfterLoop);
            }
            return;
        }

        // Explicit "next section" — usually we're leaving a fill/intro/break
        // and arriving at a main. Update currentVariation accordingly.
        const auto next = (StyleSection) after;

        // A LANDING: this boundary is where the transition ends and the main
        // begins.  Counted only when we are actually arriving FROM a transition,
        // so a main-to-main switch (which has no run-up) does not claim one.
        if (! isMainSection (getCurrentSection()) && isMainSection (next))
            landingEvents.fetch_add (1, std::memory_order_relaxed);

        if      (next == StyleSection::MainA) currentVariation.store ((int) StyleVariation::A);
        else if (next == StyleSection::MainB) currentVariation.store ((int) StyleVariation::B);
        else if (next == StyleSection::MainC) currentVariation.store ((int) StyleVariation::C);
        else if (next == StyleSection::MainD) currentVariation.store ((int) StyleVariation::D);

        enterSection (next, afterFor (next));
    }

    bool StyleSequencer::applyPendingAtBarBoundary()
    {
        // HOLD freezes transitions: queued switches stay pending and fire
        // once hold releases.  The current section keeps looping meanwhile.
        if (transitionHeld.load()) return false;

        const bool inMain = isMainSection (getCurrentSection());

        // Highest priority: ending.  Always allowed regardless of section —
        // pressing ENDING during an intro should bail out of the intro at
        // the next quant boundary, not wait for the intro's auto-jump.
        const int endingIdx = pendingEnding.load();
        if (endingIdx >= 0)
        {
            const auto end = (StyleSection) ((int) StyleSection::EndingA + endingIdx);
            pendingEnding   .store (-1);
            pendingIntro    .store (-1);
            pendingVariation.store (-1);
            pendingVariationViaFill.store (false);
            pendingFill     .store (false);
            pendingBreak    .store (false);
            transitionEvents.fetch_add (1, std::memory_order_relaxed);
            enterSection (end, kAfterStop);
            return true;
        }

        // Intro pressed mid-playback.  pendingIntro is normally consumed by
        // the start() path so playback begins WITH the chosen intro.  When
        // it's set while already playing, cut to that intro on the next
        // quant boundary, then let the intro play its full sequence and
        // auto-jump to its configured destination (jumpDestFor(intro)) —
        // exactly the same trajectory as starting with that intro.  This is
        // what makes "press INTRO mid-style" actually do something instead
        // of silently queueing for an event that'll never come.
        const int introIdx = pendingIntro.load();
        if (introIdx >= 0)
        {
            const auto intro      = (StyleSection) ((int) StyleSection::IntroA + introIdx);
            const auto afterIntro = jumpDestFor (intro);
            pendingIntro    .store (-1);
            pendingVariation.store (-1);
            pendingVariationViaFill.store (false);
            pendingFill     .store (false);
            pendingBreak    .store (false);
            transitionEvents.fetch_add (1, std::memory_order_relaxed);
            enterSection (intro, (int) afterIntro);
            return true;
        }

        // Break — only meaningful while looping a main.
        if (inMain && pendingBreak.load())
        {
            pendingBreak    .store (false);
            pendingFill     .store (false);
            pendingVariation.store (-1);
            pendingVariationViaFill.store (false);
            const auto afterBreak = jumpDestFor (StyleSection::FillBA);
            transitionEvents.fetch_add (1, std::memory_order_relaxed);
            enterSection (StyleSection::FillBA, (int) afterBreak);
            return true;
        }

        // Variation change — allowed from any section.
        //
        // HOW IT GETS THERE IS THE CALLER'S CHOICE, and that is the change:
        //
        //   VAR 1-4  -> viaFill false -> cut straight to the target main at the
        //               next quant point.  Pressing VAR 3 means "be on VAR 3",
        //               and wrapping that in VAR 3's fill put a bar of something
        //               nobody asked for in front of it.
        //   FILL 1-4 -> viaFill true  -> play the destination's fill, then land
        //               on its main.
        //
        // The two buttons used to be the same thing whenever the target differed
        // from the current variation: both ran fill-then-land, so FILL n and
        // VAR n were indistinguishable and there was no way to change groove on
        // the beat.  Now each does one job.
        //
        // Mid-non-main (intro / fill / ending / break) it is always a direct cut
        // regardless of the flag - bailing out of a fill by way of another fill
        // is not a transition anybody wants.
        const int newVarRaw = pendingVariation.load();
        if (newVarRaw >= 0)
        {
            const bool viaFill = pendingVariationViaFill.load();
            pendingVariation.store (-1);
            pendingVariationViaFill.store (false);
            if (newVarRaw != (int) getCurrentVariation() || ! inMain)
            {
                pendingFill.store (false);                  // implicit in the change
                const auto toMain = mainSectionFor ((StyleVariation) newVarRaw);
                transitionEvents.fetch_add (1, std::memory_order_relaxed);
                if (inMain && viaFill)
                {
                    // Play the DESTINATION variation's fill (a "fill into" the
                    // target), then land on its main.  Using the target's fill
                    // — rather than the outgoing variation's — means the fill
                    // button that lights matches where we're heading (pressing
                    // FILL n always lights FILL n, on the first press), instead
                    // of briefly showing the previous variation's fill.
                    const auto fromFill = fillSectionFor ((StyleVariation) newVarRaw);
                    {
                        double len = 0.0;
                        if (const auto* st = stylePtr.load())
                        {
                            const auto& sec = st->getSection (fromFill);
                            len = (double) (sec.endTick - sec.startTick);
                        }
                        logTransition ("VAR", (int) fromFill, localTick.load(), len);
                    }
                    enterSection (fromFill, (int) toMain);
                }
                else
                {
                    // Direct cut - no fill wrapper.  Either a VAR button (the
                    // point of it) or we are bailing out of a non-main section.
                    enterSection (toMain, kAfterLoop);
                }
                return true;
            }
            // Same variation requested while already in its main — no-op.
        }

        // Plain fill (return to same main after) — only meaningful in main.
        if (inMain && pendingFill.load())
        {
            pendingFill.store (false);
            const auto fromFill = fillSectionFor (getCurrentVariation());
            const auto toDest   = jumpDestFor (fromFill);   // Jumps-tab landing
            transitionEvents.fetch_add (1, std::memory_order_relaxed);

            // DIAGNOSTIC: localTick says how far into its loop cycle the
            // variation had got when the fill fired, and len is the fill's own
            // length.  Read together across a session they show how much phrase
            // each press threw away — the number the phrase-anchored timing has
            // to fix, and the reason this log stays for now.
            {
                double len = 0.0;
                if (const auto* st = stylePtr.load())
                {
                    const auto& sec = st->getSection (fromFill);
                    len = (double) (sec.endTick - sec.startTick);
                }
                logTransition ("FILL", (int) fromFill, localTick.load(), len);
            }

            enterSection (fromFill, (int) toDest);
            return true;
        }

        return false;
    }

    void StyleSequencer::processPendingCommands()
    {
        // stop wins over start in the same block
        if (pendingStop.exchange (false))
        {
            transport.store (StyleTransport::Stopped);
            return;
        }

        if (pendingStart.exchange (false))
        {
            // Idempotent start: only CUE a section (which resets the section
            // tick to 0) when the style is NOT already playing.  Sync-start and
            // gate (ONPRESS) modes fire start() on every empty→held chord-zone
            // press, so a chord CHANGE that momentarily empties the buffer would
            // otherwise re-cue the section and jump the groove back to the top —
            // the "style restarts / loses sync on chord change" bug.  When we're
            // already Playing, a stray start() is a no-op; the chord change just
            // shifts the held notes (StylePlayer) while the sequencer keeps its
            // exact tick position.
            if (transport.load() != StyleTransport::Playing)
            {
                const int introIdx = pendingIntro.exchange (-1);
                if (introIdx >= 0)
                {
                    const auto intro = (StyleSection) ((int) StyleSection::IntroA + introIdx);
                    const auto afterIntro = jumpDestFor (intro);   // Jumps-tab landing
                    enterSection (intro, (int) afterIntro);
                }
                else
                {
                    enterSection (mainSectionFor (getCurrentVariation()), kAfterLoop);
                }
                transport.store (StyleTransport::Playing);
            }
        }

        // RESTART — re-cue the current section to tick 0, keep playing.
        // Bypasses hold (it's a re-cue, not a transition).  Hung notes are
        // flushed by the processor (stylePlayer.allNotesOff) before render.
        if (pendingRestart.exchange (false))
        {
            if (transport.load() == StyleTransport::Playing)
                enterSection ((StyleSection) currentSection.load(),
                              nextAfterCurrent.load());
        }
    }

    // =========================================================================
    //  renderBlock — the audio-thread workhorse
    // =========================================================================
    void StyleSequencer::renderBlock (int blockSizeSamples,
                                      std::vector<EmittedStyleEvent>& outEvents)
    {
        processPendingCommands();

        // NB: boundaryReset is intentionally NOT cleared here.  A loop/section
        // reset that lands exactly on the previous block's final sample stays
        // pending and emits its flush marker at the top of the loop below.  A
        // start/restart also sets it (via enterSection), but flushing the style
        // slots at playback start is a harmless no-op (nothing is sounding, and
        // the processor already flushes on stop/restart), so no special-casing.

        if (transport.load() == StyleTransport::Stopped) return;

        const auto* style = stylePtr.load();
        if (style == nullptr) return;
        if (sampleRate <= 0.0 || blockSizeSamples <= 0) return;

        const double bpm = hostBPM.load();
        if (bpm <= 0.0) return;

        const double ticksPerSample =
            (double) style->ticksPerQuarter * bpm / (60.0 * sampleRate);
        if (ticksPerSample <= 0.0) return;

        const int    barTicks = style->ticksPerQuarter * style->timeSigNum * 4
                                    / std::max (1, style->timeSigDen);   // meter-aware bar
        const double samplesPerTick = 1.0 / ticksPerSample;

        int samplesProcessed = 0;
        int safetyIters      = 0;
        const int kMaxIters  = 64;   // refuses to spin forever on bad data

        while (samplesProcessed < blockSizeSamples && safetyIters++ < kMaxIters)
        {
            // A loop-wrap or section change in the previous iteration reset the
            // event cursor.  Drop a flush marker at the current sample offset so
            // the dispatcher releases every note still sounding from the section
            // we just left, in stream order, BEFORE any of the new section's
            // note-ons below.  Prevents notes that outlast the phrase (or span a
            // mid-section transition) from hanging as orphans.
            if (boundaryReset)
            {
                boundaryReset = false;
                EmittedStyleEvent flush;
                flush.flushNotes     = true;
                flush.sectionChanged = boundaryWasSectionChange;
                // keepSounding stays at its false default: it existed for SPLIT,
                // which was the only path that joined a section mid-flight and
                // wanted sustained notes carried across the splice.  Every
                // boundary now damps, as it did under every other method.
                flush.sampleOffset = juce::jlimit (0, blockSizeSamples - 1, samplesProcessed);
                outEvents.push_back (flush);
            }

            const auto curSec  = getCurrentSection();
            const auto& sec    = style->getSection (curSec);

            if (! sec.present)
            {
                // Section missing — try the current variation's main, otherwise stop.
                const auto fallback = mainSectionFor (getCurrentVariation());
                if (fallback == curSec)
                {
                    transport.store (StyleTransport::Stopped);
                    return;
                }
                enterSection (fallback, kAfterLoop);
                continue;
            }

            const double secLenTicks = (double) (sec.endTick - sec.startTick);
            if (secLenTicks <= 0.0)
            {
                onSectionEndReached();
                if (! isPlaying()) return;
                continue;
            }

            const double lt = localTick.load();

            // Did we already reach (or overshoot) the end of this section?
            if (lt >= secLenTicks - 0.5)
            {
                onSectionEndReached();
                if (! isPlaying()) return;
                continue;
            }

            // Next decision point inside this section: bar boundary OR section end.
            const double currentBar = std::floor (lt / (double) barTicks);
            double nextBar = (currentBar + 1.0) * (double) barTicks;
            if (lt >= nextBar - 0.5) nextBar += (double) barTicks;
            const double nextStop = std::min (nextBar, secLenTicks);

            const double ticksToStop = nextStop - lt;
            const double samplesToStopExact = ticksToStop * samplesPerTick;
            const int    samplesToEndOfBlock = blockSizeSamples - samplesProcessed;
            int          samplesThisChunk = (int) std::min (
                (double) samplesToEndOfBlock,
                std::max (1.0, std::floor (samplesToStopExact)));

            // Ticks actually consumed by this chunk
            double deltaTicks = (double) samplesThisChunk * ticksPerSample;
            double newLt      = lt + deltaTicks;

            // If we consumed enough samples to reach the next decision point
            // (bar boundary or section end) and we weren't just limited by
            // hitting end-of-block, snap to the exact boundary tick so we
            // don't repeatedly come up a fraction short.
            const bool reachesStop =
                samplesThisChunk >= (int) std::floor (samplesToStopExact)
                && samplesThisChunk < samplesToEndOfBlock;
            if (reachesStop && newLt < nextStop)
                newLt = nextStop;

            // Emit events in [lt, newLt)
            while (eventCursor < sec.eventIdx.size())
            {
                const auto& e = style->events[sec.eventIdx[eventCursor]];
                const double evLt = (double) e.tick - (double) sec.startTick;
                if (evLt >= newLt) break;
                if (evLt >= lt)
                {
                    EmittedStyleEvent em;
                    em.event = &e;
                    em.casm  = sec.findCasm (e.channel);
                    em.sampleOffset = samplesProcessed
                        + (int) ((evLt - lt) * samplesPerTick);
                    if (em.sampleOffset < 0)                 em.sampleOffset = 0;
                    if (em.sampleOffset >= blockSizeSamples) em.sampleOffset = blockSizeSamples - 1;
                    outEvents.push_back (em);
                }
                ++eventCursor;
            }

            localTick.store (newLt);
            samplesProcessed += samplesThisChunk;

            // Handle the boundary we just hit (if any).
            if (newLt >= secLenTicks - 0.5)
            {
                onSectionEndReached();
                if (! isPlaying()) return;
            }
            else
            {
                // TRANS selector quantisation: how often the engine checks
                // for queued transitions (variation switches, fills, breaks,
                // endings).
                //
                // METER-AWARE.  The grid is derived from the style's BEAT --
                // barTicks / timeSigNum -- not from a raw quarter note, so it
                // always divides the bar a whole number of times.  That is the
                // whole point: a fixed quarter-note grid drifts against any bar
                // that isn't a multiple of a quarter, so the same button press
                // at the same felt moment fired on a DIFFERENT beat depending on
                // which bar you happened to be in.  Measured before this fix:
                //
                //   3/4 (bar 5760) on the coarse setting (3840):
                //       bar0:beat3 -> bar1:beat2 -> bar2:beat1 -> ...  (3-bar cycle)
                //   9/8 (bar 8640) on the middle setting (1920):
                //       bar0: eighths 3,5,7,9 -> bar1: eighths 2,4,6,8 -> ...
                //       i.e. half the time it fired on a WEAK eighth, never
                //       where the 2+2+2+3 Balkan accent falls.
                //
                // Selector positions (labels "1/2" / "1/4" / "1/8" read as
                // fractions of a bar, which is exactly what they are in 4/4):
                //   0 = coarse: two beats -- or the WHOLE BAR when the meter has
                //       an odd beat count (3/4, 9/8, 5/4, 7/8), because two beats
                //       cannot divide an odd bar evenly.  The bar line is the
                //       right coarse anchor there anyway.
                //   1 = one beat.
                //   2 = half a beat (most responsive).
                //
                // In 4/4 this yields 3840 / 1920 / 960 -- byte-identical to the
                // old fixed-TPQ behaviour, so nothing already dialled in changes.
                // In every other meter the grid now lands on real musical
                // positions instead of drifting.
                //
                // Fires regardless of the section type (main / intro / fill /
                // ending all eligible).  The HOLD lock and the per-pending
                // section-type rules inside applyPendingAtBarBoundary do the
                // final gating.
                const int quant = transitionQuant.load();

                const int barTicksQ = style->ticksPerQuarter * style->timeSigNum * 4
                                          / std::max (1, style->timeSigDen);
                const int beatTicks = std::max (1, style->ticksPerQuarter * 4
                                                       / std::max (1, style->timeSigDen));
                const bool evenBeats = (style->timeSigNum % 2) == 0;

                int unitTicks;
                if      (quant == 0) unitTicks = evenBeats ? beatTicks * 2
                                                           : std::max (1, barTicksQ);
                else if (quant == 2) unitTicks = std::max (1, beatTicks / 2);
                else                 unitTicks = beatTicks;

                //--------------------------------------------------------------
                // WHEN a queued transition is allowed to fire: on the TRANS
                // quantise unit computed above, and nowhere else.  This was the
                // GRID method; it is now the only one, so there is no branch.
                //--------------------------------------------------------------
                if (unitTicks > 0)
                {
                    const long long oldUnit = (long long) (lt    / (double) unitTicks);
                    const long long newUnit = (long long) (newLt / (double) unitTicks);
                    if (newUnit > oldUnit && newLt > 0.5)
                        applyPendingAtBarBoundary();
                }
            }
        }
    }
} // namespace Betel
