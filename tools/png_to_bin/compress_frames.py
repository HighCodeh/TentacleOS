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

"""Losslessly compress generated ARGB8888 frames using the pinned LVGL converter."""
import argparse
from pathlib import Path
import struct

from LVGLImage import ColorFormat, CompressMethod, LVGLCompressData
import lz4.block

IMAGE_HEADER = struct.Struct('<BBHHHHH')
COMPRESSED_HEADER = struct.Struct('<III')
IMAGE_MAGIC = 0x19
IMAGE_COMPRESSED = 0x08
PIXEL_BYTES = 4
LITTLEFS_BLOCK_BYTES = 4096


def license_notice():
    lines = Path(__file__).read_text().splitlines()[1:]
    end = next(i for i, line in enumerate(lines) if not line.startswith('#'))
    return '\n'.join(line.removeprefix('#').removeprefix(' ') for line in lines[:end]) + '\n'


def compress_file(path):
    original = path.read_bytes()
    magic, color, flags, width, height, stride, reserved = IMAGE_HEADER.unpack_from(original)
    if (magic != IMAGE_MAGIC or color != ColorFormat.ARGB8888.value or flags != 0 or
            reserved != 0 or not width or not height or stride != width * PIXEL_BYTES or
            len(original) != IMAGE_HEADER.size + stride * height):
        raise ValueError(f'Expected an uncompressed generated ARGB8888 frame: {path}')
    pixels = original[IMAGE_HEADER.size:]
    payload = LVGLCompressData(ColorFormat.ARGB8888, CompressMethod.LZ4, pixels).compressed
    method, packed_size, unpacked_size = COMPRESSED_HEADER.unpack_from(payload)
    if (method != CompressMethod.LZ4.value or packed_size != len(payload) - COMPRESSED_HEADER.size or
            unpacked_size != len(pixels) or
            lz4.block.decompress(payload[COMPRESSED_HEADER.size:], uncompressed_size=len(pixels)) != pixels):
        raise ValueError(f'Frame compression round trip failed: {path}')
    result = IMAGE_HEADER.pack(magic, color, IMAGE_COMPRESSED, width, height, stride, reserved) + payload
    # Include a conservative full block for the generated license sidecar.
    blocks = lambda size: (size + LITTLEFS_BLOCK_BYTES - 1) // LITTLEFS_BLOCK_BYTES
    if blocks(len(result)) + 1 >= blocks(len(original)):
        return 0
    notice = path.with_suffix(path.suffix + '.license')
    if not notice.exists():
        notice.write_text(license_notice(), encoding='utf-8')
    path.write_bytes(result)
    return len(original) - len(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    files = sorted(args.directory.glob('*.bin'))
    saved = sum(compress_file(path) for path in files)
    print(f'Frames: {len(files)} checked, {saved} payload bytes saved with lossless LZ4')


if __name__ == '__main__':
    main()
