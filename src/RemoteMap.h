#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

namespace Betel
{

//==============================================================================
// REMOTE CONTROL MAP — which pad or knob drives which control.
//
//==============================================================================
// THIS FILE IS A ONE-WAY DOOR.  READ THIS BEFORE ADDING ANYTHING.
//
// Every entry below has a STABLE STRING ID, and those strings are about to
// become the plugin's parameter IDs.  VST3 derives a parameter's binary handle
// by HASHING that string, so from the first release onward:
//
//   • an ID may never be RENAMED   - the DAW would see a different parameter
//                                    and every automation lane pointing at it
//                                    goes dead
//   • an entry may never be REMOVED
//   • new entries may only be APPENDED, never inserted in the middle
//
// The enum order is not itself the contract - the strings are - but keeping the
// two in step is what makes the table readable, so treat both as append-only.
//
// If a control is retired, leave its entry here and stop attaching it.  A dead
// ID costs a few bytes; a reused one silently drives the wrong thing in
// somebody's saved session.
//==============================================================================
//
// SCOPE, as chosen: everything on the LEFT PANEL except set load/save, every
// significant control on the MAIN tab, everything on the MIXER tab, and the two
// SET EDITOR sliders.  Set management is deliberately absent - a stray CC that
// overwrites a set file is a different order of accident from one that changes
// a level.
//==============================================================================
enum class RemoteId : int
{
    // ── MAIN TAB: variation pads (16) ────────────────────────────────────────
    Intro1 = 0, Intro2, Intro3, Intro4,
    Var1, Var2, Var3, Var4,
    Fill1, Fill2, Fill3, Fill4,
    Break,
    End1, End2, End3,

    // ── MAIN TAB: transport and performance ──────────────────────────────────
    PlayStop, Restart, SyncPlay, Hold, OnPress, Crash, Fingered,

    // ── MAIN TAB: the eight style element mutes ──────────────────────────────
    Element1, Element2, Element3, Element4,
    Element5, Element6, Element7, Element8,

    // ── MAIN TAB: the eight solo slot selectors ──────────────────────────────
    Solo1, Solo2, Solo3, Solo4, Solo5, Solo6, Solo7, Solo8,

    // ── LEFT PANEL (set load/save deliberately excluded) ─────────────────────
    Tempo, Transpose, SplitPoint,
    PianoMode, DawStart, Comments, OrientalScale,
    FunkeyMode,

    // ── MIXER TAB ────────────────────────────────────────────────────────────
    StyleCh1, StyleCh2, StyleCh3, StyleCh4,
    StyleCh5, StyleCh6, StyleCh7, StyleCh8,
    SoloCh1, SoloCh2, SoloCh3, SoloCh4,
    SoloCh5, SoloCh6, SoloCh7, SoloCh8,
    StyleVolume, SoloVolume, MasterVolume, MasterBoost, FinisherOn,

    // ── SET EDITOR: the two sliders ──────────────────────────────────────────
    StyleBoost, StyleFollow,
    StyleEnergy,

    kNumRemoteIds
};

//==============================================================================
/** The stable string for each id.  Index with (int) RemoteId.

    ORDER MUST MATCH THE ENUM.  A static_assert on the size guards the count,
    which is the failure that actually happens - adding an enum entry and
    forgetting the string, so every id past it silently shifts by one and a
    saved map starts driving the wrong control.
*/
struct RemoteEntry { const char* id; const char* label; bool isContinuous; };

inline const std::array<RemoteEntry, (size_t) RemoteId::kNumRemoteIds>& remoteEntries()
{
    static const std::array<RemoteEntry, (size_t) RemoteId::kNumRemoteIds> t =
    { {
        { "intro1", "INTRO 1", false }, { "intro2", "INTRO 2", false },
        { "intro3", "INTRO 3", false }, { "intro4", "INTRO 4", false },
        { "var1",   "VAR 1",   false }, { "var2",   "VAR 2",   false },
        { "var3",   "VAR 3",   false }, { "var4",   "VAR 4",   false },
        { "fill1",  "FILL 1",  false }, { "fill2",  "FILL 2",  false },
        { "fill3",  "FILL 3",  false }, { "fill4",  "FILL 4",  false },
        { "break",  "BREAK",   false },
        { "end1",   "END 1",   false }, { "end2",   "END 2",   false },
        { "end3",   "END 3",   false },

        { "playStop", "PLAY / STOP", false }, { "restart",  "RESTART",  false },
        { "syncPlay", "SYNC PLAY",   false }, { "hold",     "HOLD",     false },
        { "onPress",  "ON PRESS",    false }, { "crash",    "CRASH",    false },
        { "fingered", "FINGERED",    false },

        { "elem1", "ELEMENT 1", false }, { "elem2", "ELEMENT 2", false },
        { "elem3", "ELEMENT 3", false }, { "elem4", "ELEMENT 4", false },
        { "elem5", "ELEMENT 5", false }, { "elem6", "ELEMENT 6", false },
        { "elem7", "ELEMENT 7", false }, { "elem8", "ELEMENT 8", false },

        { "solo1", "SOLO 1", false }, { "solo2", "SOLO 2", false },
        { "solo3", "SOLO 3", false }, { "solo4", "SOLO 4", false },
        { "solo5", "SOLO 5", false }, { "solo6", "SOLO 6", false },
        { "solo7", "SOLO 7", false }, { "solo8", "SOLO 8", false },

        { "tempo",     "TEMPO",      true  }, { "transpose", "TRANSPOSE", true },
        { "splitPoint","SPLIT POINT", true },
        { "pianoMode", "PIANO MODE", false }, { "dawStart",  "DAW START", false },
        { "comments",  "COMMENTS",   false }, { "oriental",  "ORIENTAL SCALE", false },
        { "funkey",    "FUNKEY MODE", false },

        { "styleCh1", "STYLE 1", true }, { "styleCh2", "STYLE 2", true },
        { "styleCh3", "STYLE 3", true }, { "styleCh4", "STYLE 4", true },
        { "styleCh5", "STYLE 5", true }, { "styleCh6", "STYLE 6", true },
        { "styleCh7", "STYLE 7", true }, { "styleCh8", "STYLE 8", true },
        { "soloCh1",  "SOLO 1",  true }, { "soloCh2",  "SOLO 2",  true },
        { "soloCh3",  "SOLO 3",  true }, { "soloCh4",  "SOLO 4",  true },
        { "soloCh5",  "SOLO 5",  true }, { "soloCh6",  "SOLO 6",  true },
        { "soloCh7",  "SOLO 7",  true }, { "soloCh8",  "SOLO 8",  true },
        { "styleVol", "STYLE VOLUME", true }, { "soloVol",  "SOLO VOLUME", true },
        { "masterVol","MASTER VOLUME", true }, { "masterBoost","MASTER BOOST", true },
        { "finisherOn", "FINISHER", false },

        { "styleBoost", "BOOST", true }, { "styleFollow", "FOLLOW", true },
        { "styleEnergy", "ENERGY", true }
    } };
    return t;
}

inline const char* remoteIdString (RemoteId r)
{
    const int i = (int) r;
    return (i >= 0 && i < (int) RemoteId::kNumRemoteIds)
             ? remoteEntries()[(size_t) i].id : "";
}

inline const char* remoteLabel (RemoteId r)
{
    const int i = (int) r;
    return (i >= 0 && i < (int) RemoteId::kNumRemoteIds)
             ? remoteEntries()[(size_t) i].label : "";
}

/** True for a control whose VALUE matters (a fader, a knob) rather than one that
    is simply pressed.  The assign popup uses it to decide whether PAD is even
    offered: a note has no value to give a fader, so offering it would be a
    setting that cannot work. */
inline bool remoteIsContinuous (RemoteId r)
{
    const int i = (int) r;
    return i >= 0 && i < (int) RemoteId::kNumRemoteIds
        && remoteEntries()[(size_t) i].isContinuous;
}

//==============================================================================
/** The assignments themselves.

    LIVES ON THE PROCESSOR and is read from the AUDIO THREAD, which is the whole
    reason it is a flat array of atomics rather than anything friendlier: a MIDI
    message has to find its target without taking a lock.

    Saved GLOBALLY, in grex_remote_map.xml, beside grex_cc_map.xml. Which pad on
    your controller does what is a property of your rig, not of a song - the same
    argument that keeps the split point and the solo base unity out of the set.
*/
class RemoteMap
{
public:
    enum class Kind : int { None = 0, Pad = 1, Cc = 2 };

    //==========================================================================
    // HOW THE SOURCE DRIVES THE CONTROL.  Three modes cover every control in
    // Grex, and the difference between the first two is ONLY what a RELEASE
    // does:
    //
    //   Push   - MOMENTARY. Press acts, release acts again. On a stateful
    //            control (HOLD, an element mute, ONPRESS) that means it is
    //            active only while the pad is held. On a momentary one (a
    //            variation, RESTART, CRASH) there is no state to give back and
    //            the release is ignored.
    //   Toggle - LATCHING. Press acts, release is ignored, so one tap flips it
    //            and it stays flipped. This is what the old system always did,
    //            so it stays the default for buttons.
    //   Knob   - CONTINUOUS. 0-127 scaled onto the control's own range.
    //==========================================================================
    enum class Mode : int { Push = 0, Toggle = 1, Knob = 2 };

    struct Assign
    {
        Kind kind   = Kind::None;
        int  number = -1;               // note number, or CC number
        Mode mode   = Mode::Toggle;
    };

    /** What a control is driven as before anybody chooses otherwise.  A fader
        has no meaningful press and a button has no meaningful sweep, so this
        comes from the registry rather than from a guess. */
    static Mode defaultModeFor (RemoteId r) noexcept
    {
        const int i = (int) r;
        if (i < 0 || i >= (int) RemoteId::kNumRemoteIds) return Mode::Toggle;
        return remoteEntries()[(size_t) i].isContinuous ? Mode::Knob : Mode::Toggle;
    }

    static RemoteMap& get() { static RemoteMap m; return m; }

    //==========================================================================
    // Message thread
    //==========================================================================
    void assign (RemoteId r, Kind k, int number, Mode m)
    {
        const int i = (int) r;
        if (i < 0 || i >= (int) RemoteId::kNumRemoteIds) return;

        modes[(size_t) i].store (m);

        // ONE SOURCE DRIVES ONE CONTROL.  Assigning a pad that is already in use
        // CLEARS the older owner rather than creating a second one - two controls
        // firing off one pad is never what anyone meant, and the alternative is a
        // silent conflict the player has to discover by playing.
        if (k != Kind::None && number >= 0)
            for (int j = 0; j < (int) RemoteId::kNumRemoteIds; ++j)
                if (j != i && kinds[(size_t) j].load() == k
                           && numbers[(size_t) j].load() == number)
                {
                    kinds  [(size_t) j].store (Kind::None);
                    numbers[(size_t) j].store (-1);
                }

        kinds  [(size_t) i].store (k);
        numbers[(size_t) i].store (k == Kind::None ? -1 : number);
    }

    void clear (RemoteId r) { assign (r, Kind::None, -1, defaultModeFor (r)); }

    /** Mode only, leaving the source alone - what the popup's three buttons do
        once something has already been dropped on the control. */
    void setMode (RemoteId r, Mode m)
    {
        const int i = (int) r;
        if (i < 0 || i >= (int) RemoteId::kNumRemoteIds) return;
        modes[(size_t) i].store (m);
    }

    /** AUDIO THREAD. */
    Mode modeOf (RemoteId r) const noexcept
    {
        const int i = (int) r;
        if (i < 0 || i >= (int) RemoteId::kNumRemoteIds) return Mode::Toggle;
        return modes[(size_t) i].load (std::memory_order_relaxed);
    }

    Assign get (RemoteId r) const
    {
        const int i = (int) r;
        if (i < 0 || i >= (int) RemoteId::kNumRemoteIds) return {};
        return { kinds[(size_t) i].load(), numbers[(size_t) i].load(),
                 modes[(size_t) i].load() };
    }

    /** Human-readable, for the popup and for a future "what is mapped" list. */
    juce::String describe (RemoteId r) const
    {
        const auto a = get (r);
        if (a.kind == Kind::Pad) return "PAD " + juce::String (a.number);
        if (a.kind == Kind::Cc)  return "CC "  + juce::String (a.number);
        return "unassigned";
    }

    //==========================================================================
    // AUDIO THREAD.  Returns kNumRemoteIds when nothing matches.
    //
    // A linear scan over ~85 atomics, and deliberately so: a map or a hash would
    // need a lock or a lock-free structure to stay safe against the message
    // thread reassigning, and 85 relaxed loads is a few hundred nanoseconds
    // against a MIDI event rate measured in tens per second.
    //==========================================================================
    RemoteId findPad (int noteNumber) const noexcept { return find (Kind::Pad, noteNumber); }
    RemoteId findCc  (int ccNumber)   const noexcept { return find (Kind::Cc,  ccNumber);   }

    bool anyPadAssigned (int noteNumber) const noexcept
    { return findPad (noteNumber) != RemoteId::kNumRemoteIds; }

    //==========================================================================
    // Persistence
    //==========================================================================
    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("RemoteMap");
        for (int i = 0; i < (int) RemoteId::kNumRemoteIds; ++i)
        {
            const auto k = kinds[(size_t) i].load();
            if (k == Kind::None) continue;

            // Keyed by the STABLE STRING, never by index.  An index would rot
            // the moment the enum grows, and the whole point of the string is
            // that it does not.
            juce::ValueTree e ("A");
            e.setProperty ("id",  remoteIdString ((RemoteId) i), nullptr);
            e.setProperty ("k",   (int) k, nullptr);
            e.setProperty ("n",   numbers[(size_t) i].load(), nullptr);
            e.setProperty ("m",   (int) modes[(size_t) i].load(), nullptr);
            t.appendChild (e, nullptr);
        }
        return t;
    }

    void fromTree (const juce::ValueTree& t)
    {
        for (auto& k : kinds)   k.store (Kind::None);
        for (auto& n : numbers) n.store (-1);
        for (int i = 0; i < (int) RemoteId::kNumRemoteIds; ++i)
            modes[(size_t) i].store (defaultModeFor ((RemoteId) i));
        if (! t.isValid()) return;

        for (int c = 0; c < t.getNumChildren(); ++c)
        {
            const auto e = t.getChild (c);
            const auto id = e.getProperty ("id").toString();

            for (int i = 0; i < (int) RemoteId::kNumRemoteIds; ++i)
                if (id == remoteIdString ((RemoteId) i))
                {
                    kinds  [(size_t) i].store ((Kind) (int) e.getProperty ("k", 0));
                    numbers[(size_t) i].store ((int) e.getProperty ("n", -1));
                    // ABSENT MODE MEANS THE DEFAULT, NOT Push. A map written
                    // before modes existed described latching buttons, and
                    // reading a missing field as 0 would silently turn every
                    // one of them momentary.
                    modes[(size_t) i].store ((Mode) (int) e.getProperty (
                        "m", (int) defaultModeFor ((RemoteId) i)));
                    break;
                }
            // An unrecognised id is SKIPPED, not an error: it is a map written
            // by a newer build, and dropping one entry is a far better outcome
            // than refusing the whole file.
        }
    }

    //==========================================================================
    // THE FACTORY MAP.
    //
    // These are the control notes Grex has answered since the first build, and
    // they are now REAL ASSIGNMENTS rather than a hidden fallback.
    //
    // WHY THAT MATTERS.  The fallback was all-or-nothing: `note < 36 &&
    // ! anyPadAssigned()`, so dropping ONE source on ONE control killed all
    // thirty-five of them at once.  A player who mapped a single pad silently
    // lost every element, solo, variation, PLAY/STOP and HOLD note in the same
    // instant, with nothing to say why.  Seeded here instead, each note is
    // visible in the popup, individually overridable, and CLEAR ALL genuinely
    // clears rather than secretly re-enabling a shadow map underneath.
    //
    // Toggle throughout, which is what the old dispatch did: it acted on the
    // note-on and swallowed the note-off.
    //==========================================================================
    void seedFactoryDefaults()
    {
        auto set = [this] (RemoteId r, int note)
        { assign (r, Kind::Pad, note, Mode::Toggle); };

        for (int i = 0; i < 8; ++i)                       // 1-8   elements
            set ((RemoteId) ((int) RemoteId::Element1 + i), 1 + i);

        for (int i = 0; i < 6; ++i)                       // 9-14  solo 1-6
            set ((RemoteId) ((int) RemoteId::Solo1 + i), 9 + i);

        // 15-30 variation pads.  RemoteId 0..15 is already in the legacy order -
        // Intro1-4, Var1-4, Fill1-4, Break, End1-3 - so the only thing that
        // moves is BREAK, which sits at note 30 but index 12.
        for (int i = 0; i < 4;  ++i) set ((RemoteId) (0 + i), 15 + i);   // Intro1-4
        for (int i = 0; i < 4;  ++i) set ((RemoteId) (4 + i), 19 + i);   // Var1-4
        for (int i = 0; i < 4;  ++i) set ((RemoteId) (8 + i), 23 + i);   // Fill1-4
        for (int i = 0; i < 3;  ++i) set ((RemoteId) (13 + i), 27 + i);  // End1-3
        set (RemoteId::Break, 30);

        set (RemoteId::PlayStop,  31);
        set (RemoteId::Restart,   32);
        set (RemoteId::Hold,      33);
        set (RemoteId::PianoMode, 34);
        set (RemoteId::SyncPlay,  35);
    }

    //==========================================================================
    // RESET EVERYTHING - and "everything" means back to FACTORY, not to EMPTY.
    //
    // An empty map is not a usable state any more. The all-or-nothing legacy
    // fallback that used to sit under the map is gone (its whole table lives in
    // seedFactoryDefaults now), so a map with nothing in it means NO hardware
    // control at all: no variations, no PLAY/STOP, no elements, no HOLD. A user
    // reaching for "reset" wants the instrument back as it shipped, and would
    // have no way of guessing that pressing it had disabled their pads.
    //
    // CLEAR FIRST, THEN SEED. seedFactoryDefaults only writes the 35 entries it
    // knows about; it does not touch an assignment on some other control, so a
    // CC the user put on STYLE VOLUME would survive a reseed and make "reset"
    // a half-truth.
    //==========================================================================
    void resetToFactoryDefaults()
    {
        for (int i = 0; i < (int) RemoteId::kNumRemoteIds; ++i)
            clear ((RemoteId) i);

        seedFactoryDefaults();
    }

    void save (const juce::File& f) const
    {
        if (auto xml = toTree().createXml())
            xml->writeTo (f);
    }

    void load (const juce::File& f)
    {
        if (! f.existsAsFile()) return;
        if (auto xml = juce::XmlDocument::parse (f))
            fromTree (juce::ValueTree::fromXml (*xml));
    }

private:
    RemoteMap()
    {
        for (int i = 0; i < (int) RemoteId::kNumRemoteIds; ++i)
            modes[(size_t) i].store (defaultModeFor ((RemoteId) i));
    }

    RemoteId find (Kind k, int number) const noexcept
    {
        if (number < 0) return RemoteId::kNumRemoteIds;

        for (int i = 0; i < (int) RemoteId::kNumRemoteIds; ++i)
            if (kinds[(size_t) i].load (std::memory_order_relaxed) == k
             && numbers[(size_t) i].load (std::memory_order_relaxed) == number)
                return (RemoteId) i;

        return RemoteId::kNumRemoteIds;
    }

    std::array<std::atomic<Kind>, (size_t) RemoteId::kNumRemoteIds> kinds {};
    std::array<std::atomic<int>,  (size_t) RemoteId::kNumRemoteIds> numbers {};
    std::array<std::atomic<Mode>, (size_t) RemoteId::kNumRemoteIds> modes   {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemoteMap)
};

} // namespace Betel