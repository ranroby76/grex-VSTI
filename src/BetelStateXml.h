

#pragma once
//==============================================================================
// BetelStateXml.h
//
// Helpers that round-trip SlotParams (+ its embedded DrumKitParams +
// DrumKitFxParams) through juce::ValueTree.  Used by the Favorites system to
// persist a full set snapshot to disk and reload it later.
//
// Schema (top-level Slot):
//
//   <Slot
//        attack="..." decay="..." sustain="..." release="..."
//        filterCutoff="..." filterReson="..." filterType="..."
//        ... all SlotParams scalar fields ...
//        eqGain_0="..." eqGain_1="..." ... eqGain_4="..."
//        eqFreq_0="..." ... eqFreq_4="..."
//        scaleTuning_0="..." ... scaleTuning_11="..." >
//     <DrumKit lastLoadedKit="Standard">
//       <Keys>
//         <Key midi="36" sourceKit="Standard" sourceRoleId="36"
//              gain="..." pitch="..." filterCutoff="..."
//              roundRobinAmt="..." attack="..." decay="..."
//              release="..." fxSend="..." />
//         ... only non-empty entries are written ...
//       </Keys>
//       <Fx eq_0="0" ... eq_9="0"
//           satDrive="0" satMix="1"
//           compThreshDb="0" compRatio="1" ...
//           revSize="0.5" revDamp="0.5" revWet="0"
//           delTimeMs="250" delFb="0.3" delWet="0" />
//     </DrumKit>
//   </Slot>
//==============================================================================

#include <JuceHeader.h>
#include "InstrEditPanel.h"   // SlotParams / DrumKitParams / DrumKitFxParams

namespace BetelStateXml
{

    //==========================================================================
    //  EVERY NUMBER IN A SET, TO 2 DECIMALS.
    //
    //  A control holds its value as a 32-bit float.  Widened to a double for the
    //  XML, it printed every digit of that float - "0.5999999642372131" for what
    //  was always 0.6.  Rounded in double precision first, JUCE prints "0.6".
    //  Run once over the whole set just before it is written (writeSetToFile,
    //  SetBaker), so no save can produce a long value again.
    //
    //  Numbers only: integers, on/off values and text are left alone, and no
    //  value moves by more than 0.005.  A number that arrived as TEXT (a block
    //  carried through from an older set) is rounded too - but only when the
    //  whole text is one plain decimal number, so an EDM kit's edits or a
    //  comment are never touched.
    //==========================================================================
    inline int plainDecimalPlaces (const juce::String& s)      // -1: not a plain decimal
    {
        auto p = s.getCharPointer();
        if (*p == '-') ++p;
        int intDigits = 0, frac = 0;
        while (*p >= '0' && *p <= '9') { ++p; ++intDigits; }
        if (intDigits == 0) return -1;
        if (*p == '.')
        {
            ++p;
            while (*p >= '0' && *p <= '9') { ++p; ++frac; }
            if (frac == 0) return -1;
        }
        else if (*p != 'e' && *p != 'E') return -1;             // an integer: not ours
        if (*p == 'e' || *p == 'E')
        {
            ++p;
            if (*p == '-' || *p == '+') ++p;
            int e = 0;
            while (*p >= '0' && *p <= '9') { ++p; ++e; }
            if (e == 0) return -1;
            frac = 99;                                           // exponent form: always rounded
        }
        return *p == 0 ? frac : -1;
    }

    inline double round2 (double d)
    {
        const double r = std::round (d * 100.0) / 100.0;
        return r == 0.0 ? 0.0 : r;                               // never "-0.0"
    }

    /** At most 2 decimals, as text: "8.45", "0.6", "7.0".  Written as TEXT
        because a double is printed from its binary value - the double nearest
        8.45 prints as "8.449999999999999".  A loaded set hands every value to
        its reader as text anyway, so reading is unchanged. */
    inline juce::String format2 (double d)
    {
        juce::String s (round2 (d), 2);                         // "8.45", "0.60", "7.00"
        while (s.endsWithChar ('0')) s = s.dropLastCharacters (1);
        if (s.endsWithChar ('.')) s << '0';                     // "7." -> "7.0"
        if (s == "-0.0") s = "0.0";
        return s;
    }

    inline void roundNumbersTo2Decimals (juce::ValueTree& t)
    {
        for (int i = 0; i < t.getNumProperties(); ++i)
        {
            const auto id = t.getPropertyName (i);
            const juce::var v = t.getProperty (id);
            if (v.isDouble())                                   // every double: its exact binary may print long
                t.setProperty (id, format2 ((double) v), nullptr);
            else if (v.isString())
            {
                const auto text = v.toString();
                if (plainDecimalPlaces (text) > 2)
                    t.setProperty (id, format2 (text.getDoubleValue()), nullptr);
            }
        }
        for (int c = 0; c < t.getNumChildren(); ++c)
        {
            auto child = t.getChild (c);
            roundNumbersTo2Decimals (child);
        }
    }

    //==========================================================================
    // SlotParams ↔ ValueTree
    //==========================================================================
    //==========================================================================
    // SWEETENER helpers — one writer and one reader, shared by the melodic slot
    // block and BOTH drum-rack blocks.  Three copies of eleven properties is
    // exactly how the eq/reverb/delay enables drifted out of sync in the first
    // place, so the sweetener gets one implementation from the start.
    //==========================================================================
    inline void writeSweetener (juce::ValueTree& t, const SweetenerParams& sw)
    {
        t.setProperty ("swEnabled",     sw.enabled,     nullptr);
        t.setProperty ("swMix",         sw.mix,         nullptr);
        t.setProperty ("swSoftenOn",    sw.softenOn,    nullptr);
        t.setProperty ("swSoftenDepth", sw.softenDepth, nullptr);
        t.setProperty ("swSoftenMs",    sw.softenMs,    nullptr);
        t.setProperty ("swPeakOn",      sw.peakOn,      nullptr);
        t.setProperty ("swPeakCeilDb",  sw.peakCeilDb,  nullptr);
        t.setProperty ("swPeakRatio",   sw.peakRatio,   nullptr);
        t.setProperty ("swTameOn",      sw.tameOn,      nullptr);
        t.setProperty ("swTameDepthDb", sw.tameDepthDb, nullptr);
        t.setProperty ("swTameFreqHz",  sw.tameFreqHz,  nullptr);
        t.setProperty ("swRoundOn",     sw.roundOn,     nullptr);
        t.setProperty ("swRoundDrive",  sw.roundDrive,  nullptr);
        t.setProperty ("swRoundMix",    sw.roundMix,    nullptr);
    }

    inline void readSweetener (const juce::ValueTree& t, SweetenerParams& sw)
    {
        sw.enabled     = (bool)  t.getProperty ("swEnabled",     sw.enabled);
        sw.mix         = (float) t.getProperty ("swMix",         (double) sw.mix);
        sw.softenOn    = (bool)  t.getProperty ("swSoftenOn",    sw.softenOn);
        sw.softenDepth = (float) t.getProperty ("swSoftenDepth", (double) sw.softenDepth);
        sw.softenMs    = (float) t.getProperty ("swSoftenMs",    (double) sw.softenMs);
        sw.peakOn      = (bool)  t.getProperty ("swPeakOn",      sw.peakOn);
        sw.peakCeilDb  = (float) t.getProperty ("swPeakCeilDb",  (double) sw.peakCeilDb);
        // Absent = a set written before PEAK existed.  Ratio 1 is "off", so an
        // old set comes back sounding exactly as it did rather than gaining a
        // compressor it never asked for.
        sw.peakRatio   = (float) t.getProperty ("swPeakRatio",   1.0);
        sw.tameOn      = (bool)  t.getProperty ("swTameOn",      sw.tameOn);
        sw.tameDepthDb = (float) t.getProperty ("swTameDepthDb", (double) sw.tameDepthDb);
        sw.tameFreqHz  = (float) t.getProperty ("swTameFreqHz",  (double) sw.tameFreqHz);
        sw.roundOn     = (bool)  t.getProperty ("swRoundOn",     sw.roundOn);
        sw.roundDrive  = (float) t.getProperty ("swRoundDrive",  (double) sw.roundDrive);
        sw.roundMix    = (float) t.getProperty ("swRoundMix",    (double) sw.roundMix);
    }

    inline juce::ValueTree saveSlot (const SlotParams& p)
    {
        juce::ValueTree t ("Slot");

        #define BSP(name) t.setProperty (#name, p.name, nullptr)

        BSP(attack); BSP(decay); BSP(sustain); BSP(release); BSP(ampCurve);
        BSP(ampCurveK);   // the continuous shape; ampCurve above is the legacy trio
        BSP(pan);
        BSP(filterCutoff); BSP(filterReson); BSP(filterType); BSP(filterKeytrack);
        BSP(variationAmount);
        // The two-thumb band filter (style slots 3..7).  These were missing, so
        // every .ins / .sins / .drm and every saved set silently dropped the
        // band edges back to wide open on reload — the UI had them, the engine
        // had them, the file never did.
        BSP(filterHpNorm); BSP(filterLpNorm);
        BSP(fEnvA); BSP(fEnvD); BSP(fEnvS); BSP(fEnvR); BSP(fEnvAmount);
        BSP(playMode); BSP(monoHoldStolen); BSP(monoRetrigNew); BSP(monoRetrigStolen);
        BSP(portamentoTime); BSP(octaveOffset);
        BSP(noteRangeOn); BSP(noteRangeLo); BSP(noteRangeHi);
        BSP(ampLfoRate);   BSP(ampLfoDepth);   BSP(ampLfoDelay);
        BSP(filtLfoRate);  BSP(filtLfoDepth);  BSP(filtLfoDelay);
        BSP(ampLfoEnabled); BSP(filtLfoEnabled);
        BSP(glideMode); BSP(glideDepth); BSP(glideTimeMs);
        BSP(glideShapeK); BSP(glideEveryN); BSP(glideOdds); BSP(glideVelMin);
        BSP(pitchLfoRate); BSP(pitchLfoDepth); BSP(pitchLfoDelay);
        BSP(pEnvA); BSP(pEnvD); BSP(pEnvS); BSP(pEnvR); BSP(pEnvDepth);
        BSP(reverbSize); BSP(reverbDamp); BSP(reverbWet); BSP(reverbDry); BSP(reverbTail);
        BSP(reverbHpNorm); BSP(reverbLpNorm); BSP(reverbPreDelay);
        BSP(gainPercent); BSP(baseUnityDb);
        BSP(delayTimeSig); BSP(delayDiv); BSP(delayFeedback); BSP(delayWet);
        BSP(delayDry); BSP(delayWetBase); BSP(reverbWetBase);
        BSP(delayDampHz); BSP(delayHpHz); BSP(delaySmoothMs);
        BSP(eqEnabled); BSP(reverbEnabled); BSP(delayEnabled);
        BSP(clickEnabled); BSP(clickVolume); BSP(clickDecayMs); BSP(clickFilePath);
        // SOUND ENGINE: written only when edited - an untouched slot costs a set nothing.
        if (p.engineSpec.isNotEmpty()) t.setProperty ("engineSpec", p.engineSpec, nullptr);

        BSP(chorusEnabled); BSP(chorusRate); BSP(chorusDepth); BSP(chorusMix);
        BSP(wahEnabled); BSP(wahSensitivity); BSP(wahRate); BSP(wahLfoDepth);
        BSP(wahBaseHz); BSP(wahQ); BSP(wahMix);
        BSP(phaserEnabled); BSP(phaserRate); BSP(phaserDepth); BSP(phaserFeedback); BSP(phaserMix);

        t.setProperty ("velCurve", p.velCurve, nullptr);
        writeSweetener (t, p.sweet);

        #undef BSP

        for (int i = 0; i < 5; ++i)
        {
            t.setProperty ("eqGain_" + juce::String (i), p.eqGain[i], nullptr);
            t.setProperty ("eqFreq_" + juce::String (i), p.eqFreq[i], nullptr);
        }

        // ── DrumKit child ────────────────────────────────────────────────────
        juce::ValueTree dk ("DrumKit");
        dk.setProperty ("lastLoadedKit", p.drumKit.lastLoadedKit, nullptr);
        if (p.drumKit.edmKit.isNotEmpty())                        // EDM KIT cells
            dk.setProperty ("edmKit", p.drumKit.edmKit, nullptr);

        juce::ValueTree keys ("Keys");
        for (int k = 0; k < 128; ++k)
        {
            const auto& e = p.drumKit.keys[(size_t) k];

            // ── SKIP DEFAULT KEYS.  THIS TEST HAD TWO FAULTS. ────────────────
            //
            // 1. IT COULD NEVER RETURN TRUE.  It required kickMixEnabled == true,
            //    but the struct default is FALSE (InstrEditPanel.h:303).  Every
            //    one of the 128 keys failed on that clause alone, so all 128 were
            //    written for every slot - drum or not - and the compaction this
            //    test exists for has never once happened.  Measured on a real
            //    set: 41 KB of default rows per slot, 16 slots, 574 KB of an
            //    839 KB file, ~68%.
            //
            // 2. IT WAS LOSSY, AND FIXING (1) ALONE WOULD HAVE ARMED THAT.  Six
            //    fields are WRITTEN below but were not tested here: filterCutoff,
            //    filterHpNorm, filterLpNorm, attack, length and release.  A key
            //    with only its attack or its filter edited would have satisfied
            //    the old predicate and been dropped on save.  That never bit
            //    because the test never passed - correcting the kickMix clause
            //    without adding these would have turned dead code into silent
            //    data loss.
            //
            // THE RULE THIS NOW FOLLOWS: every field written below must be
            // compared here.  If a field is added to the Key element, it belongs
            // in this predicate on the same commit, or edits to it vanish.
            //
            // velCurve BROKE THAT RULE THE OTHER WAY ROUND: it was added to
            // DrumElementParams and never to the Key element at all - not written,
            // not tested here, not read back.  So the drum editor's per-key
            // VELOCITY CURVE never survived a save, in a set or in a .drm: every
            // load put every drum, the kick included, back on a linear curve.
            //
            // Values are the struct defaults in InstrEditPanel.h; the loader
            // resets p.drumKit to a default-constructed DrumKitParams before
            // overlaying, so an omitted key restores byte-identically.
            const bool isDefault = e.sourceKit.isEmpty()
                                && e.sourceRoleId     == 0
                                && e.roundRobinAmount == 0
                                && e.fullLength       == true
                                && e.kickMixEnabled   == false      // was `== true`
                                && e.kickMixVariant   == 0
                                && std::abs (e.gain          - 1.0f)   < 1e-6f
                                && std::abs (e.pitch)                   < 1e-6f
                                && std::abs (e.velCurve)                < 1e-6f
                                && std::abs (e.fxSend)                  < 1e-6f
                                && std::abs (e.kickMixAmount - 50.0f)  < 1e-6f
                                && std::abs (e.filterCutoff  - 1.0f)   < 1e-6f
                                && std::abs (e.filterHpNorm)            < 1e-6f
                                && std::abs (e.filterLpNorm  - 1.0f)   < 1e-6f
                                && std::abs (e.attack        - 0.001f) < 1e-9f
                                && std::abs (e.length        - 100.0f) < 1e-6f
                                && std::abs (e.release       - 0.05f)  < 1e-9f;
            if (isDefault) continue;

            juce::ValueTree key ("Key");
            key.setProperty ("midi",            k,                              nullptr);
            key.setProperty ("sourceKit",       e.sourceKit,                    nullptr);
            key.setProperty ("sourceRoleId",    (int) e.sourceRoleId,           nullptr);
            key.setProperty ("gain",            e.gain,                         nullptr);
            key.setProperty ("pitch",           e.pitch,                        nullptr);
            key.setProperty ("velCurve",        e.velCurve,                     nullptr);
            key.setProperty ("filterCutoff",    e.filterCutoff,                 nullptr);
            key.setProperty ("filterHpNorm",    e.filterHpNorm,                 nullptr);
            key.setProperty ("filterLpNorm",    e.filterLpNorm,                 nullptr);
            key.setProperty ("roundRobinAmt",   e.roundRobinAmount,             nullptr);
            key.setProperty ("attack",          e.attack,                       nullptr);
            key.setProperty ("length",          e.length,                       nullptr);
            key.setProperty ("release",         e.release,                      nullptr);
            key.setProperty ("fxSend",          e.fxSend,                       nullptr);
            key.setProperty ("fullLength",      e.fullLength,                   nullptr);
            key.setProperty ("kickMixEnabled",  e.kickMixEnabled,               nullptr);
            key.setProperty ("kickMixAmount",   e.kickMixAmount,                nullptr);
            key.setProperty ("kickMixVariant",  e.kickMixVariant,               nullptr);
            keys.appendChild (key, nullptr);
        }
        dk.appendChild (keys, nullptr);

        juce::ValueTree fx ("Fx");
        for (int i = 0; i < 10; ++i)
            fx.setProperty ("eq_" + juce::String (i), p.drumKit.fx.eqGainDb[i], nullptr);
        fx.setProperty ("satDrive",      p.drumKit.fx.satDrive,      nullptr);
        fx.setProperty ("chorusSend",    p.drumKit.fx.chorusSend,    nullptr);
        fx.setProperty ("wahSend",       p.drumKit.fx.wahSend,       nullptr);
        fx.setProperty ("phaserSend",    p.drumKit.fx.phaserSend,    nullptr);
        fx.setProperty ("satMix",        p.drumKit.fx.satMix,        nullptr);
        fx.setProperty ("compThreshDb",  p.drumKit.fx.compThreshDb,  nullptr);
        fx.setProperty ("compRatio",     p.drumKit.fx.compRatio,     nullptr);
        fx.setProperty ("compAttackMs",  p.drumKit.fx.compAttackMs,  nullptr);
        fx.setProperty ("compReleaseMs", p.drumKit.fx.compReleaseMs, nullptr);
        fx.setProperty ("compMakeupDb",  p.drumKit.fx.compMakeupDb,  nullptr);
        fx.setProperty ("revSize",       p.drumKit.fx.revSize,       nullptr);
        fx.setProperty ("revDamp",       p.drumKit.fx.revDamp,       nullptr);
        fx.setProperty ("revWet",        p.drumKit.fx.revWet,        nullptr);
        fx.setProperty ("revDry",        p.drumKit.fx.revDry,        nullptr);
        fx.setProperty ("revTail",       p.drumKit.fx.revTail,      nullptr);
        fx.setProperty ("revPreDelay",   p.drumKit.fx.revPreDelay,  nullptr);
        fx.setProperty ("revHpNorm",     p.drumKit.fx.revHpNorm,    nullptr);
        fx.setProperty ("revLpNorm",     p.drumKit.fx.revLpNorm,    nullptr);
        fx.setProperty ("revAlgo",       p.drumKit.fx.revAlgo,      nullptr);
        fx.setProperty ("revWetBase",    p.drumKit.fx.revWetBase,   nullptr);
        fx.setProperty ("delSync",       p.drumKit.fx.delSync,       nullptr);
        fx.setProperty ("delTimeSig",    p.drumKit.fx.delTimeSig,    nullptr);
        fx.setProperty ("delDiv",        p.drumKit.fx.delDiv,        nullptr);
        fx.setProperty ("delTimeMs",     p.drumKit.fx.delTimeMs,     nullptr);
        fx.setProperty ("delFb",         p.drumKit.fx.delFb,         nullptr);
        fx.setProperty ("delWet",        p.drumKit.fx.delWet,        nullptr);
        fx.setProperty ("delDry",        p.drumKit.fx.delDry,       nullptr);
        fx.setProperty ("delWetBase",    p.drumKit.fx.delWetBase,   nullptr);
        fx.setProperty ("pan",           p.drumKit.fx.pan,           nullptr);
        fx.setProperty ("fxWet",         p.drumKit.fx.fxWet,         nullptr);
        writeSweetener (fx, p.drumKit.fx.sweet);
        fx.setProperty ("eqEnabled",   p.drumKit.fx.eqEnabled,   nullptr);
        fx.setProperty ("satEnabled",  p.drumKit.fx.satEnabled,  nullptr);
        fx.setProperty ("compEnabled", p.drumKit.fx.compEnabled, nullptr);
        fx.setProperty ("revEnabled",  p.drumKit.fx.revEnabled,  nullptr);
        fx.setProperty ("delEnabled",  p.drumKit.fx.delEnabled,  nullptr);
        dk.appendChild (fx, nullptr);

        t.appendChild (dk, nullptr);
        return t;
    }

    /** Where a slot payload is coming from.

        The two differ on ONE point — the EQ / REVERB / DELAY insert enables.
        A PRESET file (.ins / .sins / .drm) is a saved SOUND: everything the
        editor can set belongs to it, enables included, or "save as default"
        does not actually save the sound.  A SESSION payload (a saved set, or
        the editor-reopen snapshot) keeps the older rule that those three come
        back off, so a set written when they were blaring does not blare again.

        Everything else round-trips identically either way. */
    enum class SlotLoadContext { Session, Preset };

    inline void loadSlot (const juce::ValueTree& t, SlotParams& p,
                          SlotLoadContext ctx = SlotLoadContext::Session)
    {
        if (! t.isValid()) return;

        #define BLPF(name) p.name = (float) t.getProperty (#name, (double) p.name)
        #define BLPI(name) p.name = (int)   t.getProperty (#name, (int)    p.name)
        #define BLPB(name) p.name = (bool)  t.getProperty (#name, (bool)   p.name)

        BLPF(attack); BLPF(decay); BLPF(sustain); BLPF(release);
        BLPI(ampCurve);   // absent in old state -> keeps default 0 (Exp = prior sound)

        // ── AND THE CONTINUOUS SHAPE ────────────────────────────────────────
        //
        // A file written before this existed has no ampCurveK, so it is DERIVED
        // from the three-way ampCurve it does have — Exp +6, Lin 0, Log -4,
        // which are the exact exponents those three shapes always were.  Old
        // presets therefore sound identical rather than merely similar.
        if (t.hasProperty ("ampCurveK"))
            p.ampCurveK = (float) (double) t.getProperty ("ampCurveK", (double) p.ampCurveK);
        else
            p.ampCurveK = (p.ampCurve == 1) ?  0.0f      // Lin
                        : (p.ampCurve == 2) ? -4.0f      // Log
                                            :  6.0f;     // Exp

        // Written out rather than calling AHDSREnvelope::legacyCurveToK: this
        // file deliberately includes only InstrEditPanel.h, and pulling in the
        // DSP header to reach three constants would tie the save format to the
        // engine for no benefit.  The three numbers are fixed by the old
        // format, not by the envelope.
        BLPF(pan);
        BLPF(filterCutoff); BLPF(filterReson); BLPI(filterType); BLPF(filterKeytrack);
        // Absent in older state -> stays 0, which is OFF. A file written before
        // this existed describes a patch with no variation, and that is exactly
        // what it gets.
        BLPI(variationAmount);
        BLPF(filterHpNorm); BLPF(filterLpNorm);
        BLPF(fEnvA); BLPF(fEnvD); BLPF(fEnvS); BLPF(fEnvR); BLPF(fEnvAmount);
        BLPI(playMode);
        // Mono flags: new multi-select schema, or migrate a legacy single-select
        // monoSubMode (0=HoldStolen 1=RetrigNew 2=RetrigStolen) once.  All three
        // legacy sub-modes fell back to held notes, so Hold-Stolen maps to true.
        if (t.hasProperty ("monoRetrigNew"))
        {
            BLPB(monoHoldStolen); BLPB(monoRetrigNew); BLPB(monoRetrigStolen);
        }
        else if (t.hasProperty ("monoSubMode"))
        {
            const int sub = (int) t.getProperty ("monoSubMode", 0);
            p.monoHoldStolen   = true;
            p.monoRetrigNew    = (sub == 1);
            p.monoRetrigStolen = (sub == 2);
        }
        BLPF(portamentoTime); BLPI(octaveOffset);
        BLPB(noteRangeOn); BLPI(noteRangeLo); BLPI(noteRangeHi);
        BLPF(ampLfoRate);   BLPF(ampLfoDepth);   BLPF(ampLfoDelay);
        BLPF(filtLfoRate);  BLPF(filtLfoDepth);  BLPF(filtLfoDelay);
        BLPB(ampLfoEnabled); BLPB(filtLfoEnabled);
        // Absent in every file written before the glide existed, so all six
        // fall back to the struct defaults - and glideMode's default is OFF,
        // which is what keeps an old set sounding exactly as it was saved.
        BLPI(glideMode); BLPF(glideDepth); BLPF(glideTimeMs);
        BLPF(glideShapeK); BLPI(glideEveryN); BLPI(glideOdds); BLPI(glideVelMin);
        BLPF(pitchLfoRate); BLPF(pitchLfoDepth); BLPF(pitchLfoDelay);
        BLPF(pEnvA); BLPF(pEnvD); BLPF(pEnvS); BLPF(pEnvR); BLPF(pEnvDepth);
        BLPF(reverbSize); BLPF(reverbDamp); BLPF(reverbWet); BLPF(reverbDry);
        // TAIL falls back to SIZE, which is what used to drive the decay — so a
        // set written before the split comes back sounding exactly as saved.
        p.reverbTail = (float) t.getProperty ("reverbTail", (double) p.reverbSize);
        BLPF(reverbHpNorm); BLPF(reverbLpNorm); BLPF(reverbPreDelay);
        BLPF(gainPercent); BLPF(baseUnityDb);
        BLPI(delayTimeSig); BLPI(delayDiv); BLPF(delayFeedback); BLPF(delayWet);
        // BLPF falls back to the value already in p, so a preset written before
        // these existed loads with the struct defaults - dry 1.0, delay base
        // 0.5, reverb base 1.0.  An old REVERB therefore sounds identical; an
        // old DELAY gets the new independent dry, which is the intended change.
        BLPF(delayDry); BLPF(delayWetBase); BLPF(reverbWetBase);
        // Absent in anything written before these existed, so they fall back to
        // the struct defaults - which are exactly the old hard-coded behaviour.
        BLPF(delayDampHz); BLPF(delayHpHz); BLPF(delaySmoothMs);
        // EQ / REVERB / DELAY insert enables.
        //
        // These were the one place a control was written to the file and then
        // thrown away on the way back in — the save side has always stored all
        // three, the loader forced them off unconditionally.  That made a
        // "save as default" a lie for the three effects, and it is why the
        // toggles looked like they saved at random: chorus, wah, phaser, the
        // two LFO enables and click all round-tripped fine, so only these three
        // ever came back wrong.
        //
        // A preset now honours what the file says, because a preset IS the
        // sound.  A session payload keeps the old rule, so a set saved while
        // they happened to be on does not come back blaring.
        if (ctx == SlotLoadContext::Preset)
        {
            BLPB(eqEnabled); BLPB(reverbEnabled); BLPB(delayEnabled);
        }
        else
        {
            p.eqEnabled = p.reverbEnabled = p.delayEnabled = false;
        }
        BLPB(clickEnabled); BLPF(clickVolume); BLPF(clickDecayMs);
        p.clickFilePath = t.getProperty ("clickFilePath", p.clickFilePath).toString();
        p.engineSpec    = t.getProperty ("engineSpec", juce::String()).toString();   // SOUND ENGINE: absent = factory

        BLPB(chorusEnabled); BLPF(chorusRate); BLPF(chorusDepth); BLPF(chorusMix);
        BLPB(wahEnabled); BLPF(wahSensitivity); BLPF(wahRate); BLPF(wahLfoDepth);
        BLPF(wahBaseHz); BLPF(wahQ); BLPF(wahMix);
        BLPB(phaserEnabled); BLPF(phaserRate); BLPF(phaserDepth); BLPF(phaserFeedback); BLPF(phaserMix);

        // ── SWEETENER ────────────────────────────────────────────────────────
        // Absent-means-unchanged throughout, so a state written before the
        // sweetener existed loads with the block simply off rather than
        // resetting the slot.  Unlike eq/reverb/delay this is NOT forced off
        // for a SESSION payload: it is a corrective stage, not a blaring one,
        // and a set that was saved with it on was saved that way on purpose.
        p.velCurve = (float) t.getProperty ("velCurve", (double) p.velCurve);
        readSweetener (t, p.sweet);

        #undef BLPF
        #undef BLPI
        #undef BLPB

        for (int i = 0; i < 5; ++i)
        {
            p.eqGain[i] = (float) t.getProperty ("eqGain_" + juce::String (i), p.eqGain[i]);
            p.eqFreq[i] = (float) t.getProperty ("eqFreq_" + juce::String (i), p.eqFreq[i]);
        }

        // Reset drum-kit fields to defaults before overlaying — entries that
        // are absent from XML must come back as defaults, not the slot's
        // previous values.
        p.drumKit = DrumKitParams();

        auto dk = t.getChildWithName ("DrumKit");
        if (! dk.isValid()) return;

        p.drumKit.lastLoadedKit = dk.getProperty ("lastLoadedKit", juce::String()).toString();
        p.drumKit.edmKit        = dk.getProperty ("edmKit",        juce::String()).toString();

        auto keys = dk.getChildWithName ("Keys");
        for (int i = 0; i < keys.getNumChildren(); ++i)
        {
            auto k = keys.getChild (i);
            const int midi = (int) k.getProperty ("midi", -1);
            if (midi < 0 || midi >= 128) continue;
            auto& e = p.drumKit.keys[(size_t) midi];
            e.sourceKit       = k.getProperty ("sourceKit", juce::String()).toString();
            e.sourceRoleId    = (uint8_t) (int) k.getProperty ("sourceRoleId",   0);
            e.gain            = (float) k.getProperty ("gain",                   1.0f);
            e.pitch           = (float) k.getProperty ("pitch",                  0.0f);
            // Absent = linear, so every set saved before the curve was written
            // loads exactly as it always has.
            e.velCurve        = (float) k.getProperty ("velCurve",               0.0f);
            e.filterCutoff    = (float) k.getProperty ("filterCutoff",           1.0f);
            // Absent = fully open, so a kit saved before the band existed
            // loads with no filtering rather than a closed one.
            e.filterHpNorm    = (float) k.getProperty ("filterHpNorm",           0.0f);
            e.filterLpNorm    = (float) k.getProperty ("filterLpNorm",           1.0f);
            // New key. The old "roundRobinMode" held 0..3 and nothing ever
            // rendered it, so reading it as an amount lands on 0..3% - the
            // "off" it always effectively was.
            e.roundRobinAmount = (int) k.getProperty ("roundRobinAmt",
                                     k.getProperty ("roundRobinMode", 0));
            e.attack          = (float) k.getProperty ("attack",                 0.001f);
            e.length          = (float) k.getProperty ("length",                 100.0f);
            e.release         = (float) k.getProperty ("release",                0.05f);
            e.fxSend          = (float) k.getProperty ("fxSend",                 0.0f);
            e.fullLength      = (bool)  k.getProperty ("fullLength",             true);
            // Absent = OFF, the struct default (InstrEditPanel.h).  This read
            // `true`, so a key saved without the property came back BLENDING the
            // EDM/WOOD kick under the kit's own - the two defaults disagreed.
            e.kickMixEnabled  = (bool)  k.getProperty ("kickMixEnabled",         false);
            e.kickMixAmount   = (float) k.getProperty ("kickMixAmount",          50.0f);
            e.kickMixVariant  = (int)   k.getProperty ("kickMixVariant",         0);
        }

        auto fx = dk.getChildWithName ("Fx");
        if (fx.isValid())
        {
            for (int i = 0; i < 10; ++i)
                p.drumKit.fx.eqGainDb[i] = (float) fx.getProperty ("eq_" + juce::String (i),
                                                                    p.drumKit.fx.eqGainDb[i]);
            p.drumKit.fx.satDrive      = (float) fx.getProperty ("satDrive",      p.drumKit.fx.satDrive);
            p.drumKit.fx.chorusSend    = (float) fx.getProperty ("chorusSend",    p.drumKit.fx.chorusSend);
            p.drumKit.fx.wahSend       = (float) fx.getProperty ("wahSend",       p.drumKit.fx.wahSend);
            p.drumKit.fx.phaserSend    = (float) fx.getProperty ("phaserSend",    p.drumKit.fx.phaserSend);
            p.drumKit.fx.satMix        = (float) fx.getProperty ("satMix",        p.drumKit.fx.satMix);
            p.drumKit.fx.compThreshDb  = (float) fx.getProperty ("compThreshDb",  p.drumKit.fx.compThreshDb);
            p.drumKit.fx.compRatio     = (float) fx.getProperty ("compRatio",     p.drumKit.fx.compRatio);
            p.drumKit.fx.compAttackMs  = (float) fx.getProperty ("compAttackMs",  p.drumKit.fx.compAttackMs);
            p.drumKit.fx.compReleaseMs = (float) fx.getProperty ("compReleaseMs", p.drumKit.fx.compReleaseMs);
            p.drumKit.fx.compMakeupDb  = (float) fx.getProperty ("compMakeupDb",  p.drumKit.fx.compMakeupDb);
            p.drumKit.fx.revSize       = (float) fx.getProperty ("revSize",       p.drumKit.fx.revSize);
            p.drumKit.fx.revDamp       = (float) fx.getProperty ("revDamp",       p.drumKit.fx.revDamp);
            p.drumKit.fx.revWet        = (float) fx.getProperty ("revWet",        p.drumKit.fx.revWet);
            p.drumKit.fx.revDry        = (float) fx.getProperty ("revDry",        p.drumKit.fx.revDry);
            p.drumKit.fx.revTail = (float) fx.getProperty ("revTail", p.drumKit.fx.revTail);
            p.drumKit.fx.revPreDelay = (float) fx.getProperty ("revPreDelay", p.drumKit.fx.revPreDelay);
            p.drumKit.fx.revHpNorm = (float) fx.getProperty ("revHpNorm", p.drumKit.fx.revHpNorm);
            p.drumKit.fx.revLpNorm = (float) fx.getProperty ("revLpNorm", p.drumKit.fx.revLpNorm);
            p.drumKit.fx.revWetBase = (float) fx.getProperty ("revWetBase", p.drumKit.fx.revWetBase);
            p.drumKit.fx.revAlgo = (int) fx.getProperty ("revAlgo", p.drumKit.fx.revAlgo);
            p.drumKit.fx.delDry = (float) fx.getProperty ("delDry", p.drumKit.fx.delDry);
            p.drumKit.fx.delWetBase = (float) fx.getProperty ("delWetBase", p.drumKit.fx.delWetBase);
            p.drumKit.fx.delSync       = (bool)  fx.getProperty ("delSync",       p.drumKit.fx.delSync);
            p.drumKit.fx.delTimeSig    = (int)   fx.getProperty ("delTimeSig",    p.drumKit.fx.delTimeSig);
            p.drumKit.fx.delDiv        = (int)   fx.getProperty ("delDiv",        p.drumKit.fx.delDiv);
            p.drumKit.fx.delTimeMs     = (float) fx.getProperty ("delTimeMs",     p.drumKit.fx.delTimeMs);
            p.drumKit.fx.delFb         = (float) fx.getProperty ("delFb",         p.drumKit.fx.delFb);
            p.drumKit.fx.delWet        = (float) fx.getProperty ("delWet",        p.drumKit.fx.delWet);
            p.drumKit.fx.pan           = (float) fx.getProperty ("pan",           p.drumKit.fx.pan);
            p.drumKit.fx.fxWet         = (float) fx.getProperty ("fxWet",         p.drumKit.fx.fxWet);
            readSweetener (fx, p.drumKit.fx.sweet);
            p.drumKit.fx.eqEnabled   = (bool) fx.getProperty ("eqEnabled",   p.drumKit.fx.eqEnabled);
            p.drumKit.fx.satEnabled  = (bool) fx.getProperty ("satEnabled",  p.drumKit.fx.satEnabled);
            p.drumKit.fx.compEnabled = (bool) fx.getProperty ("compEnabled", p.drumKit.fx.compEnabled);
            p.drumKit.fx.revEnabled  = (bool) fx.getProperty ("revEnabled",  p.drumKit.fx.revEnabled);
            p.drumKit.fx.delEnabled  = (bool) fx.getProperty ("delEnabled",  p.drumKit.fx.delEnabled);
        }
    }

    // ── Standalone drum-kit document (user-kit file: *.bdk) ───────────────────
    //
    // Serialises a whole DrumKitParams on its own (sources + per-key params +
    // FX bus) so users can save a customised kit to a file and reload it into
    // any slot.  Same property layout as the embedded DrumKit child above.
    inline juce::ValueTree saveDrumKit (const DrumKitParams& dk)
    {
        juce::ValueTree root ("BetelDrumKit");
        root.setProperty ("lastLoadedKit", dk.lastLoadedKit, nullptr);
        if (dk.edmKit.isNotEmpty())                                // EDM KIT cells
            root.setProperty ("edmKit", dk.edmKit, nullptr);

        juce::ValueTree keys ("Keys");
        for (int k = 0; k < 128; ++k)
        {
            const auto& e = dk.keys[(size_t) k];
            const bool isDefault = e.sourceKit.isEmpty() && e.sourceRoleId == 0
                                && std::abs (e.gain - 1.0f) < 1e-6f
                                && std::abs (e.pitch)        < 1e-6f
                                && std::abs (e.fxSend)       < 1e-6f
                                && e.fullLength == true
                                && e.roundRobinAmount == 0
                                && e.kickMixEnabled == true
                                && std::abs (e.kickMixAmount - 50.0f) < 1e-6f
                                && e.kickMixVariant == 0;
            if (isDefault) continue;

            juce::ValueTree key ("Key");
            key.setProperty ("midi",           k,                nullptr);
            key.setProperty ("sourceKit",      e.sourceKit,      nullptr);
            key.setProperty ("sourceRoleId",   (int) e.sourceRoleId, nullptr);
            key.setProperty ("gain",           e.gain,           nullptr);
            key.setProperty ("pitch",          e.pitch,          nullptr);
            key.setProperty ("filterCutoff",   e.filterCutoff,   nullptr);
            key.setProperty ("roundRobinAmt", e.roundRobinAmount, nullptr);
            key.setProperty ("attack",         e.attack,         nullptr);
            key.setProperty ("length",         e.length,         nullptr);
            key.setProperty ("release",        e.release,        nullptr);
            key.setProperty ("fxSend",         e.fxSend,         nullptr);
            key.setProperty ("fullLength",     e.fullLength,     nullptr);
            key.setProperty ("kickMixEnabled", e.kickMixEnabled, nullptr);
            key.setProperty ("kickMixAmount",  e.kickMixAmount,  nullptr);
            key.setProperty ("kickMixVariant", e.kickMixVariant, nullptr);
            keys.appendChild (key, nullptr);
        }
        root.appendChild (keys, nullptr);

        juce::ValueTree fx ("Fx");
        for (int i = 0; i < 10; ++i)
            fx.setProperty ("eq_" + juce::String (i), dk.fx.eqGainDb[i], nullptr);
        fx.setProperty ("satDrive",      dk.fx.satDrive,      nullptr);
        fx.setProperty ("chorusSend",    dk.fx.chorusSend,    nullptr);
        fx.setProperty ("wahSend",       dk.fx.wahSend,       nullptr);
        fx.setProperty ("phaserSend",    dk.fx.phaserSend,    nullptr);
        fx.setProperty ("satMix",        dk.fx.satMix,        nullptr);
        fx.setProperty ("compThreshDb",  dk.fx.compThreshDb,  nullptr);
        fx.setProperty ("compRatio",     dk.fx.compRatio,     nullptr);
        fx.setProperty ("compAttackMs",  dk.fx.compAttackMs,  nullptr);
        fx.setProperty ("compReleaseMs", dk.fx.compReleaseMs, nullptr);
        fx.setProperty ("compMakeupDb",  dk.fx.compMakeupDb,  nullptr);
        fx.setProperty ("revSize",       dk.fx.revSize,       nullptr);
        fx.setProperty ("revDamp",       dk.fx.revDamp,       nullptr);
        fx.setProperty ("revWet",        dk.fx.revWet,        nullptr);
        fx.setProperty ("revDry",        dk.fx.revDry,        nullptr);
        fx.setProperty ("revTail",       dk.fx.revTail,      nullptr);
        fx.setProperty ("revPreDelay",   dk.fx.revPreDelay,  nullptr);
        fx.setProperty ("revHpNorm",     dk.fx.revHpNorm,    nullptr);
        fx.setProperty ("revLpNorm",     dk.fx.revLpNorm,    nullptr);
        fx.setProperty ("revAlgo",       dk.fx.revAlgo,      nullptr);
        fx.setProperty ("revWetBase",    dk.fx.revWetBase,   nullptr);
        fx.setProperty ("delSync",       dk.fx.delSync,       nullptr);
        fx.setProperty ("delTimeSig",    dk.fx.delTimeSig,    nullptr);
        fx.setProperty ("delDiv",        dk.fx.delDiv,        nullptr);
        fx.setProperty ("delTimeMs",     dk.fx.delTimeMs,     nullptr);
        fx.setProperty ("delFb",         dk.fx.delFb,         nullptr);
        fx.setProperty ("delWet",        dk.fx.delWet,        nullptr);
        fx.setProperty ("delDry",        dk.fx.delDry,       nullptr);
        fx.setProperty ("delWetBase",    dk.fx.delWetBase,   nullptr);
        fx.setProperty ("pan",           dk.fx.pan,           nullptr);
        fx.setProperty ("fxWet",         dk.fx.fxWet,         nullptr);
        writeSweetener (fx, dk.fx.sweet);
        fx.setProperty ("eqEnabled",   dk.fx.eqEnabled,   nullptr);
        fx.setProperty ("satEnabled",  dk.fx.satEnabled,  nullptr);
        fx.setProperty ("compEnabled", dk.fx.compEnabled, nullptr);
        fx.setProperty ("revEnabled",  dk.fx.revEnabled,  nullptr);
        fx.setProperty ("delEnabled",  dk.fx.delEnabled,  nullptr);
        root.appendChild (fx, nullptr);

        return root;
    }

    inline bool loadDrumKit (const juce::ValueTree& root, DrumKitParams& dk)
    {
        if (! root.isValid() || ! root.hasType ("BetelDrumKit")) return false;

        dk = DrumKitParams();
        dk.lastLoadedKit = root.getProperty ("lastLoadedKit", juce::String()).toString();
        dk.edmKit        = root.getProperty ("edmKit",        juce::String()).toString();

        auto keys = root.getChildWithName ("Keys");
        for (int i = 0; i < keys.getNumChildren(); ++i)
        {
            auto k = keys.getChild (i);
            const int midi = (int) k.getProperty ("midi", -1);
            if (midi < 0 || midi >= 128) continue;
            auto& e = dk.keys[(size_t) midi];
            e.sourceKit      = k.getProperty ("sourceKit", juce::String()).toString();
            e.sourceRoleId   = (uint8_t) (int) k.getProperty ("sourceRoleId",   0);
            e.gain           = (float) k.getProperty ("gain",           1.0f);
            e.pitch          = (float) k.getProperty ("pitch",          0.0f);
            e.filterCutoff   = (float) k.getProperty ("filterCutoff",   1.0f);
            e.filterHpNorm   = (float) k.getProperty ("filterHpNorm",   0.0f);
            e.filterLpNorm   = (float) k.getProperty ("filterLpNorm",   1.0f);
            e.roundRobinAmount = (int) k.getProperty ("roundRobinAmt",
                                     k.getProperty ("roundRobinMode", 0));
            e.attack         = (float) k.getProperty ("attack",         0.001f);
            e.length         = (float) k.getProperty ("length",         100.0f);
            e.release        = (float) k.getProperty ("release",        0.05f);
            e.fxSend         = (float) k.getProperty ("fxSend",         0.0f);
            e.fullLength     = (bool)  k.getProperty ("fullLength",     true);
            e.kickMixEnabled = (bool)  k.getProperty ("kickMixEnabled", false);   // absent = OFF, as above
            e.kickMixAmount  = (float) k.getProperty ("kickMixAmount",  50.0f);
            e.kickMixVariant = (int)   k.getProperty ("kickMixVariant", 0);
        }

        auto fx = root.getChildWithName ("Fx");
        if (fx.isValid())
        {
            for (int i = 0; i < 10; ++i)
                dk.fx.eqGainDb[i] = (float) fx.getProperty ("eq_" + juce::String (i), dk.fx.eqGainDb[i]);
            dk.fx.satDrive      = (float) fx.getProperty ("satDrive",      dk.fx.satDrive);
            dk.fx.chorusSend    = (float) fx.getProperty ("chorusSend",    dk.fx.chorusSend);
            dk.fx.wahSend       = (float) fx.getProperty ("wahSend",       dk.fx.wahSend);
            dk.fx.phaserSend    = (float) fx.getProperty ("phaserSend",    dk.fx.phaserSend);
            dk.fx.satMix        = (float) fx.getProperty ("satMix",        dk.fx.satMix);
            dk.fx.compThreshDb  = (float) fx.getProperty ("compThreshDb",  dk.fx.compThreshDb);
            dk.fx.compRatio     = (float) fx.getProperty ("compRatio",     dk.fx.compRatio);
            dk.fx.compAttackMs  = (float) fx.getProperty ("compAttackMs",  dk.fx.compAttackMs);
            dk.fx.compReleaseMs = (float) fx.getProperty ("compReleaseMs", dk.fx.compReleaseMs);
            dk.fx.compMakeupDb  = (float) fx.getProperty ("compMakeupDb",  dk.fx.compMakeupDb);
            dk.fx.revSize       = (float) fx.getProperty ("revSize",       dk.fx.revSize);
            dk.fx.revDamp       = (float) fx.getProperty ("revDamp",       dk.fx.revDamp);
            dk.fx.revWet        = (float) fx.getProperty ("revWet",        dk.fx.revWet);
            dk.fx.revDry        = (float) fx.getProperty ("revDry",        dk.fx.revDry);
            dk.fx.revTail = (float) fx.getProperty ("revTail", dk.fx.revTail);
            dk.fx.revPreDelay = (float) fx.getProperty ("revPreDelay", dk.fx.revPreDelay);
            dk.fx.revHpNorm = (float) fx.getProperty ("revHpNorm", dk.fx.revHpNorm);
            dk.fx.revLpNorm = (float) fx.getProperty ("revLpNorm", dk.fx.revLpNorm);
            dk.fx.revWetBase = (float) fx.getProperty ("revWetBase", dk.fx.revWetBase);
            dk.fx.revAlgo = (int) fx.getProperty ("revAlgo", dk.fx.revAlgo);
            dk.fx.delDry = (float) fx.getProperty ("delDry", dk.fx.delDry);
            dk.fx.delWetBase = (float) fx.getProperty ("delWetBase", dk.fx.delWetBase);
            dk.fx.delSync       = (bool)  fx.getProperty ("delSync",       dk.fx.delSync);
            dk.fx.delTimeSig    = (int)   fx.getProperty ("delTimeSig",    dk.fx.delTimeSig);
            dk.fx.delDiv        = (int)   fx.getProperty ("delDiv",        dk.fx.delDiv);
            dk.fx.delTimeMs     = (float) fx.getProperty ("delTimeMs",     dk.fx.delTimeMs);
            dk.fx.delFb         = (float) fx.getProperty ("delFb",         dk.fx.delFb);
            dk.fx.delWet        = (float) fx.getProperty ("delWet",        dk.fx.delWet);
            dk.fx.pan           = (float) fx.getProperty ("pan",           dk.fx.pan);
            dk.fx.fxWet         = (float) fx.getProperty ("fxWet",         dk.fx.fxWet);
            readSweetener (fx, dk.fx.sweet);
            dk.fx.eqEnabled   = (bool) fx.getProperty ("eqEnabled",   dk.fx.eqEnabled);
            dk.fx.satEnabled  = (bool) fx.getProperty ("satEnabled",  dk.fx.satEnabled);
            dk.fx.compEnabled = (bool) fx.getProperty ("compEnabled", dk.fx.compEnabled);
            dk.fx.revEnabled  = (bool) fx.getProperty ("revEnabled",  dk.fx.revEnabled);
            dk.fx.delEnabled  = (bool) fx.getProperty ("delEnabled",  dk.fx.delEnabled);
        }
        return true;
    }
}
