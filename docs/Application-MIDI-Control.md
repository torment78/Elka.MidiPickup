# Application MIDI Control — preview

Application MIDI Control is a separate window for controlling the volume or mute of an individual application on a VoiceMeeter playback input. The existing Strip and Bus pages keep their own pickup engine and Start/Stop controls.

The current local build is **0.4.0-preview.5**. Saved application assignments remain visible when their app disconnects; deleting a row frees its MIDI control immediately and saves the removal. The published 0.3.0 preview does not contain this window.

## First test

1. Exit the older Elka copy before opening this build. Preserve your `Elka.MidiPickup.ini`; it contains your working strip/bus setup. Start VoiceMeeter, choose your **MIDI device** and connect both in the main window.
2. Click **Application MIDI Control**. Potato shows **VAIO**, **AUX** and **VAIO 3**; Banana shows VAIO and AUX; Standard shows VAIO. Pages with saved assignments remain accessible even if an input is missing or VoiceMeeter is disconnected, so you can delete old bindings. All available pages can run together.
3. Play something in the application you want to control, routed to the chosen VoiceMeeter playback device. Click that input button. Applications appear automatically in the table, including paused apps that still hold an audio session. This is not a list of every installed program.
4. Tick **Use** beside each application you want to control. New apps are unchecked. No playback-endpoint or application dropdown is required. Audio must already be routed to the corresponding Windows playback device.
5. Select an application's row, choose **Volume / pickup**, then **Learn selected** and move an unused absolute CC fader or knob. The row shows its MIDI channel and CC number. Controls assigned to enabled Strip/Bus rows are reserved even when strip/bus pickup is stopped. To reuse one, uncheck that original row's Use box and restart any remaining strip/bus mappings.
6. Remove that new control's duplicate direct mappings in VoiceMeeter, Bome or other software, tick the preview's confirmation, and click **Start applications**. This starts the application rows only. A fader waits until it reaches or crosses the current app volume; the row then says **Tracking**. The default pickup tolerance is three MIDI steps.
7. Change the **application** volume with the mouse in VoiceMeeter's application list or the Windows volume mixer. The hardware should wait for pickup again. Move quickly in both directions, and check that other applications and whole-strip gains stay unchanged. Closing/reopening the app must re-arm its row rather than replay old movement.
8. Click **Save applications**. Mappings are stored beside the EXE in `Elka.MidiPickup.applications`, separately from strip/bus settings. This preview always starts stopped and requires its routing confirmation each run. **Stop** in this window stops application control; the main window's Stop stops strip/bus pickup. Hiding either window keeps its active controls running; exiting Elka stops both.

## Buttons and encoders

- **Mute button:** choose this action for a CC button, or learn a MIDI note and it will be selected automatically. Note-on presses toggle mute; note-off and velocity-zero messages release the button. For CC buttons, values below 64 are release and 64–127 are press. Release the button once after Start/Re-arm before the first press; this prevents a held button from toggling unexpectedly. Latching CC buttons that alternate 0/127 need momentary mode on the controller.
- **Relative encoder:** select the exact controller format before learning. **2's comp** uses 1 for +1 and 127 for −1. **Offset 64** uses 65 for +1 and 63 for −1. Each step changes the current scalar volume by 1/127. Relative actions do not need pickup because they begin from the current software value. Other relative encodings must not be guessed.
- Select an application and click **Extra control** for a separate mute row, then tick its Use box and Learn. One volume action and one mute action per application/endpoint are allowed. Two enabled rows cannot share a MIDI source or the same volume/mute target.

## Remove unwanted rows

Saved rows stay on their original input page when the app disappears or moves. The MIDI source column still shows the assigned channel and CC/note. Red statuses show **App offline**, **Input offline**, **Input unavailable**, or **Offline / on AUX** (for example). The latter means Windows currently lists the same executable on AUX and no longer on the saved input; Elka does not move the binding automatically. Offline rows cannot write audio or learn a new control.

Select a row and click **Delete row**, or right-click **Delete row and saved MIDI assignment**. This immediately saves the application setup, including other current application edits, removes that binding from the file, and frees its MIDI control. Other running controls continue. If the file cannot be saved, deletion fails visibly and the original assignment stays intact. Deleting Brave on AUX does not delete its VAIO 3 binding, close Brave, or change audio routing or volume.

Deleted rows stay hidden for the current Elka run. **Show removed (N)** redisplays them without restoring their MIDI assignments. After restarting Elka, a connected app can appear again as an unchecked, unlearned discovery row. An offline deleted app stays absent. Old profiles containing hidden learned assignments load those bindings as visible, disabled rows so you can find and delete them.

## Volume and application identity

Application volume ranges from **silence to 0 dB**. MIDI 0 is scalar zero, 127 is scalar one, and intermediate values are linear in the Windows session volume scalar. The software column displays `20 × log10(scalar)` dB; `M` marks mute. The strip/bus Top gain setting does not boost application volume above unity.

Mappings identify the exact playback endpoint and executable path, rather than a changing PID, display name prefix or list position. Apps with the same filename in different folders stay distinct. Several sessions from the same executable on the same endpoint are treated as one application. If their volumes or mute states disagree, the corresponding action shows **Mixed session levels** and waits; align those levels in the Windows mixer first. Browser tabs share their browser application's identity.

Closed apps and expired sessions leave the table automatically only when they have no configured row. Learned assignments stay visible in red and return to normal when the same app and playback device reconnect. New or changed sessions re-arm pickup. **Refresh apps** releases Elka's session cache and rescans without resetting audio; it also re-arms application faders. A paused/background app can legitimately retain an inactive session, so it stays visible while its process and session remain alive. Elka does not forcibly clear VoiceMeeter's or Windows Volume Mixer's own lists. [Windows session states](https://learn.microsoft.com/en-us/windows/win32/api/audiosessiontypes/ne-audiosessiontypes-audiosessionstate).

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
