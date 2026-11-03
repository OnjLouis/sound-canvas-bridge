cmake_minimum_required(VERSION 3.22)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "Minimum macOS version")
endif()
project(OnjSoundCanvasPilot VERSION 1.3.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
elseif(NOT WIN32)
    message(FATAL_ERROR "Sound Canvas Bridge supports macOS and Windows")
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

set(SCBRIDGE_FORMATS VST3)
set(SCBRIDGE_AUTO_MANIFEST TRUE)
if(APPLE)
    list(APPEND SCBRIDGE_FORMATS AU)
else()
    set(SCBRIDGE_AUTO_MANIFEST FALSE)
endif()
juce_add_plugin(SoundCanvasBridge
    COMPANY_NAME OnjMusic BUNDLE_ID me.onj.soundcanvasbridge
    PLUGIN_MANUFACTURER_CODE OnjM PLUGIN_CODE ScCB
    FORMATS ${SCBRIDGE_FORMATS} IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    VST3_AUTO_MANIFEST ${SCBRIDGE_AUTO_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD FALSE PRODUCT_NAME "Sound Canvas Bridge")
if(APPLE)
    set_target_properties(SoundCanvasBridge SoundCanvasBridge_AU SoundCanvasBridge_VST3
        PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
endif()
target_sources(SoundCanvasBridge PRIVATE "${SOURCE}/Processor.cpp")
target_compile_definitions(SoundCanvasBridge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_FORCE_USE_LEGACY_PARAM_IDS=1)
target_link_libraries(SoundCanvasBridge PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

juce_add_gui_app(SoundCanvasStandalone PRODUCT_NAME "Sound Canvas Bridge"
    COMPANY_NAME OnjMusic BUNDLE_ID me.onj.soundcanvasbridge.manager VERSION ${PROJECT_VERSION})
set_target_properties(SoundCanvasStandalone PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
target_sources(SoundCanvasStandalone PRIVATE "${SOURCE}/Standalone.cpp" "${SOURCE}/Processor.cpp")
if(APPLE)
    set_source_files_properties("${SOURCE}/Standalone.cpp" PROPERTIES LANGUAGE OBJCXX)
endif()
target_compile_definitions(SoundCanvasStandalone PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    SCBRIDGE_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(SoundCanvasStandalone PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

if(APPLE)
execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE HELPER_SDK
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    COMMAND "${CMAKE_CXX_COMPILER}" -std=c++17 -O2 -arch x86_64 -mmacosx-version-min=12.0 -isysroot "${HELPER_SDK}"
        "${SOURCE}/Helper.cpp" -o "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    DEPENDS "${SOURCE}/Helper.cpp" "${SOURCE}/Protocol.h" "${SOURCE}/Controls.h"
        "${SOURCE}/CoreState.h" "${SOURCE}/Scene.h" "${SOURCE}/WorkerScheduling.h" "${SOURCE}/ControllerMirror.h"
        "${SOURCE}/Insertion.h" "${SOURCE}/ControlMessage.h"
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
else()
    if(NOT ASIO_SDK_DIR OR NOT EXISTS "${ASIO_SDK_DIR}/common/asio.h")
        message(FATAL_ERROR "Set ASIO_SDK_DIR to the official ASIO SDK (GPLv3 option)")
    endif()
    set(SCBRIDGE_CSC "$ENV{SystemRoot}/Microsoft.NET/Framework64/v4.0.30319/csc.exe" CACHE FILEPATH "C# compiler for the native Windows maintenance interface")
    configure_file(Resources/WindowsVersion.cs.in "${CMAKE_BINARY_DIR}/WindowsVersion.cs" @ONLY)
    file(TO_NATIVE_PATH "${SOURCE}/MaintenanceWindows.cs" SCBRIDGE_MAINTENANCE_SOURCE)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/WindowsVersion.cs" SCBRIDGE_MAINTENANCE_VERSION)
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Resources/WindowsUpdateKey.xml" SCBRIDGE_MAINTENANCE_KEY)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance.exe" SCBRIDGE_MAINTENANCE_OUTPUT)
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance.exe"
        COMMAND "${SCBRIDGE_CSC}" /nologo /target:winexe /platform:x64 /optimize+ /utf8output
            /r:System.dll /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll
            /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:Microsoft.CSharp.dll
            "/resource:${SCBRIDGE_MAINTENANCE_KEY},WindowsUpdateKey"
            "/out:${SCBRIDGE_MAINTENANCE_OUTPUT}"
            "${SCBRIDGE_MAINTENANCE_SOURCE}" "${SCBRIDGE_MAINTENANCE_VERSION}"
        DEPENDS "${SOURCE}/MaintenanceWindows.cs" "${CMAKE_BINARY_DIR}/WindowsVersion.cs" Resources/WindowsUpdateKey.xml VERBATIM)
    add_custom_target(SoundCanvasMaintenanceBuild DEPENDS "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance.exe")
    set(SCBRIDGE_INSTALLER_INPUT "" CACHE PATH "Folder containing payload.zip and signed manifest.json/manifest.sig")
    if(SCBRIDGE_INSTALLER_INPUT)
        find_program(SCBRIDGE_ISCC NAMES ISCC.exe ISCC REQUIRED)
        foreach(name payload.zip manifest.json manifest.sig)
            if(NOT EXISTS "${SCBRIDGE_INSTALLER_INPUT}/${name}")
                message(FATAL_ERROR "Missing signed installer input: ${name}")
            endif()
        endforeach()
        file(TO_NATIVE_PATH "${SCBRIDGE_INSTALLER_INPUT}/payload.zip" installer_payload)
        file(TO_NATIVE_PATH "${SCBRIDGE_INSTALLER_INPUT}/manifest.json" installer_manifest)
        file(TO_NATIVE_PATH "${SCBRIDGE_INSTALLER_INPUT}/manifest.sig" installer_signature)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/SoundCanvasBridge-${PROJECT_VERSION}-windows-x64.exe" installer_output)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/PayloadInstaller.exe" installer_worker)
        file(TO_NATIVE_PATH "${SOURCE}/WindowsSetupBootstrap.cs" installer_bootstrap_source)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/Wizard/SoundCanvasBridge-${PROJECT_VERSION}-windows-x64.exe" installer_wizard)
        add_custom_target(SoundCanvasWindowsInstaller
            COMMAND "${SCBRIDGE_CSC}" /nologo /target:winexe /platform:x64 /optimize+ /utf8output
                /r:System.dll /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll
                /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:Microsoft.CSharp.dll
                "/resource:${SCBRIDGE_MAINTENANCE_KEY},WindowsUpdateKey"
                "/resource:${installer_payload},Payload" "/resource:${installer_manifest},Manifest" "/resource:${installer_signature},Signature"
                "/out:${installer_worker}" "${SCBRIDGE_MAINTENANCE_SOURCE}" "${SCBRIDGE_MAINTENANCE_VERSION}"
            COMMAND "${SCBRIDGE_ISCC}" "/DBridgeVersion=${PROJECT_VERSION}"
                "/DPayloadWorker=${installer_worker}" "/DMaintenanceWorker=${SCBRIDGE_MAINTENANCE_OUTPUT}"
                "/DInstallerOutput=${CMAKE_BINARY_DIR}/Wizard" "${CMAKE_CURRENT_SOURCE_DIR}/Resources/WindowsInstaller.iss"
            COMMAND "${SCBRIDGE_CSC}" /nologo /target:winexe /platform:x64 /optimize+ /utf8output
                /r:System.dll /r:System.Windows.Forms.dll
                "/resource:${installer_wizard},Wizard"
                "/out:${installer_output}" "${installer_bootstrap_source}" "${SCBRIDGE_MAINTENANCE_VERSION}"
            DEPENDS SoundCanvasMaintenanceBuild VERBATIM)
    endif()
    option(SCBRIDGE_BUILD_EXPERIMENTAL_MIDI "Build the development-only Windows MIDI Services helper" OFF)
    target_compile_definitions(SoundCanvasStandalone PRIVATE JUCE_ASIO=1 JUCE_WASAPI=1)
    target_include_directories(SoundCanvasStandalone PRIVATE "${ASIO_SDK_DIR}/common")
    foreach(target SoundCanvasBridge SoundCanvasStandalone)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(${target} PRIVATE avrt)
        # JUCE's resource rule does not depend on its version metadata input.
        get_target_property(resource_dir ${target} JUCE_GENERATED_SOURCES_DIRECTORY)
        get_target_property(resource_info ${target} JUCE_INFO_FILE)
        add_custom_command(OUTPUT "${resource_dir}/${target}_resources.rc" APPEND DEPENDS "${resource_info}")
    endforeach()
    add_executable(SoundCanvasHelper "${SOURCE}/Helper.cpp")
    target_link_libraries(SoundCanvasHelper PRIVATE avrt bcrypt)
    if(SCBRIDGE_BUILD_EXPERIMENTAL_MIDI)
        if(NOT MIDI_WINRT_INCLUDE OR NOT MIDI_RUNTIME_DLL)
            message(FATAL_ERROR "The development MIDI helper requires the official Windows MIDI Services SDK")
        endif()
        add_executable(SoundCanvasMidiHost "${SOURCE}/VirtualMidiHost.cpp")
        target_compile_features(SoundCanvasMidiHost PRIVATE cxx_std_20)
        target_include_directories(SoundCanvasMidiHost PRIVATE "${MIDI_WINRT_INCLUDE}")
        target_link_libraries(SoundCanvasMidiHost PRIVATE runtimeobject ole32 oleaut32)
    endif()
    get_target_property(SCBRIDGE_MODULES juce_audio_plugin_client INTERFACE_JUCE_MODULE_PATH)
    add_executable(SoundCanvasManifest "${SCBRIDGE_MODULES}/juce_audio_plugin_client/VST3/juce_VST3ManifestHelper.cpp")
    target_compile_definitions(SoundCanvasManifest PRIVATE $<TARGET_PROPERTY:SoundCanvasBridge,COMPILE_DEFINITIONS>)
    target_include_directories(SoundCanvasManifest PRIVATE $<TARGET_PROPERTY:SoundCanvasBridge,INCLUDE_DIRECTORIES>)
    target_link_libraries(SoundCanvasManifest PRIVATE ole32)
    if(MINGW)
        target_link_options(SoundCanvasHelper PRIVATE -municode -static)
        if(TARGET SoundCanvasMidiHost)
            target_link_options(SoundCanvasMidiHost PRIVATE -municode -static)
        endif()
        target_link_options(SoundCanvasStandalone PRIVATE -static)
        target_link_options(SoundCanvasBridge_VST3 PRIVATE -static)
    endif()
    add_custom_target(SoundCanvasStandaloneBundle
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance.exe" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/WindowsUpdateKey.xml" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasHelper>" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/WindowsManual.html" "$<TARGET_FILE_DIR:SoundCanvasStandalone>/Manual.html"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/VersionHistory.html" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
        DEPENDS SoundCanvasStandalone SoundCanvasHelper SoundCanvasMaintenanceBuild VERBATIM)
    if(SCBRIDGE_BUILD_EXPERIMENTAL_MIDI)
        add_custom_target(SoundCanvasExperimentalMidiBundle
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasMidiHost>" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MIDI_RUNTIME_DLL}" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
            DEPENDS SoundCanvasStandaloneBundle SoundCanvasMidiHost VERBATIM)
    endif()
    add_custom_target(SoundCanvasPlugins
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:SoundCanvasBridge_VST3>/../Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasHelper>" "$<TARGET_FILE_DIR:SoundCanvasBridge_VST3>/../Resources"
        COMMAND "${CMAKE_COMMAND}" "-DHELPER=$<TARGET_FILE:SoundCanvasManifest>" "-DOUTPUT=$<TARGET_FILE_DIR:SoundCanvasBridge_VST3>/../Resources/moduleinfo.json" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteManifest.cmake"
        DEPENDS SoundCanvasBridge_VST3 SoundCanvasHelper SoundCanvasManifest VERBATIM)
endif()

include(CTest)
if(SCBRIDGE_EXTRA_TESTS)
    include("${SCBRIDGE_EXTRA_TESTS}")
endif()
if(BUILD_TESTING)
    if(APPLE)
        juce_add_console_app(StandaloneUiTests PRODUCT_NAME "Standalone UI Tests")
        target_sources(StandaloneUiTests PRIVATE Tests/StandaloneUiTests.cpp "${SOURCE}/Processor.cpp")
        set_source_files_properties(Tests/StandaloneUiTests.cpp PROPERTIES LANGUAGE OBJCXX)
        target_compile_definitions(StandaloneUiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(StandaloneUiTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
        set_target_properties(StandaloneUiTests PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
        add_test(NAME StandaloneUi COMMAND StandaloneUiTests)
        set_tests_properties(StandaloneUi PROPERTIES ENVIRONMENT "SCBRIDGE_PROFILE_DIR=${CMAKE_CURRENT_BINARY_DIR}/ui-test-profile")
    endif()
    foreach(name ControllerMirror Scene Insertion)
        add_executable(${name}Tests "Tests/${name}Tests.cpp")
        target_include_directories(${name}Tests PRIVATE Source)
        set_target_properties(${name}Tests PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
        add_test(NAME ${name} COMMAND ${name}Tests)
    endforeach()
    add_executable(WakeSignalTests Tests/WakeSignalTests.cpp)
    target_include_directories(WakeSignalTests PRIVATE Source)
    if(WIN32)
        add_executable(WindowsUiTests Tests/WindowsUiTests.cpp "${SOURCE}/Processor.cpp")
        target_compile_definitions(WindowsUiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(WindowsUiTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags avrt)
        add_test(NAME WindowsUi COMMAND WindowsUiTests "${CMAKE_CURRENT_BINARY_DIR}/ui-test-files")
        set_tests_properties(WindowsUi PROPERTIES TIMEOUT 20)
        set_tests_properties(WindowsUi PROPERTIES ENVIRONMENT "SCBRIDGE_PROFILE_DIR=${CMAKE_CURRENT_BINARY_DIR}/ui-test-profile")
        target_link_libraries(WakeSignalTests PRIVATE avrt)
        add_library(MockCoreWindows SHARED Tests/MockCoreWindows.cpp)
        add_executable(WindowsIpcTests Tests/WindowsIpcTests.cpp)
        target_include_directories(WindowsIpcTests PRIVATE Source)
        if(MINGW)
            target_link_options(WindowsIpcTests PRIVATE -municode -static)
        endif()
        add_test(NAME WindowsIpc COMMAND WindowsIpcTests "$<TARGET_FILE:SoundCanvasHelper>" "$<TARGET_FILE:MockCoreWindows>")
        set_tests_properties(WindowsIpc PROPERTIES TIMEOUT 20)
    endif()
    add_test(NAME WakeSignal COMMAND WakeSignalTests)
endif()
