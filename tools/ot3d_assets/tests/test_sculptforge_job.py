# SPDX-License-Identifier: BSD-3-Clause
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock
import os
import struct
import subprocess

MODULE = Path(__file__).resolve().parents[1] / "sculptforge_job.py"
spec = importlib.util.spec_from_file_location("sculptforge_job", MODULE)
job = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = job
spec.loader.exec_module(job)

# Real subprocess fixture. It observes the fixed script and AFTER-parse --set
# arguments and writes a small GLB envelope; it never contacts a model service.
WORKER = r'''
import json, pathlib, struct, sys, time, shlex, subprocess
argv = sys.argv[1:]
if '--schema' in argv:
    definitions = {'text_to_3d': ['prompt', 'service', 'model', 'remesh'],
                   'image_to_3d': ['path', 'service', 'model', 'remesh'],
                   'export_glb': ['path']}
    print(json.dumps([{'name': name, 'input_schema': {'properties': {
        key: {'type': 'boolean' if key == 'remesh' else 'string'} for key in keys}}}
        for name, keys in definitions.items()]))
    raise SystemExit(0)
assert argv[-2:] == ['--file', '-']
script = sys.stdin.buffer.read().decode('utf-8')
lexer = shlex.shlex(script, posix=True, punctuation_chars=';')
lexer.whitespace_split = True
lexer.commenters = ''
tokens = list(lexer)
separator = tokens.index(';')
assert tokens[0] in ('text_to_3d', 'image_to_3d') and tokens[separator+1] == 'export_glb'
generation = dict(token.split('=', 1) for token in tokens[1:separator])
export = dict(token.split('=', 1) for token in tokens[separator+2:])
assert generation['service'] == 'huggingface'
values = {'OT3D_MODEL': generation['model'], 'OT3D_OUTPUT': export['path']}
if tokens[0] == 'text_to_3d':
    values['OT3D_PROMPT'] = generation['prompt']
else:
    values['OT3D_IMAGE'] = generation['path']
assert '--no-packs' in argv
if values.get('OT3D_PROMPT') == 'wait':
    time.sleep(30)
if values.get('OT3D_PROMPT') == 'flood':
    sys.stdout.buffer.write(b'x' * (3 * 1024 * 1024))
    raise SystemExit(0)
if values.get('OT3D_PROMPT') == 'secret':
    import os
    print(os.environ['HF_TOKEN'])
    raise SystemExit(2)
if values.get('OT3D_PROMPT') == 'quota':
    print('GPU quota exhausted; retry later')
    raise SystemExit(2)
output = pathlib.Path(values['OT3D_OUTPUT'])
if values.get('OT3D_PROMPT') == 'invalid':
    output.write_text('not a model')
    raise SystemExit(0)
if 'OT3D_IMAGE' in values:
    assert pathlib.Path(values['OT3D_IMAGE']).read_bytes() == b'image fixture'
else:
    assert 'prompt=' in script
data = json.dumps({'asset': {'version': '2.0'}, 'extras': {'input': values}}).encode()
data += b' ' * (-len(data) % 4)
output.write_bytes(struct.pack('<5I', 0x46546c67, 2, 20 + len(data), len(data), 0x4e4f534a) + data)
'''


class Jobs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.worker = self.root / "fake sculptforge.py"
        self.worker.write_text(WORKER, encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def request(self, prompt="a cube", **changes):
        values = dict(command=(sys.executable, str(self.worker)), mode="text",
                      model="hunyuan3d-2", output=self.root / "new model.glb",
                      prompt=prompt, timeout_seconds=5)
        values.update(changes)
        return job.JobSpec(**values)

    def test_text_data_cannot_become_script(self):
        prompt = 'a cube"; new_sphere; export_glb path="other.glb\n$HOME'
        request = self.request(prompt)
        argv = job.command_for(request, request.output)
        self.assertNotIn(prompt, argv)
        self.assertEqual(argv[-2:], ["--file", "-"])
        result = job.run_job(request)
        self.assertEqual(result["input"]["prompt"], prompt)
        self.assertEqual(result["provider"], "huggingface")
        self.assertEqual(json.loads(request.output.with_name(
            request.output.name + ".ot3d.json").read_text(encoding="utf-8")), result)
        self.assertFalse((self.root / "other.glb").exists())
        self.assertEqual(list(self.root.glob(".ot3d-generate-*")), [])

    def test_image_snapshot_and_provenance(self):
        image = self.root / "input ; image.png"
        image.write_bytes(b"image fixture")
        request = self.request("", mode="image", image=image, model="trellis-2")
        result = job.run_job(request)
        self.assertIn("image_sha256", result["input"])
        self.assertEqual(image.read_bytes(), b"image fixture")
        self.assertNotIn("prompt", result["input"])

    def test_existing_result_is_never_overwritten(self):
        request = self.request()
        request.output.write_bytes(b"existing asset")
        with self.assertRaisesRegex(job.JobError, "already exists"):
            job.run_job(request)
        self.assertEqual(request.output.read_bytes(), b"existing asset")

    def test_quota_failure_has_no_result(self):
        request = self.request("quota")
        with self.assertRaisesRegex(job.JobError, "quota exhausted"):
            job.run_job(request)
        self.assertFalse(request.output.exists())

    def test_invalid_output_is_not_published(self):
        request = self.request("invalid")
        with self.assertRaises(job.JobError):
            job.run_job(request)
        self.assertFalse(request.output.exists())

    def test_timeout_terminates_worker(self):
        request = self.request("wait", timeout_seconds=0.2)
        start = time.monotonic()
        with self.assertRaisesRegex(job.JobError, "timed out"):
            job.run_job(request)
        self.assertLess(time.monotonic() - start, 5)
        self.assertFalse(request.output.exists())

    def test_active_cancellation_terminates_worker(self):
        request = self.request("wait")
        cancelled = threading.Event()
        timer = threading.Timer(0.2, cancelled.set)
        timer.start()
        try:
            with self.assertRaisesRegex(job.JobError, "cancelled"):
                job.run_job(request, cancelled)
        finally:
            timer.cancel()
        self.assertFalse(request.output.exists())

    def test_probe_is_local_and_checks_schema(self):
        result = job.probe(self.request().command)
        self.assertFalse(result["executed"])
        self.assertEqual(len(result["schema_sha256"]), 64)
        self.assertIn("license_url", result["models"]["image"][0])
        self.assertFalse(self.request().output.exists())
        self.worker.write_text("print('[]')", encoding="utf-8")
        with self.assertRaisesRegex(job.JobError, "required.*schema"):
            job.probe(self.request().command)

    def test_unicode_and_escaped_scalars_use_utf8_stdin(self):
        prompt = 'é 日本語 🟣 \\" ; export_glb path=bad.glb # \n$HOME'
        result = job.run_job(self.request(prompt))
        self.assertEqual(result["input"]["prompt"], prompt)
        self.assertFalse((self.root / "bad.glb").exists())
        self.assertEqual(result["schema"], 2)
        self.assertEqual(result["status"], "completed")
        self.assertTrue(result["model_metadata"]["license_reference_only"])

    def test_logs_are_bounded_and_credentials_are_redacted(self):
        with self.assertRaisesRegex(job.JobError, "log budget"):
            job.run_job(self.request("flood"))
        with mock.patch.dict(os.environ, {"HF_TOKEN": "hf_private_fixture_secret"}):
            with self.assertRaises(job.JobError) as error:
                job.run_job(self.request("secret"))
            self.assertNotIn("hf_private_fixture_secret", str(error.exception))
            self.assertIn("[redacted]", str(error.exception))

    def test_cancel_after_validation_does_not_publish(self):
        event = threading.Event()
        original = job._glb_digest
        def cancel_after_digest(path):
            digest = original(path)
            event.set()
            return digest
        with mock.patch.object(job, "_glb_digest", cancel_after_digest):
            with self.assertRaisesRegex(job.JobError, "cancelled"):
                job.run_job(self.request(), event)
        self.assertFalse(self.request().output.exists())
        self.assertEqual(list(self.root.glob(".ot3d-generate-*")), [])

    def test_failed_manifest_publication_rolls_back_only_new_asset(self):
        original = job.os.link
        def link(source, destination):
            if str(destination).endswith(".ot3d.json"):
                raise OSError("fixture failure")
            return original(source, destination)
        with mock.patch.object(job.os, "link", link):
            with self.assertRaisesRegex(job.JobError, "publish"):
                job.run_job(self.request())
        self.assertFalse(self.request().output.exists())

    def test_chunk_layout_and_external_dependencies_are_rejected(self):
        path = self.root / "invalid.glb"
        for document in ({"asset": {"version": "2.0"}, "buffers": [{"uri": "other.bin"}]},
                         {"asset": {"version": "2.0"}, "images": [{"uri": "https://invalid/image.png"}]}):
            data = json.dumps(document).encode()
            data += b' ' * (-len(data) % 4)
            path.write_bytes(struct.pack("<5I", 0x46546c67, 2, 20 + len(data), len(data), 0x4e4f534a) + data)
            with self.assertRaisesRegex(job.JobError, "embed"):
                job._glb_digest(path)
        data = b'{"asset":{"version":"2.0"}}  '
        data += b' ' * (-len(data) % 4)
        body = data + struct.pack("<2I", 400, 0x004e4942)
        path.write_bytes(struct.pack("<5I", 0x46546c67, 2, 20 + len(body), len(data), 0x4e4f534a) + body)
        with self.assertRaisesRegex(job.JobError, "chunk layout"):
            job._glb_digest(path)

    def test_model_mode_and_input_validation(self):
        for request in (self.request(model="trellis-2"), self.request(""),
                        self.request(timeout_seconds=float("nan")),
                        self.request("x\0y")):
            with self.assertRaises(job.JobError):
                job.command_for(request, request.output)

    def test_timeout_stops_descendants(self):
        marker = self.root / "orphan.txt"
        child = "import pathlib,time; time.sleep(1.2); pathlib.Path(%r).write_text('orphan')" % str(marker)
        parent = ("import subprocess,sys,time; subprocess.Popen([sys.executable,'-c',%r]); "
                  "time.sleep(30)") % child
        with self.assertRaisesRegex(job.JobError, "timed out"):
            job._execute([sys.executable, "-c", parent], self.root, 0.25)
        time.sleep(1.4)
        self.assertFalse(marker.exists(), "Cancelled worker left a running descendant")

    def test_worker_cli_has_structured_failure(self):
        result = subprocess.run([sys.executable, str(MODULE), "--probe", "--sculptforge",
                                 sys.executable], capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 1)
        response = json.loads(result.stdout)
        self.assertEqual(response["status"], "failed")
        self.assertNotIn("contain the Windows", response["error"])

    def test_concurrent_jobs_publish_one_matching_asset_and_manifest(self):
        results, failures = [], []
        def run(prompt):
            try:
                results.append(job.run_job(self.request(prompt)))
            except job.JobError as error:
                failures.append(error)
        threads = [threading.Thread(target=run, args=(prompt,)) for prompt in ("one", "two")]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=10)
            self.assertFalse(thread.is_alive())
        self.assertEqual(len(results), 1)
        self.assertEqual(len(failures), 1)
        manifest = json.loads(self.request().output.with_name(
            self.request().output.name + ".ot3d.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest, results[0])
        self.assertEqual(job._glb_digest(self.request().output), manifest["sha256"])


if __name__ == "__main__":
    unittest.main()
