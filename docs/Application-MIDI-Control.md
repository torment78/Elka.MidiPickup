# Application MIDI Control — preview

Application MIDI Control is a separate window for controlling the volume or mute of an individual application on a VoiceMeeter playback input. The existing Strip and Bus pages keep their own pickup engine and Start/Stop controls.

The current local build is **0.4.0-preview.3**. It adds removable application rows and limits input pages to VAIO, AUX and VAIO 3. The published 0.3.0 preview does not contain this window.

## First test

1. Exit the older Elka copy before opening this build. Preserve your `Elka.MidiPickup.ini`; it contains your working strip/bus setup. Start VoiceMeeter, choose your **MIDI device** and connect both in the main window.
2. Click **Application MIDI Control**. Potato shows **VAIO**, **AUX** and **VAIO 3**; Banana shows VAIO and AUX; Standard shows VAIO. Missing playback devices have disabled buttons. All pages can run together.
3. Play something in the application you want to control, routed to the chosen VoiceMeeter playback device. Click that input button. Applications appear automatically in the table, including paused apps that still hold an audio session. This is not a list of every installed program.
4. Tick **Use** beside each application you want to control. New apps are unchecked. No playback-endpoint or application dropdown is required. Audio must already be routed to the corresponding Windows playback device.
5. Select an application's row, choose **Volume / pickup**, then **Learn selected** and move an unused absolute CC fader or knob. The row shows its MIDI channel and CC number. Controls assigned to enabled Strip/Bus rows are reserved even when strip/bus pickup is stopped. To reuse one, uncheck that original row's Use box and restart any remaining strip/bus mappings.
6. Remove that new control's duplicate direct mappings in VoiceMeeter, Bome or other software, tick the preview's confirmation, and click **Start applications**. This starts the application rows only. A fader waits until it reaches or crosses the current app volume; the row then says **Tracking**. The default pickup tolerance is three MIDI steps.
7. Change the **application** volume with the mouse in VoiceMeeter's application list or the Windows volume mixer. The hardware should wait for pickup again. Move quickly in both directions, and check that other applications and whole-strip gains stay unchanged. Closing/reopening the app must re-arm its row rather than replay old movement.
8. Click **Save applications**. Mappings are stored beside the EXE in `Elka.MidiPickup.applications`, separately from strip/bus settings. This preview always starts stopped and requires its routing confirmation each run. **Stop** in this window stops application control; the main window's Stop stops strip/bus pickup. Hiding either window keeps its active controls running; exiting Elka stops both.

## Buttons and encoders

- **Mute button:** choose this action for a CC button, or learn a MIDI note and it will be selected automatically. Note-on presses toggle mute; note-off and velocity-zero messages release the button. For CC buttons, values below 64 are release and 64–127 are press. Release the button once after Start/Re-arm before the first press; this prevents a held button from toggling unexpectedly. Latching CC buttons that alternate 0/127 need momentary mode on the controller.
- **Relative encoder:** select the exact controller format before learning. **2's comp** uses 1 for +1 and 127 for −1. **Offset 64** uses 65 for +1 and 63 for −1. Each step changes the current scalar volume by 1/127. Relative actions do not need pickup because they begin from the current software value. Other relative encodings must not be guessed.
- Select an application and click **Extra control** for a separate mute row, then tick its Use box and Learn. One volume action and one mute action per application/endpoint are allowed. Two enabled rows cannot share a MIDI source or the same volume/mute target. Right-click **Clear MIDI assignment** to discard an assignment; a connected app still appears as an unchecked discovery row if no other assignment remains.

## Remove unwanted rows

Select a row and click **Remove row**, or right-click it and choose **Remove row from Elka**. This hides that row and disables its MIDI control. Other rows continue running. It stays removed through refreshes and app restarts; click **Save applications** to remember the removal after restarting Elka. Removing Brave on AUX does not remove its row on VAIO 3.

**Restore removed (N)** restores the removed rows for the selected input. Their learned MIDI assignments remain available, but **Use** stays unchecked until you enable them again. Save applications again to remember the restoration. Removing a row does not close the application, change its volume or move its audio to another device.

## Volume and application identity

Application volume ranges from **silence to 0 dB**. MIDI 0 is scalar zero, 127 is scalar one, and intermediate values are linear in the Windows session volume scalar. The software column displays `20 × log10(scalar)` dB; `M` marks mute. The strip/bus Top gain setting does not boost application volume above unity.

Mappings identify the exact playback endpoint and executable path, rather than a changing PID, display name prefix or list position. Apps with the same filename in different folders stay distinct. Several sessions from the same executable on the same endpoint are treated as one application. If their volumes or mute states disagree, the corresponding action shows **Mixed session levels** and waits; align those levels in the Windows mixer first. Browser tabs share their browser application's identity.

Closed apps and expired sessions leave the visible table automatically. Learned assignments remain in your saved setup and return with the same app and playback device. New or changed sessions re-arm pickup. **Refresh apps** releases Elka's session cache and rescans without resetting audio; it also re-arms application faders. A paused/background app can legitimately retain an inactive session, so it stays visible while its process and session remain alive. Elka does not forcibly clear VoiceMeeter's or Windows Volume Mixer's own lists. [Windows session states](https://learn.microsoft.com/en-us/windows/win32/api/audiosessiontypes/ne-audiosessiontypes-audiosessionstate).

VoiceMeeter's app list comes from Windows audio sessions. The documented Windows session interfaces do not offer a command to delete another application's session. Clearing stored volume/routing settings in the registry is not the same as releasing that application's open streams. Fully exiting and reopening the affected app may release its old streams; **Refresh apps** then rescans Elka's list. Background processes may keep a session alive. [Microsoft audio sessions](https://learn.microsoft.com/en-us/windows/win32/coreaudio/audio-sessions), [session control interface](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nn-audiopolicy-iaudiosessioncontrol2).

Mappings from preview.1 and preview.2 load automatically and keep their exact endpoint identity. Older In 1–5 mappings are retained in the file but cannot control audio or reserve a MIDI source. If an executable moves to a different folder during an update, learn its new row; the old assignment remains saved. Inputs renamed beyond recognition are not guessed; restore the standard Windows playback-device name for automatic input buttons. Unidentifiable or cross-process sessions are omitted. Exclusive-mode audio, ASIO and apps routed outside VoiceMeeter are outside this preview.

If a write is rejected or the target changes during a write, that row shows **Stopped / re-arm**. Check its current volume and press **Re-arm**. Disconnecting MIDI or VoiceMeeter, entering Demo, or learning a strip/bus control stops the application preview; reconnect and explicitly start it again.

## Reset audio

If an application remains listed on an old input after you move its audio elsewhere, click **Reset audio** at the top right. Connect VoiceMeeter in the main window first; MIDI does not need to be connected for the reset.

This single button:

1. Requests Windows administrator approval and restarts Windows Audio, restoring the running services that depend on it.
2. Waits one second after those services are running again.
3. Requests VoiceMeeter's audio engine restart and refreshes Elka's application list.

All Windows audio briefly stops. Applications remain open, but some may need playback resumed. Routing, saved device choices and volume settings are not reset. Live applications can recreate their sessions, so this is not a way to hide an application permanently.

Elka temporarily pauses MIDI output and discards motion received during the reset. Controls that were running return armed for pickup; stopped controls remain stopped. Cancelling administrator approval skips both restarts. If the Windows reset fails, VoiceMeeter is not restarted and the status message explains the problem. The button is disabled until the current reset finishes.

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
