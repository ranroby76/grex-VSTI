
#include "FstLoader.h"
#include "StyleLoader.h"
#include <algorithm>

namespace Betel
{
namespace Fst
{

namespace
{
    struct SectionNameRow { const char* name; StyleSection id; };

    //  THE SAME TABLE StyleLoader.cpp USES, and it has to be: the converter
    //  writes Yamaha's section names through verbatim, so both readers must
    //  agree on what "Fill In BA" is or a style would load with a hole in it.
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
        { "Fill In BA",  StyleSection::FillBA  },
        { "Intro A",     StyleSection::IntroA  },
        { "Intro B",     StyleSection::IntroB  },
        { "Intro C",     StyleSection::IntroC  },
        { "Ending A",    StyleSection::EndingA },
        { "Ending B",    StyleSection::EndingB },
        { "Ending C",    StyleSection::EndingC },
    };

    StyleSection sectionFromName (const juce::String& raw)
    {
        const auto n = raw.trim();
        for (const auto& row : kSectionNames)
            if (n.equalsIgnoreCase (row.name)) return row.id;
        return StyleSection::Count;              // unknown -> ignored
    }

}

//==============================================================================
bool loadFromBytes (const void* data, size_t numBytes,
                    StyleData& out, juce::String& errorMsg,
                    const juce::String& accessCode)
{
    errorMsg.clear();

    if (data == nullptr || numBytes < (size_t) kHeaderSize)
    { errorMsg = "Too small to be a .fgt file."; return false; }

    const auto* rawBase = static_cast<const juce::uint8*> (data);
    juce::MemoryInputStream in (data, numBytes, false);

    if ((juce::uint32) in.readInt() != kMagic)
    { errorMsg = "Bad magic: not a .fgt file."; return false; }

    const auto version = (juce::uint16) in.readShort();
    if (version != kVersion)
    {
        // REFUSED, NOT GUESSED, and the converter's header asks for exactly
        // this. A reader that limps along on an unknown version produces a
        // style that is subtly wrong, and subtly wrong is the kind of fault
        // nobody traces back to a file format.
        errorMsg = "Unsupported .fgt version " + juce::String ((int) version)
                 + " (this build reads " + juce::String ((int) kVersion) + ").";
        return false;
    }

    in.readShort();                                       // flags, reserved

    const int   ppq          = in.readInt();
    const float tempo        = in.readFloat();
    const int   timeSigNum   = (juce::uint8) in.readByte();
    const int   timeSigDen   = (juce::uint8) in.readByte();

    const int sectionCount = (juce::uint16) in.readShort();
    const int partCount    = (juce::uint16) in.readShort();
    const int ruleCount    = (juce::uint16) in.readShort();
    const int noteCount    = in.readInt();
    const int controlCount = in.readInt();

    const auto nameOff   = (juce::uint32) in.readInt();
    /*origOff*/            in.readInt();
    /*modelOff*/           in.readInt();
    /*srcOff*/             in.readInt();
    const auto stringOff = (juce::uint32) in.readInt();
    const auto stringLen = (juce::uint32) in.readInt();
    const auto secOff    = (juce::uint32) in.readInt();
    const auto partOff   = (juce::uint32) in.readInt();
    const auto noteOff   = (juce::uint32) in.readInt();
    const auto ctrlOff   = (juce::uint32) in.readInt();
    const auto ruleOff   = (juce::uint32) in.readInt();
    const auto dsCount   = (juce::uint32) in.readInt();
    const auto dsOff     = (juce::uint32) in.readInt();
    const auto codeHash  = (juce::uint32) in.readInt();

    // ── THE CODE IS THE SEPARATION ───────────────────────────────────────────
    if (codeHash != hashAccessCode (accessCode))
    {
        errorMsg = codeHash == 0
            ? "This style has no access code, but this build supplies one."
            : "Wrong access code - this style belongs to another product.";
        return false;
    }

    // Every offset is checked against the real length before it is used: a
    // self-declared offset is a promise, not a fact, and a file may have been
    // truncated in transit.
    auto within = [numBytes] (juce::uint32 off, size_t need)
    { return (size_t) off + need <= numBytes; };

    if (! within (stringOff, stringLen)
        || ! within (secOff,  (size_t) sectionCount * kSectionRecSize)
        || ! within (partOff, (size_t) partCount    * kPartRecSize)
        || ! within (noteOff, (size_t) noteCount    * kNoteRecSize)
        || ! within (ctrlOff, (size_t) controlCount * kControlRecSize)
        || ! within (ruleOff, (size_t) ruleCount    * kRuleRecSize)
        || ! within (dsOff,   (size_t) dsCount      * kDrumSetupRecSize))
    { errorMsg = "File is truncated or its table offsets are wrong."; return false; }

    // Unscrambled into a WORKING COPY, never over the caller's buffer — the
    // input is often a memory-mapped file or a block someone else owns.
    juce::MemoryBlock plain;
    const juce::uint8* base = rawBase;

    if (codeHash != 0 && numBytes > (size_t) stringOff)
    {
        plain.append (rawBase, numBytes);
        scramble (static_cast<juce::uint8*> (plain.getData()) + stringOff,
                  numBytes - (size_t) stringOff, codeHash, stringOff);
        base = static_cast<const juce::uint8*> (plain.getData());
    }

    auto str = [&] (juce::uint32 off) -> juce::String
    {
        if (off >= stringLen) return {};
        return juce::String::fromUTF8 ((const char*) (base + stringOff + off));
    };

    // ── The style itself ─────────────────────────────────────────────────────
    out = StyleData();
    out.name            = str (nameOff);
    out.ticksPerQuarter = ppq > 0 ? ppq : 1920;
    out.originalBPM     = (double) tempo;
    out.timeSigNum      = timeSigNum > 0 ? timeSigNum : 4;
    out.timeSigDen      = timeSigDen > 0 ? timeSigDen : 4;
    out.hasCasm         = ruleCount > 0;
    out.hasOTSc         = false;              // OTS is not carried into .fgt
    out.isSFF2          = false;              // the source format no longer applies
    out.gmControlled    = true;

    // ── Parts ────────────────────────────────────────────────────────────────
    struct PartRec
    {
        juce::uint32 firstNote = 0, noteCount = 0, firstControl = 0, controlCount = 0;
        uint8_t channel = 0;
        bool    isDrums = false;
        int16_t bankMsb = -1, bankLsb = -1, program = -1,
                volume  = -1, pan = -1, reverb = -1, chorus = -1;
    };
    std::vector<PartRec> parts;
    parts.reserve ((size_t) juce::jmax (0, partCount));

    std::array<bool, 16> channelIsDrums { };

    for (int i = 0; i < partCount; ++i)
    {
        juce::MemoryInputStream p (base + partOff + (size_t) i * kPartRecSize,
                                   kPartRecSize, false);
        PartRec rec;
        rec.firstNote    = (juce::uint32) p.readInt();
        rec.noteCount    = (juce::uint32) p.readInt();
        rec.firstControl = (juce::uint32) p.readInt();
        rec.controlCount = (juce::uint32) p.readInt();

        rec.channel = (uint8_t) p.readByte();
        p.readByte();                                       // role
        rec.isDrums = ((juce::uint8) p.readByte() & partIsDrums) != 0;
        p.readByte();                                       // reserved

        rec.bankMsb = (int16_t) p.readShort();
        rec.bankLsb = (int16_t) p.readShort();
        rec.program = (int16_t) p.readShort();
        rec.volume  = (int16_t) p.readShort();
        rec.pan     = (int16_t) p.readShort();
        rec.reverb  = (int16_t) p.readShort();
        rec.chorus  = (int16_t) p.readShort();

        parts.push_back (rec);

        // WHICH SOURCE CHANNELS ARE KITS. A .fgt has no Ctab trailing flags,
        // so the CASM entries below cannot say it themselves - but the PART
        // already does, and the load passes gate on it (a drum source is never
        // pruned as a chord variant and never measured for chordal polyphony).
        if (rec.channel < 16 && rec.isDrums)
            channelIsDrums[(size_t) rec.channel] = true;

        // ── THE INITIAL VOICE SETUP, taken from the PART rather than an SInt
        //    block, because a .fgt has no SInt: the converter resolved every
        //    channel's voice at conversion and wrote it here.
        //
        //    FIRST WRITER WINS. A channel appears once per section it plays in,
        //    with the same voice each time; letting a later section overwrite
        //    would make the setup depend on section ORDER, which is exactly the
        //    inherited-bank trap that made a piano ballad play as a drum kit.
        if (rec.channel < 16)
        {
            auto& v = out.voices[(size_t) rec.channel];
            if (v.program < 0 && v.bankMsb < 0)
            {
                v.bankMsb = rec.bankMsb; v.bankLsb = rec.bankLsb;
                v.program = rec.program; v.volume  = rec.volume;
                v.pan     = rec.pan;     v.reverb  = rec.reverb;
                v.chorus  = rec.chorus;
            }
        }
    }

    // ── Sections, and the events inside them ─────────────────────────────────
    //
    // TICKS IN A .fgt ARE SECTION-RELATIVE. StyleData wants one timeline, so
    // each section is laid down after the last at a running offset. The offset
    // is ours, not the file's: it exists only so the event list has somewhere
    // to put every section without two of them overlapping.
    int runningTick = 0;

    for (int i = 0; i < sectionCount; ++i)
    {
        juce::MemoryInputStream s (base + secOff + (size_t) i * kSectionRecSize,
                                   kSectionRecSize, false);
        const auto  secName   = str ((juce::uint32) s.readInt());

        // ── THE SECTION RECORD IS ABSOLUTE. THE NOTES INSIDE IT ARE NOT. ─────
        //
        // A .fgt keeps the section's ORIGINAL start and end on the source
        // timeline - Main A at 7680..38400 - while every note it holds is
        // numbered from 0 within the section. Two different clocks in one
        // record, and only the second is documented as relative.
        //
        // I read endTick as if it were a length. Main A became twenty bars
        // instead of four: the notes filled the first four and the sequencer
        // then looped sixteen bars of silence. That is the "plays partially,
        // pauses every few seconds" report, and every section had it - the
        // last one, Fill In BA, came out 140 bars long, which is why the
        // transitions hung worst of all.
        //
        // THE SPAN IS THE DIFFERENCE. Nothing else in this file uses the
        // absolute numbers, so they are consumed here and go no further.
        const int   fileStart = s.readInt();
        const int   fileEnd   = s.readInt();
        const int   span      = juce::jmax (0, fileEnd - fileStart);
        const int   firstPart = (juce::uint16) s.readShort();
        const int   nParts    = (juce::uint16) s.readShort();
        const int   secNum    = (juce::uint8) s.readByte();
        /*secDen*/              s.readByte();

        const auto id = sectionFromName (secName);
        if (id == StyleSection::Count) continue;         // a name we do not know

        auto& sec = out.sections[(size_t) id];
        sec.id      = id;
        sec.present = true;
        sec.startTick = runningTick;
        sec.endTick   = runningTick + span;

        for (int k = 0; k < nParts; ++k)
        {
            const int idx = firstPart + k;
            if (idx < 0 || idx >= (int) parts.size()) continue;
            const auto& rec = parts[(size_t) idx];

            // ── Controls ─────────────────────────────────────────────────────
            for (juce::uint32 c = 0; c < rec.controlCount; ++c)
            {
                juce::MemoryInputStream e (base + ctrlOff
                                             + (size_t) (rec.firstControl + c) * kControlRecSize,
                                           kControlRecSize, false);
                StyleEvent ev;
                ev.tick    = runningTick + e.readInt();
                ev.status  = (uint8_t) e.readByte();
                ev.channel = rec.channel;
                ev.data1   = (uint8_t) e.readByte();
                ev.data2   = (uint8_t) e.readByte();
                sec.eventIdx.push_back (out.events.size());
                out.events.push_back (ev);
            }

            // ── Notes, expanded back into on/off pairs ───────────────────────
            for (juce::uint32 n = 0; n < rec.noteCount; ++n)
            {
                juce::MemoryInputStream e (base + noteOff
                                             + (size_t) (rec.firstNote + n) * kNoteRecSize,
                                           kNoteRecSize, false);
                const int     tick   = e.readInt();
                const int     len    = e.readInt();
                const uint8_t key    = (uint8_t) e.readByte();
                const uint8_t vel    = (uint8_t) e.readByte();
                const uint8_t offVel = (uint8_t) e.readByte();

                StyleEvent on;
                on.tick    = runningTick + tick;
                on.status  = 0x90;
                on.channel = rec.channel;
                on.data1   = key;
                on.data2   = juce::jlimit<uint8_t> (1, 127, vel);
                sec.eventIdx.push_back (out.events.size());
                out.events.push_back (on);

                // A LENGTH OF -1 MEANS THE SOURCE NEVER CLOSED THE NOTE. Held to
                // the section end is the only reading that cannot leave a voice
                // ringing into the next section.
                //
                // CLAMPED TO THE SECTION, and not only for that case. A SECTION
                // LOOPS: the sequencer plays [startTick, endTick] and jumps
                // back, so a note-off past endTick is never reached and the
                // voice sustains through every repeat. This file has no
                // over-running notes - I checked all fifteen sections - which
                // means the fault would have sat here silently until some other
                // style did, and then looked like a stuck-note bug in the
                // engine rather than a clamp missing in the reader.
                const int rawEnd = (len >= 0) ? (tick + len) : span;
                const int endRel = juce::jlimit (tick + 1, juce::jmax (tick + 1, span), rawEnd);

                StyleEvent off;
                off.tick    = runningTick + endRel;
                off.status  = 0x80;
                off.channel = rec.channel;
                off.data1   = key;
                off.data2   = offVel;
                sec.eventIdx.push_back (out.events.size());
                out.events.push_back (off);
            }
        }

        // A bar of air between sections. With note-offs clamped above, nothing
        // should ever land in it - which is exactly why it stays: if something
        // does, it shows up as a gap rather than as an event bleeding into the
        // next section's window.
        runningTick = sec.endTick + out.ticksPerQuarter * out.timeSigNum;
        juce::ignoreUnused (secNum);
    }

    // ── Chord rules -> CASM entries, filed under the sections they name ──────
    for (int i = 0; i < ruleCount; ++i)
    {
        juce::MemoryInputStream s (base + ruleOff + (size_t) i * kRuleRecSize,
                                   kRuleRecSize, false);
        const auto voiceName = str ((juce::uint32) s.readInt());
        const auto secList   = str ((juce::uint32) s.readInt());

        CasmEntry ce;
        ce.srcChannel = (uint8_t) s.readByte();
        ce.dstChannel = (uint8_t) s.readByte();

        const bool bassOn = ((juce::uint8) s.readByte() & ruleBassOn) != 0;

        ce.srcChordRoot = ce.origSrcChordRoot = (uint8_t) s.readByte();
        ce.srcChordType = ce.origSrcChordType = (uint8_t) s.readByte();

        ce.lowMidLimit  = (uint8_t) s.readByte();
        ce.midHighLimit = (uint8_t) s.readByte();
        s.readByte();                                       // reserved

        // ── THE THREE ZONES, COPIED STRAIGHT ACROSS ─────────────────────────
        //
        // These bytes are already in CasmZone's own form, NTT raw with its
        // bass bit, so there is nothing to translate. The v4 record had no NTT
        // at all and this loader synthesised one - `ntt = bassOn ? 0x80 : 0` -
        // which is NTT 0, BYPASS, on every part of every converted style.
        //
        // Under Bypass the transposer returns the source-relative pitch class
        // unchanged and simply re-roots it, so a part recorded against CMaj7
        // played its major third and major seventh over every chord the player
        // held: minor chords came out major, and nothing was ever dropped, so
        // the clashing extensions stayed too. That is the whole of the "the
        // chords sound bad" report. Nothing else about those files was wrong,
        // which is exactly why it was hard to see.
        for (int z = 0; z < 3; ++z)
        {
            auto& zone = ce.zones[z];
            zone.ntr      = (uint8_t) s.readByte();
            zone.ntt      = (uint8_t) s.readByte();          // RAW, bass bit kept
            zone.highKey  = (uint8_t) s.readByte();
            zone.noteLow  = (uint8_t) s.readByte();
            zone.noteHigh = (uint8_t) s.readByte();
            zone.rtr      = (uint8_t) s.readByte();
        }

        // ── THE MUTE BYTES, IN SOURCE ORDER ─────────────────────────────────
        //
        // v4 packed these into a uint16 and a uint64, wrote them little-endian
        // and read them back a byte at a time, which handed both masks over
        // MIRRORED. noteMuteBits() masks raw[0] with 0x0F, so a style writing
        // the usual 0F FF decoded as 0x0F0F instead of 0x0FFF - silencing pitch
        // classes E, F, F# and G on every part that used it, drums included.
        // On a CMaj7 reference that is the third and the fifth.
        //
        // Now they are stored one byte per source byte, so there is no order
        // to get wrong.
        ce.noteMuteRaw[0] = (uint8_t) s.readByte();          // source byte 19
        ce.noteMuteRaw[1] = (uint8_t) s.readByte();          // source byte 20
        for (int b = 0; b < 5; ++b)
            ce.chordMuteRaw[(size_t) b] = (uint8_t) s.readByte();

        // The bass flag is redundant with bit 7 of the middle zone's NTT and
        // is carried for the inspector; honour it either way rather than
        // letting the two disagree.
        if (bassOn) ce.zones[1].ntt = (uint8_t) (ce.zones[1].ntt | 0x80);

        ce.isDrumChannel = (ce.srcChannel < 16)
                         && channelIsDrums[(size_t) ce.srcChannel];

        voiceName.copyToUTF8 (ce.voiceName, sizeof (ce.voiceName));

        // A part named for several sections is filed under each of them: the
        // converter writes the Sdec list through verbatim, exactly as the CASM
        // held it.
        juce::StringArray names;
        names.addTokens (secList, ",", "");
        names.trim();
        names.removeEmptyStrings();
        if (names.isEmpty()) names.add (secList);

        for (const auto& n : names)
        {
            const auto id = sectionFromName (n);
            if (id == StyleSection::Count) continue;
            out.sections[(size_t) id].casm.push_back (ce);
        }
    }

    // Drum setups are read past, not applied: the converter has already resolved
    // every key and velocity into the notes above, which is the whole point of
    // the format. They stay in the file for the editor, not for playback.
    juce::ignoreUnused (dsCount, dsOff);

    // ── Order the timeline ───────────────────────────────────────────────────
    //
    // Parts were written one after another, so events arrive grouped by part
    // and not by time. Sorting by tick with NOTE-OFF BEFORE NOTE-ON at the same
    // tick is what stops a repeated key cutting its own retrigger: the off for
    // the previous note must land first or the new note is silenced instantly.
    std::vector<size_t> order (out.events.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;

    std::stable_sort (order.begin(), order.end(),
        [&out] (size_t a, size_t b)
        {
            const auto& x = out.events[a];
            const auto& y = out.events[b];
            if (x.tick != y.tick) return x.tick < y.tick;
            const int xr = (x.status & 0xF0) == 0x80 ? 0 : ((x.status & 0xF0) == 0x90 ? 2 : 1);
            const int yr = (y.status & 0xF0) == 0x80 ? 0 : ((y.status & 0xF0) == 0x90 ? 2 : 1);
            return xr < yr;
        });

    std::vector<StyleEvent> sorted;
    sorted.reserve (out.events.size());
    std::vector<size_t> remap (out.events.size());
    for (size_t i = 0; i < order.size(); ++i)
    { remap[order[i]] = i; sorted.push_back (out.events[order[i]]); }
    out.events.swap (sorted);

    for (auto& sec : out.sections)
    {
        for (auto& idx : sec.eventIdx) idx = remap[idx];
        std::sort (sec.eventIdx.begin(), sec.eventIdx.end());
    }

    // ── THE LOAD PASSES, THE SAME SIX THE SFF PATH RUNS ─────────────────────
    //
    // This loader used to run NONE of them, and their defaults are all
    // permissive, so nothing failed loudly - it just played wrong:
    //
    //   pruneDuplicateSources          every variant of a multi-source
    //                                  destination stayed active, so three
    //                                  recordings of the same comp sounded at
    //                                  once instead of the one that fits the
    //                                  chord
    //   normaliseSourceChordToCMaj7    srcRel was measured from whatever the
    //                                  record happened to declare rather than
    //                                  from the one calibration reference
    //   computeChordalDestinations     destChordal stayed all-false, so a
    //                                  chord part on a mono slot never got
    //                                  forced poly and its notes stole from
    //                                  each other
    //   computeBetelgeuseMultiSrcWinners
    //   computeBetelgeuseFingeredPlayers
    //                                  both tables default to all-true, so the
    //                                  dispatcher had no basis to pick a
    //                                  variant and fired all of them
    //   pruneIdenticalDrumSources      duplicate kit sources both fired, so
    //                                  every hit they shared played twice
    //
    // RUN HERE, NOT EARLIER, because three of them read out.events and
    // sec.eventIdx - which are only correct once the sort and remap above have
    // finished. They are the SFF path's own functions, called through one
    // entry point rather than copied, so the two loaders cannot drift.
    StyleLoader::finaliseCasm (out);

    out.buildVoiceManifest();

    if (out.events.empty())
    { errorMsg = "The style parsed, but contains no events."; return false; }

    return true;
}

//==============================================================================
bool loadFromFile (const juce::File& file, StyleData& out,
                   juce::String& errorMsg, const juce::String& accessCode)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() == 0)
    { errorMsg = "Could not read " + file.getFileName(); return false; }

    if (! loadFromBytes (mb.getData(), mb.getSize(), out, errorMsg, accessCode))
        return false;

    // The FILENAME wins over the name inside the file, and deliberately: the
    // library's display names were curated by renaming files, so the stem is
    // what the player has been shown everywhere else. An internal name that
    // disagrees is a leftover from whatever the style was converted from.
    out.name = file.getFileNameWithoutExtension();
    return true;
}

//==============================================================================
Summary peek (const juce::File& file)
{
    Summary s;

    juce::FileInputStream in (file);
    if (! in.openedOk()) return s;

    juce::HeapBlock<juce::uint8> head (kHeaderSize);
    if (in.read (head.getData(), kHeaderSize) != kHeaderSize) return s;

    juce::MemoryInputStream h (head.getData(), kHeaderSize, false);
    if ((juce::uint32) h.readInt() != kMagic) return s;
    if ((juce::uint16) h.readShort() != kVersion) return s;
    h.readShort();

    h.readInt();                                    // ppq
    s.tempo      = (double) h.readFloat();
    s.timeSigNum = (juce::uint8) h.readByte();
    s.timeSigDen = (juce::uint8) h.readByte();
    s.sectionCount = (juce::uint16) h.readShort();
    h.readShort(); h.readShort();                   // partCount, ruleCount
    h.readInt();  h.readInt();                      // noteCount, controlCount

    const auto nameOff   = (juce::uint32) h.readInt();
    h.readInt(); h.readInt(); h.readInt();          // orig, model, source
    const auto stringOff = (juce::uint32) h.readInt();
    const auto stringLen = (juce::uint32) h.readInt();
    for (int i = 0; i < 7; ++i) h.readInt();        // table offsets + drum setup
    s.accessCodeHash = (juce::uint32) h.readInt();

    // THE NAME IS IN THE SCRAMBLED REGION when a code is set, so it is read
    // through the same keystream the loader uses. The rest of the header is in
    // the clear by design, which is what lets a browser list a library it holds
    // no code for — but a name still has to be unscrambled to be legible.
    if (stringLen > 0 && nameOff < stringLen)
    {
        juce::MemoryBlock strings;
        if (in.setPosition ((juce::int64) stringOff))
        {
            strings.setSize (stringLen);
            if (in.read (strings.getData(), (int) stringLen) == (int) stringLen)
            {
                if (s.accessCodeHash != 0)
                    scramble (strings.getData(), strings.getSize(),
                              s.accessCodeHash, stringOff);

                const auto* c = static_cast<const char*> (strings.getData());
                s.name = juce::String::fromUTF8 (c + nameOff);
            }
        }
    }

    if (s.name.isEmpty()) s.name = file.getFileNameWithoutExtension();
    s.ok = true;
    return s;
}

} // namespace Fst
} // namespace Betel




