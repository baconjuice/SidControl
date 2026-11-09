cmake_minimum_required(VERSION 3.22)

project(SidControl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# Core library: SysEx codec, patch model, parameter tables (no GUI)
# ---------------------------------------------------------------------------
add_library(sid_core STATIC
    Source/sid/Patch.cpp
    Source/sid/SysEx.cpp
    Source/sid/InitPatches.cpp
    Source/sid/Params.cpp
    Source/sid/ParamNames.cpp
    Source/sid/SyxFile.cpp)
target_include_directories(sid_core PUBLIC Source)
target_compile_features(sid_core PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# Plugin + standalone app
# ---------------------------------------------------------------------------
juce_add_plugin(SidControl
    COMPANY_NAME "SidControl"
    PRODUCT_NAME "SidControl"
    BUNDLE_ID "com.sidcontrol.sidcontrol"
    PLUGIN_MANUFACTURER_CODE Sidc
    PLUGIN_CODE Sdv2
    FORMATS Standalone AU VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(SidControl PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/device/SidDevice.cpp
    Source/model/Session.cpp
    Source/model/EnsembleSession.cpp
    Source/model/MidiLearn.cpp
    Source/ui/ParamControls.cpp
    Source/ui/ConnectionBar.cpp
    Source/ui/LibraryPanel.cpp
    Source/ui/EditorCommon.cpp
    Source/ui/LeadEditor.cpp
    Source/ui/BasslineEditor.cpp
    Source/ui/DrumEditor.cpp
    Source/ui/MultiEditor.cpp
    Source/ui/EnsembleEditor.cpp
    Source/ui/LookAndFeel.cpp)

target_compile_definitions(SidControl PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SidControl
    PRIVATE
        sid_core
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests (plain C++, no framework)
# ---------------------------------------------------------------------------
enable_testing()
add_executable(sid_tests Tests/CoreTests.cpp)
target_link_libraries(sid_tests PRIVATE sid_core)
add_test(NAME sid_tests COMMAND sid_tests)

# MIDI learn end to end (device + session, no MIDI ports needed)
juce_add_console_app(learn_tests PRODUCT_NAME "learn_tests")
target_sources(learn_tests PRIVATE Tests/LearnTests.cpp Source/device/SidDevice.cpp Source/model/Session.cpp
               Source/model/EnsembleSession.cpp Source/model/MidiLearn.cpp)
target_include_directories(learn_tests PRIVATE Source)
target_compile_definitions(learn_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(learn_tests PRIVATE sid_core juce::juce_audio_devices juce::juce_data_structures juce::juce_events
                      juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME learn_tests COMMAND learn_tests)

# ---------------------------------------------------------------------------
# sidcli: command-line diagnostics (port scan, ping, edit buffer read)
# ---------------------------------------------------------------------------
juce_add_console_app(sidcli PRODUCT_NAME "sidcli")
target_sources(sidcli PRIVATE Tools/SidCli.cpp Source/device/SidDevice.cpp)
target_compile_definitions(sidcli PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(sidcli PRIVATE sid_core juce::juce_audio_devices juce::juce_events
                      juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
