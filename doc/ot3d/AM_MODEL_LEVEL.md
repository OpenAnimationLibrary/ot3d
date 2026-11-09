# Native A:M model level — experimental foundation

## User workflow

Load an Animation:Master `.mdl` through **File > Load Level** or drop it into
an Xsheet. It becomes an **A:M model level**, also listed in Scene Cast. It is
not a GLB conversion, Plastic mesh, drawing level, or user-created Model FX.
Scene Cast **Expose** creates another instance sharing the model level.
A model has one exposure identifier (`1`). Extend that cell to keep the
model visible while an attached action plays.

The Load Level dialog recognizes models as a single exposure and disables
image-subsequence, DPI and raster-caching controls for them. Previous image
sequence settings do not affect model loading. The File Browser's **Load**
context action and local-file drag-and-drop use the same native loader; no
separate Import A:M command or plugin is needed. Numeric filename suffixes and
Unicode paths are retained. Loading still follows the Auto Expose preference:
when it is off, use Scene Cast **Expose** to place the loaded model in the Xsheet.
Missing or unsupported models report an error rather than creating empty levels.

Select its column and choose the **Animate Tool**. Position uses existing X/Y/Z.
Rotation adds **Rx/Ry**, with existing Angle presented as **Rz**. Scale adds **Z**
to X/Y and uniform Scale. Center adds **Z** to the existing X/Y center. Numeric
fields and their draggable labels use the normal stage-channel undo machinery.
Global Key includes the new channels for model columns. Function Editor curves
are `AM Rotation X`, `AM Rotation Y`, `AM Scale Z`, and `AM Pivot Z`.

The extra controls are hidden for ordinary drawing levels, cameras and pegbars.
Their existing planar placement remains unchanged. New 3D axis/ring gizmos,
world-space editing and model picking are NOT implemented; select via Xsheet.
The existing planar handles continue to edit XY/Z and Rz. Do not interpret their
2D outlines as the full 3D bounding box.

### Control Point Editor

Select an exposed A:M cell in the top Xsheet, turn off Preview and choose
**Control Point Editor**. The Viewer shows the model's splines and control points.
Drag a point to move its attachment stack in the current view plane. **Shift-click**
toggles a point in the selection. Drag from empty space to box-select points, or
hold **Shift** to add the box to the selection. Drag any selected point to move the
whole selection by the same model-space offset. Orbit the Camera 3D view to edit
along a different plane.

With one point selected, drag the amber incoming or purple outgoing handle to
change that spline occurrence's bias. Hold **Shift** while dragging a handle to
change its length along its current direction. The same Shift gesture works on
vector strokes and motion paths.

At a junction, **Alt-click** the selected point or choose **Next Attached A:M
Spline** from the Viewer context menu to show the next occurrence's handles.
**Escape** cancels an active drag. A finished drag can be undone or redone.

Save the OT3D scene to retain point and bias edits along with exposures and
instance keys. All exposures of the same level share the edited geometry, and
each column keeps its own transforms. Edits live in the scene and the source
`.mdl` stays intact. If the source bytes change outside OT3D, OT3D displays
the updated source and suspends the edits until the original revision is restored.

Save As recodes the same physical source. Collect Assets copies the model
source without conversion; linked material/image assets
are not recursively collected yet. Reload through the normal level-loading path
can be used after external edits; no file watcher or live synchronization exists.
An unreadable/unsupported source is a diagnostic, not a successfully loaded empty
model. Failed explicit reload drops the previous snapshot rather than hiding the
failure with stale geometry. Keep original sources and work on scene copies.

### Animate control points with an Action

1. Right-click an exposed A:M cell in the top Xsheet or Timeline and choose
   **Action > Create Action…**. Pick a location for the reusable `.act` file.
   **Load Action…** attaches an existing compact muscle action. The clicked
   scene frame is time zero for the action. The same menu is available in the
   Viewer for the selected model.
2. Select **Control Point Editor** with Preview off. Move to a frame at or
   after the action's start and drag a selected control point. The drag adds
   or updates a position key for every selected point at the current frame.
   One undo reverses the whole drag. The editor highlights keyed points in
   pink. Right-click in the Viewer to set or remove position keys for the
   selected points on that frame.
3. Scrub or play the scene. Motion is linear between keys and holds at the
   end. The model retains its base pose before the action starts. The action
   follows scene time, so extend the model's exposure over the frames you
   want to see. In a 24 fps scene, an action starting at frame 1 reaches its
   one-second key at frame 25.
4. Save the scene to retain the attachment, start frame and working copy of
   the animation. Use **Action > Save Action…** to update the external file
   or save a reusable copy. The scene can reopen its working copy if the
   external file has moved. **Set Start to This Frame** repositions the
   action on the scene timeline. **Remove Action** detaches it from that instance.

An action belongs to the model exposure's column. A second column can use the
same model with its own action and placement. Action keys are model-space
position offsets from the level's edited base shape. A welded point has one
position track. A newly animated point keyed after the start gets a neutral
key at time zero. Bias editing is available on instances without an attached
action. Moving individual exposure cells keeps the action's start on its
existing scene frame. Exported keys carry the motion offsets and use the
recipient model's base shape on load.

The first interchange scope is one text A:M `.act` with compact muscle
position channels. OT3D checks the control-point IDs and their spline
references against the destination model. Native action time uses 135,000
ticks per second and retains fractional frame timing across frame rates.
Mixed bone, bias, stride and other unsupported action data is rejected during
load. Reopening and exporting this subset have automated coverage. Visual
comparison in A:M remains part of interchange validation.

## Ownership and implementation

`TXshAMLevel` / `AM_XSHLEVEL` is a distinct `TXshLevel` subclass. The Scene Cast
resource retains its original document bytes, parsed source ranges, CP occurrences,
attachment graph, splines, explicit patches and basic group colors. Unsupported
sections remain in the source document for later typed readers. There is no
promise of native A:M round-trip writing.

A model uses an ordinary level column with a compatibility guard: model cells
cannot be mixed with 2D cells in one column. Loading into an incompatible column
inserts a new column. Two columns share the native level while their
`TStageObject`s own independent transforms. The new channel identifiers are
appended, not inserted among old identifiers. Old scenes default to Rx/Ry/PivotZ
zero and ScaleZ one; no new saved tags are emitted for default ordinary levels.
The level capability is restored from exposed cells when a scene is loaded.

The standalone `otam` library owns parsing, approximate preview tessellation,
4x4 math, near clipping and an opaque raster reference. It has no dependency on
Hash SDK import libraries, A:M, MFC, Qt, Python, a plugin, or a GPU context.
The SDK informs semantics but is not linked or bundled.

Viewer and final FX-tree construction request owned snapshots from
`prepareAMLevelGeometry`. The internal hidden `amModelSnapshot` raster adapter
is an output adapter, not the identity or storage of the model level. Parse and
preview tessellation happen at load/reload; instance/camera evaluation operates
on those retained snapshots. Geometry is not serialized to an intermediate GLB.

## Spatial and time contract

Current calibration is explicit and **experimental**: source units are interpreted
as centimeters, Y up, +Z toward the existing OT camera. Model coordinates convert
by `Stage::inch / 2.54`. Existing stage XYZ channel units are preserved.
Column-local evaluation is translation, Rz*Ry*Rx, existing XY shear, uniform and
per-axis scale, then negative pivot (column-vector convention); ancestor matrices
compose before it. Euler channels retain the existing scalar interpolation and
multi-turn keys. Quaternion interpolation is not claimed.

Only model column placement consumes Rx/Ry/ScaleZ/PivotZ. Existing 2D ancestors
contribute their existing local affine and depth; 3D-enabled model ancestors can
contribute full spatial placement to other model instances. Ordinary 2D children
still use their legacy planar path. Legacy 2D IK on a model column is rejected.
Spatial Sub-Xsheet flattening/edit-in-place and every camera/no-scale variant
remain validation gates; the initial interactive model pass visits top-level
columns only. The attached muscle action subset is evaluated independently of
embedded A:M rigs, actions, relationships, cameras and choreographies. The
native model reader retains those embedded records as source data.

Final projection clips triangle portions at the camera near plane. Viewport and
final-render paths share the snapshot evaluator, but existing hybrid Viewer
limitations remain: no shared opaque depth with 2D drawings, no full FX DAG
propagation, and no cross-column final-render 3D occlusion. Separate model columns
are composited through the existing column/FX system. Transparent materials,
textures, lighting, decals, motion-blur/temporal FX parity and picking are deferred.

## Developer's notebook — evidence and fidelity

**Confirmed by examined 19.5 fixtures:** position-bearing CP and attachment records
have distinct source IDs; equal XYZ does not imply attachment. Incoming/outgoing
bias fields must remain separate. Both short four-corner patch records and richer
four-corner records with trailing metadata exist. Rich metadata is preserved,
not mistaken for additional CP identifiers. Model files use a mixed text/tag
container; FileInfoPos and unsupported sections are retained verbatim in memory.

**SDK reference:** `OpenAnimationLibrary/amsdk`, current 19.5 `HCP`, `HPatch`,
`HModel`, `HAction` and time headers. Historical documentation in `docs` identifies
itself as v12 (2005), and its tangent explanation explicitly predates later changes.
`GetPointOnPatch` and `BuildExportModel` are host APIs, not a portable evaluator
implementation supplied by the SDK. No SDK source is copied into this change.

**Inferred/experimental:** loop flags observed in fixtures, axis/unit calibration,
neighbor-based tangents, magnitude application and Coons surface interpolation.
The preview is NOT the proprietary Hash patch evaluator. Special CP flags are retained with limited evaluation. Warnings identify this.
Four-corner patches use a Coons approximation and curved three-corner patches
use a cubic triangular surface. Shared boundaries use the same cubic splines
drawn by the editor. Gamma rotates in the local spline plane, alpha tilts out of
it and magnitude scales the tangent. The plane and tangent are approximations
informed by the SDK's
[HCP documentation](https://github.com/OpenAnimationLibrary/amsdk/blob/ee3dd9fb82bf80de8cdd75e00273e2eeab925e33/docs/classHCP__02.md).
Coincident but unattached points remain separate.

**Unknown/deferred:** exact modern tangent rules, interior subdivision heuristic,
peaked/special CP flags, hooks, five-point evaluation, complete patch orientation
semantics and topology-driven patch discovery. Only explicit supported patches
are previewed. Models without usable explicit patches are rejected with a clear
message. Complex models can be partial and are not a compliance claim. Do not use
a plausible cube screenshot as proof that arbitrary curved A:M models match.

## Validation and acceptance

`ammodel_test` uses generated synthetic fixtures for document retention, CP identity,
forward references, cycle/duplicate/missing-reference rejection, rich patch layout,
limits, transforms, clipping and rasterization. No user's original model or SDK is
redistributed. `amlevel_test` exercises production native classes: load/exposure,
shared model with independent stage keys, global keys, key removal/restoration,
scene persistence, 2D isolation, final raster output, Save As and missing files.
It also checks action binding, fractional poses, per-instance isolation and
reopening the embedded action with its external file unavailable. The portable
action test covers file parsing, offsets, timing and export round-trips. Passing these tests does not validate exact
A:M evaluation or native visual interaction.

Manual release gate: use an asymmetric A:M-authored fixture; load/expose, hold its
cell, select Animate, key XYZ/Rx/Ry/Rz/scale/pivot, scrub and reopen. Repeat with a
second instance and a parent pegbar. Compare camera Viewer/Preview/final output,
undo/redo and Function Editor, then switch to vector/raster/Toonz Raster levels
and verify unchanged controls/rendering. Record A:M version, GPU, driver and
scaling (100%/150%). Hooks/five-point and rigged files must show explicit limits.

This is an experimental native model level with an approximate surface preview.
Older OpenToonz builds do not understand
`amModelLevel` or the new transform tags; backward round-trip is unsupported.

## Native model FX and accessible XYZ rotation

For an A:M model column, the Animate Tool offers a **3D Rotation** button next
to the object selector. Its popup exposes X, Y and Z in degrees using the
existing keyed/undoable channel fields. The ordinary canvas rotation gesture
still edits Z; this does not add a three-axis canvas manipulator. Ordinary 2D
columns do not show the model-only control.

Connect the native model column directly to **3D Transform** or **Three Point
Lighting**, or use **Model -> 3D Transform -> Three Point Lighting**. Chain
lighting nodes to combine rigs and view the result in Preview or a render.
Under **Placement**, choose **Positional** and key the XYZ fields to move the
whole rig in stage coordinates. Radius sets how far the key, fill and rim sit
from the rig center along their respective azimuth and elevation. Positional
lights fall off with distance. Directional remains the default for saved scenes.
Unsupported ordinary drawing connections are rejected before modifying the
connection or undo history.
The optional connection policy does not change the existing TFxPort vtable.

The render tree retains an immutable world-space preview mesh until downstream
3D transforms and lighting have been applied, then projects through the native
camera mapping. Column tint and opacity remain available without hiding the 3D
source behind a raster-only wrapper. FX translation values operate in stage
coordinates; column transforms retain their existing units. Rendering uses the
same bounded tile rasterizer as the other 3D sources. No source .mdl data is
rewritten. Lighting here is simple two-sided diffuse shading of the experimental
preview patches, not A:M materials, normals, decals or faithful surface shading.

The native `amfx` regression covers both direct connections, a transform/light
chain, out-of-plane X/Y rotation, unsupported connection preservation, pixels,
lighting, clone/opacity behavior, render-tree construction, cancellation and
empty exposures. Interactive acceptance is still required in a platform build.
