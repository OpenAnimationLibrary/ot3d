# 3D Merge

Add **3D Merge** from the FX browser's **3D** category. Connect the two model
branches to **Foreground** and **Background**, then connect the merge output to
**Three-Point Light**. Its output can connect directly to **Xsheet**, **Over**,
or other existing 2D FX.

Each input accepts a native A:M model column, GLB Model, 3D Image Plane, or
another 3D-aware node such as 3D Transformer, Three-Point Light or 3D Merge.
Ordinary 2D FX outputs need a 3D Image Plane before entering a merge. Either
merge input may be empty. Nest merges to combine additional sources.

Downstream lighting rigs apply to both branches. A rig on an individual branch
continues to contribute to that branch, including when another rig is added
after the merge. Downstream 3D Transformers forward their geometry/camera
transform to each source. Stage-column placement and render visibility are
preserved independently for each input.

The group retains each source's camera projection, internal depth testing,
texture, wireframe style, column opacity and color filter until raster output
is requested. Foreground is composited over Background; separate models do
not share a depth buffer or cast shadows onto each other. This permits mixed
native A:M and GLB sources with different projection/depth conventions.

The standard Over node keeps its existing raster behavior. Place 3D Merge and
the shared lighting **before** ordinary 2D FX. The first 2D FX renders the lit
group into pixels; those pixels can then pass through the existing 2D pipeline
without changes to its nodes.

With the render-mode controls, Disable Default Lighting still allows explicit
rigs to illuminate both sources. Flat Render bypasses the merge/lighting FX and
composes the exposed source columns using their base colors.
