cmake_minimum_required(VERSION 3.22)
project(DoodleSynth VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(DS_NEEDS_WEBVIEW2 FALSE)
if (WIN32)
    set(DS_NEEDS_WEBVIEW2 TRUE)
endif()

juce_add_plugin(DoodleSynth
    COMPANY_NAME "TYF"
    BUNDLE_ID "tv.tellyourfriends.doodlesynth"
    PRODUCT_NAME "Doodle Synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Tyfv
    PLUGIN_CODE Ddls
    VST3_CATEGORIES Instrument Synth
    FORMATS VST3 Standalone
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 ${DS_NEEDS_WEBVIEW2}
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(DoodleSynthData SOURCES ui/index.html ui/default.json)

target_sources(DoodleSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(DoodleSynth PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if (WIN32)
    target_compile_definitions(DoodleSynth PUBLIC JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

target_link_libraries(DoodleSynth
    PRIVATE
        DoodleSynthData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
