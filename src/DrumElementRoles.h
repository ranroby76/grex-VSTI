#pragma once
//==============================================================================
// DrumElementRoles.h
//
// The role catalog for drum-kit elements.  Each region inside a drum-kit blob
// carries an `elementRoleId` byte (see BlobFormat::RegionPOD).  This file
// defines what those IDs MEAN — display names, MIDI defaults, and category
// groupings used by the DrumsPopup UI to filter the per-key element picker.
//
// NUMBERING (stable across versions — do NOT renumber existing entries):
//   0          = Unset (a region in a melodic preset, or empty drum slot)
//   1   ..  47 = GM percussion (matches MIDI 35..81 with role = MIDI - 34)
//   50  ..  79 = Extended electronic (TR-808/909, modern trap, EDM)
//   100 .. 119 = Arabic / Middle Eastern percussion
//   120 .. 139 = Latin extended (beyond what GM covers)
//   140 .. 159 = World / Ethnic
//   160 +      = Reserved for future expansion
//
// The blob-writer tool assigns these IDs when building a kit.frb. The DrumsPopup
// reads them via DrumKitRegistry to populate the per-key element dropdown,
// filtering by role so a "kick" key shows kicks from every loaded kit.
//==============================================================================

#include <JuceHeader.h>
#include <cstdint>
#include <vector>

namespace Betel
{
    //==========================================================================
    // Role IDs — stored on disk inside RegionPOD.elementRoleId (uint8_t).
    //
    // Names follow the GM percussion vocabulary.  When a role overlaps with
    // multiple cultural names (e.g. "Closed Hi-Hat" vs "Riq Tek"), pick the
    // one that matches the source kit's idiom.  This is the role of the
    // SAMPLE, not the function on the keyboard — a Riq Tek loaded onto MIDI
    // 42 still has role = RiqTek, not ClosedHiHat.
    //==========================================================================
    enum class DrumElementRole : uint8_t
    {
        Unset                = 0,

        // ── GM percussion (role = MIDI key - 34) ────────────────────────────
        AcousticBassDrum     = 1,    // MIDI 35
        Kick                 = 2,    // MIDI 36  (a.k.a. Bass Drum 1)
        SideStick            = 3,    // MIDI 37
        AcousticSnare        = 4,    // MIDI 38
        HandClap             = 5,    // MIDI 39
        ElectricSnare        = 6,    // MIDI 40
        LowFloorTom          = 7,    // MIDI 41
        ClosedHiHat          = 8,    // MIDI 42
        HighFloorTom         = 9,    // MIDI 43
        PedalHiHat           = 10,   // MIDI 44
        LowTom               = 11,   // MIDI 45
        OpenHiHat            = 12,   // MIDI 46
        LowMidTom            = 13,   // MIDI 47
        HiMidTom             = 14,   // MIDI 48
        CrashCymbal1         = 15,   // MIDI 49
        HighTom              = 16,   // MIDI 50
        RideCymbal1          = 17,   // MIDI 51
        ChineseCymbal        = 18,   // MIDI 52
        RideBell             = 19,   // MIDI 53
        Tambourine           = 20,   // MIDI 54
        SplashCymbal         = 21,   // MIDI 55
        Cowbell              = 22,   // MIDI 56
        CrashCymbal2         = 23,   // MIDI 57
        Vibraslap            = 24,   // MIDI 58
        RideCymbal2          = 25,   // MIDI 59
        HiBongo              = 26,   // MIDI 60
        LowBongo             = 27,   // MIDI 61
        MuteHiConga          = 28,   // MIDI 62
        OpenHiConga          = 29,   // MIDI 63
        LowConga             = 30,   // MIDI 64
        HighTimbale          = 31,   // MIDI 65
        LowTimbale           = 32,   // MIDI 66
        HighAgogo            = 33,   // MIDI 67
        LowAgogo             = 34,   // MIDI 68
        Cabasa               = 35,   // MIDI 69
        Maracas              = 36,   // MIDI 70
        ShortWhistle         = 37,   // MIDI 71
        LongWhistle          = 38,   // MIDI 72
        ShortGuiro           = 39,   // MIDI 73
        LongGuiro            = 40,   // MIDI 74
        Claves               = 41,   // MIDI 75
        HiWoodBlock          = 42,   // MIDI 76
        LowWoodBlock         = 43,   // MIDI 77
        MuteCuica            = 44,   // MIDI 78
        OpenCuica            = 45,   // MIDI 79
        MuteTriangle         = 46,   // MIDI 80
        OpenTriangle         = 47,   // MIDI 81

        // GM2 / XG percussion above GM's 81 ceiling.  These continue `key - 34`
        // exactly (82 -> 48, 83 -> 49), which is why 48 and 49 were left free.
        // Supplied by the_last; key 82 in particular is heavily used — one Korg
        // conversion put 331 hits on it alone.
        Shaker_82            = 48,   // MIDI 82
        JingleBell           = 49,   // MIDI 83

        // ── Electronic — TR-808, TR-909, modern trap / EDM (50..79) ─────────
        Kick_808             = 50,
        SubKick_808          = 51,
        Snare_808            = 52,
        Clap_808             = 53,
        Rim_808              = 54,
        Cowbell_808          = 55,
        ClosedHat_808        = 56,
        OpenHat_808          = 57,
        Conga_808            = 58,
        Maracas_808          = 59,

        Kick_909             = 60,
        Snare_909            = 61,
        Clap_909             = 62,
        ClosedHat_909        = 63,
        OpenHat_909          = 64,
        RideCymbal_909       = 65,
        CrashCymbal_909      = 66,
        TomHigh_909          = 67,
        TomMid_909           = 68,
        TomLow_909           = 69,

        // Generic modern EDM / trap extras
        TrapKick             = 70,
        TrapSnareSnap        = 71,
        RollLong             = 72,
        RollShort            = 73,
        NoiseRise            = 74,
        NoiseFall            = 75,
        ReverseCymbal        = 76,
        VinylCrack           = 77,
        ClapStack            = 78,
        FingerSnap           = 79,

        // ── XG low keys (80..89 -> MIDI 25..34) ─────────────────────────────
        // Yamaha XG kits map below the GM floor of 35.  These ten are real,
        // named elements on the hardware, supplied by the GLOBAL low-zone
        // components (the_second over 23..34, with revo_first overriding 25..28
        // on a Revo! kit) and merged into every kit.
        //
        // THE IDS DO NOT FOLLOW `key - 34` AND CANNOT: that arithmetic is only
        // valid for 35..81 (roles 1..47) and would run negative here.  They take
        // the free 80..89 band instead, which keeps every existing id untouched
        // and stays under StyleBalanceFile's kMaxRoleId of 149.  getGMRoleForMidiKey
        // is the single place that maps a key to a role for this range.
        XgBrushTap           = 80,   // MIDI 25
        XgBrushSwirl         = 81,   // MIDI 26
        XgBrushSlap          = 82,   // MIDI 27
        XgBrushTapSwirl      = 83,   // MIDI 28
        XgSnareRoll          = 84,   // MIDI 29
        XgCastanet           = 85,   // MIDI 30
        XgSnareSoft          = 86,   // MIDI 31
        XgSticks             = 87,   // MIDI 32
        XgBassDrumSoft       = 88,   // MIDI 33
        XgOpenRimShot        = 89,   // MIDI 34

        // ── XG low keys, lower half (160..171 -> MIDI 13..24) ────────────────
        // The rest of the sub-GM zone: surdo, noise and click material that sits
        // below the brush band above.  Placed at 160 rather than continuing from
        // 89 because 90..99 is too small for twelve and 140..159 is the reserved
        // World / Ethnic band.  Non-contiguous with 80..89 on purpose — those
        // ten are already in shipped .bset files as sourceRoleId, and renumbering
        // a persisted id silently unmaps every key above it.
        XgSurdoMute          = 160,  // MIDI 13
        XgSurdoOpen          = 161,  // MIDI 14
        XgHiQ                = 162,  // MIDI 15
        XgWhipSlap           = 163,  // MIDI 16
        XgScratchH           = 164,  // MIDI 17
        XgScratchL           = 165,  // MIDI 18
        XgFingerSnapLow      = 166,  // MIDI 19
        XgClickNoise         = 167,  // MIDI 20
        XgMetronomeClick     = 168,  // MIDI 21
        XgMetronomeBell      = 169,  // MIDI 22
        XgSeqClickL          = 170,  // MIDI 23
        XgSeqClickH          = 171,  // MIDI 24

        // ── Arabic / Middle Eastern (100..119) ──────────────────────────────
        Darbuka_Doum         = 100,
        Darbuka_Tak          = 101,
        Darbuka_Snap         = 102,
        Doumbek_Doum         = 103,
        Doumbek_Tak          = 104,
        Riq_Tek              = 105,
        Riq_Sak              = 106,
        Riq_Open             = 107,
        Tabla_Na             = 108,
        Tabla_Tin            = 109,
        Tabla_Tete           = 110,
        Tabla_Bayan          = 111,
        Sagat_Closed         = 112,
        Sagat_Open           = 113,
        Def_Tek              = 114,
        Def_Sak              = 115,
        Tar_Doum             = 116,
        Tar_Tek              = 117,
        Bendir_Hit           = 118,
        Mazhar_Hit           = 119,

        // ── Latin extended (beyond GM) (120..139) ───────────────────────────
        Pandeiro_Closed      = 120,
        Pandeiro_Open        = 121,
        Pandeiro_Shake       = 122,
        Surdo_Open           = 123,
        Surdo_Muted          = 124,
        Caixa_Snare          = 125,
        Repinique            = 126,
        Tamborim             = 127,
        Agogo_Big            = 128,
        Agogo_Small          = 129,
        Conga_Slap           = 130,
        Conga_Heel           = 131,
        Bongo_Martillo       = 132,
        Quinto               = 133,
        Tumba                = 134,
        Chekere              = 135,
        Guira                = 136,

        // ── World / Ethnic (140..159) ───────────────────────────────────────
        Djembe_Bass          = 140,
        Djembe_Tone          = 141,
        Djembe_Slap          = 142,
        Taiko_Hit            = 143,
        Dhol_Bass            = 144,
        Dhol_Tek             = 145,
        FrameDrum_Hit        = 146,
        Shaker               = 147,
        Tambura_Drone        = 148,
        Kalimba_Note         = 149,
    };

    //==========================================================================
    // Coarse component GROUP — the buckets the DrumsPopup kit dropdown swaps by.
    // Selecting a key on the virtual keyboard picks its group; choosing a kit
    // from the dropdown then replaces every key in that group with the chosen
    // kit's element for each key's role (not the whole kit).
    //==========================================================================
    enum class DrumComponentGroup : uint8_t
    {
        Kick = 0,   // bass drums / kicks
        Snare,      // snares, claps, snaps
        Stick,      // side stick, rim, claves, wood blocks
        Tom,        // toms
        Metal,      // hats, cymbals, bells, cowbell, agogo, triangle
        First,      // "the first" — the XG low keys, MIDI 25..34
        Other       // "the last" — all remaining percussion (congas, shakers, world…)
    };

    //==========================================================================
    // Role catalog entry — display name + suggested default MIDI key.
    //==========================================================================
    struct DrumRoleInfo
    {
        DrumElementRole id;
        const char*     displayName;
        int             defaultMidiKey;   // -1 if no GM default
    };

    namespace DrumRoles
    {
        // Master table.  Lookup helpers below scan this; for ~150 entries
        // a linear scan is sub-microsecond and avoids any static init order
        // headaches.  std::vector (not std::array) so we never have to keep
        // a hand-maintained size constant in sync with the entry list.
        inline const std::vector<DrumRoleInfo>& table()
        {
            static const std::vector<DrumRoleInfo> t {
                { DrumElementRole::Unset,            "-",                 -1 },

                // XG low keys (MIDI 13..34) — supplied by the low-zone globals:
                // revo_first / gm_first over 13..22, the_second over 23..34
                { DrumElementRole::XgSurdoMute,      "Surdo Mute",         13 },
                { DrumElementRole::XgSurdoOpen,      "Surdo Open",         14 },
                { DrumElementRole::XgHiQ,            "Hi Q",               15 },
                { DrumElementRole::XgWhipSlap,       "Whip Slap",          16 },
                { DrumElementRole::XgScratchH,       "Scratch H",          17 },
                { DrumElementRole::XgScratchL,       "Scratch L",          18 },
                { DrumElementRole::XgFingerSnapLow,  "Finger Snap (low)",  19 },
                { DrumElementRole::XgClickNoise,     "Click Noise",        20 },
                { DrumElementRole::XgMetronomeClick, "Metronome Click",    21 },
                { DrumElementRole::XgMetronomeBell,  "Metronome Bell",     22 },
                { DrumElementRole::XgSeqClickL,      "Seq Click L",        23 },
                { DrumElementRole::XgSeqClickH,      "Seq Click H",        24 },
                { DrumElementRole::XgBrushTap,       "Brush Tap",          25 },                { DrumElementRole::XgBrushSwirl,     "Brush Swirl",        26 },
                { DrumElementRole::XgBrushSlap,      "Brush Slap",         27 },
                { DrumElementRole::XgBrushTapSwirl,  "Brush Tap Swirl",    28 },
                { DrumElementRole::XgSnareRoll,      "Snare Roll",         29 },
                { DrumElementRole::XgCastanet,       "Castanet",           30 },
                { DrumElementRole::XgSnareSoft,      "Snare Soft",         31 },
                { DrumElementRole::XgSticks,         "Sticks",             32 },
                { DrumElementRole::XgBassDrumSoft,   "Bass Drum Soft",     33 },
                { DrumElementRole::XgOpenRimShot,    "Open Rim Shot",      34 },

                // GM percussion
                { DrumElementRole::AcousticBassDrum, "Acoustic Bass Drum", 35 },
                { DrumElementRole::Kick,             "Kick",               36 },
                { DrumElementRole::SideStick,        "Side Stick",         37 },
                { DrumElementRole::AcousticSnare,    "Acoustic Snare",     38 },
                { DrumElementRole::HandClap,         "Hand Clap",          39 },
                { DrumElementRole::ElectricSnare,    "Electric Snare",     40 },
                { DrumElementRole::LowFloorTom,      "Low Floor Tom",      41 },
                { DrumElementRole::ClosedHiHat,      "Closed Hi-Hat",      42 },
                { DrumElementRole::HighFloorTom,     "High Floor Tom",     43 },
                { DrumElementRole::PedalHiHat,       "Pedal Hi-Hat",       44 },
                { DrumElementRole::LowTom,           "Low Tom",            45 },
                { DrumElementRole::OpenHiHat,        "Open Hi-Hat",        46 },
                { DrumElementRole::LowMidTom,        "Low-Mid Tom",        47 },
                { DrumElementRole::HiMidTom,         "Hi-Mid Tom",         48 },
                { DrumElementRole::CrashCymbal1,     "Crash Cymbal 1",     49 },
                { DrumElementRole::HighTom,          "High Tom",           50 },
                { DrumElementRole::RideCymbal1,      "Ride Cymbal 1",      51 },
                { DrumElementRole::ChineseCymbal,    "Chinese Cymbal",     52 },
                { DrumElementRole::RideBell,         "Ride Bell",          53 },
                { DrumElementRole::Tambourine,       "Tambourine",         54 },
                { DrumElementRole::SplashCymbal,     "Splash Cymbal",      55 },
                { DrumElementRole::Cowbell,          "Cowbell",            56 },
                { DrumElementRole::CrashCymbal2,     "Crash Cymbal 2",     57 },
                { DrumElementRole::Vibraslap,        "Vibraslap",          58 },
                { DrumElementRole::RideCymbal2,      "Ride Cymbal 2",      59 },
                { DrumElementRole::HiBongo,          "Hi Bongo",           60 },
                { DrumElementRole::LowBongo,         "Low Bongo",          61 },
                { DrumElementRole::MuteHiConga,      "Mute Hi Conga",      62 },
                { DrumElementRole::OpenHiConga,      "Open Hi Conga",      63 },
                { DrumElementRole::LowConga,         "Low Conga",          64 },
                { DrumElementRole::HighTimbale,      "High Timbale",       65 },
                { DrumElementRole::LowTimbale,       "Low Timbale",        66 },
                { DrumElementRole::HighAgogo,        "High Agogo",         67 },
                { DrumElementRole::LowAgogo,         "Low Agogo",          68 },
                { DrumElementRole::Cabasa,           "Cabasa",             69 },
                { DrumElementRole::Maracas,          "Maracas",            70 },
                { DrumElementRole::ShortWhistle,     "Short Whistle",      71 },
                { DrumElementRole::LongWhistle,      "Long Whistle",       72 },
                { DrumElementRole::ShortGuiro,       "Short Guiro",        73 },
                { DrumElementRole::LongGuiro,        "Long Guiro",         74 },
                { DrumElementRole::Claves,           "Claves",             75 },
                { DrumElementRole::HiWoodBlock,      "Hi Wood Block",      76 },
                { DrumElementRole::LowWoodBlock,     "Low Wood Block",     77 },
                { DrumElementRole::MuteCuica,        "Mute Cuica",         78 },
                { DrumElementRole::OpenCuica,        "Open Cuica",         79 },
                { DrumElementRole::MuteTriangle,     "Mute Triangle",      80 },
                { DrumElementRole::OpenTriangle,     "Open Triangle",      81 },
                { DrumElementRole::Shaker_82,        "Shaker",             82 },
                { DrumElementRole::JingleBell,       "Jingle Bell",        83 },

                // Electronic
                { DrumElementRole::Kick_808,         "808 Kick",           36 },
                { DrumElementRole::SubKick_808,      "808 Sub Kick",       36 },
                { DrumElementRole::Snare_808,        "808 Snare",          38 },
                { DrumElementRole::Clap_808,         "808 Clap",           39 },
                { DrumElementRole::Rim_808,          "808 Rim",            37 },
                { DrumElementRole::Cowbell_808,      "808 Cowbell",        56 },
                { DrumElementRole::ClosedHat_808,    "808 Closed Hat",     42 },
                { DrumElementRole::OpenHat_808,      "808 Open Hat",       46 },
                { DrumElementRole::Conga_808,        "808 Conga",          63 },
                { DrumElementRole::Maracas_808,      "808 Maracas",        70 },

                { DrumElementRole::Kick_909,         "909 Kick",           36 },
                { DrumElementRole::Snare_909,        "909 Snare",          38 },
                { DrumElementRole::Clap_909,         "909 Clap",           39 },
                { DrumElementRole::ClosedHat_909,    "909 Closed Hat",     42 },
                { DrumElementRole::OpenHat_909,      "909 Open Hat",       46 },
                { DrumElementRole::RideCymbal_909,   "909 Ride",           51 },
                { DrumElementRole::CrashCymbal_909,  "909 Crash",          49 },
                { DrumElementRole::TomHigh_909,      "909 Tom High",       50 },
                { DrumElementRole::TomMid_909,       "909 Tom Mid",        47 },
                { DrumElementRole::TomLow_909,       "909 Tom Low",        45 },

                { DrumElementRole::TrapKick,         "Trap Kick",          36 },
                { DrumElementRole::TrapSnareSnap,    "Trap Snare Snap",    40 },
                { DrumElementRole::RollLong,         "Roll Long",          -1 },
                { DrumElementRole::RollShort,        "Roll Short",         -1 },
                { DrumElementRole::NoiseRise,        "Noise Rise",         -1 },
                { DrumElementRole::NoiseFall,        "Noise Fall",         -1 },
                { DrumElementRole::ReverseCymbal,    "Reverse Cymbal",     -1 },
                { DrumElementRole::VinylCrack,       "Vinyl Crack",        -1 },
                { DrumElementRole::ClapStack,        "Clap Stack",         39 },
                { DrumElementRole::FingerSnap,       "Finger Snap",        -1 },

                // Arabic
                { DrumElementRole::Darbuka_Doum,     "Darbuka Doum",       36 },
                { DrumElementRole::Darbuka_Tak,      "Darbuka Tak",        38 },
                { DrumElementRole::Darbuka_Snap,     "Darbuka Snap",       37 },
                { DrumElementRole::Doumbek_Doum,     "Doumbek Doum",       36 },
                { DrumElementRole::Doumbek_Tak,      "Doumbek Tak",        38 },
                { DrumElementRole::Riq_Tek,          "Riq Tek",            42 },
                { DrumElementRole::Riq_Sak,          "Riq Sak",            44 },
                { DrumElementRole::Riq_Open,         "Riq Open",           46 },
                { DrumElementRole::Tabla_Na,         "Tabla Na",           60 },
                { DrumElementRole::Tabla_Tin,        "Tabla Tin",          62 },
                { DrumElementRole::Tabla_Tete,       "Tabla Tete",         64 },
                { DrumElementRole::Tabla_Bayan,      "Tabla Bayan",        45 },
                { DrumElementRole::Sagat_Closed,     "Sagat Closed",       80 },
                { DrumElementRole::Sagat_Open,       "Sagat Open",         81 },
                { DrumElementRole::Def_Tek,          "Def Tek",            42 },
                { DrumElementRole::Def_Sak,          "Def Sak",            44 },
                { DrumElementRole::Tar_Doum,         "Tar Doum",           36 },
                { DrumElementRole::Tar_Tek,          "Tar Tek",            38 },
                { DrumElementRole::Bendir_Hit,       "Bendir Hit",         41 },
                { DrumElementRole::Mazhar_Hit,       "Mazhar Hit",         54 },

                // Latin extended
                { DrumElementRole::Pandeiro_Closed,  "Pandeiro Closed",    54 },
                { DrumElementRole::Pandeiro_Open,    "Pandeiro Open",      55 },
                { DrumElementRole::Pandeiro_Shake,   "Pandeiro Shake",     69 },
                { DrumElementRole::Surdo_Open,       "Surdo Open",         41 },
                { DrumElementRole::Surdo_Muted,      "Surdo Muted",        43 },
                { DrumElementRole::Caixa_Snare,      "Caixa Snare",        38 },
                { DrumElementRole::Repinique,        "Repinique",          50 },
                { DrumElementRole::Tamborim,         "Tamborim",           76 },
                { DrumElementRole::Agogo_Big,        "Agogo Big",          67 },
                { DrumElementRole::Agogo_Small,      "Agogo Small",        68 },
                { DrumElementRole::Conga_Slap,       "Conga Slap",         62 },
                { DrumElementRole::Conga_Heel,       "Conga Heel",         64 },
                { DrumElementRole::Bongo_Martillo,   "Bongo Martillo",     60 },
                { DrumElementRole::Quinto,           "Quinto",              63 },
                { DrumElementRole::Tumba,            "Tumba",              64 },
                { DrumElementRole::Chekere,          "Chekere",            69 },
                { DrumElementRole::Guira,            "Guira",              73 },

                // World / Ethnic
                { DrumElementRole::Djembe_Bass,      "Djembe Bass",        36 },
                { DrumElementRole::Djembe_Tone,      "Djembe Tone",        38 },
                { DrumElementRole::Djembe_Slap,      "Djembe Slap",        40 },
                { DrumElementRole::Taiko_Hit,        "Taiko Hit",          36 },
                { DrumElementRole::Dhol_Bass,        "Dhol Bass",          36 },
                { DrumElementRole::Dhol_Tek,         "Dhol Tek",           38 },
                { DrumElementRole::FrameDrum_Hit,    "Frame Drum Hit",     41 },
                { DrumElementRole::Shaker,           "Shaker",             69 },
                { DrumElementRole::Tambura_Drone,    "Tambura Drone",      -1 },
                { DrumElementRole::Kalimba_Note,     "Kalimba Note",       -1 },
            };
            return t;
        }

        /** Returns the human display name for a role ID. Returns "(unknown)"
            for IDs not present in the table. */
        inline const char* getName (uint8_t roleId) noexcept
        {
            for (const auto& e : table())
                if ((uint8_t) e.id == roleId) return e.displayName;
            return "(unknown)";
        }

        inline const char* getName (DrumElementRole role) noexcept
        {
            return getName ((uint8_t) role);
        }

        /** Returns the suggested default MIDI key for a role (where on a GM
            drum map this role traditionally sits). Returns -1 if the role
            has no canonical GM home (e.g. NoiseRise, FingerSnap). */
        inline int getDefaultMidiKey (uint8_t roleId) noexcept
        {
            for (const auto& e : table())
                if ((uint8_t) e.id == roleId) return e.defaultMidiKey;
            return -1;
        }

        inline int getDefaultMidiKey (DrumElementRole role) noexcept
        {
            return getDefaultMidiKey ((uint8_t) role);
        }

        /** Returns the GM-derived role for a MIDI key in the standard
            percussion range (35..81), or DrumElementRole::Unset for keys
            outside that range. Used by the UI to label "empty" keys with
            the role they'd typically host on a GM drum map. */
        inline DrumElementRole getGMRoleForMidiKey (int midiKey) noexcept
        {
            // Ceiling is 83, not 81: keys 82 Shaker and 83 Jingle Bell are GM2
            // percussion supplied by the_last, and roles 48/49 continue the same
            // `key - 34` arithmetic.
            if (midiKey >= 35 && midiKey <= 83)
                return (DrumElementRole) (midiKey - 34);

            // XG low keys sit BELOW the GM floor, so `key - 34` cannot reach
            // them.  This is the one place the sub-GM mapping lives;
            // DrumKitRegistry calls straight through here rather than doing the
            // arithmetic itself, so the two can never disagree.
            //
            // Two bands, because 80..89 shipped first and its ids are persisted
            // in .bset files as sourceRoleId — renumbering them would silently
            // unmap every key that already references one.
            if (midiKey >= 25 && midiKey <= 34)
                return (DrumElementRole) (midiKey - 25
                                          + (int) DrumElementRole::XgBrushTap);

            if (midiKey >= 13 && midiKey <= 24)
                return (DrumElementRole) (midiKey - 13
                                          + (int) DrumElementRole::XgSurdoMute);

            return DrumElementRole::Unset;
        }

        /** Maps a role to its coarse component group.  Anything not explicitly
            placed in one of the five named buckets falls into Other ("the last"
            — congas, bongos, shakers, whistles, world/ethnic percussion, FX). */
        inline DrumComponentGroup groupForRole (uint8_t roleId) noexcept
        {
            using R = DrumElementRole;
            switch ((R) roleId)
            {
                // Kick
                case R::AcousticBassDrum: case R::Kick:
                case R::Kick_808: case R::SubKick_808: case R::Kick_909: case R::TrapKick:
                    return DrumComponentGroup::Kick;

                // Snare (+ claps / snaps)
                case R::AcousticSnare: case R::ElectricSnare: case R::HandClap:
                case R::Snare_808: case R::Clap_808: case R::Snare_909: case R::Clap_909:
                case R::TrapSnareSnap: case R::ClapStack: case R::FingerSnap: case R::Caixa_Snare:
                    return DrumComponentGroup::Snare;

                // Stick (side stick, rim, claves, wood blocks)
                case R::SideStick: case R::Rim_808:
                case R::Claves: case R::HiWoodBlock: case R::LowWoodBlock:
                    return DrumComponentGroup::Stick;

                // Tom
                case R::LowFloorTom: case R::HighFloorTom: case R::LowTom:
                case R::LowMidTom:   case R::HiMidTom:     case R::HighTom:
                case R::TomHigh_909: case R::TomMid_909:   case R::TomLow_909:
                    return DrumComponentGroup::Tom;

                // Metal (hats, cymbals, bells, cowbell, agogo, triangle, sagat)
                case R::ClosedHiHat: case R::PedalHiHat: case R::OpenHiHat:
                case R::CrashCymbal1: case R::RideCymbal1: case R::ChineseCymbal:
                case R::RideBell: case R::SplashCymbal: case R::CrashCymbal2: case R::RideCymbal2:
                case R::Cowbell: case R::HighAgogo: case R::LowAgogo:
                case R::MuteTriangle: case R::OpenTriangle:
                case R::Cowbell_808: case R::ClosedHat_808: case R::OpenHat_808:
                case R::ClosedHat_909: case R::OpenHat_909: case R::RideCymbal_909: case R::CrashCymbal_909:
                case R::ReverseCymbal: case R::Sagat_Closed: case R::Sagat_Open:
                case R::Agogo_Big: case R::Agogo_Small:
                    return DrumComponentGroup::Metal;

                // The XG low keys are their own family.  Leaving them in Other
                // would put them in "the last" bucket, so swapping REST GM would
                // silently replace them too.
                case R::XgBrushTap:  case R::XgBrushSwirl: case R::XgBrushSlap:
                case R::XgBrushTapSwirl: case R::XgSnareRoll: case R::XgCastanet:
                case R::XgSnareSoft: case R::XgSticks: case R::XgBassDrumSoft:
                case R::XgOpenRimShot:
                case R::XgSurdoMute: case R::XgSurdoOpen: case R::XgHiQ:
                case R::XgWhipSlap:  case R::XgScratchH:  case R::XgScratchL:
                case R::XgFingerSnapLow: case R::XgClickNoise:
                case R::XgMetronomeClick: case R::XgMetronomeBell:
                case R::XgSeqClickL: case R::XgSeqClickH:
                    return DrumComponentGroup::First;

                default:
                    return DrumComponentGroup::Other;
            }
        }

        inline DrumComponentGroup groupForRole (DrumElementRole role) noexcept
        {
            return groupForRole ((uint8_t) role);
        }

        /** Short display name for a component group (used in the dropdown hint). */
        inline const char* groupName (DrumComponentGroup g) noexcept
        {
            switch (g)
            {
                case DrumComponentGroup::Kick:  return "Kick";
                case DrumComponentGroup::Snare: return "Snare";
                case DrumComponentGroup::Stick: return "Stick";
                case DrumComponentGroup::Tom:   return "Tom";
                case DrumComponentGroup::Metal: return "Metal";
                case DrumComponentGroup::First: return "First";
                default:                        return "Other";
            }
        }

        /** Total number of catalogued roles (including Unset). */
        inline int getCount() noexcept
        {
            return (int) table().size();
        }
    } // namespace DrumRoles
} // namespace Betel