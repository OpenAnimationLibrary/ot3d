# SPDX-License-Identifier: BSD-3-Clause
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest

MODULE = Path(__file__).resolve().parents[1] / "sculptforge_job.py"
spec = importlib.util.spec_from_file_location("sculptforge_job", MODULE)
job = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = job
spec.loader.exec_module(job)

# Real subprocess fixture. It observes the fixed script and AFTER-parse --set
# arguments and writes a small GLB envelope; it never contacts a model service.
WORKER = r'''
import json, pathlib, struct, sys, time
argv = sys.argv[1:]
script = argv[argv.index('-c') + 1]
values = dict(argv[i+1].split('=', 1) for i, value in enumerate(argv) if value == '--set')
assert 'service=huggingface model=$OT3D_MODEL' in script
assert '--no-packs' in argv
if values.get('OT3D_PROMPT') == 'wait':
    time.sleep(30)
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
    assert 'prompt=$OT3D_PROMPT' in script
    assert values['OT3D_PROMPT'] not in script
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
        script = argv[argv.index("-c") + 1]
        self.assertNotIn(prompt, script)
        self.assertIn("OT3D_PROMPT=" + prompt, argv)
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

    def test_model_mode_and_input_validation(self):
        for request in (self.request(model="trellis-2"), self.request(""),
                        self.request(timeout_seconds=float("nan")),
                        self.request("x\0y")):
            with self.assertRaises(job.JobError):
                job.command_for(request, request.output)


if __name__ == "__main__":
    unittest.main()
