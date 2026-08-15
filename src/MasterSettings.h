#pragma once
//==============================================================================
// MasterSettings.h — the two settings that belong to the PLAYER, not the song.
//
//   fingeredChord    Fingered / 1 Finger chord detection.  Default: 1 FINGER.
//   tempoSynced      tempo FREE / SYNCED to the host.      Default: FREE.
//
// Kept in grex_master.xml under the plugin root, written the instant either one
// changes, and read once at startup.
//
// ── WHY THESE TWO CAME OUT OF THE SET ────────────────────────────────────────
//
// Everything else on the front panel describes the ARRANGEMENT and rightly
// travels in the set: the x2 / x1/2 speed, SINGLE / MULTI, the 1/2-1/4-1/8
// transition grid, FILL LENGTH, the semitone transpose.  Load a song and those
// should come back exactly as that song wants them.
//
// These two are different.  They describe HOW THE PLAYER PLAYS and how the
// plugin sits in the host — one is about which hand shape you use to name a
// chord, the other is about whether the DAW owns the clock.  Neither is a
// property of a song, and having a song silently change either one is how you
// end up playing Fingered while you believe you are in 1 Finger: every melodic
// part snaps onto plain triad tones because the detector resolved a plain
// triad, and nothing on screen looks wrong.
//
// So they are installation-wide, they follow the player across every style and
// every set, and the only thing that can change them is the player.
//
// ── SAVE-ON-CHANGE ───────────────────────────────────────────────────────────
//
// No SAVE button.  There is nothing to stage: each setting is a single bit the
// user just flipped deliberately, the file is two attributes, and a setting
// that has to be saved by hand is a setting that will be lost.  The write
// happens on the message thread at toggle speed, not anywhere near audio.
//
// ── THREAD RULES ─────────────────────────────────────────────────────────────
//
// Atomics.  Written from the UI on the message thread, read at startup and
// whenever something needs to know the current mode.  Never read per-sample —
// the engine reads the ChordZoneTracker and the transport, not this.
//==============================================================================

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <cmath>               // std::pow / std::abs — see getSoloBaseUnityGain

#include "GlobalMacros.h"      // GrexPaths — one root for every file we own

namespace Betel
{
    class MasterSettings
    {
    public:
        static MasterSettings& get()
        {
            static MasterSettings instance;
            return instance;
        }

        //----------------------------------------------------------------------
        // Defaults.  Both FALSE, i.e. 1 FINGER and FREE — the state a first
        // install comes up in and what DEFAULTS returns to.
        //----------------------------------------------------------------------
        static constexpr bool kDefaultFingeredChord = false;   // 1 FINGER
        static constexpr bool kDefaultTempoSynced   = false;   // FREE

        // PITCH BEND RANGE, in semitones, 1..12.  A setting of 2 means the
        // wheel travels -2..+2, which is the MIDI default and therefore the
        // default here: an install that has never touched the slider behaves
        // exactly as it did before the slider existed.
        //
        // It lives in MASTER, not in the set, because it describes the
        // PLAYER'S HARDWARE - how far this person's wheel should throw - and
        // that does not change because a different song was loaded.
        static constexpr int  kDefaultPitchBendRange = 2;
        static constexpr int  kMinPitchBendRange     = 1;
        static constexpr int  kMaxPitchBendRange     = 12;

        //----------------------------------------------------------------------
        // LOW VELOCITY RESPONSE, 0..100.  SOLO CHANNELS ONLY.
        //
        // 0 is off and is the default, so an install that never touches the
        // slider plays exactly as it did before it existed.
        //
        // WHAT IT COMPENSATES FOR is the KEYBOARD, not the instrument: most
        // controllers send a usable velocity from about 65 up and then fall away
        // steeply below it, so a soft passage that felt evenly played arrives as
        // a handful of notes that barely speak.  That is a property of the
        // player's hardware, which is exactly why it lives in MASTER beside the
        // pitch bend range rather than in the set - it does not change because a
        // different song was loaded.
        //
        // It is NOT the per-slot velCurve, which is a voicing decision about one
        // instrument and stays in the .ins.  This sits in front of that: the
        // controller is corrected first, then the instrument is voiced.
        //----------------------------------------------------------------------
        //----------------------------------------------------------------------
        // SPLIT POINT, in MIDI note numbers.
        //
        // Where the left hand stops feeding chord recognition and the right hand
        // starts playing solo voices.  60 (middle C) is the traditional default.
        //
        // MASTER, not the set, for the same reason as the two above: it is a
        // property of THIS PLAYER'S HANDS - where they sit on the keys - and it
        // should not move because a different song was loaded.  It was in the
        // project state only, so it came back on a project reload and was lost
        // on a fresh session.
        //----------------------------------------------------------------------
        static constexpr int  kDefaultSplitPoint = 60;
        static constexpr int  kMinSplitPoint     = 0;
        static constexpr int  kMaxSplitPoint     = 127;

        static constexpr int  kDefaultLowVelBoost = 0;
        static constexpr int  kMinLowVelBoost     = 0;
        static constexpr int  kMaxLowVelBoost     = 100;

        //----------------------------------------------------------------------
        // SOLO BUS BASE UNITY, in dB.  What the SOLO VOLUME fader's unity detent
        // (127) is worth, exactly as the crash and instrument base unities work.
        //
        // The right hand comes up quiet against the band, and the reason is
        // structural rather than a bad fader position: the STYLE bus carries
        // BOOST (+19.2 dB at its default) and MAKEUP, while the SOLO bus carries
        // neither.  So the two buses sit at unity meaning two different things,
        // and every set has to spend fader travel correcting the same offset.
        //
        // It belongs in MASTER, not in the set, for the same reason
        // kDefaultPitchBendRange does: it describes THIS INSTALL'S balance
        // between the two hands - a property of the rig and the sample library -
        // and it must not change because a different song was loaded.  Every set
        // then loads with the right hand already sitting correctly.
        //
        // 0 dB is the behaviour every existing install has today, so a master
        // file written before this existed comes up unchanged.
        //----------------------------------------------------------------------
        static constexpr float kDefaultSoloBaseUnityDb =   0.0f;
        static constexpr float kMinSoloBaseUnityDb     = -24.0f;
        static constexpr float kMaxSoloBaseUnityDb     =  24.0f;

        // ── Reads ─────────────────────────────────────────────────────────────
        bool isFingeredChord() const noexcept { return fingeredChord.load(); }
        bool isTempoSynced()   const noexcept { return tempoSynced  .load(); }
        int  getPitchBendRange() const noexcept { return pitchBendRange.load(); }
        int  getLowVelBoost()    const noexcept { return lowVelBoost.load(); }
        int  getSplitPoint()     const noexcept { return splitPoint.load(); }

        /** The slider position as the CURVE value Betel::VelCurve already
            speaks: 0..100 -> 0..+1.

            POSITIVE, and the sign is worth stating because the obvious guess is
            wrong.  VelCurve documents itself as "-1 soft .. +1 hard", which
            names the INSTRUMENT'S RESPONSE, not what happens to the number: the
            shape is x^gamma with gamma = 2.5^-curve, so a POSITIVE curve gives
            gamma < 1 and lifts everything below full scale.  A negative one
            steepens instead - the exact opposite of this control.

            Reusing VelCurve rather than inventing a second shape means the
            global and the per-slot control cannot disagree about what a curve
            value means. */
        float getLowVelCurve() const noexcept
        {
            return (float) juce::jlimit (kMinLowVelBoost, kMaxLowVelBoost,
                                         lowVelBoost.load())
                   / (float) kMaxLowVelBoost;
        }
        float getSoloBaseUnityDb() const noexcept { return soloBaseUnityDb.load(); }

        /** The same figure as a linear multiplier, for whoever renders it.

            std::pow rather than juce::Decibels on purpose: Decibels lives in
            juce_audio_basics, and this header is included by UI files that have
            no reason to pull an audio module in behind it.  Identical result. */
        float getSoloBaseUnityGain() const noexcept
        {
            const float dB = juce::jlimit (kMinSoloBaseUnityDb, kMaxSoloBaseUnityDb,
                                           soloBaseUnityDb.load());
            return std::pow (10.0f, dB / 20.0f);
        }

        // ── Writes.  Each one persists immediately ────────────────────────────
        void setFingeredChord (bool on)
        {
            if (fingeredChord.exchange (on) == on) return;   // no change, no write
            requestSave();
        }

        void setTempoSynced (bool on)
        {
            if (tempoSynced.exchange (on) == on) return;
            requestSave();
        }

        void setPitchBendRange (int semis)
        {
            const int v = juce::jlimit (kMinPitchBendRange, kMaxPitchBendRange, semis);
            if (pitchBendRange.exchange (v) == v) return;
            requestSave();
        }

        void setSplitPoint (int midiNote)
        {
            const int v = juce::jlimit (kMinSplitPoint, kMaxSplitPoint, midiNote);
            if (splitPoint.exchange (v) == v) return;   // no change, no write
            requestSave();
        }

        void setLowVelBoost (int amount)
        {
            const int v = juce::jlimit (kMinLowVelBoost, kMaxLowVelBoost, amount);
            if (lowVelBoost.exchange (v) == v) return;
            requestSave();
        }

        void setSoloBaseUnityDb (float dB)
        {
            const float v = juce::jlimit (kMinSoloBaseUnityDb, kMaxSoloBaseUnityDb, dB);
            if (std::abs (soloBaseUnityDb.exchange (v) - v) < 1.0e-4f) return;
            requestSave();
        }

        void resetToDefaults()
        {
            fingeredChord  .store (kDefaultFingeredChord);
            tempoSynced    .store (kDefaultTempoSynced);
            pitchBendRange .store (kDefaultPitchBendRange);
            lowVelBoost    .store (kDefaultLowVelBoost);
            splitPoint     .store (kDefaultSplitPoint);
            soloBaseUnityDb.store (kDefaultSoloBaseUnityDb);
            requestSave();
        }

        // ── Persistence ───────────────────────────────────────────────────────
        static juce::File masterFile() { return GrexPaths::master(); }

        //----------------------------------------------------------------------
        // SAVING IS DEFERRED, BECAUSE A SETTER CAN BE CALLED FROM THE AUDIO
        // THREAD.
        //
        // A MIDI CC assigned to SPLIT arrives inside processBlock's MIDI loop,
        // and a mod-wheel sweep is dozens of events per block.  Each one used to
        // run save() below: build a ValueTree, create a directory, serialise
        // XML, write a file - all on the audio thread, at control-change rate.
        // That is not a slow save, it is a dropout, and it is exactly the
        // "continuous CPU spikes" a mod wheel produced.
        //
        // So a setter now only raises a flag.  The message thread calls
        // flushIfDirty() and does the writing.  Coalescing is the point as much
        // as the thread is: a whole sweep costs ONE file write at the end
        // instead of one per event.
        //
        // requestSave() is audio-thread safe by construction - a single atomic
        // store, no allocation, no locks, no I/O.
        //----------------------------------------------------------------------
        void requestSave() noexcept { saveDirty.store (true, std::memory_order_release); }

        /** MESSAGE THREAD ONLY.  Writes if anything changed since the last call.
            Cheap to poll: one relaxed load when nothing is pending. */
        bool flushIfDirty() const
        {
            if (! saveDirty.load (std::memory_order_acquire)) return false;

            // Cleared BEFORE the write, not after: a change arriving mid-write
            // must leave the flag raised so the next tick picks it up, rather
            // than being cleared by the flush that did not include it.
            saveDirty.store (false, std::memory_order_release);
            return save();
        }

        bool save() const
        {
            juce::ValueTree t ("GrexMaster");
            t.setProperty ("fingeredChord", fingeredChord.load(), nullptr);
            t.setProperty ("tempoSynced",   tempoSynced  .load(), nullptr);
            t.setProperty ("pitchBendRange", pitchBendRange.load(), nullptr);
            t.setProperty ("lowVelBoost",    lowVelBoost.load(),    nullptr);
            t.setProperty ("splitPoint",     splitPoint.load(),     nullptr);
            t.setProperty ("soloBaseUnityDb", (double) soloBaseUnityDb.load(), nullptr);

            const auto f = masterFile();
            f.getParentDirectory().createDirectory();
            if (auto xml = t.createXml()) return xml->writeTo (f);
            return false;
        }

        /** Missing or malformed file leaves the defaults standing — the same
            contract GlobalMacros::loadFunkey follows.  Called once at startup,
            before the UI is seeded. */
        void load()
        {
            bool fing = kDefaultFingeredChord;
            bool sync = kDefaultTempoSynced;

            if (const auto xml = juce::XmlDocument::parse (masterFile()))
            {
                const auto t = juce::ValueTree::fromXml (*xml);
                if (t.isValid() && t.hasType ("GrexMaster"))
                {
                    fing = (bool) t.getProperty ("fingeredChord", fing);
                    sync = (bool) t.getProperty ("tempoSynced",   sync);
                    // Absent = a master file written before the slider existed,
                    // and absent means the MIDI default of 2 semitones.
                    // Absent in a file written before this existed -> the
                    // default, which is OFF.  An upgrade changes nothing until
                    // the slider is moved.
                    splitPoint.store (juce::jlimit (
                        kMinSplitPoint, kMaxSplitPoint,
                        (int) t.getProperty ("splitPoint", kDefaultSplitPoint)));

                    lowVelBoost.store (juce::jlimit (
                        kMinLowVelBoost, kMaxLowVelBoost,
                        (int) t.getProperty ("lowVelBoost", kDefaultLowVelBoost)));

                    pitchBendRange.store (juce::jlimit (
                        kMinPitchBendRange, kMaxPitchBendRange,
                        (int) t.getProperty ("pitchBendRange", kDefaultPitchBendRange)));

                    // Absent = written before the solo base existed, and absent
                    // means 0 dB, i.e. exactly what that install already sounds
                    // like.  Nobody's balance moves on upgrade.
                    soloBaseUnityDb.store (juce::jlimit (
                        kMinSoloBaseUnityDb, kMaxSoloBaseUnityDb,
                        (float) (double) t.getProperty ("soloBaseUnityDb",
                                                        (double) kDefaultSoloBaseUnityDb)));
                }
            }

            fingeredChord.store (fing);
            tempoSynced  .store (sync);
        }

    private:
        MasterSettings() = default;

        std::atomic<bool> fingeredChord { kDefaultFingeredChord };
        std::atomic<bool> tempoSynced   { kDefaultTempoSynced };
        std::atomic<int>  pitchBendRange { kDefaultPitchBendRange };
        std::atomic<int>  lowVelBoost     { kDefaultLowVelBoost };
        std::atomic<int>  splitPoint      { kDefaultSplitPoint };
        mutable std::atomic<bool> saveDirty { false };
        std::atomic<float> soloBaseUnityDb { kDefaultSoloBaseUnityDb };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterSettings)
    };
} // namespace Betel



