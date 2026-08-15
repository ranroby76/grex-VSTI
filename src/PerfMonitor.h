
#pragma once
//==============================================================================
// PerfMonitor.h — CPU / RAM spike diagnostics.
//
// WHY THIS EXISTS
// ---------------
// "The plugin spikes" is not actionable on its own: a dropout can come from the
// audio thread genuinely running long, or from the MESSAGE thread doing
// something heavy (decoding samples, composing a kit) that stalls the process,
// or from an audio-thread call that LOOKS cheap but occasionally blocks.  These
// have completely different fixes, and guessing between them has cost us whole
// rounds before.  This records what actually happened so the answer comes from
// data.
//
// HOW IT IS SAFE ON THE AUDIO THREAD
// ----------------------------------
// The audio thread never allocates, never locks, never touches a file.  It only
//   * reads a high-resolution tick counter, and
//   * does relaxed atomic stores/adds into fixed, preallocated storage.
// Message-thread events are pushed under a short lock (never taken by the audio
// thread) and drained by a dedicated low-rate REPORTER THREAD, which also writes
// the file.  Deliberately a thread and not a juce::Timer: a Timer only fires
// while the message loop is responsive, and a STALLED MESSAGE THREAD is one of
// the very faults being hunted — the report would vanish exactly when it was
// most needed.  A plain thread keeps reporting through the stall.  (It also
// keeps this header dependent on juce_core alone, so including it from a DSP
// translation unit pulls in nothing new.)  If the event buffer overflows the
// monitor says so rather than blocking or dropping silently.
//
// NOTE ON THE SPIKE LINE
// ----------------------
// The stage figures on a SPIKE line are that BLOCK's own; the "stages avg" line
// above it is the window mean.  Compare them deliberately — they answer
// different questions.
//
// The key reading: if a spike's stages sum to far LESS than its total, the
// audio thread was BLOCKED rather than computing.  That is the signature of
// file I/O, a lock, or an allocation on the real-time thread.  Measured here on
// a real capture: a 14550 us block whose stages summed to 124 us — 0.85% of the
// block was DSP, the other 99% was a stall.  It was grexLog() opening and
// appending to a file from dispatchBlock, once per chord change; audioMark()
// exists so that class of diagnostic can keep reporting without causing the
// very fault it is meant to observe.
//
// WHAT IT MEASURES
// ----------------
//   AUDIO   per-block wall time vs the block's real-time budget, as a
//           histogram plus the worst block in the window, split into the three
//           stages that can each dominate (style bus, solo bus, Finisher), and
//           the voice census (total + peak) so a spike can be tied to
//           polyphony.
//   MESSAGE every heavy operation, timed and labelled: sample decode (with
//           bytes), kit compose, style load, preset swap, editor open/close.
//   RAM     live decoded-sample bytes and cache entries, with the peak, so
//           growth and eviction churn are both visible.
//
// READING THE LOG
// ---------------
// A spike in AUDIO with no MESSAGE event next to it is a real DSP cost - look
// at the voice census and the stage split.  A spike WITH a message event on the
// same second is the message thread stalling the process — look at what it was
// doing and how long it took.  Steadily climbing RAM with rising evictions is
// cache thrash, which shows up as repeated decodes of the same flag.
//
// Zero cost when disabled: setEnabled(false) makes every audio-thread entry a
// single relaxed atomic load and an early return.
//==============================================================================

#include <juce_core/juce_core.h>

#include <atomic>
#include <array>
#include <cmath>

namespace Betel
{
    class PerfMonitor : private juce::Thread
    {
    public:
        /** Things the AUDIO thread may report.  An enum rather than a string so
            the audio-thread path never builds one. */
        enum class AudioMarkId : uint8_t
        {
            ChordSwitch = 0,     // value = number of events dispatched
            CasmMiss,            // value = number of events with no CASM rule
            BoundaryRestore,     // value = slot restored to its setup voice
            VoiceSteal,          // value = channel that had to steal
            NumMarks
        };

        static PerfMonitor& get()
        {
            static PerfMonitor inst;
            return inst;
        }

        //----------------------------------------------------------------------
        // Control (message thread).
        //----------------------------------------------------------------------
        void start (const juce::File& file, int reportSeconds = 5)
        {
            logFile = file;
            logFile.replaceWithText (header());
            reportMs = juce::jmax (1, reportSeconds) * 1000;
            enabled.store (true, std::memory_order_relaxed);
            if (! isThreadRunning()) startThread();
        }

        void stop()
        {
            enabled.store (false, std::memory_order_relaxed);
            stopThread (2000);
        }

        bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

        /** Real-time budget for one block, so block times can be expressed as a
            percentage of it.  Call from prepareToPlay. */
        void prepare (double sampleRate, int blockSize)
        {
            const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            const int    bs = blockSize   > 0  ? blockSize   : 512;
            budgetUs.store (1.0e6 * (double) bs / sr, std::memory_order_relaxed);
            ticksPerUs = (double) juce::Time::getHighResolutionTicksPerSecond() / 1.0e6;
            event ("PREPARE", juce::String (sr, 0) + " Hz, " + juce::String (bs) + " smp, budget "
                              + juce::String (budgetUs.load(), 1) + " us");
        }

        //----------------------------------------------------------------------
        // AUDIO THREAD.  Allocation-free, lock-free, file-free.
        //----------------------------------------------------------------------
        int64_t tick() const noexcept { return juce::Time::getHighResolutionTicks(); }

        /** Close out one processBlock.  stageTicks are the three sub-stage
            durations (style / solo / finisher) in raw ticks; pass 0 for any
            stage that was not measured. */
        void endBlock (int64_t blockStartTicks,
                       int64_t styleTicks, int64_t soloTicks, int64_t finisherTicks,
                       int activeVoices) noexcept
        {
            if (! enabled.load (std::memory_order_relaxed)) return;

            const double us = (double) (tick() - blockStartTicks) / juce::jmax (1.0e-9, ticksPerUs);
            const double budget = budgetUs.load (std::memory_order_relaxed);
            const double load   = budget > 0.0 ? (us / budget) : 0.0;

            blocks   .fetch_add (1,  std::memory_order_relaxed);
            sumUs    .fetch_add ((int64_t) us, std::memory_order_relaxed);
            styleUs  .fetch_add ((int64_t) ((double) styleTicks    / ticksPerUs), std::memory_order_relaxed);
            soloUs   .fetch_add ((int64_t) ((double) soloTicks     / ticksPerUs), std::memory_order_relaxed);
            finUs    .fetch_add ((int64_t) ((double) finisherTicks / ticksPerUs), std::memory_order_relaxed);
            voiceSum .fetch_add (activeVoices, std::memory_order_relaxed);

            atomicMax (peakUs,     (int64_t) us);
            atomicMax (peakVoices, (int64_t) activeVoices);

            // Load histogram: <25%, <50%, <75%, <100%, >=100% of budget.
            const int bucket = load < 0.25 ? 0 : load < 0.50 ? 1
                             : load < 0.75 ? 2 : load < 1.00 ? 3 : 4;
            hist[(size_t) bucket].fetch_add (1, std::memory_order_relaxed);

            // A block over the spike threshold is recorded individually — the
            // report prints the worst few with their voice count, which is what
            // ties a spike to a cause.
            if (load >= kSpikeFraction)
            {
                const int slot = spikeCount.fetch_add (1, std::memory_order_relaxed);
                if (slot < kMaxSpikes)
                {
                    spikes[(size_t) slot].us       = (float) us;
                    spikes[(size_t) slot].loadPct  = (float) (load * 100.0);
                    spikes[(size_t) slot].voices   = activeVoices;
                    spikes[(size_t) slot].styleUs  = (float) ((double) styleTicks    / ticksPerUs);
                    spikes[(size_t) slot].soloUs   = (float) ((double) soloTicks     / ticksPerUs);
                    spikes[(size_t) slot].finUs    = (float) ((double) finisherTicks / ticksPerUs);
                }
            }
        }

        /** AUDIO-THREAD marker.  Records that a labelled thing happened, with
            an optional integer, into fixed preallocated storage — no string
            building, no allocation, no lock, no file.  The reporter thread
            formats and writes it.

            This exists so diagnostics that used to call grexLog() (which opens,
            writes and closes a file) from dispatchBlock can keep reporting
            without stalling the real-time thread.  Measured cost of the old
            path: a single 14.5 ms block, >99% of it outside the DSP. */
        void audioMark (AudioMarkId id, int value = 0) noexcept
        {
            if (! enabled.load (std::memory_order_relaxed)) return;
            const int slot = markCount.fetch_add (1, std::memory_order_relaxed);
            if (slot < kMaxMarks)
            {
                marks[(size_t) slot].id    = id;
                marks[(size_t) slot].value = value;
            }
            else
                marksLost.fetch_add (1, std::memory_order_relaxed);
        }

        //----------------------------------------------------------------------
        // MESSAGE THREAD — heavy operations and RAM accounting.
        //----------------------------------------------------------------------
        /** Timed scope for anything expensive: decode, kit compose, style load.
            Usage:  { PerfMonitor::Scoped s ("DECODE", "flag 25"); ...work... } */
        struct Scoped
        {
            Scoped (const char* cat, juce::String det = {})
                : category (cat), detail (std::move (det)),
                  t0 (juce::Time::getHighResolutionTicks()) {}

            ~Scoped()
            {
                auto& pm = PerfMonitor::get();
                if (! pm.isEnabled()) return;
                const double ms = 1000.0 * (double) (juce::Time::getHighResolutionTicks() - t0)
                                / (double) juce::Time::getHighResolutionTicksPerSecond();
                pm.event (category, detail + "  " + juce::String (ms, 2) + " ms");
                if (ms >= kSlowOpMs) pm.slowOps.fetch_add (1, std::memory_order_relaxed);
            }

            const char*  category;
            juce::String detail;
            int64_t      t0;
        };

        /** Free-form marker.  Safe from the message thread only. */
        void event (const char* category, const juce::String& detail)
        {
            if (! enabled.load (std::memory_order_relaxed)) return;

            // Short lock, message thread <-> reporter thread only.  The audio
            // thread never reaches here, so its path stays lock-free.
            const juce::ScopedLock sl (eventLock);
            if (pendingEvents.size() < kMaxEvents)
                pendingEvents.add (juce::String (category).paddedRight (' ', 10) + detail);
            else
                eventsLost.fetch_add (1, std::memory_order_relaxed);
        }

        /** Track live decoded-sample memory.  Positive on decode, negative on
            evict; the peak and the churn counters come out of this. */
        void addSampleBytes (int64_t delta, int entryDelta) noexcept
        {
            if (! enabled.load (std::memory_order_relaxed)) return;
            const int64_t now = sampleBytes.fetch_add (delta, std::memory_order_relaxed) + delta;
            cacheEntries.fetch_add (entryDelta, std::memory_order_relaxed);
            atomicMax (peakSampleBytes, now);
            if (delta > 0) decodes .fetch_add (1, std::memory_order_relaxed);
            if (delta < 0) evictions.fetch_add (1, std::memory_order_relaxed);
        }

    private:
        PerfMonitor() : juce::Thread ("GrexPerfMonitor") {}
        ~PerfMonitor() override { stopThread (2000); }

        static constexpr double kSpikeFraction = 0.50;   // >=50% of budget is worth recording
        static constexpr double kSlowOpMs      = 20.0;   // a message-thread op this long can stall audio
        static constexpr int    kMaxSpikes     = 16;
        static constexpr int    kMaxEvents     = 256;

        struct Spike { float us, loadPct, styleUs, soloUs, finUs; int voices; };
        struct Mark  { AudioMarkId id; int value; };

        static const char* markName (AudioMarkId id) noexcept
        {
            switch (id)
            {
                case AudioMarkId::ChordSwitch:     return "chordSwitch";
                case AudioMarkId::CasmMiss:        return "casmMiss";
                case AudioMarkId::BoundaryRestore: return "boundaryRestore";
                case AudioMarkId::VoiceSteal:      return "voiceSteal";
                default:                           return "?";
            }
        }

        static juce::String header()
        {
            return "GREX PERFORMANCE LOG\n"
                   "====================\n"
                   "AUDIO   avg/peak block time vs the real-time budget, the load histogram,\n"
                   "        and the worst blocks with their stage split and voice count.\n"
                   "MESSAGE heavy operations, timed.  An op >= 20 ms on the same line as an\n"
                   "        audio spike is the likely cause of that spike.\n"
                   "RAM     live decoded-sample bytes / cache entries, with peak and churn.\n"
                   "\n"
                   "Reading it: a spike with NO message event beside it is real DSP cost -\n"
                   "check the voice census and stage split.  A spike WITH one is the message\n"
                   "thread stalling the process.  Climbing RAM plus rising evictions is cache\n"
                   "thrash, visible as repeated decodes of the same flag.\n\n";
        }

        static void atomicMax (std::atomic<int64_t>& dst, int64_t v) noexcept
        {
            int64_t cur = dst.load (std::memory_order_relaxed);
            while (v > cur && ! dst.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                wait (reportMs);
                if (threadShouldExit()) break;
                flush();
            }
            flush();          // final report on shutdown
        }

        void flush()
        {
            const int64_t n = blocks.exchange (0, std::memory_order_relaxed);
            if (! enabled.load (std::memory_order_relaxed)) return;

            juce::String b;
            b << "[" << juce::Time::getCurrentTime().toString (false, true, true, true) << "]\n";

            const int64_t sum   = sumUs   .exchange (0, std::memory_order_relaxed);
            const int64_t peak  = peakUs  .exchange (0, std::memory_order_relaxed);
            const int64_t sSum  = styleUs .exchange (0, std::memory_order_relaxed);
            const int64_t oSum  = soloUs  .exchange (0, std::memory_order_relaxed);
            const int64_t fSum  = finUs   .exchange (0, std::memory_order_relaxed);
            const int64_t vSum  = voiceSum.exchange (0, std::memory_order_relaxed);
            const int64_t vPeak = peakVoices.exchange (0, std::memory_order_relaxed);
            const double  budget = budgetUs.load (std::memory_order_relaxed);

            if (n > 0)
            {
                const double avg = (double) sum / (double) n;
                b << "  AUDIO   blocks=" << juce::String (n)
                  << "  avg=" << juce::String (avg, 1) << "us (" << juce::String (100.0 * avg / juce::jmax (1.0, budget), 1) << "%)"
                  << "  peak=" << juce::String ((double) peak, 1) << "us (" << juce::String (100.0 * (double) peak / juce::jmax (1.0, budget), 1) << "%)\n";
                b << "          stages avg: style=" << juce::String ((double) sSum / (double) n, 1)
                  << "us  solo=" << juce::String ((double) oSum / (double) n, 1)
                  << "us  finisher=" << juce::String ((double) fSum / (double) n, 1) << "us\n";
                b << "          voices avg=" << juce::String ((double) vSum / (double) n, 1)
                  << "  peak=" << juce::String (vPeak) << "\n";

                b << "          load    <25%:" << juce::String (hist[0].exchange (0, std::memory_order_relaxed))
                  << "  <50%:" << juce::String (hist[1].exchange (0, std::memory_order_relaxed))
                  << "  <75%:" << juce::String (hist[2].exchange (0, std::memory_order_relaxed))
                  << "  <100%:" << juce::String (hist[3].exchange (0, std::memory_order_relaxed))
                  << "  OVER:" << juce::String (hist[4].exchange (0, std::memory_order_relaxed)) << "\n";
            }

            const int ns = juce::jmin (kMaxSpikes, spikeCount.exchange (0, std::memory_order_relaxed));
            for (int i = 0; i < ns; ++i)
            {
                const auto& s = spikes[(size_t) i];
                b << "  SPIKE   " << juce::String (s.us, 1) << "us (" << juce::String (s.loadPct, 0) << "% of budget)"
                  << "  voices=" << juce::String (s.voices)
                  << "  style=" << juce::String (s.styleUs, 1)
                  << "  solo="  << juce::String (s.soloUs, 1)
                  << "  fin="   << juce::String (s.finUs, 1) << "\n";
            }

            const int nm = juce::jmin (kMaxMarks, markCount.exchange (0, std::memory_order_relaxed));
            if (nm > 0)
            {
                // Tally rather than list: these fire per chord change, and a
                // count is what tells us whether they correlate with a spike.
                std::array<int, (size_t) AudioMarkId::NumMarks> tally {};
                for (int i = 0; i < nm; ++i)
                {
                    const auto idx = (size_t) marks[(size_t) i].id;
                    if (idx < tally.size()) ++tally[idx];
                }
                b << "  AUDMARK ";
                for (size_t i = 0; i < tally.size(); ++i)
                    if (tally[i] > 0)
                        b << markName ((AudioMarkId) i) << "=" << juce::String (tally[i]) << "  ";
                b << "\n";
            }
            if (const int lost = marksLost.exchange (0, std::memory_order_relaxed); lost > 0)
                b << "  AUDMARK (" << juce::String (lost) << " dropped - buffer full)\n";

            juce::StringArray drained;
            {
                const juce::ScopedLock sl (eventLock);
                drained.swapWith (pendingEvents);
            }
            for (const auto& e : drained)
                b << "  MSG     " << e << "\n";
            if (const int lost = eventsLost.exchange (0, std::memory_order_relaxed); lost > 0)
                b << "  MSG     (" << juce::String (lost) << " events dropped - ring full)\n";

            const int64_t bytes = sampleBytes.load (std::memory_order_relaxed);
            const int64_t pk    = peakSampleBytes.load (std::memory_order_relaxed);
            const int64_t dec   = decodes  .exchange (0, std::memory_order_relaxed);
            const int64_t ev    = evictions.exchange (0, std::memory_order_relaxed);
            const int64_t slow  = slowOps  .exchange (0, std::memory_order_relaxed);
            if (n > 0 || dec > 0 || ev > 0)
                b << "  RAM     samples=" << juce::String (bytes / 1024 / 1024) << " MB"
                  << "  peak=" << juce::String (pk / 1024 / 1024) << " MB"
                  << "  entries=" << juce::String (cacheEntries.load (std::memory_order_relaxed))
                  << "  decodes=" << juce::String (dec)
                  << "  evictions=" << juce::String (ev)
                  << "  slowOps=" << juce::String (slow) << "\n";

            if (b.isNotEmpty()) logFile.appendText (b);
        }

        juce::File logFile;
        std::atomic<bool>    enabled { false };
        std::atomic<double>  budgetUs { 10000.0 };
        double               ticksPerUs = 1.0;

        std::atomic<int64_t> blocks { 0 }, sumUs { 0 }, peakUs { 0 };
        std::atomic<int64_t> styleUs { 0 }, soloUs { 0 }, finUs { 0 };
        std::atomic<int64_t> voiceSum { 0 }, peakVoices { 0 };
        std::array<std::atomic<int64_t>, 5> hist {};

        static constexpr int kMaxMarks = 512;
        std::atomic<int>     markCount { 0 }, marksLost { 0 };
        std::array<Mark, kMaxMarks> marks {};

        std::atomic<int>     spikeCount { 0 };
        std::array<Spike, kMaxSpikes> spikes {};

        juce::CriticalSection eventLock;
        juce::StringArray     pendingEvents;          // guarded by eventLock
        std::atomic<int>      eventsLost { 0 };
        int                   reportMs = 5000;

        std::atomic<int64_t> sampleBytes { 0 }, peakSampleBytes { 0 };
        std::atomic<int64_t> cacheEntries { 0 }, decodes { 0 }, evictions { 0 }, slowOps { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerfMonitor)
    };
} // namespace Betel
