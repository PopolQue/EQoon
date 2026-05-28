cmake_minimum_required(VERSION 3.22)
project(EQoonTestRunner VERSION 1.0.0 LANGUAGES C CXX)

add_subdirectory(/Applications/JUCE JUCE)

juce_add_console_app(EQoonTestRunner)

target_sources(EQoonTestRunner PRIVATE
    Source/TestRunner.cpp
    Source/PluginProcessor.cpp
    Source/FFTAnalyzer.cpp
    Source/Tests.cpp
)

target_include_directories(EQoonTestRunner PRIVATE Source JuceLibraryCode)

target_compile_definitions(EQoonTestRunner PRIVATE
    EQOON_TEST_BUILD=1
    JucePlugin_Name="EQoon"
    JucePlugin_Build_Standalone=0
    JucePlugin_WantsMidiInput=0
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsSynth=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_EditorRequiresKeyboardFocus=0
    JucePlugin_Version=0x01000
    JucePlugin_VersionString="1.0.0"
    JUCE_STANDALONE_APPLICATION=1
)

target_link_libraries(EQoonTestRunner
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_dsp
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME EQoonTestRunner COMMAND EQoonTestRunner)
