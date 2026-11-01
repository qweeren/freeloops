# freeloops - free, open-source sample browser
# Copyright (C) 2026 Eren and the freeloops contributors
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)
project(Freeloops VERSION 0.1.0)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)

# The WebView2 SDK (static loader) from NuGet, unpacked where JUCE's FindWebView2 looks for it.
set(WEBVIEW2_VERSION 1.0.3485.44)
FetchContent_Declare(webview2
    URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}
    DOWNLOAD_NAME microsoft.web.webview2.${WEBVIEW2_VERSION}.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR ${CMAKE_BINARY_DIR}/nuget/Microsoft.Web.WebView2.${WEBVIEW2_VERSION})

FetchContent_MakeAvailable(JUCE webview2)
set(JUCE_WEBVIEW2_PACKAGE_LOCATION ${CMAKE_BINARY_DIR}/nuget)

# An instrument so it shows up in FL Studio's Channel Rack "+" menu; previews play through its output.
# Copied to C:\Program Files\Common Files\VST3 after each build: FL Studio treats custom search
# paths as VST2 folders, so it never finds a .vst3 bundle there. The copy fails while FL has the plugin loaded.
juce_add_plugin(Freeloops
    COMPANY_NAME "freeloops"
    PRODUCT_NAME "freeloops"
    PLUGIN_MANUFACTURER_CODE Frlp
    PLUGIN_CODE Flp1
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_WEBVIEW2 TRUE
    VST3_CATEGORIES Instrument Tools
    COPY_PLUGIN_AFTER_BUILD TRUE)

# The web UI (../ui) is compiled into the plugin and served to the WebView by a resource provider.
juce_add_binary_data(FreeloopsUI SOURCES
    ../ui/index.html
    ../ui/styles.css
    ../ui/bridge.js
    ../ui/app.js)

target_sources(Freeloops PRIVATE
    Source/Freesound.cpp
    Source/FreesoundSignIn.cpp
    Source/Library.cpp
    Source/Looperman.cpp
    Source/LoopermanView.cpp
    Source/PreviewPlayer.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(Freeloops PRIVATE cxx_std_17)

target_compile_definitions(Freeloops PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Optional Freesound credentials compiled in, for personal builds. Each key is limited to 2,000 requests a
# day, so builds for other people should leave these empty: users then paste their own key into the UI.
set(FREELOOPS_FREESOUND_CLIENT_ID "" CACHE STRING "Freesound API client id built into the plugin (optional)")
set(FREELOOPS_FREESOUND_API_KEY "" CACHE STRING "Freesound API key built into the plugin (optional)")
if(FREELOOPS_FREESOUND_CLIENT_ID AND FREELOOPS_FREESOUND_API_KEY)
    target_compile_definitions(Freeloops PRIVATE
        FREELOOPS_FREESOUND_CLIENT_ID=${FREELOOPS_FREESOUND_CLIENT_ID}
        FREELOOPS_FREESOUND_API_KEY=${FREELOOPS_FREESOUND_API_KEY})
endif()

# Parser check against a saved Looperman page (see tests/LoopermanParseTest.cpp).
juce_add_console_app(LoopermanParseTest PRODUCT_NAME "LoopermanParseTest")
target_sources(LoopermanParseTest PRIVATE tests/LoopermanParseTest.cpp Source/Looperman.cpp)
target_compile_features(LoopermanParseTest PRIVATE cxx_std_17)
target_compile_definitions(LoopermanParseTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(LoopermanParseTest PRIVATE juce::juce_core juce::juce_events
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Parser check for Freesound's API replies (tests/freesound-search.json is a hand-written sample reply).
juce_add_console_app(FreesoundParseTest PRODUCT_NAME "FreesoundParseTest")
target_sources(FreesoundParseTest PRIVATE tests/FreesoundParseTest.cpp Source/Freesound.cpp)
target_compile_features(FreesoundParseTest PRIVATE cxx_std_17)
target_compile_definitions(FreesoundParseTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(FreesoundParseTest PRIVATE juce::juce_core juce::juce_events juce::juce_data_structures
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

target_link_libraries(Freeloops
    PRIVATE
        FreeloopsUI
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
