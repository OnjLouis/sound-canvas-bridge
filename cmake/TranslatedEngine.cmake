find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
find_program(SCBRIDGE_XCODEGEN xcodegen REQUIRED)
set(SCBRIDGE_TRANSLATED_RUNTIME "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/MacRuntime"
    CACHE PATH "Published Mac runtime inputs, supplied in the release source archive")
set(SCBRIDGE_GUEST_COMPILER "" CACHE FILEPATH "Optional i686 MinGW C++ cross-compiler")
set(SCBRIDGE_GUEST_EXECUTABLE "${SCBRIDGE_TRANSLATED_RUNTIME}/guest/SoundCanvasGuest.exe"
    CACHE FILEPATH "Prebuilt bridge guest, or set SCBRIDGE_GUEST_COMPILER to build it")
file(GLOB_RECURSE translated_sources CONFIGURE_DEPENDS "${SOURCE}/Translated/*")
set(translated_headers "${SOURCE}/Protocol.h" "${SOURCE}/Controls.h" "${SOURCE}/Scene.h"
    "${SOURCE}/CoreState.h" "${SOURCE}/CoreLayouts.h" "${SOURCE}/ControllerMirror.h"
    "${SOURCE}/ControlMessage.h" "${SOURCE}/Insertion.h" "${SOURCE}/WorkerScheduling.h")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildTranslatedEngine.py"
        --build-dir "${CMAKE_BINARY_DIR}/Translated" --architectures arm64,x86_64
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_BINARY_DIR}/Translated/SoundCanvasHelper" "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    COMMAND codesign --force --sign - --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/Resources/TranslatedHelper.entitlements"
        "${CMAKE_BINARY_DIR}/SoundCanvasHelper"
    DEPENDS ${translated_sources} ${translated_headers} "${SOURCE}/Helper.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildTranslatedEngine.py"
    VERBATIM)
if(SCBRIDGE_GUEST_COMPILER)
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SoundCanvasGuest.exe"
        COMMAND "${SCBRIDGE_GUEST_COMPILER}" -std=c++17 -O2 -static -municode "-I${SOURCE}"
            "${SOURCE}/Translated/Guest.cpp" -lbcrypt -lavrt -o "${CMAKE_BINARY_DIR}/SoundCanvasGuest.exe"
        DEPENDS ${translated_sources} ${translated_headers}
        VERBATIM)
else()
    if(NOT EXISTS "${SCBRIDGE_GUEST_EXECUTABLE}")
        message(FATAL_ERROR "Provide the release MacRuntime inputs or SCBRIDGE_GUEST_COMPILER")
    endif()
    get_filename_component(guest_directory "${SCBRIDGE_GUEST_EXECUTABLE}" DIRECTORY)
    set(guest_manifest "${guest_directory}/Build.json")
    if(NOT EXISTS "${guest_manifest}")
        message(FATAL_ERROR "The prebuilt guest needs its matching Build.json, or set SCBRIDGE_GUEST_COMPILER")
    endif()
    file(READ "${guest_manifest}" guest_metadata)
    string(JSON guest_protocol GET "${guest_metadata}" protocol)
    string(JSON guest_expected_sha GET "${guest_metadata}" sha256)
    file(STRINGS "${SOURCE}/Protocol.h" protocol_line REGEX "^constexpr uint32_t protocolVersion = [0-9]+;")
    string(REGEX MATCH "protocolVersion = ([0-9]+)" protocol_match "${protocol_line}")
    set(expected_protocol "${CMAKE_MATCH_1}")
    file(SHA256 "${SCBRIDGE_GUEST_EXECUTABLE}" guest_actual_sha)
    if(NOT guest_protocol STREQUAL expected_protocol OR NOT guest_actual_sha STREQUAL guest_expected_sha)
        message(FATAL_ERROR "The prebuilt guest does not match this bridge protocol or its recorded hash; rebuild it")
    endif()
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SoundCanvasGuest.exe"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SCBRIDGE_GUEST_EXECUTABLE}"
            "${CMAKE_BINARY_DIR}/SoundCanvasGuest.exe"
        DEPENDS "${SCBRIDGE_GUEST_EXECUTABLE}" "${guest_manifest}" VERBATIM)
endif()
foreach(runtime_file boxedwine.zip apisetschema.dll)
    if(NOT EXISTS "${SCBRIDGE_TRANSLATED_RUNTIME}/${runtime_file}")
        message(FATAL_ERROR "Missing MacRuntime input: ${runtime_file}; see BUILDING.md")
    endif()
endforeach()
add_custom_target(SoundCanvasGuestBuild DEPENDS "${CMAKE_BINARY_DIR}/SoundCanvasGuest.exe")

function(scbridge_copy_translated_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SCBRIDGE_TRANSLATED_RUNTIME}"
            "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_BINARY_DIR}/SoundCanvasGuest.exe"
            "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/guest/SoundCanvasGuest.exe"
        VERBATIM)
    add_dependencies(${target} SoundCanvasGuestBuild)
endfunction()
