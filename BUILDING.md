# Build Sound Canvas Bridge

## Windows

Use an x64 Visual Studio 2022 C++ toolchain, Windows SDK, CMake 3.22 or later,
Ninja, .NET Framework 4.8's C# compiler, and the official Steinberg ASIO SDK under
its GPLv3 option. No Roland files are needed to build. From the x64 developer
shell, configure and build:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DASIO_SDK_DIR=C:/path/to/ASIOSDK
cmake --build build --parallel 4 --target SoundCanvasStandaloneBundle SoundCanvasPlugins WindowsUiTests ControllerMirrorTests SceneTests InsertionTests WindowsIpcTests WakeSignalTests MockCoreWindows
ctest --test-dir build --output-on-failure
```

The versioned source ZIP includes the SDK used for the release at
`Dependencies/ASIO/ASIOSDK`. Point `ASIO_SDK_DIR` there when building from that
archive. Its licence is included; no audio-interface driver is supplied.

The standalone output is under `build/SoundCanvasStandalone_artefacts/Release`;
the VST3 bundle is under `build/SoundCanvasBridge_artefacts/Release/VST3`. The
maintenance executable contains only the public update-verification key. Normal
builds do not require or redistribute the preview Windows MIDI Services SDK.
The development-only `SCBRIDGE_BUILD_EXPERIMENTAL_MIDI` option is off by default.
It requires the official preview SDK and does not resolve the documented Windows
service teardown bug. Do not include that preview runtime in release packages.

The full installer compiles the same `Source/MaintenanceWindows.cs` with three
embedded inputs: `payload.zip`, `manifest.json` and binary `manifest.sig`. The ZIP
contains only `Application/` and `VST3/` files. The UTF-8 manifest has `product`,
`version` and `files` fields; each file has `path`, byte `size` and lowercase SHA256
`sha256`. It is signed over its exact bytes with RSA/SHA256 PKCS#1, checked against
`Resources/WindowsUpdateKey.xml`. Installer and payload versions must match.
Given an already signed input folder, build it with:

```powershell
cmake -S . -B build -DSCBRIDGE_INSTALLER_INPUT=C:/signed-payload
cmake --build build --target SoundCanvasWindowsInstaller
```

The installer is `SoundCanvasBridge-VERSION-windows-x64.exe`. The updater selects
only stable `windows-vVERSION` releases with that asset and a detached `.exe.sig`
asset. That outer signature is base64 RSA/SHA256 PKCS#1 over the complete installer
bytes. Signing keys are not supplied. Derivative builds must use their own key
and update channel; they are not accepted as official updates. Neither payload
may contain Roland binaries, settings, logs, host projects or NKS content.

## macOS

Use macOS 12 or later with Xcode and its command-line tools, CMake 3.22 or newer,
Git, and an Apple SDK supporting arm64 and x86_64. The official build used JUCE
8.0.15, pinned to commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4 --target \
  SoundCanvasStandaloneBundle SoundCanvasPlugins SoundCanvasInstallerBundle \
  ControllerMirrorTests SceneTests InsertionTests
ctest --test-dir build --output-on-failure
```

The first configure fetches the pinned JUCE source. For an existing, unmodified
JUCE 8.0.15 checkout, add `-DJUCE_DIR=/path/to/JUCE` instead. JUCE's source and all
of its dependency notices are available at that exact revision:
https://github.com/juce-framework/JUCE/tree/91ad83ae34a81e0833b1a2b0866f54846370ae53

The release's versioned source ZIP also includes that JUCE source
under `Dependencies/JUCE`. When using this archive, configure with
`-DJUCE_DIR="$PWD/Dependencies/JUCE"` to build without fetching it again.

Outputs are beneath `build/SoundCanvasStandalone_artefacts/Release` and
`build/SoundCanvasBridge_artefacts/Release/{AU,VST3}`. The helper remains x86_64
because it loads the original Intel engine; the front ends and maintenance tool
contain both architectures. A build does not install anything automatically.

These bundles receive local ad-hoc signatures, not the official Developer ID.
The release installer and automatic updater deliberately accept only the expected
official signing identity and notarisation. A locally built installer therefore
cannot be used as an official updater. For local development, test the bundles
separately in a disposable host environment, without replacing a working install.
On Apple Silicon, running the engine helper requires Rosetta and your own original
Sound Canvas VA installation. No engine or ROM download is part of this build.

The public tests check controller/SysEx readback, insertion messages and type
mappings, old/new scene migration and concurrent scene snapshots
without opening the proprietary engine or changing user settings. They do not
replace real-host audio and VoiceOver acceptance tests.

For redistributed builds, retain the licence notices. JUCE is used under AGPLv3
for the official open-source binaries; follow JUCE's licence terms when building
and distributing a derivative. Signing credentials are not part of this repository.
