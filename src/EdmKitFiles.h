#pragma once

#include <JuceHeader.h>
#include <map>
#include <mutex>
#include <string>
#include "SynthKits.h"

//==============================================================================
//  EdmKitFiles.h  —  the BASE EDM kits Grex plays.
//
//  A base kit is the developer's .dsin file:
//
//      <Grex VSTI>\sounds_gm\style_instruments_presets\<stem>.dsin
//
//  "EDM House" -> house.dsin, "EDM Analog T9" -> analog_t9.dsin.  The folder
//  is set by the processor next to the sound library (rescanSoundLibrary), so
//  it follows the user's library root.  Grex only ever scans that folder for
//  .ins / .drm / .sins, so .dsin files never reach another loader.
//
//  A missing or unreadable file falls back to the built-in kit (SynthKits.h):
//  a fresh install can never play a silent EDM KIT.
//
//  What a SLOT plays = its base kit + the set's shifts (edm::resolveWithBase).
//
//  Base kits are read once and cached.  A mutex guards the cache because a
//  host may restore a plugin's state off the message thread.  Nothing here is
//  ever called from the audio thread.
//==============================================================================

namespace EdmKitFiles
{
    struct State
    {
        std::mutex                        lock;
        juce::File                        folder;
        std::map<std::string, edm::Kit>   cache;
    };

    inline State& state()
    {
        static State s;
        return s;
    }

    // "EDM Analog T9" -> "analog_t9"
    inline juce::String fileStemFor (const juce::String& kitName)
    {
        auto s = kitName.trim();
        if (s.startsWithIgnoreCase ("EDM "))
            s = s.substring (4);
        return s.trim().toLowerCase().replaceCharacter (' ', '_');
    }

    // Set by the processor with the sound library; a new folder drops the cache.
    /** Bumped on every base save and every folder change.  A kit resolved before
        the bump may carry an old base - the engine's per-program EDM pools check
        this and re-resolve, so a pad saved as base reaches every later style load. */
    inline std::atomic<int>& baseVersionRef() { static std::atomic<int> v { 1 }; return v; }
    inline int baseVersion() { return baseVersionRef().load(); }

    inline void setBaseKitFolder (const juce::File& folder)
    {
        auto& st = state();
        const std::lock_guard<std::mutex> g (st.lock);
        if (folder != st.folder)
        {
            st.folder = folder;
            st.cache.clear();
            baseVersionRef()++;
        }
    }

    inline juce::File baseKitFile (const juce::String& kitName)
    {
        auto& st = state();
        const std::lock_guard<std::mutex> g (st.lock);
        if (st.folder == juce::File())
            return {};
        return st.folder.getChildFile (fileStemFor (kitName) + ".dsin");
    }

    // "EDMKIT 1|pads=36,38" - the pads a PAD base file owns.  A header without
    // the list is a WHOLE kit: every .dsin written before per-pad saving.  The
    // kit parser skips the header line, so an older build reads either kind as
    // a whole kit (a pad file carries every pad, for exactly that reason).
    inline std::vector<int> padsInHeader (const std::string& text, bool& isPadFile)
    {
        std::vector<int> pads;
        isPadFile = false;
        const std::string head = text.substr (0, text.find ('\n'));
        const auto at = head.find ("|pads=");
        if (at == std::string::npos) return pads;
        isPadFile = true;
        const std::string list = head.substr (at + 6);
        size_t i = 0;
        while (i < list.size())
        {
            const auto comma = list.find (',', i);
            const std::string item = list.substr (i, comma == std::string::npos ? std::string::npos : comma - i);
            if (! item.empty())
                if (const int p = std::atoi (item.c_str()); p >= 0 && p <= 127)
                    pads.push_back (p);
            if (comma == std::string::npos) break;
            i = comma + 1;
        }
        return pads;
    }

    // The base kit called kitName: its .dsin if present and readable, else the
    // built-in kit.  False only for a name that is neither.  A PAD file lends
    // only the pads it owns; the rest is the built-in kit (see saveBasePad).
    inline bool baseKit (const juce::String& kitName, edm::Kit& out)
    {
        auto& st = state();
        const std::lock_guard<std::mutex> g (st.lock);
        const std::string key = kitName.toStdString();

        if (const auto it = st.cache.find (key); it != st.cache.end())
        {
            out = it->second;
            return true;
        }

        auto kHeap = std::make_unique<edm::Kit>();          // ~37 KB kit: heap, not the UI thread's stack
        edm::Kit& k = *kHeap;
        bool ok = false;
        if (st.folder != juce::File())
        {
            const auto file = st.folder.getChildFile (fileStemFor (kitName) + ".dsin");
            if (file.existsAsFile())
            {
                const std::string text = file.loadFileAsString().toStdString();
                k.name = key;
                ok = edm::deserializeKit (text, k);

                // A PAD file: only the pads saved as base come from it.  Every
                // other pad, and the family racks, stay the built-in kit's - so a
                // sound refitted in a later build still reaches the pads nobody
                // saved, instead of being frozen by a save made on another pad.
                bool padFile = false;
                const auto pads = padsInHeader (text, padFile);
                if (ok && padFile)
                    if (const auto* builtIn = edm::findKit (key))
                    {
                        auto mHeap = std::make_unique<edm::Kit> (*builtIn);   // ~37 KB: heap
                        for (int p : pads)
                            mHeap->cells[(size_t) p] = k.cells[(size_t) p];
                        k = *mHeap;
                        k.name = key;
                    }
            }
        }
        if (! ok)
        {
            if (const auto* builtIn = edm::findKit (key))
            {
                k  = *builtIn;
                ok = true;
            }
        }
        if (! ok)
            return false;

        st.cache[key] = k;
        out = k;
        return true;
    }

    // What a slot plays: its base kit + the set's shifts (or an older set's
    // whole kit).  False only when the kit name is unknown.
    inline bool resolve (const juce::String& kitName, const juce::String& saved, edm::Kit& out)
    {
        auto baseHeap = std::make_unique<edm::Kit>();          // ~37 KB kit: heap, not the UI thread's stack
        edm::Kit& base = *baseHeap;
        if (! baseKit (kitName, base))
            return false;
        return edm::resolveWithBase (base, saved.toStdString(), out);
    }

    // DEVELOPER: write `kit` as the base file for its name.  Every set that
    // plays this kit re-reads its shifts against the new base from now on.
    inline juce::Result saveBaseKit (const edm::Kit& kit)
    {
        juce::File folder;
        {
            auto& st = state();
            const std::lock_guard<std::mutex> g (st.lock);
            folder = st.folder;
        }
        if (folder == juce::File())
            return juce::Result::fail ("The sound library folder is not set yet.");

        if (! folder.isDirectory())
            if (const auto r = folder.createDirectory(); r.failed())
                return r;

        const auto file = folder.getChildFile (fileStemFor (juce::String (kit.name)) + ".dsin");
        if (! file.replaceWithText (juce::String (edm::serializeKit (kit))))
            return juce::Result::fail ("Could not write " + file.getFullPathName());

        auto& st = state();
        const std::lock_guard<std::mutex> g (st.lock);
        st.cache[kit.name] = kit;
        baseVersionRef()++;
        return juce::Result::ok();
    }

    // DEVELOPER: save ONE PAD of kitName, as it sounds now, as that pad's base -
    // its default in every set that plays the kit, with each set's own changes
    // stored as shifts on top.  Only this pad moves: the other pads and the
    // family racks keep their base, and on a kit that ships built in they keep
    // FOLLOWING the built-in kit (the file lists the pads it owns).  A kit whose
    // base was saved WHOLE by an earlier build stays whole - its other pads were
    // frozen on purpose then, and saving one pad does not unfreeze them.
    inline juce::Result saveBasePad (const juce::String& kitName, int key, const edm::Cell& cell)
    {
        if (key < 0 || key > 127)
            return juce::Result::fail ("No pad selected.");

        juce::File folder;
        {
            auto& st = state();
            const std::lock_guard<std::mutex> g (st.lock);
            folder = st.folder;
        }
        if (folder == juce::File())
            return juce::Result::fail ("The sound library folder is not set yet.");
        if (! folder.isDirectory())
            if (const auto r = folder.createDirectory(); r.failed())
                return r;

        auto baseHeap = std::make_unique<edm::Kit>();          // ~37 KB kit: heap, not the UI thread's stack
        edm::Kit& base = *baseHeap;
        if (! baseKit (kitName, base))
            return juce::Result::fail ("Unknown kit: " + kitName);
        base.name = kitName.toStdString();
        base.cells[(size_t) key] = cell;

        const auto file = folder.getChildFile (fileStemFor (kitName) + ".dsin");
        std::vector<int> pads;
        bool padFile = false;
        const bool exists = file.existsAsFile();
        if (exists)
            pads = padsInHeader (file.loadFileAsString().toStdString(), padFile);
        const bool wholeFile = exists && ! padFile;

        std::string text = edm::serializeKit (base);          // every pad: an older build reads a whole kit
        if (! wholeFile && edm::findKit (base.name) != nullptr)
        {
            if (std::find (pads.begin(), pads.end(), key) == pads.end())
                pads.push_back (key);
            std::sort (pads.begin(), pads.end());
            std::string list;
            for (int p : pads)
                list += (list.empty() ? std::string() : std::string (",")) + std::to_string (p);
            text = "EDMKIT 1|pads=" + list + text.substr (text.find ('\n'));
        }
        if (! file.replaceWithText (juce::String (text)))
            return juce::Result::fail ("Could not write " + file.getFullPathName());

        auto& st = state();
        const std::lock_guard<std::mutex> g (st.lock);
        st.cache[base.name] = base;
        baseVersionRef()++;
        return juce::Result::ok();
    }
}
