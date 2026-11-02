# Python / SculptForge asset-job foundation

The initial adapter runs an independently installed SculptForge CLI in a
separate process. OT3D's core drawing and Viewer do not acquire a Python,
model-runtime or online-service requirement. No SculptForge code is copied
or linked into OT3D.

## Implemented slice

`tools/ot3d_assets/sculptforge_job.py` plans and runs text/image-to-GLB jobs
using SculptForge's explicit Hugging Face service. The model is selected
explicitly. Prompt/path values use the CLI's `--set` substitution after its
fixed script has been parsed; they are never interpolated into command code.
The worker inherits SculptForge's existing local settings/environment for
authentication; API keys are not arguments or provenance fields.

The adapter snapshots an image input, supports timeouts and cancellation,
checks the result's GLB envelope, and publishes a new asset plus provenance
without overwriting an existing result. Final geometry validation belongs to
the OT3D GLB loader on import. Source scenes are not modified by this worker.
Publishing currently requires same-filesystem hard-link support (including
ordinary NTFS/Linux filesystems). Unsupported filesystems fail explicitly.

Planning is the default. Example (replace executable/output locations):

```sh
python tools/ot3d_assets/sculptforge_job.py \
  --sculptforge /path/to/sculptforge-cli \
  --image /path/to/concept.png --model trellis-2 \
  --output /path/to/new-model.glb
```

Add `--run` to execute. For text, use `--text "a stylised stone arch"` and
a supported text model such as `hunyuan3d-2`. Hugging Face receives the chosen
prompt/image when the external generation is executed. Generated results get
a `.glb.ot3d.json` provenance file recording provider, model, input prompt or
image digest, job identity and output digest. No remote job is submitted by
configuration, discovery, dry-run or the tests.

## Integration gates

- Preferences > Addons fields for an explicit SculptForge executable and
  Python interpreter; unavailable integrations remain disabled without scans.
- An on-demand Asset Generation panel with model selection, queued/running/
  cancelled/failed/completed states and service/quota feedback.
- Start the Python worker out of process. Only the UI thread may import a
  completed GLB or make an undoable scene edit. No callbacks mutate the scene
  from the worker. Capture a target scene identity to prevent stale completion.
- Validate output with the existing GLB loader, then let the artist import or
  replace/reload an asset while preserving its OT3D placement and timing.
- Version/capability discovery and tests against a real installed SculptForge;
  preserve the exact provider/model and associated model-license metadata.
- Review Windows process-tree cancellation and process/worker log budgets.
  The initial Windows cancellation terminates the worker; POSIX also terminates
  its process group. Disk logs currently last only for the job's lifetime.
- Build the broader Python scene/Viewer API around explicit main-thread
  commands, shared undo behavior, asset handles and bounded background jobs.

The initial command contract was checked against SculptForge's `src/cli/main.cpp`
and `src/ai/AiCommands.cpp`; image and text modes have different supported
models. Hosted model availability and quotas can change and must fail visibly.
Live generation, the Addons UI and automatic scene import are **not implemented**
by this first draft.

## Validation

`python -m unittest discover -s tools/ot3d_assets/tests -v` runs actual local
subprocess fixtures for text/image argument transport, provenance, overwrite
refusal, invalid output, quota errors, timeout and cancellation. It does not
call Hugging Face or require SculptForge. Three-platform CI covers the same
worker lifecycle; successful fixtures do not validate a live model service.

References:

- https://github.com/islandmed/SculptForge/blob/main/src/cli/main.cpp
- https://github.com/islandmed/SculptForge/blob/main/src/ai/AiCommands.cpp
- https://github.com/islandmed/SculptForge/blob/main/docs/ai-services.md
