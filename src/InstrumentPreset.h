// =============================================================================
//  InstrumentPreset.h
//
//  Grex instrument / drum "default preset" format ("NNN-Name.ins").
//
//  A .ins file is a single, self-contained voice the engine can restore in one
//  step.  It pairs a source sample blob with a full SlotParams snapshot:
//
//    • MELODIC : the source .frb file name + every SlotParams field
//                (amp/filter/pitch envelopes, LFOs, EQ, reverb, delay, the
//                 sounds-path insert FX, octave, mono/portamento, …).
//    • DRUM    : the same SlotParams snapshot, whose embedded DrumKitParams
//                carries the whole kit (lastLoadedKit + every per-key role
//                assignment + the kit FX).  A single "000-standard.ins" loads
//                the entire kit — no per-component loads.
//
//  Files live in   <project>\Grex VSTI\instruments_presets\   and are named
//  "NNN-Name.ins", where NNN is the (3-digit, zero-padded) instrument flag —
//  the SAME flag namespace the sounds folder uses.  Melodic and drum presets
//  may share a flag number (e.g. 000 piano vs 000 standard kit); the embedded
//  `type` field keeps them in separate lookup maps so a drum channel only ever
//  pulls drum presets and a melodic channel only melodic ones.
//
//  Once a flag has a .ins, a program change (or a UI selector) that resolves to
//  that flag loads the .ins instead of the bare .frb — so PC-driven loading and
//  the Grex controls finally share one path.
//
//  The on-disk payload is the existing BetelStateXml::saveSlot ValueTree, so the
//  snapshot automatically tracks any new SlotParams field with no extra work
//  here.
// =============================================================================
#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include "InstrEditPanel.h"   // SlotParams / DrumKitParams
#include "BetelStateXml.h"    // saveSlot / loadSlot (round-trips the whole SlotParams)

namespace Betel
{

//==============================================================================
/** One decoded .ins preset. */
struct InstrumentPreset
{
    bool         ok          = false;   // parsed successfully
    bool         isDrum      = false;   // type = "drum"
    bool         isStyle     = false;   // set  = "style"  (.sins);  false = solo (.ins)

    /** WHAT THIS FILE CLAIMS TO GOVERN.
    
        gainOnly = true   ->  the calibration trim and NOTHING else
        gainOnly = false  ->  the whole voice (a "Save as Default" snapshot)
    
        This distinction is load-bearing.  A preset is applied by copying its
        SlotParams onto the channel, and loadSlot leaves any property the file
        omits at the STRUCT DEFAULT — so a file that mentions only the gain does
        not "leave everything else alone", it silently stamps default attack,
        default sustain (0.0!) and a fully-open filter over the voice the style
        and the per-family envelope table had set up.  A shipped gain pack did
        exactly that: it thinned every sustained part and left the piano at full
        brightness.  Marked gainOnly, such a file now applies only the trim. */
    bool         gainOnly    = false;
    int          flag        = -1;      // NNN
    juce::String displayName;           // "Ac. Grand Piano"
    juce::String frbFileName;           // "000-Ac._Grand_Piano.frb" (melodic);
                                        // empty for drums (kit lives in params)
    SlotParams   params;                // full voice snapshot
};

//==============================================================================
namespace InstrumentPresetIO
{
    static constexpr const char* kRootTag       = "GrexInstrumentPreset";
    static constexpr const char* kExtension     = ".ins";   // melodic, SOLO set
    static constexpr const char* kDrumExtension = ".drm";   // drum kits
    static constexpr const char* kStyleExtension= ".sins";  // melodic, STYLE set
    static constexpr int         kVersion       = 1;

    //==========================================================================
    // TWO PRESET SETS FOR ONE SOUND.
    //
    // The same instrument wants different settings depending on where it plays.
    // Under the right hand it is the lead voice and normally sits at unity;
    // inside a style it is one part of an arrangement and usually wants pulling
    // back.  One preset per flag could only ever satisfy one of those.
    //
    //   instruments_presets\       NNN-Name.ins    SOLO   (channels 16..23)
    //   style_instruments_presets\ NNN-Name.sins   STYLE  (channels 0..15)
    //
    // The solo folder is untouched by the split — every existing .ins keeps its
    // name, its location and its meaning.  A style channel prefers the .sins and
    // FALLS BACK to the .ins, so nothing changes until a style version is
    // actually written, and a sound only needs two files when it genuinely
    // needs two answers.
    //
    // DRUM KITS ARE NOT SPLIT.  A kit only ever loads on a style channel (solo
    // slots are never drum), so a solo/style pair would be one real file and one
    // that can never be reached.  .drm stays in instruments_presets.
    //==========================================================================

    /** Extension for a preset of the given kind.  isStyle is ignored for drums,
        which have a single set — see the note above. */
    inline juce::String extensionFor (bool isDrum, bool isStyle = false)
    {
        if (isDrum)  return juce::String (kDrumExtension);
        return juce::String (isStyle ? kStyleExtension : kExtension);
    }

    /** "Ac. Grand Piano" -> "Ac._Grand_Piano"  (mirrors the .frb naming):
        spaces become underscores and characters illegal in a Windows file name
        are dropped, so the .ins sits cleanly beside its .frb. */
    inline juce::String sanitiseName (const juce::String& name)
    {
        juce::String s = name.trim();
        s = s.replaceCharacters (" \t", "__");
        static const juce::String illegal = "\\/:*?\"<>|";
        juce::String out;
        for (auto c : s)
            if (illegal.indexOfChar (c) < 0) out += c;
        return out.isEmpty() ? juce::String ("Unnamed") : out;
    }

    /** "000-Ac._Grand_Piano.ins" (solo melodic) / "000-Ac._Grand_Piano.sins"
        (style melodic) / "000-Standard.drm" (drum) */
    inline juce::String makeFileName (int flag, const juce::String& displayName,
                                      bool isDrum = false, bool isStyle = false)
    {
        return juce::String (juce::jmax (0, flag)).paddedLeft ('0', 3)
             + "-" + sanitiseName (displayName) + extensionFor (isDrum, isStyle);
    }

    inline juce::File presetFile (const juce::File& folder, int flag,
                                  const juce::String& displayName,
                                  bool isDrum = false, bool isStyle = false)
    {
        return folder.getChildFile (makeFileName (flag, displayName, isDrum, isStyle));
    }

    /** Forward declaration — write() reads the folder's existing presets to
        enforce one preset per flag, and read() is defined below it. */
    inline InstrumentPreset read (const juce::File& file);

    //--------------------------------------------------------------------------
    /** Write "NNN-Name.ins" into `folder` (created if missing).  Returns the
        file written, or an invalid File on failure.  Any existing preset for the
        same flag+name is overwritten — "Save as Default" intentionally replaces. */
    /** Write a preset to an EXPLICIT path, rather than deriving the name from a
        folder + flag.  Used by the drum editor's SAVE KIT, which lets the user
        pick the file — same format as everything else, so one drum file type
        covers both "the default for this kit" and "a kit I saved somewhere". */
    inline bool writeToFile (const juce::File&   file,
                             int                 flag,
                             const juce::String& displayName,
                             bool                isDrum,
                             const juce::String& frbFileName,
                             const SlotParams&   params,
                             bool                isStyle = false)
    {
        juce::ValueTree root (kRootTag);
        root.setProperty ("version", kVersion,                    nullptr);
        root.setProperty ("type",    isDrum ? "drum" : "melodic", nullptr);
        root.setProperty ("set",     isStyle ? "style" : "solo",  nullptr);
        root.setProperty ("scope",   "full",                      nullptr);
        root.setProperty ("flag",    flag,                        nullptr);
        root.setProperty ("name",    displayName,                 nullptr);
        root.setProperty ("frb",     frbFileName,                 nullptr);
        root.addChild (BetelStateXml::saveSlot (params), -1, nullptr);

        if (auto xml = root.createXml()) return xml->writeTo (file);
        return false;
    }

    inline juce::File write (const juce::File&    folder,
                             int                  flag,
                             const juce::String&  displayName,
                             bool                 isDrum,
                             const juce::String&  frbFileName,
                             const SlotParams&    params,
                             bool                 isStyle = false)
    {
        if (! folder.isDirectory())
            folder.createDirectory();

        juce::ValueTree root (kRootTag);
        root.setProperty ("version", kVersion,                       nullptr);
        root.setProperty ("type",    isDrum ? "drum" : "melodic",    nullptr);
        // Which set this file belongs to.  The extension already says it; the
        // attribute makes a hand-copied file self-describing, and read() uses it
        // to keep a mis-named file out of the wrong map.
        root.setProperty ("set",     isStyle ? "style" : "solo",     nullptr);
        // "Save as Default" always writes a complete voice.
        root.setProperty ("scope",   "full",                         nullptr);
        root.setProperty ("flag",    flag,                           nullptr);
        root.setProperty ("name",    displayName,                    nullptr);
        root.setProperty ("frb",     frbFileName,                    nullptr);
        root.addChild (BetelStateXml::saveSlot (params), -1, nullptr);

        const auto file = presetFile (folder, flag, displayName, isDrum, isStyle);

        // ONE PRESET PER FLAG.  The lookup is keyed by flag, so two files
        // claiming the same one leave it to directory order which wins — and
        // that is easy to end up with: a shipped pack names a file from the GM
        // table, the user's library names the same instrument differently, and
        // the first "Save as Default" writes a second file beside the first.
        // Clear any other preset of this kind for this flag before writing.
        if (folder.isDirectory())
        {
            const juce::String ext = extensionFor (isDrum, isStyle);
            for (const auto& other : folder.findChildFiles (juce::File::findFiles, false,
                                                            "*" + ext))
            {
                if (other == file) continue;
                const auto p = read (other);
                if (p.ok && p.flag == flag && p.isDrum == isDrum)
                    other.deleteFile();
            }
        }

        if (auto xml = root.createXml())
            return xml->writeTo (file) ? file : juce::File();
        return juce::File();
    }

    //--------------------------------------------------------------------------
    /** Parse a single .ins file. `out.ok` is false on any failure. */
    inline InstrumentPreset read (const juce::File& file)
    {
        InstrumentPreset out;

        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr) return out;

        auto root = juce::ValueTree::fromXml (*xml);
        if (! root.hasType (kRootTag)) return out;

        out.isDrum      = (root.getProperty ("type").toString() == "drum");
        // Older files predate the split and carry no "set": they are solo .ins,
        // which is exactly what the default says.
        out.isStyle     = (root.getProperty ("set").toString() == "style");
        // scope="gain" -> trim only.  Absent means a full voice, which is what
        // every file this code has ever written is.
        out.gainOnly    = (root.getProperty ("scope").toString() == "gain");
        out.flag        = (int) root.getProperty ("flag", -1);
        out.displayName = root.getProperty ("name").toString();
        out.frbFileName = root.getProperty ("frb").toString();

        // Flag fallback: derive from the leading digits of the file name when the
        // attribute is missing (hand-authored presets), matching the .frb scan.
        if (out.flag < 0)
        {
            const juce::String base = file.getFileNameWithoutExtension();
            int i = 0;
            while (i < base.length() && juce::CharacterFunctions::isDigit (base[i])) ++i;
            if (i > 0) out.flag = base.substring (0, i).getIntValue();
        }

        //----------------------------------------------------------------------
        // NO <Slot> CHILD = NO VOICE.
        //
        // A file like
        //     <GrexInstrumentPreset version="1" type="melodic" flag="7" .../>
        // is a MARKER: it names an instrument and says nothing about how it
        // should sound.  Read literally it claims to be a complete saved voice
        // whose every field happens to equal the struct default — attack 0,
        // sustain 0, filter wide open — and applying it stamps that over the
        // voice the style setup and the per-GM-family envelope table built.
        //
        // That was survivable while presets were only applied at style-load
        // setup, because the editor's own commit put the voice back.  It stopped
        // being survivable once a runtime program change applied them too: every
        // section transition re-stamped the defaults, so nothing could hold, and
        // the melodic parts the style PCs mid-song went quiet while the drums
        // (their own path) and the bass (rarely PC'd mid-song) kept playing.
        //
        // A marker is calibration at most, never a voice — so it reads as
        // gain-only and everything downstream already leaves the voice alone.
        //----------------------------------------------------------------------
        if (auto slot = root.getChildWithName ("Slot"); slot.isValid())
            // Preset context: a .ins / .sins / .drm is a saved SOUND, so every
            // control in it applies — insert-FX enables included.
            BetelStateXml::loadSlot (slot, out.params,
                                     BetelStateXml::SlotLoadContext::Preset);
        else
            out.gainOnly = true;

        out.ok = (out.flag >= 0);
        return out;
    }

    //--------------------------------------------------------------------------
    /** Scan an instruments_presets folder, splitting presets into a melodic and
        a drum map keyed by flag.  Later files win on a duplicate flag within the
        same type.  Either map pointer may be null if the caller wants only one. */
    //--------------------------------------------------------------------------
    /** Set ONE field — the base unity — on the preset for `flag` in `folder`,
        without disturbing anything else it holds.

        Rewriting the whole preset from the editor's params would be wrong here:
        the base is one value per instrument, but the .ins and the .sins hold
        two DIFFERENT voices for it, and the dialog only ever has one of them in
        front of it.  So an existing file is read, its base replaced, and the
        rest written back untouched — scope included, so a gain-only pack file
        stays gain-only.  If no preset exists yet, a gain-scope one is created:
        a base is calibration, and calibration alone must never start claiming
        to be a saved voice.

        Returns the file written (invalid on failure). */
    inline juce::File writeBaseUnity (const juce::File&   folder,
                                      int                 flag,
                                      const juce::String& displayName,
                                      const juce::String& frbFileName,
                                      float               baseUnityDb,
                                      bool                isDrum,
                                      bool                isStyle)
    {
        if (! folder.isDirectory()) folder.createDirectory();

        const auto file = presetFile (folder, flag, displayName, isDrum, isStyle);

        SlotParams   params;                       // defaults unless a file exists
        bool         gainOnly = true;              // a fresh base file is calibration only
        juce::String name     = displayName;
        juce::String frb      = frbFileName;

        // Any existing preset for this flag wins on everything except the base,
        // whatever it happens to be called on disk.
        juce::File found;
        for (const auto& other : folder.findChildFiles (juce::File::findFiles, false,
                                                        "*" + extensionFor (isDrum, isStyle)))
        {
            const auto p = read (other);
            if (p.ok && p.flag == flag && p.isDrum == isDrum)
            {
                params = p.params; gainOnly = p.gainOnly; found = other;
                if (p.frbFileName.isNotEmpty()) frb = p.frbFileName;
                break;
            }
        }

        params.baseUnityDb = juce::jlimit (-40.0f, 40.0f, baseUnityDb);
        params.gainPercent = 100.0f;               // the handle returns to the middle

        juce::ValueTree root (kRootTag);
        root.setProperty ("version", kVersion,                     nullptr);
        root.setProperty ("type",    isDrum ? "drum" : "melodic",  nullptr);
        root.setProperty ("set",     isStyle ? "style" : "solo",   nullptr);
        root.setProperty ("scope",   gainOnly ? "gain" : "full",   nullptr);
        root.setProperty ("flag",    flag,                         nullptr);
        root.setProperty ("name",    name,                         nullptr);
        root.setProperty ("frb",     frb,                          nullptr);
        root.addChild (BetelStateXml::saveSlot (params), -1, nullptr);

        if (found != juce::File() && found != file) found.deleteFile();   // one per flag

        if (auto xml = root.createXml())
            return xml->writeTo (file) ? file : juce::File();
        return juce::File();
    }

    inline void scanFolder (const juce::File&                       folder,
                            std::map<int, InstrumentPreset>*        melodicOut,
                            std::map<int, InstrumentPreset>*        drumOut,
                            bool                                    styleSet = false)
    {
        if (melodicOut) melodicOut->clear();
        if (drumOut)    drumOut->clear();
        if (! folder.isDirectory()) return;

        // Melodic presets carry .ins (solo) or .sins (style); drum kits .drm.
        // Scan the pattern for this set and route by the stored "type" attribute
        // (authoritative) so a mis-named file still lands in the right map.
        const juce::String pattern = juce::String ("*")
                                   + (styleSet ? kStyleExtension : kExtension)
                                   + ";*" + kDrumExtension;
        // RECURSIVE.  A pack sorts its presets into the same category subfolders
        // its sounds use, so a non-recursive scan would find none of them.  The
        // folder structure is presentation only - the flag decides everything.
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, pattern))
        {
            auto p = read (f);
            if (! p.ok) continue;

            // The FOLDER decides set membership, not the stored attribute.  The
            // obvious way to make a style preset is to copy an .ins, rename it
            // .sins and edit — which leaves set="solo" inside.  Normalising here
            // means that file behaves correctly instead of quietly reading as
            // the wrong set.  (Drums have one set, so they are never marked.)
            p.isStyle = styleSet && ! p.isDrum;
            if (p.isDrum) { if (drumOut)    (*drumOut)   [p.flag] = std::move (p); }
            else          { if (melodicOut) (*melodicOut)[p.flag] = std::move (p); }
        }
    }

    //--------------------------------------------------------------------------
    /** Scan SEVERAL preset folders into one pair of maps - one folder per
        installed pack.

        Merging is safe because an instrument flag is unique across every pack
        (SamplePlayerEngine::setSoundLibraryFolders reports duplicates rather
        than letting them pass), so two packs can never claim the same key.
        Later folders still win on a collision, matching scanFolder.

        A missing folder is a pack the user did not install - normal, not an
        error, and simply contributes nothing. */
    //--------------------------------------------------------------------------
    inline void scanFolders (const std::vector<juce::File>&          folders,
                             std::map<int, InstrumentPreset>*        melodicOut,
                             std::map<int, InstrumentPreset>*        drumOut,
                             bool                                    styleSet = false)
    {
        if (melodicOut) melodicOut->clear();
        if (drumOut)    drumOut->clear();

        for (const auto& folder : folders)
        {
            std::map<int, InstrumentPreset> m, d;
            scanFolder (folder, &m, &d, styleSet);

            if (melodicOut) for (auto& kv : m) (*melodicOut)[kv.first] = std::move (kv.second);
            if (drumOut)    for (auto& kv : d) (*drumOut)   [kv.first] = std::move (kv.second);
        }
    }

} // namespace InstrumentPresetIO
} // namespace Betel



