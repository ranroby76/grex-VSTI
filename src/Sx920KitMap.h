#pragma once
#include <cstdint>

//==============================================================================
//  Sx920KitMap.h  -  WHERE EACH PSR-SX920 KIT KEY PLAYS ON A COMPOSED GREX KIT.
//
//  GENERATED from Yamaha's PSR-SX920 Data List (psrsx920_sx720+_sx720_en_dl_b0,
//  sheet "Drum&SFX Kit List SX920"): every key of every kit is named there.  Each
//  name is classified (kick, snare, conga open, triangle mute ...) and compared
//  with what a COMPOSED Grex kit plays on that key - the GM body on 35..81, the
//  shared low zone on 23..34, revo_first or gm_first on 13..22 and revo_first
//  over 25..28 on a Revo! kit.  Where they differ the key is re-pointed to the
//  Grex key that plays the same instrument; everything else is left alone.
//
//  Scope, enforced by Channel: STYLE notes on a COMPOSED kit only.  A sampled
//  full kit or an EDM synth kit carries its own layout; the drum editor's own
//  notes are never moved.  Keys above 81 are not touched.  SFX / vocal kits are
//  not drums and have no entry.  Revo! kits (Brush Expanded) have no brush
//  samples: brush sweeps land on Snare Roll (29), brush hits on Snare Soft (31).
//==============================================================================
namespace sx920
{
    struct KitMap
    {
        uint8_t msb, lsb, pc;
        int8_t  to[128];                  // -1 = keep the key
    };

    inline const KitMap* kitMaps (int& count) noexcept
    {
        static const KitMap maps[] =
        {
            // BassDrumKit (126-0-20):
            //     13 BD Electro                                   -> 36
            //     14 BD FX Gate                                   -> 36
            //     15 BD Hammer                                    -> 36
            //     16 BD Analog Power                              -> 36
            //     17 BD Analog Distortion 5                       -> 36
            //     18 BD Analog Distortion 6                       -> 36
            //     19 BD Analog Distortion 4                       -> 36
            //     20 BD Analog Distortion 3                       -> 36
            //     21 BD Analog Distortion 2                       -> 36
            //     22 BD Analog Tight                              -> 36
            //     23 BD Analog 94                                 -> 36
            //     24 BD Analog Blip 2                             -> 36
            //     25 BD Analog Rubber 2                           -> 36
            //     26 BD Analog 93                                 -> 36
            //     27 BD Analog 90                                 -> 36
            //     28 BD Analog 83                                 -> 36
            //     29 BD Analog 82                                 -> 36
            //     30 BD Analog 92                                 -> 36
            //     31 BD Analog 91                                 -> 36
            //     32 BD Analog Deep                               -> 36
            //     34 BD Analog Hard 1                             -> 36
            //     37 BD Analog Loose                              -> 36
            //     38 BD Synth 1                                   -> 36
            //     39 BD Synth 2                                   -> 36
            //     40 BD Analog Distortion 1                       -> 36
            //     42 BD Analog 70 L                               -> 36
            //     43 BD Analog 70                                 -> 36
            //     44 BD Analog 80                                 -> 36
            //     45 BD Analog 80 Long                            -> 36
            //     46 BD Dry                                       -> 36
            //     47 BD Dry Hard                                  -> 36
            //     48 BD Room 1                                    -> 36
            //     49 BD Soft                                      -> 36
            //     50 BD Room 2                                    -> 36
            //     51 BD Break Lo-fi 2                             -> 36
            //     52 BD Break Lo-fi 1                             -> 36
            //     53 BD & Hi-Hat Open                             -> 46
            //     54 BD Jungle 2                                  -> 36
            //     55 BD Jungle 1                                  -> 36
            //     56 BD Jungle 3                                  -> 36
            //     57 BD D&B 1                                     -> 36
            //     58 BD D&B 2                                     -> 36
            //     59 BD RX5 1                                     -> 36
            //     60 BD RX5 2                                     -> 36
            //     61 BD Room 3                                    -> 36
            //     62 BD Power Gate                                -> 36
            //     63 BD R&B 1                                     -> 36
            //     64 BD R&B 2                                     -> 36
            //     65 BD Lo-fi                                     -> 36
            //     66 BD Hip Deep                                  -> 36
            //     67 BD Break Deep                                -> 36
            //     68 BD Break Heavy                               -> 36
            //     69 BD Break Hard                                -> 36
            //     74 BD Industrial                                -> 36
            //     79 BD Human                                     -> 36
            //     80 BD Human Deep                                -> 36
            { 126, 0, 20, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 36, 36, 36,
                36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
                36, -1, 36, -1, -1, 36, 36, 36, 36, -1, 36, 36, 36, 36, 36, 36,
                36, 36, 36, 36, 36, 46, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
                36, 36, 36, 36, 36, 36, -1, -1, -1, -1, 36, -1, -1, -1, -1, 36,
                36, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // ReverseBD Kit (126-0-21):
            //     53 Reverse BD & Hi-Hat Open                     -> 46
            //     74 Reverse Tom Industrial                       -> 47
            { 126, 0, 21, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, 46, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 47, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // ArabicKit2 (126-0-35):
            //     25 Cabasa                                       -> 69
            //     29 Bongo H                                      -> 60
            //     30 Bongo L                                      -> 61
            //     31 Conga H Mute                                 -> 62
            //     32 Conga H Open                                 -> 63
            //     33 Conga L                                      -> 64
            //     52 Crash Cymbal 2                               -> 49
            //     58 Claves                                       -> 75
            { 126, 0, 35, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, 69, -1, -1, -1, 60, 61, 62,
                63, 64, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 49, -1, -1, -1, -1, -1, 75, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // ArabicKit1 (126-0-36):
            //     14 Zarb Tom f                                   -> 47
            //     17 Tombak Tom f                                 -> 47
            //     18 Neghareh Tom f                               -> 47
            //     24 Khaligi Clap 1                               -> 39
            //     26 Khaligi Clap 2                               -> 39
            //     28 Arabic Hand Clap                             -> 39
            { 126, 0, 36, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 47, -1,
                -1, 47, 47, -1, -1, -1, -1, -1, 39, -1, 39, -1, 39, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // CubanKit (126-0-40):
            //     22 Conga H Tip                                  -> 62
            //     23 Conga H Heel                                 -> 62
            //     24 Conga H Open                                 -> 63
            //     25 Conga H Mute                                 -> 62
            //     26 Conga H Slap Open                            -> 63
            //     27 Conga H Slap                                 -> 63
            //     28 Conga H Slap Mute                            -> 62
            //     29 Conga L Tip                                  -> 64
            //     30 Conga L Heel                                 -> 64
            //     31 Conga L Open                                 -> 64
            //     32 Conga L Mute                                 -> 64
            //     33 Conga L Slap Open                            -> 64
            //     34 Conga L Slap                                 -> 64
            //     35 Conga L Slide                                -> 64
            //     36 Bongo H Open One Finger                      -> 60
            //     37 Bongo H Open Three Finger                    -> 60
            //     38 Bongo H Rim                                  -> 60
            //     39 Bongo H Tip                                  -> 60
            //     40 Bongo H Heel                                 -> 60
            //     41 Bongo H Slap                                 -> 60
            //     42 Bongo L Open One Finger                      -> 61
            //     43 Bongo L Open Three Finger                    -> 61
            //     44 Bongo L Rim                                  -> 61
            //     45 Bongo L Tip                                  -> 61
            //     46 Bongo L Heel                                 -> 61
            //     47 Bongo L Slap                                 -> 61
            //     48 Timbale L                                    -> 66
            //     53 Paila L                                      -> 66
            //     54 Timbale H                                    -> 65
            //     59 Paila H                                      -> 65
            //     60 Cowbell Top                                  -> 56
            //     64 Guiro Short                                  -> 73
            //     65 Guiro Long                                   -> 74
            //     68 Tambourine                                   -> 54
            //     72 Maracas                                      -> 70
            //     73 Shaker                                       -> 70
            //     74 Cabasa                                       -> 69
            { 126, 0, 40, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, 62, 62, 63, 62, 63, 63, 62, 64, 64, 64,
                64, 64, 64, 64, 60, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61,
                66, -1, -1, -1, -1, 66, 65, -1, -1, -1, -1, 65, 56, -1, -1, -1,
                73, 74, -1, -1, 54, -1, -1, -1, 70, 70, 69, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // AfroCubanKit (126-0-42):
            //     17 Cajon Bass AF                                -> 36
            //     18 Cajon Mid AF                                 -> 38
            //     19 Cajon Hi AF                                  -> 38
            //     20 Conga Lo Open AF                             -> 64
            //     21 Conga Lo Close AF                            -> 64
            //     22 Conga Lo Slap Open AF                        -> 64
            //     23 Conga Lo Slap Mute AF                        -> 64
            //     24 Conga Hi Bass AF                             -> 64
            //     25 Conga Hi Heel AF                             -> 62
            //     26 Conga Hi Tip AF                              -> 62
            //     27 Conga Hi Open AF                             -> 63
            //     28 Conga Hi Slap Close AF                       -> 62
            //     29 Conga Hi Slap Open AF                        -> 63
            //     30 Conga Hi Slap Mute AF                        -> 62
            //     31 Bongo Lo Open 1 / Rim 1 AF                   -> 61
            //     32 Bongo Lo Open 3 AF                           -> 61
            //     33 Bongo Lo Close 3 AF                          -> 61
            //     34 Bongo Lo Slap AF                             -> 61
            //     35 Bongo Hi Heel AF                             -> 60
            //     36 Bongo Hi Tip AF                              -> 60
            //     37 Bongo Hi Open 1 /Rim 1 AF                    -> 60
            //     38 Bongo Hi Open 3 / Rim 3 AF                   -> 60
            //     39 Bongo Hi Mute1 AF                            -> 60
            //     40 Bongo Hi Slap AF                             -> 60
            //     41 Cowbell 3 Mouth AF                           -> 56
            //     42 Cowbell 3 Top1 AF                            -> 56
            //     43 Cowbell 3 Top2 AF                            -> 56
            //     44 Cowbell 3 Edge AF                            -> 56
            //     45 Cowbell 5 Mouth AF                           -> 56
            //     46 Cowbell 5 Top1 AF                            -> 56
            //     47 Cowbell 5 Top2 AF                            -> 56
            //     48 Cowbell 5 Edge AF                            -> 56
            //     55 Timbale Finger Lo Mute AF                    -> 66
            //     56 Timbale Finger Lo Open AF                    -> 66
            //     57 Timbale Lo Cask Tip / Shank AF               -> 66
            //     58 Timbale Hi Cask Tip / Shank AF               -> 65
            //     59 Timbale Lo Side Stick AF                     -> 66
            //     60 Timbale Lo Open AF                           -> 66
            //     61 Timbale Lo Rim AF                            -> 66
            //     62 Timbale Hi Open AF                           -> 65
            //     63 Timbale Hi Rim AF                            -> 65
            //     64 Plastic Wood Block AF                        -> 76
            //     65 African Clave AF                             -> 75
            //     66 Wood Clave AF                                -> 75
            //     67 Maracas 1 Lo AF                              -> 70
            //     68 Maracas 1 Hi AF                              -> 70
            //     77 Guiro Up Short AF                            -> 73
            //     78 Guiro Dn Short AF                            -> 73
            //     79 Guiro Up Slow AF                             -> 74
            //     80 Guiro Up Fast AF                             -> 74
            //     81 Metal Guiro Up Short AF                      -> 73
            { 126, 0, 42, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, 36, 38, 38, 64, 64, 64, 64, 64, 62, 62, 63, 62, 63, 62, 61,
                61, 61, 61, 60, 60, 60, 60, 60, 60, 56, 56, 56, 56, 56, 56, 56,
                56, -1, -1, -1, -1, -1, -1, 66, 66, 66, 65, 66, 66, 66, 65, 65,
                76, 75, 75, 70, 70, -1, -1, -1, -1, -1, -1, -1, -1, 73, 73, 74,
                74, 73, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // PopLatinKit1 (126-0-43):
            //     13 Cajon Low                                    -> 38
            //     14 Cajon Slap                                   -> 38
            //     15 Cajon Tip                                    -> 38
            //     16 Claves High                                  -> 75
            //     17 Claves Low                                   -> 75
            //     18 Hand Clap                                    -> 39
            //     21 Castanet                                     -> 30
            //     22 Conga H Tip                                  -> 62
            //     23 Conga H Heel                                 -> 62
            //     24 Conga H Open                                 -> 63
            //     25 Conga H Mute                                 -> 62
            //     26 Conga H Slap Open                            -> 63
            //     27 Conga H Slap                                 -> 63
            //     28 Conga H Slap Mute                            -> 62
            //     29 Conga L Tip                                  -> 64
            //     30 Conga L Heel                                 -> 64
            //     31 Conga L Open                                 -> 64
            //     32 Conga L Mute                                 -> 64
            //     33 Conga L Slap Open                            -> 64
            //     34 Conga L Slap                                 -> 64
            //     35 Conga L Slide                                -> 64
            //     36 Bongo H Open One Finger                      -> 60
            //     37 Bongo H Open Three Finger                    -> 60
            //     38 Bongo H Rim                                  -> 60
            //     39 Bongo H Tip                                  -> 60
            //     40 Bongo H Heel                                 -> 60
            //     41 Bongo H Slap                                 -> 60
            //     42 Bongo L Open One Finger                      -> 61
            //     43 Bongo L Open Three Finger                    -> 61
            //     44 Bongo L Rim                                  -> 61
            //     45 Bongo L Tip                                  -> 61
            //     46 Bongo L Heel                                 -> 61
            //     47 Bongo L Slap                                 -> 61
            //     48 Timbale L                                    -> 66
            //     53 Paila L                                      -> 66
            //     54 Timbale H                                    -> 65
            //     59 Paila H                                      -> 65
            //     60 Cowbell Top                                  -> 56
            //     61 Cowbell 1                                    -> 56
            //     62 Cowbell 2                                    -> 56
            //     63 Cowbell 3                                    -> 56
            //     64 Guiro Short                                  -> 73
            //     65 Guiro Long                                   -> 74
            //     66 Metal Guiro Short                            -> 73
            //     67 Metal Guiro Long                             -> 74
            //     68 Tambourine                                   -> 54
            //     69 Tambourim Open                               -> 54
            //     70 Tambourim Mute                               -> 54
            //     71 Tambourim Tip                                -> 54
            //     72 Maracas                                      -> 70
            //     73 Shaker                                       -> 70
            //     74 Cabasa                                       -> 69
            //     75 Cuica Mute                                   -> 78
            //     76 Cuica Open                                   -> 79
            //     77 Cowbell High 1                               -> 56
            //     78 Cowbell High 2                               -> 56
            //     81 Triangle Mute                                -> 80
            { 126, 0, 43, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 38, 38, 38,
                75, 75, 39, -1, -1, 30, 62, 62, 63, 62, 63, 63, 62, 64, 64, 64,
                64, 64, 64, 64, 60, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61,
                66, -1, -1, -1, -1, 66, 65, -1, -1, -1, -1, 65, 56, 56, 56, 56,
                73, 74, 73, 74, 54, 54, 54, 54, 70, 70, 69, 78, 79, 56, 56, -1,
                -1, 80, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // PopLatinKit2 (126-0-44):
            //     13 Cajon Low                                    -> 38
            //     14 Cajon Slap                                   -> 38
            //     15 Cajon Tip                                    -> 38
            //     16 Claves High                                  -> 75
            //     17 Claves Low                                   -> 75
            //     18 Hand Clap                                    -> 39
            //     21 Castanet                                     -> 30
            //     22 Conga H Tip                                  -> 62
            //     23 Conga H Heel                                 -> 62
            //     24 Conga H Open                                 -> 63
            //     25 Conga H Mute                                 -> 62
            //     26 Conga H Slap Open                            -> 63
            //     27 Conga H Slap                                 -> 63
            //     28 Conga H Slap Mute                            -> 62
            //     29 Conga L Tip                                  -> 64
            //     30 Conga L Heel                                 -> 64
            //     31 Conga L Open                                 -> 64
            //     32 Conga L Mute                                 -> 64
            //     33 Conga L Slap Open                            -> 64
            //     34 Conga L Slap                                 -> 64
            //     35 Conga L Slide                                -> 64
            //     36 Bongo H Open One Finger                      -> 60
            //     37 Bongo H Open Three Finger                    -> 60
            //     38 Bongo H Rim                                  -> 60
            //     39 Bongo H Tip                                  -> 60
            //     40 Bongo H Heel                                 -> 60
            //     41 Bongo H Slap                                 -> 60
            //     42 Bongo L Open One Finger                      -> 61
            //     43 Bongo L Open Three Finger                    -> 61
            //     44 Bongo L Rim                                  -> 61
            //     45 Bongo L Tip                                  -> 61
            //     46 Bongo L Heel                                 -> 61
            //     47 Bongo L Slap                                 -> 61
            //     48 Timbale L                                    -> 66
            //     53 Paila L                                      -> 66
            //     54 Timbale H                                    -> 65
            //     55 Hand Clap Mute 1                             -> 39
            //     56 Hand Clap Mute 2                             -> 39
            //     57 Hand Clap Open 1                             -> 39
            //     58 Hand Clap Open 2                             -> 39
            //     59 Paila H                                      -> 65
            //     60 Cowbell Top                                  -> 56
            //     61 Cowbell 1                                    -> 56
            //     62 Cowbell 2                                    -> 56
            //     63 Cowbell 3                                    -> 56
            //     64 Guiro Short                                  -> 73
            //     65 Guiro Long                                   -> 74
            //     66 Metal Guiro Short                            -> 73
            //     67 Metal Guiro Long                             -> 74
            //     68 Tambourine                                   -> 54
            //     69 Tambourim Open                               -> 54
            //     70 Tambourim Mute                               -> 54
            //     71 Tambourim Tip                                -> 54
            //     72 Maracas                                      -> 70
            //     73 Shaker                                       -> 70
            //     74 Cabasa                                       -> 69
            //     75 Cuica Mute                                   -> 78
            //     76 Cuica Open                                   -> 79
            //     77 Cowbell High 1                               -> 56
            //     78 Cowbell High 2                               -> 56
            //     81 Triangle Mute                                -> 80
            { 126, 0, 44, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 38, 38, 38,
                75, 75, 39, -1, -1, 30, 62, 62, 63, 62, 63, 63, 62, 64, 64, 64,
                64, 64, 64, 64, 60, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61,
                66, -1, -1, -1, -1, 66, 65, 39, 39, 39, 39, 65, 56, 56, 56, 56,
                73, 74, 73, 74, 54, 54, 54, 54, 70, 70, 69, 78, 79, 56, 56, -1,
                -1, 80, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // PopPercussionKit (126-0-45):
            //     17 Cajon Bass PP                                -> 36
            //     18 Cajon Mid PP                                 -> 38
            //     19 Cajon Hi PP                                  -> 38
            //     20 Conga Lo Open BR                             -> 64
            //     21 Conga Lo Close BR                            -> 64
            //     22 Conga Lo Slap Open BR                        -> 64
            //     23 Conga Lo Slap Mute BR                        -> 64
            //     24 Conga Hi Bass BR                             -> 64
            //     25 Conga Hi Heel BR                             -> 62
            //     26 Conga Hi Tip BR                              -> 62
            //     27 Conga Hi Open BR                             -> 63
            //     28 Conga Hi Slap Close BR                       -> 62
            //     29 Conga Hi Slap Open BR                        -> 63
            //     30 Conga Hi Slap Mute BR                        -> 62
            //     31 Bongo Lo Open 1 / Rim 1 AF                   -> 61
            //     32 Bongo Lo Open 3 AF                           -> 61
            //     33 Bongo Lo Close 3 AF                          -> 61
            //     34 Bongo Lo Slap AF                             -> 61
            //     35 Bongo Hi Heel AF                             -> 60
            //     36 Bongo Hi Tip AF                              -> 60
            //     37 Bongo Hi Open 1 /Rim 1 AF                    -> 60
            //     38 Bongo Hi Open 3 / Rim 3 AF                   -> 60
            //     39 Bongo Hi Mute1 AF                            -> 60
            //     40 Bongo Hi Slap AF                             -> 60
            //     41 Cowbell 5 Mouth AF                           -> 56
            //     42 Cowbell 3 Mouth AF                           -> 56
            //     44 Cowbell RD                                   -> 56
            //     45 Fibre Clave PP                               -> 75
            //     46 Plastic Wood Block AF                        -> 76
            //     47 African Clave AF                             -> 75
            //     48 Wood Clave AF                                -> 75
            //     53 Tambourine 1 Shake PP                        -> 54
            //     55 Tambourine 2 Shake PP                        -> 54
            //     56 Tambourine 2 Hit PP                          -> 54
            //     57 Tambourine 3 Hit PP                          -> 54
            //     62 Claps Lo PP                                  -> 39
            //     63 Claps Hi PP                                  -> 39
            //     64 Cabasa L BR                                  -> 69
            //     65 Cabasa R BR                                  -> 69
            //     66 Shaker 1 Up BR                               -> 70
            //     67 Shaker 1 Down BR                             -> 70
            //     68 Shaker 1 Long BR                             -> 70
            //     71 Shaker 2 Long BR                             -> 70
            //     72 Shaker 3 Up BR                               -> 70
            //     73 Shaker 3 Down BR                             -> 70
            //     74 Shaker 3 Long BR                             -> 70
            //     75 Shaker 4 Up BR                               -> 70
            //     76 Shaker 4 Down BR                             -> 70
            //     77 Shaker 4 Long BR                             -> 70
            //     78 Shaker 5 Up PP                               -> 70
            //     79 Shaker 5 Down PP                             -> 70
            { 126, 0, 45, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, 36, 38, 38, 64, 64, 64, 64, 64, 62, 62, 63, 62, 63, 62, 61,
                61, 61, 61, 60, 60, 60, 60, 60, 60, 56, 56, -1, 56, 75, 76, 75,
                75, -1, -1, -1, -1, 54, -1, 54, 54, 54, -1, -1, -1, -1, 39, 39,
                69, 69, 70, 70, 70, -1, -1, 70, 70, 70, 70, 70, 70, 70, 70, 70,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // PopLatinKit2Comp (126-0-46):
            //     13 Cajon Low LC                                 -> 38
            //     14 Cajon Slap LC                                -> 38
            //     15 Cajon Tip LC                                 -> 38
            //     16 Claves High LC                               -> 75
            //     17 Claves Low LC                                -> 75
            //     18 Hand Clap LC                                 -> 39
            //     21 Castanet LC                                  -> 30
            //     22 Conga H Tip LC                               -> 62
            //     23 Conga H Heel LC                              -> 62
            //     24 Conga H Open LC                              -> 63
            //     25 Conga H Mute LC                              -> 62
            //     26 Conga H Slap Open LC                         -> 63
            //     27 Conga H Slap LC                              -> 63
            //     28 Conga H Slap Mute LC                         -> 62
            //     29 Conga L Tip LC                               -> 64
            //     30 Conga L Heel LC                              -> 64
            //     31 Conga L Open LC                              -> 64
            //     32 Conga L Mute LC                              -> 64
            //     33 Conga L Slap Open LC                         -> 64
            //     34 Conga L Slap LC                              -> 64
            //     35 Conga L Slide LC                             -> 64
            //     36 Bongo H Open One Finger LC                   -> 60
            //     37 Bongo H Open Three Finger LC                 -> 60
            //     38 Bongo H Rim LC                               -> 60
            //     39 Bongo H Tip LC                               -> 60
            //     40 Bongo H Heel LC                              -> 60
            //     41 Bongo H Slap LC                              -> 60
            //     42 Bongo L Open One Finger LC                   -> 61
            //     43 Bongo L Open Three Finger LC                 -> 61
            //     44 Bongo L Rim LC                               -> 61
            //     45 Bongo L Tip LC                               -> 61
            //     46 Bongo L Heel LC                              -> 61
            //     47 Bongo L Slap LC                              -> 61
            //     48 Timbale L LC                                 -> 66
            //     53 Paila L LC                                   -> 66
            //     54 Timbale H LC                                 -> 65
            //     55 Hand Clap Mute 1 LC                          -> 39
            //     56 Hand Clap Mute 2 LC                          -> 39
            //     57 Hand Clap Open 1 LC                          -> 39
            //     58 Hand Clap Open 2 LC                          -> 39
            //     59 Paila H LC                                   -> 65
            //     60 Cowbell Top LC                               -> 56
            //     61 Cowbell 1 LC                                 -> 56
            //     62 Cowbell 2 LC                                 -> 56
            //     63 Cowbell 3 LC                                 -> 56
            //     64 Guiro Short LC                               -> 73
            //     65 Guiro Long LC                                -> 74
            //     66 Metal Guiro Short LC                         -> 73
            //     67 Metal Guiro Long LC                          -> 74
            //     68 Tambourine LC                                -> 54
            //     69 Tambourim Open LC                            -> 54
            //     70 Tambourim Mute LC                            -> 54
            //     71 Tambourim Tip LC                             -> 54
            //     72 Maracas LC                                   -> 70
            //     73 Shaker LC                                    -> 70
            //     74 Cabasa LC                                    -> 69
            //     75 Cuica Mute LC                                -> 78
            //     76 Cuica Open LC                                -> 79
            //     77 Cowbell High 1 LC                            -> 56
            //     78 Cowbell High 2 LC                            -> 56
            //     81 Triangle Mute LC                             -> 80
            { 126, 0, 46, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 38, 38, 38,
                75, 75, 39, -1, -1, 30, 62, 62, 63, 62, 63, 63, 62, 64, 64, 64,
                64, 64, 64, 64, 60, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61,
                66, -1, -1, -1, -1, 66, 65, 39, 39, 39, 39, 65, 56, 56, 56, 56,
                73, 74, 73, 74, 54, 54, 54, 54, 70, 70, 69, 78, 79, 56, 56, -1,
                -1, 80, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // CymbalKit (126-0-49):
            //     37 Timbale A                                    -> 65
            //     38 China Cymbal A                               -> 52
            //     39 Crash Cymbal A                               -> 49
            //     40 Ride Cymbal A                                -> 51
            //     41 Splash Cymbal A                              -> 55
            //     44 Crash Cymbal B1                              -> 49
            //     45 Crash Cymbal B2                              -> 49
            //     46 Ride Cymbal B                                -> 51
            //     50 Ride Cymbal C                                -> 51
            //     52 Brush Sizzle Cymbal C                        -> 25
            //     55 Crash Cymbal D1                              -> 49
            //     56 Crash Cymbal D2                              -> 49
            //     58 Ride Cymbal D                                -> 51
            //     59 Splash Cymbal D                              -> 55
            //     62 Crash Cymbal E1                              -> 49
            //     63 Crash Cymbal E2                              -> 49
            //     64 Ride Cymbal E                                -> 51
            //     65 Hi-Hat Half Open E                           -> 46
            //     72 Crash Cymbal F1                              -> 49
            //     73 Brush Crash Cymbal F1                        -> 49
            //     74 Crash Cymbal F2                              -> 49
            //     75 Jazz Ride Cymbal F1                          -> 51
            //     76 Crash Cymbal F3                              -> 49
            //     77 Hi-Hat Splash F                              -> 42
            //     78 Ride Cymbal F1                               -> 51
            //     79 Crash Cymbal F4                              -> 49
            //     80 Ride Cymbal F2                               -> 51
            //     81 Brush Crash Cymbal F2                        -> 49
            { 126, 0, 49, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, 65, 52, 49, 51, 55, -1, -1, 49, 49, 51, -1,
                -1, -1, 51, -1, 25, -1, -1, 49, 49, -1, 51, 55, -1, -1, 49, 49,
                51, 46, -1, -1, -1, -1, -1, -1, 49, 49, 49, 51, 49, 42, 51, 49,
                51, 49, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // ArabicMixKit (126-0-64):
            //     13 Conga Analog H                               -> 63
            //     14 Conga Analog M                               -> 63
            //     15 Conga Analog L                               -> 64
            //     16 Vibraslap                                    -> 58
            //     17 Kick Techno L                                -> 36
            //     18 Side Stick Arabic Mix                        -> 37
            //     19 Snare Techno                                 -> 38
            //     20 Guiro Long                                   -> 74
            //     21 Kick Techno Q                                -> 36
            //     22 Open Rim Shot                                -> 34
            //     23 Funk Snare 2                                 -> 38
            //     24 Kick Arabic Mix                              -> 36
            //     25 Funk Snare 1                                 -> 38
            //     26 Snare Arabic Mix                             -> 38
            //     27 Hand Clap                                    -> 39
            //     28 Snare                                        -> 38
            //     29 Tom Electro 1                                -> 50
            //     30 Hi-Hat Closed Arabic Mix                     -> 42
            //     31 Tom Electro 2                                -> 48
            //     32 Hi-Hat Half Arabic Mix                       -> 42
            //     33 Tom Electro 3                                -> 47
            //     34 Hi-Hat Open Arabic Mix                       -> 46
            //     35 Tom Electro 4                                -> 45
            //     36 Tom Electro 5                                -> 43
            //     37 Crash Cymbal 1                               -> 49
            //     38 Tom Electro 6                                -> 41
            //     39 Hi-Hat Open 3                                -> 46
            //     41 Timbale L                                    -> 66
            //     42 Conga H Open                                 -> 63
            //     43 Timbale H                                    -> 65
            //     44 Conga H Mute                                 -> 62
            //     45 Tambourine                                   -> 54
            //     46 Conga L                                      -> 64
            //     47 Cowbell                                      -> 56
            //     48 Claves                                       -> 75
            //     49 Bongo H                                      -> 60
            //     50 Wood Block H                                 -> 76
            //     51 Bongo L                                      -> 61
            //     52 Wood Block L                                 -> 77
            //     54 Cabasa                                       -> 69
            //     56 Shaker                                       -> 70
            //     58 Maracas                                      -> 70
            { 126, 0, 64, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 63, 63, 64,
                58, 36, 37, 38, 74, 36, 34, 38, 36, 38, 38, 39, 38, 50, 42, 48,
                42, 47, 46, 45, 43, 49, 41, 46, -1, 66, 63, 65, 62, 54, 64, 56,
                75, 60, 76, 61, 77, -1, 69, -1, 70, -1, 70, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // TurkishKit (126-0-67):
            //     78 Bongo Tek Roll                               -> 60
            //     79 Bongo Flam                                   -> 60
            //     80 Bongo Tek Flam                               -> 60
            //     81 Bongo Tek                                    -> 60
            { 126, 0, 67, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 60, 60,
                60, 60, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // IndianKit (126-0-114):
            //     15 Indian Hand Clap                             -> 39
            //     18 Dafli Rim                                    -> 37
            //     21 Duff Rim                                     -> 37
            //     50 Dhol 2 Rim                                   -> 37
            { 126, 0, 114, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 39,
                -1, -1, 37, -1, -1, 37, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, 37, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // BrazilianKit (126-0-119):
            //     20 Conga Lo Open BR                             -> 64
            //     21 Conga Lo Close BR                            -> 64
            //     22 Conga Lo Slap Open BR                        -> 64
            //     23 Conga Lo Slap Mute BR                        -> 64
            //     24 Conga Hi Bass BR                             -> 64
            //     25 Conga Hi Heel BR                             -> 62
            //     26 Conga Hi Tip BR                              -> 62
            //     27 Conga Hi Open BR                             -> 63
            //     28 Conga Hi Slap Close BR                       -> 62
            //     29 Conga Hi Slap Open BR                        -> 63
            //     30 Conga Hi Slap Mute BR                        -> 62
            //     31 Bongo Lo Open 1 / Rim 1 AF                   -> 61
            //     32 Bongo Lo Open 3 AF                           -> 61
            //     33 Bongo Lo Close 3 AF                          -> 61
            //     34 Bongo Lo Slap AF                             -> 61
            //     35 Bongo Hi Heel AF                             -> 60
            //     36 Bongo Hi Tip AF                              -> 60
            //     37 Bongo Hi Open 1 /Rim 1 AF                    -> 60
            //     38 Bongo Hi Open 3 / Rim 3 AF                   -> 60
            //     39 Bongo Hi Mute1 AF                            -> 60
            //     40 Bongo Hi Slap AF                             -> 60
            //     41 Agogo 2 Close BR                             -> 67
            //     42 Agogo 2 Bell 2 BR                            -> 67
            //     43 Agogo 2 Bell 1 BR                            -> 67
            //     44 Agogo 1 Close BR                             -> 67
            //     45 Agogo 1 Bell 2 BR                            -> 67
            //     46 Agogo 1 Bell 1 BR                            -> 67
            //     47 Cabasa L BR                                  -> 69
            //     48 Cabasa R BR                                  -> 69
            //     50 Tamborim 1 Back BR                           -> 54
            //     51 Tamborim 1 Close BR                          -> 54
            //     52 Tamborim 1 Open BR                           -> 54
            //     53 Tamborim 1 Rim BR                            -> 54
            //     55 Tamborim 2 Down BR                           -> 54
            //     68 Shaker 1 Up BR                               -> 70
            //     71 Shaker 2 Up BR                               -> 70
            //     72 Shaker 2 Down BR                             -> 70
            //     73 Shaker 2 Long BR                             -> 70
            //     74 Shaker 3 Up BR                               -> 70
            //     75 Shaker 3 Down BR                             -> 70
            //     76 Shaker 3 Long BR                             -> 70
            //     77 Shaker 4 Up BR                               -> 70
            //     78 Shaker 4 Down BR                             -> 70
            //     79 Shaker 4 Long BR                             -> 70
            { 126, 0, 119, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 64, 64, 64, 64, 64, 62, 62, 63, 62, 63, 62, 61,
                61, 61, 61, 60, 60, 60, 60, 60, 60, 67, 67, 67, 67, 67, 67, 69,
                69, -1, 54, 54, 54, 54, -1, 54, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 70, -1, -1, 70, 70, 70, 70, 70, 70, 70, 70, 70,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // ChineseKit (126-0-124):
            //     25 Da Gu Rim                                    -> 37
            { 126, 0, 124, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, 37, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // HitKit (127-0-4):
            //     34 Snare Pitched                                -> 38
            { 127, 0, 4, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // RockDrumKit (127-0-17):
            //     25 Snare Brush Mute Snappy Off Edge Pressed JB  -> 31
            //     30 Snare 2 RD                                   -> 38
            { 127, 0, 17, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, 31, -1, -1, -1, -1, 38, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // DanceKit (127-0-27):
            //     13 Kick Dance 1                                 -> 36
            //     14 Kick Dance 2                                 -> 36
            //     25 Snare Analog 3                               -> 38
            //     27 Snare Analog 4                               -> 38
            //     32 Snare Dance 1                                -> 38
            //     34 Rim Gate                                     -> 37
            { 127, 0, 27, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 36, 36, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, 38, -1, 38, -1, -1, -1, -1,
                38, -1, 37, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // HipHopKit (127-0-56):
            //     19 Hi-Hat Closed T8 2                           -> 42
            //     20 Tom T8 3                                     -> 47
            //     21 Hi-Hat Open T8 2                             -> 46
            //     22 Tom T8 6                                     -> 41
            //     23 Crash T8                                     -> 49
            //     24 Triangle Mute                                -> 80
            //     25 Triangle Open                                -> 81
            //     27 Tambourine Light 2                           -> 54
            //     28 Tambourine Light 1                           -> 54
            //     29 Kick HipHop 9                                -> 36
            //     30 Hi-Hat Closed Tek                            -> 42
            //     31 Kick Gate                                    -> 36
            //     32 Hi-Hat Open Lo-Fi                            -> 46
            //     34 Hi-Hat Reverse Drum&Bass                     -> 42
            //     37 Snare Analog Sm Rim                          -> 38
            //     39 Snare Clappy                                 -> 38
            //     48 Ride Cymbal 3                                -> 51
            //     50 Shaker 2                                     -> 70
            //     51 Scratch Bass Drum Forward                    -> 36
            //     52 Scratch Bass Drum Reverse                    -> 36
            //     53 Kick HipHop 2                                -> 36
            //     54 Snare HipHop Rim 2                           -> 38
            //     55 HipHop Clap 2                                -> 39
            //     57 Snare HipHop 3                               -> 38
            //     58 Electric Clap 2                              -> 39
            //     59 Kick Hip Deep                                -> 36
            //     60 Kick HipHop 3                                -> 36
            //     61 Snare HipHop Rim 3                           -> 38
            //     62 Snare HipHop 5                               -> 38
            //     63 Electric Clap 1                              -> 39
            //     65 Kick HipHop 4                                -> 36
            //     66 HipHop Clap 3                                -> 39
            //     68 Snare HipHop Rim 5                           -> 38
            //     71 Shaker 2                                     -> 70
            //     72 Kick HipHop 5                                -> 36
            //     73 Snare HipHop Rim 4                           -> 38
            //     74 Snare HipHop 6                               -> 38
            //     75 Snare HipHop 11                              -> 38
            //     76 Kick HipHop 10                               -> 36
            //     77 Snare HipHop 7                               -> 38
            //     78 HipHop Clap 5                                -> 39
            //     79 Conga H Tip                                  -> 62
            //     80 Conga H Heel                                 -> 62
            //     81 Conga H Open                                 -> 63
            { 127, 0, 56, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, 42, 47, 46, 41, 49, 80, 81, -1, 54, 54, 36, 42, 36,
                46, -1, 42, -1, -1, 38, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1,
                51, -1, 70, 36, 36, 36, 38, 39, -1, 38, 39, 36, 36, 38, 38, 39,
                -1, 36, 39, -1, 38, -1, -1, 70, 36, 38, 38, 38, 36, 38, 39, 62,
                62, 63, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // BreakKit (127-0-57):
            //     20 Snare Break 8                                -> 38
            //     21 Snare Break 9                                -> 38
            //     22 Hi-Hat Closed Break 1                        -> 42
            //     23 Hi-Hat Closed Break 2                        -> 42
            //     24 Kick Break Deep                              -> 36
            //     25 Snare Hip                                    -> 38
            //     26 Snare Lo-Fi                                  -> 38
            //     27 Snare Clappy                                 -> 38
            //     28 Snare LdwH Mono                              -> 38
            //     30 Snare Gate 1                                 -> 38
            //     32 Snare Break Rim                              -> 38
            //     34 Snare Hip Rim 4                              -> 38
            //     37 Snare Hip Rim 1                              -> 38
            //     39 Snare Break 1                                -> 38
            //     58 Cowbell RX11                                 -> 56
            //     64 Conga H Open                                 -> 63
            //     65 Bongo 2 H                                    -> 60
            //     66 Bongo 2 L                                    -> 61
            //     67 Conga Open                                   -> 63
            //     71 Timbale H                                    -> 65
            //     72 Timbale L                                    -> 66
            { 127, 0, 57, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 38, 38, 42, 42, 36, 38, 38, 38, 38, -1, 38, -1,
                38, -1, 38, -1, -1, 38, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 56, -1, -1, -1, -1, -1,
                63, 60, 61, 63, -1, -1, -1, 65, 66, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // AnalogT8Kit (127-0-58):
            //     19 Snare Hammer                                 -> 38
            //     20 Kick Zap Hard                                -> 36
            //     21 Snare Garg L                                 -> 38
            //     22 Kick Tek Power                               -> 36
            //     23 Kick Slimy                                   -> 36
            //     24 Kick T8 4                                    -> 36
            //     25 Snare Analog CR                              -> 38
            //     26 Snare T8 7                                   -> 38
            //     27 Snare Clap Analog                            -> 38
            //     28 Snare T8 6                                   -> 38
            //     29 Tom T8 5                                     -> 43
            //     30 Snare T8 5                                   -> 38
            //     31 Kick T8 3                                    -> 36
            //     32 Snare T8 4                                   -> 38
            //     34 Snare T8 3                                   -> 38
            //     37 Snare T8 Rim                                 -> 38
            //     60 Conga T8 5                                   -> 63
            //     61 Conga T8 4                                   -> 63
            //     64 Conga T8 1                                   -> 63
            //     73 Analog Shaker H                              -> 70
            //     74 Analog Shaker L                              -> 70
            { 127, 0, 58, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, 38, 36, 38, 36, 36, 36, 38, 38, 38, 38, 43, 38, 36,
                38, -1, 38, -1, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 63, 63, -1, -1,
                63, -1, -1, -1, -1, -1, -1, -1, -1, 70, 70, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // AnalogT9Kit (127-0-59):
            //     19 Snare Drum&Bass 1                            -> 38
            //     20 Kick Break 2                                 -> 36
            //     21 Snare Distortion                             -> 38
            //     22 Kick Tek Power                               -> 36
            //     23 Kick Distortion RM                           -> 36
            //     24 Kick T9 2                                    -> 36
            //     25 Snare Analog CR                              -> 38
            //     26 Snare T9 5                                   -> 38
            //     27 Clap Analog Sm                               -> 39
            //     28 Snare T9 Gate 1                              -> 38
            //     30 Snare T9 3                                   -> 38
            //     32 Snare T9 Gate 2                              -> 38
            //     34 Snare T9 6                                   -> 38
            //     37 Snare T9 Rim                                 -> 38
            //     58 Cowbell T8                                   -> 56
            //     60 Conga T8 5                                   -> 63
            //     61 Conga T8 4                                   -> 63
            //     64 Conga Open                                   -> 63
            //     67 Analog Click                                 -> 23
            //     68 Conga T8 1                                   -> 63
            { 127, 0, 59, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, 38, 36, 38, 36, 36, 36, 38, 38, 39, 38, -1, 38, -1,
                38, -1, 38, -1, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 56, -1, 63, 63, -1, -1,
                63, -1, -1, 23, 63, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // HouseKit (127-0-60):
            //     13 W Kick                                       -> 36
            //     24 Kick T9 4                                    -> 36
            //     25 Snare T8 Rim                                 -> 38
            //     26 Snare T8 5                                   -> 38
            //     27 Hand Clap                                    -> 39
            //     28 Snare Garg L                                 -> 38
            //     30 Snare T9 3                                   -> 38
            //     32 Snare T9 5                                   -> 38
            //     34 Snare T9 Gate                                -> 38
            //     37 Snare T9 Rim                                 -> 38
            //     52 Crash Cymbal 4                               -> 49
            //     58 Cowbell T8                                   -> 56
            //     64 Conga H Open 2                               -> 63
            //     78 Cuica H                                      -> 79
            { 127, 0, 60, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 36, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, 36, 38, 38, 39, 38, -1, 38, -1,
                38, -1, 38, -1, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 49, -1, -1, -1, -1, -1, 56, -1, -1, -1, -1, -1,
                63, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 79, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // DrumMachine (127-0-61):
            //     19 Snare Drum&Bass 1                            -> 38
            //     20 Kick Break 2                                 -> 36
            //     21 Snare Distortion                             -> 38
            //     22 Kick Tek Power                               -> 36
            //     23 Kick Distortion RM                           -> 36
            //     24 Bass Drum Hard Long                          -> 36
            //     25 Bass Drum Tek Power                          -> 36
            //     26 Bass Drum Distortion 5                       -> 36
            //     27 Bass Drum Distortion 3                       -> 36
            //     28 Bass Drum Distortion 1                       -> 36
            //     29 Bass Drum Drum&Bass 1                        -> 36
            //     30 Bass Drum Blip                               -> 36
            //     31 Bass Drum Analog Sm                          -> 36
            //     32 Kick T8 2                                    -> 36
            //     34 Kick T9 HD 3                                 -> 36
            //     37 Snare T9 Rim                                 -> 38
            //     50 Conga T8 1                                   -> 63
            //     52 Conga T8 2                                   -> 63
            //     53 Analog Click                                 -> 23
            //     54 Claves T8 1                                  -> 75
            //     55 Maracas T8                                   -> 70
            //     56 Tambourine Analog CR                         -> 54
            //     57 Analog Shaker                                -> 70
            //     58 Cowbell T8                                   -> 56
            //     59 Cowbell Analog CR                            -> 56
            //     60 Snare T8 1                                   -> 38
            //     61 Snare T8 2                                   -> 38
            //     62 Snare T8 3                                   -> 38
            //     63 Snare Analog CR                              -> 38
            //     64 Snare Jungle 1                               -> 38
            //     65 Snare Drum&Bass 2                            -> 38
            //     66 Snare Hip 1                                  -> 38
            //     67 Snare R&B 1                                  -> 38
            //     68 Snare R&B 2                                  -> 38
            //     69 Snare Hip 1                                  -> 38
            //     70 Snare Wood                                   -> 38
            //     71 Snare Timbre                                 -> 38
            //     72 Hi-Hat Closed T8 1                           -> 42
            //     73 Hi-Hat Open T8 1                             -> 46
            //     74 Hi-Hat Closed T8 2                           -> 42
            //     75 Hi-Hat Open T8 2                             -> 46
            //     76 Hi-Hat Pedal Acoustic                        -> 44
            //     77 Hi-Hat Closed Acoustic                       -> 42
            //     78 Hi-Hat Open Acoustic                         -> 46
            //     79 Hi-Hat Closed Lo-Fi                          -> 42
            //     80 Hi-Hat Open Lo-Fi                            -> 46
            //     81 Hi-Hat Closed Syn                            -> 42
            { 127, 0, 61, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, 38, 36, 38, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
                36, -1, 36, -1, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, 63, -1, 63, 23, 75, 70, 54, 70, 56, 56, 38, 38, 38, 38,
                38, 38, 38, 38, 38, 38, 38, 38, 42, 46, 42, 46, 44, 42, 46, 42,
                46, 42, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // 80sPopKit (127-0-66):
            //     13 Hi-Hat 2 Closed 81P                          -> 42
            //     14 Hi-Hat 2 Open 81P                            -> 46
            //     15 Hi-Hat 3 Closed 81P                          -> 42
            //     16 Hi-Hat 3 Open 81P                            -> 46
            //     17 Hi-Hat 1 Closed 83P                          -> 42
            //     18 Hi-Hat 1 Open 83P                            -> 46
            //     19 Clap 1 83P                                   -> 39
            //     20 Clap 4 81P                                   -> 39
            //     23 Side Stick 1 83P                             -> 37
            //     24 Kick 2 83P                                   -> 36
            //     25 Side Stick 3 82P                             -> 37
            //     26 Clap 1 82P                                   -> 39
            //     27 Clap 2 81P                                   -> 39
            //     28 Side Stick 4 81P                             -> 37
            //     29 Side Stick 2 81P                             -> 37
            //     30 Snare 4 81P                                  -> 38
            //     32 Snare 1 83P                                  -> 38
            //     34 Snare 2 83P                                  -> 38
            //     58 Cowbell T8                                   -> 56
            //     70 Tom B5 83P                                   -> 47
            //     71 Tom B4 83P                                   -> 47
            //     72 Tom B3 83P                                   -> 47
            //     73 Tom B2 83P                                   -> 47
            //     74 Tom B1 83P                                   -> 47
            //     76 Tom A6 81P                                   -> 47
            //     77 Tom A5 81P                                   -> 47
            //     78 Tom A4 81P                                   -> 47
            //     79 Tom A3 81P                                   -> 47
            //     80 Tom A2 81P                                   -> 47
            //     81 Tom A1 81P                                   -> 47
            { 127, 0, 66, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 42, 46, 42,
                46, 42, 46, 39, 39, -1, -1, 37, 36, 37, 39, 39, 37, 37, 38, -1,
                38, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 56, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, 47, 47, 47, 47, 47, -1, 47, 47, 47, 47,
                47, 47, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // 80sR&B Kit (127-0-67):
            //     13 Hi-Hat 3 Closed 80R                          -> 42
            //     14 Hi-Hat 3 Open 80R                            -> 46
            //     15 Hi-Hat 1 Closed 80R                          -> 42
            //     16 Hi-Hat 1 Open 80R                            -> 46
            //     17 Hi-Hat 3 Closed 81R                          -> 42
            //     18 Hi-Hat 3 Open 81R                            -> 46
            //     19 Hi-Hat 1 Closed 81R                          -> 42
            //     20 Hi-Hat 1 Open 81R                            -> 46
            //     23 Side Stick 5 81R                             -> 37
            //     24 Kick 4 81R                                   -> 36
            //     25 Side Stick 2 81R                             -> 37
            //     26 Clap 5 81R                                   -> 39
            //     27 Clap 2 81R                                   -> 39
            //     28 Clap 3 80R                                   -> 39
            //     29 Side Stick 5 80R                             -> 37
            //     30 Clap 5 80R                                   -> 39
            //     34 Snare 3 81R                                  -> 38
            //     59 Crash Cymbal RD 1                            -> 49
            //     70 Tom T8 1                                     -> 50
            //     71 Tom T8 2                                     -> 48
            //     72 Tom T8 3                                     -> 47
            //     73 Tom T8 4                                     -> 45
            //     74 Tom T8 6                                     -> 41
            //     76 Tom A6 80R                                   -> 47
            //     77 Tom A5 80R                                   -> 47
            //     78 Tom A4 80R                                   -> 47
            //     79 Tom A3 80R                                   -> 47
            //     80 Tom A2 80R                                   -> 47
            //     81 Tom A1 80R                                   -> 47
            { 127, 0, 67, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 42, 46, 42,
                46, 42, 46, 42, 46, -1, -1, 37, 36, 37, 39, 39, 39, 37, 39, -1,
                -1, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 49, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, 50, 48, 47, 45, 41, -1, 47, 47, 47, 47,
                47, 47, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // DubstepKit (127-0-68):
            //     13 Kick 4 DS                                    -> 36
            //     14 Kick 5 DS                                    -> 36
            //     22 Tin Cowbell DS                               -> 56
            //     23 Reverse Flanger Snare DS                     -> 38
            //     24 Kick 13 DS                                   -> 36
            //     25 Snare 3 DS                                   -> 38
            //     26 Clap 4 DS                                    -> 39
            //     27 Snare 4 DS                                   -> 38
            //     28 Snare 11 DS                                  -> 38
            //     29 Snare 5 DS                                   -> 38
            //     30 Snare T9 3                                   -> 38
            //     32 Clap 3 DS                                    -> 39
            //     34 Clap 2 DS                                    -> 39
            //     52 Crash Cymbal 4                               -> 49
            //     58 Cowbell T8                                   -> 56
            //     67 Cowbell Hi Pitch 82P                         -> 56
            //     68 Cowbell Middle Pitch 82P                     -> 56
            //     78 Hi-Hat Closed 4 DS                           -> 42
            //     79 Hi-Hat Closed 5 DS                           -> 42
            { 127, 0, 68, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 36, 36, -1,
                -1, -1, -1, -1, -1, -1, 56, 38, 36, 38, 39, 38, 38, 38, 38, -1,
                39, -1, 39, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 49, -1, -1, -1, -1, -1, 56, -1, -1, -1, -1, -1,
                -1, -1, -1, 56, 56, -1, -1, -1, -1, -1, -1, -1, -1, -1, 42, 42,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // EDM Kit (127-0-69):
            //     13 Kick 3 EDM                                   -> 36
            //     14 Kick 4 EDM                                   -> 36
            //     16 Snare 12 EDM                                 -> 38
            //     17 Snare 3 EDM                                  -> 38
            //     18 Snare 4 EDM                                  -> 38
            //     24 Kick 15 EDM                                  -> 36
            //     25 Snare 7 EDM                                  -> 38
            //     26 Snare 14 EDM                                 -> 38
            //     27 Snare 5 EDM                                  -> 38
            //     28 Snare 15 EDM                                 -> 38
            //     30 Snare 17 EDM                                 -> 38
            //     32 Snare 1 EDM                                  -> 38
            //     34 Snare 16 EDM                                 -> 38
            //     52 Crash Cymbal 4                               -> 49
            //     58 Cowbell T8                                   -> 56
            //     64 Conga H Open 2                               -> 63
            //     76 Clap 2 EDM                                   -> 39
            //     77 Clap 3 EDM                                   -> 39
            //     78 Hi-Hat Closed 3 EDM                          -> 42
            //     79 Hi-Hat Open 2 EDM                            -> 46
            //     80 Hi-Hat Closed 1 EDM                          -> 42
            //     81 Hi-Hat Closed 2 EDM                          -> 42
            { 127, 0, 69, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 36, 36, -1,
                38, 38, 38, -1, -1, -1, -1, -1, 36, 38, 38, 38, 38, -1, 38, -1,
                38, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 49, -1, -1, -1, -1, -1, 56, -1, -1, -1, -1, -1,
                63, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 39, 39, 42, 46,
                42, 42, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // ElectroKit (127-0-70):
            //     13 Kick 7 EK                                    -> 36
            //     14 Kick 8 EK                                    -> 36
            //     17 Kick 5 EK                                    -> 36
            //     18 Kick 6 EK                                    -> 36
            //     24 Kick 2 EK                                    -> 36
            //     25 Snare 2 EK                                   -> 38
            //     26 Snare 3 EK                                   -> 38
            //     27 Clap 6 EK                                    -> 39
            //     28 Snare 1 EK                                   -> 38
            //     29 Snare 4 EK                                   -> 38
            //     30 Snare 10 EK                                  -> 38
            //     32 Clap 5 EK                                    -> 39
            //     34 Clap 2 EK                                    -> 39
            //     52 Crash Cymbal 4                               -> 49
            //     58 Cowbell T8                                   -> 56
            //     78 Clap 8 EK                                    -> 39
            //     79 Clap 4 EK                                    -> 39
            //     80 Hi-Hat Open 1 EK                             -> 46
            //     81 Hi-Hat Open 2 EK                             -> 46
            { 127, 0, 70, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 36, 36, -1,
                -1, 36, 36, -1, -1, -1, -1, -1, 36, 38, 38, 39, 38, 38, 38, -1,
                39, -1, 39, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 49, -1, -1, -1, -1, -1, 56, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 39, 39,
                46, 46, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // TrapKit (127-0-71):
            //     13 Kick 5 TP                                    -> 36
            //     14 Kick 6 TP                                    -> 36
            //     24 Kick 11 TP                                   -> 36
            //     25 Snare 4 TP                                   -> 38
            //     26 Clap 1 TP                                    -> 39
            //     27 Clap 1 83P                                   -> 39
            //     28 Snare 5 TP                                   -> 38
            //     29 Snare 6 TP                                   -> 38
            //     30 Snare T9 3                                   -> 38
            //     32 Clap 2 TP                                    -> 39
            //     34 Clap 3 TP                                    -> 39
            //     52 Crash Cymbal 4                               -> 49
            //     58 Cowbell T8                                   -> 56
            //     64 Conga H Open 2                               -> 63
            //     75 Cowbell 2 TP                                 -> 56
            //     78 Hi-Hat Closed 1 TP                           -> 42
            //     79 Hi-Hat Closed 2 TP                           -> 42
            { 127, 0, 71, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 36, 36, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, 36, 38, 39, 39, 38, 38, 38, -1,
                39, -1, 39, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, 49, -1, -1, -1, -1, -1, 56, -1, -1, -1, -1, -1,
                63, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 56, -1, -1, 42, 42,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // SchlagerKit (127-0-72):
            //     24 Kick TB SC                                   -> 36
            //     32 Snare 1 83P                                  -> 38
            //     34 Snare 4 SC                                   -> 38
            //     39 Snare 3 SC                                   -> 38
            //     53 Crash Impact SC                              -> 49
            //     55 Crash Cymbal 2 SC                            -> 49
            //     58 Snare 5 SC                                   -> 38
            //     59 Snare 7 SC                                   -> 38
            //     60 Snare 8 SC                                   -> 38
            //     61 Snare 9 SC                                   -> 38
            //     62 Snare 10 SC                                  -> 38
            //     63 Clap 7 SC                                    -> 39
            //     64 Clap 1 SC                                    -> 39
            //     65 Clap 2 SC                                    -> 39
            //     66 Clap 3 SFX SC                                -> 39
            //     67 Clap 6 SC                                    -> 39
            //     68 Clap 5 SFX SC                                -> 39
            //     71 Clap 8 SC                                    -> 39
            //     76 Hi-Hat Closed 3 SC                           -> 42
            //     77 Hi-Hat Closed 4 SC                           -> 42
            //     78 Hi-Hat Closed 5 SC                           -> 42
            //     79 Hi-Hat Open 2 SC                             -> 46
            //     80 Shaker 1 SC                                  -> 70
            //     81 Shaker 2 SC                                  -> 70
            { 127, 0, 72, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, 36, -1, -1, -1, -1, -1, -1, -1,
                38, -1, 38, -1, -1, -1, -1, 38, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, 49, -1, 49, -1, -1, 38, 38, 38, 38, 38, 39,
                39, 39, 39, 39, 39, -1, -1, 39, -1, -1, -1, -1, 42, 42, 42, 46,
                70, 70, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // PopDrumKit (127-0-73):
            //     25 Snare Brush Mute Snappy Off Edge Pressed JB  -> 31
            //     30 Snare 5 PD                                   -> 38
            { 127, 0, 73, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, 31, -1, -1, -1, -1, 38, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // VintageOpenKit (127-0-74):
            //     30 Snare 3 VO                                   -> 38
            { 127, 0, 74, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 38, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // JazzBrushKitComp (127-0-75):
            //     23 Snare Brush Linear Sweep Short L/R JC        -> 26
            //     24 Snare Brush Linear Sweep Long L/R JC         -> 26
            //     30 Snare Brush Dynamic Sweep Long - Short JC    -> 26
            //     34 Snare Brush Mute only Shaft JC               -> 25
            { 127, 0, 75, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, 26, 26, -1, -1, -1, -1, -1, 26, -1,
                -1, -1, 25, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // JazzBrushExpanded (127-0-76):
            //     23 Snare Brush Swirl Loop 1 JB                  -> 29
            //     24 Snare Brush Swirl Loop 2 JB                  -> 29
            //     25 Snare Brush Dynamic Sweep Long JB            -> 29
            //     30 Snare Brush Linear Sweep Short L JB          -> 29
            //     34 Snare Brush Pressed JB                       -> 31
            //     58 Cowbell 1 Tip JB                             -> 56
            //     60 Snare Brush Roll JB                          -> 29
            //     61 Snare Brush Snappy Off Pressed JB            -> 31
            //     62 Snare Brush Snappy Off Edge Pressed JB       -> 31
            //     63 Snare Brush Snappy Off JB                    -> 31
            //     64 Snare Brush Snappy Off Edge JB               -> 31
            //     65 Snare Brush Mute Snappy Off Pressed JB       -> 31
            //     66 Snare Brush Mute Snappy Off Edge Pressed JB  -> 31
            //     67 Snare Brush Mute Snappy Off JB               -> 31
            //     68 Snare Brush Mute Snappy Off Edge JB          -> 31
            //     69 Snare Brush Swish L JB                       -> 29
            //     70 Snare Brush Swish R JB                       -> 29
            //     71 Kick 4 JB                                    -> 36
            //     72 Kick 3 JS                                    -> 36
            //     73 Snare 2 Side-Stick JS                        -> 37
            //     74 Snare 2 no-Rim JS                            -> 38
            //     75 Clap AF                                      -> 39
            //     76 Snare 2 Snappy Off JS                        -> 38
            //     77 Tom JS 1                                     -> 50
            //     78 Hi-Hat Edge 00 JS                            -> 42
            //     79 Tom JS 2                                     -> 48
            //     80 Hi-Hat Edge 50 JS                            -> 46
            //     81 Tom JS 3                                     -> 47
            { 127, 0, 76, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, 29, 29, 29, -1, -1, -1, -1, 29, -1,
                -1, -1, 31, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 56, -1, 29, 31, 31, 31,
                31, 31, 31, 31, 31, 29, 29, 36, 36, 37, 38, 39, 38, 50, 42, 48,
                46, 47, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // VintageMutedKit (127-0-77):
            //     30 Snare 3 VM                                   -> 38
            { 127, 0, 77, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 38, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // JazzStickKit (127-0-78):
            //     25 Snappy Off Side-Stick JS                     -> 37
            //     30 Snare 1 JS                                   -> 38
            //     67 Cowbell 1 JB                                 -> 56
            //     68 Cowbell 1 Tip JB                             -> 56
            { 127, 0, 78, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, 37, -1, -1, -1, -1, 38, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, 56, 56, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // AcousticKit (127-0-89):
            //     34 Rim Acoustic                                 -> 37
            { 127, 0, 89, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, 37, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // RockKit (127-0-90):
            //     34 Rim Rock                                     -> 37
            { 127, 0, 90, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, 37, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
            // RealDrumKit (127-0-91):
            //     34 Rim Real                                     -> 37
            { 127, 0, 91, {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, 37, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
            } },
        };
        count = (int) (sizeof (maps) / sizeof (maps[0]));
        return maps;
    }

    /** The key a style note should play on a composed kit, given the kit the
        style asked for.  Unknown kits and unmapped keys come back unchanged. */
    inline int remapKey (int msb, int lsb, int pc, int key) noexcept
    {
        if (key < 0 || key > 127) return key;
        int n = 0;
        const KitMap* maps = kitMaps (n);
        for (int i = 0; i < n; ++i)
            if (maps[i].msb == msb && maps[i].lsb == lsb && maps[i].pc == pc)
            {
                const int t = maps[i].to[key];
                return t >= 0 ? t : key;
            }
        return key;
    }
} // namespace sx920
