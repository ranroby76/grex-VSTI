
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "GlobalMacros.h"      // GrexPaths — one root for every file we own

//==============================================================================
//  SearchHistory — the "LAST SEARCHES" column of the style search window.
//
//  ── IT STORES KEYWORDS, NOT RESULTS, AND THAT IS THE WHOLE DESIGN ───────────
//
//  A saved search is the WORD the player typed.  Clicking it re-runs the search
//  against the library that exists right now.
//
//  Storing the result LIST instead would have been the same size of file and a
//  quietly worse product: a saved list is a photograph of a library, and the
//  moment the blob is rebuilt - a style renamed, two added - the photograph
//  shows names that no longer resolve. The player would click a remembered
//  result and get nothing, with the list still cheerfully displaying it. A
//  keyword cannot go stale; at worst it finds fewer styles than it used to,
//  which is the truth rather than a ghost.
//
//  It also means this file stays a few hundred bytes and is readable by a human
//  who opens it, which matters for something sitting in the player's own folder.
//
//  ── ORDER IS MOST-RECENT-FIRST, AND RE-SEARCHING PROMOTES ───────────────────
//
//  add() moves an existing keyword to the top rather than appending a duplicate.
//  Without that, a player who searches "waltz" every session ends up with twenty
//  identical rows and no room for anything else - the list would be a log
//  instead of a memory.
//
//  ── CAPPED, AND THE CAP DROPS THE OLDEST ────────────────────────────────────
//
//  kMaxEntries is what the right-hand column can show without becoming its own
//  scrolling problem. Past it the oldest goes, because the thing you searched
//  twenty searches ago is the thing you are least likely to want back.
//==============================================================================
namespace Betel
{
    class SearchHistory
    {
    public:
        static constexpr int kMaxEntries = 20;

        static SearchHistory& get()
        {
            static SearchHistory instance;
            return instance;
        }

        // ── Reads ────────────────────────────────────────────────────────────
        /** Most recent first. */
        juce::StringArray keywords() const
        {
            const juce::ScopedLock sl (lock);
            return terms;
        }

        int size() const
        {
            const juce::ScopedLock sl (lock);
            return terms.size();
        }

        // ── Writes.  Each one persists immediately ───────────────────────────
        /** Promotes an existing keyword to the top rather than duplicating it.
            Empty or whitespace-only input is ignored - pressing SEARCH on an
            empty box is a CLEAR, not a search, and must not leave a blank row in
            the history to prove it happened. */
        void add (const juce::String& keyword)
        {
            const auto kw = keyword.trim();
            if (kw.isEmpty()) return;

            {
                const juce::ScopedLock sl (lock);

                // CASE-INSENSITIVE match, but the NEW spelling is what is kept:
                // a player who typed "Waltz" after "waltz" meant the capital.
                for (int i = terms.size(); --i >= 0;)
                    if (terms[i].equalsIgnoreCase (kw))
                        terms.remove (i);

                terms.insert (0, kw);

                while (terms.size() > kMaxEntries)
                    terms.remove (terms.size() - 1);
            }

            save();
        }

        void remove (const juce::String& keyword)
        {
            {
                const juce::ScopedLock sl (lock);
                for (int i = terms.size(); --i >= 0;)
                    if (terms[i].equalsIgnoreCase (keyword))
                        terms.remove (i);
            }

            save();
        }

        void clearAll()
        {
            {
                const juce::ScopedLock sl (lock);
                terms.clear();
            }

            save();
        }

        // ── Persistence ──────────────────────────────────────────────────────
        static juce::File historyFile() { return GrexPaths::searchHistory(); }

        bool save() const
        {
            juce::ValueTree t ("BalladaSearchHistory");
            {
                const juce::ScopedLock sl (lock);
                for (const auto& k : terms)
                {
                    juce::ValueTree e ("Search");
                    e.setProperty ("term", k, nullptr);
                    t.appendChild (e, nullptr);
                }
            }

            const auto file = historyFile();
            file.getParentDirectory().createDirectory();
            if (auto xml = t.createXml()) return xml->writeTo (file);
            return false;
        }

        /** Missing or malformed file leaves the list EMPTY - the same contract
            StyleFavorites::load and GlobalMacros::loadFunkey follow. A player
            who has never searched has no file, and that is not an error.

            CALLED FROM reloadRootBackedSettings, not from a constructor. This
            file lives under GrexPaths::root(), and every root-backed thing that
            was read once at construction stayed wrong for the whole session
            after the folder was relocated. */
        void load()
        {
            juce::StringArray loaded;

            if (const auto xml = juce::XmlDocument::parse (historyFile()))
            {
                const auto t = juce::ValueTree::fromXml (*xml);
                if (t.isValid() && t.hasType ("BalladaSearchHistory"))
                {
                    for (int i = 0; i < t.getNumChildren(); ++i)
                    {
                        const auto c = t.getChild (i);
                        if (! c.hasType ("Search")) continue;

                        const auto k = c.getProperty ("term").toString().trim();

                        // De-duped ON READ as well as on write: a file edited by
                        // hand, or written by an older build without the
                        // promote-on-repeat rule, would otherwise seed the list
                        // with the duplicates this class exists to prevent.
                        if (k.isNotEmpty()
                            && ! std::any_of (loaded.begin(), loaded.end(),
                                              [&k] (const juce::String& e) { return e.equalsIgnoreCase (k); }))
                            loaded.add (k);

                        if (loaded.size() >= kMaxEntries) break;
                    }
                }
            }

            const juce::ScopedLock sl (lock);
            terms = std::move (loaded);
        }

    private:
        SearchHistory() = default;

        juce::CriticalSection lock;
        juce::StringArray     terms;      // most recent first

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SearchHistory)
    };
}
