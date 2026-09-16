# OT3D External Asset Compatibility

## Status of this contract

This document defines the intended early asset boundary. It is deliberately narrower
than glTF 2.0 and will be converted into a versioned support matrix as fixtures pass the
M0–M4 gates. Anything not named in a release's matrix is unsupported, even if a test
asset happens to load.

Compatibility terms used by OT3D:

| Term | Meaning |
| --- | --- |
| Supported | Covered by a redistributable fixture on every platform claimed by the release |
| Experimental | Available for testing; behavior or saved parameters may change in a pre-release |
| Parsed only | Data can be inspected internally but is not promised to affect Viewer or output |
| Unsupported | Rejected, ignored with a diagnostic, or intentionally outside the release |

Silent partial support is a defect. OT3D should identify the asset and omitted feature
when it cannot honor data that materially changes the result.

## Authoring applications and interchange format

The initial workflow targets GLB files exported from Blender and Animation:Master.

| Source | Early boundary | Native project handling |
| --- | --- | --- |
| Blender | Self-contained binary glTF 2.0 (`.glb`) from a tested exporter/version | `.blend` remains in Blender; OT3D does not read or write it |
| Animation:Master | Self-contained `.glb` produced by the documented bridge/export path | A:M project/model/action files remain in Animation:Master; OT3D does not read or write them |
| Other DCCs | May work when their output conforms to the tested GLB subset | No compatibility claim until fixtures are added |

Blender and Animation:Master are independent projects. Naming an exchange workflow
does not imply endorsement, affiliation, or support from their maintainers.

## Read-only asset contract

External 3D source remains authoritative:

1. The artist changes meshes, rigs, materials, and source animation in the DCC.
2. The DCC exports a new self-contained GLB.
3. OT3D reads that file and applies OT3D-owned placement and playback settings.
4. Reload replaces imported asset data; it never writes into the GLB or DCC project.

OT3D scenes may store a reference to the GLB and OT3D-owned metadata. They must not
silently embed a modified, untraceable copy of the artist's 3D source. Project-relative
references are preferred for movable projects. Asset collection behavior will be
specified before it is presented as supported.

Replacement exports should preserve node, skeleton, material, and animation identities
where practical. Early reload guarantees only the properties named by that release. A
structurally incompatible export must produce a diagnostic; OT3D should not guess at a
destructive remapping.

## GLB envelope

The first supported envelope is intentionally conservative:

- binary glTF 2.0 (`.glb`), not native DCC formats;
- resources contained in the GLB, with no network fetches;
- finite numeric transforms and bounded buffer/image allocation;
- triangle geometry from features named in the release support matrix;
- node hierarchy and selected embedded animation data when explicitly supported; and
- material properties only when explicitly supported by the current Viewer mode.

Compressed mesh extensions, arbitrary vendor extensions, morph targets, embedded
cameras/lights, unusual primitive modes, and full PBR are not assumed. Required glTF
extensions that OT3D does not implement must make the asset unsupported rather than
produce a deceptively incomplete result.

Malformed offsets, lengths, accessors, images, transforms, or animation values must
fail with a bounded error. Loading a GLB must not execute code from the asset or fetch a
remote resource.

## Coordinates and units

glTF 2.0 describes a right-handed, Y-up coordinate system whose linear unit is the
meter. Blender and Animation:Master may use different authoring axes or unit displays;
their tested export profiles are responsible for producing conforming GLB data.

OT3D still needs an explicit conversion between that space and OpenToonz stage units,
camera conventions, and depth direction. M1 locks that conversion using numeric
fixtures before it becomes a compatibility promise. Until then:

- scale and orientation are experimental;
- an asset-level placement transform may be used for calibration;
- the original GLB remains unchanged; and
- a release must state the conversion it implements rather than ask artists to infer it.

The eventual conversion must be shared by Viewer, picking, bounds, and final-render
paths. Applying separate “looks right” corrections in those paths is not compatible.

## Time and animation

glTF animation time is expressed in seconds; OpenToonz scene time is expressed in
frames at the scene frame rate. The intended mapping is explicit and reproducible:

```text
assetTimeSeconds = animationOffsetSeconds
                 + (sceneFrame - startFrame) / sceneFramesPerSecond * playbackSpeed
```

Loop, hold, and out-of-range behavior must be parameters rather than consequences of a
particular playback code path. Paused, scrubbed, stepped, Viewer-played, and final
results must evaluate the same supported pose at the same mapped time.

The migrated seed contains animation data recovery and a pose evaluator. That does not
by itself claim complete support for interpolation modes, skins, morph targets, or
every animation target. Each item moves from parsed-only to supported only with a
fixture and Viewer/final-path agreement.

## Materials, images, and lighting

The initial Viewer prioritizes correct position and depth. Wireframe and simple
unlit/base-color display therefore precede lighting and PBR.

The migrated seed includes material-color groundwork, but the unmerged OT-Dev PBR work
is excluded from the OT3D baseline. Any later PBR subset must define color space,
texture transform, alpha, sampler, lighting, and fallback behavior together; importing
isolated shading code is not enough.

GLB cameras and lights are not initially interchangeable with OT3D scene cameras and
lights. The migrated Three-Point Light is an OT3D/OpenToonz FX and should not be read as
a promise to interpret glTF light extensions.

## Initial feature classification

This table records planning state, not a release claim. The release notes and included
`BUILD_INFO` file are authoritative for a particular binary.

| Capability | Foundation state | Planned gate |
| --- | --- | --- |
| GLB container validation and immutable data | Migrated seed; verification required | M0 |
| Static triangle mesh data | Migrated seed/CPU FX path | M0–M2 |
| Static mesh in interactive Viewer | Not yet supported | M2 |
| Node hierarchy transforms | Seeded; cross-path agreement required | M1–M2 |
| Mutual 2D/3D opaque depth | Not yet supported | M3 |
| Embedded animation data and pose evaluation | Migrated seed; bounded subset | M0/M4 |
| Animated/skinned mesh in interactive Viewer | Not yet supported | M4, fixture dependent |
| Reliable in-place asset reload | Partial seed behavior; contract pending | M4 |
| Base color in interactive Viewer | Planned bounded subset | M2/M7 |
| Textures, transparency, and PBR | Not part of initial baseline | M7 review |
| Morph targets, glTF camera/light extensions | Unsupported until separately planned | Unscheduled |
| GLB or DCC write-back | Out of scope for early development | Unscheduled |

## Compatibility fixtures

Every supported claim should name:

- the source application and exact version;
- the exporter/bridge and exact version or source revision;
- the redistributable source project when licensing permits;
- the exported GLB plus a checksum;
- expected bounds, axes, scale, frame range, and visible result; and
- the OT3D revision and platforms that passed.

Fixtures should be small enough for automated validation. Production assets can expose
problems, but they cannot replace redistributable regression cases.

## Scene and rollback safety

During pre-releases, users should work on copies. A release that adds saved scene state
must document forward and backward behavior. Opening an OT3D scene in OpenToonz may
drop or ignore downstream-only data; that path is not considered a safe round trip
unless a release explicitly says otherwise.

OT3D must keep a missing or unsupported external asset distinguishable from an empty
or deliberately hidden one so that project damage is visible and recoverable.
