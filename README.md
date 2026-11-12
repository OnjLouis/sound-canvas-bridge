# Sound Canvas Bridge

Windows and native Apple Silicon builds include a **Download from Roland** option
beside Browse in **General** preferences. Native extraction selects the supported
64-bit engine and app-local runtime on Windows, or the 32-bit engine and its runtime on Apple Silicon,
without running either downloaded installer. Browse remains available if the download changes
or disappears. Downloads and engine selection do not modify NKS files or previews.
First launch and missing-engine recovery open General automatically. If a hosted
plug-in has no engine, it directs you to the standalone app for setup; downloads
never run inside the audio host.

Mac AU/VST3 and Windows VST3 plug-ins, with a standalone MIDI module on both platforms, for **existing Sound Canvas VA owners**.
Use the original installed synthesis engine in modern VST3 hosts on either platform.
On Apple Silicon, Komplete Kontrol and Logic run natively while a separate native
compatibility worker runs your 32-bit Windows engine without Rosetta. Intel Mac
hosts retain the original native Intel Mac engine backend.

Version 1.4 adds a searchable in-plug-in sound browser, current
program names, hosted keyboard-focus entry and grouped native preset files. Native
VST3 factory browsing remains disabled to preserve channel-specific MIDI program
changes; its own browser is available instead. These changes do not require new
NKS files or previews. Version 1.3 improves installation and discovery, without changing the audio engine
or the NKS catalogue. Version 1.2 added Windows x64 and All-parts sound-map support.
Each platform has its own installer and update channel.

**[Windows installer and release notes](https://github.com/OnjLouis/sound-canvas-bridge/releases/tag/windows-v1.4.4)**

**[Windows 7 SP1 portable edition](https://github.com/OnjLouis/sound-canvas-bridge/releases/tag/windows7-v1.4.4)** ([setup guide](Resources/Windows7Manual.html))

**[Mac installer and release notes](https://github.com/OnjLouis/sound-canvas-bridge/releases/tag/v1.4.4)**

[All releases](https://github.com/OnjLouis/sound-canvas-bridge/releases)

**[Get the Sound Canvas NKS presets from OnjMusic](https://onj.me/nks)**

The NKS pack is in **Roland / Sound Canvas Bridge.zip**. It contains 2,958 named
addresses and previews across SC-55, SC-88, SC-88 Pro and SC-8820 maps, grouped into
Programs and Drums. These are compatibility-map addresses, not 2,958 unique samples.
The presets require this bridge's VST3, not the original VST2 presets.
The same NKS download, mappings, previews and artwork work with both bridge builds.

## Requirements

- Mac: macOS 12 or later, Intel or Apple Silicon. The front ends are universal.
- Modern Windows installer: x64, .NET Framework 4.8 and a 64-bit original engine.
  Windows 11 is tested. Windows 7 SP1 uses the separate portable edition.
- Your own legitimate, working Sound Canvas VA VST installation, including its
  original engine and ROM resources. No Roland binaries, ROMs or licences are supplied.
- Apple Silicon: a supported 32-bit Windows SCCore.dll, obtained from your own
  installation or Download from Roland. Its runtime stays beside the engine,
  outside our app and plug-ins. Native playback does not require Rosetta.
- Intel Mac: the original Sound Canvas VA Mac VST installation and its core/ROM resources.
- Advanced controls recognise the verified original and downloadable engines.
  Unknown Windows engine builds are rejected rather than guessing their layouts.

This is an independent OnjMusic project, not a Roland product or a replacement licence.
It does not activate Sound Canvas VA or modify its installed files.

## Installation

On Windows, the installer places the app in `Program Files/OnjMusic/Sound Canvas
Bridge` and the plug-in in `Program Files/Common Files/VST3/Sound Canvas Bridge.vst3`.
It verifies both components, requests administrator approval and restores the
previous components if replacement fails. Per-user settings, NKS files and the
original Roland installation are preserved. The standard setup wizard offers
desktop and Start-menu shortcuts and registers Sound Canvas Bridge in Installed
Apps. Its Finish page can open the app and honours Start hidden. Uninstall removes
the bridge app, VST3 and setup-created shortcuts, but keeps settings, NKS and Roland
files. Existing shortcuts to the earlier
per-user copy are migrated to the shared app. Windows x64 and .NET Framework 4.8
are required for that installer; Windows 11 is tested.
The original Windows `SCCore.dll` must be 64-bit. No AU is installed on Windows.

On Mac, open the DMG and run **Install Sound Canvas Bridge**. It installs the standalone app
in Applications and both plug-ins in the shared system audio folders. Stop MIDI
playback and unload the bridge from audio hosts first. The installer requests
the standalone app to quit normally, including when hidden in the menu bar. It
waits for exit before requesting administrator authorisation, preserves settings
and restores the previous bundles
if replacement fails. Release downloads are Developer ID signed and notarised.

For NKS, merge the pack's **User Content** and **NI Resources** folders into the
corresponding Native Instruments folders, then rescan Komplete Kontrol's plug-ins
and User library. Browse **User / Roland Cloud / Sound Canvas**.

## Highlights

- Sixteen-part MIDI playback and SysEx, with the original engine's compatibility maps.
- Standalone All (Sound Map) selection applies a map to all sixteen parts without
  changing their bank, program, mixer or effects. It sets the default map retained
  across GS Reset, while explicit MIDI maps still take precedence. The plug-in/NKS parameter layout
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

The Presets button opens the named sound browser. Filter by name, compatibility
map and Programs/Drums; Enter loads the selected address and Escape returns to
the controls. Searching alone does not change the sound. Selection preserves
mixer/effect settings and uses an existing suitable MIDI part without changing
its melodic/drum assignment. The standalone installs grouped native AU presets
on Mac and VST3 presets on Windows; Help, Install DAW Presets can repeat this.
Programs and Drums are directly inside the product folder, followed by sound
map and melodic instrument family, without an extra Factory level. Custom
presets can be kept alongside those two installer-managed groups. An unchanged
older Factory folder is migrated only after its complete contents are verified.
The AU no
longer advertises a flat 2,958-entry menu. Host-specific folder presentation
and previous/next shortcuts depend on that host's handling and configuration.
Disk presets recall their default mixer/routing state; the internal browser
preserves the current mixer/effects. User-edited native files are not overwritten.
The Mac bank is AU-only in the standard AU folder, which Logic also discovers.
An older generated duplicate in Logic's settings folder is retired only after
its complete contents match the canonical bank. Existing redirected roots are
resolved, not rewired; edited and unrelated banks remain untouched.
The current-name selector above the controls loads adjacent sounds with Up/Down
and announces their names. Enter opens the searchable browser. Browser rows use
short names; F1 opens the full address details, and Escape returns to the same row.
Part lists all sixteen numbered parts with their current program or drum kit.
Selecting a part changes what you edit without loading another sound. The
standalone also offers All for applying a Sound Map to every part.
Choice controls, including Part, Current sound and Control page, support Home/End for
the first/last entry and Page Up/Page Down for ten-entry jumps without wrapping.
The VST3 retains one native program because JUCE's root factory list can otherwise
reinterpret per-channel MIDI program changes as global factory selections.

Read the [HTML guide](Resources/Manual.html), also available offline from the app's
Help menu, and the [version history](Resources/VersionHistory.html).
The separate [Windows guide](Resources/WindowsManual.html) describes the Windows
installer, ASIO, system MIDI inputs, tray operation and update-signature behaviour.
Windows updates use a separate release family and pinned RSA signatures. These
are not Microsoft-trusted Authenticode signatures. The development-preview
Windows MIDI Services virtual-port runtime is not distributed; existing physical
or loopback MIDI inputs remain usable, without modifying their drivers/services.
The Windows 7 edition built from this source can check its own portable release
channel and open the matching release page. ZIP installation remains manual.

Very low buffer settings depend on the computer and host; start at 128 and increase
if needed. The experimental 32-sample setting can lose samples under load; dense
multi-part MIDI may need 512 samples. Native Intel hardware and every macOS/host combination have not been
tested. Testing included native Apple Silicon and Intel/Rosetta slices, real Logic
and Komplete Kontrol playback, physical MIDI, state recall and installer rollback.
The current NKS pack was checked for cross-platform identities and paths, and
program/drum states from all four maps loaded and rendered through the installed
Windows VST3. Community testers have also confirmed working Komplete Kontrol
browsing on Windows.

Mapped state is saved across all parts and both drum setups. This is not a complete
SysEx dump: retain external initialisation in your project for unmapped settings.
Insertion Type selects Thru or one of 64 native algorithms. All 20 parameter slots,
output sends and EQ routing are editable; Part MFX enables each part's routing to
the shared effect. Parameter slots use raw values and numbers because their
functions depend on the algorithm. The matching NKS mappings require bridge 1.1.0.
Saved scenes preserve insertion settings and all 16 melodic/drum assignments.
Older scenes load with their historical reset-default assignments. Default-role
scenes retain the older encoding; custom assignments use scene version 3 and
require the updated bridge to recall them correctly.
Scenes with custom default maps use version 4 and require bridge 1.4.2 or later;
all older scene versions remain readable. Existing NKS presets need no changes.
The standalone receives MIDI and can audition the selected part with **Musical
Keyboard** (Command+K on Mac, Control+K on Windows). Play white notes with
A S D F G H J K L, semicolon and apostrophe, and sharps with W E T Y U O P. Z/X change octave and C/V change
velocity. The Octave and Velocity sliders appear while the keyboard is active.
When Part is All, the keyboard plays the last numbered part. Direct
value entry and preset search temporarily hide the keyboard and release its
notes. Escape closes the keyboard. The plug-ins use the host's MIDI keyboard.

Update cleanup never forces a busy disk image to unmount. If macOS keeps a
cancelled image busy beyond the bounded retries, the temporary download is
retained and the result is recorded in `UpdateCleanup.log` in the app's log folder.
Cancelling before choosing Install leaves installed files and playback untouched.
Once installation starts, the standalone stops; rollback restores files and saved
settings, not active notes or effect tails.

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
