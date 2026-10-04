
#pragma once
#include <JuceHeader.h>
#include "StyleData.h"

//==============================================================================
//  FstLoader.h — READ A CONVERTED .fgt STYLE INTO Betel::StyleData.
//
//  ── GREX READS .fgt.  BALLADA READS .fst.  SAME CONTAINER. ──────────────────
//
//  The EXTENSION and the ACCESS CODE are the two things that separate the two
//  products' libraries, and they are separate on purpose:
//
//     the extension  keeps the two libraries apart in one folder and in a file
//                    dialog, before anything is opened;
//     the access code keeps them apart INSIDE the file, so a renamed .fst is
//                    still refused rather than half-read.
//
//  THE MAGIC IS UNCHANGED - kMagic is still 'FSTY'.  The converter writes that
//  word into every file it produces whatever it names the output, so demanding
//  a different one here would refuse every style Rob has.  If the converter is
//  ever taught to stamp .fgt files differently, kMagic changes with it and
//  nothing else in this file does.
//
//  The layout this parses is described in ONE place: the converter's
//  StyleFormat.h, at D:\workspace\STYLE_CONVERTER\src\StyleFormat.h. That header
//  says so itself — "when Grex learns to load these it must use this file rather
//  than a copy" — and the constants below are copied from it deliberately and
//  visibly, so a version bump there fails loudly here rather than silently
//  misreading. THE VERSION CHECK IS WHAT MAKES THAT SAFE: a file this build does
//  not understand is REFUSED, never guessed at.
//
//  ── WHY THIS IS SO MUCH SMALLER THAN StyleLoader ────────────────────────────
//
//  StyleLoader.cpp is ~1,500 lines because an SFF file is a puzzle: running
//  status, section markers on a shared clock, CASM chunks with two record
//  layouts, drum keys that mean different things per bank, velocity curves,
//  octave folds, Revo substitutions.
//
//  A .fgt has none of that. EVERY DECISION IS ALREADY MADE. A note carries the
//  key it plays and the velocity it plays at; a silenced key is not in the file
//  at all. This loader reads numbers and hands them over. That is the entire
//  point of the converter, and this file is where the plugin finally collects
//  on it.
//
//  ── WHAT IS STILL RECONSTRUCTED, AND WHY ────────────────────────────────────
//
//  StyleData is an EVENT LIST — the sequencer walks note-ons and note-offs on a
//  timeline. A .fgt stores NOTES: key, velocity, start, length. So this loader
//  expands each note back into an on/off pair and sorts them.
//
//  That is not lost work. The expansion is trivial and total, where the SFF path
//  had to infer note lengths by pairing events that might never have paired. A
//  length of -1 — "never closed in the source" — becomes a note held to the end
//  of its section, which is the only reading that cannot leave a note hanging.
//
//  ── ONE CODE PER PRODUCT ────────────────────────────────────────────────────
//
//  The header carries a hash of the access code and the payload from the string
//  table onward is scrambled with it. Grex holds ONE code; a style converted for
//  a different product refuses to open and says which problem it is.
//
//  The scramble is a keyed PRNG, NOT encryption — the converter's own header is
//  emphatic about this and so is this one. It separates products and stops
//  accidents. It would not stop anyone who read either file.
//==============================================================================
namespace Betel
{
namespace Fst
{
    //==========================================================================
    //  FROM THE CONVERTER'S StyleFormat.h. Any disagreement here is a silent
    //  misread, so they are grouped, named identically, and never "tidied".
    constexpr juce::uint32 kMagic       = 0x59545346;   // little-endian 'FSTY'
    constexpr juce::uint16 kVersion     = 5;

    constexpr int kHeaderSize        = 96;
    constexpr int kSectionRecSize    = 24;
    constexpr int kPartRecSize       = 36;
    constexpr int kNoteRecSize       = 12;
    constexpr int kControlRecSize    = 8;
    constexpr int kRuleRecSize       = 48;
    constexpr int kDrumSetupRecSize  = 24;

    /** Part flags, as written. */
    enum PartFlags : juce::uint8 { partIsDrums = 1 << 0 };

    /** Rule flags, as written. */
    enum RuleFlags : juce::uint8 { ruleBassOn = 1 << 0 };

    //  THE NTR AND NTT NOW ARRIVE AS RAW BYTES, one pair per pitch zone, in
    //  exactly the form CasmZone holds them. v4 wrote a mapped TransposeRule
    //  enum and no NTT at all, so this loader had to invent one - it used
    //  Bypass, and every converted style therefore carried its source chord's
    //  own intervals onto whatever the player pressed. There is no enum to map
    //  through any more, which is the point: a byte that is copied cannot be
    //  mapped wrongly.

    //==========================================================================
    /** FNV-1a over the raw UTF-8 bytes, nudged off zero.

        COPIED EXACTLY from the converter, including the nudge: it reserves 0 to
        mean "no code at all", so a code that happened to hash to zero would
        make a protected file look unprotected and unscramble to noise. */
    inline juce::uint32 hashAccessCode (const juce::String& code) noexcept
    {
        if (code.isEmpty()) return 0;

        juce::uint32 h = 2166136261u;
        for (const char* p = code.toRawUTF8(); *p != 0; ++p)
        {
            h ^= (juce::uint32) (juce::uint8) *p;
            h *= 16777619u;
        }
        return h == 0 ? 1u : h;
    }

    /** Counter-mode xorshift32 keyed on the code hash and the byte's REAL file
        offset, so a region can be unscrambled without replaying the stream from
        the start. Symmetric: the same call scrambles and unscrambles. */
    inline void scramble (void* data, size_t numBytes, juce::uint32 keyHash,
                          juce::uint32 streamOffset) noexcept
    {
        if (keyHash == 0 || data == nullptr) return;

        auto* p = static_cast<juce::uint8*> (data);
        for (size_t i = 0; i < numBytes; ++i)
        {
            juce::uint32 x = keyHash ^ (streamOffset + (juce::uint32) i) * 2654435761u;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            p[i] = (juce::uint8) (p[i] ^ (x & 0xFF));
        }
    }

    //==========================================================================
    /** GREX'S ACCESS CODE.

        The same string the converter must be given when it writes a .fgt, and
        the ONLY thing separating this library from another product's now that
        the format's product ID is gone. Change it and every already-converted
        style stops opening.

        BALLADA'S IS "111222" AND MUST STAY DIFFERENT.  If the two ever matched,
        the free plugin's library would open in the paid one and the paid one's
        in the free plugin, which is the whole reason the code exists. */
    inline const char* const kAccessCode = "333333";

    /** The file extension Grex's library uses.  Named once, here, because it is
        referenced by the library scan, the "no styles found" report and every
        message that tells a player what was looked for - and three spellings of
        it is how one of them ends up saying .fst forever. */
    inline const char* const kFileExtension = ".fgt";
    inline const char* const kFileWildcard  = "*.fgt";

    //==========================================================================
    /** Parse .fgt bytes into a StyleData.

        Returns false with `errorMsg` set. The failures a user can act on —
        wrong code, unknown version — say exactly that rather than "corrupt". */
    bool loadFromBytes (const void* data, size_t numBytes,
                        StyleData& out, juce::String& errorMsg,
                        const juce::String& accessCode = kAccessCode);

    bool loadFromFile (const juce::File& file, StyleData& out,
                       juce::String& errorMsg,
                       const juce::String& accessCode = kAccessCode);

    /** Header-only peek for the browser: the style's display name, tempo and
        meter without decoding a single note.

        Deliberately does NOT need the access code — the converter leaves the
        header and the section/part tables in the clear precisely so a library
        can be listed without holding it. Reads only kHeaderSize bytes plus the
        one string it needs. */
    struct Summary
    {
        bool         ok = false;
        juce::String name;
        double       tempo = 120.0;
        int          timeSigNum = 4, timeSigDen = 4;
        int          sectionCount = 0;
        juce::uint32 accessCodeHash = 0;
    };

    Summary peek (const juce::File& file);

} // namespace Fst
} // namespace Betel




