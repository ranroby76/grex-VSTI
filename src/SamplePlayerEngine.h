

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

#include "StyleDucker.h"
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <iterator>
#include <map>
#include <memory>
#include <vector>
#include <algorithm>
#include "Channel.h"
#include "EdmKitFiles.h"            // EDM KIT base kits for style kit calls
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

        /** The section FX buses.  0 = LEFT (style + drums), 1 = RIGHT (solo).
            Params are plain atomics on the object, so the caller sets them
            directly and the audio thread picks them up next block. */
        Betel::SectionSendFx& sectionFx (int section) noexcept
        {
            return section == 1 ? rightSendFx : leftSendFx;
        }

        /** Const overload: the set capture is const and only reads the atomics,
            and std::atomic::load() is itself const. */
        const Betel::SectionSendFx& sectionFx (int section) const noexcept
        {
            return section == 1 ? rightSendFx : leftSendFx;
        }

        /** Per-instrument send amounts, 0..1, indexed by SectionSendFx::Slot.
            Which rack a channel reaches is decided by WHICH channel it is, not
            by anything set here — style and drum channels feed the left rack,
            solo channels the right. */
        void setChannelSend (int channelIdx, int slot, float amount) noexcept
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setSend (slot, amount);
        }
        float getChannelSend (int channelIdx, int slot) const noexcept
        {
            return (channelIdx >= 0 && channelIdx < kNumChannels)
                     ? channels[(size_t) channelIdx].getSend (slot) : 0.0f;
        }

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

        /** Per-block render timings and voice count.  Written by renderBlock on
            the audio thread and read by the same thread, so plain members are
            sufficient — no cross-thread access.  The spike log that used to
            consume these is gone; they are kept because they are free (two tick
            reads already taken inside renderBlock) and are the first thing any
            future performance question would want. */
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
        void getPadActivity (int channelIndex, uint32_t out[4]) const
        {
            for (int i = 0; i < 4; ++i) out[i] = 0u;
            if (channelIndex < 0 || channelIndex >= kNumChannels) return;
            channels[(size_t) channelIndex].getPadActivity (out);
        }

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
        // GREX HAS THREE PACKS.  Ballada dropped this to one along with the two
        // folders - see BetelFolderManager.h - and kNumSoundPacks is what sizes
        // packCategories[] and the scan loop, so putting it back to 3 is what
        // makes getPackCategories(1) and getPackInstruments(2, ...) answer
        // something again rather than empty by construction.
        //
        // WORLD AND ORIENTAL ARE OPTIONAL DOWNLOADS.  A pack whose folder is not
        // there is simply not installed: the scan skips it, it reports as
        // absent, and every getter answers empty for it.  That is a normal
        // state, never an error.
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

        /** The GM pack's preset folder, as a one-entry list.  See InstrumentPresetIO::scanFolders. */
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
                // STYLE CHANNELS ARE NOT VISITED - not even for the else-branch
                // gain reset.  With melodicPresetFor now refusing them, walking
                // one here would take the "preset gone" arm and stamp 100%/0 dB
                // over the gain the SET put there - a solo-side Save-as-Default
                // silently re-levelling the arrangement, which is the exact
                // dependency being cut.
                if (isStyleChannel (ch)) continue;

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
                    auto kp = resolveDrumKitParams (program, c.getRevoLowZone());  // picks up the new .drm
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
        juce::String kitNameForUnityKey (int unityKey, int channelIdx = -1) const
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

            // PASS THE CHANNEL AND THE NAME MATCHES WHAT SOUNDS.  Without it
            // this cannot know the style's drum bank, so a Revo! style would
            // display the kit the PC maps to while the redirect above quietly
            // loaded Standard.  channelIdx < 0 keeps the old bank-blind answer
            // for any caller that genuinely has no channel in hand.
            const bool revo = (channelIdx >= 0 && channelIdx < kNumChannels)
                            && channels[(size_t) channelIdx].getRevoLowZone();

            return drumFamilyFor (unityKey, revo);
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
            // ── THE CUT: A STYLE CHANNEL NEVER READS THE .INS ────────────────
            //
            // The .ins is the RIGHT HAND's saved voice.  Until now every
            // channel resolved through this one map, so the arrangement's
            // slots were voiced from solo-side files at every door a program
            // change opens - selectChannelPreset's preset apply, stashVoiceFor
            // (so every stash a runtime PC restores was built from solo data),
            // reapplyMelodicPreset after a Save-as-Default, the gain resolver,
            // and getPresetParamsForChannel.  Rob's set restored a style
            // slot's chain correctly THREE TIMES over and the values still
            // came back as somebody else's, because "somebody else" was this
            // fallback, reachable from more directions than any single caller
            // fix could cover.
            //
            // The SET is the style slot's ONLY voicing authority (the SoundsTab
            // side already says so: effectiveBaseUnityDb takes the set's own
            // value for a melodic style slot; adoptPresetParams refuses style
            // slots; reassertStyleSlotVoicing exists to re-state the set).
            // This makes the ENGINE agree.  nullptr here means: preset apply
            // falls to the neutral voice, the stash holds the neutral voice,
            // the gain resolver answers unity - and the set's commit, which
            // follows every one of those paths, is then the last and only
            // word.
            //
            // channelIdx was already in the signature and already ignored -
            // the .sins split this hook was built for was cancelled (see the
            // note below); the parameter finally does the job it was kept for.
            if (isStyleChannel (channelIdx))
                return nullptr;

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

        /** Mark a drum channel as holding a Revo! kit, so Channel::noteOn drops
            the low keys Revo! reassigns.  Set from the STYLE'S bank LSB at every
            publish site — applyVoiceSetup flattens runningBankLsb to 0 straight
            after publishing, so it cannot be read back later. */
        void setChannelRevoLowZone (int idx, bool b)
        {
            if (idx >= 0 && idx < kNumChannels) channels[(size_t) idx].setRevoLowZone (b);
        }

        /** The drum kit the style asked for on this channel - its raw bank, LSB
            and program - for the SX920 key map (Channel::remapStyleDrumKey). */
        void setChannelStyleKitAddress (int idx, int msb, int lsb, int pc)
        {
            if (idx >= 0 && idx < kNumChannels) channels[(size_t) idx].setStyleKitAddress (msb, lsb, pc);
        }

        /** The Crash tab's four options on an EDM kit: the kit's own crash pads
            (edm::crashPads).  False when the channel holds no EDM kit - out is
            then untouched and the caller fires the GM keys.  Audio-thread safe. */
        /** True while the channel plays an EDM kit. */
        bool channelHoldsEdmKit (int idx) const noexcept
        {
            return idx >= 0 && idx < kNumChannels && channels[(size_t) idx].getSynthKit() != nullptr;
        }

        bool edmCrashKeys (int idx, const int (&gmKeys)[4], int (&out)[4]) const noexcept
        {
            if (idx < 0 || idx >= kNumChannels) return false;
            const auto sk = channels[(size_t) idx].getSynthKit();
            if (sk == nullptr) return false;
            edm::crashPads (*sk, gmKeys, out);
            return true;
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

        /** ENERGY — the same global velocity-curve term, on every STYLE channel.

            IT SHARES globalVelCurve WITH THE SOLO SIDE'S LOW VELOCITY RESPONSE,
            and that is not a collision.  Channel runs TWO curves in sequence,
            the global one then the slot's own.  setSoloLowVelBoost claims the
            global term on the eight SOLO channels; nothing has ever written it
            on the sixteen STYLE channels, so it sat at 0 there permanently.
            ENERGY takes it - no new engine stage, no new slot parameter.

            AND THE ORDER DOES NOT MATTER, which is what makes that free rather
            than merely convenient: VelCurve is a pure power law, out =
            127*(in/127)^g with g = 2.5^-curve, so two curves in sequence
            multiply their exponents and multiplication commutes.  Sitting ahead
            of each slot's own curve is arithmetically identical to sitting
            behind it. */
        void setStyleEnergyCurve (float curve) noexcept
        {
            for (int i = 0; i < kNumStyleChannels; ++i)
                channels[(size_t) i].setGlobalVelCurve (curve);
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
        /** `revoLowZone` is the CHANNEL'S flag, taken from the style's drum bank
            LSB at publish time.  It decides which half of the split sub-GM zone
            this kit gets — revo_first or gm_first.  Every caller passes the
            flag of the channel it is about to load, because two channels can
            legitimately disagree: a style may put a Revo! kit on DRUMS and a
            legacy kit on PERC in the same song. */
        DrumKitParams resolveDrumKitParams (int program, bool revoLowZone)
        {
            DrumKitParams kp;
            if (drumKitRegistry == nullptr) return kp;

            // Exact match for one of the nine sampled kits, else collapse any
            // Genos/Tyros extended PC onto the closest sampled family so the
            // style still plays drums instead of going silent.
            const juce::String kitName = drumFamilyFor (program, revoLowZone);

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

            // ── AND IF THAT KIT IS NOT INSTALLED, USE STANDARD ───────────────
            //
            // nearestDrumFamily already answers an UNRECOGNISED program: its
            // default is "000".  The hole it does not cover is a RECOGNISED one
            // whose kit is not on this machine - PC 87 resolves to "016"
            // Power/Rock, and if the user's sounds folder has no Rock kit the
            // registry hands back an EMPTY element list.  A composed kit with no
            // elements is a SILENT drum channel, which is the worst of the three
            // possible outcomes: right kit > wrong kit > no drums at all.
            //
            // Standard is the one kit every install has, so it is the floor.  If
            // even that is missing the folder itself is wrong and there is
            // nothing here that can rescue it - the empty list stands and the
            // scan report is where that gets said.
            auto handles = drumKitRegistry->getElementsForKit (kitName);

            if (handles.empty() && kitName != "000")
                handles = drumKitRegistry->getElementsForKit ("000");

            // Merge the GLOBAL components (low_kick -> note 35, the_lasts ->
            // notes above 59) into every kit, regardless of program change.
            //
            // the_lasts is the shared upper-percussion blob.  We keep ONLY its
            // first 18 notes (the 18 lowest keys it maps) and drop the rest, for
            // every kit — the higher keys were unwanted extra percussion.  "First"
            // = lowest-key-first, computed from whatever the blob actually maps so
            // it holds no matter where those keys start.  low_kick and the per-kit
            // elements are untouched.
            // ── THE ORDER OF THE GLOBALS IS NOW DECIDED HERE, NOT BY THE SCAN ──
            //
            // The merge below is FIRST-CLAIM-WINS: a key already in `handles`
            // is never taken again, and `handles` grows as each global is
            // folded in.  That was harmless while no two globals overlapped.
            // The sub-GM split makes them overlap by design — revo_first and
            // the_second both land on 25..28, and revo_first has to win there —
            // so leaving the outcome to the registry's scan order would make a
            // Revo! kit's tambourine depend on the order the folder happened to
            // enumerate.  Silent, machine-dependent, and impossible to explain.
            //
            // Two rules, and between them they settle every case:
            //
            //   1. THE KIT-DEPENDENT HALF GOES FIRST.  revo_first (or gm_first)
            //      claims its keys before the_second is offered any, which is
            //      exactly the 25..28 override.
            //   2. ONLY ONE OF THE TWO IS ELIGIBLE.  Both are non-numeric, so
            //      the registry catalogs both as globals and would otherwise
            //      merge both into every kit.  The style's bank LSB picks one
            //      and the other is skipped outright.
            //
            // the_second is KitAgnostic and merges either way; legacy the_first
            // merges last so that on an install carrying both old and new blobs
            // the split wins and the_first only fills what nothing else did.
            //
            // ALLOCATION NOTE: this builds three small vectors, and on a pool
            // miss resolveDrumKitParams runs on the audio thread.  That path
            // already allocates freely — getElementsForKit and
            // getGlobalComponentNames both return vectors by value, and the
            // compose that follows decodes samples — so it is the documented
            // expensive path, budgeted per block by StylePlayer, and this adds
            // nothing to its character.
            //
            // POOLING IS SAFE AND NEEDS NO FLAG OF ITS OWN.  Kits are pooled per
            // channel by drum PC, the whole pool is cleared on every style load
            // (reloadBalanceFile -> setBalanceRoleGainFn -> clearDrumKitPool),
            // and StylePlayer re-states the Revo flag immediately BEFORE each
            // publish or program change.  So a pooled entry was always composed
            // under the same bank LSB that PC arrives with in this style, and a
            // warm entry can never be the wrong variant.
            // ── KEY 33 GOES FIRST OF ALL ───────────────────────────────────────
            //
            // The soft kick has its own two blobs (see DrumKitRegistry,
            // kSoftKickKey), and exactly one of them is eligible per kit: the EDM
            // kick for the electronic families, the soft kick for everything
            // else.  The kit being composed is `kitName` - drumFamilyFor's answer
            // - so the choice rides on the same table that picked the kit.
            //
            // It is merged AHEAD of every other global so that it owns key 33 by
            // construction.  the_second has been re-sampled without 33, but an
            // install still carrying the old the_second, or legacy the_first,
            // would otherwise claim the key first and keep the generic soft kick
            // on a Dance kit.  First-claim-wins makes the order the whole rule.
            //
            // If neither 33 blob is installed nothing is added here and the key
            // falls through exactly as before: to whatever low-zone blob still
            // carries it, and failing that to composeDrumKit's clone of the kit's
            // own kick (xgLowKeySubstitute).
            const bool edmKick33 = DrumKitRegistry::usesEdmKick33 (kitName);

            std::vector<juce::String> kick33, kitDependent, agnostic, others;
            for (const auto& gName : drumKitRegistry->getGlobalComponentNames())
            {
                switch (DrumKitRegistry::lowZoneKindFor (gName))
                {
                    case DrumKitRegistry::LowZoneKind::SoftKick33:
                        if (! edmKick33)   kick33.push_back (gName);
                        break;
                    case DrumKitRegistry::LowZoneKind::EdmKick33:
                        if (edmKick33)     kick33.push_back (gName);
                        break;
                    case DrumKitRegistry::LowZoneKind::RevoOnly:
                        if (revoLowZone)   kitDependent.push_back (gName);
                        break;
                    case DrumKitRegistry::LowZoneKind::GmOnly:
                        if (! revoLowZone) kitDependent.push_back (gName);
                        break;
                    case DrumKitRegistry::LowZoneKind::KitAgnostic:
                        agnostic.push_back (gName);
                        break;
                    case DrumKitRegistry::LowZoneKind::NotLowZone:
                    default:
                        others.push_back (gName);
                        break;
                }
            }

            std::vector<juce::String> mergeOrder;
            mergeOrder.reserve (kick33.size() + kitDependent.size() + agnostic.size() + others.size());
            mergeOrder.insert (mergeOrder.end(), kick33      .begin(), kick33      .end());
            mergeOrder.insert (mergeOrder.end(), kitDependent.begin(), kitDependent.end());
            mergeOrder.insert (mergeOrder.end(), agnostic   .begin(), agnostic   .end());
            mergeOrder.insert (mergeOrder.end(), others     .begin(), others     .end());

            for (const auto& gName : mergeOrder)
            {
                auto g = drumKitRegistry->getElementsForKit (gName);

                // THE 18-KEY CAP IS GONE.  It kept only the_lasts' eighteen
                // lowest mapped keys and dropped the rest, because the old blob
                // ran past what was wanted at the top.  The component now
                // declares a ZONE instead (kXgHighZoneLoKey..kXgHighZoneHiKey =
                // 60..83) and the registry clamps to it at catalog time, which
                // is both more precise and one mechanism rather than two.
                //
                // Leaving the count cap in place would have been actively wrong:
                // the re-sampled blob maps 24 keys, so it would have kept 60..77
                // and silently dropped 78..83 — including key 82 Shaker, which
                // is the single most-used key above the GM ceiling across the
                // whole style library.

                // ── GAP-FILL ONLY: NEVER LAYER OVER A KEY THE KIT ALREADY HAS ──
                //
                // These globals were APPENDED with no key check, so any key a
                // kit already maps ended up with TWO elements on it - the kit's
                // own and the shared global - and both sounded. Every kit's low
                // kick became "this kit's kick PLUS low_kick", which is why a
                // Brush kit's kick sounded nothing like the same kit in an SFZ
                // player, and why every kit's kick sounded oddly alike.
                //
                // The purpose stated directly above is to FILL what a kit does
                // not carry: low_kick for kits with no deep kick, the_lasts for
                // upper percussion. Filling a gap and doubling an existing
                // element are different operations, and only the first was ever
                // wanted.
                //
                // The kit's own elements win, always. Its identity is the whole
                // point of having nine kits.
                for (const auto& ge : g)
                {
                    const bool kitAlreadyHasKey =
                        std::any_of (handles.begin(), handles.end(),
                                     [&ge] (const auto& h) { return h.midiKey == ge.midiKey; });

                    if (! kitAlreadyHasKey)
                        handles.push_back (ge);
                }
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
            // THE BRUSH-KIT SNARE SEED IS GONE.
            //
            // It set key 38's gain slider to 20 instead of 50 because that snare
            // "sits far too hot against the rest of the kit at unity". It was
            // only a default, but it was also the ONE per-kit special case in
            // the engine - invisible, uneditable without knowing it existed, and
            // the reason a Brush kit A/B'd against an SFZ player came back 8 dB
            // light on the snare and looked like a bug.
            //
            // If it really is too hot, that is a per-key gain in the drum editor
            // and a SAVE AS DEFAULT, which is exactly the mechanism that already
            // overrides everything here.

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
            // resolve(), so an ALIASED kit names the blob that is actually
            // sounding.  With find() the editor showed nothing at all for an
            // aliased request - correct kit, correct sound, blank name - which
            // is the kind of gap that gets reported as "the kit did not load".
            if (const auto* e = fullKits.resolve (msb, lsb, pc))
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
            auto kp = resolveDrumKitParams (program,
                                            channels[(size_t) channelIdx].getRevoLowZone());
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
                auto kp = resolveDrumKitParams (program,
                                                channels[(size_t) channelIdx].getRevoLowZone());
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
            auto kp = resolveDrumKitParams (program,
                                            channels[(size_t) channelIdx].getRevoLowZone());
            if (! kp.hasMappedKeys()) return false;
            channels[(size_t) channelIdx].preloadDrumKit (*drumKitRegistry, kp, program);
            if (! channels[(size_t) channelIdx].selectPooledDrumKit (program, /*syncName*/ false))
                return false;

            clearFullKitOnChannel (channelIdx);
            currentKitUnityKey[(size_t) channelIdx].v.store (program);
            return true;
        }

        //======================================================================
        //  EDM KIT for STYLE kit calls.  A style asking (bank 127 / LSB 0) for
        //  an electronic kit the synth covers - Dance 27, HipHop 56, Break 57,
        //  Analog T8 58, Analog T9 59, House 60, and the 2024 kits 69 / 72 -
        //  plays the live synth kit instead of a sampled stand-in.  Same two
        //  stages as the sampled kits: resolve + pool at style load (message
        //  thread), pointer swap at runtime (audio thread, RT-safe).  A pool
        //  miss returns false and the caller falls back to the sampled path.
        //======================================================================
        bool preloadEdmKitForChannel (int channelIdx, int msb, int lsb, int pc)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            const char* name = edm::kitForStyleProgram (msb, lsb, pc);
            if (name == nullptr) return false;
            // Re-resolve a pooled kit whose base has been saved since (a pad saved
            // as base): the pool would otherwise play the OLD base until restart.
            if (! channels[(size_t) channelIdx].isSynthKitPooled (pc)
                || channels[(size_t) channelIdx].synthKitPoolVersion (pc) != EdmKitFiles::baseVersion())
            {
                auto kitHeap = std::make_unique<edm::Kit>();          // ~37 KB kit: heap, not the UI thread's stack
                edm::Kit& kit = *kitHeap;
                if (! EdmKitFiles::resolve (juce::String (name), {}, kit)) return false;
                channels[(size_t) channelIdx].preloadSynthKit (pc, kit);
                channels[(size_t) channelIdx].setSynthKitPoolVersion (pc, EdmKitFiles::baseVersion());
            }
            return true;
        }

        bool publishEdmKitWithName (int channelIdx, int msb, int lsb, int pc)   // message thread
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            if (! channels[(size_t) channelIdx].getDrumChannel()) return false;
            if (! preloadEdmKitForChannel (channelIdx, msb, lsb, pc)) return false;
            if (! channels[(size_t) channelIdx].selectPooledSynthKit (pc, /*syncName*/ true)) return false;
            clearFullKitOnChannel (channelIdx);
            applyKitUnityToChannel (channelIdx, 0);      // the unity the SOUNDS EDM cell uses
            return true;
        }

        bool programChangeEdm (int channelIdx, int msb, int lsb, int pc)        // audio thread
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return false;
            if (edm::kitForStyleProgram (msb, lsb, pc) == nullptr) return false;
            if (! channels[(size_t) channelIdx].getDrumChannel()) return false;
            if (! channels[(size_t) channelIdx].selectPooledSynthKit (pc, /*syncName*/ false)) return false;
            clearFullKitOnChannel (channelIdx);
            currentKitUnityKey[(size_t) channelIdx].v.store (0);
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

            // The trim belongs to the kit that ACTUALLY loads, so this has to
            // follow the Revo! redirect too — otherwise a Revo! style would
            // apply the brush kit's saved .drm trim to a Standard kit.
            const juce::String kitName =
                drumFamilyFor (program,
                               channels[(size_t) channelIdx].getRevoLowZone());

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

            // ── AND THE SET OUTRANKS THE .drm's PERCENT TOO ──────────────────
            //
            // This runs on EVERY drum program change, so on a set-owned channel
            // it was re-imposing the .drm's gainPercent (or a bare 100%) over
            // whatever the set had committed - the drum-side twin of the
            // per-sound gain reset that took all night on the melodic side.
            //
            // The BASE is already deferred to the set two lines up; the PERCENT
            // has to follow the same rule or the pair disagrees about who owns
            // the level. Keeping the channel's current percent is what "the set
            // decides" means here: the set pushed it through onSlotGainChanged
            // and nothing since has had the right to change it.
            if (channels[(size_t) channelIdx].getVoiceOwnedBySet())
                pct = chGain[(size_t) channelIdx].pct.load();

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

        // STYLE BUS BASE UNITY -- the twin of the one above: what the STYLE
        // VOLUME fader's unity detent is worth.  Necessary since MAKEUP was
        // removed, because MAKEUP was an automatic normaliser on this bus and
        // without it the detent's real loudness is whatever the samples give
        // rather than where the player wants it.  Stored per SET.
        std::atomic<float> styleBusBaseUnity { 1.0f };

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

        void setStyleBusBaseUnity (float linearGain)
        {
            styleBusBaseUnity.store (juce::jlimit (0.0f, 16.0f, linearGain));
        }

        float getStyleBusBaseUnity() const { return styleBusBaseUnity.load(); }

        /** THE DUCKER lives here rather than beside the Finisher because of
            WHERE IT ACTS: on the style bus, before the two buses are summed.
            The Finisher runs on the finished master; this has to run while the
            style is still a separate signal, which is only true inside this
            class.  Its editor sits in the Finisher WINDOW, but that is a UI
            grouping and nothing more. */
        Betel::StyleDucker& getDucker() noexcept             { return ducker; }
        const Betel::StyleDucker& getDucker() const noexcept { return ducker; }
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

        //=====================================================================
        // TWO GAINS, MULTIPLIED - THE STYLE DECIDES, WE ADJUST AFTER
        //
        // THE STYLE'S NUMBER IS THE UNITY REFERENCE.  Whatever CC 7 a style
        // sends for a part is that part's base, and nothing here argues with
        // it.  The mixer fader is a SEPARATE, POST multiplier: at 127 it is
        // 1.0 and you hear exactly what the style asked for; below that you
        // are trimming the style's decision, not replacing it.
        //
        //      audible = styleVolume x userVolume
        //
        // WHY THIS REPLACED THE OLD MODEL.  Before, both sides wrote ONE value
        // and whoever wrote last won.  That single fact produced the FOLLOW
        // ratchet (a blend that read its own output back as the user's level),
        // the sticky-fader-across-styles problem, and the long hunt for why a
        // set's gain kept reverting - every one of them a symptom of two
        // authorities sharing one number.  Multiplying makes the question
        // "whose value is this" disappear: both are, always, and neither can
        // erase the other.
        //=====================================================================

        /** THE USER'S POST-FADER, 1.0 = the style's own level untouched.

            Every caller outside StylePlayer is user intent - the mixer fader, a
            MIDI-learned remote fader, a set's MixerState on restore - so they
            all land here. The style's own writes go through
            setChannelVolumeDerived and move the OTHER factor. */
        void setChannelVolume (int channelIdx, float linearGain)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            chUserVolume[(size_t) channelIdx].store (juce::jmax (0.0f, linearGain));
            recomputeChannelVolume (channelIdx);
        }

        /** Reset every user fader to unity - "127 everywhere", the state a style
            load starts from before a set (if there is one) applies its own.

            THE STYLE SIDE IS DELIBERATELY UNTOUCHED: the incoming style is about
            to state its own levels, and clearing them here would only put a
            gap of silence in front of that. */
        void resetUserVolumesToUnity()
        {
            for (int ch = 0; ch < kNumChannels; ++ch)
            {
                chUserVolume[(size_t) ch].store (1.0f);
                // THE BASE TRIM RESETS WITH THE FADER, and for the same reason:
                // it is a correction for THIS style's balance, so carrying it
                // onto the next style would be the sticky-fader problem again
                // in a second variable. The set restores its own value after.
                chUserBaseDb[(size_t) ch].store (0.0f);
                recomputeChannelVolume (ch);
            }
        }

        /** THE STYLE DERIVED THIS FADER.  Audible level only; the anchor is left
            exactly where the user last put it.

            WHY THIS EXISTS.  FOLLOW PROGRAMMED GAINS blends from the user's
            level toward the style's, and refreshLevels used to read the anchor
            back out of the LIVE fader it had just written - so the anchor was
            its own previous output.  Two things followed, and Rob heard both:
            the slider became one-way (dragging FOLLOW back to 0 left the fader
            wherever the last blend put it, because blending 0% away from itself
            is a no-op), and because GoldSlider::mouseDrag notifies on EVERY
            mouse-move tick, each tick applied another blend step - so a drag to
            30% converged on the style value just as surely as a drag to 100%.
            Only a style reload appeared to "fix" it, because applyVoiceSetup
            re-established a fresh anchor.

            With the anchor held separately the blend is idempotent: FOLLOW 0 is
            exactly the user's fader, FOLLOW 100 is exactly the style's, and
            dragging back and forth is reversible however many ticks it takes. */
        void setChannelVolumeDerived (int channelIdx, float linearGain)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            chStyleVolume[(size_t) channelIdx].store (juce::jmax (0.0f, linearGain));
            recomputeChannelVolume (channelIdx);
        }

        //=====================================================================
        // THE POST BASE TRIM - A THIRD FACTOR, DELIBERATELY
        //
        //      audible = styleVolume x userVolume x dbToGain (userBaseDb)
        //
        // Rob's case: "sometimes style instruments are playing so loud or so
        // quiet that a brutal interven needed" - a fader alone cannot rescue a
        // part the style sends at 12 or at 127.
        //
        // WHY A THIRD FACTOR AND NOT A TWEAK TO EITHER EXISTING ONE. Folding it
        // into the style factor would put us back where the whole night
        // started, with two authorities writing one number. Folding it into the
        // user factor would make the fader jump when the trim moved. Kept
        // separate, each of the three has exactly one writer and the product is
        // the only thing that changes.
        //
        // POST, like the fader: it never touches what the style SENT, only what
        // is heard afterwards.
        //=====================================================================
        void setChannelUserBaseDb (int channelIdx, float db)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            chUserBaseDb[(size_t) channelIdx].store (juce::jlimit (-40.0f, 40.0f, db));
            recomputeChannelVolume (channelIdx);
        }

        float getChannelUserBaseDb (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 0.0f;
            return chUserBaseDb[(size_t) channelIdx].load();
        }

        //=====================================================================
        // THE STYLE BALANCE TRIM - the fourth factor, and the invisible one.
        //
        // Set from balance.grexv per GM instrument, on STYLE channels only. It
        // is kept apart from the other three for the same reason they are kept
        // apart from each other: one writer each, and the product is the only
        // thing that moves. In particular it must never touch the fader, or a
        // library calibration would silently rewrite the player's mix.
        //=====================================================================
        /** Hand every channel the per-DEPARTMENT lookup from balance.grexv, and
            drop any composed kit already in the pool so the next publish
            rebuilds with the new trims. Called by StylePlayer after each file
            reload - which is what makes a drum edit audible on a style switch,
            exactly like the melodic side. */
        void setBalanceRoleGainFn (std::function<float (int)> fn)
        {
            for (auto& c : channels)
            {
                c.setBalanceRoleGainFn (fn);
                c.clearDrumKitPool();      // trims are BAKED at compose time
            }
        }

        void setChannelBalanceGain (int channelIdx, float linear)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            chBalanceGain[(size_t) channelIdx].store (juce::jmax (0.0f, linear));
            recomputeChannelVolume (channelIdx);
        }

        float getChannelBalanceGain (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 1.0f;
            return chBalanceGain[(size_t) channelIdx].load();
        }

        /** The style's own stated level for this channel, with no user trim. */
        float getChannelStyleVolume (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 1.0f;
            return chStyleVolume[(size_t) channelIdx].load();
        }

        /** The anchor: the fader as the USER last set it, ignoring anything the
            style has blended on top.  Message thread. */
        float getChannelUserVolume (int channelIdx) const
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return 1.0f;
            return chUserVolume[(size_t) channelIdx].load();
        }

        /** Expression (CC 11) — a 0..1 multiplier on top of the channel volume,
            used by styles for dynamic swells.  Kept separate from volume so the
            two combine (gain = volume × expression) instead of clobbering. */
        void setChannelExpression (int channelIdx, float norm)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].channelExpression.store (juce::jlimit (0.0f, 1.0f, norm));
        }

        /** Style CC74 (brightness / filter sweep), raw 0..127, 64 = neutral. */
        void setChannelBrightness (int channelIdx, int cc74)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setBrightnessCc (cc74);
        }

        /** Style per-section level automation (sectionCC7 / setupCC7).  A
            relative multiplier that composes with the channel volume (the
            mixer fader) so a per-section voice swap's balancing CC 7 is honoured
            without overwriting the user's fader.  1.0 = no change. */
        /** FUNKEY MODE's own wah+phaser stage for one channel — see
            Channel::applyFunkeyInPlace.  Its own route rather than part of
            applyChannelParams, because that call is also what a program change
            performs. */
        void setChannelFunkeyFx (int channelIdx, const Channel::FunkeyFx& f)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].setFunkeyFx (f);
        }

        /** THE FUNKEY MIX, 0..1 - one value (the set's), so it goes to EVERY
            channel.  Only the funkeyed ones hear it; on the rest it is an
            unread atomic. */
        void setFunkeyMix (float mix01) noexcept
        {
            for (int i = 0; i < kNumChannels; ++i)
                channels[(size_t) i].setFunkeyMix (mix01);
        }

        void setChannelAutoLevel (int channelIdx, float level)
        {
            if (channelIdx < 0 || channelIdx >= kNumChannels) return;
            channels[(size_t) channelIdx].channelAutoLevel.store (juce::jmax (0.0f, level));
        }

        /** WHAT THE MIXER FADER SHOWS - the USER factor, NOT the product.
            
            It used to return channels[].channelVolume, which was the one number
            both sides wrote.  Under the multiply model that number is the
            PRODUCT, and a fader showing the product would jump every time the
            style restated its own level - which is precisely the fight this
            change exists to end.  The fader shows what the user moved; 1.0 is
            "as the style intended". */
        float getChannelVolume (int channelIdx) const
        {
            return getChannelUserVolume (channelIdx);
        }

        /** The PRODUCT actually feeding the audio, for anything that needs the
            real mix level rather than the fader position. */
        float getChannelAudibleVolume (int channelIdx) const
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

        // ONE decoded-preset cache for the whole engine - see Channel::PresetCache.
        // Per ENGINE, deliberately not static: two plugin instances can hold
        // different sound libraries, and a preset INDEX means different audio in
        // each. A process-wide cache would hand one instance the other's sounds.
        Channel::PresetCache presetCache;

        // FOLLOW PROGRAMMED GAINS anchor - see setChannelVolumeDerived.  Not in
        // Channel because the audio thread never reads it: it is message-thread
        // bookkeeping about what the USER asked for, not part of the signal path.
        // The two factors.  channels[].channelVolume stays the PRODUCT, so the
        // audio path is untouched and still reads one number per block.
        std::array<std::atomic<float>, kNumChannels> chUserVolume;
        std::array<std::atomic<float>, kNumChannels> chStyleVolume;
        std::array<std::atomic<float>, kNumChannels> chUserBaseDb;
        std::array<std::atomic<float>, kNumChannels> chBalanceGain;

        //---------------------------------------------------------------------
        // THE GAIN CHAIN, IN THE ORDER IT IS MEANT TO BE READ.
        //
        //   station 0   the STYLE's own CC 7          - the style decides
        //   station 1   balance.grexv, per instrument - the library calibration
        //   station 2   the SLOT BASE TRIM text box   - the blunt correction
        //   station 3   the MIXER FADER               - the performance
        //
        // EACH STATION'S OUTPUT IS THE NEXT ONE'S UNITY. That is not enforced by
        // anything here - it follows from all four being PURE GAINS multiplied
        // into one number, so the product is the same whatever order they are
        // written in, and every control's 0 dB / 1.0 means "leave what reached
        // me alone". The listing order below is documentation, not mechanism.
        //
        // THE CONSTRAINT THAT KEEPS IT TRUE: nothing may clamp, saturate or
        // compress BETWEEN the stations. Add a limiter in the middle and the
        // order stops being free - stations before it would be scaled by it and
        // stations after would not, and every control's meaning shifts. Each
        // setter clamps its OWN value on the way in, which is safe; the product
        // is formed here in one multiply and nowhere else.
        //---------------------------------------------------------------------
        void recomputeChannelVolume (int ch)
        {
            channels[(size_t) ch].channelVolume.store (
                chStyleVolume[(size_t) ch].load()                                        // 0
              * chBalanceGain[(size_t) ch].load()                                        // 1
              * juce::Decibels::decibelsToGain (chUserBaseDb[(size_t) ch].load(), -40.0f) // 2
              * chUserVolume [(size_t) ch].load());                                      // 3
        }

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
        std::map<int, int>           soundLibraryPack;       // flag -> SoundPack (always PackGm here)
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
        // TWO BUS BUFFERS, and they are why renderBlock had to change.
        //
        // The buses used to be summed as they were produced - each group
        // scaled and addFrom'd straight into the caller's buffer, and at unity
        // gain rendered into it directly with no intermediate at all.  A
        // sidechain needs them apart: the ducker has to see the style as one
        // signal and the solo as another, at the same instant, before either
        // has been mixed with the other.
        juce::AudioBuffer<float> styleBus, soloBus;

        //======================================================================
        // THE TWO SECTION FX RACKS
        //
        // LEFT  = the 16 style channels, drums and perc included.
        // RIGHT = the 8 solo channels.
        //
        // Three global effects each — chorus, reverb and delay — replacing the
        // per-channel inserts that used to run on all 24.  Six instances instead
        // of up to seventy-two.
        //
        // Wah, phaser and sweetener are NOT here.  They are shapers whose output
        // replaces the signal, so they stayed per-instrument inserts on the
        // channel, ahead of the send tap.
        //
        // The section split cost nothing to find: the ducker already needed the
        // style and the right hand rendered into separate buffers, and those are
        // exactly the two sections.
        //
        // The send buffers live INSIDE the rack now.  The engine had four loose
        // ones when there were two effects; at three it needs six, and keeping
        // them next to the effects that consume them is what stops a buffer
        // being cleared in one place and read in another.
        Betel::SectionSendFx leftSendFx, rightSendFx;

        Betel::StyleDucker ducker;

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

        // the_last (global upper percussion) and the three low-zone globals are
        // all bounded by DrumKitRegistry's ZONE clamp at catalog time — 60..83
        // for the_last, 13..28 for revo_first, 13..22 for gm_first and 23..34
        // for the_second.  The old kTheLastsName / kTheLastsMaxNotes count cap
        // that used to live here is gone: it kept only the eighteen lowest keys,
        // which after the component was re-sampled would have dropped 78..83
        // including key 82 Shaker.

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
        //=====================================================================
        // REVO! KITS FALL BACK TO STANDARD
        //
        // Yamaha's Revo! drum kits (drum bank LSB 8) are not sampled here, so
        // every one of them is answered by the nearest family below.  For a
        // BALLAD library that answer was almost always Brush: ballad styles
        // cluster on JazzBrushKitComp (PC 75) and JazzBrushExpanded (PC 76),
        // and both land on 040.  Ballada's brush kit is a quiet kit, and a
        // power ballad asking for a Revo! kit got a soft one.
        //
        // WHY THIS IS KEYED ON THE BANK AND NOT THE PC.  Redirecting PC 75/76
        // in nearestDrumFamily would move them for EVERY bank, taking a
        // non-Revo style that legitimately wants brushes off its kit as
        // collateral.  The Revo! flag says precisely what is meant - "this
        // style asked for a kit we do not have" - and nothing else is touched.
        //
        // ── TO CARVE ONE OUT, EDIT THIS FUNCTION AND NOTHING ELSE ────────────
        //
        // Every PC -> kit decision in the engine runs through drumFamilyFor
        // below, so an exception added here is honoured by the composed kit,
        // the .drm trim lookup and the name the SOUNDS tab shows, all three.
        // They must agree: a UI reading "Brush" over a Standard kit is the kind
        // of disagreement that gets reported as a sound bug.
        static juce::String revoFallbackFamily (int pc, const juce::String& mappedFamily)
        {
            // A carve-out is one line, e.g. to keep RockDrumKit on Rock:
            //     if (pc == 17) return mappedFamily;
            juce::ignoreUnused (pc, mappedFamily);
            return "000";
        }

        /** THE ONE PLACE A DRUM PC BECOMES A KIT NAME.

            `revoLowZone` is the channel's flag, set from the style's drum bank
            LSB at every publish site — the same flag that decides whether
            revo_first or gm_first is merged into the low zone. */
        juce::String drumFamilyFor (int pc, bool revoLowZone) const
        {
            auto it = drumKitPCMap.find (pc);
            const juce::String mapped = (it != drumKitPCMap.end())
                                      ? it->second
                                      : nearestDrumFamily (pc);

            return revoLowZone ? revoFallbackFamily (pc, mapped) : mapped;
        }

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

                // ── NOTHING FALLS BACK TO ROOM ANY MORE ─────────────────────
                //
                // 77 VintageMutedKit, 86 StudioKit, 89 AcousticKit and
                // 91 RealDrums used to land on Room ("008") on the reasoning
                // that Room is Standard with the ambience left in, so a
                // recorded-room kit belonged there rather than on a dry studio
                // kit.  They now fall through to Standard with everything else.
                //
                // ROOM IS STILL REACHABLE - this is about APPROXIMATION only.
                // drumKitPCMap[8] = "008" is untouched, so a style that asks
                // for PC 8 gets the real Room kit exactly as before. What
                // changed is only where the UNKNOWN kits land, and an
                // approximation carrying somebody else's room ambience is
                // harder to sit a mix on than a dry kit is.

                // ── Standard ────────────────────────────────────────────────
                // 1  StandardKit2   72  SchlagerKit   73  PopDrumKit
                // 77 VintageMutedKit  86 StudioKit  89 AcousticKit  91 RealDrums
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
