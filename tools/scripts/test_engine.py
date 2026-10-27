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

"""Run the real vendored interpreters and production adapter on the host."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'firmware_p4/components/Service/script_engine'
JS = ROOT / 'firmware_p4/components/Service/script_engine/mquickjs'
LUA = ROOT / 'firmware_p4/components/Service/script_engine/lua/src'
LUA_SOURCES = '''lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes
lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib ltablib lstrlib
lmathlib lutf8lib'''.split()


def verify_dependencies():
    for vendor in (JS, LUA.parent):
        proof = json.loads((vendor / 'provenance.json').read_text())
        patches = proof.get('local_patches', {})
        for name, original in proof['files'].items():
            expected = patches.get(name, {}).get('after_sha256', original)
            if name in patches and patches[name]['before_sha256'] != original:
                raise RuntimeError('Invalid dependency patch chain: ' + name)
            if hashlib.sha256((vendor / name).read_bytes()).hexdigest() != expected:
                raise RuntimeError('Dependency changed without review: ' + str(vendor / name))


def main():
    verify_dependencies()
    with tempfile.TemporaryDirectory(prefix='highboy-script-engine-') as tmp:
        directory = Path(tmp)
        (directory / 'esp_log.h').write_text(
            '#define ESP_LOGD(tag, ...) ((void)(tag))\n#define ESP_LOGE(tag, ...) ((void)(tag))\n')
        executable = directory / 'test'
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=gnu11', '-O1',
                   '-D_GNU_SOURCE', '-DLUAI_MAXCCALLS=32', '-include', 'stddef.h', '-fno-math-errno', '-fno-trapping-math',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(directory), '-I', str(ENGINE / 'include'),
                   '-I', str(JS), '-I', str(JS / 'generated'), '-I', str(LUA),
                   str(ENGINE / 'script_engine.c'), str(ROOT / 'tools/scripts/tests/test_engine.c')]
        command += ['-I', str(ROOT / 'firmware_p4/components/Service/app_runtime/include'),
                    '-I', str(ROOT / 'tools/dependencies/build/managed_components/espressif__cjson/cJSON'),
                    str(ROOT / 'tools/dependencies/build/managed_components/espressif__cjson/cJSON/cJSON.c'),
                    str(ENGINE / 'script_bindings.c')]
        command += [str(JS / (name + '.c')) for name in ('mquickjs', 'dtoa', 'libm', 'cutils')]
        command += [str(LUA / (name + '.c')) for name in LUA_SOURCES]
        subprocess.run([*command, '-lm', '-o', str(executable)], check=True, timeout=120)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == '__main__':
    main()
