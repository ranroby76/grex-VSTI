#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "GrexSongRecorder.h"   // for Event / Setup / loadFromFile

//==============================================================================
// GrexSongPlayer.h  —  MIDI performance playback (PHASE 2)
//==============================================================================
//
// STATUS: PARKED / NOT WIRED.  Complete and self-contained companion to
// GrexSongRecorder.  Plays a captured .grxsong "straight out": it re-emits the
// recorded events — INCLUDING the arranger control-notes — back through the
// live input path, so the arranger regenerates the whole performance.  Costs
// nothing until the activation hooks below are added.
//
// ─── Why "straight out" reproduces everything ────────────────────────────────
// The recorded stream is the user's ACTIONS: solo notes, CC, program-change,
// AND the reserved control-notes that drive style transitions, jumps, play/stop,
// hold, etc.  Replaying that exact stream from beat 0 reproduces the full song:
//   • solo/melody notes  → sound directly,
//   • control-notes      → the arranger transitions/jumps at the same beats,
//                          so it regenerates the same accompaniment.
// No separate "start the arranger" step is required — whatever the user did
// first (often pressing PLAY) is event 0 and re-fires on playback.
//
// ─── Clock / sync ────────────────────────────────────────────────────────────
// The player keeps its own MONOTONIC beat clock, starting at 0 on start() and
// advancing by (numSamples / sampleRate) * (bpm / 60) every processBlock — the
// same beat domain the recorder timestamped against.  Because the player and
// the arranger both advance on the same processBlock calls at the same tempo,
// they stay in lockstep automatically.  Beat-domain timing is tempo-robust:
// changing tempo on playback speeds/slows wall-clock but keeps musical
// positions intact.
//
// As an instrument plugin, processBlock runs continuously (audio always flows),
// so playback works whether or not the host transport is rolling.
//
// ─── Thread-safety ───────────────────────────────────────────────────────────
//   • setSong / loadFile / start / stop      — MESSAGE THREAD only.
//   • renderBlock                            — AUDIO THREAD only (read-only over
//                                              a pre-loaded vector + a cursor;
//                                              no allocation beyond MidiBuffer's
//                                              own, which is the standard plugin
//                                              pattern the arranger already uses).
//   start() sets the cursor/clock while Idle (the audio thread ignores
//   renderBlock when Idle), so there is no race.
//   hasFinished() is an atomic flag a timer can poll to auto-stop the UI.
//
//==============================================================================
//
// ─── ACTIVATION HOOKS (Phase 2b — do this when playback goes live) ────────────
//
//   1) Processor member:
//          GrexSongPlayer songPlayer;
//
//   2) PLAY-SONG UI (message thread).  Load, restore the setup, then start:
//          GrexSongRecorder::Setup setup;
//          std::vector<GrexSongRecorder::Event> events;
//          if (GrexSongRecorder::loadFromFile (file, setup, events))
//          {
//              applySetup (setup);            // (3) below — restore session
//              songPlayer.setSong (setup, events);
//              songPlayer.start();
//          }
//
//   3) applySetup() — restore the captured session before starting (touches the
//      processor/tabs, hence it lives in the host, not here):
//          juce::String err;
//          processor.loadStyle (juce::File (setup.stylePath), err);
//          processor.setSplitPoint     (setup.splitPoint);
//          processor.setGlobalSemitone (setup.transpose);
//          for (int i = 0; i < 8; ++i) { soundsTab.setSoloPatch  (i, setup.soloPatch[i]);
//                                        soundsTab.setStylePatch (i, setup.stylePatch[i]); }
//          for (int i = 0; i < 8; ++i) jumpsTab.setDestination (Betel::kJumpSourceSections[i],
//                                                               (Betel::StyleSection) setup.jumpsDest[i]);
//          for (int i = 0; i < 5; ++i) processor.setCcNumber (i, setup.ccNumber[i]);
//          // tempo: host-driven, or push setup.tempoBpm to the internal tempo.
//
//   4) processBlock() — AUDIO THREAD, at the VERY START, before reading/
//      dispatching the input MIDI, so the scheduled events merge with (or stand
//      in for) live input and flow through the normal dispatch:
//          songPlayer.renderBlock (midiMessages, buffer.getNumSamples(),
//                                  getSampleRate(), currentBpm);
//
//   5) A message-thread timer (editor already has one) to auto-stop at the end:
//          if (songPlayer.hasFinished()) { /* reset PLAY button, etc. */ }
//
//==============================================================================

class GrexSongPlayer
{
public:
    using Event = GrexSongRecorder::Event;
    using Setup = GrexSongRecorder::Setup;
    enum class State { Idle = 0, Playing };

    GrexSongPlayer() = default;

    //==========================================================================
    // MESSAGE THREAD
    //==========================================================================

    /** Load the song to play. Stops any current playback first. */
    void setSong (const Setup& s, const std::vector<Event>& evts)
    {
        state.store ((int) State::Idle, std::memory_order_release);
        setup  = s;
        events = evts;
        cursor = 0;
        currentBeat = 0.0;
        finished.store (false, std::memory_order_release);
    }

    /** Convenience: load a .grxsong straight into the player. The caller should
        still applySetup(getSetup()) before start() to restore the session. */
    bool loadFile (const juce::File& file)
    {
        Setup s;
        std::vector<Event> evts;
        if (! GrexSongRecorder::loadFromFile (file, s, evts)) return false;
        setSong (s, evts);
        return true;
    }

    /** Begin playback from beat 0. Call applySetup(getSetup()) first. */
    void start()
    {
        state.store ((int) State::Idle, std::memory_order_release);
        cursor = 0;
        currentBeat = 0.0;
        finished.store (false, std::memory_order_release);
        if (! events.empty())
            state.store ((int) State::Playing, std::memory_order_release);
    }

    void stop() { state.store ((int) State::Idle, std::memory_order_release); }

    State getState()    const noexcept { return (State) state.load (std::memory_order_acquire); }
    bool  isPlaying()   const noexcept { return getState() == State::Playing; }

    /** Latched true when playback reaches the end. Cleared by start()/setSong().
        Poll from a timer to reset the transport UI. */
    bool  hasFinished() const noexcept { return finished.load (std::memory_order_acquire); }

    const Setup& getSetup() const noexcept { return setup; }

    //==========================================================================
    // AUDIO THREAD
    //==========================================================================

    /** Emit every event due in this block into @p midi (merged with live input),
        then advance the playback clock. Call at the start of processBlock. */
    void renderBlock (juce::MidiBuffer& midi, int numSamples,
                      double sampleRate, double bpm) noexcept
    {
        if ((State) state.load (std::memory_order_acquire) != State::Playing) return;
        if (sampleRate <= 0.0 || bpm <= 0.0 || numSamples <= 0)              return;

        const double blockBeats = (numSamples / sampleRate) * (bpm / 60.0);
        const double endBeat    = currentBeat + blockBeats;

        const int n = (int) events.size();
        while (cursor < n && events[(size_t) cursor].beats < endBeat)
        {
            const auto& e = events[(size_t) cursor];

            double frac = (blockBeats > 0.0) ? (e.beats - currentBeat) / blockBeats : 0.0;
            frac = juce::jlimit (0.0, 0.99999, frac);
            const int samp = juce::jlimit (0, numSamples - 1, (int) (frac * numSamples));

            midi.addEvent (makeMessage (e), samp);
            ++cursor;
        }

        currentBeat = endBeat;

        if (cursor >= n)
        {
            finished.store (true, std::memory_order_release);
            state.store ((int) State::Idle, std::memory_order_release);
        }
    }

private:
    // Rebuild a MidiMessage honouring 2-byte vs 3-byte status (program change /
    // channel pressure are 2-byte; everything else the arranger uses is 3-byte).
    static juce::MidiMessage makeMessage (const Event& e) noexcept
    {
        const int hi = e.status & 0xF0;
        if (hi == 0xC0 || hi == 0xD0)
            return juce::MidiMessage (e.status, e.data1);
        return juce::MidiMessage (e.status, e.data1, e.data2);
    }

    Setup              setup;
    std::vector<Event> events;

    std::atomic<int>   state    { (int) State::Idle };
    std::atomic<bool>  finished { false };

    // Audio-thread only (set by start()/setSong() while Idle).
    double currentBeat = 0.0;
    int    cursor      = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrexSongPlayer)
};
