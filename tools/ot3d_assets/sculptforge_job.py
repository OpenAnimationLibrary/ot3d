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
import re
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import uuid


MODELS = {
    "image": frozenset({"trellis-2", "hunyuan3d-2.1", "hunyuan3d-2-turbo",
                        "hunyuan3d-2", "stable-fast-3d"}),
    "text": frozenset({"hunyuan3d-2-turbo", "hunyuan3d-2"}),
}

MODEL_INFO = {
    "trellis-2": ("TRELLIS.2", "microsoft/TRELLIS.2-4B", "microsoft/TRELLIS.2",
                  "https://github.com/microsoft/TRELLIS.2/blob/main/LICENSE"),
    "hunyuan3d-2.1": ("Hunyuan3D 2.1", "tencent/Hunyuan3D-2.1", "tencent/Hunyuan3D-2.1",
                       "https://huggingface.co/tencent/Hunyuan3D-2.1/blob/main/LICENSE"),
    "hunyuan3d-2-turbo": ("Hunyuan3D 2 mini Turbo", "tencent/Hunyuan3D-2mini",
                         "tencent/Hunyuan3D-2mini-Turbo",
                         "https://huggingface.co/tencent/Hunyuan3D-2mini/blob/main/LICENSE"),
    "hunyuan3d-2": ("Hunyuan3D 2", "tencent/Hunyuan3D-2", "tencent/Hunyuan3D-2",
                    "https://huggingface.co/tencent/Hunyuan3D-2/blob/main/LICENSE"),
    "stable-fast-3d": ("Stable Fast 3D", "stabilityai/stable-fast-3d", "stabilityai/stable-fast-3d",
                       "https://huggingface.co/stabilityai/stable-fast-3d/blob/main/LICENSE.md"),
}


def model_catalog() -> dict:
    return {mode: [{"id": model, "label": MODEL_INFO[model][0],
                    "model_url": "https://huggingface.co/" + MODEL_INFO[model][1],
                    "space_url": "https://huggingface.co/spaces/" + MODEL_INFO[model][2],
                    "license_url": MODEL_INFO[model][3], "license_reference_only": True}
                   for model in sorted(models)] for mode, models in MODELS.items()}


class JobError(RuntimeError):
    def __init__(self, message: str, code: str = "failed"):
        super().__init__(message)
        self.code = code


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
    if (not isinstance(spec.mode, str) or not isinstance(spec.model, str) or
            spec.mode not in MODELS or spec.model not in MODELS[spec.mode]):
        raise JobError("Choose a supported model for this text/image mode")
    if not spec.command or any(not isinstance(value, str) or not value or "\0" in value
                               for value in spec.command):
        raise JobError("Supply the SculptForge CLI executable")
    if not isinstance(spec.output, Path) or spec.output.suffix.lower() != ".glb":
        raise JobError("The output must be a new .glb file")
    if not math.isfinite(spec.timeout_seconds) or spec.timeout_seconds <= 0:
        raise JobError("The job timeout must be positive and finite")
    if not isinstance(spec.prompt, str):
        raise JobError("The prompt must be text")
    if spec.mode == "text":
        if (not spec.prompt.strip() or len(spec.prompt) > 16000 or
                "\0" in spec.prompt or spec.image is not None):
            raise JobError("Supply one text prompt (up to 16000 characters)")
    elif not isinstance(spec.image, Path) or not spec.image.is_file() or spec.prompt:
        raise JobError("Supply one existing image file")


def command_for(spec: JobSpec, output: Path, image: Path | None = None) -> list[str]:
    """No user text or paths are passed in the external CLI's argv.

    The UTF-8 stdin script quotes every scalar, including embedded quotes and
    backslashes. This also avoids Windows narrow-argv code-page corruption.
    """
    validate(spec)
    return [*spec.command, "--no-packs", "--file", "-"]


def script_for(spec: JobSpec, output: Path, image: Path | None = None) -> str:
    validate(spec)

    def quoted(value):
        return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'

    if spec.mode == "text":
        generation = "text_to_3d prompt=" + quoted(spec.prompt)
    else:
        generation = "image_to_3d path=" + quoted((image or spec.image).resolve())
    return (generation + " service=huggingface model=" + quoted(spec.model) +
            " remesh=false; export_glb path=" + quoted(output))


def _glb_digest(path: Path) -> str:
    if path.is_symlink() or not path.is_file():
        raise JobError("SculptForge did not produce a regular GLB file")
    size = path.stat().st_size
    if not 24 <= size <= 1024 * 1024 * 1024:
        raise JobError("Generated GLB size is invalid or exceeds the import budget")
    with path.open("rb") as stream:
        magic, version, length, json_length, chunk = struct.unpack("<5I", stream.read(20))
        if (magic != 0x46546C67 or version != 2 or length != size or
                chunk != 0x4E4F534A or json_length % 4 or
                json_length > min(size - 20, 16 * 1024 * 1024)):
            raise JobError("Generated output has an invalid GLB envelope")
        try:
            document = json.loads(stream.read(json_length).decode("utf-8"))
            if document.get("asset", {}).get("version") != "2.0":
                raise ValueError("asset version")
        except (ValueError, AttributeError, RecursionError) as error:
            raise JobError("Generated GLB JSON is invalid") from error
        # A GLB generated as one asset must be self-contained on import.
        for collection in (document.get("buffers", []), document.get("images", [])):
            if not isinstance(collection, list) or any(
                    not isinstance(item, dict) or "uri" in item for item in collection):
                raise JobError("Generated GLB must embed its buffers and images")
        binary_seen = False
        while stream.tell() < size:
            header = stream.read(8)
            if len(header) != 8:
                raise JobError("Generated GLB chunk header is truncated")
            chunk_length, chunk_type = struct.unpack("<2I", header)
            if (chunk_length % 4 or chunk_length > size - stream.tell() or
                    chunk_type != 0x004E4942 or binary_seen):
                raise JobError("Generated GLB chunk layout is invalid")
            binary_seen = True
            stream.seek(chunk_length, os.SEEK_CUR)
        stream.seek(0)
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _stop(process: subprocess.Popen) -> None:
    # A process may exit before descendants do. Clean up its group as well.
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGTERM)
        elif process.poll() is None:
            taskkill = Path(os.environ["SystemRoot"]) / "System32" / "taskkill.exe"
            subprocess.run([str(taskkill), "/PID", str(process.pid), "/T", "/F"],
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=5, check=False)
            if process.poll() is None:
                process.kill()
        process.wait(timeout=3)
    except ProcessLookupError:
        pass
    except subprocess.TimeoutExpired:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.wait()
    finally:
        if os.name == "posix":
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass


def _redact(detail: str) -> str:
    for key, value in os.environ.items():
        if len(value) >= 6 and key.upper().endswith(("TOKEN", "KEY", "SECRET")):
            detail = detail.replace(value, "[redacted]")
    return re.sub(r"\bhf_[A-Za-z0-9]+\b", "[redacted]", detail)


def _execute(argv: list[str], cwd: Path | None, timeout: float,
             cancelled=None, limit: int = 2 * 1024 * 1024,
             capture: int = 32000, stdin_data: bytes | None = None) -> bytes:
    if cancelled is not None and cancelled.is_set():
        raise JobError("Generation cancelled", "cancelled")
    try:
        process = subprocess.Popen(argv, cwd=cwd,
                                   stdin=subprocess.PIPE if stdin_data is not None else subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   shell=False, start_new_session=os.name == "posix")
    except OSError as error:
        raise JobError("Cannot launch the configured SculptForge CLI") from error
    tail = bytearray()
    exceeded = threading.Event()

    def drain():
        total = 0
        while block := process.stdout.read(4096):
            total += len(block)
            tail.extend(block)
            del tail[:-capture]
            if total > limit:
                exceeded.set()

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()

    def feed():
        try:
            process.stdin.write(stdin_data)
            process.stdin.close()
        except (OSError, ValueError):
            pass

    writer = threading.Thread(target=feed, daemon=True) if stdin_data is not None else None
    if writer:
        writer.start()
    started = time.monotonic()
    try:
        while process.poll() is None:
            if cancelled is not None and cancelled.is_set():
                raise JobError("Generation cancelled", "cancelled")
            if exceeded.is_set():
                raise JobError("SculptForge exceeded the job log budget")
            if time.monotonic() - started > timeout:
                raise JobError("SculptForge generation timed out", "timed_out")
            try:
                process.wait(timeout=0.05)
            except subprocess.TimeoutExpired:
                pass
    finally:
        _stop(process)
        if writer:
            writer.join(timeout=1)
        reader.join(timeout=5)
        if not reader.is_alive():
            process.stdout.close()
    if reader.is_alive():
        raise JobError("SculptForge left an output stream open")
    if exceeded.is_set():
        raise JobError("SculptForge exceeded the job log budget")
    if process.returncode:
        detail = _redact(bytes(tail).decode("utf-8", errors="replace")).strip()
        if "Text to 3D is disable" in detail:
            raise JobError("The selected hosted Space has disabled text-to-3D. "
                           "Choose another model or use image input; this setting belongs "
                           "to the hosted service.", "service_unavailable")
        raise JobError("SculptForge failed: " + detail)
    return bytes(tail)


def probe(command: tuple[str, ...], cancelled=None) -> dict:
    """Explicit local capability check; --schema submits no generation job."""
    if not command or any(not value or "\0" in value for value in command):
        raise JobError("Supply the SculptForge CLI executable")
    raw = _execute([*command, "--no-packs", "--schema"], None, 15, cancelled,
                   limit=8 * 1024 * 1024, capture=8 * 1024 * 1024)
    try:
        schema = json.loads(raw.decode("utf-8"))
        commands = {item["name"]: item["input_schema"]["properties"] for item in schema}
        required = {"text_to_3d": ("prompt", "service", "model", "remesh"),
                    "image_to_3d": ("path", "service", "model", "remesh"),
                    "export_glb": ("path",)}
        for name, parameters in required.items():
            for parameter in parameters:
                expected = "boolean" if parameter == "remesh" else "string"
                if commands[name][parameter]["type"] != expected:
                    raise ValueError("incompatible parameter")
    except (ValueError, KeyError, TypeError, UnicodeError) as error:
        raise JobError("SculptForge CLI lacks the required text/image/export schema") from error
    return {"provider": "huggingface", "schema_sha256": hashlib.sha256(raw).hexdigest(),
            "models": model_catalog(), "executed": False}


_windows_job_handle = None


def _contain_windows_worker() -> None:
    """Keep descendants in a kill-on-close Job Object if the UI kills us."""
    global _windows_job_handle
    if os.name != "nt" or _windows_job_handle is not None:
        return
    import ctypes
    from ctypes import wintypes

    class Basic(ctypes.Structure):
        _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                    ("flags", wintypes.DWORD), ("min_ws", ctypes.c_size_t),
                    ("max_ws", ctypes.c_size_t), ("active", wintypes.DWORD),
                    ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                    ("scheduling", wintypes.DWORD)]

    class Limits(ctypes.Structure):
        _fields_ = [("basic", Basic), ("io", ctypes.c_uint64 * 6),
                    ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                    ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                               ctypes.c_void_p, wintypes.DWORD]
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateJobObjectW(None, None)
    limits = Limits()
    limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if (not handle or
            not kernel.SetInformationJobObject(handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)) or
            not kernel.AssignProcessToJobObject(handle, kernel.GetCurrentProcess())):
        if handle:
            kernel.CloseHandle(handle)
        raise JobError("Cannot contain the Windows asset worker process tree")
    # Deliberately retain the handle for this worker process's entire lifetime.
    # Windows closes it on normal exit or forced termination, killing children.
    _windows_job_handle = handle


def run_job(spec: JobSpec, cancelled: threading.Event | None = None) -> dict:
    validate(spec)
    if spec.output.is_symlink():
        raise JobError("Choose a new filename instead of an output symlink")
    output = spec.output.resolve()
    manifest = output.with_name(output.name + ".ot3d.json")
    if output.exists() or manifest.exists() or manifest.is_symlink():
        raise JobError("Output or provenance already exists; choose a new filename")
    if not output.parent.is_dir():
        raise JobError("Create or select the output directory first")
    if cancelled is not None and cancelled.is_set():
        raise JobError("Generation cancelled", "cancelled")
    capabilities = probe(spec.command, cancelled)
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
        _execute(argv, work, spec.timeout_seconds, cancelled,
                 stdin_data=script_for(spec, generated, staged_image).encode("utf-8"))
        if cancelled is not None and cancelled.is_set():
            raise JobError("Generation cancelled", "cancelled")
        digest = _glb_digest(generated)
        metadata = next(item for item in capabilities["models"][spec.mode]
                        if item["id"] == spec.model)
        result = {"schema": 2, "status": "completed", "job_id": uuid.uuid4().hex,
                  "adapter": "sculptforge-cli", "provider": "huggingface",
                  "mode": spec.mode, "model": spec.model, "input": input_info,
                  "model_metadata": metadata,
                  "cli_schema_sha256": capabilities["schema_sha256"],
                  "asset": output.name, "sha256": digest,
                  "elapsed_seconds": round(time.monotonic() - started, 3)}
        staged_manifest = work / "provenance.json"
        staged_manifest.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n",
                                   encoding="utf-8")
        if cancelled is not None and cancelled.is_set():
            raise JobError("Generation cancelled", "cancelled")
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
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sculptforge", type=Path)
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--catalog", action="store_true")
    parser.add_argument("--request", type=Path)
    parser.add_argument("--cancel-file", type=Path)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--text")
    source.add_argument("--image", type=Path)
    parser.add_argument("--model")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout", type=float, default=1800)
    parser.add_argument("--run", action="store_true", help="Execute the external generation job")
    args = parser.parse_args()
    try:
        _contain_windows_worker()
        if args.catalog:
            print(json.dumps({"models": model_catalog()}))
            return 0
        cancelled = threading.Event()
        signal.signal(signal.SIGTERM, lambda *_: cancelled.set())
        signal.signal(signal.SIGINT, lambda *_: cancelled.set())

        class Cancellation:
            def is_set(self):
                return cancelled.is_set() or (args.cancel_file is not None and args.cancel_file.exists())

        if args.request:
            if args.request.stat().st_size > 256 * 1024:
                raise JobError("Asset request exceeds its size budget")
            request = json.loads(args.request.read_text(encoding="utf-8"))
            args.sculptforge = Path(request["sculptforge"])
            args.model = request["model"]
            args.output = Path(request["output"])
            args.text = request.get("text")
            args.image = Path(request["image"]) if request.get("image") else None
            args.timeout = float(request.get("timeout", 1800))
        if args.sculptforge is None:
            raise JobError("Configure the SculptForge CLI executable first")
        command = (str(args.sculptforge.resolve()),)
        if args.probe:
            result = probe(command, Cancellation())
        else:
            if args.output is None or args.model is None:
                raise JobError("Choose the model and output GLB filename")
            spec = JobSpec(command, "text" if args.text is not None else "image",
                           args.model, args.output, args.text or "", args.image, args.timeout)
            if args.run:
                result = run_job(spec, Cancellation())
            else:
                result = {"provider": "huggingface", "model": spec.model,
                          "argv": command_for(spec, spec.output.resolve()),
                          "stdin_script": script_for(spec, spec.output.resolve()),
                          "executed": False}
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return 0
    except (JobError, OSError, ValueError, TypeError, KeyError) as error:
        print(json.dumps({"error": _redact(str(error)),
                          "status": getattr(error, "code", "failed")}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
