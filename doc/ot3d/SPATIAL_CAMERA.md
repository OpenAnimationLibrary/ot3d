# Spatial camera controls

This builds on the native model-level work in PR #11. Cameras expose the same
keyed X/Y/Z rotation, Z scale and Z pivot channels as native A:M model columns.
Select **Camera** in the Animate Tool object selector, then open **3D Rotation**.
Position X/Y/Z and the existing Z rotation, XY scale and pivot controls remain
available. X/Y rotation, Z scale and Z pivot also appear in the Function Editor,
participate in Global Key and key deletion/restoration, and survive scene saves.
The standard canvas rotation gesture is still Z-only, not a three-axis gizmo.

## Scene camera versus FX camera

The scene camera is a stage object. Its evaluated optical pose now drives native
A:M model projection in the hybrid Viewer and Preview/final rendering. Pitch and
yaw turn the view about the camera's eye, not about the model origin. Small angles
are useful for a first test; turning the camera away can move a small model fully
out of view. Preview and render cameras remain independently selectable. Spatial parenting
moves the camera eye with its parent, including when rigged to a model column.

**3D Transform > Target** has two values:

- **Geometry** is the existing default and transforms the input model.
- **Camera** adds a local XYZ translation/rotation/scale to that input's camera
  pose before projection. The renderer uses the inverse of the resulting pose.
  It does not change the scene's camera keys, mutate the input model, or affect
  another FX branch. This mode works with native model levels, GLB Model and
  Image Plane. It is not a new raster-valued Camera node in the FX schematic.

Connect **Model -> 3D Transform (Camera) -> Three Point Lighting**, and enable
Preview to see the FX. Geometry-target and Camera-target transforms may be mixed
in a chain. Geometry transforms are applied in source-to-output order; camera
local deltas are composed in source-to-output order after the source camera pose.
The camera transform is inverted only after composition, with clipping in camera
space before perspective division. Native A:M preview lighting keeps its existing
world-space directions; moving the camera changes the eye/facing, not the rig.
GLB keeps its existing camera-relative lighting convention.

In Camera mode the model-origin canvas gizmo is deliberately hidden: it does not
represent the optical eye. Use the numeric, keyable FX controls. Existing saved
3D Transform nodes have no Target value and retain Geometry behavior. Target is
a non-animated selector; its position/rotation/scale parameters remain animated.

## Units and compatibility

The native scene camera retains the historical 1000-stage-unit focal offset;
its Z channel remains a displacement relative to that offset. Existing camera
uniform scale continues to affect XY framing, not optical Z scale. The new Z
scale is separate. With X/Y rotation and Z pivot at zero and Z scale at one, the
native model path preserves prior pan, roll, scale, shear, parenting and Z framing.
The existing downstream camera affine is compensated so it is not applied twice.
Camera poses and the output mapping are included in immutable render snapshots
and cache identities. Singular/non-finite camera poses fail explicitly.

FX translations retain the source's units: stage units for native A:M columns,
and the existing model units for GLB/Image Plane. Camera scale changes the view
coordinates; it is not a physical focal-length control. GLB's declared
perspective/orthographic mode and clipping settings remain in force. Image Plane
retains its orthographic projection and existing whole-plane near-clip behavior.

## Current boundary

The new scene-camera pitch/yaw is consumed by the native A:M spatial path.
Ordinary 2D levels still use the original planar camera path. GLB and Image Plane
use their own source cameras in the FX pipeline; their camera movement is exposed
through the Camera target, not yet bound to the shared stage camera. This does
not claim unified 2D/3D camera projection, shared depth, a rotatable camera-frustum
overlay, orbit navigation, new lens controls, or a three-axis camera gizmo.

Validation includes portable matrix inversion/projection and GLB camera tests;
`amcamera` exercises scene/FX camera equivalence, legacy framing, parenting,
independent preview cameras, cache invalidation, immutable snapshots, camera keys,
persistence and the public render-tree compute path. Interactive acceptance is
still required in a new platform build.
