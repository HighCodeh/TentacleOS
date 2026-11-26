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

"""Exercise generated SDK write code and reject unreviewed override inputs."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import prepare

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--idf', type=Path, default=os.environ.get('IDF_PATH'))
args = parser.parse_args()
if args.idf is None:
    parser.error('--idf or IDF_PATH is required')
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='sdmmc-check-') as temporary:
    work = Path(temporary)
    out = work / 'build/components/Drivers/sdmmc'
    prepare.prepare(args.idf, out)
    original = (args.idf / 'components/sdmmc/sdmmc_cmd.c').read_bytes()
    stamp = (out / 'sdmmc_cmd.c').stat().st_mtime_ns
    prepare.prepare(args.idf, out)
    assert (out / 'sdmmc_cmd.c').stat().st_mtime_ns == stamp
    def run_code(source, suffix):
        start = source.index('esp_err_t sdmmc_write_sectors_dma(')
        end = source.index('\nesp_err_t sdmmc_read_sectors(', start)
        # Preserve upstream licensing on this generated excerpt.
        notice = source[:source.index('#include')]
        (work / 'write_function.inc').write_text(notice + source[start:end])
        binary = work / suffix
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined,bounds', '-fno-sanitize-recover=all',
                        '-I', str(work), str(root / 'tools/sdmmc/test_write.c'),
                        '-o', str(binary)], check=True)
        return subprocess.run([str(binary)], capture_output=True, text=True)
    result = run_code((out / 'sdmmc_cmd.c').read_text(), 'patched')
    assert result.returncode == 0, result.stderr
    print(result.stdout.strip())
    assert run_code(original.decode(), 'upstream').returncode != 0, 'Regression test must reject the original bug'
    assert (args.idf / 'components/sdmmc/sdmmc_cmd.c').read_bytes() == original
    for item in ('patch', 'source', 'version'):
        fake = work / item
        shutil.copytree(prepare.PATCH_DIR, fake)
        if item == 'patch':
            with (fake / 'write_errors.patch').open('a') as f:
                f.write('\nmodified\n')
        else:
            manifest = json.loads((fake / 'patch_manifest.json').read_text())
            if item == 'source':
                manifest['files']['sdmmc_cmd.c'] = '0' * 64
            else:
                manifest['sdk_version_sha256'] = '0' * 64
            (fake / 'patch_manifest.json').write_text(json.dumps(manifest))
        try:
            prepare.prepare(args.idf, out, fake)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted tampered ' + item)
    (out / 'sdmmc_cmd.c').write_text('stale generated output')
    prepare.prepare(args.idf, out)
    assert 'sdmmc_write_sectors_dma' in (out / 'sdmmc_cmd.c').read_text()
print('PASS: original bug reproduced; source/patch/version guards, repeat generation and output repair; SDK untouched')
