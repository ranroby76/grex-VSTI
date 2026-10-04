# Mirror of Harmonizer::compute + the masks it depends on, so the note choice
# can be checked ON PAPER before any of it reaches the engine.
import re

src = open('src/ChordTransposer.h', encoding='utf-8').read()
seg = src[src.find('static uint16_t chordToneMask'):]
seg = seg[:seg.find('\n        static int snapToChordTone')]

# case ChordQuality::X: ... return M({a,b,c});
masks = {}
for m in re.finditer(r'case ChordQuality::(\w+)\s*:\s*return M \(\{([^}]*)\}\);', seg):
    name = m.group(1)
    tones = [int(t.strip()) for t in m.group(2).split(',') if t.strip()]
    masks[name] = sum(1 << (t % 12) for t in tones)
print(f"parsed {len(masks)} quality masks from ChordTransposer.h")

NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def nn(n): return f"{NAMES[n%12]}{n//12-1}"

VOICES = {'Duet':1,'Trio':2,'Block':3,'Octave':1,'Fifth':1}

def compute(played, root, quality, typ, below=True):
    d = -1 if below else 1
    if typ == 'Octave': return [played + d*12]
    if typ == 'Fifth':  return [played + d*7]
    mask = masks.get(quality, 0)
    if mask == 0: return [played + d*12]
    want, out, step = VOICES[typ], [], 1
    while step <= 36 and len(out) < want:
        c = played + d*step
        if c < 0 or c > 127: break
        if mask & (1 << (((c - root) % 12 + 12) % 12)): out.append(c)
        step += 1
    return out

print("\n=== THE CASE THAT PROVES IT IS CHORD-AWARE ===")
print("same melody note, three different chords under it:")
for root, q, label in [(0,'Maj','C  major'), (9,'Min','Am minor'), (5,'Maj7','F  maj7')]:
    got = compute(72, root, q, 'Trio')
    print(f"  C5 over {label:9} -> {[nn(x) for x in got]}")

print("\n=== EVERY TYPE, C5 over Cmaj7 ===")
for t in ['Duet','Trio','Block','Octave','Fifth']:
    got = compute(72, 0, 'Maj7', t)
    print(f"  {t:7} -> {str([nn(x) for x in got]):28} ({len(got)} voice{'s' if len(got)!=1 else ''})")

print("\n=== INVARIANTS ACROSS ALL 34 QUALITIES x 12 ROOTS x FULL RANGE ===")
bad_unison = bad_range = bad_count = bad_nonchord = 0
checked = 0
for q, mask in masks.items():
    for root in range(12):
        for played in range(0, 128, 7):
            for t in ['Duet','Trio','Block']:
                got = compute(played, root, q, t)
                checked += 1
                if played in got: bad_unison += 1
                if any(x < 0 or x > 127 for x in got): bad_range += 1
                if len(got) > VOICES[t]: bad_count += 1
                if mask:
                    for x in got:
                        if not (mask & (1 << (((x-root) % 12 + 12) % 12))): bad_nonchord += 1
print(f"  {checked} cases checked")
print(f"  unison returned      : {bad_unison}")
print(f"  out of MIDI range    : {bad_range}")
print(f"  too many voices      : {bad_count}")
print(f"  note NOT in the chord: {bad_nonchord}")

print("\n=== SPARSE CHORD (1+5 has two tones) - can Block still find three? ===")
for q in ['OnePlus5','OnePlus8']:
    if q in masks:
        got = compute(72, 0, q, 'Block')
        print(f"  C5 over {q:9} -> {[nn(x) for x in got]}  ({len(got)} found)")

print("\n=== BOTTOM OF THE KEYBOARD - partial result rather than nothing ===")
for played in (3, 2, 1, 0):
    got = compute(played, 0, 'Maj', 'Block')
    print(f"  {nn(played):5} over Cmaj -> {[nn(x) for x in got]}")

print("\n=== ABOVE (below=False) ===")
print(f"  C5 over Cmaj, above -> {[nn(x) for x in compute(72,0,'Maj','Trio',below=False)]}")

ok = (bad_unison==0 and bad_range==0 and bad_count==0 and bad_nonchord==0)
print("\n" + ("ALL INVARIANTS HOLD" if ok else "*** INVARIANT VIOLATED ***"))
