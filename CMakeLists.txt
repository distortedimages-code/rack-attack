cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project(RackAttack VERSION 0.2.0 LANGUAGES C CXX)

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
add_library(ra_core STATIC Source/core/RaSysex.cpp Source/core/RaKit.cpp)
target_compile_features(ra_core PUBLIC cxx_std_17)
set_target_properties(ra_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

#==============================================================================
# RackAttack Editor: the app (front panel, kit, library, test tools)
juce_add_gui_app(RackAttackEditor
    PRODUCT_NAME "RackAttack Editor"
    COMPANY_NAME "distortedimages"
    BUNDLE_ID com.distortedimages.rackattackeditor
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(RackAttackEditor)

target_sources(RackAttackEditor PRIVATE
    Source/app/Main.cpp
    Source/app/MainComponent.cpp
    Source/app/ConnectWizard.cpp
    Source/app/Pages.cpp
    Source/app/ToolsPage.cpp
    Source/app/Engine.cpp
    Source/app/Library.cpp
    Source/app/Session.cpp)

target_compile_definitions(RackAttackEditor PRIVATE
    RA_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RackAttackEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RackAttackEditor,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(RackAttackEditor
    PRIVATE
        ra_core
        juce::juce_gui_extra
        juce::juce_audio_devices
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

    # The Labo engine end to end, against a fake RackAttack (temporary HOME)
    juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
    juce_generate_juce_header(EngineTests)
    target_sources(EngineTests PRIVATE Tests/EngineTests.cpp Source/app/Engine.cpp Source/app/Session.cpp Source/app/Library.cpp)
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
endif()
