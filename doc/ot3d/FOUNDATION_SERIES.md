# OT3D foundation implementation series

These changes implement the agreed priorities in reviewable slices.
A draft can contain a working backend while its native UI, platform and
compatibility gates remain unfinished. Do not infer completion from PR creation.

## Review and merge relationships

| Priority | Draft | Relationship | Initial result |
| --- | --- | --- | --- |
| CI prerequisite | [#10](https://github.com/OpenAnimationLibrary/ot3d/pull/10) | Merge first; correction carried in affected drafts | Keep clang-format 14 on a supported runner |
| Workspace | [#3](https://github.com/OpenAnimationLibrary/ot3d/pull/3) | Independent, targets main | Xsheet left, Viewer center, FX Schematic right |
| Native EXR | [#4](https://github.com/OpenAnimationLibrary/ot3d/pull/4) | Independent, targets main | Full OpenEXR/Imath library and adapter |
| AOV workflow | [#5](https://github.com/OpenAnimationLibrary/ot3d/pull/5) | Based on #4 | Multipart/layer/channel routing and raw data |
| Retained assets | [#6](https://github.com/OpenAnimationLibrary/ot3d/pull/6) | Based on #5 | Retained EXR levels/sequences and sibling images |
| Direct 3D Viewer | [#7](https://github.com/OpenAnimationLibrary/ot3d/pull/7) | Independent, targets main | Replacement based on SceneViewer; direct GLB geometry |
| EXR inspection/deep | [#8](https://github.com/OpenAnimationLibrary/ot3d/pull/8) | Based on #4 | Native metadata inspection and typed deep transport |
| Python/external jobs | [#9](https://github.com/OpenAnimationLibrary/ot3d/pull/9) | Builds on merged #4–#8 | Addons configuration, generation panel, cancellable SculptForge/Hugging Face jobs and explicit undoable GLB import |

For stacked PRs, merge the parent first and retarget the child to main without
bringing neighboring OT-Dev history across. #8's source-CMake addition must be
retained alongside #5/#6 when integrating both branches.

## Functional priorities after the first slices

1. Verify ordinary 2D drawing, save/reopen and Viewer interaction on each
   supported platform; preserve the read-only external-asset boundary.
2. Finish the replacement Viewer's spatial contract: shared camera, scale,
   depth and clipping for 2D planes and 3D meshes. Its initial model pass has
   mesh-to-mesh depth only and is not the M3 hybrid-occlusion milestone.
3. Make object selection, numeric/gizmo transforms and undo dependable; then
   harden animation, asset reload/relink/collection and source diagnostics.
4. Integrate materials, lighting/shadows and explicit Viewer/final-render
   differences, with bounded caches, background preparation and GPU batching.
5. Connect an EXR Inspector and Viewer channel/part controls, raw pixel values,
   exposure/OCIO, data/display windows and A/B inspection. Preserve exact
   non-color channels, including UINT IDs, behind display representations.
6. Decode/preserve deep scanline and tiled samples, inspect counts/depth ranges,
   define volume integration and FX boundaries, then add deep compositing and
   output. Point-only preview flattening is not full deep support.
7. Validate #9's Addons paths, Asset Generation panel and explicit undoable
   GLB import. Extend its out-of-process job boundary with persistent history,
   replacement/reload and a general Python command/undo API. Online generation
   stays optional; core 2D/3D interaction remains independent.
8. Add depth/normal/object-ID FX workflows and hybrid projection/drawing tools
   after the source, camera, transport and interaction contracts are verified.

[ROADMAP.md](ROADMAP.md) continues to define milestone exit gates. Each draft's
implementation notes identify its current slice and unfinished work. Platform
CI results, local backend tests and native GUI/GPU acceptance are distinct.
