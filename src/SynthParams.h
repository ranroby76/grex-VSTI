#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "SynthLibrary.h"      // edm::Sound, VoiceType, library(), findSound()

//==============================================================================
//  SynthParams.h  —  every value that shapes an EDM KIT sound, as editor
//  parameters.
//
//  A kit cell carries a full COPY of its sound's prototype (edm::Sound), and
//  the voice plays from that copy.  So every "seeded" number - mode ratios,
//  wire noise, click bands, envelope times - can be edited per key: this file
//  names each field, gives it a range and a tab, and saves what differs.
//
//      Spec    one field: storage id, slider label, tab, real range, mapping
//      Table   the tabs and specs of one voice type (kick, snare, clap, hat,
//              cymbal, metal, tone).  The editor's MAIN tab (the macros) is
//              not in here - it sits before these tabs.
//
//  The editor shows every value 0..200 (201 steps) like the other sound
//  editors; toUi / fromUi convert.  Frequencies, times and ratios are mapped
//  LOG (even resolution per octave / per decade), levels and amounts LINEAR.
//  Every range contains every factory value with headroom (checked by test).
//
//  SAVING: only fields that differ from a reference sound are written, as
//  "id=value" with value x1,000,000 as a 64-bit integer - no decimal point, so
//  a file reads back identically under any number locale, exact to 1e-6.
//  IDs are the prototype field names and must never change once shipped.
//
//  tabInfo: the text behind each tab's "i" button - what that group of
//  sliders shapes, section first, then one line per slider.
//
//  Header-only, no JUCE: the engine, the editor and the tests all include it.
//==============================================================================
namespace edm
{
namespace params
{
    enum class Map : uint8_t { Lin, Log };

    struct Spec
    {
        const char* id;                     // storage id (never rename)
        const char* label;                  // slider caption
        int         tab;                    // index into Table::tabs
        float       lo, hi;                 // real range
        Map         map;
        float*      (*field) (Sound&);      // the field inside the cell's prototype copy
    };

    struct Table
    {
        std::vector<const char*> tabs;      // tabs after MAIN
        std::vector<Spec>        specs;
    };

   #define EDM_SPEC(ID, LABEL, TAB, LO, HI, MAP, MEMBER) \
        Spec { ID, LABEL, TAB, LO, HI, Map::MAP, [] (Sound& s) -> float* { return &s.MEMBER; } }

    //==========================================================================
    //  KICK voice - kicks, toms and hand drums (a struck membrane)
    //==========================================================================
    inline const Table& kickTable()
    {
        static const Table t
        {
            { "BODY", "PITCH", "CLICK", "NOISE", "MODE FREQ", "MODE LEVEL", "MODE DECAY" },
            {
                EDM_SPEC ("f0",          "FREQ",      0, 20.0f,    1200.0f, Log, kick.f0),
                EDM_SPEC ("level",       "LEVEL",     0, 0.0f,     1.5f,    Lin, kick.level),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, kick.drive),
                EDM_SPEC ("asym",        "ASYM",      0, 0.0f,     1.0f,    Lin, kick.asym),
                EDM_SPEC ("onset",       "ONSET",     0, 0.0f,     1.0f,    Lin, kick.onset),
                EDM_SPEC ("modeScatter", "SCATTER",   0, 0.0f,     0.05f,   Lin, kick.modeScatter),
                EDM_SPEC ("fastShare",   "FAST AMT",  0, 0.0f,     1.0f,    Lin, kick.fastShare),
                EDM_SPEC ("fastRatio",   "FAST RATE", 0, 0.02f,    1.0f,    Log, kick.fastRatio),

                EDM_SPEC ("pitchRatio",  "AMOUNT",    1, 1.0f,     16.0f,   Log, kick.pitchRatio),
                EDM_SPEC ("pitchTau",    "TIME",      1, 0.001f,   0.5f,    Log, kick.pitchTau),
                EDM_SPEC ("spike",       "SPIKE",     1, 0.0f,     3.0f,    Lin, kick.spike),
                EDM_SPEC ("spikeTau",    "SPK TIME",  1, 0.0005f,  0.02f,   Log, kick.spikeTau),

                EDM_SPEC ("clickLevel",  "LEVEL",     2, 0.0f,     1.5f,    Lin, kick.clickLevel),
                EDM_SPEC ("clickFreq",   "FREQ",      2, 200.0f,   12000.0f, Log, kick.clickFreq),
                EDM_SPEC ("clickTau",    "TIME",      2, 0.0003f,  0.03f,   Log, kick.clickTau),
                EDM_SPEC ("clickQ",      "Q",         2, 0.2f,     4.0f,    Log, kick.clickQ),

                EDM_SPEC ("noiseLevel",  "LEVEL",     3, 0.0f,     1.0f,    Lin, kick.noiseLevel),
                EDM_SPEC ("noiseCutoff", "FREQ",      3, 50.0f,    16000.0f, Log, kick.noiseCutoff),
                EDM_SPEC ("noiseMode",   "LP BP HP",  3, 0.0f,     1.0f,    Lin, kick.noiseMode),
                EDM_SPEC ("noiseTau",    "TIME",      3, 0.002f,   1.0f,    Log, kick.noiseTau),

                EDM_SPEC ("modeRatio1",  "R1", 4, 0.5f, 16.0f, Log, kick.modeRatio[0]),
                EDM_SPEC ("modeRatio2",  "R2", 4, 0.5f, 16.0f, Log, kick.modeRatio[1]),
                EDM_SPEC ("modeRatio3",  "R3", 4, 0.5f, 16.0f, Log, kick.modeRatio[2]),
                EDM_SPEC ("modeRatio4",  "R4", 4, 0.5f, 16.0f, Log, kick.modeRatio[3]),
                EDM_SPEC ("modeRatio5",  "R5", 4, 0.5f, 16.0f, Log, kick.modeRatio[4]),
                EDM_SPEC ("modeRatio6",  "R6", 4, 0.5f, 16.0f, Log, kick.modeRatio[5]),
                EDM_SPEC ("modeRatio7",  "R7", 4, 0.5f, 16.0f, Log, kick.modeRatio[6]),
                EDM_SPEC ("modeRatio8",  "R8", 4, 0.5f, 16.0f, Log, kick.modeRatio[7]),

                EDM_SPEC ("modeGain1",   "L1", 5, 0.0f, 1.5f, Lin, kick.modeGain[0]),
                EDM_SPEC ("modeGain2",   "L2", 5, 0.0f, 1.5f, Lin, kick.modeGain[1]),
                EDM_SPEC ("modeGain3",   "L3", 5, 0.0f, 1.5f, Lin, kick.modeGain[2]),
                EDM_SPEC ("modeGain4",   "L4", 5, 0.0f, 1.5f, Lin, kick.modeGain[3]),
                EDM_SPEC ("modeGain5",   "L5", 5, 0.0f, 1.5f, Lin, kick.modeGain[4]),
                EDM_SPEC ("modeGain6",   "L6", 5, 0.0f, 1.5f, Lin, kick.modeGain[5]),
                EDM_SPEC ("modeGain7",   "L7", 5, 0.0f, 1.5f, Lin, kick.modeGain[6]),
                EDM_SPEC ("modeGain8",   "L8", 5, 0.0f, 1.5f, Lin, kick.modeGain[7]),

                EDM_SPEC ("modeTau1",    "D1", 6, 0.005f, 4.0f, Log, kick.modeTau[0]),
                EDM_SPEC ("modeTau2",    "D2", 6, 0.005f, 4.0f, Log, kick.modeTau[1]),
                EDM_SPEC ("modeTau3",    "D3", 6, 0.005f, 4.0f, Log, kick.modeTau[2]),
                EDM_SPEC ("modeTau4",    "D4", 6, 0.005f, 4.0f, Log, kick.modeTau[3]),
                EDM_SPEC ("modeTau5",    "D5", 6, 0.005f, 4.0f, Log, kick.modeTau[4]),
                EDM_SPEC ("modeTau6",    "D6", 6, 0.005f, 4.0f, Log, kick.modeTau[5]),
                EDM_SPEC ("modeTau7",    "D7", 6, 0.005f, 4.0f, Log, kick.modeTau[6]),
                EDM_SPEC ("modeTau8",    "D8", 6, 0.005f, 4.0f, Log, kick.modeTau[7]),
            }
        };
        return t;
    }

    //==========================================================================
    //  KICK MODELS (KickVoice.h, THE KICK MODELS): each model has its own
    //  controls.  The ids of fields shared with the modal kick stay the same.
    //==========================================================================
    //  KICK MODEL 4 - SYNTH: the produced EDM kick (KickVoice.h)
    //==========================================================================
    inline const Table& kickSynthTable()
    {
        static const Table t
        {
            { "SWEEP", "BODY", "CLICK", "SATURATE" },
            {
                EDM_SPEC ("f0",          "END FREQ",  0, 20.0f,    200.0f,  Log, kick.f0),
                EDM_SPEC ("sweepOct",    "SWEEP",     0, 0.0f,     6.0f,    Lin, kick.sweepOct),
                EDM_SPEC ("sweepFast",   "FAST",      0, 0.0005f,  0.05f,   Log, kick.sweepFast),
                EDM_SPEC ("sweepSlow",   "SLOW",      0, 0.005f,   0.5f,    Log, kick.sweepSlow),
                EDM_SPEC ("sweepMix",    "SLOW AMT",  0, 0.0f,     1.0f,    Lin, kick.sweepMix),

                EDM_SPEC ("holdTime",    "HOLD",      1, 0.0f,     0.3f,    Lin, kick.holdTime),
                EDM_SPEC ("bodyDecay",   "DECAY",     1, 0.002f,   1.0f,    Log, kick.bodyDecay),
                EDM_SPEC ("tailLevel",   "TAIL",      1, 0.0f,     1.0f,    Lin, kick.tailLevel),
                EDM_SPEC ("tailDecay",   "TAIL TIME", 1, 0.005f,   2.0f,    Log, kick.tailDecay),
                EDM_SPEC ("startPhase",  "PHASE",     1, 0.0f,     1.0f,    Lin, kick.startPhase),
                EDM_SPEC ("gateTime",    "LENGTH",    1, 0.0f,     1.0f,    Lin, kick.gateTime),
                EDM_SPEC ("level",       "LEVEL",     1, 0.0f,     2.0f,    Lin, kick.level),

                EDM_SPEC ("clickLevel",  "CLICK",     2, 0.0f,     1.5f,    Lin, kick.clickLevel),
                EDM_SPEC ("clickFreq",   "CLK FREQ",  2, 500.0f,   15000.0f, Log, kick.clickFreq),
                EDM_SPEC ("clickTau",    "CLK TIME",  2, 0.0003f,  0.03f,   Log, kick.clickTau),
                EDM_SPEC ("clickQ",      "CLK Q",     2, 0.2f,     4.0f,    Log, kick.clickQ),

                EDM_SPEC ("drive",       "DRIVE",     3, 0.0f,     1.0f,    Lin, kick.drive),
                EDM_SPEC ("asym",        "ASYM",      3, 0.0f,     1.0f,    Lin, kick.asym),
            }
        };
        return t;
    }

    //==========================================================================
    inline const Table& kick808Table()
    {
        static const Table t
        {
            { "CIRCUIT" },
            {
                EDM_SPEC ("f0",          "FREQ",      0, 20.0f,    400.0f,  Log, kick.f0),
                EDM_SPEC ("decay808",    "DECAY",     0, 0.0f,     1.0f,    Lin, kick.decay808),
                EDM_SPEC ("tone808",     "TONE",      0, 0.0f,     1.0f,    Lin, kick.tone808),
                EDM_SPEC ("attackFm",    "ATTACK FM", 0, 0.0f,     2.0f,    Lin, kick.attackFm),
                EDM_SPEC ("selfFm",      "SELF FM",   0, 0.0f,     2.0f,    Lin, kick.selfFm),
                EDM_SPEC ("level",       "LEVEL",     0, 0.0f,     2.0f,    Lin, kick.level),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, kick.drive),
                EDM_SPEC ("asym",        "ASYM",      0, 0.0f,     1.0f,    Lin, kick.asym),
            }
        };
        return t;
    }

    inline const Table& kick909Table()
    {
        static const Table t
        {
            { "CIRCUIT" },
            {
                EDM_SPEC ("f0",          "FREQ",      0, 20.0f,    400.0f,  Log, kick.f0),
                EDM_SPEC ("decay909",    "DECAY",     0, 0.0f,     1.0f,    Lin, kick.decay909),
                EDM_SPEC ("tone909",     "TONE",      0, 0.0f,     1.0f,    Lin, kick.tone909),
                EDM_SPEC ("dirt",        "DIRT",      0, 0.0f,     1.0f,    Lin, kick.dirt),
                EDM_SPEC ("fmAmount",    "SWEEP",     0, 0.0f,     2.0f,    Lin, kick.fmAmount),
                EDM_SPEC ("fmDecay",     "SWEEP TM",  0, 0.0f,     1.0f,    Lin, kick.fmDecay),
                EDM_SPEC ("level",       "LEVEL",     0, 0.0f,     2.0f,    Lin, kick.level),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, kick.drive),
                EDM_SPEC ("asym",        "ASYM",      0, 0.0f,     1.0f,    Lin, kick.asym),
            }
        };
        return t;
    }

    inline const Table& kickStruckTable()
    {
        static const Table t
        {
            { "BODY", "STRIKE", "CLICK", "NOISE", "MODE FREQ", "MODE LEVEL", "MODE DECAY" },
            {
                EDM_SPEC ("f0",          "FREQ",      0, 20.0f,    1200.0f, Log, kick.f0),
                EDM_SPEC ("level",       "LEVEL",     0, 0.0f,     2.0f,    Lin, kick.level),
                EDM_SPEC ("tension",     "TENSION",   0, 0.0f,     4.0f,    Lin, kick.tension),
                EDM_SPEC ("vcaDrive",    "VCA",       0, 0.0f,     1.0f,    Lin, kick.vcaDrive),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, kick.drive),
                EDM_SPEC ("asym",        "ASYM",      0, 0.0f,     1.0f,    Lin, kick.asym),
                EDM_SPEC ("modeScatter", "SCATTER",   0, 0.0f,     0.05f,   Lin, kick.modeScatter),
                EDM_SPEC ("strikeFast",  "FAST AMT",  0, 0.0f,     0.95f,   Lin, kick.strikeFast),
                EDM_SPEC ("strikeFastTau","FAST TIME",0, 0.002f,   0.5f,    Log, kick.strikeFastTau),

                EDM_SPEC ("hardness",    "HARDNESS",  1, 0.0f,     1.0f,    Lin, kick.hardness),
                EDM_SPEC ("pitchRatio",  "SWEEP",     1, 1.0f,     16.0f,   Log, kick.pitchRatio),
                EDM_SPEC ("pitchTau",    "SWEEP TM",  1, 0.001f,   0.5f,    Log, kick.pitchTau),
                EDM_SPEC ("spike",       "SPIKE",     1, 0.0f,     3.0f,    Lin, kick.spike),
                EDM_SPEC ("spikeTau",    "SPK TIME",  1, 0.0005f,  0.02f,   Log, kick.spikeTau),

                EDM_SPEC ("bodyClick",   "BODY CLK",  2, 0.0f,     2.0f,    Lin, kick.bodyClick),
                EDM_SPEC ("clickLevel",  "BEATER",    2, 0.0f,     1.5f,    Lin, kick.clickLevel),
                EDM_SPEC ("clickFreq",   "FREQ",      2, 200.0f,   12000.0f, Log, kick.clickFreq),
                EDM_SPEC ("clickTau",    "TIME",      2, 0.0003f,  0.03f,   Log, kick.clickTau),
                EDM_SPEC ("clickQ",      "Q",         2, 0.2f,     4.0f,    Log, kick.clickQ),

                EDM_SPEC ("noiseLevel",  "LEVEL",     3, 0.0f,     1.0f,    Lin, kick.noiseLevel),
                EDM_SPEC ("noiseCutoff", "FREQ",      3, 50.0f,    16000.0f, Log, kick.noiseCutoff),
                EDM_SPEC ("noiseMode",   "LP BP HP",  3, 0.0f,     1.0f,    Lin, kick.noiseMode),
                EDM_SPEC ("noiseTau",    "TIME",      3, 0.002f,   1.0f,    Log, kick.noiseTau),

                EDM_SPEC ("modeRatio1",  "R1", 4, 0.5f, 16.0f, Log, kick.modeRatio[0]),
                EDM_SPEC ("modeRatio2",  "R2", 4, 0.5f, 16.0f, Log, kick.modeRatio[1]),
                EDM_SPEC ("modeRatio3",  "R3", 4, 0.5f, 16.0f, Log, kick.modeRatio[2]),
                EDM_SPEC ("modeRatio4",  "R4", 4, 0.5f, 16.0f, Log, kick.modeRatio[3]),
                EDM_SPEC ("modeRatio5",  "R5", 4, 0.5f, 16.0f, Log, kick.modeRatio[4]),
                EDM_SPEC ("modeRatio6",  "R6", 4, 0.5f, 16.0f, Log, kick.modeRatio[5]),
                EDM_SPEC ("modeRatio7",  "R7", 4, 0.5f, 16.0f, Log, kick.modeRatio[6]),
                EDM_SPEC ("modeRatio8",  "R8", 4, 0.5f, 16.0f, Log, kick.modeRatio[7]),

                EDM_SPEC ("modeGain1",   "L1", 5, 0.0f, 1.5f, Lin, kick.modeGain[0]),
                EDM_SPEC ("modeGain2",   "L2", 5, 0.0f, 1.5f, Lin, kick.modeGain[1]),
                EDM_SPEC ("modeGain3",   "L3", 5, 0.0f, 1.5f, Lin, kick.modeGain[2]),
                EDM_SPEC ("modeGain4",   "L4", 5, 0.0f, 1.5f, Lin, kick.modeGain[3]),
                EDM_SPEC ("modeGain5",   "L5", 5, 0.0f, 1.5f, Lin, kick.modeGain[4]),
                EDM_SPEC ("modeGain6",   "L6", 5, 0.0f, 1.5f, Lin, kick.modeGain[5]),
                EDM_SPEC ("modeGain7",   "L7", 5, 0.0f, 1.5f, Lin, kick.modeGain[6]),
                EDM_SPEC ("modeGain8",   "L8", 5, 0.0f, 1.5f, Lin, kick.modeGain[7]),

                EDM_SPEC ("modeTau1",    "D1", 6, 0.00005f, 4.0f, Log, kick.modeTau[0]),
                EDM_SPEC ("modeTau2",    "D2", 6, 0.00005f, 4.0f, Log, kick.modeTau[1]),
                EDM_SPEC ("modeTau3",    "D3", 6, 0.00005f, 4.0f, Log, kick.modeTau[2]),
                EDM_SPEC ("modeTau4",    "D4", 6, 0.00005f, 4.0f, Log, kick.modeTau[3]),
                EDM_SPEC ("modeTau5",    "D5", 6, 0.00005f, 4.0f, Log, kick.modeTau[4]),
                EDM_SPEC ("modeTau6",    "D6", 6, 0.00005f, 4.0f, Log, kick.modeTau[5]),
                EDM_SPEC ("modeTau7",    "D7", 6, 0.00005f, 4.0f, Log, kick.modeTau[6]),
                EDM_SPEC ("modeTau8",    "D8", 6, 0.00005f, 4.0f, Log, kick.modeTau[7]),
            }
        };
        return t;
    }

    //==========================================================================
    //  SNARE voice - snares, side sticks and woods
    //==========================================================================
    inline const Table& snareTable()
    {
        static const Table t
        {
            { "BODY", "MODES", "WIRES", "CLICK", "RIM" },
            {
                EDM_SPEC ("bodyF0",      "FREQ",      0, 60.0f,    2000.0f, Log, snare.bodyF0),
                EDM_SPEC ("level",       "LEVEL",     0, 0.0f,     1.5f,    Lin, snare.level),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, snare.drive),
                EDM_SPEC ("onset",       "ONSET",     0, 0.0f,     1.0f,    Lin, snare.onset),
                EDM_SPEC ("bodyScatter", "SCATTER",   0, 0.0f,     0.05f,   Lin, snare.bodyScatter),
                EDM_SPEC ("pitchRatio",  "PITCH AMT", 0, 1.0f,     4.0f,    Log, snare.pitchRatio),
                EDM_SPEC ("pitchTau",    "PITCH TM",  0, 0.001f,   0.1f,    Log, snare.pitchTau),

                EDM_SPEC ("bodyRatio1",  "R1", 1, 0.5f,   8.0f, Log, snare.bodyRatio[0]),
                EDM_SPEC ("bodyRatio2",  "R2", 1, 0.5f,   8.0f, Log, snare.bodyRatio[1]),
                EDM_SPEC ("bodyRatio3",  "R3", 1, 0.5f,   8.0f, Log, snare.bodyRatio[2]),
                EDM_SPEC ("bodyGain1",   "L1", 1, 0.0f,   1.5f, Lin, snare.bodyGain[0]),
                EDM_SPEC ("bodyGain2",   "L2", 1, 0.0f,   1.5f, Lin, snare.bodyGain[1]),
                EDM_SPEC ("bodyGain3",   "L3", 1, 0.0f,   1.5f, Lin, snare.bodyGain[2]),
                EDM_SPEC ("bodyTau1",    "D1", 1, 0.002f, 1.0f, Log, snare.bodyTau[0]),
                EDM_SPEC ("bodyTau2",    "D2", 1, 0.002f, 1.0f, Log, snare.bodyTau[1]),
                EDM_SPEC ("bodyTau3",    "D3", 1, 0.002f, 1.0f, Log, snare.bodyTau[2]),

                EDM_SPEC ("wireLevel",   "LEVEL",     2, 0.0f,     1.5f,    Lin, snare.wireLevel),
                EDM_SPEC ("wireFreq",    "FREQ",      2, 500.0f,   14000.0f, Log, snare.wireFreq),
                EDM_SPEC ("wireQ",       "Q",         2, 0.2f,     4.0f,    Log, snare.wireQ),
                EDM_SPEC ("wireHpMix",   "HP MIX",    2, 0.0f,     1.0f,    Lin, snare.wireHpMix),
                EDM_SPEC ("wireTau",     "DECAY",     2, 0.005f,   1.5f,    Log, snare.wireTau),
                EDM_SPEC ("wireAttack",  "ATTACK",    2, 0.0001f,  0.02f,   Log, snare.wireAttack),
                EDM_SPEC ("wireBuzz",    "BUZZ",      2, 0.0f,     1.0f,    Lin, snare.wireBuzz),
                EDM_SPEC ("wireFast",    "FAST",      2, 0.0f,     1.0f,    Lin, snare.wireFast),

                EDM_SPEC ("clickLevel",  "LEVEL",     3, 0.0f,     1.5f,    Lin, snare.clickLevel),
                EDM_SPEC ("clickFreq",   "FREQ",      3, 500.0f,   14000.0f, Log, snare.clickFreq),
                EDM_SPEC ("clickTau",    "TIME",      3, 0.0003f,  0.03f,   Log, snare.clickTau),
                EDM_SPEC ("clickQ",      "Q",         3, 0.2f,     4.0f,    Log, snare.clickQ),

                EDM_SPEC ("rimLevel",    "LEVEL",     4, 0.0f,     1.5f,    Lin, snare.rimLevel),
                EDM_SPEC ("rimFreq",     "FREQ",      4, 300.0f,   8000.0f, Log, snare.rimFreq),
                EDM_SPEC ("rimTau",      "TIME",      4, 0.003f,   0.3f,    Log, snare.rimTau),
            }
        };
        return t;
    }

    //==========================================================================
    //  SNARE MODELS (SnareVoice.h, THE SNARE MODELS): each model has its own
    //  controls.  Ids shared with the original snare stay the same.
    //==========================================================================
    inline const Table& snare808Table()
    {
        static const Table t
        {
            { "CIRCUIT" },
            {
                EDM_SPEC ("bodyF0",      "FREQ",      0, 60.0f,    1000.0f, Log, snare.bodyF0),
                EDM_SPEC ("tone808",     "TONE",      0, 0.0f,     1.0f,    Lin, snare.tone808),
                EDM_SPEC ("decay808",    "DECAY",     0, 0.0f,     1.0f,    Lin, snare.decay808),
                EDM_SPEC ("snappy",      "SNAPPY",    0, 0.0f,     1.0f,    Lin, snare.snappy),
                EDM_SPEC ("level",       "LEVEL",     0, 0.0f,     2.0f,    Lin, snare.level),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, snare.drive),
            }
        };
        return t;
    }

    inline const Table& snare909Table()
    {
        static const Table t
        {
            { "CIRCUIT" },
            {
                EDM_SPEC ("bodyF0",      "FREQ",      0, 50.0f,    1000.0f, Log, snare.bodyF0),
                EDM_SPEC ("fmAmount",    "SWEEP",     0, 0.0f,     1.0f,    Lin, snare.fmAmount),
                EDM_SPEC ("decay909",    "DECAY",     0, 0.0f,     1.0f,    Lin, snare.decay909),
                EDM_SPEC ("snappy",      "SNAPPY",    0, 0.0f,     1.0f,    Lin, snare.snappy),
                EDM_SPEC ("level",       "LEVEL",     0, 0.0f,     2.0f,    Lin, snare.level),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, snare.drive),
            }
        };
        return t;
    }

    inline const Table& snareStruckTable()
    {
        static const Table t
        {
            { "HEAD", "STRIKE", "MODES", "WIRES", "RIM" },
            {
                EDM_SPEC ("bodyF0",      "FREQ",      0, 60.0f,    9000.0f, Log, snare.bodyF0),
                EDM_SPEC ("level",       "LEVEL",     0, 0.0f,     2.0f,    Lin, snare.level),
                EDM_SPEC ("tension",     "TENSION",   0, 0.0f,     2.0f,    Lin, snare.tension),
                EDM_SPEC ("bodyScatter", "SCATTER",   0, 0.0f,     0.05f,   Lin, snare.bodyScatter),
                EDM_SPEC ("vcaDrive",    "VCA",       0, 0.0f,     1.0f,    Lin, snare.vcaDrive),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, snare.drive),
                EDM_SPEC ("pitchRatio",  "PITCH AMT", 0, 1.0f,     4.0f,    Log, snare.pitchRatio),
                EDM_SPEC ("pitchTau",    "PITCH TM",  0, 0.001f,   0.1f,    Log, snare.pitchTau),

                EDM_SPEC ("hardness",    "HARDNESS",  1, 0.0f,     1.0f,    Lin, snare.hardness),
                EDM_SPEC ("bodyClick",   "BODY CLK",  1, 0.0f,     2.0f,    Lin, snare.bodyClick),
                EDM_SPEC ("clickLevel",  "CLICK",     1, 0.0f,     1.5f,    Lin, snare.clickLevel),
                EDM_SPEC ("clickFreq",   "CLK FREQ",  1, 500.0f,   14000.0f, Log, snare.clickFreq),
                EDM_SPEC ("clickTau",    "CLK TIME",  1, 0.0003f,  0.03f,   Log, snare.clickTau),
                EDM_SPEC ("clickQ",      "CLK Q",     1, 0.2f,     4.0f,    Log, snare.clickQ),

                EDM_SPEC ("bodyRatio1",  "R1", 2, 0.5f,   8.0f, Log, snare.bodyRatio[0]),
                EDM_SPEC ("bodyRatio2",  "R2", 2, 0.5f,   8.0f, Log, snare.bodyRatio[1]),
                EDM_SPEC ("bodyRatio3",  "R3", 2, 0.5f,   8.0f, Log, snare.bodyRatio[2]),
                EDM_SPEC ("bodyGain1",   "L1", 2, 0.0f,   1.5f, Lin, snare.bodyGain[0]),
                EDM_SPEC ("bodyGain2",   "L2", 2, 0.0f,   1.5f, Lin, snare.bodyGain[1]),
                EDM_SPEC ("bodyGain3",   "L3", 2, 0.0f,   1.5f, Lin, snare.bodyGain[2]),
                EDM_SPEC ("bodyTau1",    "D1", 2, 0.001f, 1.0f, Log, snare.bodyTau[0]),
                EDM_SPEC ("bodyTau2",    "D2", 2, 0.001f, 1.0f, Log, snare.bodyTau[1]),
                EDM_SPEC ("bodyTau3",    "D3", 2, 0.001f, 1.0f, Log, snare.bodyTau[2]),

                EDM_SPEC ("wireLevel",   "LEVEL",     3, 0.0f,     1.5f,    Lin, snare.wireLevel),
                EDM_SPEC ("wireFreq",    "FREQ",      3, 500.0f,   14000.0f, Log, snare.wireFreq),
                EDM_SPEC ("wireQ",       "Q",         3, 0.2f,     4.0f,    Log, snare.wireQ),
                EDM_SPEC ("wireHpMix",   "HP MIX",    3, 0.0f,     1.0f,    Lin, snare.wireHpMix),
                EDM_SPEC ("wireAttack",  "ATTACK",    3, 0.0001f,  0.02f,   Log, snare.wireAttack),
                EDM_SPEC ("wireHold",    "HOLD",      3, 0.0f,     0.1f,    Lin, snare.wireHold),
                EDM_SPEC ("wireTau",     "DECAY",     3, 0.005f,   1.5f,    Log, snare.wireTau),
                EDM_SPEC ("wireFast",    "FAST",      3, 0.0f,     1.0f,    Lin, snare.wireFast),
                EDM_SPEC ("wireBuzz",    "BUZZ",      3, 0.0f,     1.0f,    Lin, snare.wireBuzz),
                EDM_SPEC ("wireRect",    "RECT",      3, 0.0f,     1.0f,    Lin, snare.wireRect),

                EDM_SPEC ("rimLevel",    "LEVEL",     4, 0.0f,     2.5f,    Lin, snare.rimLevel),
                EDM_SPEC ("rimFreq",     "FREQ",      4, 300.0f,   10000.0f, Log, snare.rimFreq),
                EDM_SPEC ("rimTau",      "RING",      4, 0.002f,   0.3f,    Log, snare.rimTau),
            }
        };
        return t;
    }

    //==========================================================================
    //  CLAP voice - claps, scrapes and castanets
    //==========================================================================
    inline const Table& clapTable()
    {
        static const Table t
        {
            { "BURSTS", "TONE", "TAIL" },
            {
                EDM_SPEC ("bursts",      "COUNT",     0, 1.0f,     32.0f,   Lin, clap.bursts),
                EDM_SPEC ("spacing",     "SPACING",   0, 0.001f,   0.08f,   Log, clap.spacing),
                EDM_SPEC ("spread",      "SPREAD",    0, 0.0f,     1.0f,    Lin, clap.spread),
                EDM_SPEC ("burstTau",    "BURST",     0, 0.0005f,  0.03f,   Log, clap.burstTau),
                EDM_SPEC ("burstDecay",  "FADE",      0, 0.1f,     1.2f,    Lin, clap.burstDecay),

                EDM_SPEC ("bpFreq",      "FREQ",      1, 200.0f,   8000.0f, Log, clap.bpFreq),
                EDM_SPEC ("bpQ",         "Q",         1, 0.2f,     10.0f,   Log, clap.bpQ),
                EDM_SPEC ("hpMix",       "SIZZLE",    1, 0.0f,     1.5f,    Lin, clap.hpMix),
                EDM_SPEC ("hpFreq",      "SIZ FREQ",  1, 500.0f,   14000.0f, Log, clap.hpFreq),

                EDM_SPEC ("tailLevel",   "TAIL",      2, 0.0f,     1.5f,    Lin, clap.tailLevel),
                EDM_SPEC ("tailTau",     "TAIL TIME", 2, 0.005f,   2.0f,    Log, clap.tailTau),
                EDM_SPEC ("drive",       "DRIVE",     2, 0.0f,     1.0f,    Lin, clap.drive),
                EDM_SPEC ("level",       "LEVEL",     2, 0.0f,     1.5f,    Lin, clap.level),
            }
        };
        return t;
    }

    //==========================================================================
    //  HAT voice - hi-hats and shakers
    //==========================================================================
    inline const Table& hatTable()
    {
        static const Table t
        {
            { "METAL", "NOISE", "ENVELOPE" },
            {
                EDM_SPEC ("baseFreq",    "FREQ",      0, 100.0f,   6000.0f, Log, hat.baseFreq),
                EDM_SPEC ("ratio1",      "R1",        0, 0.5f,     8.0f,    Log, hat.ratio[0]),
                EDM_SPEC ("ratio2",      "R2",        0, 0.5f,     8.0f,    Log, hat.ratio[1]),
                EDM_SPEC ("ratio3",      "R3",        0, 0.5f,     8.0f,    Log, hat.ratio[2]),
                EDM_SPEC ("ratio4",      "R4",        0, 0.5f,     8.0f,    Log, hat.ratio[3]),
                EDM_SPEC ("ratio5",      "R5",        0, 0.5f,     8.0f,    Log, hat.ratio[4]),
                EDM_SPEC ("ratio6",      "R6",        0, 0.5f,     8.0f,    Log, hat.ratio[5]),
                EDM_SPEC ("metalLevel",  "LEVEL",     0, 0.0f,     1.5f,    Lin, hat.metalLevel),

                EDM_SPEC ("noiseLevel",  "LEVEL",     1, 0.0f,     1.5f,    Lin, hat.noiseLevel),
                EDM_SPEC ("bpFreq",      "BP FREQ",   1, 1000.0f,  16000.0f, Log, hat.bpFreq),
                EDM_SPEC ("bpQ",         "BP Q",      1, 0.2f,     6.0f,    Log, hat.bpQ),
                EDM_SPEC ("hpFreq",      "HP FREQ",   1, 500.0f,   14000.0f, Log, hat.hpFreq),

                EDM_SPEC ("closedTau",   "CLOSED",    2, 0.003f,   0.5f,    Log, hat.closedTau),
                EDM_SPEC ("pedalTau",    "PEDAL",     2, 0.003f,   0.5f,    Log, hat.pedalTau),
                EDM_SPEC ("openTau",     "OPEN",      2, 0.02f,    3.0f,    Log, hat.openTau),
                EDM_SPEC ("attack",      "ATTACK",    2, 0.00005f, 0.05f,   Log, hat.attack),
                EDM_SPEC ("clickLevel",  "CLICK",     2, 0.0f,     1.5f,    Lin, hat.clickLevel),
                EDM_SPEC ("drive",       "DRIVE",     2, 0.0f,     1.0f,    Lin, hat.drive),
                EDM_SPEC ("level",       "OUT",       2, 0.0f,     2.0f,    Lin, hat.level),
            }
        };
        return t;
    }

    //==========================================================================
    //  CYMBAL voice - crashes and rides
    //==========================================================================
    inline const Table& cymbalTable()
    {
        static const Table t
        {
            { "PARTIALS", "WASH", "PING", "STICK" },
            {
                EDM_SPEC ("baseFreq",    "FREQ",      0, 80.0f,    2000.0f, Log, cymbal.baseFreq),
                EDM_SPEC ("ratio1",      "R1",        0, 0.5f,     12.0f,   Log, cymbal.ratio[0]),
                EDM_SPEC ("ratio2",      "R2",        0, 0.5f,     12.0f,   Log, cymbal.ratio[1]),
                EDM_SPEC ("ratio3",      "R3",        0, 0.5f,     12.0f,   Log, cymbal.ratio[2]),
                EDM_SPEC ("ratio4",      "R4",        0, 0.5f,     12.0f,   Log, cymbal.ratio[3]),
                EDM_SPEC ("ratio5",      "R5",        0, 0.5f,     12.0f,   Log, cymbal.ratio[4]),
                EDM_SPEC ("ratio6",      "R6",        0, 0.5f,     12.0f,   Log, cymbal.ratio[5]),
                EDM_SPEC ("ratio7",      "R7",        0, 0.5f,     12.0f,   Log, cymbal.ratio[6]),
                EDM_SPEC ("ratio8",      "R8",        0, 0.5f,     12.0f,   Log, cymbal.ratio[7]),
                EDM_SPEC ("metalLevel",  "LEVEL",     0, 0.0f,     1.5f,    Lin, cymbal.metalLevel),

                EDM_SPEC ("noiseLevel",  "LEVEL",     1, 0.0f,     1.5f,    Lin, cymbal.noiseLevel),
                EDM_SPEC ("bpFreq",      "BP FREQ",   1, 1000.0f,  16000.0f, Log, cymbal.bpFreq),
                EDM_SPEC ("bpQ",         "BP Q",      1, 0.2f,     6.0f,    Log, cymbal.bpQ),
                EDM_SPEC ("hpFreq",      "HP FREQ",   1, 500.0f,   14000.0f, Log, cymbal.hpFreq),
                EDM_SPEC ("washTau",     "DECAY",     1, 0.05f,    8.0f,    Log, cymbal.washTau),
                EDM_SPEC ("attack",      "SWELL",     1, 0.0002f,  0.2f,    Log, cymbal.attack),

                EDM_SPEC ("pingLevel",   "LEVEL",     2, 0.0f,     2.0f,    Lin, cymbal.pingLevel),
                EDM_SPEC ("pingFreq1",   "P1",        2, 200.0f,   16000.0f, Log, cymbal.pingFreq[0]),
                EDM_SPEC ("pingFreq2",   "P2",        2, 200.0f,   16000.0f, Log, cymbal.pingFreq[1]),
                EDM_SPEC ("pingFreq3",   "P3",        2, 200.0f,   16000.0f, Log, cymbal.pingFreq[2]),
                EDM_SPEC ("pingTau",     "DECAY",     2, 0.02f,    4.0f,    Log, cymbal.pingTau),
                EDM_SPEC ("pingDetune",  "DETUNE",    2, 0.0f,     0.03f,   Lin, cymbal.pingDetune),
                EDM_SPEC ("pingSpread",  "SPREAD",    2, 0.0f,     1.0f,    Lin, cymbal.pingSpread),

                EDM_SPEC ("clickLevel",  "LEVEL",     3, 0.0f,     1.5f,    Lin, cymbal.clickLevel),
                EDM_SPEC ("clickFreq",   "FREQ",      3, 500.0f,   14000.0f, Log, cymbal.clickFreq),
                EDM_SPEC ("clickTau",    "TIME",      3, 0.0003f,  0.03f,   Log, cymbal.clickTau),
                EDM_SPEC ("drive",       "DRIVE",     3, 0.0f,     1.0f,    Lin, cymbal.drive),
                EDM_SPEC ("level",       "OUT",       3, 0.0f,     1.5f,    Lin, cymbal.level),
            }
        };
        return t;
    }

    //==========================================================================
    //  METAL voice - cowbells, agogos, triangles, jingle bell, bell tree
    //==========================================================================
    inline const Table& metalTable()
    {
        static const Table t
        {
            { "PARTIALS", "BODY", "STRIKES", "STICK" },
            {
                EDM_SPEC ("f0",          "FREQ",      0, 100.0f,   8000.0f, Log, metal.f0),
                EDM_SPEC ("ratio1",      "R1",        0, 0.5f,     12.0f,   Log, metal.ratio[0]),
                EDM_SPEC ("ratio2",      "R2",        0, 0.5f,     12.0f,   Log, metal.ratio[1]),
                EDM_SPEC ("ratio3",      "R3",        0, 0.5f,     12.0f,   Log, metal.ratio[2]),
                EDM_SPEC ("ratio4",      "R4",        0, 0.5f,     12.0f,   Log, metal.ratio[3]),
                EDM_SPEC ("gain1",       "L1",        0, 0.0f,     1.5f,    Lin, metal.gain[0]),
                EDM_SPEC ("gain2",       "L2",        0, 0.0f,     1.5f,    Lin, metal.gain[1]),
                EDM_SPEC ("gain3",       "L3",        0, 0.0f,     1.5f,    Lin, metal.gain[2]),
                EDM_SPEC ("gain4",       "L4",        0, 0.0f,     1.5f,    Lin, metal.gain[3]),

                EDM_SPEC ("square",      "SQUARE",    1, 0.0f,     1.0f,    Lin, metal.square),
                EDM_SPEC ("bpFreq",      "BP FREQ",   1, 200.0f,   14000.0f, Log, metal.bpFreq),
                EDM_SPEC ("bpQ",         "BP Q",      1, 0.2f,     10.0f,   Log, metal.bpQ),
                EDM_SPEC ("bpMix",       "BP MIX",    1, 0.0f,     1.0f,    Lin, metal.bpMix),
                EDM_SPEC ("fastTau",     "FAST",      1, 0.002f,   0.2f,    Log, metal.fastTau),
                EDM_SPEC ("slowTau",     "RING",      1, 0.02f,    6.0f,    Log, metal.slowTau),
                EDM_SPEC ("fastAmount",  "FAST AMT",  1, 0.0f,     1.0f,    Lin, metal.fastAmount),

                EDM_SPEC ("strikes",     "COUNT",     2, 1.0f,     24.0f,   Lin, metal.strikes),
                EDM_SPEC ("strikeSpacing","SPACING",  2, 0.0f,     0.2f,    Lin, metal.strikeSpacing),
                EDM_SPEC ("strikeStep",  "STEP",      2, -12.0f,   12.0f,   Lin, metal.strikeStep),
                EDM_SPEC ("strikeDecay", "FADE",      2, 0.1f,     1.2f,    Lin, metal.strikeDecay),

                EDM_SPEC ("clickLevel",  "CLICK",     3, 0.0f,     1.5f,    Lin, metal.clickLevel),
                EDM_SPEC ("clickFreq",   "CLK FREQ",  3, 500.0f,   14000.0f, Log, metal.clickFreq),
                EDM_SPEC ("noiseLevel",  "NOISE",     3, 0.0f,     1.5f,    Lin, metal.noiseLevel),
                EDM_SPEC ("noiseFreq",   "NSE FREQ",  3, 500.0f,   16000.0f, Log, metal.noiseFreq),
                EDM_SPEC ("drive",       "DRIVE",     3, 0.0f,     1.0f,    Lin, metal.drive),
                EDM_SPEC ("level",       "OUT",       3, 0.0f,     1.5f,    Lin, metal.level),
            }
        };
        return t;
    }

    //==========================================================================
    //  TONE voice - whistles, cuicas, scratches, zaps, blips, noise FX
    //==========================================================================
    inline const Table& toneTable()
    {
        static const Table t
        {
            { "OSC", "PITCH", "AMP", "NOISE" },
            {
                EDM_SPEC ("f0",          "FREQ",      0, 20.0f,    8000.0f, Log, tone.f0),
                EDM_SPEC ("toneLevel",   "LEVEL",     0, 0.0f,     1.5f,    Lin, tone.toneLevel),
                EDM_SPEC ("saw",         "SAW",       0, 0.0f,     1.0f,    Lin, tone.saw),
                EDM_SPEC ("lpFreq",      "LOWPASS",   0, 200.0f,   20000.0f, Log, tone.lpFreq),
                EDM_SPEC ("drive",       "DRIVE",     0, 0.0f,     1.0f,    Lin, tone.drive),
                EDM_SPEC ("level",       "OUT",       0, 0.0f,     1.5f,    Lin, tone.level),

                EDM_SPEC ("pitchStartSemis", "START", 1, -48.0f,   48.0f,   Lin, tone.pitchStartSemis),
                EDM_SPEC ("pitchTau",    "GLIDE",     1, 0.001f,   1.0f,    Log, tone.pitchTau),
                EDM_SPEC ("pitchFallSemis",  "FALL",  1, -24.0f,   24.0f,   Lin, tone.pitchFallSemis),
                EDM_SPEC ("fallTau",     "FALL TIME", 1, 0.005f,   1.0f,    Log, tone.fallTau),
                EDM_SPEC ("vibRate",     "VIB RATE",  1, 0.0f,     40.0f,   Lin, tone.vibRate),
                EDM_SPEC ("vibSemis",    "VIB DEPTH", 1, 0.0f,     12.0f,   Lin, tone.vibSemis),

                EDM_SPEC ("attack",      "ATTACK",    2, 0.0001f,  0.5f,    Log, tone.attack),
                EDM_SPEC ("hold",        "HOLD",      2, 0.0f,     1.0f,    Lin, tone.hold),
                EDM_SPEC ("decayTau",    "DECAY",     2, 0.002f,   2.0f,    Log, tone.decayTau),
                EDM_SPEC ("tremRate",    "TRM RATE",  2, 0.0f,     40.0f,   Lin, tone.tremRate),
                EDM_SPEC ("tremDepth",   "TRM DEPTH", 2, 0.0f,     1.0f,    Lin, tone.tremDepth),

                EDM_SPEC ("noiseLevel",  "LEVEL",     3, 0.0f,     1.5f,    Lin, tone.noiseLevel),
                EDM_SPEC ("noiseFreq",   "FREQ",      3, 100.0f,   14000.0f, Log, tone.noiseFreq),
                EDM_SPEC ("noiseQ",      "Q",         3, 0.2f,     10.0f,   Log, tone.noiseQ),
                EDM_SPEC ("noiseTrack",  "TRACK",     3, 0.0f,     1.0f,    Lin, tone.noiseTrack),
            }
        };
        return t;
    }

   #undef EDM_SPEC

    //==========================================================================
    inline const Table* tableFor (VoiceType t) noexcept
    {
        switch (t)
        {
            case VoiceType::Kick:   return &kickTable();
            case VoiceType::Snare:  return &snareTable();
            case VoiceType::Clap:   return &clapTable();
            case VoiceType::Hat:    return &hatTable();
            case VoiceType::Cymbal: return &cymbalTable();
            case VoiceType::Metal:  return &metalTable();
            case VoiceType::Tone:   return &toneTable();
            case VoiceType::None:   break;
        }
        return nullptr;
    }

    // The controls of THIS sound: a kick's table depends on its model.
    inline int kickModel (const Sound& s) noexcept
    {
        return s.type == VoiceType::Kick ? (int) std::lround (std::min (4.0f, std::max (0.0f, s.kick.model))) : 0;
    }

    inline const Table* tableFor (const Sound& s) noexcept
    {
        if (s.type == VoiceType::Kick)
            switch (kickModel (s))
            {
                case 1:  return &kickStruckTable();
                case 2:  return &kick808Table();
                case 3:  return &kick909Table();
                case 4:  return &kickSynthTable();
                default: return &kickTable();
            }
        if (s.type == VoiceType::Snare)
            switch ((int) std::lround (std::min (3.0f, std::max (0.0f, s.snare.model))))
            {
                case 1:  return &snareStruckTable();
                case 2:  return &snare808Table();
                case 3:  return &snare909Table();
                default: return &snareTable();
            }
        return tableFor (s.type);
    }

    inline const Spec* findSpec (const Table& t, const std::string& id) noexcept
    {
        for (const auto& s : t.specs)
            if (id == s.id)
                return &s;
        return nullptr;
    }

    inline float clampTo (const Spec& s, float v) noexcept
    {
        return std::min (s.hi, std::max (s.lo, v));
    }

    // real value <-> 0..200 on screen
    inline float toUi (const Spec& s, float v) noexcept
    {
        v = clampTo (s, v);
        if (s.map == Map::Log)
            return 200.0f * (float) (std::log ((double) v / s.lo) / std::log ((double) s.hi / s.lo));
        return 200.0f * (v - s.lo) / (s.hi - s.lo);
    }

    inline float fromUi (const Spec& s, float u) noexcept
    {
        const double t = std::min (200.0f, std::max (0.0f, u)) / 200.0;
        if (s.map == Map::Log)
            return (float) (s.lo * std::pow ((double) s.hi / s.lo, t));
        return (float) (s.lo + (s.hi - s.lo) * t);
    }

    // The library's unedited prototype of a cell's sound (same name and
    // articulation), or nullptr for a silent cell / a name no longer shipped.
    inline const Sound* librarySound (const Sound& s)
    {
        if (s.isEmpty()) return nullptr;
        const int idx = findSound (s.name(), s.articulation);
        return idx >= 0 ? &library()[(size_t) idx].sound : nullptr;
    }

    inline long long toStore (float v) noexcept     { return std::llround ((double) v * 1.0e6); }
    inline float     fromStore (long long v) noexcept { return (float) ((double) v / 1.0e6); }

    //==========================================================================
    //  "id=value,id=value" for every field where `cur` differs from `ref`
    //  (both the same voice type).  "" when nothing differs.
    //==========================================================================
    inline std::string shapeDiff (const Sound& ref, const Sound& cur)
    {
        const Table* t = tableFor (cur);
        if (t == nullptr || ref.type != cur.type || tableFor (ref) != t)
            return {};
        Sound r = ref, c = cur;
        std::string out;
        for (const auto& sp : t->specs)
        {
            const float a = *sp.field (r), b = *sp.field (c);
            if (a == b)
                continue;
            if (! out.empty()) out += ",";
            out += sp.id;
            out += "=";
            out += std::to_string (toStore (b));
        }
        return out;
    }

    // Apply "id=value,..." to s.  Unknown ids are skipped; values are clamped.
    inline void applyShape (Sound& s, const std::string& text)
    {
        const Table* t = tableFor (s);
        if (t == nullptr)
            return;
        size_t a = 0;
        while (a < text.size())
        {
            size_t b = text.find (',', a);
            if (b == std::string::npos) b = text.size();
            const std::string item = text.substr (a, b - a);
            a = b + 1;
            const size_t eq = item.find ('=');
            if (eq == std::string::npos)
                continue;
            if (const Spec* sp = findSpec (*t, item.substr (0, eq)))
                *sp->field (s) = clampTo (*sp, fromStore (std::atoll (item.c_str() + eq + 1)));
        }
    }

    //==========================================================================
    //  THE "i" TEXT OF EACH TAB - what that group of sliders shapes.
    //
    //  Lines: plain text describes the section; "LABEL|text" explains one
    //  slider.  uiTab 0 = MAIN (the macros), 1.. = the voice's own tabs.
    //==========================================================================
    inline const char* voiceKindName (VoiceType t) noexcept
    {
        switch (t)
        {
            case VoiceType::Kick:   return "Kick, tom, hand drum";
            case VoiceType::Snare:  return "Snare, side stick, wood";
            case VoiceType::Clap:   return "Clap, scrape, castanets";
            case VoiceType::Hat:    return "Hi-hat, shaker";
            case VoiceType::Cymbal: return "Crash, ride";
            case VoiceType::Metal:  return "Cowbell, agogo, triangle, bells";
            case VoiceType::Tone:   return "Whistle, cuica, FX";
            case VoiceType::None:   break;
        }
        return "Silent key";
    }

    inline std::string mainInfo (VoiceType t)
    {
        const char* damp  = "DAMP|Tightens (up) or loosens (down) the sound. What it acts on depends on the sound.";
        const char* snap  = "SNAP|Strength of the attack at the start of the hit.";
        const char* color = "COLOR|Brighter (up) or darker (down).";
        switch (t)
        {
            case VoiceType::Kick:
                damp  = "DAMP|Up = the upper tones die faster: a tighter, more muffled drum. Down = they ring longer.";
                snap  = "SNAP|Level of the beater or stick click at the hit.";
                color = "COLOR|Up = louder upper tones and brighter noise. Down = a darker, rounder drum.";
                break;
            case VoiceType::Snare:
                damp  = "DAMP|Up = a shorter drum body: tighter, more muffled. Down = the body rings longer.";
                snap  = "SNAP|Level of the stick click and the rim crack.";
                color = "COLOR|Up = brighter wires and stick. Down = darker.";
                break;
            case VoiceType::Clap:
                damp  = "DAMP|Up = less room tail after the hands. Down = more room.";
                snap  = "SNAP|Up = sharper, drier hand hits. Down = softer, smeared hits.";
                color = "COLOR|Up = a brighter clap with more sizzle. Down = darker.";
                break;
            case VoiceType::Hat:
                damp  = "DAMP|Up = more noise: washier, softer. Down = more metal: ringier, more tonal.";
                snap  = "SNAP|Level of the stick tip on the hat.";
                color = "COLOR|Up = brighter. Down = darker.";
                break;
            case VoiceType::Cymbal:
                damp  = "DAMP|Up = drier, less wash. Down = washier.";
                snap  = "SNAP|Level of the stick hit.";
                color = "COLOR|Up = a brighter wash and stick. Down = darker.";
                break;
            case VoiceType::Metal:
                damp  = "DAMP|Up = more of the strike dies at once, like a hand on the bell. Down = it rings on.";
                snap  = "SNAP|Level of the stick click.";
                color = "COLOR|Up = a brighter body and shimmer. Down = darker.";
                break;
            case VoiceType::Tone:
                damp  = "DAMP|Up = darker (closes the output low-pass). Down = brighter.";
                snap  = "SNAP|Up = a faster attack and a bigger pitch sweep at the start. Down = a softer onset.";
                color = "COLOR|Up = buzzier (more saw) with brighter noise. Down = purer.";
                break;
            case VoiceType::None:
                break;
        }
        std::string s = "The quick controls: eight macros that reshape the whole sound at once, plus its level. "
                        "At 100 a bipolar slider leaves the sound exactly as designed.\n";
        s += "TUNE|Pitch of the whole sound, +/-25 semitones. 100 = as designed.\n";
        s += "DECAY|Length of the sound: from a quarter to four times its designed length.\n";
        s += std::string (damp) + "\n" + snap + "\n" + color + "\n";
        s += "DRIVE|Saturation added on top of the sound's own: thicker, louder, grittier.\n";
        s += "HUMAN|Random variation on every hit (pitch, length, attack), so repeats never sound identical.\n";
        s += "VEL SENS|How much velocity changes the sound. Low = every hit alike; high = soft hits are softer, darker and gentler.\n";
        s += "GAIN|Output level of this pad, +/-20 dB. 100 = 0 dB.";
        return s;
    }

    inline std::string tabInfo (VoiceType t, int uiTab)
    {
        if (uiTab == 0)
            return mainInfo (t);

        static const char* kick[] =
        {
            "The drum itself: a struck membrane made of up to eight resonant tones (modes).\n"
            "FREQ|Fundamental pitch of the drum.\n"
            "LEVEL|Level of the drum body.\n"
            "DRIVE|The sound's own saturation (MAIN DRIVE adds to it).\n"
            "ASYM|Uneven saturation: adds even harmonics for a warmer, fatter distortion.\n"
            "ONSET|Low = the tones swell in softly. High = struck, starting at full strength.\n"
            "SCATTER|Random detune of the upper tones on every hit, like a real skin.\n"
            "FAST AMT|Share of each tone lost in a quick first drop right after the hit.\n"
            "FAST RATE|Speed of that first drop: lower = quicker.",

            "The pitch sweep at the start of the hit: the kick's thump, the tom's boing.\n"
            "AMOUNT|How far above its final pitch the drum starts.\n"
            "TIME|How fast the pitch falls to its final note.\n"
            "SPIKE|A very short extra pitch jump at the instant of the hit: the beater or stick impact.\n"
            "SPK TIME|Length of that spike.",

            "The beater or stick noise at the very start of the hit.\n"
            "LEVEL|Loudness of the click.\n"
            "FREQ|Its pitch region: low = a thud, high = a tick.\n"
            "TIME|Its length.\n"
            "Q|Width of its band: low = wide and full, high = narrow and pitched.",

            "A noise layer under the body: air, skin rattle, room.\n"
            "LEVEL|Loudness of the noise.\n"
            "FREQ|Filter frequency of the noise.\n"
            "LP BP HP|Filter type: low-pass (dark) through band-pass to high-pass (thin).\n"
            "TIME|How long the noise lasts.",

            "The pitch of each of the eight resonant tones, as a multiple of FREQ. Their spacing is the drum's "
            "character: near whole numbers sound tuneful, scattered ratios sound boomy or metallic.\n"
            "R1 to R8|Pitch ratio of tone 1 to 8 (1.0 = FREQ itself).",

            "The loudness of each resonant tone: this mixes the drum's tone colour.\n"
            "L1 to L8|Level of tone 1 to 8 (bottom = off).",

            "The ring time of each resonant tone. Long low tones with short high tones sound natural.\n"
            "D1 to D8|How long tone 1 to 8 rings.",
        };
        static const char* snare[] =
        {
            "The drum shell and head: the tone under the wires.\n"
            "FREQ|Pitch of the drum body.\n"
            "LEVEL|Level of the body.\n"
            "DRIVE|The sound's own saturation (MAIN DRIVE adds to it).\n"
            "ONSET|Low = the body swells in. High = struck at full strength.\n"
            "SCATTER|Random detune of the upper body tones on every hit.\n"
            "PITCH AMT|How far above its final pitch the body starts.\n"
            "PITCH TM|How fast that pitch drop happens.",

            "The body's three resonant tones: pitch (R), level (L) and ring time (D) of each.\n"
            "R1 to R3|Pitch of each tone as a multiple of FREQ.\n"
            "L1 to L3|Level of each tone.\n"
            "D1 to D3|How long each tone rings.",

            "The snare wires: the rattling noise that makes a snare a snare.\n"
            "LEVEL|Loudness of the wires.\n"
            "FREQ|Centre of the wire noise: low = a dark rattle, high = a crisp sizzle.\n"
            "Q|Width of the wire band: low = wide, high = narrow.\n"
            "HP MIX|Low = band-passed wires. High = high-passed: thinner, brighter.\n"
            "DECAY|How long the wires rattle.\n"
            "ATTACK|How fast the wires come in.\n"
            "BUZZ|How much the wires rattle with the drum head: buzz rather than plain hiss.\n"
            "FAST|Share of the wire noise lost in a quick first drop.",

            "The stick hitting the head.\n"
            "LEVEL|Loudness of the stick click.\n"
            "FREQ|Its pitch region: low = a knock, high = a tick.\n"
            "TIME|Its length.\n"
            "Q|Width of its band: low = wide and full, high = narrow.",

            "The rim crack: the stick on the metal hoop.\n"
            "LEVEL|Loudness of the rim.\n"
            "FREQ|Pitch of the rim ring.\n"
            "TIME|How long it rings.",
        };
        static const char* clap[] =
        {
            "A clap is a quick burst of noise hits (the hands), then a room tail.\n"
            "COUNT|Number of hand hits in the clap.\n"
            "SPACING|Time between the hits.\n"
            "SPREAD|Random variation of timing and level between hits: a tight pair of hands or a loose crowd.\n"
            "BURST|Length of each hit.\n"
            "FADE|Level of each hit relative to the one before: lower = each hit softer.",

            "The colour of the clap noise.\n"
            "FREQ|Centre of the clap's body band.\n"
            "Q|Width of that band: low = wide and full, high = narrow and honky.\n"
            "SIZZLE|High-passed noise added on top, for air.\n"
            "SIZ FREQ|Where the sizzle starts.",

            "The room after the hands, and the output.\n"
            "TAIL|Level of the room tail.\n"
            "TAIL TIME|Length of the tail.\n"
            "DRIVE|The sound's own saturation (MAIN DRIVE adds to it).\n"
            "LEVEL|Output level of the clap.",
        };
        static const char* hat[] =
        {
            "Six square-wave oscillators at inharmonic pitches: the metallic body of the hat, the classic "
            "808/909 method. Shakers use the same voice.\n"
            "FREQ|Pitch of oscillator 1. The others follow as ratios.\n"
            "R1 to R6|Pitch of each oscillator as a multiple of FREQ.\n"
            "LEVEL|Level of the metallic layer.",

            "A filtered noise layer: the hiss and sizzle.\n"
            "LEVEL|Level of the noise.\n"
            "BP FREQ|Centre of the band that shapes the hat's tone.\n"
            "BP Q|Width of that band.\n"
            "HP FREQ|Low cut: higher = thinner and brighter.",

            "How the hat speaks and dies away, per articulation. Shakers use the closed values.\n"
            "CLOSED|Decay of the closed hat.\n"
            "PEDAL|Decay of the pedal hat.\n"
            "OPEN|Decay of the open hat.\n"
            "ATTACK|How fast the hat reaches full level.\n"
            "CLICK|Level of the stick tip.\n"
            "DRIVE|The sound's own saturation (MAIN DRIVE adds to it).\n"
            "OUT|Output level.",
        };
        static const char* cymbal[] =
        {
            "Eight inharmonic partials: the metallic tone of the cymbal.\n"
            "FREQ|Pitch of partial 1. The others follow as ratios.\n"
            "R1 to R8|Pitch of each partial as a multiple of FREQ.\n"
            "LEVEL|Level of the metallic layer.",

            "The noise wash: the long shimmering body of a crash or ride.\n"
            "LEVEL|Level of the wash.\n"
            "BP FREQ|Centre of the band that shapes the wash.\n"
            "BP Q|Width of that band.\n"
            "HP FREQ|Low cut: higher = thinner and brighter.\n"
            "DECAY|How long the wash lasts.\n"
            "SWELL|How slowly it swells in: bottom = instant.",

            "The bell tones of a ride: three pitched pings, each a slightly detuned pair that beats like real "
            "metal. Crashes have none.\n"
            "LEVEL|Level of the pings (bottom = none).\n"
            "P1 to P3|Pitch of each ping.\n"
            "DECAY|How long the pings ring.\n"
            "DETUNE|How far each pair is detuned: more = faster beating.\n"
            "SPREAD|How much faster the higher pings fade.",

            "The stick hitting the cymbal, and the output.\n"
            "LEVEL|Loudness of the stick hit.\n"
            "FREQ|Its pitch region.\n"
            "TIME|Its length.\n"
            "DRIVE|The sound's own saturation (MAIN DRIVE adds to it).\n"
            "OUT|Output level.",
        };
        static const char* metal[] =
        {
            "Up to four partials: the pitched tone of the bell, cowbell or triangle.\n"
            "FREQ|Pitch of partial 1.\n"
            "R1 to R4|Pitch of each partial as a multiple of FREQ.\n"
            "L1 to L4|Level of each partial (bottom = off).",

            "The resonating body, and how the tone dies away.\n"
            "SQUARE|Bottom = pure sine tones (bells, triangles). Top = square waves (808 cowbell).\n"
            "BP FREQ|Centre of the body resonance.\n"
            "BP Q|Width of the body resonance.\n"
            "BP MIX|How much of the tone passes through the body (bottom = raw).\n"
            "FAST|Length of the quick drop right after the strike.\n"
            "RING|Length of the ring after that.\n"
            "FAST AMT|Share of the tone lost in the quick drop: more = more choked.",

            "Repeated strikes, for bell trees and rolls.\n"
            "COUNT|Number of strikes per hit (bottom = a single hit).\n"
            "SPACING|Time between strikes.\n"
            "STEP|Pitch change per strike in semitones (below the middle = descending, like a bell tree).\n"
            "FADE|Level of each strike relative to the one before.",

            "The stick and the shimmer, and the output.\n"
            "CLICK|Level of the stick click.\n"
            "CLK FREQ|Pitch region of the click.\n"
            "NOISE|Level of the shimmer noise.\n"
            "NSE FREQ|Pitch region of the shimmer.\n"
            "DRIVE|The sound's own saturation (MAIN DRIVE adds to it).\n"
            "OUT|Output level.",
        };
        static const char* tone[] =
        {
            "The oscillator: the pitched part of whistles, cuicas, zaps and blips.\n"
            "FREQ|The pitch the sound lands on.\n"
            "LEVEL|Level of the oscillator (bottom = a pure noise effect).\n"
            "SAW|Bottom = a pure sine. Top = a buzzy saw.\n"
            "LOWPASS|Output filter: lower = darker.\n"
            "DRIVE|The sound's own saturation (MAIN DRIVE adds to it).\n"
            "OUT|Output level.",

            "Pitch movement: the sweep of a zap, the slide of a whistle, the wobble of a cuica.\n"
            "START|Pitch at the hit, in semitones from FREQ. It glides to FREQ.\n"
            "GLIDE|How fast it glides there.\n"
            "FALL|A slower late pitch drift (below the middle = falls).\n"
            "FALL TIME|How slowly that drift happens.\n"
            "VIB RATE|Speed of the pitch vibrato.\n"
            "VIB DEPTH|Depth of the vibrato, in semitones.",

            "The volume shape.\n"
            "ATTACK|Fade-in time.\n"
            "HOLD|Time held at full level.\n"
            "DECAY|Fade-out time.\n"
            "TRM RATE|Speed of a volume trill: a whistle's roll.\n"
            "TRM DEPTH|Depth of that trill.",

            "A filtered noise layer: breath, grit, scratch.\n"
            "LEVEL|Level of the noise.\n"
            "FREQ|Centre of the noise band.\n"
            "Q|Width of the band: low = wide, high = narrow.\n"
            "TRACK|How much the noise band follows the pitch movement.",
        };

        auto pick = [uiTab] (const char* const* arr, int n) -> std::string
        {
            return (uiTab >= 1 && uiTab <= n) ? std::string (arr[uiTab - 1]) : std::string();
        };
        switch (t)
        {
            case VoiceType::Kick:   return pick (kick,   (int) (sizeof (kick)   / sizeof (kick[0])));
            case VoiceType::Snare:  return pick (snare,  (int) (sizeof (snare)  / sizeof (snare[0])));
            case VoiceType::Clap:   return pick (clap,   (int) (sizeof (clap)   / sizeof (clap[0])));
            case VoiceType::Hat:    return pick (hat,    (int) (sizeof (hat)    / sizeof (hat[0])));
            case VoiceType::Cymbal: return pick (cymbal, (int) (sizeof (cymbal) / sizeof (cymbal[0])));
            case VoiceType::Metal:  return pick (metal,  (int) (sizeof (metal)  / sizeof (metal[0])));
            case VoiceType::Tone:   return pick (tone,   (int) (sizeof (tone)   / sizeof (tone[0])));
            case VoiceType::None:   break;
        }
        return {};
    }

    //==========================================================================
    //  THE KICK MODELS' "i" TEXTS, and the sound-aware entry points the
    //  editor uses (a kick's controls depend on its model).
    //==========================================================================
    inline std::string kickModelInfo (int model, int uiTab)
    {
        if (uiTab == 0)
        {
            const char* damp  = "DAMP|Up = a shorter ring. Down = longer.";
            const char* snap  = "SNAP|Strength of the attack.";
            const char* color = "COLOR|Up = brighter. Down = darker.";
            if (model == 1)
            {
                damp  = "DAMP|Up = the upper tones die faster: tighter, more muffled. Down = they ring longer.";
                snap  = "SNAP|Level of the click: the body click and the beater.";
                color = "COLOR|Up = louder upper tones and brighter noise. Down = a darker, rounder drum.";
            }
            else if (model == 2)
            {
                snap  = "SNAP|The punch: how far the pitch jumps at the attack (ATTACK FM).";
                color = "COLOR|Up = brighter: more of the trigger click, a more open tone.";
            }
            else if (model == 3)
            {
                snap  = "SNAP|Level of the click at the attack.";
                color = "COLOR|Up = brighter, with more click. Down = darker.";
            }
            std::string s = "The quick controls: eight macros that reshape the whole sound at once, plus its level. "
                            "At 100 a bipolar slider leaves the sound exactly as designed.\n";
            s += "TUNE|Pitch of the whole sound, +/-25 semitones. 100 = as designed.\n";
            s += "DECAY|Length of the sound: shorter below 100, longer above.\n";
            s += std::string (damp) + "\n" + snap + "\n" + color + "\n";
            s += "DRIVE|Saturation added on top of the sound's own: thicker, louder, grittier.\n";
            s += "HUMAN|Random variation on every hit (pitch, length, attack), so repeats never sound identical.\n";
            s += "VEL SENS|How much velocity changes the sound. Low = every hit alike; high = soft hits are softer and rounder.\n";
            s += "GAIN|Output level of this pad, +/-20 dB. 100 = 0 dB.";
            return s;
        }
        if (model == 4)
        {
            if (uiTab == 1)
                return "The pitch: a sine that dives from far above its end pitch - a fast dive, then a slow glide. "
                       "The long, deep dive is what makes a produced kick sound big.\n"
                       "END FREQ|The pitch the kick lands on: lower = deeper.\n"
                       "SWEEP|How many octaves above END FREQ the dive starts.\n"
                       "FAST|Time of the fast first dive: shorter = a harder punch.\n"
                       "SLOW|Time of the slow glide into the end pitch.\n"
                       "SLOW AMT|How much of the dive is left for the slow glide.";
            if (uiTab == 2)
                return "The level: the body holds, falls, then fades on a quiet tail.\n"
                       "HOLD|How long the body stays at full level - the produced, compressed thump.\n"
                       "DECAY|How fast the body falls after the hold.\n"
                       "TAIL|How much level stays on the tail after the fall.\n"
                       "TAIL TIME|How long the tail fades.\n"
                       "PHASE|The attack: low = the sine starts from silence (soft), high = from its peak (a hard click).\n"
                       "LENGTH|Cuts the kick off at this time with a quick release, like a gate. 0 = off: it fades on its own.\n"
                       "LEVEL|Output level.";
            if (uiTab == 3)
                return "The click: a short burst of filtered noise on top of the dive.\n"
                       "CLICK|Loudness of the click.\n"
                       "CLK FREQ|Pitch region of the click.\n"
                       "CLK TIME|Length of the click.\n"
                       "CLK Q|Width of the click band: low = wide, high = narrow.";
            if (uiTab == 4)
                return "Saturation: the harmonics that make the sub audible on small speakers.\n"
                       "DRIVE|How hard the kick is driven (MAIN DRIVE adds to it).\n"
                       "ASYM|Uneven saturation: even harmonics for a warmer, fatter drive.";
        }
        if (model == 2 && uiTab == 1)
            return "The TR-808 bass drum circuit: a trigger pulse rings a resonant filter. The pitch jumps at the "
                   "attack and follows the drum's own level, so it glides down as the drum fades.\n"
                   "FREQ|The pitch the drum settles on.\n"
                   "DECAY|How long the circuit rings.\n"
                   "TONE|Brightness: how much of the trigger click leaks through, and how open the output filter is.\n"
                   "ATTACK FM|The pitch jump in the first 6 ms: the punch at the front.\n"
                   "SELF FM|How much the pitch follows the level: louder sits higher, then glides down as it fades.\n"
                   "LEVEL|Output level.\n"
                   "DRIVE|Saturation after the circuit (MAIN DRIVE adds to it).\n"
                   "ASYM|Uneven saturation: adds even harmonics for a warmer distortion.";
        if (model == 3 && uiTab == 1)
            return "The TR-909 bass drum circuit: a triangle oscillator shaped into a sine, a fast pitch sweep at "
                   "the attack, a transistor VCA that saturates while loud, and a filtered click.\n"
                   "FREQ|The pitch the sweep lands on.\n"
                   "DECAY|Length of the body.\n"
                   "TONE|Brightness and the level of the click.\n"
                   "DIRT|A rougher oscillator shape and a little analog drift.\n"
                   "SWEEP|How far above FREQ the pitch starts.\n"
                   "SWEEP TM|How long the pitch takes to drop.\n"
                   "LEVEL|Output level.\n"
                   "DRIVE|Saturation after the circuit (MAIN DRIVE adds to it).\n"
                   "ASYM|Uneven saturation: adds even harmonics for a warmer distortion.";
        if (model == 1)
        {
            if (uiTab == 1)
                return "A drum head rung by a strike: eight resonant tones whose pitch rises with how hard the head "
                       "vibrates (tension modulation), as on a real drum.\n"
                       "FREQ|Fundamental pitch of the drum.\n"
                       "LEVEL|Output level.\n"
                       "TENSION|How much the pitch rises with the head's energy: a hard hit starts higher and glides down as it fades; a soft hit barely moves.\n"
                       "VCA|Level-dependent saturation: grit while loud, clean as it fades, plus a sub thump that follows the level.\n"
                       "DRIVE|Saturation after the VCA (MAIN DRIVE adds to it).\n"
                       "ASYM|Uneven saturation: even harmonics, a warmer distortion.\n"
                       "SCATTER|Random detune of the upper tones on every hit, like a real skin.\n"
                       "FAST AMT|Share of the level lost in a quick first drop, as a real head does before it rings on.\n"
                       "FAST TIME|How fast that first drop happens.";
            if (uiTab == 2)
                return "The beater hitting the head.\n"
                       "HARDNESS|Contact time: soft = a longer, darker push; hard = a short, bright hit.\n"
                       "SWEEP|An extra electronic pitch drop at the start, on top of TENSION.\n"
                       "SWEEP TM|How fast that drop happens.\n"
                       "SPIKE|A very short pitch jump at the instant of the hit.\n"
                       "SPK TIME|Length of that spike.";
            if (uiTab == 3)
                return "The attack.\n"
                       "BODY CLK|A click made from the drum's own motion, so it stays locked to the body.\n"
                       "BEATER|The beater's own noise at the hit.\n"
                       "FREQ|Pitch region of the beater noise; the body click follows it.\n"
                       "TIME|Length of the click.\n"
                       "Q|Width of the beater noise band: low = wide and full, high = narrow.";
            return tabInfo (VoiceType::Kick, uiTab);         // NOISE and the three MODE tabs
        }
        return {};
    }

    //==========================================================================
    //  THE SNARE MODELS' "i" TEXTS
    //==========================================================================
    inline int snareModel (const Sound& s) noexcept
    {
        return s.type == VoiceType::Snare ? (int) std::lround (std::min (3.0f, std::max (0.0f, s.snare.model))) : 0;
    }

    inline std::string snareModelInfo (int model, int uiTab)
    {
        if (uiTab == 0)
        {
            const char* damp  = "DAMP|Up = shorter. Down = longer.";
            const char* snap  = "SNAP|More snappy: the snare noise against the shell.";
            const char* color = "COLOR|Up = brighter. Down = darker.";
            if (model == 1)
            {
                damp  = "DAMP|Up = the head and the wires stop sooner: tighter. Down = they ring longer.";
                snap  = "SNAP|The stick: its click, the click from the head's motion, and the rim crack.";
                color = "COLOR|Up = brighter wires and stick. Down = darker.";
            }
            else if (model == 2)
                color = "COLOR|Up = a brighter shell with more modes. Down = the plain two-mode 808 shell.";
            else if (model == 3)
                color = "COLOR|Moves the snare noise band: up = brighter, down = darker.";
            std::string s = "The quick controls: eight macros that reshape the whole sound at once, plus its level. "
                            "At 100 a bipolar slider leaves the sound exactly as designed.\n";
            s += "TUNE|Pitch of the whole sound, +/-25 semitones. 100 = as designed.\n";
            s += "DECAY|Length of the sound: shorter below 100, longer above.\n";
            s += std::string (damp) + "\n" + snap + "\n" + color + "\n";
            s += "DRIVE|Saturation added on top of the sound's own: thicker, louder, grittier.\n";
            s += "HUMAN|Random variation on every hit (pitch, length, attack), so repeats never sound identical.\n";
            s += "VEL SENS|How much velocity changes the sound. Low = every hit alike; high = soft hits are softer and gentler.\n";
            s += "GAIN|Output level of this pad, +/-20 dB. 100 = 0 dB.";
            return s;
        }
        if (model == 2 && uiTab == 1)
            return "The TR-808 snare circuit: a trigger pulse rings the shell's resonances, soft-clipped together, "
                   "while rectified noise - band-passed high above the shell - plays the snares.\n"
                   "FREQ|Pitch of the shell.\n"
                   "TONE|The shell: low = the 808's two modes, high = up to five for a fuller shell.\n"
                   "DECAY|How long the shell and the snare noise last.\n"
                   "SNAPPY|The snare noise against the shell.\n"
                   "LEVEL|Output level.\n"
                   "DRIVE|Saturation after the circuit (MAIN DRIVE adds to it).";
        if (model == 3 && uiTab == 1)
            return "The TR-909 snare circuit: two shaped oscillators with a fast pitch drop and the 909's gritty "
                   "coupling, plus noise band-limited around the shell's pitch that holds before it fades.\n"
                   "FREQ|Pitch of the shell; the noise band follows it.\n"
                   "SWEEP|How far the pitch drops at the start.\n"
                   "DECAY|Length of the shell and the snare noise.\n"
                   "SNAPPY|The snare noise against the shell.\n"
                   "LEVEL|Output level.\n"
                   "DRIVE|Saturation after the circuit (MAIN DRIVE adds to it).";
        if (model == 1)
        {
            if (uiTab == 1)
                return "A stick hits a head: three resonant tones whose pitch rises with how hard the head vibrates.\n"
                       "FREQ|Pitch of the head.\n"
                       "LEVEL|Output level.\n"
                       "TENSION|How much the pitch rises with the head's energy: hard hits start higher and settle.\n"
                       "SCATTER|Random detune of the upper tones on every hit.\n"
                       "VCA|Level-dependent saturation: grit while loud, clean as it fades, plus a thump.\n"
                       "DRIVE|Saturation after the VCA (MAIN DRIVE adds to it).\n"
                       "PITCH AMT|An extra pitch drop at the start.\n"
                       "PITCH TM|How fast that drop happens.";
            if (uiTab == 2)
                return "The stick.\n"
                       "HARDNESS|Contact time: soft = a longer, darker hit; hard = a short, bright one.\n"
                       "BODY CLK|A click made from the head's own motion, locked to it.\n"
                       "CLICK|The stick's own noise at the hit.\n"
                       "CLK FREQ|Pitch region of the stick noise; the body click follows it.\n"
                       "CLK TIME|Length of the click.\n"
                       "CLK Q|Width of the stick noise band: low = wide, high = narrow.";
            if (uiTab == 3)
                return "The head's three resonant tones: pitch (R), level (L) and ring time (D) of each.\n"
                       "R1 to R3|Pitch of each tone as a multiple of FREQ.\n"
                       "L1 to L3|Level of each tone.\n"
                       "D1 to D3|How long each tone rings.";
            if (uiTab == 4)
                return "The snare wires: they hold, then decay, and buzz with the head's motion. Zero LEVEL for "
                       "cross sticks, claves and woodblocks.\n"
                       "LEVEL|Loudness of the wires.\n"
                       "FREQ|Centre of the wire noise: low = a dark rattle, high = a crisp sizzle.\n"
                       "Q|Width of the wire band: low = wide, high = narrow.\n"
                       "HP MIX|Low = band-passed wires. High = high-passed: thinner, brighter.\n"
                       "ATTACK|How fast the wires come in.\n"
                       "HOLD|How long the wires hold at full level before they decay.\n"
                       "DECAY|How long the wires rattle.\n"
                       "FAST|Share of the wire noise lost in a quick first drop.\n"
                       "BUZZ|How much the wires rattle with the head: buzz rather than plain hiss.\n"
                       "RECT|The 808's grain: rectified wire noise.";
            if (uiTab == 5)
                return "The rim: a ringing crack when the stick catches the hoop. For sticks and claves it is the "
                       "main tone.\n"
                       "LEVEL|Loudness of the rim.\n"
                       "FREQ|Pitch of the rim ring.\n"
                       "RING|How long it rings.";
        }
        return {};
    }

    inline const char* voiceKindName (const Sound& s) noexcept
    {
        switch (kickModel (s))
        {
            case 1:  return "Kick, tom, hand drum - struck head";
            case 2:  return "Kick - 808 circuit";
            case 3:  return "Kick - 909 circuit";
            case 4:  return "Kick - synth";
            default: break;
        }
        switch (snareModel (s))
        {
            case 1:  return "Snare, stick, wood - struck head";
            case 2:  return "Snare - 808 circuit";
            case 3:  return "Snare - 909 circuit";
            default: break;
        }
        return voiceKindName (s.type);
    }

    inline std::string tabInfo (const Sound& s, int uiTab)
    {
        if (const int m = kickModel (s))  return kickModelInfo (m, uiTab);
        if (const int m = snareModel (s)) return snareModelInfo (m, uiTab);
        return tabInfo (s.type, uiTab);
    }
} // namespace params
} // namespace edm
