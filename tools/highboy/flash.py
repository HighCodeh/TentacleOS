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

"""Update an installed High Boy P4 or C5 app, preserving its partition map and data."""
import argparse
import binascii
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0xC00
SECTOR_SIZE = 0x1000
BOOT_OFFSET = 0x2000
SLOT_SIZE = 0x2D0000
SLOT_SIZES = {'p4': SLOT_SIZE, 'c5': 0x200000}
FLASH_SIZE = 0x800000
BACKUP_CHUNK_SIZE = 0x80000
TOOL_TIMEOUT_SECONDS = 120


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def active_slot(data):
    require(len(data) == 2 * SECTOR_SIZE, 'Invalid OTA metadata size.')
    if data == b'\xff' * len(data):
        return 0
    entries = []
    for offset in (0, SECTOR_SIZE):
        seq, = struct.unpack_from('<I', data, offset)
        state, crc = struct.unpack_from('<II', data, offset + 24)
        expected = binascii.crc32(struct.pack('<I', seq), 0xFFFFFFFF) & 0xFFFFFFFF
        if 0 < seq < 0xFFFFFFFF and crc == expected and state not in (3, 4):
            entries.append((seq, state))
    require(entries, 'OTA selection is invalid; no app was written.')
    seq, state = max(entries)
    require(state in (2, 0xFFFFFFFF), 'OTA update pending or unknown; boot and confirm it first.')
    return (seq - 1) % 2


def image_valid(data, target='p4'):
    from esptool import bin_image
    image_class = getattr(bin_image, f'ESP32{target.upper()}FirmwareImage')
    image = image_class(io.BytesIO(data))
    require(image.checksum == image.calculate_checksum(), f'Invalid ESP32-{target.upper()} image checksum.')
    require(image.append_digest and image.stored_digest == image.calc_digest,
            f'Missing or invalid ESP32-{target.upper()} image SHA256.')
    return image


def validate_device(flash, expected_table, gen, target='p4'):
    installed_table = flash[TABLE_OFFSET:TABLE_OFFSET + TABLE_SIZE]
    require(installed_table == expected_table[:TABLE_SIZE],
            'Installed partition map differs from build_highboy. No migration or write performed.')
    table = gen.PartitionTable.from_binary(installed_table)
    table.verify()
    by_name = {entry.name: entry for entry in table}
    require(all(name in by_name for name in ('ota_0', 'ota_1', 'otadata', 'assets')),
            'Missing required partitions.')
    require(all(entry.offset + entry.size <= len(flash) for entry in table),
            'Partition extends beyond the flash backup.')
    ota = by_name['otadata']
    slot = by_name[f'ota_{active_slot(flash[ota.offset:ota.offset + ota.size])}']
    require(slot.size == SLOT_SIZES[target], 'Unexpected app slot size.')
    require(slot.offset % SECTOR_SIZE == 0, 'Unaligned app slot.')
    image_valid(flash[BOOT_OFFSET:TABLE_OFFSET], target)
    image_valid(flash[slot.offset:slot.offset + slot.size], target)
    assets = by_name['assets']
    assets_data = flash[assets.offset:assets.offset + assets.size]
    require(assets_data not in (b'\xff' * assets.size, b'\x00' * assets.size),
            'Assets partition is empty; an app-only update cannot restore it.')
    return slot


def run_tool(args, *command, reset=False):
    cmd = [sys.executable, '-u', '-m', 'esptool', '--chip', f'esp32{args.target}',
           '--port', args.port, '--baud', str(args.baud),
           '--after', 'hard_reset' if reset else 'no_reset']
    if args.target == 'c5':
        cmd.append('--no-stub')
    cmd.extend(command)
    capture = command[0] in ('get_security_info', 'flash_id')
    result = subprocess.run(cmd, text=True, stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT, timeout=TOOL_TIMEOUT_SECONDS)
    if capture:
        print(result.stdout, end='', flush=True)
    require(result.returncode == 0, 'esptool failed. See output above; backup files were retained.')
    return result.stdout or ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=('p4', 'c5'), default='p4')
    parser.add_argument('-p', '--port', required=True, help='Target serial port; never auto-selected')
    parser.add_argument('-b', '--baud', type=int, default=460800)
    parser.add_argument('--check', action='store_true', help='Back up and validate only; resets the target')
    parser.add_argument('--idf-path', type=Path, default=os.environ.get('IDF_PATH'))
    args = parser.parse_args()
    require(bool(args.port.strip()), 'An explicit target serial port is required.')
    require(args.baud > 0, 'Baud rate must be positive.')
    require(args.idf_path is not None, 'An ESP-IDF path is required.')
    sys.path.insert(0, str(args.idf_path / 'components/partition_table'))
    import gen_esp32part as gen
    import check_contracts
    # The contract checker has its own argument parser.
    old_argv = sys.argv
    try:
        sys.argv = [old_argv[0], '--require-built', args.target]
        check_contracts.main()
    finally:
        sys.argv = old_argv
    project = ROOT / f'firmware_{args.target}'
    build = project / 'build_highboy'
    app = (build / f'TentacleOS_{args.target.upper()}.bin').read_bytes()
    require(0 < len(app) <= SLOT_SIZES[args.target], 'Build the target app first; it must fit the preserved slot.')
    image_valid(app, args.target)
    expected_table = (build / 'partition_table/partition-table.bin').read_bytes()
    source_table = gen.PartitionTable.from_csv((project / 'partitions.csv').read_text())
    require(source_table.to_binary() == expected_table[:TABLE_SIZE], 'Stale build partition table.')
    print(f'{args.target.upper()} app SHA256:', digest(app), flush=True)
    print('Connecting resets the target. Existing data will be backed up before any app write.', flush=True)
    security = run_tool(args, 'get_security_info')
    try:
        require('Secure Boot: Disabled' in security and 'Flash Encryption: Disabled' in security,
                'This development updater requires Secure Boot and Flash Encryption disabled.')
        update_device(args, app, expected_table, gen)
    finally:
        run_tool(args, 'read_mac', reset=True)


def update_device(args, app, expected_table, gen):
    identity = run_tool(args, 'flash_id')
    require('Detected flash size: 8MB' in identity,
            'This updater requires the reviewed 8 MB flash profile.')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    dest = ROOT / 'integration/device_backups' / (args.target + '_' + stamp)
    dest.mkdir(parents=True, mode=0o700)
    backup = dest / 'flash.bin'
    print(f'Reading the entire flash into {backup}; this can take several minutes.', flush=True)
    chunk = dest / 'chunk.bin'
    with backup.open('xb') as output:
        for offset in range(0, FLASH_SIZE, BACKUP_CHUNK_SIZE):
            print(f'Backup: {offset // 1024}/{FLASH_SIZE // 1024} KiB', flush=True)
            run_tool(args, 'read_flash', '--no-progress', hex(offset),
                     hex(BACKUP_CHUNK_SIZE), str(chunk))
            data = chunk.read_bytes()
            require(len(data) == BACKUP_CHUNK_SIZE, 'Incomplete flash backup chunk.')
            output.write(data)
            output.flush()
            chunk.unlink()
    backup.chmod(0o600)
    flash = backup.read_bytes()
    report = {'port': args.port, 'chip': f'esp32{args.target}', 'backup_sha256': digest(flash),
              'backup_bytes': len(flash), 'app_sha256': digest(app), 'app_bytes': len(app),
              'partition_table_sha256': digest(expected_table),
              'installed_partition_table_sha256': digest(
                  flash[TABLE_OFFSET:TABLE_OFFSET + TABLE_SIZE]),
              'status': 'backup_read', 'other_chip_updated': False}
    manifest = dest / 'manifest.json'
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    slot = validate_device(flash, expected_table, gen, args.target)
    # Snapshot the app so rebuilding concurrently cannot change the selected bytes.
    candidate = dest / 'candidate.bin'
    candidate.write_bytes(app)
    report.update(slot=slot.name, offset=slot.offset, status='checked')
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Backup: {backup}\nSelected app: {slot.name} at {slot.offset:#x}', flush=True)
    if args.check:
        print('Check complete. No flash bytes written. The other chip was not accessed.')
        return
    # Require a matching device snapshot again immediately before writing.
    # No force, bootloader, partition-table, otadata, assets, NVS or erase-all command.
    run_tool(args, 'verify_flash', '0', str(backup))
    report['status'] = 'write_started'
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    run_tool(args, 'write_flash', hex(slot.offset), str(candidate))
    run_tool(args, 'verify_flash', hex(slot.offset), str(candidate))
    report['status'] = 'written_and_verified'
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{args.target.upper()} app written and verified. The other chip was not accessed or updated.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, RuntimeError, ImportError, subprocess.SubprocessError) as error:
        print(f'Flash stopped: {error}', file=sys.stderr)
        sys.exit(2)
