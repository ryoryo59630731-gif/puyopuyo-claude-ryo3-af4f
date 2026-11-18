cmake_minimum_required(VERSION 3.22)

project(SasariOsaeru VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

option(SASARI_BUILD_TESTS "Build the DSP unit tests and the plugin host test" ON)

# JUCE is fetched at configure time. To use a local checkout instead:
#   cmake -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE ...
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SasariOsaeru
    COMPANY_NAME "SasariOsaeru"
    PRODUCT_NAME "SasariOsaeru"
    DESCRIPTION "Harshness and sibilance suppressor"
    BUNDLE_ID "com.sasariosaeru.plugin"
    PLUGIN_MANUFACTURER_CODE Ssro
    PLUGIN_CODE Sas1
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics EQ)

target_sources(SasariOsaeru PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/Engine.cpp)

target_compile_definitions(SasariOsaeru PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SasariOsaeru
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(SasariOsaeru PRIVATE /utf-8)
endif()

if(SASARI_BUILD_TESTS)
    enable_testing()

    add_executable(EngineTests tests/EngineTests.cpp src/dsp/Engine.cpp)
    if(MSVC)
        target_compile_options(EngineTests PRIVATE /utf-8)
    endif()
    add_test(NAME EngineTests COMMAND EngineTests)

    juce_add_console_app(PluginHostTest PRODUCT_NAME "PluginHostTest")
    target_sources(PluginHostTest PRIVATE tests/PluginHostTest.cpp)
    target_compile_definitions(PluginHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(PluginHostTest PRIVATE
        juce::juce_audio_processors
        juce::juce_recommended_config_flags)
    add_dependencies(PluginHostTest SasariOsaeru_VST3)
    get_target_property(SASARI_VST3_BUNDLE SasariOsaeru_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
    add_test(NAME PluginHostTest COMMAND PluginHostTest "${SASARI_VST3_BUNDLE}")
endif()
