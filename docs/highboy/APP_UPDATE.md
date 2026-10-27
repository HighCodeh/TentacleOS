<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# High Boy application update assets

Each release contains two separate application binaries built with ESP-IDF v6.1:
`TentacleOS-P4-<version>-app-only.bin` and
`TentacleOS-C5-<version>-app-only.bin`. Keep the pair together: both chips must
use command protocol v2 and SPI transport revision 1. Updating P4 does not update C5.

These files are for matching existing installations. They contain no bootloader,
partition table, filesystem/assets image or security provisioning. They cannot
restore an erased device or migrate a different partition layout. The preserved
P4 application slot is 0x2D0000 bytes; the C5 slot is 0x200000 bytes. Slot size
alone does not establish installation compatibility.

Verify the downloaded files against `SHA256SUMS` before use. The per-chip
`build-evidence.json` reports bind the binaries to their build outputs and static
memory/compiler checks; they do not certify physical-device validation.

For an already running matching installation, use the existing independent SD
OTA inputs: the P4 binary goes to `/update/tentacleos.bin`, and the C5 binary to
`/c5/TentacleOS_C5.bin`. Invoke each chip's updater; copying both files does not
itself update both chips. Back up the device first and confirm both updates
before using features that depend on the paired SPI transport.

Do not write these application files at flash offset zero or use them as a full
flash image. For deployment details, consult `docs/highboy/README.md` and
`docs/ota/README.md` at the corresponding release tag in the repository.
