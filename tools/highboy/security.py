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

"""Resolve and audit a review-only security configuration; no serial or key operations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import verify_package

ROOT = Path(__file__).resolve().parents[2]
VERSIONS = {'p4': 'v6.1', 'c5': 'v6.1'}
REQUIRED = {
    'SECURE_BOOT': 'y', 'SECURE_BOOT_V2_ENABLED': 'y',
    'SECURE_SIGNED_APPS_RSA_SCHEME': 'y', 'SECURE_SIGNED_ON_BOOT': 'y',
    'SECURE_SIGNED_ON_UPDATE': 'y', 'SECURE_FLASH_ENC_ENABLED': 'y',
    'SECURE_FLASH_ENCRYPTION_AES128': 'y', 'SECURE_FLASH_ENCRYPTION_MODE_RELEASE': 'y',
    'SECURE_FLASH_CHECK_ENC_EN_IN_APP': 'y', 'SECURE_ENABLE_SECURE_ROM_DL_MODE': 'y',
}
FORBIDDEN = ('SECURE_BOOT_BUILD_SIGNED_BINARIES', 'SECURE_BOOT_FLASH_BOOTLOADER_DEFAULT',
             'SECURE_BOOT_INSECURE', 'SECURE_BOOT_ALLOW_JTAG', 'SECURE_BOOT_ALLOW_SHORT_APP_PARTITION',
             'SECURE_BOOT_ENABLE_AGGRESSIVE_KEY_REVOKE', 'SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS',
             'SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT', 'EFUSE_VIRTUAL', 'EFUSE_VIRTUAL_KEEP_IN_FLASH',
             'SECURE_INSECURE_ALLOW_DL_MODE', 'SECURE_BOOT_ALLOW_UNUSED_DIGEST_SLOTS',
             'SECURE_FLASH_UART_BOOTLOADER_ALLOW_ENC', 'SECURE_FLASH_UART_BOOTLOADER_ALLOW_DEC',
             'SECURE_FLASH_UART_BOOTLOADER_ALLOW_CACHE', 'SECURE_FLASH_SKIP_WRITE_PROTECTION_CACHE',
             'BOOTLOADER_SKIP_VALIDATE_ALWAYS', 'BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON',
             'BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP', 'NVS_COMPATIBLE_PRE_V4_3_ENCRYPTION_FLAG')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def config_values(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            name, value = line.split('=', 1)
            name = name.removeprefix('CONFIG_')
            if name in result and result[name] != value:
                raise ValueError('Duplicate config field: ' + name)
            result[name] = value
    return result


def audit(target, config, baseline=None, factory=False):
    if target not in VERSIONS:
        raise ValueError('Unsupported target')
    if config.get('IDF_TARGET') != json.dumps('esp32' + target):
        raise ValueError('Wrong configuration target')
    for name, expected in REQUIRED.items():
        if config.get(name) != expected:
            raise ValueError('Missing security requirement: ' + name)
    for name in FORBIDDEN:
        if config.get(name, 'n') != 'n':
            raise ValueError('Disallowed security setting: ' + name)
    if config.get('SECURE_BOOT_SIGNING_KEY', '""') not in ('""', 'n'):
        raise ValueError('External signing profile must not select a private key')
    if factory:
        expected = {'PARTITION_TABLE_OFFSET': '0x10000',
                    'PARTITION_TABLE_CUSTOM_FILENAME': json.dumps('../integration/security/partitions-' + target + '.csv'),
                    'NVS_ENCRYPTION': 'y', 'NVS_SEC_KEY_PROTECT_USING_FLASH_ENC': 'y'}
        for name, value in expected.items():
            if config.get(name) != value:
                raise ValueError('Factory profile mismatch: ' + name)
    if baseline is not None:
        # C5 retains its explicit memory overlay; P4 layout is checked below.
        if target == 'c5':
            overlay = ROOT / 'firmware_c5/sdkconfig.baseline'
            for name in config_values(overlay):
                if factory and name == 'PARTITION_TABLE_OFFSET':
                    continue
                if config.get(name) != baseline.get(name):
                    raise ValueError('Board overlay changed: ' + name)
        for name in ('PARTITION_TABLE_OFFSET', 'PARTITION_TABLE_CUSTOM_FILENAME',
                     'ESPTOOLPY_FLASHSIZE', 'ESP_REV_MIN_FULL', 'BOOTLOADER_OFFSET_IN_FLASH',
                     'MMU_PAGE_SIZE', 'MMU_PAGE_MODE'):
            if factory and name in ('PARTITION_TABLE_OFFSET', 'PARTITION_TABLE_CUSTOM_FILENAME'):
                continue
            if config.get(name) != baseline.get(name):
                raise ValueError('Installed profile changed: ' + name)
    return {'target': target, 'idf_version': VERSIONS[target],
            'status': 'resolved_security_configuration_requires_signing_and_provisioning_review',
            'secure_boot_scheme': 'RSA-3072', 'flash_encryption': 'XTS-AES-128',
            'signing': 'external_required', 'production_keys_provisioned': False,
            'efuses_written': False, 'hardware_validated': False,
            'normal_app_updater_supported': False, 'factory_metadata_migration_required': factory}


def resolve(target, idf, python, build, output, factory=False):
    idf, build, output = Path(idf).resolve(), Path(build).resolve(), Path(output).absolute()
    version = subprocess.check_output(['git', '-C', str(idf), 'describe', '--tags', '--exact-match'], text=True).strip()
    if version != VERSIONS[target]:
        raise ValueError('Wrong SDK lane: ' + version)
    baseline = config_values(build / 'sdkconfig')
    if baseline.get('IDF_TARGET') != json.dumps('esp32' + target):
        raise ValueError('Wrong baseline target')
    environment = json.loads((build / 'config.env').read_text())
    if Path(environment.get('IDF_PATH', '')).resolve() != idf:
        raise ValueError('Baseline references another SDK')
    if output.exists() or output.is_symlink():
        raise ValueError('Output already exists; refusing to overwrite')
    output.parent.mkdir(parents=True, exist_ok=True)
    profile = ROOT / 'integration/security' / (target + '.defaults')
    with tempfile.TemporaryDirectory(prefix='.security-', dir=output.parent) as temp:
        staging = Path(temp) / 'review'
        staging.mkdir(mode=0o700)
        config = staging / 'sdkconfig'
        env = dict(os.environ, IDF_PATH=str(idf))
        command = [str(python), '-m', 'kconfgen', '--list-separator=semicolon',
                   '--kconfig', str(idf / 'Kconfig'), '--sdkconfig-rename', str(idf / 'sdkconfig.rename'),
                   '--config', str(config), '--defaults', str(build / 'sdkconfig'),
                   '--defaults', str(profile), '--env-file', str(build / 'config.env'),
                   '--env', 'IDF_TARGET=esp32' + target, '--env', 'IDF_TOOLCHAIN=gcc',
                   '--env', 'IDF_ENV_FPGA=', '--env', 'IDF_INIT_VERSION=' + version[1:],
                   '--env', 'IDF_MINIMAL_BUILD=n', '--dont-write-deprecated', '--output', 'config', str(config)]
        if factory:
            command.extend(['--defaults', str(ROOT / 'integration/security' / (target + '.factory.defaults'))])
        subprocess.run(command, cwd=ROOT, env=env, check=True, timeout=120)
        report = audit(target, config_values(config), baseline, factory)
        image = build / ('TentacleOS_' + target.upper() + '.bin')
        if image.is_file():
            # Baseline-only projection; the secure configuration must still be built.
            page = 65536
            sector = 4096
            projected = ((image.stat().st_size + page - 1) // page) * page + sector
            slot = verify_package.PROFILE[target.upper()]['slot_bytes']
            report['baseline_size_projection'] = {'unsigned_bytes': image.stat().st_size,
                'unsigned_sha256': digest(image), 'mmu_page_bytes': page,
                'signed_padded_bytes': projected, 'slot_bytes': slot,
                'over_slot_bytes': max(0, projected - slot), 'secure_build_validated': False}
        if factory:
            report['factory_profile_sha256'] = digest(ROOT / 'integration/security' / (target + '.factory.defaults'))
        report.update(profile_sha256=digest(profile), sdkconfig_sha256=digest(config),
                      baseline_sdkconfig_sha256=digest(build / 'sdkconfig'))
        (staging / 'review.json').write_text(json.dumps(report, indent=2) + '\n')
        staging.rename(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=VERSIONS, required=True)
    parser.add_argument('--idf', type=Path, required=True)
    parser.add_argument('--python', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True, help='Existing matching development build, read only')
    parser.add_argument('--out', type=Path, required=True, help='New isolated review directory')
    parser.add_argument('--factory-review', action='store_true', help='Resolve the explicit relocated-metadata factory profile; never flash it')
    args = parser.parse_args()
    try:
        result = resolve(args.target, args.idf, args.python, args.build, args.out, args.factory_review)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit('Security preparation failed: ' + str(error)) from error
    print('Security configuration resolved and audited:', result)
    print('Review only: no keys generated/read, firmware compiled/signed, serial access or eFuse operations.')


if __name__ == '__main__':
    main()
