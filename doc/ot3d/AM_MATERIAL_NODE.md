# A:M Material node and direct-light shine

The current delivery is the **direct-light material/shine workflow**: choose a
constant A:M material, choose its target group, inspect the evaluated settings,
and render or key its specular response to lights. The shipped sample materials
provide a starting point for visual testing. Environment and inter-object
reflections are deferred until there is a reflection scene.

This delivery does not claim exact A:M renderer parity or support for every
surface feature. The coverage table and later fidelity gates state those limits;
they remain future work rather than hidden approximations.

## Use

Add **3D > A:M Material** in the FX Schematic. Connect a native A:M model column
to **Source**, use **Browse...** to select a `.mat` file, and connect the result
to **Three-Point Light** or the Xsheet. Enable Preview to see FX shading.
The ordinary non-Preview model display still shows the level palette.

Use **Group > Choose...** to select a group from the exposed model at the current
frame. Leave Group empty for the whole model. Exact names can also be typed.
Membership uses all patch corners' resolved CP heads, including groups that do
not supply a diffuse color. A missing group with no renderable patches reports
an error. Equal group names target all matching memberships. No model source,
group, palette, or material file is modified.

**Diffuse Color Source** chooses Material File or Level Palette. An unset file
color also inherits the level color. Level Palette uses the existing evaluated
palette keys at the Xsheet row, including held exposures (PR #21). Other material
attributes still come from the file. Each node assigns one complete surface;
later nodes override earlier nodes on overlapping selections.

Materials pass through geometry/camera transforms, chained lighting and 3D Merge
before rasterization. Place a group-specific node on its model branch before a
merge to avoid targeting unrelated branches. An A:M material on a GLB or Image
Plane reports an incompatible-source error. An empty file path passes through unless Override Specular is enabled.

The node stores its file path, group and options in the scene. Files stay
externally linked, using the current string-path convention of GLB Model FX;
Save Assets does not yet collect material dependencies. Content hashes invalidate
the cache after external edits, including edits that preserve size/timestamps.
There is an 8 MiB file limit and 64-assignment chain limit.

**Inspect** displays the evaluated specular values, diffuse source, ambiance,
transparency, deferred reflections and unsupported attributes at the current
frame. **Clear** removes the reference without deleting its source file.

## Working on shine

On the **Specular** page, enable **Override Specular** to key Specular Size,
Specular Intensity and Specular Color in OT3D. With this switch off, imported
values stay authoritative. With no material file, the node adds shine to the
level's existing palette surface. A fileless specular node after another material
changes only its specular attributes, preserving that material's color, ambiance
and opacity. A node with a file still assigns a complete surface.

Three sample materials are included in `stuff/library/materials`:
`AM_Specular_Matte.mat`, `AM_Specular_Tight.mat` and
`AM_Specular_Broad.mat`. They share the same diffuse color and specular intensity,
so switching files isolates highlight width. The matte sample disables the
highlight. Use the Inspect button to verify the selected values.

Start with a Three-Point Light, reduce fill/rim intensity, and move the key light
while watching Preview. Size controls highlight width; intensity controls its
strength. Either at zero disables the highlight. Specular Color tints the light's
color. Ambient light contributes diffuse illumination and produces no highlight.
Rig master and each direct light's intensity affect shine. Positional lights also
change direction and attenuation across the surface. Chained rigs add their
independent highlights; moving the camera's eye changes the view-dependent
response. Reflections are not required for any of these controls.

Imported Reflectivity, Reflectivity Falloff and Reflective Blend remain stored.
Active reflections add a **Deferred A:M scene reflections** diagnostic, rather
than blocking the material. This is independent of Unsupported Attributes mode;
other unavailable active effects still follow that mode.

## Surface coverage

| A:M attribute | Current behavior |
| --- | --- |
| Diffuse Color | File RGB or evaluated Level Palette color; converted to linear light for shading |
| Diffuse Falloff | SDK Lambert sample rule: clamped `N.L / falloff`; zero removes front-side falloff |
| Ambiance Color / Intensity / Blend | Unset color follows diffuse; self illumination and diffuse blending; independent of light-rig master |
| Specular Color / Size / Intensity | Per-sample view-dependent Blinn preview with direct-light incidence; keyable node overrides; zero size/intensity disables highlights |
| Transparency | Constant surface alpha; depth-ordered, premultiplied compositing in linear light within one native source |
| Roughness / Roughness Scale | Parsed and retained; A:M turbulence/normal perturbation is not implemented |
| Reflectivity / Falloff / Reflective Blend | Parsed and retained; explicitly deferred without blocking direct-light shine |
| Index of Refraction | Parsed and retained; refracted rays are not implemented |
| Density | Parsed and retained; thickness-dependent absorption is not implemented |
| Translucency | Parsed and retained; raytraced back lighting and occlusion are not implemented |
| Radiance | Parsed and retained; radiosity is not implemented |
| Glow | Parsed and retained; choreography-dependent glow post processing is not implemented |

**Unsupported Attributes: Report Error** is the default. An active unsupported
effect blocks rendering with the attribute name, except explicitly deferred scene reflections. **Supported Attributes Preview**
explicitly omits those effects and adds diagnostics to the render snapshot.
Neither setting promises A:M renderer parity: in particular, Hash's exact
specular lobe has not been validated. Roughness is never mapped to a PBR
highlight-width parameter.

The reader accepts modern tagged `MATERIALFILE` documents containing one
constant `ATTRIBUTE`, including scalar child blocks such as
`<Reflectivity>Value=…</Reflectivity>`. It retains source bytes. Procedural,
layered, animated-property and plugin trees are rejected, rather than flattened
to the first attribute. Legacy bracketed 8.x files are not supported. No plugin,
MFC code or SDK runtime is loaded.

## Rendering contract and limits

Assignments reach the immutable native model snapshot before lighting or
projection. Four-corner preview patches supply smooth normals from finite differences of
that same approximate patch surface, before tessellation. Normals are transformed
with the inverse transpose for nonuniform model scale, clipped with the surface
and kept in world space during projection. They are interpolated per coverage
sample and normalized for shading. Three-corner/manual meshes retain geometric
normal fallback. There is no smoothing across independent patch boundaries. Near-clipped triangles retain patch selection; world sample
positions are reconstructed from the source camera and interpolated with
perspective correction. Specular and positional lighting are evaluated at each
coverage sample. Camera motion changes the eye without moving world lights.

The shared tile renderer's optional immutable sample evaluator keeps all
material-bearing intersections until depth sorting. Coincident tessellation
edge hits are deduplicated so alpha is not applied twice. Fragment storage is
bounded by the existing 256 MiB tile budget, with explicit failure if exhausted.
Existing opaque GLB/Image Plane rasterization keeps its original path. 3D Merge
retains its existing per-source depth and source-over composition; this change
does not supply cross-source rays, shadows or a unified depth buffer.

Flat Render bypasses the material node with the FX graph. Disable Default
Lighting suppresses the fallback headlight; material ambiance remains emissive.
The material's opacity multiplies the native column's opacity at raster output.

## References and completion gates

Semantic reference: [A:M SDK 19.5](https://github.com/OpenAnimationLibrary/amsdk/tree/ee3dd9fb82bf80de8cdd75e00273e2eeab925e33),
`HAttrProperty`/`HMaterialNode`, `Include/OldAttr.h` and the Lambert/BlinnPhong shader samples.
No SDK code is copied or linked. The
[Hash Technical Reference](https://www.hash.com/ftp/pub/docs/TECHREF.pdf)
distinguishes turbulence roughness, specular width, density and raytraced
translucency. File-layout checks also inspected the version-17 `black gloss`,
`glass`, `brass`, `gray rough` and `red glow` materials in
[extrastuff](https://github.com/OpenAnimationLibrary/extrastuff/tree/cf1bf2b6e1dac250c6cd4f6f9392d34aaafb7b8b/Data/Materials).
Automated fixtures are independently written synthetic records.

Before this can claim the requested basic-surface fidelity:

1. Compare fixed model/light/camera renders from A:M 19.5 with OT3D. Establish
   color-management, highlight-width and ambiance-blend reference values and
   tolerances, including camera motion, curved patches and nonuniform scaling.
2. Carry validated smooth patch normals and source-space sampling coordinates;
   validate continuity across patch boundaries; implement and compare A:M turbulence roughness and scale. The existing
   approximate patch evaluation remains a separate fidelity limitation.
3. Establish a common ray-query scene with bounded acceleration structures,
   source/object identities, shadows and reflection/refraction recursion. Add
   reflection falloff/blend, IOR, thickness/density absorption and translucency
   against A:M reference images. Test overlapping and nested transparent solids.
4. Add radiance/global illumination and glow with choreography/render settings.
   Keep surface emission distinct from the glow post effect.
5. Evaluate material trees, property animation, transforms and linked images
   without executing native plugins. Add dependency collection and portable
   scene-relative paths. Unsupported operators must remain explicit.

Portable tests cover parsing, units/defaults, malformed data, unsupported-feature
diagnostics, direct-light specularity, camera/light angles, colored and multiple lights,
zero controls, normal interpolation, clipping and nonuniform scaling. Shared-renderer tests cover transparent depth
order, linear blending, coincident hits, tile seams and cancellation. Native
`amfx` coverage includes assignment, pixels, palette selection, light ordering,
cloning, persistence, file invalidation, empty exposure, render-tree expansion,
fileless/keyed specular overrides and highlights on reflective source materials.
Native application/Windows and visual A:M comparisons are still required.
