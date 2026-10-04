
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>
#include "GrexSongRecorder.h"   // for Event / Setup / loadFromFile

//==============================================================================
// GrexSongPlayer.h  —  MIDI performance playback (PHASE 2)
//==============================================================================
//
// STATUS: LIVE.  Driven by the SONG PLAYER half of the seventh play-control
// slot, which opens SongWindowContent's transport (load / play / pause / stop /
// rewind, position, tempo and the 16 mutes); rendered from
// BetelgeuseProcessor::processBlock.
//
// Companion to GrexSongRecorder.  Plays a captured .grxsong "straight out": it
// re-emits the recorded events — INCLUDING the arranger control-notes — back
// through the live input path, so the arranger regenerates the whole
// performance.
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
        // Derived from the events, not carried separately, so it can never
        // disagree with them.
        lengthBeats.store (evts.empty() ? 0.0 : evts.back().beats,
                           std::memory_order_relaxed);
        positionBeats.store (0.0, std::memory_order_relaxed);
        paused.store (false, std::memory_order_release);

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

    void stop()
    {
        state.store ((int) State::Idle, std::memory_order_release);
        paused.store (false, std::memory_order_release);
        panicRequested.store (true, std::memory_order_release);
    }

    /** PAUSE, and it MUST silence what is sounding.

        A note held at the pause point has no note-off coming while the clock is
        frozen, so it would hang until something else happened to stop it -
        forever, in practice, because the thing that would have stopped it is the
        event we are refusing to play.

        Resume deliberately does NOT re-trigger them. A short gap is correct and
        is what every sequencer does; re-triggering would restart envelopes
        mid-phrase and sound worse than the gap. */
    void setPaused (bool p)
    {
        paused.store (p, std::memory_order_release);
        if (p) panicRequested.store (true, std::memory_order_release);
    }

    bool isPaused() const noexcept { return paused.load (std::memory_order_acquire); }

    //==========================================================================
    /** Jump to a beat. Message thread; the audio thread picks it up.

        SEEKING NEEDS CHASING and that is the whole difficulty. Replaying from
        bar 40 skips every program change, control change and section press that
        happened before it, so the band arrives in the wrong voicing and possibly
        the wrong section - it would not be the song, it would be whatever the
        engine happened to be holding.

        So the request is parked and the audio thread replays every NON-NOTE
        event up to the target in one go before resuming. Notes are skipped
        because a chase must not make sound. */
    void requestSeek (double beats) noexcept
    {
        seekTarget.store (juce::jmax (0.0, beats), std::memory_order_release);
        seekRequested.store (true, std::memory_order_release);
    }

    double getPositionBeats() const noexcept
    { return positionBeats.load (std::memory_order_relaxed); }

    double getLengthBeats() const noexcept
    { return lengthBeats.load (std::memory_order_relaxed); }

    /** Mute a replayed channel. 0-7 style, 8-15 solo.

        Applied to the EVENT STREAM, not to the mixer: a muted channel's notes
        are simply never emitted, so nothing has to be un-muted afterwards and
        the engine never sees them. The live toggles beat whatever the song's own
        set said - they are a performance control. */
    void setChannelMuted (int channel, bool muted) noexcept
    {
        if (channel < 0 || channel >= 16) return;
        const uint32_t bit = 1u << channel;
        if (muted) muteMask.fetch_or  (bit, std::memory_order_relaxed);
        else       muteMask.fetch_and (~bit, std::memory_order_relaxed);
    }

    /** BACKING-TRACK MODE. The recorded right hand is dropped and the keyboard
        owns that range; the recorded left hand keeps driving the band. */
    void setSoloLive (bool live) noexcept
    {
        soloLive.store (live, std::memory_order_relaxed);
    }

    bool isSoloLive() const noexcept
    {
        return soloLive.load (std::memory_order_relaxed);
    }

    bool isChannelMuted (int channel) const noexcept
    {
        return channel >= 0 && channel < 16
            && (muteMask.load (std::memory_order_relaxed) & (1u << channel)) != 0;
    }

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
        // ── PANIC ────────────────────────────────────────────────────────────
        //
        // Serviced BEFORE the playing check, because stop() and pause() both
        // raise it and both leave the state non-Playing - if it were inside the
        // guard the notes it exists to silence would hang exactly when they
        // matter.
        if (panicRequested.exchange (false, std::memory_order_acq_rel))
            for (int ch = 1; ch <= 16; ++ch)
                midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);

        if ((State) state.load (std::memory_order_acquire) != State::Playing) return;
        if (paused.load (std::memory_order_acquire))                          return;
        if (sampleRate <= 0.0 || bpm <= 0.0 || numSamples <= 0)              return;

        // ── SEEK, WITH A CHASE ───────────────────────────────────────────────
        //
        // Every NON-NOTE event up to the target is replayed in one burst so the
        // band arrives voiced and sectioned as it was at that point. Notes are
        // skipped: a chase must reposition the arrangement without making a
        // sound.
        if (seekRequested.exchange (false, std::memory_order_acq_rel))
        {
            const double target = seekTarget.load (std::memory_order_acquire);

            cursor      = 0;
            currentBeat = 0.0;
            finished.store (false, std::memory_order_release);

            const int total = (int) events.size();
            while (cursor < total && events[(size_t) cursor].beats < target)
            {
                const auto& e = events[(size_t) cursor];
                const int hi = e.status & 0xF0;

                const bool isNote = (hi == 0x80 || hi == 0x90);
                if (! isNote && ! isMutedEvent (e))
                    midi.addEvent (makeMessage (e), 0);

                ++cursor;
            }

            currentBeat = target;

            // Anything still ringing from before the jump belongs to the old
            // position and has no note-off coming.
            for (int ch = 1; ch <= 16; ++ch)
                midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        }

        const double blockBeats = (numSamples / sampleRate) * (bpm / 60.0);
        const double endBeat    = currentBeat + blockBeats;

        const int n = (int) events.size();
        while (cursor < n && events[(size_t) cursor].beats < endBeat)
        {
            const auto& e = events[(size_t) cursor];

            double frac = (blockBeats > 0.0) ? (e.beats - currentBeat) / blockBeats : 0.0;
            frac = juce::jlimit (0.0, 0.99999, frac);
            const int samp = juce::jlimit (0, numSamples - 1, (int) (frac * numSamples));

            // A muted channel's events are simply never emitted, so nothing
            // downstream has to be un-muted afterwards.
            if (! isMutedEvent (e) && ! isSuppressedRightHand (e))
                midi.addEvent (makeMessage (e), samp);

            ++cursor;
        }

        currentBeat = endBeat;
        positionBeats.store (currentBeat, std::memory_order_relaxed);

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

    //==========================================================================
    // BACKING-TRACK MODE: drop the RECORDED right hand, keep the live one.
    //
    // WHY NOT A CHANNEL MUTE. The recorded notes and the player's own live
    // notes end up on the SAME engine channels - the song is replayed through
    // the live input path so the arranger regenerates it - so muting the solo
    // channels would silence the player along with the recording. The two are
    // only separable HERE, before the recorded events are emitted.
    //
    // WHY THE RECORDED SPLIT, NOT THE LIVE ONE. Where the hands divided is a
    // property of the performance being replayed. If the player has moved the
    // split since, using the live value would either double a band of notes or
    // leave a silent gap in the recording. setup.splitPoint travels in the file
    // for exactly this.
    //
    // NOTES ONLY. Control notes carry the arrangement - sections, fills,
    // endings - and live below 36 anyway; a pitch bend or a sustain pedal has
    // no note number to test. Suppressing any of those would stop the backing
    // track rather than free the right hand.
    //==========================================================================
    bool isSuppressedRightHand (const Event& e) const noexcept
    {
        if (! soloLive.load (std::memory_order_relaxed)) return false;
        if (e.status == GrexSongRecorder::kRemoteStatus) return false;

        const int hi = e.status & 0xF0;
        if (hi != 0x80 && hi != 0x90) return false;      // note off / note on

        return (int) e.data1 >= setup.splitPoint;
    }

    /** Which of the sixteen a replayed event belongs to.

        A REMOTE gesture is never muted: those are section presses, and muting a
        channel must silence an instrument, not stop the arrangement moving. */
    bool isMutedEvent (const Event& e) const noexcept
    {
        if (e.status == GrexSongRecorder::kRemoteStatus) return false;

        const int hi = e.status & 0xF0;
        if (hi < 0x80 || hi > 0xE0) return false;

        const int ch = e.status & 0x0F;          // 0-15, style 0-7 / solo 8-15
        return (muteMask.load (std::memory_order_relaxed) & (1u << ch)) != 0;
    }

    Setup              setup;
    std::vector<Event> events;

    std::atomic<bool>     paused         { false };
    std::atomic<bool>     panicRequested { false };
    std::atomic<bool>     seekRequested  { false };
    std::atomic<double>   seekTarget     { 0.0 };
    std::atomic<double>   positionBeats  { 0.0 };
    std::atomic<double>   lengthBeats    { 0.0 };
    std::atomic<uint32_t> muteMask       { 0 };

    // BACKING-TRACK MODE. Off by default: a song plays back complete unless
    // the player asks for the top half back.
    std::atomic<bool>     soloLive       { false };

    std::atomic<int>   state    { (int) State::Idle };
    std::atomic<bool>  finished { false };

    // Audio-thread only (set by start()/setSong() while Idle).
    double currentBeat = 0.0;
    int    cursor      = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrexSongPlayer)
};
