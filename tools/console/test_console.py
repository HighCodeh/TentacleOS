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

"""Exercise the actual pinned SDK editor before/after the repeat-key fix, without serial access."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

import patch_linenoise

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf', type=Path, required=True)
    parser.add_argument('--build', type=Path)
    args = parser.parse_args()
    original = args.idf / 'components/console/linenoise/linenoise.c'
    source = original.read_bytes()
    patched = patch_linenoise.patched_source(source)
    if args.build is not None:
        build = args.build.resolve()
        generated = build / 'highboy_console/linenoise.c'
        commands = json.loads((build / 'compile_commands.json').read_text())
        entries = [item for item in commands if Path(item['file']).name == 'linenoise.c']
        if len(entries) != 1 or Path(entries[0]['file']).resolve() != generated or generated.read_bytes() != patched:
            raise SystemExit('Build must compile exactly the generated, reviewed editor source')
    cases = [
        ('repeated DEL', b'abc\x7f\x7fX\n', b'aX'),
        ('repeated BS', b'abc\x08\x08X\n', b'aX'),
        ('DEL beyond empty', b'abc' + b'\x7f' * 64 + b'help\n', b'help'),
        ('BS beyond empty', b'abc' + b'\x08' * 64 + b'help\n', b'help'),
        ('rapid left and insertion', b'abcd\x1b[D\x1b[DX\n', b'abXcd'),
        ('rapid middle backspace', b'abcd\x1b[D\x7fX\n', b'abXd'),
        ('rapid forward delete', b'abcd\x1b[D\x1b[D\x1b[3~X\n', b'abXd'),
        ('rapid home insertion', b'abcd\x1b[HX\n', b'Xabcd'),
        ('rapid end insertion', b'abcd\x1b[H\x1b[FX\n', b'abcdX'),
        ('application home/end', b'abcd\x1bOHX\x1bOFY\n', b'XabcdY'),
        ('Ctrl-U', b'garbage\x15help\n', b'help'),
        ('Ctrl-A/K', b'garbage\x01\x0bhelp\n', b'help'),
        ('Ctrl-E', b'abcd\x01\x05X\n', b'abcdX'),
        ('Ctrl-W', b'one two\x17three\n', b'one three'),
        ('Ctrl-B/F', b'abcd\x02\x02X\x06Y\n', b'abXcYd'),
        ('Ctrl-D delete', b'abcd\x02\x04\n', b'abc'),
        ('plain paste', b'help wifi scan\n', b'help wifi scan'),
        ('long repeated deletion', b'x' * 500 + b'\x7f' * 600 + b'help\n', b'help'),
        ('full line then delete', b'x' * 600 + b'\x7f' * 511 + b'help\n', b'help'),
        ('bounded line', b'x' * 600 + b'\n', b'x' * 511),
    ]
    with tempfile.TemporaryDirectory(prefix='highboy-console-') as temporary:
        work = Path(temporary)
        (work / 'sdkconfig.h').write_text('#define CONFIG_IDF_TARGET_LINUX 1\n')
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        command = [*compiler, '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                   '-Wno-sign-compare', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(work), '-I', str(original.parent),
                   str(ROOT / 'tools/console/test_console.c')]
        (work / 'linenoise_under_test.c').write_bytes(source)
        baseline = work / 'baseline'
        subprocess.run([*command, '-o', str(baseline)], check=True)
        _, data, expected = cases[0]
        reproduced = subprocess.run([str(baseline), data.hex(), expected.hex(), 'fast'])
        if reproduced.returncode != 1:
            raise SystemExit('Expected unmodified SDK to reproduce literal DEL insertion')
        (work / 'linenoise_under_test.c').write_bytes(patched)
        fixed = work / 'fixed'
        subprocess.run([*command, '-o', str(fixed)], check=True)
        for speed in ('fast', 'slow'):
            for name, data, expected in cases:
                result = subprocess.run([str(fixed), data.hex(), expected.hex(), speed])
                if result.returncode:
                    raise SystemExit(f'Console regression: {name}, {speed}, exit {result.returncode}')
        # A changed SDK must stop configuration rather than silently bypass the fix.
        try:
            patch_linenoise.patched_source(source + b'\n')
        except ValueError:
            pass
        else:
            raise SystemExit('Modified SDK was accepted')
    print(f'PASS: reproduced SDK rapid-DEL bug; {len(cases) * 2} real-editor cases pass with ASan/UBSan; no UART accessed')


if __name__ == '__main__':
    main()
