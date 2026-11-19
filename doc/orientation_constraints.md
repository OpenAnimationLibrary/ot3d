# Aim At and Orient Like

In the **Stage Schematic**, right-click a drawing column, native model column,
or pegbar and choose **Constraints > Aim At...** or **Orient Like...**.
Choose a target, influence, and optional additional rotations. Edits, enabling,
removal, and Clear All support Undo/Redo.

- **Aim At** points local +X toward the target's pivot. Planar rigs aim in XY;
  spatial rigs aim in XYZ, using world +Z as up and +Y near the vertical pole.
  Coincident pivots retain their input orientation.
- **Orient Like** matches the target's world orientation without copying its
  translation, scale, or shear. It compensates for the affected object's
  existing parent rotation, including a target that is also its parent.
- **Keep current orientation** captures the initial rotational difference when
  adding or editing Orient Like on This Object. Turn it off to align the axes
  immediately. Additional X/Y/Z rotations are applied after the stored offset.
- **Influence** blends along the shortest quaternion arc. These controls are
  persistent constraint settings; they are not new Function Editor channels.
  Existing position, scale, exposure and drawing animation remain live. At full
  influence the constraint determines orientation, overriding authored rotation.
- **Child Objects (automatic)** applies once to each direct child root; further
  descendants inherit the result normally. Keep current orientation is available
  for This Object only, since children can have different initial offsets.

## Attaching a drawing to a model

1. Parent the drawing column to the model in the Stage Schematic. Parenting
   supplies the positional attachment.
2. On the drawing, add **Orient Like** with that model as target.
3. Use Keep current orientation or additional rotation offsets to place the
   artwork against the required side. Adjust its position and scale normally.
4. Rotate the model, including Rotation X/Y, and edit or animate the drawing.

Ordinary drawing columns with an active orientation dependency on a spatial
object are projected as textured planes in **Preview and final rendering**.
Raster, Toonz Raster and vector simple levels enter through their normal column
image path. The plane retains the drawing's local coordinates when artwork
bounds change; it is not automatically recentered. Camera perspective, near-plane
clipping, alpha and column DPI are evaluated before the image is composited.
The editing view uses the planar part of the transform; use Preview to judge
perspective. Ordinary unconstrained columns keep their existing rendering path.

This is rigid attachment. It does not wrap artwork around curved surfaces,
follow individual bones or CP deformation inside a model, or provide shared
2D/3D surface occlusion. Use column stacking for compositing. Model movement
must be on Stage Schematic objects; transforms inside an FX node do not become
stage-object constraint targets. Full surface decals remain separate work.

The spatial drawing path currently follows Image Plane's bounded 32-bit input
capture (8192 pixels per side, at most 16 megapixels). Additional downstream
geometry/material modifiers are not supported for these drawing snapshots.

## Persistence and dependencies

Constraints preserve authored animation channels and are saved with the scene.
OT-Dev's original Aim At records are accepted; Buffer/gravity assignments are
outside this port. Untouched objects acquire no constraint tags.

Targets carry persistent identities as well as column IDs. Column insertion,
swapping, copied-rig remapping and target deletion/restoration preserve or suspend
links instead of silently attaching them to another column. Missing targets are
ignored until restored. New cyclic links are rejected; cycles introduced by
scene editing or reparenting are skipped during evaluation.

## Validation

- Standalone orientation regression: XYZ orientation, pivot preservation, scale/
  shear preservation, partial influence, shortest-arc wrapping, poles,
  coincident targets, reflected scale and singular transforms.
- Renderer regression: perspective-correct texture sampling, changing artwork
  bounds, near clipping and drawings behind the camera.
- Existing native `amcamera_test`: live target edits, parent compensation,
  offset capture, planar aiming, dependency rejection, snapshot restoration,
  save/reopen, column remapping, missing/restored targets, real raster-column
  exposure changes, production render-tree expansion and recursive FX cloning.
  This native test runs in the existing Windows build workflow.

Manual acceptance: attach an asymmetric drawing to both A:M and GLB columns;
rotate each on all axes; scrub drawing exposures; test column movement, Undo/
Redo and scene reopen; compare Preview with final rendering. Also check vector
and Toonz Raster inputs and normal editing/picking while a constraint is active.
