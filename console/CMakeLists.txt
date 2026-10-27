cmake_minimum_required(VERSION 3.22)

# Undertow Rack: a small app that runs AU/VST3 effects on the Mac's own audio.
# Mac: universal binary, and 14.2 is the first macOS with Core Audio process taps.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "")
project(UndertowRack VERSION 1.0.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout if present (console/JUCE or a path in JUCE_DIR), otherwise download it.
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(UndertowRack
    PRODUCT_NAME "Undertow Rack"
    COMPANY_NAME "Undertow Audio"
    BUNDLE_ID com.undertowaudio.rack
    VERSION ${PROJECT_VERSION}
    # Shown by macOS the first time the app asks to hear system audio.
    PLIST_TO_MERGE [[
<plist>
  <dict>
    <key>NSAudioCaptureUsageDescription</key>
    <string>Undertow Rack runs your Mac's audio through your chosen plug-ins.</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.2</string>
  </dict>
</plist>
]])

target_sources(UndertowRack PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Rack.cpp)

if(APPLE)
    target_sources(UndertowRack PRIVATE Source/SystemAudio_mac.mm)
    set_source_files_properties(Source/SystemAudio_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(UndertowRack PRIVATE
        "-framework CoreAudio" "-framework Foundation" "-framework AppKit")
else()
    target_sources(UndertowRack PRIVATE Source/SystemAudio_stub.cpp)
endif()

target_include_directories(UndertowRack PRIVATE Source)

target_compile_definitions(UndertowRack PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:UndertowRack,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:UndertowRack,JUCE_VERSION>")

target_link_libraries(UndertowRack
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(NOT MSVC)
    target_compile_options(UndertowRack PRIVATE -Wall -Wextra)
endif()

juce_generate_juce_header(UndertowRack)
