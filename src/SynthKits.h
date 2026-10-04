#pragma once

#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <cstdint>
#include "SynthLibrary.h"
#include "SynthParams.h"        // every sound-shaping field: editor tabs + shape saving
#include "DrumSplitEq.h"        // the EQ's range: silence .. +24 dB

//==============================================================================
//  SynthKits.h  —  an EDM kit is 128 key cells.
//
//  A cell is a library Sound plus the macro settings it plays with, a gain
//  and a choke group.  An empty Sound = a silent key.
//
//  The six factory kits follow the per-kit key map measured from the 830
//  styles: every key the styles play gets a sound, the most-played keys the
//  most careful choice.  First drafts on the library: kicks, snares, claps,
//  rims, hats, toms, cymbals, GM percussion 54-84 and each kit's FX keys.
//==============================================================================

namespace edm
{

struct Cell
{
    Sound        sound;          // VoiceType::None = silent key
    drum::Macros macros;         // the eight macros this key plays with
    float        gainDb     = 0.0f;
    int          chokeGroup = 0; // 0 = none; same non-zero group chokes
    // The key's own two-handle filter - the same 20 * 1000^norm law as a
    // sampled kit's per-key band: 0 = low cut open, 1 = high cut open.
    float        filterHpNorm = 0.0f;
    float        filterLpNorm = 1.0f;
};

//==============================================================================
//  FAMILIES and their FX racks.  The pads are grouped by family - one colour
//  each in the editor - and every family runs its own chain with the same six
//  pages a sampled kit's rack has: EQ, SAT, COMP, SWEET, PAN and SENDS
//  (Channel::processEdmFamily).  A family left neutral costs nothing.
//==============================================================================
static constexpr int kNumFamilies = 15;

inline const char* familyName (int i)
{
    static const char* n[kNumFamilies] = { "Kick", "Tom", "Snare", "Side Stick", "Clap", "Hat", "Crash", "Ride",
                                           "Hand Drum", "Shaker", "Wood", "Scrape", "Metal", "Whistle", "FX" };
    return (i >= 0 && i < kNumFamilies) ? n[i] : "";
}

inline int familyIndex (const std::string& name)
{
    for (int i = 0; i < kNumFamilies; ++i)
        if (name == familyName (i)) return i;
    return -1;
}

struct FamilyFx
{
    bool  eqOn = false;
    float eqGainDb[10] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };   // 31 Hz .. 16 kHz, one band each, dB
    bool  satOn = false;
    float satDrive = 0.0f, satMix = 1.0f;
    bool  compOn = false;
    float compThreshDb = 0.0f, compRatio = 1.0f, compAttackMs = 5.0f, compReleaseMs = 50.0f, compMakeupDb = 0.0f;
    bool  sweetOn = false;
    float sweetMix = 1.0f, softenDepth = 0.0f, softenMs = 8.0f, peakCeilDb = 12.0f, peakRatio = 4.0f,
          tameDepthDb = 0.0f, tameFreqHz = 4000.0f, roundDrive = 0.0f, roundMix = 1.0f;
    float pan = 0.0f;                                     // -1 left .. +1 right
    float chorusSend = 0.0f, reverbSend = 0.0f, delaySend = 0.0f;

    // Nothing to run: every stage off, centred, no sends
    bool isNeutral() const noexcept
    {
        return ! eqOn && ! satOn && ! compOn && ! sweetOn && pan == 0.0f
            && chorusSend <= 0.0f && reverbSend <= 0.0f && delaySend <= 0.0f;
    }
};

// FamilyFx <-> "id=value,..." (values x1000, integers: locale-proof)
namespace fxdetail
{
    struct FloatField { const char* id; float FamilyFx::* p; };
    struct BoolField  { const char* id; bool  FamilyFx::* p; };
    inline const std::array<BoolField, 4>& bools()
    {
        static const std::array<BoolField, 4> b { { { "eq", &FamilyFx::eqOn }, { "sat", &FamilyFx::satOn },
                                                    { "comp", &FamilyFx::compOn }, { "sweet", &FamilyFx::sweetOn } } };
        return b;
    }
    inline const std::array<FloatField, 20>& floats()
    {
        static const std::array<FloatField, 20> f { {
            { "sat.drive", &FamilyFx::satDrive }, { "sat.mix", &FamilyFx::satMix },
            { "comp.thr", &FamilyFx::compThreshDb }, { "comp.ratio", &FamilyFx::compRatio },
            { "comp.att", &FamilyFx::compAttackMs }, { "comp.rel", &FamilyFx::compReleaseMs },
            { "comp.mk", &FamilyFx::compMakeupDb },
            { "sweet.mix", &FamilyFx::sweetMix }, { "sweet.soft", &FamilyFx::softenDepth },
            { "sweet.softms", &FamilyFx::softenMs }, { "sweet.peak", &FamilyFx::peakCeilDb },
            { "sweet.pratio", &FamilyFx::peakRatio }, { "sweet.tame", &FamilyFx::tameDepthDb },
            { "sweet.tamehz", &FamilyFx::tameFreqHz }, { "sweet.round", &FamilyFx::roundDrive },
            { "sweet.rmix", &FamilyFx::roundMix }, { "pan", &FamilyFx::pan },
            { "send.chorus", &FamilyFx::chorusSend }, { "send.reverb", &FamilyFx::reverbSend },
            { "send.delay", &FamilyFx::delaySend } } };
        return f;
    }
    inline std::string q (float v) { return std::to_string ((long long) std::llround ((double) v * 1000.0)); }

    inline std::string toString (const FamilyFx& fx)
    {
        std::string out;
        auto add = [&out] (const std::string& id, const std::string& v) { out += (out.empty() ? "" : ",") + id + "=" + v; };
        for (const auto& b : bools())  add (b.id, fx.*(b.p) ? "1" : "0");
        for (int i = 0; i < 10; ++i)   add ("e" + std::to_string (i), q (fx.eqGainDb[i]));
        for (const auto& f : floats()) add (f.id, q (fx.*(f.p)));
        return out;
    }

    inline void parse (const std::string& text, FamilyFx& fx)
    {
        size_t a = 0;
        while (a < text.size())
        {
            size_t b = text.find (',', a);
            if (b == std::string::npos) b = text.size();
            const std::string item = text.substr (a, b - a);
            a = b + 1;
            const size_t eq = item.find ('=');
            if (eq == std::string::npos) continue;
            const std::string id = item.substr (0, eq);
            const char* v = item.c_str() + eq + 1;
            bool done = false;
            for (const auto& bf : bools())
                if (id == bf.id) { fx.*(bf.p) = std::atoll (v) != 0; done = true; }
            if (! done && id.size() == 2 && id[0] == 'e' && id[1] >= '0' && id[1] <= '9')
            {
                fx.eqGainDb[id[1] - '0'] = std::min (Betel::DrumSplitEq::kMaxDb,
                                                     std::max (Betel::DrumSplitEq::kSilenceDb,
                                                               (float) ((double) std::atoll (v) / 1000.0)));
                done = true;
            }
            if (! done)
                for (const auto& ff : floats())
                    if (id == ff.id) fx.*(ff.p) = (float) ((double) std::atoll (v) / 1000.0);
        }
    }
}

inline bool operator== (const FamilyFx& a, const FamilyFx& b) { return fxdetail::toString (a) == fxdetail::toString (b); }
inline bool operator!= (const FamilyFx& a, const FamilyFx& b) { return ! (a == b); }

struct Kit
{
    std::string              name;
    std::array<Cell, 128>    cells {};
    std::array<FamilyFx, kNumFamilies> fx {};          // each family's rack
    std::array<int8_t, 128>  family {};                 // key -> family (-1 = none); assignFamilies()

    int numSounding() const
    {
        int n = 0;
        for (const auto& c : cells)
            if (! c.sound.isEmpty()) ++n;
        return n;
    }
};

// Fill kit.family from the library - each key's family is what routes its
// voices into that family's rack.  Message thread (Channel's publish paths).
inline void assignFamilies (Kit& k)
{
    for (int n = 0; n < 128; ++n)
    {
        const auto& snd = k.cells[(size_t) n].sound;
        int fam = -1;
        if (! snd.isEmpty())
        {
            const int idx = findSound (snd.name(), snd.articulation);
            if (idx >= 0) fam = familyIndex (library()[(size_t) idx].family);
        }
        k.family[(size_t) n] = (int8_t) fam;
    }
}

//==============================================================================
//  The kit's CRASH pads - the purple ones - for the Crash tab's four options.
//  Option i is the GM crash key gmKeys[i] whenever that pad holds a crash
//  (every factory kit keeps its four crashes exactly there: 49 / 55 / 52 /
//  57), else the kit's next crash pad in key order that no other option has
//  taken; -1 when the kit has no crash left.  Reads k.family, so the kit must
//  have been through assignFamilies - every published kit has.  No allocation:
//  triggerCrash asks from the audio thread.
//==============================================================================
inline void crashPads (const Kit& k, const int (&gmKeys)[4], int (&out)[4])
{
    static const int crash = familyIndex ("Crash");
    bool used[128] = {};
    for (int i = 0; i < 4; ++i)
    {
        const int g = gmKeys[i];
        out[i] = (g >= 0 && g < 128 && k.family[(size_t) g] == crash) ? g : -1;
        if (out[i] >= 0) used[out[i]] = true;
    }
    int next = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (out[i] >= 0) continue;
        while (next < 128 && (used[next] || k.family[(size_t) next] != crash)) ++next;
        if (next < 128) { out[i] = next; used[next] = true; }
    }
}

//==============================================================================
//  Authoring helper.  Unknown sound names are collected, never silently
//  dropped — the test harness fails on any.
//==============================================================================
class KitBuilder
{
public:
    KitBuilder (const char* kitName, std::vector<std::string>* missingOut)
        : missing (missingOut)
    {
        kit.name = kitName;
    }

    KitBuilder& put (int key, const char* soundName, int articulation = 0,
                     float tuneSemis = 0.0f, float decay = 0.5f, float gainDb = 0.0f)
    {
        if (key < 0 || key > 127)
            return *this;

        const int idx = findSound (soundName, articulation);
        if (idx < 0)
        {
            if (missing != nullptr)
                missing->push_back (kit.name + " key " + std::to_string (key) + ": " + soundName);
            return *this;
        }

        auto& c  = kit.cells[(size_t) key];
        c.sound  = library()[(size_t) idx].sound;
        c.macros = drum::Macros();
        c.macros.tuneSemis = tuneSemis;
        c.macros.decay     = decay;
        c.gainDb = gainDb;
        return *this;
    }

    // 42 closed, 44 pedal, 46 open — one hi-hat, one choke group
    KitBuilder& hats (const char* soundName)
    {
        put (42, soundName, 0);
        put (44, soundName, 1);
        put (46, soundName, 2);
        for (int k : { 42, 44, 46 })
            kit.cells[(size_t) k].chokeGroup = 1;
        return *this;
    }

    // Six toms on the GM keys, one prototype along the fixed ladder
    KitBuilder& toms (const char* soundName)
    {
        static const int keys[6] = { 41, 43, 45, 47, 48, 50 };
        for (int i = 0; i < 6; ++i)
            put (keys[i], soundName, 0, tom::kTomLadderSemis[i]);
        return *this;
    }

    KitBuilder& cymbals (const char* crash1, const char* crash2, const char* splash,
                         const char* china,  const char* ride1,  const char* ride2)
    {
        put (49, crash1);
        put (57, crash2);
        put (55, splash);
        put (52, china);
        put (51, ride1);
        put (59, ride2);
        put (53, "Ride Bell");
        return *this;
    }

    // Fill a key only if the kit has not claimed it
    KitBuilder& putIfEmpty (int key, const char* soundName, int articulation = 0,
                            float tuneSemis = 0.0f, float decay = 0.5f)
    {
        if (key < 0 || key > 127 || ! kit.cells[(size_t) key].sound.isEmpty())
            return *this;
        return put (key, soundName, articulation, tuneSemis, decay);
    }

    // Keys 86-91: GM2 / XG extras the 2024 styles reach.  Only fills what the
    // kit left free, so Break keeps its kicks and snares up there.
    KitBuilder& fillHighKeys()
    {
        putIfEmpty (86, "Deep Floor", 0, -5.0f, 0.30f);    // mute surdo
        putIfEmpty (87, "Deep Floor", 0, -5.0f, 0.60f);    // open surdo
        putIfEmpty (88, "Tight Clap");
        putIfEmpty (89, "Snap Clap");
        putIfEmpty (90, "Electro Snap");
        putIfEmpty (91, "909 Rim");
        return *this;
    }

    // GM percussion, keys 54-84, on every key the kit left free - Break keeps
    // its kicks and snares up at 83-91.  analog = the 808-style set (T8).
    KitBuilder& percussion (bool analog)
    {
        static const std::pair<int, const char*> gm[] =
        {
            { 54, "Tambourine" },     { 56, "Cowbell" },        { 58, "Vibraslap" },
            { 60, "Bongo High" },     { 61, "Bongo Low" },      { 62, "Conga Mute" },
            { 63, "Conga Open" },     { 64, "Conga Low" },      { 65, "Timbale High" },
            { 66, "Timbale Low" },    { 67, "Agogo High" },     { 68, "Agogo Low" },
            { 69, "Cabasa" },         { 70, "Maracas" },        { 71, "Whistle Short" },
            { 72, "Whistle Long" },   { 73, "Guiro Short" },    { 74, "Guiro Long" },
            { 75, "Claves" },         { 76, "Woodblock High" }, { 77, "Woodblock Low" },
            { 78, "Cuica Mute" },     { 79, "Cuica Open" },     { 80, "Triangle Mute" },
            { 81, "Triangle Open" },  { 82, "Shaker" },         { 83, "Jingle Bell" },
            { 84, "Bell Tree" },
        };
        static const std::pair<int, const char*> analogSet[] =
        {
            { 56, "808 Cowbell" }, { 62, "808 Conga Hi" }, { 63, "808 Conga Mid" },
            { 64, "808 Conga Lo" }, { 70, "808 Maracas" }, { 75, "808 Claves" },
        };

        if (analog)
            for (const auto& [key, name] : analogSet)
                putIfEmpty (key, name);
        for (const auto& [key, name] : gm)
            putIfEmpty (key, name);

        // Mute triangle chokes the open one, like a hand closing on it
        for (int k : { 80, 81 })
            if (std::string (kit.cells[(size_t) k].sound.name()).rfind ("Triangle", 0) == 0)
                kit.cells[(size_t) k].chokeGroup = 2;
        return *this;
    }

    // The kit is MOVED into the caller's vector, never copied through the stack.
    Kit& build() { return kit; }

private:
    std::unique_ptr<Kit> owned = std::make_unique<Kit>();   // ~37 KB: kept on the heap, see buildFactoryKits
    Kit& kit = *owned;
    std::vector<std::string>* missing = nullptr;
};

//==============================================================================
//  The factory kits.  Key lists in each block follow the styles' usage,
//  most-played first.
//==============================================================================
inline std::vector<Kit> buildFactoryKits (std::vector<std::string>* missing = nullptr)
{
    // STACK: a Kit is 128 full cells (~37 KB).  Every builder keeps its kit on
    // the HEAP and moves it into this vector - with a kit on the stack in each
    // of the fifteen blocks below, MSVC gave this function a 1.1 MB frame
    // (it does not share stack slots between blocks) and overflowed the 1 MB
    // stack of the host's UI thread the first time a style called an EDM kit.
    std::vector<Kit> kits;
    kits.reserve (24);

    // ---- DANCE (PC 27) - 68 styles -------------------------------------------
    {
        KitBuilder b ("EDM Dance", missing);
        b.put (33, "909 Punch").put (36, "Deep House").put (13, "808 Sub").put (15, "Tight Pop")
         .put (19, "Trap Boom").put (26, "808 Long Boom").put (35, "Beater Head").put (14, "Lo-fi Crunch");
        b.put (39, "909 Clap").put (32, "House Snare").put (34, "909 Snare").put (40, "Electro Snap")
         .put (31, "Tight Clap").put (38, "909 Snare", 0, -1.0f).put (27, "House Clap").put (16, "Snap Clap")
         .put (37, "909 Rim").put (25, "808 Snare").put (21, "Big Room Clap");
        b.hats ("909 Hat");
        b.put (28, "House Hat", 2).put (29, "Techno Hat", 2).put (24, "Trap Hat", 2)
         .put (20, "Crisp Pop", 0).put (23, "808 Hat", 2);
        b.toms ("909 Tom");
        b.cymbals ("909 Crash", "Bright Crash", "Splash", "Trash Crash", "909 Ride", "Ping Ride");
        b.percussion (false);
        b.putIfEmpty (17, "Scratch Push").putIfEmpty (18, "Scratch Pull")
         .putIfEmpty (22, "Zap").putIfEmpty (30, "Castanets");
        // SX920 evidence: keys the 47 dance styles play that this kit left silent
        b.putIfEmpty (12, "Tight Pop", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- ANALOG T9 (PC 59) - 53 styles: the 909 family -------------------------
    {
        KitBuilder b ("EDM Analog T9", missing);
        b.put (33, "909 Punch").put (36, "909 Punch", 0, 0.0f, 0.35f).put (24, "909 Punch", 0, -2.0f, 0.7f)
         .put (35, "Tight Pop");
        b.put (39, "909 Clap").put (40, "909 Snare", 0, 1.0f).put (34, "909 Snare", 0, 0.0f, 0.65f)
         .put (86, "909 Snare", 0, -2.0f).put (38, "909 Snare").put (27, "909 Clap", 0, 2.0f)
         .put (31, "House Snare").put (32, "Electro Snap").put (37, "909 Rim").put (30, "909 Rim", 0, -2.0f)
         .put (28, "Snap Clap").put (26, "Tight Clap");
        b.hats ("909 Hat");
        b.put (85, "909 Hat", 0, 2.0f).put (88, "Crisp Pop", 0).put (25, "Trap Hat", 0);
        b.toms ("909 Tom");
        b.cymbals ("909 Crash", "Bright Crash", "Splash", "Trash Crash", "909 Ride", "Ping Ride");
        b.percussion (false);
        b.putIfEmpty (20, "Noise Click").putIfEmpty (90, "Zap Low");
        // SX920 evidence: keys the 47 dance styles play that this kit left silent
        b.putIfEmpty (12, "909 Punch", 0, -2.0f); b.putIfEmpty (13, "Deep House", 0, 0.0f); b.putIfEmpty (14, "909 Punch", 0, -2.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "909 Clap", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (21, "Blip High", 0, 0.0f);
        b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f); b.putIfEmpty (29, "909 Snare", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- HOUSE (PC 60) - 32 styles ---------------------------------------------
    {
        KitBuilder b ("EDM House", missing);
        b.put (24, "Deep House").put (13, "909 Punch").put (35, "Tight Pop").put (36, "808 Sub");
        b.put (39, "House Clap").put (17, "Big Room Clap").put (18, "909 Clap").put (40, "House Snare")
         .put (32, "909 Snare").put (16, "Snap Clap").put (15, "Tight Clap").put (38, "909 Snare", 0, -1.0f)
         .put (26, "Reverb Clap").put (28, "808 Clap").put (34, "Electro Snap").put (37, "909 Rim");
        b.hats ("House Hat");
        b.put (19, "Techno Hat", 2).put (27, "Crisp Pop", 0);
        b.toms ("Electro Tom");
        b.cymbals ("Big Room Crash", "909 Crash", "Splash", "Trash Crash", "909 Ride", "Ping Ride");
        b.percussion (false);
        b.putIfEmpty (22, "Zap").putIfEmpty (23, "Blip Low").putIfEmpty (20, "Noise Click")
         .putIfEmpty (21, "Blip High").putIfEmpty (14, "Noise Hit");
        // SX920 evidence: keys the 47 dance styles play that this kit left silent
        b.putIfEmpty (12, "Deep House", 0, -2.0f); b.putIfEmpty (25, "909 Snare", 0, 0.0f); b.putIfEmpty (29, "House Snare", 0, 0.0f);
        b.putIfEmpty (30, "House Snare", 0, 0.0f); b.putIfEmpty (31, "909 Snare", 0, 0.0f); b.putIfEmpty (33, "909 Punch", 0, 0.0f);
        b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- BREAK (PC 57) - 37 styles: breakbeat drums, kicks/snares up high ------
    {
        KitBuilder b ("EDM Break", missing);
        b.put (87, "Acoustic Rock 22").put (22, "Beater Head").put (24, "Vintage R&B").put (83, "Lo-fi Crunch")
         .put (36, "Acoustic Rock 22", 0, -1.0f).put (86, "Tight Pop").put (33, "Acoustic Jazz 18")
         .put (85, "Beater Head", 0, 2.0f).put (35, "Beater Head");
        b.put (91, "Rimshot Crack").put (88, "Lo-fi Snare").put (89, "Acoustic Rock").put (27, "Tight Funk")
         .put (21, "Fat Ballad").put (37, "Acoustic Cross Stick").put (90, "Piccolo").put (38, "Acoustic Rock")
         .put (34, "Rimshot Crack", 0, -2.0f).put (40, "Lo-fi Snare", 0, 1.0f).put (20, "Soft Cross Stick")
         .put (39, "Real Clap Small");
        b.hats ("Acoustic Bright");
        b.put (23, "Acoustic Dark", 2).put (16, "Lo-fi Hat", 0).put (15, "Vintage Tight", 2).put (19, "Acoustic Dark", 0);
        b.toms ("Acoustic Rock Toms");
        b.cymbals ("Bright Crash", "Dark Crash", "Splash", "Trash Crash", "Rock Ride", "Jazz Ride");
        b.percussion (false);
        b.putIfEmpty (26, "Noise Sweep");
        // SX920 evidence: keys the 47 dance styles play that this kit left silent
        b.putIfEmpty (14, "Acoustic Bright", 2, 0.0f); b.putIfEmpty (25, "Lo-fi Snare", 0, 0.0f); b.putIfEmpty (28, "Lo-fi Snare", 0, 0.0f);
        b.putIfEmpty (29, "Tight Funk", 0, 0.0f); b.putIfEmpty (30, "Lo-fi Clap", 0, 0.0f); b.putIfEmpty (32, "Tight Funk", 0, 0.0f);
        b.putIfEmpty (12, "Beater Head", 0, 0.0f); b.putIfEmpty (13, "Lo-fi Crunch", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (31, "Lo-fi Snare", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- HIPHOP (PC 56) - 41 styles --------------------------------------------
    {
        KitBuilder b ("EDM HipHop", missing);
        b.put (27, "Trap Boom").put (36, "808 Long Boom").put (84, "Lo-fi Crunch").put (90, "Vintage R&B")
         .put (29, "808 Sub").put (25, "Deep House").put (19, "Tight Pop").put (30, "Beater Head")
         .put (35, "808 Sub", 0, -2.0f);
        b.put (40, "Trap Snare").put (28, "Lo-fi Snare").put (38, "Tight Funk").put (32, "Trap Clap")
         .put (39, "Lo-fi Clap").put (85, "Snap Clap").put (37, "808 Rimshot");
        b.hats ("Trap Hat");
        b.put (34, "Lo-fi Hat", 0).put (31, "Vintage Tight", 0).put (21, "Trap Hat", 2).put (87, "808 Hat", 0);
        b.toms ("808 Tom");
        b.cymbals ("Lo-fi Crash", "Dark Crash", "Splash", "Trash Crash", "Dark Ride", "808 Ride");
        b.percussion (false);
        b.putIfEmpty (26, "Scratch Pull");
        // SX920 evidence: keys the 47 dance styles play that this kit left silent
        b.putIfEmpty (12, "Trap Boom", 0, 0.0f); b.putIfEmpty (13, "Lo-fi Crunch", 0, 0.0f); b.putIfEmpty (14, "Trap Boom", 0, 0.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "Snap Clap", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (20, "Noise Click", 0, 0.0f); b.putIfEmpty (22, "Blip Low", 0, 0.0f);
        b.putIfEmpty (23, "Noise Click", 0, 0.0f); b.putIfEmpty (24, "Vintage R&B", 0, 0.0f); b.putIfEmpty (33, "Lo-fi Crunch", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- ANALOG T8 (PC 58) - 8 styles: the 808 family ---------------------------
    {
        KitBuilder b ("EDM Analog T8", missing);
        b.put (33, "808 Sub").put (35, "808 Long Boom").put (36, "808 Sub", 0, 2.0f)
         .put (24, "808 Long Boom", 0, -2.0f);
        b.put (37, "808 Rimshot").put (30, "808 Rimshot", 0, -3.0f).put (38, "808 Snare")
         .put (40, "808 Snare", 0, 2.0f).put (39, "808 Clap");
        b.hats ("808 Hat");
        b.toms ("808 Tom");
        b.cymbals ("808 Cymbal", "808 Cymbal", "Splash", "Trash Crash", "808 Ride", "808 Ride");
        b.put (57, "808 Cymbal", 0, -2.0f).put (59, "808 Ride", 0, -2.0f);
        b.percussion (true);
        b.putIfEmpty (31, "Hi Q");
        // SX920 evidence: keys the 47 dance styles play that this kit left silent
        b.putIfEmpty (12, "808 Long Boom", 0, -2.0f); b.putIfEmpty (13, "808 Sub", 0, 0.0f); b.putIfEmpty (14, "808 Long Boom", 0, -2.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "808 Clap", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (20, "Noise Click", 0, 0.0f);
        b.putIfEmpty (21, "Blip High", 0, 0.0f); b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f);
        b.putIfEmpty (25, "808 Snare", 0, 0.0f); b.putIfEmpty (26, "808 Snare", 0, 0.0f); b.putIfEmpty (27, "808 Clap", 0, 0.0f);
        b.putIfEmpty (28, "808 Snare", 0, 0.0f); b.putIfEmpty (29, "808 Snare", 0, 0.0f); b.putIfEmpty (32, "808 Snare", 0, 0.0f);
        b.putIfEmpty (34, "808 Rimshot", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM Schlager - Yamaha kit program 72 (bank 127/0). Keys measured from the
    //      25 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM Schlager", missing);
        b.hats ("House Hat");
        b.toms ("Electro Tom");
        b.cymbals ("909 Crash", "Bright Crash", "Splash", "Trash Crash", "909 Ride", "Ping Ride");
        b.put (36, "909 Punch").put (35, "Deep House").put (38, "House Snare").put (40, "909 Snare").put (39, "House Clap").put (37, "909 Rim");
        b.put (13, "Snap Clap", 0, 0.0f);   // backbeat
        b.put (14, "909 Hat", 2, 0.0f);   // offbeat
        b.put (15, "House Hat", 2, 0.0f);   // offbeat
        b.put (16, "Tight Clap", 0, 1.0f);   // backbeat
        b.put (17, "House Hat", 0, 0.0f);   // 16ths
        b.put (18, "Crisp Pop", 2, 0.0f);   // offbeat
        b.put (19, "Electro Snap", 0, 0.0f);   // backbeat
        b.put (20, "909 Hat", 2, 0.0f);   // offbeat
        b.put (21, "House Clap", 0, 1.0f);   // backbeat
        b.put (22, "Crisp Pop", 2, 0.0f);   // offbeat
        b.put (24, "909 Punch", 0, 0.0f);   // four-on-floor
        b.put (25, "Crisp Pop", 0, 0.0f);   // 16ths
        b.put (27, "909 Hat", 2, 0.0f);   // offbeat
        b.put (28, "Crisp Pop", 2, 0.0f);   // offbeat
        b.put (31, "House Snare", 0, 0.0f);   // backbeat
        b.put (32, "Electro Snap", 0, 1.0f);   // backbeat
        b.put (34, "House Snare", 0, 1.0f);   // backbeat
        b.put (45, "909 Snare", 0, 1.0f);   // backbeat on a tom key
        b.put (58, "House Clap", 0, 0.0f);   // backbeat on scrape key
        b.put (71, "Snap Clap", 0, 1.0f);   // backbeat on whistle key
        b.put (72, "909 Hat", 0, 0.0f);   // on-beat pulse
        b.put (87, "909 Snare", 0, 0.0f);   // backbeat
        b.put (88, "Deep House", 0, 0.0f);   // four-on-floor
        b.put (89, "House Hat", 2, 0.0f);   // offbeat
        b.put (90, "Tight Clap", 0, 0.0f);   // backbeat
        b.put (91, "House Hat", 2, 0.0f);   // offbeat
        b.percussion (false);
        b.putIfEmpty (12, "Tight Pop", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f); b.putIfEmpty (26, "Electro Snap", 0, 0.0f);
        b.putIfEmpty (29, "House Snare", 0, 0.0f); b.putIfEmpty (30, "Electro Snap", 0, 0.0f); b.putIfEmpty (33, "Deep House", 0, 0.0f);
        b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM Kit - Yamaha kit program 69 (bank 127/0). Keys measured from the
    //      23 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM Kit", missing);
        b.hats ("909 Hat");
        b.toms ("909 Tom");
        b.cymbals ("Big Room Crash", "909 Crash", "Splash", "Trash Crash", "909 Ride", "Ping Ride");
        b.put (36, "909 Punch").put (35, "Hardstyle").put (38, "909 Snare").put (40, "House Snare").put (39, "909 Clap").put (37, "909 Rim");
        b.put (13, "Hardstyle", 0, 0.0f);   // four-on-floor
        b.put (14, "909 Punch", 0, -2.0f);   // four-on-floor
        b.put (16, "909 Snare", 0, 0.0f);   // backbeat
        b.put (17, "House Snare", 0, 0.0f);   // backbeat
        b.put (19, "909 Hat", 2, 0.0f);   // offbeat
        b.put (21, "Hardstyle", 0, -2.0f);   // four-on-floor
        b.put (22, "Techno Hat", 0, 0.0f);   // 16ths
        b.put (23, "House Snare", 0, 1.0f);   // backbeat
        b.put (24, "909 Punch", 0, 0.0f);   // four-on-floor
        b.put (25, "Big Room Clap", 0, 0.0f);   // backbeat
        b.put (26, "Tight Clap", 0, 0.0f);   // backbeat
        b.put (27, "Electro Snap", 0, 0.0f);   // backbeat
        b.put (28, "909 Snare", 0, 1.0f);   // backbeat
        b.put (29, "909 Clap", 0, 1.0f);   // backbeat
        b.put (32, "909 Clap", 0, 0.0f);   // backbeat
        b.put (33, "Deep House", 0, 0.0f);   // four-on-floor
        b.put (76, "909 Hat", 0, 0.0f);   // on-beat pulse
        b.put (85, "Tight Pop", 0, 0.0f);   // four-on-floor
        b.percussion (false);
        b.putIfEmpty (12, "Deep House", 0, 0.0f); b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (18, "Scratch Pull", 0, 0.0f);
        b.putIfEmpty (20, "Noise Click", 0, 0.0f); b.putIfEmpty (30, "Electro Snap", 0, 0.0f); b.putIfEmpty (31, "House Snare", 0, 0.0f);
        b.putIfEmpty (34, "909 Rim", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM Dubstep - Yamaha kit program 68 (bank 127/0). Keys measured from the
    //      4 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM Dubstep", missing);
        b.hats ("Techno Hat");
        b.toms ("Syn Tom");
        b.cymbals ("Big Room Crash", "Trash Crash", "Splash", "Dark Crash", "Trash Ride", "Ping Ride");
        b.put (36, "Hardstyle").put (35, "Trap Boom").put (38, "Electro Snap").put (40, "Trap Snare").put (39, "Big Room Clap").put (37, "909 Rim");
        b.put (17, "Trap Snare", 0, 0.0f);   // backbeat
        b.put (24, "Hardstyle", 0, 0.0f);   // four-on-floor
        b.put (26, "Trap Boom", 0, 0.0f);   // four-on-floor
        b.put (31, "Electro Snap", 0, 0.0f);   // backbeat
        b.put (86, "808 Sub", 0, 0.0f);   // four-on-floor
        b.put (89, "Big Room Clap", 0, 0.0f);   // backbeat
        b.percussion (false);
        b.putIfEmpty (12, "808 Sub", 0, 0.0f); b.putIfEmpty (13, "Trap Boom", 0, 0.0f); b.putIfEmpty (14, "808 Sub", 0, 0.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "Snap Clap", 0, 0.0f); b.putIfEmpty (18, "Scratch Pull", 0, 0.0f);
        b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (20, "Noise Click", 0, 0.0f); b.putIfEmpty (21, "Blip High", 0, 0.0f);
        b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f); b.putIfEmpty (25, "Trap Snare", 0, 0.0f);
        b.putIfEmpty (27, "Big Room Clap", 0, 0.0f); b.putIfEmpty (28, "Trap Snare", 0, 0.0f); b.putIfEmpty (29, "Electro Snap", 0, 0.0f);
        b.putIfEmpty (30, "House Snare", 0, 0.0f); b.putIfEmpty (32, "Electro Snap", 0, 0.0f); b.putIfEmpty (33, "Trap Boom", 0, 0.0f);
        b.putIfEmpty (34, "909 Rim", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM Trap - Yamaha kit program 71 (bank 127/0). Keys measured from the
    //      1 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM Trap", missing);
        b.hats ("Trap Hat");
        b.toms ("808 Tom");
        b.cymbals ("808 Cymbal", "Dark Crash", "Splash", "Trash Crash", "808 Ride", "Ping Ride");
        b.put (36, "Trap Boom").put (35, "808 Long Boom").put (38, "Trap Snare").put (40, "808 Snare").put (39, "Trap Clap").put (37, "808 Rimshot");
        b.put (20, "Trap Clap", 0, 0.0f);   // backbeat
        b.put (91, "Trap Snare", 0, 0.0f);   // backbeat
        b.percussion (false);
        b.putIfEmpty (12, "808 Sub", 0, 0.0f); b.putIfEmpty (13, "808 Long Boom", 0, 0.0f); b.putIfEmpty (14, "808 Sub", 0, 0.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "Snap Clap", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (21, "Blip High", 0, 0.0f);
        b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f); b.putIfEmpty (24, "Trap Boom", 0, 0.0f);
        b.putIfEmpty (25, "808 Snare", 0, 0.0f); b.putIfEmpty (26, "Trap Snare", 0, 0.0f); b.putIfEmpty (27, "Trap Clap", 0, 0.0f);
        b.putIfEmpty (28, "808 Snare", 0, 0.0f); b.putIfEmpty (29, "Trap Snare", 0, 0.0f); b.putIfEmpty (30, "Trap Snare", 0, 0.0f);
        b.putIfEmpty (31, "808 Snare", 0, 0.0f); b.putIfEmpty (32, "Trap Snare", 0, 0.0f); b.putIfEmpty (33, "808 Long Boom", 0, 0.0f);
        b.putIfEmpty (34, "808 Rimshot", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM Electro - Yamaha kit program 70 (bank 127/0). Keys measured from the
    //      1 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM Electro", missing);
        b.hats ("909 Hat");
        b.toms ("Electro Tom");
        b.cymbals ("909 Crash", "Bright Crash", "Splash", "Trash Crash", "909 Ride", "Ping Ride");
        b.put (36, "909 Punch").put (35, "Beater Head").put (38, "Electro Snap").put (40, "909 Snare").put (39, "909 Clap").put (37, "909 Rim");
        b.put (13, "909 Punch", 0, 0.0f);   // four-on-floor
        b.percussion (false);
        b.putIfEmpty (12, "909 Punch", 0, -2.0f); b.putIfEmpty (14, "909 Punch", 0, -2.0f); b.putIfEmpty (15, "Hi Q", 0, 0.0f);
        b.putIfEmpty (16, "Snap Clap", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f); b.putIfEmpty (18, "Scratch Pull", 0, 0.0f);
        b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (20, "Noise Click", 0, 0.0f); b.putIfEmpty (21, "Blip High", 0, 0.0f);
        b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f); b.putIfEmpty (24, "909 Punch", 0, 0.0f);
        b.putIfEmpty (25, "909 Snare", 0, 0.0f); b.putIfEmpty (26, "Electro Snap", 0, 0.0f); b.putIfEmpty (27, "909 Clap", 0, 0.0f);
        b.putIfEmpty (28, "909 Snare", 0, 0.0f); b.putIfEmpty (29, "Electro Snap", 0, 0.0f); b.putIfEmpty (30, "Electro Snap", 0, 0.0f);
        b.putIfEmpty (31, "909 Snare", 0, 0.0f); b.putIfEmpty (32, "Electro Snap", 0, 0.0f); b.putIfEmpty (33, "Beater Head", 0, 0.0f);
        b.putIfEmpty (34, "909 Rim", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM Analog - Yamaha kit program 25 (bank 127/0). Keys measured from the
    //      1 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM Analog", missing);
        b.hats ("808 Hat");
        b.toms ("808 Tom");
        b.cymbals ("808 Cymbal", "Dark Crash", "Splash", "Trash Crash", "808 Ride", "Ping Ride");
        b.put (36, "808 Long Boom").put (35, "808 Sub").put (38, "808 Snare").put (40, "808 Snare", 0, 1.0f).put (39, "808 Clap").put (37, "808 Rimshot");
        b.put (21, "808 Hat", 0, 0.0f);   // 16ths
        b.percussion (false);
        b.putIfEmpty (12, "808 Long Boom", 0, -2.0f); b.putIfEmpty (13, "808 Sub", 0, 0.0f); b.putIfEmpty (14, "808 Long Boom", 0, -2.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "808 Clap", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (20, "Noise Click", 0, 0.0f);
        b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f); b.putIfEmpty (24, "808 Long Boom", 0, 0.0f);
        b.putIfEmpty (25, "808 Snare", 0, 0.0f); b.putIfEmpty (26, "808 Snare", 0, 0.0f); b.putIfEmpty (27, "808 Clap", 0, 0.0f);
        b.putIfEmpty (28, "808 Snare", 0, 0.0f); b.putIfEmpty (29, "808 Snare", 0, 0.0f); b.putIfEmpty (30, "808 Snare", 0, 0.0f);
        b.putIfEmpty (31, "808 Snare", 0, 0.0f); b.putIfEmpty (32, "808 Snare", 0, 0.0f); b.putIfEmpty (33, "808 Sub", 0, 0.0f);
        b.putIfEmpty (34, "808 Rimshot", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM 80s Pop - Yamaha kit program 66 (bank 127/0). Keys measured from the
    //      1 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM 80s Pop", missing);
        b.hats ("Vintage Tight");
        b.toms ("Power Toms");
        b.cymbals ("Bright Crash", "Dark Crash", "Splash", "Trash Crash", "Rock Ride", "Ping Ride");
        b.put (36, "Beater Head").put (35, "Acoustic Rock 22").put (38, "Rimshot Crack").put (40, "Acoustic Rock").put (39, "Reverb Clap").put (37, "Bright Cross Stick");
        b.percussion (false);
        b.putIfEmpty (12, "Beater Head", 0, -2.0f); b.putIfEmpty (13, "Acoustic Rock 22", 0, 0.0f); b.putIfEmpty (14, "Beater Head", 0, -2.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "Real Clap Small", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (20, "Noise Click", 0, 0.0f);
        b.putIfEmpty (21, "Blip High", 0, 0.0f); b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f);
        b.putIfEmpty (24, "Beater Head", 0, 0.0f); b.putIfEmpty (25, "Acoustic Rock", 0, 0.0f); b.putIfEmpty (26, "Rimshot Crack", 0, 0.0f);
        b.putIfEmpty (27, "Reverb Clap", 0, 0.0f); b.putIfEmpty (28, "Acoustic Rock", 0, 0.0f); b.putIfEmpty (29, "Rimshot Crack", 0, 0.0f);
        b.putIfEmpty (30, "Rimshot Crack", 0, 0.0f); b.putIfEmpty (31, "Acoustic Rock", 0, 0.0f); b.putIfEmpty (32, "Rimshot Crack", 0, 0.0f);
        b.putIfEmpty (33, "Acoustic Rock 22", 0, 0.0f); b.putIfEmpty (34, "Bright Cross Stick", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM 80s R&B - Yamaha kit program 67 (bank 127/0). Keys measured from the
    //      1 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM 80s R&B", missing);
        b.hats ("Lo-fi Hat");
        b.toms ("Lo-fi Tom");
        b.cymbals ("Lo-fi Crash", "Dark Crash", "Splash", "Trash Crash", "Dark Ride", "Ping Ride");
        b.put (36, "Vintage R&B").put (35, "Lo-fi Crunch").put (38, "Tight Funk").put (40, "Lo-fi Snare").put (39, "Lo-fi Clap").put (37, "Woody Click");
        b.percussion (false);
        b.putIfEmpty (12, "Vintage R&B", 0, -2.0f); b.putIfEmpty (13, "Lo-fi Crunch", 0, 0.0f); b.putIfEmpty (14, "Vintage R&B", 0, -2.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "Real Clap Small", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (20, "Noise Click", 0, 0.0f);
        b.putIfEmpty (21, "Blip High", 0, 0.0f); b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f);
        b.putIfEmpty (24, "Vintage R&B", 0, 0.0f); b.putIfEmpty (25, "Lo-fi Snare", 0, 0.0f); b.putIfEmpty (26, "Tight Funk", 0, 0.0f);
        b.putIfEmpty (27, "Lo-fi Clap", 0, 0.0f); b.putIfEmpty (28, "Lo-fi Snare", 0, 0.0f); b.putIfEmpty (29, "Tight Funk", 0, 0.0f);
        b.putIfEmpty (30, "Tight Funk", 0, 0.0f); b.putIfEmpty (31, "Lo-fi Snare", 0, 0.0f); b.putIfEmpty (32, "Tight Funk", 0, 0.0f);
        b.putIfEmpty (33, "Lo-fi Crunch", 0, 0.0f); b.putIfEmpty (34, "Woody Click", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }

    // ---- EDM Pop Drum - Yamaha kit program 73 (bank 127/0). Keys measured from the
    //      2 SX920 styles that call it; roles decide, XG conventions fill the rest.
    {
        KitBuilder b ("EDM Pop Drum", missing);
        b.hats ("Crisp Pop");
        b.toms ("Tight Pop Toms");
        b.cymbals ("Bright Crash", "Dark Crash", "Splash", "Trash Crash", "Rock Ride", "Ping Ride");
        b.put (36, "Tight Pop").put (35, "Beater Head").put (38, "Tight Funk").put (40, "Acoustic Maple").put (39, "Real Clap Small").put (37, "Acoustic Cross Stick");
        b.percussion (false);
        b.putIfEmpty (12, "Tight Pop", 0, -2.0f); b.putIfEmpty (13, "Beater Head", 0, 0.0f); b.putIfEmpty (14, "Tight Pop", 0, -2.0f);
        b.putIfEmpty (15, "Hi Q", 0, 0.0f); b.putIfEmpty (16, "Tight Clap", 0, 0.0f); b.putIfEmpty (17, "Scratch Push", 0, 0.0f);
        b.putIfEmpty (18, "Scratch Pull", 0, 0.0f); b.putIfEmpty (19, "Snap Clap", 0, 0.0f); b.putIfEmpty (20, "Noise Click", 0, 0.0f);
        b.putIfEmpty (21, "Blip High", 0, 0.0f); b.putIfEmpty (22, "Blip Low", 0, 0.0f); b.putIfEmpty (23, "Noise Click", 0, 0.0f);
        b.putIfEmpty (24, "Tight Pop", 0, 0.0f); b.putIfEmpty (25, "Acoustic Maple", 0, 0.0f); b.putIfEmpty (26, "Tight Funk", 0, 0.0f);
        b.putIfEmpty (27, "Real Clap Small", 0, 0.0f); b.putIfEmpty (28, "Acoustic Maple", 0, 0.0f); b.putIfEmpty (29, "Tight Funk", 0, 0.0f);
        b.putIfEmpty (30, "Tight Funk", 0, 0.0f); b.putIfEmpty (31, "Acoustic Maple", 0, 0.0f); b.putIfEmpty (32, "Tight Funk", 0, 0.0f);
        b.putIfEmpty (33, "Beater Head", 0, 0.0f); b.putIfEmpty (34, "Acoustic Cross Stick", 0, 0.0f); b.putIfEmpty (85, "Castanets", 0, 0.0f);
        b.fillHighKeys();
        kits.push_back (std::move (b.build()));
    }


    // THE MAIN KICK (key 36): the produced kicks, fitted to reference kicks
    // (KickPrototypes.h, THE FITTED PRODUCED KICKS), chosen per kit by genre.
    // Placed the way put() places a sound: the kick's own tuning, neutral
    // macros, unity gain; the key keeps its choke group.
    static const std::pair<const char*, const char*> kMainKick[] =
    {
        { "EDM Dance",     "Deep Punch"  }, { "EDM Electro",   "Deep Punch"  }, { "EDM Schlager",  "Deep Punch"  },
        { "EDM Break",     "Deep Punch"  }, { "EDM Kit",       "Big Room"    }, { "EDM Dubstep",   "Long Dive"   },
        { "EDM Pop Drum",  "Long Dive"   }, { "EDM Trap",      "Sub Drop"    }, { "EDM Analog T8", "Sub Drop"    },
        { "EDM HipHop",    "Sub Drop"    }, { "EDM Analog T9", "909 Classic" }, { "EDM 80s Pop",   "909 Classic" },
        { "EDM House",     "909 Round"   }, { "EDM Analog",    "909 Round"   }, { "EDM 80s R&B",   "909 Round"   },
    };
    for (auto& k : kits)
        for (const auto& [kitName, kick] : kMainKick)
            if (k.name == kitName)
            {
                const int idx = findSound (kick);
                if (idx < 0)
                {
                    if (missing != nullptr) missing->push_back (k.name + " key 36: " + kick);
                    continue;
                }
                auto& c  = k.cells[36];
                c.sound  = library()[(size_t) idx].sound;
                c.macros = drum::Macros();
                c.gainDb = 0.0f;
            }
    return kits;
}

//==============================================================================
//  Which EDM kit a STYLE's drum-kit call becomes.  Bank 127 / LSB 0 only:
//  the Revo and other banks reuse these numbers for different kits.
//==============================================================================
inline const char* kitForStyleProgram (int msb, int lsb, int pc) noexcept
{
    if (msb != 127 || lsb != 0)
        return nullptr;
    switch (pc)                              // Yamaha kit names, bank 127 / LSB 0
    {
        case 25: return "EDM Analog";        // Analog Kit
        case 27: return "EDM Dance";         // Dance Kit
        case 56: return "EDM HipHop";        // Hip Hop Kit
        case 57: return "EDM Break";         // Break Kit
        case 58: return "EDM Analog T8";     // Analog T8 Kit
        case 59: return "EDM Analog T9";     // Analog T9 Kit
        case 60: return "EDM House";         // House Kit
        case 66: return "EDM 80s Pop";       // 80s Pop Kit
        case 67: return "EDM 80s R&B";       // 80s R&B Kit
        case 68: return "EDM Dubstep";       // Dubstep Kit
        case 69: return "EDM Kit";           // EDM Kit
        case 70: return "EDM Electro";       // Electro Kit
        case 71: return "EDM Trap";          // Trap Kit
        case 72: return "EDM Schlager";      // Schlager Kit
        // 73 Pop Drum Kit is NOT routed here: on the SX920 it is an ACOUSTIC Revo
        // kit, called by pop, R&B, country and rock styles (141 calls measured
        // across the library), so it stays on the acoustic path.  "EDM Pop Drum"
        // is still in the EDM editor's kit list for anyone who wants it.
        default: return nullptr;             // acoustic, Live! and SFX kits stay sampled
    }
}

//==============================================================================
//  Saving a kit.  One line per sounding key; a key with no line is silent.
//
//      EDMKIT 1
//      key|sound|art|tune|decay|damp|snap|color|drive|human|velsens|gainDb|choke[|shape]
//      33|909 Punch|0|0|500|500|500|500|150|250|600|0|0
//      36|909 Punch|0|0|500|500|500|500|150|250|600|0|0|f0=52000000,modeTau1=900000
//
//  The optional 14th field holds the sound's SHAPE: every prototype field the
//  developer edited away from the library sound (SynthParams.h), values
//  x1,000,000.  A key without it plays the library sound as shipped.
//
//  INTEGERS ONLY (values x1000): no decimal point ever reaches the file, so a
//  kit saved under one number locale reads back identically under any other.
//  Sounds are stored by NAME + articulation, so a prototype retuned in a later
//  version is heard in the user's kit - and a renamed one falls silent, which
//  is why library names must never change once shipped.
//==============================================================================
inline std::string serializeKit (const Kit& kit)
{
    auto q = [] (float v) { return std::to_string ((long) std::lround (v * 1000.0f)); };

    std::string out = "EDMKIT 1\n";
    for (int k = 0; k < 128; ++k)
    {
        const auto& c = kit.cells[(size_t) k];
        if (c.sound.isEmpty())
            continue;

        const auto& m = c.macros;
        out += std::to_string (k) + "|" + c.sound.name() + "|" + std::to_string (c.sound.articulation)
             + "|" + q (m.tuneSemis) + "|" + q (m.decay) + "|" + q (m.damp) + "|" + q (m.snap)
             + "|" + q (m.color)     + "|" + q (m.drive) + "|" + q (m.humanize) + "|" + q (m.velSens)
             + "|" + q (c.gainDb)    + "|" + std::to_string (c.chokeGroup);
        std::string shape;
        if (const Sound* ref = params::librarySound (c.sound))
            shape = params::shapeDiff (*ref, c.sound);
        // The key's filter rides in the shape field as fhp / flp: an older build
        // skips ids it does not know, so the file still loads there.
        if (c.filterHpNorm > 0.0f || c.filterLpNorm < 1.0f)
            shape += (shape.empty() ? std::string() : std::string (","))
                   + "fhp=" + std::to_string (params::toStore (c.filterHpNorm))
                   + ",flp=" + std::to_string (params::toStore (c.filterLpNorm));
        if (! shape.empty())
            out += "|" + shape;
        out += "\n";
    }
    // Each family's rack, when it is not the default one
    for (int f = 0; f < kNumFamilies; ++f)
        if (kit.fx[(size_t) f] != FamilyFx())
            out += std::string ("fx|") + familyName (f) + "|" + fxdetail::toString (kit.fx[(size_t) f]) + "\n";
    return out;
}

// A key's filter from its shape field (fhp / flp, x1e6)
inline void cellFilterFromShape (const std::string& shape, Cell& c)
{
    size_t a = 0;
    while (a < shape.size())
    {
        size_t b = shape.find (',', a);
        if (b == std::string::npos) b = shape.size();
        const std::string item = shape.substr (a, b - a);
        a = b + 1;
        if (item.rfind ("fhp=", 0) == 0)
            c.filterHpNorm = std::min (1.0f, std::max (0.0f, params::fromStore (std::atoll (item.c_str() + 4))));
        else if (item.rfind ("flp=", 0) == 0)
            c.filterLpNorm = std::min (1.0f, std::max (0.0f, params::fromStore (std::atoll (item.c_str() + 4))));
    }
}

// Parse a saved kit into `out` (whose name the caller has set).  Returns false
// only when the text is not an EDM kit at all.  Unknown sound names leave that
// key silent and are reported through `unknown`.
inline bool deserializeKit (const std::string& text, Kit& out,
                            std::vector<std::string>* unknown = nullptr)
{
    if (text.rfind ("EDMKIT 1", 0) != 0)
        return false;

    auto heapKit = std::make_unique<Kit>();       // ~37 KB: never on the stack
    Kit& k = *heapKit;
    k.name = out.name;

    auto clampF = [] (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); };

    size_t lineStart = text.find ('\n');
    while (lineStart != std::string::npos && lineStart + 1 < text.size())
    {
        const size_t lineEnd = text.find ('\n', lineStart + 1);
        const std::string line = text.substr (lineStart + 1,
                                              lineEnd == std::string::npos ? std::string::npos
                                                                           : lineEnd - lineStart - 1);
        lineStart = lineEnd;
        if (line.empty())
            continue;

        std::vector<std::string> f;
        for (size_t a = 0;;)
        {
            const size_t b = line.find ('|', a);
            f.push_back (line.substr (a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        if (f.size() >= 3 && f[0] == "fx")          // a family's rack
        {
            const int fi = familyIndex (f[1]);
            if (fi >= 0)
            {
                k.fx[(size_t) fi] = FamilyFx();
                fxdetail::parse (f[2], k.fx[(size_t) fi]);
            }
            continue;
        }
        if (f.size() != 13 && f.size() != 14)       // 14th = the sound's shape
            continue;

        const int key = std::atoi (f[0].c_str());
        if (key < 0 || key > 127)
            continue;

        const int idx = findSound (f[1].c_str(), std::atoi (f[2].c_str()));
        if (idx < 0)
        {
            if (unknown != nullptr) unknown->push_back (f[1]);
            continue;
        }

        auto dq = [&f] (size_t i) { return (float) std::atol (f[i].c_str()) / 1000.0f; };

        auto& c = k.cells[(size_t) key];
        c.sound             = library()[(size_t) idx].sound;
        c.macros.tuneSemis  = clampF (dq (3), -25.0f, 25.0f);
        c.macros.decay      = clampF (dq (4), 0.0f, 1.0f);
        c.macros.damp       = clampF (dq (5), 0.0f, 1.0f);
        c.macros.snap       = clampF (dq (6), 0.0f, 1.0f);
        c.macros.color      = clampF (dq (7), 0.0f, 1.0f);
        c.macros.drive      = clampF (dq (8), 0.0f, 1.0f);
        c.macros.humanize   = clampF (dq (9), 0.0f, 1.0f);
        c.macros.velSens    = clampF (dq (10), 0.0f, 1.0f);
        c.gainDb            = clampF (dq (11), -20.0f, 20.0f);
        c.chokeGroup        = std::min (16, std::max (0, std::atoi (f[12].c_str())));
        if (f.size() == 14)
        {
            params::applyShape (c.sound, f[13]);
            cellFilterFromShape (f[13], c);
        }
    }

    out = k;
    return true;
}

inline const std::vector<Kit>& factoryKits()
{
    static const std::vector<Kit> kits = buildFactoryKits();
    return kits;
}

inline const Kit* findKit (const std::string& name)
{
    // Kits first named after their program number keep loading under the
    // Yamaha name they turned out to have.
    const std::string real = name == "EDM PC69" ? std::string ("EDM Kit")
                           : name == "EDM PC72" ? std::string ("EDM Schlager")
                           : name;
    for (const auto& k : factoryKits())
        if (k.name == real)
            return &k;
    return nullptr;
}

//==============================================================================
//  USER CHANGES AS SHIFTS FROM THE BASE KIT.
//
//  The base is the developer's .dsin file (or the built-in kit).  A set stores
//  only what the user changed, and numbers are stored as SHIFTS: base 0.27,
//  user 0.13 -> the set holds -0.14; if a later base says 0.30, the user hears
//  0.16 - their change rides on the developer's update.  What cannot be a
//  shift is stored as itself: a key's sound, a silenced key, a choke group.
//  Untouched keys and knobs store nothing and follow the base exactly.
//
//      EDMSHIFT 1
//      33|tune|-3000            numbers x1000, integers only (locale-proof)
//      60|sound|808 Clap|0      sound name + articulation
//      61|silent
//      42|choke|0
//      36|p:modeTau1|250000     a SHAPE field (SynthParams.h), shift x1,000,000
//
//  A shape shift is measured from the base cell's own sound - or, when the
//  user picked a different sound for the key, from that sound as the library
//  ships it (which is what the "sound" line restores before the shifts).
//==============================================================================
namespace shiftdetail
{
    static constexpr int kNumParams = 11;
    inline const char* paramName (int i)
    {
        static const char* n[kNumParams] = { "tune", "decay", "damp", "snap", "color",
                                             "drive", "human", "velsens", "gain", "fhp", "flp" };
        return n[i];
    }
    inline float& param (Cell& c, int i)
    {
        switch (i)
        {
            case 0:  return c.macros.tuneSemis;
            case 1:  return c.macros.decay;
            case 2:  return c.macros.damp;
            case 3:  return c.macros.snap;
            case 4:  return c.macros.color;
            case 5:  return c.macros.drive;
            case 6:  return c.macros.humanize;
            case 7:  return c.macros.velSens;
            case 9:  return c.filterHpNorm;
            case 10: return c.filterLpNorm;
            default: return c.gainDb;
        }
    }
    inline float param (const Cell& c, int i)   { return param (const_cast<Cell&> (c), i); }
    inline float clampParam (int i, float v)
    {
        // Same ranges as the editor's 0..200 sliders: TUNE +/-25 st, GAIN +/-20 dB
        if (i == 0) return std::min (25.0f, std::max (-25.0f, v));
        if (i == 8) return std::min (20.0f, std::max (-20.0f, v));
        return std::min (1.0f, std::max (0.0f, v));
    }
    inline int paramIndex (const std::string& name)
    {
        for (int i = 0; i < kNumParams; ++i)
            if (name == paramName (i)) return i;
        return -1;
    }
    inline bool sameSound (const Sound& a, const Sound& b)
    {
        if (a.isEmpty() || b.isEmpty()) return a.isEmpty() == b.isEmpty();
        return a.type == b.type && a.articulation == b.articulation
            && std::string (a.name()) == b.name();
    }
    inline std::vector<std::string> split (const std::string& line)
    {
        std::vector<std::string> f;
        for (size_t a = 0;;)
        {
            const size_t b = line.find ('|', a);
            f.push_back (line.substr (a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        return f;
    }
}

// What the user changed relative to base, as shift text ("" = nothing changed).
inline std::string computeShifts (const Kit& base, const Kit& user)
{
    using namespace shiftdetail;
    std::string body;
    for (int k = 0; k < 128; ++k)
    {
        const auto& b = base.cells[(size_t) k];
        const auto& u = user.cells[(size_t) k];
        const std::string key = std::to_string (k);

        if (! sameSound (b.sound, u.sound))
        {
            if (u.sound.isEmpty()) { body += key + "|silent\n"; continue; }
            body += key + "|sound|" + u.sound.name() + "|" + std::to_string (u.sound.articulation) + "\n";
        }
        if (u.sound.isEmpty())
            continue;

        for (int i = 0; i < kNumParams; ++i)
        {
            const long d = std::lround ((param (u, i) - param (b, i)) * 1000.0f);
            if (d != 0)
                body += key + "|" + paramName (i) + "|" + std::to_string (d)
                      + "|" + std::to_string (std::lround (param (b, i) * 1000.0f)) + "\n";   // + the base it was measured from
        }
        if (u.chokeGroup != b.chokeGroup)
            body += key + "|choke|" + std::to_string (u.chokeGroup) + "\n";

        // Shape: every prototype field the user moved (SynthParams.h)
        const Sound* ref = sameSound (b.sound, u.sound) ? &b.sound : params::librarySound (u.sound);
        const auto*  tab = params::tableFor (u.sound);
        if (ref != nullptr && tab != nullptr && ref->type == u.sound.type && params::tableFor (*ref) == tab)
        {
            Sound r = *ref, uu = u.sound;
            for (const auto& sp : tab->specs)
            {
                const float rv = *sp.field (r), uv = *sp.field (uu);
                if (rv == uv)
                    continue;
                const long long d = std::llround (((double) uv - (double) rv) * 1.0e6);
                if (d != 0)
                    body += key + "|p:" + sp.id + "|" + std::to_string (d)
                          + "|" + std::to_string (std::llround ((double) rv * 1.0e6)) + "\n";   // + its base
            }
        }
    }
    // A family's rack, whole, when it differs from the base kit's
    for (int f = 0; f < kNumFamilies; ++f)
        if (user.fx[(size_t) f] != base.fx[(size_t) f])
            body += std::string ("fx|") + familyName (f) + "|" + fxdetail::toString (user.fx[(size_t) f]) + "\n";
    return body.empty() ? std::string() : "EDMSHIFT 1\n" + body;
}

// out = base with the shifts applied.  Unknown lines and sounds are skipped.
inline void applyShifts (const Kit& base, const std::string& text, Kit& out)
{
    using namespace shiftdetail;
    out = base;
    size_t lineStart = text.find ('\n');
    while (lineStart != std::string::npos && lineStart + 1 < text.size())
    {
        const size_t lineEnd = text.find ('\n', lineStart + 1);
        const auto f = split (text.substr (lineStart + 1, lineEnd == std::string::npos ? std::string::npos
                                                                                      : lineEnd - lineStart - 1));
        lineStart = lineEnd;
        if (f.size() < 2) continue;

        if (f[0] == "fx")                             // a family's rack, whole
        {
            if (f.size() >= 3)
                if (const int fi = familyIndex (f[1]); fi >= 0)
                {
                    out.fx[(size_t) fi] = FamilyFx();
                    fxdetail::parse (f[2], out.fx[(size_t) fi]);
                }
            continue;
        }

        const int k = std::atoi (f[0].c_str());
        if (k < 0 || k > 127) continue;
        auto& c = out.cells[(size_t) k];
        const auto& b = base.cells[(size_t) k];

        if (f[1] == "silent")                        { c.sound = Sound(); continue; }
        if (f[1] == "sound" && f.size() >= 4)
        {
            const int idx = findSound (f[2].c_str(), std::atoi (f[3].c_str()));
            if (idx >= 0) c.sound = library()[(size_t) idx].sound;
            continue;
        }
        if (f[1] == "choke" && f.size() >= 3)        { c.chokeGroup = std::min (16, std::max (0, std::atoi (f[2].c_str()))); continue; }
        if (f[1].rfind ("p:", 0) == 0 && f.size() >= 3)
        {
            // Shape shift, relative to the sound the key holds at this point
            // (the base's, or the library sound a "sound" line just restored)
            if (const auto* tab = params::tableFor (c.sound))
                if (const auto* sp = params::findSpec (*tab, f[1].substr (2)))
                {
                    // 4th field: the base value the edit was measured from - see below.
                    // (Recorded base used only when the base truly moved: within
                    // the stored resolution it IS today's base, measured exactly.)
                    const float cur  = *sp->field (c.sound);
                    const float rec  = f.size() >= 4 ? params::fromStore (std::atoll (f[3].c_str())) : cur;
                    const float from = std::abs (rec - cur) <= std::max (2.0e-6f, std::abs (cur) * 2.0e-6f) ? cur : rec;
                    *sp->field (c.sound) = params::clampTo (*sp, from + params::fromStore (std::atoll (f[2].c_str())));
                }
            continue;
        }

        const int i = paramIndex (f[1]);
        if (i >= 0 && f.size() >= 3)
        {
            // A SET KEEPS THE VALUE IT SAVED.  The 4th field is the base value the
            // edit was measured from, so a pad saved as base AFTER the set was saved
            // does not add the set's edit a second time: base 27 + 4 stays 31 even
            // when the base has become 31 since.  Pads the set never edited follow
            // the base.  Sets from older builds (3 fields) ride on today's base,
            // exactly as they always did.
            const float cur  = param (b, i);
            const float rec  = f.size() >= 4 ? (float) std::atol (f[3].c_str()) / 1000.0f : cur;
            const float from = std::abs (rec - cur) <= 0.0006f ? cur : rec;   // same base within the stored resolution
            param (c, i) = clampParam (i, from + (float) std::atol (f[2].c_str()) / 1000.0f);
        }
    }
}

// The kit a slot plays, given its base kit and what the set saved:
//   ""            -> the base kit
//   "EDMSHIFT 1"  -> base + the user's shifts
//   "EDMKIT 1"    -> a whole kit saved by an earlier Grex build (kept playable)
inline bool resolveWithBase (const Kit& base, const std::string& saved, Kit& out)
{
    if (saved.rfind ("EDMSHIFT 1", 0) == 0) { applyShifts (base, saved, out); return true; }
    if (saved.rfind ("EDMKIT 1", 0) == 0)
    {
        out.name = base.name;
        if (deserializeKit (saved, out)) return true;
    }
    out = base;
    return true;
}

// The kit a slot plays: its saved cells if it has any, else the factory kit it
// is named after.  False when neither exists (unknown name, no saved cells).
inline bool resolveKit (const std::string& baseName, const std::string& savedState, Kit& out)
{
    if (! savedState.empty())
    {
        out.name = baseName;
        if (deserializeKit (savedState, out))
            return true;
    }
    if (const auto* factory = findKit (baseName))
    {
        out = *factory;
        return true;
    }
    return false;
}

} // namespace edm
