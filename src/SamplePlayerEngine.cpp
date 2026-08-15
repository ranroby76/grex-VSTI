
//==============================================================================
// SamplePlayerEngine.cpp  —  top-level engine implementation.
//
// Channel index conventions used throughout Betelgeuse:
//   0..15  →  STYLE channels (visible as "CH. 1".."CH. 16")
//   16..23 →  SOLO channels  (visible as "CH. 17".."CH. 24")
//==============================================================================

#include "SamplePlayerEngine.h"
#include "SfzLoader.h"
#include "SlotParamConvert.h"   // slotParamsToChannelParams (apply .ins settings)
#include <cmath>

namespace Betel
{
    SamplePlayerEngine::SamplePlayerEngine()
    {
        // Assign stable channel indices so each Channel knows where it sits.
        for (int i = 0; i < kNumChannels; ++i)
            channels[(size_t) i].setIndex(i);

        // Style BASS (engine channel 2) plays at its AUTHORED register — no
        // hidden octave bias.  The old +1 bias here silently transposed every
        // bass note +12 semitones ("so the octave slider can read 0"), which
        // killed the low octave: authored G1 sounded G2, fundamentals moved
        // from ~33–65 Hz up to 65–130 Hz, and the allowed-notes C1 floor never
        // truly reached C1.  Blobs whose keymaps start above the authored
        // register are covered by findRegion's octave fallback instead (the
        // note still SOUNDS at the requested pitch via pitchKeycenter).

        // Pre-populate the default drum-kit PC mapping (GM2-ish + custom slots
        // for our 14 kits).  Host can override via setDrumKitPCMapping().
        populateDefaultDrumKitPCMap();
    }

    SamplePlayerEngine::~SamplePlayerEngine() = default;

    //==========================================================================
    void SamplePlayerEngine::prepareToPlay(double sampleRate, int blockSize)
    {
        currentSampleRate = sampleRate;
        currentBlockSize  = blockSize;
        for (auto& ch : channels) ch.prepare(sampleRate, blockSize);
        styleScratch.setSize (2, blockSize, false, true, true);
    }

    void SamplePlayerEngine::releaseResources()
    {
        for (auto& ch : channels) ch.release();
    }

    //==========================================================================
    void SamplePlayerEngine::renderBlock(juce::AudioBuffer<float>& outBuffer,
                                         int numSamples, double hostBPM)
    {
        // Mix every channel additively into the output buffer through two
        // gain buses:
        //   • STYLE bus  → channels [0 .. kNumActiveStyleChannels)   × styleBusGain
        //   • RIGHT-HAND → channels [kNumStyleChannels .. kNumChannels)
        //                    × soloBusGain × soloBusBaseUnity
        // Style channels 8..15 are unreachable (see kNumActiveStyleChannels) and
        // always silent, so they are skipped entirely.  When a bus gain is at
        // unity (the common case) its channels render straight into the output
        // (a flat loop, identical to before).  Otherwise the group renders
        // through the scratch buffer so it can be scaled before summing.  The
        // scratch is reused sequentially per group.
        auto renderGroup = [&] (int first, int last, float busGain)
        {
            if (std::abs (busGain - 1.0f) > 0.0001f)
            {
                if (styleScratch.getNumSamples() < numSamples)
                    styleScratch.setSize (2, numSamples, false, false, true);
                styleScratch.clear (0, 0, numSamples);
                styleScratch.clear (1, 0, numSamples);

                for (int i = first; i < last; ++i)
                    channels[(size_t) i].renderBlock (styleScratch, numSamples, hostBPM);

                const int numCh = outBuffer.getNumChannels();
                for (int c = 0; c < numCh; ++c)
                    outBuffer.addFrom (c, 0, styleScratch,
                                       juce::jmin (c, styleScratch.getNumChannels() - 1),
                                       0, numSamples, busGain);
            }
            else
            {
                for (int i = first; i < last; ++i)
                    channels[(size_t) i].renderBlock (outBuffer, numSamples, hostBPM);
            }
        };

        // Spike diagnostics: time the two buses separately so a spike can be
        // attributed to the style parts or the right hand rather than just "the
        // audio thread".  Two tick reads per block — see PerfMonitor.h.
        const int64_t tA = juce::Time::getHighResolutionTicks();
        renderGroup (0,                 kNumActiveStyleChannels,
                     styleBusGain.load() * styleMakeupGain.load()
                                         * styleSectionTrim.load());            // style bus
        const int64_t tB = juce::Time::getHighResolutionTicks();
        // Right-hand bus: fader x base unity.  The base is what makes the solo
        // detent mean the same loudness as the style detent -- the style bus has
        // BOOST and MAKEUP behind it and this one has nothing, so without a base
        // the two unities are simply not the same level.
        renderGroup (kNumStyleChannels, kNumChannels,
                     soloBusGain.load() * soloBusBaseUnity.load());              // right-hand bus
        const int64_t tC = juce::Time::getHighResolutionTicks();

        lastStyleTicks = tB - tA;
        lastSoloTicks  = tC - tB;
        lastVoiceCount = 0;
        for (auto& c : channels) lastVoiceCount += c.activeVoiceCount();

        // ── Master gain (no limiting / soft-clip) ──────────────────────────
        // Clean linear master volume — the channel sum flows through untouched.
        // No knee, no asymptotic clip; the signal is left exactly as the buses
        // produced it (× masterVolume).  Unity is the common case, so the
        // per-sample pass is skipped entirely when mv == 1.0.
        const float mv = masterVolume.load();
        if (std::abs (mv - 1.0f) > 1.0e-6f)
            outBuffer.applyGain (0, numSamples, mv);
    }

    //==========================================================================
    // MIDI routing
    //==========================================================================
    void SamplePlayerEngine::setGlobalTransposeSemis (int semis) noexcept
    {
        for (auto& c : channels) c.setSemitoneOffset (semis);
    }

    void SamplePlayerEngine::noteOn(int channelIndex, int note, int velocity, bool fromEditor,
                                    float extraGain)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;
        channels[(size_t) channelIndex].noteOn(note, velocity, fromEditor, extraGain);
    }

    void SamplePlayerEngine::noteOff(int channelIndex, int note, bool fromEditor)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;
        channels[(size_t) channelIndex].noteOff(note, fromEditor);
    }

    bool SamplePlayerEngine::retuneNote(int channelIndex, int oldNote, int newNote)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return false;
        return channels[(size_t) channelIndex].retuneVoice(oldNote, newNote);
    }

    void SamplePlayerEngine::allNotesOff(int channelIndex)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;
        channels[(size_t) channelIndex].allNotesOff();
    }

    void SamplePlayerEngine::allChannelsOff()
    {
        for (auto& ch : channels) ch.allNotesOff();
    }

    void SamplePlayerEngine::pitchBend(int channelIndex, float normalized)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;
        channels[(size_t) channelIndex].pitchBend(normalized);
    }

    //==========================================================================
    // Blob loading
    //==========================================================================
    bool SamplePlayerEngine::loadBlob(const juce::File& file, const juce::String& accessCode)
    {
        // Tear down everything first — opening a new blob invalidates the
        // memory mapping that channels read from during loadPreset().
        for (auto& ch : channels) ch.clearPreset();
        blobLoaded.store(false);

        if (!blob.open(file.getFullPathName().toStdString(),
                       accessCode.toStdString()))
            return false;

        blobFile       = file;
        blobAccessCode = accessCode;
        blobLoaded.store(true);
        return true;
    }

    std::vector<juce::String> SamplePlayerEngine::getBlobPresetNames() const
    {
        std::vector<juce::String> out;
        if (!blobLoaded.load()) return out;
        const auto& presets = blob.getPresets();
        out.reserve(presets.size());
        for (const auto& p : presets) out.push_back(juce::String(p.name));
        return out;
    }

    int SamplePlayerEngine::getBlobPresetCount() const
    {
        return blobLoaded.load() ? (int) blob.getPresets().size() : 0;
    }

    // selectChannelPreset / preloadChannelPreset take an instrument FLAG and
    // resolve it through the per-instrument sound library (flag -> .frb file).
    // The blob is opened transiently: Channel copies the decoded samples into
    // its own pool (keyed by flag), so the reader can close immediately.
    std::shared_ptr<Channel::PresetVoice> SamplePlayerEngine::getOrDecodeInstrument (int flag)
    {
        // ── Missing-flag fallback (the walk BankProgramMap documents) ─────────
        // A Yamaha program change can resolve to a GM flag this blob doesn't
        // carry — "special" Yamaha voices with no exact GM match, sparse custom
        // libraries, or simply a program our pack omits.  Without a fallback,
        // the lookup below returns nullptr and the caller (selectChannelPreset /
        // preloadChannelPreset → selectPooledPreset) silently keeps the channel
        // on its PREVIOUS instrument — the "wrong instrument after a program
        // change" bug.  Degrade a missing flag to the nearest present voice in
        // the same GM family (groups of 8), then Piano, then anything audible,
        // so a PC always lands on a related sound.  Decoding under the resolved
        // flag means preloadChannelPreset pools it under the ORIGINAL flag, so a
        // later runtime selectPooledPreset(originalFlag) finds these samples.
        if (flag < 0) flag = 0;
        if (soundLibrary.find (flag) == soundLibrary.end())
        {
            int resolved = -1;
            const int base = flag & ~0x07;                 // GM family base
            for (int f = base; f < base + 8; ++f)
                if (soundLibrary.find (f) != soundLibrary.end()) { resolved = f; break; }
            if (resolved < 0 && soundLibrary.find (0) != soundLibrary.end())
                resolved = 0;                              // Piano — the GM anchor
            if (resolved < 0 && ! soundLibrary.empty())
                resolved = soundLibrary.begin()->first;    // last resort: anything present
            if (resolved >= 0) flag = resolved;
        }

        // Cache hit: hand out the SAME decoded instrument (no decode, no copy).
        auto c = decodedInstrumentCache.find (flag);
        if (c != decodedInstrumentCache.end()) return c->second;

        auto it = soundLibrary.find (flag);
        if (it == soundLibrary.end()) return nullptr;            // unknown flag

        BlobReader reader;
        if (! reader.open (it->second.getFullPathName().toStdString(),
                           libraryAccessCode.toStdString()))
            return nullptr;

        auto pv = Channel::decodeInstrument (reader, 0);
        if (pv == nullptr) return nullptr;

        // Soft cap: evict entries no channel references any more.
        if ((int) decodedInstrumentCache.size() >= kMaxCachedInstruments)
            for (auto e = decodedInstrumentCache.begin();
                 e != decodedInstrumentCache.end()
                 && (int) decodedInstrumentCache.size() >= kMaxCachedInstruments;)
                e = (e->second.use_count() == 1) ? decodedInstrumentCache.erase (e)
                                                 : std::next (e);

        decodedInstrumentCache[flag] = pv;
        return pv;
    }

    //==========================================================================
    // USER SFZ OVERRIDE
    //==========================================================================
    bool SamplePlayerEngine::loadSfzOnChannel (int channelIndex,
                                               const juce::File& sfzFile,
                                               juce::String& errorOut,
                                               juce::String& nameOut)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels)
        { errorOut = "Bad channel."; return false; }

        Channel::PresetVoice pv;
        const auto res = SfzLoader::load (sfzFile, pv);
        if (! res.ok) { errorOut = res.error; return false; }

        // Published exactly like a decoded blob, through adoptInstrument's own
        // atomic swap - so the switch is as safe mid-playback as a style's own
        // program change is.
        auto shared = std::make_shared<Channel::PresetVoice> (std::move (pv));
        shared->name = res.name;
        channels[(size_t) channelIndex].adoptSfzVoice (shared);

        {
            const juce::ScopedLock sl (sfzLock);
            sfzNames[(size_t) channelIndex] = res.name;
            sfzPaths[(size_t) channelIndex] = sfzFile.getFullPathName();
        }
        sfzActive[(size_t) channelIndex].store (true);

        nameOut = res.name;
        return true;
    }

    /** Put the channel back on its blob source WITHOUT forgetting the SFZ.

        The sleeping source stays parked, so toggling back on is another pointer
        swap rather than a re-parse of the .sfz and its samples. */
    void SamplePlayerEngine::sleepSfzOnChannel (int channelIndex)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;
        sfzActive[(size_t) channelIndex].store (false);
        channels[(size_t) channelIndex].selectSfz (false);
    }

    void SamplePlayerEngine::wakeSfzOnChannel (int channelIndex)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;
        if (! channels[(size_t) channelIndex].hasSfzVoice()) return;
        sfzActive[(size_t) channelIndex].store (true);
        channels[(size_t) channelIndex].selectSfz (true);
    }

    void SamplePlayerEngine::clearSfzOnChannel (int channelIndex)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;

        sfzActive[(size_t) channelIndex].store (false);
        {
            const juce::ScopedLock sl (sfzLock);
            sfzNames[(size_t) channelIndex].clear();
            sfzPaths[(size_t) channelIndex].clear();
        }

        // Drop it for good and fall back to the parked blob source - no decode,
        // because that source never went away.  Only if the channel has no blob
        // (nothing was ever loaded) do we ask for the flag again.
        auto& ch = channels[(size_t) channelIndex];
        ch.dropSfzVoice();

        if (! ch.isReady())
        {
            const int flag = ch.getCurrentInstrumentFlag();
            if (flag >= 0) selectChannelPreset (channelIndex, flag);
        }
    }

    void SamplePlayerEngine::selectChannelPreset(int channelIndex, int flag)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;

        // THE SFZ HOLDS THE SLOT.  A style sends program changes to all eight
        // style channels on every load and often mid-song; without this the
        // user's instrument would survive until the next bar and no longer.
        // The flag is still recorded on the channel, so switching the override
        // off returns to exactly the sound the style asked for.
        if (sfzActive[(size_t) channelIndex].load())
        {
            channels[(size_t) channelIndex].noteCurrentInstrumentFlag (flag);
            return;
        }
        // Not overridden: this load becomes the channel's BLOB source, and is
        // what a later selectSfz(false) will return to.
        if (auto pv = getOrDecodeInstrument (flag))
        {
            channels[(size_t) channelIndex].adoptInstrument (flag, pv, true);

            // The per-sound gain trim belongs to the SOUND, not the channel, so
            // it goes back to unity here.  Without the reset a calibrated voice
            // would leak its trim onto whatever the style loads next on the
            // same channel — the -6 dB that tamed a hot piano would quietly
            // bury the strings that replaced it.  The .ins below re-establishes
            // the incoming sound's own trim when it has one.
            // ── THE TRIM IS A SETTING, SO THE FREEZE HOLDS IT TOO ────────────
            //
            // This reset exists so a trim calibrated for the outgoing sound does
            // not bury the incoming one.  That is right for a normal swap and
            // wrong for a frozen slot: the trim is one of the settings being
            // carried across on purpose, and resetting it to unity would leave
            // exactly one thing to re-tweak after every sound change - which is
            // the tweaking the freeze exists to remove.
            if (! channels[(size_t) channelIndex].getIgnorePresetParams())
                setChannelInstrumentGainPercent (channelIndex, 100.0f);

            // Default-preset override: if the user saved this flag with "Save as
            // Default", re-apply the stored SlotParams on top of the freshly
            // loaded samples so PC-driven loads and UI selectors restore the
            // exact saved voice (envelopes / filter / LFOs / EQ / FX / gain).
            //
            // WHICH saved voice depends on where this channel sits: a style
            // channel takes its .sins if one exists, otherwise the .ins; the
            // right hand always takes the .ins.  That is the whole point of the
            // split — the same instrument can be unity under the right hand and
            // pulled back inside an arrangement.
            // Keep the pooled stash in step, so a later runtime PC back to this
            // same flag restores the identical voice.
            stashVoiceFor (channelIndex, flag);

            // NO SAVED VOICE -> A NEUTRAL ONE, never the outgoing sound's.
            //
            // A gain-only preset says nothing about the voice, and neither does
            // no preset at all — but "say nothing" used to mean "leave the last
            // instrument's voice in place".  Save a full voice on the french
            // horn, let the style switch back to the flute, and the flute played
            // through the horn's envelopes, filter and FX.  The gain was already
            // reset for exactly this reason; the params were not.
            //==============================================================
            // IGNORE PRESET CHANGES - the freeze has to be honoured HERE.
            //
            // This is where a .ins actually reaches the audio, and it is
            // reached from every direction: a set restoring a slot's patch, a
            // style's program change, the instrument selector, a runtime PC
            // through CASM routing.  Gating the UI paths alone left this one
            // open, so a set load still overwrote a frozen slot's voice - the
            // samples changed and the voicing went with them.
            //
            // What the freeze holds is the VOICE - envelopes, filter, LFOs, EQ,
            // FX, trim.  The samples still change, because the freeze is about
            // how a sound is shaped, not about which sound the slot holds; that
            // is what makes it useful for auditioning one voicing across
            // several instruments.
            //
            // The gain reset above is deliberately left alone: it runs before
            // this and belongs to the SOUND swap, and skipping it would let a
            // frozen slot keep a trim that was calibrated for the sound that
            // just left.
            if (! channels[(size_t) channelIndex].getIgnorePresetParams())
            {
                const auto* preset = melodicPresetFor (channelIndex, flag);
                const bool  hasVoice = (preset != nullptr && ! preset->gainOnly);

                if (! hasVoice)
                    applyChannelParams (channelIndex, Channel::ChannelParams{});
                else
                    applyChannelParams (channelIndex, slotParamsToChannelParams (preset->params));

                if (preset != nullptr)
                {
                    // The trim rides its own path either way, so it is applied here
                    // explicitly rather than arriving inside the params above.
                    setChannelInstrumentGainPercent (channelIndex, preset->params.gainPercent,
                                                     preset->params.baseUnityDb);
                }
            }
        }
    }

    void SamplePlayerEngine::preloadChannelPreset(int channelIndex, int flag)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;

        // Stash unconditionally, even when the pool is already warm: a Save as
        // Default between two preloads changes the saved voice without changing
        // the samples, and the stash is what a runtime PC will read.
        stashVoiceFor (channelIndex, flag);

        if (channels[(size_t) channelIndex].isInstrumentPooled (flag)) return; // already warm
        if (auto pv = getOrDecodeInstrument (flag))
            channels[(size_t) channelIndex].adoptInstrument (flag, pv, false);
    }

    void SamplePlayerEngine::selectChannelPooledPreset(int channelIndex, int flag)
    {
        if (channelIndex < 0 || channelIndex >= kNumChannels) return;
        channels[(size_t) channelIndex].selectPooledPreset(flag);   // RT-safe swap by flag
    }

    //==========================================================================
    // Per-instrument sound library (message thread)
    //==========================================================================
    //==========================================================================
    // SCAN THE THREE PACKS.
    //
    // RECURSIVE now, and that is the point: every pack sorts its sounds into
    // category subfolders, and the old scan passed `false` for recursion so a
    // sound one level down was invisible.  A pack installed today would have
    // looked completely empty to the old code.
    //
    // The CATEGORY is the subfolder name directly under the pack root, so
    // "world/Ouds/1203-Turkish Oud.frb" is category "Ouds".  A file sitting
    // loose in the pack root gets an empty category and still loads - it just
    // has no tab to appear under, which is visible enough to get noticed and
    // harmless enough not to break anything.
    //
    // Category ORDER is the order the folders come back in, sorted - so it is
    // stable between runs and a user can force an order by prefixing folder
    // names with digits if they want one.
    //==========================================================================
    void SamplePlayerEngine::setSoundLibraryFolders (const juce::File& gmFolder,
                                                     const juce::File& worldFolder,
                                                     const juce::File& orientalFolder,
                                                     const juce::String& accessCode)
    {
        soundLibrary.clear();
        decodedInstrumentCache.clear();   // decoded data belongs to the old library
        soundLibraryNames.clear();
        soundLibraryPack.clear();
        soundLibraryCategory.clear();
        for (auto& c : packCategories) c.clear();
        libraryCollisions.clear();

        // GM's folder stays the "the" library folder for anything that still
        // asks for one (preset resolution, relative lookups): it is the only
        // pack guaranteed to be installed.
        soundLibraryFolder = gmFolder;
        libraryAccessCode  = accessCode;

        const juce::File roots[kNumSoundPacks] = { gmFolder, worldFolder, orientalFolder };
        const char* const packNames[kNumSoundPacks] = { "gm sounds", "world", "oriental" };

        juce::StringArray clashes;
        juce::StringArray unnumbered;
        int loadedPerPack[kNumSoundPacks] = { 0, 0, 0 };
        bool packPresent[kNumSoundPacks]  = { false, false, false };

        for (int pack = 0; pack < kNumSoundPacks; ++pack)
        {
            const auto& root = roots[(size_t) pack];
            if (! root.isDirectory()) continue;          // pack simply not installed
            packPresent[(size_t) pack] = true;

            for (const auto& f : root.findChildFiles (juce::File::findFiles, true, "*.frb"))
            {
                const juce::String base = f.getFileNameWithoutExtension();

                // Drum-kit blobs (*_kit.frb) are owned by DrumKitRegistry -- skip.
                if (base.endsWithIgnoreCase ("_kit")) continue;

                // Parse the leading run of digits = the instrument flag.
                int i = 0;
                while (i < base.length() && juce::CharacterFunctions::isDigit (base[i]))
                    ++i;
                // ── NO NUMBER, NO SOUND - AND SAY SO ─────────────────────────
                // The flag IS the leading digits; a file without them cannot be
                // referred to by a saved set and is skipped.  Silently, until
                // now - which meant a pack whose files had never been numbered
                // loaded as completely empty with nothing anywhere to say why.
                if (i == 0)
                {
                    if (unnumbered.size() < 40)          // enough to see the shape
                        unnumbered.add ("   " + juce::String (packNames[(size_t) pack])
                                        + "  " + f.getFileName());
                    continue;
                }

                const int flag = base.substring (0, i).getIntValue();
                if (flag < 0) continue;

                // ── DUPLICATE FLAGS ARE REPORTED, NOT SILENTLY RESOLVED ───────
                // soundLibrary is keyed by flag, so the second file would simply
                // replace the first and a saved set would load whichever won the
                // scan order that day.  Better to keep the first and say so.
                if (soundLibrary.find (flag) != soundLibrary.end())
                {
                    clashes.add (juce::String (flag) + "  kept: "
                                 + soundLibrary[flag].getFileName()
                                 + "   ignored: " + f.getFileName()
                                 + "  (" + packNames[(size_t) pack] + ")");
                    continue;
                }

                soundLibrary[flag] = f;

                const int dash = base.indexOfChar ('-');
                soundLibraryNames[flag] = (dash >= 0) ? base.substring (dash + 1).trim()
                                                      : base.substring (i).trim();

                soundLibraryPack[flag] = pack;

                // The category is the FIRST folder under the pack root, however
                // deep the file actually sits - so a pack may nest further for
                // its own tidiness without inventing categories nobody asked for.
                juce::String category;
                for (auto dir = f.getParentDirectory();
                     dir.isDirectory() && dir != root;
                     dir = dir.getParentDirectory())
                {
                    category = dir.getFileName();
                    if (dir.getParentDirectory() == root) break;
                }

                soundLibraryCategory[flag] = category;

                if (category.isNotEmpty()
                    && ! packCategories[(size_t) pack].contains (category))
                    packCategories[(size_t) pack].add (category);

                ++loadedPerPack[(size_t) pack];
            }

            packCategories[(size_t) pack].sort (true);   // stable between runs
        }

        //======================================================================
        // THE SCAN REPORT.
        //
        // Always written when there is anything to say, not only on a collision.
        // An empty pack has exactly two causes - the folder is not there, or its
        // files carry no numbers - and neither is visible from the UI, which
        // just shows a bank with no categories in it.
        //======================================================================
        juce::StringArray report;

        for (int pack = 0; pack < kNumSoundPacks; ++pack)
            report.add (juce::String (packNames[(size_t) pack]).paddedRight (' ', 18)
                        + (packPresent[(size_t) pack]
                              ? juce::String (loadedPerPack[(size_t) pack]) + " sound(s) loaded"
                              : juce::String ("folder not found - pack not installed")));

        if (! unnumbered.isEmpty())
        {
            report.add ({});
            report.add ("FILES SKIPPED - NO NUMBER AT THE START OF THE NAME.");
            report.add ("An instrument's number is the leading digits of its file name,");
            report.add ("and that number is what every saved set stores.  A file without");
            report.add ("one cannot be referred to, so it is not loaded at all.");
            report.add ("Rename them to \"NNNN-Name.frb\" and rescan.");
            report.add ({});
            report.addArray (unnumbered);
        }

        if (! clashes.isEmpty())
        {
            report.add ({});
            report.add ("DUPLICATE INSTRUMENT NUMBERS - the second file of each pair is");
            report.add ("NOT loaded.  Every number must be unique across all three packs,");
            report.add ("because saved sets store the number.");
            report.add ({});
            report.addArray (clashes);
        }

        if (! unnumbered.isEmpty() || ! clashes.isEmpty()
            || loadedPerPack[PackGm] == 0)
            libraryCollisions = report.joinIntoString ("\n");
    }

    int SamplePlayerEngine::getInstrumentPack (int flag) const
    {
        const auto it = soundLibraryPack.find (flag);
        return it == soundLibraryPack.end() ? (int) PackGm : it->second;
    }

    juce::String SamplePlayerEngine::getInstrumentCategory (int flag) const
    {
        const auto it = soundLibraryCategory.find (flag);
        return it == soundLibraryCategory.end() ? juce::String() : it->second;
    }

    const juce::StringArray& SamplePlayerEngine::getPackCategories (int pack) const
    {
        static const juce::StringArray empty;
        if (pack < 0 || pack >= kNumSoundPacks) return empty;
        return packCategories[(size_t) pack];
    }

    std::vector<std::pair<int, juce::String>>
        SamplePlayerEngine::getPackInstruments (int pack, const juce::String& category) const
    {
        std::vector<std::pair<int, juce::String>> out;

        // soundLibrary is a std::map, so this walks in ascending flag order and
        // the caller gets a stable, numerically sorted page every time.
        for (const auto& kv : soundLibrary)
        {
            const int flag = kv.first;
            if (getInstrumentPack (flag) != pack) continue;
            if (getInstrumentCategory (flag) != category) continue;

            const auto n = soundLibraryNames.find (flag);
            out.push_back ({ flag, n == soundLibraryNames.end() ? juce::String() : n->second });
        }
        return out;
    }

    bool SamplePlayerEngine::hasInstrumentFlag (int flag) const
    {
        return soundLibrary.find (flag) != soundLibrary.end();
    }

    void SamplePlayerEngine::setInstrumentPresetFolder (const juce::File& folder)
    {
        instrumentPresetFolder = folder;
        InstrumentPresetIO::scanFolder (folder, &instrumentPresets, &drumPresets,
                                        /*styleSet*/ false);
        refreshPooledVoices();
    }

    void SamplePlayerEngine::setInstrumentPresetFolders (const std::vector<juce::File>& folders)
    {
        // The FIRST folder stays the "write here" answer for anything that saves
        // without naming a pack; GM is always installed, so it is the safe one.
        instrumentPresetFolder = folders.empty() ? juce::File() : folders.front();
        InstrumentPresetIO::scanFolders (folders, &instrumentPresets, &drumPresets,
                                         /*styleSet*/ false);
        refreshPooledVoices();
    }

    void SamplePlayerEngine::setStyleInstrumentPresetFolder (const juce::File& folder)
    {
        // Melodic only.  Drum kits are not split — a kit never loads on a solo
        // channel, so the .drm scanned from instruments_presets is the whole
        // story for them (see InstrumentPreset.h).
        styleInstrumentPresetFolder = folder;
        InstrumentPresetIO::scanFolder (folder, &styleInstrumentPresets, nullptr,
                                        /*styleSet*/ true);
        refreshPooledVoices();
    }

    void SamplePlayerEngine::setStyleInstrumentPresetFolders (const std::vector<juce::File>& folders)
    {
        styleInstrumentPresetFolder = folders.empty() ? juce::File() : folders.front();
        InstrumentPresetIO::scanFolders (folders, &styleInstrumentPresets, nullptr,
                                         /*styleSet*/ true);
        refreshPooledVoices();
    }

    juce::File SamplePlayerEngine::getInstrumentFile (int flag) const
    {
        auto it = soundLibrary.find (flag);
        return it == soundLibrary.end() ? juce::File() : it->second;
    }

    juce::String SamplePlayerEngine::getInstrumentName (int flag) const
    {
        auto it = soundLibraryNames.find (flag);
        return it == soundLibraryNames.end() ? juce::String() : it->second;
    }

    std::vector<int> SamplePlayerEngine::getInstrumentFlags() const
    {
        std::vector<int> out;
        out.reserve (soundLibrary.size());
        for (const auto& kv : soundLibrary) out.push_back (kv.first);  // std::map = ascending
        return out;
    }

    int SamplePlayerEngine::getSoundLibraryCount() const
    {
        return (int) soundLibrary.size();
    }
} // namespace Betel



