cmake_minimum_required(VERSION 3.16)

project(BetelgeuseArranger VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR     "D:/Workspace/JUCE")
set(ASIO_SDK_DIR "D:/Workspace/ASIOSDK")
set(BETELGEUSE_ASSETS_DIR "D:/Workspace/BetelgeuseArranger/assets")
set(BETELGEUSE_INSTR_IMG  "${BETELGEUSE_ASSETS_DIR}/instrumet images")
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

set(CLAP_JUCE_EXTENSIONS_DIR "D:/Workspace/BetelgeuseArranger/libs/CLAP_Extensions")

if (NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at: ${JUCE_DIR}")
endif()
if (NOT EXISTS "${ASIO_SDK_DIR}")
    message(FATAL_ERROR "ASIO SDK not found at: ${ASIO_SDK_DIR}")
endif()
if (NOT EXISTS "${BETELGEUSE_ASSETS_DIR}")
    message(FATAL_ERROR "Assets folder not found at: ${BETELGEUSE_ASSETS_DIR}")
endif()

# ==========================================================
# Faster builds
# ==========================================================
include(ProcessorCount)
ProcessorCount(BETELGEUSE_CPU_CORES)
if (BETELGEUSE_CPU_CORES GREATER 1)
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
    # note: 053 does not exist — Voice Oohs shares 052
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
    # Synth Lead — one shared image (080); 081-087 share it
    "${BETELGEUSE_INSTR_IMG}/080.png"
    # Synth Pad — one shared image (088); 089-094 share it
    "${BETELGEUSE_INSTR_IMG}/088.png"
    # FX + SFX — one shared image (095); 096-103 and 120-127 share it
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
    FORMATS VST3
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
    src/RegistrationManager.h
    src/RegistrationManager.cpp
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

target_compile_definitions(BetelgeuseArranger PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_ASIO=1
    JUCE_USE_OPENGL=1
)

target_include_directories(BetelgeuseArranger PRIVATE
    "${ASIO_SDK_DIR}/common"
    "${ASIO_SDK_DIR}/host"
)

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
# ==========================================================
set(ARTEFACTS_DIR "${CMAKE_SOURCE_DIR}/build/BetelgeuseArranger_artefacts/Release")

add_custom_command(TARGET BetelgeuseArranger_CLAP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ARTEFACTS_DIR}/CLAP"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:BetelgeuseArranger_CLAP>"
        "${ARTEFACTS_DIR}/CLAP/$<TARGET_FILE_NAME:BetelgeuseArranger_CLAP>"
    COMMENT "Copying CLAP to artefacts..."
)

message(STATUS "Betelgeuse Arranger configured.")
message(STATUS "   JUCE:   ${JUCE_DIR}")
message(STATUS "   ASIO:   ${ASIO_SDK_DIR}")
message(STATUS "   CLAP:   ${CLAP_JUCE_EXTENSIONS_DIR}")
message(STATUS "   Assets: ${BETELGEUSE_ASSETS_DIR}")
