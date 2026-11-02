#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Out-of-process SculptForge/Hugging Face asset jobs; standard library only."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import threading
import time
import uuid


MODELS = {
    "image": frozenset({"trellis-2", "hunyuan3d-2.1", "hunyuan3d-2-turbo",
                        "hunyuan3d-2", "stable-fast-3d"}),
    "text": frozenset({"hunyuan3d-2-turbo", "hunyuan3d-2"}),
}


class JobError(RuntimeError):
    pass


@dataclass(frozen=True)
class JobSpec:
    command: tuple[str, ...]
    mode: str
    model: str
    output: Path
    prompt: str = ""
    image: Path | None = None
    timeout_seconds: float = 1800


def validate(spec: JobSpec) -> None:
    if spec.mode not in MODELS or spec.model not in MODELS[spec.mode]:
        raise JobError("Choose a supported model for this text/image mode")
    if not spec.command or any(not value or "\0" in value for value in spec.command):
        raise JobError("Supply the SculptForge CLI executable")
    if spec.output.suffix.lower() != ".glb":
        raise JobError("The output must be a new .glb file")
    if not math.isfinite(spec.timeout_seconds) or spec.timeout_seconds <= 0:
        raise JobError("The job timeout must be positive and finite")
    if spec.mode == "text":
        if (not spec.prompt.strip() or len(spec.prompt) > 16000 or
                "\0" in spec.prompt or spec.image is not None):
            raise JobError("Supply one text prompt (up to 16000 characters)")
    elif spec.image is None or not spec.image.is_file() or spec.prompt:
        raise JobError("Supply one existing image file")


def command_for(spec: JobSpec, output: Path, image: Path | None = None) -> list[str]:
    """Values enter --set AFTER SculptForge parses this fixed script.

    Quotes, semicolons and newlines in a prompt/path cannot introduce commands.
    No shell, eval, generated Python, or remote code is used by this adapter.
    """
    validate(spec)
    values = {"OT3D_MODEL": spec.model, "OT3D_OUTPUT": str(output)}
    if spec.mode == "text":
        generation = "text_to_3d prompt=$OT3D_PROMPT"
        values["OT3D_PROMPT"] = spec.prompt
    else:
        generation = "image_to_3d path=$OT3D_IMAGE"
        values["OT3D_IMAGE"] = str((image or spec.image).resolve())
    script = (generation + " service=huggingface model=$OT3D_MODEL remesh=false; "
              "export_glb path=$OT3D_OUTPUT")
    argv = [*spec.command, "--no-packs", "-c", script]
    for key, value in values.items():
        argv.extend(["--set", key + "=" + value])
    return argv


def _glb_digest(path: Path) -> str:
    if path.is_symlink() or not path.is_file():
        raise JobError("SculptForge did not produce a regular GLB file")
    size = path.stat().st_size
    if not 24 <= size <= 1024 * 1024 * 1024:
        raise JobError("Generated GLB size is invalid or exceeds the import budget")
    with path.open("rb") as stream:
        magic, version, length, json_length, chunk = struct.unpack("<5I", stream.read(20))
        if (magic != 0x46546C67 or version != 2 or length != size or
                chunk != 0x4E4F534A or json_length > min(size - 20, 16 * 1024 * 1024)):
            raise JobError("Generated output has an invalid GLB envelope")
        try:
            document = json.loads(stream.read(json_length).decode("utf-8"))
            if document.get("asset", {}).get("version") != "2.0":
                raise ValueError("asset version")
        except (ValueError, AttributeError, RecursionError) as error:
            raise JobError("Generated GLB JSON is invalid") from error
        stream.seek(0)
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _stop(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGTERM)
        else:
            process.terminate()
        process.wait(timeout=3)
    except ProcessLookupError:
        pass
    except subprocess.TimeoutExpired:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.wait()


def run_job(spec: JobSpec, cancelled: threading.Event | None = None) -> dict:
    validate(spec)
    output = spec.output.resolve()
    manifest = output.with_name(output.name + ".ot3d.json")
    if output.exists() or manifest.exists():
        raise JobError("Output or provenance already exists; choose a new filename")
    if not output.parent.is_dir():
        raise JobError("Create or select the output directory first")
    if cancelled is not None and cancelled.is_set():
        raise JobError("Generation cancelled")
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix=".ot3d-generate-", dir=output.parent) as tmp:
        work = Path(tmp)
        generated = work / "generated.glb"
        staged_image = None
        input_info = {"prompt": spec.prompt} if spec.mode == "text" else {}
        if spec.mode == "image":
            with spec.image.open("rb") as source:
                data = source.read(64 * 1024 * 1024 + 1)
            if len(data) > 64 * 1024 * 1024:
                raise JobError("Source image exceeds the 64 MiB job budget")
            staged_image = work / ("input" + spec.image.suffix)
            staged_image.write_bytes(data)
            input_info = {"image_sha256": hashlib.sha256(data).hexdigest()}
        argv = command_for(spec, generated, staged_image)
        with (work / "worker.log").open("w+b") as log:
            try:
                process = subprocess.Popen(argv, cwd=work, stdin=subprocess.DEVNULL,
                                           stdout=log, stderr=subprocess.STDOUT,
                                           shell=False, start_new_session=os.name == "posix")
            except OSError as error:
                raise JobError("Cannot launch the configured SculptForge CLI") from error
            try:
                while process.poll() is None:
                    if cancelled is not None and cancelled.is_set():
                        raise JobError("Generation cancelled")
                    if time.monotonic() - started > spec.timeout_seconds:
                        raise JobError("SculptForge generation timed out")
                    try:
                        process.wait(timeout=0.1)
                    except subprocess.TimeoutExpired:
                        pass
            finally:
                _stop(process)
            if process.returncode:
                log.seek(0, os.SEEK_END)
                log.seek(max(0, log.tell() - 32000))
                detail = log.read().decode("utf-8", errors="replace")
                # Credentials remain in the worker's existing environment/settings.
                for key in ("HF_TOKEN", "FAL_KEY", "MESHY_API_KEY"):
                    value = os.environ.get(key)
                    if value:
                        detail = detail.replace(value, "[redacted]")
                raise JobError("SculptForge failed: " + detail.strip())
        if cancelled is not None and cancelled.is_set():
            raise JobError("Generation cancelled")
        digest = _glb_digest(generated)
        result = {"schema": 1, "job_id": uuid.uuid4().hex,
                  "adapter": "sculptforge-cli", "provider": "huggingface",
                  "mode": spec.mode, "model": spec.model, "input": input_info,
                  "asset": output.name, "sha256": digest,
                  "elapsed_seconds": round(time.monotonic() - started, 3)}
        staged_manifest = work / "provenance.json"
        staged_manifest.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n",
                                   encoding="utf-8")
        # Same-filesystem hard links publish complete files without overwriting
        # another job's result. Fail explicitly on unsupported filesystems.
        linked = False
        try:
            os.link(generated, output)
            linked = True
            os.link(staged_manifest, manifest)
        except OSError as error:
            if linked:
                output.unlink()
            raise JobError("Cannot publish new asset/provenance files without overwrite") from error
        return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sculptforge", required=True, type=Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--text")
    source.add_argument("--image", type=Path)
    parser.add_argument("--model", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=1800)
    parser.add_argument("--run", action="store_true", help="Execute the external generation job")
    args = parser.parse_args()
    spec = JobSpec((str(args.sculptforge.resolve()),), "text" if args.text is not None else "image",
                   args.model, args.output, args.text or "", args.image, args.timeout)
    try:
        if args.run:
            result = run_job(spec)
        else:
            result = {"provider": "huggingface", "model": spec.model,
                      "argv": command_for(spec, spec.output.resolve()),
                      "executed": False}
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 0
    except (JobError, OSError) as error:
        print(json.dumps({"error": str(error)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
