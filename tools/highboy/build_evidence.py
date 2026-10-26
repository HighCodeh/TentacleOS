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

"""Audit linked internal RAM and compiler policy for an app-only High Boy build."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

import check_contracts
import verify_package

ROOT = Path(__file__).resolve().parents[2]
MIN_LINKER_FREE = {'P4': 160 * 1024, 'C5': 60 * 1024}
POLICIES = {
    'P4': {
        'components/Drivers/cc1101/cc1101.c': ('-O2', False),
        'components/Drivers/audio_i2s/audio_i2s.c': ('-O2', False),
        'components/Drivers/spi_bridge_phy/spi_bridge_phy.c': ('-O2', False),
        'components/Applications/doom/d_main.c': ('-O2', False),
        'components/Applications/gameboy/gb_highboy.c': ('-O2', False),
        'managed_components/lvgl__lvgl/src/draw/sw/blend/lv_draw_sw_blend.c': ('-O2', True),
        'managed_components/espressif__libsodium/libsodium/src/libsodium/sodium/utils.c': ('-Os', False),
        'components/Service/app_runtime/tos_app_mgr.c': ('-Os', True),
    },
    'C5': {
        'components/Drivers/spi_slave/spi_slave_driver.c': ('-Og', False),
        'components/Service/spi_bridge/hb_muse_c5_service.c': ('-Og', False),
        'components/Service/wifi/wifi_service.c': ('-Og', False),
    },
}
require = verify_package.require


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def normalize_memory(chip, raw):
    require(chip in POLICIES and isinstance(raw, dict), 'Invalid SDK memory report')
    if chip == 'P4' and 'diram_total' in raw:
        require({'diram_total', 'used_diram', 'diram_remain'} <= raw.keys(), 'Missing DIRAM counts')
        regions = [{'name': 'DIRAM', 'total_bytes': raw['diram_total'],
                    'used_bytes': raw['used_diram'], 'linker_free_bytes': raw['diram_remain']}]
    else:
        require(raw.get('version') == '1.2', 'Unsupported SDK size-report schema')
        require(isinstance(raw.get('layout'), list) and all(isinstance(item, dict) and
                {'name', 'total', 'used', 'free'} <= item.keys() for item in raw['layout']),
                'Invalid C5 memory layout')
        regions = [{'name': item['name'], 'total_bytes': item['total'],
                    'used_bytes': item['used'], 'linker_free_bytes': item['free']}
                   for item in raw['layout'] if item['name'] == ('DIRAM' if chip == 'P4' else 'HP SRAM')]
    require(len(regions) == 1, 'Missing or duplicate main internal RAM region')
    memory = regions[0]
    for name in ('total_bytes', 'used_bytes', 'linker_free_bytes'):
        require(type(memory[name]) is int and memory[name] >= 0, 'Invalid memory count: ' + name)
    require(memory['total_bytes'] > 0 and
            memory['used_bytes'] + memory['linker_free_bytes'] == memory['total_bytes'],
            'Inconsistent internal RAM accounting')
    require(memory['linker_free_bytes'] >= MIN_LINKER_FREE[chip],
            chip + ' internal RAM regression: linker headroom below reviewed floor')
    return {**memory, 'minimum_linker_free_bytes': MIN_LINKER_FREE[chip],
            'runtime_heap_measured': False, 'runtime_stack_measured': False}


def compiler_policies(chip, commands):
    result = {}
    for suffix, (optimization, lto) in POLICIES[chip].items():
        matches = [entry for entry in commands if
                   entry['file'].endswith('/firmware_' + chip.lower() + '/' + suffix)]
        require(len(matches) == 1, 'Missing or duplicate compile entry: ' + suffix)
        entry = matches[0]
        args = entry.get('arguments') or shlex.split(entry['command'])
        opts = [arg for arg in args if re.fullmatch(r'-O(?:[0-3gsz]|fast)', arg)]
        lto_opts = [arg for arg in args if arg == '-fno-lto' or re.fullmatch(r'-flto(?:=.*)?', arg)]
        require(opts and opts[-1] == optimization, 'Compiler optimization changed: ' + suffix)
        has_lto = bool(lto_opts and lto_opts[-1] != '-fno-lto')
        require(has_lto is lto, 'Compiler LTO boundary changed: ' + suffix)
        result[suffix] = {'optimization': opts[-1], 'lto': has_lto}
    return result


def validate(report, target=None):
    require(isinstance(report, dict) and type(report.get('schema_version')) is int and
            report['schema_version'] == 1, 'Invalid build-evidence schema')
    chip = report.get('target')
    require(chip in POLICIES, 'Unknown evidence target')
    require(report.get('idf_version') == verify_package.PROFILE[chip]['idf_version'], 'Wrong evidence SDK')
    for field in ('hardware_validated', 'runtime_performance_measured', 'efuses_written'):
        require(report.get(field) is False, 'Static build cannot certify ' + field)
    require(report.get('status') == 'static_build_evidence_only', 'Wrong build-evidence status')
    require(report.get('memory') == normalize_memory(chip, report.get('sdk_size_report')),
            'Memory summary differs from SDK evidence')
    expected = {name: {'optimization': policy[0], 'lto': policy[1]}
                for name, policy in POLICIES[chip].items()}
    require(report.get('compiler_policies') == expected, 'Unreviewed compiler policy')
    for field in ('sha256', 'elf_sha256', 'map_sha256', 'sdkconfig_sha256',
                  'compile_commands_sha256', 'partition_table_sha256'):
        value = report.get(field)
        require(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value), 'Invalid digest: ' + field)
        if target is not None and field != 'map_sha256':
            require(value == target.get(field), 'Candidate/evidence mismatch: ' + field)
    require(type(report.get('bytes')) is int and 0 < report['bytes'] <=
            verify_package.PROFILE[chip]['slot_bytes'], 'Evidence app exceeds slot')
    require(report['partition_table_sha256'] == verify_package.PROFILE[chip]['partition_table_sha256'],
            'Evidence partition migration')
    if target is not None:
        require(report['bytes'] == target['bytes'], 'Evidence app size mismatch')


def collect(chip, build):
    chip, build = chip.upper(), Path(build).resolve()
    require(chip in POLICIES, 'Unknown target')
    project_name = 'TentacleOS_' + chip
    description = json.loads((build / 'project_description.json').read_text())
    require(description['target'] == 'esp32' + chip.lower() and
            description['project_name'] == project_name, 'Wrong build target')
    sdk = Path(description['idf_path'])
    version = subprocess.check_output(['git', '-C', str(sdk), 'describe', '--tags', '--exact-match'],
                                      text=True).strip()
    require(version == verify_package.PROFILE[chip]['idf_version'], 'Wrong SDK lane')
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    python = cache.get('PYTHON')
    require(python and Path(python).is_file(), 'Missing configured SDK Python')
    config = check_contracts.config_values([build / 'sdkconfig'])
    require(config.get('CONFIG_IDF_TARGET') == json.dumps('esp32' + chip.lower()), 'Wrong sdkconfig target')
    for name in ('SECURE_BOOT', 'SECURE_FLASH_ENC_ENABLED', 'BOOTLOADER_APP_ANTI_ROLLBACK'):
        require(config.get('CONFIG_' + name) != 'y', 'Unexpected security migration: ' + name)
    require(config.get('CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE') == 'y', 'OTA rollback is disabled')
    require(config.get('CONFIG_PARTITION_TABLE_OFFSET') == '0x8000', 'Unexpected partition offset')
    if chip == 'C5':
        for key, value in check_contracts.C5_MEMORY.items():
            require(config.get(key) == value, 'C5 memory overlay changed: ' + key)
    paths = {'sha256': build / (project_name + '.bin'), 'elf_sha256': build / (project_name + '.elf'),
             'map_sha256': build / (project_name + '.map'), 'sdkconfig_sha256': build / 'sdkconfig',
             'compile_commands_sha256': build / 'compile_commands.json',
             'partition_table_sha256': build / 'partition_table/partition-table.bin'}
    before = {key: digest(path) for key, path in paths.items()}
    command = [str(python), '-m', 'esp_idf_size', '--format', 'json2']
    raw = json.loads(subprocess.check_output(command + [str(paths['map_sha256'])], text=True))
    commands = json.loads(paths['compile_commands_sha256'].read_text())
    report = {'schema_version': 1, 'target': chip, 'idf_version': version,
              'status': 'static_build_evidence_only', 'hardware_validated': False,
              'runtime_performance_measured': False, 'efuses_written': False,
              'bytes': paths['sha256'].stat().st_size, **before,
              'sdk_size_report': raw, 'memory': normalize_memory(chip, raw),
              'compiler_policies': compiler_policies(chip, commands)}
    require(before == {key: digest(path) for key, path in paths.items()}, 'Build changed during audit')
    validate(report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=('p4', 'c5'), required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = collect(args.target, args.build)
        with args.out.open('x') as stream:
            stream.write(json.dumps(report, indent=2) + '\n')
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        raise SystemExit('Build evidence failed: ' + str(error)) from error
    print(json.dumps(report['memory'], indent=2))


if __name__ == '__main__':
    main()
