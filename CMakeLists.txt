cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS so it runs natively on Apple Silicon and Intel.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# Static MSVC runtime: the Windows VST3 needs no Visual C++ redistributable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(dendrite VERSION 1.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(DENDRITE_FORMATS VST3)
if(APPLE)
    list(APPEND DENDRITE_FORMATS AU)
endif()

# Identity. Hosts find dendrite by the two codes and the bundle id, and its settings by
# the state type "viber" (in the processor): none of them ever change, so saved Live
# sets keep the device.
juce_add_plugin(dendrite
    COMPANY_NAME "hakdiriko"
    BUNDLE_ID "com.hakdiriko.viber"
    PLUGIN_MANUFACTURER_CODE Hakd
    PLUGIN_CODE Vibr
    PRODUCT_NAME "dendrite"
    DESCRIPTION "a dub echo that grows around your head"
    FORMATS ${DENDRITE_FORMATS}
    VST3_CATEGORIES Fx Delay Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(dendrite_fonts
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/fonts/Urbanist-Light.ttf
        Resources/fonts/Urbanist-Regular.ttf
        Resources/fonts/Urbanist-Medium.ttf
        Resources/fonts/FragmentMono-Regular.ttf
        Resources/lamps/led-off.png
        Resources/lamps/led-on.png)

target_sources(dendrite PRIVATE
    Source/Processor.cpp
    Source/Editor.cpp
    Source/ui/Theme.cpp
    Source/ui/Field.cpp
    Source/ui/Medium.cpp
    Source/ui/Controls.cpp
    Source/ui/Tape.cpp
    Source/ui/FirstLight.cpp
    Source/ui/HostFocus.cpp
    Source/dsp/Echo.cpp
    Source/dsp/Itch.cpp
    Source/dsp/Engine.cpp
    Source/keep/Keep.cpp)

target_include_directories(dendrite PRIVATE Source)

target_compile_definitions(dendrite PUBLIC
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(dendrite
    PRIVATE
        dendrite_fonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Local tools (not built in CI): the offline renderer and a window snapshot.
option(DENDRITE_TOOLS "Build the offline renderer and the window snapshot tool" OFF)
if(DENDRITE_TOOLS)
    add_executable(dendrite_render tools/render/render.cpp Source/dsp/Echo.cpp Source/dsp/Itch.cpp Source/dsp/Engine.cpp)
    target_include_directories(dendrite_render PRIVATE Source)

    juce_add_console_app(dendrite_snapshot PRODUCT_NAME "dendrite_snapshot")
    target_sources(dendrite_snapshot PRIVATE
        tools/snapshot/snapshot.cpp
        Source/Processor.cpp
        Source/Editor.cpp
        Source/ui/Theme.cpp
        Source/ui/Field.cpp
        Source/ui/Medium.cpp
        Source/ui/Controls.cpp
        Source/ui/Tape.cpp
        Source/ui/FirstLight.cpp
        Source/ui/HostFocus.cpp
        Source/dsp/Echo.cpp
        Source/dsp/Itch.cpp
        Source/dsp/Engine.cpp
        Source/keep/Keep.cpp)
    target_include_directories(dendrite_snapshot PRIVATE Source)
    target_compile_definitions(dendrite_snapshot PRIVATE
        JucePlugin_Name="dendrite" JucePlugin_VersionString="1.1.0"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(dendrite_snapshot PRIVATE dendrite_fonts juce::juce_audio_utils juce::juce_recommended_config_flags)

    # keep, tested headless: marks, bar-aligned takes, WAVs
    juce_add_console_app(dendrite_keeptest PRODUCT_NAME "dendrite_keeptest")
    get_target_property(dendrite_snapshot_sources dendrite_snapshot SOURCES)
    list(FILTER dendrite_snapshot_sources EXCLUDE REGEX "snapshot\\.cpp$")
    target_sources(dendrite_keeptest PRIVATE tools/keeptest/keeptest.cpp ${dendrite_snapshot_sources})
    target_include_directories(dendrite_keeptest PRIVATE Source)
    target_compile_definitions(dendrite_keeptest PRIVATE
        JucePlugin_Name="dendrite" JucePlugin_VersionString="1.3.0"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(dendrite_keeptest PRIVATE dendrite_fonts juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
