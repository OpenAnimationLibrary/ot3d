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
3. Exchange 3D assets through GLB (binary glTF 2.0).
4. Stage and review both media together in OT3D's interactive Viewer.
5. Use the final renderer when final-quality output is required.

GLB is a read-only interchange boundary in the initial architecture. The external DCC
project remains the source of truth; OT3D does not write changes back to `.blend`,
Animation:Master project files, or GLB assets.

## Initial scope

Essential Viewer work comes before broad 3D feature coverage:

- establish a deterministic OpenToonz and migrated-3D source baseline;
- draw static GLB geometry in the interactive Viewer;
- make 2D planes and 3D geometry share a camera and depth model;
- play supported GLB animation at scene time;
- add selection, transforms, diagnostics, and dependable asset reload;
- then expand 2D-plane orientation, fidelity, performance, and exchange workflows.

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

## Builds and releases

Tagged OT3D builds will be published in this repository's
[Releases](https://github.com/OpenAnimationLibrary/ot3d/releases). Early releases will
be clearly marked pre-release and will prioritize a portable Windows package so that
test preferences can remain isolated from an installed OpenToonz configuration.

Until the first tagged release is published, build from source using the inherited
OpenToonz instructions:

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
