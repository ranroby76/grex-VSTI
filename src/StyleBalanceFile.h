#pragma once
//==============================================================================
// StyleBalanceFile.h  -  balance.grexv, the per-instrument STYLE gain trim.
//
// A PLAIN TEXT FILE in the Grex root, one GM program per line:
//
//     000 Acoustic Grand Piano=0
//     001 Bright Acoustic Piano=-6
//
// The number after '=' is a dB trim. 0 is unity. It is read on startup and
// again on every style load, so the edit-and-listen loop is "save the file,
// switch style and back" with no restart.
//
// WHAT IT IS FOR, and the boundaries that follow from it:
//
//   * STYLE CHANNELS ONLY. A sound loaded on a SOLO slot is the player's own
//     right hand and is never touched. Same instrument, two roles, one trim.
//   * DRUM SLOTS FALL OUT FOR FREE - a kit carries no GM program, so its
//     sounding flag is -1 and no line can match it. No special case needed.
//   * It is a FOURTH, INDEPENDENT gain factor, multiplied with the style's own
//     level, the mixer fader and the per-slot base trim. It does not move any
//     of them and it appears in no UI. That is deliberate: this is a library
//     calibration file, not a user control.
//
// PARSING IS DELIBERATELY FORGIVING. The leading 3-digit number is the only
// thing that identifies a line; the name is for the human reading it and may
// be edited freely. Blank lines, '#' or ';' comments, stray whitespace and
// out-of-range numbers are skipped rather than treated as errors, because a
// half-typed file should cost one instrument's trim, not the whole calibration.
//==============================================================================

#include <JuceHeader.h>
#include "GlobalMacros.h"
#include <array>

namespace Betel
{
    struct StyleBalanceFile
    {
        //----------------------------------------------------------------------
        //  ***  THE MASTER SWITCH.  THIS IS THE LINE TO CHANGE.  ***
        //
        //  FALSE = balance.grexv is not read, not written, and not applied.
        //  Every lookup answers unity, so every style channel carries exactly
        //  what its sample and its fader produce.
        //
        //  WHY IT IS OFF: it was the second of two calibration layers stacked on
        //  the style bus, and the first one - StyleLoudness - is now off too.
        //  Turning both off is what makes the style bus honest: one sample, one
        //  fader, nothing correcting anything behind them.
        //
        //  THE FILE IS LEFT ON DISK, NOT DELETED, and note that load() no longer
        //  writes a TEMPLATE either: with the switch off, a missing balance.grexv
        //  must stay missing rather than have a fresh all-unity file appear next
        //  to the one that was there.  Flipping this back to true restores the
        //  existing calibration in full, with nothing to re-enter.
        //----------------------------------------------------------------------
        static constexpr bool kApplyBalanceTrim = false;

        static constexpr const char* kFileName = "balance.grexv";

        /** dB per GM program, 0 = unity. Index IS the program number. */
        std::array<float, 128> db {};

        /** dB per DRUM ELEMENT ROLE, 0 = unity. Index IS the role id.
            Written as `Dnnn Name=dB` so a role id can never be mistaken for a
            GM program - the two number spaces overlap and the prefix is what
            keeps them apart. */
        // 171 is the top of the XG sub-GM band (keys 13..24 -> 160..171).
        // Raised from 149, which was exactly the old maximum and therefore a
        // silent ceiling: a role added above it gets no template line and a
        // unity trim, with nothing to say so.  200 leaves real headroom.
        static constexpr int kMaxRoleId = 200;
        std::array<float, kMaxRoleId + 1> roleDb {};

        static juce::File fileLocation()
        {
            return GrexPaths::root().getChildFile (kFileName);
        }

        /** Read the file, creating a full 128-line template at unity if it is
            not there yet. Never throws, never leaves `db` partly written:
            everything starts at 0 and only recognised lines move. */
        void load()
        {
            // UNITY FIRST, ALWAYS.  Both arrays are zeroed before anything else
            // so an early return leaves a fully defined, fully neutral table
            // rather than whatever was in them last.
            db.fill (0.0f);
            roleDb.fill (0.0f);

            // OFF MEANS THE FILE IS NEVER TOUCHED - not read, and not created.
            if (! kApplyBalanceTrim) return;

            const auto f = fileLocation();
            if (! f.existsAsFile())
            {
                writeTemplate (f);
                return;                      // a fresh template is all-unity
            }

            juce::StringArray lines;
            lines.addLines (f.loadFileAsString());

            for (const auto& raw : lines)
            {
                const auto line = raw.trim();
                if (line.isEmpty() || line.startsWithChar ('#') || line.startsWithChar (';'))
                    continue;

                const int eq = line.indexOfChar ('=');
                if (eq <= 0) continue;

                // The program number is the leading digits, however the rest of
                // the line is spelled or spaced.
                auto head = line.substring (0, eq).trim();

                // A LEADING 'D' MEANS A DRUM ROLE, everything else is a GM
                // program. One character keeps the two number spaces apart.
                const bool isRole = head.startsWithIgnoreCase ("D");
                if (isRole) head = head.substring (1).trim();

                int idx = -1;
                {
                    juce::String digits;
                    for (int i = 0; i < head.length(); ++i)
                    {
                        const auto c = head[i];
                        if (c >= '0' && c <= '9') digits += c;
                        else break;
                    }
                    if (digits.isNotEmpty()) idx = digits.getIntValue();
                }

                const auto valueText = line.substring (eq + 1).trim();
                if (valueText.isEmpty()) continue;

                const float v = juce::jlimit (-40.0f, 40.0f, valueText.getFloatValue());

                if (isRole)
                {
                    if (idx >= 0 && idx <= kMaxRoleId) roleDb[(size_t) idx] = v;
                }
                else
                {
                    if (idx >= 0 && idx <= 127) db[(size_t) idx] = v;
                }
            }
        }

        /** Linear multiplier for a program, or 1.0 for anything unrecognised
            (which includes a drum slot's -1). */
        float gainFor (int gmProgram) const noexcept
        {
            if (! kApplyBalanceTrim) return 1.0f;

            if (gmProgram < 0 || gmProgram > 127) return 1.0f;
            const float d = db[(size_t) gmProgram];
            return d == 0.0f ? 1.0f
                             : juce::Decibels::decibelsToGain (d, -40.0f);
        }

        /** Linear multiplier for a drum element role, 1.0 for anything
            unrecognised. Every composed region carries its roleId, so this is
            applied once at kit-compose time rather than per note. */
        float roleGainFor (int roleId) const noexcept
        {
            if (! kApplyBalanceTrim) return 1.0f;

            if (roleId <= 0 || roleId > kMaxRoleId) return 1.0f;
            const float d = roleDb[(size_t) roleId];
            return d == 0.0f ? 1.0f
                             : juce::Decibels::decibelsToGain (d, -40.0f);
        }

        static const char* gmName (int prog) noexcept
        {
            return (prog >= 0 && prog < 128) ? kGmNames[(size_t) prog] : "-";
        }

    private:
        static void writeTemplate (const juce::File& f)
        {
            juce::String out;
            out << "# Grex STYLE balance - per-GM-instrument gain trim, in dB.\r\n"
                << "#\r\n"
                << "# Applies ONLY when the instrument is loaded on a STYLE channel.\r\n"
                << "# A sound on a SOLO slot is never touched by this file.\r\n"
                << "# Drum slots carry a kit rather than a GM program, so no line affects them.\r\n"
                << "#\r\n"
                << "# 0 = unity. Negative is quieter, positive is louder. Range -40..+40.\r\n"
                << "# The leading 3-digit number identifies the line; rename the text freely.\r\n"
                << "# Re-read on every style load - save this file and switch style to hear it.\r\n"
                << "#\r\n";

            for (int i = 0; i < 128; ++i)
                out << juce::String (i).paddedLeft ('0', 3) << " " << kGmNames[(size_t) i] << "=0\r\n";

            out << "\r\n"
                << "# ── DRUM ELEMENT ROLES ─────────────────────────────────────────\r\n"
                << "# Per-DEPARTMENT trim, applied when a kit is composed - so it hits\r\n"
                << "# every kick in every kit, not one key in one kit.\r\n"
                << "# The leading D is what tells a role id from a GM program.\r\n"
                << "\r\n";

            for (const auto& r : kRoles)
                out << "D" << juce::String (r.id).paddedLeft ('0', 3)
                    << " " << r.name << "=0\r\n";

            f.getParentDirectory().createDirectory();
            f.replaceWithText (out);
        }

        struct RoleEntry { int id; const char* name; };
        static constexpr RoleEntry kRoles[] = {
            { 160, "Surdo Mute" },
            { 161, "Surdo Open" },
            { 162, "Hi Q" },
            { 163, "Whip Slap" },
            { 164, "Scratch H" },
            { 165, "Scratch L" },
            { 166, "Finger Snap (low)" },
            { 167, "Click Noise" },
            { 168, "Metronome Click" },
            { 169, "Metronome Bell" },
            { 170, "Seq Click L" },
            { 171, "Seq Click H" },
            {  80, "Brush Tap" },
            {  81, "Brush Swirl" },
            {  82, "Brush Slap" },
            {  83, "Brush Tap Swirl" },
            {  84, "Snare Roll" },
            {  85, "Castanet" },
            {  86, "Snare Soft" },
            {  87, "Sticks" },
            {  88, "Bass Drum Soft" },
            {  89, "Open Rim Shot" },
            {   1, "Acoustic Bass Drum" },
            {   2, "Kick" },
            {   3, "Side Stick" },
            {   4, "Acoustic Snare" },
            {   5, "Hand Clap" },
            {   6, "Electric Snare" },
            {   7, "Low Floor Tom" },
            {   8, "Closed Hi-Hat" },
            {   9, "High Floor Tom" },
            {  10, "Pedal Hi-Hat" },
            {  11, "Low Tom" },
            {  12, "Open Hi-Hat" },
            {  13, "Low-Mid Tom" },
            {  14, "Hi-Mid Tom" },
            {  15, "Crash Cymbal 1" },
            {  16, "High Tom" },
            {  17, "Ride Cymbal 1" },
            {  18, "Chinese Cymbal" },
            {  19, "Ride Bell" },
            {  20, "Tambourine" },
            {  21, "Splash Cymbal" },
            {  22, "Cowbell" },
            {  23, "Crash Cymbal 2" },
            {  24, "Vibraslap" },
            {  25, "Ride Cymbal 2" },
            {  26, "Hi Bongo" },
            {  27, "Low Bongo" },
            {  28, "Mute Hi Conga" },
            {  29, "Open Hi Conga" },
            {  30, "Low Conga" },
            {  31, "High Timbale" },
            {  32, "Low Timbale" },
            {  33, "High Agogo" },
            {  34, "Low Agogo" },
            {  35, "Cabasa" },
            {  36, "Maracas" },
            {  37, "Short Whistle" },
            {  38, "Long Whistle" },
            {  39, "Short Guiro" },
            {  40, "Long Guiro" },
            {  41, "Claves" },
            {  42, "Hi Wood Block" },
            {  43, "Low Wood Block" },
            {  44, "Mute Cuica" },
            {  45, "Open Cuica" },
            {  46, "Mute Triangle" },
            {  47, "Open Triangle" },
            {  48, "Shaker" },
            {  49, "Jingle Bell" },
            {  50, "808 Kick" },
            {  51, "808 Sub Kick" },
            {  52, "808 Snare" },
            {  53, "808 Clap" },
            {  54, "808 Rim" },
            {  55, "808 Cowbell" },
            {  56, "808 Closed Hat" },
            {  57, "808 Open Hat" },
            {  58, "808 Conga" },
            {  59, "808 Maracas" },
            {  60, "909 Kick" },
            {  61, "909 Snare" },
            {  62, "909 Clap" },
            {  63, "909 Closed Hat" },
            {  64, "909 Open Hat" },
            {  65, "909 Ride" },
            {  66, "909 Crash" },
            {  67, "909 Tom High" },
            {  68, "909 Tom Mid" },
            {  69, "909 Tom Low" },
            {  70, "Trap Kick" },
            {  71, "Trap Snare Snap" },
            {  72, "Roll Long" },
            {  73, "Roll Short" },
            {  74, "Noise Rise" },
            {  75, "Noise Fall" },
            {  76, "Reverse Cymbal" },
            {  77, "Vinyl Crack" },
            {  78, "Clap Stack" },
            {  79, "Finger Snap" },
            { 100, "Darbuka Doum" },
            { 101, "Darbuka Tak" },
            { 102, "Darbuka Snap" },
            { 103, "Doumbek Doum" },
            { 104, "Doumbek Tak" },
            { 105, "Riq Tek" },
            { 106, "Riq Sak" },
            { 107, "Riq Open" },
            { 108, "Tabla Na" },
            { 109, "Tabla Tin" },
            { 110, "Tabla Tete" },
            { 111, "Tabla Bayan" },
            { 112, "Sagat Closed" },
            { 113, "Sagat Open" },
            { 114, "Def Tek" },
            { 115, "Def Sak" },
            { 116, "Tar Doum" },
            { 117, "Tar Tek" },
            { 118, "Bendir Hit" },
            { 119, "Mazhar Hit" },
            { 120, "Pandeiro Closed" },
            { 121, "Pandeiro Open" },
            { 122, "Pandeiro Shake" },
            { 123, "Surdo Open" },
            { 124, "Surdo Muted" },
            { 125, "Caixa Snare" },
            { 126, "Repinique" },
            { 127, "Tamborim" },
            { 128, "Agogo Big" },
            { 129, "Agogo Small" },
            { 130, "Conga Slap" },
            { 131, "Conga Heel" },
            { 132, "Bongo Martillo" },
            { 133, "Quinto" },
            { 134, "Tumba" },
            { 135, "Chekere" },
            { 136, "Guira" },
            { 140, "Djembe Bass" },
            { 141, "Djembe Tone" },
            { 142, "Djembe Slap" },
            { 143, "Taiko Hit" },
            { 144, "Dhol Bass" },
            { 145, "Dhol Tek" },
            { 146, "Frame Drum Hit" },
            { 147, "Shaker" },
            { 148, "Tambura Drone" },
            { 149, "Kalimba Note" }
        };

        static constexpr const char* kGmNames[128] = {
        "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
        "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
        "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
        "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
        "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
        "Reed Organ", "Accordion", "Harmonica", "Bandoneon",
        "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
        "Electric Guitar (muted)", "Electric Guitar (overdrive)", "Electric Guitar (distortion)", "Electric Guitar (harmonics)",
        "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (picked)", "Electric Bass (fretless)",
        "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
        "Violin", "Viola", "Cello", "Contrabass",
        "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
        "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
        "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
        "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
        "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
        "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
        "Oboe", "English Horn", "Bassoon", "Clarinet",
        "Piccolo", "Flute", "Recorder", "Pan Flute",
        "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
        "Lead 1", "Lead 2", "Lead 3", "Lead 4",
        "Lead 5", "Lead 6", "Lead 7", "Lead 8",
        "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)",
        "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)", "FX 1 (rain)",
        "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)",
        "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)", "FX 9 (new age)",
        "Sitar", "Banjo", "Shamisen", "Koto",
        "Kalimba", "Bag Pipe", "Fiddle", "Shanai",
        "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
        "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
        "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
        "Telephone Ring", "Helicopter", "Applause", "Gunshot"
        };
    };
}