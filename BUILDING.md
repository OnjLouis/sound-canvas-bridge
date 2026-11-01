# Build Sound Canvas Bridge

Use macOS 12 or later with Xcode and its command-line tools, CMake 3.22 or newer,
Git, and an Apple SDK supporting arm64 and x86_64. The official build used JUCE
8.0.15, pinned to commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4 --target \
  SoundCanvasStandaloneBundle SoundCanvasPlugins SoundCanvasInstallerBundle \
  ControllerMirrorTests SceneTests
ctest --test-dir build --output-on-failure
```

The first configure fetches the pinned JUCE source. For an existing, unmodified
JUCE 8.0.15 checkout, add `-DJUCE_DIR=/path/to/JUCE` instead. JUCE's source and all
of its dependency notices are available at that exact revision:
https://github.com/juce-framework/JUCE/tree/91ad83ae34a81e0833b1a2b0866f54846370ae53

The release's `SoundCanvasBridge-1.0.0-source.zip` also includes that JUCE source
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

The public tests check controller/SysEx readback and concurrent scene snapshots
without opening the proprietary engine or changing user settings. They do not
replace real-host audio and VoiceOver acceptance tests.

For redistributed builds, retain the licence notices. JUCE is used under AGPLv3
for the official open-source binaries; follow JUCE's licence terms when building
and distributing a derivative. Signing credentials are not part of this repository.
