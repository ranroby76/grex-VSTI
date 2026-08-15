#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

//==============================================================================
// GrexSongRecorder.h  —  MIDI performance capture (PHASE 1)
//==============================================================================
//
// STATUS: PARKED / NOT WIRED.  This component is complete and self-contained,
// but it is intentionally NOT hooked into the processor yet and may or may not
// ship in the first public release of Grex.  Including this header has zero
// runtime cost until the activation hooks below are added.
//
// ─── What it does ────────────────────────────────────────────────────────────
// Records a whole arranger performance as a single self-contained "Grex song".
// Because every Grex control (style transitions, variation pads, jumps,
// play/stop, hold, arranger/piano, solo selects) is fired by a reserved MIDI
// control-note, and because solo notes / CC / program-change are already MIDI,
// ONE timestamped MIDI stream captures the entire performance uniformly.
//
// The file also stores the SESSION SETUP captured at the moment recording is
// armed (style loaded, jumps config, solo/style patch assignments, CC map,
// tempo / time-signature / split point), so the song can be rebuilt from A to Z
// from a single file.
//
// ─── Phase-1 behaviour (locked decisions) ────────────────────────────────────
//   • Format       : Grex-native .grxsong (setup + timestamped event stream).
//   • Arming       : armed-and-waiting — the FIRST user action of ANY kind
//                    latches the song start (t = 0).  Nothing before it counts.
//   • Workflow     : record → stop → save.  No timeline / overdub / quantise.
//   • Playback     : NOT in phase 1.  Phase 2 will "play it straight out" by
//                    re-emitting the recorded events (incl. the control-notes)
//                    back through the live input path so the arranger
//                    regenerates the accompaniment.  See loadFromFile() — it is
//                    provided now so phase 2 only has to add the scheduler.
//
// ─── Timeline / clock ────────────────────────────────────────────────────────
// Events are timestamped in BEATS (quarter-notes) from the song start.  The
// caller supplies an absolute beat position per event (recordEvent), so the
// recorder imposes no clock of its own.  Recommended source: the arranger's own
// running song position so capture works whether or not the host transport is
// rolling; host PPQ from AudioPlayHead is an equally valid source.
//
// ─── Thread-safety ───────────────────────────────────────────────────────────
//   • recordEvent()  — AUDIO THREAD only.  Lock-free, no allocation: it writes
//                      into a fixed-capacity FIFO (events are dropped, never
//                      blocked, if the FIFO ever overruns).
//   • arm/stop/serviceFifo/save/load — MESSAGE THREAD only.
//   serviceFifo() must be pumped regularly (e.g. the 30 Hz editor timer) to move
//   events from the FIFO into the growable song buffer.
//
//==============================================================================
//
// ─── ACTIVATION HOOKS (Phase 1b — do this when the feature goes live) ─────────
//
//   1) Processor member:
//          GrexSongRecorder songRecorder;
//
//   2) processBlock() — AUDIO THREAD, for every incoming MIDI message, before or
//      after dispatch (order doesn't matter, the timestamp does):
//          if (songRecorder.isActive())
//          {
//              const double beats = blockStartBeats
//                                 + (msg.getTimeStamp() / sampleRate) * (bpm / 60.0);
//              const auto* d = msg.getRawData();
//              const int   n = msg.getRawDataSize();
//              songRecorder.recordEvent (beats,
//                                        (uint8_t) d[0],
//                                        (uint8_t) (n > 1 ? d[1] : 0),
//                                        (uint8_t) (n > 2 ? d[2] : 0));
//          }
//      (blockStartBeats = the arranger song position, or playhead ppqPosition.)
//
//   3) A message-thread timer (editor already has one):
//          songRecorder.serviceFifo();
//
//   4) REC / STOP UI (e.g. a button on the Main tab or transport):
//        on REC  : build a Setup snapshot from current state, then
//                  songRecorder.arm (setup);
//        on STOP : songRecorder.stop();
//                  if (songRecorder.getEventCount() > 0)
//                      songRecorder.saveToFile (chosenFile);   // *.grxsong
//
//      Building the Setup snapshot (message thread, at REC time):
//          GrexSongRecorder::Setup s;
//          s.stylePath  = processor.getCurrentStylePath();
//          s.tempoBpm   = currentBpm;
//          s.timeSigNum = num;  s.timeSigDen = den;
//          s.splitPoint = processor.getSplitPoint();
//          s.transpose  = processor.getGlobalSemitone();
//          for (int i = 0; i < 8; ++i) { s.soloPatch[i]  = soundsTab.getSoloPatch(i);
//                                        s.stylePatch[i] = soundsTab.getStylePatch(i); }
//          const auto jc = jumpsTab.getConfig();      // 8 source -> dest section
//          for (int i = 0; i < 8; ++i) s.jumpsDest[i] = (int) jc.destinations[i];
//          for (int i = 0; i < 5; ++i) s.ccNumber[i]  = processor.getCcNumber(i);
//
//==============================================================================
//
// ─── FILE FORMAT (.grxsong) ──────────────────────────────────────────────────
// A single juce::ValueTree of type "GrexSong" written with writeToStream():
//   properties (setup): version, stylePath, tempoBpm, timeSigNum, timeSigDen,
//                       splitPoint, transpose, soloPatch, stylePatch, jumpsDest,
//                       ccNumber.  (int arrays are stored as space-joined ints.)
//   property  "events": a binary MemoryBlock — [int32 count] followed by, per
//                       event, [double beats][uint8 status][uint8 d1][uint8 d2].
// Compact, versioned, and trivially forward-extensible (add properties; bump
// version; readers tolerate missing properties).
//==============================================================================

class GrexSongRecorder
{
public:
    enum class State { Idle = 0, Armed, Recording };

    // One captured MIDI event.  POD so it lives safely in the lock-free FIFO.
    struct Event
    {
        double  beats  = 0.0;   // quarter-notes from song start
        uint8_t status = 0;     // MIDI status byte (note/CC/PC/etc.)
        uint8_t data1  = 0;
        uint8_t data2  = 0;
    };

    // Session state captured the moment recording is armed.
    struct Setup
    {
        juce::String       stylePath;
        double             tempoBpm   = 120.0;
        int                timeSigNum = 4;
        int                timeSigDen = 4;
        int                splitPoint = 60;
        int                transpose  = 0;       // global semitones
        std::array<int, 8> soloPatch  { {} };    // GM program per solo slot
        std::array<int, 8> stylePatch { {} };    // GM program per style role
        std::array<int, 8> jumpsDest  { {} };    // jump source row -> dest col
        std::array<int, 5> ccNumber   { { 7, 11, 16, 17, 18 } }; // learnable CC map
    };

    GrexSongRecorder() = default;

    //==========================================================================
    // MESSAGE THREAD
    //==========================================================================

    /** Capture the session setup and arm. The next user action latches t = 0. */
    void arm (const Setup& setupAtArm)
    {
        // Order matters: while Idle the audio thread ignores recordEvent(), so
        // it is safe to clear the buffers here before flipping to Armed.
        state.store ((int) State::Idle, std::memory_order_release);
        setup = setupAtArm;
        song.clear();
        fifo.reset();
        startBeats.store (0.0, std::memory_order_release);
        state.store ((int) State::Armed, std::memory_order_release);
    }

    /** Stop recording and drain any remaining events. The song stays in memory
        until the next arm(); call saveToFile() afterwards to persist it. */
    void stop()
    {
        state.store ((int) State::Idle, std::memory_order_release);
        serviceFifo();   // final drain
    }

    /** Move queued events from the lock-free FIFO into the song buffer. Pump
        this regularly from a message-thread timer while armed/recording. */
    void serviceFifo()
    {
        int s1, n1, s2, n2;
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;

        fifo.prepareToRead (ready, s1, n1, s2, n2);
        song.reserve (song.size() + (size_t) (n1 + n2));
        for (int i = 0; i < n1; ++i) song.push_back (fifoBuffer[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) song.push_back (fifoBuffer[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }

    State getState()     const noexcept { return (State) state.load (std::memory_order_acquire); }
    bool  isArmed()      const noexcept { return getState() == State::Armed; }
    bool  isRecording()  const noexcept { return getState() == State::Recording; }
    bool  isActive()     const noexcept { return getState() != State::Idle; }
    int   getEventCount() const noexcept { return (int) song.size(); }

    const Setup&              getSetup() const noexcept { return setup; }
    const std::vector<Event>& getSong()  const noexcept { return song;  }

    //==========================================================================
    // AUDIO THREAD — RT-safe, lock-free, allocation-free.
    //==========================================================================

    /** Record one MIDI event at absolute beat position @p beatsAbsolute.
        Call for every incoming message while isActive(). The first event seen
        while Armed latches the song start (action-driven). */
    void recordEvent (double beatsAbsolute, uint8_t status, uint8_t d1, uint8_t d2) noexcept
    {
        const State st = (State) state.load (std::memory_order_acquire);
        if (st == State::Idle) return;

        if (st == State::Armed)
        {
            // First action of the session — latch start and begin recording.
            startBeats.store (beatsAbsolute, std::memory_order_release);
            state.store ((int) State::Recording, std::memory_order_release);
        }

        const double rel = beatsAbsolute - startBeats.load (std::memory_order_acquire);

        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) fifoBuffer[(size_t) s1] = { rel, status, d1, d2 };
        // If the FIFO is full (n1 == 0) the event is dropped rather than
        // blocking the audio thread. serviceFifo() must keep up.
        fifo.finishedWrite (n1);
    }

    //==========================================================================
    // PERSISTENCE  (message thread)
    //==========================================================================

    bool saveToFile (const juce::File& file) const
    {
        juce::ValueTree vt ("GrexSong");
        vt.setProperty ("version",    1,                  nullptr);
        vt.setProperty ("stylePath",  setup.stylePath,    nullptr);
        vt.setProperty ("tempoBpm",   setup.tempoBpm,     nullptr);
        vt.setProperty ("timeSigNum", setup.timeSigNum,   nullptr);
        vt.setProperty ("timeSigDen", setup.timeSigDen,   nullptr);
        vt.setProperty ("splitPoint", setup.splitPoint,   nullptr);
        vt.setProperty ("transpose",  setup.transpose,    nullptr);
        vt.setProperty ("soloPatch",  packInts (setup.soloPatch.data(),  8), nullptr);
        vt.setProperty ("stylePatch", packInts (setup.stylePatch.data(), 8), nullptr);
        vt.setProperty ("jumpsDest",  packInts (setup.jumpsDest.data(),  8), nullptr);
        vt.setProperty ("ccNumber",   packInts (setup.ccNumber.data(),   5), nullptr);

        juce::MemoryBlock mb;
        {
            juce::MemoryOutputStream os (mb, false);
            os.writeInt ((int) song.size());
            for (const auto& e : song)
            {
                os.writeDouble (e.beats);
                os.writeByte ((char) e.status);
                os.writeByte ((char) e.data1);
                os.writeByte ((char) e.data2);
            }
        }
        vt.setProperty ("events", juce::var (mb), nullptr);

        file.create();
        juce::FileOutputStream fos (file);
        if (! fos.openedOk()) return false;
        fos.setPosition (0);
        fos.truncate();
        vt.writeToStream (fos);
        return true;
    }

    /** Provided now so Phase-2 playback only needs to add a scheduler. */
    static bool loadFromFile (const juce::File& file,
                              Setup& outSetup,
                              std::vector<Event>& outEvents)
    {
        juce::FileInputStream fis (file);
        if (! fis.openedOk()) return false;

        auto vt = juce::ValueTree::readFromStream (fis);
        if (! vt.isValid() || ! vt.hasType ("GrexSong")) return false;

        outSetup = Setup{};
        outSetup.stylePath  = vt.getProperty ("stylePath").toString();
        outSetup.tempoBpm   = (double) vt.getProperty ("tempoBpm",   120.0);
        outSetup.timeSigNum = (int)    vt.getProperty ("timeSigNum", 4);
        outSetup.timeSigDen = (int)    vt.getProperty ("timeSigDen", 4);
        outSetup.splitPoint = (int)    vt.getProperty ("splitPoint", 60);
        outSetup.transpose  = (int)    vt.getProperty ("transpose",  0);
        unpackInts (vt.getProperty ("soloPatch") .toString(), outSetup.soloPatch .data(), 8);
        unpackInts (vt.getProperty ("stylePatch").toString(), outSetup.stylePatch.data(), 8);
        unpackInts (vt.getProperty ("jumpsDest") .toString(), outSetup.jumpsDest .data(), 8);
        unpackInts (vt.getProperty ("ccNumber")  .toString(), outSetup.ccNumber  .data(), 5);

        outEvents.clear();
        if (auto* mb = vt.getProperty ("events").getBinaryData())
        {
            juce::MemoryInputStream is (*mb, false);
            const int n = is.readInt();
            outEvents.reserve ((size_t) juce::jmax (0, n));
            for (int i = 0; i < n; ++i)
            {
                Event e;
                e.beats  = is.readDouble();
                e.status = (uint8_t) is.readByte();
                e.data1  = (uint8_t) is.readByte();
                e.data2  = (uint8_t) is.readByte();
                outEvents.push_back (e);
            }
        }
        return true;
    }

private:
    //==========================================================================
    static juce::String packInts (const int* v, int n)
    {
        juce::StringArray sa;
        for (int i = 0; i < n; ++i) sa.add (juce::String (v[i]));
        return sa.joinIntoString (" ");
    }

    static void unpackInts (const juce::String& s, int* out, int n)
    {
        juce::StringArray sa;
        sa.addTokens (s, " ", {});
        for (int i = 0; i < n; ++i)
            out[i] = (i < sa.size()) ? sa[i].getIntValue() : 0;
    }

    // Lock-free FIFO between the audio thread (writer) and message thread
    // (reader). 16k events comfortably covers a dense performance between
    // 30 Hz drains.
    static constexpr int kFifoCapacity = 16384;
    juce::AbstractFifo            fifo { kFifoCapacity };
    std::array<Event, kFifoCapacity> fifoBuffer {};

    std::atomic<int>    state      { (int) State::Idle };
    std::atomic<double> startBeats { 0.0 };

    Setup              setup;            // captured at arm() — message thread
    std::vector<Event> song;            // grown only by serviceFifo()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrexSongRecorder)
};
