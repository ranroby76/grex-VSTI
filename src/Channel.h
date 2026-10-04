

#pragma once

//==============================================================================
// Channel.h  —  single sampler channel inside SamplePlayerEngine.
//
// A Channel owns:
//   - Its own region table + decoded sample buffers (loaded from a blob preset
//     OR composed from a drum kit via loadDrumKit)
//   - A pool of voices (kMaxVoices) with full per-voice DSP
//   - All instrument-level synth parameters (ADSR, filter, filter env,
//     pitch env, three LFOs, mono/portamento, loop-sync, vol/pan)
//   - Per-channel post-mix effects chain: 5-band EQ → stereo delay → reverb
//   - The held-note stack used by mono legato modes
//   - In drum mode: a per-MIDI-key user-state array (gain/pitch/cutoff/A/D/R)
//     so the DrumsPopup sliders can edit each key individually without
//     touching the per-channel atomics
//
// Drum-channel flag forces Poly mode and disables portamento regardless of
// the playMode / portamentoTime atomics, so arrangement code can mark percussion
// channels safely without touching their other parameters.
//
// Threading:
//   - All parameter atomics are written from the message thread, read from
//     the audio thread.
//   - noteOn/noteOff/allNotesOff/pitchBend may be called from the audio thread
//     (e.g. from the host MIDI buffer in processBlock) — they do not allocate.
//   - loadPreset / loadDrumKit / clearPreset MUST be called from the message
//     thread only; audio thread checks `ready` and bails out while a preset
//     swap is in flight.
//   - applyParams() and setDrumKeyParams() are message-thread; they only
//     write to atomics.
//==============================================================================

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>   // uint32_t / int32_t in the pseudo-RR xorshift
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>   // balanceRoleGainFn
#include <algorithm>       // std::find - PresetCache LRU touch
#include <array>
#include <limits>    // kNoGlide - a quiet NaN
#include "SynthCommon.h"
#include "BlobReader.h"
#include "InstrEditPanel.h"      // DrumKitParams / DrumElementParams
#include "SoundsFx.h"            // Chorus / AutoWah / Phaser / Reverb / StereoDelay
#include "SweetenerFx.h"         // per-slot SOFTEN / TAME / ROUND
#include "SynthKits.h"           // EDM KIT: live synth drum voices
#include "Sx920KitMap.h"          // SX920 kits' keys onto a composed kit's layout
#include "OscEngine.h"            // SOUND ENGINE: the built-in synth voice (GM 38 / 39)
#include "DrumSplitEq.h"          // the drum rack's 10-band EQ - a real band split

namespace Betel
{
    class DrumKitRegistry;       // forward — defined in DrumKitRegistry.h

    class Channel
    {
    public:
        // Forward declaration: the full struct is defined lower down in the
        // "Public data types" section, but the per-instrument pool methods
        // (decodeInstrument / adoptInstrument / loadInstrument) reference
        // std::shared_ptr<PresetVoice> in their signatures above that point.
        // A method's return/parameter types are parsed immediately (not in
        // complete-class context), so the name must be visible here.
        struct PresetVoice;

        Channel();
        ~Channel();

        // ---------------------------------------------------------------------
        // Lifecycle (called by SamplePlayerEngine)
        // ---------------------------------------------------------------------
        void prepare(double sampleRate, int blockSize);
        void release();

        // Adds this channel's voices into outBuffer (caller pre-clears the buffer
        // or accumulates from multiple channels). hostBPM is forwarded for the
        // loop-sync-to-host case and for delay-time computation.
        /** `sectionFx` is the SECTION's global effect rack, owned by the engine
            and shared by every channel in the section.  This channel adds itself
            into each of its three send buffers, scaled by its own send amounts,
            at the same moment it sums its dry into `outBuffer`.

            Null is legal and means "no section bus" — the channel is then simply
            dry, which is what it should be with nowhere to send. */
        void renderBlock(juce::AudioBuffer<float>& outBuffer, int numSamples, double hostBPM,
                         SectionSendFx* sectionFx = nullptr);

        // ---------------------------------------------------------------------
        // MIDI input (channel-resolved by engine, no MIDI channel byte needed)
        // ---------------------------------------------------------------------
        // fromEditor: the note came from the SOUNDS editor's piano strip, not
        // from the style / MIDI stream.  Editor notes are deliberately kept
        // separate — see Voice::fromEditor and editorKeyDown below.
        /** `extraGain` multiplies THIS voice's output and nothing else's.

            Added for the crash: the crash fires onto the DRUMS channel, which is
            shared with the style's own kit, so a channel-level gain would move
            the whole kit with it.  A per-voice multiplier moves only the hit
            that asked for it. */
        void noteOn(int note, int velocity, bool fromEditor = false,
                    float extraGain = 1.0f);
        void noteOff(int note, bool fromEditor = false);
        void allNotesOff();

        /** Which notes currently have at least one voice sounding on this
            channel, as a 128-bit mask (4 x 32).  Published once per render block
            from the VOICE POOL — not from note-on/off bookkeeping — so it
            self-clears when a one-shot drum ends or a release tail dies, and it
            reports the note actually heard (post-transpose).

            Read from the message thread by the editor's piano strip.  Relaxed
            ordering is deliberate: this is a display hint, and being one block
            stale is invisible at any repaint rate. */
        /** EDM KIT pad LEDs - MIDI truth.  A key is lit from its note-on to its
            note-off exactly (the key-down tallies: no latch, no ringing voice).
            A note shorter than one UI frame still flashes once: every note-on
            also sets a hit flag that THIS call consumes, so each hit is seen
            by exactly one poll.  One reader only - the EDM KIT editor. */
        void getPadActivity (uint32_t out[4]) const noexcept
        {
            for (int i = 0; i < 4; ++i)
                out[i] = padActivityMask[(size_t) i].load (std::memory_order_relaxed)
                       | padHitMask[(size_t) i].exchange (0u, std::memory_order_relaxed);
        }

        void getSoundingNotes (uint32_t out[4]) const noexcept
        {
            for (int i = 0; i < 4; ++i)
                out[i] = soundingMask[(size_t) i].load (std::memory_order_relaxed);
        }
        void pitchBend(float normalized); // -1.0 .. +1.0

        /** Re-pitch a still-sounding voice from oldNote to newNote WITHOUT a
            re-attack: the amp/filter/pitch envelopes, the sample read position
            and the region all stay intact, so the note simply changes pitch
            (the PITCH_SHIFT chord-change transition).  Drum channels are never
            re-pitched.  Returns true if a matching sounding voice was found.
            Audio thread only. */
        bool retuneVoice(int oldNote, int newNote) noexcept;

        // ---------------------------------------------------------------------
        // Preset loading (message thread only)
        // ---------------------------------------------------------------------
        void           clearPreset();
        void           loadPreset(BlobReader& reader, int presetIndex);

        // Pool API (the manifest-pool fix for RT-safe program changes).
        //  • preloadPreset : decode a blob preset into the channel's pool if it
        //    isn't there yet.  Message thread only (it decodes + allocates).
        //  • selectPooledPreset : make a previously-pooled preset active.  Cheap
        //    atomic pointer swap — safe to call from the audio thread.  If the
        //    preset was never preloaded it is a no-op (never decodes here).
        void           preloadPreset(BlobReader& reader, int presetIndex);
        void           selectPooledPreset(int presetIndex);

        // -- Per-instrument (small-blob) pool API ----------------------------
        // Grex per-instrument model: every instrument lives in its own .frb
        // (one preset at blob index 0).  These decouple the POOL KEY (the
        // instrument's numeric flag -- the leading "NNN" in "NNN-Name.frb")
        // from the in-blob preset index, so one channel can pool many
        // single-preset blobs and switch between them with a cheap atomic swap.
        //
        //  - preloadInstrument : decode blob preset `blobPresetIndex` and store
        //    it in the pool under `poolKey`.  Message thread only (decodes +
        //    allocates).  Samples are COPIED, so `reader` may be closed right
        //    after this returns.  No-op if `poolKey` is already pooled.
        //  - loadInstrument    : preloadInstrument + publish as the active voice
        //    (message thread; calls clearPreset()).
        //  - selectPooledPreset(poolKey) (above) republishes a pooled instrument
        //    by flag -- RT-safe, no decode.
        //
        // Like preloadPreset, these only ever INSERT into presetPool; the pool
        // is read on the audio thread by selectPooledPreset, so it is never
        // cleared while audio may be running.
        /** Decode a blob preset into a standalone, SHAREABLE PresetVoice
            (message thread; touches no channel state).  This is the expensive
            step -- the engine caches the result and hands the SAME shared_ptr
            to every channel that needs the instrument, so each instrument is
            decoded (and held in RAM) exactly once regardless of how many
            channels or styles use it. */
        static std::shared_ptr<PresetVoice> decodeInstrument (BlobReader& reader,
                                                              int blobPresetIndex)
        {
            if (blobPresetIndex < 0) return nullptr;
            const auto& presets = reader.getPresets();
            if (blobPresetIndex >= (int) presets.size()) return nullptr;
            const auto& preset = presets[(size_t) blobPresetIndex];

            auto pv = std::make_shared<PresetVoice>();
            pv->name = juce::String (preset.name);
            for (const auto& blobRegion : preset.regions)
            {
                auto fs = reader.getSampleDataFloat (blobRegion);
                if (fs.numFrames == 0) continue;

                Region reg;
                reg.lokey          = blobRegion.keyRangeLow;
                reg.hikey          = blobRegion.keyRangeHigh;
                reg.pitchKeycenter = blobRegion.rootKey;
                reg.lovel          = blobRegion.velRangeLow;
                reg.hivel          = blobRegion.velRangeHigh;
                reg.hasLoop        = blobRegion.loopEnabled > 0;
                reg.loopStart      = blobRegion.loopStart;
                reg.loopEnd        = blobRegion.loopEnd;
                reg.volume         = -blobRegion.attenuation;
                reg.tune           = (int) blobRegion.fineTune
                                   + (int) blobRegion.coarseTune * 100;
                reg.offset         = 0;
                reg.roleId         = blobRegion.elementRoleId;

                SampleData sd;
                fillSampleData (sd, fs.data.data(), fs.channels, fs.numFrames, fs.sampleRate);

                pv->regions.push_back (reg);
                pv->samples.push_back (std::move (sd));
            }

            pv->computeKeyRange();   // mapped register — see PresetVoice::loKey
            return pv;
        }

        /** Move a decoded voice bodily by `semitones` - key ranges AND
            keycenters together.

            Both, and that is the whole point.  A voice's playback pitch is
            (note - pitchKeycenter), so moving the key range on its own would
            re-pitch every sample by exactly the amount it was moved: the kick
            would answer the right key and sound an octave wrong.  Moving the
            keycenter with it holds that difference at zero, which is what a
            drum needs - the same recording, on a different key.

            Message thread, and ONCE per decode: a PresetVoice is immutable after
            it is published, and the sampled-kit cache shares one voice between
            every channel holding that kit, so a shift applied at adoption time
            would land twice the moment DRUMS and PERC both took it. */
        static void transposeVoiceKeys (PresetVoice& pv, int semitones) noexcept
        {
            if (semitones == 0) return;

            for (auto& r : pv.regions)
            {
                r.lokey          = juce::jlimit (0, 127, r.lokey          + semitones);
                r.hikey          = juce::jlimit (0, 127, r.hikey          + semitones);
                r.pitchKeycenter = juce::jlimit (0, 127, r.pitchKeycenter + semitones);
            }

            pv.computeKeyRange();      // the mapped span moved with the regions
        }

        /** Adopt an already-decoded (possibly shared) instrument into this
            channel's pool, optionally publishing it as the active voice.
            Near-free: no decode, no sample copy. */
        void adoptInstrument (int poolKey, std::shared_ptr<PresetVoice> pv, bool publish)
        {
            if (pv == nullptr || poolKey < 0) return;
            // Shared adopt — no decode, no copy.  A slot sharing an instrument
            // with another costs nothing here.
            presetPool[poolKey] = pv;
            if (publish)
            {
                clearPreset();
                std::atomic_store (&active, pv);
                blobVoice = pv;            // the source an SFZ sleeps back to
                presetName = pv->name;
                currentInstrumentFlag.store (poolKey);
                ready.store (true);
            }
        }

        /** Publish an already-decoded WHOLE SAMPLED KIT as this channel's voice.

            Same shape as adoptInstrument, minus two things, and both omissions
            are deliberate:

              * NO POOL ENTRY.  A sampled kit is one preset in its own .frb, so
                every one of them is blob index 0.  loadPreset() pooled by that
                index, which meant the SECOND kit selected on a channel found
                the FIRST kit's entry under key 0, took preloadPreset's
                "already pooled" early return, and republished the kit that was
                already there -- the grid lit the new cell and the sound never
                changed.  The decode cache now lives in the engine, keyed on
                msb/lsb/pc, where the key describes the KIT rather than its
                position inside a file.

              * NO INSTRUMENT FLAG.  A channel holding a kit holds no melodic
                instrument.  Stamping a flag here would send the SFZ-drop path
                (dropSfzVoice -> selectChannelPreset(flag)) off to fetch a
                melodic sound that does not exist, and the kit would vanish. */
        void adoptFullKit (std::shared_ptr<PresetVoice> pv)
        {
            if (pv == nullptr) return;
            clearPreset();
            std::atomic_store (&active, pv);
            blobVoice  = pv;              // the source an SFZ sleeps back to
            presetName = pv->name;
            fullKitActive.store (true);   // KICK MIX is composed-kits-only
            ready.store (true);
        }

        void preloadInstrument (BlobReader& reader, int poolKey, int blobPresetIndex)
        {
            if (poolKey < 0) return;
            if (presetPool.find (poolKey) != presetPool.end()) return;   // already pooled
            if (auto pv = decodeInstrument (reader, blobPresetIndex))
                presetPool[poolKey] = std::move (pv);
        }

        void loadInstrument (BlobReader& reader, int poolKey, int blobPresetIndex)
        {
            clearPreset();
            preloadInstrument (reader, poolKey, blobPresetIndex);

            auto it = presetPool.find (poolKey);
            if (it == presetPool.end()) return;

            std::atomic_store (&active, it->second);
            blobVoice = it->second;        // the source an SFZ sleeps back to

            const auto& presets = reader.getPresets();
            if (blobPresetIndex >= 0 && blobPresetIndex < (int) presets.size())
                presetName = juce::String (presets[(size_t) blobPresetIndex].name);

            currentInstrumentFlag.store (poolKey);
            ready.store (true);
        }

        /** EMPTY state: fully unload this channel.  clearPreset() publishes the
            null instrument (the audio + MIDI paths already bail on it: noteOn
            checks ready/active/regions and the renderer checks haveContent, so
            every incoming MIDI stream is ignored), kills sounding voices, and
            clears the names/flag.  On top of that the per-channel decode pool
            is released, so a previous style's samples on an untargeted slot
            stop holding RAM.  Message thread only. */
        void clearInstrument()
        {
            clearPreset();
            presetPool.clear();     // free the previous style's decoded blobs
        }

        /** True if an instrument with this flag has already been decoded into
            the channel's pool (message-thread query). */
        bool isInstrumentPooled (int poolKey) const
        {
            return presetPool.find (poolKey) != presetPool.end();
        }
        bool           isReady()       const { return ready.load(); }
        juce::String   getPresetName() const { return presetName; }

        /** The flag of the instrument currently PUBLISHED on this channel
            (-1 = none / drum kit).  Updated by every publish path —
            adoptInstrument, loadInstrument, selectPooledPreset — and cleared
            by clearPreset/loadDrumKit, so the UI can mirror what the engine
            is actually playing regardless of WHO selected it (style setup,
            runtime PC via CASM, set restore, or user click). */
        int getCurrentInstrumentFlag() const { return currentInstrumentFlag.load(); }

        /** Record the flag a style asked for WITHOUT loading it.

            Used while a user SFZ holds the channel: the style's program change
            is ignored, but the flag it wanted is remembered so switching the
            override off restores exactly that sound instead of silence. */
        void noteCurrentInstrumentFlag (int flag) { currentInstrumentFlag.store (flag); }

        /** Publish a user SFZ as this channel's voice.

            Deliberately NOT pooled: the pool is keyed by instrument flag and an
            SFZ has none, and pooling a possibly-hundreds-of-MB voice under a
            fake key would keep it alive long after the user switched it off. */
        /** Park an SFZ on this channel and select it.

            The blob voice is NOT discarded - it is remembered as the sleeping
            source, so selectSfz(false) brings the style's own instrument back
            instantly with no decode. */
        void adoptSfzVoice (std::shared_ptr<PresetVoice> pv)
        {
            if (pv == nullptr) return;

            // Whatever is playing now is the BLOB source, unless an SFZ already
            // held the channel - in which case the blob is already parked.
            if (! sfzSelected.load())
                blobVoice = std::atomic_load (&active);

            pv->computeKeyRange();
            sfzVoice = pv;
            selectSfz (true);
        }

        /** Swap which source sounds.  An atomic pointer store, nothing more.

            Notes already sounding keep playing from the voice they started on -
            each Voice holds its own region pointers - so the switch never cuts
            a ringing note in half. */
        void selectSfz (bool useSfz)
        {
            auto& wanted = useSfz ? sfzVoice : blobVoice;
            if (wanted == nullptr) return;

            sfzSelected.store (useSfz);
            std::atomic_store (&active, wanted);
            presetName = wanted->name;
            ready.store (true);
        }

        bool isSfzSelected()  const noexcept { return sfzSelected.load(); }
        bool hasSfzVoice()    const noexcept { return sfzVoice != nullptr; }

        /** Forget the SFZ entirely and fall back to the blob source.  Used when
            a style change drops every override. */
        void dropSfzVoice()
        {
            if (blobVoice != nullptr) selectSfz (false);
            sfzSelected.store (false);
            sfzVoice.reset();
        }
        int            getNumRegions() const { return (int) getRegions().size(); }

        // ---------------------------------------------------------------------
        // Drum-kit loading (message thread only)
        //
        // loadDrumKit composes this channel's region table by walking the 128
        // per-key entries in `kit` and pulling the matching element from the
        // registry. Entries with empty sourceKit are skipped (key silent).
        // Existing regions are cleared first; you do NOT need to call
        // clearPreset() beforehand.
        //
        // After loadDrumKit returns, the channel:
        //   - has regions[i].lokey == regions[i].hikey == that key's MIDI note,
        //     so the standard noteOn → findRegion path works unchanged.
        //   - has drumKeyStates[note] populated with the per-key user-edit
        //     values (gain / pitch / cutoff / A / D / R / round-robin).
        //
        // setDrumKeyParams updates ONE key's runtime params (gain etc.) without
        // touching the sample.  Use this for slider drags in the Kitton popup.
        // ---------------------------------------------------------------------
        void loadDrumKit (const DrumKitRegistry& registry, const DrumKitParams& kit);
        // EDM KIT for STYLE kit calls: a per-channel pool of synth kits keyed
        // by the style's drum program.  preload = message thread (resolves the
        // base kit, allocates); select = any thread incl. audio (pointer swap
        // only - no lock, no file, no allocation; syncName must be false there).
        void preloadSynthKit      (int program, const edm::Kit& kit);
        bool isSynthKitPooled     (int program) const;
        bool selectPooledSynthKit (int program, bool syncName);
        /** The EdmKitFiles base version a pooled kit was resolved under (message thread). */
        int  synthKitPoolVersion    (int program) const noexcept { return (program >= 0 && program < 128) ? edmPoolVer[program] : -1; }
        void setSynthKitPoolVersion (int program, int v) noexcept { if (program >= 0 && program < 128) edmPoolVer[program] = v; }
        void clearDrumKit() { clearPreset(); }
        void setDrumKeyParams (int midiKey, const DrumElementParams& p);

        // ---------------------------------------------------------------------
        // Drum-kit POOL — the kit analogue of the melodic presetPool.  A kit
        // reload (decoding one sample per key) is the single heaviest thing a
        // style transition can do; doing it on the audio thread is what spikes
        // the CPU.  preloadDrumKit composes a kit into the pool ONCE on the
        // message thread (at style load); selectPooledDrumKit then switches to
        // it with an atomic pointer swap — never a decode.  Because every
        // sounding voice captures its own PresetVoice, the swap is graceful:
        // hits already ringing on the old kit finish on it, new hits use the
        // new kit.  poolKey is the drum Program-Change value.
        // ---------------------------------------------------------------------
        /** Drop a pooled kit so the next preload re-composes it.
        
            The pool is keyed by drum PC and its entries are immutable for the
            life of a style — which is exactly right at playback and exactly
            wrong after a SAVE AS DEFAULT: the .drm on disk changed, but the
            warm entry still holds the kit as it was, and a program change takes
            the fast pointer-swap path and never re-resolves.  That is why a
            saved rack appeared not to load. */
        void invalidatePooledDrumKit (int poolKey)
        {
            if (poolKey >= 0) drumKitPool.erase (poolKey);
        }

        /** Drop EVERY warm kit. Same reasoning as invalidatePooledDrumKit above,
            for the case where the thing that changed is not one kit but the
            per-DEPARTMENT trims in balance.grexv - those are BAKED into each
            region's volume at compose time, so a warm entry would keep serving
            the old numbers however many times the file is re-read. */
        void clearDrumKitPool() { drumKitPool.clear(); }

        void preloadDrumKit (const DrumKitRegistry& registry,
                             const DrumKitParams& kit, int poolKey);
        /** Switch to a kit already in the pool.  RT-safe when syncName == false
            (atomics + bounded per-key stores only); pass syncName = true ONLY
            from the message thread (it touches juce::String UI fields).  Returns
            false if the kit isn't pooled. */
        bool selectPooledDrumKit (int poolKey, bool syncName = false);
        bool isDrumKitPooled (int poolKey) const
        {
            return drumKitPool.find (poolKey) != drumKitPool.end();
        }
        int  getActiveDrumPoolKey() const { return activeDrumPoolKey.load(); }

        /** Push the kit-wide FX bus params (10-band EQ + Saturation +
            Compressor + Reverb + Delay) onto this channel.  Atomics only —
            RT-safe, no allocation.  When the channel is not in drum mode the
            FX bus is silently ignored at render time. */
        void applyDrumKitFx (const DrumKitFxParams& fx);

        /** Push the SWEETENER block.  Called from BOTH applyParams (melodic
            bulk push) and applyDrumKitFx (drum rack push), which is what keeps
            the two kinds of slot on one implementation. */
        void setSlotVelCurve (float c) noexcept
        { slotVelCurve.store (juce::jlimit (-1.0f, 1.0f, c)); }

        /** GLOBAL low-velocity lift, applied BEFORE the per-slot curve.

            A separate atomic rather than folded into slotVelCurve, because the
            two have different owners and different lifetimes: the slot curve is
            reloaded from the .ins on every program change, and adding the
            global amount into it would be undone by the next instrument switch.
            Keeping them apart means each is written by exactly one thing. */
        void setGlobalVelCurve (float c) noexcept
        { globalVelCurve.store (juce::jlimit (-1.0f, 1.0f, c)); }

        //----------------------------------------------------------------------
        // IGNORE PRESET CHANGES - freeze this channel's VOICE.
        //
        // While set, selectChannelPreset loads the new samples but does NOT
        // apply the incoming instrument's saved .ins/.sins on top of them, so a
        // set load, a style program change or a runtime PC all leave the voicing
        // exactly where the user left it.
        //
        // On the CHANNEL rather than in the UI because that is the only place
        // every route converges: the editor could gate its own two paths and
        // still be overwritten by a program change arriving from the style.
        //----------------------------------------------------------------------
        void setIgnorePresetParams (bool b) noexcept { ignorePresetParams.store (b); }
        bool getIgnorePresetParams() const noexcept  { return ignorePresetParams.load(); }

        void applySweetenerParams (const SweetenerParams& sp) noexcept
        {
            sweetEnabled    .store (sp.enabled);
            sweetMix        .store (juce::jlimit (0.0f, 1.0f,   sp.mix));
            sweetSoftenOn   .store (sp.softenOn);
            sweetSoftenDepth.store (juce::jlimit (-1.0f, 1.0f,  sp.softenDepth));
            sweetSoftenMs   .store (juce::jlimit (1.0f, 150.0f, sp.softenMs));
            sweetPeakOn     .store (sp.peakOn);
            sweetPeakCeilDb .store (juce::jlimit (3.0f, 24.0f,  sp.peakCeilDb));
            sweetPeakRatio  .store (juce::jlimit (1.0f, 20.0f,  sp.peakRatio));
            sweetTameOn     .store (sp.tameOn);
            sweetTameDepthDb.store (juce::jlimit (0.0f, 12.0f,  sp.tameDepthDb));
            sweetTameFreqHz .store (juce::jlimit (500.0f, 12000.0f, sp.tameFreqHz));
            sweetRoundOn    .store (sp.roundOn);
            sweetRoundDrive .store (juce::jlimit (0.0f, 1.0f,   sp.roundDrive));
            sweetRoundMix   .store (juce::jlimit (0.0f, 1.0f,   sp.roundMix));
        }

        /** Gain reduction the sweetener is applying right now, in dB.  Read by
            the editor's meters; 0 means the stage is doing nothing. */
        float getSweetSoftenGrDb() const noexcept { return sweetenerFx.getSoftenGrDb(); }
        float getSweetTameGrDb()   const noexcept { return sweetenerFx.getTameGrDb();   }
        float getSweetPeakGrDb()   const noexcept { return sweetenerFx.getPeakGrDb();   }
        float getSweetRoundGrDb()  const noexcept { return sweetenerFx.getRoundGrDb();  }

        /** Live per-key FX-send update (slider drag).  RT-safe atomic write. */
        void setDrumKeyFxSend (int midiKey, float sendNorm);

        /** Name of the kit last loaded by loadDrumKit (or empty if loaded by
            loadPreset).  Read-only convenience for UI status displays. */
        juce::String getLoadedDrumKitName() const { return loadedDrumKitName; }

        /** Record which kit a channel holds when it did NOT come from
            composeDrumKit.

            A full sampled kit reaches the channel through loadPreset, which
            knows nothing about kits - so without this the UI has no name to show
            and no cell to light, which is exactly why the sampled kits were
            invisible after loading correctly. */
        void setLoadedDrumKitName (const juce::String& n) { loadedDrumKitName = n; }

        // ---------------------------------------------------------------------
        // Channel identity / role
        // ---------------------------------------------------------------------
        //======================================================================
        // THE SHARED DECODED-PRESET CACHE
        //
        // ONE decode per sound for the whole plugin, kept ACROSS style changes.
        // Before this, every channel unpacked its own copy of the same voice -
        // up to 24 copies of one piano - and a style change threw all of them
        // away, which is why every third or fourth pick stalled for seconds
        // while an already-seen sound was unpacked again.
        //
        // WHY THE CACHE IS NOT THE THING THE AUDIO THREAD READS.
        //
        // `selectPooledPreset` runs on the AUDIO thread and looks its voice up
        // in `presetPool`. A cache that EVICTS cannot be that map: the message
        // thread erasing an entry while the audio thread walks it is undefined
        // behaviour, and eviction is the whole point of a cache with a ceiling.
        //
        // So there are two structures with different jobs:
        //
        //   * THIS CACHE - message thread only. Owns the decoded voices, does
        //     the LRU, and is the only thing that ever erases.
        //   * `presetPool` - per channel, INSERT ONLY, audio-thread readable,
        //     exactly as before. Its entries are now shared_ptr COPIES of cache
        //     entries, so they cost a pointer rather than a second unpack.
        //
        // EVICTION IS SAFE BY REFERENCE COUNT. Dropping an entry from the cache
        // releases one shared_ptr; any channel still holding it, and the audio
        // thread's published `active`, keep the audio alive until they let go.
        // Nothing is ever freed under a reader.
        //======================================================================
        struct PresetCache
        {
            std::unordered_map<int, std::shared_ptr<PresetVoice>> map;
            std::vector<int>  lru;                       // oldest first
            size_t            bytes    = 0;
            size_t            capBytes = 512ull * 1024 * 1024;   // 512 MB

            std::shared_ptr<PresetVoice> get (int key)
            {
                auto it = map.find (key);
                if (it == map.end()) return {};
                touch (key);
                return it->second;
            }

            void put (int key, std::shared_ptr<PresetVoice> pv, size_t sizeBytes)
            {
                if (pv == nullptr || map.count (key)) return;
                map[key] = std::move (pv);
                lru.push_back (key);
                bytes += sizeBytes;
                sizes[key] = sizeBytes;
                evictWhileOver();
            }

            /** The blob changed, so every decoded voice in here describes audio
                that no longer exists at those indices. The ONLY invalidation
                point - a style change deliberately does not clear this. */
            void clear() { map.clear(); lru.clear(); sizes.clear(); bytes = 0; }

        private:
            std::unordered_map<int, size_t> sizes;

            void touch (int key)
            {
                auto it = std::find (lru.begin(), lru.end(), key);
                if (it != lru.end()) lru.erase (it);
                lru.push_back (key);
            }

            void evictWhileOver()
            {
                // Never evict the entry just added, however large it is - a
                // single voice bigger than the whole ceiling would otherwise be
                // decoded and dropped on every pick, which is worse than being
                // over budget.
                while (bytes > capBytes && lru.size() > 1)
                {
                    const int victim = lru.front();
                    lru.erase (lru.begin());
                    bytes -= sizes[victim];
                    sizes.erase (victim);
                    map.erase (victim);
                }
            }
        };

        /** balance.grexv's per-DEPARTMENT trim, consulted at kit-compose time.
            A std::function rather than a pointer to the file so Channel needs no
            knowledge of it - the engine owns the file and hands over a lookup. */
        void setBalanceRoleGainFn (std::function<float (int)> fn)
        {
            balanceRoleGainFn = std::move (fn);
        }

        void setSharedPresetCache (PresetCache* c) noexcept { sharedPresets = c; }

        void setIndex(int idx)         { channelIndex = idx; }

        //======================================================================
        // THE SET OWNS THIS CHANNEL'S VOICE - so nothing may neutral-stamp it.
        //
        // THE EVIDENCE, and it is a controlled experiment the project already
        // ran: the TWO-HANDLE BAND FILTER is the one editor value that survives
        // a set load intact, and it is the one editor value stored OUTSIDE
        // ChannelParams (bandHpFreq / bandLpFreq, written only by
        // setBandFilterHz).  applyParams(ChannelParams{}) - the "no saved
        // voice, use a neutral one" stamp - physically cannot reach it.
        // Everything that DID die (ADSR, filter, EQ, LFOs, FX) lives inside
        // ChannelParams and is wiped by that one line.
        //
        // A style channel has no .ins by design now, so EVERY program change it
        // receives takes the no-saved-voice branch and re-stamps defaults.  The
        // set's commit lands correctly and is erased by the next PC, which is
        // why it only ever looked right just after the editor re-committed it.
        //
        // Set for channels 0..kNumStyleChannels-1 by the engine constructor.
        // The right hand is NOT owned this way: its .ins IS its authority, and
        // the neutral stamp is what stops one sound inheriting another's voice.
        //======================================================================
        void setVoiceOwnedBySet (bool b) noexcept { voiceOwnedBySet.store (b); }

        //======================================================================
        // REVO! LOW-ZONE SILENCING
        //
        // A Revo! kit (bank LSB 8 on Genos) does NOT use the XG map below 29.
        // Yamaha's own Drum Kit Assign List puts HI-HAT articulations there:
        //
        //     key   StandardKit          Revo!
        //     13-20 Surdo/HiQ/Whip/      Hi-Hat Tip & Edge, 00 -> 99 open
        //           Scratch/Snap/Click
        //     21-22 Metronome Click/Bell Hi-Hat Pedal Closed / Pedal Splash
        //     23-24 Seq Click L / H      Seq Click L / H     <- IDENTICAL
        //     25-28 Brush Tap/Swirl/     Tambourine Stick,
        //           Slap/TapSwirl        Snare 4/3/2 no-Rim
        //     29-34 Snare Roll, Castanet,Snare Roll PD, Snare 3/4, Stick Count,
        //           Snare Soft, Sticks,  Kick 4, Snare 2 Open Rim
        //           Kick Soft, Open Rim  <- COMPATIBLE, kick and snare survive
        //
        // gm_first and the_second supply the XG reading of 13..34.  On a Revo!
        // style that is the wrong instrument entirely: a style writing its HAT
        // GROOVE at key 16 gets a whip slap on every eighth (CountryShuffle1 does
        // exactly this, 52 hits).
        //
        // revo_first now supplies 13..22 for Revo! kits, and the test below only
        // drops a key that resolved to NOTHING - so this is a fallback for a
        // library without it, not the blanket silence it used to be.  Silence
        // remains the honest answer for an unsampled articulation - the same rule
        // the bank-126 fallback already follows, that a
        // wrong drum is worse than an absent one.
        //
        // 23 and 24 are DELIBERATELY NOT SILENCED: both kits agree on them, so
        // dropping them would throw away correct content.  29..34 are left alone
        // for the same reason - that is where the kick and snare live.
        //======================================================================
        static constexpr bool isRevoRemappedLowKey (int key) noexcept
        {
            return (key >= 13 && key <= 22)    // hi-hat articulations on Revo!
                || (key >= 25 && key <= 28);   // tambourine + no-rim snares
        }

        /** Message thread.  Set when a kit is published, from the style's own
            bank LSB - not from runningBankLsb, which applyVoiceSetup flattens to
            0 immediately after publishing. */
        void setRevoLowZone (bool b) noexcept { revoLowZone.store (b); }

        /** The drum kit the STYLE asked for (bank, LSB, program), so its notes can
            be re-pointed onto a composed kit's layout (Sx920KitMap.h). */
        void setStyleKitAddress (int msb, int lsb, int pc) noexcept
        { styleKitMsb.store (msb); styleKitLsb.store (lsb); styleKitPc.store (pc); }

        /** The EDM kit this channel plays, or null.  Lock-free: the audio thread
            may ask (the Crash tab fires the kit's own crash pads). */
        std::shared_ptr<const edm::Kit> getSynthKit() const noexcept { return std::atomic_load (&synthKit); }
        bool getRevoLowZone() const noexcept  { return revoLowZone.load(); }
        bool getVoiceOwnedBySet() const noexcept  { return voiceOwnedBySet.load(); }
        void setOctaveBias(int oct)    { octaveBias.store(oct); }
        void setSemitoneOffset(int s)  { semitoneOffset.store(s); }

        /** Sounding voices, for the spike diagnostics' census.  Audio
            thread only; a plain scan of the fixed voice pool, no locking. */
        int activeVoiceCount() const noexcept
        {
            int n = 0;
            for (const auto& v : voices) if (v.active) ++n;
            return n;
        }
        int  getIndex() const          { return channelIndex; }
        void setDrumChannel(bool b)    { isDrumChannel.store(b); }

        /** Set once by SamplePlayerEngine's constructor.  Gates ATTACK GLIDE,
            which is a right-hand feature - see SlotParams::glideMode. */
        void setSoloChannel(bool b)    { isSoloChannel.store(b); }
        bool getSoloChannel() const    { return isSoloChannel.load(); }
        bool getDrumChannel() const    { return isDrumChannel.load(); }

        /** Does this channel currently have anything mapped at `note`?
            Diagnostic only — the crash feature reports it so a silent cymbal can
            be told apart from a cymbal the kit never had. */
        bool hasRegionForNote (int note) const { return findRegion (note, 100) >= 0; }

        /** THE LOADED STYLE PLAYS THIS SLOT AS CHORDS.

            An override at the point of use, exactly like the `fromEditor` gate
            beside it, and deliberately NOT a write to playMode: the user's Mono
            setting is theirs, it stays in the set, it stays on screen, and it
            comes straight back when a style that plays lines is loaded.  A
            style-scoped fact should not overwrite a user-scoped one.

            Set at style load, cleared for every slot first, so nothing leaks
            from the previous style. */
        void setStyleForcedPoly (bool forced) noexcept { styleForcedPoly.store (forced); }
        bool isStyleForcedPoly() const noexcept        { return styleForcedPoly.load(); }

        // Configure mono note-priority directly with the three INDEPENDENT
        // switches (multi-select), bypassing the legacy single-select
        // monoSubMode.  Used e.g. to make the bass slot mono by default.
        void setMonoConfig (bool mono, bool holdStolen,
                            bool retrigNew, bool retrigStolen) noexcept
        {
            playMode        .store (mono ? Mono : Poly);
            monoHoldStolen  .store (holdStolen);
            monoRetrigNew   .store (retrigNew);
            monoRetrigStolen.store (retrigStolen);
        }

        // =====================================================================
        // Public data types
        // =====================================================================
        struct Region
        {
            int   lokey = 0,  hikey = 127;
            int   pitchKeycenter = 60;
            int   lovel = 0,  hivel = 127;
            bool  hasLoop = false;
            int64_t loopStart = 0, loopEnd = 0;
            float volume = 0.0f;  // dB
            int   tune   = 0;     // cents
            int   offset = 0;

            // Drum-element role (DrumElementRole enum, 0 = Unset).  Stored
            // only for traceability — the live per-key user state lives in
            // Channel::drumKeyStates indexed by lokey.
            uint8_t roleId = 0;

            // KICK MIX blend-layer tag (notes 35 & 36 only).  -1 = a normal
            // region (matched by findRegion as usual).  0 = EDM blend layer,
            // 1 = WOOD blend layer: these are EXCLUDED from findRegion and are
            // triggered explicitly by noteOn as a second (supplemental) voice
            // whose level is the equal-power "supplemental" side of the mix.
            int kickMixVariant = -1;
        };

        struct SampleData
        {
            juce::AudioBuffer<float> buffer;
            double sampleRate  = 44100.0;
            int    numChannels = 1;
        };

        //======================================================================
        // Deinterleave a decoded blob sample into SampleData — the ONE place
        // every decode path builds its buffers, so the rules below hold for
        // melodic voices, drum kits, kick-mix layers and the global one-shots
        // alike.
        //
        // Two things happen here:
        //
        //  1. DUAL-MONO COLLAPSE.  Sample libraries are full of "stereo" files
        //     whose two channels are bit-identical — the same signal stored
        //     twice.  Those are written as ONE channel: half the RAM, and the
        //     renderer's mono fast path then also halves their filter cost.
        //     The check is exact equality, so a genuinely stereo sample (even a
        //     very narrow one) is never collapsed and never loses its image.
        //
        //  2. Everything else is stored with its real channel count, and the
        //     renderer reads channel 1 for the right output — true stereo.
        //     Before this, the renderer read channel 0 only and fanned it out,
        //     so every stereo sample paid double RAM for an image that was then
        //     discarded.
        //
        // Message thread (decode time); never called from the audio thread.
        //======================================================================
        static void fillSampleData (SampleData& sd,
                                    const float* interleaved,
                                    int channels, int numFrames, double sampleRate)
        {
            sd.sampleRate = sampleRate;

            channels  = juce::jmax (0, channels);
            numFrames = juce::jmax (0, numFrames);

            if (interleaved == nullptr || channels <= 0 || numFrames <= 0)
            {
                sd.numChannels = 1;
                sd.buffer.setSize (1, 0);
                return;
            }

            int storeCh = channels;
            if (channels == 2)
            {
                bool identical = true;
                for (int s = 0; s < numFrames && identical; ++s)
                    identical = (interleaved[(size_t) (s * 2)]
                                 == interleaved[(size_t) (s * 2 + 1)]);
                if (identical) storeCh = 1;      // dual-mono -> store once
            }

            sd.numChannels = storeCh;
            sd.buffer.setSize (storeCh, numFrames);
            for (int c = 0; c < storeCh; ++c)
            {
                float* dst = sd.buffer.getWritePointer (c);
                for (int s = 0; s < numFrames; ++s)
                    dst[s] = interleaved[(size_t) (s * channels + c)];
            }
        }

        // A fully-decoded instrument: its region map + the sample data each
        // region points at.  Presets are decoded once (message thread) and held
        // immutably in the channel's pool; switching instrument is then just an
        // atomic pointer swap of `active`, never a decode.  Sounding voices
        // capture the PresetVoice they started on, so a mid-ring program change
        // never retunes or frees a note that's still playing.
        struct PresetVoice
        {
            std::vector<Region>     regions;
            std::vector<SampleData> samples;
            juce::String            name;     // preset display name (set at decode)

            // ── Mapped key span (min lokey .. max hikey over every playable
            //    region), computed ONCE at build time by computeKeyRange() —
            //    decodeInstrument() for melodic voices, composeDrumKit() for
            //    kits.  A PresetVoice is immutable after construction, so these
            //    are plain ints: the audio thread reads them straight off the
            //    published `active` pointer with no extra synchronisation.
            //
            //    This is the register the instrument ACTUALLY has samples for.
            //    StylePlayer's MegaVoice articulation fallback folds a note into
            //    it so the voice answers with a real region at its natural
            //    keycenter instead of findRegion() borrowing a distant octave and
            //    stretching it (see foldToInstrumentRange).  A voice with no
            //    regions reports the full 0..127 span, so callers fold nothing.
            int loKey = 0;
            int hiKey = 127;

            void computeKeyRange() noexcept
            {
                int lo = 128, hi = -1;
                for (const auto& r : regions)
                {
                    if (r.kickMixVariant >= 0) continue;   // blend layer: not key-mapped
                    lo = juce::jmin (lo, r.lokey);
                    hi = juce::jmax (hi, r.hikey);
                }
                if (lo > hi) { loKey = 0; hiKey = 127; return; }   // nothing mapped
                loKey = juce::jlimit (0, 127, lo);
                hiKey = juce::jlimit (0, 127, hi);
            }
        };

        // A fully-composed drum kit held in the pool.  `voice` is the kit's
        // region/sample table (decoded once, message thread); `keys` is the
        // per-key engine-unit state snapshot (what loadDrumKit would write into
        // drumKeyStates); `fx` is the kit-wide FX-bus snapshot.  publishDrumKit
        // applies all three with atomics + an atomic pointer swap of `active`,
        // so a runtime kit switch is a swap, never a decode.
        struct PooledDrumKit
        {
            struct KeySnap
            {
                float gain          = 1.0f;
                float pitchSemi     = 0.0f;
                float cutoffNorm    = -1.0f;
                float attackMs      = -1.0f;
                float decayMs       = -1.0f;
                float sustain       = -1.0f;
                float releaseMs     = -1.0f;
                float fxSendNorm    = 0.0f;
                int   roundRobinAmount = 0;   // 0..100 pseudo-RR depth
                bool  fullLength    = true;
                // KICK MIX per-key blend controls (notes 35/36; ignored else).
                bool  kickMixEnabled = false;   // default OFF for non-kick keys
                float kickMixAmount  = 50.0f;   // 0..100 (equal-power crossfade)
                int   kickMixVariant = 0;       // 0 = EDM, 1 = WOOD
            };
            std::shared_ptr<PresetVoice>     voice;
            juce::String                     name;
            DrumKitFxParams                  fx;
            std::array<KeySnap, 128>         keys {};
        };

        /** The mapped key span of the instrument currently PUBLISHED on this
            channel — the lowest lokey and highest hikey it has samples for.
            Reports 0..127 when nothing is loaded, so callers fold nothing.

            Audio-thread safe: one atomic_load of the active pointer (the same
            one noteOn / findRegion / renderBlock already do), then two plain
            reads of an immutable struct.  Used by StylePlayer to fold MegaVoice
            articulation keys into a register the voice can actually answer. */
        void getMappedKeyRange (int& lo, int& hi) const noexcept
        {
            auto pv = std::atomic_load (&active);
            if (pv == nullptr) { lo = 0; hi = 127; return; }
            lo = pv->loKey;
            hi = pv->hiKey;
        }

        /** Total octave shift this channel adds at note-on: the instrument's
            OCTAVE slider (octaveOffset) plus the hidden per-channel bias.  A
            caller that pre-folds a note has to know it, or the shift applied
            inside noteOn() pushes the note back out of the mapped register. */
        int getOctaveShift() const noexcept
        {
            return octaveOffset.load() + octaveBias.load();
        }

        const std::vector<Region>& getRegions() const
        {
            static const std::vector<Region> empty;
            auto pv = std::atomic_load (&active);
            return pv ? pv->regions : empty;
        }
        const std::vector<SampleData>& getSamples() const
        {
            static const std::vector<SampleData> empty;
            auto pv = std::atomic_load (&active);
            return pv ? pv->samples : empty;
        }

        // =====================================================================
        // ChannelParams — POD push-bag the host fills from its slot store and
        // hands to applyParams(). Units are EXACTLY what the engine atomics
        // expect (ms for envelopes, Hz for cutoff/freq, dB for EQ gain, etc.),
        // so the host is the sole place that converts from any other unit
        // system (e.g. SlotParams' seconds).
        // =====================================================================
        struct ChannelParams
        {
            juce::String engineSpec;   // SOUND ENGINE: oscengine text, empty = factory
            // Amp envelope (ms / 0..1 sustain)
            float ampAttack = 10.0f, ampHold = 0.0f, ampDecay = 200.0f,
                  ampSustain = 0.8f, ampRelease = 300.0f;
            int   ampCurve  = 0;      // legacy 0=Exp 1=Lin 2=Log
            float ampCurveK = 6.0f;   // what the envelope uses; see SlotParams

            // Stereo pan, -1 (full left) .. +1 (full right), 0 = centre.
            float pan = 0.0f;

            // NOTE: the per-sound calibration trim is deliberately NOT here.
            // ChannelParams is what a BULK push carries, and a bulk push is
            // exactly what must not be able to move a calibration — see the
            // note on Channel::instrumentGain.

            // Filter
            int   filterType     = 0;          // 0=LP, 1=HP, 2=BP, 3=Notch
            float filterCutoff   = 20000.0f;   // Hz
            float filterReson    = 0.0f;       // 0..1
            int   variationAmount = 0;         // 0..100 melodic pseudo-RR
            float filterKeytrack = 0.0f;       // cents per semitone from C4

            // THE TWO-HANDLE BAND FILTER IS NOT HERE ANY MORE, ON PURPOSE.
            //
            // It is a PER-CHANNEL effect, like the mixer fader and the
            // calibration trim: it belongs to the channel strip, not to the
            // sound sitting on it.  Carrying it in this struct is what let a
            // mid-performance program change wipe it -- selectChannelPreset
            // pushes a default-constructed ChannelParams for any instrument
            // with no saved voice, and that reset the band to 20 Hz / 20 kHz
            // under a playing style.  See Channel::setBandFilterHz.
            //
            // Removed rather than merely ignored by applyParams: a field left
            // in the struct is an invitation to store it again.

            // Filter envelope (ms / 0..1 sustain) + env amount 0..1
            float filtAttack = 10.0f, filtHold = 0.0f, filtDecay = 200.0f,
                  filtSustain = 1.0f, filtRelease = 300.0f, filtEnvAmount = 0.0f;

            // Pitch envelope (ms) + depth in semitones at full env level
            float pitchAttack = 0.0f, pitchHold = 0.0f, pitchDecay = 0.0f,
                  pitchSustain = 0.0f, pitchRelease = 0.0f, pitchEnvDepth = 0.0f;

            // ATTACK GLIDE — see SlotParams.  Ignored on style and drum channels.
        int   glideMode    = 0;
        float glideDepth   = 3.0f;
        float glideTimeMs  = 60.0f;
        float glideShapeK  = 6.0f;
        int   glideEveryN  = 4;
        int   glideOdds    = 5;
        int   glideVelMin  = 100;

        // Mono / portamento (three INDEPENDENT mono switches — multi-select)
            int   playMode         = 0;      // 0=Poly, 1=Mono
            bool  monoHoldStolen   = true;   // note-off falls back to a still-held note
            bool  monoRetrigNew    = false;  // a new note restarts sample + envelope
            bool  monoRetrigStolen = false;  // the fallback note restarts
            float portamentoTime = 100.0f;     // ms

            // Per-instrument octave shift (-3..+3)
            int   octaveOffset   = 0;

            // Three LFOs (rate Hz, depth normalised, delay ms)
            // Amp + filter LFO each have an explicit on/off toggle (default OFF);
            // when off the engine gates the LFO regardless of depth.
            bool  ampLfoEnabled = false, filtLfoEnabled = false;
            float ampLfoRate = 0.0f, ampLfoDepth = 0.0f, ampLfoDelay = 0.0f;
            float filtLfoRate = 0.0f, filtLfoDepth = 0.0f, filtLfoDelay = 0.0f;
            float pitchLfoRate = 0.0f, pitchLfoDepth = 0.0f, pitchLfoDelay = 0.0f;

            // 5-band EQ — band 0 = low shelf, 1..3 = peaking, 4 = high shelf
            float eqGain[5] = { 0,0,0,0,0 };               // dB, -12..+12
            float eqFreq[5] = { 200.f, 600.f, 1500.f, 5000.f, 12000.f };

            // Reverb (0..1)
            float reverbSize = 0.5f, reverbDamp = 0.5f, reverbWet = 0.0f;
            // 1.0 = source untouched.  A reverb is a SEND - see ReverbFx::process.
            float reverbDry  = 1.0f;
            float reverbTail = 0.5f;
            float reverbHpNorm = 0.2917f;
            float reverbLpNorm = 1.0f;
            float reverbPreDelay = 0.0f;                   // 0..1 → 0..200 ms
            // Reverb algorithm: 0 = HALL/ROOM (FDN), 1 = PLATE (Dattorro).
            // size/damp/wet/predelay keep their meanings in both.
            int   reverbAlgo = 0;

            // Delay — time derived at render from (timeSig, div, hostBPM)
            int   delayTimeSig  = 0;     // 0=4/4, 1=3/4
            int   delayDiv      = 2;     // index into the active div table
            float delayFeedback = 0.3f;  // 0..0.95
            float delayWet      = 0.0f;  // 0..1
            // 1.0 = source untouched.  Independent of wet - see StereoDelayFx.
            float delayDry      = 1.0f;  // 0..1

            // Tone, all three previously hidden or absent - see StereoDelayFx.
            float delayDampHz   = 5000.0f;
            float delayHpHz     = 20.0f;
            float delaySmoothMs = 40.0f;

            // What full travel on a WET slider is worth.  See SlotParams.
            float delayWetBase  = 0.5f;
            float reverbWetBase = 1.0f;

            // Melodic insert-FX on/off (default OFF; UI toggles in EffectsPanel).
            // SWEETENER IS DELIBERATELY ABSENT HERE — see instrumentGain above,
            // which is absent for the same reason and was the precedent.
            //
            // ChannelParams is what a BULK push carries, and selectChannelPreset
            // pushes a DEFAULT-CONSTRUCTED ChannelParams whenever an instrument
            // arrives with no .ins voice of its own ("say nothing" must not mean
            // "keep the last instrument's voice").  That is right for envelopes,
            // filter and FX - and fatal for the sweetener, because every style
            // program change on a melodic slot would have switched it back off.
            //
            // The sweetener belongs to the SLOT, not to the instrument sitting
            // in it, so it travels on its own route: applySweetenerParams, fed
            // by SamplePlayerEngine::setChannelSweetener for melodic slots and
            // by applyDrumKitFx for kit slots.

            // Slot velocity curve.  Unlike the sweetener this DOES ride the bulk
            // push: it describes how hard the player strikes (solo) or how the
            // arrangement was authored (style), and both belong to the voice
            // that is being loaded, so a new instrument SHOULD reset it.
            float velCurve       = 0.0f;

            bool  eqEnabled      = false;
            bool  reverbEnabled  = false;
            bool  delayEnabled   = false;

            // ── Sounds-path insert FX (melodic channels only) ──────────────
            // Chorus  (Bambino DSP — normalised controls)
            bool  chorusEnabled  = false;
            float chorusRate     = 0.5f;   // 0..1 → 0.1..6 Hz
            float chorusDepth    = 0.5f;   // 0..1 → 0..10 ms
            float chorusMix      = 0.0f;   // 0..1
            // Auto-wah
            bool  wahEnabled     = false;
            float wahSensitivity = 0.5f;   // 0..1 (envelope amount)
            float wahRate        = 1.0f;   // Hz (LFO)
            float wahLfoDepth    = 0.0f;   // 0..1
            float wahBaseHz      = 400.0f; // Hz
            float wahQ           = 0.6f;   // 0..1 resonance
            float wahMix         = 0.0f;   // 0..1
            // Phaser  (Bambino DSP — normalised controls)
            bool  phaserEnabled  = false;
            float phaserRate     = 0.4f;   // 0..1 → 0.05..4 Hz
            float phaserDepth    = 0.8f;   // 0..1
            float phaserFeedback = 0.5f;   // 0..1 → 0..0.9
            float phaserMix      = 0.0f;   // 0..1
        };

        /** THE FUNKEY STAGE'S PARAMETERS.  Same twelve fields as the wah and
            phaser above, carried separately because they come from a different
            owner: the FAMILY preset in GlobalMacros, not the slot.

            ITS OWN NARROW SETTER, NOT PART OF ChannelParams, and deliberately.
            applyParams is also what a PROGRAM CHANGE performs, so folding these
            in would let a style swapping an instrument mid-performance re-stamp
            the funkey stage with default-constructed values - the exact fault
            the two-handle band filter was moved out of that push to escape. */
        struct FunkeyFx
        {
            bool  wahEnabled     = false;
            float wahSensitivity = 0.5f;
            float wahRate        = 1.0f;
            float wahLfoDepth    = 0.0f;
            float wahBaseHz      = 400.0f;
            float wahQ           = 0.6f;
            float wahMix         = 0.0f;

            bool  phaserEnabled  = false;
            float phaserRate     = 0.4f;
            float phaserDepth    = 0.8f;
            float phaserFeedback = 0.5f;
            float phaserMix      = 0.0f;
        };

        /** Message-thread only, like applyParams. */
        void setFunkeyFx (const FunkeyFx& f);

        /** THE FUNKEY MIX, 0..1 - one value for every channel (the engine
            writes them all; the SET owns it).  Blends the dry instrument with
            Funkey's finished output; see applyFunkeyInPlace.  Any thread. */
        void setFunkeyMix (float mix01) noexcept { funkeyMix.store (juce::jlimit (0.0f, 1.0f, mix01)); }

        /** Message-thread only. Copies every field from `p` into the channel's
            atomics. The audio thread picks the new values up on the next block.

            Does NOT carry instrumentGain or the band filter -- both are
            per-channel and have their own narrow setters, so that a program
            change cannot reach them. */
        void applyParams(const ChannelParams& p);

        /** THE ONLY WAY THE BAND FILTER MOVES.  Hz, order enforced (hp <= lp);
            they may meet, because a zero-width band is silence by design and
            the editor allows it.  Called by the sound editor when the handles
            move and by a set restore, and by nothing else. */
        void setBandFilterHz (float hpHz, float lpHz) noexcept
        {
            float hp = juce::jlimit (20.0f, 20000.0f, hpHz);
            float lp = juce::jlimit (20.0f, 20000.0f, lpHz);
            if (hp > lp) { const float t = hp; hp = lp; lp = t; }
            bandHpFreq.store (hp);
            bandLpFreq.store (lp);
        }

        float getBandFilterHpHz() const noexcept { return bandHpFreq.load(); }
        float getBandFilterLpHz() const noexcept { return bandLpFreq.load(); }

        //======================================================================
        // SAVED-VOICE STASH
        //
        // The .ins / .sins voice belonging to a POOLED flag, resolved on the
        // MESSAGE thread so selectPooledPreset — which runs on the AUDIO thread
        // for every runtime program change — can restore it without touching
        // the engine's preset maps.
        //
        // A FIXED ARRAY, not a map, and that is the whole point.  A style load
        // calls preloadChannelPreset once per CASM destination while playback is
        // live, so a map here would be GROWN by the message thread while the
        // audio thread was reading it: rehashing invalidates buckets mid-lookup,
        // and the reader gets a miss or garbage.  Garbage read as a voice is a
        // channel that stops sounding until something re-pushes it — which is
        // exactly the "only drums and bass play until I reopen the UI" failure.
        //
        // Indexed by GM flag, so it is allocation-free, rehash-free and
        // wait-free.  hasParams is released AFTER the params are written and
        // acquired before they are read, so the audio thread never sees a
        // half-written voice.
        //======================================================================
        static constexpr int kMaxPooledFlag = 128;

        struct PooledVoice
        {
            ChannelParams params;
            float         gainLinear = 1.0f;    // base unity x trim, already combined
            bool          hasParams  = false;   // false for a gain-only preset
        };

        void stashPooledVoice (int poolKey, const PooledVoice& v)
        {
            if (poolKey < 0 || poolKey >= kMaxPooledFlag) return;
            const auto i = (size_t) poolKey;

            pooledHasParams[i].store (false, std::memory_order_release);  // park readers
            pooledParams[i] = v.params;
            pooledGain  [i].store (v.gainLinear, std::memory_order_relaxed);
            pooledStashed[i].store (true, std::memory_order_release);
            pooledHasParams[i].store (v.hasParams, std::memory_order_release);
        }

        /** Every flag currently warm in this channel's pool.  Message thread —
            used to re-stash the saved voices after a preset rescan. */
        std::vector<int> pooledFlags() const
        {
            std::vector<int> v;
            v.reserve (presetPool.size());
            for (const auto& kv : presetPool) v.push_back (kv.first);
            return v;
        }

        // =====================================================================
        // Parameters (atomics — set from message thread, read from audio thread)
        // =====================================================================

        // Amp envelope (ms / 0..1 sustain)
        std::atomic<float> ampAttack   { 10.0f };
        std::atomic<float> ampHold     { 0.0f  };
        std::atomic<float> ampDecay    { 200.0f };
        std::atomic<float> ampSustain  { 0.8f  };
        std::atomic<float> ampRelease  { 300.0f };
        std::atomic<float> ampCurveK    { 6.0f };   // signed exponent; see fallAmount

        // Filter
        enum FilterType { LowPass = 0, HighPass = 1, BandPass = 2, Notch = 3 };
        std::atomic<int>   filterType     { LowPass };
        std::atomic<float> filterCutoff   { 20000.0f };
        std::atomic<float> filterReso     { 0.0f };

        // Melodic pseudo round robin depth, 0..100.  See SlotParams::
        // variationAmount.  Read once per note-on, never per sample.
        std::atomic<int>   variationAmount { 0 };
        std::atomic<float> filterKeytrack { 0.0f };   // cents per semitone from C4

        //======================================================================
        // BAND FILTER (style channels 3..7 and every solo channel).
        // Low-cut / high-cut edges in Hz, run HP -> LP in series (clean, no
        // resonance).  20 / 20000 = fully open.
        //
        // PER-CHANNEL, and reachable by exactly ONE writer: setBandFilterHz.
        // applyParams does NOT touch it, for the same reason it does not touch
        // instrumentGain -- a bulk params push arrives on every program change,
        // and a program change must not be able to move a setting the player
        // dialled into the channel strip.  The set file owns these values; the
        // instrument on the channel does not.
        //======================================================================
        std::atomic<bool>  voiceOwnedBySet { false };

        // Set on every drum-kit publish from the style's own bank LSB (8 = Revo!).
        // Read on the audio thread in noteOn; see isRevoRemappedLowKey above.
        std::atomic<bool>  revoLowZone     { false };
        std::atomic<int>   styleKitMsb     { -1 };   // the kit the style asked for,
        std::atomic<int>   styleKitLsb     { -1 };   // for the SX920 key map
        std::atomic<int>   styleKitPc      { -1 };
        int remapStyleDrumKey (int note, bool fromEditor) const noexcept;
        std::atomic<float> bandHpFreq     { 20.0f };
        std::atomic<float> bandLpFreq     { 20000.0f };

        //======================================================================
        // IS THIS CHANNEL HOLDING A SAMPLED FULL KIT?
        //
        // Set by adoptFullKit, cleared by publishDrumKit -- the only two things
        // that publish a drum voice.  Read on the AUDIO thread by the kick-mix
        // trigger, which is why it is an atomic and not the engine's
        // fullKitLive[] (that one lives a layer up and the channel cannot see it).
        //
        // The kick mix is for the NINE COMPOSED GM KITS ONLY.  A sampled kit
        // bypasses composeDrumKit entirely, so it carries no tagged blend layers
        // -- and without this flag a stale MIX left on from a composed kit would
        // still duck its kick by cos(a*pi/2) with nothing to fill the gap, while
        // the editor hides the MIX slider so there is no visible control to
        // explain it.  Suppressing rather than clearing keeps the user's setting
        // intact for when a composed kit comes back.
        //======================================================================
        std::atomic<bool>  fullKitActive  { false };

        //======================================================================
        //======================================================================
        // STYLE ENERGY — THE THIRD VELOCITY CURVE, and it is a THIRD one on
        // purpose rather than a write into slotVelCurve.
        //
        // slotVelCurve belongs to the instrument: it is saved in the .ins, in
        // the set and in the drum kit, and it is what the user dialled for that
        // sound.  A live dynamics control that wrote into it would be a SECOND
        // OWNER of that field - the exact failure this codebase has hit twice
        // already - and moving ENERGY would silently destroy every slot's own
        // voicing with no way back.
        //
        // So it composes instead: keyboard curve, then slot curve, then this.
        // Each is applied in sequence for the reason the noteOn comment gives -
        // the shape is x^gamma with gamma exponential in the curve, so sequence
        // multiplies exponents where summing the values would not.  Neutral is
        // 0.0, which VelCurve::apply short-circuits, so a channel nobody has
        // touched pays one atomic load.
        std::atomic<float> energyCurve { 0.0f };

        //======================================================================
        // ATTACK GLIDE.  Message thread writes, audio thread reads.
        //
        // glideNoteCounter and glideRng are AUDIO THREAD ONLY: both are touched
        // exclusively from noteOn, which is the only place the mode is
        // evaluated, so neither needs to be atomic and neither is.
        //======================================================================
        std::atomic<int>   glideMode   { 0 };
        std::atomic<float> glideDepth  { 3.0f };
        std::atomic<float> glideTimeMs { 60.0f };
        std::atomic<float> glideShapeK { 6.0f };
        std::atomic<int>   glideEveryN { 4 };
        std::atomic<int>   glideOdds   { 5 };
        std::atomic<int>   glideVelMin { 100 };

        bool     pendingGlide     = false;   // latched by noteOn, read by startVoice
        int      glideNoteCounter = 0;
        uint32_t glideRng         = 0xB5297A4Du;   // its own state: sharing the
                                                   // round-robin generator would
                                                   // correlate two unrelated
                                                   // decisions on the same note.

        float glideNextUnit() noexcept              // 0 .. 1
        {
            uint32_t x = glideRng;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            glideRng = x;
            return (float) (x >> 8) * (1.0f / 16777216.0f);
        }

        // PSEUDO ROUND ROBIN — per-voice jitter latched at note-on.
        //
        // Xorshift32.  No allocation, no locks, no std::random_device: this runs
        // inside startVoice on the audio thread.  Relaxed atomics because
        // noteOn can also arrive from the editor's piano strip on the message
        // thread, and a torn read here costs nothing worse than a different
        // random number.
        //
        // ONE draw per voice, taken at the start and kept for the voice's whole
        // life.  That is what real round robin does and it is not optional: a
        // value re-rolled per sample would be noise on the pitch and a zipper on
        // the gain.
        //======================================================================
        mutable std::atomic<uint32_t> rrRng { 0x9E3779B9u };

        float rrNextBipolar() const noexcept        // -1 .. +1
        {
            uint32_t x = rrRng.load (std::memory_order_relaxed);
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            rrRng.store (x, std::memory_order_relaxed);
            return (float) (int32_t) x * (1.0f / 2147483648.0f);
        }

        // Full-scale deviations, reached at amount 100 and scaled linearly below.
        // Sized from what the ear tolerates on a KIT rather than from what is
        // measurable: a snare moves this far hit to hit without anyone calling
        // it out of tune, and past it the kit starts to sound broken instead of
        // alive.  Pitched material would need roughly a quarter of the cents.
        static constexpr float kRrPitchCents = 14.0f;   // +/-
        static constexpr float kRrGainDb     =  1.6f;   // +/-
        static constexpr float kRrStartMs    =  1.8f;   // forward only

        //======================================================================
        // MELODIC BUDGETS.  A quarter of the kit's cents, exactly as the drum
        // comment above predicted pitched material would need, and a cutoff
        // offset stated in CENTS rather than Hz so it means the same thing at
        // 300 Hz as it does at 6 kHz.
        //
        // CUTOFF DOES THE AUDIBLE WORK and pitch does the rest: what separates
        // two takes is mostly brightness, not tuning.  But cutoff only speaks
        // when the patch's filter is actually in the audible range - on a patch
        // sitting at the 20 kHz default the filter is bypassed and this changes
        // nothing, which is a property of the effect, not a fault.
        //======================================================================
        static constexpr float kVarPitchCents  =   4.0f;   // +/-
        static constexpr float kVarCutoffCents = 150.0f;   // +/-

        // Filter envelope (ms / 0..1 sustain) + env amount in normalised range
        std::atomic<float> filtAttack    { 10.0f };
        std::atomic<float> filtHold      { 0.0f };
        std::atomic<float> filtDecay     { 200.0f };
        std::atomic<float> filtSustain   { 1.0f };
        std::atomic<float> filtRelease   { 300.0f };
        std::atomic<float> filtEnvAmount { 0.0f };    // 0..1 ; scaled by 10000Hz in render

        // Pitch envelope
        std::atomic<float> pitchAttack   { 0.0f };
        std::atomic<float> pitchHold     { 0.0f };
        std::atomic<float> pitchDecay    { 0.0f };
        std::atomic<float> pitchSustain  { 0.0f };
        std::atomic<float> pitchRelease  { 0.0f };
        std::atomic<float> pitchEnvDepth { 0.0f };    // semitones at full env level

        // Pitch tracking + bend
        std::atomic<float> pitchBendRange { 2.0f };   // semitones
        std::atomic<float> pitchKeytrack  { 100.0f }; // cents per semitone (informational)
        std::atomic<float> pitchVeltrack  { 0.0f };
        std::atomic<int>   octaveOffset   { 0 };      // -3..+3 (per-instrument, from SlotParams)
        // Hidden per-channel octave bias, added on top of octaveOffset at
        // note-on.  Used to lift the style BASS channel up an octave so its
        // slider can read 0 while the engine actually plays +12 semitones.
        std::atomic<int>   octaveBias     { 0 };

        // Global "bypass instrument audio chain" toggle, set by the engine
        // from the BetelgeuseProcessor's Settings → Global Settings button.
        // When true, only the amp ADSR + channel volume survive — every
        // other per-voice / per-channel DSP stage is suppressed:
        //   • Filter (cutoff, resonance, filter ENV, filter LFO, keytrack)
        //   • Amp LFO (tremolo) — ampMod stays at 1.0
        //   • Pitch ENV and pitch LFO — modulation contributions ignored
        //   • Velocity gain — every note plays as if vel=127
        //   • Channel pan — forced to equal-power center (samples' natural
        //     stereo content preserved, channel-level pan ignored)
        //   • Click transient layer — skipped
        //   • Post-mix insert chain (Sweetener, 5-band EQ, Wah, Phaser) and the
        //     three section sends (chorus, reverb, delay) that follow it
        //   • Drum FX bus (per-key dry/wet forced to 100% dry)
        // What still runs: portamento, pitch bend, sample read with
        // interpolation, region gain (sample-level calibration), drum per-
        // key gain multiplier (voiceGainMult), the amp ADSR itself, AND
        // channel volume × expression so the mixer faders keep balancing
        // the instruments against each other.  Default false (full chain
        // active).
        std::atomic<bool>  chainBypassed   { false };

        // Three LFOs (rate Hz, depth normalised, delay ms)
        std::atomic<float> ampLfoRate    { 0.0f };
        std::atomic<float> ampLfoDepth   { 0.0f };    // 0..1 tremolo depth
        std::atomic<float> ampLfoDelay   { 0.0f };
        std::atomic<bool>  ampLfoEnabled { false };   // explicit on/off toggle

        std::atomic<float> filtLfoRate   { 0.0f };
        std::atomic<float> filtLfoDepth  { 0.0f };    // 0..1 cutoff modulation
        std::atomic<float> filtLfoDelay  { 0.0f };
        std::atomic<bool>  filtLfoEnabled { false };  // explicit on/off toggle

        std::atomic<float> pitchLfoRate  { 0.0f };
        std::atomic<float> pitchLfoDepth { 0.0f };    // semitones vibrato
        std::atomic<float> pitchLfoDelay { 0.0f };

        // Mono / portamento (ignored when isDrumChannel == true)
        enum PlayMode { Poly = 0, Mono = 1 };
        std::atomic<int>   playMode         { Poly };
        std::atomic<bool>  styleForcedPoly  { false };   // see setStyleForcedPoly
        std::atomic<bool>  monoHoldStolen   { true  }; // note-off: fall back to a still-held note
        std::atomic<bool>  monoRetrigNew    { false }; // new note restarts sample + envelope
        std::atomic<bool>  monoRetrigStolen { false }; // returned (fallback) note restarts
        std::atomic<float> portamentoTime   { 100.0f }; // ms

        // Channel volume + pan
        // GLOBAL TRANSPOSE, in semitones.  Pushed to every channel by the
        // processor's SEMITONE knob.  Sits alongside octaveOffset/octaveBias in
        // the ONE shift computation in noteOn, so it inherits that path's
        // per-note latch (keyDownXpose) and srcNote identity for free — turning
        // the knob while notes are held can neither hang them nor strand a lit
        // key.  DRUM channels ignore it: a kit's notes are keys, not pitches,
        // and shifting them would turn a kick into a tom.
        // First SOLO channel index.  The engine lays out 16 style channels
        // (0..15) then the solo block; Channel needs the boundary to decide
        // which voices take the melodic BAND filter.  Mirrors
        // SamplePlayerEngine::kNumStyleChannels.
        static constexpr int kFirstSoloChannel = 16;

        std::atomic<int>   semitoneOffset { 0 };

        std::atomic<float> channelVolume { 1.0f };
        // Expression (CC 11): a 0..1 multiplier applied ON TOP of channelVolume
        // for style dynamic swells.  Default 1.0 = no attenuation.
        // ── CC 11 IS THE TARGET, NOT THE GAIN ───────────────────────────────
        //
        // A style writes expression as a STEP: one value per event, and a loop
        // point can put a 12.7 dB jump between two consecutive events - Main A
        // of VienneseWaltz2 rides its bass down to 29 and the next cycle's first
        // CC 11 snaps it to 126, 42 ms after the wrap.  Applied raw that is a
        // discontinuity in the gain, which is a click.
        //
        // So this atomic is what the style ASKED FOR, and expSmoothed below is
        // what the audio actually multiplies by.  See advanceExpression().
        std::atomic<float> channelExpression { 1.0f };

        // CC74 BRIGHTNESS (style filter sweeps).  Target multiplier on every
        // voice's filter cutoff: 1.0 at the neutral 64, up to 3 octaves either
        // way.  Written by the style (any thread); renderBlock glides to it
        // once per block so a dense sweep does not step.
        std::atomic<float> brightnessTarget { 1.0f };
        float              brightnessSmoothed = 1.0f;          // audio thread only
        void setBrightnessCc (int cc74) noexcept
        {
            const float v = (float) juce::jlimit (0, 127, cc74);
            brightnessTarget.store (std::exp2 ((v - 64.0f) * (3.0f / 64.0f)));
        }
        // Style per-section level automation: the RELATIVE CC 7 ride
        // (sectionCC7 / setupCC7) the style sends alongside each per-section
        // voice swap.  Kept separate from channelVolume so it composes with the
        // user's fader (fader stays king) instead of overwriting it.  1.0 = no
        // change; reset to 1.0 on every style load.
        std::atomic<float> channelAutoLevel { 1.0f };
        //======================================================================
        // Per-sound calibration trim (linear, 1.0 = unity).
        //
        // Reachable by exactly TWO writers, and that is the whole design:
        //   1. a SOUND LOAD  — selectChannelPreset / programChangeDrum set it
        //      from the .ins / .sins / .drm governing the incoming flag;
        //   2. the GAIN SLIDER — an explicit, deliberate change by the user.
        //
        // applyParams does NOT touch it.  The editor's slot snapshot is a copy
        // of what the UI last edited, and it goes STALE the moment a style
        // program change swaps the instrument on that channel without the UI
        // seeing it.  If a bulk push carried the trim, the next push of that
        // slot — a slider move, a set restore, applySoundCalibration — would
        // send the PREVIOUS sound's calibration and silently undo the loaded
        // one.  Keeping it off that path makes the preset the single source of
        // truth for calibration, which is what "save it in the .ins" promises.
        //======================================================================
        // Linear, 0.0 .. 2.0 — the 0..200 percent the editor shows, divided by
        // 100.  1.0 (slider 100) is unity and the default, so an uncalibrated
        // sound is untouched.
        std::atomic<float> instrumentGain { 1.0f };
        std::atomic<float> channelPan    { 0.0f };    // -1..+1

        // Send level into the global reverb bus.  Storage-only today —
        // the audio path doesn't yet route to a shared reverb processor,
        // but style voice setups + mid-track CC 91 will land their values
        // here so they're ready for the bus when it lands.
        std::atomic<float> channelReverbSend { 0.0f };  // 0..1

        // Per-pitch-class fine tuning in cents (one entry per chromatic note,
        // index 0 = C, 1 = C#, ..., 11 = B). Applied to playback pitch at
        // noteOn for non-drum channels. Used for Arabic/microtonal scales.
        std::atomic<float> scaleTuningCents [12] {
            { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f },
            { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f },
            { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f }
        };

        // =====================================================================
        // Post-mix effects parameters (message thread writes; audio reads)
        // =====================================================================

        // 5-band EQ. Coefficients are recomputed lazily on the audio thread
        // when these atomics change since the last block.
        std::atomic<float> eqGain [5] {
            { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f }
        };
        std::atomic<float> eqFreq [5] {
            { 200.0f }, { 600.0f }, { 1500.0f }, { 5000.0f }, { 12000.0f }
        };

        // Reverb (SoundsFx ReverbFx: FDN Hall/Room + Dattorro Plate).
        // Params applied per block.
        std::atomic<float> reverbSize { 0.5f };
        std::atomic<float> reverbDamp { 0.5f };
        std::atomic<float> reverbWet  { 0.0f };
        std::atomic<float> reverbDry  { 1.0f };
        std::atomic<float> reverbTail { 0.5f };
        std::atomic<float> reverbHpNorm { 0.2917f };
        std::atomic<float> reverbLpNorm { 1.0f };
        std::atomic<float> reverbPreDelay { 0.0f };   // 0..1 → 0..200 ms
        std::atomic<int>   reverbAlgo { 0 };          // 0 = Hall/Room, 1 = Plate

        // Delay — musical-time. Delay time in samples is recomputed each block
        // from (delayTimeSig, delayDiv, hostBPM).
        std::atomic<int>   delayTimeSig  { 0 };     // 0=4/4, 1=3/4
        std::atomic<int>   delayDiv      { 2 };     // index into 4/4 or 3/4 table
        std::atomic<float> delayFeedback { 0.3f };  // 0..0.95
        std::atomic<float> delayWet      { 0.0f };  // 0..1
        std::atomic<float> delayDry      { 1.0f };  // 0..1, independent of wet
        std::atomic<float> delayDampHz   { 5000.0f };
        std::atomic<float> delayHpHz     { 20.0f };
        std::atomic<float> delaySmoothMs { 40.0f };
        std::atomic<float> delayWetBase  { 0.5f };
        std::atomic<float> reverbWetBase { 1.0f };

        //======================================================================
        // SECTION SENDS — HOW MUCH OF THIS CHANNEL REACHES EACH GLOBAL EFFECT
        //
        // Three, one per effect on the section bus: chorus, reverb, delay.
        // Indexed by SectionSendFx::Slot so the ordering cannot drift between
        // here and the rack.
        //
        // WAH, PHASER AND SWEETENER ARE NOT SENDS.  They are shapers whose
        // output replaces the signal, so they went back to being per-instrument
        // INSERTS ahead of the send tap — see the insert chain in
        // Channel::renderBlock and the note in SectionSendFx.
        //
        // 0 means dry, and dry is the DEFAULT for all three: a send that started
        // open would drop every slot into an effect nobody asked for the first
        // time a set loads without send values.
        //
        // A send cannot change the instrument's own level — the dry path is
        // untouched by all three — which is the practical difference from the
        // wet/dry inserts they replace, where raising wet lowered the dry.
        //
        // EQ AND PAN ARE NOT HERE.  They stay inserts on this channel because
        // they are corrective and per-instrument: a shared EQ would mean tilting
        // one voice tilts sixteen, and a shared pan is a contradiction.
        std::atomic<float> sendAmount [SectionSendFx::kNumSlots] {};

        void  setSend (int slot, float v) noexcept
        {
            if (slot < 0 || slot >= SectionSendFx::kNumSlots) return;
            sendAmount[(size_t) slot].store (juce::jlimit (0.0f, 1.0f, v));
        }
        float getSend (int slot) const noexcept
        {
            return (slot >= 0 && slot < SectionSendFx::kNumSlots)
                     ? sendAmount[(size_t) slot].load() : 0.0f;
        }

        // Melodic insert-FX on/off (gate applyEq/Delay/Reverb in renderBlock).
        std::atomic<bool>  eqEnabled      { false };
        std::atomic<bool>  reverbEnabled  { false };
        std::atomic<bool>  delayEnabled   { false };

        // ── Sounds-path insert FX (melodic channels only) ──────────────────
        std::atomic<bool>  chorusEnabled  { false };
        std::atomic<float> chorusRate     { 0.5f };
        std::atomic<float> chorusDepth    { 0.5f };
        std::atomic<float> chorusMix      { 0.0f };

        std::atomic<bool>  wahEnabled      { false };
        std::atomic<float> wahSensitivity  { 0.5f };
        std::atomic<float> wahRate         { 1.0f };
        std::atomic<float> wahLfoDepth     { 0.0f };
        std::atomic<float> wahBaseHz       { 400.0f };
        std::atomic<float> wahQ            { 0.6f };
        std::atomic<float> wahMix          { 0.0f };

        std::atomic<bool>  phaserEnabled  { false };
        std::atomic<float> phaserRate     { 0.4f };
        std::atomic<float> phaserDepth    { 0.8f };
        std::atomic<float> phaserFeedback { 0.5f };
        std::atomic<float> phaserMix      { 0.0f };

        // =====================================================================
        //  FUNKEY MODE — ITS OWN WAH AND PHASER, AHEAD OF EVERYTHING ELSE.
        //
        //      STYLE INSTRUMENT  ->  FUNKEY  ->  CHANNEL EFFECTS
        //
        //  A SEPARATE STAGE, not a takeover.  Funkey used to OVERWRITE the
        //  slot's own wah and phaser parameters for as long as the macro was on,
        //  so a slot lost its private wah to get the family's.  Now the family's
        //  pair runs FIRST, in its own instances, and hands an already-wet
        //  signal to the channel's own chain - which is left completely alone.
        //
        //  SEPARATE INSTANCES ARE THE POINT, not a duplication to tidy away
        //  later.  The wah is envelope-driven and the phaser has an LFO; sharing
        //  one object with the slot's own stage would mean one envelope follower
        //  and one LFO phase serving two different settings, which is not two
        //  effects in series, it is one effect with its parameters changing
        //  under it.
        //
        //  Both bypass on a single atomic load when the macro is off, so the
        //  cost of the stage on a channel that is not funkeyed is nil.
        // =====================================================================
        std::atomic<bool>  fkWahEnabled     { false };
        std::atomic<float> fkWahSensitivity { 0.5f };
        std::atomic<float> fkWahRate        { 1.0f };
        std::atomic<float> fkWahLfoDepth    { 0.0f };
        std::atomic<float> fkWahBaseHz      { 400.0f };
        std::atomic<float> fkWahQ           { 0.6f };
        std::atomic<float> fkWahMix         { 0.0f };

        std::atomic<bool>  fkPhaserEnabled  { false };
        std::atomic<float> fkPhaserRate     { 0.4f };
        std::atomic<float> fkPhaserDepth    { 0.8f };
        std::atomic<float> fkPhaserFeedback { 0.5f };
        std::atomic<float> fkPhaserMix      { 0.0f };

        // THE FUNKEY MIX (per set; the "E" on the main tab's FUNKEY button).
        // 0 = the dry instrument, 1 = Funkey's full output, 0.5 by default.
        // funkeyMixNow is the audio thread's own copy, for the per-block glide;
        // funkeyDry holds the instrument as it was before the stage, sized in
        // prepare() so the audio thread never allocates for it.
        std::atomic<float>       funkeyMix    { 0.5f };
        float                    funkeyMixNow = 0.5f;
        juce::AudioBuffer<float> funkeyDry;

        // =====================================================================
        // Drum-mode per-key user state (active only when isDrumChannel == true)
        //
        // Indexed by MIDI key 0..127.  Each entry mirrors the user-editable
        // fields in DrumElementParams (gain / pitch / cutoff / A/D/R / RR)
        // as atomics so the Kitton popup can drag sliders in real time
        // without breaking the audio thread.
        //
        // The "-1.0f means inherit from channel atomic" sentinel makes it
        // safe to initialise empty keys without surprising the engine — a
        // drum slot that never had loadDrumKit called still behaves like a
        // normal melodic channel (which is the right default for a slot
        // freshly switched into DRUMS but not yet populated).
        // =====================================================================
        struct DrumKeyState
        {
            std::atomic<float> userGain        { 1.0f };
            std::atomic<float> userPitchSemi   { 0.0f };
            std::atomic<float> userCutoffNorm  { -1.0f }; // -1 = inherit channel filterCutoff
            // Two-handle band, per key.  Defaults are FULLY OPEN, so a kit that
            // has never been touched runs the same code path it always did.
            std::atomic<float> userHpNorm      { 0.0f };  // low-cut  edge, 0 = 20 Hz
            std::atomic<float> userLpNorm      { 1.0f };  // high-cut edge, 1 = 20 kHz
            std::atomic<float> userAttackMs    { -1.0f }; // -1 = inherit channel ampAttack
            std::atomic<float> userDecayMs     { -1.0f }; // -1 = inherit channel ampDecay
            std::atomic<float> userSustain     { -1.0f }; // -1 = inherit channel ampSustain (0..1)
            std::atomic<float> userReleaseMs   { -1.0f }; // -1 = inherit channel ampRelease
            std::atomic<float> velCurve        { 0.0f };  // -1 soft .. +1 hard
            //------------------------------------------------------------------
            // PSEUDO ROUND ROBIN, 0..100 — an AMOUNT, not a mode.
            //
            // Real round robin rotates between several recorded takes of the
            // same drum.  The composed kits hold VELOCITY layers instead, so
            // there is no second take to rotate to, and the field sat here for
            // a long time marked "reserved; not yet used at render" while the
            // slider it belonged to did nothing.
            //
            // What it does now is the agreed alternative: perturb the ONE
            // sample slightly on every hit, so repeats stop being bit-identical.
            // That is what defeats the machine-gun effect - the ear latches on
            // to exact repetition, not to any particular timbre.
            //
            // Was an int mode (0=off 1=rotate 2=random 3=cycle).  Nothing ever
            // read it, so nothing breaks; an old set's 0..3 lands as 0..3% here,
            // which is the "off" it always effectively was.
            //------------------------------------------------------------------
            std::atomic<int>   roundRobinAmount { 0 };
            // One-shot: when true, note-off is ignored and the sample plays
            // start→end on every trigger (default for drums).  When false the
            // per-key release fade applies on note-off.
            std::atomic<bool>  fullLength      { true };
            // Per-key send into the kit-wide FX bus (drum mode only).
            // 0 = all dry (key bypasses the bus), 1 = all wet (key fully sent).
            std::atomic<float> userFxSendNorm  { 0.0f };

            // KICK MIX (notes 35 & 36).  Live-editable via setDrumKeyParams; the
            // supplemental EDM/WOOD kick plays as a second voice whose gain is
            // the equal-power "supplemental" side of kickMixAmount.  Blend layers
            // are baked into the composed voice, so these atomics drive the mix
            // with no re-compose and survive style program changes.
            std::atomic<bool>  kickMixEnabled  { false };   // default OFF
            std::atomic<float> kickMixAmount   { 50.0f };   // 0..100
            std::atomic<int>   kickMixVariant  { 0 };       // 0 = EDM, 1 = WOOD
        };
        // Default-constructed in Channel().  Layout = 128 atomics; ~3.5 KB.
        DrumKeyState drumKeyStates [128];

        // Per-key drum choke group (0 = none).  On a drum note-on, any still-
        // ringing voice whose key shares the same non-zero group is given a
        // fast release — e.g. closed/foot hi-hat (42/44) choking the open hat
        // (46).  Set in loadDrumKit; read on the audio thread in noteOn.
        std::atomic<int> drumChokeGroup [128] {};

        // ─────────────────────────────────────────────────────────────────────
        // Click library (transient attack-layer fired on every noteOn).
        // ─────────────────────────────────────────────────────────────────────
        // Click library DSP removed.  loadClickSample/clearClickSample remain as
        // inert no-ops (see Channel.cpp) so SamplePlayerEngine's forwarders and
        // any saved state/presets that referenced them still compile.
        bool loadClickSample (const juce::File& file);   // no-op
        void clearClickSample();                          // no-op

    private:
        // Per-channel post-mix effects on tempBuffer (audio thread)
        void applyEqInPlace     (juce::AudioBuffer<float>& buf, int numSamples);
        void applyDelayInPlace  (juce::AudioBuffer<float>& buf, int numSamples, double hostBPM);
        void applyReverbInPlace (juce::AudioBuffer<float>& buf, int numSamples);
        /** The FUNKEY stage — wah then phaser, from the family preset.  Runs
            before every other insert on this channel. */
        void applyFunkeyInPlace      (juce::AudioBuffer<float>& buf, int numSamples);
        void applyWahInPlace         (juce::AudioBuffer<float>& buf, int numSamples);
        void applyPhaserInPlace      (juce::AudioBuffer<float>& buf, int numSamples);
        void applyChorusInPlace      (juce::AudioBuffer<float>& buf, int numSamples);

    public:

    private:
        // Drum-kit pool internals: composeDrumKit does the heavy decode into a
        // PooledDrumKit (message thread, no member mutation); publishDrumKit
        // applies one to this channel (RT-safe unless syncName).
        PooledDrumKit composeDrumKit (const DrumKitRegistry& registry,
                                      const DrumKitParams& kit);
        void          publishDrumKit (const PooledDrumKit& pk, int poolKey,
                                      bool syncName);

        // =====================================================================
        // Voice  (kept private — internal to Channel)
        // =====================================================================
        static constexpr int kMaxVoices = 48;

        /** Mono region-crossfade length, in ms.  Long enough to hide a waveform
            splice, short enough that the two samples never audibly double.
        
            SET TO 0 TO DISABLE THE CROSSFADE ENTIRELY: a region crossing then
            takes the fast-release path instead — still clickless, just without
            the glide across the boundary, which is exactly how the plugin
            behaved before the crossfade existed.
        
            Defaulted OFF while the "notes going silent" report is outstanding,
            so a build is never carrying an unproven change to the voice
            lifecycle.  Set it to 10.0f to turn the crossfade back on and hear
            whether the silencing follows it. */
        static constexpr float kMonoXfadeMs = 0.0f;

        struct Voice
        {
            bool   active       = false;
            // The SOUNDING pitch (post octave-shift): what the sample plays and
            // what the strip / choke-group / drum per-key logic index by.
            int    note         = -1;
            // The ORIGINAL incoming MIDI note — the voice's pairing IDENTITY.
            // noteOff and retuneVoice match on THIS, never on a recomputed
            // shift, so moving the OCTAVE slider between a note's on and off
            // can no longer orphan the voice (the old bug: both ends read the
            // LIVE octave atomics, the off's lookup missed after a slider move,
            // and the note stuck — stacking looped voices into distortion).
            int    srcNote      = -1;
            // Played from the editor's piano strip rather than by the style.
            // Voice stealing sacrifices these LAST and the mono contract skips
            // them, so a manual note can't be walked over by a busy style part.
            bool   fromEditor   = false;
            float  extraGain    = 1.0f;   // per-voice multiplier (see noteOn)
            int    velocity     = 0;
            int    regionIndex  = -1;
            int    synthIndex   = -1;   // EDM KIT: slot in synthVoices[], -1 = sample voice
            // SOUND ENGINE (GM 38 / 39): the oscillators replace the sample.
            bool   oscEngine    = false;
            int    oscProgram   = 38;
            double oscOffset    = 0.0;   // region root - tune: pitchSemitones + this = the note played
            oscengine::Voice osc;
            int    edmFamily    = -1;   // EDM KIT: the key's family - the rack it plays through
            float  synthHpNorm  = 0.0f; // EDM KIT: the key's own two-handle filter (20 * 1000^norm)
            float  synthLpNorm  = 1.0f;
            // Monotonic start order — lets noteOff release the OLDEST voice at
            // a pitch (instance-correct pairing for authored same-pitch overlaps).
            uint32_t startSerial = 0;

            // The instrument this voice started on.  Held so a later program
            // change can't retune or free a note that's still sounding.
            std::shared_ptr<PresetVoice> preset;

            double playPosition           = 0.0;
            double currentPitchSemitones  = 60.0;
            double targetPitchSemitones   = 60.0;
            //------------------------------------------------------------------
            // GLIDE COEFFICIENT, not a rate.  ONE-POLE, NOT A LINEAR RAMP.
            //
            // This was `portamentoRateSemitones` - a fixed number of semitones
            // added per sample until the target was reached, then a hard snap.
            // A straight line between two pitches with a corner at each end is
            // the one glide shape no analogue synth ever made, and it is why
            // mono mode sounded mechanical: the slide leaves at full speed,
            // travels at full speed and stops dead.
            //
            // Every reference agrees on the musical shape - Sound On Sound's
            // note-priority primer, the KVR DSP threads, the AAS Multiphonics
            // manual, and players describing the Minimoog and OB-8 - it is an
            // RC charge curve: fast at first, easing into the destination.
            // mystran's one-liner on KVR is the whole algorithm: put PITCH
            // through a one-pole lowpass and exponentiate to frequency.
            //
            // Pitch is already in semitones here, which IS the log domain, so
            // the filter runs directly on currentPitchSemitones and the sample
            // rate conversion downstream does the exponentiating for free.
            //
            //     current += (target - current) * glideCoeff
            //
            // glideCoeff = 1 - exp(-5 / (timeSeconds * sampleRate)), so the
            // stated time is the time to arrive within ~0.7% of the target -
            // the convention the AAS manual uses and near enough to what
            // players call "the glide time".
            //------------------------------------------------------------------
            double glideCoeff             = 0.0;
            bool   portamentoActive       = false;

            //==================================================================
            // ATTACK GLIDE — a SEPARATE, ADDITIVE offset, not a second
            // portamento.
            //
            // It has to be additive because portamento may be running at the
            // same time on the same voice: portamento moves
            // currentPitchSemitones toward the target, and the glide is a
            // deviation applied ON TOP of wherever that has got to.  Folding the
            // glide into currentPitchSemitones would make a legato move fight
            // the scoop and the loser would be whichever wrote last.
            //
            // A PHASE, not a one-pole.  Portamento above uses an exponential
            // that never quite arrives and needs a floor to stop it; the glide
            // is a fixed-duration gesture with a shape, so it runs 0..1 over the
            // labelled time and ENDS - exactly like the amp envelope's fall
            // segment, which is why it reuses fallAmount rather than inventing
            // a second curve law.
            float glideSemis  = 0.0f;   // current offset, negative, -> 0
            float glideDepth  = 0.0f;   // where it started, semitones below
            float glidePhase  = 0.0f;   // 0..1
            float glideInc    = 0.0f;   // 1 / timeSamples
            float glideShapeK = 6.0f;
            bool  glideActive = false;

            float  velocityGain = 1.0f;
            float  regionGain   = 1.0f;
            // Melodic pseudo-RR cutoff multiplier, latched at note-on.
            // EXACTLY 1.0 when the feature is off, and the render path skips
            // the multiply on 1.0, so an untouched patch pays nothing.
            float  rrCutoffMul  = 1.0f;
            // KICK MIX equal-power gain for this voice: the kit/low-kick voice
            // gets cos(a·π/2) and the supplemental EDM/WOOD layer voice gets
            // sin(a·π/2) (a = kickMixAmount/100).  1.0 for every other voice.
            float  kickMixGain  = 1.0f;

            //------------------------------------------------------------------
            // MONO REGION CROSSFADE.
            //
            // A mono legato glide keeps the running voice and its sample
            // position — which is only meaningful while the note stays inside
            // ONE region.  Crossing into a different sample used to splice two
            // unrelated waveforms mid-phase: a click as loud as the envelope
            // was at that instant.
            //
            // So a crossing is played as a short equal-time crossfade instead.
            // The outgoing voice keeps reading its own sample and ramps to
            // silence; a second voice starts the new sample from its beginning
            // and ramps up, having INHERITED the envelope objects rather than
            // re-attacking. The glide survives, the splice does not.
            //
            // xfadeInc == 0 means "not fading" — the overwhelmingly common case,
            // and one compare per voice per block to check.
            //------------------------------------------------------------------
            float  xfadeGain  = 1.0f;    // multiplies this voice's output
            float  xfadeInc   = 0.0f;    // per-sample delta; 0 = not crossfading
            bool   fadingOut  = false;   // true = retiring; skipped by the mono search

            AHDSREnvelope  ampEnv;
            AHDSREnvelope  filtEnv;
            AHDSREnvelope  pitchEnv;
            SVFFilter      filterL, filterR;      // band: reused as the LP (high-cut) stage
            SVFFilter      hpFilterL, hpFilterR;  // band: HP (low-cut) stage, in series before filterL/R
            LFO            ampLfo, filtLfo, pitchLfo;

            bool    looping   = false;
            int64_t loopStart = 0, loopEnd = 0;
            int     pitchKeycenter = 60;
            int     tuneCents      = 0;
            double  sourceSampleRate = 44100.0;

            void reset();
        };

        // =====================================================================
        // RBJ biquad — Direct-Form-II Transposed, normalised coefficients.
        // Used by the 5-band EQ (one per channel side per band).
        // =====================================================================
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;

            void reset() { z1 = 0.0f; z2 = 0.0f; }

            inline float process (float x) noexcept
            {
                const float y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }

            void setPeaking (float freq, float gainDb, float Q, double sampleRate);
            void setLowShelf  (float freq, float gainDb, double sampleRate);
            void setHighShelf (float freq, float gainDb, double sampleRate);
        };

        // =====================================================================
        // Internal helpers
        // =====================================================================
        int   findRegion(int note, int velocity) const;
        Voice* allocateVoice();
        // glideFromPitch: the SOUNDING pitch (MIDI semitones) the previous note
        // was at, to glide from - or kNoGlide.  An absolute pitch on purpose:
        // a voice's own pitch is relative to its sample's root, so it means
        // nothing on another voice or another sample zone.
        void  startVoice(Voice* v, int note, int velocity, int regionIdx, double glideFromPitch);
        static constexpr double kNoGlide = std::numeric_limits<double>::quiet_NaN();
        // What a voice is sounding right now, in MIDI semitones: its pitch
        // relative to its sample's root, plus that root (keycenter and tune).
        static double soundingPitchOf (const Voice& v) noexcept;
        // EDM KIT: a hit on a synth kit, and publishing one (message thread).
        void  noteOnSynthDrum (const edm::Kit& kit, int note, int velocity,
                               bool fromEditor, float extraGain);
        void  publishSynthKit (const edm::Kit& kit);
        static double semitoneToRatio(double semitones, double sourceSR, double targetSR);

        // =====================================================================
        // State
        // =====================================================================
        int                       channelIndex   = 0;
        std::atomic<bool>         isDrumChannel  { false };
        std::atomic<bool>         isSoloChannel  { false };
        std::atomic<bool>         ready          { false };
        juce::String              presetName;
        juce::String              loadedDrumKitName;     // set by loadDrumKit; "" otherwise

        // Flag of the currently-published melodic instrument (-1 = none/drums).
        // Written on the message thread by the publish paths and from the
        // audio thread by selectPooledPreset (runtime PC); read by the UI's
        // 30 Hz mirror — hence atomic.
        std::atomic<int>          currentInstrumentFlag { -1 };

        // The instrument currently driving new notes.  Swapped atomically by
        // selectPooledPreset / loadPreset / loadDrumKit; read by the audio
        // thread via std::atomic_load.  Immutable once published.
        std::shared_ptr<PresetVoice> active;

        //----------------------------------------------------------------------
        // THE TWO SOURCES, SIDE BY SIDE.
        //
        // `active` is what the audio thread plays.  `blobVoice` and `sfzVoice`
        // are the two things it can BE - the library instrument the style asked
        // for, and the user's (or a full Yamaha kit's) SFZ.  Both stay resident;
        // whichever is not selected simply sleeps.
        //
        // That is the whole point of holding two: switching becomes an atomic
        // pointer swap instead of a decode.  The previous shape cleared the blob
        // on load and re-decoded it on unload, so every A/B of "does my sound
        // beat the style's" cost a full instrument decode each way.
        //----------------------------------------------------------------------
        std::shared_ptr<PresetVoice> blobVoice;   // the library instrument
        std::shared_ptr<PresetVoice> sfzVoice;    // the loaded .sfz
        std::atomic<bool>            sfzSelected { false };

        // Per-channel decoded-preset pool, keyed by blob preset index.  Filled
        // by preloadPreset on the message thread; entries are immutable and
        // kept alive for the life of the loaded style, so atomic swaps are free.
        std::unordered_map<int, std::shared_ptr<PresetVoice>> presetPool;

        // The engine's shared cache; set once at construction, never null in
        // practice. preloadPreset falls back to decoding straight into
        // presetPool if it is, so a missing cache costs speed and nothing else.
        PresetCache* sharedPresets = nullptr;

        // See setBalanceRoleGainFn. Unity when unset, so a Channel with no file
        // behind it composes exactly as it always did.
        std::function<float (int)> balanceRoleGainFn;
        float balanceRoleGain (int roleId) const
        {
            return balanceRoleGainFn ? balanceRoleGainFn (roleId) : 1.0f;
        }
        // Saved-voice stash, flag-indexed.  Written on the message thread by
        // stashPooledVoice, read on the audio thread by selectPooledPreset.
        ChannelParams                  pooledParams   [kMaxPooledFlag];
        std::atomic<float>             pooledGain     [kMaxPooledFlag];
        std::atomic<bool>              pooledHasParams[kMaxPooledFlag];
        std::atomic<bool>              pooledStashed  [kMaxPooledFlag];

        // Per-channel composed-drum-kit pool, keyed by drum Program-Change
        // value.  Filled by preloadDrumKit on the message thread; entries are
        // immutable and kept for the life of the loaded style, so the runtime
        // selectPooledDrumKit swap is an atomic pointer assignment, not a
        // 128-key sample decode.  activeDrumPoolKey tracks the kit currently
        // published (-1 = none / hand-edited kit) for UI/diagnostics.
        std::unordered_map<int, PooledDrumKit> drumKitPool;
        std::atomic<int>                       activeDrumPoolKey { -1 };

        Voice                     voices[kMaxVoices];

        // ── EDM KIT: live synth drum voices ──────────────────────────────────
        // A synth hit rides one of the voices above - same amp envelope
        // (LENGTH), per-key band filter, gains, choke groups, kit FX bus and
        // inserts.  The ONLY thing it swaps is the sample read: the voice
        // pulls its block from synthVoices[] instead of a SampleData.
        //
        // synthVoices is index-aligned with voices[], so a synth hit never
        // allocates.  synthKit is published exactly like `active`
        // (std::atomic_load / atomic_store); null = a sampled kit is loaded.
        // synthPlaceholder gives the render loop something to bind its
        // `sample` reference to on a synth voice - it is never read.
        edm::SynthVoice                   synthVoices[kMaxVoices];
        std::shared_ptr<const edm::Kit>   synthKit;

        // SOUND ENGINE settings for each program it plays (38, 39), published whole
        // by applyParams; a note picks by the current instrument flag, so a
        // program change on the audio thread allocates nothing.
        std::shared_ptr<const oscengine::Params> oscParams[2] {
            std::make_shared<const oscengine::Params> (oscengine::factory (38)),
            std::make_shared<const oscengine::Params> (oscengine::factory (39)) };
        juce::String lastEngineSpec;
        std::vector<float>                synthScratch;
        std::vector<SampleData>           synthPlaceholder;
        // A ride or crash rings up to 12 s: without a per-key cap, a steady
        // ride alone would hold most of the 48 voices.
        static constexpr int              kMaxSynthVoicesPerKey = 4;
        std::array<std::atomic<uint32_t>, 4> padActivityMask {};  // see getPadActivity
        mutable std::array<std::atomic<uint32_t>, 4> padHitMask {}; // note-on since the last poll
        std::shared_ptr<const edm::Kit>   edmPool[128];          // style-driven EDM kits, by program
        int                               edmPoolVer[128] {};    // the base version each was resolved under

        // Sounding-note mask (128 bits) published once per render block from the
        // voice pool; read by the editor's piano strip.  See getSoundingNotes().
        std::atomic<uint32_t>     soundingMask[4] { {0u}, {0u}, {0u}, {0u} };

        // ── Piano-strip key tracking (audio thread only) ─────────────────────
        //
        // Driven DIRECTLY by note-on / note-off — NOT derived from the voice pool.
        // The voice pool answers "is a voice audible", which is a different
        // question from "is the key down", and it lies in both directions:
        //
        //   * full-length DRUM keys ignore note-off entirely (see noteOff): the
        //     envelope never enters Release, so the voice sits in Sustain until
        //     the sample runs out — the key stayed lit the whole time;
        //   * any voice that misses its matching note-off sustains forever.
        //
        // Counting the MIDI events themselves is exactly what the strip is for.
        //
        // A COUNT rather than a bit, because style data legitimately overlaps the
        // same pitch: each note-on owns one instance, each note-off consumes one.
        uint8_t                   keyDownCount[128] {};

        // Editor (piano-strip) key-down counter, kept SEPARATE from the style's
        // keyDownCount.  The strip publishes the union of the two, so a key you
        // are holding always lights and always clears on release, no matter what
        // the style does to its own counter in between.  Sharing one counter was
        // why a melodic slot's strip drifted: the style holds notes for whole
        // bars, so any asymmetry stayed visible for seconds — where a drum slot's
        // one-shots returned it to zero within a frame and hid the same drift.
        uint8_t                   editorKeyDown[128] {};

        // Effective octave shift (in semitones, INCLUDING the 0..127 clamp)
        // latched per ORIGINAL key at its most recent note-on.  noteOff uses it
        // — never the live octave atomics — to decrement the SAME transposed
        // strip index its note-on incremented, so an OCTAVE-slider move between
        // on and off can't strand a lit key or drain the wrong one.
        // retuneVoice re-latches it under the new source key.  Audio thread
        // only, exactly like keyDownCount.
        int8_t                    keyDownXpose[128] {};

        // Per-note trigger latch, in samples.  Armed by noteOn and aged in
        // renderBlock, it guarantees a note stays in soundingMask long enough for
        // the UI to poll it at least once — a style drum hit is routinely shorter
        // than one 30 Hz frame.  Audio thread only; the UI never touches it.
        int                       noteLatch[128] {};
        uint32_t                  voiceSerialCounter = 0;   // see Voice::startSerial
        std::vector<int>          heldNotes;
        float                     currentPitchBendValue = 0.0f;

        double currentSampleRate = 44100.0;
        int    currentBlockSize  = 512;

        // Per-channel temp mix buffer (audio thread)
        juce::AudioBuffer<float>  tempBuffer;

        // =====================================================================
        // DrumFxBus — kit-wide FX chain for drum slots: EQ, then the rack
        // proper (Sat → Comp; the reverb and delay moved to the section bus).
        // Active only when isDrumChannel == true; otherwise ignored at render.
        // All atomics are message-thread writable; the runtime DSP state is
        // touched only by the audio thread.
        //
        // THE EQ IS NOT PART OF THE WET BLEND.  On a drum slot, renderBlock
        // runs processEq on the whole kit first and only then splits dry/wet
        // for processRack - so the rack's WET fader can no longer water down a
        // cut (at 50% WET no slider could take more than 6 dB off).  The EDM
        // families have no wet/dry and call process(), which is both in order.
        // =====================================================================
        struct DrumFxBus
        {
            // Per-stage on/off (default ON; toggles live in DrumsPopup).  Gate
            // each stage in DrumFxBus::process.
            std::atomic<bool> eqEnabled   { true };
            std::atomic<bool> satEnabled  { true };
            std::atomic<bool> compEnabled { true };
            std::atomic<bool> revEnabled  { true };
            std::atomic<bool> delEnabled  { true };
            // ── 10-band EQ: A REAL SPLIT, ONE BAND PER SLIDER ─────────────────
            // Betel::DrumSplitEq - see DrumSplitEq.h for why the ten bell
            // filters went.  The gains are still stored in dB, one per band, in
            // exactly the places they always were, so every saved kit loads
            // as it is; only what the numbers DO changed.
            std::atomic<float> eqGainDb [DrumSplitEq::kBands] {
                { 0.0f },{ 0.0f },{ 0.0f },{ 0.0f },{ 0.0f },
                { 0.0f },{ 0.0f },{ 0.0f },{ 0.0f },{ 0.0f }
            };
            DrumSplitEq eq;

            // ── Saturation (tanh) ─────────────────────────────────────────────
            std::atomic<float> satDrive { 0.0f };   // 0..1 — mapped to 1..20× pre-gain
            std::atomic<float> satMix   { 1.0f };   // 0..1 wet

            // ── Compressor (feed-forward, peak detector) ──────────────────────
            std::atomic<float> compThreshDb  { 0.0f };
            std::atomic<float> compRatio     { 1.0f };
            std::atomic<float> compAttackMs  { 5.0f };
            std::atomic<float> compReleaseMs { 50.0f };
            std::atomic<float> compMakeupDb  { 0.0f };
            float compEnv = 0.0f;   // envelope state (linear)

            // ── Reverb — Betel::ReverbFx, THE MELODIC ENGINE ──────────────────
            //
            // This was juce::Reverb (Freeverb) with a size/damp cache and a
            // scratch buffer.  Two different reverbs in one plugin meant a
            // drum bus and a melodic slot with identical numbers on identical
            // knobs sounded nothing alike, and neither TAIL, PRE, the send band
            // nor the PLATE algorithm existed on this side at all.
            //
            // ReverbFx does its own dry/wet SUM in place, so the scratch buffer
            // and the manual dry gain that used to follow it are both gone.
            std::atomic<float> revSize     { 0.5f };
            std::atomic<float> revDamp     { 0.5f };
            std::atomic<float> revWet      { 0.0f };
            std::atomic<float> revDry      { 1.0f };
            std::atomic<float> revTail     { 0.5f };
            std::atomic<float> revPreDelay { 0.0f };
            std::atomic<float> revHpNorm   { 0.2917f };
            std::atomic<float> revLpNorm   { 1.0f };
            std::atomic<int>   revAlgo     { 0 };
            std::atomic<float> revWetBase  { 1.0f };
            Betel::ReverbFx    reverbFx;

            // ── Delay ─────────────────────────────────────────────────────────
            // Two time sources, matching the melodic delay: free ms, or a
            // musical division resolved against the host BPM at render.  The
            // free time survives while synced, so switching SYNC off returns to
            // exactly the ms that was dialled.
            // Betel::StereoDelayFx now, the same ping-pong the melodic channels
            // use.  The hand-rolled ring buffer (delL/delR/delWritePos/delSize)
            // is gone with it - StereoDelayFx owns its own lines.
            //
            // BOTH TIME SOURCES SURVIVE.  delaySamples is computed here and
            // handed to the engine, so free-ms and musical division are a
            // question this bus answers before the DSP is involved - the engine
            // swap costs nothing that was already working.
            std::atomic<bool>  delSync    { false };
            std::atomic<int>   delTimeSig { 0 };     // 0=4/4, 1=3/4
            std::atomic<int>   delDiv     { 2 };     // index into the active table
            std::atomic<float> delTimeMs  { 250.0f };
            std::atomic<float> delFb      { 0.3f };
            std::atomic<float> delWet     { 0.0f };
            std::atomic<float> delDry     { 1.0f };
            std::atomic<float> delWetBase { 0.5f };
            Betel::StereoDelayFx delayFx;

            double sampleRate = 44100.0;

            // Master rack wet/dry (1 = fully wet, 0 = bypass).  Stored here so
            // the audio thread reads a single atomic; the actual blend is done
            // in Channel::renderNextBlock after process() returns.
            std::atomic<float> fxWet { 1.0f };

            void prepare (double sr, int blockSize);
            // The EQ alone: the EDM families prepare only what they run.
            void prepareEq (double sr);
            void reset();

            // The EQ on its own, in place.  R may be null (mono).
            void processEq (float* L, float* R, int numSamples);
            // The rack after the EQ: saturation, then compression.
            // hostBPM is kept for the call sites; nothing in the rack is
            // tempo-synced since the delay moved to the section bus.
            void processRack (juce::AudioBuffer<float>& buf, int numSamples, double hostBPM);
            // Both, in order - for the EDM families, which have no wet/dry.
            void process (juce::AudioBuffer<float>& buf, int numSamples, double hostBPM);
        };

        DrumFxBus drumFxBus;

        // EDM KIT: ONE RACK PER FAMILY.  Every family of an EDM kit (the pad
        // colours) has its own EQ / SAT / COMP / SWEET / PAN / SENDS, set in the
        // kit (edm::Kit::fx) and read from the kit the block plays - so a kit
        // switch brings its racks along with it.  A family left neutral sums
        // straight into the kit mix and costs nothing (edmFamOn false).
        std::array<DrumFxBus, edm::kNumFamilies>                edmFamBus;
        std::array<Betel::SweetenerFx, edm::kNumFamilies>       edmFamSweet;
        std::array<juce::AudioBuffer<float>, edm::kNumFamilies> edmFamBuf;
        std::array<bool, edm::kNumFamilies>                     edmFamOn {};   // audio thread, per block
        void processEdmFamily (int f, const edm::FamilyFx& fx, int numSamples, double bpm,
                               SectionSendFx* sectionFx);
        // Scratch buffers for the kit FX rack (insert): the whole drum mix is
        // summed into drumFxScratch, copied to drumFxDry, processed in place,
        // then blended back per the master wet/dry.
        juce::AudioBuffer<float> drumFxScratch;
        juce::AudioBuffer<float> drumFxDry;

        // EQ runtime state
        Biquad eqL [5];
        Biquad eqR [5];
        float  cachedEqGain [5] { 0,0,0,0,0 };
        float  cachedEqFreq [5] { -1,-1,-1,-1,-1 };

        // Reverb runtime — Bambino Schroeder reverb (does its own dry/wet mix).
        Betel::ReverbFx reverbFx;

        // Delay runtime — Bambino ping-pong stereo delay.  The musical-time
        // delay length (samples) is computed per block and passed to process().
        Betel::StereoDelayFx delayFx;

        // Sounds-path insert FX engines (melodic channels only)
        // SWEETENER — one instance per channel, ahead of everything else in the
        // insert chain.  Its parameters live in atomics beside it because the
        // audio thread reads them once per block.
        Betel::SweetenerFx    sweetenerFx;
        std::atomic<bool>     sweetEnabled     { false };
        std::atomic<float>    sweetMix         { 1.0f  };
        std::atomic<bool>     sweetSoftenOn    { true  };
        std::atomic<float>    sweetSoftenDepth { 0.0f  };
        // Slot-wide velocity curve (melodic).  A DRUM channel ignores this and
        // reads the per-key value instead - see Channel::noteOn.
        std::atomic<float>    slotVelCurve     { 0.0f  };
        std::atomic<float>    globalVelCurve   { 0.0f  };   // keyboard compensation
        std::atomic<bool>     ignorePresetParams { false };  // voice freeze

        std::atomic<float>    sweetSoftenMs    { 8.0f  };
        std::atomic<bool>     sweetPeakOn      { true  };
        std::atomic<float>    sweetPeakCeilDb  { 12.0f };
        std::atomic<float>    sweetPeakRatio   { 1.0f  };
        std::atomic<bool>     sweetTameOn      { true  };
        std::atomic<float>    sweetTameDepthDb { 0.0f  };
        std::atomic<float>    sweetTameFreqHz  { 4000.0f };
        std::atomic<bool>     sweetRoundOn     { true  };
        std::atomic<float>    sweetRoundDrive  { 0.0f  };
        std::atomic<float>    sweetRoundMix    { 1.0f  };

        /** Read the atomics into a Params and run the block in place. */
        void applySweetenerInPlace (juce::AudioBuffer<float>& buf, int numSamples) noexcept
        {
            if (! sweetEnabled.load()) return;

            Betel::SweetenerFx::Params sp;
            sp.enabled     = true;
            sp.mix         = sweetMix        .load();
            sp.softenOn    = sweetSoftenOn   .load();
            sp.softenDepth = sweetSoftenDepth.load();
            sp.softenMs    = sweetSoftenMs   .load();
            sp.peakOn      = sweetPeakOn     .load();
            sp.peakCeilDb  = sweetPeakCeilDb .load();
            sp.peakRatio   = sweetPeakRatio  .load();
            sp.tameOn      = sweetTameOn     .load();
            sp.tameDepthDb = sweetTameDepthDb.load();
            sp.tameFreqHz  = sweetTameFreqHz .load();
            sp.roundOn     = sweetRoundOn    .load();
            sp.roundDrive  = sweetRoundDrive .load();
            sp.roundMix    = sweetRoundMix   .load();

            float* L = buf.getWritePointer (0);
            float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
            sweetenerFx.process (L, R, numSamples, sp);
        }

        // ── IDLE-CHANNEL SKIP ────────────────────────────────────────────────
        //
        // Blocks this channel has gone without a sounding voice.  Once it
        // reaches kChainTailBlocks the whole insert chain stops running - see
        // renderBlock.  Audio thread only; nothing else reads it.
        //
        // kChainTailBlocks is ~1 second at 44.1 kHz / 512 and is deliberately
        // far longer than any insert here rings: being early truncates a tail
        // audibly, being late costs one idle second of DSP.
        static constexpr int kChainTailBlocks = 86;
        int silentBlocks = 0;

        // ── THE EXPRESSION RAMP ──────────────────────────────────────────────
        //
        // expStart/expEnd bracket ONE BLOCK and the voice loop interpolates
        // between them per sample, so a step of any size becomes a straight
        // line across the block instead of an edge.  Block-rate smoothing was
        // not enough on its own: at 512 samples a one-pole still moves half the
        // distance in one go, and half of 12.7 dB is still a click.
        //
        // 15 ms is the time constant.  Long enough that nothing steps audibly,
        // short enough that a written swell still arrives on time - a style
        // rides expression in 16th notes and at 180 BPM that is 83 ms.
        static constexpr float kExpSmoothMs = 15.0f;
        float expSmoothed = 1.0f;     // carried across blocks
        float expStart    = 1.0f;     // this block's ramp, computed in renderBlock
        float expEnd      = 1.0f;

        // Which of the FIVE channel EQ bands are not flat, rebuilt only when a
        // coefficient changes.  Belongs to Channel::applyEqInPlace - the drum
        // rack's ten-band EQ is a different EQ inside DrumFxBus and has its own.
        int  eqActiveIdx[5] {};
        int  eqActiveCount = 0;
        bool eqListDirty   = true;

        Betel::ChorusFx       chorusFx;
        Betel::AutoWahFx      wahFx;
        Betel::PhaserFx       phaserFx;

        // FUNKEY's own pair.  See the atomics above for why they are not shared
        // with the two directly beside them.
        Betel::AutoWahFx      funkeyWahFx;
        Betel::PhaserFx       funkeyPhaserFx;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Channel)
    };
} // namespace Betel
