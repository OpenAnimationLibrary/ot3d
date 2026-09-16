# OT3D Mission

## Mission

OT3D exists to make hybrid 2D/3D production feel native to the OpenToonz workflow.
Artists should be able to place 2D drawings, multiplane elements, cameras, lights, and
externally authored 3D assets in one virtual scene; inspect the result interactively;
and make staging decisions without waiting for a Preview render.

Complete hybrid 2D/3D production is the goal. Complete native 3D authoring is not.

## Product promise

The interactive Viewer should become a trustworthy representation of scene space:

- the same scene time drives visible 2D and supported 3D animation;
- the camera, transforms, scale, and depth relationships are understandable;
- 2D planes and 3D geometry occlude one another predictably;
- changes needed for layout and animation review appear without a Preview render;
- unsupported data is reported clearly instead of failing silently; and
- final rendering remains available for effects and quality that are intentionally
  outside the interactive path.

“Without Preview rendering” is an edit-time responsiveness requirement, not a promise
that the Viewer immediately matches every final-render effect.

## Workflow model

OT3D is the staging, timing, drawing, and compositing center of the workflow. Dedicated
3D applications remain the authoring center for meshes, armatures, materials, and
complex 3D animation.

The first supported exchange boundary is GLB, the binary form of glTF 2.0:

- Blender or Animation:Master source projects remain authoritative.
- Artists export a GLB for OT3D to consume.
- OT3D reads the GLB and stores scene-level placement and playback choices.
- Edits to 3D source content return to the DCC and arrive through a new export/reload.
- OT3D does not write back into GLB or native DCC project formats in the initial plan.

This boundary is intentionally simple. It permits useful 3D integration without tying
OT3D's scene format or release schedule to a DCC's private file format.

## Principles

### Preserve the 2D workflow

Drawing, cleanup, xsheet/timeline work, palettes, levels, and existing scene behavior
must remain first-class. A 2D-only scene should not pay a material compatibility or
performance cost for unused 3D support.

### Establish spatial truth before visual polish

Camera agreement, transform agreement, depth ordering, stable playback, and useful
diagnostics come before physically based materials or broad renderer parity. A plain
mesh in the correct place is more valuable early than a beautiful mesh in an
unreliable space.

### Use one scene model, with explicit boundaries

Viewer and final-render paths should share transform and time evaluation wherever
practical. Where they differ, the difference must be documented and visible to the
artist. Adapters should isolate GLB parsing and DCC-specific conventions from the core
scene model.

### Make migration auditable

Every imported change records its source repository and commit. OT3D preserves
OpenToonz authorship, license notices, and relevant test history. Unrelated experiments
from staging repositories are not swept into the downstream baseline.

### Earn compatibility with fixtures

Claims about Blender, Animation:Master, platforms, materials, animation, and scene
round-tripping require reproducible assets and documented expected results. A graceful
warning is preferable to an accidental partial interpretation.

### Keep upstream collaboration practical

OT3D may integrate changes more quickly than OpenToonz, but generally useful fixes
should remain small enough to review and contribute upstream. OT3D must not imply that
its roadmap, builds, or support obligations belong to the OpenToonz maintainers.

## Success criteria

The initial mission is fulfilled when an artist can open a representative project,
load a conforming GLB exported from either supported DCC workflow, and interactively:

1. see 2D and 3D elements from the scene camera and a navigable perspective view;
2. verify position, scale, orientation, and mutual occlusion;
3. scrub and play supported animation at the intended scene time;
4. select and stage imported assets with predictable controls;
5. reload a revised export without rebuilding the scene; and
6. understand every material or animation feature that is omitted from the Viewer.

Final-render correctness, data preservation, and 2D-only compatibility remain release
gates throughout that work.

## Independence statement

OT3D is an independent downstream project maintained by the Open Animation Library
community. It is not an official distribution of OpenToonz and is not endorsed by or
affiliated with DWANGO, Digital Video, Studio Ghibli, the Blender Foundation, or Hash,
Inc.
