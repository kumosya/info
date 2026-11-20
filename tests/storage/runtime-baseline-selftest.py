"""Verify focused reruns cannot promote mixed boot configurations to PASS."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
from io import StringIO
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, patch

import runtime_baseline as baseline


class RuntimeBaselineTest(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.output = Path(temporary.name)
        self.stage = self.output / "fixture"
        self.stage.mkdir()
        (self.stage / "4kn.img").touch()
        self.report_file = self.stage / "result.json"
        self.report = {
            "status": "FAIL",
            "version": "0.1.5",
            "disk_only": True,
            "commands": [],
            "inputs_sha256": {"osinfo.img": "disk", "storage-probe": "probe"},
            "boots": [
                {"mode": "disk-create", "status": "PASS",
                 "argv": baseline.boot_arguments(self.stage, "disk-create", 2, True)},
                {"mode": "disk-persist", "status": "FAIL",
                 "argv": baseline.boot_arguments(self.stage, "disk-persist", 2, True)},
            ],
        }
        (self.stage / "disk-persist.log").write_text("earlier attempt failed\n")

    def run_rerun(self, *extra):
        argv = ["runtime_baseline.py", "--name", "fixture", "--run", "--disk-only",
                "--version", "0.1.5", "--case", "disk-persist", *extra]
        with patch.object(sys, "argv", argv), patch.object(baseline, "OUTPUT", self.output):
            with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
                return baseline.main()

    def test_mixed_configurations_rejected_before_boot(self):
        self.report_file.write_text(json.dumps(self.report))
        original = self.report_file.read_bytes()
        with patch.object(baseline.subprocess, "Popen") as launch:
            for args in (("--smp", "1", "--extra-4kn"), (), ("--normal", "--extra-4kn")):
                with self.subTest(args=args):
                    with self.assertRaises(SystemExit) as error:
                        self.run_rerun(*args)
                    self.assertEqual(error.exception.code, 2)
                    self.assertEqual(self.report_file.read_bytes(), original)
            launch.assert_not_called()

    def test_same_legacy_configuration_merges_with_new_budget(self):
        self.report_file.write_text(json.dumps(self.report))

        def boot(*args, stdout, **kwargs):
            stdout.write("[M0 STORAGE] PASS\n[M0 STORAGE] BACKEND VirtioBlock\n"
                         "[M0 STORAGE] FILE persisted\n"
                         "[M0 STORAGE] 4Kn identified, I/O rejected\n"
                         "[RELEASE] 0.1.5 verified\n")
            stdout.flush()
            return process

        process = Mock(returncode=0)
        process.poll.return_value = 0
        with patch.object(baseline.subprocess, "Popen", side_effect=boot) as launch:
            self.assertEqual(self.run_rerun("--extra-4kn", "--seconds", "180"), 0)
            launch.assert_called_once()
        updated = json.loads(self.report_file.read_text())
        self.assertEqual(updated["status"], "PASS")
        self.assertEqual(updated["boot_profile"]["smp"], 2)
        self.assertEqual(updated["boots"][1]["host_wait_seconds"], 180)
        self.assertEqual({case["baseline_id"] for case in updated["boots"]},
                         {updated["baseline_id"]})
        self.assertEqual(updated["previous_attempts"][0]["status"], "FAIL")

    def test_streamed_digest_matches_whole_file(self):
        path = self.stage / "multi-chunk.img"
        payload = bytes(range(256)) * 5000 + b"tail"
        path.write_bytes(payload)
        self.assertEqual(baseline.digest(path), hashlib.sha256(payload).hexdigest())


if __name__ == "__main__":
    unittest.main()
