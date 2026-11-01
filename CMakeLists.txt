cmake_minimum_required(VERSION 3.22)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "Minimum macOS version")
endif()
project(OnjSoundCanvasPilot VERSION 1.0.0 LANGUAGES C CXX OBJCXX)
if(NOT APPLE)
    message(FATAL_ERROR "Sound Canvas Bridge currently supports macOS only")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED JUCE_DIR)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 91ad83ae34a81e0833b1a2b0866f54846370ae53)
    FetchContent_MakeAvailable(juce)
else()
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce")
endif()
set(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

juce_add_plugin(SoundCanvasBridge
    COMPANY_NAME OnjMusic BUNDLE_ID me.onj.soundcanvasbridge
    PLUGIN_MANUFACTURER_CODE OnjM PLUGIN_CODE ScCB
    FORMATS AU VST3 IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE PRODUCT_NAME "Sound Canvas Bridge")
set_target_properties(SoundCanvasBridge SoundCanvasBridge_AU SoundCanvasBridge_VST3
    PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
target_sources(SoundCanvasBridge PRIVATE "${SOURCE}/Processor.cpp")
target_compile_definitions(SoundCanvasBridge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_FORCE_USE_LEGACY_PARAM_IDS=1)
target_link_libraries(SoundCanvasBridge PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

juce_add_gui_app(SoundCanvasStandalone PRODUCT_NAME "Sound Canvas Bridge"
    COMPANY_NAME OnjMusic BUNDLE_ID me.onj.soundcanvasbridge.manager VERSION ${PROJECT_VERSION})
set_target_properties(SoundCanvasStandalone PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
target_sources(SoundCanvasStandalone PRIVATE "${SOURCE}/Standalone.cpp" "${SOURCE}/Processor.cpp")
set_source_files_properties("${SOURCE}/Standalone.cpp" PROPERTIES LANGUAGE OBJCXX)
target_compile_definitions(SoundCanvasStandalone PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    SCBRIDGE_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(SoundCanvasStandalone PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE HELPER_SDK
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    COMMAND "${CMAKE_CXX_COMPILER}" -std=c++17 -O2 -arch x86_64 -mmacosx-version-min=12.0 -isysroot "${HELPER_SDK}"
        "${SOURCE}/Helper.cpp" -o "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    DEPENDS "${SOURCE}/Helper.cpp" "${SOURCE}/Protocol.h" "${SOURCE}/Controls.h"
        "${SOURCE}/CoreState.h" "${SOURCE}/Scene.h" "${SOURCE}/WorkerScheduling.h" "${SOURCE}/ControllerMirror.h"
    VERBATIM)
add_custom_target(SoundCanvasHelperBuild DEPENDS "${CMAKE_BINARY_DIR}/SoundCanvasHelper")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance"
    COMMAND xcrun swiftc -swift-version 5 -O -target arm64-apple-macosx12.0
        "${SOURCE}/Maintenance.swift" -o "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance-arm64"
    COMMAND xcrun swiftc -swift-version 5 -O -target x86_64-apple-macosx12.0
        "${SOURCE}/Maintenance.swift" -o "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance-x86_64"
    COMMAND lipo -create "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance-arm64" "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance-x86_64"
        -output "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance"
    DEPENDS "${SOURCE}/Maintenance.swift" VERBATIM)
add_custom_target(SoundCanvasMaintenanceBuild DEPENDS "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance")

configure_file(Resources/InstallerInfo.plist.in "${CMAKE_BINARY_DIR}/InstallerInfo.plist" @ONLY)
add_custom_target(SoundCanvasInstallerBundle
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/Install Sound Canvas Bridge.app/Contents/MacOS"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance"
        "${CMAKE_BINARY_DIR}/Install Sound Canvas Bridge.app/Contents/MacOS/SoundCanvasMaintenance"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_BINARY_DIR}/InstallerInfo.plist"
        "${CMAKE_BINARY_DIR}/Install Sound Canvas Bridge.app/Contents/Info.plist"
    COMMAND codesign --force --deep --sign - "${CMAKE_BINARY_DIR}/Install Sound Canvas Bridge.app"
    DEPENDS SoundCanvasMaintenanceBuild VERBATIM)
add_custom_target(SoundCanvasStandaloneBundle
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Manual.html"
        "$<TARGET_BUNDLE_DIR:SoundCanvasStandalone>/Contents/Resources/Manual.html"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/VersionHistory.html"
        "$<TARGET_BUNDLE_DIR:SoundCanvasStandalone>/Contents/Resources/VersionHistory.html"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
        "$<TARGET_BUNDLE_DIR:SoundCanvasStandalone>/Contents/Resources/SoundCanvasHelper"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance"
        "$<TARGET_BUNDLE_DIR:SoundCanvasStandalone>/Contents/Resources/SoundCanvasMaintenance"
    COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:SoundCanvasStandalone>"
    DEPENDS SoundCanvasStandalone SoundCanvasHelperBuild SoundCanvasMaintenanceBuild VERBATIM)
add_custom_target(SoundCanvasPlugins
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
        "$<TARGET_BUNDLE_DIR:SoundCanvasBridge_VST3>/Contents/Resources/SoundCanvasHelper"
    COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:SoundCanvasBridge_VST3>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
        "$<TARGET_BUNDLE_DIR:SoundCanvasBridge_AU>/Contents/Resources/SoundCanvasHelper"
    COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:SoundCanvasBridge_AU>"
    DEPENDS SoundCanvasBridge_VST3 SoundCanvasBridge_AU SoundCanvasHelperBuild VERBATIM)

include(CTest)
if(BUILD_TESTING)
    foreach(name ControllerMirror Scene)
        add_executable(${name}Tests "Tests/${name}Tests.cpp")
        target_include_directories(${name}Tests PRIVATE Source)
        set_target_properties(${name}Tests PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
        add_test(NAME ${name} COMMAND ${name}Tests)
    endforeach()
endif()
