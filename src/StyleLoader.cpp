#include "StyleLoader.h"
#include "ChordTransposer.h"
#include <cstring>
#include <algorithm>
#include <vector>   // pruneDuplicateSources score table
#include <array>    // pruneIdenticalDrumSources per-channel hit cache
#include <cstdlib>  // std::abs on ints

namespace Betel
{
    // =========================================================================
    //  Lookup tables (referenced from StyleData.h via extern declarations)
    // =========================================================================
    const char* const kNoteNames[12] =
    {
        "C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"
    };

    const char* const kChordTypeNames[34] =
    {
        "Maj","Maj6","Maj7","M7#11","Madd9","M7(9)","M6(9)","aug",
        "m","m6","m7","m7b5","m(9)","m7(9)","m7(11)","mM7","mM7(9)",
        "dim","dim7","7","7sus","7b5","7(9)","7(#11)","7(13)",
        "7(b9)","7(b13)","7(#9)","M7aug","7aug","1+8","1+5","sus4","1+2+5"
    };

    const char* const kNTRNames[3]        = { "RootTrans", "RootFixed", "Guitar" };
    const char* const kNTTNames[11]       =
    {
        "Bypass","Melody","Chord","MelMinor","MelMinor5V",
        "HarMinor","HarMinor5V","NatMinor","NatMinor5V","Dorian","Dorian5V"
    };
    const char* const kNTTGuitarNames[3]  = { "AllPurpose","Stroke","Arpeggio" };
    const char* const kRTRNames[6]        =
    {
        "Stop","PitchShift","PitchShiftToRoot","Retrigger","RetriggerToRoot","NoteGen"
    };

    // =========================================================================
    //  Section name <-> enum mapping
    // =========================================================================
    namespace
    {
        struct SectionNameRow { const char* name; StyleSection id; };

        const SectionNameRow kSectionNames[] =
        {
            { "Main A",      StyleSection::MainA   },
            { "Main B",      StyleSection::MainB   },
            { "Main C",      StyleSection::MainC   },
            { "Main D",      StyleSection::MainD   },
            { "Fill In AA",  StyleSection::FillAA  },
            { "Fill In BB",  StyleSection::FillBB  },
            { "Fill In CC",  StyleSection::FillCC  },
            { "Fill In DD",  StyleSection::FillDD  },
            { "Fill In BA",  StyleSection::FillBA  },   // The "Break"
            { "Intro A",     StyleSection::IntroA  },
            { "Intro B",     StyleSection::IntroB  },
            { "Intro C",     StyleSection::IntroC  },
            { "Ending A",    StyleSection::EndingA },
            { "Ending B",    StyleSection::EndingB },
            { "Ending C",    StyleSection::EndingC },
        };
    }

    const char* getStyleSectionName (StyleSection s) noexcept
    {
        for (const auto& row : kSectionNames)
            if (row.id == s) return row.name;
        return "?";
    }

    StyleSection getStyleSectionFromName (const juce::String& name) noexcept
    {
        for (const auto& row : kSectionNames)
            if (name == row.name) return row.id;
        return StyleSection::Count;
    }

    // =========================================================================
    //  Byte reader — big-endian, with VLQ support
    // =========================================================================
    namespace
    {
        struct ByteReader
        {
            const uint8_t* data;
            size_t         size;
            size_t         pos;

            ByteReader (const uint8_t* d, size_t s, size_t p = 0) : data (d), size (s), pos (p) {}

            bool atEnd()        const noexcept { return pos >= size; }
            size_t remaining()  const noexcept { return size > pos ? size - pos : 0; }

            uint8_t  u8()
            {
                if (pos >= size) return 0;
                return data[pos++];
            }
            uint16_t u16be()
            {
                uint16_t v = (uint16_t) u8() << 8;
                v |= u8();
                return v;
            }
            uint32_t u32be()
            {
                uint32_t v  = (uint32_t) u8() << 24;
                v |= (uint32_t) u8() << 16;
                v |= (uint32_t) u8() << 8;
                v |=             u8();
                return v;
            }
            int vlq()
            {
                int v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    uint8_t b = u8();
                    v = (v << 7) | (b & 0x7F);
                    if ((b & 0x80) == 0) return v;
                }
                return v;
            }
            void skip (size_t n) noexcept { pos = juce::jmin (pos + n, size); }
            bool tagMatches (size_t at, const char* tag4) const noexcept
            {
                if (at + 4 > size) return false;
                return std::memcmp (data + at, tag4, 4) == 0;
            }
        };
    }

    // =========================================================================
    //  Track event walker
    //
    //  Fills outStyle.events with channel events (note on/off, CC, PC, etc.)
    //  Captures voice setup during the SInt measure (bar 1).
    //  Records section markers as (tick, name) pairs in the supplied vector.
    // =========================================================================
    namespace
    {
        struct MarkerHit { int tick; juce::String name; };

        bool parseTrack (ByteReader& r, size_t trackEnd, StyleData& out,
                         std::vector<MarkerHit>& markers,
                         juce::String& err)
        {
            // Setup bar = ONE bar of the style's actual meter (not always 4/4).
            // Defaults to 4/4 here and is corrected the moment the time-signature
            // meta is parsed below (that meta sits at tick 0, before any section
            // note, so the corrected value is in place before the first test).
            int setupBarEndTick = out.ticksPerQuarter * 4;

            int     tick        = 0;
            uint8_t lastStatus  = 0;

            while (r.pos < trackEnd)
            {
                const int dt = r.vlq();
                tick += dt;

                if (r.pos >= trackEnd) break;
                uint8_t b = r.u8();

                // ─── Meta event ────────────────────────────────────────────────
                if (b == 0xFF)
                {
                    if (r.pos >= trackEnd) break;
                    const uint8_t mtype = r.u8();
                    const int     mlen  = r.vlq();
                    if (mtype == 0x58 && mlen >= 4)              // Time signature
                    {
                        out.timeSigNum = r.u8();
                        out.timeSigDen = 1 << r.u8();
                        r.skip ((size_t) mlen - 2);
                        // Correct the setup-bar length now that the meter is
                        // known: one bar = num * (4/den) quarter-notes.
                        // 3/4 -> 3·tpq, 6/8 -> 3·tpq, 4/4 -> 4·tpq.
                        setupBarEndTick = out.ticksPerQuarter * out.timeSigNum * 4
                                              / juce::jmax (1, out.timeSigDen);
                    }
                    else if (mtype == 0x51 && mlen >= 3)         // Tempo (uSec / quarter)
                    {
                        // Tempo meta is 3 big-endian bytes encoding microseconds
                        // per quarter note.  We capture the FIRST occurrence as
                        // the style's "original BPM" — subsequent in-track
                        // tempo changes (rare in Yamaha styles) are ignored;
                        // host tempo always wins at playback time.
                        const uint8_t b0 = r.u8();
                        const uint8_t b1 = r.u8();
                        const uint8_t b2 = r.u8();
                        const uint32_t uspq = ((uint32_t) b0 << 16)
                                            | ((uint32_t) b1 <<  8)
                                            |  (uint32_t) b2;
                        if (uspq > 0 && out.originalBPM == 120.0f)
                            out.originalBPM = (float) (60'000'000.0 / (double) uspq);
                        r.skip ((size_t) mlen - 3);
                    }
                    else if (mtype == 0x06 || mtype == 0x01 || mtype == 0x03) // marker/text/name
                    {
                        juce::String text;
                        int consumed = 0;
                        for (int i = 0; i < mlen; ++i)
                        {
                            uint8_t c = r.u8();
                            ++consumed;
                            if (c == 0) break;
                            text << (juce::juce_wchar) c;
                        }
                        // Drain any remaining bytes inside this meta event
                        for (int i = consumed; i < mlen; ++i) r.u8();

                        if (mtype == 0x03 && out.name.isEmpty())
                            out.name = text;
                        else if (mtype == 0x06)
                        {
                            if (text == "SFF1") { out.isSFF2 = false; }
                            else if (text == "SFF2") { out.isSFF2 = true; }
                            else if (text == "SInt") { /* setup-measure marker */ }
                            else
                                markers.push_back ({ tick, text });
                        }
                        // mtype 0x01 fn:Foo lines are ignored
                    }
                    else if (mtype == 0x2F)                      // End of Track
                    {
                        r.skip ((size_t) mlen);
                        break;
                    }
                    else
                    {
                        r.skip ((size_t) mlen);
                    }
                    continue;
                }

                // ─── SysEx ─────────────────────────────────────────────────────
                if (b == 0xF0 || b == 0xF7)
                {
                    const int slen = r.vlq();
                    r.skip ((size_t) slen);
                    continue;
                }

                // ─── Channel event (with running status) ──────────────────────
                uint8_t status, data1;
                if (b & 0x80)
                {
                    status     = b;
                    lastStatus = b;
                    data1      = r.u8();
                }
                else
                {
                    status = lastStatus;
                    data1  = b;
                }
                const uint8_t cmd = status & 0xF0;
                const uint8_t ch  = status & 0x0F;
                uint8_t data2 = 0;
                if (cmd == 0x80 || cmd == 0x90 || cmd == 0xA0 || cmd == 0xB0 || cmd == 0xE0)
                    data2 = r.u8();

                // Capture voice setup from the SInt measure
                if (tick < setupBarEndTick)
                {
                    auto& vs = out.voices[ch];
                    if (cmd == 0xB0)
                    {
                        switch (data1)
                        {
                            case 0:   vs.bankMsb = data2; break;
                            case 32:  vs.bankLsb = data2; break;
                            case 7:   vs.volume  = data2; break;
                            case 10:  vs.pan     = data2; break;
                            // CC 11 Expression — the style's second gain input.
                            // Was dropped here, so the setup had nothing to
                            // apply and StylePlayer forced unity; a part the
                            // composer parked below its fader started too loud.
                            case 11:  vs.expression = data2; break;
                            case 91:  vs.reverb  = data2; break;
                            case 93:  vs.chorus  = data2; break;
                            default: break;
                        }
                    }
                    else if (cmd == 0xC0)
                    {
                        vs.program = data1;
                    }
                }

                // Store musical events (skip the entire setup measure — controllers
                // and PCs there are setup, not music). Skip pure-control B0 events
                // outside setup too: they're parameter changes, not notes.
                if (tick >= setupBarEndTick
                    && (cmd == 0x80 || cmd == 0x90 || cmd == 0xA0
                        || cmd == 0xB0 || cmd == 0xC0 || cmd == 0xD0 || cmd == 0xE0))
                {
                    StyleEvent e;
                    e.tick    = tick;
                    e.status  = cmd;
                    e.channel = ch;
                    e.data1   = data1;
                    e.data2   = data2;
                    out.events.push_back (e);
                }
            }

            juce::ignoreUnused (err);
            return true;
        }

        // Compute each section's startTick from its marker and endTick from
        // the marker that follows it in TICK order (not enum order).
        void finaliseSectionRanges (StyleData& out,
                                    std::vector<MarkerHit>& markers,
                                    int trackEndTick)
        {
            std::sort (markers.begin(), markers.end(),
                       [](const MarkerHit& a, const MarkerHit& b){ return a.tick < b.tick; });

            for (size_t i = 0; i < markers.size(); ++i)
            {
                const auto& m = markers[i];
                const StyleSection s = getStyleSectionFromName (m.name);
                if (s == StyleSection::Count) continue;

                auto& sec = out.getSection (s);
                sec.id       = s;
                sec.present  = true;
                sec.startTick = m.tick;
                sec.endTick   = (i + 1 < markers.size()) ? markers[i + 1].tick : trackEndTick;
            }
        }

        // Bin each event's index into the section that contains its tick.
        void assignEventsToSections (StyleData& out)
        {
            for (size_t i = 0; i < out.events.size(); ++i)
            {
                const int t = out.events[i].tick;
                for (auto& sec : out.sections)
                {
                    if (sec.present && t >= sec.startTick && t < sec.endTick)
                    {
                        sec.eventIdx.push_back (i);
                        break;
                    }
                }
            }
        }

        // ─────────────────────────────────────────────────────────────────
        //  Load-time seam guard.
        //  Truncate any MELODIC note still sounding at its section's loop
        //  boundary so it releases exactly at the seam instead of ringing
        //  across it.  Two payoffs:
        //    • no hung notes when the section loops — a note-off that lands
        //      past endTick is never replayed inside the loop, so the voice
        //      would otherwise stick;
        //    • a sustained note can't hold the previous chord into the next
        //      phrase (pairs with the no-re-pitch chord-change behaviour).
        //  Only notes that ACTUALLY cross the seam are touched; within-phrase
        //  notes are left untouched.  Drums (source channel 8/9) are one-shots
        //  whose note-offs are irrelevant, so they are skipped.
        //  Re-bins events at the end, so this must run LAST in the load.
        // ─────────────────────────────────────────────────────────────────
        void clampSeamCrossingNotes (StyleData& out)
        {
            const size_t origCount = out.events.size();
            bool dirty = false;

            for (auto& sec : out.sections)
            {
                if (! sec.present) continue;
                const int seam      = sec.endTick;
                const int clampTick = juce::jmax (sec.startTick, seam - 1);

                for (size_t k = 0; k < sec.eventIdx.size(); ++k)
                {
                    const size_t onIdx = (size_t) sec.eventIdx[k];
                    const StyleEvent on = out.events[onIdx];   // copy: vector may grow
                    const int ch = on.channel & 0x0F;
                    if (ch == 8 || ch == 9)                       continue; // drums
                    if (! (on.status == 0x90 && on.data2 > 0))    continue; // not note-on
                    const int note = on.data1 & 0x7F;

                    // Find this note-on's release: the next 0x80 / 0x90-vel0 for
                    // the same ch+note, stopping if the key re-triggers first
                    // (overlapping source note — leave that pairing alone).
                    int offIdx = -1;
                    for (size_t j = onIdx + 1; j < origCount; ++j)
                    {
                        const auto& e = out.events[j];
                        if ((e.channel & 0x0F) != ch || (e.data1 & 0x7F) != note) continue;
                        const bool isOff = (e.status == 0x80) || (e.status == 0x90 && e.data2 == 0);
                        const bool isOn  = (e.status == 0x90 && e.data2 > 0);
                        if (isOff) { offIdx = (int) j; break; }
                        if (isOn)  break;
                    }

                    if (offIdx >= 0)
                    {
                        // Pull a seam-crossing release back to the boundary.
                        if (out.events[(size_t) offIdx].tick >= seam)
                        {
                            out.events[(size_t) offIdx].tick = clampTick;
                            dirty = true;
                        }
                    }
                    else
                    {
                        // Dangling note-on (no release found at all): add one
                        // at the seam so the voice cannot hang forever.
                        StyleEvent off;
                        off.tick    = clampTick;
                        off.status  = 0x80;
                        off.channel = on.channel;
                        off.data1   = on.data1;
                        off.data2   = 0;
                        out.events.push_back (off);
                        dirty = true;
                    }
                }
            }

            if (dirty)
            {
                std::stable_sort (out.events.begin(), out.events.end(),
                                  [] (const StyleEvent& a, const StyleEvent& b)
                                  { return a.tick < b.tick; });
                for (auto& sec : out.sections) sec.eventIdx.clear();
                assignEventsToSections (out);
            }
        }
    } // anonymous namespace

    // =========================================================================
    //  CASM / CSEG / Sdec / Ctab / Ctb2 parsing
    // =========================================================================
    namespace
    {
        void parseZoneBlock (ByteReader& r, CasmZone& z)
        {
            z.ntr      = r.u8();
            z.ntt      = r.u8();
            z.highKey  = r.u8();
            z.noteLow  = r.u8();
            z.noteHigh = r.u8();
            z.rtr      = r.u8();
        }

        // Reads either a "Ctab" (SFF1) or "Ctb2" (SFF2) record. The 4-byte tag
        // and 4-byte length have already been consumed by the caller.
        // bodyLen = number of content bytes for this record.
        CasmEntry parseCtabBody (const uint8_t* body, size_t bodyLen, bool isCtb2)
        {
            CasmEntry e;
            ByteReader r (body, bodyLen);

            e.srcChannel = r.u8();
            for (int i = 0; i < 8 && i < (int) bodyLen; ++i)
                e.voiceName[i] = (char) r.u8();
            e.voiceName[8] = 0;
            e.dstChannel  = r.u8();
            e.isEditable  = (r.u8() == 0);                      // 0 = editable, 1 = read-only

            e.noteMuteRaw[0]  = r.u8();
            e.noteMuteRaw[1]  = r.u8();
            for (int i = 0; i < 5; ++i) e.chordMuteRaw[i] = r.u8();

            e.srcChordRoot = r.u8();
            e.srcChordType = r.u8();

            // Snapshot for Betelgeuse multi-source dispatch — survives any
            // subsequent normalisation / Maj7-forcing that the HonorCasm
            // path applies.
            e.origSrcChordRoot = e.srcChordRoot;
            e.origSrcChordType = e.srcChordType;

            if (isCtb2)
            {
                e.lowMidLimit   = r.u8();
                e.midHighLimit  = r.u8();
                parseZoneBlock (r, e.zones[0]);
                parseZoneBlock (r, e.zones[1]);
                parseZoneBlock (r, e.zones[2]);
            }
            else
            {
                // SFF1 has a single NTR/NTT block — replicate to all three zones
                CasmZone single;
                parseZoneBlock (r, single);
                e.zones[0] = e.zones[1] = e.zones[2] = single;
                e.lowMidLimit  = 0;
                e.midHighLimit = 127;
            }

            // Trailing flag bytes (6 or 7 across formats; we read defensively).
            // Layout per spec:
            //   +0: extra-break-voice flag (0x80 = set)
            //   +1: drum-channel flag      (0x01 = drum channel)
            //   +2: always 0
            //   +3: drum sub-flag          (0x18 = set)
            //   +4: drum percussion key    (0x23..0x50 when drum, else 0x7F/0x80)
            //   +5: drum volume
            if (r.remaining() >= 6)
            {
                /* uint8_t breakVoice = */ r.u8();
                e.isDrumChannel = (r.u8() == 0x01);
                r.u8();                       // reserved
                /* uint8_t drumSub  = */ r.u8();
                e.drumPercKey   = r.u8();
                e.drumVolume    = r.u8();
            }

            return e;
        }

        // Splits "Main A,Main B,Fill In AA" -> ["Main A", "Main B", "Fill In AA"].
        std::vector<juce::String> splitSectionNames (const uint8_t* p, size_t len)
        {
            std::vector<juce::String> out;
            juce::String cur;
            for (size_t i = 0; i < len; ++i)
            {
                const char c = (char) p[i];
                if (c == 0) break;
                if (c == ',')
                {
                    if (cur.isNotEmpty()) { out.push_back (cur.trim()); cur.clear(); }
                }
                else
                {
                    cur << c;
                }
            }
            if (cur.isNotEmpty()) out.push_back (cur.trim());
            return out;
        }

        // Forward declarations — defined just below; parseCasm calls
        // pruneDuplicateSources before its definition appears in this TU.
        static int sourcePreference     (const CasmEntry& e) noexcept;
        void       pruneDuplicateSources (StyleData& out);
        // Force every instrumental (non-drum) CASM entry to declare CMaj7 as
        // its source chord so the dispatcher transposes against ONE unified
        // calibration reference across the whole style.  Drums (NTR_FIXED +
        // NTT_BYPASS) are left alone because they don't transpose.
        void       normaliseSourceChordToCMaj7 (StyleData& out);
        // Mark the destinations this style plays as CHORDS rather than as
        // lines, so a mono slot can go poly for the duration.  Defined below
        // and, like the others here, called from parseCasm before that point.
        void       computeChordalDestinations (StyleData& out);
        // Mark each CASM entry as winner / loser per Betelgeuse chord-quality
        // bucket so the dispatcher can route events to the right source
        // variant at runtime.  See definition for the full scoring model.
        void       computeBetelgeuseMultiSrcWinners (StyleData& out);

        // Mark each CASM entry as playing / silent per FINGERED chord quality,
        // honouring the file's chord-mute bitmap so multi-record styles layer
        // the right voicings per chord.  MUST run after the bucket-winner pass
        // above — its no-match fallback reuses those winners.
        void       computeBetelgeuseFingeredPlayers (StyleData& out);

        // Silence a DRUM source that merely duplicates another source already
        // pointing at the same destination.  MUST run last — it overrides both
        // passes above, because a duplicate has to stay silent in every chord
        // mode.  See definition.
        void       pruneIdenticalDrumSources (StyleData& out);

        void parseCasm (const uint8_t* raw, size_t size, size_t casmStart, StyleData& out)
        {
            if (casmStart + 8 > size)                                 return;
            if (std::memcmp (raw + casmStart, "CASM", 4) != 0)        return;

            const uint32_t casmLen = (uint32_t) (
                ((uint32_t) raw[casmStart + 4] << 24) |
                ((uint32_t) raw[casmStart + 5] << 16) |
                ((uint32_t) raw[casmStart + 6] << 8)  |
                 (uint32_t) raw[casmStart + 7]);

            out.hasCasm = true;

            size_t pos     = casmStart + 8;
            const size_t end = juce::jmin (casmStart + 8 + (size_t) casmLen, size);

            while (pos + 8 <= end)
            {
                if (std::memcmp (raw + pos, "CSEG", 4) != 0) break;
                const uint32_t csegLen = (uint32_t) (
                    ((uint32_t) raw[pos + 4] << 24) |
                    ((uint32_t) raw[pos + 5] << 16) |
                    ((uint32_t) raw[pos + 6] << 8)  |
                     (uint32_t) raw[pos + 7]);

                const size_t csegEnd = juce::jmin (pos + 8 + (size_t) csegLen, end);
                size_t cur = pos + 8;

                // ─── Sdec ──────────────────────────────────────────────────────
                std::vector<StyleSection> targetSections;
                if (cur + 8 <= csegEnd && std::memcmp (raw + cur, "Sdec", 4) == 0)
                {
                    const uint32_t sdLen = (uint32_t) (
                        ((uint32_t) raw[cur + 4] << 24) |
                        ((uint32_t) raw[cur + 5] << 16) |
                        ((uint32_t) raw[cur + 6] << 8)  |
                         (uint32_t) raw[cur + 7]);
                    auto names = splitSectionNames (raw + cur + 8, sdLen);
                    for (auto& n : names)
                    {
                        const auto s = getStyleSectionFromName (n);
                        if (s != StyleSection::Count)
                            targetSections.push_back (s);
                    }
                    cur += 8 + (size_t) sdLen;
                }

                // ─── Ctab / Ctb2 / Cntt entries until end of CSEG ──────────────
                while (cur + 8 <= csegEnd)
                {
                    const bool isCtb2 = (std::memcmp (raw + cur, "Ctb2", 4) == 0);
                    const bool isCtab = (std::memcmp (raw + cur, "Ctab", 4) == 0);
                    const bool isCntt = (std::memcmp (raw + cur, "Cntt", 4) == 0);

                    if (! (isCtab || isCtb2 || isCntt)) break;

                    const uint32_t recLen = (uint32_t) (
                        ((uint32_t) raw[cur + 4] << 24) |
                        ((uint32_t) raw[cur + 5] << 16) |
                        ((uint32_t) raw[cur + 6] << 8)  |
                         (uint32_t) raw[cur + 7]);
                    const size_t bodyStart = cur + 8;
                    const size_t bodyEnd   = juce::jmin (bodyStart + (size_t) recLen, csegEnd);

                    if (isCtab || isCtb2)
                    {
                        auto entry = parseCtabBody (raw + bodyStart,
                                                    bodyEnd - bodyStart,
                                                    isCtb2);
                        for (auto s : targetSections)
                            out.getSection (s).casm.push_back (entry);
                    }
                    // Cntt is a rarely-used per-section NTT override — ignored
                    // for now (no styles in our test corpus use it).

                    cur = bodyEnd;
                }

                pos = csegEnd;
            }

            // Collapse multi-source destinations down to one major-family
            // source each (the NTT generates every chord from it).
            pruneDuplicateSources (out);

            // Then force every surviving instrumental entry to claim CMaj7 as
            // its source chord, so every non-drum channel transposes against
            // the same calibration reference.  Matches Yamaha SFF convention
            // (the spec's "all source data is written as CMaj7"); the prior
            // step already biased survivors toward Maj7-recorded sources, so
            // in practice this only normalises the edge cases.
            normaliseSourceChordToCMaj7 (out);

            // Which destinations are played as CHORDS rather than as lines.
            // AFTER the prune, so only sources that will actually reach the
            // engine are measured.
            computeChordalDestinations (out);

            // Pre-compute the per-bucket winner per dst for Betelgeuse
            // multi-source dispatch.  Reads origSrcChordType (the file's
            // original chord-quality tag, captured before normalisation) so
            // we route by what the AUTHOR recorded, not the Maj7 override.
            computeBetelgeuseMultiSrcWinners (out);

            // FINGERED-mode per-quality selection (chord-mute bitmap driven).
            // Runs after the winner pass so its fallback can reuse the winners.
            computeBetelgeuseFingeredPlayers (out);

            // LAST: drop drum sources that are byte-for-byte duplicates of a
            // sibling on the same destination.  Neither pass above can catch
            // these — both deliberately exempt drum entries — so the duplicate
            // survived into dispatch and every hit fired twice.
            pruneIdenticalDrumSources (out);
        }

        // Preference score for keeping a source as the single NTT reference for
        // its destination.  The NTT assumes the recorded pattern is a major /
        // Maj7 reference, so a major-family source must always win over a
        // minor-/dominant-recorded duplicate.  Maj7 is the ideal reference.
        static int sourcePreference (const CasmEntry& e) noexcept
        {
            switch (e.srcChordType)
            {
                case 2:  return 100;   // Maj7  — ideal NTT reference
                case 0:  return 95;    // Maj
                case 1:  return 90;    // Maj6
                case 3: case 4: case 5: case 6: return 85;   // other major-family
                case 7:  return 60;    // aug   — major-ish, last-resort major
                default: return 10;    // min / dom7 / dim / sus … avoid if possible
            }
        }

        /** Do two sources ever fire on the SAME chord quality?
            The chord-mute bitmap says which qualities a source is recorded for.
            Sources whose bitmaps are disjoint are VARIANTS of one another —
            each is the correct recording for its own half of the chord space —
            and pruning either one silences that half.  Only sources that
            genuinely contend for the same quality are duplicates. */
        static bool chordMasksOverlap (const CasmEntry& a, const CasmEntry& b) noexcept
        {
            for (size_t i = 0; i < a.chordMuteRaw.size(); ++i)
                if ((a.chordMuteRaw[i] & b.chordMuteRaw[i]) != 0) return true;
            return false;
        }

        //======================================================================
        // Keep ONE source per destination — but only among sources that
        // actually contend.
        //
        // THE BUG THIS FIXES.  This pass used to keep the single
        // highest-preference source per destination and drop every other,
        // assuming extra sources were redundant recordings.  They usually are
        // not: multiple sources on one destination is the SFF CHORD-QUALITY
        // VARIANT mechanism, and arbitrating between them per chord is exactly
        // what the winner gate (firesForFingeredQuality) exists to do at
        // dispatch.  Pruning ran first and threw the variant away before the
        // gate ever saw it.
        //
        // Measured on 6_8_Brush_Bld_S838: CHORD2 is fed by two sources whose
        // chord masks are DISJOINT and together cover all 34 qualities —
        //
        //     src 0  srcChordType 2  (Maj7)     23 qualities  -> preference 100
        //     src 1  srcChordType 15 (minMaj7)  11 qualities  -> preference  10
        //
        // — so src 1, the style's own MINOR recording, was discarded.  On a
        // minor chord src 0 fired instead and the NTT had to bend a
        // Maj7-referenced voicing across a third it was never recorded for.
        // That is the "strange and pitched" CHORD2, and it was CHORD2 alone
        // because it was the only destination in that style with two sources.
        //
        // The rule now: a source is pruned only if it OVERLAPS a
        // higher-preference source on at least one chord quality.  Disjoint
        // masks are variants and all of them stay active.  sourcePreference
        // still decides among genuine duplicates, which is what it was written
        // for.
        //======================================================================
        void pruneDuplicateSources (StyleData& out)
        {
            for (auto& sec : out.sections)
            {
                if (sec.casm.size() < 2) continue;

                // Score once — used both for ordering and for reporting.
                std::vector<int> score (sec.casm.size());
                for (size_t i = 0; i < sec.casm.size(); ++i)
                    score[i] = sourcePreference (sec.casm[i]) * 2
                             + (sec.casm[i].srcChannel == sec.casm[i].dstChannel ? 1 : 0);

                for (size_t i = 0; i < sec.casm.size(); ++i)
                {
                    const int dst = sec.casm[i].dstChannel & 0x0F;

                    // Never prune the drum / percussion destinations (SFF dst 8
                    // = RhythmSub/perc, dst 9 = RhythmMain).  Those are
                    // key-mapped kits, not chord variants — a style routinely
                    // points several entries at them, and dropping any would
                    // silence the drums.
                    if (dst == 8 || dst == 9 || sec.casm[i].isDrumChannel)
                    {
                        sec.casm[i].active = true;
                        continue;
                    }

                    // Active unless some OTHER source on this destination both
                    // outranks it AND contends for a quality it also covers.
                    bool keep = true;
                    for (size_t j = 0; j < sec.casm.size() && keep; ++j)
                    {
                        if (j == i) continue;
                        if ((sec.casm[j].dstChannel & 0x0F) != dst) continue;
                        if (sec.casm[j].isDrumChannel) continue;
                        if (! chordMasksOverlap (sec.casm[i], sec.casm[j])) continue;

                        // Strictly-greater score, with index as the tie-break, so
                        // exactly one of an equal-scoring overlapping pair
                        // survives and the choice is deterministic.
                        if (score[j] > score[i] || (score[j] == score[i] && j < i))
                            keep = false;
                    }
                    sec.casm[i].active = keep;
                }
            }
        }

        //======================================================================
        // WHICH DESTINATIONS ARE PLAYED AS CHORDS
        //
        // Grex's BASS slot is Mono with monoRetrigNew off and a 100 ms
        // portamento, which is exactly right for a bass LINE: one voice, last
        // note wins, legato between notes.  It is exactly wrong for a part that
        // strikes two notes at once - the second steals the voice and glides to
        // it instead of sounding beside it, so an octave in the left hand
        // becomes a 100 ms sweep across twelve semitones, twice a bar.
        //
        // THE TEST IS SIMULTANEITY, NOT OVERLAP.  Legato bass lines overlap
        // constantly - that is what the glide is for - so counting overlaps
        // would flag every well-played bass part in the library.  Notes struck
        // on the SAME TICK cannot be legato; they are a chord.  Measured on the
        // two styles in hand the separation is not close:
        //
        //     4_Stroke   bass (piano left hand)   75-100% of note-ons in groups
        //     Danzon     bass (a real bass line)  0%
        //
        // so the half-of-all-note-ons threshold below has enormous margin and
        // needs no tuning.  kMinGrouped stops a two-note fragment in a short
        // fill from speaking for the whole style.
        //
        // Only ACTIVE entries are measured: a source pruneDuplicateSources has
        // already dropped never reaches the engine, so its polyphony is not the
        // engine's problem.  Drums are skipped - a kit is key-mapped, always
        // polyphonic, and never on a mono slot.
        //======================================================================
        void computeChordalDestinations (StyleData& out)
        {
            constexpr int   kMinGrouped   = 4;      // ignore tiny fragments
            constexpr float kGroupedShare = 0.5f;   // "most of this part is chords"

            for (const auto& sec : out.sections)
            {
                if (! sec.present) continue;

                for (const auto& e : sec.casm)
                {
                    if (! e.active)      continue;
                    if (e.isDrumChannel) continue;

                    const int dst = e.dstChannel & 0x0F;
                    if (dst == 8 || dst == 9)      continue;   // rhythm dests
                    if (out.destChordal[(size_t) dst]) continue;   // already known

                    // Collect this source's note-on ticks within the section.
                    std::vector<int> ticks;
                    for (auto idx : sec.eventIdx)
                    {
                        const auto& ev = out.events[idx];
                        if (ev.channel != e.srcChannel)      continue;
                        if ((ev.status & 0xF0) != 0x90)      continue;
                        if (ev.data2 == 0)                   continue;   // note-off
                        ticks.push_back (ev.tick);
                    }
                    if ((int) ticks.size() < kMinGrouped) continue;

                    // How many of them share their tick with another?
                    std::sort (ticks.begin(), ticks.end());
                    int grouped = 0;
                    for (size_t i = 0; i < ticks.size();)
                    {
                        size_t j = i;
                        while (j < ticks.size() && ticks[j] == ticks[i]) ++j;
                        const int run = (int) (j - i);
                        if (run > 1) grouped += run;
                        i = j;
                    }

                    if (grouped >= kMinGrouped
                        && (float) grouped >= kGroupedShare * (float) ticks.size())
                        out.destChordal[(size_t) dst] = true;
                }
            }
        }

        // Force every instrumental (non-drum) CASM entry's source chord to
        // C Maj7 — the canonical Yamaha SFF calibration reference.
        //
        // Why this exists:
        //   The NoteTransposer math uses srcChordRoot for the PC-relative
        //   transposition (srcRel = srcPC - srcRoot) and assumes the source
        //   data is in major-mode context.  Yamaha authoring convention is
        //   that ALL style data is written against CMaj7 (root=C, type=Maj7)
        //   so every channel transposes from the same reference.
        //
        //   In practice, well-authored Yamaha styles already declare CMaj7
        //   in their Ctb2 chunks; this pass simply normalises the edge cases
        //   (older SFF1 styles, third-party edits, broken Ctb2 declarations)
        //   so EVERY instrumental channel in a loaded style behaves the same
        //   way.  Drums are intentionally left alone — they use NTR_FIXED +
        //   NTT_BYPASS, never run the transposition path, and their srcChord
        //   field is moot.
        void normaliseSourceChordToCMaj7 (StyleData& out)
        {
            for (auto& sec : out.sections)
            {
                for (auto& e : sec.casm)
                {
                    // Skip drum destinations: SFF dst 8 = RhythmSub/perc,
                    // dst 9 = RhythmMain, plus any entry explicitly flagged
                    // as a drum channel.  Same gate as pruneDuplicateSources
                    // uses, kept identical so the two passes never disagree
                    // about what counts as a drum.
                    const int dst = e.dstChannel & 0x0F;
                    if (dst == 8 || dst == 9 || e.isDrumChannel) continue;

                    // 0 = C, 2 = Maj7 in the ChordQuality enum.
                    e.srcChordRoot = 0;
                    e.srcChordType = 2;
                }
            }
        }

        // Compute the per-CASM-entry `isWinnerForBucket` flags used by the
        // Betelgeuse multi-source dispatch.  For each section, for each of
        // the 8 destination chord-quality buckets, find the highest-affinity
        // CASM entry (per dstChannel) among the section's siblings and mark
        // it as the winner for that bucket.  All others get false.  Drums
        // (dst 8 / 9 / isDrumChannel) are always winners — they don't compete
        // with melodic entries and there's typically one per dst anyway.
        //
        // Affinity scoring is delegated to NoteTransposer::srcFamilyAffinity
        // so the substitution rules (which reuse the same scoring) and the
        // dispatch winner stay in lock-step.  Stable tiebreaker: lower
        // srcChannel wins (matches the file's authored order).
        //
        // Result: at dispatch, when the held chord lands in bucket K, the
        // events from exactly one CASM entry per dst fire — the one the
        // file author authored for that chord context (or the best
        // available math-fallback when no exact match exists).
        //======================================================================
        // DUPLICATE-DRUM-SOURCE PRUNE
        //
        // Some styles point TWO source channels at the same drum destination
        // carrying the same part.  Bigger_Band_S162 is the case that exposed
        // it: the CASM record shared by Intro C and Ending C routes src ch9 AND
        // src ch10 both to dst ch10, and both channels hold the same drum
        // pattern.  Every kick, snare and cymbal therefore fired twice on one
        // channel — +6 dB on the drums for those two sections, and double the
        // drum voices.  That was the "crazy gain" on Ending 3.
        //
        // Neither existing pass catches it, and both are right not to try:
        // pruneDuplicateSources and computeBetelgeuseMultiSrcWinners BOTH
        // exempt drum destinations, because drums are key-mapped kits rather
        // than chord variants — a style legitimately points several entries at
        // dst 8 / 9 and dropping one on chord grounds would silence the part.
        // So the duplicate sailed straight through into dispatch.
        //
        // The safe discriminator is CONTENT, not chord tags: compare what the
        // two sources actually play inside this section and drop one only when
        // the parts are the same part.
        //
        // WHY NOT EXACT EQUALITY.  It was the obvious test and it does not
        // work.  These duplicates are double-TRACKED, not copy-pasted: in
        // Ending C the two channels differ by a single pedal-hat nudged nine
        // ticks (under a twentieth of a beat), and Intro C differs in a handful
        // of velocities and one extra hit out of two hundred.  On a real
        // Yamaha the two rhythm channels drive different kits and that jitter
        // thickens the sound; here both land on engine 0 with ONE kit, so it is
        // just a doubled hit with a flam.  An equality test silently does
        // nothing on exactly the files that need it.
        //
        // So: greedy note-matching within a 1/32-note window (TPQ-relative, so
        // it holds at any resolution), scored as 2*matches/(|A|+|B|).  Measured
        // on the file above the two drum pairs score 0.98 and 1.00 while every
        // melodic pair in the same style scores 0.37-0.90 — the separation is
        // wide, and melodic destinations are excluded here anyway.
        //
        // Silencing goes through isWinnerForBucket / firesForFingeredQuality
        // rather than the `active` flag: those two arrays are what dispatchOne
        // actually tests, and clearing BOTH keeps the duplicate silent in
        // SINGLE-FINGER and FINGERED alike.  `active` is cleared too so the
        // entry reads as rejected anywhere else that inspects it.
        //======================================================================
        static constexpr float kDrumDuplicateSimilarity = 0.90f;

        void pruneIdenticalDrumSources (StyleData& out)
        {
            const int tolTicks = std::max (1, out.ticksPerQuarter / 8);   // 1/32 note

            // Note-on (tick, key) pairs for one source channel inside one
            // section, section-relative so each section compares independently.
            auto hitsFor = [&out] (const StyleSectionData& sec, int srcCh)
            {
                std::vector<std::pair<int, int>> h;
                for (const auto idx : sec.eventIdx)
                {
                    if (idx >= out.events.size()) continue;
                    const auto& ev = out.events[idx];
                    if ((int) (ev.channel & 0x0F) != srcCh) continue;
                    if ((ev.status & 0xF0) != 0x90)         continue;
                    if (ev.data2 == 0)                      continue;   // note-off in disguise
                    h.emplace_back (ev.tick - sec.startTick, (int) ev.data1);
                }
                std::sort (h.begin(), h.end());
                return h;
            };

            auto similarity = [tolTicks] (const std::vector<std::pair<int, int>>& A,
                                          const std::vector<std::pair<int, int>>& B)
            {
                if (A.empty() || B.empty()) return 0.0f;
                std::vector<char> used (B.size(), 0);
                int matched = 0;
                for (const auto& a : A)
                    for (size_t k = 0; k < B.size(); ++k)
                    {
                        if (used[k] || B[k].second != a.second) continue;
                        if (std::abs (B[k].first - a.first) > tolTicks) continue;
                        used[k] = 1; ++matched; break;
                    }
                return 2.0f * (float) matched / (float) (A.size() + B.size());
            };

            for (auto& sec : out.sections)
            {
                if (! sec.present || sec.casm.size() < 2) continue;

                // One scan per source channel — a section can hold a couple of
                // thousand events and this would otherwise rescan per pair.
                std::array<std::vector<std::pair<int, int>>, 16> cache;
                std::array<bool, 16> done { };
                auto hitsOf = [&] (int srcCh) -> const std::vector<std::pair<int, int>>&
                {
                    if (! done[(size_t) srcCh])
                    {
                        cache[(size_t) srcCh] = hitsFor (sec, srcCh);
                        done [(size_t) srcCh] = true;
                    }
                    return cache[(size_t) srcCh];
                };

                auto isDrumDest = [] (const CasmEntry& e)
                {
                    const int d = e.dstChannel & 0x0F;
                    return d == 8 || d == 9 || e.isDrumChannel;
                };

                for (size_t i = 0; i < sec.casm.size(); ++i)
                {
                    const auto& keeper = sec.casm[i];
                    if (! isDrumDest (keeper)) continue;          // drums only
                    if (! keeper.active)       continue;          // already dropped

                    const auto& hitsI = hitsOf ((int) (keeper.srcChannel & 0x0F));
                    if (hitsI.empty()) continue;

                    for (size_t j = i + 1; j < sec.casm.size(); ++j)
                    {
                        auto& dup = sec.casm[j];
                        if ((dup.dstChannel & 0x0F) != (keeper.dstChannel & 0x0F)) continue;
                        if (! isDrumDest (dup)) continue;
                        if (! dup.active)       continue;

                        const auto& hitsJ = hitsOf ((int) (dup.srcChannel & 0x0F));
                        if (similarity (hitsI, hitsJ) < kDrumDuplicateSimilarity)
                            continue;                             // a real second part

                        // Same destination, same part.  The lower index
                        // survives, so the choice is deterministic across loads.
                        dup.active = false;
                        dup.isWinnerForBucket.fill (false);
                        dup.firesForFingeredQuality.fill (false);
                    }
                }
            }
        }

        void computeBetelgeuseMultiSrcWinners (StyleData& out)
        {
            for (auto& sec : out.sections)
            {
                if (sec.casm.empty()) continue;

                for (int bucket = 0; bucket < 8; ++bucket)
                {
                    int bestScore[16];
                    int bestIdx  [16];
                    for (int d = 0; d < 16; ++d) { bestScore[d] = INT_MIN; bestIdx[d] = -1; }

                    for (size_t i = 0; i < sec.casm.size(); ++i)
                    {
                        const auto& e = sec.casm[i];
                        const int dst = e.dstChannel & 0x0F;

                        // Drums never compete — skip melodic scoring entirely.
                        // The marking pass below flags every drum entry as a
                        // winner, so they must NOT occupy bestIdx[dst] (which
                        // would otherwise starve a melodic part sharing the dst).
                        if (dst == 8 || dst == 9 || e.isDrumChannel)
                            continue;

                        // Score this variant against the bucket using the
                        // ORIGINAL chord type (the parser stashed it before
                        // any normalisation).
                        const auto fam = NoteTransposer::srcFamilyOf (
                            (ChordQuality) (int) e.origSrcChordType);
                        const int score = NoteTransposer::srcFamilyAffinityScore (
                            fam, bucket);

                        bool wins = (score > bestScore[dst]);
                        if (! wins && score == bestScore[dst] && bestIdx[dst] >= 0)
                        {
                            // Score tie: prefer the NATIVE-routed source
                            // (srcChannel == dst) — the part that belongs on
                            // this destination — over a cross-routed double (a
                            // low-channel per-quality / SFX variant).  Real
                            // files tag every variant with the same reference
                            // chord (CMaj7), so without this a sparse cross-
                            // routed take (e.g. a 2-note "Bass SFX" on ch 2)
                            // out-ranks the real native bass on ch 10 purely by
                            // lower channel and starves the part to silence in
                            // single-finger mode.  This restores the native
                            // preference the old pruneDuplicateSources used.
                            // Fall back to file-order (lower srcChannel) when
                            // both are native or both cross-routed.
                            const auto& cur = sec.casm[(size_t) bestIdx[dst]];
                            const bool eNative   = ((int) e.srcChannel   == dst);
                            const bool curNative = ((int) cur.srcChannel == dst);
                            wins = (eNative != curNative)
                                     ? eNative
                                     : (e.srcChannel < cur.srcChannel);
                        }
                        if (wins)
                        {
                            bestScore[dst] = score;
                            bestIdx  [dst] = (int) i;
                        }
                    }

                    // Mark winners (and clear all others' flag for this
                    // bucket).  For dsts that nothing targeted, no entry
                    // gets the flag — harmless, no dispatch happens there.
                    //
                    // Drums are the exception: EVERY entry on a rhythm
                    // destination (dst 8/9 or a drum-flagged channel) wins,
                    // even when several target the same dst.  The old
                    // "first drum entry per dst wins" starved the real kit
                    // whenever a second (often note-less placeholder) entry
                    // also routed to dst 9 — the missing-drums bug.  This now
                    // matches computeBetelgeuseFingeredPlayers, which already
                    // fires all drum entries, so both dispatch modes agree.
                    for (size_t i = 0; i < sec.casm.size(); ++i)
                    {
                        const auto& e = sec.casm[i];
                        const int dst = e.dstChannel & 0x0F;
                        sec.casm[i].isWinnerForBucket[(size_t) bucket] =
                            (dst == 8 || dst == 9 || e.isDrumChannel)
                                ? true
                                : (bestIdx[dst] == (int) i);
                    }
                }
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        // Betelgeuse FINGERED-mode per-quality player selection.
        //
        // SINGLE-FINGER mode reduces every press to major / minor and uses the
        // 8-bucket winners (computeBetelgeuseMultiSrcWinners, above).  FINGERED
        // mode instead honours the file's chord-mute bitmap: for each of the 34
        // fingered chord qualities, a source plays iff its chordMuteRaw bit for
        // that quality is set.  This lets multi-record styles — a broad chord
        // comp plus per-quality colour voicings all targeting one destination —
        // sound the right combination per chord instead of collapsing to one
        // bucket winner (which the file-order tiebreak otherwise starves down to
        // a single sparse take, because real files tag every variant with the
        // same reference chord, CMaj7).
        //
        // No-match fallback: if NO sibling targeting a destination enables a
        // given quality (the bitmap leaves a gap — e.g. a file with no Maj7
        // take), that quality fires the destination's BROADEST source (most
        // chord-mute bits set = the main comp) so the part plays its core
        // voicing rather than going silent or dropping to a sparse colour take.
        // Drums always fire.
        // ─────────────────────────────────────────────────────────────────────
        void computeBetelgeuseFingeredPlayers (StyleData& out)
        {
            for (auto& sec : out.sections)
            {
                if (sec.casm.empty()) continue;

                // Broadest-coverage (most chord-mute bits set) non-drum source
                // per dst — the no-match fallback's "main comp".  Tiebreak:
                // lower srcChannel (the file's authored order).
                int broadestForDst[16];
                int broadestBits  [16];
                for (int d = 0; d < 16; ++d) { broadestForDst[d] = -1; broadestBits[d] = -1; }
                for (size_t i = 0; i < sec.casm.size(); ++i)
                {
                    const auto& e = sec.casm[i];
                    const int dst = e.dstChannel & 0x0F;
                    if (dst == 8 || dst == 9 || e.isDrumChannel) continue;

                    int bits = 0;
                    for (int b = 0; b < 5; ++b)
                    {
                        unsigned v = e.chordMuteRaw[(size_t) b];
                        while (v) { bits += (int) (v & 1u); v >>= 1; }
                    }
                    const bool better =
                        bits > broadestBits[dst]
                        || (bits == broadestBits[dst] && broadestForDst[dst] >= 0
                            && e.srcChannel < sec.casm[(size_t) broadestForDst[dst]].srcChannel);
                    if (better)
                    {
                        broadestBits[dst]   = bits;
                        broadestForDst[dst] = (int) i;
                    }
                }

                for (int q = 0; q < 34; ++q)
                {
                    const auto quality = (ChordQuality) q;

                    // Does any non-drum source for each dst enable this quality
                    // via its chord-mute bitmap?
                    bool anyAllowed[16] = { false };
                    for (const auto& e : sec.casm)
                    {
                        const int dst = e.dstChannel & 0x0F;
                        if (dst == 8 || dst == 9 || e.isDrumChannel) continue;
                        if (NoteTransposer::chordMaskBit (quality, e))
                            anyAllowed[dst] = true;
                    }

                    for (size_t i = 0; i < sec.casm.size(); ++i)
                    {
                        auto& e = sec.casm[i];
                        const int dst = e.dstChannel & 0x0F;

                        // Drums: always fire (matches the winner pass).
                        if (dst == 8 || dst == 9 || e.isDrumChannel)
                        {
                            e.firesForFingeredQuality[(size_t) q] = true;
                            continue;
                        }

                        if (anyAllowed[dst])
                        {
                            // At least one sibling covers this quality — honour
                            // the bitmap exactly (voicings may layer).
                            e.firesForFingeredQuality[(size_t) q]
                                = NoteTransposer::chordMaskBit (quality, e);
                        }
                        else
                        {
                            // Bitmap gap for this dst+quality — fire the broadest
                            // comp so the destination plays its main voicing.
                            e.firesForFingeredQuality[(size_t) q]
                                = ((int) i == broadestForDst[dst]);
                        }
                    }
                }
            }
        }

        void detectTrailingChunks (const uint8_t* raw, size_t size,
                                   size_t fromOffset, StyleData& out)
        {
            size_t pos = fromOffset;
            while (pos + 8 <= size)
            {
                const char* tag = (const char*) (raw + pos);
                const uint32_t tlen = (uint32_t) (
                    ((uint32_t) raw[pos + 4] << 24) |
                    ((uint32_t) raw[pos + 5] << 16) |
                    ((uint32_t) raw[pos + 6] << 8)  |
                     (uint32_t) raw[pos + 7]);

                if      (std::memcmp (tag, "OTSc", 4) == 0) out.hasOTSc = true;
                else if (std::memcmp (tag, "FNRc", 4) == 0) out.hasFNRc = true;
                else break;

                pos += 8 + (size_t) tlen;
            }
        }
    }

    // =========================================================================
    //  Public API
    // =========================================================================
    bool StyleLoader::loadFromFile (const juce::File& file,
                                    StyleData& outStyle,
                                    juce::String& errorMsg)
    {
        if (! file.existsAsFile())
        {
            errorMsg = "File not found: " + file.getFullPathName();
            return false;
        }

        juce::MemoryBlock mb;
        if (! file.loadFileAsData (mb))
        {
            errorMsg = "Failed to read file: " + file.getFullPathName();
            return false;
        }

        const bool ok = loadFromBytes (static_cast<const uint8_t*> (mb.getData()),
                                       mb.getSize(), outStyle, errorMsg);
        if (ok) outStyle.sourceFile = file;
        return ok;
    }

    bool StyleLoader::loadFromBytes (const uint8_t* data,
                                     size_t size,
                                     StyleData& outStyle,
                                     juce::String& errorMsg)
    {
        outStyle = StyleData{};
        errorMsg.clear();

        if (size < 22)
        {
            errorMsg = "File too small to be a style";
            return false;
        }

        // ─── MThd ────────────────────────────────────────────────────────────
        if (std::memcmp (data, "MThd", 4) != 0)
        {
            errorMsg = "Not a MIDI file (missing MThd)";
            return false;
        }
        ByteReader r (data, size, 4);
        if (r.u32be() != 6)
        {
            errorMsg = "Unexpected MThd header length";
            return false;
        }
        const uint16_t format = r.u16be();
        const uint16_t ntrks  = r.u16be();
        const uint16_t div    = r.u16be();

        if (format != 0)
        {
            errorMsg = "Style files must be SMF Type 0 (got type " + juce::String (format) + ")";
            return false;
        }
        if (ntrks != 1)
        {
            errorMsg = "Style files must have exactly 1 track (got " + juce::String (ntrks) + ")";
            return false;
        }
        if ((div & 0x8000) != 0)
        {
            errorMsg = "SMPTE-based timing not supported";
            return false;
        }
        outStyle.ticksPerQuarter = div;

        // ─── MTrk ────────────────────────────────────────────────────────────
        if (! r.tagMatches (r.pos, "MTrk"))
        {
            errorMsg = "Missing MTrk after MThd";
            return false;
        }
        r.pos += 4;
        const uint32_t trackLen = r.u32be();
        const size_t   trackEnd = r.pos + trackLen;
        if (trackEnd > size)
        {
            errorMsg = "Truncated track (declared length exceeds file)";
            return false;
        }

        std::vector<MarkerHit> markers;
        parseTrack (r, trackEnd, outStyle, markers, errorMsg);

        // Convert "ticks within track" -> last marker tick + maximum bar count
        // so the final section ends at the highest event tick we saw.
        int trackEndTick = 0;
        for (const auto& e : outStyle.events) trackEndTick = juce::jmax (trackEndTick, e.tick + 1);
        for (const auto& m : markers)         trackEndTick = juce::jmax (trackEndTick, m.tick);

        finaliseSectionRanges (outStyle, markers, trackEndTick);
        assignEventsToSections (outStyle);

        // ─── CASM ────────────────────────────────────────────────────────────
        // The CASM chunk normally follows the MTrk data immediately, but some
        // SFF files insert padding (commonly a single 0x00 byte, occasionally
        // more) between the end of the track and the "CASM" tag.  Requiring
        // CASM to sit exactly at trackEnd silently dropped the ENTIRE CASM for
        // those files — every channel then fell back to an identity route with
        // no transposition rule, so chord switching died on the whole style
        // while padding-free files worked.  Scan forward from trackEnd for the
        // tag instead.  We start past the track data, so the first "CASM" we
        // find is the real chunk, not a coincidental match inside the music.
        size_t cursor = trackEnd;
        while (cursor + 4 <= size && std::memcmp (data + cursor, "CASM", 4) != 0)
            ++cursor;
        if (cursor + 8 <= size && std::memcmp (data + cursor, "CASM", 4) == 0)
        {
            const uint32_t casmLen = (uint32_t) (
                ((uint32_t) data[cursor + 4] << 24) |
                ((uint32_t) data[cursor + 5] << 16) |
                ((uint32_t) data[cursor + 6] << 8)  |
                 (uint32_t) data[cursor + 7]);
            parseCasm (data, size, cursor, outStyle);
            cursor += 8 + (size_t) casmLen;
        }

        // ─── OTSc / FNRc (presence only; content parsed lazily later) ─────────
        detectTrailingChunks (data, size, cursor, outStyle);

        // Collect the complete (bank, program) voice set for pre-loading.
        outStyle.buildVoiceManifest();

        // Release any melodic note that would ring across a section's loop seam
        // (hung-note guard; also stops a sustained note from holding the old
        // chord into the next phrase).  Re-bins events, so it runs last.
        clampSeamCrossingNotes (outStyle);

        return true;
    }

    // =========================================================================
    //  Debug describe()
    // =========================================================================
    juce::String StyleLoader::describe (const StyleData& s)
    {
        juce::String out;
        out << "Style: \"" << s.name << "\""
            << "  (" << (s.isSFF2 ? "SFF2" : "SFF1") << ")\n"
            << "PPQN: " << s.ticksPerQuarter
            << "   Time: " << s.timeSigNum << "/" << s.timeSigDen << "\n"
            << "Events: " << (int) s.events.size()
            << "   CASM: " << (s.hasCasm ? "yes" : "no")
            << "   OTSc: " << (s.hasOTSc ? "yes" : "no")
            << "   FNRc: " << (s.hasFNRc ? "yes" : "no") << "\n\n";

        out << "=== Sections (in tick order) ===\n";
        std::vector<const StyleSectionData*> sorted;
        for (auto& sec : s.sections) if (sec.present) sorted.push_back (&sec);
        std::sort (sorted.begin(), sorted.end(),
                   [](const StyleSectionData* a, const StyleSectionData* b)
                   { return a->startTick < b->startTick; });
        for (auto* sec : sorted)
        {
            out << "  " << juce::String (getStyleSectionName (sec->id)).paddedRight (' ', 11)
                << "  bar " << juce::String (1 + sec->startTick
                                                   / juce::jmax (1, s.ticksPerQuarter * s.timeSigNum * 4
                                                                        / juce::jmax (1, s.timeSigDen))).paddedLeft (' ', 3)
                << " (" << sec->lengthBars (s.ticksPerQuarter, s.timeSigNum) << " bars, "
                << (int) sec->eventIdx.size() << " events, "
                << (int) sec->casm.size() << " CASM rules)\n";
        }

        out << "\n=== Voice setup ===\n";
        out << "  Ch  Bank   PC  Vol  Pan  Rev  Cho\n";
        for (int ch = 0; ch < 16; ++ch)
        {
            const auto& v = s.voices[ch];
            if (! v.isUsed()) continue;
            out << "  " << juce::String (ch + 1).paddedLeft (' ', 2)
                << "  " << juce::String (v.bankMsb).paddedLeft (' ', 3) << "/"
                <<        juce::String (v.bankLsb).paddedLeft (' ', 3)
                << "  " << juce::String (v.program).paddedLeft (' ', 3)
                << "  " << juce::String (v.volume) .paddedLeft (' ', 3)
                << "  " << juce::String (v.pan)    .paddedLeft (' ', 3)
                << "  " << juce::String (v.reverb) .paddedLeft (' ', 3)
                << "  " << juce::String (v.chorus) .paddedLeft (' ', 3)
                << "\n";
        }

        out << "\n=== CASM rules per section ===\n";
        for (auto* sec : sorted)
        {
            if (sec->casm.empty()) continue;
            out << "  [" << getStyleSectionName (sec->id) << "]\n";
            for (const auto& e : sec->casm)
            {
                const char* root = kNoteNames[e.srcChordRoot % 12];
                const char* type = (e.srcChordType < 34) ? kChordTypeNames[e.srcChordType] : "?";
                out << "    src " << juce::String (e.srcChannel + 1).paddedLeft (' ', 2)
                    << " -> dst " << juce::String (e.dstChannel + 1).paddedLeft (' ', 2)
                    << "  voice='" << e.voiceName << "'"
                    << "  src=" << root << type
                    << (e.isDrumChannel ? "  [drum]" : "")
                    << "\n";
                for (int z = 0; z < 3; ++z)
                {
                    const auto& zone = e.zones[z];
                    const char* zname = (z == 0 ? "low " : z == 1 ? "mid " : "high");
                    const char* nttN  = zone.isGuitarNTR()
                                        ? (zone.nttCode() < 3 ? kNTTGuitarNames[zone.nttCode()] : "?")
                                        : (zone.nttCode() < 11 ? kNTTNames[zone.nttCode()] : "?");
                    out << "      " << zname
                        << "  NTR=" << kNTRNames[zone.ntr]
                        << " NTT=" << nttN
                        << (zone.isBassOn() ? "(Bass)" : "")
                        << "  range " << (int) zone.noteLow << ".." << (int) zone.noteHigh
                        << "  RTR=" << kRTRNames[zone.rtr] << "\n";
                }
            }
        }
        return out;
    }

} // namespace Betel
