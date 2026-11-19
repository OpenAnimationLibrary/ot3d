# OT3D

OT3D is an experimental downstream of [OpenToonz](https://github.com/opentoonz/opentoonz)
for productions that combine 2D artwork and externally authored 3D assets in one
interactive virtual space.

The project is working toward a Viewer in which artists can stage, inspect, and play
mixed 2D/3D scenes without first running a Preview render. Final rendering remains a
separate quality and delivery step.

> **Project status:** early development. OT3D is not yet a production-ready replacement
> for OpenToonz. Scenes and preferences should be backed up before testing development
> builds.

## Why OT3D exists

OpenToonz already provides a 3D stage for arranging 2D columns, but its interactive
views do not yet present a complete mixed 2D/3D workspace. OT3D is the integration
branch for closing that gap while keeping OpenToonz's drawing, xsheet, and compositing
workflow at the center.

The intended workflow is:

1. Draw and animate 2D material in OT3D.
2. Model, rig, and animate 3D material in a dedicated DCC such as
   [Blender](https://www.blender.org/) or
   [Animation:Master](https://www.hash.com/animation-master-features).
3. Exchange 3D assets through GLB (binary glTF 2.0), or load native
   Animation:Master `.mdl` models.
4. Stage and review both media together in OT3D's interactive Viewer.
5. Use the final renderer when final-quality output is required.

GLB is a read-only interchange boundary in the initial architecture. The external DCC
project remains the source of truth; OT3D does not write changes back to `.blend`,
Animation:Master project files, or GLB assets.

## Current experimental capabilities

The current development line includes:

- native GLB and Animation:Master model levels, hybrid Viewer staging, spatial
  transforms, and supported GLB animation playback;
- A:M group palettes, control point editing and Actions, native bone animation,
  3D Plastic posing, and Distortion Cage deformation;
- model materials, Ink Line and Toon Line, Point Light and Three-Point Light,
  environment reflections, image based lighting, and surface smoothing;
- Live Render controls and GPU environment reflection previews; and
- retained EXR sources, multipart/channel selection, and deep-image transport.

These are experimental features with individual limits. See the feature
documents below for supported inputs and workflows. The roadmap describes the
remaining mixed-media camera/depth, fidelity, performance, and exchange work;
the presence of a feature does not establish completion of a roadmap milestone.

Arbitrary 3D rotation of 2D planes is deliberately deferred. During the early
milestones, 2D columns retain their current planar orientation controls and Z placement;
pitch/yaw that tilts a plane through depth (Rx/Ry) is planned for a later milestone.

See the [roadmap](doc/ot3d/ROADMAP.md) for ordered milestones and explicit non-goals.

## Project documents

- [Mission and principles](doc/ot3d/MISSION.md)
- [Development roadmap](doc/ot3d/ROADMAP.md)
- [External asset compatibility contract](doc/ot3d/COMPATIBILITY.md)
- [Release policy](doc/ot3d/RELEASES.md)
- [OpenToonz relationship and upstream policy](doc/ot3d/UPSTREAM.md)
- [Initial 3D source migration record](doc/ot3d/THREE_D_MIGRATION.md)
- [Native A:M models, editing, and animation](doc/ot3d/AM_MODEL_LEVEL.md)
- [GLB Model and 3D FX workflows](doc/glb_model_fx.md)
- [A:M Material node](doc/ot3d/AM_MATERIAL_NODE.md)
- [Point Light](doc/point_light_fx.md)
- [Live Render](doc/ot3d/LIVE_RENDER.md)
- [EXR and deep-image foundations](doc/ot3d/EXR_DOCUMENT_AND_DEEP.md)

## Builds and releases

Tagged OT3D alpha builds are available from this repository's
[Releases](https://github.com/OpenAnimationLibrary/ot3d/releases). Download the
portable Windows x64 package for the selected release. Its `portablestuff` folder
keeps test preferences separate from an installed OpenToonz configuration.

Untagged CI artifacts are development snapshots and can contain changes newer
than the latest alpha. Read the release notes for the features included in a
particular tagged build.

To build from source, use the inherited OpenToonz instructions. Automatic OT3D
application checks currently target Windows; macOS and Linux instructions do
not imply current CI verification:

- [Windows](doc/how_to_build_win.md)
- [macOS](doc/how_to_build_macosx.md)
- [Linux](doc/how_to_build_linux.md)
- [BSD](doc/how_to_build_bsd.md)

## Relationship to other projects

OT3D derives from OpenToonz, which is based on Toonz Studio Ghibli Version, originally
developed by Digital Video and customized by Studio Ghibli. OT3D preserves the upstream
source history, authorship, licenses, and third-party notices associated with that work.

OT3D is maintained independently by the Open Animation Library community. It is not an
official OpenToonz distribution and is not endorsed by or affiliated with DWANGO,
Digital Video, Studio Ghibli, the Blender Foundation, or Hash, Inc. “OpenToonz,”
“Blender,” and “Animation:Master” identify their respective projects and products.

General fixes that do not depend on OT3D should be kept suitable for contribution to
OpenToonz. Integrated or experimental 3D work may remain downstream until it can be
reviewed as a focused, independently useful change.

## Contributing

Use focused pull requests with an observable acceptance case. A Viewer change should
state what is visible before and after it, which scene/asset reproduces the result, and
whether final rendering changes. New asset behavior should include a redistributable
fixture or a generator for one when licensing permits.

Please preserve upstream attribution and third-party notices. See the
[upstream policy](doc/ot3d/UPSTREAM.md) before moving code between repositories.

## Licensing

OT3D retains OpenToonz's licensing and attribution. Files outside `thirdparty` and
`stuff/library/mypaint brushes` are generally covered by the
[Modified BSD License](LICENSE.txt). Third-party directories and bundled brush assets
carry their own notices; consult the license files beside those materials.

This repository's downstream name and documentation do not alter the licenses of
OpenToonz or any third-party component.
