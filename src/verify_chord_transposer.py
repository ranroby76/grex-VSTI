#!/usr/bin/env python3
"""
verify_chord_transposer.py
==========================
Python mirror of ChordTransposer.h. Verifies the chord recogniser and the NTR
+ NTT note transposer against hand-derived expected behaviour. Run as:

    python3 verify_chord_transposer.py

Exit code 0 = all green. Any mismatch prints the failing case and exits 1.
"""

from dataclasses import dataclass
from enum import IntEnum
from typing import List, Tuple, Optional

# ───────────────────────── ChordQuality enum (mirrors C++) ─────────────────────
class ChordQuality(IntEnum):
    Maj = 0; Maj6 = 1; Maj7 = 2; Maj7s11 = 3; MajAdd9 = 4
    Maj7_9 = 5; Maj6_9 = 6; Aug = 7
    Min = 8; Min6 = 9; Min7 = 10; Min7b5 = 11
    MinAdd9 = 12; Min7_9 = 13; Min7_11 = 14; MinMaj7 = 15
    MinMaj7_9 = 16; Dim = 17; Dim7 = 18; Dom7 = 19
    Dom7sus4 = 20; Dom7b5 = 21; Dom7_9 = 22; Dom7s11 = 23
    Dom7_13 = 24; Dom7b9 = 25; Dom7b13 = 26; Dom7s9 = 27
    Maj7Aug = 28; Dom7Aug = 29; OnePlus8 = 30; OnePlus5 = 31
    Sus4 = 32; OnePlus2Plus5 = 33

NUM_QUALITIES = 34
UNKNOWN = 0xFF

# ─────────────────────────── Required-interval masks ───────────────────────────
def m(*intervals):
    r = 0
    for i in intervals:
        if 0 <= i < 12:
            r |= 1 << i
    return r

CHORD_REQUIRED = [
    m(0,4,7),           # Maj
    m(0,4,7,9),         # Maj6
    m(0,4,7,11),        # Maj7
    m(0,4,6,11),        # Maj7#11
    m(0,2,4,7),         # MajAdd9
    m(0,2,4,7,11),      # Maj7(9)
    m(0,2,4,7,9),       # Maj6(9)
    m(0,4,8),           # Aug
    m(0,3,7),           # Min
    m(0,3,7,9),         # Min6
    m(0,3,7,10),        # Min7
    m(0,3,6,10),        # Min7b5
    m(0,2,3,7),         # MinAdd9
    m(0,2,3,7,10),      # Min7(9)
    m(0,3,5,7,10),      # Min7(11)
    m(0,3,7,11),        # MinMaj7
    m(0,2,3,7,11),      # MinMaj7(9)
    m(0,3,6),           # Dim
    m(0,3,6,9),         # Dim7
    m(0,4,7,10),        # Dom7
    m(0,5,7,10),        # Dom7sus4
    m(0,4,6,10),        # Dom7b5
    m(0,2,4,7,10),      # Dom7(9)
    m(0,4,6,7,10),      # Dom7#11
    m(0,4,7,9,10),      # Dom7(13)
    m(0,1,4,7,10),      # Dom7b9
    m(0,4,7,8,10),      # Dom7b13
    m(0,3,4,7,10),      # Dom7#9
    m(0,4,8,11),        # Maj7Aug
    m(0,4,8,10),        # Dom7Aug
    m(0),               # 1+8
    m(0,7),             # 1+5
    m(0,5,7),           # Sus4
    m(0,2,7),           # 1+2+5
]

CHORD_SPECIFICITY = [
    3,4,4,4,4,5,5,3,
    3,4,4,4,4,5,5,4,
    5,3,4,4,4,4,5,5,
    5,5,5,5,4,4,1,2,
    3,3
]

# ───────────────────────────── Chord recogniser ────────────────────────────────
def pitch_class(midi):
    return midi % 12

def rotate_right(v, n):
    n %= 12
    return ((v >> n) | (v << (12 - n))) & 0x0FFF

def popcount(v):
    c = 0
    while v:
        c += v & 1
        v >>= 1
    return c

@dataclass
class Chord:
    root: int
    quality: ChordQuality
    def __repr__(self):
        return f"Chord({NOTE_NAMES[self.root]} {self.quality.name})"

NOTE_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]

def recognize(notes: List[int]) -> Chord:
    if not notes:
        return Chord(0, UNKNOWN)
    pc_set = 0
    lowest = 127
    for n in notes:
        if 0 <= n <= 127:
            pc_set |= 1 << pitch_class(n)
            if n < lowest: lowest = n
    if pc_set == 0:
        return Chord(0, UNKNOWN)
    bass_pc = pitch_class(lowest)
    best_score = -1
    best = Chord(bass_pc, ChordQuality.Maj)
    for root in range(12):
        rotated = rotate_right(pc_set, root)
        for q in range(NUM_QUALITIES):
            req = CHORD_REQUIRED[q]
            if (rotated & req) != req:
                continue
            score = CHORD_SPECIFICITY[q] * 100
            if root == bass_pc:
                score += 30
            score -= popcount(rotated & ~req)
            if score > best_score:
                best_score = score
                best = Chord(root, ChordQuality(q))
    return best

# ────────────────────────── NTT MELODY by chord family ─────────────────────────
class Family(IntEnum):
    Major = 0; Minor = 1; Dom7 = 2; Diminished = 3
    HalfDim = 4; Augmented = 5; Sus = 6; Power = 7

def family_of(q: ChordQuality) -> Family:
    if q in (ChordQuality.Maj, ChordQuality.Maj6, ChordQuality.Maj7,
             ChordQuality.Maj7s11, ChordQuality.MajAdd9,
             ChordQuality.Maj7_9, ChordQuality.Maj6_9):
        return Family.Major
    if q in (ChordQuality.Min, ChordQuality.Min6, ChordQuality.Min7,
             ChordQuality.MinAdd9, ChordQuality.Min7_9, ChordQuality.Min7_11,
             ChordQuality.MinMaj7, ChordQuality.MinMaj7_9):
        return Family.Minor
    if q in (ChordQuality.Dom7, ChordQuality.Dom7sus4, ChordQuality.Dom7b5,
             ChordQuality.Dom7_9, ChordQuality.Dom7s11, ChordQuality.Dom7_13,
             ChordQuality.Dom7b9, ChordQuality.Dom7b13, ChordQuality.Dom7s9,
             ChordQuality.Dom7Aug):
        return Family.Dom7
    if q in (ChordQuality.Dim, ChordQuality.Dim7):       return Family.Diminished
    if q == ChordQuality.Min7b5:                          return Family.HalfDim
    if q in (ChordQuality.Aug, ChordQuality.Maj7Aug):    return Family.Augmented
    if q in (ChordQuality.Sus4, ChordQuality.OnePlus2Plus5): return Family.Sus
    if q in (ChordQuality.OnePlus8, ChordQuality.OnePlus5):  return Family.Power
    return Family.Major

def melody_table(src_rel: int, q: ChordQuality) -> int:
    src_rel %= 12
    fam = family_of(q)
    dest = src_rel
    if fam == Family.Major:
        pass
    elif fam == Family.Minor:
        if src_rel == 4: dest = 3
        if src_rel == 11 and q in (ChordQuality.Min, ChordQuality.Min6, ChordQuality.Min7,
                                    ChordQuality.Min7_9, ChordQuality.Min7_11, ChordQuality.MinAdd9):
            dest = 10
    elif fam == Family.Dom7:
        if src_rel == 11: dest = 10
        if q == ChordQuality.Dom7sus4 and src_rel == 4: dest = 5
        if q in (ChordQuality.Dom7b5, ChordQuality.Dom7s11) and src_rel == 7: dest = 6
        if q == ChordQuality.Dom7b9 and src_rel == 2: dest = 1
        if q == ChordQuality.Dom7s9 and src_rel == 2: dest = 3
        if q == ChordQuality.Dom7b13 and src_rel == 9: dest = 8
        if q == ChordQuality.Dom7Aug and src_rel == 7: dest = 8
    elif fam == Family.Diminished:
        if src_rel == 4: dest = 3
        if src_rel == 7: dest = 6
        if src_rel == 11: dest = 9 if q == ChordQuality.Dim7 else 10
    elif fam == Family.HalfDim:
        if src_rel == 4: dest = 3
        if src_rel == 7: dest = 6
        if src_rel == 11: dest = 10
    elif fam == Family.Augmented:
        if src_rel == 7: dest = 8
    elif fam == Family.Sus:
        if src_rel == 4: dest = 5
        if q == ChordQuality.OnePlus2Plus5 and src_rel == 4: dest = 2
    elif fam == Family.Power:
        if src_rel in (3,4): dest = 0
        if src_rel in (10,11): dest = 0 if q == ChordQuality.OnePlus8 else 7
    return dest % 12

# ────────────────────────── Transposer (mirrors apply()) ───────────────────────
@dataclass
class CasmZone:
    ntr: int = 0          # 0 = ROOT_TRANS, 1 = ROOT_FIXED
    ntt: int = 1          # 1 = MELODY (default for our tests)

@dataclass
class CasmEntry:
    src_chord_root: int = 0
    chord_mute_raw: bytes = bytes([0xFF]*5)
    note_mute_raw: bytes = bytes([0xFF, 0xFF])
    zone: CasmZone = None
    def __post_init__(self):
        if self.zone is None:
            self.zone = CasmZone()

def chord_allowed(q: ChordQuality, casm: CasmEntry) -> bool:
    bit = int(q)
    if not 0 <= bit < 40: return True
    return ((casm.chord_mute_raw[bit >> 3] >> (bit & 7)) & 1) == 1

def note_allowed(midi: int, casm: CasmEntry) -> bool:
    mute_bits = ((casm.note_mute_raw[0] & 0x0F) << 8) | casm.note_mute_raw[1]
    return ((mute_bits >> pitch_class(midi)) & 1) == 1

def apply_transposer(source_note: int, dest_chord: Chord, casm: CasmEntry):
    """Returns (should_play, dest_note)."""
    if not chord_allowed(dest_chord.quality, casm):
        return False, 0
    if not note_allowed(source_note, casm):
        return False, 0
    z = casm.zone
    if z.ntr == 1:           # ROOT_FIXED
        return True, max(0, min(127, source_note))
    src_root = casm.src_chord_root & 0x0F
    src_pc   = pitch_class(source_note)
    src_rel  = (src_pc - src_root) % 12
    octave   = source_note // 12
    ntt_code = z.ntt & 0x7F
    if ntt_code == 0:
        dest_rel = src_rel
    else:
        dest_rel = melody_table(src_rel, dest_chord.quality)
    dest_pc = (dest_chord.root + dest_rel) % 12
    base = octave * 12 + dest_pc
    n = base
    while n - source_note > 6:  n -= 12
    while source_note - n > 6:  n += 12
    return True, max(0, min(127, n))


# ───────────────────────────────── TESTS ──────────────────────────────────────
TESTS_OK = 0
TESTS_FAIL = 0
def check(label, expected, actual):
    global TESTS_OK, TESTS_FAIL
    if expected == actual:
        TESTS_OK += 1
    else:
        TESTS_FAIL += 1
        print(f"  FAIL  {label}")
        print(f"        expected: {expected}")
        print(f"        actual:   {actual}")

# ─── ChordRecognizer cases ────────────────────────────────────────────────────
print("=== ChordRecognizer ===")

def n(*notes): return list(notes)

# C major in root position
check("C E G              -> CMaj",        Chord(0, ChordQuality.Maj),
      recognize(n(60,64,67)))
# C major root position, with octave
check("C E G C            -> CMaj",        Chord(0, ChordQuality.Maj),
      recognize(n(48,52,55,60)))
# F major
check("F A C              -> FMaj",        Chord(5, ChordQuality.Maj),
      recognize(n(53,57,60)))
# A minor
check("A C E              -> Am",          Chord(9, ChordQuality.Min),
      recognize(n(57,60,64)))
# E minor
check("E G B              -> Em",          Chord(4, ChordQuality.Min),
      recognize(n(52,55,59)))
# C major 7
check("C E G B            -> CMaj7",       Chord(0, ChordQuality.Maj7),
      recognize(n(60,64,67,71)))
# C dominant 7
check("C E G Bb           -> C7",          Chord(0, ChordQuality.Dom7),
      recognize(n(60,64,67,70)))
# A minor 7
check("A C E G            -> Am7",         Chord(9, ChordQuality.Min7),
      recognize(n(57,60,64,67)))
# F major 7 with 9
check("F A C E G          -> FMaj7(9)",    Chord(5, ChordQuality.Maj7_9),
      recognize(n(53,57,60,64,67)))
# G7
check("G B D F            -> G7",          Chord(7, ChordQuality.Dom7),
      recognize(n(55,59,62,65)))
# C diminished
check("C Eb Gb            -> Cdim",        Chord(0, ChordQuality.Dim),
      recognize(n(60,63,66)))
# Cdim7
check("C Eb Gb A          -> Cdim7",       Chord(0, ChordQuality.Dim7),
      recognize(n(60,63,66,69)))
# Csus4
check("C F G              -> Csus4",       Chord(0, ChordQuality.Sus4),
      recognize(n(60,65,67)))
# Caug
check("C E G#             -> Caug",        Chord(0, ChordQuality.Aug),
      recognize(n(60,64,68)))
# Half-diminished
check("B D F A             -> Bm7b5",      Chord(11, ChordQuality.Min7b5),
      recognize(n(59,62,65,69)))
# Just a single note → Maj on that pitch class
check("Single C            -> CMaj (1+8)", Chord(0, ChordQuality.OnePlus8),
      recognize(n(60)))
# Power chord (root+fifth)
check("C G                 -> C 1+5",      Chord(0, ChordQuality.OnePlus5),
      recognize(n(60,67)))
# C7 in 1st inversion still recognised as C7
check("C7 first inversion  -> C7",         Chord(0, ChordQuality.Dom7),
      recognize(n(64,67,70,72)))
# 5-note Dom7(9)
check("C E G Bb D          -> C7(9)",      Chord(0, ChordQuality.Dom7_9),
      recognize(n(60,64,67,70,74)))

# ─── NoteTransposer (ROOT_TRANS + MELODY) cases ───────────────────────────────
print("\n=== NoteTransposer (ROOT_TRANS + MELODY) ===")

def case_melody(label, src_note, dest_root, dest_q,
                expected_play, expected_dest):
    """Standard CASM: source root C, all chord types allowed, all notes allowed."""
    casm = CasmEntry()
    play, dest = apply_transposer(src_note, Chord(dest_root, dest_q), casm)
    check(label, (expected_play, expected_dest), (play, dest))

# CMaj source -> CMaj dest: identity
case_melody("E4 in CMaj                 -> stays E4",      64, 0, ChordQuality.Maj,        True, 64)
case_melody("C4 in CMaj                 -> stays C4",      60, 0, ChordQuality.Maj,        True, 60)
case_melody("G4 in CMaj                 -> stays G4",      67, 0, ChordQuality.Maj,        True, 67)

# CMaj source -> Cm dest: 3rd flattens
case_melody("E4 in Cm                   -> Eb4 (63)",      64, 0, ChordQuality.Min,        True, 63)
case_melody("G4 in Cm                   -> stays G4",      67, 0, ChordQuality.Min,        True, 67)
case_melody("B4 in Cm7                  -> Bb4 (70)",      71, 0, ChordQuality.Min7,       True, 70)
case_melody("B4 in CmMaj7               -> stays B4",      71, 0, ChordQuality.MinMaj7,    True, 71)

# CMaj source -> FMaj dest: transpose up by 5
case_melody("C4 in FMaj                 -> F4 (65)",       60, 5, ChordQuality.Maj,        True, 65)
case_melody("E4 in FMaj                 -> A4 (69)",       64, 5, ChordQuality.Maj,        True, 69)
case_melody("G4 in FMaj                 -> C5 → but staying close: C5(72)",
            67, 5, ChordQuality.Maj, True, 72)

# CMaj source -> Am dest: 3rd flattens, transposed up 9
case_melody("C4 in Am                   -> A3 (57)",       60, 9, ChordQuality.Min,        True, 57)
case_melody("E4 in Am7                  -> C4 (60)",       64, 9, ChordQuality.Min7,       True, 60)
case_melody("B4 in Am7                  -> G4 (67)",       71, 9, ChordQuality.Min7,       True, 67)

# CMaj source -> C7 dest: 7th flattens
case_melody("B4 in C7                   -> Bb4 (70)",      71, 0, ChordQuality.Dom7,       True, 70)
case_melody("E4 in C7                   -> stays E4",      64, 0, ChordQuality.Dom7,       True, 64)

# CMaj source -> Cdim7 dest: 3, 5, 7 all flatten
case_melody("E4 in Cdim7                -> Eb4 (63)",      64, 0, ChordQuality.Dim7,       True, 63)
case_melody("G4 in Cdim7                -> Gb4 (66)",      67, 0, ChordQuality.Dim7,       True, 66)
case_melody("B4 in Cdim7                -> Bbb4 = A4 (69)",71, 0, ChordQuality.Dim7,       True, 69)

# Csus4: 3rd → 4th
case_melody("E4 in Csus4                -> F4 (65)",       64, 0, ChordQuality.Sus4,       True, 65)

# Caug: 5th → #5
case_melody("G4 in Caug                 -> G#4 (68)",      67, 0, ChordQuality.Aug,        True, 68)

# ─── Octave-following: should snap to ±6 semitones of source ──────────────────
case_melody("E4 in BMaj source-pc closer-down", 64, 11, ChordQuality.Maj, True, 63)
# B4+4 = Eb→ closest octave choice: 63 (Eb4 below E4) vs 75 (Eb5 above E4 +11)
# 63 is 1 below; we expect 63.

# ─── ROOT_FIXED: nothing transposes ───────────────────────────────────────────
print("\n=== NoteTransposer (ROOT_FIXED) ===")
def case_fixed(label, src_note, dest_root, dest_q):
    casm = CasmEntry(zone=CasmZone(ntr=1, ntt=0))
    play, dest = apply_transposer(src_note, Chord(dest_root, dest_q), casm)
    check(label, (True, src_note), (play, dest))

case_fixed("C4 in FMaj ROOT_FIXED", 60, 5, ChordQuality.Maj)
case_fixed("E4 in Am  ROOT_FIXED", 64, 9, ChordQuality.Min)
case_fixed("Mega-noise C7 ROOT_FIXED above C6", 84, 7, ChordQuality.Dom7)

# ─── BYPASS NTT (passes through the chord-root transposition without remapping)
print("\n=== NoteTransposer (BYPASS NTT) ===")
def case_bypass(label, src_note, dest_root, dest_q, exp_dest):
    casm = CasmEntry(zone=CasmZone(ntr=0, ntt=0))
    play, dest = apply_transposer(src_note, Chord(dest_root, dest_q), casm)
    check(label, (True, exp_dest), (play, dest))

case_bypass("E4 in Am with BYPASS (chord root rotates, no qual reshape) -> C#4(61)",
            64, 9, ChordQuality.Min, 61)   # E4 → 4 + 9 = 13 % 12 = 1 → C#4 = 61

# ─── Chord-mute ────────────────────────────────────────────────────────────────
print("\n=== Chord mute ===")
# Only Major chord types allowed
mute_maj_only = bytes([0xFF, 0x00, 0x00, 0x00, 0x00])
casm_maj = CasmEntry(chord_mute_raw=mute_maj_only)
p1, _ = apply_transposer(60, Chord(0, ChordQuality.Maj),  casm_maj)
p2, _ = apply_transposer(60, Chord(0, ChordQuality.Min),  casm_maj)
p3, _ = apply_transposer(60, Chord(0, ChordQuality.Dom7), casm_maj)
check("Maj allowed when only major-bits set",  True,  p1)
check("Min muted when only major-bits set",    False, p2)
check("Dom7 muted when only major-bits set",   False, p3)

# Only Min7 allowed (byte 1, bit 2)
mute_min7 = bytes([0x00, 0x04, 0x00, 0x00, 0x00])
casm_min7 = CasmEntry(chord_mute_raw=mute_min7)
p_maj,  _ = apply_transposer(60, Chord(0, ChordQuality.Maj),  casm_min7)
p_min7, _ = apply_transposer(60, Chord(0, ChordQuality.Min7), casm_min7)
check("Maj muted when only Min7 set",  False, p_maj)
check("Min7 allowed when only Min7 set", True,  p_min7)

# ─── Note-mute ─────────────────────────────────────────────────────────────────
print("\n=== Note mute ===")
# Only the C pitch class allowed (bit 0)
note_c_only = bytes([0x00, 0x01])
casm_c = CasmEntry(note_mute_raw=note_c_only)
p_c, _ = apply_transposer(60, Chord(0, ChordQuality.Maj), casm_c)
p_e, _ = apply_transposer(64, Chord(0, ChordQuality.Maj), casm_c)
check("C allowed when only C-bit set", True,  p_c)
check("E muted when only C-bit set",   False, p_e)

# ───────────────────────────── Summary ────────────────────────────────────────
print(f"\nResult: {TESTS_OK} passed, {TESTS_FAIL} failed")
import sys
sys.exit(0 if TESTS_FAIL == 0 else 1)
