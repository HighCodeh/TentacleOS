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

"""Generate fixed 32/64-bit MicroQuickJS ROM tables with the host C compiler."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / 'firmware_p4/components/Service/script_engine/mquickjs'
OUTPUT = VENDOR / 'generated'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    arguments = parser.parse_args()
    if not arguments.check:
        OUTPUT.mkdir(exist_ok=True)
    def write(name, data):
        path = OUTPUT / name
        if arguments.check:
            if not path.is_file() or path.read_bytes() != data:
                raise SystemExit('JS ROM differs: ' + name)
        else:
            path.write_bytes(data)
    with tempfile.TemporaryDirectory(prefix='highboy-js-rom-') as tmp:
        exe = Path(tmp) / 'build'
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-O2', '-I', str(VENDOR),
                        str(ROOT / 'tools/scripts/script_js_stdlib.c'), str(VENDOR / 'mquickjs_build.c'),
                        '-lm', '-o', str(exe)], check=True)
        for bits in (32, 64):
            flags = ['-m32'] if bits == 32 else []
            write(f'script_js_rom_{bits}.h', subprocess.check_output([str(exe), *flags]))
            atoms = subprocess.check_output([str(exe), '-a', *flags])
            write(f'mquickjs_atom_{bits}.h', atoms)
        write('mquickjs_atom.h', (
            '#include <stdint.h>\n#if UINTPTR_MAX == UINT32_MAX\n'
            '#include "mquickjs_atom_32.h"\n#else\n'
            '#include "mquickjs_atom_64.h"\n#endif\n').encode())


if __name__ == '__main__':
    main()
