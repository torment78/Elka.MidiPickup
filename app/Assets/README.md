# MIDI Pickup icon

`Elka.MidiPickup.png` is the full-resolution approved artwork. `Elka.MidiPickup.ico` packages that same artwork at 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 pixels, preserving alpha transparency. Both are embedded/referenced by the Visual Studio project; only the ICO is embedded into the executable by `App.rc`. Runtime window/taskbar and tray icons use the same resource.

Rebuild the ICO with `scripts/Build-Icon.ps1`. This is optional for normal builds: the finished ICO is already in the source tree and no image-generation service is needed to compile.

Created with the built-in image-generation tool on 2026-10-16. The user approved the initial design after asking for consistency with the existing Elka app icons. FX Host, VBAN Stream and EchoPad artwork was inspected for that comparison; the approved design was retained without further edits.

## Generation prompt

Use case: logo-brand. Create one finished Windows desktop application icon for Elka MIDI Pickup, an audio MIDI fader soft takeover app. Square icon, isolated on transparent background. A dark charcoal rounded square tile (#111417 / #1A2025), occupying 90% of the canvas. Inside it, a bold very simple mixer-fader symbol: two clean upright fader tracks with generous spacing, one turquoise (#22A6B3), one pale white (#EDF3F6), their broad horizontal slider caps aligned at the middle to suggest software and hardware meeting. A single small warm orange (#F08A3E) pickup indicator at the meeting point is the only extra accent. Strong uncluttered silhouette, flat precise geometric shapes, subtle polished surface shading only, professional restrained Elka audio-tool style. Centered, balanced, generous internal margins, readable at 16 and 32 pixels, no micro-details. No text, no letters, no labels, no numbers, no musical notes, no surrounding presentation, no mockup, no watermark. Output a single full-resolution icon asset, transparent outside the rounded tile.
