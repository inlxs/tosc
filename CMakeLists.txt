cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(Tosc VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- Windows: WebView2 SDK (headers + static loader) from NuGet, fetched on first configure
if(WIN32)
    set(WV2_VERSION 1.0.2903.40)
    set(WV2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/nuget/Microsoft.Web.WebView2.${WV2_VERSION}")
    if(NOT EXISTS "${WV2_DIR}/build/native/include/WebView2.h")
        message(STATUS "Downloading Microsoft.Web.WebView2 ${WV2_VERSION}")
        file(DOWNLOAD "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WV2_VERSION}"
             "${WV2_DIR}/package.zip" STATUS wv2_status TLS_VERIFY ON)
        list(GET wv2_status 0 wv2_code)
        if(NOT wv2_code EQUAL 0)
            message(FATAL_ERROR "WebView2 download failed: ${wv2_status}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${WV2_DIR}/package.zip" DESTINATION "${WV2_DIR}")
        file(REMOVE "${WV2_DIR}/package.zip")
    endif()
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/nuget")
endif()

add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

set(TO_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND TO_FORMATS AU)
endif()

juce_add_plugin(Tosc
    COMPANY_NAME "Okllo"
    COMPANY_WEBSITE "https://okllo.com"
    BUNDLE_ID "com.okllo.tosc"
    PLUGIN_MANUFACTURER_CODE Oklo
    PLUGIN_CODE Tosc
    FORMATS ${TO_FORMATS}
    PRODUCT_NAME "Tosc"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEBVIEW2 TRUE          # Windows: WebView2
    NEEDS_WEB_BROWSER TRUE       # Linux: webkit2gtk
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

target_sources(Tosc PRIVATE
    src/WavetableBank.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

file(GLOB UI_FILES CONFIGURE_DEPENDS ui/*.html ui/*.js ui/*.css ui/vendor/*.js ui/vendor/*.css ui/vendor/images/*.png)
juce_add_binary_data(ToscData SOURCES resources/Default.wt ${UI_FILES})

target_compile_definitions(Tosc PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Tosc
    PRIVATE
        ToscData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET Tosc
    CLAP_ID "com.okllo.tosc"
    CLAP_SUPPORT_URL "https://okllo.com"
    CLAP_FEATURES instrument synthesizer stereo)

# Offline render test: plays notes through the processor and writes a WAV
juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
target_sources(RenderTest PRIVATE tests/RenderTest.cpp src/WavetableBank.cpp src/PluginProcessor.cpp)
target_compile_definitions(RenderTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 TERRAIN_HEADLESS=1 JucePlugin_Name="Tosc")
target_link_libraries(RenderTest PRIVATE
    ToscData juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags)
