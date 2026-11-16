# Point Light FX

Add **FX Schematic > Add FX > 3D > Point Light** and connect a native A:M
or GLB level, GLB Model FX, or 3D Merge to **Source**. Connect the output to
the Xsheet or a normal compositing FX. Chain Point Light and Three-Point Light
nodes to combine independent lights. A light after 3D Merge illuminates all
of its model branches. Geometry, material assignments and animation stay live.

| Mode | Illumination | Aim |
| --- | --- | --- |
| Sun | Parallel rays; no distance falloff | From Position toward Aim/Target |
| Bulb (default) | Omnidirectional, with distance falloff | No directional effect |
| Klieg (Spotlight) | Positional light restricted to a cone | From Position toward Aim/Target |

Position and aim use the model source's world coordinates. Sun's Position
controls its direction and the location of its optional visible source, not
the strength of the parallel illumination. Color and Intensity are independent
for each node. Bulb/Klieg use the existing bounded falloff `2/(1+(distance/radius)^2)`:
intensity equals the authored value at Falloff Radius and approaches twice that
value near the emitter. It is not a hard range limit or inverse-square singularity.

Cone Angle is the full outer angle. Cone Softness sets the transition band as a
percentage of the half angle; zero gives a hard edge. Lighting is evaluated at
surface samples, including within a large triangle. Native A:M material shading
uses the same attenuation for diffuse and specular highlights.

## Target

**Target** is optional and accepts the same typed 3D model/group sources. It is
an aiming reference and is not included in the output. Its animated geometry
bounds supply the center; 3D Merge targets use the average of their nonempty
child centers. Native model placement, GLB embedded animation and model
transforms are evaluated at the current frame. Column image placement is
retained through scene expansion and cloning. Connect sources that share the
same world coordinate system and camera convention; different independent
camera projections are not reconciled into a shared scene camera.

Without Target, Aim XYZ is an absolute point. With Target, Aim XYZ offsets that
source's center. An empty or unexposed target falls back to absolute Aim XYZ.
A coincident Position and aim uses the stable forward direction (negative Z).
Bulb remains omnidirectional. Target evaluation queries geometry, never its
raster pixels or environment image.

## Visible source

**Visible in Render** defaults off. Enable it to render an emissive sphere at
Position, using the light's color and intensity. Visible Source Radius controls
only the sphere's appearance; it does not change falloff, cone width, or shadow
softness. A Sun is represented by the same finite sphere at its authored
position, rather than an infinitely distant sky disc. This is a visible source,
not a physical lamp housing.

The sphere uses the model's camera, clipping, depth test and image placement;
models can occlude it. It contributes to output bounds and supports integer,
floating and linear-light output. A Source supplies the projection, so a
standalone disconnected node renders empty. In 3D Merge, branches retain their
existing independent cameras and compositing order; depth occlusion works
within a source, not between separate Merge layers. The visible source inherits
its branch's wireframe and column color/opacity settings.

Volumetric beams/haze are reserved for a separate PR. This addition does not
introduce cast shadows or area-light sampling. Existing default-lighting and
Flat Render controls retain their meanings; explicit Point Lights replace the
default model illumination and combine with explicit lights/IBL/reflections.
Flat Render still shows a visible source when enabled.

## Validation

- Portable renderer tests: cone boundaries and validation, per-sample lighting,
  world-space Sun directions, target placement, visible-source depth occlusion,
  output bounds, clipping, HDR radiance and tile consistency.
- Native FX tests: all three modes, animated Target aiming, target exclusion,
  manual aim, visibility, model occlusion, independent chaining, render-tree
  expansion, cloning, scene persistence and cancellation.
- Native A:M regression: positional illumination, target/camera metadata,
  visible-source geometry and typed render-tree expansion.

Local validation: all eight portable GLB/renderer tests and both A:M parser/material
tests pass. The Point Light FX, native renderer integration, and native test
translation units pass C++17 syntax checks against Qt 5.15 headers. Renderer
ASan/UBSan checks pass with leak detection disabled because this execution
environment does not support LeakSanitizer. Full native FX runtime execution
and the Windows application build remain pending CI.
