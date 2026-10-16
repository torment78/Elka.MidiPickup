# Compatibility milestone — 16 October 2026

**Historical investigation:** These are dated observations from one development PC, not requirements for the portable release or a guarantee about another Windows installation. The current **0.3.0-preview.1** desktop app provides a direct MIDI-input to VoiceMeeter API path, with sixteen pickup rows and a disconnected demo mode. See [Testing.md](Testing.md). It does not need the native preview runtime below. This route avoids virtual-port creation and requires the user to disable selected faders' duplicate mappings.

## Verified on this PC

- Windows reports **26H2, build 26300.9457**.
- Windows MIDI Services (`midisrv`) is running.
- With the Windows compatibility manifest enabled, the service executable's fixed file version is **10.0.26100.9444**. The localized FileVersion string seen in an earlier PowerShell inventory differs; the probe records the fixed version resource.
- The installed app-to-app virtual and MIDI 2 loopback transport DLLs report **1.0.15.0**.
- The new `Windows.Devices.Midi2.dll` is absent from System32. The local probe can load Microsoft's **0.99.92-preview.11** API beside the executable.
- VoiceMeeter Potato is running; the probe logged into its Remote API and read all 8 strip gains and all 8 bus gains successfully.
- The APC40 mkII can be opened twice simultaneously by the probe. Both handles were closed after the check.
- Native SDK endpoint enumeration works. Isolated diagnostic loopback delivery passed **128 ordered messages A → B and 128 B → A**, with exact payloads.
- **Release and Debug x64** builds both passed using Visual Studio 2026 / v145. The probe rejected an invalid monitor duration and a nonexistent input.
- A 30-second APC40 capture reported **0 messages and 0 errors/overruns**. No physical movement reception was verified. This does not by itself establish a controller fault.

These checks made no changes to VoiceMeeter mappings, Windows updates, the MIDI service configuration, or installed drivers. Existing Bome MIDI ports are present; the native test does not use them.

## Why the full pass-through test is not complete

Microsoft's [issue 1047](https://github.com/microsoft/MIDI/issues/1047) documents an app-owned virtual-endpoint cleanup defect that can leave the shared MIDI service unusable. The maintainer states that its fix is scheduled for the **late-November 2026 Windows rollout**, with staged enablement. A closed GitHub issue therefore does not mean the fix is installed on this machine.

The reported affected transport version matches this PC's installed version. We did not create an app-owned virtual endpoint on this active MIDI setup. Merely restarting the service, installing the preview SDK, or running C++ instead of C# does not establish that the service fix is present.

The separate native MIDI 2 loopback transport is present. A transport availability check and diagnostic loopback success do not prove that user loopback creation/management works with this preview API and the older in-box transport. Earlier [Preview 8 release notes](https://github.com/microsoft/MIDI/releases/tag/inbox-dev-preview-8) explicitly describe this management compatibility limitation ahead of the November Windows changes. No user loopback was created or removed during this milestone.

The [Preview 11 release notes](https://github.com/microsoft/MIDI/releases/tag/inbox-preview-11) lock the API surface but refer to previous notes for service limitations. Preview Tools and Transports installers are distinct from Windows servicing. The development package was downloaded directly from Microsoft's release and verified against its published SHA-256. No preview installer was run.

## Remaining acceptance checks

1. Verify the delivered Windows service fix or establish a supported alternative native loopback method using the exact installed versions.
2. Create the intended Elka endpoint and confirm it appears in VoiceMeeter's MIDI input selection.
3. Forward actual APC40 messages through it, including unselected controls and relevant feedback/SysEx behavior.
4. Validate the MIDI-to-gain mapping against VoiceMeeter.
5. The pickup engine is now implemented and regression-tested in the direct-control app. Validate its actual physical-controller behavior and later integrate it into the virtual pass-through route.

The input monitor is available for physical movement verification. An open handle and successful synthetic diagnostic messages do not prove that a physical fader was moved or that VoiceMeeter received it through Elka.

## Resource measurement

The final Release x64 diagnostic run reported **13,396 KiB working set / 2,552 KiB private commit**. The native endpoint test reported **14,476 KiB working set / 5,980 KiB private commit**. The idle 30-second input capture ended at **10,672 KiB working set / 2,012 KiB private commit**. These are probe measurements, not a C# comparison or a forecast of the finished application's CPU and memory use.

## Dependencies and distribution

The project uses Microsoft Windows MIDI Preview 11 locally for development. Its [go-live conditions](https://github.com/microsoft/MIDI/releases/tag/inbox-preview-11) must be reviewed before distributing an installer. Current preview installers/transports must not be bundled as if they were ordinary redistributables. The eventual prerequisite UI must report the exact missing capability rather than treating the Windows display version alone as sufficient.

Microsoft's [Windows MIDI Services documentation](https://microsoft.github.io/MIDI/) is the primary reference. Release notes and actual component/runtime tests take precedence over assumptions based only on the OS version label.
