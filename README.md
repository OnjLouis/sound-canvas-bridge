# Sound Canvas Bridge

Mac AU/VST3 and Windows VST3 plug-ins, with a standalone MIDI module on both platforms, for **existing Sound Canvas VA owners**.
Use the original installed synthesis engine in modern VST3 hosts on either platform.
On Apple Silicon, Komplete Kontrol and Logic run natively while a separate helper
runs the original Intel engine under Rosetta.

Version 1.2 adds Windows x64 support and a standalone All-parts sound-map option.
Each platform has its own installer and update channel.

**[View published installers](https://github.com/OnjLouis/sound-canvas-bridge/releases)**

**[Get the Sound Canvas NKS presets from OnjMusic](https://onj.me/nks)**

The NKS pack is in **Roland / Sound Canvas Bridge.zip**. It contains 2,958 named
addresses and previews across SC-55, SC-88, SC-88 Pro and SC-8820 maps, grouped into
Programs and Drums. These are compatibility-map addresses, not 2,958 unique samples.
The presets require this bridge's VST3, not the original VST2 presets.
The same NKS download, mappings, previews and artwork work with both bridge builds.

## Requirements

- Mac: macOS 12 or later, Intel or Apple Silicon. The front ends are universal.
- Windows: x64, .NET Framework 4.8 and a 64-bit original engine. Windows 11 is tested;
  other Windows versions remain unverified.
- Your own legitimate, working Sound Canvas VA VST installation, including its
  original engine and ROM resources. No Roland binaries, ROMs or licences are supplied.
- Mac only: Rosetta on Apple Silicon, for the separate Intel engine helper.
- Advanced controls recognise the tested Sound Canvas VA 1.1.6 engine. Other engine
  builds may provide basic playback only; the bridge reports that limitation.

This is an independent OnjMusic project, not a Roland product or a replacement licence.
It does not activate Sound Canvas VA or modify its installed files.

## Installation

On Windows, the installer places the app in `Program Files/OnjMusic/Sound Canvas
Bridge` and the plug-in in `Program Files/Common Files/VST3/Sound Canvas Bridge.vst3`.
It verifies both components, requests administrator approval and restores the
previous components if replacement fails. Per-user settings, NKS files and the
original Roland installation are preserved. Existing shortcuts to the earlier
per-user copy are migrated to the shared app. Windows x64 and .NET Framework 4.8
are required; Windows 11 is tested, other Windows versions remain unverified.
The original Windows `SCCore.dll` must be 64-bit. No AU is installed on Windows.

On Mac, open the DMG and run **Install Sound Canvas Bridge**. It installs the standalone app
in Applications and both plug-ins in the shared system audio folders. Stop MIDI
playback and unload the bridge from audio hosts first. The installer requests
administrator authorisation, preserves settings and restores the previous bundles
if replacement fails. Release downloads are Developer ID signed and notarised.

For NKS, merge the pack's **User Content** and **NI Resources** folders into the
corresponding Native Instruments folders, then rescan Komplete Kontrol's plug-ins
and User library. Browse **User / Roland Cloud / Sound Canvas**.

## Highlights

- Sixteen-part MIDI playback and SysEx, with the original engine's compatibility maps.
- Standalone All (Sound Map) selection applies a map to all sixteen parts without
  changing their bank, program, mixer or effects. The plug-in/NKS parameter layout
  is unchanged; no replacement previews or preset identities are needed for 1.2.
- Ninety-three controls, including tone shaping, vibrato, master settings, reverb,
  chorus, delay, EQ, insertion effects and individual drum-note settings.
  NKS kits start on channel 10. The matching 1.1 NKS pack adds four insertion pages.
- Accessible names, values, focused-control help and keyboard editing, including Enter-to-type.
- A shared, saved bridge-latency setting: 128 samples initially, adjustable 32-2048.
- Selectable physical MIDI inputs and audio output, plus a virtual MIDI destination on Mac.
  Windows accepts existing system loopback ports; it does not install a virtual-port driver.
- Menu-bar/system-tray operation, optional login/hidden startup and a quiet MIDI activity display.
- Daily update checks by default, with Never, At startup and Hourly alternatives.
  Updates require confirmation and verified packages: signed/notarised DMGs on Mac,
  or RSA-signed installers on Windows. No silent installation.

Command+comma opens Preferences, F1 reads focused-control help, Shift+F1 checks
updates and Command+F1 opens this project. The app also has Panic and reset commands.
F9 sends All Notes Off. Windows uses Control instead of Command. The standard Mac
Command+M and Command+H actions are retained; GM Reset and Hide to Menu Bar use
Command+Shift+M and Command+Shift+H. Other action shortcuts do not require Shift.

## Guide and Limitations

Read the [HTML guide](Resources/Manual.html), also available offline from the app's
Help menu, and the [version history](Resources/VersionHistory.html).
The separate [Windows guide](Resources/WindowsManual.html) describes the Windows
installer, ASIO, system MIDI inputs, tray operation and update-signature behaviour.
Windows updates use a separate release family and pinned RSA signatures. These
are not Microsoft-trusted Authenticode signatures. The development-preview
Windows MIDI Services virtual-port runtime is not distributed; existing physical
or loopback MIDI inputs remain usable, without modifying their drivers/services.

Very low buffer settings depend on the computer and host; start at 128 and increase
if needed. Native Intel hardware and every macOS/host combination have not been
tested. Testing included native Apple Silicon and Intel/Rosetta slices, real Logic
and Komplete Kontrol playback, physical MIDI, state recall and installer rollback.
The current NKS pack was checked for cross-platform identities and paths, and
program/drum states from all four maps loaded and rendered through the installed
Windows VST3. Windows Komplete Kontrol browsing and hardware operation still need
community testing; that host was not used for the Windows acceptance test.

Mapped state is saved across all parts and both drum setups. This is not a complete
SysEx dump: retain external initialisation in your project for unmapped settings.
Insertion Type selects Thru or one of 64 native algorithms. All 20 parameter slots,
output sends and EQ routing are editable; Part MFX enables each part's routing to
the shared effect. Parameter slots use raw values and numbers because their
functions depend on the algorithm. The matching NKS mappings require bridge 1.1.0.
Saved scenes preserve insertion settings and remain compatible with older scenes.
The standalone receives MIDI, but is not a MIDI-file browser or onscreen keyboard.

Update cleanup never forces a busy disk image to unmount. If macOS keeps a
cancelled image busy beyond the bounded retries, the temporary download is
retained and the result is recorded in `UpdateCleanup.log` in the app's log folder.
Installed files and ongoing playback are unaffected by cancelling the installer.

## Source and Building

See [BUILDING.md](BUILDING.md). No Roland files are needed to compile; playback
requires the owner's installation. Public source contains the bridge, installer
and automated regression tests, not personal settings or private deployment tools.

## Licence and Contact

Original bridge source is MIT. Official JUCE-based binaries are distributed under
GNU AGPLv3, with complete project source here and the exact JUCE dependency pinned
in CMake. See [LICENSE](LICENSE) and [third-party notices](THIRD-PARTY-NOTICES.txt).
The separate proprietary Roland installation remains under its own terms.

[Report a problem](https://github.com/OnjLouis/sound-canvas-bridge/issues),
[contact OnjMusic](https://onj.me/contact) or [donate](https://onj.me/donate).
Please include your host, operating-system version, interface, engine version and buffer settings;
do not attach proprietary engine/ROM files or licence keys.
