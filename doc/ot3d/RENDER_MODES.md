# Render modes

Output Settings and Preview Settings each have a **Render Mode** section.
Both options default to off, preserving existing scenes. Output and preview
choices are independent, including when their color settings are synchronized.
The options are saved in scenes and output presets.

## Flat Render (No FX or Lighting)

Renders the exposed source columns with their base colors, bypassing the FX
schematic and all 3D lighting. Hidden render columns stay hidden. Stage-object
placement, camera framing, column opacity/color filters and nested xsheets still
apply. GLB source generators keep their model, material/color selection,
animation and instance placement. Other source generators keep their own source
content.

This mode also works when the schematic's Output node is disconnected. FX-based
transforms, composites and lighting nodes are bypassed along with other FX; use
stage-object placement or the GLB source's own transforms for placement that must
remain visible in a flat render.

## Disable Default Lighting

Requires explicit lighting for GLB and native Animation:Master model surfaces.
Models without a Three Point Lighting rig render black, retaining their coverage
and transparency. Connected lighting rigs, including chained rigs, continue to
illuminate their source. This overrides the GLB source's Headlight and Unlit
fallbacks. Ordinary 2D artwork and unlit image planes retain their source colors.

Flat Render takes precedence over this switch. Its checkbox is temporarily
disabled while Flat Render is on, preserving its previous choice for when Flat
Render is turned off. Background color/transparency follows the scene settings.

These controls affect rendered output and rendered preview. The interactive
camera-stand viewer keeps its existing display behavior.
