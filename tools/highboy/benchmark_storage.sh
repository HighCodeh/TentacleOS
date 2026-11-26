#!/bin/bash
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

# macOS file benchmark; never writes directly to a block device.
set -euo pipefail
exec python3 -u - "$@" <<'PYTHON'
import argparse
import fcntl
import hashlib
import os
from pathlib import Path
import signal
import sys
import tempfile
import threading
import time

MIB = 1024 * 1024
F_NOCACHE = 48
F_FULLFSYNC = 51
SYNC_MIB = 8
HEARTBEAT_SECONDS = 5

parser = argparse.ArgumentParser(prog="benchmark_storage.sh", description="Measure USB Storage write and read speeds on macOS using a temporary file.")
parser.add_argument("volume", nargs="?", default="/Volumes/Highboy", help="mounted volume (default: /Volumes/Highboy)")
parser.add_argument("--size-mib", type=int, default=32, help="test size in MiB, from 1 to 4096 (default: 32)")
parser.add_argument("--sync-at-end", action="store_true", help="sync only after the entire write; include the final flush in write timing")
args = parser.parse_args()
if sys.platform != "darwin":
    parser.error("this script requires macOS cache controls")
if not 1 <= args.size_mib <= 4096:
    parser.error("--size-mib must be between 1 and 4096")
volume = Path(args.volume).resolve()
if volume.parent != Path("/Volumes") or not os.path.ismount(volume):
    parser.error("specify a volume mounted directly under /Volumes, not a directory or /dev/disk")
size = args.size_mib * MIB
stats = os.statvfs(volume)
if stats.f_bavail * stats.f_frsize < size + 16 * MIB:
    parser.error("not enough free space for the test and the 16 MiB reserve")


def interrupted(signum, frame):
    raise KeyboardInterrupt


signal.signal(signal.SIGTERM, interrupted)
fd = None
path = None
result = None
status = 0
progress = ("Preparing", time.monotonic())
stop_reporter = threading.Event()


def report_wait():
    while not stop_reporter.wait(HEARTBEAT_SECONDS):
        phase, since = progress
        elapsed = time.monotonic() - since
        if elapsed >= HEARTBEAT_SECONDS:
            print(f"  Waiting: {phase} ({elapsed:.0f} s in this operation)", flush=True)


reporter = threading.Thread(target=report_wait, daemon=True)
reporter.start()
try:
    block = os.urandom(MIB)
    expected = hashlib.sha256()
    for _ in range(args.size_mib):
        expected.update(block)
    fd, path = tempfile.mkstemp(prefix=".highboy-speed-test-", suffix=".tmp", dir=volume)
    fcntl.fcntl(fd, F_NOCACHE, 1)
    sync_mode = "sync at end" if args.sync_at_end else f"sync every {SYNC_MIB} MiB"
    print(f"Volume: {volume}\nTest: {args.size_mib} MiB | F_NOCACHE requested | {sync_mode}")
    print("Writing... do not disconnect or leave Storage mode.")
    started = time.monotonic()
    for index in range(args.size_mib):
        progress = (f"writing MiB {index + 1}/{args.size_mib}", time.monotonic())
        remaining = memoryview(block)
        while remaining:
            count = os.write(fd, remaining)
            if count <= 0:
                raise OSError("incomplete write")
            remaining = remaining[count:]
        is_final = index + 1 == args.size_mib
        is_checkpoint = (index + 1) % SYNC_MIB == 0
        if args.sync_at_end and is_checkpoint and not is_final:
            print(f"  Submitted: {index + 1}/{args.size_mib} MiB (final sync pending)", flush=True)
        if is_final or (is_checkpoint and not args.sync_at_end):
            progress = (f"syncing up to {index + 1}/{args.size_mib} MiB", time.monotonic())
            print(f"  Syncing up to {index + 1}/{args.size_mib} MiB...", flush=True)
            os.fsync(fd)
            fcntl.fcntl(fd, F_FULLFSYNC)
            elapsed = time.monotonic() - started
            print(f"  Synced: {index + 1}/{args.size_mib} MiB | "
                  f"average {(index + 1) * MIB / 1_000_000 / elapsed:.2f} MB/s", flush=True)
    write_seconds = time.monotonic() - started
    os.close(fd)
    fd = None

    print("Reading and verifying SHA-256...", flush=True)
    fd = os.open(path, os.O_RDONLY)
    fcntl.fcntl(fd, F_NOCACHE, 1)
    actual = hashlib.sha256()
    total = 0
    started = time.monotonic()
    while True:
        progress = (f"reading after {total // MIB}/{args.size_mib} MiB", time.monotonic())
        data = os.read(fd, MIB)
        if not data:
            break
        total += len(data)
        actual.update(data)
        if total % (SYNC_MIB * MIB) == 0 or total == size:
            print(f"  Read: {total / MIB:g}/{args.size_mib} MiB", flush=True)
    read_seconds = time.monotonic() - started
    if total != size or actual.digest() != expected.digest():
        raise OSError("INTEGRITY FAILURE: read data differs from written data")
    result = (write_seconds, read_seconds)
except KeyboardInterrupt:
    print("\nTest interrupted; results discarded.", file=sys.stderr)
    status = 130
except OSError as error:
    print(f"\nTest failed: {error}", file=sys.stderr)
    status = 1
finally:
    progress = ("closing and removing the temporary file", time.monotonic())
    try:
        if fd is not None:
            os.close(fd)
    except OSError as error:
        print(f"Failed to close file: {error}", file=sys.stderr)
        status = 1
    if path is not None:
        try:
            os.unlink(path)
            print("Temporary file removed.")
        except OSError as error:
            print(f"Could not remove {path}: {error}", file=sys.stderr)
            status = 1
    stop_reporter.set()
    reporter.join()
if result is not None and status == 0:
    print("\nResults (decimal MB/s):")
    for label, seconds in zip(("Write", "Read"), result):
        print(f"  {label:7}: {size / 1_000_000 / seconds:7.2f} MB/s | {args.size_mib / seconds:.2f} MiB/s | {seconds:.2f} s")
    print("SHA-256 integrity: OK")
    print("Filesystem measurement including synchronization and verification; not raw bus throughput.")
sys.exit(status)
PYTHON
