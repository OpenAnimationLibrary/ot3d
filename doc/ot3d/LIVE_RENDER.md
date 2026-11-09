# Live Render: first GPU milestone

Use **Render > Live Render**, or right-click the viewer and enable **Live Render**.
The command is assignable in Configure Shortcuts. It is off initially and local to
each viewer; scenes, output settings and final/Task Rendering are not switched to a
new rendering backend. Enabling it turns off that viewer's Preview. Starting
Preview, entering level editing or using the free 3D view pauses Live Render.
Return to camera view in scene editing to resume it.

Connect native A:M/GLB levels or GLB Model FX through the existing 3D material,
transform, lighting and Environment Reflections nodes. Connect an equirectangular
image to Environment Reflections. Optional Image Based Lighting can use the same
image. Existing reflection intensity, tint, exposure, rotation, material override,
A:M Reflectivity/ReflectiveBlend and finite reflection-distance behavior apply.
A room or studio panorama with broad bright windows produces readable reflections
on curved surfaces. The camera background remains independently composited.

## Implemented

- Persistent vertex buffers and shared floating-point environment textures.
- Per-fragment reflected environment direction and bilinear panorama lookup, with
  horizontal wrapping, pole clamping and the existing world-space rotation.
- Linear-light reflection mixing and a floating-point intermediate framebuffer.
  HDR is retained internally; presentation uses the existing display transfer.
- Smooth native A:M preview normals and the current GLB faceted normals.
- Local material/direct-light and diffuse IBL shading prepared at mesh vertices.
  Authored specular and reflection settings are retained; this is not a new PBR
  interpretation of A:M materials.
- Background preparation from a detached FX tree, one active preparation per
  viewer, coalesced pending updates, bounded allocations and stale-result rejection.
- Ordinary viewer pan/zoom reuses uploaded geometry and environments. Scene camera,
  model, lighting, frame and material changes prepare a replacement snapshot.
  The last completed frame remains visible while that snapshot is prepared.
- Original tool interaction, gizmos, input and undo remain with SceneViewer.
- Context-local resources, cleanup/recreation and diagnostics with normal-view
  fallback when Live Render cannot evaluate a graph or initialize the GPU.

The worker uses the existing rendering-session lifecycle for panorama inputs.
Live Render neither computes complete output rasters nor reads the framebuffer
back to the CPU during ordinary drawing. Preparation stops between sources and
live vertex batches when superseded; a panorama/source evaluation already in
progress must finish its bounded request before the pending snapshot starts.

## Deliberate first-milestone boundaries

This is a camera-view GPU renderer, not yet the shared unprojected world-scene
architecture. CPU scene snapshot construction and projection remain necessary
when model/camera state changes. GPU transforms, GPU skinning, adaptive patch
subdivision and independent camera navigation are follow-up work.

The existing per-source depth and Merge 3D branch composition are retained.
Other scene objects do not appear in reflections. Rough reflections, probes,
planar mirrors, cast shadows, ray tracing and idle sample accumulation are not
part of this milestone. Local shading is interpolated from vertices, while
reflections are evaluated per pixel. Transparency uses triangle sorting and can
differ from the software renderer for intersecting transparent geometry.

Ordinary 2D columns continue through the existing viewer. The live 3D result is
composited above them, as in the initial hybrid viewer; this is not a complete
mixed 2D/3D FX-graph preview. Unsupported 2D post effects on a 3D branch produce a
visible diagnostic and fall back to the ordinary viewer rather than silently
bypassing those effects. Use Preview for complete FX composition. Wireframe,
sub-xsheet edit-in-place, picking and camera-test paths use the ordinary viewer.

Limits: 500,000 triangles per live frame; four reflection environments per source;
384 MiB of uploaded geometry/textures; 16 megapixels for the live framebuffer;
existing environment bounds of four megapixels per map. Desktop OpenGL with
floating-point textures/framebuffers and GLSL 1.20 is required. No hardware ray
tracing or vendor-specific SDK is required. An unsupported device falls back.

## Validation and acceptance

Portable GLB/software-rendering tests continue to run without Qt or a GPU. The
optional `BUILD_LIVE_GPU_TESTS=ON` builds a Qt context-backed conformance executable:

```sh
cmake -S toonz/sources/glb -B live-build -DBUILD_LIVE_GPU_TESTS=ON
cmake --build live-build --parallel 2
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 ctest --test-dir live-build --output-on-failure
```

It checks reflected HDR values against CPU surface evaluation, rotation, tint,
material overrides, IBL ordering, transparency, graphics-state restoration,
resource reuse and capacity failures. Native A:M FX regression coverage checks
surface export, asynchronous preparation and rejection of obsolete results.

Hardware/interactive acceptance remains necessary on Windows/NVIDIA (including
GTX 1080), AMD, Intel and macOS: move model/camera/light controls, scrub animated
palettes and GLB clips, pan/zoom, toggle Preview, undo edits, dock/undock, open two
viewers and close a viewer during preparation. Inspect reflective curved A:M and
GLB surfaces with an asymmetric studio environment. Compare the intended material
appearance with Preview and final float output. Report preparation time, GPU frame
time, input latency and VRAM use; no 30/60 fps hardware claim follows from software
OpenGL conformance testing.
