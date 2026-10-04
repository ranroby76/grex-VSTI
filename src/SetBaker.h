
#pragma once
//==============================================================================
// SetBaker.h — write the GM starting point into every set in the pack.
//
// WHAT THIS IS FOR.
//
// The GM default table used to run automatically on every style load, deciding
// each style slot's envelope, filter, note range, play mode and octave from its
// program number — invisibly, and on top of whatever preset had just been
// applied.  It is now the SET that owns those values (SoundsTab::applyState),
// which means every set needs them written into it, and there are 633 of them.
//
// This does that in one pass.
//
// ── WHY IT DOES NOT LOAD THE STYLES ──────────────────────────────────────────
//
// A real style load decodes every voice the style needs and composes its drum
// kits — by the engine's own account the biggest message-thread burst there is.
// Doing that 633 times would take most of an hour and lock the UI solid.
//
// It is also unnecessary.  The only thing the bake needs from a style is which
// GM program each of the eight slots ends up on, and that resolution — the CASM
// destination map, the CASM voice name, the bank lookup, the bass-role
// correction — is entirely static and engine-free.  So the baker PARSES each
// style with StyleLoader (which is stateless by design) and calls
// StylePlayer::resolveStyleSlotFlags.  No samples, no kits, no audio.
//
// ── WHY IT RUNS ON A TIMER ───────────────────────────────────────────────────
//
// Parsing is fast but not free, and 633 of anything in one message-thread call
// is a spinning cursor.  A few files per tick keeps the editor alive, gives the
// user a live count, and makes cancelling trivial.
//
// ── WHAT IT WRITES ───────────────────────────────────────────────────────────
//
// The <StyleSlots> child, per style — and then EVERY OTHER BLOCK A SET CAN
// CARRY, filled in from a template so the baked set is complete.
//
// ── WHY COMPLETENESS MATTERS, AND WHY IT IS NOT COSMETIC ─────────────────────
//
// applySetPayload follows one rule throughout: ABSENT MEANS UNCHANGED.  A block
// a set does not carry is not reset to anything — it simply keeps whatever the
// previous style left behind.  That is the right rule for a set the user wrote
// by hand, and the wrong outcome for a freshly baked one: a set holding only
// <StyleSlots> inherits the last session's mixer, crash, jumps and levels, so
// loading it gives a different instrument depending on what was loaded before
// it.  "Some of the set did not apply" is exactly what that looks like from
// the outside.
//
// A complete set is deterministic: it fully describes the instrument, so it
// lands the same way every time and a re-save round-trips everything.
//
// ── THE TEMPLATE, AND WHY IT IS NOT A TABLE OF DEFAULTS ──────────────────────
//
// The baker is HEADLESS by design (see below), so it cannot ask the engine what
// its state is.  The obvious alternative — a table of defaults compiled into
// this file — would be a SECOND copy of every value in buildSetSnapshot, and
// the two would drift the first time a parameter was added.  The symptom would
// be baked sets silently missing whatever was newest, which is the hardest kind
// of fault to see.
//
// So the caller hands in ONE live snapshot, taken from the running plugin by
// the same buildSetSnapshot that saving uses, and every set is completed from
// it.  One capture path, no duplication, and a new parameter appears in baked
// sets the day it appears in saved ones.
//
// THE SESSION YOU BAKE FROM BECOMES THE BASELINE.  That is the cost of this
// approach and it is worth stating plainly: bake while transposed and every set
// carries that transpose.  Bake from a neutral session.
//
// ── FILL, NEVER OVERWRITE ────────────────────────────────────────────────────
//
// A block the set ALREADY has is left strictly alone, in both BAKE MISSING and
// REBUILD ALL.  The two buttons differ only over <StyleSlots>, which is
// style-derived and can always be re-derived; a user's mixer or crash tuning
// cannot, and stamping the current session over it would be destroying work to
// fix a gap.  REBUILD ALL is documented as unable to lose anything, and this
// keeps that promise true.
//
// SOLO SLOTS ARE NOT REBUILT.  They belong to the .ins files; the only thing a
// set says about them is their mixer fader — but they ARE filled from the
// template when the set has none, for the same completeness reason.
//==============================================================================

#include "FstLibrary.h"
#include "FstLoader.h"
#include <JuceHeader.h>
#include <functional>

#include "StyleData.h"
#include "StyleLoader.h"
#include "StylePlayer.h"
#include "SoundsTab.h"
#include "BankProgramMap.h"

namespace Betel
{

//==============================================================================
/** The .bset filename for a style's display name.

    ONE COPY, called by the baker when it WRITES a set and by MainComponent when
    it LOOKS ONE UP. Two copies of this rule would drift by a space or a dash and
    the symptom would be sets that exist but are never found - the worst kind,
    because everything looks correct on disk.

    Windows will not accept a slash and 93 display names carry one from their
    time-signature prefix, so the FILENAME is sanitised. The set's contents and
    the browser both keep the real name. */
inline juce::String setFileNameForStyle (const juce::String& displayName)
{
    juce::String out;
    for (auto c : displayName)
        out += (juce::String ("\\/:*?\"<>|").containsChar (c)) ? '-' : c;
    return out.trim();
}

    class SetBaker : private juce::Timer
    {
    public:
        /** Progress, every tick.  done/total, plus the file just handled. */
        std::function<void (int done, int total, const juce::String& name)> onProgress;

        /** Finished or cancelled.  written / skipped / failed. */
        std::function<void (int written, int skipped, int failed, bool cancelled)> onFinished;

        SetBaker (BankProgramMap& bankMapToUse) : bankMap (bankMapToUse) {}
        ~SetBaker() override { stopTimer(); }

        /** Style file types the loader will open — mirrors the plugin's filter. */

        bool isRunning() const noexcept { return running; }

        /** Walk `stylesRoot`, and for every style write <StyleSlots> into the
            matching `<setsRoot>/<genre>/<name>.bset`.

            `overwrite` false leaves a set that ALREADY has a StyleSlots block
            alone, so a re-run after adding styles only fills the gaps and never
            destroys tuning the user has done by hand. */
        /** `templateSet` is a complete <BetelSet> snapshot from the running
            plugin — pass buildSetSnapshot().  Every block a set lacks is filled
            from it.  An invalid tree is accepted and simply bakes StyleSlots
            alone, exactly as this did before. */
        bool start (const Betel::FstLibrary& library,
                    const juce::File& setsRoot,
                    bool overwrite,
                    const juce::ValueTree& templateSet = {})
        {
            if (running) return false;

            templ = templateSet;

            // THE WORK LIST IS THE LIBRARY'S TOC, not a directory walk.
            //
            // The styles folder holds .grxbld blobs now, so the old recursive
            // scan for *.sty found nothing and reported "no styles found" over a
            // library that was loading perfectly in the browser at the same
            // moment. Same list the browser shows, same order, same names.
            entries = library.allEntries();
            lib     = &library;

            if (entries.empty()) return false;

            sets     = setsRoot;
            force    = overwrite;
            index    = 0;
            written  = skipped = failed = 0;
            cancelled = false;
            running   = true;

            startTimer (1);
            return true;
        }

        void cancel()
        {
            if (! running) return;
            cancelled = true;
            finish();
        }

        int total() const noexcept { return (int) entries.size(); }

    private:
        /** Files per tick.  Small enough that the editor repaints between
            batches, large enough that 633 files do not take a minute of ticks. */
        static constexpr int kPerTick = 8;

        void timerCallback() override
        {
            for (int n = 0; n < kPerTick && index < (int) entries.size(); ++n, ++index)
                bakeOne (entries[(size_t) index]);

            if (onProgress)
                onProgress (index, (int) entries.size(),
                            index > 0 ? entries[(size_t) index - 1].displayName
                                      : juce::String());

            if (index >= (int) entries.size()) finish();
        }

        void bakeOne (const Betel::FstLibrary::Entry& e)
        {
            // NAMED FOR THE DISPLAY NAME NOW, not a filename stem.
            //
            // The old comment here warned that two styles differing only by
            // model code would bake to one .bset and overwrite each other. That
            // cannot happen any more: display names are unique across the whole
            // library by construction, which the manifest enforces.
            const auto genre   = e.group;
            const auto setFile = sets.getChildFile (genre)
                                     .getChildFile (setFileNameForStyle (e.displayName) + ".bset");

            // Read the existing set so every other block survives untouched.
            juce::ValueTree payload;
            if (setFile.existsAsFile())
                if (auto xml = juce::XmlDocument::parse (setFile))
                    payload = juce::ValueTree::fromXml (*xml);

            if (! payload.isValid() || ! payload.hasType ("BetelSet"))
                payload = juce::ValueTree ("BetelSet");

            // Already baked?  A SoundsState carrying a StyleSlots half is the
            // marker — and it is also whatever the user has saved by hand, so
            // without -force we leave it strictly alone.
            if (! force)
                if (auto ss = payload.getChildWithName ("SoundsState");
                    ss.isValid() && ss.getChildWithName ("StyleSlots").isValid())
                {
                    ++skipped;
                    return;
                }

            // PARSE ONLY — no engine, no samples, no kits.
            StyleData style;
            juce::String err;

            // STRAIGHT FROM THE FILE. The blob path read bytes out of a mapping
            // and handed them to the SFF parser; a .fst is its own file and its
            // own reader, so there is no intermediate buffer to get wrong.
            if (lib == nullptr || ! Fst::loadFromFile (e.file, style, err))
            {
                ++failed;
                return;
            }

            int flags[StylePlayer::kNumUserStyleSlots] {};
            StylePlayer::resolveStyleSlotFlags (style, bankMap, flags);

            auto baked = SoundsTab::bakeStyleSlots (flags);   // <SoundsState> w/ StyleSlots

            // MERGE, do not replace.  An existing SoundsState may carry the
            // user's SoloSlots and their soloMode / selectedSlot — none of which
            // is ours to discard just because the style half is being rebuilt.
            auto ss = payload.getChildWithName ("SoundsState");
            if (! ss.isValid())
            {
                payload.appendChild (baked, nullptr);
            }
            else
            {
                if (auto oldStyle = ss.getChildWithName ("StyleSlots"); oldStyle.isValid())
                    ss.removeChild (oldStyle, nullptr);
                ss.appendChild (baked.getChildWithName ("StyleSlots").createCopy(), nullptr);
            }

            // ── COMPLETE THE SET FROM THE TEMPLATE ───────────────────────────
            //
            // Every block the payload does not already have, copied in from the
            // snapshot.  Fill only — see the note at the top of this file on why
            // an existing block is never touched.
            //
            // SoundsState is handled separately below because it is the one
            // block with a style-derived half: the StyleSlots merge above owns
            // that, and only the SoloSlots and the tab's own attributes may come
            // from the template.
            if (templ.isValid())
            {
                for (int i = 0; i < templ.getNumChildren(); ++i)
                {
                    const auto src = templ.getChild (i);
                    if (! src.isValid()) continue;

                    const auto type = src.getType();
                    if (type == juce::Identifier ("SoundsState")) continue;

                    if (! payload.getChildWithName (type).isValid())
                        payload.appendChild (src.createCopy(), nullptr);
                }

                // SOLO SLOTS + the SoundsState attributes, into the block the
                // StyleSlots merge has already created.
                if (auto tSounds = templ.getChildWithName ("SoundsState"); tSounds.isValid())
                    if (auto pSounds = payload.getChildWithName ("SoundsState"); pSounds.isValid())
                    {
                        if (! pSounds.getChildWithName ("SoloSlots").isValid())
                            if (auto tSolo = tSounds.getChildWithName ("SoloSlots"); tSolo.isValid())
                                pSounds.appendChild (tSolo.createCopy(), nullptr);

                        for (int i = 0; i < tSounds.getNumProperties(); ++i)
                        {
                            const auto name = tSounds.getPropertyName (i);
                            if (! pSounds.hasProperty (name))
                                pSounds.setProperty (name, tSounds.getProperty (name), nullptr);
                        }
                    }
            }

            // ── AND THEN THIS SET IS ABOUT *THIS* STYLE ──────────────────────
            //
            // THE ONE THING THAT MUST NOT BE COPIED WHOLESALE.  The template
            // names the style that happened to be loaded when the bake started;
            // left as it is, all 633 sets would name that one style and every
            // one of them would load it.  These two properties are the style's
            // identity and are rewritten per set, always — including on a set
            // that already had a GlobalState, because a set whose slots have
            // just been rebuilt for THIS style must not still point at another.
            if (auto g = payload.getChildWithName ("GlobalState"); g.isValid())
                g.setProperty ("currentStylePath", e.styleId, nullptr);

            if (auto u = payload.getChildWithName ("UiState"); u.isValid())
                u.setProperty ("styleName", e.displayName, nullptr);

            // Keep the style link current while we are here — it is the fallback
            // applySetPayload uses when a set carries no GlobalState, and a set
            // the user built by hand may not have one.
            //
            // WRITTEN UNCONDITIONALLY NOW.  It used to be skipped whenever a
            // GlobalState existed, which was fine while the baker never created
            // one; with the template it always does, so that test would have
            // meant no set ever got a link again.  Two records of the same fact
            // is cheap here, and the link is the one that survives a GlobalState
            // being edited out by hand.
            {
                auto link = payload.getChildWithName ("StyleLink");
                if (! link.isValid())
                {
                    link = juce::ValueTree ("StyleLink");
                    payload.appendChild (link, nullptr);
                }
                // THE STABLE ID, not a path. A path was only ever meaningful
                // while styles were loose files; the id survives a rename, a
                // recategorisation and a library rebuild, which is the entire
                // reason it exists. `relPath` is left unwritten so an old set
                // still carrying one is visibly legacy rather than half-updated.
                link.setProperty ("styleId", e.styleId, nullptr);
            }

            setFile.getParentDirectory().createDirectory();
            BetelStateXml::roundNumbersTo2Decimals (payload);    // 2 decimals, like every set save
            if (auto xml = payload.createXml(); xml != nullptr && xml->writeTo (setFile))
                ++written;
            else
                ++failed;
        }

        void finish()
        {
            stopTimer();
            running = false;
            if (onFinished) onFinished (written, skipped, failed, cancelled);
        }

        BankProgramMap&  bankMap;
        std::vector<Betel::FstLibrary::Entry> entries;
        const Betel::FstLibrary* lib = nullptr;
        juce::ValueTree  templ;          // complete snapshot; every set is filled from it
        juce::File       sets;
        int              index = 0;
        int              written = 0, skipped = 0, failed = 0;
        bool             force = false, running = false, cancelled = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetBaker)
    };
} // namespace Betel
