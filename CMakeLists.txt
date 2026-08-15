
# ==========================================================
# 3.22 is JUCE 9's own minimum (JUCE/CMakeLists.txt declares
# cmake_minimum_required(VERSION 3.22)).  Ours must be at least that or the
# policy set the sub-project runs under is older than the one it was written
# for.  clap-juce-extensions asks for 3.21, so 3.22 covers both.
# ==========================================================
cmake_minimum_required(VERSION 3.22)

# LANGUAGES is spelled out on purpose.  JUCE 9 hard-fails with
# "A C compiler is required to build targets that depend on JUCE" unless C is
# in the enabled-language list; the implicit default already is "C CXX", but
# writing it down means a later edit cannot silently remove it.
# OBJCXX is deliberately NOT enabled: this project owns no .mm files, and
# JUCE's own .mm sources compile through the C++ compiler, which is the
# configuration every proven build of ours uses.
project(BetelgeuseArranger VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================================
# WHERE THINGS LIVE - resolved, not hard-coded.
#
# This block is the whole reason the project could not be built anywhere but
# one Windows machine.  Every path used to be a literal "D:/..." with no way to
# override it, so a CI runner died at configure time before a single file was
# compiled.
#
# The order is the same for all four: an explicit -D on the command line wins,
# then the copy inside the repository, then the old D: location.  Because the
# repo root IS D:/Workspace/BetelgeuseArranger on the development machine, the
# repo-relative branch resolves to exactly the same folders locally as the
# hard-coded paths did - the local build does not change behaviour, it just
# stops being the only build that can work.
# ==========================================================
set(BETELGEUSE_ASSETS_PATH "" CACHE PATH "Path to the assets folder")
set(ASIO_SDK_PATH          "" CACHE PATH "Path to the Steinberg ASIO SDK (Windows only)")

# ---- JUCE ------------------------------------------------------------------
# Same idiom as villain-vst, which is the repo where this pipeline is already
# proven: an externally-defined JUCE_DIR wins outright, otherwise autodetect.
# The workflow clones into libs/JUCE, so on CI the second branch fires and no
# -D flag is strictly needed - passing one is still honoured.
# JUCE_PATH is accepted as an alias so a Colosseum-style command line works too.
if(NOT DEFINED JUCE_DIR OR "${JUCE_DIR}" STREQUAL "")
    if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        set(JUCE_DIR "${JUCE_PATH}" CACHE PATH "Path to JUCE")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE" CACHE PATH "Path to JUCE")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE")
    elseif(EXISTS "D:/Workspace/JUCE/CMakeLists.txt")
        set(JUCE_DIR "D:/Workspace/JUCE" CACHE PATH "Path to JUCE")
    endif()
endif()

if(NOT JUCE_DIR OR NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found.  Pass -DJUCE_DIR=/path/to/JUCE, or place a checkout at "
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE.")
endif()

# ---- CLAP extensions -------------------------------------------------------
# Not committed to the repository.  Locally it sits in libs/CLAP_Extensions; on
# CI the workflow git-clones it to the same place, so both resolve identically
# and the CLAP target never depends on a path that only exists on one machine.
if(NOT DEFINED CLAP_EXTENSIONS_DIR OR "${CLAP_EXTENSIONS_DIR}" STREQUAL "")
    if(DEFINED CLAP_EXTENSIONS_PATH AND EXISTS "${CLAP_EXTENSIONS_PATH}/CMakeLists.txt")
        set(CLAP_EXTENSIONS_DIR "${CLAP_EXTENSIONS_PATH}" CACHE PATH "Path to clap-juce-extensions")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/CLAP_Extensions/CMakeLists.txt")
        set(CLAP_EXTENSIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/CLAP_Extensions" CACHE PATH "Path to clap-juce-extensions")
    elseif(EXISTS "D:/Workspace/BetelgeuseArranger/libs/CLAP_Extensions/CMakeLists.txt")
        set(CLAP_EXTENSIONS_DIR "D:/Workspace/BetelgeuseArranger/libs/CLAP_Extensions" CACHE PATH "Path to clap-juce-extensions")
    endif()
endif()

if(NOT CLAP_EXTENSIONS_DIR OR NOT EXISTS "${CLAP_EXTENSIONS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "clap-juce-extensions not found.  Pass -DCLAP_EXTENSIONS_DIR=..., or place a "
        "checkout at ${CMAKE_CURRENT_SOURCE_DIR}/libs/CLAP_Extensions.")
endif()

set(CLAP_JUCE_EXTENSIONS_DIR "${CLAP_EXTENSIONS_DIR}")

# ---- Assets ----------------------------------------------------------------
# These are committed to the repository, so the repo-relative branch is the one
# that fires on every machine including this one.
if(BETELGEUSE_ASSETS_PATH AND EXISTS "${BETELGEUSE_ASSETS_PATH}")
    set(BETELGEUSE_ASSETS_DIR "${BETELGEUSE_ASSETS_PATH}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    set(BETELGEUSE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
elseif(EXISTS "D:/Workspace/BetelgeuseArranger/assets")
    set(BETELGEUSE_ASSETS_DIR "D:/Workspace/BetelgeuseArranger/assets")
else()
    message(FATAL_ERROR
        "Assets folder not found.  Pass -DBETELGEUSE_ASSETS_PATH=..., or place it at "
        "${CMAKE_CURRENT_SOURCE_DIR}/assets.")
endif()

set(BETELGEUSE_INSTR_IMG "${BETELGEUSE_ASSETS_DIR}/instrumet images")

# ---- ASIO SDK - WINDOWS ONLY -----------------------------------------------
# ASIO is a Steinberg Windows driver API.  It does not exist on macOS or Linux,
# and the old unconditional
#     if(NOT EXISTS "${ASIO_SDK_DIR}") message(FATAL_ERROR ...)
# was the single line that killed every non-Windows configure before anything
# else ran.  On Windows the failure is still FATAL, deliberately: a Windows
# build that silently loses ASIO is worse than one that refuses to configure.
set(GREX_HAVE_ASIO FALSE)

if(WIN32)
    if(ASIO_SDK_PATH AND EXISTS "${ASIO_SDK_PATH}")
        set(ASIO_SDK_DIR "${ASIO_SDK_PATH}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/ASIOSDK")
        set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/ASIOSDK")
    elseif(EXISTS "D:/Workspace/ASIOSDK")
        set(ASIO_SDK_DIR "D:/Workspace/ASIOSDK")
    else()
        message(FATAL_ERROR
            "ASIO SDK not found on a Windows build.  Pass -DASIO_SDK_PATH=..., or place "
            "the SDK at D:/Workspace/ASIOSDK.")
    endif()

    set(GREX_HAVE_ASIO TRUE)
endif()

# ==========================================================
# DRUM-KIT images: D<kit key>.png, one per sampled kit.
#
# They live beside the instrument images, in "instrumet images".  That is also
# why the D prefix matters: 016.png (an instrument) and D016.png (the Power
# kit) share a folder AND a flat BinaryData symbol space, and only the prefix
# keeps them apart -- 016.png mangles to _016_png, D016.png to D016_png.
#
# Each file is EXISTS-checked before it is listed.  juce_add_binary_data aborts
# the entire build if it cannot open a source, so one typo would otherwise take
# everything down with an error that names nothing.  A kit with no image is not
# an error: loadKitFromBinaryData returns an invalid image and that slot simply
# shows no picture.
#
# NOTE FOR CI: macOS is case-sensitive on some volumes where Windows never is.
# The names below must match the committed files exactly, "instrumet images"
# typo included.
# ==========================================================
set(BETELGEUSE_KIT_IMG "${BETELGEUSE_ASSETS_DIR}/instrumet images")

set(BETELGEUSE_KIT_IMAGES "")
set(BETELGEUSE_KIT_MISSING "")

foreach(kit 000 008 016 024 025 032 040 048 056)
    if(EXISTS "${BETELGEUSE_KIT_IMG}/D${kit}.png")
        list(APPEND BETELGEUSE_KIT_IMAGES "${BETELGEUSE_KIT_IMG}/D${kit}.png")
    else()
        list(APPEND BETELGEUSE_KIT_MISSING "D${kit}.png")
    endif()
endforeach()

# ----------------------------------------------------------------------------
# SOUND-PACK PICTURES: SP_WORLD.png and SP_ORIENTAL.png
#
# A pack instrument has no picture of its own - there are 153 world and 88
# oriental sounds and no artwork per sound - so each PACK shows one image and
# every instrument in it uses that.
#
# They sit in the assets root beside back.png, not in "instrumet images", which
# holds the numbered GM set and the D-prefixed kits.  The SP_ prefix keeps them
# clear of both in BinaryData's flat symbol space: SP_WORLD.png mangles to
# SP_WORLD_png, which cannot collide with _000_png or D016_png.
#
# EXISTS-checked, exactly like the kit images and for the same reason: a missing
# pack picture must be a slot that shows nothing, not a build that dies naming
# no file.
# ----------------------------------------------------------------------------
set(BETELGEUSE_PACK_IMAGES "")
set(BETELGEUSE_PACK_MISSING "")

foreach(packimg SP_WORLD SP_ORIENTAL)
    if(EXISTS "${BETELGEUSE_ASSETS_DIR}/${packimg}.png")
        list(APPEND BETELGEUSE_PACK_IMAGES "${BETELGEUSE_ASSETS_DIR}/${packimg}.png")
    elseif(EXISTS "${BETELGEUSE_INSTR_IMG}/${packimg}.png")
        # Also accepted next to the instrument images, since that is the other
        # folder someone would reasonably drop them into.
        list(APPEND BETELGEUSE_PACK_IMAGES "${BETELGEUSE_INSTR_IMG}/${packimg}.png")
    else()
        list(APPEND BETELGEUSE_PACK_MISSING "${packimg}.png")
    endif()
endforeach()

if(BETELGEUSE_PACK_MISSING)
    message(STATUS "   Pack images NOT found: ${BETELGEUSE_PACK_MISSING}")
endif()

# ----------------------------------------------------------------------------
# THE SAMPLED ("unique") KITS SHARE ONE PICTURE: DUNIQUE.png
#
# There is deliberately no D<n>.png per sampled kit.  Those kits are defined by
# what is on disk under sounds/drums -- drop in a folder and it works on the
# next scan, no table to edit -- and a per-kit image would put a CMake edit and
# a full rebuild in front of that, which is the exact friction the folder
# naming was designed to remove.  One image stands for all of them; SoundsTab
# reaches for it whenever the slot holds a full kit.
#
# Same EXISTS check as above, and the same D prefix logic: DUNIQUE.png mangles
# to DUNIQUE_png and cannot collide with the numbered instrument art.
# ----------------------------------------------------------------------------
if(EXISTS "${BETELGEUSE_KIT_IMG}/DUNIQUE.png")
    list(APPEND BETELGEUSE_KIT_IMAGES "${BETELGEUSE_KIT_IMG}/DUNIQUE.png")
else()
    list(APPEND BETELGEUSE_KIT_MISSING "DUNIQUE.png")
endif()

list(LENGTH BETELGEUSE_KIT_IMAGES BETELGEUSE_KIT_COUNT)
message(STATUS "Drum-kit images: ${BETELGEUSE_KIT_COUNT} of 10 in ${BETELGEUSE_KIT_IMG}")
if(BETELGEUSE_KIT_MISSING)
    message(STATUS "   not found: ${BETELGEUSE_KIT_MISSING}")
endif()

# ==========================================================
# Faster builds
#
# The job pools are a LOCAL optimisation: they cap link jobs at 1 so a big
# link does not fight the compile jobs for memory.  On a CI runner the whole
# machine is ours and the pool only slows things down, so it is skipped there.
# ==========================================================
include(ProcessorCount)
ProcessorCount(BETELGEUSE_CPU_CORES)
if (BETELGEUSE_CPU_CORES GREATER 1 AND NOT DEFINED ENV{GITHUB_ACTIONS})
    set_property(GLOBAL PROPERTY JOB_POOLS compile_pool=${BETELGEUSE_CPU_CORES} link_pool=1)
    set_property(GLOBAL PROPERTY JOB_POOL_COMPILE compile_pool)
    set_property(GLOBAL PROPERTY JOB_POOL_LINK link_pool)
    message(STATUS "CPU cores detected: ${BETELGEUSE_CPU_CORES}")
endif()
set(CMAKE_UNITY_BUILD OFF)

# ==========================================================
# JUCE
# ==========================================================
add_subdirectory("${JUCE_DIR}" JUCE)

# ==========================================================
# CLAP support
# ==========================================================
set(CLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLAP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLAP_HELPERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CLAP_JUCE_EXTENSIONS_DIR}" clap-juce-extensions EXCLUDE_FROM_ALL)

# ==========================================================
# Embedded binary assets (instrument images)
#
# 98 unique image files: 000-052, 054-055, 056-063, 064-071,
# 072-079, 080, 088, 095, 104-111, 112-119
# (shared image rules handled in SoundsTab.h at runtime)
# ==========================================================
juce_add_binary_data(BetelgeuseAssets SOURCES
    # UI background
    "${BETELGEUSE_ASSETS_DIR}/back.png"
    # Application icon (used by the InstrEditorWindow title bar)
    "${BETELGEUSE_ASSETS_DIR}/icon.ico"
    # ------------------------------------------------------------------
    # DRUM KITS - D<registry key>.png, resolved above.  Whatever was found,
    # nothing more: an absent kit image is a kit that shows no picture, not a
    # build failure.
    #
    # The D prefix is doing real work: BinaryData is a FLAT symbol space, and
    # 000/008/016/024/032/040/048/056 are already taken below by the
    # instrument images (000.png -> _000_png).  D016.png mangles to D016_png
    # instead, so the two families cannot collide.
    # ------------------------------------------------------------------
    ${BETELGEUSE_KIT_IMAGES}
    # ------------------------------------------------------------------
    # WORLD / ORIENTAL pack pictures - one per pack, shared by every sound
    # in it.  Resolved above; whatever was found, nothing more.
    # ------------------------------------------------------------------
    ${BETELGEUSE_PACK_IMAGES}
    # Piano (000-007)
    "${BETELGEUSE_INSTR_IMG}/000.png"
    "${BETELGEUSE_INSTR_IMG}/001.png"
    "${BETELGEUSE_INSTR_IMG}/002.png"
    "${BETELGEUSE_INSTR_IMG}/003.png"
    "${BETELGEUSE_INSTR_IMG}/004.png"
    "${BETELGEUSE_INSTR_IMG}/005.png"
    "${BETELGEUSE_INSTR_IMG}/006.png"
    "${BETELGEUSE_INSTR_IMG}/007.png"
    # Chromatic Perc (008-015)
    "${BETELGEUSE_INSTR_IMG}/008.png"
    "${BETELGEUSE_INSTR_IMG}/009.png"
    "${BETELGEUSE_INSTR_IMG}/010.png"
    "${BETELGEUSE_INSTR_IMG}/011.png"
    "${BETELGEUSE_INSTR_IMG}/012.png"
    "${BETELGEUSE_INSTR_IMG}/013.png"
    "${BETELGEUSE_INSTR_IMG}/014.png"
    "${BETELGEUSE_INSTR_IMG}/015.png"
    # Organ (016-023)
    "${BETELGEUSE_INSTR_IMG}/016.png"
    "${BETELGEUSE_INSTR_IMG}/017.png"
    "${BETELGEUSE_INSTR_IMG}/018.png"
    "${BETELGEUSE_INSTR_IMG}/019.png"
    "${BETELGEUSE_INSTR_IMG}/020.png"
    "${BETELGEUSE_INSTR_IMG}/021.png"
    "${BETELGEUSE_INSTR_IMG}/022.png"
    "${BETELGEUSE_INSTR_IMG}/023.png"
    # Guitar (024-031)
    "${BETELGEUSE_INSTR_IMG}/024.png"
    "${BETELGEUSE_INSTR_IMG}/025.png"
    "${BETELGEUSE_INSTR_IMG}/026.png"
    "${BETELGEUSE_INSTR_IMG}/027.png"
    "${BETELGEUSE_INSTR_IMG}/028.png"
    "${BETELGEUSE_INSTR_IMG}/029.png"
    "${BETELGEUSE_INSTR_IMG}/030.png"
    "${BETELGEUSE_INSTR_IMG}/031.png"
    # Bass (032-039)
    "${BETELGEUSE_INSTR_IMG}/032.png"
    "${BETELGEUSE_INSTR_IMG}/033.png"
    "${BETELGEUSE_INSTR_IMG}/034.png"
    "${BETELGEUSE_INSTR_IMG}/035.png"
    "${BETELGEUSE_INSTR_IMG}/036.png"
    "${BETELGEUSE_INSTR_IMG}/037.png"
    "${BETELGEUSE_INSTR_IMG}/038.png"
    "${BETELGEUSE_INSTR_IMG}/039.png"
    # Strings (040-047)
    "${BETELGEUSE_INSTR_IMG}/040.png"
    "${BETELGEUSE_INSTR_IMG}/041.png"
    "${BETELGEUSE_INSTR_IMG}/042.png"
    "${BETELGEUSE_INSTR_IMG}/043.png"
    "${BETELGEUSE_INSTR_IMG}/044.png"
    "${BETELGEUSE_INSTR_IMG}/045.png"
    "${BETELGEUSE_INSTR_IMG}/046.png"
    "${BETELGEUSE_INSTR_IMG}/047.png"
    # Ensemble (048-052, 054-055)
    # note: 053 does not exist - Voice Oohs shares 052
    "${BETELGEUSE_INSTR_IMG}/048.png"
    "${BETELGEUSE_INSTR_IMG}/049.png"
    "${BETELGEUSE_INSTR_IMG}/050.png"
    "${BETELGEUSE_INSTR_IMG}/051.png"
    "${BETELGEUSE_INSTR_IMG}/052.png"
    "${BETELGEUSE_INSTR_IMG}/054.png"
    "${BETELGEUSE_INSTR_IMG}/055.png"
    # Brass (056-063)
    "${BETELGEUSE_INSTR_IMG}/056.png"
    "${BETELGEUSE_INSTR_IMG}/057.png"
    "${BETELGEUSE_INSTR_IMG}/058.png"
    "${BETELGEUSE_INSTR_IMG}/059.png"
    "${BETELGEUSE_INSTR_IMG}/060.png"
    "${BETELGEUSE_INSTR_IMG}/061.png"
    "${BETELGEUSE_INSTR_IMG}/062.png"
    "${BETELGEUSE_INSTR_IMG}/063.png"
    # Reed (064-071)
    "${BETELGEUSE_INSTR_IMG}/064.png"
    "${BETELGEUSE_INSTR_IMG}/065.png"
    "${BETELGEUSE_INSTR_IMG}/066.png"
    "${BETELGEUSE_INSTR_IMG}/067.png"
    "${BETELGEUSE_INSTR_IMG}/068.png"
    "${BETELGEUSE_INSTR_IMG}/069.png"
    "${BETELGEUSE_INSTR_IMG}/070.png"
    "${BETELGEUSE_INSTR_IMG}/071.png"
    # Pipe (072-079)
    "${BETELGEUSE_INSTR_IMG}/072.png"
    "${BETELGEUSE_INSTR_IMG}/073.png"
    "${BETELGEUSE_INSTR_IMG}/074.png"
    "${BETELGEUSE_INSTR_IMG}/075.png"
    "${BETELGEUSE_INSTR_IMG}/076.png"
    "${BETELGEUSE_INSTR_IMG}/077.png"
    "${BETELGEUSE_INSTR_IMG}/078.png"
    "${BETELGEUSE_INSTR_IMG}/079.png"
    # Synth Lead - one shared image (080); 081-087 share it
    "${BETELGEUSE_INSTR_IMG}/080.png"
    # Synth Pad - one shared image (088); 089-094 share it
    "${BETELGEUSE_INSTR_IMG}/088.png"
    # FX + SFX - one shared image (095); 096-103 and 120-127 share it
    "${BETELGEUSE_INSTR_IMG}/095.png"
    # Ethnic (104-111)
    "${BETELGEUSE_INSTR_IMG}/104.png"
    "${BETELGEUSE_INSTR_IMG}/105.png"
    "${BETELGEUSE_INSTR_IMG}/106.png"
    "${BETELGEUSE_INSTR_IMG}/107.png"
    "${BETELGEUSE_INSTR_IMG}/108.png"
    "${BETELGEUSE_INSTR_IMG}/109.png"
    "${BETELGEUSE_INSTR_IMG}/110.png"
    "${BETELGEUSE_INSTR_IMG}/111.png"
    # Percussive (112-119)
    "${BETELGEUSE_INSTR_IMG}/112.png"
    "${BETELGEUSE_INSTR_IMG}/113.png"
    "${BETELGEUSE_INSTR_IMG}/114.png"
    "${BETELGEUSE_INSTR_IMG}/115.png"
    "${BETELGEUSE_INSTR_IMG}/116.png"
    "${BETELGEUSE_INSTR_IMG}/117.png"
    "${BETELGEUSE_INSTR_IMG}/118.png"
    "${BETELGEUSE_INSTR_IMG}/119.png"
)

# ==========================================================
# Plugin target
#
# FORMATS now reads "VST3 AU".  AU is added UNCONDITIONALLY and that is safe:
# JUCE filters the requested formats through _juce_get_platform_plugin_kinds(),
# which only offers AU when CMAKE_SYSTEM_NAME is "Darwin".  On Windows the AU
# entry is silently dropped and nothing about the existing build changes - no
# if(APPLE) wrapper needed, and no way for the two platforms' format lists to
# drift apart.
#
# AU needs no extra metadata here either: AU_MAIN_TYPE defaults to
# kAudioUnitType_MusicDevice because IS_SYNTH is TRUE, the AU subtype comes
# from PLUGIN_CODE (BtPP) and the manufacturer from PLUGIN_MANUFACTURER_CODE
# (Fnan) - both already four characters with a capital, which AU requires.
#
# VST3_AUTO_MANIFEST stays FALSE on purpose.  Generating the manifest means
# RUNNING the freshly built binary, and a universal arm64+x86_64 build made on
# a CI runner is exactly the case where that is unreliable.
# ==========================================================
juce_add_plugin(BetelgeuseArranger
    COMPANY_NAME "Fanan"
    BUNDLE_ID com.fanan.betelgeuse-pnp
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
    PLUGIN_MANUFACTURER_CODE Fnan
    PLUGIN_CODE BtPP
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    HARDENED_RUNTIME_ENABLED TRUE
    FORMATS VST3 AU
    PRODUCT_NAME "Grex"
)

clap_juce_extensions_plugin(TARGET BetelgeuseArranger
    CLAP_ID "com.fanan.betelgeuse-pnp"
    CLAP_FEATURES instrument midi
)

juce_generate_juce_header(BetelgeuseArranger)

target_sources(BetelgeuseArranger PRIVATE
    src/Main.h
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponent.h

    # Machine-locked serial + demo mode.  The .cpp MUST be listed here: it holds
    # the only definitions of checkRegistration / tryRegister / getMachineIDString
    # / updateDemoMode, and Main.cpp, MainComponent.cpp and SettingsTab.h all call
    # them.  Leaving it out is what produced LNK2019 on four symbols in both the
    # VST3 and the CLAP target - the headers compiled fine, there was simply
    # nothing to link against.
    src/RegistrationManager.h
    src/RegistrationManager.cpp

    src/TabHeader.cpp
    src/TabHeader.h
    src/BetelSidePanel.h
    src/RectangleKnob.h
    src/SelectorGroup.h
    src/SpaceAnimation.h

    src/MainTab.cpp
    src/MainTab.h
    src/JumpsTab.cpp
    src/JumpsTab.h
    src/JumpsConfig.h
    src/StylesTab.h
    src/SoundsTab.h
    src/MixerTab.h
    src/SetEditorTab.h
    src/CrashTab.h
    src/SettingsTab.h
    src/TutorialTab.h
    src/GrexPopupWindow.h
    src/SliderNorm.h
    src/GlobalMacrosPanel.h
    src/StyleLevels.h
    src/StyleFavorites.h
    src/MasterSettings.h
    src/CcMap.h
    src/SetBaker.h
    src/SfzLoader.h
    src/StyleCensus.h
    src/FullKitMap.h
    src/StyleLevelsPanel.h
    src/MacroFxWindows.h

    # Instrument editor (popup)
    src/InstrEditPanel.h
    src/SynthesisPanel.h
    src/ModulationPanel.h
    src/EffectsPanel.h
    src/InstrEditorWindow.h

    # Sampler engine (blob-based)
    src/AccessCode.h
    src/BlobFormat.h
    src/BlobReader.h
    src/SynthCommon.h
    src/SoundsFx.h
    src/SweetenerFx.h
    src/PerfMonitor.h
    src/GlobalMacros.h
    src/Channel.h
    src/Channel.cpp
    src/SamplePlayerEngine.h
    src/SamplePlayerEngine.cpp

    # Yamaha SFF1/SFF2 style parser
    src/StyleData.h
    src/StyleLoader.h
    src/StyleLoader.cpp
    src/StyleSequencer.h
    src/StyleSequencer.cpp
    src/ChordTransposer.h
    src/BankProgramMap.h
    src/StylePlayer.h
    src/StyleLoudness.h
)

# JUCE_ASIO is now conditional.  It used to be defined unconditionally, which
# on a Mac asks juce_audio_devices to compile a Windows driver back end that
# does not exist and whose headers were never there to include.
target_compile_definitions(BetelgeuseArranger PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_OPENGL=1
)

if(GREX_HAVE_ASIO)
    target_compile_definitions(BetelgeuseArranger PRIVATE JUCE_ASIO=1)

    target_include_directories(BetelgeuseArranger PRIVATE
        "${ASIO_SDK_DIR}/common"
        "${ASIO_SDK_DIR}/host"
    )
endif()

# ==========================================================
# SOURCE ENCODING
#
# Every source file here is UTF-8, but MSVC assumes the machine's ANSI code page
# unless told otherwise - so a non-ASCII character inside a STRING LITERAL was
# being re-interpreted byte by byte and reached the UI as mojibake.  That is what
# turned "LEARN..." and a handful of dashes into gibberish on screen.
#
# /utf-8 sets both the source and execution charsets, which fixes the cause
# rather than the symptom.  The literals have been ASCII-ised as well, so the
# build is correct with or without this flag - but with it, the next em dash
# someone types will not come back as a bug report.
#
# clang and gcc already assume UTF-8 for both charsets, so the macOS and Linux
# builds need no equivalent.
# ==========================================================
if(MSVC)
    target_compile_options(BetelgeuseArranger PRIVATE /utf-8)
endif()

target_link_libraries(BetelgeuseArranger PRIVATE
    BetelgeuseAssets           # embedded binary data
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_opengl
)

# ==========================================================
# Copy CLAP to artefacts
#
# The destination used to be the literal string
#     ${CMAKE_SOURCE_DIR}/build/BetelgeuseArranger_artefacts/Release
# which assumed the build tree is always a folder called "build" directly
# inside the source tree, and that the configuration is always Release.  It is
# now derived from CMAKE_BINARY_DIR with a $<CONFIG> generator expression, so
# it lands beside the VST3 wherever the build actually happens - including an
# out-of-tree CI build directory and a Debug configuration.
# ==========================================================
set(ARTEFACTS_DIR "${CMAKE_BINARY_DIR}/BetelgeuseArranger_artefacts/$<CONFIG>")

add_custom_command(TARGET BetelgeuseArranger_CLAP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARTEFACTS_DIR}/CLAP"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:BetelgeuseArranger_CLAP>"
        "${ARTEFACTS_DIR}/CLAP/$<TARGET_FILE_NAME:BetelgeuseArranger_CLAP>"
    COMMENT "Copying CLAP to artefacts..."
)

message(STATUS "Betelgeuse Arranger configured.")
message(STATUS "   JUCE:   ${JUCE_DIR}")
if(GREX_HAVE_ASIO)
    message(STATUS "   ASIO:   ${ASIO_SDK_DIR}")
else()
    message(STATUS "   ASIO:   disabled (not a Windows build)")
endif()
message(STATUS "   CLAP:   ${CLAP_JUCE_EXTENSIONS_DIR}")
message(STATUS "   Assets: ${BETELGEUSE_ASSETS_DIR}")
