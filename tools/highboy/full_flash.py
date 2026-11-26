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

"""Validate the exact IDF full-flash bundle before any device write."""
import json
from pathlib import Path

SECTOR_SIZE = 0x1000
BOOT_OFFSET = 0x2000
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0xC00


def require(condition, message):
    if not condition:
        raise ValueError(message)


def prepare(build, target, flash_size, expected_table, app, gen, validate_image):
    table = gen.PartitionTable.from_binary(expected_table)
    table.verify()
    partitions = {entry.name: entry for entry in table}
    require(all(name in partitions for name in ('nvs', 'phy_init', 'otadata', 'ota_0', 'assets', 'coredump')),
            'Full flash requires the complete reviewed partition layout.')
    specs = [
        ('bootloader', BOOT_OFFSET, TABLE_OFFSET - BOOT_OFFSET, 'bootloader/bootloader.bin'),
        ('partition-table', TABLE_OFFSET, SECTOR_SIZE, 'partition_table/partition-table.bin'),
        ('otadata', partitions['otadata'].offset, partitions['otadata'].size, 'ota_data_initial.bin'),
        ('app', partitions['ota_0'].offset, partitions['ota_0'].size, f'TentacleOS_{target.upper()}.bin'),
        ('assets', partitions['assets'].offset, partitions['assets'].size, 'assets.bin'),
    ]
    generated = json.loads((build / 'flasher_args.json').read_text())
    expected_files = {offset: name for _, offset, _, name in specs}
    actual_files = {int(offset, 0): name for offset, name in generated['flash_files'].items()}
    require(len(actual_files) == len(generated['flash_files']) and actual_files == expected_files,
            'Full flash files/offsets differ from the reviewed layout; rebuild first.')
    images = []
    for label, offset, limit, name in specs:
        path = (build / name).resolve()
        require(path.is_relative_to(build.resolve()), 'Full flash input escapes the build directory.')
        require(path.is_file(), f'Missing {name}; run tools/build.sh {target} first.')
        require(0 < path.stat().st_size <= limit, f'{name} does not fit its flash region.')
        data = path.read_bytes()
        require(len(data) <= limit, f'{name} changed size while reading.')
        if label == 'bootloader':
            image = validate_image(data, target)
            capacity = f'{flash_size // (1024 * 1024)}MB'
            require(image.flash_size_freq & 0xF0 == image.ROM_LOADER.FLASH_SIZES[capacity],
                    'Bootloader flash size differs from the build; rebuild the bootloader.')
        elif label == 'partition-table':
            require(data == expected_table, 'Partition table changed while preparing full flash.')
        elif label == 'app':
            require(data == app, 'Application changed while preparing full flash.')
            validate_image(data, target)
        elif label == 'otadata':
            require(len(data) == limit and data == b'\xff' * limit,
                    'Initial OTA data must select the factory/default ota_0 image.')
        elif label == 'assets':
            require(len(data) == limit and data != b'\xff' * limit and data != b'\x00' * limit,
                    'Assets image must fill its partition and contain a filesystem.')
        images.append((label, offset, data))
    dump = partitions['coredump']
    images.append(('coredump', dump.offset, b'\xff' * dump.size))
    previous_end = 0
    for _, offset, data in sorted(images, key=lambda item: item[1]):
        end = (offset + len(data) + SECTOR_SIZE - 1) // SECTOR_SIZE * SECTOR_SIZE
        require(offset % SECTOR_SIZE == 0 and offset >= previous_end and end <= flash_size,
                'Overlapping, unaligned or out-of-capacity full-flash region.')
        for name in ('nvs', 'phy_init'):
            preserved = partitions[name]
            require(end <= preserved.offset or offset >= preserved.offset + preserved.size,
                    f'Full flash would overwrite {name}.')
        previous_end = end
    return images


def validate_migration(backup, expected_table, gen):
    if backup == b'\xff' * len(backup):
        return
    installed = gen.PartitionTable.from_binary(backup[TABLE_OFFSET:TABLE_OFFSET + TABLE_SIZE])
    installed.verify()
    require(all(entry.offset + entry.size <= len(backup) for entry in installed),
            'Installed partition map extends beyond the device.')
    new = {entry.name: entry for entry in gen.PartitionTable.from_binary(expected_table)}
    old = {entry.name: entry for entry in installed}
    for name in ('nvs', 'phy_init'):
        require(name in old and name in new, f'Missing preserved {name} partition.')
        require((old[name].offset, old[name].size, old[name].type, old[name].subtype) ==
                (new[name].offset, new[name].size, new[name].type, new[name].subtype),
                f'Migration moves {name}; explicit data migration is required.')
