# Installation and saved settings

Download the installer or portable ZIP from the [0.4.0-preview.6 release](https://github.com/torment78/Elka.MidiPickup/releases/tag/v0.4.0-preview.6). This is an unsigned testing preview for Windows 11 x64.

## Installer

Exit MIDI Pickup using **Exit**, including any copy in the tray. Run the setup EXE and approve Windows elevation. The installer provides a Start-menu shortcut under **ElkaSoft**, an optional desktop shortcut, and an uninstaller in Windows Installed Apps.

| Content | Default folder |
| --- | --- |
| Program, documentation and artwork | `C:\Program Files\ElkaSoft\Elka MIDI Pickup` |
| Strip/bus and application MIDI saves | `%APPDATA%\ElkaSoft\Elka MIDI Pickup\Saves` |
| Troubleshooting log | `%LOCALAPPDATA%\ElkaSoft\Elka MIDI Pickup\Logs` |

The program folder can be changed during setup; later upgrades remember it. Saves and logs belong to the current Windows user and remain in the same locations. Normal operation does not require administrator rights. The optional **Reset audio** button requests elevation separately.

Install a new version over the existing installation after exiting the app. Your settings survive upgrades and uninstall. Uninstall removes the program and shortcuts, and removes the current user's Windows-startup entry only if it points to that installation. It does not remove a startup entry belonging to another portable copy.

## Move an existing portable setup

1. Exit the portable app. Keep its files as a backup.
2. Create `%APPDATA%\ElkaSoft\Elka MIDI Pickup\Saves` if needed.
3. Copy `Elka.MidiPickup.ini` and `Elka.MidiPickup.applications` from the portable folder into **Saves**, before starting the installed copy. Copy only files you actually used. If Saves already contains a setup, back it up first and choose which setup to keep.
4. Open the installed app. Check the MIDI device, gain range, tolerance and mappings. **Save setup** reapplies your Windows-startup preference using the installed executable's path.

If legacy settings already sit beside the installed EXE, the app copies them together on first launch only when no Saves directory exists. Originals remain available. Existing Saves data is never merged or replaced automatically.

## Portable ZIP

Extract the ZIP into a writable folder and run `Elka.MidiPickup.exe`. Saves and logs stay beside that EXE. Keep those files when replacing the portable program with a newer version.

Use the portable ZIP for portable operation. The installer adds `Elka.MidiPickup.installed` to select per-user storage; copying an installed folder also copies that mode marker.

## Requirements

VoiceMeeter must be installed and running for live control. MIDI Pickup uses VoiceMeeter's installed Remote API and Windows MIDI input. It does not install VoiceMeeter, change routing, restart audio during installation, or require .NET, loopMIDI or a Windows MIDI Services preview package.

SHA-256 checksums for the installer and ZIPs are included with the release.
