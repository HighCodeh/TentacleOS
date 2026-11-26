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

"""Inactive-slot application updates with sector-atomic OTA selection."""
import binascii
import json
import struct
import tempfile
from pathlib import Path

SECTOR = 4096
STATE_NEW = 0
STATE_VALID = 2
STATE_UNDEFINED = 0xffffffff
MAX_SEQ = 0xfffffffe


def selection(data):
    if len(data) != 2 * SECTOR:
        raise ValueError('Invalid OTA metadata size.')
    if data == b'\xff' * len(data):
        return 0, 1, None
    entries = []
    for index in range(2):
        seq, = struct.unpack_from('<I', data, index * SECTOR)
        state, crc = struct.unpack_from('<II', data, index * SECTOR + 24)
        expected = binascii.crc32(struct.pack('<I', seq), 0xffffffff) & 0xffffffff
        if 0 < seq <= MAX_SEQ and crc == expected and state not in (3, 4):
            entries.append((seq, -index, state))
    if not entries:
        raise ValueError('Invalid OTA selection; use full recovery.')
    seq, negative_index, state = max(entries)
    if state not in (STATE_VALID, STATE_UNDEFINED):
        raise ValueError('OTA update pending or unknown; boot and confirm it first.')
    if seq == MAX_SEQ:
        raise ValueError('OTA sequence exhausted; full recovery required.')
    return (seq - 1) % 2, seq, -negative_index


def entry(seq, state):
    result = bytearray(b'\xff' * SECTOR)
    struct.pack_into('<I', result, 0, seq)
    crc = binascii.crc32(struct.pack('<I', seq), 0xffffffff) & 0xffffffff
    struct.pack_into('<II', result, 24, state, crc)
    return bytes(result)


def verify_bootloader(installed, built, api, target):
    old, new = api.image_valid(installed, target), api.image_valid(built, target)
    api.require(installed[:24] == built[:24] and len(old.segments) == len(new.segments),
                'Bootloader differs from the rollback-enabled build; use full mode.')
    for index, (left, right) in enumerate(zip(old.segments, new.segments)):
        a, b = bytearray(left.data), bytearray(right.data)
        # IDF v6.1 esp_bootloader_desc_t: only compile date/time is nonfunctional.
        if index == 0 and len(a) >= 80 and len(b) >= 80 and a[0] == b[0] == 0x50:
            a[40:64] = b'\x00' * 24
            b[40:64] = b'\x00' * 24
        api.require(left.addr == right.addr and a == b,
                    'Bootloader code/config differs from rollback-enabled build; use full mode.')


def update(args, app, expected_table, gen, dest, api):
    table = gen.PartitionTable.from_binary(expected_table)
    table.verify()
    parts = {p.name: p for p in table}
    api.require(all(name in parts for name in ('ota_0', 'ota_1', 'otadata')),
                'Missing OTA partitions.')
    ota = parts['otadata']
    api.require(ota.size == 2 * SECTOR and ota.offset % SECTOR == 0, 'Invalid OTA layout.')
    with tempfile.TemporaryDirectory(prefix='highboy-ota-') as directory:
        temporary = Path(directory)
        def read(offset, size):
            path = temporary / 'read.bin'
            api.run_tool(args, 'read_flash', '--no-progress', hex(offset), hex(size), str(path))
            data = path.read_bytes()
            api.require(len(data) == size, 'Incomplete OTA preflight read.')
            return data
        print('OTA preflight: checking partition map and rollback bootloader', flush=True)
        api.require(read(api.TABLE_OFFSET, api.TABLE_SIZE) == expected_table[:api.TABLE_SIZE],
                    'Installed partition map differs; use full mode to migrate.')
        boot = read(api.BOOT_OFFSET, api.TABLE_OFFSET - api.BOOT_OFFSET)
        verify_bootloader(boot, args.bootloader.read_bytes(), api, args.target)
        boot_path = temporary / 'bootloader.bin'
        boot_path.write_bytes(boot)
        metadata = read(ota.offset, ota.size)
        active, seq, sector = selection(metadata)
        old, new = parts[f'ota_{active}'], parts[f'ota_{1-active}']
        api.require(len(app) <= new.size, 'App does not fit the inactive slot.')
        # Read only the current image, not the entire flash or unused slot padding.
        header = read(old.offset, 24)
        api.require(header[0] == 0xe9 and 0 < header[1] <= 16 and header[23] == 1,
                    'Current OTA image header is invalid; use full recovery.')
        position = 24
        for _ in range(header[1]):
            api.require(position + 8 <= old.size, 'Current image exceeds its slot.')
            segment = read(old.offset + position, 8)
            length, = struct.unpack_from('<I', segment, 4)
            position += 8 + length
            api.require(position < old.size, 'Current image exceeds its slot.')
        size = (position + 16) // 16 * 16 + 32
        api.require(size <= old.size, 'Current image digest exceeds its slot.')
        current = bytearray()
        for offset in range(0, size, api.BACKUP_CHUNK_SIZE):
            print(f'OTA preflight: {offset}/{size} bytes', flush=True)
            current.extend(read(old.offset + offset, min(api.BACKUP_CHUNK_SIZE, size-offset)))
        print(f'OTA preflight: {size}/{size} bytes', flush=True)
        api.image_valid(bytes(current), args.target)
        current_path = temporary / 'current.bin'
        current_path.write_bytes(current)
        metadata_path = temporary / 'otadata.bin'
        metadata_path.write_bytes(metadata)
        candidate = dest / 'candidate.bin'
        candidate.write_bytes(app)
        report = dict(mode='app', chip=f'esp32{args.target}', status='checked',
                      old_slot=old.name, slot=new.name, offset=new.offset,
                      app_sha256=api.digest(app), other_chip_updated=False,
                      full_backup=False, rollback='NEW requires firmware confirmation')
        manifest = dest / 'manifest.json'
        def save():
            manifest.write_text(json.dumps(report, indent=2) + '\n')
        save()
        print(f'OTA: preserve {old.name}; write {new.name} at {new.offset:#x}. No full backup.', flush=True)
        if args.check:
            return
        api.run_tool(args, 'verify_flash', hex(old.offset), str(current_path),
                     hex(ota.offset), str(metadata_path), hex(api.BOOT_OFFSET), str(boot_path))
        report['status'] = 'writing_inactive'; save()
        api.run_tool(args, 'write_flash', hex(new.offset), str(candidate))
        api.run_tool(args, 'verify_flash', hex(new.offset), str(candidate))
        # Never erase both selection sectors: retain a valid fallback at every step.
        if sector is None:
            fallback = dest / 'ota-fallback.bin'
            fallback.write_bytes(entry(1, STATE_VALID))
            api.run_tool(args, 'write_flash', hex(ota.offset), str(fallback))
            api.run_tool(args, 'verify_flash', hex(ota.offset), str(fallback))
            sector = 0
        switch = dest / 'ota-next.bin'
        switch.write_bytes(entry(seq + 1, STATE_NEW))
        report['status'] = 'selecting_verified_app'; save()
        address = ota.offset + (1-sector) * SECTOR
        api.run_tool(args, 'write_flash', hex(address), str(switch))
        api.run_tool(args, 'verify_flash', hex(address), str(switch), hex(old.offset), str(current_path))
        report['status'] = 'written_and_verified'; save()
        print('Inactive OTA app verified and selected as NEW; firmware must confirm health after boot.', flush=True)
