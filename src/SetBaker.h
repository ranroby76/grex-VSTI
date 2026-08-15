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
// Only the <StyleSlots> child.  Every other block in the set — StyleLevels,
// MixerState, StyleLink, anything the user has saved — is preserved exactly,
// because the baker reads the existing file, replaces one child and writes it
// back.  A set that does not exist yet gets created with the link.
//
// SOLO SLOTS ARE NOT TOUCHED.  They belong to the .ins files; the only thing a
// set says about them is their mixer fader.
//==============================================================================

#include <JuceHeader.h>
#include <functional>

#include "StyleData.h"
#include "StyleLoader.h"
#include "StylePlayer.h"
#include "SoundsTab.h"
#include "BankProgramMap.h"

namespace Betel
{
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
        static bool isStyleFile (const juce::File& f)
        {
            return f.hasFileExtension ("sty;prs;bcs;sst;pst;pcs;fps;scp;aus");
        }

        bool isRunning() const noexcept { return running; }

        /** Walk `stylesRoot`, and for every style write <StyleSlots> into the
            matching `<setsRoot>/<genre>/<name>.bset`.

            `overwrite` false leaves a set that ALREADY has a StyleSlots block
            alone, so a re-run after adding styles only fills the gaps and never
            destroys tuning the user has done by hand. */
        bool start (const juce::File& stylesRoot,
                    const juce::File& setsRoot,
                    bool overwrite)
        {
            if (running) return false;
            if (! stylesRoot.isDirectory()) return false;

            files.clear();
            for (const auto& e : juce::RangedDirectoryIterator (stylesRoot, true,
                                                                "*",
                                                                juce::File::findFiles))
                if (isStyleFile (e.getFile())) files.add (e.getFile());

            if (files.isEmpty()) return false;

            styles   = stylesRoot;
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

        int total() const noexcept { return files.size(); }

    private:
        /** Files per tick.  Small enough that the editor repaints between
            batches, large enough that 633 files do not take a minute of ticks. */
        static constexpr int kPerTick = 8;

        void timerCallback() override
        {
            for (int n = 0; n < kPerTick && index < files.size(); ++n, ++index)
                bakeOne (files[index]);

            if (onProgress)
                onProgress (index, files.size(),
                            index > 0 ? files[index - 1].getFileName() : juce::String());

            if (index >= files.size()) finish();
        }

        void bakeOne (const juce::File& styleFile)
        {
            // Where the set lives: the sets tree mirrors the styles tree.
            const auto genre = styleFile.getParentDirectory().getFileName();
            const auto setFile = sets.getChildFile (genre)
            // RAW stem, not Betel::displayNameFor: this is a FILENAME.  Two
            // styles differing only by model code (Waltz.S460 / Waltz.T170)
            // would bake to one .bset and silently overwrite each other.
                                     .getChildFile (styleFile.getFileNameWithoutExtension()
                                                        + ".bset");

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
            if (! StyleLoader::loadFromFile (styleFile, style, err))
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

            // Keep the style link current while we are here — a set the user
            // built by hand may not have one, and without it LOAD SET on the
            // file alone would not know which style it belongs to.
            if (! payload.getChildWithName ("GlobalState").isValid())
            {
                auto link = payload.getChildWithName ("StyleLink");
                if (! link.isValid())
                {
                    link = juce::ValueTree ("StyleLink");
                    payload.appendChild (link, nullptr);
                }
                link.setProperty ("relPath",
                                  styleFile.getRelativePathFrom (styles), nullptr);
            }

            setFile.getParentDirectory().createDirectory();
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
        juce::Array<juce::File> files;
        juce::File       styles, sets;
        int              index = 0;
        int              written = 0, skipped = 0, failed = 0;
        bool             force = false, running = false, cancelled = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetBaker)
    };
} // namespace Betel
