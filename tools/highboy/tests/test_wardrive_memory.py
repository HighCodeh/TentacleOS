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

"""Exercise the C5 survey snapshot with failing PSRAM and asynchronous requests."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SERVICE = ROOT / "firmware_c5/components/Service"
NOTICE = (SERVICE / "spi_bridge/wardrive_radio.c").read_text().split('#include', 1)[0]


def main():
    with tempfile.TemporaryDirectory(prefix="highboy-wardrive-") as temporary:
        directory = Path(temporary)
        stubs = {
            "esp_err.h": """
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
""",
            "esp_rom_crc.h": """
#include <stdint.h>
static inline uint16_t esp_rom_crc16_le(uint16_t c, const uint8_t *d, uint32_t n) {
  (void)d; (void)n; return c;
}
""",
            "esp_log.h": """
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
""",
            "esp_heap_caps.h": """
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_calloc(size_t, size_t, unsigned);
""",
            "freertos/FreeRTOS.h": """
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
""",
            "spi_bridge.h": """
#include <stdbool.h>
bool spi_bridge_async_scan_busy(void);
bool spi_bridge_async_scan_start(void (*fn)(void));
""",
            "wifi_service.h": """
#include "esp_err.h"
#include <stdint.h>
typedef struct { uint8_t bssid[6], ssid[33], primary, authmode; int8_t rssi; } wifi_ap_record_t;
esp_err_t wifi_service_ward_scan(uint8_t, wifi_ap_record_t *, uint16_t *, uint16_t *);
""",
        }
        for name, text in stubs.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(NOTICE + text)
        binary = directory / "test"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
                   "-I", str(directory), "-I", str(SERVICE / "spi_bridge/include"),
                   "-I", str(SERVICE / "spi_bridge"),
                   str(Path(__file__).with_suffix(".c")), "-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
        print("C5 survey: PSRAM failure/retry, reuse, capacity, stale/busy requests and errors passed")


if __name__ == "__main__":
    main()
