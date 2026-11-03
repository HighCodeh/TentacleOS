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

"""Exercise real config staging with PSRAM allocation and storage faults."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware_c5/components/Service/spi_bridge/config_cache.c"


def main():
    source = re.sub(r'^#include "[^"\n]+"\n', '', SOURCE.read_text(), flags=re.M)
    harness = Path(__file__).with_suffix('.c').read_text()
    with tempfile.TemporaryDirectory(prefix='config-cache-') as temporary:
        unit = Path(temporary) / 'test.c'
        unit.write_text(harness.replace('/* PRODUCTION */', source))
        binary = Path(temporary) / 'test'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
            str(unit), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
