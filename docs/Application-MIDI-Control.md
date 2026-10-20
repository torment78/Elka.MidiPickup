# Application MIDI Control — preview

Application MIDI Control is a separate window for controlling the volume or mute of an individual application on a VoiceMeeter playback input. The existing Strip and Bus pages keep their own pickup engine and Start/Stop controls.

This feature is new in the local **0.4.0-preview.1** build. It still needs physical MIDI and listening tests. The published 0.3.0 preview does not contain this window.

## First test

1. Exit the older Elka copy before opening this build. Preserve your `Elka.MidiPickup.ini`; it contains your working strip/bus setup. Start VoiceMeeter, choose your **MIDI device** and connect both in the main window.
2. Click **Application MIDI Control**. Potato shows **VAIO**, **AUX** and **VAIO 3**. Banana shows VAIO and AUX; Standard shows VAIO. All available pages can run together.
3. Play something in the application you want to control, routed to the chosen VoiceMeeter playback endpoint. Select that input page. **Connected apps** lists Windows shared-mode audio sessions on that endpoint, including sessions that are currently silent. It is not a list of every installed program.
4. Check **Playback endpoint**. The ordinary named VoiceMeeter endpoints are selected automatically when unambiguous. If you renamed endpoints or use VoiceMeeter's custom Audio Map, choose the actual playback endpoint yourself. The extra **Voicemeeter In 1–5** devices are not assumed to belong to VAIO. Changing this chooser affects discovery and new rows; it never silently retargets existing rows.
5. Select an application and click **Add application**. Select its row, choose **Volume / pickup**, then **Learn selected** and move an unused absolute CC fader or knob. The row shows its MIDI channel and CC number. Controls assigned to enabled Strip/Bus rows are reserved even when strip/bus pickup is stopped. To reuse one, uncheck that original row's Use box and restart any remaining strip/bus mappings.
6. Remove that new control's duplicate direct mappings in VoiceMeeter, Bome or other software, tick the preview's confirmation, and click **Start applications**. This starts the application rows only. A fader waits until it reaches or crosses the current app volume; the row then says **Tracking**. The default pickup tolerance is three MIDI steps.
7. Change the **application** volume with the mouse in VoiceMeeter's application list or the Windows volume mixer. The hardware should wait for pickup again. Move quickly in both directions, and check that other applications and whole-strip gains stay unchanged. Closing/reopening the app must re-arm its row rather than replay old movement.
8. Click **Save applications**. Mappings are stored beside the EXE in `Elka.MidiPickup.applications`, separately from strip/bus settings. This preview always starts stopped and requires its routing confirmation each run. **Stop** in this window stops application control; the main window's Stop stops strip/bus pickup. Hiding either window keeps its active controls running; exiting Elka stops both.

## Buttons and encoders

- **Mute button:** choose this action for a CC button, or learn a MIDI note and it will be selected automatically. Note-on presses toggle mute; note-off and velocity-zero messages release the button. For CC buttons, values below 64 are release and 64–127 are press. Release the button once after Start/Re-arm before the first press; this prevents a held button from toggling unexpectedly. Latching CC buttons that alternate 0/127 need momentary mode on the controller.
- **Relative encoder:** select the exact controller format before learning. **2's comp** uses 1 for +1 and 127 for −1. **Offset 64** uses 65 for +1 and 63 for −1. Each step changes the current scalar volume by 1/127. Relative actions do not need pickup because they begin from the current software value. Other relative encodings must not be guessed.
- Add the same application a second time for a separate mute control. One volume action and one mute action per application/endpoint are allowed. Two enabled rows cannot share a MIDI source or the same volume/mute target.

## Volume and application identity

Application volume ranges from **silence to 0 dB**. MIDI 0 is scalar zero, 127 is scalar one, and intermediate values are linear in the Windows session volume scalar. The software column displays `20 × log10(scalar)` dB; `M` marks mute. The strip/bus Top gain setting does not boost application volume above unity.

Mappings identify the exact playback endpoint and executable path, rather than a changing PID, display name prefix or list position. Apps with the same filename in different folders stay distinct. Several sessions from the same executable on the same endpoint are treated as one application. If their volumes or mute states disagree, the corresponding action shows **Mixed session levels** and waits; align those levels in the Windows mixer first. Browser tabs share their browser application's identity.

An unavailable app remains in the mapping table as **App offline**. A new or changed set of sessions re-arms pickup. If an executable moves to a different folder during an update, add it again; the old mapping stays offline. Unidentifiable or cross-process sessions are omitted rather than assigned to a guessed application. Exclusive-mode audio, ASIO and apps routed outside the selected VoiceMeeter endpoint are outside this preview.

If a write is rejected or the target changes during a write, that row shows **Stopped / re-arm**. Check its current volume and press **Re-arm**. Disconnecting MIDI or VoiceMeeter, entering Demo, or learning a strip/bus control stops the application preview; reconnect and explicitly start it again.

## Why Windows audio sessions are used

VoiceMeeter documents its Potato application gain/mute commands as **write-only**. Pickup requires the current value and a reliable application identity, so the preview uses Windows Core Audio for session enumeration and volume/mute reads and writes. The VoiceMeeter connection provides edition detection; MIDI comes from the same input already open in the main window. No second MIDI device connection or virtual driver is required.

References: [VoiceMeeter Remote API, application commands](https://download.vb-audio.com/Download_CABLE/VoicemeeterRemoteAPI.pdf), [Microsoft ISimpleAudioVolume](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-isimpleaudiovolume), [audio-session enumeration](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nn-audiopolicy-iaudiosessionenumerator).

## Acceptance still required

- Verify a real controller with a real application on each input you use.
- Confirm VoiceMeeter's application slider and the Windows session readback agree on your version.
- Check fast sweeps, external edits, app restart, audio-service/device recovery and multiple apps.
- Check mute press/release behavior and the selected relative-encoder encoding.
- Confirm existing strip/bus pickup remains smooth while application control is running.

Automated tests exercise the state machine and the full controller against a silent test-owned audio session. Those checks do not replace this physical acceptance test.
