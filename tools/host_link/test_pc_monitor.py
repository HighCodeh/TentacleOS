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

"""Host regression tests for PC telemetry, including the production C receiver."""
import asyncio
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

# Protocol tests have no serial-port or third-party Python requirements in CI.
serial = types.ModuleType("serial")
serial.Serial = object
serial.tools = types.ModuleType("serial.tools")
serial.tools.list_ports = types.SimpleNamespace(comports=lambda: [])
with patch.dict(sys.modules, {"serial": serial, "serial.tools": serial.tools}):
    import pc_monitor as monitor

import pc_monitor_ble as ble

ROOT = Path(__file__).resolve().parents[2]
MEMORY = types.SimpleNamespace(total=8192 * monitor.MIB, available=2048 * monitor.MIB, percent=75.0)
DISK = types.SimpleNamespace(total=256 * 1024 * monitor.MIB, used=128 * 1024 * monitor.MIB, percent=50.0)


def sample():
    return monitor.pack_sample(12.34, MEMORY, DISK, "test-pc", "/", "192.168.1.2", "2001:db8::1")


class MonitorTests(unittest.TestCase):
    def test_wire_and_invalid_measurements(self):
        data = sample()
        self.assertEqual(len(data), 180)
        self.assertEqual(struct.unpack_from("<BBHHHIIII", data),
                         (1, 0, 1234, 7500, 5000, 6144, 8192, 131072, 262144))
        for value in (-1, 101, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                monitor.percent(value)
        with self.assertRaises(ValueError):
            monitor.ip_field("999.1.2.3")
        self.assertEqual(monitor.text_field("a" * 80, 32), b"a" * 31 + b"\0")
        self.assertEqual(monitor.text_field("PC\n", 4), b"PC?\0")

    def test_network_failure_and_public_validation(self):
        with patch.object(monitor.socket, "socket", side_effect=OSError):
            self.assertEqual(monitor.local_address(None, None), "")
        response = unittest.mock.MagicMock()
        response.__enter__.return_value.read.return_value = b"203.0.113.1\n"
        with patch.object(monitor.urllib.request, "urlopen", return_value=response):
            self.assertEqual(monitor.fetch_public_ip(), "203.0.113.1")
            response.__enter__.return_value.read.return_value = b"<html>error</html>"
            with self.assertRaises(ValueError):
                monitor.fetch_public_ip()
        metrics = types.SimpleNamespace(net_if_addrs=lambda: {"eth0": [
            types.SimpleNamespace(family=monitor.socket.AF_INET6, address="fe80::1%eth0")]})
        self.assertEqual(monitor.local_address("eth0", metrics), "fe80::1")
        self.assertEqual(monitor.local_address("missing", metrics), "")

    def test_response_matching(self):
        link = monitor.MonitorLink.__new__(monitor.MonitorLink)
        link._send = unittest.mock.Mock()
        link._body = lambda frame: frame
        link._read_frame = unittest.mock.Mock(side_effect=[b"\x02\x00\x01\x00",
                                                          b"\x04\x00\x00log",
                                                          b"\x02\x00\x4e\x00"])
        link.publish(sample())
        self.assertEqual(link._read_frame.call_count, 3)
        link._read_frame = lambda _: b"\x02\x00\x4e\x04"
        with self.assertRaises(RuntimeError):
            link.publish(sample())
        link._read_frame = lambda _: None
        with self.assertRaises(TimeoutError):
            link.publish(sample())

    def test_monitor_tolerates_missing_ack_and_resets_budget(self):
        args = types.SimpleNamespace(no_public_ip=True, interval=1, interface=None)
        metrics = types.SimpleNamespace(cpu_percent=unittest.mock.Mock(return_value=12),
            virtual_memory=lambda: MEMORY, disk_usage=lambda _: DISK)
        timeout = monitor.TelemetryAckTimeout("missing ACK")
        link = unittest.mock.Mock()
        link.publish.side_effect = [timeout, timeout, None, timeout, None, KeyboardInterrupt()]
        with patch.object(monitor, "local_address", return_value=""), patch("builtins.print") as output:
            with self.assertRaises(KeyboardInterrupt):
                monitor.monitor_session(args, link, b"", metrics, "/", "test")
        self.assertEqual(link.publish.call_count, 6)
        self.assertEqual(metrics.cpu_percent.call_count, 7)
        self.assertEqual(sum("resumed" in str(c) for c in output.call_args_list), 2)
        link.close.assert_called_once()

    def test_monitor_stops_on_persistent_loss_or_other_errors(self):
        args = types.SimpleNamespace(no_public_ip=True, interval=1, interface=None)
        metrics = types.SimpleNamespace(cpu_percent=lambda **_: 12,
            virtual_memory=lambda: MEMORY, disk_usage=lambda _: DISK)
        for error, calls in ((monitor.TelemetryAckTimeout("missing ACK"), monitor.TELEMETRY_MAX_MISSES),
                             (TimeoutError("GATT write timed out"), 1),
                             (OSError("BLE disconnected"), 1),
                             (PermissionError("bad MAC"), 1),
                             (RuntimeError("device rejected sample"), 1)):
            link = unittest.mock.Mock()
            link.publish.side_effect = error
            with patch.object(monitor, "local_address", return_value=""), patch("builtins.print"):
                with self.assertRaises(type(error)):
                    monitor.monitor_session(args, link, b"", metrics, "/", "test")
            self.assertEqual(link.publish.call_count, calls)
            link.close.assert_called_once()

    def test_missing_ack_uses_fresh_authenticated_counter(self):
        stream = unittest.mock.Mock()
        link = monitor.MonitorLink.from_stream(stream)
        link.k_a2d = bytes(32)
        link.tx_counter = 2
        link._read_frame = lambda _: None
        for cpu in (12, 34):
            with self.assertRaises(monitor.TelemetryAckTimeout):
                link.publish(monitor.pack_sample(cpu, MEMORY, DISK, "pc", "/", "", ""))
        frames = [call.args[0] for call in stream.write.call_args_list]
        self.assertEqual([struct.unpack_from("<I", frame, 4)[0] for frame in frames], [2, 3])
        self.assertEqual([struct.unpack_from("<H", frame, 15)[0] for frame in frames], [1200, 3400])

    def test_port_selection(self):
        with self.assertRaises(ValueError):
            monitor.find_port()
        port = types.SimpleNamespace(vid=monitor.USB_VID, pid=monitor.USB_PID, device="test")
        with patch.object(monitor.list_ports, "comports", return_value=[port]):
            self.assertEqual(monitor.find_port(), "test")
        with patch.object(monitor.list_ports, "comports", return_value=[port, port]):
            with self.assertRaises(ValueError):
                monitor.find_port()

    def test_cli_closes_transport_on_cancel_and_error(self):
        metrics = types.SimpleNamespace(disk_usage=lambda _: DISK)
        for error, expected in ((KeyboardInterrupt(), 0), (PermissionError("pairing rejected"), 1)):
            link = unittest.mock.Mock()
            link.handshake.side_effect = error
            with patch.dict(sys.modules, {"psutil": metrics}), \
                 patch.dict(os.environ, {"HB_PSK": "00" * 32}), \
                 patch.object(monitor, "MonitorLink", return_value=link), \
                 patch("builtins.print"):
                self.assertEqual(monitor.main(["--port", "test", "--no-public-ip"]), expected)
                link.close.assert_called_once()

    def test_public_failure_clears_cached_address(self):
        public = monitor.PublicAddress()
        public._value = "203.0.113.1"
        public._stop = unittest.mock.Mock()
        public._stop.is_set.side_effect = [False, True]
        with patch.object(monitor, "fetch_public_ip", side_effect=OSError("offline")):
            public._run()
        self.assertEqual(public.get(), "")
        public._stop.wait.assert_called_once_with(monitor.PUBLIC_IP_INTERVAL)

    def test_psk_needs_no_extra_terminal_confirmation(self):
        with patch.dict(os.environ, {"HB_PSK": "01" * 32}), \
             patch("builtins.input", side_effect=AssertionError("unexpected Enter prompt")):
            self.assertEqual(monitor.read_psk(), b"\x01" * 32)

    def test_default_psk_and_invalid_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "monitor.psk"
            notice = (ROOT / "tools/host_link/pc_monitor.py").read_text().split('"""')[0]
            path.write_text(notice + "01" * 32 + "\n")
            with patch.dict(os.environ, {}, clear=True), patch.object(monitor, "DEFAULT_PSK_PATH", path):
                self.assertEqual(monitor.read_psk(), bytes.fromhex("01" * 32))
                path.unlink()
                with self.assertRaisesRegex(ValueError, "Set HB_PSK"):
                    monitor.read_psk()
        for value in ("", "not-hex", "00"):
            with patch.dict(os.environ, {"HB_PSK": value}):
                with self.assertRaises(ValueError):
                    monitor.read_psk()

    def test_gatt_rejects_failed_relay(self):
        source = (ROOT / "firmware_c5/components/Service/host_link/host_link_gatt.c").read_text()
        start = source.index("static int rx_access(", source.index("esp_err_t host_link_gatt_init"))
        function = source[start:source.index("static int tx_access(", start)]
        notice = source.split("#include")[0]
        fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define OS_MBUF_PKTLEN(om) (*(uint16_t *)(om))
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
struct ble_gatt_access_ctxt { int op; void *om; };
static uint8_t s_rx_buf[512];
static bool s_has_rx_write;
static int copy_error, relay_calls;
static bool relay_ok;
static int os_mbuf_copydata(void *om, int offset, int len, void *buf) {
  (void)om; (void)offset; memset(buf, 0, len); return copy_error;
}
static bool host_transport_send_to_p4(const uint8_t *data, uint16_t len) {
  (void)data; (void)len; ++relay_calls; return relay_ok;
}
"""
        main = r"""
int main(void) {
  uint16_t length = 78;
  struct ble_gatt_access_ctxt ctx = {1, &length};
  assert(rx_access(0, 0, &ctx, 0) == BLE_ATT_ERR_INSUFFICIENT_RES);
  assert(relay_calls == 1 && !s_has_rx_write);
  relay_ok = true;
  assert(rx_access(0, 0, &ctx, 0) == 0 && s_has_rx_write);
  copy_error = 1;
  assert(rx_access(0, 0, &ctx, 0) == BLE_ATT_ERR_UNLIKELY);
  assert(relay_calls == 2);
  length = 513;
  assert(rx_access(0, 0, &ctx, 0) == BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
  length = 0;
  assert(rx_access(0, 0, &ctx, 0) == BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
  assert(relay_calls == 2);
  return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="pc-monitor-gatt-") as tmp:
            directory = Path(tmp)
            c_file = directory / "test.c"
            c_file.write_text(notice + fixture + function + main)
            subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                str(c_file), "-o", str(directory / "test")], check=True)
            subprocess.run([str(directory / "test")], check=True)

    def test_production_receiver(self):
        service = ROOT / "firmware_p4/components/Service/host_link"
        notice = (service / "host_pc_monitor.c").read_text().split('#include')[0]
        with tempfile.TemporaryDirectory(prefix="pc-monitor-test-") as tmp:
            directory = Path(tmp)
            def stub(name, content):
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(notice + content)
            stub("esp_log.h", '#define ESP_LOGD(tag, ...) ((void)(tag))\n')
            stub("esp_timer.h", '#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
            stub("host_link.h", '#include <stdint.h>\nuint32_t host_link_output_generation(void);\n')
            stub("freertos/FreeRTOS.h", """typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
""")
            stub("lwip/sockets.h", '#include <arpa/inet.h>\n')
            # Include the real shared header's command/status definitions without ESP CRC dependencies.
            header = (ROOT / "firmware_p4/components/Service/spi_bridge/include/spi_protocol.h").read_text()
            self.assertIn('SPI_ID_SYSTEM_PC_MONITOR = SPI_CMD(SPI_CAT_SYSTEM, 0x4E)', header)
            stub("spi_protocol.h", """#define SPI_ID_SYSTEM_PC_MONITOR 0x4e
#define SPI_STATUS_OK 0
#define SPI_STATUS_ERROR 2
#define SPI_STATUS_UNSUPPORTED 3
#define SPI_STATUS_INVALID_ARG 4
""")
            data = ",".join(str(value) for value in sample())
            stub("test.c", """#include <assert.h>
#include <string.h>
#include "host_pc_monitor.h"
static int64_t now = 100;
static uint32_t generation = 7;
int64_t esp_timer_get_time(void) { return now; }
uint32_t host_link_output_generation(void) { return generation; }
static uint8_t wire[] = {""" + data + """};
static uint8_t send_sample(const uint8_t *data, uint16_t size) {
  uint16_t length = 99;
  uint8_t status = host_pc_monitor_handle(0x4e, data, size, NULL, 0, &length);
  assert(length == 0);
  return status;
}
int main(void) {
  host_pc_monitor_snapshot_t out;
  assert(!host_pc_monitor_get(&out));
  assert(send_sample(wire, sizeof(wire)) == 0);
  assert(host_pc_monitor_get(&out));
  assert(out.cpu == 1234 && out.ram_used_mib == 6144);
  assert(strcmp(out.hostname, "test-pc") == 0);
  assert(strcmp(out.public_ip, "2001:db8::1") == 0);
  for (uint16_t i = 0; i < sizeof(wire); i++) assert(send_sample(wire, i) == 4);
  assert(send_sample(NULL, sizeof(wire)) == 4);
  uint8_t bad[sizeof(wire)];
  memcpy(bad, wire, sizeof(wire)); bad[0] = 2; assert(send_sample(bad, sizeof(bad)) == 4);
  memcpy(bad, wire, sizeof(wire)); bad[1] = 1; assert(send_sample(bad, sizeof(bad)) == 4);
  memcpy(bad, wire, sizeof(wire)); bad[2] = bad[3] = 255; assert(send_sample(bad, sizeof(bad)) == 4);
  memcpy(bad, wire, sizeof(wire)); memset(bad + 12, 0, 4); assert(send_sample(bad, sizeof(bad)) == 4);
  memcpy(bad, wire, sizeof(wire)); memset(bad + 24, 'x', 32); assert(send_sample(bad, sizeof(bad)) == 4);
  memcpy(bad, wire, sizeof(wire)); bad[24] = 10; assert(send_sample(bad, sizeof(bad)) == 4);
  memcpy(bad, wire, sizeof(wire)); bad[88] = 'x'; assert(send_sample(bad, sizeof(bad)) == 4);
  assert(host_pc_monitor_get(&out)); assert(out.cpu == 1234);
  now += HOST_PC_MONITOR_STALE_US - 1; assert(host_pc_monitor_get(&out));
  now++; assert(!host_pc_monitor_get(&out)); assert(out.hostname[0] == 0);
  assert(send_sample(wire, sizeof(wire)) == 0);
  generation++; assert(!host_pc_monitor_get(&out));
  generation = 0; assert(send_sample(wire, sizeof(wire)) == 2);
  assert(!host_pc_monitor_get(NULL));
  return 0;
}
""")
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I" + str(directory), "-I" + str(service / "include"),
                str(service / "host_pc_monitor.c"), str(directory / "test.c"), "-o", str(directory / "test")]
            # The source's host_link.h is found only in include/, after the fake headers.
            subprocess.run(command, check=True)
            subprocess.run([str(directory / "test")], check=True)


class BleTests(unittest.IsolatedAsyncioTestCase):
    async def test_chunking_and_notification_stream(self):
        for mtu in (23, 185, 512):
            client = types.SimpleNamespace(mtu_size=mtu, is_connected=True,
                                          write_gatt_char=unittest.mock.AsyncMock())
            stream = ble.BleStream(client, asyncio.get_running_loop())
            frame = bytes(range(209))
            self.assertEqual(await asyncio.to_thread(stream.write, frame), len(frame))
            chunks = [call.args[1] for call in client.write_gatt_char.call_args_list]
            self.assertEqual(b"".join(chunks), frame)
            self.assertTrue(all(len(chunk) <= mtu - 3 for chunk in chunks))
            self.assertTrue(all(call.kwargs == {"response": True}
                                for call in client.write_gatt_char.call_args_list))
            for chunk in chunks:
                stream.notify(None, chunk)
            result = bytearray()
            while len(result) < len(frame):
                result.extend(await asyncio.to_thread(stream.read, 17))
            self.assertEqual(result, frame)
            self.assertEqual(stream.tx_bytes, len(frame))
            self.assertEqual(stream.rx_bytes, len(frame))
            self.assertEqual(await asyncio.to_thread(stream.read, 1), b"")
            stream.close()
            with self.assertRaises(OSError):
                await asyncio.to_thread(stream.read, 1)

    async def test_disconnect_overflow_and_write_failure(self):
        client = types.SimpleNamespace(mtu_size=23, is_connected=False,
                                      write_gatt_char=unittest.mock.AsyncMock())
        stream = ble.BleStream(client, asyncio.get_running_loop())
        with self.assertRaises(OSError):
            await asyncio.to_thread(stream.write, b"test")
        client.is_connected = True
        stream = ble.BleStream(client, asyncio.get_running_loop())
        stream.notify(None, b"x" * (ble.RX_LIMIT + 1))
        with self.assertRaisesRegex(OSError, "overflow"):
            await asyncio.to_thread(stream.read, 1)
        self.assertEqual(len(stream._buffer), 0)
        stream = ble.BleStream(client, asyncio.get_running_loop())
        client.write_gatt_char.side_effect = OSError("write failed")
        with self.assertRaisesRegex(OSError, "write failed"):
            await asyncio.to_thread(stream.write, b"test")
        with self.assertRaises(OSError):
            await asyncio.to_thread(stream.read, 1)

    async def test_interactive_selection(self):
        first = types.SimpleNamespace(address="a", name="Tentacle-A")
        second = types.SimpleNamespace(address="b", name="Tentacle-B")
        ad = types.SimpleNamespace(service_uuids=[ble.SERVICE_UUID], local_name=None, rssi=-50)
        wrong = types.SimpleNamespace(service_uuids=["other"], local_name="Unrelated")
        found = {"b": (second, ad), "a": (first, ad), "c": (first, wrong)}
        with patch.object(ble.sys.stdin, "isatty", return_value=True), patch("builtins.print"), \
             patch("builtins.input", side_effect=["invalid", "0", "3", "2"]):
            self.assertIs(ble.prompt_device(found), second)
        with patch.object(ble.sys.stdin, "isatty", return_value=True), patch("builtins.print"), \
             patch("builtins.input", return_value="R"):
            self.assertIsNone(ble.prompt_device({}))
        with patch.object(ble.sys.stdin, "isatty", return_value=True), patch("builtins.print"), \
             patch("builtins.input", return_value="q"):
            with self.assertRaises(KeyboardInterrupt):
                ble.prompt_device(found)
        with patch.object(ble.sys.stdin, "isatty", return_value=False), patch("builtins.print"):
            with self.assertRaises(ValueError):
                ble.prompt_device(found)

    async def test_scan_and_context_cleanup(self):
        device = types.SimpleNamespace(address="os-uuid", name="Tentacle-1234")
        advertisement = types.SimpleNamespace(service_uuids=[ble.SERVICE_UUID], local_name=device.name)
        found = {device.address: (device, advertisement)}
        self.assertIs(ble.select_device(found, "Tentacle-1234"), device)
        self.assertIs(ble.select_device(found, "os-uuid"), device)
        with self.assertRaises(ValueError):
            ble.select_device(found, "wrong")
        with self.assertRaises(ValueError):
            ble.select_device({**found, "duplicate": (device, advertisement)}, None)
        client = unittest.mock.MagicMock()
        client.__aenter__ = unittest.mock.AsyncMock(return_value=client)
        client.__aexit__ = unittest.mock.AsyncMock(return_value=False)
        client.start_notify = unittest.mock.AsyncMock()
        module = types.ModuleType("bleak")
        module.BleakScanner = types.SimpleNamespace(discover=unittest.mock.AsyncMock(return_value=found))
        module.BleakClient = unittest.mock.Mock(return_value=client)
        errors = types.ModuleType("bleak.exc")
        errors.BleakError = type("BleakError", (Exception,), {})
        def callback(stream, name, prepared):
            self.assertEqual(prepared, "ready")
            self.assertEqual(name, device.name)
            raise OSError("worker stopped")
        def prepare():
            module.BleakClient.assert_not_called()
            return "ready"
        with patch.dict(sys.modules, {"bleak": module, "bleak.exc": errors}), \
             patch.object(ble, "prompt_device", side_effect=[None, device]):
            with self.assertRaisesRegex(OSError, "worker stopped"):
                await ble._run(None, callback, prepare)
        self.assertEqual(module.BleakScanner.discover.await_count, 2)
        client.__aexit__.assert_awaited_once()
        module.BleakClient.reset_mock()
        def timeout_callback(stream, name, prepared):
            raise TimeoutError("no HELLO_ACK; open the Companion pairing screen on the device")
        with patch.dict(sys.modules, {"bleak": module, "bleak.exc": errors}):
            with self.assertRaisesRegex(TimeoutError, "GATT writes accepted: 0 bytes; notifications received: 0 bytes"):
                await ble._run("Tentacle-1234", timeout_callback, prepare)
        stream = client.start_notify.call_args.args[1].__self__
        with self.assertRaises(OSError):
            stream.read(1)



if __name__ == "__main__":
    unittest.main()
