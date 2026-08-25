#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. See <https://www.gnu.org/licenses/>.
"""
cli - one terminal tester for the whole TentacleOS host link (USB CDC). Handshakes,
then drives every command group (system / wifi / bt / ir) and shows the
RESP / STREAM / LOG frames. For validating the app link end to end without the app.

Usage:
    python cli.py                              # interactive
    python cli.py <group> <command> [args...]  # one-shot
    python cli.py list                          # print all groups + commands

Examples:
    python cli.py system ping
    python cli.py wifi scan_ap
    python cli.py bt spam 0
    python cli.py ir tx 1 0x04 0x08            # NEC, addr 0x04, cmd 0x08
    python cli.py ir rx                         # stream captures (Ctrl-C stops)
    python cli.py raw 0x02 0x53                 # any category/op + hex payload

PSK from --psk, $HB_PSK, or a prompt (Settings -> PAIRING, or `hostlink psk`).
Requires: pip install pyserial. Wire contracts: docs/host_link/*-commands.md.
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

# --- wire constants (must match firmware host_link.h / spi_protocol.h) ---------
MAGIC = b"\x48\x42"
VER = 1
FLAG_AUTH = 0x01
HDR_SIZE = 10
MAC_SIZE = 16
NONCE_SIZE = 16
DEVICE_ID_SIZE = 6
PSK_SIZE = 32

TYPE_CMD = 0x01
TYPE_RESP = 0x02
TYPE_STREAM = 0x03
TYPE_LOG = 0x04
TYPE_HELLO = 0x10
TYPE_HELLO_ACK = 0x11

CAT_SYSTEM = 0x00
CAT_WIFI = 0x01
CAT_BT = 0x02
CAT_IR = 0x08
CAT_SUBGHZ = 0x09
CAT_AUDIO = 0x0A
CAT_LED = 0x0B
CAT_SESSION = 0xFF

OP_SYSTEM_DATA = 0x05
OP_SESSION_HEARTBEAT = 0xF0
OP_SESSION_STOP = 0xF2
DATA_INDEX_COUNT = 0xFFFF

WIFI_SCAN_STATUS = 0x50
BT_SCAN_STATUS = 0x7F

STATUS = {0: "OK", 1: "BUSY", 2: "ERROR", 3: "UNSUPPORTED", 4: "INVALID_ARG"}
LOG_SRC = {0: "P4", 1: "C5"}
LOG_LVL = {0: "E", 1: "W", 2: "I", 3: "D", 4: "V"}

USB_VID = 0xCAFE
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
    def __init__(self, port, timeout=1.0):
        self.ser = serial.Serial(port, timeout=timeout)
        self.buf = bytearray()
        self.k_a2d = None
        self.k_d2a = None
        self.tx_counter = 0

    def close(self):
        self.ser.close()

    def _send(self, msg_type, category, op, payload, authed):
        body = bytes([msg_type, category, op]) + payload
        flags = FLAG_AUTH if authed else 0
        header = MAGIC + bytes([VER, flags]) + struct.pack("<IH", self.tx_counter, len(body))
        frame = header + body
        if authed:
            frame += hmac16(self.k_a2d, frame[2:])
        self.ser.write(frame)
        if authed:
            self.tx_counter += 1

    def _read_frame(self, deadline):
        while True:
            i = self.buf.find(MAGIC)
            if i > 0:
                del self.buf[:i]
            elif i < 0:
                del self.buf[: max(0, len(self.buf) - 1)]
            if len(self.buf) >= HDR_SIZE:
                length = struct.unpack("<H", self.buf[8:10])[0]
                authed = self.buf[3] & FLAG_AUTH
                total = HDR_SIZE + length + (MAC_SIZE if authed else 0)
                if len(self.buf) >= total:
                    frame = bytes(self.buf[:total])
                    del self.buf[:total]
                    return frame
            if time.time() > deadline:
                return None
            chunk = self.ser.read(256)
            if chunk:
                self.buf += chunk

    @staticmethod
    def _body(frame):
        length = struct.unpack("<H", frame[8:10])[0]
        return frame[HDR_SIZE : HDR_SIZE + length]

    def handshake(self, psk):
        client_nonce = os.urandom(NONCE_SIZE)
        self._send(TYPE_HELLO, 0x00, 0x00, bytes([VER]) + client_nonce, authed=False)
        deadline = time.time() + 5.0
        while True:
            frame = self._read_frame(deadline)
            if frame is None:
                raise TimeoutError("no HELLO_ACK from device (is it on native USB?)")
            body = self._body(frame)
            if body and body[0] == TYPE_HELLO_ACK:
                break
        p = body[3:]
        server_nonce = p[1 : 1 + NONCE_SIZE]
        device_id = p[1 + NONCE_SIZE : 1 + NONCE_SIZE + DEVICE_ID_SIZE]
        mac_psk = p[1 + NONCE_SIZE + DEVICE_ID_SIZE : 1 + NONCE_SIZE + DEVICE_ID_SIZE + MAC_SIZE]
        salt = client_nonce + server_nonce
        if not hmac.compare_digest(mac_psk, hmac16(psk, salt)):
            raise PermissionError("PSK mismatch - device does not hold this key")
        self.k_a2d = hkdf_sha256(psk, salt, b"tos-host-a2d")
        self.k_d2a = hkdf_sha256(psk, salt, b"tos-host-d2a")
        self.tx_counter = 1
        return device_id.hex(":")

    def cmd(self, category, op, payload=b"", timeout=6.0):
        self._send(TYPE_CMD, category, op, payload, authed=True)
        deadline = time.time() + timeout
        while True:
            frame = self._read_frame(deadline)
            if frame is None:
                raise TimeoutError(f"no RESP for cat=0x{category:02X} op=0x{op:02X}")
            body = self._body(frame)
            if not body:
                continue
            if body[0] == TYPE_RESP:
                return body[3], body[4:]
            self._show_async(body)

    def drain(self, seconds, heartbeat=None, decoder=None):
        """Print STREAM/LOG for a while. heartbeat=session_id keeps a session alive;
        decoder(cat, op, data) may render STREAM payloads specially."""
        end = time.time() + seconds
        next_hb = time.time() + 2.0
        while time.time() < end:
            if heartbeat is not None and time.time() >= next_hb:
                self._send(
                    TYPE_CMD, CAT_SESSION, OP_SESSION_HEARTBEAT,
                    struct.pack("<II", heartbeat, 0), authed=True,
                )
                next_hb = time.time() + 2.0
            frame = self._read_frame(min(end, next_hb))
            if frame is None:
                continue
            body = self._body(frame)
            if not body:
                continue
            if body[0] == TYPE_STREAM and decoder is not None:
                line = decoder(body[1], body[2], body[3:])
                if line is not None:
                    print(f"  {line}")
            elif body[0] in (TYPE_STREAM, TYPE_LOG):
                self._show_async(body)

    @staticmethod
    def _show_async(body):
        if body[0] == TYPE_LOG:
            src = LOG_SRC.get(body[3], "?")
            lvl = LOG_LVL.get(body[4], "?")
            print(f"  [LOG {src}/{lvl}] {body[5:].decode(errors='replace').rstrip()}")
        elif body[0] == TYPE_STREAM:
            print(f"  [STREAM cat=0x{body[1]:02X} op=0x{body[2]:02X}] {len(body) - 3} bytes")


# --- shared decoders ----------------------------------------------------------
def dec_cstr(b):
    return b.split(b"\x00", 1)[0].decode(errors="replace")


def dec_mac(b):
    return ":".join(f"{x:02x}" for x in b[:6])


def ip4(v):
    return ".".join(str((v >> (8 * i)) & 0xFF) for i in range(4))


def parse_mac(s):
    parts = s.replace("-", ":").split(":")
    if len(parts) != 6:
        raise ValueError(f"bad MAC: {s}")
    return bytes(int(p, 16) for p in parts)


def fetch_list(link, decoder):
    st, data = link.cmd(CAT_SYSTEM, OP_SYSTEM_DATA, struct.pack("<H", DATA_INDEX_COUNT))
    if st != 0 or len(data) < 2:
        print(f"  data pipe count: {STATUS.get(st, st)}")
        return
    count = struct.unpack("<H", data[:2])[0]
    print(f"  {count} result(s):")
    for i in range(count):
        st, d = link.cmd(CAT_SYSTEM, OP_SYSTEM_DATA, struct.pack("<H", i))
        if st == 0:
            print(f"    {decoder(d)}")


def poll_scan(link, cat, status_op, timeout=25.0):
    end = time.time() + timeout
    while time.time() < end:
        st, data = link.cmd(cat, status_op)
        if st == 0 and data and data[0] == 0:
            return True
        time.sleep(0.2)
    print("  scan did not finish in time")
    return False


def _need(args, n, usage):
    if len(args) < n:
        raise ValueError(f"usage: {usage}")


def _session(link, cat, op, payload=b"", decoder=None):
    st, d = link.cmd(cat, op, payload)
    if st != 0 or len(d) < 4:
        print(f"  {STATUS.get(st, st)}")
        return
    sid = struct.unpack("<I", d[:4])[0]
    print(f"  session {sid} started - streaming, Ctrl-C to stop")
    try:
        link.drain(3600, heartbeat=sid, decoder=decoder)
    except KeyboardInterrupt:
        pass
    finally:
        link.cmd(CAT_SESSION, OP_SESSION_STOP, struct.pack("<I", sid))
        print(f"\n  session {sid} stopped")


# --- SYSTEM -------------------------------------------------------------------
def sys_status(link, args):
    st, d = link.cmd(CAT_SYSTEM, 0x02)
    if st == 0 and len(d) >= 4:
        print(f"  OK  wifi_active={d[0]} wifi_conn={d[1]} bt_running={d[2]} bt_init={d[3]}")
    else:
        print(f"  {STATUS.get(st, st)}")


def sys_info(link, args):
    st, d = link.cmd(CAT_SYSTEM, 0x0C)
    if st == 0 and len(d) >= 13:
        rev = struct.unpack("<H", d[1:3])[0]
        heap = struct.unpack("<I", d[9:13])[0]
        print(f"  OK  chip_model={d[0]} rev={rev} mac={dec_mac(d[3:9])} free_heap={heap}")
    else:
        print(f"  {STATUS.get(st, st)}")


def sys_version(link, args):
    st, d = link.cmd(CAT_SYSTEM, 0x04)
    print(f"  {STATUS.get(st, st)}  c5_version={dec_cstr(d)!r}")


SYSTEM_CMDS = {
    "ping": (lambda l, a: print(f"  {STATUS.get(l.cmd(CAT_SYSTEM, 0x01)[0], '?')}"), "C5 alive?"),
    "status": (sys_status, "wifi/bt active flags"),
    "version": (sys_version, "C5 firmware version"),
    "info": (sys_info, "C5 chip model/rev/mac/heap"),
    "reboot": (lambda l, a: print(f"  {STATUS.get(l.cmd(CAT_SYSTEM, 0x03)[0], '?')} (reboots C5)"), "reboot the C5"),
}


# --- WIFI ---------------------------------------------------------------------
def dec_wifi_ap(d):
    rssi = struct.unpack("<b", d[6:7])[0]
    return f"{dec_mac(d[0:6])}  ch{d[7]:<3} {rssi:>4}dBm  auth={d[8]}  {dec_cstr(d[9:42])!r}"


def dec_wifi_client(d):
    return f"ap={dec_mac(d[0:6])} client={dec_mac(d[6:12])} rssi={struct.unpack('<b', d[12:13])[0]} ch{d[13]}"


def dec_port(d):
    port = struct.unpack("<H", d[16:18])[0]
    proto = "TCP" if d[18] == 0 else "UDP"
    state = "OPEN" if d[19] == 0 else "OPEN|FILTERED"
    return f"{dec_cstr(d[0:16])}:{port}/{proto} {state} {dec_cstr(d[20:84])!r}"


def wifi_scan_ap(link, args):
    st, _ = link.cmd(CAT_WIFI, 0x20)
    print(f"  start: {STATUS.get(st, st)}")
    if st == 0 and poll_scan(link, CAT_WIFI, WIFI_SCAN_STATUS):
        fetch_list(link, dec_wifi_ap)


def wifi_scan_client(link, args):
    st, _ = link.cmd(CAT_WIFI, 0x21)
    print(f"  start: {STATUS.get(st, st)}")
    if st == 0 and poll_scan(link, CAT_WIFI, WIFI_SCAN_STATUS):
        fetch_list(link, dec_wifi_client)


def wifi_get_mac(link, args):
    iface = 1 if (args and args[0].lower() == "ap") else 0
    st, d = link.cmd(CAT_WIFI, 0x4E, bytes([iface]))
    print(f"  {STATUS.get(st, st)}  {dec_mac(d) if st == 0 else ''}")


def wifi_get_ip(link, args):
    iface = 1 if (args and args[0].lower() == "ap") else 0
    st, d = link.cmd(CAT_WIFI, 0x4F, bytes([iface]))
    if st == 0 and len(d) >= 19:
        ip, mask, gw = struct.unpack("<III", d[7:19])
        print(f"  OK  iface={'AP' if d[0] else 'STA'} mac={dec_mac(d[1:7])} ip={ip4(ip)} mask={ip4(mask)} gw={ip4(gw)}")
    else:
        print(f"  {STATUS.get(st, st)}")


def wifi_connect(link, args):
    _need(args, 1, "wifi connect <ssid> [password]")
    ssid = args[0].encode()[:32].ljust(32, b"\x00")
    pw = args[1].encode()[:64] if len(args) > 1 else b""
    print(f"  {STATUS.get(link.cmd(CAT_WIFI, 0x11, ssid + pw)[0], '?')}")


def wifi_port_scan(link, args):
    _need(args, 1, "wifi port_scan <ip> [start] [end]")
    ip = args[0].encode()[:15].ljust(16, b"\x00")
    start = int(args[1]) if len(args) > 1 else 1
    end = int(args[2]) if len(args) > 2 else 1024
    req = ip + struct.pack("<HHH", start, end, 32) + b"\x00\x00"
    st, _ = link.cmd(CAT_WIFI, 0x49, req)
    print(f"  start: {STATUS.get(st, st)}")
    if st == 0 and poll_scan(link, CAT_WIFI, WIFI_SCAN_STATUS):
        fetch_list(link, dec_port)


def wifi_sniffer(link, args):
    t = int(args[0]) if len(args) > 0 else 0
    ch = int(args[1]) if len(args) > 1 else 0
    mon = int(args[2]) if len(args) > 2 else 0
    _session(link, CAT_WIFI, 0x25, bytes([t, ch, mon]))


def _simple(cat, op, note=""):
    def h(link, args):
        st, d = link.cmd(cat, op)
        extra = f"  data={d.hex()}" if d else ""
        print(f"  {STATUS.get(st, st)}{extra}{('  ' + note) if note else ''}")
    return h


WIFI_CMDS = {
    "scan_ap": (wifi_scan_ap, "AP scan + results"),
    "scan_client": (wifi_scan_client, "client scan + results"),
    "connect": (wifi_connect, "connect <ssid> [pw]"),
    "disconnect": (_simple(CAT_WIFI, 0x12), "disconnect STA"),
    "start": (_simple(CAT_WIFI, 0x15), "start wifi"),
    "stop": (_simple(CAT_WIFI, 0x16), "stop wifi"),
    "get_mac": (wifi_get_mac, "get_mac [sta|ap]"),
    "get_ip": (wifi_get_ip, "get_ip [sta|ap]"),
    "port_scan": (wifi_port_scan, "port_scan <ip> [start] [end]"),
    "sniffer": (wifi_sniffer, "sniffer [type] [ch] [monitor] (session)"),
    "beacon_spam": (lambda l, a: _session(l, CAT_WIFI, 0x22), "beacon spam (session)"),
    "deauth_det": (lambda l, a: _session(l, CAT_WIFI, 0x27), "deauth detector (session)"),
}


# --- BT -----------------------------------------------------------------------
def dec_bt_scan(d):
    rssi = struct.unpack("<i", d[168:172])[0] if len(d) >= 172 else 0
    uuids = dec_cstr(d[32:160])
    extra = f"  uuids={uuids}" if uuids else ""
    return f"{dec_mac(d[160:166])} (type {d[166]})  {rssi:>4}dBm  {dec_cstr(d[0:32])!r}{extra}"


def bt_info(link, args):
    st, d = link.cmd(CAT_BT, 0x53)
    if st == 0 and len(d) >= 10:
        conn = struct.unpack("<H", d[8:10])[0]
        print(f"  OK  mac={dec_mac(d[0:6])} running={d[6]} initialized={d[7]} connections={conn}")
    else:
        print(f"  {STATUS.get(st, st)}")


def bt_scan(link, args):
    payload = struct.pack("<I", int(args[0])) if args else b""
    st, _ = link.cmd(CAT_BT, 0x50, payload)
    print(f"  start: {STATUS.get(st, st)}")
    if st == 0 and poll_scan(link, CAT_BT, BT_SCAN_STATUS):
        fetch_list(link, dec_bt_scan)


def bt_spam(link, args):
    _need(args, 1, "bt spam <0-5>")
    _session(link, CAT_BT, 0x62, bytes([int(args[0]) & 0xFF]))


def bt_flood(link, args):
    _need(args, 1, "bt flood <mac> [addr_type]")
    at = int(args[1]) if len(args) > 1 else 0
    _session(link, CAT_BT, 0x63, parse_mac(args[0]) + bytes([at]))


def bt_gatt_exp(link, args):
    _need(args, 1, "bt gatt_exp <mac> [addr_type]")
    at = int(args[1]) if len(args) > 1 else 0
    st, _ = link.cmd(CAT_BT, 0x66, parse_mac(args[0]) + bytes([at]))
    print(f"  {STATUS.get(st, st)} (findings via C5 LOG)")


def bt_hid_key(link, args):
    _need(args, 2, "bt hid_key <modifier> <keycode>")
    st, _ = link.cmd(CAT_BT, 0x74, bytes([int(args[0], 0) & 0xFF, int(args[1], 0) & 0xFF]))
    print(f"  {STATUS.get(st, st)}")


BT_CMDS = {
    "info": (bt_info, "mac/running/initialized/connections"),
    "scan": (bt_scan, "scan [duration_ms] + results"),
    "init": (_simple(CAT_BT, 0x54), "init stack"),
    "deinit": (_simple(CAT_BT, 0x55), "deinit stack"),
    "random_mac": (_simple(CAT_BT, 0x58), "set random MAC"),
    "adv_on": (_simple(CAT_BT, 0x59), "start advertising"),
    "adv_off": (_simple(CAT_BT, 0x5A), "stop advertising"),
    "max_power": (_simple(CAT_BT, 0x5B), "set max TX power"),
    "sniffer": (lambda l, a: _session(l, CAT_BT, 0x61), "adv sniffer (session)"),
    "spam": (bt_spam, "spam <0-5> (session)"),
    "flood": (bt_flood, "flood <mac> [addr_type] (session)"),
    "skimmer": (lambda l, a: _session(l, CAT_BT, 0x64), "skimmer detector (session)"),
    "tracker_det": (lambda l, a: _session(l, CAT_BT, 0x65), "AirTag detector (session)"),
    "gatt_exp": (bt_gatt_exp, "gatt_exp <mac> [addr_type]"),
    "hid_init": (_simple(CAT_BT, 0x71), "start BLE HID keyboard"),
    "hid_deinit": (_simple(CAT_BT, 0x72), "stop BLE HID keyboard"),
    "hid_connected": (_simple(CAT_BT, 0x73), "HID host connected?"),
    "hid_key": (bt_hid_key, "hid_key <modifier> <keycode>"),
    "l2cap_status": (_simple(CAT_BT, 0x70), "L2CAP flood running?"),
}


# --- IR -----------------------------------------------------------------------
IR_PROTOCOLS = "UNKNOWN NEC SAMSUNG RC6 RC5 SONY LG JVC DENON PANASONIC RCA PIONEER NEC42".split()


def _ir_proto_name(n):
    return IR_PROTOCOLS[n] if 0 <= n < len(IR_PROTOCOLS) else str(n)


def dec_ir_frame(cat, op, d):
    if cat != CAT_IR or len(d) < 10:
        return None
    proto = d[0]
    addr = struct.unpack("<I", d[1:5])[0]
    cmd = struct.unpack("<I", d[5:9])[0]
    rep = d[9]
    return f"IR {_ir_proto_name(proto)}  addr=0x{addr:X} cmd=0x{cmd:X} repeat={rep}"


def ir_tx(link, args):
    _need(args, 3, "ir tx <protocol> <address> <command> [repeat]  (protocol int 0-12)")
    proto = int(args[0], 0)
    addr = int(args[1], 0)
    cmd = int(args[2], 0)
    rep = int(args[3], 0) if len(args) > 3 else 0
    payload = bytes([proto & 0xFF]) + struct.pack("<II", addr, cmd) + bytes([rep & 0xFF])
    print(f"  {STATUS.get(link.cmd(CAT_IR, 0x01, payload)[0], '?')}")


def ir_tx_raw(link, args):
    _need(args, 2, "ir tx_raw <carrier_hz> <hex-symbols>  (hex = raw rmt_symbol bytes, 4/symbol)")
    carrier = int(args[0], 0)
    sym = bytes.fromhex(args[1])
    count = len(sym) // 4
    payload = struct.pack("<IH", carrier, count) + sym[: count * 4]
    print(f"  {STATUS.get(link.cmd(CAT_IR, 0x02, payload)[0], '?')}")


def ir_rx(link, args):
    st, _ = link.cmd(CAT_IR, 0x03)
    print(f"  start: {STATUS.get(st, st)} - point a remote, Ctrl-C to stop")
    if st != 0:
        return
    try:
        link.drain(3600, decoder=dec_ir_frame)
    except KeyboardInterrupt:
        pass
    finally:
        link.cmd(CAT_IR, 0x04)
        print("\n  rx stopped")


IR_CMDS = {
    "tx": (ir_tx, "tx <protocol> <addr> <cmd> [repeat]"),
    "tx_raw": (ir_tx_raw, "tx_raw <carrier_hz> <hex-symbols>"),
    "rx": (ir_rx, "stream captures (Ctrl-C stops)"),
    "torch_on": (_simple(CAT_IR, 0x06), "IR LED DC torch on"),
    "torch_off": (_simple(CAT_IR, 0x07), "IR LED DC torch off"),
}


# --- SUBGHZ -------------------------------------------------------------------
def dec_subghz_rx(cat, op, d):
    if cat != CAT_SUBGHZ or op != 0x02 or len(d) < 19:
        return None
    seq = struct.unpack("<I", d[0:4])[0]
    decoded = d[4]
    freq = struct.unpack("<I", d[5:9])[0]
    bits = d[9]
    btn = d[10]
    serial = struct.unpack("<I", d[11:15])[0]
    raw = struct.unpack("<I", d[15:19])[0]
    nlen = d[19] if len(d) > 19 else 0
    proto = d[20 : 20 + nlen].decode(errors="replace") if nlen else "raw"
    return f"SUBGHZ [{seq}] {proto} {freq}Hz decoded={decoded} bits={bits} btn={btn} serial=0x{serial:X} raw=0x{raw:X}"


def dec_subghz_spec(cat, op, d):
    if cat != CAT_SUBGHZ or op != 0x07 or len(d) < 17:
        return None
    center, span, start, step = struct.unpack("<IIII", d[0:16])
    count = d[16]
    bins = [struct.unpack("<b", d[17 + i : 18 + i])[0] for i in range(min(count, len(d) - 17))]
    peak = max(bins) if bins else 0
    peak_bin = bins.index(peak) if bins else 0
    return f"SUBGHZ spectrum @ {center}Hz span {span}Hz  peak {peak}dBm @ {start + peak_bin * step}Hz"


def dec_subghz_stream(cat, op, d):
    return dec_subghz_rx(cat, op, d) or dec_subghz_spec(cat, op, d)


def subghz_rx(link, args):
    _need(args, 1, "subghz rx <freq_hz> [mode 0scan/1raw] [preset]")
    freq = int(args[0], 0)
    mode = int(args[1]) if len(args) > 1 else 0
    preset = int(args[2]) if len(args) > 2 else 2  # OOK_650K
    payload = bytes([mode, preset]) + struct.pack("<I", freq)
    st, _ = link.cmd(CAT_SUBGHZ, 0x01, payload)
    print(f"  start: {STATUS.get(st, st)} - Ctrl-C to stop")
    if st != 0:
        return
    try:
        link.drain(3600, decoder=dec_subghz_stream)
    except KeyboardInterrupt:
        pass
    finally:
        link.cmd(CAT_SUBGHZ, 0x03)
        print("\n  rx stopped")


def subghz_spectrum(link, args):
    _need(args, 2, "subghz spectrum <center_hz> <span_hz>")
    payload = struct.pack("<II", int(args[0], 0), int(args[1], 0))
    st, _ = link.cmd(CAT_SUBGHZ, 0x06, payload)
    print(f"  start: {STATUS.get(st, st)} - Ctrl-C to stop")
    if st != 0:
        return
    try:
        link.drain(3600, decoder=dec_subghz_stream)
    except KeyboardInterrupt:
        pass
    finally:
        link.cmd(CAT_SUBGHZ, 0x08)
        print("\n  spectrum stopped")


def subghz_list(link, args):
    st, d = link.cmd(CAT_SUBGHZ, 0x09)
    if st != 0 or not d:
        print(f"  {STATUS.get(st, st)}")
        return
    count = d[0]
    print(f"  {count} capture(s):")
    off = 1
    for _ in range(count):
        name = dec_cstr(d[off : off + 40])
        proto = dec_cstr(d[off + 40 : off + 64])
        freq = struct.unpack("<I", d[off + 64 : off + 68])[0]
        print(f"    {name:<24} {proto:<10} {freq} Hz")
        off += 68


def subghz_replay(link, args):
    _need(args, 1, "subghz replay <name>")
    print(f"  {STATUS.get(link.cmd(CAT_SUBGHZ, 0x05, args[0].encode())[0], '?')}")


def subghz_delete(link, args):
    _need(args, 1, "subghz delete <name>")
    print(f"  {STATUS.get(link.cmd(CAT_SUBGHZ, 0x0A, args[0].encode())[0], '?')}")


SUBGHZ_CMDS = {
    "rx": (subghz_rx, "rx <freq> [mode] [preset] - stream captures (Ctrl-C stops)"),
    "spectrum": (subghz_spectrum, "spectrum <center> <span> - stream sweep (Ctrl-C stops)"),
    "list": (subghz_list, "list saved captures"),
    "replay": (subghz_replay, "replay <name>"),
    "delete": (subghz_delete, "delete <name>"),
}


# --- AUDIO --------------------------------------------------------------------
def audio_volume(link, args):
    _need(args, 1, "audio volume <pct>")
    print(f"  {STATUS.get(link.cmd(CAT_AUDIO, 0x01, bytes([int(args[0]) & 0xFF]))[0], '?')}")


def audio_tone(link, args):
    _need(args, 2, "audio tone <freq_hz> <ms> [amp_pct]")
    amp = int(args[2]) if len(args) > 2 else 50
    payload = struct.pack("<HHB", int(args[0]) & 0xFFFF, int(args[1]) & 0xFFFF, amp & 0xFF)
    print(f"  {STATUS.get(link.cmd(CAT_AUDIO, 0x02, payload)[0], '?')}")


AUDIO_CMDS = {
    "volume": (audio_volume, "volume <pct>"),
    "tone": (audio_tone, "tone <freq_hz> <ms> [amp_pct]"),
    "chime": (_simple(CAT_AUDIO, 0x03), "play UI chime"),
    "click": (_simple(CAT_AUDIO, 0x04), "play UI click"),
}


# --- LED ----------------------------------------------------------------------
def led_color(link, args):
    _need(args, 3, "led color <r> <g> <b>")
    payload = bytes([int(args[0]) & 0xFF, int(args[1]) & 0xFF, int(args[2]) & 0xFF])
    print(f"  {STATUS.get(link.cmd(CAT_LED, 0x01, payload)[0], '?')}")


def led_blink(link, args):
    _need(args, 4, "led blink <r> <g> <b> <ms>")
    payload = bytes([int(args[0]) & 0xFF, int(args[1]) & 0xFF, int(args[2]) & 0xFF]) + struct.pack(
        "<H", int(args[3]) & 0xFFFF
    )
    print(f"  {STATUS.get(link.cmd(CAT_LED, 0x03, payload)[0], '?')}")


def led_signal(link, args):
    _need(args, 1, "led signal <info|warn|error>")
    which = {"info": 0, "warn": 1, "warning": 1, "error": 2}.get(args[0].lower(), 0)
    print(f"  {STATUS.get(link.cmd(CAT_LED, 0x04, bytes([which]))[0], '?')}")


LED_CMDS = {
    "color": (led_color, "color <r> <g> <b>"),
    "clear": (_simple(CAT_LED, 0x02), "turn the LED off"),
    "blink": (led_blink, "blink <r> <g> <b> <ms>"),
    "signal": (led_signal, "signal <info|warn|error>"),
}


GROUPS = {
    "system": SYSTEM_CMDS,
    "wifi": WIFI_CMDS,
    "bt": BT_CMDS,
    "ir": IR_CMDS,
    "subghz": SUBGHZ_CMDS,
    "audio": AUDIO_CMDS,
    "led": LED_CMDS,
}


def do_raw(link, args):
    _need(args, 2, "raw <cat-hex> <op-hex> [payload-hex]")
    cat = int(args[0], 16)
    op = int(args[1], 16)
    payload = bytes.fromhex(args[2]) if len(args) > 2 else b""
    st, d = link.cmd(cat, op, payload)
    print(f"  {STATUS.get(st, st)}  data={d.hex()}")


def print_table():
    for gname, cmds in GROUPS.items():
        print(f"[{gname}]")
        for name, (_, help_text) in cmds.items():
            print(f"  {gname} {name:<14} {help_text}")
    print("[raw]\n  raw <cat-hex> <op-hex> [payload-hex]")


def dispatch(link, parts):
    if not parts:
        return
    if parts[0] == "raw":
        do_raw(link, parts[1:])
        return
    if parts[0] == "list":
        print_table()
        return
    group = GROUPS.get(parts[0])
    if group is None:
        print(f"  unknown group: {parts[0]} (try 'list')")
        return
    if len(parts) < 2:
        print(f"  commands: {', '.join(group)}")
        return
    entry = group.get(parts[1])
    if entry is None:
        print(f"  unknown command: {parts[0]} {parts[1]} (try 'list')")
        return
    entry[0](link, parts[2:])


def interactive(link):
    print_table()
    print("Type '<group> <command> [args]', 'list', or 'quit'.")
    while True:
        try:
            line = input("hb> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return
        if not line:
            continue
        if line in ("quit", "exit", "q"):
            return
        try:
            dispatch(link, line.split())
        except (ValueError, KeyboardInterrupt) as e:
            print(f"  {e}")
        except Exception as e:  # keep the REPL alive
            print(f"  error: {e}")


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == (USB_VID, USB_PID):
            return p.device
    cands = [p.device for p in list_ports.comports()]
    if len(cands) == 1:
        return cands[0]
    hint = ", ".join(cands) if cands else "none found"
    sys.exit(f"could not auto-detect the device port (candidates: {hint}). Pass --port.")


def main():
    ap = argparse.ArgumentParser(description="TentacleOS host-link tester (USB CDC).")
    ap.add_argument("words", nargs="*", help="<group> <command> [args], or 'list'")
    ap.add_argument("--port", help="serial port (auto-detected if omitted)")
    ap.add_argument("--psk", help="64-char hex PSK (else $HB_PSK, else prompt)")
    args = ap.parse_args()

    if args.words and args.words[0] == "list":
        print_table()
        return

    psk_hex = args.psk or os.environ.get("HB_PSK")
    if not psk_hex:
        import getpass

        psk_hex = getpass.getpass("PSK (64 hex chars): ").strip()
    try:
        psk = bytes.fromhex(psk_hex)
    except ValueError:
        sys.exit("PSK must be 64 hex characters")
    if len(psk) != PSK_SIZE:
        sys.exit(f"PSK must be {PSK_SIZE} bytes ({PSK_SIZE * 2} hex chars)")

    port = args.port or find_port()
    link = Link(port)
    try:
        dev = link.handshake(psk)
        print(f"connected: {port}  device {dev}")
        if args.words:
            dispatch(link, args.words)
        else:
            interactive(link)
    finally:
        link.close()


if __name__ == "__main__":
    main()
