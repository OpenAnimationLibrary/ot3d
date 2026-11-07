# A:M Material node — experimental foundation

This is a draft implementation of the native material path. It does **not** yet
meet the goal of accurately rendering every basic A:M surface attribute. The
remaining gates below are part of that goal, not optional substitutes for it.

## Use

Add **3D > A:M Material** in the FX Schematic. Connect a native A:M model column
to **Source**, enter the absolute path to a `.mat` file, and connect the result
to **Three-Point Light** or the Xsheet. Enable Preview to see FX shading.
The ordinary non-Preview model display still shows the level palette.

Leave **Group** empty for the whole model, or enter an exact A:M group name.
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
Plane reports an incompatible-source error. An empty file path passes through.

The node stores its file path, group and options in the scene. Files stay
externally linked, using the current string-path convention of GLB Model FX;
Save Assets does not yet collect material dependencies. Content hashes invalidate
the cache after external edits, including edits that preserve size/timestamps.
There is an 8 MiB file limit and 64-assignment chain limit.

## Surface coverage

| A:M attribute | Current behavior |
| --- | --- |
| Diffuse Color | File RGB or evaluated Level Palette color; converted to linear light for shading |
| Diffuse Falloff | SDK Lambert sample rule: clamped `N.L / falloff`; zero removes front-side falloff |
| Ambiance Color / Intensity / Blend | Unset color follows diffuse; self illumination and diffuse blending; independent of light-rig master |
| Specular Color / Size / Intensity | Per-sample view-dependent Blinn preview; unset color follows diffuse; zero size disables highlights |
| Transparency | Constant surface alpha; depth-ordered, premultiplied compositing in linear light within one native source |
| Roughness / Roughness Scale | Parsed and retained; A:M turbulence/normal perturbation is not implemented |
| Reflectivity / Falloff / Reflective Blend | Parsed and retained; scene reflection rays and Fresnel behavior are not implemented |
| Index of Refraction | Parsed and retained; refracted rays are not implemented |
| Density | Parsed and retained; thickness-dependent absorption is not implemented |
| Translucency | Parsed and retained; raytraced back lighting and occlusion are not implemented |
| Radiance | Parsed and retained; radiosity is not implemented |
| Glow | Parsed and retained; choreography-dependent glow post processing is not implemented |

**Unsupported Attributes: Report Error** is the default. An active unsupported
effect blocks rendering with the attribute name. **Supported Attributes Preview**
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
projection. Material normals are geometric normals of the existing approximate
preview tessellation. Near-clipped triangles retain patch selection; world sample
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
`HAttrProperty`/`HMaterialNode`, `Include/OldAttr.h` and the Lambert shader sample.
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
   implement and compare A:M turbulence roughness and scale. The existing
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
diagnostics and local illumination. Shared-renderer tests cover transparent depth
order, linear blending, coincident hits, tile seams and cancellation. Native
`amfx` coverage includes assignment, pixels, palette selection, light ordering,
cloning, persistence, file invalidation, empty exposure and render-tree expansion.
Native application/Windows and visual A:M comparisons are still required.
