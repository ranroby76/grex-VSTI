

#include "RegistrationManager.h"
#include "GlobalMacros.h"   // Betel::GrexPaths - one root for every file we own
#include "HmacSha256.h"      // Betel::serialForMachineId - the new serial
#include "LicenceCarrier.h"
#include <cstring>       // std::memset - clearing the cached carrier key  // Betel::LicenceCarrier - the secret, out of the binary

#if JUCE_WINDOWS
 #include <windows.h>
#endif

#if JUCE_LINUX
 #include <fstream>
 #include <string>
#endif

#if JUCE_MAC
 #include <cstdio>
#endif

//==============================================================================
namespace
{
    // ── WHERE THE LICENCE LIVES, AND WHY IT LIVES IN TWO PLACES ─────────────
    //
    // PRIMARY: the plugin's own folder.  BetelFolderManager already roots that
    // at Documents/Grex VSTI, so it is user-writable by construction - no
    // Program Files permission problem - and the licence sits beside the sets
    // and styles rather than in a second location nobody backs up.
    //
    // NOTE the path is root() itself, NOT root()/"Grex VSTI": the root IS the
    // Grex VSTI folder, and nesting a second one inside it was a mistake in the
    // first draft of this file.
    //
    // THERE IS ONLY ONE LICENCE FILE, and it lives in Documents\Fanan.
    //
    // NOT in the Grex VSTI folder: that one is user-relocatable (see the folder
    // locator) and people move, clean and reinstall it, which would take a paid
    // registration with it.  NOT in AppData either - it is hidden, users cannot
    // find it to back it up or clear it, and an uninstaller can wipe it.
    // Documents is visible, stable across a reinstall, and somewhere a customer
    // can actually be pointed at over email.
    //
    // The folder is shared across Fanan products on purpose - each writes its
    // own filename, so a second product cannot unlock this one from its key.
    //
    // Note none of this weakens anything, because THE FILE IS NEVER TRUSTED:
    // checkRegistration re-derives the expected serial from this machine's ID
    // and compares, so a licence file copied anywhere - another folder, another
    // drive, another computer - is inert unless the machine agrees.  The file
    // travels; the authority does not.
    juce::File grexLicencePrimary()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Fanan")
                   .getChildFile ("grex_license.key");
    }

    // ── THE CARRIER KEY CACHE ────────────────────────────────────────────────
    //
    // File scope rather than a function-local static, so forgetCarrierKey() can
    // clear it.  It HAS to be clearable: the carrier lives under
    // GrexPaths::root(), and the root MOVES when the user picks a folder with
    // LOCATE SYSTEM FOLDER.
    //
    // Without that, a clean install dead-ends.  The constructor rescans before
    // the root exists, checkRegistration then finds no carrier, `carrierValid`
    // latches false - and after the user locates the folder the carrier is
    // sitting right there while registration keeps failing, until the plugin is
    // reloaded.  Exactly the shape of the style-library bug that
    // rescanFromFolderManager had.
    //
    // Message thread only: checkRegistration runs from the processor
    // constructor and tryRegister from a button press.
    bool    carrierLoaded = false;
    bool    carrierValid  = false;
    uint8_t carrierKey[Betel::LicenceCarrier::kKeyBytes] = {};

}

//==============================================================================
void RegistrationManager::checkRegistration()
{
    // The stored serial is re-VALIDATED, never trusted: it is checked against
    // this machine's own ID exactly as a typed one is.  So a licence file copied
    // to another computer does nothing there.
    const auto f = grexLicencePrimary();

    if (f.existsAsFile())
    {
        const juce::String savedSerial = f.loadFileAsString().trim();

        if (savedSerial.isNotEmpty() && tryRegister (savedSerial))
        {
            registered.store (true);
            return;   // tryRegister has already re-written the file
        }
    }

    registered.store (false);
}

//==============================================================================
void RegistrationManager::forgetCarrierKey()
{
    // Called whenever the root folder moves, so the next verification re-reads
    // the carrier from the NEW location instead of trusting an answer derived
    // from the old one - including a "there is no carrier" answer.
    carrierLoaded = false;
    carrierValid  = false;
    std::memset (carrierKey, 0, sizeof (carrierKey));
}

//==============================================================================
bool RegistrationManager::tryRegister (const juce::String& serialInput)
{
    try
    {
        // ── STRIP FIRST, THEN VALIDATE.  THE ORDER WAS THE BUG ───────────────
        //
        // This used to run containsOnly BEFORE removeCharacters, so a serial
        // pasted with an internal space - which is exactly how a number lands
        // when it has been read out over the phone, copied from an email, or
        // typed by someone grouping the digits - failed the character test and
        // was rejected as invalid.  trim() only ever removed the outer edges,
        // so the one whitespace that mattered was the one that survived.
        //
        // Tabs and non-breaking spaces go too: a paste from a web page or a
        // spreadsheet cell carries them, and the user cannot see any of it.
        // The check that actually protects anything is the comparison against
        // hmacSerialMatches() below; this stage exists only to reject
        // obvious rubbish early, so it should not be the thing that rejects a
        // correct key.
        juce::String cleanInput = serialInput.trim()
                                             .removeCharacters (" \t\r\n")
                                             .removeCharacters (juce::String::charToString (
                                                 (juce::juce_wchar) 0x00A0));

        if (cleanInput.isEmpty() || ! cleanInput.containsOnly ("0123456789-"))
            return false;

        // HMAC ONLY.  The legacy linear formula - 6*ID + 34977, which two known
        // (id, serial) pairs solve with a pencil - is gone, along with the
        // dual-accept path that briefly kept it alive.  Removed at Rob's request
        // while he is the only user, which is the one moment it costs nothing:
        // every serial issued from here on is an HMAC serial, and there is no
        // second door to leave open.
        if (! hmacSerialMatches (cleanInput))
            return false;

        // A write failure is deliberately NOT fatal.  The serial has already
        // been verified against this machine, so the session is legitimately
        // registered whether or not the file lands; failing here would refuse a
        // valid customer because a folder happened to be read-only.  The only
        // cost of a failed write is that they retype the serial next launch.
        {
            const auto f = grexLicencePrimary();
            auto folder  = f.getParentDirectory();

            if (! folder.exists())
                folder.createDirectory();

            f.replaceWithText (cleanInput);
        }

        registered.store (true);
        return true;
    }
    catch (...)
    {
        // A malformed serial must fail CLOSED.  Anything thrown here leaves
        // `registered` as it was, which for a fresh session is false.
        return false;
    }
}

//==============================================================================
juce::String RegistrationManager::getMachineIDString()
{
    return juce::String (getMachineIDNumber());
}

int RegistrationManager::getMachineIDNumber()
{
    // ── CACHED, AND ON macOS THAT IS NOT AN OPTIMISATION ─────────────────────
    //
    // The Windows path is a single GetVolumeInformationW call and costs nothing.
    // The macOS path SHELLS OUT: popen() forks a process, runs ioreg, pipes it
    // through awk and tr, and waits.  That is tens of milliseconds, and it was
    // being paid several times over - hmacSerialMatches calls this,
    // tryRegister calls hmacSerialMatches, and checkRegistration calls
    // tryRegister once per licence location.
    //
    // It matters more now than it did: checkRegistration has moved into the
    // PROCESSOR constructor so a window-less session is still licensed, which
    // means this now runs during plugin instantiation - i.e. during the host's
    // startup scan, once per instance, on the message thread.  Two or three
    // forked shells there is a slow-loading plugin for no reason.
    //
    // The volume serial / platform UUID cannot change while the process lives,
    // so one read is all there ever was to do.  Function-local static, so it is
    // initialised exactly once and thread-safely (C++11 magic statics) even if
    // the SETTINGS page and the constructor ever raced.
    static const int cachedId = []
    {
        juce::String hex = getSystemVolumeSerial();

        if (hex.length() < 5)
            hex = hex.paddedRight ('0', 5);

        hex = hex.substring (0, 5);

        juce::String numericStr;

        for (int i = 0; i < hex.length(); ++i)
        {
            const juce::juce_wchar c = hex[i];
            char val = '0';

            switch (c)
            {
                case 'A': val = '1'; break;  case 'B': val = '2'; break;  case 'C': val = '3'; break;
                case 'D': val = '4'; break;  case 'E': val = '5'; break;  case 'F': val = '6'; break;
                case 'G': val = '7'; break;  case 'H': val = '8'; break;  case 'I': val = '9'; break;
                case 'J': val = '0'; break;  case 'K': val = '2'; break;  case 'L': val = '3'; break;
                case 'M': val = '4'; break;  case 'N': val = '5'; break;  case 'O': val = '6'; break;
                case 'P': val = '7'; break;  case '1': val = '8'; break;  case '2': val = '9'; break;
                case '3': val = '2'; break;  case '4': val = '1'; break;  case '5': val = '3'; break;
                case '6': val = '4'; break;  case '7': val = '5'; break;  case '8': val = '6'; break;
                case '9': val = '7'; break;  case '0': val = '8'; break;
                case 'Q': val = '8'; break;  case 'R': val = '9'; break;  case 'S': val = '2'; break;
                case 'T': val = '1'; break;  case 'U': val = '2'; break;  case 'V': val = '3'; break;
                case 'W': val = '4'; break;  case 'X': val = '5'; break;  case 'Y': val = '6'; break;
                case 'Z': val = '7'; break;
                default:  val = '0'; break;
            }

            numericStr += val;
        }

        if (numericStr.isEmpty())
            return 12345;

        return numericStr.getIntValue();
    }();

    // The substitution table above is deliberately NOT a hex conversion: it maps
    // every letter and digit onto a digit, so an ID stays five characters
    // whatever the platform hands back, and two different machines have to
    // collide on all five to share a key.
    return cachedId;
}

//==============================================================================
//==============================================================================
// THE HMAC SERIAL — what every NEW licence uses.
//
//     serial = HMAC-SHA256 (secret, machineID as ASCII) -> first 8 bytes
//              -> big-endian uint64 -> mod 1e9 -> 9 digits
//
// Two known (id, serial) pairs now reveal NOTHING, because reversing it means
// breaking SHA-256.  That is the whole point of the change; everything else -
// the typed nine digits, the instant reissue after a reformat, no database, no
// internet - is deliberately identical to before.
//
// THE SECRET IS NOT IN THIS BINARY.  It is read from the carrier audio file,
// which ships as an ordinary asset.  That is obfuscation and not cryptography
// (see LicenceCarrier.h), but it is the same trade OnStage's MIDI carrier made
// and it keeps the key out of `strings` and out of a build diff.
//
// FAILS CLOSED.  If the carrier is missing or damaged, this returns false and
// nothing verifies.  A silently wrong key would mean "every serial rejected"
// with nothing to point at - which is why the carrier carries an integrity tag
// and why this does not fall back to anything.
//==============================================================================
bool RegistrationManager::hmacSerialMatches (const juce::String& cleanInput)
{
    // The carrier is read ONCE and the key cached: checkRegistration runs from
    // the processor constructor, and a plugin instantiated per track would
    // otherwise re-read a megabyte of WAV for every one of them.  The cache is
    // cleared by forgetCarrierKey() whenever the root folder moves.
    if (! carrierLoaded)
    {
        carrierLoaded = true;

        // GrexPaths::root() IS the "Grex VSTI" folder - sounds_gm, sfz and the
        // licence key all sit directly inside it, and the folder manager
        // defaults it to ~/Documents/Grex VSTI.  So the carrier is a plain
        // top-level file there, beside the library it ships with.
        const auto carrier = Betel::GrexPaths::root()
                                 .getChildFile ("grex_ambience.wav");

        juce::MemoryBlock mb;
        if (carrier.existsAsFile() && carrier.loadFileAsData (mb) && mb.getSize() > 44)
            carrierValid = Betel::LicenceCarrier::extractKey (
                               static_cast<const uint8_t*> (mb.getData()),
                               mb.getSize(), carrierKey);
    }

    if (! carrierValid)
        return false;

    // The message bytes must match the website generator EXACTLY: trimmed plain
    // ASCII decimal, no zero padding, no whitespace, no newline.  A single stray
    // byte here produces a completely different serial and every issued key is
    // rejected, with the numbers looking equally random either way.
    const juce::String machineId = getMachineIDString().trim();
    const std::string  msg (machineId.toRawUTF8());

    const uint64_t expected =
        Betel::serialForMachineId (carrierKey, Betel::LicenceCarrier::kKeyBytes, msg);

    // Compare as TEXT, zero-padded to nine digits, so a serial the user typed
    // with its leading zero intact ("012345678") matches.  Comparing numerically
    // would work too, but only by accident of the leading zero being dropped on
    // both sides - and it would break the moment the digit count changed.
    const juce::String expectedText =
        juce::String (expected).paddedLeft ('0', kSerialDigits);

    return cleanInput == expectedText;
}
//==============================================================================
juce::String RegistrationManager::getSystemVolumeSerial()
{
#if JUCE_WINDOWS
    DWORD serialNum = 0;

    if (GetVolumeInformationW (L"C:\\", nullptr, 0, &serialNum, nullptr, nullptr, nullptr, 0))
        return juce::String::toHexString ((int) serialNum).toUpperCase();

    return "00000";

#elif JUCE_LINUX
    std::ifstream file ("/etc/machine-id");

    if (file.is_open())
    {
        std::string line;
        if (std::getline (file, line))
            return juce::String (line).substring (0, 8).toUpperCase();
    }

    return "LINUX01";

#else
    // macOS — IOPlatformUUID read directly rather than through a JUCE helper,
    // so it does not move when JUCE does.
    juce::String uuid;

    if (FILE* pipe = popen ("ioreg -rd1 -c IOPlatformExpertDevice"
                            " | awk '/IOPlatformUUID/{print $3}' | tr -d '\"'", "r"))
    {
        char buffer[256] = {};
        if (fgets (buffer, sizeof (buffer), pipe) != nullptr)
            uuid = juce::String (buffer).trim().removeCharacters ("-");
        pclose (pipe);
    }

    if (uuid.isEmpty())
        uuid = "MACOS001";

    return uuid.substring (0, 8).toUpperCase();
#endif
}

//==============================================================================
void RegistrationManager::updateDemoMode()
{
    // AUDIO THREAD.  `registered` is an atomic precisely so this load is legal
    // and so a registration typed a moment ago on the message thread is seen
    // here on the very next block rather than whenever the cache felt like it.
    if (registered.load())
    {
        demoSilenceActive.store (false);

        // Rearm the window.  Without this a user who registers mid-session
        // keeps the stale elapsed time, so the first silent window after
        // un-registering (or a future demo state) would fire at a random
        // fraction of its length.
        lastSilenceToggleTime = 0.0;
        inSilencePeriod       = false;
        return;
    }

    // DEMO: 18 seconds of playing, then 3 seconds of silence.
    //
    // An arranger is auditioned by starting a style and hearing it develop
    // through an intro and a couple of variations, so the playing window has to
    // be long enough for a section to speak - too short and the user judges the
    // interruption rather than the product.  Three seconds of silence is short
    // enough not to feel punitive and long enough that nobody records around
    // it.
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

    if (lastSilenceToggleTime == 0.0)
    {
        lastSilenceToggleTime = now;
        inSilencePeriod = false;
    }

    const double elapsed = now - lastSilenceToggleTime;

    if (! inSilencePeriod)
    {
        if (elapsed >= 18.0)
        {
            inSilencePeriod = true;
            lastSilenceToggleTime = now;
            demoSilenceActive.store (true);
        }
    }
    else
    {
        if (elapsed >= 3.0)
        {
            inSilencePeriod = false;
            lastSilenceToggleTime = now;
            demoSilenceActive.store (false);
        }
    }
}




