# Environment Reflections FX

Add **3D → Environment Reflections**. Connect a native A:M level, native GLB
level, GLB Model FX or a chain of 3D nodes to **Source**. Connect a 360 degree
equirectangular panorama (a 2:1 image is conventional) to **Environment**. A level
or a finite image FX can provide that panorama, including animated frames.
The camera background stays transparent; composite the background separately.

The default **Override Material** mode reflects 50% of the panorama on every
surface, making the effect usable with palette colors and GLB sources. Set
**Override Reflectivity** to zero to restore the source's local appearance.
**Override Blend** controls the local surface contribution: zero adds reflection;
100% replaces the reflected fraction of local shading.

Choose **A:M Material** to use each assigned `.mat` surface's Reflectivity and
ReflectiveBlend instead. Unassigned surfaces have zero reflectivity. Assign to a
specific model group with an upstream A:M Material node. A:M's reflection falloff
is a distance, not a percentage: a finite nonzero falloff excludes this infinitely
distant environment. Override mode explicitly bypasses that material setting.

**Intensity**, **Exposure** (stops), **Tint** and XYZ **Rotation** are animatable.
The panorama is fixed in world space, independent of model/camera motion. +Y is
north (the image top); +Z faces the panorama center; +X faces three quarters of
the way across it. Rotations apply X, Y, then Z. Sampling wraps horizontally and
clamps at the poles. Source rasters use OpenToonz's bottom-up row convention.

The input is evaluated as floating-point linear light. Premultiplied transparent
areas contribute against black. Float output retains HDR radiance; 8/16-bit output
clips to its display range. Environment images must have finite bounds, no more
than 8192 pixels in either dimension and at most **4 megapixels** (for example
2048 × 1024). Resize/crop larger or infinite FX before connecting. Bounds queries
never compute environment pixels. Pixel loading honors cancellation and caches
by the upstream image alias, frame and render settings.

Environment contributions survive Transform 3D, A:M Material, Three-Point Light
and Merge 3D in either source/downstream order. Three-Point Light's master controls
its own lights, not the panorama. Multiple reflection nodes apply in graph order;
use one node on a Merge 3D output to share a panorama across its branches. Image
Plane remains an unlit textured card. Flat Render bypasses environment shading;
Disable Default Lighting removes the default light while retaining authored
reflection and explicit lights.

This implementation supplies sharp environment reflections. It does not reflect
other scene objects, cast shadows, add reflection occlusion, blur reflections by
roughness, or reproduce A:M ray tracing. Native A:M uses the preview patch normals;
GLB Model FX uses its existing faceted mesh surface. Geometry, camera projection,
depth, alpha and tessellation are retained rather than relighting a flattened image.
