cmake_minimum_required(VERSION 3.22)

# Mac: universal binary (Apple Silicon + Intel). Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
project(Undertow VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime on Windows so the .vst3 has no runtime DLL dependencies
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Use a local JUCE checkout if present, otherwise download it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Undertow
    COMPANY_NAME "Undertow Audio"
    PLUGIN_MANUFACTURER_CODE Undt
    PLUGIN_CODE Usub
    PRODUCT_NAME "Undertow"
    BUNDLE_ID com.undertowaudio.undertow
    FORMATS VST3 AU Standalone          # AU is only built on macOS
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE       # set TRUE to auto-install into your system plugin folder
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Undertow PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(Undertow PRIVATE Source)

target_compile_definitions(Undertow PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Undertow
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Sensible warnings (JUCE's own recommended set is extremely pedantic).
if(MSVC)
    target_compile_options(Undertow PRIVATE /W4)
else()
    target_compile_options(Undertow PRIVATE -Wall -Wextra)
endif()

