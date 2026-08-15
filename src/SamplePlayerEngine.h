#pragma once
//==============================================================================
// SamplePlayerEngine.h  —  top-level sampler engine for Betelgeuse Arranger.
//
// Owns 24 Channel objects:
//   indices  0..15  →  the 16 STYLE channels (CH. 1..16)
//   indices 16..23  →  the  8 SOLO  channels (CH. 17..24)
//
// Single shared BlobReader for the main library (melodic). Drum-kit blobs
// live in a separate DrumKitRegistry that the host wires in via
// setDrumKitRegistry(). When a drum-mode channel receives Program Change,
// programChangeDrum() looks up the kit by PC value and reloads the channel.
//==============================================================================

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <iterator>
#include <map>
#include <memory>
#include <vector>
#include <algorithm>
#include "Channel.h"
#include "BlobReader.h"
#include "DrumKitRegistry.h"
#include "FullKitMap.h"
#include "InstrumentPreset.h"   // .ins default presets (melodic + drum)
#include "SlotParamConvert.h"   // stashVoiceFor resolves a preset in this header

namespace Betel
{
    class SamplePlayerEngine
    {
    public:
        static constexpr int kNumStyleChannels = 16;
        static constexpr int kNumSoloChannels  = 8;
        static constexpr int kNumChannels      = kNumStyleChannels + kNumSoloChannels; // 24

        // Only the lower 8 style channels are ever reachable: the SFF style
        // channels (Ch9–16) all remap to engine 0–7 via StylePlayer's
        // mapSourceToEngineChannel, the keyboard feeds the solo block (16–23),
        // and no preset/param/FX setter ever addresses 8–15.  Those upper 8
        // style channels therefore stay at construction defaults (no content,
        // all FX off) and render pure silence, so the style-bus render skips
        // them.  kNumStyleChannels is left at 16 so the solo block keeps its
        // base index of 16 and no channel indices shift.
        static constexpr int kNumActiveStyleChannels = 8;

        SamplePlayerEngine();
        ~SamplePlayerEngine();

        // ---------------------------------------------------------------------
        // Lifecycle
        // ---------------------------------------------------------------------
        void prepareToPlay(double sampleRate, int blockSize);
        void releaseResources();

        void renderBlock(juce::AudioBuffer<float>& outBuffer, int numSamples, double hostBPM);

        // ---------------------------------------------------------------------
        // MIDI input
        // ---------------------------------------------------------------------
        // fromEditor marks a note played from the SOUNDS editor's piano strip.
        // Those notes are polyphonic even on a mono slot, are stolen last, and
        // are tracked on their own key-down tally — see Channel::noteOn.
        /** GLOBAL TRANSPOSE in semitones, pushed to every channel at once.
            Applied inside Channel::noteOn alongside the octave terms, so it
            shifts BOTH the style parts and the solo voices and cannot strand a
            held note.  Drum channels ignore it — see Channel::semitoneOffset. */
        void setGlobalTransposeSemis (int semis) noexcept;

        /** Spike diagnostics (see PerfMonitor.h).  Written by renderBlock on the
            audio thread and read by the same thread immediately after, so plain
            members are sufficient — no cross-thread access. */
        int64_t getLastStyleRenderTicks() const noexcept { return lastStyleTicks; }
        int64_t getLastSoloRenderTicks()  const noexcept { return lastSoloTicks;  }
        int     getActiveVoiceCount()     const noexcept { return lastVoiceCount; }

        void noteOn   (int channelIndex, int note, int velocity, bool fromEditor = false,
                       float extraGain = 1.0f);
        void noteOff  (int channelIndex, int note, bool fromEditor = false);
        bool retuneNote (int channelIndex, int oldNote, int newNote);
        void allNotesOff(int channelIndex);
        void allChannelsOff();
        void pitchBend(int channelIndex, float normalized);

        /** Hidden per-channel octave bias, added on top of the instrument's own
            OCTAVE slider at note-on.  Lets a part sit in the register the styles
            expect while its slider still reads 0 — see Channel::octaveBias. */
        void setChannelOctaveBias (int channelIndex, int octaves)
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return;
            channels[(size_t) channelIndex].setOctaveBias (juce::jlimit (-3, 3, octaves));
        }

        /** Notes currently sounding on a channel, as a 128-bit mask (4 x 32).
            Message-thread safe (relaxed atomics); used by the instrument editor's
            piano strip to light up what the style / solo MIDI is playing. */
        void getSoundingNotes (int channelIndex, uint32_t out[4]) const
        {
            for (int i = 0; i < 4; ++i) out[i] = 0u;
            if (channelIndex < 0 || channelIndex >= kNumChannels) return;
            channels[(size_t) channelIndex].getSoundingNotes (out);
        }

        /** The mapped key span of the instrument currently loaded on a channel
            (the lowest lokey / highest hikey it has samples for); 0..127 when
            the channel is empty.  Audio-thread safe.

            StylePlayer uses it for the MegaVoice articulation fallback: an
            articulation key is folded into this span so the SELECTED voice
            answers it with a real region at its natural keycenter, instead of
            Channel::findRegion() borrowing a region two octaves away and
            stretching it up to reach the note (the screech). */
        void getChannelKeyRange (int channelIndex, int& lo, int& hi) const
        {
            lo = 0; hi = 127;
            if (channelIndex < 0 || channelIndex >= kNumChannels) return;
            channels[(size_t) channelIndex].getMappedKeyRange (lo, hi);
        }

        /** Total octave shift a channel applies at note-on (instrument OCTAVE
            slider + hidden bias).  A caller that pre-folds a note needs it to
            predict where the note will actually land — see getChannelKeyRange. */
        int getChannelOctaveShift (int channelIndex) const
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return 0;
            return channels[(size_t) channelIndex].getOctaveShift();
        }

        void controlChange (int channelIndex, int cc, int value)
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return;
            const float linear = juce::jlimit (0, 127, value) / 127.0f;
            if (cc == 7)        channels[(size_t) channelIndex].channelVolume.store (linear);
            else if (cc == 11)  channels[(size_t) channelIndex].channelExpression.store (linear);
        }

        // ---------------------------------------------------------------------
        // Blob loading (main melodic library — message thread)
        // ---------------------------------------------------------------------
        bool loadBlob(const juce::File& blobFile, const juce::String& accessCode);
        bool isBlobLoaded() const { return blobLoaded.load(); }
        juce::File getBlobFile() const { return blobFile; }
        std::vector<juce::String> getBlobPresetNames() const;
        int  getBlobPresetCount() const;
        int  numBlobPresets() const { return getBlobPresetCount(); }

        void selectChannelPreset(int channelIndex, int presetIndex);

        // Manifest-pool program-change path:
        //  • preloadChannelPreset      : decode a preset into a channel's pool
        //    (message thread; safe to call repeatedly — it skips work if pooled).
        //  • selectChannelPooledPreset : switch a channel to a pooled preset with
        //    no decode (audio-thread safe).  No-op if it was never preloaded.
        void preloadChannelPreset(int channelIndex, int presetIndex);
        void selectChannelPooledPreset(int channelIndex, int presetIndex);

        /** True if `flag` has already been decoded into this channel's pool —
            i.e. a selectChannelPooledPreset(ch, flag) would actually switch.
            Used by StylePlayer's runtime-PC diagnostics. */
        bool isChannelInstrumentPooled (int channelIndex, int flag) const
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return false;
            return channels[(size_t) channelIndex].isInstrumentPooled (flag);
        }

        // ── Per-channel "what is actually loaded" state (UI mirror) ──────────
        /** Flag of the instrument currently published on the channel, -1 if
            none / drum kit.  Reflects EVERY selection path (style setup,
            runtime PCs through CASM routing, set restore, user picks). */
        int getChannelInstrumentFlag (int channelIndex) const
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return -1;
            return channels[(size_t) channelIndex].getCurrentInstrumentFlag();
        }

        /** True when the channel is in drum mode. */
        bool isChannelDrum (int channelIndex) const
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return false;
            return channels[(size_t) channelIndex].getDrumChannel();
        }

        /** Name/key of the kit loaded on a drum channel ("000", "the_lasts"
            merges excluded — this is the kit the last programChangeDrum
            resolved to), empty if none. */
        juce::String getChannelDrumKitName (int channelIndex) const
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return {};
            return channels[(size_t) channelIndex].getLoadedDrumKitName();
        }

        // ---------------------------------------------------------------------
        // Per-instrument sound library (Grex small-blob model)
        //
        // The melodic library is a folder of single-preset "NNN-Name.frb"
        // files; the leading number is the instrument FLAG.  Flags 0..127 are
        // GM-addressable (driven by style program changes); flags >= 200 are
        // "reference" sounds, only used when the user substitutes a slot and
        // saves it in the set.  Drum-kit blobs (*_kit.frb) are skipped here --
        // they are owned by DrumKitRegistry.
        //
        // selectChannelPreset / preloadChannelPreset now take a FLAG (not a
        // single-blob preset index): they resolve flag->file, open the blob,
        // and pool/publish its preset 0 under the flag.  selectChannelPooledPreset
        // republishes a pooled flag with no decode (audio-thread safe).
        // ---------------------------------------------------------------------
        //---------------------------------------------------------------------
        // USER SFZ OVERRIDE — one per channel, parallel to the blob library.
        //
        // A loaded SFZ REPLACES the channel's instrument and holds it: style
        // program changes are ignored for as long as it is on, which is the
        // whole point (a style would otherwise take the slot back on the very
        // next load).  Switch it off and the channel returns to whatever flag
        // the style last asked for.
        //
        // The SFZ reaches the engine as an ordinary PresetVoice, so it runs
        // through the same filter, envelopes, FX chain and mixer fader as a
        // .frb.  Nothing downstream knows the difference.
        //---------------------------------------------------------------------
        bool loadSfzOnChannel  (int channelIndex, const juce::File& sfzFile,
                                juce::String& errorOut, juce::String& nameOut);
        void clearSfzOnChannel (int channelIndex);

        /** SLEEP / WAKE - the switch, without losing either source.

            Both voices stay resident on the channel, so these are an atomic
            pointer swap.  Toggling A/B between the style's instrument and your
            own no longer costs a decode each way. */
        void sleepSfzOnChannel (int channelIndex);
        void wakeSfzOnChannel  (int channelIndex);

        /** Is an SFZ parked on this channel, whether or not it is the one
            currently sounding? */
        bool hasSfzVoice (int channelIndex) const
        {
            return channelIndex >= 0 && channelIndex < kNumChannels
                && channels[(size_t) channelIndex].hasSfzVoice();
        }

        bool isSfzActive (int channelIndex) const
        {
            return channelIndex >= 0 && channelIndex < kNumChannels
                && sfzActive[(size_t) channelIndex].load();
        }

        juce::String getSfzName (int channelIndex) const
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return {};
            const juce::ScopedLock sl (sfzLock);
            return sfzNames[(size_t) channelIndex];
        }

        juce::String getSfzPath (int channelIndex) const
        {
            if (channelIndex < 0 || channelIndex >= kNumChannels) return {};
            const juce::ScopedLock sl (sfzLock);
            return sfzPaths[(size_t) channelIndex];
        }

        //======================================================================
        // THE SOUND LIBRARY IS THREE PACKS IN THREE FOLDERS.
        //
        // Was one flat, NON-RECURSIVE scan of <root>/sounds.  Two things had to
        // change: the packs are separate folders now so each can be installed or
        // deleted on its own, and every pack sorts its sounds into CATEGORY
        // subfolders - which the old scan could not even see.
        //
        // WHICH FOLDER A FILE WAS FOUND IN DECIDES ITS PACK.  Not a number
        // range.  The old code partitioned GM from everything else with
        // `flag >= 200`, and with a second optional pack that test can no longer
        // tell WORLD from ORIENTAL - it would have to become two ranges that
        // every future pack has to squeeze between.  The folder already knows,
        // and it cannot drift out of step with the numbering.
        //
        // THE FLAG STAYS THE FILENAME'S LEADING DIGITS, because that flag is
        // what every saved set and every .ins/.sins stores.  It must stay
        // globally unique: two files with the same number, in any packs, are the
        // same sound as far as saved state is concerned.  Collisions are
        // reported rather than silently resolved - see getLibraryCollisions().
        //======================================================================
        enum SoundPack { PackGm = 0, PackWorld = 1, PackOriental = 2, kNumSoundPacks = 3 };

        void setSoundLibraryFolders (const juce::File& gmFolder,
                                     const juce::File& worldFolder,
                                     const juce::File& orientalFolder,
                                     const juce::String& accessCode);

        /** Which pack a flag came from, or PackGm if it is unknown. */
        int getInstrumentPack (int flag) const;

        /** The category subfolder a flag was found in ("" for a loose file). */
        juce::String getInstrumentCategory (int flag) const;

        /** Category names for a pack, in the order they should be shown.
            Empty when the pack is not installed. */
        const juce::StringArray& getPackCategories (int pack) const;

        /** Every sound in one category of one pack, ascending by flag. */
        std::vector<std::pair<int, juce::String>>
            getPackInstruments (int pack, const juce::String& category) const;

        /** Human-readable duplicate-flag report, empty when the library is
            clean.  Written to grex_sound_library.txt on every scan. */
        juce::String getLibraryCollisions() const { return libraryCollisions; }
        bool         hasInstrumentFlag  (int flag) const;
        juce::File   getInstrumentFile  (int flag) const;
        juce::String getInstrumentName  (int flag) const;
        std::vector<int> getInstrumentFlags() const;   // ascending
        int          getSoundLibraryCount() const;
        juce::File   getSoundLibraryFolder() const { return soundLibraryFolder; }

        // ---------------------------------------------------------------------
        // .ins default presets (instruments_presets folder).  When a flag has a
        // melodic .ins, selectChannelPreset loads its .frb AND re-applies the
        // saved SlotParams, so a program change / selector restores the exact
        // saved voice.  Drum presets are scanned too (used by the drum path).
        // ---------------------------------------------------------------------
        void setInstrumentPresetFolder (const juce::File& folder);

        /** One folder per installed pack, merged.  See InstrumentPresetIO::scanFolders. */
        void setInstrumentPresetFolders      (const std::vector<juce::File>& folders);
        void setStyleInstrumentPresetFolders (const std::vector<juce::File>& folders);

        /** style_instruments_presets — the STYLE half of the split (.sins).
            Scanned separately so the two sets can never contaminate each other;
            see the note in InstrumentPreset.h. */
        void setStyleInstrumentPresetFolder (const juce::File& folder);

        bool hasInstrumentPreset (int flag) const
        {
            return instrumentPresets.find (flag) != instrumentPresets.end();
        }
        bool hasStyleInstrumentPreset (int flag) const
        {
            return styleInstrumentPresets.find (flag) != styleInstrumentPresets.end();
        }

        /** Resolve the saved voice for `flag` on `channelIdx` and stash it on the
            channel, so a later runtime program change (which runs on the audio
            thread and must not touch the preset maps) can restore it.

            Called from every MESSAGE-THREAD path that puts a flag into the
            channel's pool.  Resolving here rather than at select time is what
            keeps the audio thread clear of instrumentPresets / styleInstrumentPresets. */
        //======================================================================
        // LIVE PRESET RELOAD.
        //
        // Re-read the preset governing `flag` and push it to EVERY channel that
        // currently has that instrument loaded — not just the one that was
        // edited.  The same piano can sit on CHORD1 and CHORD2 at once;
        // calibrating it on one and leaving the other on the old value would
        // make the two disagree until the next program change, which is exactly
        // what makes calibration by ear impossible.
        //
        // Resolution stays per-channel: a style channel takes the .sins, the
        // right hand takes the .ins.  So one save can legitimately move two
        // channels to two different results, and that is correct.
        //
        // Message thread.  The stash is refreshed too, so a later runtime
        // program change back to this flag restores the NEW voice.
        //======================================================================
        void reapplyMelodicPreset (int flag)
        {
            if (flag < 0) return;
            for (int ch = 0; ch < kNumChannels; ++ch)
            {
                auto& c = channels[(size_t) ch];
                if (c.getDrumChannel()) continue;
                if (c.getCurrentInstrumentFlag() != flag) continue;

                stashVoiceFor (ch, flag);

                if (const auto* preset = melodicPresetFor (ch, flag))
                {
                    if (! preset->gainOnly)
                        applyChannelParams (ch, slotParamsToChannelParams (preset->params));
                    setChannelInstrumentGainPercent (ch, preset->params.gainPercent,
                                                     preset->params.baseUnityDb);
                }
                else
                {
                    setChannelInstrumentGainPercent (ch, 100.0f, 0.0f);   // preset gone
                }
            }
        }

        /** Drum twin: every drum channel holding this kit takes the .drm again —
            rack, per-key params and trim.

            EVERY channel, not just the edited one.  A style routinely puts the
            same kit on DRUMS and PERC, and calibrating one while the other kept
            the old rack is exactly the split-brain the live reload exists to
            prevent.

            The pooled entry is dropped first: it is immutable by design, so
            without that the re-publish would hand back the kit as it was before
            the save and nothing would appear to have happened. */
        void reapplyDrumPreset (int kitFlag)
        {
            if (kitFlag < 0 || drumKitRegistry == nullptr) return;

            for (int ch = 0; ch < kNumChannels; ++ch)
            {
                auto& c = channels[(size_t) ch];
                if (! c.getDrumChannel()) continue;
                if (getChannelDrumKitName (ch).getIntValue() != kitFlag) continue;

                const int program = c.getActiveDrumPoolKey();
                if (program >= 0)
                {
                    c.invalidatePooledDrumKit (program);       // force a re-compose
                    auto kp = resolveDrumKitParams (program);  // picks up the new .drm
                    if (kp.hasMappedKeys())
                    {
                        c.preloadDrumKit (*drumKitRegistry, kp, program);
                        c.selectPooledDrumKit (program, /*syncName*/ true);
                    }
                }

                float pct = 100.0f, baseDb = 0.0f;
                if (auto it = drumPresets.find (kitFlag); it != drumPresets.end())
                {
                    pct    = it->second.params.gainPercent;
                    baseDb = it->second.params.baseUnityDb;
                }
                setChannelInstrumentGainPercent (ch, pct, baseDb);
            }
        }

        /** Re-resolve every pooled flag's saved voice.  A "Save as Default"
            rewrites a preset without touching the sample pool, so the stashes
            made earlier are stale — a runtime PC back to that flag would
            otherwise restore the voice as it was BEFORE the save, which reads
            exactly like the save not working. */
        void refreshPooledVoices()
        {
            for (int ch = 0; ch < kNumChannels; ++ch)
                for (int flag : channels[(size_t) ch].pooledFlags())
                    stashVoiceFor (ch, flag);
        }

        void stashVoiceFor (int channelIdx, int flag)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels || flag < 0) return;

            Channel::PooledVoice v;
            if (const auto* preset = melodicPresetFor (channelIdx, flag))
            {
                v.gainLinear = instrumentGainLinear (preset->params.gainPercent,
                                                     preset->params.baseUnityDb);
                if (! preset->gainOnly)
                {
                    v.params    = slotParamsToChannelParams (preset->params);
                    v.hasParams = true;
                }
            }
            channels[(size_t) channelIdx].stashPooledVoice (flag, v);
        }

        /** Set a channel's per-sound calibration trim, as a PERCENT OF UNITY
            (100 = unity, 0 = silence, 200 = twice unity).  The ONLY entry point:
            a sound load (below) and the editor's GAIN slider both come through
            here, and nothing else can move it.  Clamped to the range the slider
            offers so a corrupt preset cannot hand the mix an exploding channel. */
        void setChannelInstrumentGainPercent (int channelIdx, float gainPercent,
                                             float baseUnityDb = 0.0f)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;

            // REMEMBERED, not just applied.  The two halves arrive from different
            // places - the base from a calibration, the percent from the GAIN
            // slider - and a caller that only knows one of them needs to be able
            // to ask for the other instead of passing 0 and silently wiping it.
            chGain[(size_t) channelIdx].pct   .store (gainPercent);
            chGain[(size_t) channelIdx].baseDb.store (baseUnityDb);

            channels[(size_t) channelIdx].instrumentGain.store (
                instrumentGainLinear (gainPercent, baseUnityDb));
        }

        /** What the last call to the funnel above applied.  A caller moving one
            half reads the other from here rather than defaulting it to 0. */
        float getChannelInstrumentGainPercent (int channelIdx) const noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 100.0f;
            return chGain[(size_t) channelIdx].pct.load();
        }
        float getChannelBaseUnityDb (int channelIdx) const noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 0.0f;
            return chGain[(size_t) channelIdx].baseDb.load();
        }

        //=====================================================================
        // PER-KIT BASE UNITY, OWNED BY THE SET
        //
        // A calibration belongs to a KIT, and two kits are two calibrations.
        // The key makes that literal:
        //
        //     composed kit   its drum PC, 0..127
        //     sampled kit    kFullKitUnityBit | msb<<16 | lsb<<8 | pc
        //
        // so they can never collide.  They used to, badly: the dialog resolved a
        // drum channel's kit with getChannelDrumKitName().getIntValue(), and a
        // sampled kit's name is a LABEL - "Pop Latin Kit".getIntValue() is 0 - so
        // calibrating any sampled kit wrote itself into 000 Standard's slot and
        // read Standard's value back.  Every sampled kit shared one number, and
        // that number belonged to a composed kit.
        //
        // Int-keyed, because applyDrumPresetGain reads this from the runtime PC
        // path on the AUDIO thread: a find on a map<int,float> allocates nothing,
        // which a string key would not manage.  Written from the message thread
        // only (a set load, or the BASE UNITY dialog) - the same exposure the
        // drumPresets map beside it already has.
        //=====================================================================
        static constexpr int kFullKitUnityBit = 1 << 24;

        static int fullKitUnityKey (int msb, int lsb, int pc) noexcept
        {
            return kFullKitUnityBit | ((msb & 0xFF) << 16)
                                    | ((lsb & 0xFF) << 8) | (pc & 0xFF);
        }

        void setKitUnityDb (int unityKey, float db)
        {
            if (unityKey < 0) return;
            kitUnityDb[unityKey] = juce::jlimit (-40.0f, 40.0f, db);
        }

        bool getKitUnityDb (int unityKey, float& out) const
        {
            auto it = kitUnityDb.find (unityKey);
            if (it == kitUnityDb.end()) return false;
            out = it->second;
            return true;
        }

        void clearKitUnity() { kitUnityDb.clear(); }
        const std::map<int, float>& allKitUnity() const noexcept { return kitUnityDb; }

        /** A display name for a unity key: the sampled kit's label, or the
            composed kit the PC actually resolves to - nearest-family fallback
            included, so a PC with no explicit mapping still names the kit that
            IS playing rather than nothing at all.

            This is the answer the UI mirror needs.  A channel's loadedDrumKitName
            cannot supply it: the runtime PC path publishes with syncName false
            because it runs on the AUDIO thread and cannot write a juce::String,
            so after a mid-song kit change the name on the channel is the kit
            BEFORE it. */
        juce::String kitNameForUnityKey (int unityKey) const
        {
            if (unityKey < 0) return {};

            if ((unityKey & kFullKitUnityBit) != 0)
            {
                if (const auto* e = fullKits.find ((unityKey >> 16) & 0xFF,
                                                   (unityKey >>  8) & 0xFF,
                                                    unityKey        & 0xFF))
                    return FullKitMap::prettyLabel (*e);
                return {};
            }

            auto it = drumKitPCMap.find (unityKey);
            return (it != drumKitPCMap.end()) ? it->second
                                              : nearestDrumFamily (unityKey);
        }

        /** Which kit this channel is holding, as a unity key.  -1 when nothing
            identifiable has been published on it. */
        int kitUnityKeyForChannel (int channelIdx) const noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return -1;
            return currentKitUnityKey[(size_t) channelIdx].v.load();
        }

        /** Record which kit a drum channel now holds and push that kit's saved
            base onto it, KEEPING the trim percent the load just decided.

            No set value for this kit means the set has no opinion, and whatever
            the load path applied stands. */
        void applyKitUnityToChannel (int channelIdx, int unityKey)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            currentKitUnityKey[(size_t) channelIdx].v.store (unityKey);

            float db = 0.0f;
            if (! getKitUnityDb (unityKey, db)) return;

            setChannelInstrumentGainPercent (channelIdx,
                                             chGain[(size_t) channelIdx].pct.load(), db);
        }

        /** The per-sound calibration a preset would apply to `flag` on
            `channelIdx`, as a LINEAR multiplier (1.0 when no preset governs it).

            The style-load estimators need this.  slotLoadGain, the loudness
            makeup and the rhythm ceiling all used to measure a mix built purely
            from the style's CC 7 — which was the whole mix, until per-sound
            calibration arrived.  Now a user can move an instrument +/-12 dB and
            those estimators would still be reasoning about the level it had
            before he touched it, so every judgement they make drifts further
            from what is actually heard the more calibration work gets done. */
        float presetGainLinearFor (int channelIdx, int flag) const
        {
            // A drum channel's flag is a KIT number and would otherwise collide
            // with the melodic flag of the same value.  Kit calibration reaches
            // the channel through applyDrumPresetGain, not through here.
            if (channelIdx >= 0 && channelIdx < kNumChannels
                && channels[(size_t) channelIdx].getDrumChannel())
                return 1.0f;

            if (const auto* p = melodicPresetFor (channelIdx, flag))
                return instrumentGainLinear (p->params.gainPercent, p->params.baseUnityDb);
            return 1.0f;
        }

        /** base x trim, the one place the two combine.  100 % always lands
            exactly on the base, whatever the base is. */
        static float instrumentGainLinear (float gainPercent, float baseUnityDb) noexcept
        {
            const float base = juce::Decibels::decibelsToGain (
                                   juce::jlimit (-40.0f, 40.0f, baseUnityDb));
            return base * juce::jlimit (0.0f, 200.0f, gainPercent) * 0.01f;
        }

        /** Diagnostic: is anything mapped at `note` on this channel? */
        bool channelHasNote (int channelIdx, int note) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            return channels[(size_t) channelIdx].hasRegionForNote (note);
        }

        /** Diagnostic: the kit name a drum channel currently holds. */
        juce::String channelDrumKit (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return {};
            return channels[(size_t) channelIdx].getLoadedDrumKitName();
        }

        /** Copy the SlotParams of the preset governing `flag` on `channelIdx`
            into `out`, or return false when no preset governs it.

            This exists because the EDITOR needs to know what the engine was
            actually loaded with.  selectChannelPreset applies an .ins / .sins
            straight to the channel, and the editor's own slot snapshot knows
            nothing about it — so without this the editor shows stale values for
            a sound the style loaded, and the first knob move pushes those stale
            values back over the preset. */
        /** The .drm governing this kit number, if any.  The melodic lookups
            deliberately refuse drum channels, so the base-unity dialog needs its
            own way in — without this it read a drum slot through the melodic
            maps, found nothing, and showed 0 for a value that was on disk and
            audibly in force. */
        bool drumPresetParamsFor (int kitFlag, SlotParams& out) const
        {
            if (auto it = drumPresets.find (kitFlag); it != drumPresets.end())
            { out = it->second.params; return true; }
            return false;
        }

        /** Like presetParamsFor but WITHOUT the gain-only filter — the base-unity
            dialog has to see a calibration file, which is exactly what the
            editor-facing query is designed to hide. */
        bool presetParamsForAnyScope (int channelIdx, int flag, SlotParams& out) const
        {
            if (const auto* p = melodicPresetFor (channelIdx, flag)) { out = p->params; return true; }
            return false;
        }

        bool presetParamsFor (int channelIdx, int flag, SlotParams& out) const
        {
            // A DRUM channel is never governed by a melodic preset.  Its flag is
            // a kit number, which would otherwise collide with a melodic flag of
            // the same value and report, say, the grand piano as governing the
            // DRUMS slot.
            if (channelIdx >= 0 && channelIdx < kNumChannels
                && channels[(size_t) channelIdx].getDrumChannel())
                return false;

            const auto* p = melodicPresetFor (channelIdx, flag);
            // A gain-only preset governs the trim, not the voice — so it must
            // not present itself as a saved voice to the editor, nor suppress
            // the factory calibration for that slot.
            if (p == nullptr || p->gainOnly) return false;

            out = p->params;
            return true;
        }

        /** True for the style section (0..15), false for the right hand
            (16..23).  The ONE thing that decides which preset set a load reads,
            and it is already available everywhere a load happens. */
        static bool isStyleChannel (int channelIdx) noexcept
        {
            return channelIdx < kNumStyleChannels;
        }

        /** The melodic preset governing `flag` on `channelIdx`, or nullptr.

            ── .sins IS CANCELLED ───────────────────────────────────────────────

            Style channels used to prefer a .sins from style_instruments_presets
            and fall back to the .ins.  That file existed to calibrate a sound
            differently for arrangements than for the right hand — which is
            exactly the job the SET does now, per style, instead of once per
            instrument for the whole library.

            So every melodic channel reads the .ins, and for a STYLE channel that
            is only the baseline: the set's <StyleSlots> block is applied after
            the style load and is the final word.  Solo channels are unaffected —
            the .ins remains their sole authority.

            style_instruments_presets is dead.  It is still scanned (harmlessly)
            so an install that has one does not error; nothing consults it. */
        const InstrumentPreset* melodicPresetFor (int channelIdx, int flag) const
        {
            juce::ignoreUnused (channelIdx);
            if (auto it = instrumentPresets.find (flag); it != instrumentPresets.end())
                return &it->second;

            return nullptr;
        }

        // ---------------------------------------------------------------------
        // Channel access
        // ---------------------------------------------------------------------
        Channel&       getChannel(int idx)       { return channels[(size_t) idx]; }
        const Channel& getChannel(int idx) const { return channels[(size_t) idx]; }

        void setDrumChannel(int idx, bool b)
        {
            if (idx >= 0 && idx < kNumChannels) channels[(size_t) idx].setDrumChannel(b);
        }

        // Global "bypass instrument audio chain" toggle.  When set, only the
        // amp ADSR + channel volume survive the audio path per channel —
        // filter, amp LFO, pitch ENV/LFO, velocity, channel pan, clicks, and
        // every post-mix stage (5-band EQ, Chorus, Wah, Phaser, Delay,
        // Reverb, drum FX bus) are all bypassed.  Channel volume stays
        // connected so the mixer faders (and CC 7) keep balancing the
        // instruments against each other.  Reads on the audio thread are
        // lock-free atomic loads.  See chainBypassed in Channel.h for the
        // precise list of what survives.
        void setBypassInstrumentChain (bool b) noexcept
        {
            for (int i = 0; i < kNumChannels; ++i)
                channels[(size_t) i].chainBypassed.store (b);
        }
        bool getBypassInstrumentChain() const noexcept
        {
            // All channels are kept in sync; reading channel 0 is canonical.
            return channels[0].chainBypassed.load();
        }

        // ---------------------------------------------------------------------
        // Per-channel synth parameter push (message thread)
        // ---------------------------------------------------------------------
        /** Pitch-bend range, in semitones, for one channel. */
        void setChannelPitchBendRange (int channelIdx, float semitones) noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].pitchBendRange.store (
                juce::jlimit (1.0f, 12.0f, semitones));
        }

        /** GLOBAL low-velocity lift for one channel.  Same shape of call as the
            pitch bend range above, and pushed to the same set of channels. */
        /** IGNORE PRESET CHANGES for one channel - see Channel::setIgnorePresetParams. */
        void setChannelIgnorePresetParams (int channelIdx, bool ignore) noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setIgnorePresetParams (ignore);
        }

        void setChannelGlobalVelCurve (int channelIdx, float curve) noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setGlobalVelCurve (curve);
        }

        /** The SWEETENER's own route into a channel.

            Separate from applyChannelParams because the sweetener must survive
            an instrument change: selectChannelPreset resets ChannelParams to
            defaults for any instrument with no saved voice, which would switch
            the block off on every style program change. */
        void setChannelSweetener (int channelIdx, const SweetenerParams& sw)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].applySweetenerParams (sw);
        }

        /** What the SWEETENER is doing right now on one channel, in dB of gain
            reduction per stage.  Fills gr4 as { SOFTEN, PEAK, TAME, ROUND };
            0 means that stage is not moving the signal.

            SweetenerFx has published these since the day it shipped and nothing
            ever read them, so the block was being tuned entirely by ear with no
            feedback - which is the hardest possible way to set a threshold and
            a ratio.  Read from the UI timer; every value behind it is an atomic
            the audio thread stores once per block, so this is a plain read with
            no lock and no cost to the render. */
        void getChannelSweetenerGr (int channelIdx, float* gr4) const
        {
            if (gr4 == nullptr) return;

            gr4[0] = gr4[1] = gr4[2] = gr4[3] = 0.0f;
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;

            const auto& c = channels[(size_t) channelIdx];
            gr4[0] = c.getSweetSoftenGrDb();
            gr4[1] = c.getSweetPeakGrDb();
            gr4[2] = c.getSweetTameGrDb();
            gr4[3] = c.getSweetRoundGrDb();
        }

        void applyChannelParams (int channelIdx, const Channel::ChannelParams& p)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].applyParams (p);
        }

        /** THE TWO-HANDLE BAND FILTER, on its own route.

            Separate from applyChannelParams for the same reason the sweetener
            and the calibration trim are: this one must SURVIVE a program
            change, and applyChannelParams is what a program change calls. */
        void setChannelBandFilter (int channelIdx, float hpHz, float lpHz)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setBandFilterHz (hpHz, lpHz);
        }

        float getChannelBandFilterHpHz (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 20.0f;
            return channels[(size_t) channelIdx].getBandFilterHpHz();
        }

        float getChannelBandFilterLpHz (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 20000.0f;
            return channels[(size_t) channelIdx].getBandFilterLpHz();
        }

        // =====================================================================
        // Drum-kit support
        // =====================================================================

        /** Wire a DrumKitRegistry instance. Non-owning — caller must keep the
            registry alive for as long as it's set here. Pass nullptr to
            detach.  Setting a registry enables programChangeDrum() and
            applyDrumKitToChannel(). */
        void setDrumKitRegistry (DrumKitRegistry* r) { drumKitRegistry = r; }
        const DrumKitRegistry* getDrumKitRegistry() const { return drumKitRegistry; }
        DrumKitRegistry*       getDrumKitRegistry()       { return drumKitRegistry; }

        /** Compose a hand-edited DrumKitParams onto a channel.  Used by the
            DrumsPopup when the user picks "load kit" or swaps an element.
            Message thread only (samples are decoded synchronously). */
        void applyDrumKitToChannel (int channelIdx, const DrumKitParams& kit)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            if (drumKitRegistry == nullptr) return;
            channels[(size_t) channelIdx].loadDrumKit (*drumKitRegistry, kit);
            clearFullKitOnChannel (channelIdx);   // a composed kit now holds it
            applyKitUnityToChannel (channelIdx, kit.lastLoadedKit.getIntValue());
        }

        /** Live per-key edit (slider drag in the Kitton popup).  Updates
            atomics only — no allocation, RT-safe. */
        void setDrumKeyParams (int channelIdx, int midiKey, const DrumElementParams& p)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setDrumKeyParams (midiKey, p);
        }

        /** Push the kit-wide FX bus params (10-band EQ + Sat + Comp + Reverb +
            Delay) onto a drum channel. Atomics only, RT-safe. */
        void applyDrumKitFx (int channelIdx, const DrumKitFxParams& fx)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].applyDrumKitFx (fx);
        }

        /** Live per-key FX-send update.  RT-safe atomic write. */
        void setDrumKeyFxSend (int channelIdx, int midiKey, float sendNorm)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setDrumKeyFxSend (midiKey, sendNorm);
        }

        /** Resolve a drum Program-Change value to a fully-specified
            DrumKitParams — the same PC->kit-name mapping, .drm default-preset
            override, global-component merge and nearest-family fallback the old
            programChangeDrum did inline.  Returns a kit with no mapped keys if
            nothing playable resolves (callers treat that as "not a drum kit").
            Message thread (touches juce::String); does NOT decode samples. */
        DrumKitParams resolveDrumKitParams (int program)
        {
            DrumKitParams kp;
            if (drumKitRegistry == nullptr) return kp;

            // Exact match for one of the nine sampled kits, else collapse any
            // Genos/Tyros extended PC onto the closest sampled family so the
            // style still plays drums instead of going silent.
            auto it = drumKitPCMap.find (program);
            const juce::String kitName = (it != drumKitPCMap.end())
                                       ? it->second
                                       : nearestDrumFamily (program);

            // .drm default-preset override: whole kit (per-key roles + params +
            // FX) in one step.  A default ".drm" marker has no mapped keys, so it
            // falls through to the registry build below (never silent).
            // ── The saved .drm, in two independent halves ─────────────────────
            //
            // A preset can carry a KEY MAP and a RACK, and they have to be
            // honoured separately.  The old test demanded mapped keys before it
            // would use ANY of it, which threw the rack away for every preset
            // saved from the drum editor: the editor's DrumKitParams holds the
            // user's per-key overrides, not the composed key map (the engine
            // composes that from the registry and never hands it back), so a
            // "SAVE AS DEFAULT" writes 128 unmapped keys with a fully-populated
            // <Fx>.  hasMappedKeys() was false, the whole preset was skipped,
            // and an EQ cut the file plainly contained never reached the kit.
            //
            // So: a preset WITH mapped keys still wins outright, and one without
            // contributes its rack to the freshly composed kit.
            const DrumKitParams* savedKit = nullptr;
            if (auto pit = drumPresets.find (kitName.getIntValue()); pit != drumPresets.end())
            {
                savedKit = &pit->second.params.drumKit;
                if (savedKit->hasMappedKeys()) return *savedKit;
            }

            auto handles = drumKitRegistry->getElementsForKit (kitName);

            // Merge the GLOBAL components (low_kick -> note 35, the_lasts ->
            // notes above 59) into every kit, regardless of program change.
            //
            // the_lasts is the shared upper-percussion blob.  We keep ONLY its
            // first 18 notes (the 18 lowest keys it maps) and drop the rest, for
            // every kit — the higher keys were unwanted extra percussion.  "First"
            // = lowest-key-first, computed from whatever the blob actually maps so
            // it holds no matter where those keys start.  low_kick and the per-kit
            // elements are untouched.
            for (const auto& gName : drumKitRegistry->getGlobalComponentNames())
            {
                auto g = drumKitRegistry->getElementsForKit (gName);

                if (gName == kTheLastsName && (int) g.size() > kTheLastsMaxNotes)
                {
                    // Sort by key ascending, then keep only the lowest N distinct
                    // keys' worth of elements (a key can hold >1 velocity layer,
                    // and all layers of a kept key are kept).
                    std::sort (g.begin(), g.end(),
                               [] (const auto& a, const auto& b) { return a.midiKey < b.midiKey; });

                    std::vector<int> keptKeys;
                    keptKeys.reserve (kTheLastsMaxNotes);
                    std::vector<DrumKitRegistry::ElementHandle> filtered;
                    filtered.reserve (g.size());
                    for (const auto& e : g)
                    {
                        const bool alreadyKept =
                            std::find (keptKeys.begin(), keptKeys.end(), e.midiKey) != keptKeys.end();
                        if (! alreadyKept)
                        {
                            if ((int) keptKeys.size() >= kTheLastsMaxNotes) continue;   // past the 18th key
                            keptKeys.push_back (e.midiKey);
                        }
                        filtered.push_back (e);
                    }
                    g.swap (filtered);
                }

                handles.insert (handles.end(), g.begin(), g.end());
            }

            if (handles.empty()) return kp;   // no mapped keys -> not a drum kit

            kp.lastLoadedKit = kitName;
            for (const auto& h : handles)
            {
                if (h.midiKey < 0 || h.midiKey >= 128) continue;
                auto& entry = kp.keys[(size_t) h.midiKey];
                // Use the element's OWN kit name (per-kit "000" or a global like
                // "the_lasts") so the channel can re-find it in the registry.
                entry.sourceKit    = h.kitName;
                entry.sourceRoleId = h.roleId;
                // Other per-key fields keep DrumElementParams defaults (neutral).
            }

            // ── Per-kit gain trims ───────────────────────────────────────────
            // BRUSH kit: its snare (note 38) sits far too hot against the rest of
            // the kit at unity, so seed the key's GAIN slider at 20 instead of the
            // neutral 50.  The slider is 0..100 with 50 = unity (DrumsPopup's
            // Map::sliderToGain is v * 0.02), so 20 -> 0.4 linear.
            //
            // This is only a DEFAULT: it seeds the editor's slider, the user can
            // still move it, and a saved .drm preset for this kit overrides the
            // whole thing above (the early-return) — so nothing here fights a
            // deliberate SAVE AS DEFAULT.
            if (kitName == kBrushKitName && kp.keys[(size_t) kSnareKey].sourceRoleId != 0)
                kp.keys[(size_t) kSnareKey].gain = 0.4f;   // slider 20

            // The rack from a saved preset that had no key map of its own.  Last,
            // so a deliberate SAVE AS DEFAULT outranks every default above it —
            // which is what the comment on the Brush trim already promised.
            if (savedKit != nullptr) kp.fx = savedKit->fx;

            return kp;
        }

        //---------------------------------------------------------------------
        // FULL SAMPLED KITS
        //
        // A kit folder under sounds/drums holds one .frb containing a whole
        // sampled kit.  It bypasses composeDrumKit entirely - no components, no
        // shared metal, no XG substitution - because a sampled Arabic kit brings
        // its own everything and mixing the two would be neither.
        //
        // FullKitMap decides IF this should happen; the engine only carries it
        // out.  Nothing falls back into this path: see the policy note there.
        //---------------------------------------------------------------------
        /** "msb/lsb/pc" of the sampled kit on this channel, or "" if composed. */
        juce::String getFullKitOnChannel (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return {};
            if (! fullKitLive[(size_t) channelIdx].load()) return {};
            return fullKitOnChannel[(size_t) channelIdx];
        }

        /** The loaded style plays this slot as chords - see
            Channel::setStyleForcedPoly.  Message thread, at style load. */
        void setChannelStyleForcedPoly (int channelIdx, bool forced) noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setStyleForcedPoly (forced);
        }

        /** Is this channel holding a SAMPLED kit right now?

            An atomic bool read and nothing else, so the AUDIO thread can ask it -
            getFullKitOnChannel returns a juce::String by value and allocates. */
        bool isFullKitOnChannel (int channelIdx) const noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            return fullKitLive[(size_t) channelIdx].load();
        }

        /** THE CHANNEL NO LONGER HOLDS A SAMPLED KIT.

            Called from every path that publishes a COMPOSED kit over the top of
            one.  Until this existed the marker was written and never cleared, so
            a channel that had once held a sampled kit reported it for ever: pick
            a GM kit afterwards and the grid lit BOTH - the new GM cell and the
            stale sampled one.

            Only the atomic flag is touched, never the string.  programChangeDrum
            is one of the callers and it runs on the AUDIO thread, where clearing
            a juce::String could free its shared buffer. */
        void clearFullKitOnChannel (int channelIdx) noexcept
        {
            if (channelIdx >= 0 && channelIdx < kNumChannels)
                fullKitLive[(size_t) channelIdx].store (false);
        }

        FullKitMap& getFullKitMap() noexcept { return fullKits; }
        const FullKitMap& getFullKitMap() const noexcept { return fullKits; }

        /** Load a full sampled kit onto a channel, whole.  Returns false if the
            folder has no readable blob, so the caller can fall through to the
            composed path rather than leaving the slot silent. */
        bool loadFullKitOnChannel (int channelIdx, int msb, int lsb, int pc,
                                   const juce::String& accessCode)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;

            // ONE DECODE PER KIT, KEYED ON THE KIT.
            //
            // This used to be channels[ch].loadPreset (reader, 0), and that is
            // where the "second unique kit does not switch" bug lived.  Every
            // sampled kit is preset 0 OF ITS OWN .frb, so the channel's pool -
            // which is keyed by the in-blob preset index - saw the same key 0
            // for all of them.  The first kit was decoded and stored under 0;
            // the second hit preloadPreset's "already pooled" early return and
            // republished the FIRST kit's samples.  The grid lit the new cell,
            // setLoadedDrumKitName wrote the new name, fullKitOnChannel updated
            // - everything reported the switch except the sound.
            //
            // The same collision hit STYLE loads, not just the user's clicks: a
            // style asking for a sampled kit on a channel that already held a
            // different one got the old kit back.
            //
            // getOrDecodeFullKit keys the cache on msb/lsb/pc instead, so the
            // key describes the KIT rather than its position inside a file, and
            // the decode is shared - DRUMS and PERC on the same sampled kit cost
            // one decode between them.
            auto pv = getOrDecodeFullKit (msb, lsb, pc, accessCode);
            if (pv == nullptr) return false;

            channels[(size_t) channelIdx].adoptFullKit (pv);
            channels[(size_t) channelIdx].setDrumChannel (true);

            // Tell the UI what it is holding.  adoptFullKit publishes a voice and
            // nothing more, so without this the sampled kit plays correctly and
            // shows up nowhere - no name in the editor, no lit cell in the grid.
            if (const auto* e = fullKits.find (msb, lsb, pc))
                channels[(size_t) channelIdx].setLoadedDrumKitName (FullKitMap::prettyLabel (*e));

            fullKitOnChannel[(size_t) channelIdx] = key3 (msb, lsb, pc);
            fullKitLive[(size_t) channelIdx].store (true);

            // Its OWN calibration, under its own key - never the composed kit's.
            // A sampled kit has no .drm and no drum PC, so this is the only place
            // its base can come from.
            setChannelInstrumentGainPercent (channelIdx, 100.0f, 0.0f);
            applyKitUnityToChannel (channelIdx, fullKitUnityKey (msb, lsb, pc));
            return true;
        }

        /** Pre-warm: compose the kit a drum PC resolves to into the channel's
            pool ONCE, without publishing it.  Message thread (decodes samples).
            StylePlayer calls this at load for every drum kit the style can
            reach, so runtime swaps become atomic pointer swaps. */
        void preloadDrumKitForChannel (int channelIdx, int program)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            if (drumKitRegistry == nullptr) return;
            auto kp = resolveDrumKitParams (program);
            if (! kp.hasMappedKeys()) return;
            channels[(size_t) channelIdx].preloadDrumKit (*drumKitRegistry, kp, program);
        }

        /** Load-time publish that also syncs the UI kit-name fields.  Composes
            the kit if it isn't pooled yet, then publishes it WITH name sync.
            Message thread only (touches juce::String).  Returns true if a drum
            kit was published. */
        bool publishDrumKitWithName (int channelIdx, int program)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            if (drumKitRegistry == nullptr) return false;
            if (! channels[(size_t) channelIdx].getDrumChannel()) return false;

            if (! channels[(size_t) channelIdx].isDrumKitPooled (program))
            {
                auto kp = resolveDrumKitParams (program);
                if (! kp.hasMappedKeys()) return false;
                channels[(size_t) channelIdx].preloadDrumKit (*drumKitRegistry, kp, program);
            }
            if (! channels[(size_t) channelIdx].selectPooledDrumKit (program, /*syncName*/ true))
                return false;

            clearFullKitOnChannel (channelIdx);   // a composed kit now holds it
            applyKitUnityToChannel (channelIdx, program);
            return true;
        }

        /** True if the kit this drum PC resolves to is already pooled on the
            channel — i.e. programChangeDrum would be a cheap swap, not a decode.
            Used by StylePlayer's per-block expensive-load budget. */
        bool isChannelDrumKitPooled (int channelIdx, int program) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            return channels[(size_t) channelIdx].isDrumKitPooled (program);
        }

        /** Drum-aware Program Change dispatch (runtime / audio thread).
              - Returns true if the PC was handled as a drum-kit swap.
              - Returns false otherwise, signalling the caller to fall back to
                the melodic path (selectChannelPreset + bankMap.resolve).

            Fast path is an atomic pointer swap to a pre-warmed pooled kit (no
            decode, RT-safe).  Only a pool MISS still composes on the calling
            thread; StylePlayer budgets those per block and pre-warms the pool at
            load so misses are rare. */
        bool programChangeDrum (int channelIdx, int program)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            if (drumKitRegistry == nullptr) return false;
            if (! channels[(size_t) channelIdx].getDrumChannel()) return false;

            // Per-sound gain trim, drum side.  The melodic twin lives in
            // selectChannelPreset; a kit needs its own because a kit PC never
            // goes through there.  Applied on BOTH paths below (pooled swap and
            // compose) so the trim does not depend on whether the kit happened
            // to be warm — the same PC must sound the same either way.
            applyDrumPresetGain (channelIdx, program);

            // Fast path: kit already pooled for this PC -> pointer swap.
            if (channels[(size_t) channelIdx].selectPooledDrumKit (program, /*syncName*/ false))
            {
                clearFullKitOnChannel (channelIdx);   // a composed kit now holds it
                currentKitUnityKey[(size_t) channelIdx].v.store (program);
                return true;
            }

            // Miss: resolve + compose + pool + publish (the only path that still
            // decodes on the calling thread).
            auto kp = resolveDrumKitParams (program);
            if (! kp.hasMappedKeys()) return false;
            channels[(size_t) channelIdx].preloadDrumKit (*drumKitRegistry, kp, program);
            if (! channels[(size_t) channelIdx].selectPooledDrumKit (program, /*syncName*/ false))
                return false;

            clearFullKitOnChannel (channelIdx);
            currentKitUnityKey[(size_t) channelIdx].v.store (program);
            return true;
        }

        /** Set the channel's gain trim from the .drm saved for the kit this PC
            resolves to, or back to unity when that kit has no preset — the same
            "trim belongs to the sound, not the channel" rule selectChannelPreset
            follows for melodic voices. */
        void applyDrumPresetGain (int channelIdx, int program)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            if (drumKitRegistry == nullptr) return;

            auto it = drumKitPCMap.find (program);
            const juce::String kitName = (it != drumKitPCMap.end())
                                       ? it->second
                                       : nearestDrumFamily (program);

            float pct = 100.0f, baseDb = 0.0f;   // unity unless the .drm says otherwise
            if (auto pit = drumPresets.find (kitName.getIntValue());
                pit != drumPresets.end())
            {
                pct    = pit->second.params.gainPercent;
                baseDb = pit->second.params.baseUnityDb;
            }

            // THE SET OUTRANKS THE .drm.  A base saved in the .bset is the user's
            // calibration for THIS song; the .drm value is the older global one
            // and stays as the answer for kits the set says nothing about.
            if (float setDb = 0.0f; getKitUnityDb (program, setDb)) baseDb = setDb;

            setChannelInstrumentGainPercent (channelIdx, pct, baseDb);
        }

        /** Map / unmap a Program Change value to a kit name.  Defaults are
            populated in the constructor; call these to override.  Kit names
            must match a kit's DrumKitRegistry display name exactly. */
        void setDrumKitPCMapping (int pcValue, const juce::String& kitName)
        {
            drumKitPCMap[pcValue] = kitName;
        }
        void removeDrumKitPCMapping (int pcValue) { drumKitPCMap.erase (pcValue); }
        void clearAllDrumKitPCMappings()          { drumKitPCMap.clear(); }
        juce::String getDrumKitNameForPC (int pcValue) const
        {
            auto it = drumKitPCMap.find (pcValue);
            return it == drumKitPCMap.end() ? juce::String() : it->second;
        }

        // ---------------------------------------------------------------------
        // Scale tuning (Arabic / microtonal) — per channel
        // ---------------------------------------------------------------------
        void  setChannelScaleTuningCents (int channelIdx, int noteClass, float cents)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            if (noteClass  < 0 || noteClass  >= 12)           return;
            channels[(size_t) channelIdx].scaleTuningCents[noteClass].store(cents);
        }
        float getChannelScaleTuningCents (int channelIdx, int noteClass) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 0.0f;
            if (noteClass  < 0 || noteClass  >= 12)           return 0.0f;
            return channels[(size_t) channelIdx].scaleTuningCents[noteClass].load();
        }
        void clearChannelScaleTuning (int channelIdx)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            for (int n = 0; n < 12; ++n)
                channels[(size_t) channelIdx].scaleTuningCents[n].store(0.0f);
        }

        // ---------------------------------------------------------------------
        // Click library
        // ---------------------------------------------------------------------
        bool loadClickSample (int channelIdx, const juce::File& file)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            return channels[(size_t) channelIdx].loadClickSample(file);
        }
        void clearClickSample (int channelIdx)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].clearClickSample();
        }
        // Click library retired — these setters are inert no-ops kept so any
        // existing callers still compile.  Parameters are intentionally unused.
        void setClickEnabled (int /*channelIdx*/, bool  /*enabled*/) {}
        void setClickVolume  (int /*channelIdx*/, float /*volume*/)  {}
        void setClickDecayMs (int /*channelIdx*/, float /*decayMs*/) {}

        // ---------------------------------------------------------------------
        // Master
        // ---------------------------------------------------------------------
        std::atomic<float> masterVolume { 1.0f };

        // Style-bus gain: a single multiplier over the style channels
        // (0 .. kNumStyleChannels-1), independent of per-channel volumes so it
        // composes with the mixer rather than fighting it.  1.0 = unity.
        std::atomic<float> styleBusGain { 1.0f };
        // Per-style loudness makeup: a second style-bus multiplier set once at
        // style load (StylePlayer::applyVoiceSetup) to normalise the wide
        // loudness gaps between styles.  Kept SEPARATE from styleBusGain so it
        // composes with — never overwrites — the user's STYLE VOLUME slider, and
        // separate from the per-channel faders (which still show literal CC 7).
        std::atomic<float> styleMakeupGain { 1.0f };
        // Per-SECTION loudness trim: a third style-bus multiplier, driven by
        // StyleLoudness from a real BS.1770 measurement of what each section
        // actually sounded like.  Separate from styleMakeupGain because that one
        // is a whole-style figure written once at load, while this changes as
        // the arrangement moves — and because keeping them apart means either
        // can be inspected or disabled without disturbing the other.
        std::atomic<float> styleSectionTrim { 1.0f };
        // Right-hand (solo channels 16..23) bus gain.  Driven by the Global
        // Settings "RIGHT HAND VOLUME" slider; unity by default.
        std::atomic<float> soloBusGain  { 1.0f };

        // BASE UNITY for the right-hand bus, linear.  Deliberately NOT folded
        // into soloBusGain: that value is the FADER, and the mixer reads it back
        // to draw the handle.  Multiplying the base into it would make the
        // handle jump every time the base moved -- the same mistake the crash
        // GAIN box used to make.  Two stores, multiplied once at render.
        std::atomic<float> soloBusBaseUnity { 1.0f };

        void setMasterVolume (float linearGain)
        {
            masterVolume.store(juce::jmax(0.0f, linearGain));
        }
        void setStyleBusGain (float linearGain)
        {
            styleBusGain.store(juce::jmax(0.0f, linearGain));
        }
        float getStyleBusGain() const { return styleBusGain.load(); }
        void setStyleMakeupGain (float linearGain)
        {
            styleMakeupGain.store(juce::jmax(0.0f, linearGain));
        }
        float getStyleMakeupGain() const { return styleMakeupGain.load(); }
        void setStyleSectionTrim (float linearGain)
        {
            styleSectionTrim.store(juce::jmax(0.0f, linearGain));
        }
        float getStyleSectionTrim() const { return styleSectionTrim.load(); }
        float getMasterVolume() const { return masterVolume.load(); }

        /** Sounding voices across the right-hand bus (solo channels only).
            StyleLoudness uses it to know when the rendered block is a clean look
            at the style alone — the player's right hand over the top would be
            measured as if it were part of the style. */
        int soloVoicesActive() const noexcept
        {
            int n = 0;
            for (int c = kNumStyleChannels; c < kNumChannels; ++c)
                n += channels[(size_t) c].activeVoiceCount();
            return n;
        }
        float getSoloBusGain() const { return soloBusGain.load(); }

        void setSoloBusGain (float linearGain)
        {
            soloBusGain.store(juce::jmax(0.0f, linearGain));
        }

        /** What the SOLO VOLUME fader's unity detent is worth, linear.  The
            fader position is untouched by this; see the member's note. */
        void setSoloBusBaseUnity (float linearGain)
        {
            soloBusBaseUnity.store (juce::jlimit (0.0f, 16.0f, linearGain));
        }

        float getSoloBusBaseUnity() const { return soloBusBaseUnity.load(); }
        /** EMPTY slot: unload the channel's instrument completely.  The slot
            stops consuming RAM (its pooled decodes are freed) and ignores any
            MIDI sent to it — noteOn and the renderer bail on the null preset.
            A later selectChannelPreset (a style PC routed via CASM, or the user
            picking a sound) re-populates it normally. */
        void clearChannelInstrument (int channelIdx)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].clearInstrument();
            channels[(size_t) channelIdx].setDrumChannel (false);
        }

        void setChannelVolume (int channelIdx, float linearGain)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].channelVolume.store(juce::jmax(0.0f, linearGain));
        }

        /** Expression (CC 11) — a 0..1 multiplier on top of the channel volume,
            used by styles for dynamic swells.  Kept separate from volume so the
            two combine (gain = volume × expression) instead of clobbering. */
        void setChannelExpression (int channelIdx, float norm)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].channelExpression.store (juce::jlimit (0.0f, 1.0f, norm));
        }

        /** Style per-section level automation (sectionCC7 / setupCC7).  A
            relative multiplier that composes with the channel volume (the
            mixer fader) so a per-section voice swap's balancing CC 7 is honoured
            without overwriting the user's fader.  1.0 = no change. */
        void setChannelAutoLevel (int channelIdx, float level)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].channelAutoLevel.store (juce::jmax (0.0f, level));
        }

        /** The channel's BASE volume (CC 7 / style setup / mixer fader), i.e.
            the mix level WITHOUT the live expression swell.  The mixer faders
            read this so they show — and start from — the channel's real level
            instead of a fixed unity, which otherwise makes the first fader
            touch jump the channel. */
        float getChannelVolume (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 1.0f;
            return channels[(size_t) channelIdx].channelVolume.load();
        }

        /** Pan in the range -1 (full left) .. +1 (full right).  Out-of-range
            values are clamped.  Channel applies equal-power panning. */
        void setChannelPan (int channelIdx, float pan)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].channelPan.store (juce::jlimit (-1.0f, 1.0f, pan));
        }

        /** Reverb-send level in the range 0..1.  Storage-only today (the
            global reverb bus is not yet wired in the render path); kept
            here so style voice setups and mid-track CC 91 land their
            values where the bus will look for them once it exists. */
        void setChannelReverbSend (int channelIdx, float send)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].channelReverbSend.store (juce::jlimit (0.0f, 1.0f, send));
        }

    private:
        std::array<Channel, kNumChannels> channels;

        // Spike diagnostics — see getLastStyleRenderTicks().
        int64_t lastStyleTicks = 0, lastSoloTicks = 0;
        int     lastVoiceCount = 0;

        BlobReader            blob;
        std::atomic<bool>     blobLoaded { false };
        juce::File            blobFile;
        juce::String          blobAccessCode;

        // Per-instrument sound library: flag -> file + display name.
        std::map<int, juce::File>    soundLibrary;
        std::map<int, juce::String>  soundLibraryNames;
        std::map<int, int>           soundLibraryPack;       // flag -> SoundPack
        std::map<int, juce::String>  soundLibraryCategory;   // flag -> subfolder
        juce::StringArray            packCategories[kNumSoundPacks];
        juce::String                 libraryCollisions;
        juce::File                   soundLibraryFolder;

        // .ins default presets, split by type, keyed by flag.
        // USER SFZ, per channel.  `sfzActive` is read by selectChannelPreset on
        // the message thread and by nothing on the audio thread - the voice
        // itself is published through the normal atomic swap - so an atomic bool
        // plus a lock for the two strings is sufficient.
        FullKitMap fullKits;   // the sampled-kit folders under sounds/drums

        /** Which sampled kit each channel holds, "" when composed.  Read by the
            UI to light the right cell in the kit grid.

            The STRING is written on the message thread only.  `fullKitLive` is
            the half that says whether it still means anything, because it has to
            be cleared from programChangeDrum on the audio thread - hence an
            atomic flag beside the string rather than clearing the string. */
        std::array<juce::String, kNumChannels> fullKitOnChannel;
        std::array<std::atomic<bool>, kNumChannels> fullKitLive {};

        // What the gain funnel last applied, per channel - see
        // setChannelInstrumentGainPercent.
        struct ChGain
        {
            std::atomic<float> pct    { 100.0f };
            std::atomic<float> baseDb {   0.0f };
        };
        std::array<ChGain, kNumChannels> chGain;

        // Per-KIT base unity and which kit each channel holds - see the block
        // around fullKitUnityKey.  -1 = nothing identifiable published yet.
        std::map<int, float> kitUnityDb;

        // Wrapped, NOT a bare std::array<std::atomic<int>>: that value-initialises
        // to 0, and 0 is a VALID composed key (drum PC 0 = Standard), so every
        // untouched channel would claim to be holding Standard.
        struct KitKey { std::atomic<int> v { -1 }; };
        std::array<KitKey, kNumChannels> currentKitUnityKey;

        static juce::String key3 (int msb, int lsb, int pc)
        { return juce::String (msb) + "/" + juce::String (lsb) + "/" + juce::String (pc); }

        std::array<std::atomic<bool>, kNumChannels> sfzActive {};
        mutable juce::CriticalSection               sfzLock;
        std::array<juce::String, kNumChannels>      sfzNames;
        std::array<juce::String, kNumChannels>      sfzPaths;

        std::map<int, InstrumentPreset> instrumentPresets;      // melodic, SOLO  (.ins)
        std::map<int, InstrumentPreset> styleInstrumentPresets; // melodic, STYLE (.sins)
        std::map<int, InstrumentPreset> drumPresets;         // used by drum path
        juce::File                      instrumentPresetFolder;
        juce::File                      styleInstrumentPresetFolder;
        juce::String                 libraryAccessCode;

        double currentSampleRate = 44100.0;
        int    currentBlockSize  = 512;

        // Scratch for the scaled style-bus path (only used when styleBusGain ≠ 1).
        juce::AudioBuffer<float> styleScratch;

        // ── Shared decoded-instrument cache ─────────────────────────────────
        // One decoded PresetVoice per flag, shared (shared_ptr) by every
        // channel that uses the instrument -- across channels AND across style
        // loads.  Message thread only.  Soft-capped: once over budget, entries
        // referenced only by the cache (use_count == 1) are evicted.
        std::map<int, std::shared_ptr<Channel::PresetVoice>> decodedInstrumentCache;
        static constexpr int kMaxCachedInstruments = 48;
        std::shared_ptr<Channel::PresetVoice> getOrDecodeInstrument (int flag);

        // -- Shared decoded SAMPLED-KIT cache --------------------------------
        // The same idea as above, keyed on msb/lsb/pc rather than on a flag,
        // because a whole sampled kit has no instrument flag and every one of
        // them is preset 0 of its own .frb.  Keying on the KIT is what makes a
        // switch between two sampled kits actually switch.
        //
        // The cap is small on purpose: a whole sampled kit is a much bigger
        // object than one melodic instrument, and a channel only ever holds one
        // at a time.  Four keeps A/B auditioning instant across a handful of
        // kits without letting a click through all seventeen sit in RAM.
        // Eviction is the melodic cache's rule exactly: only entries no channel
        // still references (use_count == 1) can go.
        std::map<juce::String, std::shared_ptr<Channel::PresetVoice>> decodedFullKitCache;
        static constexpr int kMaxCachedFullKits = 4;

        // -- THE SAMPLED KITS SIT AN OCTAVE HIGH ------------------------------
        //
        // Every kit in the current pack was exported one octave above the GM
        // percussion map: its kick answers MIDI 48 where GM puts Bass Drum 1 at
        // 36, and where Grex, every style, the drum editor's key labels and the
        // XG low-key substitution all address it.  So a sampled kit played its
        // kick from the wrong key - and the composed kits, which are built
        // against the GM map, were right all along.
        //
        // Corrected at decode rather than by re-exporting seventeen blobs, and
        // corrected ONCE, before the voice is cached and shared.
        //
        // If a future pack is exported on the GM map, this constant is the only
        // thing to change - set it to 0 and the shift disappears.
        static constexpr int kFullKitKeyShift = -12;

        std::shared_ptr<Channel::PresetVoice> getOrDecodeFullKit (int msb, int lsb, int pc,
                                                                  const juce::String& accessCode)
        {
            const auto k = key3 (msb, lsb, pc);

            // Cache hit: hand out the SAME decoded kit (no decode, no copy).
            auto c = decodedFullKitCache.find (k);
            if (c != decodedFullKitCache.end()) return c->second;

            const auto blob = fullKits.blobFor (msb, lsb, pc);
            if (blob == juce::File()) return nullptr;

            // BlobReader speaks std::string, not juce::File - same call shape as
            // decodeInstrument's, so the two paths cannot drift apart.
            BlobReader reader;
            if (! reader.open (blob.getFullPathName().toStdString(),
                               accessCode.toStdString())) return nullptr;
            if (reader.getPresets().empty()) return nullptr;

            auto pv = Channel::decodeInstrument (reader, 0);
            if (pv == nullptr) return nullptr;

            // Down onto the GM map before anything else can see it - see
            // kFullKitKeyShift.  Here and not in adoptFullKit, because the voice
            // below is SHARED: DRUMS and PERC on the same kit would otherwise
            // shift the one object twice and land two octaves down.
            Channel::transposeVoiceKeys (*pv, kFullKitKeyShift);

            // Soft cap: evict entries no channel references any more.
            if ((int) decodedFullKitCache.size() >= kMaxCachedFullKits)
                for (auto e = decodedFullKitCache.begin();
                     e != decodedFullKitCache.end()
                     && (int) decodedFullKitCache.size() >= kMaxCachedFullKits;)
                    e = (e->second.use_count() == 1) ? decodedFullKitCache.erase (e)
                                                     : std::next (e);

            decodedFullKitCache[k] = pv;
            return pv;
        }

        // ── Drum-kit dispatch ────────────────────────────────────────────────
        DrumKitRegistry*           drumKitRegistry = nullptr;  // non-owning
        std::map<int, juce::String> drumKitPCMap;              // PC → kit name

        // Populates drumKitPCMap with the 16-kit default set:
        // GM Level 1 standard kits sit at their canonical PCs; the seven
        // remaining slots are filled with our additional kits at non-GM PCs.
        // Mapping must stay in sync with the 16-kit name list shown in
        // SoundsTab's DRUMS row.
        void populateDefaultDrumKitPCMap()
        {
            // ── GM standard kits (canonical PC values) ────────────────────
            // Family tokens are LOWER-CASE and must match the filename prefix of
            // the drum component blobs (e.g. standard_kick1.frb -> "standard").
            // Drum kits are keyed by the 3-digit ZERO-PADDED program-change
            // value -- the "PPP" prefix of the component files
            // (drums/<role>/PPP_Family.frb, e.g. drums/kick/000_standard.frb).
            // The registry names each component blob from the text before the
            // first '_', i.e. "000", so getElementsForKit("000") gathers the
            // kick/snare/stick/metal/tom/the-last components of that kit across
            // all role subfolders.  The family word after '_' is human-readable
            // only; resolution depends only on the number the style sends.
            drumKitPCMap[0]   = "000";   // Standard
            drumKitPCMap[8]   = "008";   // Room
            drumKitPCMap[16]  = "016";   // Power
            drumKitPCMap[24]  = "024";   // Electro
            drumKitPCMap[25]  = "025";   // Analog
            drumKitPCMap[32]  = "032";   // Jazz Set
            drumKitPCMap[40]  = "040";   // Brush Set
            drumKitPCMap[48]  = "048";   // Orchestra
            // PC 56 is HipHopKit in Yamaha's bank 127 (NOT an SFX kit -- the GM
            // "SFX at 56" convention does not hold on Yamaha arrangers), so it
            // resolves to Electro.  The 056_SFX kit stays in the registry and is
            // still selectable by hand; no style program change points at it.
            drumKitPCMap[56]  = "024";   // HipHopKit -> Electro

            // Every other drum PC (Genos/Tyros extended + Live!/Revo! kits) is
            // resolved at lookup time by nearestDrumFamily() onto the closest of
            // these nine, so no style is ever left without drums.
        }

        // Per-kit gain trims (see resolveDrumKitParams).
        static constexpr const char* kBrushKitName = "040";   // 040_Brush.frb
        static constexpr int         kSnareKey     = 38;      // GM Acoustic Snare

        // the_lasts global upper-percussion blob: keep only its lowest N mapped
        // keys in every kit, drop the rest — see resolveDrumKitParams merge.
        static constexpr const char* kTheLastsName    = "the_lasts";
        static constexpr int         kTheLastsMaxNotes = 18;

        // Collapse any drum PC that isn't one of the nine sampled kits onto the
        // closest sampled family.  `pc` is the raw 0-based MIDI value (Yamaha's
        // listed PC# minus 1).
        //
        // Covers the Genos / PSR-SX / Tyros extended kits, including the Live!
        // and Revo! kits that share MSB 127.  Yamaha's Revo! kits are listed
        // under LSB 8 on Genos but LSB 0 on PSR-SX (e.g. PopDrumKit is
        // 127-008-074 on Genos and 127-000-074 on SX); the drum path keys off
        // MSB 127 only and ignores LSB, so one number covers both.
        //
        // Kit identities below are from the Yamaha Data Lists (Genos / Tyros5 /
        // CVP), converted from their printed PC# (1-128) to 0-based.
        // Unknown -> Standard.
        static juce::String nearestDrumFamily (int pc) noexcept
        {
            switch (pc)
            {
                // THE ONLY RESOLVER FOR BANK 127.  FullKitMap's
                // composedFallbackPc used to carry a second table and disagreed
                // with this one on PC 4, 17, 61, 73 and 75 — so a style played a
                // different kit depending on which sampled-kit folders happened
                // to exist.  That table is gone; everything it caught is here.
                //
                // ── Power / Rock ────────────────────────────────────────────
                case 16:    // RockKit       (canonical, kept explicit)
                case 17:    // RockDrumKit   (Revo!)  <- fallback said Standard
                case 66:    // 80sPopKit     gated snare, big room: Power, not
                            //               Electro as the fallback had it
                case 87:    // PowerKit1     (Live!)
                case 88:    // PowerKit2     (Live!)
                case 90:    // RockKit       (Live!)
                    return "016";

                // ── Electro / Dance / EDM ───────────────────────────────────
                case 4:     // HitKit        (hybrid toms / electro snares)
                case 27:    // DanceKit
                case 56:    // HipHopKit     (also repointed in drumKitPCMap)
                case 57:    // BreakKit      <- was falling through to Standard
                case 60:    // HouseKit      (Tyros5 numbering)
                case 64:    // HouseKit      (PSR-E numbering)
                case 67:    // 80sR&B Kit    <- was falling through to Standard
                case 68:    // DubstepKit
                case 69:    // EDM Kit
                case 70:    // ElectroKit    (Genos)
                case 71:    // TrapKit
                case 112:   // DanceKit      (PSR-E numbering)
                    return "024";

                // ── Analog / drum machine ───────────────────────────────────
                case 58:    // AnalogT8Kit
                case 59:    // AnalogT9Kit
                case 61:    // DrumMachine   <- fallback said Electro; a drum
                            //                  machine IS the analog family
                    return "025";

                // ── Jazz ────────────────────────────────────────────────────
                case 74:    // VintageOpenKit  (Revo!)
                case 78:    // JazzStickKit    (Revo!)
                    return "032";

                // ── Brush ───────────────────────────────────────────────────
                case 41:    // RealBrushes
                case 42:    // brush variant  <- only the fallback had this one
                case 75:    // JazzBrushKitComp    (Revo!)  <- fallback: Standard
                case 76:    // JazzBrushExpanded   (Revo!)
                    return "040";

                // ── Room ────────────────────────────────────────────────────
                // Room is Standard with the ambience left in, so the ACOUSTIC
                // kits that used to default to Standard belong here: they are
                // recorded rooms, not dry studio kits.
                case 77:    // VintageMutedKit (Revo!)
                case 86:    // StudioKit
                case 89:    // AcousticKit
                case 91:    // RealDrums
                    return "008";

                // ── Standard ────────────────────────────────────────────────
                // 1  StandardKit2   72  SchlagerKit   73  PopDrumKit
                //
                // 73 PopDrumKit is deliberately HERE and not in Electro, where
                // this table used to put it: the Revo! pop kit is an acoustic
                // kit, and Electro flattened it into a machine.  72 Schlager is
                // the same argument against the fallback's Electro.
                default:
                    return "000";
            }
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePlayerEngine)
    };
} // namespace Betel



