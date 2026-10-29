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

"""Send local PC telemetry over an authenticated Highboy USB or BLE connection."""

import argparse
import ipaddress
import math
import os
import platform
from pathlib import Path
import socket
import struct
import sys
import threading
import time
import urllib.request

from cli import Link, CAT_SYSTEM, TYPE_CMD, TYPE_RESP, USB_VID, USB_PID, list_ports

WIRE = struct.Struct("<BBHHHIIII32s32s46s46s")
OP_PC_MONITOR = 0x4E
VERSION = 1
DEFAULT_PSK_PATH = Path(__file__).resolve().parents[2] / ".highboy-pc-monitor.psk"
MIB = 1024 * 1024
PERCENT_SCALE = 100
PUBLIC_IP_INTERVAL = 300
PUBLIC_IP_TIMEOUT = 3
TELEMETRY_ACK_TIMEOUT = 3
TELEMETRY_MAX_MISSES = 3
PUBLIC_IP_URL = "https://api.ipify.org"


def text_field(value, size):
    # These labels use the firmware's ASCII font; never truncate a filesystem operation.
    text = "".join(c if 32 <= ord(c) <= 126 else "?" for c in str(value))
    return text.encode("ascii")[:size - 1].ljust(size, b"\0")


def ip_field(value):
    return str(ipaddress.ip_address(value)).encode("ascii") if value else b""


def percent(value):
    if not math.isfinite(value) or not 0 <= value <= 100:
        raise ValueError("invalid percentage")
    return round(value * PERCENT_SCALE)


def pack_sample(cpu, memory, disk, hostname, volume, local_ip, public_ip):
    sizes = (memory.total - memory.available, memory.total, disk.used, disk.total)
    if any(not 0 <= size // MIB <= 0xFFFFFFFF for size in sizes):
        raise ValueError("capacity outside monitor range")
    if memory.total < MIB or disk.total < MIB or not 0 <= memory.available <= memory.total:
        raise ValueError("invalid memory/disk capacity")
    if not 0 <= disk.used <= disk.total:
        raise ValueError("invalid disk usage")
    return WIRE.pack(VERSION, 0, percent(cpu), percent(memory.percent), percent(disk.percent),
                     *(size // MIB for size in sizes), text_field(hostname, 32),
                     text_field(volume, 32), ip_field(local_ip), ip_field(public_ip))


def local_address(interface, metrics):
    if interface:
        addresses = metrics.net_if_addrs().get(interface, [])
        for family in (socket.AF_INET, socket.AF_INET6):
            for address in addresses:
                if address.family == family:
                    value = address.address.split("%", 1)[0]
                    if not ipaddress.ip_address(value).is_loopback:
                        return value
        return ""
    # UDP connect chooses the route without sending packets to this documentation address.
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
            connection.connect(("192.0.2.1", 9))
            return connection.getsockname()[0]
    except OSError:
        return ""


def fetch_public_ip():
    request = urllib.request.Request(PUBLIC_IP_URL, headers={"User-Agent": "Highboy-PC-Monitor/1"})
    with urllib.request.urlopen(request, timeout=PUBLIC_IP_TIMEOUT) as response:
        return str(ipaddress.ip_address(response.read(64).decode("ascii").strip()))


class PublicAddress:
    def __init__(self):
        self._lock = threading.Lock()
        self._value = ""
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="public-ip", daemon=True)

    def start(self):
        self._thread.start()

    def close(self):
        self._stop.set()
        self._thread.join(timeout=PUBLIC_IP_TIMEOUT + 1)

    def get(self):
        with self._lock:
            return self._value

    def _run(self):
        while not self._stop.is_set():
            try:
                value = fetch_public_ip()
            except (OSError, ValueError, UnicodeError):
                value = ""
            with self._lock:
                self._value = value
            self._stop.wait(PUBLIC_IP_INTERVAL)


class TelemetryAckTimeout(TimeoutError):
    """A sent telemetry sample received no matching response before its deadline."""


class MonitorLink(Link):
    @classmethod
    def from_stream(cls, stream):
        link = cls.__new__(cls)
        link.ser = stream
        link.buf = bytearray()
        link.rx_counter = None
        link.k_a2d = link.k_d2a = None
        link.tx_counter = 0
        return link

    def publish(self, payload):
        self._send(TYPE_CMD, CAT_SYSTEM, OP_PC_MONITOR, payload, authed=True)
        deadline = time.monotonic() + TELEMETRY_ACK_TIMEOUT
        while time.monotonic() < deadline:
            frame = self._read_frame(deadline)
            if frame is None:
                break
            body = self._body(frame)
            if len(body) >= 4 and body[:3] == bytes((TYPE_RESP, CAT_SYSTEM, OP_PC_MONITOR)):
                if body[3] != 0:
                    raise RuntimeError(f"Highboy rejected sample (status {body[3]})")
                return
        raise TelemetryAckTimeout("Highboy did not acknowledge telemetry")

    def wait(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            # Drain logs/streams so the serial queue remains bounded between samples.
            if self._read_frame(deadline) is None:
                break


def find_port():
    ports = [p.device for p in list_ports.comports() if p.vid == USB_VID and p.pid == USB_PID]
    if len(ports) != 1:
        raise ValueError("Specify --port: expected exactly one Highboy USB CDC device")
    return ports[0]


def monitor_session(args, link, psk, metrics, disk_path, connection_name):
    public = None
    try:
        link.handshake(psk)
        if not args.no_public_ip:
            public = PublicAddress()
            public.start()
        metrics.cpu_percent(interval=None)
        link.wait(args.interval)
        print(f"Connected on {connection_name}; monitoring {disk_path}. Ctrl-C to stop.")
        missed_acks = 0
        while True:
            start = time.monotonic()
            sample = pack_sample(metrics.cpu_percent(interval=None), metrics.virtual_memory(),
                                 metrics.disk_usage(disk_path), platform.node() or "PC", disk_path,
                                 local_address(args.interface, metrics), public.get() if public else "")
            try:
                link.publish(sample)
            except TelemetryAckTimeout:
                missed_acks += 1
                if missed_acks >= TELEMETRY_MAX_MISSES:
                    raise TelemetryAckTimeout(
                        f"Highboy did not acknowledge telemetry for {missed_acks} consecutive updates; "
                        "closing the session. Check P4/C5 SPI and Host Link logs."
                    ) from None
                print(f"Telemetry acknowledgement timeout ({missed_acks}/{TELEMETRY_MAX_MISSES}); "
                      "keeping connection and sending a fresh sample.", file=sys.stderr)
            else:
                if missed_acks:
                    print("Telemetry acknowledgements resumed.", file=sys.stderr)
                missed_acks = 0
            link.wait(max(0, args.interval - (time.monotonic() - start)))
    finally:
        link.close()
        if public is not None:
            public.close()


def read_psk():
    value = os.environ.get("HB_PSK")
    if value is None:
        try:
            value = "".join(line.strip() for line in DEFAULT_PSK_PATH.read_text().splitlines()
                            if not line.lstrip().startswith("#"))
        except FileNotFoundError:
            raise ValueError("Set HB_PSK or save the device PSK in .highboy-pc-monitor.psk") from None
    psk = bytes.fromhex(value)
    if len(psk) != 32:
        raise ValueError("PSK must contain 64 hex digits")
    return psk


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ble", action="store_true", help="connect over Bluetooth LE instead of USB")
    parser.add_argument("--device", help="BLE name or address/OS UUID; omit to select from a scan list")
    parser.add_argument("--port", help="Highboy native USB CDC port (auto-detect if unique)")
    parser.add_argument("--disk", default=os.path.abspath(os.sep), help="filesystem path to monitor")
    parser.add_argument("--interface", help="network interface for local IP (default: outgoing IPv4 route)")
    parser.add_argument("--interval", type=float, default=1.0, help="sample interval, 0.5 to 2 seconds")
    parser.add_argument("--no-public-ip", action="store_true", help="disable periodic HTTPS request to ipify")
    args = parser.parse_args(argv)
    if not math.isfinite(args.interval) or not 0.5 <= args.interval <= 2:
        parser.error("--interval must be between 0.5 and 2 seconds")
    if args.ble and args.port:
        parser.error("--port is only used with USB")
    if args.device and not args.ble:
        parser.error("--device requires --ble")
    try:
        import psutil
        disk_path = os.path.abspath(args.disk)
        psutil.disk_usage(disk_path)
        if args.interface and args.interface not in psutil.net_if_addrs():
            raise ValueError("unknown network interface")
        port = None if args.ble else (args.port or find_port())
        print("Open Developer > PC Monitor, then compare and approve the pairing code on Highboy.")
        if args.ble:
            import pc_monitor_ble
            return pc_monitor_ble.run(args.device, lambda stream, name, psk: monitor_session(
                args, MonitorLink.from_stream(stream), psk, psutil, disk_path, name), read_psk)
        psk = read_psk()
        link = MonitorLink(port, timeout=0.1)
        link.ser.write_timeout = 3
        return monitor_session(args, link, psk, psutil, disk_path, port)
    except KeyboardInterrupt:
        print("\nMonitor stopped.")
        return 0
    except ImportError:
        print("Install dependencies: pip install -r tools/host_link/pc_monitor_requirements.txt", file=sys.stderr)
        return 1
    except (OSError, ValueError, RuntimeError, EOFError) as error:
        print(f"PC monitor: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
