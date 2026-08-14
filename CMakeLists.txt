cmake_minimum_required(VERSION 3.22)
project(DMXLightController VERSION 1.3.0 LANGUAGES C CXX)

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
        Source/UserProfileStore.cpp
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

# ---------- Quick Light menu bar app ----------
# A separate, deliberately tiny app for the "I just want the lights on"
# case — no DAW, no sequencer, one colour across the rig. It shares the
# plugin's fixture model and DMX-over-MIDI encoding (FixtureProfile.h,
# PatternData.h, UserProfileStore) so a rig described in one addresses the
# same channels in the other, but it keeps its own settings file because
# the plugin's fixture layout lives inside a DAW project.
option(LC1X_BUILD_QUICKLIGHT "Build the LC-1X+ Quick Light menu bar app" ON)

if(LC1X_BUILD_QUICKLIGHT)
    # LSUIElement makes it an agent app: it lives in the menu bar and nowhere
    # else, with no Dock icon. This goes through PLIST_TO_MERGE rather than
    # MACOSX_BUNDLE_INFO_PLIST, because overriding the latter would throw away
    # the Info.plist JUCE generates — bundle identifier, version strings and
    # all — leaving a bundle that codesign and pkgbuild can't identify.
    juce_add_gui_app(LC1XQuickLight
        PRODUCT_NAME    "LC-1X+ Quick Light"
        COMPANY_NAME    "AMFAS"
        BUNDLE_ID       "com.amfas.lc1xquicklight"
        PLIST_TO_MERGE  "<plist><dict><key>LSUIElement</key><true/></dict></plist>"
    )

    juce_generate_juce_header(LC1XQuickLight)

    target_sources(LC1XQuickLight
        PRIVATE
            Source/QuickLight/QuickLightMain.cpp
            Source/QuickLight/QuickLightEngine.cpp
            Source/UserProfileStore.cpp
    )

    target_include_directories(LC1XQuickLight
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
            ${CMAKE_CURRENT_SOURCE_DIR}/Source/QuickLight
    )

    target_compile_features(LC1XQuickLight PRIVATE cxx_std_17)

    target_compile_definitions(LC1XQuickLight
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_APPLICATION_NAME_STRING="LC-1X+ Quick Light"
            JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    )

    target_link_libraries(LC1XQuickLight
        PRIVATE
            juce::juce_audio_devices
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    # Signing is handled with the plugin bundles at the bottom of this file,
    # so there's one place that knows about the Developer ID identity.
endif()

# ---------- Unit tests (optional, headless) ----------
# Build with:  cmake -B build -DLC1X_BUILD_TESTS=ON  &&  cmake --build build --target LC1XTests
# Run:         ./build/LC1XTests_artefacts/LC1XTests   (path varies per generator/config)
option(LC1X_BUILD_TESTS "Build the LC1X unit test runner" OFF)

if(LC1X_BUILD_TESTS)
    juce_add_console_app(LC1XTests PRODUCT_NAME "LC1XTests")

    target_sources(LC1XTests
        PRIVATE
            Tests/TestMain.cpp
            Tests/EngineTests.cpp
            Tests/ProcessorTests.cpp
    )

    # The realtime-safety tests replace global operator new to count
    # allocations, so the runner must not be built with an allocator that
    # bypasses it.
    target_include_directories(LC1XTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests)

    # Link the plugin's shared-code target: pulls in the compiled Source/
    # files, the JUCE modules, the generated JuceHeader.h include path and
    # the JucePlugin_* definitions.
    target_link_libraries(LC1XTests PRIVATE DMXLightController)

    target_include_directories(LC1XTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
            # The generated JuceHeader.h lives in the plugin target's
            # generated-sources directory and isn't propagated by linking.
            $<TARGET_PROPERTY:DMXLightController,JUCE_GENERATED_SOURCES_DIRECTORY>
            # Module headers for JuceHeader.h — the module *code* comes from
            # the linked shared-code static library, headers resolve here.
            ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/modules)

    target_compile_features(LC1XTests PRIVATE cxx_std_17)

    enable_testing()
    add_test(NAME LC1XTests COMMAND LC1XTests)
endif()

# ---------- macOS code signing (optional) ----------
# Leave empty for unsigned local dev builds. Set to a full Developer ID
# Application identity string to sign every bundle post-build, e.g.:
#   -DLC1X_CODESIGN_IDENTITY="Developer ID Application: Stephen McLeod Blythe (2N9AC8M66C)"
set(LC1X_CODESIGN_IDENTITY "" CACHE STRING
    "macOS Developer ID Application identity for post-build codesign (empty = unsigned)")

if(APPLE AND LC1X_CODESIGN_IDENTITY)
    set(LC1X_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Entitlements.plist")

    # The plugin bundles are named DMXLightController_<FORMAT>; Quick Light is
    # a plain target, so it's listed by its own name.
    foreach(_tgt DMXLightController_AU DMXLightController_VST3
                 DMXLightController_Standalone LC1XQuickLight)
        if(TARGET ${_tgt})
            # Disable Xcode's built-in code signing — we do it explicitly below
            # with the right identity, entitlements, and hardened runtime flags.
            # Without this, Xcode picks up any Developer ID cert it finds in the
            # keychain and runs its own signing phase, which clashes with ours
            # (especially for VST3, where a helper tool produces moduleinfo.xml).
            set_target_properties(${_tgt} PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY       ""
                XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED    "NO"
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED     "NO"
            )

            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo
                    "Codesigning $<TARGET_BUNDLE_DIR:${_tgt}> with ${LC1X_CODESIGN_IDENTITY}"
                COMMAND codesign --force --options runtime --timestamp
                    --sign "${LC1X_CODESIGN_IDENTITY}"
                    --entitlements "${LC1X_ENTITLEMENTS}"
                    "$<TARGET_BUNDLE_DIR:${_tgt}>"
                VERBATIM)
        endif()
    endforeach()
endif()
