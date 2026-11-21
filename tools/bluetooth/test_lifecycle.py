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

"""Run production BLE lifecycle paths with threaded host/controller fault injection."""
from pathlib import Path
import re
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/nfc_api"))
from test_emu_tx import extract

source = (ROOT / "firmware_c5/components/Service/bluetooth/bluetooth_service.c").read_text()
start = source.index("#define BLE_ANNOUNCE_CONFIG_FILE")
end = source.index("esp_err_t bluetooth_service_init(void) {")
globals_code = source[start:end]
signatures = [
    "esp_err_t bluetooth_service_init(void)", "esp_err_t bluetooth_service_start(void)",
    "esp_err_t bluetooth_service_stop(void)", "esp_err_t bluetooth_service_deinit(void)",
    "bool bluetooth_service_is_initialized(void)", "bool bluetooth_service_is_running(void)",
    "void bluetooth_service_scan(uint32_t duration_ms)",
    "esp_err_t bluetooth_service_start_sniffer(bluetooth_service_sniffer_cb_t cb)",
    "esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, bluetooth_service_tracker_cb_t cb)",
    "static esp_err_t init_locked(void)", "static esp_err_t start_locked(void)",
    "static esp_err_t stop_locked(void)", "static esp_err_t deinit_locked(void)",
    "static void on_reset(int reason)", "static void on_sync(void)",
    "static void host_task(void *param)", "static void stop_task(void *param)",
]
functions = "\n\n".join(extract(source, signature + " {") for signature in signatures)
fixture = Path(__file__).with_suffix(".c").read_text()
fixture = fixture.replace("/* GLOBALS */", globals_code).replace("/* FUNCTIONS */", functions)
with tempfile.TemporaryDirectory(prefix="highboy-ble-lifecycle-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(fixture)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=30)
