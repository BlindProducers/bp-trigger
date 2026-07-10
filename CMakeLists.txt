cmake_minimum_required(VERSION 3.22)

project(BPTrigger VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED JUCE_DIR OR NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE_DIR must point to a JUCE checkout. "
        "Configure with -DJUCE_DIR=/path/to/JUCE")
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_plugin(BPTrigger
    COMPANY_NAME "Blind Producers"
    BUNDLE_ID "com.blindproducers.bptrigger"
    PLUGIN_MANUFACTURER_CODE BlPr
    PLUGIN_CODE BPTr
    FORMATS AU VST3
    PRODUCT_NAME "BP-Trigger"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(BPTrigger
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/AccessibleSlider.h
        Source/UI/CurveEditor.h
        Source/UI/CurveEditor.cpp)

target_compile_definitions(BPTrigger
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BPTrigger
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
