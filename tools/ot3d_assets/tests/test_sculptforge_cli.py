# SPDX-License-Identifier: BSD-3-Clause
"""Optional smoke tests against a separately installed, real SculptForge CLI."""
import os
from pathlib import Path
import tempfile
import unittest

from test_sculptforge_job import job


@unittest.skipUnless(os.environ.get("OT3D_SCULPTFORGE_CLI"), "real SculptForge CLI not configured")
class SculptForgeCli(unittest.TestCase):
    def test_real_capabilities_and_utf8_export(self):
        command = (os.environ["OT3D_SCULPTFORGE_CLI"],)
        capabilities = job.probe(command)
        self.assertFalse(capabilities["executed"])
        self.assertEqual(len(capabilities["models"]["image"]), 5)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'é 日本語 ; " model.glb'
            # Reuse the adapter's exact scalar serializer. Only the first
            # command is changed to a local primitive; no provider is called.
            spec = job.JobSpec(command, "text", "hunyuan3d-2", output, "unused")
            script = "new_sphere; export_glb " + job.script_for(spec, output).split("; export_glb ", 1)[1]
            job._execute([*command, "--no-packs", "--file", "-"], Path(directory), 30,
                         stdin_data=script.encode("utf-8"))
            self.assertTrue(output.is_file())
            self.assertEqual(len(job._glb_digest(output)), 64)
            self.assertEqual(len(list(Path(directory).glob("*.glb"))), 1)


if __name__ == "__main__":
    unittest.main()
