# Development

Use Visual Studio 2026, the v145 C++ toolset and Windows SDK 10.0.26100.0. The app and tests build as x64 native C++20 programs with the C runtime linked statically.

## Source layout

| Path | Purpose |
| --- | --- |
| `app/Main.cpp` | Win32 UI, worker coordination, tray and startup handling |
| `app/ControlLayout.h` | Cached control geometry and deferred child repainting |
| `app/Pickup.h` | Per-fader pickup and readback state machine |
| `app/MidiInbox.h` | Buffered MIDI callback input |
| `app/Devices.h` | WinMM input and dynamically loaded VoiceMeeter Remote API |
| `app/Profile.h` | Saved settings and legacy profile migration |
| `app/ApplicationMidi.h` | App identity, MIDI actions and scalar pickup model |
| `app/AudioSessions.h` | Windows endpoint/session discovery, notifications and guarded session writes |
| `app/ApplicationControl.cpp` | Independent MTA worker, preview lifecycle and shared MIDI routing |
| `app/ApplicationWindow.cpp` | Dynamic application mapping window |
| `app/ApplicationProfile.h` | Separate versioned preview settings |
| `app/Version.h` | Displayed prerelease version and Windows version resource |
| `app/App.rc` | Embedded icon and file/product version metadata |
| `tests/PickupTests.cpp` | Hardware-independent regression checks |
| `tests/ApplicationTests.h` | Application pickup/action/profile checks and isolated Core Audio/controller integration |
| `src/` | Optional native MIDI compatibility probe |
| `docs/images/` | Header PNG masters, compressed JPEGs and generation prompts |

The processing worker has a nominal 10 ms interval. Visible UI refreshes every 100 ms; hidden/minimized windows skip routine redraws. Actual timing depends on Windows scheduling. Each mapping owns independent pickup state.

The application preview starts its own MTA worker only when first opened. It observes VoiceMeeter playback sessions at a nominal 10 ms while active/learning, 50 ms while stopped, with endpoint reconciliation every two seconds plus session-created notifications. It never enumerates sessions on the strip/bus worker. Identity is the exact endpoint ID and normalized executable path; the sorted session-instance set is revalidated before writes. Session membership changes re-arm pickup. Conflicting enabled sources are blocked across both features.

## Build and test

```powershell
.\scripts\Build.ps1 -Configuration Release
.\artifacts\x64\Release\Elka.MidiPickup.Tests.exe
.\scripts\Build.ps1 -Configuration Debug
.\artifacts\x64\Debug\Elka.MidiPickup.Tests.exe
```

The solution builds the app and tests by default. Normal builds require no network access or MIDI SDK package. The optional probe can be built with `scripts/Build.ps1 -IncludeProbe`; its dependencies remain in the ignored project-local `.cache` folder.

Do not run the probe's device or endpoint tests against someone else's active audio setup without arranging that test first. Unit tests do not open MIDI or VoiceMeeter connections.

Optional application integration checks:

```powershell
.\artifacts\x64\Release\Elka.MidiPickup.Tests.exe --inspect-audio
.\artifacts\x64\Release\Elka.MidiPickup.Tests.exe --audio-integration
.\artifacts\x64\Debug\Elka.MidiPickup.Tests.exe --preview-window
.\artifacts\x64\Debug\Elka.MidiPickup.Tests.exe --preview-main
```

`--inspect-audio` reads VoiceMeeter playback endpoints and their sessions. `--audio-integration` creates silent, non-persistent WASAPI sessions owned by the tests and changes only their volume/mute. A short-lived child process tests cleanup across process exit; the controller is exercised with synthetic MIDI. No audio stream is started. `--preview-window` opens the production preview UI in a development host with a temporary setup and **no MIDI connection**. It reads the VM edition without registering a custom display button, and cannot start application writes. Hide closes this inspection host. `--preview-main` compiles the production main window in an inspection-only mode: no profiles, startup registration, devices, tray or processing worker; only page navigation and Exit commands are accepted. It can run beside the user's normal app. Neither helper is shipped in the portable package.

The layout caches the requested dimensions of controls, including the expanded height of dropdowns. Scroll-only moves use `SWP_NOSIZE`; layout work disables intermediate redraw/copying and invalidates all children after repositioning. A wheel message at the scroll boundary performs no control moves. Closed dropdowns do not consume wheel input as a selection change. Verify scrolling, dropdown open/close, page changes and reduced window heights in the inspection host.

Application profiles use version 2 for eight input routes and still read version 1. Endpoint IDs remain authoritative when older manually selected routes are classified into the new pages. Discovery-only rows are unchecked and omitted from saved profiles. Closed configured apps remain in the model but are hidden by the UI; session membership changes re-arm pickup. Windows process handles are retained to identify exited owners without relying on a reused PID. Refresh releases this client's cached session interfaces without restarting audio services.

## Package a preview

```powershell
.\scripts\Package-Prerelease.ps1
```

Packaging builds Release, runs the regression tests, checks the EXE version, and creates a fresh staging folder. It copies only the EXE and public documentation/artwork, never existing profiles, logs, debug symbols or MIDI preview binaries. Outputs include a portable ZIP, artwork ZIP and SHA256SUMS.txt under `artifacts/releases/0.4.0-preview.2`.

The package is intentionally labelled **unsigned**. Signing and installer work are separate from this portable test release. New release versions must update `Version.h`, the packaging default, README and release notes together.

## Publication boundaries

Personal `.ini`, `.log`, `.user`, local reports, build outputs and dependency caches are excluded from Git. Do not add credentials or signing material. The source tree includes no bundled VoiceMeeter DLL or Microsoft preview package.

Hardware acceptance is tracked in [Limitations.md](Limitations.md). Passing simulated tests is not a claim that all controllers or VoiceMeeter editions have been tested.
