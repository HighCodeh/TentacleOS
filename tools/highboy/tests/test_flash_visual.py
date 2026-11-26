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

import contextlib
import io
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import flash_visual as visual


class VisualTests(unittest.TestCase):
    def test_progress_and_verification(self):
        state = visual.FlashState()
        state.consume('OTA preflight: 50/100 bytes')
        self.assertEqual(state.stage, 'Checking current app')
        self.assertEqual(state.percent, 50)
        state.consume('Backup: 512/8192 KiB')
        self.assertEqual(state.percent, 6.25)
        state.consume('\x1b[35mWriting at 0x00020000 [====] 42.5%\x1b[0m')
        self.assertEqual(state.percent, 42.5)
        self.assertEqual(state.stage, 'Flashing')
        state.consume('Hash of data verified.')
        self.assertIsNone(state.percent)
        self.assertEqual(state.stage, 'Verifying')

    def test_artwork_is_ascii(self):
        output = visual.frame(visual.FlashState(), 80, 30, color=False)
        self.assertTrue(output.isascii())
        artwork = '\n'.join(visual.mascot_lines(False))
        self.assertGreaterEqual(len(set(artwork) & set(visual.ASCII_SHADES)), 8)
        self.assertEqual(artwork, '\n'.join(visual.mascot_lines(False)))
        colored = '\n'.join(visual.mascot_lines(True))
        self.assertGreaterEqual(len(set(re.findall(r'38;5;(\d+)m', colored))), 10)

    def test_indeterminate_indicator_stays_inside_bar(self):
        lines = visual.frame(visual.FlashState(), 80, 30, color=False).splitlines()
        bar = next(line for line in lines if line.startswith('['))
        self.assertTrue(bar.endswith(']'))
        self.assertNotIn('...', bar.split(']', 1)[1])

    def test_dimensions(self):
        for columns, rows in ((80, 24), (120, 40), (40, 20), (16, 4)):
            output = visual.frame(visual.FlashState(), columns, rows)
            lines = output.splitlines()
            self.assertLess(len(lines), rows)
            self.assertTrue(all(len(visual.clean(line)) < columns for line in lines))

    def test_preview_does_not_spawn(self):
        with mock.patch.object(visual.subprocess, 'Popen') as spawn:
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(visual.main(['--preview']), 0)
            spawn.assert_not_called()
            self.assertIn('no device access', output.getvalue())

    def test_plain_preserves_arguments(self):
        with mock.patch.object(visual.os, 'execv', side_effect=SystemExit) as execute:
            with self.assertRaises(SystemExit):
                visual.main(['--plain', '--mode', 'app', '-p', '/dev/test', '--target', 'c5', '--check'])
        command = execute.call_args.args[1]
        self.assertEqual(command[-3:], ['--target', 'c5', '--check'])
        self.assertNotIn('--plain', command)

    def test_child_success_and_failure(self):
        for code in (0, 7):
            output = io.StringIO()
            command = [sys.executable, '-c',
                       f'import sys; print("Writing at 0x20000 50%"); '
                       f'print("diagnostic", file=sys.stderr); sys.exit({code})']
            self.assertEqual(visual.run_visual(command, output), code)
            text = output.getvalue()
            self.assertIn('\x1b[?25h\x1b[?1049l', text)
            log = Path(re.search(r'Full log: (.+)', text)[1])
            try:
                self.assertIn('diagnostic', log.read_text())
                self.assertEqual(log.stat().st_mode & 0o777, 0o600)
                if code:
                    self.assertIn('Flash stopped (exit 7)', text)
                    self.assertNotIn('Updater completed successfully', text)
                else:
                    self.assertIn('Updater completed successfully', text)
            finally:
                log.unlink()

    def test_termination_restores_terminal_and_stops_child(self):
        script = ("import io, os, signal, sys, threading; import flash_visual as v; "
                  "threading.Timer(0.3, lambda: os.kill(os.getpid(), signal.SIGTERM)).start(); "
                  "sys.exit(v.run_visual([sys.executable, '-c', "
                  "'import time; print(\\\"started\\\", flush=True); time.sleep(30)']))")
        # Avoid shell quoting: the wrapper and child receive argv directly.
        script = script.replace('\\"', '"')
        result = subprocess.run([sys.executable, '-c', script],
                                cwd=Path(visual.__file__).parent,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 130, result.stderr)
        self.assertIn('\x1b[?25h\x1b[?1049l', result.stdout)
        log = Path(re.search(r'Full log: (.+)', result.stdout)[1])
        log.unlink()


if __name__ == '__main__':
    unittest.main()
