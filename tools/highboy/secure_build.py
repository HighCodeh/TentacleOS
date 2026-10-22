#!/usr/bin/env python3
"""Build/audit an isolated factory-security review. No signing, serial or fuse actions."""
import argparse
import csv
import struct
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import package as app_package
import recovery
import security
import verify_package

ROOT = security.ROOT
SECTOR = 4096
BOOT_OFFSET = 0x2000
FACTORY_TABLE_OFFSET = 0x10000
FILES = {'app': 'app.bin', 'bootloader': 'bootloader.bin', 'partitions': 'partition-table.bin',
         'config': 'sdkconfig', 'baseline': 'baseline-sdkconfig'}
BOUNDARY = {'schema_version': 1, 'status': 'unsigned_factory_security_review',
            'deployment_approved': False, 'factory_metadata_migration_required': True,
            'production_keys_provisioned': False, 'efuses_written': False,
            'hardware_validated': False, 'normal_app_updater_supported': False,
            'preserves_plaintext_nvs_in_place': False}
require = verify_package.require


def digest(data):
    return hashlib.sha256(data).hexdigest()


def signature_budget(unsigned_bytes, capacity, alignment):
    require(type(unsigned_bytes) is int and unsigned_bytes > 0, 'Empty image')
    require(unsigned_bytes % alignment == 0, 'Missing secure image padding')
    signed = unsigned_bytes + SECTOR
    require(signed <= capacity, 'Image plus signature sector exceeds its region')
    return {'unsigned_bytes': unsigned_bytes, 'signed_bytes': signed,
            'capacity_bytes': capacity, 'signed_free_bytes': capacity - signed}


def partition_table(path):
    types = {'app': 0, 'data': 1}
    subtypes = {'ota_0': 0x10, 'ota_1': 0x11, 'ota': 0, 'phy': 1,
                'nvs': 2, 'coredump': 3, 'nvs_keys': 4, 'littlefs': 0x83}
    entries, data = {}, bytearray()
    end = 0
    def number(text):
        return int(text[:-1], 0) * {'K': 1024, 'M': 1024*1024}[text[-1]] if text[-1:] in ('K', 'M') else int(text, 0)
    for row in csv.reader(line for line in Path(path).read_text().splitlines() if line.strip() and not line.lstrip().startswith('#')):
        row = [field.strip() for field in row]
        row += [''] * (6 - len(row))
        name, kind, subtype, offset, size, flags = row
        require(name not in entries and len(name.encode()) < 16 and flags in ('', 'encrypted'), 'Invalid partition definition')
        entry = (types[kind], subtypes[subtype], number(offset), number(size), 1 if flags else 0)
        require(entry[2] >= end and entry[2] % SECTOR == 0 and entry[3] > 0 and
                entry[3] % SECTOR == 0 and entry[2] + entry[3] <= 0x800000,
                'Overlapping, unordered or unaligned partition: '+name)
        end = entry[2] + entry[3]
        entries[name] = entry
        data += struct.pack('<2sBBII16sI', b'\xaa\x50', *entry[:4], name.encode(), entry[4])
    data += b'\xeb\xeb' + b'\xff'*14 + hashlib.md5(data).digest()
    require(len(data) <= 0xc00, 'Partition table too large')
    return bytes(data).ljust(0xc00, b'\xff'), entries


def audit_artifacts(target, config_path, baseline_path, images):
    require(target in security.VERSIONS, 'Unknown target')
    config = security.config_values(config_path)
    baseline = security.config_values(baseline_path)
    security.audit(target, config, baseline, factory=True)
    require(int(config.get('BOOTLOADER_OFFSET_IN_FLASH', '0'), 0) == BOOT_OFFSET, 'Wrong bootloader offset')
    require(int(config.get('MMU_PAGE_SIZE', '0'), 0) == 65536, 'Unexpected MMU page size')
    require(config.get('COMPILER_OPTIMIZATION_ASSERTION_LEVEL') in ('1', '2'), 'Runtime assertions disabled')
    chip = target.upper()
    for name in ('app', 'bootloader'):
        recovery.verify_image(images[name], chip)
    expected, by_name = partition_table(ROOT/'integration/security'/f'partitions-{target}.csv')
    require(images['partitions'] == expected, 'Factory partition map differs from reviewed source')
    _, installed = partition_table(ROOT/f'firmware_{target}/partitions.csv')
    for name, original in installed.items():
        actual = by_name.get(name)
        require(actual is not None and actual[:2] == original[:2] and actual[3] == original[3],
                'Partition type/size changed: ' + name)
        if name not in ('nvs', 'otadata', 'phy_init'):
            require(actual[2] == original[2], 'App/data offset changed: ' + name)
    for name, offset, size in (('nvs', 0x11000, 0x6000), ('otadata', 0x17000, 0x2000),
                               ('phy_init', 0x19000, 0x1000), ('nvs_keys', 0x1a000, 0x1000)):
        require(by_name[name][2:4] == (offset, size), 'Wrong factory metadata region: '+name)
    require(all(by_name[name][4] == 1 for name in ('assets', 'coredump', 'nvs_keys')),
            'Data or NVS key partition lacks encryption')
    require(by_name['nvs'][4] == 0, 'NVS must use its own encrypted storage scheme')
    return {'target': target, 'idf_version': security.VERSIONS[target],
            'app': signature_budget(len(images['app']), verify_package.PROFILE[chip]['slot_bytes'], 65536),
            'bootloader': signature_budget(len(images['bootloader']), FACTORY_TABLE_OFFSET - BOOT_OFFSET, SECTOR),
            'partition_table_offset': FACTORY_TABLE_OFFSET, 'bootloader_offset': BOOT_OFFSET,
            'files': {name: {'file': FILES[name], 'sha256': digest(data), 'bytes': len(data)}
                      for name, data in images.items()}}


def read_inputs(directory):
    result = {}
    for name, filename in FILES.items():
        limit = 4 * 1024 * 1024 if name == 'app' else 1024 * 1024
        result[name] = recovery.read_regular(Path(directory)/filename, limit)
    return result


def verify(directory):
    directory = Path(directory)
    report = recovery.read_json(directory/'build.json')
    require(isinstance(report, dict), 'Invalid build report')
    for name, expected in BOUNDARY.items():
        require(report.get(name) == expected and type(report.get(name)) is type(expected),
                'Invalid factory review boundary: ' + name)
    inputs = read_inputs(directory)
    actual = audit_artifacts(report.get('target'), directory/FILES['config'],
                             directory/FILES['baseline'], inputs)
    for name, expected in actual.items():
        require(report.get(name) == expected, 'Factory review content mismatch: ' + name)
    return report


def outside_checkout(path):
    path = Path(path).resolve()
    require(not path.is_relative_to(ROOT), 'Security work/output must stay outside the checkout')
    return path


def verify_sdk(idf, target):
    common = ['components/lwip/lwip', 'components/mbedtls/mbedtls', 'components/heap/tlsf',
              'components/bootloader/subproject/components/micro-ecc/micro-ecc',
              'components/spiffs/spiffs', 'components/unity/unity', 'components/cmock/CMock',
              'components/protobuf-c/protobuf-c']
    if target == 'c5':
        common += ['components/esp_wifi/lib', 'components/esp_phy/lib', 'components/esp_coex/lib',
                   'components/bt/controller/lib_esp32c5/esp32c5-bt-lib', 'components/bt/host/nimble/nimble']
    status = subprocess.check_output(['git', '-C', str(idf), 'submodule', 'status', '--', *common], text=True).splitlines()
    require(len(status) == len(common) and all(line.startswith(' ') for line in status),
            'SDK submodules are missing or differ from pinned revisions')
    require(subprocess.run(['git', '-C', str(idf), 'diff', '--quiet', 'HEAD']).returncode == 0,
            'SDK has tracked source modifications')


def build(args):
    target = args.target
    require(target == 'p4' or not (args.app_production or args.app_public_key),
            'Native app trust configuration belongs to P4')
    output = outside_checkout(args.out)
    require(not output.exists(), 'Output already exists')
    work = output/'work'
    require(not work.is_symlink(), 'Symlinked build directory')
    baseline = Path(args.baseline).resolve()
    idf = Path(args.idf).resolve()
    python = Path(args.python).absolute()
    version = subprocess.check_output(['git', '-C', str(idf), 'describe', '--tags', '--exact-match'], text=True).strip()
    require(version == security.VERSIONS[target], 'Wrong SDK lane')
    verify_sdk(idf, target)
    require(python.is_file() and (Path(args.tools)/'tools').is_dir(), 'Missing installed Python/toolchain')
    output.mkdir(mode=0o700, parents=True)
    report = {'status': 'building', 'target': target, 'deployment_approved': False}
    (output/'build.json').write_text(json.dumps(report, indent=2)+'\n')
    try:
        security.resolve(target, idf, python, baseline, output/'configuration', factory=True)
        environment = dict(os.environ, IDF_PATH=str(idf), IDF_TOOLS_PATH=str(Path(args.tools).resolve()),
                           IDF_PYTHON_ENV_PATH=str(python.parent.parent), IDF_TARGET='esp32'+target,
                           IDF_COMPONENT_MANAGER='0', PIP_NO_INDEX='1', PYTHONDONTWRITEBYTECODE='1',
                           CCACHE_DISABLE='1', IDF_SKIP_CHECK_SUBMODULES='1')
        for name in ('SDKCONFIG', 'SDKCONFIG_DEFAULTS', 'EXTRA_COMPONENT_DIRS', 'EXTRA_CFLAGS',
                     'EXTRA_CXXFLAGS', 'EXTRA_CPPFLAGS', 'EXTRA_LDFLAGS', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS',
                     'CMAKE_TOOLCHAIN_FILE'):
            environment.pop(name, None)
        work.mkdir(mode=0o700, parents=True, exist_ok=True)
        generated = output/'generated-sdkconfig'
        defaults = output/'configuration/sdkconfig'
        command = [str(python), str(idf/'tools/idf.py'), '-C', str(ROOT/f'firmware_{target}'),
                   '-B', str(work), '-D', 'SDKCONFIG='+str(generated), '-D', 'SDKCONFIG_DEFAULTS='+str(defaults),
                   '-D', 'HIGH_BOY_USB_VOICE=OFF']
        if target == 'p4':
            command += ['-D', 'TOS_APP_PRODUCTION='+('ON' if args.app_production else 'OFF'),
                        '-D', 'TOS_APP_TRUST_PUBLIC_KEY='+(args.app_public_key or '')]
        # Positional shell parameters keep toolchain paths out of executable shell text.
        script = 'set -euo pipefail; source "$1/export.sh" >/dev/null; shift; exec "$@"'
        subprocess.run(['bash', '-c', script, 'secure-build', str(idf), *command, 'reconfigure'],
                       cwd=ROOT, env=environment, check=True, timeout=300)
        subprocess.run(['bash', '-c', script, 'secure-build', str(idf), 'cmake', '--build', str(work),
                        '--target', 'app', 'bootloader', '--parallel', str(args.jobs)],
                       cwd=ROOT, env=environment, check=True, timeout=1800)
        sources = {'app': work/f'TentacleOS_{target.upper()}.bin',
                   'bootloader': work/'bootloader/bootloader.bin',
                   'partitions': work/'partition_table/partition-table.bin',
                   'config': generated, 'baseline': baseline/'sdkconfig'}
        for name, source in sources.items():
            shutil.copyfile(source, output/FILES[name])
        report = dict(BOUNDARY, **audit_artifacts(target, output/FILES['config'], output/FILES['baseline'], read_inputs(output)))
        report['source_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        report['source_dirty'] = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT))
        report['signing'] = 'external_required'
        if target == 'p4':
            report['app_signing'] = app_package.app_signing_configuration(ROOT/'firmware_p4', work)
        (output/'build.json').write_text(json.dumps(report, indent=2)+'\n')
        verify(output)
    except Exception as error:
        (output/'build.json').write_text(json.dumps({'status': 'failed', 'target': target,
            'deployment_approved': False, 'error': str(error)}, indent=2)+'\n')
        raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    compile_parser = commands.add_parser('build')
    compile_parser.add_argument('--target', choices=security.VERSIONS, required=True)
    for name in ('idf', 'python', 'tools', 'baseline', 'out'):
        compile_parser.add_argument('--'+name, type=Path, required=True)
    compile_parser.add_argument('--app-production', action='store_true', help='P4: require explicit production .hb trust identity')
    compile_parser.add_argument('--app-public-key', help='P4: explicit public Ed25519 key, 64 hex characters')
    compile_parser.add_argument('--jobs', type=int, default=2)
    check = commands.add_parser('verify'); check.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'build':
            require(1 <= args.jobs <= 8, 'Invalid job count')
            result = build(args)
            print('Unsigned factory-security build audited:', result)
        else:
            report = verify(args.directory)
            print('Verified factory-security review:', report['target'])
        print('No signature identity, encryption-key custody, device provisioning or deployment approval implied.')
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        raise SystemExit('Factory-security review failed: '+str(error)) from error


if __name__ == '__main__':
    main()
