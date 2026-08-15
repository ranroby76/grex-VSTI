#pragma once
//==============================================================================
// StyleData.h
//
// Plain-old-data structures that hold a parsed Yamaha SFF1/SFF2 (SFF GE)
// style file. The parser (StyleLoader) fills these in; the engine (added in a
// later step) consumes them at playback.
//
// We model the file the way the spec describes it:
//   • 15 named sections (4 Mains, 4 Fills, 1 Break, 3 Intros, 3 Endings).
//   • Each section has a start/end tick within the single MIDI track.
//   • Per-channel voice setup captured from the SInt measure.
//   • A flat event vector covering the whole track, with each section
//     holding indices into that vector.
//   • CASM Ctab/Ctb2 rules attached per section, per source channel.
//==============================================================================

#include <JuceHeader.h>
#include <array>
#include <cstdint>
#include <vector>

namespace Betel
{
    // ─────────────────────────────────────────────────────────────────────────
    // Section enum — order is logical, NOT tick-order. Tick-order is per-style.
    // ─────────────────────────────────────────────────────────────────────────
    enum class StyleSection : int
    {
        MainA = 0, MainB, MainC, MainD,
        FillAA, FillBB, FillCC, FillDD,
        FillBA,                                // The "Break" section
        IntroA, IntroB, IntroC,
        EndingA, EndingB, EndingC,
        Count
    };

    constexpr int kNumStyleSections = static_cast<int> (StyleSection::Count); // 15

    const char*   getStyleSectionName     (StyleSection s) noexcept;
    StyleSection  getStyleSectionFromName (const juce::String& name) noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Lookup tables used by both the parser and any downstream display code.
    // Defined out-of-line in StyleLoader.cpp.
    // ─────────────────────────────────────────────────────────────────────────
    extern const char* const kNoteNames[12];        // "C", "C#", "D", ...
    extern const char* const kChordTypeNames[34];   // CASM chord-type 0x00..0x21
    extern const char* const kNTRNames[3];          // RootTrans, RootFixed, Guitar
    extern const char* const kNTTNames[11];         // Bypass..Dorian5V (codes 0..0x0A)
    extern const char* const kNTTGuitarNames[3];    // AllPurpose, Stroke, Arpeggio
    extern const char* const kRTRNames[6];          // Stop..NoteGen

    // ─────────────────────────────────────────────────────────────────────────
    // One MIDI event from the SMF track. SysEx and meta events are excluded —
    // those are processed at load time (voice setup, markers) and not replayed.
    // ─────────────────────────────────────────────────────────────────────────
    struct StyleEvent
    {
        int      tick    = 0;   // absolute tick from start of track
        uint8_t  status  = 0;   // top nibble = command (0x80..0xE0)
        uint8_t  channel = 0;   // source MIDI channel 0..15
        uint8_t  data1   = 0;
        uint8_t  data2   = 0;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // One NTR/NTT zone block. In SFF2 a Ctb2 has three of these (low/mid/high);
    // in SFF1 a Ctab has one and we fill all three identically.
    // ─────────────────────────────────────────────────────────────────────────
    struct CasmZone
    {
        uint8_t ntr      = 0;   // 0=RootTrans, 1=RootFixed, 2=Guitar
        uint8_t ntt      = 0;   // 0..0x0A normal, 0x80..0x8A bass-on
        uint8_t highKey  = 0;   // 0..0x0B (semitone-from-C cap)
        uint8_t noteLow  = 0;   // MIDI note 0..127
        uint8_t noteHigh = 127;
        uint8_t rtr      = 0;   // 0..5

        bool isBassOn()         const noexcept { return (ntt & 0x80) != 0; }
        uint8_t nttCode()       const noexcept { return ntt & 0x7F; }
        bool isGuitarNTR()      const noexcept { return ntr == 2; }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // One Ctab/Ctb2 record — the rule book for a single source channel
    // within the sections this CSEG covers.
    //
    // Note: noteMuteRaw and chordMuteRaw are stored as the raw bytes from the
    // file. The bit-to-chord-type mapping is documented in the spec but is
    // mildly ambiguous — runtime decode is deferred until we exercise it with
    // real chord progressions in step 3.
    // ─────────────────────────────────────────────────────────────────────────
    struct CasmEntry
    {
        uint8_t  srcChannel = 0;        // 0..15
        uint8_t  dstChannel = 0;        // 8..15 typically
        char     voiceName[9] = {};     // 8 ASCII chars + null terminator
        bool     isEditable = true;

        std::array<uint8_t, 2> noteMuteRaw  { { 0xFF, 0xFF } };  // bytes 19-20 in spec
        std::array<uint8_t, 5> chordMuteRaw { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };  // bytes 21-25

        uint8_t  srcChordRoot = 0;      // 0..0x0B
        uint8_t  srcChordType = 0;      // 0..0x21

        // Snapshot of srcChordRoot / srcChordType as they appeared in the
        // file — captured at parse time, BEFORE normaliseSourceChordToCMaj7
        // overrides the live fields.  The Betelgeuse multi-source dispatch
        // reads from these so it can route events by chord-family match and
        // do position substitution against the variant's real chord (e.g.
        // an entry recorded as Cmin stays tagged Cmin for Betelgeuse, even
        // though srcChordRoot/Type were rewritten to CMaj7 for the HonorCasm
        // path).  HonorCasm dispatch ignores these.
        uint8_t  origSrcChordRoot = 0;
        uint8_t  origSrcChordType = 0;

        uint8_t  lowMidLimit  = 0;      // SFF2: zone[0] / zone[1] split
        uint8_t  midHighLimit = 127;    // SFF2: zone[1] / zone[2] split
        CasmZone zones[3];              // [0]=low, [1]=mid, [2]=high

        bool     isDrumChannel  = false;
        uint8_t  drumPercKey    = 0;
        uint8_t  drumVolume     = 127;

        // De-duplication of multi-source destinations.  Some styles ship more
        // than one source channel for the same destination (e.g. a Maj7-,
        // a min- and a Maj-recorded version of the bass) and rely on the CASM
        // chord-mute mask to pick one per chord.  We instead keep a single
        // major-family source per destination and let the NTT generate every
        // chord from it; the rejected duplicates are flagged inactive here and
        // their events are dropped at dispatch time.
        bool     active = true;

        // Per-bucket winner flag for the Betelgeuse multi-source dispatch.
        // computeBetelgeuseMultiSrcWinners (in StyleLoader.cpp) sets one bit
        // per ChordTransposer 8-quality bucket — true if this CASM entry is
        // the highest-affinity variant for that bucket among siblings
        // (same section, same dstChannel).  At dispatch time, when in
        // Betelgeuse mode, events from non-winners are dropped so only the
        // best matching variant per chord fires.  Defaults to all true so
        // entries without competition (single-source destinations) always
        // play.  HonorCasm dispatch ignores this field.
        std::array<bool, 8> isWinnerForBucket
            { { true, true, true, true, true, true, true, true } };

        // Per-fingered-quality "this entry plays" flag for Betelgeuse FINGERED
        // mode.  computeBetelgeuseFingeredPlayers (in StyleLoader.cpp) sets one
        // bool per ChordTransposer fingered quality (0..33): true when this
        // entry's chord-mute bitmap (chordMuteRaw) enables it for that quality,
        // so multiple voicings can LAYER per chord (a broad comp + per-quality
        // colour takes) instead of collapsing to a single bucket winner.  When
        // the bitmap leaves a destination/quality gap (no sibling enables it),
        // the bucket-winner fires as a fallback so the destination never goes
        // silent.  Defaults to all true so single-source destinations always
        // play.  SINGLE-FINGER mode ignores this and uses isWinnerForBucket
        // (major/minor reduction); HonorCasm dispatch ignores it too.
        std::array<bool, 34> firesForFingeredQuality
            = [] { std::array<bool, 34> a{}; a.fill (true); return a; }();

        // 12-bit "which notes this channel plays on" bitmap, decoded from
        // the raw bytes. bit 0 = C, bit 1 = C#, ..., bit 11 = B.
        uint16_t noteMuteBits() const noexcept
        {
            return (uint16_t) (((noteMuteRaw[0] & 0x0F) << 8) | noteMuteRaw[1]);
        }
        bool noteAllowed (int midiNote) const noexcept
        {
            const int pc = ((midiNote % 12) + 12) % 12;
            return ((noteMuteBits() >> pc) & 1u) != 0u;
        }
        const CasmZone& zoneForNote (int midiNote) const noexcept
        {
            if (midiNote < lowMidLimit)  return zones[0];
            if (midiNote < midHighLimit) return zones[1];
            return zones[2];
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // Per-channel voice setup captured during the SInt measure (bar 1).
    // -1 means "not set" — channels not used by the style stay at -1.
    // ─────────────────────────────────────────────────────────────────────────
    struct StyleVoiceSetup
    {
        int bankMsb = -1;
        int bankLsb = -1;
        int program = -1;
        int volume  = -1;      // CC 7  Main Volume
        int pan     = -1;
        int reverb  = -1;
        int chorus  = -1;
        // CC 11 Expression.  The style's SECOND gain input, alongside CC 7:
        // composers park a part below its fader here and then ride it per
        // section (and for ending fade-outs).  Captured so the setup can apply
        // the authored value instead of forcing unity — see StylePlayer's
        // applyVoiceSetup and its CC 11 dispatch case.
        int expression = -1;

        bool isUsed() const noexcept { return program >= 0 || bankMsb >= 0; }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // One section's slice of the track plus its CASM rules.
    // ─────────────────────────────────────────────────────────────────────────
    struct StyleSectionData
    {
        StyleSection id        = StyleSection::Count;
        int   startTick        = 0;
        int   endTick          = 0;
        bool  present          = false;
        std::vector<size_t>    eventIdx;   // indices into StyleData::events
        std::vector<CasmEntry> casm;       // rules for this section

        int   lengthTicks() const noexcept { return endTick - startTick; }
        int   lengthBars (int ticksPerQuarter, int beatsPerBar = 4) const noexcept
        {
            const int bar = ticksPerQuarter * beatsPerBar;
            return (bar > 0) ? lengthTicks() / bar : 0;
        }

        const CasmEntry* findCasm (uint8_t srcChannel) const noexcept
        {
            for (auto& e : casm)
                if (e.srcChannel == srcChannel) return &e;
            return nullptr;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    // The fully-parsed style.
    // ─────────────────────────────────────────────────────────────────────────
    struct StyleData
    {
        juce::String name;
        juce::File   sourceFile;

        int  ticksPerQuarter = 1920;
        int  timeSigNum      = 4;
        int  timeSigDen      = 4;
        float originalBPM    = 120.0f;   // Yamaha tempo meta event (FF 51 03 …)
                                         // captured at parse time; used as the
                                         // sequencer's default when the host
                                         // doesn't supply one.

        bool isSFF2  = false;          // false = SFF1
        bool hasCasm = false;
        bool hasOTSc = false;          // One-Touch settings present (not parsed)
        bool hasFNRc = false;          // Music Finder DB present (not parsed)

        std::array<StyleVoiceSetup, 16>             voices;
        std::vector<StyleEvent>                     events;
        std::array<StyleSectionData, kNumStyleSections> sections;

        // Per-style "GM Controlled" (PCs apply) vs "Fixed" (PCs ignored; each
        // slot keeps its initial instrument).  Defaults to GM-controlled.
        bool gmControlled = true;

        // The complete, de-duplicated set of (bank, program) voices this style
        // can ever request — its initial per-channel setups plus every mid-track
        // program change.  Built once at load so the engine can pre-load them
        // all and make runtime program changes a cheap in-RAM index swap rather
        // than an audio-thread sample decode.
        std::vector<StyleVoiceSetup> voiceManifest;

        // The same, broken down per source MIDI channel (0..15).  Used to
        // pre-load only what each channel can actually switch to.
        std::array<std::vector<StyleVoiceSetup>, 16> channelManifest;

        // ── CHORDAL DESTINATIONS ─────────────────────────────────────────────
        // Indexed by RAW SFF destination channel 0..15.  True when the part
        // this style plays into that destination strikes notes TOGETHER rather
        // than one at a time - a piano left hand rather than a bass line.
        //
        // It exists for the BASS slot, which Grex defaults to Mono with a
        // 100 ms legato glide.  That is right for a bass line and wrong for a
        // chord: two notes landing on one mono voice collapse into a portamento
        // sweep across the interval between them, which on 4_Stroke (an
        // all-piano style whose "bass" is a two-voice left hand) came out as a
        // laser sound on every beat.
        //
        // Measured once at load, ORed across sections - see
        // computeChordalDestinations.  Per style, never per section: flipping a
        // slot's play mode when the user changes variation would be worse than
        // either answer on its own.
        std::array<bool, 16> destChordal {};

        StyleSectionData&       getSection (StyleSection s) noexcept
        {
            return sections[static_cast<size_t> (s)];
        }
        const StyleSectionData& getSection (StyleSection s) const noexcept
        {
            return sections[static_cast<size_t> (s)];
        }

        bool isLoaded() const noexcept { return ! events.empty(); }

        // Walk the initial voice setups + every program change (bank-aware) and
        // collect the distinct voices into voiceManifest.  Read-only over the
        // parsed data; call once after loading, on the message thread.
        void buildVoiceManifest()
        {
            voiceManifest.clear();
            for (auto& cm : channelManifest) cm.clear();

            auto addGlobal = [this] (int msb, int lsb, int prog)
            {
                if (prog < 0) return;
                for (const auto& v : voiceManifest)
                    if (v.bankMsb == msb && v.bankLsb == lsb && v.program == prog)
                        return;                       // already have it
                StyleVoiceSetup s;
                s.bankMsb = msb; s.bankLsb = lsb; s.program = prog;
                voiceManifest.push_back (s);
            };

            auto addChannel = [this] (int ch, int msb, int lsb, int prog)
            {
                if (prog < 0 || ch < 0 || ch >= 16) return;
                auto& cm = channelManifest[(size_t) ch];
                for (const auto& v : cm)
                    if (v.bankMsb == msb && v.bankLsb == lsb && v.program == prog)
                        return;
                StyleVoiceSetup s;
                s.bankMsb = msb; s.bankLsb = lsb; s.program = prog;
                cm.push_back (s);
            };

            // 1) Initial per-channel instruments.
            for (int ch = 0; ch < 16; ++ch)
            {
                const auto& v = voices[(size_t) ch];
                if (v.program >= 0)
                {
                    addGlobal  (v.bankMsb, v.bankLsb, v.program);
                    addChannel (ch, v.bankMsb, v.bankLsb, v.program);
                }
            }

            // 2) Mid-track program changes, tracking per-channel running bank
            //    select (events are stored in track order).
            int runMsb[16], runLsb[16];
            for (int i = 0; i < 16; ++i)
            {
                runMsb[i] = voices[(size_t) i].bankMsb;
                runLsb[i] = voices[(size_t) i].bankLsb;
            }
            for (const auto& e : events)
            {
                const int ch  = e.channel & 0x0F;
                const int cmd = e.status  & 0xF0;
                if (cmd == 0xB0)
                {
                    if      (e.data1 == 0)  runMsb[ch] = e.data2;
                    else if (e.data1 == 32) runLsb[ch] = e.data2;
                }
                else if (cmd == 0xC0)
                {
                    addGlobal  (runMsb[ch], runLsb[ch], (int) e.data1);
                    addChannel (ch, runMsb[ch], runLsb[ch], (int) e.data1);
                }
            }
        }
    };
} // namespace Betel
