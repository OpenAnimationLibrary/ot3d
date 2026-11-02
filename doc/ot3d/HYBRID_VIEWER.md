# Replacement hybrid Viewer — initial draft

## Implemented slice

`HybridSceneViewer` derives from the existing `SceneViewer` and replaces it
in `BaseViewerPanel`, so both ordinary and Combo Viewer panels gain the same
implementation. The persistent `SceneViewer` panel ID is unchanged. Locator
and other specialized uses can retain the base class. Tablet input, drawing,
onion skin, navigation, playback, full-screen, room persistence and Preview
remain inherited; this is not a second incompatible drawing-tool stack.

An exposed, camera-stand-visible GLB Model FX column supplies an immutable,
bounded, unprojected triangle snapshot through `T3DViewerSource`. The Viewer
draws triangles with OpenGL without requesting a Preview or FX raster render.
The snapshot expands triangle lists, strips and fans while preserving winding,
XYZ, node transforms and supported node animation,
the Model FX instance transform, material color overrides and wireframe mode.
Frame changes and changed file revisions invalidate the snapshot.

The initial coordinate bridge uses the existing GLB FX's 1000-unit reference
height divided by its Orthographic Height. Node and instance transforms apply
before this bridge; column placement then applies in OT stage coordinates.
Camera-stand/camera views use the existing column-perspective transform;
the navigable 3D view uses the inherited orbit/pan/zoom matrix and column Z.
The GLB FX's own perspective camera is not a second scene camera in this path.

GL attributes, matrix mode/stack and the active shader program are restored.
No persistent GPU objects are introduced, so floating/docking a Viewer does
not leave resources tied to a destroyed context. Initial loading and geometry
preparation are synchronous; background preparation is still required.

## Explicit completion gates

This draft is not ready to replace a production Viewer yet:

- Windows/macOS/Linux visual and GPU checks. Native application compilation
  passed on all three platforms at the original PR head; corrections must
  pass updated platform CI too.
- Fixture-tested shared scene camera/units/clipping and 2D/3D depth. The first
  pass has **mesh-to-mesh depth only**, draws models after 2D, and clears depth
  for that pass. It does not claim correct interleaving with drawings.
- Sub-Xsheet ancestor transforms/edit-in-place, column masks/filter colors,
  and final FX DAG lighting/transform propagation. Direct GLB columns only.
- Skinning, morphs, transparent/alpha-cutout materials, textures and lighting.
  Unsupported geometry is skipped with diagnostics instead of a false pose.
- 3D picking, selection handles, renderer parity, async loading/cancellation,
  and large-scene GPU batching. Current immediate-mode drawing is a first slice.
- Reconcile the normal camera view with per-FX perspective, near/far and
  inherited Viewer depth clipping; current output is a staging approximation.
- EXR Inspector, color-management and deep-image viewing integration.

Preview retains the final-render path and suppresses the extra model pass.
Level editing, picking and camera-test rendering retain base Viewer behavior.

## Acceptance and validation

The standalone `glbviewer` regression compares transformed XY and retained Z
against the existing CPU renderer, checks node animation, indexed geometry,
list/strip/fan topology and winding, capacity rejection, malformed vertices,
opaque-alpha handling and unsupported-feature diagnostics.
It runs in the existing three-platform GLB workflow, including its Linux
sanitizer build. It does not validate a GL context or the native application.

Native acceptance: insert a GLB Model FX, choose a redistributable opaque
triangle/cube, expose its column, turn Preview off, and verify it appears.
Change instance transforms, scrub, hide/unexpose the column, reload the file,
orbit in 3D view, float/redock the Viewer, switch rooms, draw in a 2D level,
and toggle Preview to verify no duplicated model. Repeat at 100%/150% scaling.
Record platform, GPU and driver. Keep this PR draft until those checks pass.
