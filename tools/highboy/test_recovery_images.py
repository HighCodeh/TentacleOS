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

"""Exercise real esptool and built paired apps in synthetic, non-bootable flash fixtures."""
import argparse
import json
from pathlib import Path
import tempfile

from esptool import bin_image
import recovery


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='highboy-recovery-images-') as temp:
        root = Path(temp); backups = {}
        for chip, profile in recovery.verify_package.PROFILE.items():
            directory = root / chip; directory.mkdir()
            image = getattr(bin_image, 'ESP32' + chip + 'FirmwareImage')()
            address = 0x4ff00000 if chip == 'P4' else 0x40800000
            image.segments = [bin_image.ELFSection(b'.dram0.data', address, b'\x00' * 16)]
            image.entrypoint = address
            boot = directory / 'image-container.bin'; image.save(boot)
            data = bytearray(b'\xff' * recovery.FLASH_SIZE)
            boot_data = boot.read_bytes()
            data[recovery.BOOT_OFFSET:recovery.BOOT_OFFSET + len(boot_data)] = boot_data
            table = (recovery.ROOT / ('firmware_' + chip.lower()) / 'build/partition_table/partition-table.bin').read_bytes()
            data[recovery.TABLE_OFFSET:recovery.TABLE_OFFSET + len(table)] = table
            application = (args.candidate / profile['file']).read_bytes()
            data[recovery.APP_OFFSET:recovery.APP_OFFSET + len(application)] = application
            data[recovery.ASSETS[chip][0]] = 0x42
            backup = directory / 'flash.bin'; backup.write_bytes(data); backups[chip] = backup
            (directory / 'manifest.json').write_text(json.dumps({
                'chip':'esp32'+chip.lower(), 'backup_bytes':len(data),
                'backup_sha256':recovery.digest(data), 'status':'checked',
                'origin':'synthetic non-bootable host fixture; no assets filesystem'}))
        output = recovery.prepare(args.candidate, backups, root / 'review')
        report = recovery.verify(output)
        assert not report['assets_filesystem_validated'] and not report['deployment_approved']
        for chip, profile in recovery.verify_package.PROFILE.items():
            data = bytearray((args.candidate / profile['file']).read_bytes())
            data[-1] ^= 1
            try: recovery.verify_image(data, chip)
            except ValueError: pass
            else: raise AssertionError('corrupt image accepted')
        try: recovery.verify_image((args.candidate/'TentacleOS_C5.bin').read_bytes(), 'P4')
        except ValueError: pass
        else: raise AssertionError('crossed chip accepted')
    print('PASS: real esptool checksum/digest/chip checks, paired built apps and private review publication; synthetic boot containers/assets, no boot/device acceptance')


if __name__ == '__main__': main()
