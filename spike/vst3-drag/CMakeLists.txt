cmake_minimum_required(VERSION 3.22)
project(FreeloopsSpike VERSION 0.0.1)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# An instrument so it shows up in FL Studio's Channel Rack "+" menu. It outputs silence.
# Copied to C:\Program Files\Common Files\VST3 after each build: FL Studio treats custom search
# paths as VST2 folders, so it never finds a .vst3 bundle there. The copy fails while FL has the plugin loaded.
juce_add_plugin(FreeloopsSpike
    COMPANY_NAME "freeloops"
    PRODUCT_NAME "freeloops spike"
    PLUGIN_MANUFACTURER_CODE Frlp
    PLUGIN_CODE Fls0
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    VST3_CATEGORIES Instrument Tools
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(FreeloopsSpike PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TestLoop.cpp)

target_compile_features(FreeloopsSpike PRIVATE cxx_std_17)

target_compile_definitions(FreeloopsSpike PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FreeloopsSpike
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
