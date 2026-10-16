# Prerelease limitations

This release is a working direct-control preview, not the completed virtual MIDI pass-through application.

- Input is limited to 7-bit **absolute MIDI Control Change** values. Relative encoders, 14-bit CC, pitch bend, motorized feedback and SysEx forwarding are not implemented.
- Elka sends gains through the VoiceMeeter Remote API and sends no MIDI back to the controller.
- Selected faders must have their duplicate mappings disabled in other software. Elka does not edit those mappings automatically.
- Other controller functions are not forwarded by Elka. Sharing the physical device depends on Windows and the device driver; simultaneous access cannot be assumed.
- The gain curve is linear in dB from -60 to the selected maximum. It is not a reproduction of every VoiceMeeter MIDI-mapping curve.
- There are eight Strip rows and eight Bus rows. The installed VoiceMeeter edition determines which destinations are valid.
- The optional display button requires a supported VoiceMeeter Potato version. The tray remains available when this API is absent.
- VoiceMeeter must use its standard installation location for the current DLL discovery code.
- This is a portable, unsigned x64 Windows build. Settings and logs require a writable folder. There is no installer, updater, driver installation or Windows upgrade step.
- Windows 11 26H2 was used during development. Other Windows builds, devices and mixer editions still need testing. The GUI does not require the optional Microsoft MIDI preview SDK.
- Saved device names are used for reconnecting. Test setups with identical device names before enabling unattended active startup.
- Readback timeout re-arms the affected fader; actual device/API failures stop output. Check the status message and reconnect/rearm as appropriate.

## Before promotion to stable

- [ ] Confirm fast sweeps and reversals over longer sessions on physical hardware.
- [ ] Confirm independent pickup on multiple Strips and Buses.
- [ ] Confirm external software edits re-arm only the intended mapping.
- [ ] Confirm button 2 label **and outline** disappear after Exit, without changing FX Host button 1.
- [ ] Confirm tray launch and reopening across supported DPI settings.
- [ ] Confirm Windows sign-in launch and delayed device/mixer availability.
- [ ] Confirm active startup reads fresh gains and produces no gain writes before acquisition.
- [ ] Test disconnected/reconnected controllers, sharing and additional VoiceMeeter editions.

The original native MIDI virtual-endpoint investigation is retained as dated research in [Compatibility.md](Compatibility.md). Its machine-specific findings are not a general compatibility promise. Native pass-through and assisted prerequisite installation remain future work.
