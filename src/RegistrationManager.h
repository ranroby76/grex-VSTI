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
//   * the SERIAL FORMULA is Grex's own — see calculateExpectedSerial();
//   * the demo behaviour is Grex's (18 s playing / 3 s silent), because an
//     arranger going silent mid-song is a different experience from a host
//     going silent.
//
// ── WHAT THIS IS AND IS NOT ─────────────────────────────────────────────────
//
// It is a machine-locked key check: the user reads an ID off the screen, you
// compute a serial from it, they type it back, and it is stored.  That stops
// casual sharing — a key from one machine does nothing on another — and it is
// exactly what most boutique plugins ship.
//
// It is NOT cryptography, and the code should not pretend otherwise.  The
// formula below is a LINEAR function: expand it and it reduces to
// serial = 6*id + 34977.  Anyone holding two (id, serial) pairs can solve for
// it with a pencil, and anyone with a debugger can find the comparison in
// tryRegister and branch around it.  What actually protects a product like this
// is that it is niche enough not to be worth a cracker's afternoon — not the
// arithmetic.  Worth knowing before betting a launch on it.
//
// If that ever stops being true the upgrade path is a signature, not a longer
// formula: sign the machine ID with a private key you keep, verify with the
// public key embedded here.  Then two known pairs reveal nothing, because there
// is nothing to solve.
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

    long long calculateExpectedSerial();

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