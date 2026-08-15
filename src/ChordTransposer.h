#pragma once
//==============================================================================
// ChordTransposer.h
//
// Step 3 of the engine pipeline: chord-aware note transposition.
//
// Inputs at runtime:
//   • The set of MIDI notes the player is currently holding in the left hand
//     (chord-recognition zone).
//   • One CasmEntry per source channel (parsed by StyleLoader).
//   • A source note coming out of StyleSequencer's emitted-event stream.
//
// Output:
//   • Either a destination MIDI note to play (transposed), or "mute" if the
//     channel should not sound for the current chord / note class.
//
// What's implemented here:
//   • ChordRecognizer — 34 Yamaha chord types, root inference, inversion-aware.
//     Also exposes recognizeSingleFinger() for Yamaha's "1 Finger" mode.
//   • NoteTransposer — ROOT_TRANS / ROOT_FIXED NTR.  All eleven NTT codes
//     have real implementations: BYPASS, MELODY, CHORD, Melodic Minor (+5V),
//     Harmonic Minor (+5V), Natural Minor (+5V), Dorian (+5V).  The four
//     modal variants only activate when the destination chord is in the
//     minor family; over major / dom7 / aug / sus / dim / power chords they
//     defer to the MELODY table.  The Guitar NTR is still stubbed to
//     ROOT_TRANS — adequate for voice-leading-agnostic guitar parts.
//   • Chord-mute and note-mute bit decoders matching Yamaha's CASM byte layout.
//   • Zone selection delegates to CasmEntry::zoneForNote() (already in
//     StyleData.h).
//
// Conventions:
//   • All pitch classes are 0..11, with 0 = C, 1 = C#, ..., 11 = B.
//   • All MIDI notes are 0..127.
//   • The "source chord" comes from CasmEntry::srcChordRoot / srcChordType.
//     Yamaha defaults are root=C(0), type=Maj7(2), but each Ctb2 declares its
//     own; we honour the declared values.
//==============================================================================

#include <JuceHeader.h>
#include <atomic>
#include "StyleData.h"
#include <array>
#include <cstdint>
#include <vector>

namespace Betel
{
    //==========================================================================
    // ChordQuality — Yamaha's 34 chord types in canonical order.
    //
    // Bit layout in CASM's `chordMuteRaw` array — REVERSED byte order,
    // low-bit-first within each byte:
    //   byte 4 bit 0 = Maj, bit 1 = Maj6, bit 2 = Maj7, ..., bit 7 = aug
    //   byte 3 bit 0 = min, ...
    //   byte 0 bits 0..1 = 1+8, 1+2+5  (top of the enum; remaining bits unused)
    //
    // Verified EMPIRICALLY against multi-variant SFF2 files (80sDisco et al.):
    // a "min"-take mask of 00 00 07 FF 00 decodes to exactly the 11 minor-
    // family qualities (min..dim7, enum 8..18) ONLY under this layout, and its
    // sibling "Maj" mask to precisely the complementary 23 — a format-perfect
    // partition that rules out the straight byte order (which scattered both
    // masks across unrelated families and mis-picked variants on minor and
    // dominant chords).
    //
    // We keep this enum in EXACTLY the canonical order so the enum value
    // doubles as the bit index (chordMaskBit maps it into the reversed bytes).
    //==========================================================================
    enum class ChordQuality : uint8_t
    {
        Maj          =  0,
        Maj6         =  1,
        Maj7         =  2,
        Maj7s11      =  3,   // Maj7#11
        MajAdd9      =  4,
        Maj7_9       =  5,
        Maj6_9       =  6,
        Aug          =  7,

        Min          =  8,
        Min6         =  9,
        Min7         = 10,
        Min7b5       = 11,
        MinAdd9      = 12,
        Min7_9       = 13,
        Min7_11      = 14,
        MinMaj7      = 15,

        MinMaj7_9    = 16,
        Dim          = 17,
        Dim7         = 18,
        Dom7         = 19,   // dominant 7
        Dom7sus4     = 20,
        Dom7b5       = 21,
        Dom7_9       = 22,
        Dom7s11      = 23,   // 7#11

        Dom7_13      = 24,
        Dom7b9       = 25,
        Dom7b13      = 26,
        Dom7s9       = 27,   // 7#9
        Maj7Aug      = 28,
        Dom7Aug      = 29,
        OnePlus8     = 30,
        OnePlus5     = 31,

        Sus4         = 32,
        OnePlus2Plus5 = 33,  // sus2-like

        Unknown      = 0xFF
    };

    inline constexpr int kNumChordQualities = 34;

    struct Chord
    {
        int          root    = 0;                  // 0..11
        ChordQuality quality = ChordQuality::Maj;

        bool operator==(const Chord& o) const noexcept
            { return root == o.root && quality == o.quality; }
    };

    //==========================================================================
    // Chord interval templates.
    //
    // For each ChordQuality we declare a 12-bit bitmask: bit k set means the
    // pitch class k (semitones above root) belongs to that chord.
    //
    // These are used (a) by ChordRecognizer to match held notes to a quality
    // and (b) by NoteTransposer's MELODY/CHORD mode to decide which source
    // intervals are scale tones for the dest chord.
    //==========================================================================
    namespace detail
    {
        constexpr uint16_t mask(std::initializer_list<int> intervals) noexcept
        {
            uint16_t r = 0;
            for (int i : intervals)
                if (i >= 0 && i < 12) r |= (uint16_t) (1u << i);
            return r;
        }

        // Required intervals (must be present for recognition)
        inline constexpr uint16_t kChordRequired[kNumChordQualities] = {
            /* Maj            */ mask({0, 4, 7}),
            /* Maj6           */ mask({0, 4, 7, 9}),
            /* Maj7           */ mask({0, 4, 7, 11}),
            /* Maj7#11        */ mask({0, 4, 6, 11}),
            /* MajAdd9        */ mask({0, 2, 4, 7}),
            /* Maj7(9)        */ mask({0, 2, 4, 7, 11}),
            /* Maj6(9)        */ mask({0, 2, 4, 7, 9}),
            /* Aug            */ mask({0, 4, 8}),

            /* Min            */ mask({0, 3, 7}),
            /* Min6           */ mask({0, 3, 7, 9}),
            /* Min7           */ mask({0, 3, 7, 10}),
            /* Min7b5         */ mask({0, 3, 6, 10}),
            /* MinAdd9        */ mask({0, 2, 3, 7}),
            /* Min7(9)        */ mask({0, 2, 3, 7, 10}),
            /* Min7(11)       */ mask({0, 3, 5, 7, 10}),
            /* MinMaj7        */ mask({0, 3, 7, 11}),

            /* MinMaj7(9)     */ mask({0, 2, 3, 7, 11}),
            /* Dim            */ mask({0, 3, 6}),
            /* Dim7           */ mask({0, 3, 6, 9}),
            /* Dom7           */ mask({0, 4, 7, 10}),
            /* Dom7sus4       */ mask({0, 5, 7, 10}),
            /* Dom7b5         */ mask({0, 4, 6, 10}),
            /* Dom7(9)        */ mask({0, 2, 4, 7, 10}),
            /* Dom7#11        */ mask({0, 4, 6, 7, 10}),

            /* Dom7(13)       */ mask({0, 4, 7, 9, 10}),
            /* Dom7b9         */ mask({0, 1, 4, 7, 10}),
            /* Dom7b13        */ mask({0, 4, 7, 8, 10}),
            /* Dom7#9         */ mask({0, 3, 4, 7, 10}),
            /* Maj7Aug        */ mask({0, 4, 8, 11}),
            /* Dom7Aug        */ mask({0, 4, 8, 10}),
            /* 1+8            */ mask({0}),
            /* 1+5            */ mask({0, 7}),

            /* Sus4           */ mask({0, 5, 7}),
            /* 1+2+5          */ mask({0, 2, 7}),
        };

        // Number of "characteristic" tones — higher means more specific match.
        // Used as a tiebreaker so e.g. Maj7(9) is preferred over Maj7 when the
        // 9th is present.
        inline constexpr int kChordSpecificity[kNumChordQualities] = {
            3,4,4,4,4,5,5,3,
            3,4,4,4,4,5,5,4,
            5,3,4,4,4,4,5,5,
            5,5,5,5,4,4,1,2,
            3,3
        };
    }

    //==========================================================================
    // ChordRecognizer — turns a vector of MIDI notes into (root, quality).
    //==========================================================================
    class ChordRecognizer
    {
    public:
        /** Identify the chord implied by a set of held MIDI notes.
            The lowest note's pitch class is the preferred bass / root hint,
            but the algorithm picks the (root, quality) combination that best
            matches the full pitch-class set with the highest specificity. */
        static Chord recognize (const std::vector<int>& notes)
        {
            if (notes.empty()) return { 0, ChordQuality::Unknown };

            // Build the pitch-class set (12-bit mask of unique notes)
            uint16_t pcSet = 0;
            int lowestNote = 127;
            for (int n : notes)
            {
                if (n < 0 || n > 127) continue;
                pcSet |= (uint16_t) (1u << pitchClass(n));
                if (n < lowestNote) lowestNote = n;
            }
            if (pcSet == 0) return { 0, ChordQuality::Unknown };

            const int bassPC = pitchClass(lowestNote);

            // Score every (root, quality) candidate. A candidate is VALID when
            // (rotated pcSet) & required == required. Among valid ones, prefer:
            //   1. Highest specificity (most chord tones present)
            //   2. Root matches the bass (musical convention)
            //   3. Smaller delta between actual pcSet and required (fewer extras)
            int bestScore = -1;
            Chord best { bassPC, ChordQuality::Maj };

            for (int root = 0; root < 12; ++root)
            {
                const uint16_t rotated = rotateRight (pcSet, root);

                for (int q = 0; q < kNumChordQualities; ++q)
                {
                    const uint16_t req = detail::kChordRequired[q];
                    if ((rotated & req) != req) continue;        // required not satisfied

                    int score = detail::kChordSpecificity[q] * 100;
                    if (root == bassPC) score += 30;             // bass-equals-root bonus
                    score -= popcount16 (rotated & ~req);        // penalty per extra note

                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = { root, (ChordQuality) q };
                    }
                }
            }
            return best;
        }

        /** Just compute the pitch-class set without identifying. Useful for
            quick filters in the chord-recognition zone. */
        static uint16_t pitchClassSet (const std::vector<int>& notes)
        {
            uint16_t s = 0;
            for (int n : notes)
                if (n >= 0 && n < 128) s |= (uint16_t) (1u << pitchClass(n));
            return s;
        }

        /** Yamaha "Single Finger" / "1 Finger" mode — MAJORS and MINORS only.
              * 1 key alone                          → Major (key = root)
              * Root + ANY single key to its left     → Minor
            The HIGHEST held note is the chord root.  (We intentionally do NOT
            produce 7th/min7 variants in one-finger mode; the player wants just
            major vs minor here.  Fingered mode covers the full chord set.) */
        static Chord recognizeSingleFinger (const std::vector<int>& notes)
        {
            if (notes.empty()) return { 0, ChordQuality::Unknown };

            int rootMidi = -1;
            for (int n : notes)
                if (n > rootMidi && n >= 0 && n <= 127) rootMidi = n;
            if (rootMidi < 0) return { 0, ChordQuality::Unknown };

            const int root = pitchClass (rootMidi);

            // Any key held below the root → minor; root alone → major.
            for (int n : notes)
                if (n >= 0 && n < rootMidi)
                    return { root, ChordQuality::Min };

            return { root, ChordQuality::Maj };
        }

    private:
        static int pitchClass (int midi) noexcept
            { return ((midi % 12) + 12) % 12; }
        static bool isBlackKey (int pc) noexcept
        {
            // C#, D#, F#, G#, A#
            return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        }
        static uint16_t rotateRight (uint16_t v, int n) noexcept
        {
            n = ((n % 12) + 12) % 12;
            return (uint16_t) (((v >> n) | (v << (12 - n))) & 0x0FFFu);
        }
        static int popcount16 (uint16_t v) noexcept
        {
            int c = 0;
            while (v) { c += (v & 1u); v >>= 1; }
            return c;
        }
    };

    //==========================================================================
    // NoteTransposer — apply NTR + NTT to a single source note.
    //==========================================================================
    class NoteTransposer
    {
    public:
        struct Result
        {
            bool shouldPlay = true;
            int  destNote   = 0;     // valid only if shouldPlay is true
        };

        /** Apply the channel's transposition rules to convert a source MIDI
            note (as written in the style file) into a destination MIDI note
            for the currently-played chord.

            Pre-conditions:
              • casm.zoneForNote(sourceNote) returns the right zone for the note
              • destChord is the player's current chord
            Post-conditions:
              • If the channel should mute for this chord or this note class,
                result.shouldPlay is false
              • Otherwise result.destNote is a transposed MIDI note in 0..127
                (clamped). */
        static Result apply (int sourceNote,
                             const Chord& destChord,
                             const CasmEntry& casm)
        {
            Result out;
            out.shouldPlay = false;

            if (sourceNote < 0 || sourceNote > 127) return out;

            // 1) Chord-mute check (this CASM channel mutes for certain chord
            //    types — used by multi-source-channel style files to dispatch
            //    one channel per chord quality).
            if (! chordAllowed (destChord.quality, casm)) return out;

            // 2) Note-mute check (12-bit "which note classes this channel plays")
            if (! casm.noteAllowed (sourceNote)) return out;

            // 3) Resolve the right zone for this source note
            const CasmZone& zone = casm.zoneForNote (sourceNote);

            const int srcRoot = ((int) casm.srcChordRoot) & 0x0F;
            const int srcPC   = ((sourceNote % 12) + 12) % 12;
            const int srcRel  = ((srcPC - srcRoot) + 12) % 12;     // PC relative to source root
            const int octave  = sourceNote / 12;                   // raw octave (0..10)

            // 4) Apply NTR (Note Transposition Rule)
            int destPC;
            int guitarNote = -1;   // >= 0 -> GUITAR NTR produced an ABSOLUTE pitch

            // NOTE ON ORDER: GUITAR must be the FIRST case.  ROOT_FIXED below
            // deliberately falls THROUGH to ROOT_TRANS, and if GUITAR sat between
            // them it would swallow that fallthrough.
            switch (zone.ntr)
            {
                case 2:  // ─────────────────────────── GUITAR ──────────────────
                {
                    // A guitar strum is a VOICING, not a melody.
                    //
                    // Yamaha records guitar parts as a fingering template: the
                    // source note picks WHICH STRING is struck (the strum dynamics
                    // live in the velocities), and the engine re-voices those onto
                    // the destination chord.  They are NOT literal pitches.
                    //
                    // Proof, from CountryRock_T151's Tele channel: every strum is
                    // the same four notes — F4 G4 A4 B4.  Four consecutive scale
                    // degrees; a cluster no guitarist can play.  Running that
                    // through the ordinary NTT tables (which is what NTR=Guitar
                    // used to fall through to) PRESERVES the cluster and merely
                    // re-roots it onto every chord: G7 came out as C-D-E-F.  That
                    // is the "disharmonic strumming" symptom, on all four Mains.
                    //
                    // The NTT code means something ELSE here too.  Under Guitar
                    // NTR the SFF spec redefines it — the loader already knows
                    // (kNTTGuitarNames = AllPurpose / Stroke / Arpeggio) — so
                    // codes 0/1/2 are NOT Bypass/Melody/Chord, and feeding them to
                    // applyNTT was simply reading the wrong table.  All three modes
                    // are voicing rules; what separates a stroke from an arpeggio
                    // is the pattern's RHYTHM, which we already play verbatim.
                    //
                    // So: shift by the SHORTEST root interval, then snap every note
                    // onto a tone of the destination chord.  Anything that sounds
                    // is a chord tone by construction — a cluster cannot survive —
                    // while the voicing keeps its spread, contour and register.
                    const int shift = shortestRootShift (srcRoot, destChord.root);
                    guitarNote = snapToChordTone (sourceNote + shift, destChord);
                    destPC     = ((guitarNote % 12) + 12) % 12;
                    break;
                }

                case 1:  // ROOT_FIXED
                    // Bypass-NTT parts (drums, truly fixed elements) genuinely
                    // do not transpose — a kick is a kick on every chord — so
                    // keep the source note untouched.
                    if (zone.nttCode() == 0)
                    {
                        out.shouldPlay = true;
                        out.destNote   = juce::jlimit (0, 127, sourceNote);
                        return out;
                    }
                    // But a CHORDAL Root-Fixed part (strings/guitar pads, etc.,
                    // NTT = Chord/Melody) must still follow the chord.  In SFF
                    // "Root Fixed" only means "stay in a tight register, don't
                    // leap octaves" — which the ±6-semitone snap below already
                    // does — NOT "ignore the chord".  Fall through and map it to
                    // the chord exactly like Root-Transpose.
                    [[fallthrough]];

                case 0:  // ROOT_TRANS — standard
                default:
                {
                    // 5) Apply NTT (Note Transposition Table) on the source-
                    //    relative pitch class
                    const int destRel = applyNTT (srcRel, destChord.quality, zone.nttCode());

                    // A negative result is the NTT's "drop this note" signal —
                    // e.g. a source major-7th held over a plain triad / sus
                    // chord, which would clash a semitone under the root.
                    if (destRel < 0)
                        return out;          // out.shouldPlay stays false

                    // 6) Re-root to the destination chord
                    destPC = ((destChord.root + destRel) % 12 + 12) % 12;
                    break;
                }
            }

            // 7) Octave reconstruction: keep the note in the same octave as the
            //    original source note's class (so a source C4 transposed up a
            //    fifth ends up at G4, not G3 or G5).
            int finalNote;
            if (guitarNote >= 0)
            {
                // GUITAR NTR already produced an absolute pitch, chosen to sit in
                // the guitar's register.  Re-snapping it against the SOURCE note
                // would undo that — the source note is a string selector, not a
                // pitch, so "stay within ±6 of it" is meaningless here.
                finalNote = guitarNote;
            }
            else
            {
                const int baseOctaveNote = octave * 12 + destPC;
                finalNote = baseOctaveNote;
                // Bring it close to the source note (within ±6 semitones) so the
                // melody stays in register rather than jumping octaves.
                while (finalNote - sourceNote >  6) finalNote -= 12;
                while (sourceNote - finalNote >  6) finalNote += 12;
            }

            // 8) Bass-range folding.  For bass-on zones (the CASM bass flag),
            //    fold the result into the configured effective bass register
            //    by octaves so transposition never throws the bass line into
            //    a muddy-low or thin-high octave.  This is what keeps the
            //    bass sounding good across every chord the player lands on.
            //    The bass channel is *identified* at style-load time (the
            //    isBassOn flag lives in the parsed CASM); the actual fold has
            //    to happen here because the final pitch depends on the live
            //    chord.
            if (zone.isBassOn())
            {
                const int lo = sBassLow.load();
                const int hi = sBassHigh.load();
                if (hi >= lo)
                {
                    while (finalNote > hi) finalNote -= 12;
                    while (finalNote < lo) finalNote += 12;
                    // If the window is narrower than the note can fit, clamp.
                    finalNote = juce::jlimit (lo, hi, finalNote);
                }
            }

            out.shouldPlay = true;
            out.destNote   = juce::jlimit (0, 127, finalNote);
            return out;
        }

        // ── Effective bass range (folded into by bass-on zones) ───────────────
        // Written from the message thread (UI / style-load), read from the
        // audio thread.  Defaults to E1..E3 — a comfortable electric/acoustic
        // bass register that keeps arranger bass lines tight and present.
        static void setBassRange (int lowMidi, int highMidi) noexcept
        {
            sBassLow .store (juce::jlimit (0, 127, lowMidi));
            sBassHigh.store (juce::jlimit (0, 127, highMidi));
        }
        static int getBassLow()  noexcept { return sBassLow.load(); }
        static int getBassHigh() noexcept { return sBassHigh.load(); }

        //----------------------------------------------------------------------
        // Chord-mute / note-mute helpers
        //----------------------------------------------------------------------

        /** Returns true if the given chord quality is allowed (bit set = 1)
            in the CasmEntry's chordMuteRaw bitmap. */
        static bool chordAllowed (ChordQuality /*q*/, const CasmEntry& /*casm*/) noexcept
        {
            // Per-chord-quality channel muting is DISABLED.  The CASM chord-mute
            // mask is a multi-source-channel dispatch feature (one source channel
            // per chord quality), but in practice it made instrument channels
            // drop out whenever the recognised quality wasn't in a channel's mask
            // — e.g. the player holds a 7th or a 9th and a backing part goes
            // silent.  The desired behaviour is sample-and-hold: once a chord is
            // recognised, every channel keeps playing (transposed) until the next
            // chord.  So always allow; note-class muting (noteAllowed) still
            // applies for per-note pattern shaping.
            return true;
        }

        /** CASM chord-mute bit for a quality — the FINGERED-mode variant
            selector's data source (computeBetelgeuseFingeredPlayers in
            StyleLoader.cpp reads it at load time to fill each entry's
            firesForFingeredQuality table).  chordAllowed above stays disabled:
            per-quality PLAYBACK muting of a single-source channel is a
            different feature from picking WHICH variant of a multi-source
            destination fires, which is what this bit drives. */
        static bool chordMaskBit (ChordQuality q, const CasmEntry& casm) noexcept
        {
            const int bitIdx = (int) q;
            if (bitIdx < 0 || bitIdx >= 40) return true;
            // REVERSED byte order — byte 4 holds qualities 0..7.  See the
            // layout note (and its empirical verification) on the
            // ChordQuality enum above.
            const int byteIdx  = 4 - (bitIdx >> 3);
            const int innerBit = bitIdx & 7;
            return ((casm.chordMuteRaw[(size_t) byteIdx] >> innerBit) & 1u) != 0u;
        }

        //----------------------------------------------------------------------
        // Multi-source chord-quality bucketing + family affinity
        //----------------------------------------------------------------------
        //
        // These helpers support the multi-source variant selector (a style that
        // ships more than one source recording per destination — a Maj7 take, a
        // Min take, etc.).  The recognizer can output any of 34 fingered
        // qualities; betelgeuseQualityBucket() folds them down to one of 8
        // coarse buckets (Maj / Min / Maj7 / Min7 / Dom7 / Dim / Aug / Sus4),
        // and srcFamilyOf() + srcFamilyAffinityScore() (below) score how well a
        // variant's source chord fits each bucket.  StyleLoader uses these at
        // parse time to pick the winning variant per dst per bucket; the
        // dispatch then fires only that variant for the held chord.  The actual
        // note transposition is always apply() (full NTR / NTT).

        /** Map a recognized ChordQuality (34 distinct qualities) to one of 8
            buckets (Maj / Min / Maj7 / Min7 / Dom7 / Dim / Aug / Sus4), used to
            pick the multi-source variant for the held chord.  Defaults to Maj
            for Unknown / corner cases. */
        static int betelgeuseQualityBucket (ChordQuality q) noexcept
        {
            using Q = ChordQuality;
            switch (q)
            {
                // Maj-family triads + add9 / 6 variants + power-chord-ish stubs.
                case Q::Maj:       case Q::Maj6:      case Q::MajAdd9:
                case Q::Maj6_9:    case Q::OnePlus8:  case Q::OnePlus5:
                case Q::OnePlus2Plus5:
                    return 0;   // Maj bucket

                // Min-family triads + add9 / 6 variants.  Min7b5 is technically
                // half-diminished but the recognizer doesn't expose enough to
                // disambiguate musically — treat as Min variant.
                case Q::Min:       case Q::Min6:      case Q::MinAdd9:
                case Q::Min7b5:
                    return 1;   // Min bucket

                // Maj7-family (M7 in chord).
                case Q::Maj7:      case Q::Maj7s11:   case Q::Maj7_9:
                case Q::Maj7Aug:
                    return 2;   // Maj7 bucket

                // m7-family + min/maj7 variants (have a 7th but minor 3rd).
                case Q::Min7:      case Q::Min7_9:    case Q::Min7_11:
                case Q::MinMaj7:   case Q::MinMaj7_9:
                    return 3;   // Min7 bucket

                // Dominant-7 family — every variant with a major 3rd + m7.
                case Q::Dom7:      case Q::Dom7sus4:  case Q::Dom7b5:
                case Q::Dom7_9:    case Q::Dom7s11:   case Q::Dom7_13:
                case Q::Dom7b9:    case Q::Dom7b13:   case Q::Dom7s9:
                case Q::Dom7Aug:
                    return 4;   // Dom7 bucket

                // Diminished.
                case Q::Dim:       case Q::Dim7:
                    return 5;   // Dim bucket

                // Augmented (Aug + AugMaj7Aug already captured in their families
                // above — only the bare Aug enum lands here).
                case Q::Aug:
                    return 6;   // Aug bucket

                // Sus4.
                case Q::Sus4:
                    return 7;   // Sus4 bucket

                // Defensive default: Unknown → Maj (so the style doesn't go
                // silent when chord recognition flickers between presses).
                case Q::Unknown:
                default:
                    return 0;
            }
        }

        //----------------------------------------------------------------------
        // Source-family classification + affinity scoring (multi-source picker)
        //----------------------------------------------------------------------
        //
        // For files that ship multiple source variants per dst (Maj7 take,
        // Dim take, Dom7 take, ...) the Betelgeuse dispatch needs to pick
        // ONE variant per held chord — the file author's intended match.
        // Classify each variant's source chord into a coarse "family", score
        // its affinity for each held chord-quality bucket, and pick the
        // highest-scoring variant per dst at style-load time.

        enum class SrcFamily : uint8_t
        {
            Maj = 0,   // Maj, Maj6, Maj7, Maj7Aug, Maj-add9, etc.
            Aug = 1,   // Aug triad
            Min = 2,   // Min, Min6, Min7, MinMaj7, MinAdd9, etc.
            Dim = 3,   // Dim, Dim7, m7b5 (half-dim)
            Dom7 = 4,  // Dom7 + all its altered flavours
            Sus4 = 5,  // Sus4, 7sus4
            Power = 6  // 1+8, 1+5, 1+2+5 (root-only / power chords)
        };

        /** Bucket a source ChordQuality into one of 7 families.  Mirrors the
            Yamaha SFF file-byte mapping (the enum values are identical to
            the spec's chord-type byte). */
        static SrcFamily srcFamilyOf (ChordQuality srcType) noexcept
        {
            using Q = ChordQuality;
            switch (srcType)
            {
                case Q::Maj:       case Q::Maj6:      case Q::Maj7:
                case Q::Maj7s11:   case Q::MajAdd9:   case Q::Maj7_9:
                case Q::Maj6_9:    case Q::Maj7Aug:
                    return SrcFamily::Maj;

                case Q::Aug:
                    return SrcFamily::Aug;

                case Q::Min:       case Q::Min6:      case Q::Min7:
                case Q::MinAdd9:   case Q::Min7_9:    case Q::Min7_11:
                case Q::MinMaj7:   case Q::MinMaj7_9:
                    return SrcFamily::Min;

                case Q::Min7b5:    case Q::Dim:       case Q::Dim7:
                    return SrcFamily::Dim;

                case Q::Dom7:      case Q::Dom7sus4:  case Q::Dom7b5:
                case Q::Dom7_9:    case Q::Dom7s11:   case Q::Dom7_13:
                case Q::Dom7b9:    case Q::Dom7b13:   case Q::Dom7s9:
                case Q::Dom7Aug:
                    return SrcFamily::Dom7;

                case Q::Sus4:      case Q::OnePlus2Plus5:
                    return SrcFamily::Sus4;

                case Q::OnePlus8:  case Q::OnePlus5:
                    return SrcFamily::Power;

                case Q::Unknown:
                default:
                    return SrcFamily::Maj;   // safe fallback
            }
        }

        /** Score a source family's affinity for a destination bucket.  Higher
            = better match.  When a section has multiple variants targeting
            one dst, the variant with the highest score for the held bucket
            fires; the rest stay silent.

            Hand-tuned table — exact family match scores 100, similar-shape
            matches (same 3rd quality / same 7th presence) score 60-95, the
            generic Maj fallback scores 30-65 depending on how far from Maj
            the target is, and Power chords get a universal 80 (they fit
            everything because they're just root + 5th). */
        static int srcFamilyAffinityScore (SrcFamily fam, int bucket) noexcept
        {
            // Row-major: 7 families × 8 buckets.
            //                       held: Maj  Min  Maj7 Min7 Dom7 Dim  Aug  Sus4
            static constexpr int8_t T[7][8] =
            {
                /* src Maj  */     { 100,  40,  95,  40,  70,  30,  75,  65 },
                /* src Aug  */     {  60,  20,  70,  20,  55,  15, 100,  25 },
                /* src Min  */     {  40, 100,  45,  95,  45,  75,  25,  35 },
                /* src Dim  */     {  25,  75,  25,  60,  35, 100,  15,  25 },
                /* src Dom7 */     {  65,  40,  65,  50, 100,  35,  45,  65 },
                /* src Sus4 */     {  65,  35,  55,  35,  65,  25,  35, 100 },
                /* src Power*/     {  80,  80,  80,  80,  80,  80,  80,  80 },
            };
            const int f = (int) fam;
            if ((unsigned) f >= 7u || (unsigned) bucket >= 8u) return 0;
            return (int) T[f][bucket];
        }

        static Result applyAuto (int sourceNote,
                                 const Chord& destChord,
                                 const CasmEntry& casm) noexcept
        {
            // UNIFIED PATH: transposition is the SFF-faithful path (apply() —
            // full NTR/NTT).  Honouring the file's real NTT is what lets a comp
            // part's NTT=Chord snap to chord tones (dropping passing notes that
            // otherwise clashed on minor chords), while NTT=Melody / Bypass /
            // Guitar parts keep behaving as the style authored them.  Kept as a
            // thin single entry point for the dispatch; there is one path now.
            return apply (sourceNote, destChord, casm);
        }

        /** NTT — convert a source-root-relative pitch class to a dest-root-
            relative pitch class for the given destination chord quality.
            srcRel is 0..11, return is 0..11.

            The 11 NTT codes (matching kNTTNames in StyleLoader):
              0  Bypass        — identity passthrough
              1  Melody        — major-mode default, full table per chord family
              2  Chord         — chord-tone NTT (uses melody table; refined if needed)
              3  MelMinor      ─┐
              4  MelMinor5V     │ scale = 1 2 b3 4 5 6 7  (flat 3 only)
              5  HarMinor      ─┐
              6  HarMinor5V     │ scale = 1 2 b3 4 5 b6 7 (flat 3 + flat 6)
              7  NatMinor      ─┐
              8  NatMinor5V     │ scale = 1 2 b3 4 5 b6 b7 (all flatted)
              9  Dorian        ─┐
              10 Dorian5V       │ scale = 1 2 b3 4 5 6 b7 (flat 3 + flat 7)

            The "5V" variants (Melodic Minor 5th-Variation, etc.) refer to the
            SOURCE-side chord-root reference being the 5th rather than the
            root — the scale logic itself is identical, so they map to the
            same dispatch in applyNTT.

            For destination chords NOT in the minor family (Major / Dom7 /
            Aug / Sus / Power / Dim / HalfDim), all modal modes fall back to
            the MELODY table since the modal flips only make musical sense
            over minor-tonic harmony.
        */
        //======================================================================
        //  GUITAR NTR helpers
        //======================================================================

        /** Shortest signed interval from the source root to the destination root,
            in -6..+5.  A strum must move by the SHORTEST path: taking C -> A the
            long way (+9) would drag the whole voicing up out of the guitar's
            register, where a player would simply reach DOWN 3 semitones instead. */
        static int shortestRootShift (int srcRoot, int destRoot) noexcept
        {
            return ((((destRoot - srcRoot) % 12) + 12 + 6) % 12) - 6;
        }

        /** 12-bit mask of a chord's tones, as semitones above its root.
            Bit N set => N semitones above the root is a chord tone. */
        static uint16_t chordToneMask (ChordQuality q) noexcept
        {
            auto M = [] (std::initializer_list<int> tones) -> uint16_t
            {
                uint16_t m = 0;
                for (int t : tones)
                    m |= (uint16_t) (1u << ((((t % 12) + 12) % 12)));
                return m;
            };

            switch (q)
            {
                case ChordQuality::Maj:           return M ({ 0, 4, 7 });
                case ChordQuality::Maj6:          return M ({ 0, 4, 7, 9 });
                case ChordQuality::Maj7:          return M ({ 0, 4, 7, 11 });
                case ChordQuality::Maj7s11:       return M ({ 0, 4, 6, 7, 11 });
                case ChordQuality::MajAdd9:       return M ({ 0, 2, 4, 7 });
                case ChordQuality::Maj7_9:        return M ({ 0, 2, 4, 7, 11 });
                case ChordQuality::Maj6_9:        return M ({ 0, 2, 4, 7, 9 });
                case ChordQuality::Aug:           return M ({ 0, 4, 8 });

                case ChordQuality::Min:           return M ({ 0, 3, 7 });
                case ChordQuality::Min6:          return M ({ 0, 3, 7, 9 });
                case ChordQuality::Min7:          return M ({ 0, 3, 7, 10 });
                case ChordQuality::Min7b5:        return M ({ 0, 3, 6, 10 });
                case ChordQuality::MinAdd9:       return M ({ 0, 2, 3, 7 });
                case ChordQuality::Min7_9:        return M ({ 0, 2, 3, 7, 10 });
                case ChordQuality::Min7_11:       return M ({ 0, 3, 5, 7, 10 });
                case ChordQuality::MinMaj7:       return M ({ 0, 3, 7, 11 });
                case ChordQuality::MinMaj7_9:     return M ({ 0, 2, 3, 7, 11 });

                case ChordQuality::Dim:           return M ({ 0, 3, 6 });
                case ChordQuality::Dim7:          return M ({ 0, 3, 6, 9 });

                case ChordQuality::Dom7:          return M ({ 0, 4, 7, 10 });
                case ChordQuality::Dom7sus4:      return M ({ 0, 5, 7, 10 });
                case ChordQuality::Dom7b5:        return M ({ 0, 4, 6, 10 });
                case ChordQuality::Dom7_9:        return M ({ 0, 2, 4, 7, 10 });
                case ChordQuality::Dom7s11:       return M ({ 0, 4, 6, 7, 10 });
                case ChordQuality::Dom7_13:       return M ({ 0, 4, 7, 9, 10 });
                case ChordQuality::Dom7b9:        return M ({ 0, 1, 4, 7, 10 });
                case ChordQuality::Dom7b13:       return M ({ 0, 4, 7, 8, 10 });
                case ChordQuality::Dom7s9:        return M ({ 0, 3, 4, 7, 10 });
                case ChordQuality::Maj7Aug:       return M ({ 0, 4, 8, 11 });
                case ChordQuality::Dom7Aug:       return M ({ 0, 4, 8, 10 });

                case ChordQuality::OnePlus8:      return M ({ 0 });          // root only
                case ChordQuality::OnePlus5:      return M ({ 0, 7 });       // power chord
                case ChordQuality::Sus4:          return M ({ 0, 5, 7 });
                case ChordQuality::OnePlus2Plus5: return M ({ 0, 2, 7 });    // sus2

                case ChordQuality::Unknown:
                default:                          return M ({ 0, 4, 7 });    // safe major triad
            }
        }

        /** Snap an absolute MIDI note to the nearest tone of the destination
            chord.  This is what makes the GUITAR path safe: whatever the source
            template contained, every note that reaches the engine is a chord tone,
            so a cluster is impossible by construction.

            Ties resolve DOWNWARD — a guitar voicing sits under the melody, and
            reaching down is what a player does.  Any pitch class is at most 6
            semitones from some chord tone, so the search always terminates. */
        static int snapToChordTone (int note, const Chord& c) noexcept
        {
            const uint16_t mask = chordToneMask (c.quality);
            if (mask == 0) return juce::jlimit (0, 127, note);

            for (int d = 0; d <= 6; ++d)
            {
                for (int dir = 0; dir < 2; ++dir)
                {
                    const int cand = note + (dir == 0 ? -d : d);   // down first => ties go down
                    if (cand < 0 || cand > 127) continue;

                    const int rel = ((((cand - c.root) % 12) + 12) % 12);
                    if (mask & (uint16_t) (1u << rel))
                        return cand;

                    if (d == 0) break;      // -0 and +0 are the same note
                }
            }
            return juce::jlimit (0, 127, note);   // unreachable with a non-zero mask
        }

        static int applyNTT (int srcRel, ChordQuality destQ, uint8_t nttCode)
        {
            srcRel = ((srcRel % 12) + 12) % 12;

            // Yamaha's table order, confirmed against the published SFF/CASM
            // reference:
            //
            //    0 BYPASS      1 MELODY      2 CHORD       3 BASS
            //    4 MELODIC MINOR       5 MELODIC MINOR 5th Var.
            //    6 HARMONIC MINOR      7 HARMONIC MINOR 5th Var.
            //    8 NATURAL MINOR       9 NATURAL MINOR 5th Var.
            //   10 DORIAN             11 DORIAN 5th Var.
            //
            // Each mode occupies an EVEN code and its 5th-variant the odd code
            // ABOVE it.  The previous table paired them the other way round —
            // (3,4) MelodicMinor, (5,6) HarmonicMinor, (7,8) NaturalMinor,
            // (9,10) Dorian — which put every odd code on the mode BELOW the
            // one the style asked for, and left 3 (BASS) and 11 (Dorian 5th)
            // wrong as well.  The even codes happened to land right, which is
            // why it survived: styles using plain MELODIC/HARMONIC/NATURAL
            // MINOR played correctly and only the 5th variants misbehaved.
            //
            // The cost was real.  A Genos-era style measured here put NTT 5
            // (Melodic Minor 5th) on 53 of its ~112 melodic zones — about half
            // the file — and every one of them rendered as HARMONIC minor,
            // flatting the 6th on minor chords when the style had asked for the
            // 3rd alone.  Wrong note, not merely wrong flavour.  Older PSR-S950
            // and Tyros 5 styles use only codes 0/1/2 and were never affected,
            // which is why this only surfaced when Genos styles arrived.
            switch (nttCode)
            {
                case 0:                                       // BYPASS — identity
                    return srcRel;

                case 1:                                       // MELODY — keep
                    return melodyTable (srcRel, destQ);       // passing tones

                case 2:                                       // CHORD — snap to
                    return chordTable (srcRel, destQ);        // chord tones, drop rest

                // BASS.  The spec describes it as the MELODY table that also
                // honours on-bass chords, and notes newer models simply replaced
                // it WITH Melody.  The on-bass part is not this function's job —
                // it rides the separate 0x80 bass-on flag (CasmZone::isBassOn),
                // so Melody is the whole of the behaviour here.
                case 3:
                    return melodyTable (srcRel, destQ);

                case 4:                                       // MELODIC MINOR
                    return modalMinorTable (srcRel, destQ, ModalKind::MelodicMinor,  false);
                case 5:                                       //   ...5th Var.
                    return modalMinorTable (srcRel, destQ, ModalKind::MelodicMinor,  true);

                case 6:                                       // HARMONIC MINOR
                    return modalMinorTable (srcRel, destQ, ModalKind::HarmonicMinor, false);
                case 7:                                       //   ...5th Var.
                    return modalMinorTable (srcRel, destQ, ModalKind::HarmonicMinor, true);

                case 8:                                       // NATURAL MINOR
                    return modalMinorTable (srcRel, destQ, ModalKind::NaturalMinor,  false);
                case 9:                                       //   ...5th Var.
                    return modalMinorTable (srcRel, destQ, ModalKind::NaturalMinor,  true);

                case 10:                                      // DORIAN
                    return modalMinorTable (srcRel, destQ, ModalKind::Dorian,        false);
                case 11:                                      //   ...5th Var.
                    return modalMinorTable (srcRel, destQ, ModalKind::Dorian,        true);

                default:                                      // unknown — safe fallback
                    return melodyTable (srcRel, destQ);
            }
        }

    private:
        enum class ModalKind { MelodicMinor, HarmonicMinor, NaturalMinor, Dorian };

        /** Modal NTT variants — only meaningful when the destination chord is
            in the Minor family.  Each mode differs in which of the {3rd,
            6th, 7th} scale degrees are flatted:

                              3rd    6th    7th
              MelodicMinor  : b3     6      7        (asc. melodic minor)
              HarmonicMinor : b3    b6      7        (Spanish / flamenco)
              NaturalMinor  : b3    b6     b7        (Aeolian)
              Dorian        : b3     6     b7        (jazz / modal)

            For non-minor destinations we defer to the MELODY table since the
            mode only colours minor harmony — over a major or dom7 chord, the
            mode flips would clash with the chord's own quality.  The one
            exception is the 5th VARIANT, below.

            fifthVar selects the "5th Var." form of each table (NTT codes 5, 7,
            9 and 11).  The spec defines it as the plain table PLUS: augmented
            and diminished chords additionally affect the 5th note of the
            pattern.  That is the only difference, and it applies where the
            plain table does nothing at all — over aug and dim destinations,
            which are not in the Minor family — so it is handled before the
            minor-family early-out. */
        static int modalMinorTable (int srcRel, ChordQuality q, ModalKind k,
                                    bool fifthVar = false)
        {
            // 5th VARIANT — augmented and diminished destinations bend the
            // pattern's 5th to match the chord's own altered 5th: sharp for
            // augmented, flat for diminished / half-diminished.  Everything
            // else about those chords is left to the MELODY table.
            if (fifthVar)
            {
                const auto fam = familyOf (q);
                if (srcRel == 7)
                {
                    if (fam == Family::Augmented)  return 8;   // #5
                    if (fam == Family::Diminished
                     || fam == Family::HalfDim)    return 6;   // b5
                }
            }

            if (familyOf (q) != Family::Minor)
                return melodyTable (srcRel, q);

            int dest = srcRel;

            // 3rd — always flat in every minor mode.
            if (srcRel == 4) dest = 3;

            // 6th — flat in Harmonic Minor and Natural Minor; natural in
            // Melodic Minor and Dorian.
            if (srcRel == 9)
            {
                if (k == ModalKind::HarmonicMinor || k == ModalKind::NaturalMinor)
                    dest = 8;
            }

            // 7th — flat in Natural Minor and Dorian; natural in Melodic
            // Minor and Harmonic Minor.  This deliberately OVERRIDES the
            // MELODY-table behaviour of flatting the 7 for m7-family chords:
            // Melodic / Harmonic Minor want the natural-7 leading tone even
            // when the destination chord nominally has a b7 (the resulting
            // melodic colour is the whole point of these modes).
            if (srcRel == 11)
            {
                if (k == ModalKind::NaturalMinor || k == ModalKind::Dorian)
                    dest = 10;
            }

            return ((dest % 12) + 12) % 12;
        }

        /** MELODY mode — collapses the "third" and "seventh" intervals to
            whatever the destination chord's quality demands. Other intervals
            either pass through unchanged or move to the nearest scale tone. */
        static int melodyTable (int srcRel, ChordQuality q)
        {
            // Group dest qualities by harmonic family — this gives us 6 "rules"
            // covering the 34 chord types, and any quality not explicitly
            // covered falls through to the default MAJOR behaviour.
            const auto family = familyOf (q);

            // Default mapping: identity (passthrough)
            int dest = srcRel;

            switch (family)
            {
                case Family::Major:
                    // Source intervals already match a major tonality.  But a
                    // source major-7th (srcRel 11) has to be DROPPED when the
                    // held chord is a plain major triad / 6 / add9 (it has no
                    // major-7th of its own) — otherwise that note sits a
                    // semitone under the root and clashes.  Keep it only for the
                    // genuine maj7-family chords.  This matches the Betelgeuse
                    // substitution table, which drops the 7th on triads too.
                    if (srcRel == 11
                        && q != ChordQuality::Maj7
                        && q != ChordQuality::Maj7s11
                        && q != ChordQuality::Maj7_9)
                        return -1;          // drop (apply() treats <0 as "don't play")
                    break;

                case Family::Minor:
                    // Flatten the major-3rd to minor-3rd (4 → 3)
                    if (srcRel == 4) dest = 3;

                    // The source 7th.  This MUST mirror what the Major branch
                    // above does, and it did not — which is the whole bug.
                    //
                    // Major DROPS a source maj-7th on a chord that has no 7th of
                    // its own (a plain triad / 6 / add9), because it would sit a
                    // semitone under the root and clash.  Minor was FLATTENING it
                    // to a b7 instead and keeping it — on chords that have no 7th
                    // either.  So a plain Am was handed a G it never asked for.
                    //
                    // That is exactly what broke CountryStraits' organ.  Its
                    // source is a ROOTLESS CMaj7 voicing — E(3rd) G(5th) B(maj7) —
                    // and over Am it came out as C, E and G.  With no root in the
                    // voicing, those three notes are a complete, self-standing
                    // C MAJOR TRIAD: the arranger showed "Am" while the organ
                    // played C.  Dropping the 7th leaves C and E — the 3rd and
                    // 5th of Am, unambiguously Am, with the bass supplying the A.
                    //
                    //   Min / Min6 / MinAdd9   -> no 7th  -> DROP  (like Major)
                    //   Min7 / Min7(9) / (11)  -> has b7  -> flatten 11 → 10
                    //   Min7b5                 -> has b7  -> flatten 11 → 10
                    //   MinMaj7 / MinMaj7(9)   -> has maj7 -> keep 11 as-is
                    if (srcRel == 11)
                    {
                        if (q == ChordQuality::Min
                            || q == ChordQuality::Min6
                            || q == ChordQuality::MinAdd9)
                            return -1;              // no 7th in the chord — DROP

                        if (q == ChordQuality::Min7
                            || q == ChordQuality::Min7_9
                            || q == ChordQuality::Min7_11
                            || q == ChordQuality::Min7b5)
                            dest = 10;              // b7
                        // MinMaj7 / MinMaj7_9 keep the natural 7 (dest stays 11)
                    }
                    break;

                case Family::Dom7:
                    // Dominant 7th family: flatten the major-7th
                    if (srcRel == 11) dest = 10;
                    // Dom7sus4 also replaces 3rd with 4th
                    if (q == ChordQuality::Dom7sus4 && srcRel == 4) dest = 5;
                    // Dom7b5 / Dom7#11 — flatten the 5th
                    if ((q == ChordQuality::Dom7b5 || q == ChordQuality::Dom7s11) && srcRel == 7) dest = 6;
                    // Dom7b9 — flatten the 9th
                    if (q == ChordQuality::Dom7b9 && srcRel == 2) dest = 1;
                    // Dom7#9 — raise the 9th
                    if (q == ChordQuality::Dom7s9 && srcRel == 2) dest = 3;
                    // Dom7b13 — flatten the 6th
                    if (q == ChordQuality::Dom7b13 && srcRel == 9) dest = 8;
                    // Aug-7 — raise the 5th
                    if (q == ChordQuality::Dom7Aug && srcRel == 7) dest = 8;
                    break;

                case Family::Diminished:
                    // dim / dim7: flatten 3rd, 5th, and (for dim7) 7th to bb7
                    if (srcRel == 4)  dest = 3;
                    if (srcRel == 7)  dest = 6;
                    if (srcRel == 11) dest = (q == ChordQuality::Dim7) ? 9 : 10;
                    break;

                case Family::HalfDim:
                    // m7b5: flatten 3rd, 5th, 7th
                    if (srcRel == 4)  dest = 3;
                    if (srcRel == 7)  dest = 6;
                    if (srcRel == 11) dest = 10;
                    break;

                case Family::Augmented:
                    // aug / Maj7Aug: raise the 5th
                    if (srcRel == 7) dest = 8;
                    // Maj7Aug keeps the major-7; a bare aug triad has none, so
                    // drop a source major-7th to avoid a semitone clash.
                    if (srcRel == 11 && q != ChordQuality::Maj7Aug)
                        return -1;          // drop
                    break;

                case Family::Sus:
                    // Sus4: replace 3rd with 4th
                    if (srcRel == 4) dest = 5;
                    // sus2 (1+2+5): replace 3rd with 2nd
                    if (q == ChordQuality::OnePlus2Plus5 && srcRel == 4) dest = 2;
                    // Sus chords have no 7th — drop a source major-7th so it
                    // doesn't clash a semitone under the root.
                    if (srcRel == 11) return -1;   // drop
                    break;

                case Family::Power:
                    // 1+5 / 1+8 — strip everything except root and 5th
                    if (srcRel == 4 || srcRel == 3) dest = (q == ChordQuality::OnePlus8) ? 0 : 0;
                    if (srcRel == 11 || srcRel == 10) dest = (q == ChordQuality::OnePlus8) ? 0 : 7;
                    break;
            }

            return ((dest % 12) + 12) % 12;
        }

        /** CHORD mode — the reference SFF distinction Grex previously collapsed
            into MELODY.  A 'chord' part snaps to the destination chord's tones
            and DROPS every source note that isn't a chord tone of the
            (CMaj7-normalised) source — root / M3 / P5 / M7.  The kept tones are
            remapped exactly as MELODY does (so the 3rd/5th/7th follow the held
            quality, and a M7 over a plain triad still drops).  Passing / colour
            tones are discarded, which is what stops a comp part's non-chord
            notes from clashing on a distant chord. */
        static int chordTable (int srcRel, ChordQuality q)
        {
            srcRel = ((srcRel % 12) + 12) % 12;

            // Source CMaj7 chord tones: root(0), M3(4), P5(7), M7(11).
            if (srcRel == 0 || srcRel == 4 || srcRel == 7 || srcRel == 11)
                return melodyTable (srcRel, q);   // remap (may itself return -1)

            return -1;                            // non-chord tone — drop
        }

        enum class Family { Major, Minor, Dom7, Diminished, HalfDim, Augmented, Sus, Power };

        static Family familyOf (ChordQuality q) noexcept
        {
            switch (q)
            {
                case ChordQuality::Maj:
                case ChordQuality::Maj6:
                case ChordQuality::Maj7:
                case ChordQuality::Maj7s11:
                case ChordQuality::MajAdd9:
                case ChordQuality::Maj7_9:
                case ChordQuality::Maj6_9:
                    return Family::Major;

                case ChordQuality::Min:
                case ChordQuality::Min6:
                case ChordQuality::Min7:
                case ChordQuality::MinAdd9:
                case ChordQuality::Min7_9:
                case ChordQuality::Min7_11:
                case ChordQuality::MinMaj7:
                case ChordQuality::MinMaj7_9:
                    return Family::Minor;

                case ChordQuality::Dom7:
                case ChordQuality::Dom7sus4:
                case ChordQuality::Dom7b5:
                case ChordQuality::Dom7_9:
                case ChordQuality::Dom7s11:
                case ChordQuality::Dom7_13:
                case ChordQuality::Dom7b9:
                case ChordQuality::Dom7b13:
                case ChordQuality::Dom7s9:
                case ChordQuality::Dom7Aug:
                    return Family::Dom7;

                case ChordQuality::Dim:
                case ChordQuality::Dim7:
                    return Family::Diminished;

                case ChordQuality::Min7b5:
                    return Family::HalfDim;

                case ChordQuality::Aug:
                case ChordQuality::Maj7Aug:
                    return Family::Augmented;

                case ChordQuality::Sus4:
                case ChordQuality::OnePlus2Plus5:
                    return Family::Sus;

                case ChordQuality::OnePlus8:
                case ChordQuality::OnePlus5:
                    return Family::Power;

                default:
                    return Family::Major;
            }
        }

        // Effective bass register (see setBassRange). Default E1 (28) .. E3 (52).
        static inline std::atomic<int> sBassLow  { 28 };
        static inline std::atomic<int> sBassHigh { 52 };
    };
} // namespace Betel