cmake_minimum_required(VERSION 3.22)
project(DMXLightController VERSION 1.0.0 LANGUAGES CXX)

# ---------- macOS universal binary (Intel + Apple Silicon) ----------
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---------- JUCE ----------
# JUCE must be cloned into this directory:
#   git clone https://github.com/juce-framework/JUCE.git
add_subdirectory(JUCE)

# ---------- Plugin target ----------
juce_add_plugin(DMXLightController
    COMPANY_NAME             "AMFAS"
    IS_SYNTH                 FALSE
    IS_MIDI_EFFECT           TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Amfs
    PLUGIN_CODE              Dmxl
    FORMATS                  AU VST3 Standalone     # AU for Logic, VST3 for others, Standalone for testing
    PRODUCT_NAME             "LC-1X+ MIDI2DMX"
    BUNDLE_ID                "com.amfas.lc1xplusmidi2dmx"

    # AU-specific: tell the host this is a MIDI processor (aumi)
    AU_MAIN_TYPE             "kAudioUnitType_MIDIProcessor"
)

# ---------- Sources ----------
target_sources(DMXLightController
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

# ---------- Include path for our headers ----------
target_include_directories(DMXLightController
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# ---------- C++17 ----------
target_compile_features(DMXLightController PRIVATE cxx_std_17)

# ---------- Compile definitions ----------
target_compile_definitions(DMXLightController
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# ---------- JUCE modules ----------
juce_generate_juce_header(DMXLightController)

target_link_libraries(DMXLightController
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
