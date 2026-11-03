# Native A:M model level — experimental foundation

## User workflow

Load an Animation:Master `.mdl` through **File > Load Level** or drop it into
an Xsheet. It becomes an **A:M model level**, also listed in Scene Cast. It is
not a GLB conversion, Plastic mesh, drawing level, or user-created Model FX.
Scene Cast **Expose** creates another instance sharing the read-only asset.
A model has one exposure identifier (`1`); extend that cell to keep it visible.
Internal A:M action playback is not implemented in this slice.

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

Save the OT3D scene to retain references, exposures and instance keys. `.mdl`
bytes are never rewritten. Save As recodes the same physical source. Collect
Assets copies the model source without conversion; linked material/image assets
are not recursively collected yet. Reload through the normal level-loading path
can be used after external edits; no file watcher or live synchronization exists.
An unreadable/unsupported source is a diagnostic, not a successfully loaded empty
model. Failed explicit reload drops the previous snapshot rather than hiding the
failure with stale geometry. Keep original sources and work on scene copies.

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
columns only. Imported A:M rigs, CP animation, actions, relationships, cameras and
choreographies are preserved as source records, NOT evaluated.

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
The preview is NOT the proprietary Hash patch evaluator. Alpha/gamma and special
CP flags are retained but their effect is not evaluated. Warnings identify this.
Four-corner patches use a Hermite-boundary Coons approximation; three-corner
records use a flat triangular preview. Shared boundaries reuse topology-based
curves; coincident but unattached points are not welded.

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
Linux native builds run the latter; portable core checks run on all three platforms
with a separate Linux ASan/UBSan job. Passing these tests does not validate exact
A:M evaluation or native visual interaction.

Manual release gate: use an asymmetric A:M-authored fixture; load/expose, hold its
cell, select Animate, key XYZ/Rx/Ry/Rz/scale/pivot, scrub and reopen. Repeat with a
second instance and a parent pegbar. Compare camera Viewer/Preview/final output,
undo/redo and Function Editor, then switch to vector/raster/Toonz Raster levels
and verify unchanged controls/rendering. Record A:M version, GPU, driver and
scaling (100%/150%). Hooks/five-point and rigged files must show explicit limits.

This remains a **draft experimental feature**, not full native project compliance
or a production-ready surface evaluator. Older OpenToonz builds do not understand
`amModelLevel` or the new transform tags; backward round-trip is unsupported.
