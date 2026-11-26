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

"""Terminal presentation for the guarded updater; never opens a device itself."""
import codecs
import errno
import math
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time

# Pixel silhouette redrawn from the user's Octobit reference; W=outline, P=body, B=eyes/mouth.
OCTOBIT = (
    "        WWWWW        ",
    "      WWWPPPWWW      ",
    "     WWPPPPPPPWW     ",
    "     WPPPPPPPPPW     ",
    "     WBWPPPPPWBW     ",
    "     WBBPPPPPBBW     ",
    "     WPPPPPPPPPW     ",
    " WWWWWPPPBBPPPPWWWWW ",
    " WPWPPPPPPPPPPPPWPW ",
    " WWPPPWPPPPPWPPPPWW ",
    "  WWWPPPPPPPPPPWWW  ",
    " WWPPPPPPPPPPPPPPWW ",
    " WPWWPPWPPPWPPWWPW  ",
    " WWWPPPPWWWPPPPWWW  ",
    "   WPWWWW WWWWPW    ",
    "   WWPPW   WPPWW    ",
    "    WWWW   WWWW     ",
)
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]|\x1b\][^\x07]*(?:\x07|\x1b\\)")
REFRESH_SECONDS = 0.12
TERMINATE_SECONDS = 5


def clean(text):
    return ''.join(c for c in ANSI.sub('', text) if c == '\t' or c.isprintable())


class FlashState:
    def __init__(self):
        self.stage = 'Preparing'
        self.percent = None
        self.detail = 'Checking build and deployment guards'

    def consume(self, line):
        text = clean(line).strip()
        if not text:
            return
        self.detail = text
        backup = re.search(r'^Backup: (\d+)/(\d+) KiB', text)
        writing = re.search(r'Writing at .*?(\d+(?:\.\d+)?)\s*%', text)
        preflight = re.search(r'^OTA preflight: (\d+)/(\d+) bytes', text)
        if text.startswith('OTA preflight:'):
            self.stage, self.percent = 'Checking current app', None
            if preflight:
                done, total = map(int, preflight.groups())
                self.percent = min(100, done * 100 / total) if total else None
        elif backup:
            done, total = map(int, backup.groups())
            self.stage = 'Backing up'
            self.percent = min(100, done * 100 / total) if total else None
        elif 'Reading the entire flash' in text:
            self.stage, self.percent = 'Backing up', 0
        elif text.startswith('Selected app:'):
            self.stage, self.percent = 'Validating', None
        elif writing:
            self.stage, self.percent = 'Flashing', min(100, float(writing[1]))
        elif text.startswith(('Writing ', 'Flash will be erased', 'Compressed ')):
            self.stage, self.percent = 'Flashing', None
        elif text.startswith(('Verifying', 'Hash of data verified', 'Verification successful')):
            self.stage, self.percent = 'Verifying', None
        elif 'resetting' in text.lower():
            self.stage, self.percent = 'Resetting', None
        elif text.startswith('Connecting') and self.stage == 'Preparing':
            self.stage, self.percent = 'Connecting', None



# Fixed lighting and dithering keep the mascot stable while progress redraws.
ASCII_SHADES = ".:;+=xX$@"
BODY_COLORS = (235, 54, 55, 91, 92, 129, 135, 177, 219)
OUTLINE_COLORS = (238, 240, 243, 245, 247, 250, 252, 254, 255)
EYE_COLOR = 234


def mascot_lines(color):
    lines = []
    for y, row in enumerate(OCTOBIT):
        pixels = []
        for x, material in enumerate(row):
            for half in range(2):
                if material == ' ':
                    pixels.append(' ')
                    continue
                px = x + half / 2
                highlight = math.exp(-((px - 7) / 6) ** 2 - ((y - 3) / 10) ** 2)
                grain = (((x * 37 + y * 19 + half * 13) % 23) / 22 - 0.5) * 0.28
                light = 0.12 + highlight * 0.68 + grain
                if material == 'W':
                    light += 0.18
                shade = max(0, min(len(ASCII_SHADES) - 1, int(light * len(ASCII_SHADES))))
                if material == 'B':
                    ink, glyph = EYE_COLOR, '.' if (x + y + half) % 3 == 0 else ' '
                else:
                    palette = OUTLINE_COLORS if material == 'W' else BODY_COLORS
                    ink, glyph = palette[shade], ASCII_SHADES[shade]
                pixels.append((f'\x1b[38;5;{ink}m' if color else '') + glyph)
        lines.append(''.join(pixels) + ('\x1b[0m' if color else ''))
    return lines


def frame(state, columns, rows, color=True, elapsed=0):
    width = max(1, columns - 2)
    if columns < 24 or rows < 10:
        return clean(f'{state.stage}: {state.detail}')[:width]
    purple = '\x1b[95m' if color else ''
    white = '\x1b[97m' if color else ''
    reset = '\x1b[0m' if color else ''
    lines = [purple + 'HIGH BOY / FLASH'.center(min(width, 50)) + reset]
    if width >= 44 and rows >= 24:
        lines.extend('  ' + row for row in mascot_lines(color))
    else:
        lines += [purple + '  .--.  ' + reset, purple + ' (o  o) ' + reset,
                  purple + ' /||||\\ ' + reset]
    label = f'{state.stage} | {elapsed:.0f}s'
    lines += ['', white + label[:width] + reset]
    bar_width = max(4, min(width - 10, 60))
    if state.percent is None:
        bar, suffix = '.' * bar_width, ''
    else:
        filled = int(bar_width * state.percent / 100)
        bar = '=' * filled + ' ' * (bar_width - filled)
        suffix = f' {state.percent:3.0f}%'
    lines.append(purple + '[' + bar + ']' + suffix + reset)
    lines.append(state.detail[:width])
    return '\n'.join(lines)


def run_visual(command, stream=sys.stdout):
    import fcntl
    import pty
    import termios
    log_fd, log_name = tempfile.mkstemp(prefix='highboy-flash-', suffix='.log')
    state = FlashState()
    master, slave = -1, -1
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    old_term = signal.signal(signal.SIGTERM, interrupted)
    dimensions = shutil.get_terminal_size((80, 30))
    process = None
    result = 2
    started = time.monotonic()
    decoder = codecs.getincrementaldecoder('utf-8')('replace')
    pending = ''
    try:
        with os.fdopen(log_fd, 'wb', buffering=0) as log, selectors.DefaultSelector() as selector:
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', dimensions.lines, dimensions.columns, 0, 0))
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=slave, stderr=slave,
                                       start_new_session=True)
            os.close(slave)
            slave = -1
            selector.register(master, selectors.EVENT_READ)
            stream.write('\x1b[?1049h\x1b[?25l')
            stream.flush()
            eof = False
            while not eof:
                for key, _ in selector.select(REFRESH_SECONDS):
                    try:
                        data = os.read(key.fd, 65536)
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
                        data = b''
                    if not data:
                        eof = True
                        break
                    log.write(data)
                    pending += decoder.decode(data)
                    pieces = re.split('[\r\n]', pending)
                    pending = pieces.pop()
                    for piece in pieces:
                        state.consume(piece)
                    if len(pending) > 8192:
                        state.consume(pending)
                        pending = ''
                dimensions = shutil.get_terminal_size((80, 30))
                fcntl.ioctl(master, termios.TIOCSWINSZ,
                            struct.pack('HHHH', dimensions.lines, dimensions.columns, 0, 0))
                stream.write('\x1b[H\x1b[J' + frame(state, dimensions.columns, dimensions.lines,
                                                    elapsed=time.monotonic() - started))
                stream.flush()
            state.consume(pending + decoder.decode(b'', final=True))
            result = process.wait()
    except KeyboardInterrupt:
        result = 130
        state.detail = 'Interrupted; the device may remain in download mode'
    except (OSError, subprocess.SubprocessError) as error:
        state.detail = f'Flash presentation failed: {error}'
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        if process is not None and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGINT)
            except ProcessLookupError:
                pass
            deadline = time.monotonic() + TERMINATE_SECONDS
            os.set_blocking(master, False)
            with open(log_name, 'ab', buffering=0) as log:
                while process.poll() is None and time.monotonic() < deadline:
                    try:
                        data = os.read(master, 65536)
                        if data:
                            log.write(data)
                    except OSError as error:
                        if error.errno not in (errno.EIO, errno.EAGAIN):
                            raise
                    time.sleep(0.02)
                if process.poll() is None:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                process.wait()
                while True:
                    try:
                        data = os.read(master, 65536)
                        if not data:
                            break
                        log.write(data)
                    except OSError:
                        break
        if master >= 0:
            os.close(master)
        if slave >= 0:
            os.close(slave)
        signal.signal(signal.SIGTERM, old_term)
        stream.write('\x1b[?25h\x1b[?1049l')
        stream.flush()
    if result == 0:
        state.stage, state.percent = 'Complete', 100
        state.detail = 'Check complete; no write requested' if '--check' in command else 'Updater completed successfully'
        stream.write(frame(state, dimensions.columns, dimensions.lines, elapsed=time.monotonic() - started) + '\n')
    else:
        stream.write(f'\nFlash stopped (exit {result}): {state.detail}\nFull tool output:\n')
        with open(log_name, encoding='utf-8', errors='replace') as log:
            for line in log:
                stream.write(clean(line) + '\n')
    stream.write(f'Full log: {log_name}\n')
    stream.flush()
    return result if result >= 0 else 128 - result


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    preview = '--preview' in args
    plain = '--plain' in args
    args = [arg for arg in args if arg not in ('--preview', '--plain')]
    updater = Path(__file__).with_name('flash.py')
    if preview:
        state = FlashState()
        state.stage, state.percent, state.detail = 'Preview', 65, 'Artwork preview only - no device access'
        columns, rows = shutil.get_terminal_size((80, 30))
        print(frame(state, columns, rows, color=sys.stdout.isatty() and 'NO_COLOR' not in os.environ))
        return 0
    if '-h' in args or '--help' in args:
        print("""Usage: tools/flash.sh [p4|c5|both] [options]

No chip: select ESP32-P4, ESP32-C5 or Both chips with Up/Down and Enter.
Both chips: preflight both builds, then update C5 followed by P4; stop on failure.
No port: detect the chip's CP2105 USB interface; select a device if several match.

Options:
  --mode app|full  Select application update or full flash without the mode menu
  -p, --port PORT   Use this port instead of USB interface detection
  --p4-port PORT   P4 port for both mode (use with --c5-port)
  --c5-port PORT   C5 port for both mode (use with --p4-port)
  --control-port PORT  P4 console for C5 boot and post-update SPI confirmation
  --build-dir DIR  Compatibility option; only firmware_<chip>/build is accepted
  -b, --baud BAUD   Override both read and write speeds
  --read-baud BAUD  Read speed (P4/C5: 230400)
  --write-baud BAUD Write speed (P4: 921600; C5: 460800)
  --before MODE    default-reset or no-reset (after entering the bootloader)
  --check          Validate without writing (full mode backs up); resets the target
  --idf-path PATH  ESP-IDF path override
  --plain          Keep original updater output
  --preview        Preview Octobit without accessing a device
  -h, --help       Show this help

Deployment guards, backups and chip validation remain enabled.""")
        return 0
    from flash_ports import resolve_port, resolve_mode
    try:
        args = resolve_mode(args)
        args = resolve_port(args)
    except KeyboardInterrupt:
        print('Cancelled; no flash started.', file=sys.stderr)
        return 130
    except (ValueError, ImportError) as error:
        print(f'Port selection failed: {error}', file=sys.stderr)
        return 2
    command = [sys.executable, '-u', str(updater), *args]
    rich = (not plain and sys.stdout.isatty() and os.environ.get('TERM', '') not in ('', 'dumb')
            and 'NO_COLOR' not in os.environ and not os.environ.get('CI'))
    if not rich:
        os.execv(sys.executable, command)
    return run_visual(command)


if __name__ == '__main__':
    sys.exit(main())
