# OT3D Development Roadmap

## How to read this roadmap

The roadmap is ordered by dependency, not by visual impact. Each milestone must leave a
testable vertical slice and must keep ordinary 2D scenes usable. Dates will be assigned
only when a maintainer and test capacity are available; release quality is the gate.

The imported GLB/FX work is a valuable data and rendering seed, but it is not yet the
interactive mixed-media Viewer described by the OT3D mission.

## Foundation phase

### F0 — Downstream foundation

**Purpose:** make OT3D an auditable, releasable downstream before adding more behavior.

Deliverables:

- establish the official OpenToonz baseline and preserve the original OT3D bootstrap;
- migrate only the reviewed 3D commit set, with source commit references;
- publish the OT3D mission, compatibility boundary, roadmap, and release policy;
- give development builds an unmistakable experimental identity;
- confirm a clean configure/build path and run inherited plus migrated tests; and
- define a small, redistributable 2D/3D compatibility fixture set.

Exit gate:

- the exact upstream and migration inputs can be reconstructed from repository records;
- no unrelated OT-Dev experiments are present; and
- a tester can distinguish an OT3D build from an official OpenToonz build.

### M0 — Preserve and verify the 3D seed

**Purpose:** turn the migrated work into a dependable base for Viewer development.

The seed includes the read-only GLB loader and data model, material-color recovery,
animation data and pose evaluation, the GLB Model FX CPU rendering path, 3D FX menu
grouping, the Three-Point Light FX, and Animation:Master bridge documentation.

Deliverables:

- fixtures for valid, invalid, static, animated, skinned, and multi-material GLB data;
- bounded parsing with clear errors for unsupported or malformed assets;
- deterministic scene save/load for migrated FX parameters;
- transform and animation evaluator tests independent of the GUI; and
- an explicit list of what the seed parses versus what it actually displays.

Exit gate: the seed builds and its automated tests pass on the primary development
platform, with malformed assets failing safely.

## Interactive Viewer phase

### M1 — Viewer substrate and spatial contract

**Purpose:** establish one trustworthy path from scene data to interactive 3D drawing.

Deliverables:

- document the current Viewer, stage, camera, and final-render transform paths;
- define the OT3D stage-to-GLB coordinate, unit, camera, and frame-time conversions;
- isolate 3D Viewer drawing behind a narrow internal boundary and a development toggle;
- add depth-buffer and graphics-state ownership rules that do not leak into 2D tools;
- add a diagnostic grid, axes, bounds, camera frustum, and transform readout; and
- produce a headless transform reference test where possible.

Exit gate: the same fixture produces numerically agreed world, view, and clip transforms
in the diagnostic path and the existing final-render reference path. No visible 3D mesh
is required yet.

### M2 — Static GLB geometry in the Viewer

**Purpose:** display the smallest useful 3D asset directly in both camera and navigable
perspective views.

Deliverables:

- upload and draw supported static triangle meshes from a read-only GLB;
- support node hierarchy transforms and deterministic bounds;
- provide a wireframe and a simple unlit/base-color mode;
- handle near/far clipping, resize, device-pixel ratio, and graphics-context loss; and
- report unsupported primitives/material properties without crashing.

Exit gate: a static fixture appears at the documented scale and orientation in camera
and perspective views, survives scene reopen, and never requires Preview rendering.

### M3 — Hybrid camera and depth

**Purpose:** make 2D and 3D placement readable as one scene.

Deliverables:

- use one evaluated scene camera for supported 2D planes and 3D geometry;
- establish deterministic opaque depth testing and clipping between those elements;
- preserve onion skin, selection overlays, safe-area guides, and other essential 2D
  Viewer overlays;
- define the first transparent-2D policy and clearly mark cases not yet exact; and
- provide front/intersection/behind fixtures for occlusion regression testing.

Exit gate: moving an opaque GLB object through a 2D plane produces the expected front,
intersection, and behind result during navigation and playback, without a Preview.

This is the target capability for `0.1.0-alpha`.

### M4 — Time, animation, and reload

**Purpose:** make externally animated assets dependable during scene playback.

Deliverables:

- bind scene frame/time to a selected embedded GLB animation;
- apply supported node and skin poses through the shared evaluator;
- define clip selection, offset, speed, loop, hold, and out-of-range behavior;
- reload a changed export without losing OT3D placement and timing choices;
- invalidate GPU and CPU caches safely; and
- keep paused, scrubbed, stepped, and playing results consistent.

Exit gate: the reference animated fixtures remain synchronized with 2D keys while
scrubbing and real-time playback, and a compatible re-export can be reloaded in place.

This is the target capability for `0.2.0-alpha`.

### M5 — Selection and staging tools

**Purpose:** let artists work with 3D assets instead of merely viewing them.

Deliverables:

- object picking with an unambiguous selected-object overlay;
- numeric and gizmo-based translation, scale, and supported rotation for 3D assets;
- predictable camera navigation that does not steal existing drawing-tool gestures;
- undo/redo and scene save/load for all new staging state;
- missing-file, stale-file, unsupported-feature, and performance diagnostics; and
- a Viewer quality switch whose modes and limitations are visible to the artist.

Exit gate: a tester can load, select, place, time, save, reopen, and revise a reference
asset without losing data or entering Preview.

This is the target capability for `0.3.0-beta`.

## Expansion phase

### M6 — Full spatial transforms for 2D planes

**Purpose:** remove the early orientation constraint after the shared spatial model and
interaction design have proved stable.

Deliverables:

- pitch and yaw (Rx/Ry) for 2D image planes, in addition to existing planar rotation;
- clear local/world axes and pivot behavior;
- consistent parenting, camera, column, and pegbar evaluation;
- 3D-aware handles that preserve access to drawing and plastic tools; and
- migration behavior for scenes created before the new transform fields exist.

Exit gate: a 2D plane can be tilted through depth, animated, parented, saved, reopened,
and rendered consistently without changing an unmodified legacy scene.

**Explicit deferral:** arbitrary depth-plane rotation of 2D columns is not part of
F0–M5. Until M6, their supported orientation remains the existing planar model plus Z
placement. This is a planned limitation, not an implicit promise in early releases.

### M7 — Viewer fidelity and performance

**Purpose:** improve visual confidence only after spatial behavior is trustworthy.

Candidate deliverables, promoted individually when fixtures and performance budgets
exist:

- texture sampling, alpha handling, color-space rules, and material diagnostics;
- a simple dependable lighting mode shared with supported scene lights;
- batched drawing, resource lifetime management, cache budgets, and large-scene tests;
- documented Viewer-versus-final-render differences; and
- evaluation of a bounded glTF PBR subset.

Exit gate: agreed production fixtures meet interactive performance targets on declared
reference hardware, and visual omissions are enumerated in the UI and release notes.

The previously proposed OT-Dev PBR work is not part of the initial OT3D seed and is not
a dependency for M1–M6. It must be reviewed separately against the Viewer architecture.

M6 and a stable subset of M7 form the target for `0.4.0-beta`.

### M8 — Exchange hardening and stable workflow

**Purpose:** make the external-DCC boundary repeatable enough for a supported workflow.

Deliverables:

- tested Blender and Animation:Master export profiles and sample projects;
- compatibility fixtures tied to specific exporter/application versions;
- project-relative asset collection and missing-asset relink tools;
- optional watched-file reload after explicit safety and performance review;
- upgrade and rollback documentation; and
- a supported-feature matrix for every release platform.

Exit gate: documented projects can move through authoring, export, staging, reopen,
re-export, and final output on supported platforms without silent data loss.

A stable, compatibility-frozen subset of the mission—not every candidate feature—is the
target for `1.0.0`.

## Constraints deliberately left in place early

Through M5, the project does **not** plan to provide:

- arbitrary pitch/yaw (Rx/Ry) of 2D planes through Z depth;
- native mesh modeling, sculpting, UV editing, rig creation, or weight painting;
- native reading or writing of `.blend`, `.mdl`, `.cho`, or other DCC project formats;
- GLB modification, write-back, or bidirectional live synchronization;
- full glTF PBR, every material extension, every animation feature, or every primitive;
- exact interactive parity with every OpenToonz raster/vector FX or final-render result;
- a replacement final renderer or a rewrite of the OpenToonz scene format;
- a stable third-party plug-in ABI for the new Viewer internals; or
- a production support promise for every platform produced by inherited build scripts.

These limits keep the first milestones focused on spatial correctness, safe data
handling, and a useful no-Preview feedback loop. Moving an item earlier requires a
written dependency and test plan, not only an implementation prototype.

## Release sequence

| Development gate | Intended release channel | User-visible proof |
| --- | --- | --- |
| F0–M2 | CI/developer snapshots | Auditable baseline; static GLB visible in Viewer |
| M3 | `0.1.0-alpha` | 2D/3D camera and opaque depth without Preview |
| M4 | `0.2.0-alpha` | Animated GLB playback and safe reload |
| M5 | `0.3.0-beta` | Selectable, stageable mixed scene workflow |
| M6 + stable M7 subset | `0.4.0-beta` | Tilted 2D planes and bounded fidelity/performance |
| M8 compatibility subset | `1.0.0` | Documented, reproducible external-DCC workflow |

Version numbers express compatibility maturity, not a deadline. A milestone may be
split into smaller pre-releases when doing so produces safer review and testing.

## Definition of done for every milestone

A milestone is not complete until it has:

- a redistributable fixture and an observable acceptance case;
- automated tests for non-visual logic and targeted visual/manual checks where needed;
- legacy 2D scene open/save and core Viewer regression coverage;
- bounded failure behavior for absent, malformed, or unsupported external data;
- user-facing documentation and release-note limitations; and
- platform results for every platform claimed by that release.
