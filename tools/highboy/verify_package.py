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

"""Verify an app-only candidate's profile, lengths and hashes without device access."""
import argparse
import hashlib
import json
from pathlib import Path
import re

PROFILE = {
    'P4': {'file': 'TentacleOS_P4.bin', 'slot_bytes': 0x2d0000, 'idf_version': 'v5.5.3',
           'partition_table_sha256': 'bb76257d010de4925ddf8925b46a2f89e0512a2a858e0c4939d705c74a1e51bd'},
    'C5': {'file': 'TentacleOS_C5.bin', 'slot_bytes': 0x200000, 'idf_version': 'v6.0.1',
           'partition_table_sha256': '236a9c2e19bf6373b94589e0683c3a588523c026a1a21dae141ed2deda97c7a1'},
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f'Duplicate manifest field: {key}')
        result[key] = value
    return result


def validate_report(report):
    require(isinstance(report, dict), 'Invalid manifest root')
    require(type(report.get('schema_version')) is int and report['schema_version'] == 1,
            'Unsupported manifest schema')
    require(report.get('status') == 'compiled_candidate_requires_hardware_acceptance', 'Wrong candidate status')
    require(type(report.get('protocol_version')) is int and report['protocol_version'] == 8,
            'Wrong paired protocol')
    for field, value in {'app_only': True, 'flashed': False, 'assets_rebuilt': False,
                         'runtime_validated': False, 'partition_migration': False,
                         'host_tests_passed': True}.items():
        require(report.get(field) is value, f'Invalid candidate boundary: {field}')
    signing = report.get('app_signing')
    require(isinstance(signing, dict), 'Missing app-signing configuration')
    require(type(signing.get('production_mode')) is bool and type(signing.get('custom_public_key')) is bool,
            'Invalid app-signing configuration')
    require(not signing['production_mode'] or signing['custom_public_key'], 'Missing production identity selection')
    require(signing.get('key_custody_verified') is False, 'Candidate cannot certify production key custody')
    require(isinstance(signing.get('trust_store_sha256'), str) and
            re.fullmatch('[0-9a-f]{64}', signing['trust_store_sha256']), 'Invalid trust-store fingerprint')
    targets = report.get('targets')
    require(isinstance(targets, dict) and set(targets) == set(PROFILE), 'Package must contain P4 and C5')
    for chip, expected in PROFILE.items():
        target = targets[chip]
        require(isinstance(target, dict), f'Invalid target: {chip}')
        for field, value in expected.items():
            require(target.get(field) == value and type(target.get(field)) is type(value),
                    f'{chip} profile mismatch: {field}')
        size, free = target.get('bytes'), target.get('free_bytes')
        require(type(size) is int and 0 < size <= expected['slot_bytes'], f'{chip} invalid image size')
        require(type(free) is int and free == expected['slot_bytes'] - size, f'{chip} invalid slot margin')
        checksum = target.get('sha256')
        require(isinstance(checksum, str) and re.fullmatch('[0-9a-f]{64}', checksum), f'{chip} invalid digest')
        if 'build_evidence' in target:
            import build_evidence
            require(isinstance(target['build_evidence'], dict), 'Invalid build evidence')
            require(target['build_evidence'].get('target') == chip, 'Crossed build evidence')
            build_evidence.validate(target['build_evidence'], target)


def reject_constant(value):
    raise ValueError(f'Invalid JSON constant: {value}')


def verify(directory):
    directory = Path(directory)
    manifest = directory / 'manifest.json'
    require(manifest.is_file() and not manifest.is_symlink(), 'Missing or symlinked manifest')
    require(manifest.stat().st_size <= 1024 * 1024, 'Manifest exceeds 1 MiB')
    report = json.loads(manifest.read_text(), object_pairs_hook=unique_object, parse_constant=reject_constant)
    validate_report(report)
    for chip, expected in PROFILE.items():
        target = report['targets'][chip]
        size, checksum = target['bytes'], target['sha256']
        image = directory / expected['file']
        require(image.is_file() and not image.is_symlink(), f'{chip} missing or symlinked application')
        require(image.stat().st_size == size, f'{chip} image length mismatch')
        require(digest(image) == checksum, f'{chip} image SHA-256 mismatch')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        report = verify(args.directory)
    except (ValueError, OSError, TypeError) as error:
        raise SystemExit(f'Package verification failed: {error}') from error
    for chip, target in report['targets'].items():
        print(f'{chip}: {target["bytes"]} bytes, {target["free_bytes"]} free; SHA-256 OK')
    print('Verified app-only candidate integrity/profile; no signature authentication or hardware acceptance implied.')


if __name__ == '__main__':
    main()
