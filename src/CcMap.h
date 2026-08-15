
#pragma once
//==============================================================================
// CcMap.h — the MIDI CC learn map, as an installation-wide preset.
//
//   grex_cc_map.xml, under the plugin root.  Five learnable targets, in the
//   BetelgeuseProcessor::CcTarget order:
//
//     0  master volume
//     1  style volume
//     2  tempo
//     3  transpose
//     4  split point
//
//   A controller number of -1 means UNASSIGNED, which is also the default —
//   a fresh install learns nothing until the user teaches it something.
//
// ── WHY THIS CAME OUT OF THE SET ─────────────────────────────────────────────
//
// It used to be written into every .bset as cc0..cc4, and that is a category
// error: this map describes THE RIG.  It says which physical knob on the
// controller in front of you is wired to which function.  Nothing about that is
// a property of a song.
//
// The failure it caused was quiet and nasty.  Load a set you saved on a
// different controller — or a set someone else saved — and your knobs silently
// re-map underneath you, or go dead because the set was written on a rig where
// nothing was learned.  Nothing on screen looks wrong; the knob just stops
// doing what it did a minute ago.
//
// It is NOT folded into grex_master.xml either.  That file holds two flags about
// how the player plays; this is a hardware map with its own lifetime — you
// re-learn it when you change controller, not when you change your mind about
// chord mode — and mixing the two would mean one can't be reset without the
// other.
//
// ── SAVE-ON-CHANGE ───────────────────────────────────────────────────────────
//
// No SAVE button, same reasoning as MasterSettings: a learn is a deliberate act
// the user just performed, the file is five attributes, and a mapping that has
// to be saved by hand is a mapping that will be lost.  The assignment itself is
// captured on the AUDIO thread (the learn lands when the controller moves), so
// the persist is driven from the editor's timer via the processor's ccDirty
// flag rather than from the capture itself — nothing here touches a file from
// the audio thread.
//
// ── THREAD RULES ─────────────────────────────────────────────────────────────
//
// Atomics.  Written from the message thread only (startup load, and the timer
// mirror after a learn).  The engine reads the processor's own ccNumber array,
// never this — this is the persistence layer, not the live map.
//==============================================================================

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

#include "GlobalMacros.h"      // GrexPaths — one root for every file we own

namespace Betel
{
    class CcMap
    {
    public:
        /** Mirrors BetelgeuseProcessor::kNumCcTargets.  Kept as its own constant
            so this header does not have to include Main.h — a static_assert at
            the use site would be nice, but the include direction runs the other
            way and five is not a number that moves quietly. */
        static constexpr int kNumTargets = 5;

        static CcMap& get()
        {
            static CcMap instance;
            return instance;
        }

        // ── Reads ─────────────────────────────────────────────────────────────
        /** The controller assigned to `target`, or -1 for unassigned. */
        int ccFor (int target) const noexcept
        {
            if (target < 0 || target >= kNumTargets) return -1;
            return cc[(size_t) target].load();
        }

        int count() const noexcept
        {
            int n = 0;
            for (int i = 0; i < kNumTargets; ++i)
                if (cc[(size_t) i].load() >= 0) ++n;
            return n;
        }

        // ── Writes.  Each one persists immediately ────────────────────────────
        void setCc (int target, int ccNumber)
        {
            if (target < 0 || target >= kNumTargets) return;
            const int v = (ccNumber >= 0 && ccNumber <= 127) ? ccNumber : -1;
            if (cc[(size_t) target].exchange (v) == v) return;   // no change, no write
            save();
        }

        /** Take a whole snapshot in one go and write once.

            Used by the editor's timer after a learn lands: the processor is the
            live owner, so the cheapest correct thing is to copy all five back
            rather than guess which one moved. */
        void syncFrom (const std::array<int, kNumTargets>& live)
        {
            bool changed = false;
            for (int i = 0; i < kNumTargets; ++i)
            {
                const int v = (live[(size_t) i] >= 0 && live[(size_t) i] <= 127)
                                  ? live[(size_t) i] : -1;
                if (cc[(size_t) i].exchange (v) != v) changed = true;
            }
            if (changed) save();
        }

        void forgetAll()
        {
            for (int i = 0; i < kNumTargets; ++i) cc[(size_t) i].store (-1);
            save();
        }

        // ── Persistence ───────────────────────────────────────────────────────
        static juce::File ccMapFile() { return GrexPaths::ccMap(); }

        bool save() const
        {
            juce::ValueTree t ("GrexCcMap");
            for (int i = 0; i < kNumTargets; ++i)
                t.setProperty ("cc" + juce::String (i), cc[(size_t) i].load(), nullptr);

            const auto f = ccMapFile();
            f.getParentDirectory().createDirectory();
            if (auto xml = t.createXml()) return xml->writeTo (f);
            return false;
        }

        /** Missing or malformed file leaves everything UNASSIGNED — the same
            contract GlobalMacros::loadFunkey follows.  Called once at startup. */
        void load()
        {
            std::array<int, kNumTargets> v {};
            v.fill (-1);

            if (const auto xml = juce::XmlDocument::parse (ccMapFile()))
            {
                const auto t = juce::ValueTree::fromXml (*xml);
                if (t.isValid() && t.hasType ("GrexCcMap"))
                    for (int i = 0; i < kNumTargets; ++i)
                    {
                        const int n = (int) t.getProperty ("cc" + juce::String (i), -1);
                        v[(size_t) i] = (n >= 0 && n <= 127) ? n : -1;
                    }
            }

            for (int i = 0; i < kNumTargets; ++i) cc[(size_t) i].store (v[(size_t) i]);
        }

    private:
        CcMap() = default;

        std::array<std::atomic<int>, kNumTargets> cc { { {-1}, {-1}, {-1}, {-1}, {-1} } };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CcMap)
    };
} // namespace Betel
