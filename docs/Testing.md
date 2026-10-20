# Testing the desktop app

For the new separate beta window, see [Application MIDI Control](Application-MIDI-Control.md). The steps below describe the existing strip/bus feature.

## First physical test

Use one fader initially. Start VoiceMeeter and open `Elka.MidiPickup.exe` from the extracted portable package (or `artifacts\x64\Release` when building from source).

1. Choose your **MIDI device → Connect MIDI**, then **Connect VM**. The software column should show your actual gains. The message count should increase when you move the controller.
2. Click **Learn** on row 1, then move the desired fader. Its channel and CC number should appear. Learn listens until you supply a CC or click Cancel; it has no capture countdown.
3. Use **Strips** or **Buses**, then select the destination in **Target**. Both pages remain active together. Strip numbering is one-based in the app: Strip 1 controls API parameter `Strip[0].Gain`.
4. Remove/disable the same fader's existing mapping in VoiceMeeter or Bome. Do this for selected faders only. Elka does not alter those applications' mappings. Otherwise the other route can move the gain immediately, regardless of Elka's pickup state.
5. Open **Settings** and choose **Top gain**. New setups default to +12 dB; existing setups keep their saved value. MIDI 0 is -60 dB and MIDI 127 is the chosen maximum, with linear dB values in between. A target above that maximum is unreachable; the app shows **Outside range** rather than snapping to a boundary.
6. Tick the direct-mapping confirmation and click **Start pickup**. Move the hardware toward the software position. It should hold the software level until the target is reached/crossed, then track the hardware.
7. Use your mouse to change that software fader. The row should return to **Move up/down to pick up**. Move the hardware to the new target and confirm it picks up again.
8. Click **Stop**. Elka should keep showing input and software values without writing gains. **Save setup** stores your mappings, acknowledgement and startup options for next time.

For VoiceMeeter Potato, Bus 1–5 correspond to A1–A5 and Bus 6–8 to B1–B3. Banana and Standard have fewer valid buses; an unavailable target cannot be enabled. The current GUI uses generic Bus numbering.

## Demo test

Under **Settings**, select **Demo mode (no live audio)**. It closes both live connections and temporarily sets up one simulated fader. Your real mappings are restored when leaving demo; you must reconnect the real devices. **Save setup** is disabled in demo so simulated mappings cannot overwrite your setup.

- Click **Start pickup**. The software starts at -18 dB; the hardware starts well below it.
- Move the left slider upward without crossing the software level. The software must stay still.
- Cross it. The row should say **Tracking**, and the software should follow further movement.
- Move the right slider to another level. The row must wait for pickup again.
- Stop, move the left slider, and confirm the software stays still.

Demo events never call the VoiceMeeter write API. The left slider always simulates channel 1 / CC 7. It is for testing the first demo fader, rather than simulating a complete controller.

## VoiceMeeter display button

After **Connect VM**, supported Potato versions show **MIDI Pickup** in custom display button **2**. This is API slot 1 (zero-based), separate from FX Host's button 1/API slot 0. Clicking the button hides or shows Elka; the MIDI processing worker keeps running. The label appears after the connection settles, normally within a few seconds.

**Hide** and the window X also hide the app. Double-click its tray icon, choose **Open** from the tray menu, or launch the EXE again to restore it. **Exit** stops pickup, removes its VoiceMeeter button, and closes the process. If a VoiceMeeter version lacks the optional custom-button API, MIDI pickup still works and the tray remains available.

Shutdown allows about 1.5 seconds for the asynchronous button-removal command. Verify that both the label and outline disappear, and that FX Host's button 1 stays as it was.

## Automatic startup

1. Learn the required mappings on both pages. Confirm their other direct mappings are disabled, then tick the confirmation.
2. In **Settings**, enable **Launch with Windows**, **Launch in tray**, and **Launch active**, then **Save setup**. The startup registration points to this exact executable, so use the Release copy for daily use.
3. Click **Exit**, wait for the process to close, then open the same EXE. The window should remain in the tray; VoiceMeeter button 2 should appear once the connection is ready.
4. Open it from the tray or VoiceMeeter button 2. Check both connections and **PICKUP RUNNING**. Gain values must remain unchanged until you physically acquire a target. Both Strip and Bus mappings remain armed while either page is hidden.
5. If the saved MIDI device or VoiceMeeter is not available, startup waits and retries every two seconds. It must not substitute another input. **Stop** cancels the wait; manual connection/settings changes also cancel it.
6. Sign out/in when convenient to check Windows startup. To disable it, uncheck **Launch with Windows** and save. No Windows restart or sign-out is performed by the app.

If an older setup has no saved direct-mapping acknowledgement, the app connects but remains in monitor mode. Confirm the mapping arrangement once and save again. A diagnostic gain/input/write failure still stops control; startup automation does not repeatedly override that stop.

## Donation button

The orange button in the lower right opens **https://ko-fi.com/msffixit** in the default browser. It is the same destination used by VBAN Stream; no donation or payment is initiated by the app.

## If something does not work

- **No messages:** verify the selected physical input, reconnect it, and move a fader. A successful connection alone does not prove message reception. Windows/driver sharing must be available if other apps also hold the device.
- **Messages arrive but Learn does not complete:** this build learns MIDI Control Change messages. Notes, relative encoders and high-resolution fader protocols are not supported as pickup sources.
- **The software jumps before Tracking:** another mapping may still be acting on that fader. Stop pickup and check the selected fader's mappings in VoiceMeeter/Bome.
- **Target unavailable:** connect VoiceMeeter and choose a strip/bus that exists in your edition.
- **Outside range:** bring the software value into the selected -60-to-Top range or choose a higher Top gain. The app intentionally does not clamp a live software value into pickup.
- **Slow readback:** after two seconds without confirming progress, only that fader waits for pickup again. The session stays running, the confirmation stays checked, and other faders keep working. Move to the displayed software level to regain control.
- **Timing recovery:** stale MIDI is discarded and pickup re-arms; Start pickup stays active. Windows busy notifications are valid MIDI packets and are counted as **Bursts**, not errors.
- **Output stopped after a real error:** read the bottom message. A disconnected mixer/input, invalid gain, malformed MIDI, or rejected write still stops control. Details are in `Elka.MidiPickup.log` beside the EXE.
- **Settings do not save:** the EXE folder must be writable. This is a portable test build; its setup file lives beside the EXE.

The gain API range is documented by [VB-Audio](https://download.vb-audio.com/Download_CABLE/VoicemeeterRemoteAPI.pdf). Elka's displayed linear conversion is a test-app choice; the future virtual pass-through version still needs validation against VoiceMeeter's own MIDI mapping curve.

## Retest for fast movement (0.2.1)

1. Use **Top gain +12 dB**, **Tolerance 3 steps**, learn your faders, and **Save setup**.
2. Start pickup and acquire one target. Sweep quickly up and down, then reverse direction several times. It should continue tracking and Start pickup should remain disabled because the session is running.
3. Change tolerance to two steps while tracking. Control should stay active; tolerance affects the next pickup only.
4. Move the software fader with the mouse. Only that fader should wait for pickup again. Check a second acquired fader still works.
5. If tracking unexpectedly drops, note **Readback re-arms**, **External**, **Input resets**, **Max gap**, and the bottom message. The log distinguishes a timing recovery from an unmatched mixer value.

Microsoft documents that [`MIM_MOREDATA`](https://learn.microsoft.com/en-us/windows/win32/multimedia/mim-moredata) contains valid MIDI data and should be buffered with minimal callback work. The app now follows this path instead of treating the notification as a fatal error.
