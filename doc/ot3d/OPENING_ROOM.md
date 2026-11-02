# OT3D opening room

The first room in the Default room set is **OT3D**. It places the vertical
Xsheet on the left, the standard Viewer in the center, and the FX Schematic
on the right. The command bar and tool options span the top; the drawing
Tool Bar sits beside the Viewer. Playback remains in the Viewer.

The 1920-pixel template assigns approximately 22% to timing, 50% to the
Viewer and its tools, and 28% to the schematic. Dock separators remain
resizable. The Schematic starts in FX mode (`viewtype=0`); its existing
Stage/FX toggle remains available. FX Settings, palettes, Python and future
EXR inspection panels can be opened when needed.

Existing Drawing and other room templates remain available. Existing saved
personal layouts and the last selected room are preserved. On an existing
installation, select the Default room set and reset rooms to the template
through the application's room reset command to adopt the new opening room.
Keep a copy of customized rooms before resetting.

## Acceptance

- With fresh Default room preferences, the OT3D tab is first and selected.
- At 1920x1080 and 1366x768, Xsheet, Viewer and FX Schematic appear in that
  order without floating panels; test 100% and 150% desktop scaling.
- Resize the separators, restart, and confirm saved geometry returns.
- Switch the schematic to Stage mode and back; open FX Settings.
- Draw, scrub, and play an ordinary 2D scene; confirm tool options and Viewer
  playback controls remain usable.

This template uses the existing `SceneViewer` panel identifier. The hybrid
Viewer work replaces its implementation while retaining room compatibility.
