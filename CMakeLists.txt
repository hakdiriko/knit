cmake_minimum_required(VERSION 3.22)

# Mac settings must exist before project(); other platforms ignore them.
# One download for Apple Silicon and Intel Macs.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# The hypnagogic branch of dendrite: a binaural space instrument.
project(dendrite VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # No Visual C++ redistributable needed on Hakan's machine.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

if(APPLE)
    set(formats VST3 AU)
else()
    set(formats VST3)
endif()

# Identity. Hosts find dendrite by the two codes and the bundle id: they stay the
# same on every branch, so Live sets keep the device.
juce_add_plugin(dendrite
    PRODUCT_NAME "dendrite"
    COMPANY_NAME "hakdiriko"
    BUNDLE_ID "com.hakdiriko.viber"
    PLUGIN_MANUFACTURER_CODE Hakd
    PLUGIN_CODE Vibr
    FORMATS ${formats}
    VST3_CATEGORIES Fx Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(dendrite PRIVATE
    Source/Processor.cpp
    Source/Editor.cpp
    Source/space/Field.cpp
    Source/space/Itch.cpp)

target_compile_definitions(dendrite PUBLIC
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(dendrite
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
