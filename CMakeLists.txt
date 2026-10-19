cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(BigBossMasteringConsole VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BB_BUILD_PLUGIN "Build the JUCE plugin" ON)
option(BB_BUILD_TESTS  "Build the engine tests and fuzzer" ON)
option(BB_BUILD_SNAPSHOT "Build the UI snapshot tool (renders the console to a PNG)" OFF)

# ---------------------------------------------------------------- engine (no JUCE dependency)
# src/dsp is shared with mingusdynasty/chocolatefudgeverb (namespace cf); keep it in step with that repo.
add_library(bb_engine STATIC
    src/dsp/TapeHead.cpp src/dsp/TapeSaturator.cpp src/dsp/PlateAmp.cpp src/dsp/Oversampler.cpp
    src/eq/ClassAEq.cpp src/tape/TapeMachine.cpp src/dyn/DualCompressor.cpp src/dyn/Limiter.cpp src/deess/DeEsser.cpp
    src/meter/LoudnessMeter.cpp src/meter/TruePeak.cpp src/meter/MeterSuite.cpp
    src/chain/Chain.cpp src/assist/Assistant.cpp)
target_include_directories(bb_engine PUBLIC src)
set_target_properties(bb_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BB_BUILD_TESTS)
    enable_testing()
    add_executable(bb_engine_tests tests/EngineTests.cpp)
    target_link_libraries(bb_engine_tests PRIVATE bb_engine)
    add_test(NAME engine COMMAND bb_engine_tests)

    add_executable(bb_module_tests tests/ModuleTests.cpp)
    target_link_libraries(bb_module_tests PRIVATE bb_engine)
    add_test(NAME modules COMMAND bb_module_tests)

    add_executable(bb_dynamics_tests tests/DynamicsTests.cpp)
    target_link_libraries(bb_dynamics_tests PRIVATE bb_engine)
    add_test(NAME dynamics COMMAND bb_dynamics_tests)

    add_executable(bb_deesser_tests tests/DeEsserTests.cpp)
    target_link_libraries(bb_deesser_tests PRIVATE bb_engine)
    add_test(NAME deesser COMMAND bb_deesser_tests)

    add_executable(bb_assist_tests tests/AssistTests.cpp)
    target_link_libraries(bb_assist_tests PRIVATE bb_engine)
    add_test(NAME assist COMMAND bb_assist_tests)

    add_executable(bb_fuzz tests/Fuzz.cpp)
    target_link_libraries(bb_fuzz PRIVATE bb_engine)
    add_test(NAME fuzz COMMAND bb_fuzz 0.25)
endif()

# ---------------------------------------------------------------- plugin
if(BB_BUILD_PLUGIN)
    include(FetchContent)
    # To use a local checkout instead: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(BigBoss
        PRODUCT_NAME              "Big Boss Mastering Console"
        COMPANY_NAME              "mingusdynasty"
        BUNDLE_ID                 "com.mingusdynasty.bigboss"
        PLUGIN_MANUFACTURER_CODE  Mgdy
        PLUGIN_CODE               Bbmc
        FORMATS                   VST3 Standalone
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          FALSE
        NEEDS_MIDI_OUTPUT         FALSE
        IS_MIDI_EFFECT            FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   FALSE
        VST3_CATEGORIES           Fx Mastering Analyzer
        DESCRIPTION               "Mastering console: metering and a hardware-modelled chain")

    # rack panel fonts (Barlow, SIL Open Font License, see resources/fonts)
    juce_add_binary_data(BigBossFonts HEADER_NAME BigBossFonts.h NAMESPACE BigBossFonts SOURCES
        resources/fonts/Barlow-SemiBold.ttf
        resources/fonts/Barlow-Bold.ttf
        resources/fonts/BarlowCondensed-Bold.ttf)

    set(BB_PLUGIN_SOURCES
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/Params.cpp src/plugin/Presets.cpp
        src/plugin/ui/Look.cpp
        src/plugin/ui/MeterPanel.cpp
        src/plugin/ui/Spectrum.cpp
        src/plugin/rack/RackLookAndFeel.cpp
        src/plugin/rack/KnobRenderer.cpp
        src/plugin/rack/Widgets.cpp
        src/plugin/rack/Panels.cpp)
    target_sources(BigBoss PRIVATE ${BB_PLUGIN_SOURCES})

    target_compile_definitions(BigBoss PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(BigBoss
        PRIVATE
            bb_engine
            BigBossFonts
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(BB_BUILD_SNAPSHOT)
        juce_add_console_app(bb_snapshot PRODUCT_NAME "bb_snapshot")
        target_sources(bb_snapshot PRIVATE tools/snapshot/Snapshot.cpp ${BB_PLUGIN_SOURCES})
        target_include_directories(bb_snapshot PRIVATE src)
        target_compile_definitions(bb_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            JucePlugin_Name="Big Boss Mastering Console")
        target_link_libraries(bb_snapshot PRIVATE bb_engine BigBossFonts juce::juce_audio_utils juce::juce_dsp
            juce::juce_recommended_config_flags)
    endif()
endif()
