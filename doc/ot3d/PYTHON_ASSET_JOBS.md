# Python / SculptForge asset generation

OT3D uses a separately installed Python 3.11+ interpreter and SculptForge CLI.
Generation runs out of process. Core drawing, rendering and the hybrid Viewer
continue to work without Python, SculptForge, accounts or online services.
OT3D does not copy, link, bundle or redistribute SculptForge code or model weights.

## Artist workflow

1. In **Preferences > Addons**, select the Python executable and
   `sculptforge-cli` executable. This page is available on Windows, macOS and
   Linux. Paths are explicit; OT3D does not search drives or install software.
2. Open **File > Script > Asset Generation**, or **Customize > Asset Generation**
   in a compact room menu. Existing custom menus can add the Asset Generation
   command through their menu editor or shortcut settings.
3. Choose **Check Integration (Local Only)**. The Python worker asks SculptForge
   for its command schema and verifies text/image generation and GLB export
   parameters. This check does not submit a model job or require an HF token.
4. Choose image or text input, an explicit model, and a new absolute GLB filename
   in an existing output directory. Image mode defaults to TRELLIS.2 when listed;
   text mode exposes only SculptForge's supported text models.
5. Review the selected model's terms and hosted Space. Enable sending the input
   and choose **Generate New Asset**. SculptForge sends that prompt/image to
   Hugging Face. Credentials remain in SculptForge's settings or environment;
   OT3D does not provide a token field or put credentials in process arguments.
6. Cancel if needed, or wait for Completed. A generated GLB and adjacent
   `.glb.ot3d.json` provenance file are saved. Generation alone makes no scene edit.
7. Choose **Import GLB Model FX** to validate the saved asset and insert a new FX
   column through the existing undo command, using the current row and column.
   Material colors are enabled. Undo removes the insertion. Existing models,
   placement and timing are not replaced.

A scene or Xsheet switch invalidates the captured import target, including a
switch away and back. The completed files remain available for a deliberate
manual import into the intended scene. Hiding the panel does not submit another
job; destroying it cancels its worker. The panel runs one job at a time.

## Process and data boundary

The panel embeds the single canonical Python adapter as a Qt resource; portable
packages do not depend on the source checkout or a second script installation.
A private temporary JSON request carries prompts and paths to the worker.
The external CLI receives a UTF-8 stdin script whose scalar strings are quoted
with escaped backslashes and quotes. Input is never passed through a shell,
`eval`, generated Python, or narrow Windows command-line values. A fixed
command structure allows only generation followed by GLB export.

Each execution rechecks capabilities. Images are snapshotted with a 64 MiB
limit. Jobs have a timeout, bounded log capture, cancellation before publication
and process-tree cleanup. POSIX uses a separate process group. On Windows the
worker puts itself into a kill-on-close Job Object before spawning SculptForge;
normal cancellation also uses process-tree termination. If the worker cannot
establish that containment, it fails before generation. The UI has its own
cancellation deadline for an unresponsive worker.

The adapter checks the GLB envelope, aligned chunk boundaries and embedded
buffer/image references, then hashes the result. It publishes complete files
without replacing existing assets, using same-filesystem hard links. NTFS and
ordinary local Linux/macOS filesystems support this; unsupported output volumes
fail explicitly. A publication failure rolls back the newly linked asset.

Before import, the UI checks the hash again and uses OT3D's existing bounded
GLB loader. Import and undo registration happen on the UI thread. Generated
meshes still inherit the hybrid Viewer's current rendering limitations.

Provenance records the provider/model key, prompt or source-image digest,
model/Space/license reference URLs, CLI schema digest, job ID, output digest
and elapsed time. License references are identified as references only: they
are not a downloaded license snapshot, model-weight revision, legal clearance
or a promise of service availability. Hosted Space implementations can change.
Credentials are excluded and known token patterns are redacted from failures.

## Command line

Planning remains the default:

```sh
python tools/ot3d_assets/sculptforge_job.py --sculptforge /path/to/sculptforge-cli \
  --image /path/to/concept.png --model trellis-2 --output /path/to/new-model.glb
```

Add `--run` to generate, or use `--text "a stylised stone arch"` with
`hunyuan3d-2` or `hunyuan3d-2-turbo`. `--probe` checks local capabilities;
`--catalog` prints the supported model references without contacting a service.
The UI uses `--request FILE --run --cancel-file FILE` for its bounded JSON
request/result protocol. Expected failures return a JSON error and nonzero exit.

## Validation and remaining gates

`python -m unittest discover -s tools/ot3d_assets/tests -v` tests real local
subprocesses: text/image input, Unicode and escaped scalars, provenance,
capability rejection, non-overwrite publication races and rollback, GLB chunk
validation, bounded/redacted logs, timeouts, cancellation and descendant cleanup.
The lifecycle suite runs on Windows, macOS and Linux.

An additional workflow builds the independent SculptForge CLI at
`e55d3e04e43458985e1dd7adb7939625dde23cf3` (0.10.7) with its desktop app and
HTTP support disabled. It verifies the real command schema and local primitive
export through a Unicode/punctuation filename. Set `OT3D_SCULPTFORGE_CLI` to
run that optional smoke test locally. No generation request is submitted by tests.
The source contract was checked against `src/cli/main.cpp`, `src/core/Script.cpp`,
`src/ai/AiCommands.cpp` and `src/ai/ImageTo3DService.cpp` at that revision.

The BUILD_ASSET_GENERATION_TESTS option enables a native QApplication smoke test
using the embedded Python worker and a local CLI fixture with no network code. It
checks probe/failure/cancel, scene-switch protection, GLB import/Undo and scene
save/reopen in platform CI. Before merge, verify the portable panel visually: configure paths,
probe, start/cancel/fail/complete a job, switch scenes during a job, import/undo,
save/reopen and check a portable package. Real hosted text/image generation,
quota/authentication feedback and current Space availability need explicit live
acceptance. A live anonymous image-to-3D request using Hunyuan3D 2.1 completed in about
35 seconds. Its 2.4 MB GLB loaded without warnings in OT3D (one mesh, 99,562
triangles) and rendered through the existing CPU renderer. Live anonymous text
attempts with both current Hunyuan text presets returned the hosted Space's
text-generation-disabled error. The adapter reports service_unavailable with a
hosted-service explanation. A successful hosted text job is not claimed.

Follow-up scope: persistent job history and queues, background native geometry
validation, replacement/reload with placement preservation, exact remote model
revision/license snapshots, other providers/local generators, and a general
Python scene command/undo API. This PR does not expose arbitrary Python execution
inside the scene or automatically install third-party runtimes.
