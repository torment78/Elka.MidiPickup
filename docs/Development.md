# Development

Use Visual Studio 2026, the v145 C++ toolset and Windows SDK 10.0.26100.0. The app and tests build as x64 native C++20 programs with the C runtime linked statically.

## Source layout

| Path | Purpose |
| --- | --- |
| `app/Main.cpp` | Win32 UI, worker coordination, tray and startup handling |
| `app/Pickup.h` | Per-fader pickup and readback state machine |
| `app/MidiInbox.h` | Buffered MIDI callback input |
| `app/Devices.h` | WinMM input and dynamically loaded VoiceMeeter Remote API |
| `app/Profile.h` | Saved settings and legacy profile migration |
| `app/Version.h` | Displayed prerelease version and Windows version resource |
| `app/App.rc` | Embedded icon and file/product version metadata |
| `tests/PickupTests.cpp` | Hardware-independent regression checks |
| `src/` | Optional native MIDI compatibility probe |
| `docs/images/` | Header PNG masters, compressed JPEGs and generation prompts |

The processing worker has a nominal 10 ms interval. Visible UI refreshes every 100 ms; hidden/minimized windows skip routine redraws. Actual timing depends on Windows scheduling. Each mapping owns independent pickup state.

## Build and test

```powershell
.\scripts\Build.ps1 -Configuration Release
.\artifacts\x64\Release\Elka.MidiPickup.Tests.exe
.\scripts\Build.ps1 -Configuration Debug
.\artifacts\x64\Debug\Elka.MidiPickup.Tests.exe
```

The solution builds the app and tests by default. Normal builds require no network access or MIDI SDK package. The optional probe can be built with `scripts/Build.ps1 -IncludeProbe`; its dependencies remain in the ignored project-local `.cache` folder.

Do not run the probe's device or endpoint tests against someone else's active audio setup without arranging that test first. Unit tests do not open MIDI or VoiceMeeter connections.

## Package a preview

```powershell
.\scripts\Package-Prerelease.ps1
```

Packaging builds Release, runs the regression tests, checks the EXE version, and creates a fresh staging folder. It copies only the EXE and public documentation/artwork, never existing profiles, logs, debug symbols or MIDI preview binaries. Outputs include a portable ZIP, artwork ZIP and SHA256SUMS.txt under `artifacts/releases/0.3.0-preview.1`.

The package is intentionally labelled **unsigned**. Signing and installer work are separate from this portable test release. New release versions must update `Version.h`, the packaging default, README and release notes together.

## Publication boundaries

Personal `.ini`, `.log`, `.user`, local reports, build outputs and dependency caches are excluded from Git. Do not add credentials or signing material. The source tree includes no bundled VoiceMeeter DLL or Microsoft preview package.

Hardware acceptance is tracked in [Limitations.md](Limitations.md). Passing simulated tests is not a claim that all controllers or VoiceMeeter editions have been tested.
