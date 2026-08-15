
#pragma once
//==============================================================================
// StyleFavorites.h — the star on every style button.
//
// One flag per style, kept in grex_style_favorites.xml under the plugin root
// and read once at startup.  Nothing else in the plugin owns it: the Styles tab
// draws it, the user toggles it, this class remembers it.
//
// ── WHY THE KEY IS "<genre>/<filename>" ──────────────────────────────────────
//
// NOT the absolute path.  The library root is relocatable (the folder-locator
// LED exists precisely so it can move), and an absolute-path key turns every
// star off the moment the user points Grex at the same library on another
// drive.  NOT the bare filename either: the folders are genres, and two genres
// are allowed to hold a style of the same name — a bare-name key would make
// them one flag.
//
// Parent folder plus file name is stable across a root move, unique across the
// library, and readable in the file if anyone opens it.
//
// ── FILE SHAPE ───────────────────────────────────────────────────────────────
//
//   <GrexStyleFavorites>
//     <Fav key="Ballad/Analog Ballad.prs"/>
//     <Fav key="Oriental/Ciftetelli 2.prs"/>
//   </GrexStyleFavorites>
//
// Only the ON entries are written, so the file stays small and "not present"
// unambiguously means "not a favourite".
//
// ── THREAD RULES ─────────────────────────────────────────────────────────────
//
// Message thread only — this is UI state, read when the grid repaints and
// written when a star is clicked.  A lock guards the map anyway, because the
// startup load and a repaint can be a frame apart and the cost is nil at this
// call rate.
//==============================================================================

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "GlobalMacros.h"      // GrexPaths — one root for every file we own

namespace Betel
{
    class StyleFavorites
    {
    public:
        static StyleFavorites& get()
        {
            static StyleFavorites instance;
            return instance;
        }

        /** "<parent folder>/<file name>" — see the header for why. */
        static juce::String keyFor (const juce::File& styleFile)
        {
            if (styleFile == juce::File()) return {};
            const auto parent = styleFile.getParentDirectory().getFileName();
            return parent.isEmpty() ? styleFile.getFileName()
                                    : parent + "/" + styleFile.getFileName();
        }

        static juce::String keyFor (const juce::String& absolutePath)
        {
            if (absolutePath.isEmpty()) return {};
            return keyFor (juce::File (absolutePath));
        }

        // ── Reads ─────────────────────────────────────────────────────────────
        bool isFavorite (const juce::String& absolutePath) const
        {
            const auto k = keyFor (absolutePath);
            if (k.isEmpty()) return false;
            const juce::ScopedLock sl (lock);
            return keys.contains (k);
        }

        bool isFavorite (const juce::File& styleFile) const
        { return isFavorite (styleFile.getFullPathName()); }

        int count() const
        {
            const juce::ScopedLock sl (lock);
            return keys.size();
        }

        // ── Writes.  Each one persists immediately ────────────────────────────

        /** Set the flag and write the file.  A star is a single click the user
            expects to survive a crash, so there is no separate save step to
            forget — the file is tiny and this happens at mouse speed, not audio
            speed. */
        void setFavorite (const juce::String& absolutePath, bool on)
        {
            const auto k = keyFor (absolutePath);
            if (k.isEmpty()) return;

            {
                const juce::ScopedLock sl (lock);
                if (on)
                {
                    if (keys.contains (k)) return;      // already so — no write
                    keys.add (k);
                }
                else
                {
                    const int i = keys.indexOf (k);
                    if (i < 0) return;                  // already so — no write
                    keys.remove (i);
                }
            }
            save();
        }

        /** Flip it.  Returns the NEW state so the caller can repaint without
            asking again. */
        bool toggle (const juce::String& absolutePath)
        {
            const bool next = ! isFavorite (absolutePath);
            setFavorite (absolutePath, next);
            return next;
        }

        // ── Persistence ───────────────────────────────────────────────────────
        static juce::File favoritesFile() { return GrexPaths::styleFavorites(); }

        bool save() const
        {
            juce::ValueTree t ("GrexStyleFavorites");
            {
                const juce::ScopedLock sl (lock);
                for (const auto& k : keys)
                {
                    juce::ValueTree f ("Fav");
                    f.setProperty ("key", k, nullptr);
                    t.appendChild (f, nullptr);
                }
            }

            const auto file = favoritesFile();
            file.getParentDirectory().createDirectory();
            if (auto xml = t.createXml()) return xml->writeTo (file);
            return false;
        }

        /** Missing or malformed file leaves the set EMPTY — the same contract
            GlobalMacros::loadFunkey follows.  Called once at startup. */
        void load()
        {
            juce::StringArray loaded;

            if (const auto xml = juce::XmlDocument::parse (favoritesFile()))
            {
                const auto t = juce::ValueTree::fromXml (*xml);
                if (t.isValid() && t.hasType ("GrexStyleFavorites"))
                {
                    for (int i = 0; i < t.getNumChildren(); ++i)
                    {
                        const auto c = t.getChild (i);
                        if (! c.hasType ("Fav")) continue;
                        const auto k = c.getProperty ("key").toString();
                        if (k.isNotEmpty() && ! loaded.contains (k)) loaded.add (k);
                    }
                }
            }

            const juce::ScopedLock sl (lock);
            keys = std::move (loaded);
        }

    private:
        StyleFavorites() = default;

        juce::CriticalSection lock;
        juce::StringArray     keys;      // only the ON entries

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleFavorites)
    };
} // namespace Betel
