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

"""BLE byte stream adapter for the existing authenticated Host Link client."""

import asyncio
import threading
import sys
import time

SERVICE_UUID = "6e540001-b5a3-f393-e0a9-e50e24dcca9e"
RX_UUID = "6e540002-b5a3-f393-e0a9-e50e24dcca9e"
TX_UUID = "6e540003-b5a3-f393-e0a9-e50e24dcca9e"
RX_LIMIT = 16384
WRITE_TIMEOUT = 10
SCAN_SECONDS = 10


class BleStream:
    """Synchronous worker-facing stream; all Bleak I/O stays on the asyncio loop."""

    def __init__(self, client, loop):
        self.client = client
        self.loop = loop
        self.timeout = 0.1
        self._condition = threading.Condition()
        self._buffer = bytearray()
        self._error = None
        self.tx_bytes = 0
        self.rx_bytes = 0

    def notify(self, _characteristic, data):
        with self._condition:
            if self._error is not None:
                return
            self.rx_bytes += len(data)
            if len(self._buffer) + len(data) > RX_LIMIT:
                self._error = "BLE receive buffer overflow; reconnect"
                self._buffer.clear()
            else:
                self._buffer.extend(data)
            self._condition.notify_all()

    def close(self):
        with self._condition:
            if self._error is None:
                self._error = "BLE connection closed"
            self._buffer.clear()
            self._condition.notify_all()

    def read(self, size):
        deadline = time.monotonic() + self.timeout
        with self._condition:
            while not self._buffer and self._error is None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return b""
                self._condition.wait(remaining)
            if self._error is not None:
                raise OSError(self._error)
            result = bytes(self._buffer[:size])
            del self._buffer[:size]
            return result

    async def _write(self, data):
        # With-response writes provide pacing even at the default 23-byte ATT MTU.
        chunk_size = min(512, max(20, self.client.mtu_size - 3))
        for offset in range(0, len(data), chunk_size):
            if not self.client.is_connected:
                raise OSError("BLE disconnected during write")
            chunk = data[offset:offset + chunk_size]
            await self.client.write_gatt_char(RX_UUID, chunk, response=True)
            self.tx_bytes += len(chunk)

    def write(self, data):
        with self._condition:
            if self._error is not None:
                raise OSError(self._error)
        future = asyncio.run_coroutine_threadsafe(self._write(bytes(data)), self.loop)
        try:
            future.result(timeout=WRITE_TIMEOUT)
        except BaseException:
            future.cancel()
            self.close()
            raise
        return len(data)


def matching_devices(found, selector=None):
    candidates = []
    for device, advertisement in found.values():
        services = [uuid.lower() for uuid in (advertisement.service_uuids or [])]
        if SERVICE_UUID not in services:
            continue
        name = advertisement.local_name or device.name or "Highboy"
        if selector and selector.casefold() not in (device.address.casefold(), name.casefold()):
            continue
        candidates.append((device, name, getattr(advertisement, "rssi", None)))
    return sorted(candidates, key=lambda item: (item[1].casefold(), item[0].address))


def select_device(found, selector):
    candidates = matching_devices(found, selector)
    if len(candidates) != 1:
        available = ", ".join(f"{name} [{device.address}]" for device, name, _ in candidates)
        raise ValueError("Open PC Monitor and select one Highboy with --device NAME_OR_ADDRESS. "
                         f"Matches: {available or 'none'}")
    return candidates[0][0]


def prompt_device(found):
    candidates = matching_devices(found)
    print("\nHighboys found:")
    for index, (device, name, rssi) in enumerate(candidates, 1):
        signal = f"{rssi} dBm" if rssi is not None else "signal unavailable"
        print(f"  {index}. {name}  [{device.address}]  {signal}")
    if not candidates:
        print("  None. Open Developer > PC Monitor on Highboy and enable Bluetooth on the PC.")
    if not sys.stdin.isatty():
        raise ValueError("Interactive selection requires a terminal; use --device NAME_OR_ADDRESS")
    while True:
        answer = input("Select number, R to scan again, Q to cancel: ").strip().casefold()
        if answer == "q":
            raise KeyboardInterrupt
        if answer == "r":
            return None
        if answer.isdecimal() and 1 <= int(answer) <= len(candidates):
            return candidates[int(answer) - 1][0]
        print("Invalid selection. Enter a listed number, R or Q.")


async def _run(selector, callback, prepare):
    from bleak import BleakClient, BleakScanner
    from bleak.exc import BleakError

    stream = None
    try:
        while True:
            print("Scanning for Highboy BLE (10s); keep PC Monitor open...", flush=True)
            found = await BleakScanner.discover(timeout=SCAN_SECONDS, return_adv=True,
                                               service_uuids=[SERVICE_UUID])
            device = select_device(found, selector) if selector else prompt_device(found)
            if device is not None:
                break
        print(f"Selected: {device.name or 'Highboy'} [{device.address}]")
        # No connection is open yet; keep terminal prompts interruptible on the main thread.
        prepared = prepare()
        def disconnected(_client):
            if stream is not None:
                stream.close()
        async with BleakClient(device, disconnected_callback=disconnected, timeout=20) as client:
            stream = BleStream(client, asyncio.get_running_loop())
            try:
                await client.start_notify(TX_UUID, stream.notify)
                # Let the P4's connection-status poll acquire the BLE session before HELLO.
                await asyncio.sleep(1)
                try:
                    return await asyncio.to_thread(callback, stream, device.name or device.address, prepared)
                except TimeoutError as error:
                    if str(error).startswith("no HELLO_ACK"):
                        raise TimeoutError(
                            f"No HELLO_ACK after BLE connection and notification subscription "
                            f"(GATT writes accepted: {stream.tx_bytes} bytes; "
                            f"notifications received: {stream.rx_bytes} bytes). "
                            "This does not prove the PSK is wrong. Check the selected device's PSK "
                            "and keep PC Monitor open while retrying. If its screen shows a pairing "
                            "code but the CLI does not, report it: the return path needs diagnosis."
                        ) from error
                    raise
            finally:
                stream.close()
    except BleakError as error:
        raise OSError(f"BLE: {error}") from error


def run(selector, callback, prepare):
    return asyncio.run(_run(selector, callback, prepare))
