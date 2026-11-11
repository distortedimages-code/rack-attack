cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project(RackAttack VERSION 0.4.0 LANGUAGES C CXX)

# GitHub repository whose releases the plug-in checks for updates (public releases).
set(RA_UPDATE_REPO "distortedimages-code/rack-attack" CACHE STRING "owner/repo checked for updates (empty = no update check)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local checkout with -DJUCE_DIR=/path/to/JUCE, otherwise download JUCE 8.0.4 (same as OXIde).
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#==============================================================================
# SysEx core, no JUCE inside: shared by the Labo, the future plugin and the tests.
add_library(ra_core STATIC Source/core/RaSysex.cpp Source/core/RaKit.cpp Source/core/RaSeq.cpp Source/sim/RaSynth.cpp)
target_compile_features(ra_core PUBLIC cxx_std_17)
set_target_properties(ra_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

#==============================================================================
# RackAttack Editor: AU, VST3 and the app (standalone)
juce_add_plugin(RackAttackEditor
    COMPANY_NAME "distortedimages"
    PRODUCT_NAME "RackAttack Editor"
    BUNDLE_ID com.distortedimages.rackattackeditor
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Dimg
    PLUGIN_CODE Ratk
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Tools
    FORMATS AU VST3 Standalone)

juce_generate_juce_header(RackAttackEditor)

set(RA_APP_SOURCES
    Source/plugin/PluginProcessor.cpp
    Source/plugin/Updater.cpp
    Source/app/AppContext.cpp
    Source/app/MainComponent.cpp
    Source/app/ConnectWizard.cpp
    Source/app/Pages.cpp
    Source/app/ToolsPage.cpp
    Source/app/Engine.cpp
    Source/app/Library.cpp
    Source/app/Session.cpp
    Source/app/Sequencer.cpp
    Source/app/SeqPage.cpp
    Source/app/SoundViz.cpp
    Source/app/SimDevice.cpp
    Source/app/Pixel.cpp)

# Pixel fonts (SIL Open Font License, see Resources/fonts)
juce_add_binary_data(RaFonts SOURCES
    Resources/fonts/Silkscreen-Regular.ttf
    Resources/fonts/Silkscreen-Bold.ttf
    Resources/fonts/VT323-Regular.ttf)
set_target_properties(RaFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(RA_DEFINITIONS
    RA_VERSION="${PROJECT_VERSION}"
    RA_UPDATE_REPO="${RA_UPDATE_REPO}")

target_sources(RackAttackEditor PRIVATE ${RA_APP_SOURCES})

target_compile_definitions(RackAttackEditor PUBLIC
    ${RA_DEFINITIONS}
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(RackAttackEditor
    PRIVATE
        ra_core
        RaFonts
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
option(RA_BUILD_TESTS "Build the unit tests" ON)

if(RA_BUILD_TESTS)
    enable_testing()
    add_executable(CoreTests Tests/CoreTests.cpp)
    target_link_libraries(CoreTests PRIVATE ra_core)
    add_test(NAME CoreTests COMMAND CoreTests)

    add_executable(SeqTests Tests/SeqTests.cpp)
    target_link_libraries(SeqTests PRIVATE ra_core)
    add_test(NAME SeqTests COMMAND SeqTests)

    add_executable(SynthTests Tests/SynthTests.cpp)
    target_link_libraries(SynthTests PRIVATE ra_core)
    add_test(NAME SynthTests COMMAND SynthTests)

    # The Labo engine end to end, against a fake RackAttack (temporary HOME)
    juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
    juce_generate_juce_header(EngineTests)
    target_sources(EngineTests PRIVATE Tests/EngineTests.cpp Source/app/Engine.cpp Source/app/Session.cpp Source/app/Library.cpp
        Source/app/Sequencer.cpp)
    target_compile_definitions(EngineTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(EngineTests PRIVATE
        ra_core
        juce::juce_gui_basics
        juce::juce_audio_devices
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME EngineTests COMMAND EngineTests)
    set_tests_properties(EngineTests PROPERTIES
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home")

    # Not a test: renders the simulator playing a pattern to a WAV file
    juce_add_console_app(SimRender PRODUCT_NAME "SimRender")
    juce_generate_juce_header(SimRender)
    target_sources(SimRender PRIVATE Tests/SimRender.cpp Source/app/Engine.cpp Source/app/Session.cpp Source/app/Library.cpp)
    target_compile_definitions(SimRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SimRender PRIVATE
        ra_core
        juce::juce_gui_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Update checker: version compare, GitHub answer parsing, checksum, bundle replacement
    juce_add_console_app(UpdaterTests PRODUCT_NAME "UpdaterTests")
    juce_generate_juce_header(UpdaterTests)
    target_sources(UpdaterTests PRIVATE Tests/UpdaterTests.cpp Source/plugin/Updater.cpp)
    target_compile_definitions(UpdaterTests PRIVATE ${RA_DEFINITIONS} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(UpdaterTests PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME UpdaterTests COMMAND UpdaterTests)
endif()
