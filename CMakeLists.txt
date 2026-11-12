cmake_minimum_required(VERSION 3.22)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "Minimum macOS version")
endif()
project(OnjSoundCanvasPilot VERSION 1.4.4 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
    option(SCBRIDGE_USE_TRANSLATED_ENGINE "Use the native Mac compatibility worker" ON)
    set(SCBRIDGE_MAC_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac front-end architectures")
    if(SCBRIDGE_USE_TRANSLATED_ENGINE)
        add_compile_definitions(SCBRIDGE_MAC_TRANSLATION_AVAILABLE=1)
    endif()
elseif(NOT WIN32)
    message(FATAL_ERROR "Sound Canvas Bridge supports macOS and Windows")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(SCBRIDGE_WINDOWS7 "Build the separate Windows 7 SP1 portable edition" OFF)
set(scbridge_juce_revision 91ad83ae34a81e0833b1a2b0866f54846370ae53)
if(SCBRIDGE_WINDOWS7)
    if(NOT WIN32)
        message(FATAL_ERROR "SCBRIDGE_WINDOWS7 requires Windows")
    endif()
    if(SCBRIDGE_INSTALLER_INPUT OR SCBRIDGE_BUILD_EXPERIMENTAL_MIDI)
        message(FATAL_ERROR "The Windows 7 edition is portable and uses WinMM MIDI")
    endif()
    add_compile_definitions(SCBRIDGE_WINDOWS7=1 WINVER=0x0601 _WIN32_WINNT=0x0601)
    set(scbridge_juce_revision 4f43011b96eb0636104cb3e433894cda98243626)
    link_libraries(oleacc)
elseif(WIN32)
    link_libraries(uiautomationcore oleaut32)
endif()
if(NOT DEFINED JUCE_DIR)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${scbridge_juce_revision})
    FetchContent_MakeAvailable(juce)
else()
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce")
endif()
set(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
set(scbridge_processor_sources "${SOURCE}/Processor.cpp")
if(APPLE)
    list(APPEND scbridge_processor_sources "${SOURCE}/KeyboardAnnouncement.mm")
elseif(NOT SCBRIDGE_WINDOWS7)
    list(APPEND scbridge_processor_sources "${SOURCE}/KeyboardAnnouncementWindows.cpp")
endif()

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
        PROPERTIES OSX_ARCHITECTURES "${SCBRIDGE_MAC_ARCHITECTURES}")
endif()
target_sources(SoundCanvasBridge PRIVATE ${scbridge_processor_sources})
target_compile_definitions(SoundCanvasBridge PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_FORCE_USE_LEGACY_PARAM_IDS=1)
target_link_libraries(SoundCanvasBridge PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

juce_add_gui_app(SoundCanvasStandalone PRODUCT_NAME "Sound Canvas Bridge"
    COMPANY_NAME OnjMusic BUNDLE_ID me.onj.soundcanvasbridge.manager VERSION ${PROJECT_VERSION})
set_target_properties(SoundCanvasStandalone PROPERTIES OSX_ARCHITECTURES "${SCBRIDGE_MAC_ARCHITECTURES}")
target_sources(SoundCanvasStandalone PRIVATE "${SOURCE}/Standalone.cpp" ${scbridge_processor_sources})
if(APPLE)
    set_source_files_properties("${SOURCE}/Standalone.cpp" PROPERTIES LANGUAGE OBJCXX)
endif()
target_compile_definitions(SoundCanvasStandalone PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    SCBRIDGE_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(SoundCanvasStandalone PRIVATE juce::juce_audio_utils juce::juce_cryptography juce::juce_recommended_config_flags)
include(cmake/OfficialEngine.cmake)

if(APPLE)
if(SCBRIDGE_USE_TRANSLATED_ENGINE)
    include(cmake/TranslatedEngine.cmake)
    scbridge_copy_translated_runtime(SoundCanvasStandalone)
    scbridge_copy_translated_runtime(SoundCanvasBridge_AU)
    scbridge_copy_translated_runtime(SoundCanvasBridge_VST3)
else()
execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE HELPER_SDK
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    COMMAND "${CMAKE_CXX_COMPILER}" -std=c++17 -O2 -arch x86_64 -mmacosx-version-min=12.0 -isysroot "${HELPER_SDK}"
        "${SOURCE}/Helper.cpp" -o "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    DEPENDS "${SOURCE}/Helper.cpp" "${SOURCE}/Protocol.h" "${SOURCE}/Controls.h"
        "${SOURCE}/CoreState.h" "${SOURCE}/Scene.h" "${SOURCE}/WorkerScheduling.h" "${SOURCE}/ControllerMirror.h"
        "${SOURCE}/Insertion.h" "${SOURCE}/ControlMessage.h"
    VERBATIM)
endif()
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
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/DawPresetsMac.zip"
        "$<TARGET_BUNDLE_DIR:SoundCanvasStandalone>/Contents/Resources/DAW Presets.zip"
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
    set(scbridge_windows_manual WindowsManual.html)
    set(scbridge_windows_history VersionHistory.html)
    if(SCBRIDGE_WINDOWS7)
        set(scbridge_windows_manual Windows7Manual.html)
        set(scbridge_windows_history Windows7VersionHistory.html)
    endif()
    add_custom_target(SoundCanvasStandaloneBundle
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/DawPresetsWindows.zip" "$<TARGET_FILE_DIR:SoundCanvasStandalone>/DAW Presets.zip"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasHelper>" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/${scbridge_windows_manual}" "$<TARGET_FILE_DIR:SoundCanvasStandalone>/Manual.html"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/${scbridge_windows_history}" "$<TARGET_FILE_DIR:SoundCanvasStandalone>/VersionHistory.html"
        DEPENDS SoundCanvasStandalone SoundCanvasHelper VERBATIM)
    if(NOT SCBRIDGE_WINDOWS7)
        add_dependencies(SoundCanvasStandaloneBundle SoundCanvasMaintenanceBuild)
        add_custom_command(TARGET SoundCanvasStandaloneBundle POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_BINARY_DIR}/SoundCanvasMaintenance.exe" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Resources/WindowsUpdateKey.xml" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
            VERBATIM)
    else()
        add_executable(SoundCanvasUpdateCheck "${SOURCE}/PortableUpdateCheck.cpp")
        target_compile_definitions(SoundCanvasUpdateCheck PRIVATE JUCE_USE_CURL=0 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(SoundCanvasUpdateCheck PRIVATE juce::juce_core juce::juce_recommended_config_flags)
        add_dependencies(SoundCanvasStandaloneBundle SoundCanvasUpdateCheck)
        add_custom_command(TARGET SoundCanvasStandaloneBundle POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasUpdateCheck>" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
            VERBATIM)
    endif()
    if(SCBRIDGE_BUILD_EXPERIMENTAL_MIDI)
        add_custom_target(SoundCanvasExperimentalMidiBundle
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasMidiHost>" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MIDI_RUNTIME_DLL}" "$<TARGET_FILE_DIR:SoundCanvasStandalone>"
            DEPENDS SoundCanvasStandaloneBundle SoundCanvasMidiHost VERBATIM)
    endif()
    set(scbridge_manifest_options)
    if(SCBRIDGE_WINDOWS7)
        list(APPEND scbridge_manifest_options
            "-DMODULE=$<TARGET_FILE_DIR:SoundCanvasBridge_VST3>/../.."
            "-DVERSION=${PROJECT_VERSION}")
    endif()
    add_custom_target(SoundCanvasPlugins
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:SoundCanvasBridge_VST3>/../Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasHelper>" "$<TARGET_FILE_DIR:SoundCanvasBridge_VST3>/../Resources"
        COMMAND "${CMAKE_COMMAND}" "-DHELPER=$<TARGET_FILE:SoundCanvasManifest>" "-DOUTPUT=$<TARGET_FILE_DIR:SoundCanvasBridge_VST3>/../Resources/moduleinfo.json" ${scbridge_manifest_options} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteManifest.cmake"
        DEPENDS SoundCanvasBridge_VST3 SoundCanvasHelper SoundCanvasManifest VERBATIM)
endif()

include(CTest)
if(SCBRIDGE_EXTRA_TESTS)
    include("${SCBRIDGE_EXTRA_TESTS}")
endif()
if(BUILD_TESTING)
    add_executable(PortableUpdateTests Tests/PortableUpdateTests.cpp)
    target_include_directories(PortableUpdateTests PRIVATE Source)
    target_compile_definitions(PortableUpdateTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(PortableUpdateTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
    add_test(NAME PortableUpdate COMMAND PortableUpdateTests)
    add_executable(BackendTests Tests/BackendTests.cpp)
    if(APPLE)
        set_target_properties(BackendTests PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
    endif()
    add_test(NAME Backend COMMAND BackendTests)
    if(APPLE OR WIN32)
        juce_add_console_app(OfficialEngineTests PRODUCT_NAME "Official Engine Tests")
        target_sources(OfficialEngineTests PRIVATE Tests/OfficialEngineTests.cpp)
        target_compile_definitions(OfficialEngineTests PRIVATE JUCE_USE_CURL=0)
        target_link_libraries(OfficialEngineTests PRIVATE juce::juce_cryptography juce::juce_recommended_config_flags)
        add_test(NAME OfficialEngine COMMAND OfficialEngineTests "${CMAKE_BINARY_DIR}/official-engine-fixture")
    endif()
    add_executable(CoreLayoutTests Tests/CoreLayoutTests.cpp)
    target_compile_features(CoreLayoutTests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(CoreLayoutTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(CoreLayoutTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME CoreLayouts COMMAND CoreLayoutTests)
    juce_add_console_app(DawPresetsTests PRODUCT_NAME "DAW Preset Tests")
    target_sources(DawPresetsTests PRIVATE Tests/DawPresetsTests.cpp)
    target_link_libraries(DawPresetsTests PRIVATE juce::juce_cryptography juce::juce_recommended_config_flags)
    target_compile_definitions(DawPresetsTests PRIVATE JUCE_USE_CURL=0)
    add_test(NAME DawPresets COMMAND DawPresetsTests "${CMAKE_CURRENT_BINARY_DIR}/daw-preset-test-files")
    if(APPLE)
        set(daw_preset_package Resources/DawPresetsMac.zip)
    else()
        set(daw_preset_package Resources/DawPresetsWindows.zip)
    endif()
    add_test(NAME DawPresetPackage COMMAND DawPresetsTests "${CMAKE_CURRENT_BINARY_DIR}/daw-package-test-files" "${CMAKE_CURRENT_SOURCE_DIR}/${daw_preset_package}")
    set_tests_properties(DawPresets PROPERTIES TIMEOUT 60)
    set_tests_properties(DawPresetPackage PROPERTIES TIMEOUT 180)
    juce_add_console_app(PluginUiTests PRODUCT_NAME "Plugin UI Tests")
    target_sources(PluginUiTests PRIVATE Tests/PluginUiTests.cpp ${scbridge_processor_sources})
    target_compile_definitions(PluginUiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 SCBRIDGE_TEST_ANNOUNCEMENTS=1)
    target_link_libraries(PluginUiTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    if(WIN32)
        target_compile_definitions(PluginUiTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(PluginUiTests PRIVATE avrt)
    endif()
    add_test(NAME PluginUi COMMAND PluginUiTests)
    juce_add_console_app(ProgramBrowserTests PRODUCT_NAME "Program Browser Tests")
    target_sources(ProgramBrowserTests PRIVATE Tests/ProgramBrowserTests.cpp)
    target_include_directories(ProgramBrowserTests PRIVATE Source)
    target_compile_definitions(ProgramBrowserTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ProgramBrowserTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_test(NAME ProgramBrowser COMMAND ProgramBrowserTests)
    set_tests_properties(ProgramBrowser PROPERTIES TIMEOUT 20)
    set_tests_properties(PluginUi PROPERTIES TIMEOUT 20
        ENVIRONMENT "SCBRIDGE_PROFILE_DIR=${CMAKE_CURRENT_BINARY_DIR}/plugin-ui-test-profile")
    if(APPLE)
        juce_add_console_app(StandaloneUiTests PRODUCT_NAME "Standalone UI Tests")
        target_sources(StandaloneUiTests PRIVATE Tests/StandaloneUiTests.cpp ${scbridge_processor_sources})
        set_source_files_properties(Tests/StandaloneUiTests.cpp PROPERTIES LANGUAGE OBJCXX)
        target_compile_definitions(StandaloneUiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(StandaloneUiTests PRIVATE juce::juce_audio_utils juce::juce_cryptography juce::juce_recommended_config_flags)
        set_target_properties(StandaloneUiTests PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
        add_dependencies(StandaloneUiTests SoundCanvasEngineDownload)
        add_custom_command(TARGET StandaloneUiTests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/StandaloneUiTests_artefacts/Resources"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasEngineDownload>"
                "${CMAKE_BINARY_DIR}/StandaloneUiTests_artefacts/Resources/SoundCanvasEngineDownload")
        add_test(NAME StandaloneUi COMMAND StandaloneUiTests)
        set_tests_properties(StandaloneUi PROPERTIES ENVIRONMENT "SCBRIDGE_PROFILE_DIR=${CMAKE_CURRENT_BINARY_DIR}/ui-test-profile")
    endif()
    foreach(name ControllerMirror Scene Insertion ProgramCatalogue ProgramSelection EngineSetupPolicy UpdatePresentation MusicalTyping)
        add_executable(${name}Tests "Tests/${name}Tests.cpp")
        target_include_directories(${name}Tests PRIVATE Source)
        set_target_properties(${name}Tests PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
        if(MSVC)
            target_compile_options(${name}Tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(${name}Tests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${name} COMMAND ${name}Tests)
    endforeach()
    add_executable(WakeSignalTests Tests/WakeSignalTests.cpp)
    target_include_directories(WakeSignalTests PRIVATE Source)
    if(WIN32)
        set(installer_workdir_test "${CMAKE_CURRENT_BINARY_DIR}/WindowsInstallerWorkingDirectoryTests.exe")
        file(TO_NATIVE_PATH "${installer_workdir_test}" installer_workdir_output)
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Tests/WindowsInstallerWorkingDirectoryTests.cs" installer_workdir_source)
        add_custom_command(OUTPUT "${installer_workdir_test}"
            COMMAND "${SCBRIDGE_CSC}" /nologo /target:exe /platform:x64 /optimize+ /utf8output
                "/out:${installer_workdir_output}" "${installer_workdir_source}"
            DEPENDS Tests/WindowsInstallerWorkingDirectoryTests.cs VERBATIM)
        add_custom_target(WindowsInstallerWorkingDirectoryTests ALL DEPENDS "${installer_workdir_test}")
        add_test(NAME WindowsInstallerWorkingDirectory COMMAND "${installer_workdir_test}"
            "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/installer-working-directory-test")
        set_tests_properties(WindowsInstallerWorkingDirectory PROPERTIES TIMEOUT 120)
        add_executable(WindowsUiTests Tests/WindowsUiTests.cpp ${scbridge_processor_sources})
        target_compile_definitions(WindowsUiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 NOMINMAX WIN32_LEAN_AND_MEAN JUCE_MODAL_LOOPS_PERMITTED=1)
        if(SCBRIDGE_WINDOWS7)
            target_compile_definitions(WindowsUiTests PRIVATE _CONSOLE)
        endif()
        add_dependencies(WindowsUiTests SoundCanvasEngineDownload)
        add_custom_command(TARGET WindowsUiTests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasEngineDownload>" "$<TARGET_FILE_DIR:WindowsUiTests>/SoundCanvasEngineDownload.exe")
        target_link_libraries(WindowsUiTests PRIVATE juce::juce_audio_utils juce::juce_cryptography juce::juce_recommended_config_flags avrt)
        add_test(NAME WindowsUi COMMAND WindowsUiTests "${CMAKE_CURRENT_BINARY_DIR}/ui-test-files")
        set_tests_properties(WindowsUi PROPERTIES TIMEOUT 20)
        set_tests_properties(WindowsUi PROPERTIES ENVIRONMENT "SCBRIDGE_PROFILE_DIR=${CMAKE_CURRENT_BINARY_DIR}/ui-test-profile")
        target_link_libraries(WakeSignalTests PRIVATE avrt)
        add_library(MockCoreWindows SHARED Tests/MockCoreWindows.cpp)
        add_dependencies(PluginUiTests SoundCanvasHelper MockCoreWindows)
        add_custom_command(TARGET PluginUiTests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SoundCanvasHelper>" "$<TARGET_FILE_DIR:PluginUiTests>")
        add_test(NAME PartMidiReadback COMMAND PluginUiTests --part-midi "$<TARGET_FILE:MockCoreWindows>")
        set_tests_properties(PartMidiReadback PROPERTIES TIMEOUT 20
            ENVIRONMENT "SCBRIDGE_PROFILE_DIR=${CMAKE_CURRENT_BINARY_DIR}/part-midi-profile")
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
