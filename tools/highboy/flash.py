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
SLOT_SIZES = {'p4': 0x310000, 'c5': 0x300000}
FLASH_SIZE = 0x800000
BACKUP_CHUNK_SIZE = 0x80000
TOOL_TIMEOUT_SECONDS = 600
PROFILES = {
    ('p4', '8MB'): (0x800000, 0x310000, 'partitions.csv'),
    ('p4', '16MB'): (0x1000000, 0x600000, 'partitions_16mb.csv'),
    ('c5', '8MB'): (0x800000, 0x300000, 'partitions.csv'),
}


def select_build(project, target, explicit=None):
    build = (project / 'build').resolve()
    require(explicit is None or explicit.resolve() == build,
            'Firmware builds must use the standard firmware_<chip>/build directory.')
    require((build / f'TentacleOS_{target.upper()}.bin').is_file(),
            f'No built {target.upper()} app found in {build}; build it first.')
    description = json.loads((build / 'project_description.json').read_text())
    require(Path(description['project_path']).resolve() == project.resolve(),
            'Build belongs to a different firmware project.')
    require(description['target'] == f'esp32{target}', 'Build has the wrong chip target.')
    config = json.loads((build / 'config/sdkconfig.json').read_text())
    require(config.get('IDF_TARGET') == f'esp32{target}', 'Wrong generated build target.')
    profile = PROFILES.get((target, config.get('ESPTOOLPY_FLASHSIZE')))
    require(profile is not None, 'Unsupported build flash profile.')
    require(config.get('PARTITION_TABLE_CUSTOM_FILENAME') == profile[2],
            'Build partition filename does not match the reviewed profile.')
    settings = json.loads((build / 'flasher_args.json').read_text())['flash_settings']
    require(settings['flash_size'] == config['ESPTOOLPY_FLASHSIZE'],
            'Build flash settings disagree with generated configuration.')
    return build, profile


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


def validate_device(flash, expected_table, gen, target='p4', flash_size=FLASH_SIZE):
    require(len(flash) == flash_size, 'Incomplete full flash backup.')
    installed_table = flash[TABLE_OFFSET:TABLE_OFFSET + TABLE_SIZE]
    require(installed_table == expected_table[:TABLE_SIZE],
            'Installed partition map differs from the selected build. No migration or write performed.')
    table = gen.PartitionTable.from_binary(installed_table)
    table.verify()
    by_name = {entry.name: entry for entry in table}
    require(all(name in by_name for name in ('ota_0', 'ota_1', 'otadata', 'assets')),
            'Missing required partitions.')
    require(all(entry.offset + entry.size <= len(flash) for entry in table),
            'Partition extends beyond the flash backup.')
    ota = by_name['otadata']
    slot = by_name[f'ota_{active_slot(flash[ota.offset:ota.offset + ota.size])}']
    profile = PROFILES.get((target, f'{flash_size // (1024 * 1024)}MB'))
    require(profile is not None and slot.size == profile[1], 'Unexpected app slot size.')
    require(slot.offset % SECTOR_SIZE == 0, 'Unaligned app slot.')
    image_valid(flash[BOOT_OFFSET:TABLE_OFFSET], target)
    image_valid(flash[slot.offset:slot.offset + slot.size], target)
    assets = by_name['assets']
    assets_data = flash[assets.offset:assets.offset + assets.size]
    require(assets_data not in (b'\xff' * assets.size, b'\x00' * assets.size),
            'Assets partition is empty; an app-only update cannot restore it.')
    return slot


def run_tool(args, *command, reset=False):
    from flash_session import FlashSession
    if reset:
        session = getattr(args, '_session', None)
        if session is not None:
            session.close(TOOL_TIMEOUT_SECONDS)
        return ''
    if not hasattr(args, '_session'):
        args._session = FlashSession(args)
    return args._session.run(command, TOOL_TIMEOUT_SECONDS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=('p4', 'c5'), default='p4')
    parser.add_argument('--mode', choices=('app', 'full'), default='app')
    parser.add_argument('-p', '--port', required=True, help='Target serial port; never auto-selected')
    parser.add_argument('--build-dir', type=Path, help='Compatibility option; must be the target project build directory')
    parser.add_argument('-b', '--baud', type=int, help='Override both read and write baud')
    parser.add_argument('--read-baud', type=int, help='Read baud (P4/C5: 230400)')
    parser.add_argument('--write-baud', type=int, help='Write baud (P4: 921600; C5: 460800)')
    parser.add_argument('--before', choices=('default-reset', 'no-reset'), default='default-reset',
                        help='Use no-reset after entering the ROM bootloader with p4 boot')
    parser.add_argument('--control-port', help='P4 console port for automatic C5 download entry')
    parser.add_argument('--validate-build', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--check', action='store_true', help='Validate only; full mode also backs up flash; resets the target')
    parser.add_argument('--idf-path', type=Path, default=os.environ.get('IDF_PATH'))
    args = parser.parse_args()
    args.read_baud = args.read_baud if args.read_baud is not None else args.baud if args.baud is not None else 230400
    args.write_baud = args.write_baud if args.write_baud is not None else args.baud if args.baud is not None else (921600 if args.target == 'p4' else 460800)
    args.baud = args.read_baud
    require(bool(args.port.strip()), 'An explicit target serial port is required.')
    require(args.read_baud > 0 and args.write_baud > 0, 'Baud rates must be positive.')
    require(args.idf_path is not None, 'An ESP-IDF path is required.')
    sys.path.insert(0, str(args.idf_path / 'components/partition_table'))
    import gen_esp32part as gen
    import check_contracts
    project = ROOT / f'firmware_{args.target}'
    build, profile = select_build(project, args.target, args.build_dir)
    args.flash_size, slot_size, table_file = profile
    print(f'Build directory: {build}', flush=True)
    print(f'App binary: {build / f"TentacleOS_{args.target.upper()}.bin"}', flush=True)
    print(f'Build profile: {args.flash_size // (1024 * 1024)} MB; app slot {slot_size} bytes', flush=True)
    # The contract checker has its own argument parser.
    old_argv = sys.argv
    try:
        sys.argv = [old_argv[0], '--require-built', args.target, '--build-dir', str(build)]
        check_contracts.main()
    finally:
        sys.argv = old_argv
    app = (build / f'TentacleOS_{args.target.upper()}.bin').read_bytes()
    require(0 < len(app) <= slot_size, f'App is {len(app)} bytes; selected profile allows {slot_size} bytes.')
    image_valid(app, args.target)
    expected_table = (build / 'partition_table/partition-table.bin').read_bytes()
    source_table = gen.PartitionTable.from_csv((project / table_file).read_text())
    require(source_table.to_binary() == expected_table[:TABLE_SIZE], 'Stale build partition table.')
    source_table.verify()
    require(all(entry.offset + entry.size <= args.flash_size for entry in source_table),
            'Build partition extends beyond configured flash.')
    args.bootloader = build / 'bootloader/bootloader.bin'
    if args.mode == 'app':
        config = json.loads((build / 'config/sdkconfig.json').read_text())
        require(config.get('BOOTLOADER_APP_ROLLBACK_ENABLE') is True, 'OTA updates require rollback enabled.')
        require(args.bootloader.is_file(), 'Build the rollback-enabled bootloader first.')
        boot_config = json.loads((build / 'bootloader/config/sdkconfig.json').read_text())
        require(boot_config.get('BOOTLOADER_APP_ROLLBACK_ENABLE') is True, 'Installed bootloader must support rollback.')
    args.full_images = None
    if args.mode == 'full':
        from full_flash import prepare
        args.full_images = prepare(build, args.target, args.flash_size, expected_table, app, gen, image_valid)
    if args.validate_build:
        print(f'{args.target.upper()} build preflight passed; no device accessed.', flush=True)
        return
    if args.target == 'c5' and not args.check:
        from flash_ports import peer_port
        args.control_port = args.control_port or peer_port(args.port, 'p4')
        require(args.control_port is not None,
                'C5 updates require the paired P4 console for boot health confirmation; '
                'specify --control-port PORT.')
    print(f'Flash mode: {args.mode}', flush=True)
    print(f'{args.target.upper()} app SHA256:', digest(app), flush=True)
    print(f'Read: {args.read_baud} baud; write: {args.write_baud} baud. Connecting resets the target.', flush=True)
    try:
        security = run_tool(args, 'get_security_info')
        require('Secure Boot: Disabled' in security and 'Flash Encryption: Disabled' in security,
                'This development updater requires Secure Boot and Flash Encryption disabled.')
        update_device(args, app, expected_table, gen)
    finally:
        failed = sys.exc_info()[0] is not None
        try:
            run_tool(args, 'read_mac', reset=True)
        except Exception as error:
            if not failed:
                raise
            print(f'Final reset also failed: {error}', file=sys.stderr)
    if args.target == 'c5' and not args.check:
        from flash_session import confirm_c5_boot
        confirm_c5_boot(args.control_port)


def update_device(args, app, expected_table, gen):
    identity = run_tool(args, 'flash_id')
    flash_size = args.flash_size
    capacity = f'{flash_size // (1024 * 1024)}MB'
    require(f'Detected flash size: {capacity}' in identity,
            f'Device flash capacity does not match the selected {capacity} build; no write performed.')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    dest = ROOT / 'integration/device_backups' / (args.target + '_' + stamp)
    dest.mkdir(parents=True, mode=0o700)
    if args.mode == 'app':
        import ota_flash
        ota_flash.update(args, app, expected_table, gen, dest, sys.modules[__name__])
        return
    backup = dest / 'flash.bin'
    print(f'Reading the entire flash into {backup}; this can take several minutes.', flush=True)
    chunk = dest / 'chunk.bin'
    with backup.open('xb') as output:
        for offset in range(0, flash_size, BACKUP_CHUNK_SIZE):
            print(f'Backup: {offset // 1024}/{flash_size // 1024} KiB', flush=True)
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
    update_full(args, flash, expected_table, gen, dest, backup, report, manifest)


def update_full(args, flash, expected_table, gen, dest, backup, report, manifest):
    from full_flash import validate_migration
    validate_migration(flash, expected_table, gen)
    files = []
    report.update(mode='full', status='checked', files=[])
    for label, offset, data in args.full_images:
        path = dest / f'{label}.bin'
        path.write_bytes(data)
        files.extend([hex(offset), str(path)])
        report['files'].append({'name': label, 'offset': offset, 'bytes': len(data), 'sha256': digest(data)})
        print(f'Full flash: {label} at {offset:#x}, {len(data)} bytes', flush=True)
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    if args.check:
        print('Check complete. Full flash validated; no flash bytes written.')
        return
    run_tool(args, 'verify_flash', '0', str(backup))
    report['status'] = 'write_started'
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    run_tool(args, 'write_flash', *files)
    run_tool(args, 'verify_flash', *files)
    report['status'] = 'written_and_verified'
    manifest.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{args.target.upper()} full flash written and verified. The other chip and SD were not accessed.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, RuntimeError, ImportError, subprocess.SubprocessError) as error:
        print(f'Flash stopped: {error}', file=sys.stderr)
        sys.exit(2)
