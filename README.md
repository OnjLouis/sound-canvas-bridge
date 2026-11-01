# Sound Canvas Bridge

A Mac AU, VST3 and standalone MIDI module for **existing Sound Canvas VA owners**.
Use the original installed Intel synthesis engine from native Apple Silicon hosts,
including Komplete Kontrol and Logic, without running those hosts under Rosetta.

**[Download the latest Mac installer](https://github.com/OnjLouis/sound-canvas-bridge/releases/latest)**

**[Get the Sound Canvas NKS presets from OnjMusic](https://onj.me/nks)**

The NKS pack is in **Roland / Sound Canvas Bridge.zip**. It contains 2,958 named
addresses and previews across SC-55, SC-88, SC-88 Pro and SC-8820 maps, grouped into
Programs and Drums. These are compatibility-map addresses, not 2,958 unique samples.
The presets require this bridge's VST3, not the original VST2 presets.

## Requirements

- macOS 12 or later, Intel or Apple Silicon. The front ends are universal.
- Your own legitimate, working Sound Canvas VA VST installation, including its
  original engine and ROM resources. No Roland binaries, ROMs or licences are supplied.
- Rosetta on Apple Silicon, for the separate Intel engine helper only.
- Advanced controls recognise the tested Sound Canvas VA 1.1.6 engine. Other engine
  builds may provide basic playback only; the bridge reports that limitation.

This is an independent OnjMusic project, not a Roland product or a replacement licence.
It does not activate Sound Canvas VA or modify its installed files.

## Installation

Open the DMG and run **Install Sound Canvas Bridge**. It installs the standalone app
in Applications and both plug-ins in the shared system audio folders. Stop MIDI
playback and unload the bridge from audio hosts first. The installer requests
administrator authorisation, preserves settings and restores the previous bundles
if replacement fails. Release downloads are Developer ID signed and notarised.

For NKS, merge the pack's **User Content** and **NI Resources** folders into the
corresponding Native Instruments folders, then rescan Komplete Kontrol's plug-ins
and User library. Browse **User / Roland Cloud / Sound Canvas**.

## Highlights

- Sixteen-part MIDI playback and SysEx, with the original engine's compatibility maps.
- Sixty-six controls, including tone shaping, vibrato, master settings, reverb,
  chorus, delay, EQ and individual drum-note settings. NKS kits start on channel 10.
- VoiceOver names, values, help tags and keyboard editing, including Enter-to-type.
- A shared, saved bridge-latency setting: 128 samples initially, adjustable 32-2048.
- Standalone virtual MIDI input plus selectable physical MIDI inputs and audio output.
- Menu-bar operation, optional login/hidden startup and a quiet MIDI activity display.
- Daily update checks by default, with Never, At startup and Hourly alternatives.
  Updates require confirmation and signed/notarised packages; no silent installation.

Command+comma opens Preferences, F1 reads focused-control help, Shift+F1 checks
updates and Command+F1 opens this project. The app also has Panic and reset commands.

## Guide and Limitations

Read the [HTML guide](Resources/Manual.html), also available offline from the app's
Help menu, and the [version history](Resources/VersionHistory.html).

Very low buffer settings depend on the computer and host; start at 128 and increase
if needed. Native Intel hardware and every macOS/host combination have not been
tested. Testing included native Apple Silicon and Intel/Rosetta slices, real Logic
and Komplete Kontrol playback, physical MIDI, state recall and installer rollback.

Mapped state is saved across all parts and both drum setups. This is not a complete
SysEx dump: retain external initialisation in your project for unmapped settings.
Part MFX enables routing to the current MFX; it is not a full MFX algorithm editor.
The standalone receives MIDI, but is not a MIDI-file browser or onscreen keyboard.

## Source and Building

See [BUILDING.md](BUILDING.md). No Roland files are needed to compile; playback
requires the owner's installation. Public source contains the bridge, installer
and two standalone unit tests, not personal settings or private deployment tools.

## Licence and Contact

Original bridge source is MIT. Official JUCE-based binaries are distributed under
GNU AGPLv3, with complete project source here and the exact JUCE dependency pinned
in CMake. See [LICENSE](LICENSE) and [third-party notices](THIRD-PARTY-NOTICES.txt).
The separate proprietary Roland installation remains under its own terms.

[Report a problem](https://github.com/OnjLouis/sound-canvas-bridge/issues),
[contact OnjMusic](https://onj.me/contact) or [donate](https://onj.me/donate).
Please include your host, macOS version, interface, engine version and buffer settings;
do not attach proprietary engine/ROM files or licence keys.
