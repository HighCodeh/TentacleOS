#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. See <https://www.gnu.org/licenses/>.
"""
hb_push - push a file from the PC to a TentacleOS (Highboy) device over the
native USB companion link, no SD adapter needed.

Usage:
    python hb_push.py <local_file> <device_path> [options]

Examples:
    python hb_push.py firmware.json /sdcard/config/OTA/firmware.json
    python hb_push.py rick.txt /sdcard/storage/bad_usb_scripts/rick.txt --port COM7

The PSK (device pairing key) comes from, in order: --psk, the HB_PSK env var,
or an interactive prompt. Get it on the device at Settings -> PAIRING (hex), or
on the dev console with `hostlink psk`.

Also supports pulling and listing:
    python hb_push.py --pull /sdcard/log.txt out.txt
    python hb_push.py --ls   /sdcard

Requires: pip install pyserial   (nothing else - HKDF is done with stdlib).
"""

import argparse
import hashlib
import hmac
import os
import struct
import sys
import time

try:
    import serial  # pyserial
    from serial.tools import list_ports
except ImportError:
    sys.exit("missing dependency: pip install pyserial")

# --- wire constants (must match firmware host_link.h / host_link_sec.c) --------
MAGIC = b"\x48\x42"          # 'H' 'B'
VER = 1
FLAG_AUTH = 0x01
HDR_SIZE = 10                # MAGIC(2)+VER(1)+FLAGS(1)+COUNTER(4)+LEN(2)
MAC_SIZE = 16                # truncated HMAC-SHA256
NONCE_SIZE = 16
DEVICE_ID_SIZE = 6
PSK_SIZE = 32

TYPE_CMD = 0x01
TYPE_RESP = 0x02
TYPE_HELLO = 0x10
TYPE_HELLO_ACK = 0x11

CAT_SYSTEM = 0x00
OP_FILE_LIST = 0x40
OP_FILE_READ = 0x42
OP_FILE_WRITE = 0x43

FILE_WRITE_TRUNCATE = 0x01
CHUNK = 1024                 # HOST_FILE_DATA_MAX; keep <= this per FILE_WRITE

STATUS = {0: "OK", 1: "BUSY", 2: "ERROR", 3: "UNSUPPORTED", 4: "INVALID_ARG"}

USB_VID = 0xCAFE             # native companion CDC (VID CAFE / PID 4001)
USB_PID = 0x4001


# --- crypto (stdlib only) -----------------------------------------------------
def hmac16(key, data):
    return hmac.new(key, data, hashlib.sha256).digest()[:MAC_SIZE]


def hkdf_sha256(ikm, salt, info, length=32):
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    out, t, counter = b"", b"", 1
    while len(out) < length:
        t = hmac.new(prk, t + info + bytes([counter]), hashlib.sha256).digest()
        out += t
        counter += 1
    return out[:length]


# --- framing ------------------------------------------------------------------
class Link:
    def __init__(self, port, timeout=3.0):
        self.ser = serial.Serial(port, timeout=timeout)
        self.buf = bytearray()
        self.k_a2d = None       # app -> device sign key
        self.k_d2a = None       # device -> app verify key
        self.tx_counter = 0     # our monotonic counter

    def close(self):
        self.ser.close()

    def _send(self, msg_type, category, op, payload, authed):
        body = bytes([msg_type, category, op]) + payload
        flags = FLAG_AUTH if authed else 0
        header = MAGIC + bytes([VER, flags]) + struct.pack(
            "<IH", self.tx_counter, len(body))
        frame = header + body
        if authed:
            frame += hmac16(self.k_a2d, frame[2:])   # MAC over VER..BODY
        self.ser.write(frame)
        if authed:
            self.tx_counter += 1

    def _read_frame(self, deadline):
        # Reassemble one frame from the byte stream (frames may split/coalesce).
        while True:
            i = self.buf.find(MAGIC)
            if i > 0:
                del self.buf[:i]                     # resync
            elif i < 0:
                del self.buf[:max(0, len(self.buf) - 1)]
            if len(self.buf) >= HDR_SIZE:
                length = struct.unpack("<H", self.buf[8:10])[0]
                authed = self.buf[3] & FLAG_AUTH
                total = HDR_SIZE + length + (MAC_SIZE if authed else 0)
                if len(self.buf) >= total:
                    frame = bytes(self.buf[:total])
                    del self.buf[:total]
                    return frame
            if time.time() > deadline:
                raise TimeoutError("no complete frame from device")
            chunk = self.ser.read(256)
            if chunk:
                self.buf += chunk

    def _recv_resp(self, timeout=5.0):
        deadline = time.time() + timeout
        while True:
            frame = self._read_frame(deadline)
            body = frame[HDR_SIZE:HDR_SIZE + struct.unpack("<H", frame[8:10])[0]]
            if not body:
                continue
            if body[0] == TYPE_RESP:
                # [type][cat][op][status][data...]
                status = body[3]
                return status, body[4:]
            # ignore LOG/STREAM frames that arrive in between

    def handshake(self, psk):
        client_nonce = os.urandom(NONCE_SIZE)
        self._send(TYPE_HELLO, 0x00, 0x00, bytes([VER]) + client_nonce, authed=False)

        deadline = time.time() + 5.0
        while True:
            frame = self._read_frame(deadline)
            body = frame[HDR_SIZE:HDR_SIZE + struct.unpack("<H", frame[8:10])[0]]
            if body and body[0] == TYPE_HELLO_ACK:
                break

        # payload after type/cat/op:
        #   [host_ver][server_nonce[16]][device_id[6]][mac_psk[16]]
        p = body[3:]
        server_nonce = p[1:1 + NONCE_SIZE]
        device_id = p[1 + NONCE_SIZE:1 + NONCE_SIZE + DEVICE_ID_SIZE]
        mac_psk = p[1 + NONCE_SIZE + DEVICE_ID_SIZE:
                    1 + NONCE_SIZE + DEVICE_ID_SIZE + MAC_SIZE]

        salt = client_nonce + server_nonce
        if not hmac.compare_digest(mac_psk, hmac16(psk, salt)):
            raise PermissionError(
                "PSK mismatch - the device does not hold this key "
                "(check Settings -> PAIRING)")

        self.k_a2d = hkdf_sha256(psk, salt, b"tos-host-a2d")
        self.k_d2a = hkdf_sha256(psk, salt, b"tos-host-d2a")
        self.tx_counter = 1  # HELLO used the unauth counter; authed frames start at 1
        return device_id.hex(":")

    def cmd(self, op, payload):
        self._send(TYPE_CMD, CAT_SYSTEM, op, payload, authed=True)
        return self._recv_resp()


# --- file ops -----------------------------------------------------------------
def do_push(link, local, remote):
    size = os.path.getsize(local)
    sent = 0
    remote_b = remote.encode()
    with open(local, "rb") as f:
        first = True
        while True:
            data = f.read(CHUNK)
            if not data and not first:
                break
            flags = FILE_WRITE_TRUNCATE if first else 0
            payload = (struct.pack("<IBH", sent, flags, len(remote_b))
                       + remote_b + data)
            status, resp = link.cmd(OP_FILE_WRITE, payload)
            if status != 0:
                raise IOError(f"FILE_WRITE @ {sent}: {STATUS.get(status, status)}")
            wrote = struct.unpack("<I", resp[:4])[0] if len(resp) >= 4 else 0
            if wrote != len(data):
                raise IOError(f"short write @ {sent}: {wrote}/{len(data)}")
            sent += len(data)
            first = False
            _progress(sent, size)
            if not data:
                break
    print(f"\ndone: {sent} bytes -> {remote}")


def do_pull(link, remote, local):
    remote_b = remote.encode()
    off = 0
    with open(local, "wb") as f:
        while True:
            payload = struct.pack("<IH", off, CHUNK) + remote_b
            status, data = link.cmd(OP_FILE_READ, payload)
            if status != 0:
                raise IOError(f"FILE_READ @ {off}: {STATUS.get(status, status)}")
            if not data:
                break
            f.write(data)
            off += len(data)
            print(f"\rpulled {off} bytes", end="", flush=True)
    print(f"\ndone: {off} bytes -> {local}")


def do_ls(link, remote):
    status, data = link.cmd(OP_FILE_LIST, remote.encode())
    if status != 0:
        raise IOError(f"FILE_LIST: {STATUS.get(status, status)}")
    count = struct.unpack("<H", data[:2])[0]
    p = 2
    print(f"{remote}  ({count} entries)")
    for _ in range(count):
        is_dir, sz, nlen = struct.unpack("<BIB", data[p:p + 6])
        p += 6
        name = data[p:p + nlen].decode(errors="replace")
        p += nlen
        print(f"  {'d' if is_dir else '-'} {sz:>10}  {name}")


def _progress(done, total):
    pct = (done * 100 // total) if total else 100
    print(f"\rpushing {done}/{total} bytes ({pct}%)", end="", flush=True)


# --- port discovery -----------------------------------------------------------
def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == (USB_VID, USB_PID):
            return p.device
    cands = [p.device for p in list_ports.comports()]
    if len(cands) == 1:
        return cands[0]
    hint = ", ".join(cands) if cands else "none found"
    sys.exit(f"could not auto-detect the device port (candidates: {hint}). "
             "Pass --port. Is the USB-C mux on native USB? "
             "(Connection settings on the device)")


def main():
    ap = argparse.ArgumentParser(description="Push/pull files to a Highboy over USB.")
    ap.add_argument("src", nargs="?", help="local file (push) or device path (--pull/--ls)")
    ap.add_argument("dst", nargs="?", help="device path (push) or local file (--pull)")
    ap.add_argument("--pull", action="store_true", help="download instead of upload")
    ap.add_argument("--ls", action="store_true", help="list a device directory")
    ap.add_argument("--port", help="serial port (auto-detected if omitted)")
    ap.add_argument("--psk", help="64-char hex PSK (else $HB_PSK, else prompt)")
    args = ap.parse_args()

    psk_hex = args.psk or os.environ.get("HB_PSK")
    if not psk_hex:
        import getpass
        psk_hex = getpass.getpass("PSK (64 hex chars): ").strip()
    try:
        psk = bytes.fromhex(psk_hex)
    except ValueError:
        sys.exit("PSK must be 64 hex characters")
    if len(psk) != PSK_SIZE:
        sys.exit(f"PSK must be {PSK_SIZE} bytes ({PSK_SIZE*2} hex chars)")

    port = args.port or find_port()
    link = Link(port)
    try:
        dev = link.handshake(psk)
        print(f"connected: {port}  device {dev}")
        if args.ls:
            if not args.src:
                sys.exit("--ls needs a device path")
            do_ls(link, args.src)
        elif args.pull:
            if not (args.src and args.dst):
                sys.exit("--pull needs <device_path> <local_file>")
            do_pull(link, args.src, args.dst)
        else:
            if not (args.src and args.dst):
                sys.exit("push needs <local_file> <device_path>")
            if not os.path.isfile(args.src):
                sys.exit(f"no such local file: {args.src}")
            do_push(link, args.src, args.dst)
    finally:
        link.close()


if __name__ == "__main__":
    main()
