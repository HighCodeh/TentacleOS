#!/usr/bin/env python3
"""Prepare/verify a private paired recovery review bundle; never accesses a device."""
import argparse
import binascii
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile

import verify_package

ROOT = Path(__file__).resolve().parents[2]
FLASH_SIZE = 0x800000
BOOT_OFFSET = 0x2000
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0xc00
SECTOR_SIZE = 0x1000
OTA_OFFSET = 0xf000
APP_OFFSET = 0x20000
ASSETS = {'P4': (0x5d0000, 0x230000), 'C5': (0x420000, 0x1e0000)}
BOUNDARY = {'schema_version': 1, 'status': 'private_recovery_review_only',
            'contains_private_device_data': True, 'deployment_approved': False,
            'device_identity_verified': False, 'security_state_verified': False,
            'assets_filesystem_validated': False, 'hardware_validated': False}
require = verify_package.require


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_regular(path, limit):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), 'Missing or symlinked input: ' + path.name)
    require(path.stat().st_size <= limit, 'Oversized input: ' + path.name)
    data = path.read_bytes()
    require(len(data) <= limit, 'Input grew while reading: ' + path.name)
    return data


def read_json(path):
    return json.loads(read_regular(path, 1024 * 1024), object_pairs_hook=verify_package.unique_object,
                      parse_constant=verify_package.reject_constant)


def active_slot(data):
    require(len(data) == 2 * SECTOR_SIZE, 'Invalid OTA metadata length')
    if data == b'\xff' * len(data):
        return 0
    entries = []
    for offset in (0, SECTOR_SIZE):
        sequence, = struct.unpack_from('<I', data, offset)
        state, crc = struct.unpack_from('<II', data, offset + 24)
        expected = binascii.crc32(struct.pack('<I', sequence), 0xffffffff) & 0xffffffff
        if 0 < sequence < 0xffffffff and crc == expected and state not in (3, 4):
            entries.append((sequence, state))
    require(entries, 'Invalid OTA selection')
    sequence, state = max(entries)
    require(state in (2, 0xffffffff), 'Pending or unknown OTA state; recovery review cannot assume a healthy app')
    return (sequence - 1) % 2


def verify_image(data, chip):
    # esptool parses image headers, checksums and appended digests; not signatures.
    from esptool import bin_image
    from esptool.util import FatalError
    image_class = getattr(bin_image, 'ESP32' + chip + 'FirmwareImage')
    try:
        image = image_class(io.BytesIO(data))
    except (FatalError, struct.error, IndexError) as error:
        raise ValueError(chip + ' invalid image container: ' + str(error)) from error
    require(image.chip_id == image.ROM_LOADER.IMAGE_CHIP_ID, 'Wrong image chip')
    require(image.checksum == image.calculate_checksum(), 'Invalid image checksum')
    require(image.append_digest and image.stored_digest == image.calc_digest,
            'Missing or invalid image SHA-256')


def validate_flash(flash, chip, candidate):
    require(len(flash) == FLASH_SIZE, chip + ' backup must cover the full reviewed 8 MiB flash')
    profile = verify_package.PROFILE[chip]
    table = flash[TABLE_OFFSET:TABLE_OFFSET + TABLE_SIZE]
    require(digest(table) == profile['partition_table_sha256'], chip + ' installed map differs from the integrated profile')
    verify_image(flash[BOOT_OFFSET:TABLE_OFFSET], chip)
    slot = active_slot(flash[OTA_OFFSET:OTA_OFFSET + 2 * SECTOR_SIZE])
    offset = APP_OFFSET if slot == 0 else (0x2f0000 if chip == 'P4' else 0x220000)
    verify_image(flash[offset:offset + profile['slot_bytes']], chip)
    require(0 < len(candidate) <= profile['slot_bytes'], chip + ' candidate exceeds its slot')
    verify_image(candidate, chip)
    assets_offset, assets_size = ASSETS[chip]
    assets = flash[assets_offset:assets_offset + assets_size]
    require(assets not in (b'\xff' * assets_size, b'\x00' * assets_size), chip + ' assets are blank')
    return {'chip': chip, 'snapshot_file': chip + '_flash.bin', 'snapshot_sha256': digest(flash),
            'snapshot_bytes': len(flash), 'selected_slot': slot, 'app_update_offset': offset,
            'candidate_file': 'candidate/' + profile['file'], 'candidate_sha256': digest(candidate),
            'partition_table_sha256': digest(table), 'assets_sha256': digest(assets),
            'assets_present': True}


def validate_metadata(metadata, flash, chip):
    require(isinstance(metadata, dict), 'Invalid backup metadata')
    require(metadata.get('chip') == 'esp32' + chip.lower(), 'Backup target mismatch')
    require(type(metadata.get('backup_bytes')) is int and metadata['backup_bytes'] == len(flash),
            'Backup length differs from its capture metadata')
    require(metadata.get('backup_sha256') == digest(flash), 'Backup differs from its capture metadata')
    require(metadata.get('status') in ('backup_read', 'checked', 'write_started', 'written_and_verified'),
            'Unknown backup capture status')


def candidate_inputs(directory):
    report = verify_package.verify(directory)
    files = {'manifest.json': read_regular(Path(directory) / 'manifest.json', 1024 * 1024)}
    for chip, profile in verify_package.PROFILE.items():
        data = read_regular(Path(directory) / profile['file'], profile['slot_bytes'])
        require(digest(data) == report['targets'][chip]['sha256'], 'Candidate changed while reading')
        files[profile['file']] = data
    return report, files


def private_file(path, data):
    with path.open('xb') as output:
        path.chmod(0o600)
        output.write(data)


def verify(directory):
    directory = Path(directory)
    report = read_json(directory / 'recovery.json')
    require(isinstance(report, dict), 'Invalid recovery manifest')
    for name, expected in BOUNDARY.items():
        require(report.get(name) == expected and type(report.get(name)) is type(expected),
                'Invalid recovery boundary: ' + name)
    candidate, files = candidate_inputs(directory / 'candidate')
    require(report.get('candidate_manifest_sha256') == digest(files['manifest.json']), 'Candidate manifest mismatch')
    require(isinstance(report.get('targets'), dict) and set(report['targets']) == set(ASSETS), 'Missing recovery target')
    for chip, target in report['targets'].items():
        flash = read_regular(directory / (chip + '_flash.bin'), FLASH_SIZE)
        metadata = read_json(directory / (chip + '_capture.json'))
        validate_metadata(metadata, flash, chip)
        actual = validate_flash(flash, chip, files[verify_package.PROFILE[chip]['file']])
        require(isinstance(target, dict) and set(target) == set(actual) and
                all(target[name] == value and type(target[name]) is type(value) for name, value in actual.items()),
                chip + ' recovery plan differs from verified offsets/content')
    return report


def prepare(candidate_directory, backups, destination):
    destination = Path(destination).absolute()
    require(not destination.exists() and not destination.is_symlink(), 'Output already exists')
    # Full flash snapshots can contain Wi-Fi credentials, tokens and private identity material.
    resolved = destination.resolve()
    if resolved.is_relative_to(ROOT):
        require(resolved.is_relative_to(ROOT / 'integration/recovery_candidates'),
                'Private bundles inside the repository must use ignored integration/recovery_candidates/')
    candidate, files = candidate_inputs(candidate_directory)
    require(set(backups) == set(ASSETS), 'Both chip backups are required')
    snapshots, metadata, targets = {}, {}, {}
    for chip, backup in backups.items():
        backup = Path(backup)
        snapshots[chip] = read_regular(backup, FLASH_SIZE)
        metadata[chip] = read_regular(backup.parent / 'manifest.json', 1024 * 1024)
        capture = json.loads(metadata[chip], object_pairs_hook=verify_package.unique_object,
                             parse_constant=verify_package.reject_constant)
        validate_metadata(capture, snapshots[chip], chip)
        targets[chip] = validate_flash(snapshots[chip], chip, files[verify_package.PROFILE[chip]['file']])
    report = dict(BOUNDARY, candidate_manifest_sha256=digest(files['manifest.json']), targets=targets,
                  remaining_review=['Match both physical chip identities and current security flags',
                                    'Mount/inspect restored assets and confirm their compatibility',
                                    'Review per-device data, paired boot/rollback and full-restore approval'])
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.recovery-', dir=destination.parent) as temp:
        staging = Path(temp) / 'review'; staging.mkdir(mode=0o700)
        (staging / 'candidate').mkdir(mode=0o700)
        for name, data in files.items(): private_file(staging / 'candidate' / name, data)
        for chip in backups:
            private_file(staging / (chip + '_flash.bin'), snapshots[chip])
            private_file(staging / (chip + '_capture.json'), metadata[chip])
        private_file(staging / 'recovery.json', (json.dumps(report, indent=2) + '\n').encode())
        verify(staging)
        staging.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    build = commands.add_parser('prepare')
    build.add_argument('--candidate', type=Path, required=True)
    build.add_argument('--p4-backup', type=Path, required=True)
    build.add_argument('--c5-backup', type=Path, required=True)
    build.add_argument('--out', type=Path, required=True)
    check = commands.add_parser('verify'); check.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            output = prepare(args.candidate, {'P4': args.p4_backup, 'C5': args.c5_backup}, args.out)
            print('Private recovery review bundle:', output)
        else:
            verify(args.directory)
            print('Private recovery content/offset verification passed.')
    except (ValueError, OSError, TypeError, ImportError) as error:
        raise SystemExit('Recovery preparation failed: ' + str(error)) from error
    print('No device access. Identity/security/assets acceptance and full-restore approval remain required.')


if __name__ == '__main__': main()
