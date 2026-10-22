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

"""Generate the pinned P4 line-editor fix without modifying ESP-IDF sources."""
import argparse
import hashlib
from pathlib import Path

SOURCE_SHA256 = '38753ab0df3140232ed65a9b412ba670497eaebe424a45098eaaebb2dc5c4fce'
ORIGINAL = b'if ( (getMillis() - t1) < LINENOISE_PASTE_KEY_DELAY && c != ENTER) {'
CORRECTED = b'''if ((getMillis() - t1) < LINENOISE_PASTE_KEY_DELAY &&
            (unsigned char)c >= ' ' && (unsigned char)c != BACKSPACE &&
            l.pos == l.len) {'''


def patched_source(source):
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256 or source.count(ORIGINAL) != 1:
        raise ValueError('Unreviewed linenoise source; expected the pinned ESP-IDF v5.5.3 file')
    return source.replace(ORIGINAL, CORRECTED)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.source.resolve() == args.out.resolve():
            raise ValueError('Output must differ from the SDK source')
        data = patched_source(args.source.read_bytes())
        args.out.parent.mkdir(parents=True, exist_ok=True)
        if not args.out.exists() or args.out.read_bytes() != data:
            args.out.write_bytes(data)
    except (ValueError, OSError) as error:
        raise SystemExit('Console patch failed: ' + str(error)) from error


if __name__ == '__main__':
    main()
