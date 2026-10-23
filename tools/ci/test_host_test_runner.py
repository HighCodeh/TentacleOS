#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
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

"""Verify aggregate host-test status using explicit fake interpreters."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix='host-runner-test-') as temporary:
        work = Path(temporary)
        stub = '''#!/bin/bash
printf '%s\\n' "$*" >> "$HOST_RUNNER_TEST_LOG"
case "$1" in
  */test_contracts.py) exit "$HOST_RUNNER_TEST_FAILURE" ;;
esac
exit 0
'''
        for name in ('python3', 'node'):
            executable = work / name
            executable.write_text(stub)
            executable.chmod(0o755)
        for failure in (0, 7):
            log = work / f'commands-{failure}.log'
            environment = {**os.environ, 'PATH': str(work) + os.pathsep + os.environ['PATH'],
                           'HOST_RUNNER_TEST_LOG': str(log),
                           'HOST_RUNNER_TEST_FAILURE': str(failure)}
            result = subprocess.run(['bash', str(ROOT / 'tools/highboy/test.command')],
                                    env=environment, capture_output=True, text=True, timeout=30)
            assert result.returncode == (0 if failure == 0 else 1), result.stderr
            executed = log.read_text()
            assert 'test_contracts.py' in executed
            assert 'test_usb_msc.mjs' in executed and 'test_doom_dma.mjs' in executed
            assert ('1 host test group(s) failed.' in result.stderr) is (failure != 0)
    print('PASS: host runner reaches the final group and preserves nonzero aggregate status')


if __name__ == '__main__':
    main()
