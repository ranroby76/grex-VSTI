#include "RegistrationManager.h"
#include "GlobalMacros.h"   // Betel::GrexPaths - one root for every file we own

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
    // MIRROR: AppData.  The plugin folder is user-relocatable (see the folder
    // locator) and users clean, move and reinstall it; losing a paid
    // registration to a tidy-up is a support call that should never happen.
    // AppData survives all of that, and survives an uninstall.
    //
    // Writing both costs nothing and neither copy weakens the scheme, because
    // NEITHER IS TRUSTED: checkRegistration re-derives the expected serial from
    // this machine's ID and compares, so a licence file copied anywhere - the
    // other folder, another drive, another computer - is inert unless the
    // machine agrees.  The file travels; the authority does not.
    juce::File grexLicencePrimary()
    {
        return Betel::GrexPaths::root().getChildFile ("grex_license.key");
    }

    juce::File grexLicenceMirror()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Fanan Team")
                   .getChildFile ("Grex")
                   .getChildFile ("grex_license.key");
    }
}

//==============================================================================
void RegistrationManager::checkRegistration()
{
    // Either copy will do, and each is re-VALIDATED rather than trusted: the
    // stored serial is checked against this machine's own ID exactly as a typed
    // one is.  So a licence file copied to another computer does nothing there,
    // and reading from two locations adds no exposure at all.
    for (const auto& f : { grexLicencePrimary(), grexLicenceMirror() })
    {
        if (! f.existsAsFile())
            continue;

        const juce::String savedSerial = f.loadFileAsString().trim();

        if (savedSerial.isNotEmpty() && tryRegister (savedSerial))
        {
            registered.store (true);
            return;   // tryRegister has already re-written BOTH copies
        }
    }

    registered.store (false);
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
        // calculateExpectedSerial() below; this stage exists only to reject
        // obvious rubbish early, so it should not be the thing that rejects a
        // correct key.
        juce::String cleanInput = serialInput.trim()
                                             .removeCharacters (" \t\r\n")
                                             .removeCharacters (juce::String::charToString (
                                                 (juce::juce_wchar) 0x00A0));

        if (cleanInput.isEmpty() || ! cleanInput.containsOnly ("0123456789-"))
            return false;

        const long long inputNum = cleanInput.getLargeIntValue();
        const long long expected = calculateExpectedSerial();

        if (inputNum != expected)
            return false;

        // Written to BOTH, and a failure on either is not fatal: if the plugin
        // folder happens to be read-only on this install, the AppData copy
        // still carries the registration, and vice versa.  Failing the whole
        // registration because one of two redundant writes did not land would
        // be the redundancy working against the user.
        for (const auto& f : { grexLicencePrimary(), grexLicenceMirror() })
        {
            auto folder = f.getParentDirectory();

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
    // being paid several times over - calculateExpectedSerial calls this,
    // tryRegister calls calculateExpectedSerial, and checkRegistration calls
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
long long RegistrationManager::calculateExpectedSerial()
{
    const long long id = getMachineIDNumber();

    // GREX SERIAL FORMULA:  (((((ID + 6868) * 3) + 1880) * 2) - 9991)
    //
    // Deliberately DIFFERENT from OnStage's (((((ID+8401)*2)+1289)*2)-9090) and
    // from Colosseum's: sharing a formula between products would mean one
    // leaked key generator opens all of them.
    //
    // ── WHY THE CONSTANTS ARE MASKED ────────────────────────────────────────
    //
    // Written plainly, 6868 / 1880 / 9991 appear verbatim in the binary, so
    // `strings` on the DLL - or a scan for the numbers a single leaked key
    // implies - finds the formula without a debugger.  XOR-masking removes
    // that, which is the cheapest real improvement available here.
    //
    // TWO THINGS DIFFER FROM COLOSSEUM'S VERSION, AND BOTH MATTER:
    //
    //   * a different mask.  Colosseum uses 0xA5B7; reusing it would mean one
    //     leaked source file explains both products.
    //
    //   * `volatile`, so the decode happens at RUNTIME.  Colosseum's array is
    //     constexpr, which lets the compiler fold the XOR away and put the
    //     DECODED values straight into the instruction stream - hidden from
    //     `strings`, plainly visible to any disassembler.  Reading the mask
    //     through a volatile forbids that fold, so the plain constants exist
    //     nowhere in the file and only briefly in a register.
    //
    // None of this makes the maths stronger - see the header.  It raises the
    // cost of the LAZY attack, which is the attack that actually happens.
    static volatile long long maskCell = 0x5C31D7LL;
    const long long k = maskCell;

    const long long c0 = 6040323LL ^ k;   // 6868
    const long long c1 = 6042068LL ^ k;   // 3
    const long long c2 = 6043279LL ^ k;   // 1880
    const long long c3 = 6042069LL ^ k;   // 2
    const long long c4 = 6035152LL ^ k;   // 9991

    long long result = id + c0;
    result = result * c1;
    result = result + c2;
    result = result * c3;
    result = result - c4;
    return result;
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



