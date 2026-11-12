# Build Sound Canvas Bridge

## Windows

Use an x64 Visual Studio 2022 C++ toolchain, Windows SDK, CMake 3.22 or later,
Ninja, .NET Framework 4.8's C# compiler, and the official Steinberg ASIO SDK under
its GPLv3 option. No Roland files are needed to build. From the x64 developer
shell, configure and build:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DASIO_SDK_DIR=C:/path/to/ASIOSDK
cmake --build build --parallel 4
cmake --build build --parallel 4 --target SoundCanvasStandaloneBundle SoundCanvasPlugins
ctest --test-dir build --output-on-failure
```

The versioned source ZIP includes the SDK used for the release at
`Dependencies/ASIO/ASIOSDK`. Point `ASIO_SDK_DIR` there when building from that
archive. Its licence is included; no audio-interface driver is supplied.

The `WindowsInstallerWorkingDirectory` test embeds a dummy wizard in the real
installer launcher and starts it from a disposable installation directory. It
checks directory replacement and staging cleanup without installing anything,
requesting elevation or changing user settings.

The standalone output is under `build/SoundCanvasStandalone_artefacts/Release`;
the VST3 bundle is under `build/SoundCanvasBridge_artefacts/Release/VST3`. The
maintenance executable contains only the public update-verification key. Normal
builds do not require or redistribute the preview Windows MIDI Services SDK.
The development-only `SCBRIDGE_BUILD_EXPERIMENTAL_MIDI` option is off by default.
It requires the official preview SDK and does not resolve the documented Windows
service teardown bug. Do not include that preview runtime in release packages.

The full installer requires Inno Setup 7.1.1 (set `SCBRIDGE_ISCC` to its compiler
if it is not on PATH). It compiles `Source/MaintenanceWindows.cs` with three
embedded inputs: `payload.zip`, `manifest.json` and binary `manifest.sig`. The ZIP
contains only `Application/` and `VST3/` files. The UTF-8 manifest has `product`,
`version` and `files` fields; each file has `path`, byte `size` and lowercase SHA256
`sha256`. It is signed over its exact bytes with RSA/SHA256 PKCS#1, checked against
`Resources/WindowsUpdateKey.xml`. Installer and payload versions must match.
Given an already signed input folder, build it with:

```powershell
cmake -S . -B build -DSCBRIDGE_INSTALLER_INPUT=C:/signed-payload -DSCBRIDGE_ISCC="C:/path/to/ISCC.exe"
cmake --build build --target SoundCanvasWindowsInstaller
```

`Resources/WindowsInstaller.iss` supplies the standard wizard and owns shortcut
and uninstall registration. The signed worker replaces both components and
retains recovery copies until the wizard commits. Setup support lives in a
separate shared folder so it is not mistaken for signed app payload. The
uninstaller delegates verified component removal to the maintenance worker;
it never recursively deletes user content or settings.
The small `WindowsSetupBootstrap.cs` container retains the exact version-resource
format required by older updaters and launches the embedded Inno wizard without
custom UI. Sign the complete outer EXE only after this final packaging step.

The installer is `SoundCanvasBridge-VERSION-windows-x64.exe`. The updater selects
only stable `windows-vVERSION` releases with that asset and a detached `.exe.sig`
asset. That outer signature is base64 RSA/SHA256 PKCS#1 over the complete installer
bytes. Signing keys are not supplied. Derivative builds must use their own key
and update channel; they are not accepted as official updates. Neither payload
may contain Roland binaries, settings, logs, host projects or NKS content.

## Windows 7 SP1

Use a separate x86 or x64 MSVC developer shell and Windows SDK 10.0.18362.0.
The legacy option targets Windows 7 SP1 with the secure DLL loader updates
and pins JUCE 7.0.12, revision `4f43011b96eb0636104cb3e433894cda98243626`.
Do not use the modern JUCE source or reuse its build directory.
From the versioned legacy source ZIP:

```powershell
cmake -S . -B build-win7 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DSCBRIDGE_WINDOWS7=ON -DJUCE_DIR=Dependencies/JUCE -DASIO_SDK_DIR=Dependencies/ASIO/ASIOSDK -DUNSHIELD_DIR=Dependencies/Unshield
cmake --build build-win7 --parallel 4 --target SoundCanvasStandaloneBundle SoundCanvasPlugins
```

Use absolute dependency paths if configuring from another working directory.
The portable Application and VST3 outputs are in the same relative locations
as the modern build. The helper and original engine must match their
architecture. The portable bundle includes a check-only helper for matching
`windows7-vVERSION` releases with an architecture-specific ZIP and published
checksums. The modern installer/updater and preview MIDI Services runtime are
not used. Legacy binaries and corresponding JUCE 7 source are under
GPLv3; the original bridge source is also available under MIT. Test on the
target version of Windows before distributing a build for that architecture.

## macOS

Use macOS 12 or later with Xcode and its command-line tools, CMake 3.22 or newer,
Python 3.9 or newer, XcodeGen, Git, and an Apple SDK supporting arm64 and x86_64. The official build used JUCE
8.0.15, pinned to commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="$PWD/Dependencies/JUCE"
cmake --build build --config Release --parallel 4
cmake --build build --config Release --parallel 4 --target \
  SoundCanvasStandaloneBundle SoundCanvasPlugins SoundCanvasInstallerBundle
ctest --test-dir build --output-on-failure
```

The release source archive supplies `Dependencies/JUCE`, `Dependencies/MacRuntime`
and `Dependencies/BoxedWine-source.zip`. These are build inputs, not files from a
live installation. No Roland engine or Microsoft engine runtime is included.
When building a Git checkout, obtain those inputs from the matching release
source archive, or supply `JUCE_DIR`, `SCBRIDGE_TRANSLATED_RUNTIME` and the pinned
BoxedWine source separately. Omitting `JUCE_DIR` fetches the pinned JUCE source.
JUCE's source and all
of its dependency notices are available at that exact revision:
https://github.com/juce-framework/JUCE/tree/91ad83ae34a81e0833b1a2b0866f54846370ae53

The release's versioned source ZIP also includes that JUCE source
under `Dependencies/JUCE`. When using this archive, configure with
`-DJUCE_DIR="$PWD/Dependencies/JUCE"` to build without fetching it again.

Outputs are beneath `build/SoundCanvasStandalone_artefacts/Release` and
`build/SoundCanvasBridge_artefacts/Release/{AU,VST3}`. The front ends, helper and
maintenance tool contain both architectures. Apple Silicon selects the native
BoxedWine compatibility worker; Intel selects the original Mac engine backend.
`Source/Backend.h` makes that choice per architecture, not per build machine.
A build does not install anything automatically.

`cmake/BuildTranslatedEngine.py` verifies the pinned BoxedWine archive, applies
the published overrides, generates the headless SDL/BoxedWine Xcode project and
builds the helper. The overrides add the exercised SSE3 instruction handling and
bounded code-cache invalidation. Audio runtime stubs implement the private guest
scheduling calls; they do not patch Roland code. The ARM helper uses only the
`allow-jit` entitlement. Local file paths are removed from compiled diagnostics.

The supplied Win32 guest is built from `Source/Translated/Guest.cpp`. To rebuild
it, set `SCBRIDGE_GUEST_COMPILER` to an i686 MinGW C++ compiler. The default uses
the matching bridge-built executable in MacRuntime; its complete source is in
the same release. The guest protocol is shared with `Source/Protocol.h`.
Prebuilt guests must include `guest/Build.json` with their protocol number and
SHA-256. Configuration rejects a mismatched protocol or changed executable.
After a protocol change, rebuild the guest and refresh this metadata together;
an older release's runtime inputs are not a compatible substitute.
The release source archive also includes `Dependencies/RuntimeSources`: Wine,
TinyCore userspace component source archives and their manifest/build references.
The open-source runtime filesystem remains replaceable. Microsoft DLLs are
obtained from the owner's engine installation or the verified Roland download,
not from the bridge's packages.

For the historical Rosetta-backed development path, configure with
`-DSCBRIDGE_USE_TRANSLATED_ENGINE=OFF`. This is not the default 1.4 Apple Silicon
release backend. `SCBRIDGE_MAC_ARCHITECTURES` can restrict front-end slices for
development; the release default is `arm64;x86_64`.

These bundles receive local ad-hoc signatures, not the official Developer ID.
The release installer and automatic updater deliberately accept only the expected
official signing identity and notarisation. A locally built installer therefore
cannot be used as an official updater. For local development, test the bundles
separately in a disposable host environment, without replacing a working install.
Native Apple Silicon playback does not require Rosetta. Testing an Intel slice
on Apple Silicon does require Rosetta; native Intel hardware remains a separate
acceptance environment. Runtime engine setup is performed by the standalone,
not during building or plug-in scanning.

The public tests check controller/SysEx readback, insertion messages and type
mappings, old/new scene migration and concurrent scene snapshots
without opening the proprietary engine or changing user settings. They do not
replace real-host audio and VoiceOver acceptance tests.

For redistributed builds, retain the licence notices. JUCE is used under AGPLv3
for the official open-source binaries; follow JUCE's licence terms when building
and distributing a derivative. Signing credentials are not part of this repository.
