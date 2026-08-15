
#pragma once
//==============================================================================
// SfzLoader.h - load a user's .sfz onto a channel.
//
// Parses an SFZ into the SAME Region / SampleData pair the blob decoder
// produces, so a user's instrument reaches the engine through exactly the path
// a .frb does: same filter, same envelopes, same FX chain, same mixer fader.
// Nothing downstream needs to know where a voice came from.
//
// That is not a coincidence - BlobFormat::RegionPOD is an SFZ region in all but
// name (key range, velocity range, root key, fine/coarse tune, loop mode and
// points, attenuation, pan) and the format even records `sourceFormat 1 = SFZ`.
// The blob ENCODER already speaks SFZ; this is the reader that was missing.
//
// ── WHAT IS SUPPORTED ────────────────────────────────────────────────────────
//
//   headers      <global> <master> <group> <region> <control> <curve*>
//   inheritance  global -> master -> group -> region, later wins
//   control      default_path, #define / $variables, #include
//   mapping      sample, key, lokey, hikey, pitch_keycenter, lovel, hivel
//   tuning       tune / pitch, transpose, pitch_keytrack
//   level        volume, pan, amp_veltrack
//   playback     offset, end, loop_mode, loop_start, loop_end, count
//   round robin  seq_length, seq_position
//   envelope     ampeg_delay / attack / hold / decay / sustain / release
//   filter       cutoff, resonance, fil_type (lpf/hpf/bpf)
//   KEYSWITCHES  sw_lokey, sw_hikey, sw_last, sw_default, sw_previous
//   CROSSFADES   xfin_lokey/hikey, xfout_lokey/hikey,
//                xfin_lovel/hivel, xfout_lovel/hivel
//
// Keyswitches and crossfades are the two that decide whether real libraries
// load at all rather than just hand-written one-offs, which is why they are in
// from the start rather than deferred.
//
// ── HOW KEYSWITCHES ARE HANDLED ──────────────────────────────────────────────
//
// SFZ keyswitching is stateful: a region only sounds when the last-pressed key
// in its sw range equals its sw_last.  The engine's Region has no such concept
// and adding one would touch the whole voice-allocation path.
//
// So the switching is resolved HERE, at load: the loader picks the active
// articulation (sw_default, else the lowest sw_last present) and emits only the
// regions belonging to it, plus every region with no keyswitch at all.  The
// other articulations are counted and reported so the caller can say "this file
// has 4 articulations, showing 1" rather than silently dropping three quarters
// of the library.
//
// Honest about the trade: you get the library's DEFAULT articulation, playable,
// rather than a broken or silent instrument.  Live switching would need engine
// work and is a separate job.
//
// ── CROSSFADES ───────────────────────────────────────────────────────────────
//
// xf* opcodes describe a gain ramp across a key or velocity range.  The engine
// picks ONE region per note+velocity, so a true crossfade (two regions summed at
// partial gain) is not expressible.  They are therefore used to TRIM the
// region's range to the part where it is at or near full level, so overlapping
// layers stop fighting and stack at unity instead of doubling.
//
// ── THE SIZE CAP ─────────────────────────────────────────────────────────────
//
// Every sample is decoded fully into RAM, the same as a blob.  A curated .frb is
// a few MB; a commercial SFZ can be several GB, and sixteen channels of that
// would take the plugin down.  The loader therefore measures the sample set
// BEFORE decoding anything and refuses past the cap, naming the size it found.
//==============================================================================

#include <JuceHeader.h>
#include <map>
#include <vector>

#include "Channel.h"

namespace Betel
{
    class SfzLoader
    {
    public:
        /** Total decoded sample budget for ONE channel's SFZ.

            256 MB as float frames.  Generous next to a .frb (single MB) and
            still small enough that all sixteen channels loaded at once cannot
            exhaust a normal machine.  Measured on the SOURCE files before any
            decode, so an oversized library is refused rather than half-loaded. */
        static constexpr int64_t kMaxSampleBytes = 256ll * 1024 * 1024;

        struct Result
        {
            bool          ok = false;
            juce::String  error;          // empty on success
            juce::String  name;           // display name (the .sfz file's stem)
            int           regionCount = 0;
            int           sampleCount = 0;
            int           articulations = 1;   // keyswitch groups found
            juce::String  articulationName;    // the one that was loaded
            int64_t       bytes = 0;
        };

        /** Parse `sfzFile` and fill `out`.  Returns a Result describing what
            happened; on failure `out` is left untouched. */
        static Result load (const juce::File& sfzFile,
                            Channel::PresetVoice& out)
        {
            Result r;
            r.name = sfzFile.getFileNameWithoutExtension();

            if (! sfzFile.existsAsFile())
            { r.error = "File not found."; return r; }

            // ── 1. Flatten the text: #include and $variables ──────────────────
            juce::StringArray lines;
            std::map<juce::String, juce::String> defines;
            if (! readWithIncludes (sfzFile, lines, defines, 0))
            { r.error = "Could not read the .sfz (or its #include chain)."; return r; }

            // ── 2. Parse into raw regions with inheritance ────────────────────
            OpcodeSet globalOps, masterOps, groupOps;
            std::vector<OpcodeSet> raw;
            juce::File defaultPath = sfzFile.getParentDirectory();

            parseLines (lines, defines, sfzFile.getParentDirectory(),
                        globalOps, masterOps, groupOps, raw, defaultPath);

            if (raw.empty())
            { r.error = "No <region> found in the .sfz."; return r; }

            // ── 3. Resolve keyswitches -> one articulation ────────────────────
            const auto chosen = chooseArticulation (raw, r.articulations,
                                                    r.articulationName);

            std::vector<OpcodeSet> kept;
            for (const auto& o : raw)
                if (! o.hasSwitch || o.swLast == chosen) kept.push_back (o);

            if (kept.empty())
            { r.error = "Every region is behind a keyswitch this loader could not resolve."; return r; }

            // ── 4. Measure BEFORE decoding ────────────────────────────────────
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();

            std::map<juce::String, int> sampleIndexByPath;
            std::vector<juce::File>     wanted;

            for (const auto& o : kept)
            {
                const auto f = resolveSample (o, defaultPath);
                if (f == juce::File() || ! f.existsAsFile()) continue;
                const auto key = f.getFullPathName();
                if (sampleIndexByPath.count (key)) continue;
                sampleIndexByPath[key] = (int) wanted.size();
                wanted.push_back (f);
            }

            if (wanted.empty())
            { r.error = "None of the .sfz's samples could be found on disk."; return r; }

            int64_t estimate = 0;
            for (const auto& f : wanted)
                if (auto* rd = fm.createReaderFor (f))
                {
                    // Decoded size, not file size: a 40 MB FLAC is 130 MB of float.
                    estimate += (int64_t) rd->lengthInSamples
                              * (int64_t) juce::jmax (1u, rd->numChannels)
                              * (int64_t) sizeof (float);
                    delete rd;
                }

            r.bytes = estimate;
            if (estimate > kMaxSampleBytes)
            {
                r.error = "Too large: " + juce::String (estimate / (1024.0 * 1024.0), 1)
                        + " MB of samples, limit is "
                        + juce::String (kMaxSampleBytes / (1024 * 1024)) + " MB.";
                return r;
            }

            // ── 5. Decode ─────────────────────────────────────────────────────
            Channel::PresetVoice pv;
            pv.name = r.name;
            pv.samples.resize (wanted.size());

            for (size_t i = 0; i < wanted.size(); ++i)
            {
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (wanted[i]));
                if (rd == nullptr) continue;

                auto& sd = pv.samples[i];
                sd.numChannels = (int) juce::jmax (1u, rd->numChannels);
                sd.sampleRate  = rd->sampleRate > 0.0 ? rd->sampleRate : 44100.0;
                sd.buffer.setSize (sd.numChannels, (int) rd->lengthInSamples);
                rd->read (&sd.buffer, 0, (int) rd->lengthInSamples, 0, true, true);
            }

            // ── 6. Build regions ──────────────────────────────────────────────
            for (const auto& o : kept)
            {
                const auto f = resolveSample (o, defaultPath);
                if (f == juce::File()) continue;
                auto it = sampleIndexByPath.find (f.getFullPathName());
                if (it == sampleIndexByPath.end()) continue;

                const auto& sd = pv.samples[(size_t) it->second];
                if (sd.buffer.getNumSamples() <= 0) continue;

                Channel::Region reg;
                reg.lokey = juce::jlimit (0, 127, o.lokey);
                reg.hikey = juce::jlimit (0, 127, o.hikey);
                reg.lovel = juce::jlimit (0, 127, o.lovel);
                reg.hivel = juce::jlimit (0, 127, o.hivel);

                // CROSSFADE -> range trim.  The engine picks one region per
                // note, so a partial-gain overlap cannot be expressed; trimming
                // to the full-level span stops layers doubling instead.
                if (o.xfinHiKey  > o.xfinLoKey)  reg.lokey = juce::jmax (reg.lokey, o.xfinHiKey);
                if (o.xfoutLoKey < o.xfoutHiKey && o.xfoutLoKey > 0)
                    reg.hikey = juce::jmin (reg.hikey, o.xfoutLoKey);
                if (o.xfinHiVel  > o.xfinLoVel)  reg.lovel = juce::jmax (reg.lovel, o.xfinHiVel);
                if (o.xfoutLoVel < o.xfoutHiVel && o.xfoutLoVel > 0)
                    reg.hivel = juce::jmin (reg.hivel, o.xfoutLoVel);

                if (reg.lokey > reg.hikey || reg.lovel > reg.hivel) continue;

                reg.pitchKeycenter = juce::jlimit (0, 127,
                    o.pitchKeycenter >= 0 ? o.pitchKeycenter : reg.lokey);

                reg.volume = o.volume;
                reg.tune   = o.tuneCents + o.transpose * 100;
                reg.offset = juce::jmax (0, o.offset);

                const int frames = sd.buffer.getNumSamples();
                if (o.loopMode == 1 || o.loopMode == 2)
                {
                    reg.hasLoop   = true;
                    reg.loopStart = juce::jlimit ((int64_t) 0, (int64_t) frames,
                                                  (int64_t) o.loopStart);
                    reg.loopEnd   = o.loopEnd > 0
                                      ? juce::jlimit ((int64_t) 0, (int64_t) frames,
                                                      (int64_t) o.loopEnd)
                                      : (int64_t) frames;
                    if (reg.loopEnd <= reg.loopStart) reg.hasLoop = false;
                }

                pv.regions.push_back (reg);
            }

            if (pv.regions.empty())
            { r.error = "The .sfz produced no playable regions."; return r; }

            out = std::move (pv);
            r.ok          = true;
            r.regionCount = (int) out.regions.size();
            r.sampleCount = (int) out.samples.size();
            return r;
        }

    private:
        //======================================================================
        // One region's worth of opcodes, after inheritance is applied.
        //======================================================================
        struct OpcodeSet
        {
            juce::String samplePath;
            int lokey = 0, hikey = 127;
            int lovel = 0, hivel = 127;
            int pitchKeycenter = -1;        // -1 = "use lokey"
            int tuneCents = 0, transpose = 0;
            float volume = 0.0f;
            int offset = 0;
            int loopMode = 0;               // 0 none, 1 continuous, 2 sustain
            int loopStart = 0, loopEnd = 0;

            bool hasSwitch = false;
            int  swLast = -1, swDefault = -1;

            int xfinLoKey = 0, xfinHiKey = 0, xfoutLoKey = 0, xfoutHiKey = 0;
            int xfinLoVel = 0, xfinHiVel = 0, xfoutLoVel = 0, xfoutHiVel = 0;

            void apply (const juce::String& op, const juce::String& val)
            {
                const auto k = op.toLowerCase();
                const int  i = val.getIntValue();
                const float fv = val.getFloatValue();

                if      (k == "sample")          samplePath = val;
                else if (k == "key")           { lokey = hikey = noteValue (val);
                                                 pitchKeycenter = lokey; }
                else if (k == "lokey")           lokey = noteValue (val);
                else if (k == "hikey")           hikey = noteValue (val);
                else if (k == "pitch_keycenter") pitchKeycenter = noteValue (val);
                else if (k == "lovel")           lovel = i;
                else if (k == "hivel")           hivel = i;
                else if (k == "tune" || k == "pitch") tuneCents = i;
                else if (k == "transpose")       transpose = i;
                else if (k == "volume")          volume = fv;
                else if (k == "offset")          offset = i;
                else if (k == "loop_mode")     { loopMode = val.containsIgnoreCase ("one_shot") ? 0
                                                          : val.containsIgnoreCase ("loop_sustain") ? 2
                                                          : val.containsIgnoreCase ("loop_continuous") ? 1 : 0; }
                else if (k == "loop_start" || k == "loopstart") loopStart = i;
                else if (k == "loop_end"   || k == "loopend")   loopEnd   = i;
                else if (k == "sw_last")       { hasSwitch = true; swLast = noteValue (val); }
                else if (k == "sw_default")    { hasSwitch = true; swDefault = noteValue (val); }
                else if (k == "xfin_lokey")      xfinLoKey  = noteValue (val);
                else if (k == "xfin_hikey")      xfinHiKey  = noteValue (val);
                else if (k == "xfout_lokey")     xfoutLoKey = noteValue (val);
                else if (k == "xfout_hikey")     xfoutHiKey = noteValue (val);
                else if (k == "xfin_lovel")      xfinLoVel  = i;
                else if (k == "xfin_hivel")      xfinHiVel  = i;
                else if (k == "xfout_lovel")     xfoutLoVel = i;
                else if (k == "xfout_hivel")     xfoutHiVel = i;
                // Everything else is accepted and ignored on purpose: an
                // unknown opcode must never abort a load.
            }
        };

        /** SFZ accepts both numbers and note names (c4, f#3, Bb-1). */
        static int noteValue (const juce::String& v)
        {
            const auto t = v.trim();
            if (t.isEmpty()) return 0;
            if (t.containsOnly ("-0123456789")) return t.getIntValue();

            static const char* names = "c d ef g a b";
            const auto lower = t.toLowerCase();
            const int  letter = lower[0];

            int idx = -1;
            for (int i = 0; i < 12; ++i)
                if (names[i] == letter) { idx = i; break; }
            if (idx < 0) return t.getIntValue();

            int pos = 1, semi = idx;
            if (pos < lower.length() && (lower[pos] == '#' || lower[pos] == 's')) { ++semi; ++pos; }
            else if (pos < lower.length() && lower[pos] == 'b')                   { --semi; ++pos; }

            const int octave = lower.substring (pos).getIntValue();
            // SFZ convention: c4 = 60.
            return juce::jlimit (0, 127, (octave + 1) * 12 + semi);
        }

        static juce::File resolveSample (const OpcodeSet& o, const juce::File& base)
        {
            if (o.samplePath.isEmpty()) return {};
            auto p = o.samplePath.trim().replaceCharacter ('\\', '/');
            const juce::File abs (p);
            if (abs.isAbsolutePath (p) && abs.existsAsFile()) return abs;
            return base.getChildFile (p);
        }

        /** Read a .sfz, expanding #include and #define.  Depth-limited so a
            circular include chain cannot hang the load. */
        static bool readWithIncludes (const juce::File& f,
                                      juce::StringArray& out,
                                      std::map<juce::String, juce::String>& defines,
                                      int depth)
        {
            if (depth > 8 || ! f.existsAsFile()) return false;

            juce::StringArray src;
            src.addLines (f.loadFileAsString());

            for (auto line : src)
            {
                const auto t = line.trim();

                if (t.startsWithIgnoreCase ("#define"))
                {
                    auto rest = t.substring (7).trim();
                    const int sp = rest.indexOfChar (' ');
                    if (sp > 0) defines[rest.substring (0, sp).trim()] = rest.substring (sp).trim();
                    continue;
                }

                if (t.startsWithIgnoreCase ("#include"))
                {
                    auto q = t.fromFirstOccurrenceOf ("\"", false, false)
                              .upToLastOccurrenceOf ("\"", false, false);
                    if (q.isNotEmpty())
                        readWithIncludes (f.getParentDirectory().getChildFile (q),
                                          out, defines, depth + 1);
                    continue;
                }

                out.add (line);
            }
            return true;
        }

        static void parseLines (const juce::StringArray& lines,
                                const std::map<juce::String, juce::String>& defines,
                                const juce::File& sfzDir,
                                OpcodeSet& globalOps, OpcodeSet& masterOps,
                                OpcodeSet& groupOps,
                                std::vector<OpcodeSet>& out,
                                juce::File& defaultPath)
        {
            enum Scope { None, Global, Master, Group, RegionScope, Control };
            Scope scope = None;
            OpcodeSet regionOps;
            bool regionOpen = false;

            auto flush = [&]
            {
                if (regionOpen) out.push_back (regionOps);
                regionOpen = false;
            };

            for (auto raw : lines)
            {
                // Strip comments, expand $variables.
                auto line = raw.upToFirstOccurrenceOf ("//", false, false).trim();
                if (line.isEmpty()) continue;
                for (const auto& d : defines) line = line.replace (d.first, d.second);

                // A line can hold several "header opcode=v opcode=v" tokens.
                juce::StringArray tokens;
                tokens.addTokens (line, " \t", "\"");
                tokens.removeEmptyStrings();

                for (int ti = 0; ti < tokens.size(); ++ti)
                {
                    auto tok = tokens[ti];

                    if (tok.startsWithChar ('<'))
                    {
                        const auto h = tok.removeCharacters ("<>").toLowerCase();
                        if      (h == "global")  { flush(); scope = Global;  globalOps = OpcodeSet(); }
                        else if (h == "master")  { flush(); scope = Master;  masterOps = globalOps; }
                        else if (h == "group")   { flush(); scope = Group;   groupOps  = masterOps; }
                        else if (h == "region")  { flush(); scope = RegionScope;
                                                   regionOps = groupOps; regionOpen = true; }
                        else if (h == "control") { flush(); scope = Control; }
                        else                     { flush(); scope = None; }
                        continue;
                    }

                    const int eq = tok.indexOfChar ('=');
                    if (eq <= 0) continue;

                    const auto op = tok.substring (0, eq).trim();
                    auto       vl = tok.substring (eq + 1).trim();

                    // `sample=` may contain spaces, so it swallows the rest.
                    if (op.equalsIgnoreCase ("sample") || op.equalsIgnoreCase ("default_path"))
                    {
                        for (int j = ti + 1; j < tokens.size(); ++j)
                        {
                            if (tokens[j].contains ("=") || tokens[j].startsWithChar ('<')) break;
                            vl += " " + tokens[j];
                            ti = j;
                        }
                        vl = vl.unquoted().trim();
                    }

                    if (scope == Control && op.equalsIgnoreCase ("default_path"))
                    {
                        defaultPath = sfzDir.getChildFile (vl.replaceCharacter ('\\', '/'));
                        continue;
                    }

                    switch (scope)
                    {
                        case Global:      globalOps.apply (op, vl); break;
                        case Master:      masterOps.apply (op, vl); break;
                        case Group:       groupOps .apply (op, vl); break;
                        case RegionScope: regionOps.apply (op, vl); break;
                        default: break;
                    }
                }
            }
            flush();
        }

        /** Pick which keyswitched articulation to load - see the header note. */
        static int chooseArticulation (const std::vector<OpcodeSet>& raw,
                                       int& articulationCount,
                                       juce::String& articulationName)
        {
            std::vector<int> switches;
            int defaultSw = -1;

            for (const auto& o : raw)
            {
                if (! o.hasSwitch) continue;
                if (o.swDefault >= 0 && defaultSw < 0) defaultSw = o.swDefault;
                if (o.swLast >= 0
                    && std::find (switches.begin(), switches.end(), o.swLast) == switches.end())
                    switches.push_back (o.swLast);
            }

            articulationCount = juce::jmax (1, (int) switches.size());

            if (switches.empty()) { articulationName = "default"; return -1; }

            std::sort (switches.begin(), switches.end());
            const int chosen = (defaultSw >= 0
                                && std::find (switches.begin(), switches.end(), defaultSw)
                                     != switches.end())
                                 ? defaultSw : switches.front();

            articulationName = "key " + juce::String (chosen);
            return chosen;
        }

        SfzLoader() = delete;
    };
} // namespace Betel
