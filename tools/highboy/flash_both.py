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

"""Sequential paired update using each target's existing guarded updater."""
import argparse
from pathlib import Path
import subprocess
import sys

from flash_ports import resolve_mode, resolve_port, peer_port


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('app', 'full'))
    parser.add_argument('--p4-port')
    parser.add_argument('--c5-port')
    parser.add_argument('--baud', '-b', type=int)
    parser.add_argument('--read-baud', type=int)
    parser.add_argument('--write-baud', type=int)
    parser.add_argument('--before', choices=('default-reset', 'no-reset'))
    parser.add_argument('--idf-path')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--plain', action='store_true')
    args = parser.parse_args(argv)
    try:
        mode = args.mode or resolve_mode([])[-1]
        p4, c5 = args.p4_port, args.c5_port
        if not p4:
            p4 = peer_port(c5, 'p4') if c5 else resolve_port(['--target', 'p4'])[-1]
        if not c5 and p4:
            c5 = peer_port(p4, 'c5')
        if not p4 or not c5:
            raise ValueError('Cannot identify both interfaces of the same Highboy. '
                             'Specify --p4-port PORT --c5-port PORT.')
        if p4 == c5:
            raise ValueError('P4 and C5 must use different serial ports.')
        common = ['--mode', mode]
        for name in ('baud', 'read_baud', 'write_baud', 'before', 'idf_path'):
            value = getattr(args, name)
            if value is not None:
                common += ['--' + name.replace('_', '-'), str(value)]
        if args.check:
            common += ['--check']
        if args.plain:
            common += ['--plain']
        launcher = str(Path(__file__).with_name('flash.command'))
        commands = [(target, ['bash', launcher, target, '-p', port, *common] +
                     (['--control-port', p4] if target == 'c5' else []))
                    for target, port in (('c5', c5), ('p4', p4))]
        # Validate both artifacts before the first serial port is opened.
        for target, command in commands:
            result = subprocess.run([*command, '--plain', '--validate-build'])
            if result.returncode:
                print(f'{target.upper()} build preflight failed; neither chip was flashed.', file=sys.stderr)
                return result.returncode
        completed = []
        for target, command in commands:
            print(f'Both chips: {target.upper()} ({len(completed)+1}/2)', flush=True)
            result = subprocess.run(command)
            if result.returncode:
                print(f'Stopped at {target.upper()}. Completed: {", ".join(completed) or "none"}. '
                      'The two-chip update is not atomic; later targets were not started.', file=sys.stderr)
                return result.returncode
            completed.append(target.upper())
        print('Both chips checked.' if args.check else 'Both chips updated and reset.', flush=True)
        return 0
    except (ValueError, ImportError, OSError) as error:
        print(f'Paired flash stopped: {error}', file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print('Paired flash cancelled; no later target will be started.', file=sys.stderr)
        return 130


if __name__ == '__main__':
    sys.exit(main())
