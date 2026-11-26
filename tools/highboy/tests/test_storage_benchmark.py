#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Check benchmark flush progress and cleanup without accessing a device."""
import contextlib
import io
import os
from pathlib import Path
import signal
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/highboy/benchmark_storage.sh"
CODE = compile(SCRIPT.read_text().split("<<'PYTHON'\n", 1)[1].rsplit("\nPYTHON", 1)[0], str(SCRIPT), "exec")


class BenchmarkTests(unittest.TestCase):
    def run_benchmark(self, fail_flush=False, sync_at_end=False):
        import fcntl
        original_create = tempfile.mkstemp
        checkpoints = []
        output = io.StringIO()
        previous_handler = signal.getsignal(signal.SIGTERM)
        with tempfile.TemporaryDirectory() as work:
            def create(**kwargs):
                kwargs["dir"] = work
                return original_create(**kwargs)

            def control(fd, operation, *args):
                if operation == 51:
                    checkpoints.append(os.fstat(fd).st_size)
                    if fail_flush:
                        raise OSError("injected flush error")
                return 0

            try:
                with patch.object(sys, "argv", [str(SCRIPT), "--size-mib", "9"] +
                                  (["--sync-at-end"] if sync_at_end else [])), \
                     patch.object(sys, "platform", "darwin"), \
                     patch("os.path.ismount", return_value=True), \
                     patch("os.statvfs", return_value=os.statvfs(work)), \
                     patch("tempfile.mkstemp", side_effect=create), \
                     patch.object(fcntl, "fcntl", side_effect=control), \
                     contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                    with self.assertRaises(SystemExit) as exit_status:
                        exec(CODE, {"__name__": "__main__"})
                self.assertEqual(list(Path(work).iterdir()), [])
            finally:
                signal.signal(signal.SIGTERM, previous_handler)
        return exit_status.exception.code, checkpoints, output.getvalue()

    def test_flush_batches_and_verified_read(self):
        status, checkpoints, output = self.run_benchmark()
        self.assertEqual(status, 0)
        self.assertEqual(checkpoints, [8 * 1024**2, 9 * 1024**2])
        self.assertIn("Synced: 9/9 MiB", output)
        self.assertIn("SHA-256 integrity: OK", output)

    def test_final_sync_only(self):
        status, checkpoints, output = self.run_benchmark(sync_at_end=True)
        self.assertEqual(status, 0)
        self.assertEqual(checkpoints, [9 * 1024**2])
        self.assertIn("Submitted: 8/9 MiB (final sync pending)", output)
        self.assertNotIn("Synced: 8/9 MiB", output)
        self.assertIn("Synced: 9/9 MiB", output)
        self.assertIn("SHA-256 integrity: OK", output)

    def test_final_sync_failure_discards_results(self):
        status, checkpoints, output = self.run_benchmark(True, True)
        self.assertEqual(status, 1)
        self.assertEqual(checkpoints, [9 * 1024**2])
        self.assertNotIn("Results (", output)
        self.assertNotIn("Synced:", output)
        self.assertIn("Temporary file removed", output)

    def test_flush_failure_is_not_reported_as_success(self):
        status, checkpoints, output = self.run_benchmark(True)
        self.assertEqual(status, 1)
        self.assertEqual(checkpoints, [8 * 1024**2])
        self.assertNotIn("Synced:", output)
        self.assertNotIn("Results (", output)
        self.assertIn("Temporary file removed", output)


if __name__ == "__main__":
    unittest.main()
