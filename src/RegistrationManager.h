

#pragma once

#include <JuceHeader.h>
#include <atomic>

//==============================================================================
// RegistrationManager — machine-locked serial + demo mode.
//
// Ported from OnStage, which has used the same scheme in the field without
// being defeated.  Three things differ and all three matter:
//
//   * the LICENCE FILE lives in Grex's own plugin folder, mirrored to AppData,
//     under names no other Fanan product uses, so the two cannot unlock each
//     other from one key;
//   * the SERIAL is Grex's own HMAC over its own secret — see below;
//   * the demo behaviour is Grex's (18 s playing / 3 s silent), because an
//     arranger going silent mid-song is a different experience from a host
//     going silent.
//
// ── WHAT THIS IS AND IS NOT ─────────────────────────────────────────────────
//
// It is a machine-locked key check: the user reads an ID off the screen, the
// website computes a serial from it, they type it back, and it is stored.  A
// key from one machine does nothing on another.
//
// THE SERIAL IS NOW A KEYED HASH, NOT A FORMULA:
//
//     serial = HMAC-SHA256 (secret, machineID as ASCII) -> first 8 bytes
//              -> big-endian uint64 -> mod 1e9 -> 9 digits
//
// It replaced a LINEAR formula, 6*id + 34977, which two known (id, serial)
// pairs solved with a pencil.  That whole class of attack is gone: reversing
// HMAC-SHA256 means breaking SHA-256, so a thousand known pairs reveal nothing.
// The website generator computes the identical function, which is what keeps
// reissue instant — no database, no accounts, no revocation state, and a
// customer who reformats gets a new serial in seconds.
//
// The legacy formula and the dual-accept path that briefly kept it alive have
// both been DELETED.  There is one door.
//
// ── WHAT IT STILL IS NOT ────────────────────────────────────────────────────
//
// This stops KEYGENS, not patches.  Anyone with a debugger can still find the
// comparison in tryRegister and branch around it, exactly as before — and the
// secret, though not a constant in this binary, is recoverable by anyone
// willing to follow LicenceCarrier.h into the carrier audio file.  That is
// obfuscation, and it is honest about being obfuscation.
//
// What changed is that nobody can publish a generator.  A keygen is written
// once and every user self-serves; a patch has to be redone per build and
// produces a modified binary.  That difference is the whole return on this
// work.
//
// If it ever needs to be stronger, the next step is asymmetric: sign the
// machine ID with a private key that never ships and verify with an embedded
// public key.  The cost is a ~104-character licence instead of nine digits,
// which is why it was not done now.
//==============================================================================
class RegistrationManager
{
public:
    static RegistrationManager& getInstance()
    {
        static RegistrationManager instance;
        return instance;
    }

    /** Read the licence file and validate it.

        CALL FROM THE PROCESSOR CONSTRUCTOR, not from the editor.  It used to
        run only when the plugin WINDOW was built, which meant an offline
        bounce, a headless render, or simply reopening a project without
        clicking the plugin left `registered` false on a machine that owns a
        licence - and muted a paying user for three seconds out of every
        twenty-one.  The processor always exists; the editor does not. */
    void checkRegistration();

    bool isRegistered() const { return registered.load(); }

    /** Validate a typed serial and, on success, write the licence file. */
    bool tryRegister (const juce::String& serialInput);

    /** Drop the cached carrier key so the next verification re-reads it from
        the CURRENT root folder.  Call after the root moves - a stale "there is
        no carrier" answer would otherwise make registration impossible for the
        rest of the session. */
    static void forgetCarrierKey();

    /** The number the user reads off the screen and quotes to you. */
    juce::String getMachineIDString();
    int          getMachineIDNumber();

    /** True while the demo is in its silent window.  Read from the AUDIO
        thread, so it is an atomic and nothing else. */
    bool isDemoSilenceActive() const { return demoSilenceActive.load(); }

    /** Advance the demo timer.  Called from processBlock; cheap and lock-free. */
    void updateDemoMode();

private:
    RegistrationManager() = default;
    ~RegistrationManager() = default;

    // ATOMIC, for the same reason demoSilenceActive beside it is.
    //
    // It is WRITTEN on the message thread - at startup by checkRegistration,
    // and again the moment the user types a serial into the SETTINGS page -
    // and READ on the AUDIO thread, every block, by updateDemoMode.  A plain
    // bool across those two threads is a data race: formally undefined, and in
    // practice the kind that surfaces as the audio thread not noticing a
    // registration for an unbounded number of blocks because the value is
    // sitting in another core's store buffer.  Nobody wants to debug "it went
    // quiet for a while after I registered".
    std::atomic<bool> registered { false };

    std::atomic<bool> demoSilenceActive { false };

    // Touched ONLY by updateDemoMode, i.e. only on the audio thread, so these
    // two stay plain.  Do not read them from the UI.
    double lastSilenceToggleTime = 0.0;
    bool   inSilencePeriod       = false;

    /** The HMAC serial: true when `cleanInput` matches this machine's expected
        nine digits.  The secret comes from the carrier audio file, is read and
        cached on first use, and a missing or damaged carrier means FALSE - this
        never falls back to anything. */
    bool hmacSerialMatches (const juce::String& cleanInput);

    /** Nine digits, matching the website generator's zero-padded output.
        Changing this breaks every serial ever issued. */
    static constexpr int kSerialDigits = 9;


    // STATIC, and it has to be.
    //
    // getMachineIDNumber caches its result in a function-local static, and the
    // lambda that initialises it takes no capture - a capture-less lambda has no
    // `this`, so it cannot call a non-static member.  MSVC says so in as many
    // words: C4573 "requires the compiler to capture 'this' but the current
    // default capture mode does not allow it", followed by C2352.
    //
    // Capturing `this` would compile and would be wrong: a function-local static
    // is initialised ONCE, by whichever call arrives first, so the cached value
    // would silently belong to that one instance forever.  Harmless while this
    // is a singleton and a trap the day it stops being one.
    //
    // Making it static is the honest fix rather than a workaround: it reads the
    // volume serial / platform UUID straight from the OS and touches no member
    // state at all, so it never needed an object.  Defined WITHOUT the `static`
    // keyword in the .cpp, as the language requires.
    static juce::String getSystemVolumeSerial();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegistrationManager)
};




