cmake_minimum_required(VERSION 3.22)
project(TapeSweet VERSION 0.10.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

add_subdirectory(JUCE)

juce_add_plugin(TapeSweet
    COMPANY_NAME            "skein12"
    BUNDLE_ID               "com.skein12.TapeSweet"
    PLUGIN_MANUFACTURER_CODE Skn1
    PLUGIN_CODE             Tswt
    FORMATS                 AU VST3 Standalone
    PRODUCT_NAME            "TapeSweet"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(TapeSweet PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(TapeSweet PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(TapeSweet
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
