# MIDI Pickup icon

`Elka.MidiPickup.png` is the full-resolution approved artwork. `Elka.MidiPickup.ico` packages that same artwork at 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 pixels, preserving alpha transparency. Both are embedded/referenced by the Visual Studio project; only the ICO is embedded into the executable by `App.rc`. Runtime window/taskbar and tray icons use the same resource.

Rebuild the ICO with `scripts/Build-Icon.ps1`. This is optional for normal builds: the finished ICO is already in the source tree.
