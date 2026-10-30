cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS so it runs natively on Apple Silicon and Intel.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# Static MSVC runtime: the Windows VST3 needs no Visual C++ redistributable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(dendrite VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(DENDRITE_FORMATS VST3)
if(APPLE)
    list(APPEND DENDRITE_FORMATS AU)
endif()

# Renamed from viber in 0.2.0, and written lowercase since 0.4.0. The codes (and the
# bundle id) never change: hosts find the plug-in by them, so saved Live sets keep it.
juce_add_plugin(dendrite
    COMPANY_NAME "hakdiriko"
    BUNDLE_ID "com.hakdiriko.viber"
    PLUGIN_MANUFACTURER_CODE Hakd
    PLUGIN_CODE Vibr
    PRODUCT_NAME "dendrite"
    FORMATS ${DENDRITE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(dendrite PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/DubEcho.cpp)

target_compile_definitions(dendrite PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(dendrite
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
