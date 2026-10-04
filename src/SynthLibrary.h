#pragma once

#include <vector>
#include <cstring>
#include "SynthVoice.h"
#include "TomPrototypes.h"
#include "PercPrototypes.h"

//==============================================================================
//  SynthLibrary.h  —  every sound the EDM kits can put on a key.
//
//  Built once from the prototype banks, on the MESSAGE thread (first call
//  comes from kit loading).  The audio thread never reads it: Channel copies
//  the Sounds it needs into its own cell table when a kit loads.
//
//  Families are what the synth window will browse by.  A hat prototype
//  appears three times — closed, pedal, open — because a key plays one
//  articulation.
//==============================================================================

namespace edm
{

struct LibraryEntry
{
    const char* family;
    Sound       sound;
};

inline const std::vector<LibraryEntry>& library()
{
    static const std::vector<LibraryEntry> lib = []
    {
        std::vector<LibraryEntry> v;

        auto addKick = [&v] (const char* fam, const kick::KickPrototype& p)
        { Sound s; s.type = VoiceType::Kick;   s.kick   = p; v.push_back ({ fam, s }); };
        auto addSnare = [&v] (const char* fam, const snare::SnarePrototype& p)
        { Sound s; s.type = VoiceType::Snare;  s.snare  = p; v.push_back ({ fam, s }); };
        auto addCym = [&v] (const char* fam, const cymbal::CymbalPrototype& p)
        { Sound s; s.type = VoiceType::Cymbal; s.cymbal = p; v.push_back ({ fam, s }); };

        for (const auto& p : kick::prototypes())            addKick  ("Kick",       p);
        for (const auto& p : tom::prototypes())             addKick  ("Tom",        p);
        for (const auto& p : snare::snarePrototypes())      addSnare ("Snare",      p);
        for (const auto& p : snare::sideStickPrototypes())  addSnare ("Side Stick", p);

        for (const auto& p : clap::prototypes())
        { Sound s; s.type = VoiceType::Clap; s.clap = p; v.push_back ({ "Clap", s }); }

        for (const auto& p : hat::prototypes())
            for (int a = 0; a < 3; ++a)
            { Sound s; s.type = VoiceType::Hat; s.hat = p; s.articulation = a; v.push_back ({ "Hat", s }); }

        for (const auto& p : cymbal::crashPrototypes())     addCym ("Crash", p);
        for (const auto& p : cymbal::ridePrototypes())      addCym ("Ride",  p);

        // ---- Percussion and effects -----------------------------------------
        auto addClap = [&v] (const char* fam, const clap::ClapPrototype& p)
        { Sound s; s.type = VoiceType::Clap;  s.clap  = p; v.push_back ({ fam, s }); };
        auto addTone = [&v] (const char* fam, const tone::TonePrototype& p)
        { Sound s; s.type = VoiceType::Tone;  s.tone  = p; v.push_back ({ fam, s }); };
        auto isCuica = [] (const char* n) { return std::strncmp (n, "Cuica", 5) == 0; };

        for (const auto& p : perc::handDrums())             addKick ("Hand Drum", p);
        for (const auto& p : tone::prototypes())            // a cuica is a drum
            if (isCuica (p.name))                           addTone ("Hand Drum", p);

        for (const auto& p : perc::shakers())               // hat voice, closed articulation
        { Sound s; s.type = VoiceType::Hat; s.hat = p; s.articulation = 0; v.push_back ({ "Shaker", s }); }

        for (const auto& p : perc::woods())                 addSnare ("Wood", p);
        for (const auto& p : perc::scrapes())
            addClap (std::strcmp (p.name, "Castanets") == 0 ? "Wood" : "Scrape", p);

        for (const auto& p : metal::prototypes())
        { Sound s; s.type = VoiceType::Metal; s.metal = p; v.push_back ({ "Metal", s }); }

        for (const auto& p : tone::prototypes())
            if (! isCuica (p.name))
                addTone (std::strstr (p.name, "Whistle") != nullptr ? "Whistle" : "FX", p);

        return v;
    }();

    return lib;
}

// Index of a sound by name (+ hat articulation), or -1.
inline int findSound (const char* name, int articulation = 0)
{
    const auto& lib = library();
    for (int i = 0; i < (int) lib.size(); ++i)
        if (lib[(size_t) i].sound.articulation == articulation
            && std::strcmp (lib[(size_t) i].sound.name(), name) == 0)
            return i;
    return -1;
}

} // namespace edm
