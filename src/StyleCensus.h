
#pragma once
//==============================================================================
// StyleCensus.h - what does the LIBRARY actually ask for?
//
// Two questions that no data sheet can answer, only the styles themselves:
//
//   1. Which drum kits does the library request?  Every msb/lsb/pc on a rhythm
//      channel, ranked, with the folders that ask.  That is the list a fallback
//      table has to cover - and the unnamed entries are exactly the kits that
//      need a folder, a sample set, or a rule.
//
//   2. Which notes below 35 does it play?  The XG region from 13 to 34 is not a
//      fixed map: in Standard/Room/Jazz kits those keys are Surdo, scratches and
//      metronome clicks, while in Dance/Techno/HipHop kits the SAME keys are
//      kicks, snares and hi-hats.  So a note-indexed substitution table cannot
//      be right everywhere, and guessing donors from a data sheet is how the
//      current scrambling got in.
//
// ── PARSE, DON'T LOAD ────────────────────────────────────────────────────────
//
// StyleLoader::loadFromFile is static and touches no engine state, so a whole
// style is readable without decoding one sample or composing one kit.  A real
// load is the biggest message-thread burst in the plugin; 600 of those would
// take most of an hour.  This is seconds.
//
// ── STEPPED, NOT BLOCKING ────────────────────────────────────────────────────
//
// A few files per timer tick, so the window stays alive and the count moves.
// Same shape as SetBaker, for the same reason.
//
// The report is written to grex_style_census.txt under the plugin root.
//==============================================================================

#include <JuceHeader.h>
#include <map>
#include <vector>

#include "StyleData.h"
#include "StyleLoader.h"
#include "GlobalMacros.h"

namespace Betel
{
    class StyleCensus : private juce::Timer
    {
    public:
        std::function<void (int done, int total)>            onProgress;
        std::function<void (const juce::File&, int scanned)> onFinished;

        StyleCensus() = default;
        ~StyleCensus() override { stopTimer(); }

        bool isRunning() const noexcept { return running; }
        int  total()     const noexcept { return files.size(); }

        static bool isStyleFile (const juce::File& f)
        {
            return f.hasFileExtension ("sty;prs;bcs;sst;pst;pcs;fps;scp;aus");
        }

        bool start (const juce::File& stylesRoot)
        {
            if (running || ! stylesRoot.isDirectory()) return false;

            files.clear();
            for (const auto& e : juce::RangedDirectoryIterator (stylesRoot, true, "*",
                                                                juce::File::findFiles))
                if (isStyleFile (e.getFile())) files.add (e.getFile());

            if (files.isEmpty()) return false;

            root = stylesRoot;
            index = 0;
            scanned = failed = 0;
            kitCount.clear();
            kitFolders.clear();
            lowNote.clear();
            lowNoteFolders.clear();
            folderFiles.clear();
            running = true;
            startTimer (1);
            return true;
        }

        void cancel() { if (running) finish(); }

    private:
        static constexpr int kPerTick = 6;

        // Yamaha SFF source channels: 8 = RHY1, 9 = RHY2.
        static bool isRhythm (int ch) { return ch == 8 || ch == 9; }

        void timerCallback() override
        {
            for (int i = 0; i < kPerTick && index < files.size(); ++i, ++index)
                scanOne (files[index]);

            if (onProgress) onProgress (index, files.size());
            if (index >= files.size()) finish();
        }

        void scanOne (const juce::File& f)
        {
            const auto folder = f.getParentDirectory().getFileName();
            folderFiles[folder]++;

            StyleData style;
            juce::String err;
            if (! StyleLoader::loadFromFile (f, style, err)) { ++failed; return; }
            ++scanned;

            // ── kits: the SETUP's bank/program on each rhythm source ──────────
            for (int ch = 0; ch < (int) style.voices.size(); ++ch)
            {
                if (! isRhythm (ch)) continue;
                const auto& v = style.voices[(size_t) ch];
                if (v.program < 0) continue;

                const auto key = juce::String (v.bankMsb >= 0 ? v.bankMsb : -1) + "/"
                               + juce::String (v.bankLsb >= 0 ? v.bankLsb : 0)  + "/"
                               + juce::String (v.program);
                kitCount[key]++;
                kitFolders[key][folder]++;
            }

            // ── low notes: note-ons under 35 on a rhythm channel ──────────────
            //
            // A section holds INDICES into StyleData::events, not events of its
            // own - one flat event list, sliced per section - so the lookup goes
            // through eventIdx.
            for (const auto& sec : style.sections)
            {
                if (! sec.present) continue;
                for (const size_t ei : sec.eventIdx)
                {
                    if (ei >= style.events.size()) continue;
                    const auto& e = style.events[ei];

                    if ((e.status & 0xF0) != 0x90 || e.data2 == 0) continue;
                    if (! isRhythm (e.channel)) continue;
                    if (e.data1 >= 35) continue;
                    lowNote[e.data1]++;
                    lowNoteFolders[e.data1][folder]++;
                }
            }
        }

        /** RAW MIDI program bytes - Yamaha's lists print PC# from 1, so every
            number here is theirs minus one.  Confirmed both ways: the P-525 list
            gives Pop Latin as 126-0-44 while the PSR-S900 / Tyros-2 docs give the
            same kit as [126,0,43] raw, and a style file writes the raw byte. */
        static juce::String kitName (const juce::String& key)
        {
            static const std::map<juce::String, juce::String> names = {
                { "127/0/0",  "Standard Kit 1" }, { "127/0/1",  "Standard Kit 2" },
                { "127/0/8",  "Room Kit" },       { "127/0/16", "Rock Kit" },
                { "127/0/24", "Electro Kit" },    { "127/0/25", "Analog Kit" },
                { "127/0/27", "Dance Kit" },      { "127/0/32", "Jazz Kit" },
                { "127/0/40", "Brush Kit" },      { "127/0/41", "Real Brushes" },
                { "127/0/48", "Symphony Kit" },   { "127/0/56", "Hip Hop Kit" },
                { "127/0/57", "Break Kit" },      { "127/0/87", "Power Kit" },
                { "127/0/91", "Real Drums" },
                { "126/0/0",  "SFX Kit 1" },      { "126/0/1",  "SFX Kit 2" },
                { "126/0/35", "Arabic Kit" },     { "126/0/40", "Cuban Kit" },
                { "126/0/43", "Pop Latin Kit" },
                { "120/0/0",  "GM2 Standard" },   { "120/0/8",  "GM2 Room" },
                { "120/0/16", "GM2 Power" },      { "120/0/24", "GM2 Electronic" },
                { "120/0/25", "GM2 Analog" },     { "120/0/32", "GM2 Jazz" },
                { "120/0/40", "GM2 Brush" },      { "120/0/48", "GM2 Orchestra" }
            };
            auto it = names.find (key);
            return it != names.end() ? it->second : juce::String();
        }

        static const char* xgName (int note)
        {
            switch (note)
            {
                case 13: return "Surdo Mute";   case 14: return "Surdo Open";
                case 15: return "Hi Q";         case 16: return "Whip Slap";
                case 17: return "Scratch H";    case 18: return "Scratch L";
                case 19: return "Finger Snap";  case 20: return "Click Noise";
                case 21: return "Metro Click";  case 22: return "Metro Bell";
                case 23: return "Seq Click L";  case 24: return "Seq Click H";
                case 25: return "Brush Tap";    case 26: return "Brush Swirl";
                case 27: return "Brush Slap";   case 28: return "BrushTapSwirl";
                case 29: return "Snare Roll";   case 30: return "Castanet";
                case 31: return "Snare Soft";   case 32: return "Sticks";
                case 33: return "Kick Soft";    case 34: return "Open Rim Shot";
                default: return "";
            }
        }

        void finish()
        {
            stopTimer();
            running = false;

            juce::String r;
            r << "GREX STYLE CENSUS" << juce::newLine
              << juce::String::repeatedString ("=", 78) << juce::newLine
              << "root    : " << root.getFullPathName() << juce::newLine
              << "scanned : " << scanned << " of " << files.size()
              << " (" << failed << " unreadable)" << juce::newLine
              << "date    : " << juce::Time::getCurrentTime().toString (true, true)
              << juce::newLine << juce::newLine;

            // ── kit census ────────────────────────────────────────────────────
            r << juce::String::repeatedString ("=", 78) << juce::newLine
              << "KIT CENSUS - every msb/lsb/pc requested on a rhythm channel"
              << juce::newLine
              << "PC is the RAW MIDI byte; Yamaha data lists print it +1."
              << juce::newLine
              << juce::String::repeatedString ("=", 78) << juce::newLine;

            std::vector<std::pair<juce::String, int>> kits (kitCount.begin(), kitCount.end());
            std::sort (kits.begin(), kits.end(),
                       [] (auto& a, auto& b) { return a.second > b.second; });

            juce::StringArray unnamed;
            for (const auto& k : kits)
            {
                const auto nm = kitName (k.first);
                if (nm.isEmpty()) unnamed.add (k.first);

                r << "  " << k.first.paddedRight (' ', 12)
                  << (nm.isEmpty() ? juce::String ("???") : nm).paddedRight (' ', 18)
                  << juce::String (k.second).paddedLeft (' ', 6) << "  ";

                juce::StringArray fs;
                for (const auto& f : kitFolders[k.first])
                    fs.add (f.first + "(" + juce::String (f.second) + ")");
                r << fs.joinIntoString (", ") << juce::newLine;

                if (k.first.startsWith ("126/"))
                    r << "              ^ ethnic/SFX - must NOT fall back to a "
                         "bank-127 drum kit" << juce::newLine;
            }

            r << juce::newLine << "  " << (int) kits.size() << " distinct kits, "
              << unnamed.size() << " UNNAMED." << juce::newLine;
            if (! unnamed.isEmpty())
                r << "  Need a folder, a sample set, or a fallback rule:"
                  << juce::newLine << "     " << unnamed.joinIntoString ("  ")
                  << juce::newLine;

            // ── low notes ─────────────────────────────────────────────────────
            r << juce::newLine << juce::String::repeatedString ("=", 78) << juce::newLine
              << "NOTES BELOW 35 ON RHYTHM CHANNELS" << juce::newLine
              << "25..34 are substituted today; 13..24 are NOT." << juce::newLine
              << juce::String::repeatedString ("=", 78) << juce::newLine;

            if (lowNote.empty())
                r << "  none - nothing in the library plays below 35." << juce::newLine;

            for (const auto& n : lowNote)
            {
                r << "  " << juce::String (n.first).paddedLeft (' ', 4) << "  "
                  << juce::String (xgName (n.first)).paddedRight (' ', 15)
                  << juce::String (n.second).paddedLeft (' ', 7) << " hits";
                if (n.first < 25) r << "   <<< NOT SUBSTITUTED";
                r << juce::newLine;

                juce::StringArray fs;
                for (const auto& f : lowNoteFolders[n.first])
                    fs.add (f.first + "(" + juce::String (f.second) + ")");
                r << "        " << fs.joinIntoString (", ") << juce::newLine;
            }

            // ── folder tally ──────────────────────────────────────────────────
            r << juce::newLine << juce::String::repeatedString ("=", 78) << juce::newLine
              << "FILES PER FOLDER" << juce::newLine
              << juce::String::repeatedString ("=", 78) << juce::newLine;
            for (const auto& f : folderFiles)
                r << "  " << f.first.paddedRight (' ', 16) << f.second << juce::newLine;

            const auto out = GrexPaths::root().getChildFile ("grex_style_census.txt");
            out.getParentDirectory().createDirectory();
            out.replaceWithText (r);

            if (onFinished) onFinished (out, scanned);
        }

        juce::Array<juce::File> files;
        juce::File root;
        int  index = 0, scanned = 0, failed = 0;
        bool running = false;

        std::map<juce::String, int>                               kitCount;
        std::map<juce::String, std::map<juce::String, int>>       kitFolders;
        std::map<int, int>                                        lowNote;
        std::map<int, std::map<juce::String, int>>                lowNoteFolders;
        std::map<juce::String, int>                               folderFiles;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleCensus)
    };
} // namespace Betel
