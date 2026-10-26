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

"""Prepare/verify a paired SD OTA candidate for an existing matching installation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

import build_evidence
import verify_package

SD_FILES = {'P4': 'sdcard/update/tentacleos.bin', 'C5': 'sdcard/c5/TentacleOS_C5.bin'}
BOUNDARY = {'schema_version': 1, 'status': 'paired_sd_ota_candidate',
            'app_only': True, 'protocol_version': 8, 'flashed': False,
            'hardware_validated': False, 'runtime_performance_measured': False,
            'production_release': False, 'security_provisioned': False,
            'efuses_written': False, 'restores_assets': False, 'partition_migration': False,
            'requires_matching_installed_profile': True}
require = verify_package.require


def read_report(path):
    require(path.is_file() and not path.is_symlink() and path.stat().st_size <= 1024 * 1024,
            'Missing, symlinked or oversized report: ' + path.name)
    return json.loads(path.read_text(), object_pairs_hook=verify_package.unique_object,
                      parse_constant=verify_package.reject_constant)


def verify(directory):
    directory = Path(directory)
    require(directory.is_dir() and not directory.is_symlink(), 'Invalid OTA directory')
    report = read_report(directory / 'ota.json')
    for field, value in BOUNDARY.items():
        require(type(report.get(field)) is type(value) and report[field] == value,
                'Invalid OTA boundary: ' + field)
    require(report.get('sd_files') == SD_FILES, 'Wrong OTA destinations')
    candidate = directory / 'candidate'
    require(candidate.is_dir() and not candidate.is_symlink(), 'Invalid candidate directory')
    source = verify_package.verify(candidate)
    require(report.get('candidate_manifest_sha256') == verify_package.digest(candidate / 'manifest.json'),
            'Candidate manifest changed')
    for chip, name in SD_FILES.items():
        target = source['targets'][chip]
        require('build_evidence' in target, 'OTA candidate requires linked memory/compiler evidence')
        build_evidence.validate(target['build_evidence'], target)
        image = directory / name
        require(image.is_file() and not image.is_symlink(), 'Missing or symlinked SD image')
        require(image.stat().st_size == target['bytes'] and verify_package.digest(image) == target['sha256'],
                'SD image differs from verified candidate: ' + chip)
    allowed = {'ota.json', 'INSTALL.txt', 'candidate/manifest.json'} | set(SD_FILES.values()) | {
        'candidate/' + profile['file'] for profile in verify_package.PROFILE.values()}
    actual = set()
    for path in directory.rglob('*'):
        require(not path.is_symlink(), 'Symlink in OTA package')
        require(path.is_dir() or path.is_file(), 'Unexpected special file')
        if path.is_file():
            actual.add(path.relative_to(directory).as_posix())
    require(actual == allowed, 'Unexpected or missing OTA files')
    require((directory / 'INSTALL.txt').read_text() == instructions(), 'Installation contract changed')
    return report


def instructions():
    return (
        'High Boy paired SD OTA development candidate\n\n'
        'Use only on an already running matching integrated installation: protocol 8,\n'
        'P4 IDF v6.1 / C5 IDF v6.1, original partition maps and intact assets.\n'
        'This package does not restore an erased device or migrate any partition map.\n'
        'It contains no bootloader, production identity, security provisioning or eFuse action.\n'
        'Verify with: python3 tools/highboy/ota_package.py verify <this-directory>\n'
        'This checks integrity/profile, not authenticity or device compatibility.\n\n'
        'Copy the CONTENTS of sdcard/ onto the SD root. Do not rename the files.\n'
        'P4 reads /sdcard/update/tentacleos.bin; its Update screen starts P4 OTA.\n'
        'C5 reads /sdcard/c5/TentacleOS_C5.bin through the existing independent C5 updater.\n'
        'Both images are required as a pair; neither updater updates the other target.\n'
        'Retain a private paired backup before updating; do not interrupt either write.\n'
        'P4 confirmation requires local assets/render health; C5 requires P4 bridge health.\n'
        'Actual boot, rollback, latency, internal heap and stack acceptance are unmeasured.\n'
    )


def prepare(candidate, destination):
    candidate, destination = Path(candidate), Path(destination)
    require(candidate.is_dir() and not candidate.is_symlink(), 'Invalid candidate directory')
    source = verify_package.verify(candidate)
    for target in source['targets'].values():
        require('build_evidence' in target, 'Repackage with current package.py: linked build evidence required')
    manifest_hash = verify_package.digest(candidate / 'manifest.json')
    report = {**BOUNDARY, 'sd_files': SD_FILES, 'candidate_manifest_sha256': manifest_hash}
    final = destination / ('ota_' + manifest_hash[:20])
    destination.mkdir(parents=True, exist_ok=True)
    if final.exists():
        require(verify(final) == report, 'Existing OTA package differs')
        return final
    with tempfile.TemporaryDirectory(prefix='.ota-', dir=destination) as temporary:
        staging = Path(temporary) / 'package'
        (staging / 'candidate').mkdir(parents=True)
        for name in ('manifest.json', *(p['file'] for p in verify_package.PROFILE.values())):
            shutil.copyfile(candidate / name, staging / 'candidate' / name)
        for chip, name in SD_FILES.items():
            image = staging / name
            image.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(staging / 'candidate' / source['targets'][chip]['file'], image)
        (staging / 'ota.json').write_text(json.dumps(report, indent=2) + '\n')
        (staging / 'INSTALL.txt').write_text(instructions())
        verify(staging)
        require(verify_package.digest(staging / 'candidate/manifest.json') == manifest_hash,
                'Candidate changed while packaging')
        staging.rename(final)
    return final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    export = commands.add_parser('prepare')
    export.add_argument('--candidate', required=True, type=Path)
    export.add_argument('--out', required=True, type=Path)
    check = commands.add_parser('verify')
    check.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            print(prepare(args.candidate, args.out))
        else:
            verify(args.directory)
            print('Paired SD OTA candidate verified; physical/production acceptance remains unmeasured.')
    except (ValueError, OSError, KeyError, TypeError) as error:
        raise SystemExit('OTA package failed: ' + str(error)) from error


if __name__ == '__main__':
    main()
