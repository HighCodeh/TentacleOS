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

"""Generate the pinned P4 line-editor fix without modifying ESP-IDF sources."""
import argparse
import hashlib
from pathlib import Path

SOURCE_SHA256 = '07a402902a665eb49e65140db116438ed3c69191ccfc8ae469853690c1a7f6ab'
ORIGINAL = b'if ( (getMillis() - t1) < LINENOISE_PASTE_KEY_DELAY && c != ENTER) {'
CORRECTED = b'''if ((getMillis() - t1) < LINENOISE_PASTE_KEY_DELAY &&
            (unsigned char)c >= ' ' && (unsigned char)c != BACKSPACE &&
            l.pos == l.len && !hintsCallback) {'''


def patched_source(source):
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256 or source.count(ORIGINAL) != 1:
        raise ValueError('Unreviewed linenoise source; expected the pinned ESP-IDF v6.1 file')
    result = source.replace(ORIGINAL, CORRECTED)
    # Rendering only: ANSI bytes never enter the editable command or its cursor length.
    helper = br"""
static void highboyAppendInput(struct abuf *ab, struct linenoiseState *l,
                                const char *buf, size_t len) {
    size_t command_end = strcspn(l->buf, " ");
    size_t offset = (size_t)(buf - l->buf);
    size_t command_len = offset < command_end ? command_end - offset : 0;
    if (command_len > len) command_len = len;
    abAppend(ab, "\033[96m", 5);
    abAppend(ab, buf, command_len);
    abAppend(ab, "\033[0m", 4);
    abAppend(ab, buf + command_len, len - command_len);
}
"""
    marker = b'/* Helper of refreshSingleLine()'
    result = result.replace(marker, helper + b'\n' + marker, 1)
    result = result.replace(b'abAppend(&ab,buf,len);', b'highboyAppendInput(&ab,l,buf,len);')
    result = result.replace(b'abAppend(&ab,l->buf,l->len);', b'highboyAppendInput(&ab,l,l->buf,l->len);')
    # Only the ANSI editor uses color; the dumb terminal path stays plain.
    result = result.replace(b'    l.prompt = prompt;',
        b'    if (strstr(prompt, "highboy>") != NULL) prompt = "\\033[95mhighboy> \\033[0m";\n    l.prompt = prompt;')
    return result


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
