cmake_minimum_required(VERSION 3.22)

project(BPTrigger VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Link the C++ runtime into the plug-in, so it runs without the Visual C++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

FetchContent_Declare(juce
    URL https://github.com/juce-framework/JUCE/archive/2cdfca8feb300fb424002ba2c2751569e5bacb64.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(juce)

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

set(BP_TRIGGER_SOURCES
    Source/Curve.h
    Source/Curve.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/AccessibleComboBox.h
    Source/UI/AccessibleSlider.h
    Source/UI/AccessibleToggleButton.h
    Source/UI/CurveEditor.h
    Source/UI/CurveEditor.cpp
    Source/UI/ScreenReader.h
    Source/UI/ScreenReader.cpp)

if(WIN32)
    # UI Automation calls for Windows screen readers, kept apart from the JUCE sources.
    list(APPEND BP_TRIGGER_SOURCES Source/UI/WindowsUIAutomation.cpp)
endif()

target_sources(BPTrigger PRIVATE ${BP_TRIGGER_SOURCES})

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

if(WIN32)
    target_link_libraries(BPTrigger PRIVATE oleaut32)
endif()

# Test tools: an offline host for the built VST3, and a check of the curve editor's model.
# On Windows, also the editor driven from the keyboard while a UI Automation client records
# what a screen reader hears.
option(BP_TRIGGER_BUILD_TESTS "Build the test tools" OFF)

if(BP_TRIGGER_BUILD_TESTS)
    juce_add_console_app(BPTriggerCheck PRODUCT_NAME "BPTriggerCheck")
    target_sources(BPTriggerCheck PRIVATE Tools/BPTriggerCheck.cpp)
    target_compile_definitions(BPTriggerCheck PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(BPTriggerCheck
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    juce_add_console_app(BPTriggerCurveTest PRODUCT_NAME "BPTriggerCurveTest")
    target_sources(BPTriggerCurveTest PRIVATE Tools/BPTriggerCurveTest.cpp Source/Curve.cpp)
    target_compile_definitions(BPTriggerCurveTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(BPTriggerCurveTest
        PRIVATE
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

if(BP_TRIGGER_BUILD_TESTS AND WIN32)
    juce_add_console_app(BPTriggerUiTest PRODUCT_NAME "BPTriggerUiTest")
    target_sources(BPTriggerUiTest PRIVATE Tools/BPTriggerUiTest.cpp ${BP_TRIGGER_SOURCES})
    target_include_directories(BPTriggerUiTest PRIVATE Source)
    target_compile_definitions(BPTriggerUiTest PRIVATE
        JucePlugin_Name="BP-Trigger"
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(BPTriggerUiTest
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            oleaut32
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Built next to BPTriggerUiTest, which starts it.
    add_executable(BPTriggerUiaProbe Tools/BPTriggerUiaProbe.cpp)
    target_link_libraries(BPTriggerUiaProbe PRIVATE ole32 oleaut32)
    set_target_properties(BPTriggerUiaProbe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/BPTriggerUiTest_artefacts/$<CONFIG>")
    add_dependencies(BPTriggerUiTest BPTriggerUiaProbe)
endif()
