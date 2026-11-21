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

"""Exercise BLE preference migration, boot policy and SPI response budgets."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/nfc_api"))
from test_emu_tx import extract
UI = ROOT / "firmware_p4/components/Applications/ui"
notice_c = (ROOT / "tools/bluetooth/test_lifecycle.c").read_text().split("#include", 1)[0]
config = (ROOT / "firmware_p4/components/Service/storage_api/tos_config.c").read_text()
SERVICE = ROOT / "firmware_p4/components/Service"
CJSON = ROOT / "firmware_p4/managed_components/espressif__cjson/cJSON"
signatures = ["static void json_get_str(cJSON *obj, const char *key, char *dst, size_t max)",
              "static int json_get_int(cJSON *obj, const char *key, int fallback)",
              "static bool json_get_bool(cJSON *obj, const char *key, bool fallback)",
              "static void parse_ble(cJSON *root)", "static cJSON *serialize_ble(void)",
              "static void parse_wifi(cJSON *root)", "static cJSON *serialize_wifi(void)"]
functions = "\n".join(extract(config, signature + " {") for signature in signatures)
config_fixture = Path(__file__).with_suffix(".c").read_text().replace("/* FUNCTIONS */", functions)
with tempfile.TemporaryDirectory(prefix="highboy-ble-settings-") as tmp:
    path = Path(tmp)
    (path / "esp_err.h").write_text(notice_c + "#pragma once\ntypedef int esp_err_t;\n")
    (path / "test.c").write_text(config_fixture)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-deprecated-declarations",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-I" + str(path), "-I" + str(SERVICE / "storage_api/include"), "-I" + str(CJSON),
                    str(path / "test.c"), str(CJSON / "cJSON.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)
menu = (UI / "screens/menu/menu_ui.c").read_text()
assert "g_config_ble.enabled = true" not in menu
assert "ble_enable_task" not in menu
kernel = (ROOT / "firmware_p4/components/Core/kernel.c").read_text()
boot = kernel[kernel.index("if (g_config_ble.enabled)"):kernel.index("// Companion host link")]
assert "bluetooth_service_stop()" not in boot and "bluetooth_service_deinit()" not in boot
assert "ble_error == ESP_OK" in boot and 'boot_report_record("bluetooth"' in kernel
print("PASS: opening BLE menu does not persist/enable the radio; boot uses explicit preference")

bridge_dir = ROOT / "firmware_p4/components/Service/spi_bridge"
bridge = (bridge_dir / "spi_bridge.c").read_text()
timeout_function = extract(bridge, "uint32_t spi_bridge_get_timeout(spi_id_t id) {")
timeout_fixture = notice_c + r"""
#include <assert.h>
#include <stdio.h>
#include "spi_protocol.h"
#include "spi_timeouts.h"
""" + timeout_function + r"""
int main(void) {
 assert(spi_bridge_get_timeout(SPI_ID_BT_INIT) >= 10000);
 assert(spi_bridge_get_timeout(SPI_ID_BT_START) >= 10000);
 assert(spi_bridge_get_timeout(SPI_ID_BT_DEINIT) >= 10000);
 assert(spi_bridge_get_timeout(SPI_ID_BT_APP_SCANNER) >= 10000);
 assert(spi_bridge_get_timeout(SPI_ID_BT_HID_DEINIT) >= 10000);
 assert(spi_bridge_get_timeout(SPI_ID_BT_SCAN_STATUS) == 1000);
 assert(spi_bridge_get_timeout(SPI_ID_BT_GET_INFO) == 1000);
 assert(spi_bridge_get_timeout(SPI_ID_BT_HID_SEND_KEY) == 1000);
 assert(spi_bridge_get_timeout(SPI_ID_WIFI_SCAN) == SPI_TIMEOUT_WIFI_MS);
 puts("PASS: lifecycle response budget covers cleanup/sync without extending BLE status polling");
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-ble-timeouts-") as tmp:
    path = Path(tmp)
    (path / "esp_rom_crc.h").write_text(notice_c + "static inline uint16_t esp_rom_crc16_le(uint16_t crc,const void *data,unsigned len) { return crc; }\n")
    (path / "test.c").write_text(timeout_fixture)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-deprecated-declarations",
                    "-I" + str(path), "-I" + str(bridge_dir / "include"),
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)
