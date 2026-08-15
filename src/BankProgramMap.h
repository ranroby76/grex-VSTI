#pragma once
//==============================================================================
// BankProgramMap.h
//
// Translates a Yamaha-style (Bank MSB, Bank LSB, Program Change, voice-name)
// tuple into our blob's preset index (a GM "flag" — the leading NNN of
// "NNN-Name.frb").
//
// Why four inputs instead of three
// --------------------------------
//   The (MSB, LSB, PC) triplet doesn't uniquely identify a voice on Yamaha
//   hardware unless you have the model's full Voice Map.  In particular,
//   MegaVoice (MSB 8) and S.Articulation banks use a YAMAHA-INTERNAL PC
//   index that is NOT GM-aligned — PC 17 in MSB 8 is "ElectricBass", not
//   "FM E.Piano".  When the LSB-specific entry isn't in our table, the
//   8-char CASM voice name written by the style author is the next-best
//   signal: "FngrBass", "ClnGtr ", "OrchTrp", etc. all tell us the GM
//   family unambiguously.
//
// Resolution chain
// ----------------
//   1) MSB 8 (MegaVoice / S.Art):
//        a) (LSB, PC) → GM-flag table lookup  (~30 explicit entries)
//        b) LSB-agnostic PC safety net (PC range → GM family base)
//        c) voiceName keyword parse           (~150 keyword rules)
//        d) fall through to flag 0 (Piano)
//
//   2) MSB ≠ 8 (GM / XG variations / Cool!/Sweet!/Live! / GM2 / …):
//        Yamaha convention is that the PC is GM-aligned across these banks
//        (LSB selects timbre variation, NOT a different program).  So we
//        return the PC directly.  The voice-name parser is consulted only
//        if the caller's engine validation later says the resolved flag
//        isn't in our library — that fallback lives in SamplePlayerEngine,
//        which is where the library presence map already is.
//
//   3) MSB 127 (drum kits): never reaches here.  The drum registry owns
//        drum-bank program changes (engine.programChangeDrum).
//
// Threading
// ---------
//   All public methods are noexcept and read-only after construction; the
//   tables are constexpr static.  numPresets is an atomic (set on the
//   message thread by applyVoiceSetup, never read here — kept as a
//   public knob for future use).
//==============================================================================

#include <JuceHeader.h>
#include <cstring>

namespace Betel
{
    class BankProgramMap
    {
    public:
        /** Set the number of presets available in the currently-loaded blob.
            Currently kept for diagnostics / future use; the engine owns the
            real library-presence map (SamplePlayerEngine::hasInstrumentFlag). */
        void setNumBlobPresets (int n) noexcept { numPresets.store (juce::jmax (0, n)); }

        /** Resolve a Yamaha (bankMsb, bankLsb, pc, voiceName) tuple into a
            GM flag usable with SamplePlayerEngine::selectChannelPreset.

            bankMsb / bankLsb may be -1 ("not set") — treated as generic GM.
            voiceName is the 8-char CASM voice name; nullptr / empty string
            disables the keyword-parser tier.  Returns a flag in [0, 127],
            or 0 (Piano) for negative pc. */
        int resolve (int bankMsb, int bankLsb, int pc,
                     const char* voiceName) const noexcept
        {
            if (pc < 0) return 0;
            pc &= 0x7F;                            // hard-clamp to MIDI range

            // ── Tier 1: MegaVoice / S.Articulation (MSB 8) ────────────────────
            if (bankMsb == 8)
            {
                // 1a) Explicit (LSB, PC) table — highest confidence.
                if (const int t = megaVoiceTableLookup (bankLsb, pc); t >= 0)
                    return t;

                // 1b) LSB-agnostic PC safety net — known Yamaha-internal PC
                //     ranges across the MegaVoice instruments.
                if (const int s = megaVoiceLsbAgnosticFallback (pc); s >= 0)
                    return s;

                // 1c) The voice-name parser — author-supplied label.
                if (const int kw = voiceNameToGmFamilyBase (voiceName); kw >= 0)
                    return kw;

                // 1d) Truly unknown MegaVoice slot.  This used to return 0 —
                //     ACOUSTIC GRAND PIANO — which is the worst possible guess:
                //     a piano standing in for a brass stab or a string swell
                //     does not read as "close enough", it reads as a broken
                //     style.  70sDisco2's "Brs" part (8/1/PC 56) landed exactly
                //     there.
                //
                //     MegaVoice PC numbering only DIVERGES from GM in the ranges
                //     the two tiers above already cover (guitars 0..7, basses
                //     17..24); outside those it tracks GM closely enough to be
                //     far better than a fixed guess — PC 48 is a string
                //     ensemble, PC 56 is brass, and so on.  So fall through to
                //     the GM reading and let the engine's own validation chain
                //     (pc -> pc & ~7 -> 0) handle a flag the library lacks.
                return pc;
            }

            // ── Tier 2: GM-aligned banks ──────────────────────────────────────
            //
            // Every non-MSB-8 Yamaha bank (GM, XG variations with LSB 1..127,
            // Cool!/Sweet!/Live! at LSB 113/114/122, GM2 at MSB 121, etc.)
            // numbers its programs by the GM family.  PC IS the GM flag.
            //
            // If the caller's engine doesn't have that exact flag in the
            // library, the engine's own validation chain (selectChannelPreset)
            // walks  pc → (pc & ~7) → 0  to land on something audible.
            return pc;
        }

        /** Backwards-compatible overload for callers that don't have a
            voice-name hint to pass.  Behaves like the four-arg version with
            voiceName = nullptr. */
        int resolve (int bankMsb, int bankLsb, int pc) const noexcept
        {
            return resolve (bankMsb, bankLsb, pc, nullptr);
        }

        //======================================================================
        // ROLE CORRECTION — the BASS destination (SFF Ch11 -> engine slot 2).
        //
        // Tier 2 above takes a non-MegaVoice bank's PC at face value, which is
        // right for the GM number but blind to the part's ROLE.  Styles very
        // often put a synth program on the bass channel: 80sDisco's bass is
        // bank 104 / PC 87 ("Lead 8 — bass+lead"), oriental styles use PC 81
        // from the same panel bank.  The style means "a synth BASS"; taken
        // literally the slot loads a LEAD — and if that flag isn't in the
        // library, getOrDecodeInstrument's blind family walk (flag & ~7, then
        // Piano) lands it somewhere worse still.  Everything downstream that
        // keys off the GM program then misses too: the allowed-notes floor, the
        // Synth-Bass-1 octave bias, the per-voice calibration in SoundsTab.
        //
        // So on the bass destination ONLY, a program that can't plausibly BE a
        // bass is mapped into the GM bass family (32..39).  Kept deliberately
        // conservative:
        //
        //   * 32..39 pass through untouched — already a GM bass.
        //   * Cello (42), Contrabass (43) and Tuba (58) also pass through: real
        //     orchestral / polka / brass-band styles use these AS the bass, and
        //     rewriting them to an electric bass would be the regression this
        //     correction exists to prevent.
        //   * Anything else is mapped by its GM family, because the family
        //     carries the timbre the author was reaching for — a synth-lead
        //     number means a SYNTH bass (38), a guitar number an electric one.
        //   * An 8-char CASM name that states a SPECIFIC bass type overrides
        //     the family guess ("SynBass", "PickBs", "AcBs", "FrtlsBs", ...).
        //     A bare "Bass" resolves to the generic finger bass (33), which
        //     says nothing the family map doesn't say better, so it does NOT
        //     override — see the (name != 33) test below.
        //
        // Melodic destinations other than the bass are never touched: only the
        // bass has a register/monophony contract strong enough to justify
        // overriding what the file asked for.
        //======================================================================
        static int correctFlagForBassRole (int flag, const char* voiceName) noexcept
        {
            if (flag < 0 || flag > 127) return flag;

            // Already a bass, or a legitimate low-register bass voice.
            if (flag >= 32 && flag <= 39) return flag;
            if (flag == 42 || flag == 43 || flag == 58) return flag;

            // The author's own label, when it names a specific bass type.
            if (const int named = voiceNameToGmFamilyBase (voiceName);
                named >= 32 && named <= 39 && named != 33)
                return named;

            // Otherwise infer from the GM family of the program the file asked
            // for.  GM family = flag / 8.
            switch (flag >> 3)
            {
                case 10: case 11:                     // 80..95  Synth Lead / Synth Pad
                case 12:                              // 96..103 Synth FX
                    // -> SYNTH BASS 1 (38), deliberately NOT 39.
                    //
                    // This arm covers 24 programs, so pointing it at 39 made
                    // Synth Bass 2 the single destination for every panel-bank
                    // lead used as a bass — common in oriental and dance
                    // conversions — and 39 kept turning up in styles that never
                    // asked for it.  Worse, those styles then inherited GM 39's
                    // special handling (its own note window and a heavy gain
                    // trim), both of which exist for one specific library voice
                    // and are wrong for an arbitrary remapped lead.
                    //
                    // 38 is the same synth-bass timbre without that baggage: it
                    // takes the ordinary bass floor and the ordinary bass gain.
                    // GM 39 is now reached ONLY when a style declares it.
                    return 38;

                case 2:  case 3:                      // 16..31  Organ / Guitar
                    return 33;                        //   -> Electric Bass (finger)

                case 5:  case 6:                      // 40..55  Strings / Ensemble
                case 7:  case 8:  case 9:             // 56..79  Brass / Reed / Pipe
                    return 32;                        //   -> Acoustic Bass

                default:                              // 0..15, 104..127
                    return 33;                        //   -> Electric Bass (finger)
            }
        }

    private:
        //======================================================================
        // 1a — Explicit (LSB, PC) MegaVoice / S.Art entries.
        //
        // Source: Tyros5 / Genos public Data Lists.  The GM flag is the
        // closest GM Level-1 family voice; the engine's library-validation
        // chain falls back to the family root if the exact flag isn't loaded.
        //
        // Returns -1 if the (lsb, pc) pair isn't in the table.
        //======================================================================
        static int megaVoiceTableLookup (int lsb, int pc) noexcept
        {
            struct Entry { int lsb; int pc; int gm; };

            static constexpr Entry kTable[] =
            {
                // ── Guitar MegaVoices (LSB 0) ────────────────────────────────
                { 0,  0, 24 },   // NylonGuitar      (8/0/PRG1)
                { 0,  1, 25 },   // SteelGuitar      (8/0/PRG2)
                { 0,  2, 25 },   // HiStringGtr      (8/0/PRG3)
                { 0,  3, 27 },   // CleanGuitar      (8/0/PRG4)
                { 0,  4, 29 },   // OverdriveGt      (8/0/PRG5)
                { 0,  5, 30 },   // DistortionGt     (8/0/PRG6)
                { 0,  6, 26 },   // JazzGuitar       (8/0/PRG7)
                { 1,  2, 25 },   // 12StringGtr      (8/1/PRG3)
                { 1,  3, 27 },   // SolidGuitar1     (8/1/PRG4)
                { 2,  3, 27 },   // SolidGuitar2     (8/2/PRG4)
                { 3,  3, 27 },   // SingleCoil       (8/3/PRG4)
                { 4,  3, 27 },   // FingerGuitar     (8/4/PRG4)
                { 5,  3, 27 },   // FingerSlapGtr    (8/5/PRG4)
                { 6,  3, 27 },   // VintagePickGtr   (8/6/PRG4)
                { 7,  3, 27 },   // VintageSlapGtr   (8/7/PRG4)
                { 8,  3, 27 },   // SlapAmpGt        (8/8/PRG4)

                // ── Bass MegaVoices ──────────────────────────────────────────
                { 0, 17, 33 },   // ElectricBass     (8/0/PRG18) → Finger Bass
                { 0, 18, 34 },   // PickBass         (8/0/PRG19) → Pick Bass
                { 0, 19, 35 },   // FretlessBass     (8/0/PRG20) → Fretless
                { 1, 17, 33 },   // VintageRound     (8/1/PRG18)
                { 1, 18, 34 },   // VintagePickBass  (8/1/PRG19)
                { 2, 17, 33 },   // VintageFlat      (8/2/PRG18)

                // ── Acoustic Bass MegaVoice ──────────────────────────────────
                { 0, 24, 32 },   // AcousticBassMV  → GM Acoustic Bass
            };

            for (const auto& e : kTable)
                if (e.lsb == lsb && e.pc == pc)
                    return e.gm;

            return -1;
        }

        //======================================================================
        // 1b — LSB-agnostic safety net for MSB 8.
        //
        // When the LSB isn't in our explicit table, the PC alone tells us the
        // Yamaha MegaVoice slot family (Yamaha groups MegaVoices by PC ranges
        // — guitars cluster around PC 0..6, basses around PC 17..21).  Returns
        // -1 outside the known ranges.
        //======================================================================
        static int megaVoiceLsbAgnosticFallback (int pc) noexcept
        {
            switch (pc)
            {
                // ── Guitar cluster ───────────────────────────────────────────
                case 0:  return 24;   // → Nylon Guitar
                case 1:  return 25;   // → Steel Guitar
                case 2:  return 25;   // → Steel / 12-string
                case 3:  return 27;   // → Clean Guitar
                case 4:  return 29;   // → Overdrive Guitar
                case 5:  return 30;   // → Distortion Guitar
                case 6:  return 26;   // → Jazz Guitar
                case 7:  return 27;   // → variant clean / hi-string

                // ── Bass cluster ─────────────────────────────────────────────
                case 17: return 33;   // → Finger Bass
                case 18: return 34;   // → Pick Bass
                case 19: return 35;   // → Fretless Bass
                case 20: return 32;   // → Acoustic Bass
                case 21: return 36;   // → Slap Bass 1
                case 22: return 37;   // → Slap Bass 2
                case 23: return 38;   // → Synth Bass 1
                case 24: return 32;   // → Acoustic Bass (alt slot)

                default: return -1;
            }
        }

        //======================================================================
        // 1c — Voice-name keyword parser.
        //
        // Style authors write an 8-char ASCII name into each CASM entry — e.g.
        // "FngrBass", "ClnGtr  ", "OrchTrp ", "JzGtr   ".  When the bank+PC
        // chain has nothing confident to say, the name itself maps onto a GM
        // family.  Rules are scanned in order; the FIRST match wins, so the
        // table is ordered most-specific-first to avoid e.g. "Bass" matching
        // before "Brass" (it can't — "Bass" sits AFTER "Brass" check uses a
        // longer literal "brass", so the order works).
        //
        // Returns -1 if no keyword matches.
        //======================================================================
        static int voiceNameToGmFamilyBase (const char* name) noexcept
        {
            if (name == nullptr || name[0] == 0) return -1;

            // Lowercase copy, up to 15 chars (8 is the CASM cap but allow
            // longer just in case a future caller passes a richer name).
            char buf[16] = {0};
            int  blen = 0;
            for (int i = 0; i < 15 && name[i] != 0; ++i)
            {
                char c = name[i];
                if (c >= 'A' && c <= 'Z') c = (char) (c + ('a' - 'A'));
                buf[blen++] = c;
            }
            if (blen == 0) return -1;

            auto contains = [blen, &buf] (const char* kw) noexcept -> bool
            {
                const int klen = (int) std::strlen (kw);
                if (klen > blen) return false;
                for (int i = 0; i + klen <= blen; ++i)
                    if (std::memcmp (buf + i, kw, (size_t) klen) == 0)
                        return true;
                return false;
            };

            // ── Bass (check BEFORE brass to avoid "bs" / "bass" ambiguity) ───
            //    Also check before guitar so "BsGtr" rare combos don't
            //    misfire — bass intent dominates if the word "bass" is there.
            if (contains ("bass") || contains ("fngrbs") || contains ("pickbs")
                                  || contains ("bs")     || contains ("acbs"))
            {
                if (contains ("frtls")  || contains ("fretl")) return 35;
                if (contains ("pick"))                          return 34;
                if (contains ("slap"))                          return 36;
                if (contains ("syn"))                           return 38;
                if (contains ("acou")   || contains ("upright")
                                        || contains ("uprght")
                                        || contains ("acbs"))   return 32;
                return 33;   // generic / finger
            }

            // ── Brass (check before guitar — "br" alone is too short)  ───────
            // SHORT YAMAHA ABBREVIATIONS.  CASM voice names are an 8-char
            // field and composers abbreviate hard: 70sDisco2 labels its brass
            // part "Brs", which matches neither "brass" nor "bras" and so fell
            // all the way through to the unknown-MegaVoice default (piano).
            // These are checked FIRST because they are exact-ish short forms;
            // the longer keyword rules below still catch the spelled-out names.
            if (contains ("brs"))                                 return 61;   // Brass Section
            if (contains ("sbrs")  || contains ("synbr"))         return 62;   // Synth Brass
            if (contains ("epno")  || contains ("ep "))           return 4;    // Electric Piano
            if (contains ("pno")   || contains ("pf "))           return 0;    // Acoustic Piano
            if (contains ("cho"))                                 return 52;   // Choir
            if (contains ("orgn")  || contains ("org "))          return 16;   // Organ
            if (contains ("acc"))                                 return 21;   // Accordion
            if (contains ("harm"))                                return 22;   // Harmonica
            if (contains ("clav"))                                return 7;    // Clavi
            if (contains ("vibe")  || contains ("vib "))          return 11;   // Vibraphone
            if (contains ("mrmb"))                                return 12;   // Marimba

            if (contains ("brass") || contains ("bras"))
            {
                if (contains ("syn"))                           return 62;
                if (contains ("sec"))                           return 61;
                return 61;
            }
            if (contains ("trump")  || contains ("trp")
                                   || contains ("flugel")
                                   || contains ("cornet"))        return 56;
            if (contains ("tromb")  || contains ("tbn"))         return 57;
            if (contains ("tuba"))                                return 58;
            if (contains ("muttr"))                               return 59;
            if (contains ("frhrn")  || contains ("fhorn")
                                    || contains ("horn"))         return 60;

            // ── Guitar ───────────────────────────────────────────────────────
            if (contains ("nylgt") || contains ("nylon"))        return 24;
            if (contains ("steelg")|| contains ("stlgt"))        return 25;
            if (contains ("12str") || contains ("12-str"))       return 25;
            if (contains ("jazzg") || contains ("jzgtr")
                                   || contains ("jzgt"))          return 26;
            if (contains ("clean") || contains ("clngtr")
                                   || contains ("clngt"))         return 27;
            if (contains ("muteg") || contains ("mutgt"))        return 28;
            if (contains ("overdr")|| contains ("ovrdgt")
                                   || contains ("crunch"))        return 29;
            if (contains ("dist"))                                return 30;
            if (contains ("harmgt"))                              return 31;
            if (contains ("strat")  || contains ("tele")
                                    || contains ("60s"))          return 27;   // single-coil electrics
            if (contains ("clgt"))                                return 27;   // clean guitar (abbrev)
            if (contains ("spanish")|| contains ("nyln"))         return 24;   // nylon
            if (contains ("acgt")   || contains ("ac gt"))        return 25;   // acoustic steel-string
            if (contains ("gtsfx")  || contains ("gt sfx"))       return 24;   // guitar SFX / noise layer
            if (contains ("guit")  || contains ("gtr"))           return 24;

            // ── Saxes (before "as" / "ts" 2-letter risk) ─────────────────────
            if (contains ("sopsax")|| contains ("sopsx"))        return 64;
            if (contains ("altsax")|| contains ("altsx")
                                   || contains ("asax"))          return 65;
            if (contains ("tensax")|| contains ("tnrsx")
                                   || contains ("tsax"))          return 66;
            if (contains ("barisx")|| contains ("barsax")
                                   || contains ("barsx"))         return 67;
            if (contains ("sax"))                                 return 66;

            // ── Reeds & pipes ────────────────────────────────────────────────
            if (contains ("oboe"))                                return 68;
            if (contains ("englh")  || contains ("ehrn"))         return 69;
            if (contains ("bassoon")|| contains ("bsn"))          return 70;
            if (contains ("clarin") || contains ("clrnt"))        return 71;
            if (contains ("piccolo")|| contains ("piccol")
                                    || contains ("picc"))         return 72;
            if (contains ("flute")  || contains ("flt"))          return 73;
            if (contains ("recordr")|| contains ("recrdr"))       return 74;
            if (contains ("panfl")  || contains ("panpipe"))      return 75;
            if (contains ("bottle"))                              return 76;
            if (contains ("shakuh"))                              return 77;
            if (contains ("whistl"))                              return 78;
            if (contains ("ocarina"))                             return 79;

            // ── Strings & orchestral ─────────────────────────────────────────
            if (contains ("violin") || contains ("vln"))          return 40;
            if (contains ("viola")  || contains ("vla"))          return 41;
            if (contains ("cello"))                               return 42;
            if (contains ("contrab")|| contains ("ctrbs"))        return 43;
            if (contains ("tremst"))                              return 44;
            if (contains ("pizz"))                                return 45;
            if (contains ("harp"))                                return 46;
            if (contains ("timpani")|| contains ("timp"))         return 47;
            if (contains ("slowstr")|| contains ("slstr"))        return 49;
            if (contains ("synstr") || contains ("synthst"))      return 50;
            if (contains ("strngs") || contains ("strens")
                                   || contains ("string")
                                   || contains ("strs")
                                   || contains ("strng")
                                   || contains ("str ")
                                   || contains ("seattle")
                                   || contains ("orchstr")
                                   || contains ("chamber"))       return 48;
            if (contains ("orchhit"))                             return 55;

            // ── Choirs / voices ──────────────────────────────────────────────
            if (contains ("choir")  || contains ("aahs") || contains ("aah")
                                   || contains ("voiceaa"))       return 52;
            if (contains ("ohs")    || contains ("ooh")
                                   || contains ("voiceoo"))       return 53;
            if (contains ("synvox") || contains ("synvoi")
                                   || contains ("synvoc"))        return 54;
            if (contains ("vox")    || contains ("vocal")
                                   || contains ("popvoc")
                                   || contains ("scat"))          return 53;   // generic vocal / pop vox

            // ── Organs ───────────────────────────────────────────────────────
            if (contains ("drwbar") || contains ("drworg")
                                   || contains ("percorg"))       return 17;
            if (contains ("rockorg")|| contains ("rockor"))       return 18;
            if (contains ("churchorg")
                                  || contains ("pipeorg")
                                  || contains ("church"))         return 19;
            if (contains ("reedorg"))                             return 20;
            if (contains ("accord")  || contains ("musette"))     return 21;
            if (contains ("harmon"))                              return 22;
            if (contains ("tango")  || contains ("bandone"))      return 23;
            if (contains ("organ")  || contains ("org"))          return 16;

            // ── Pianos / EPs / harpsi ────────────────────────────────────────
            if (contains ("ep1")    || contains ("rhodes")
                                   || contains ("rhd")
                                   || contains ("e.pno") || contains ("elpno")
                                   || contains ("epno")  || contains ("elecpno")) return 4;
            if (contains ("ep2")    || contains ("dx7")
                                   || contains ("wurli")
                                   || contains ("wur")
                                   || contains ("fmep"))          return 5;
            if (contains ("harpsi") || contains ("harps"))        return 6;
            if (contains ("clav"))                                return 7;
            if (contains ("honkyto")|| contains ("hnktnk"))       return 3;
            if (contains ("grndpno")|| contains ("grandp")
                                   || contains ("grdpno")
                                   || contains ("piano")
                                   || contains ("pno"))           return 0;

            // ── Chromatic percussion ─────────────────────────────────────────
            if (contains ("celest"))                              return 8;
            if (contains ("glock"))                               return 9;
            if (contains ("musicbx")|| contains ("musbox"))       return 10;
            if (contains ("vibes")  || contains ("vibe"))         return 11;
            if (contains ("marimba"))                             return 12;
            if (contains ("xylo"))                                return 13;
            if (contains ("tubular"))                             return 14;
            if (contains ("dulcim"))                              return 15;

            // ── Synth lead / pad / FX ────────────────────────────────────────
            if (contains ("sqlead") || contains ("sqrlead"))      return 80;
            if (contains ("sawlead")|| contains ("sawld"))        return 81;
            if (contains ("calliop"))                             return 82;
            if (contains ("chiff"))                               return 83;
            if (contains ("charang"))                             return 84;
            if (contains ("voicld") || contains ("voicelead"))    return 85;
            if (contains ("fifthsaw")
                                  || contains ("5th"))            return 86;
            if (contains ("leadbas")|| contains ("leadbs"))       return 87;
            if (contains ("warmpad"))                             return 89;
            if (contains ("polypad"))                             return 90;
            if (contains ("chorpad")|| contains ("choirpad"))     return 91;
            if (contains ("bowedpad"))                            return 92;
            if (contains ("metalpad"))                            return 93;
            if (contains ("halopad"))                             return 94;
            if (contains ("sweeppad"))                            return 95;
            if (contains ("newage"))                              return 88;
            if (contains ("pad"))                                 return 89;
            if (contains ("rainfx") || contains ("rain"))         return 96;
            if (contains ("soundtr"))                             return 97;
            if (contains ("crystal"))                             return 98;
            if (contains ("atmosp"))                              return 99;
            if (contains ("brightn"))                             return 100;
            if (contains ("goblin"))                              return 101;
            if (contains ("echoes"))                              return 102;
            if (contains ("scifi"))                               return 103;

            // ── Ethnic ───────────────────────────────────────────────────────
            if (contains ("sitar"))                               return 104;
            if (contains ("banjo"))                               return 105;
            if (contains ("shamis"))                              return 106;
            if (contains ("koto"))                                return 107;
            if (contains ("kalimb"))                              return 108;
            if (contains ("bagpip"))                              return 109;
            if (contains ("fiddle"))                              return 110;
            if (contains ("shanai"))                              return 111;

            // ── Percussive ───────────────────────────────────────────────────
            if (contains ("tinkle"))                              return 112;
            if (contains ("agogo"))                               return 113;
            if (contains ("steeldr"))                             return 114;
            if (contains ("woodbl"))                              return 115;
            if (contains ("taiko"))                               return 116;
            if (contains ("melotom"))                             return 117;
            if (contains ("syndrum"))                             return 118;
            if (contains ("reverse"))                             return 119;

            // ── SFX ──────────────────────────────────────────────────────────
            if (contains ("fretno"))                              return 120;
            if (contains ("breath"))                              return 121;
            if (contains ("seash"))                               return 122;
            if (contains ("bird"))                                return 123;
            if (contains ("telep"))                               return 124;
            if (contains ("helic"))                               return 125;
            if (contains ("appl"))                                return 126;
            if (contains ("gun"))                                 return 127;

            // ── Generic catches (LAST, after everything specific) ────────────
            if (contains ("lead"))                                return 80;
            if (contains ("perc"))                                return 115;

            return -1;
        }

        std::atomic<int> numPresets { 0 };
    };
} // namespace Betel
