cmake_minimum_required(VERSION 3.22)
project(TerrainOscillator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# WebView2 SDK (headers + static loader) vendored from NuGet: libs/nuget/Microsoft.Web.WebView2.*
set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/nuget")

add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(TerrainOscillator
    COMPANY_NAME "Okllo"
    COMPANY_WEBSITE "https://okllo.com"
    BUNDLE_ID "com.okllo.terrainoscillator"
    PLUGIN_MANUFACTURER_CODE Oklo
    PLUGIN_CODE Tosc
    FORMATS Standalone VST3
    PRODUCT_NAME "Terrain Oscillator"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

target_sources(TerrainOscillator PRIVATE
    src/WavetableBank.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

file(GLOB UI_FILES CONFIGURE_DEPENDS ui/*.html ui/*.js ui/*.css ui/vendor/*.js ui/vendor/*.css ui/vendor/images/*.png)
juce_add_binary_data(TerrainOscillatorData SOURCES resources/Default.wt ${UI_FILES})

target_compile_definitions(TerrainOscillator PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TerrainOscillator
    PRIVATE
        TerrainOscillatorData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET TerrainOscillator
    CLAP_ID "com.okllo.terrain-oscillator"
    CLAP_SUPPORT_URL "https://okllo.com"
    CLAP_FEATURES instrument synthesizer stereo)

# Offline render test: plays notes through the processor and writes a WAV
juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
target_sources(RenderTest PRIVATE tests/RenderTest.cpp src/WavetableBank.cpp src/PluginProcessor.cpp)
target_compile_definitions(RenderTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 TERRAIN_HEADLESS=1 JucePlugin_Name="Terrain Oscillator")
target_link_libraries(RenderTest PRIVATE
    TerrainOscillatorData juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)
