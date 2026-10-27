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

"""Compile the production classifier and P4 proxy with host sanitizers; no radio access."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent


def main():
    with tempfile.TemporaryDirectory(prefix="highboy-quick-scan-") as temporary:
        stub = Path(temporary)
        (stub / "esp_err.h").write_text("""
#ifndef ESP_ERR_H
#define ESP_ERR_H
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
const char *esp_err_to_name(esp_err_t);
#endif
""")
        (stub / "esp_rom_crc.h").write_text("""
#include <stdint.h>
static inline uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *data, uint32_t len) {
  (void)data; (void)len; return crc;
}
""")
        (stub / "esp_log.h").write_text("""
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
""")
        (stub / "spi_bridge.h").write_text("""
#include <stddef.h>
#include "esp_err.h"
#include "spi_protocol.h"
esp_err_t spi_bridge_run_scan(spi_id_t, spi_id_t, const uint8_t *, uint8_t);
esp_err_t spi_bridge_send_command(spi_id_t, const uint8_t *, uint8_t, spi_header_t *,
                                  uint8_t *, size_t, uint32_t);
""")
        common = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                  "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-I", str(stub)]
        (stub / "esp_heap_caps.h").write_text("""
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_calloc(size_t, size_t, unsigned);
""")
        (stub / "esp_timer.h").write_text("#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
        (stub / "esp_wifi.h").write_text("""
#ifndef ESP_WIFI_H
#define ESP_WIFI_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { WIFI_PKT_MGMT, WIFI_PKT_CTRL, WIFI_PKT_DATA, WIFI_PKT_MISC } wifi_promiscuous_pkt_type_t;
typedef struct { int8_t rssi; uint8_t channel; uint8_t rx_state; uint16_t sig_len; } wifi_pkt_rx_ctrl_t;
typedef struct { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[64]; } wifi_promiscuous_pkt_t;
typedef void (*wifi_promiscuous_cb_t)(void *, wifi_promiscuous_pkt_type_t);
#endif
""")
        (stub / "freertos").mkdir()
        (stub / "freertos/FreeRTOS.h").write_text("""
#ifndef FREERTOS_H
#define FREERTOS_H
#include <stdint.h>
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
#endif
""")
        (stub / "freertos/semphr.h").write_text("""
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, uint32_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
""")
        (stub / "freertos/queue.h").write_text("""
#include <stddef.h>
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, size_t);
void vQueueDelete(QueueHandle_t);
BaseType_t xQueueReset(QueueHandle_t);
BaseType_t xQueueSend(QueueHandle_t, const void *, uint32_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, uint32_t);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t);
""")
        (stub / "freertos/task.h").write_text("""
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, unsigned,
                                  TaskHandle_t *, unsigned);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(uint32_t);
""")
        (stub / "sys_prio.h").write_text("#define SYS_PRIO_SERVICE_HI 5\n#define SYS_CORE_MAIN 0\n"
                                        "#define SYS_PRIO_SERVICE_LO 4\n#define SYS_CORE_RADIO 0\n")
        (stub / "esp_attr.h").write_text("#define EXT_RAM_BSS_ATTR\n")
        (stub / "wifi_service.h").write_text("""
#include "esp_wifi.h"
typedef bool (*wifi_service_capture_tick_cb_t)(void *);
typedef struct { uint32_t duration_ms; uint8_t band; wifi_service_capture_tick_cb_t tick_cb;
                 void *ctx; } wifi_service_capture_config_t;
bool wifi_service_is_busy(void);
bool wifi_service_is_active(void);
esp_err_t wifi_service_capture_passive(const wifi_service_capture_config_t *, wifi_promiscuous_cb_t);
""")
        for target in ("core", "proxy", "runtime", "view"):
            chip = "p4" if target in ("proxy", "view") else "c5"
            app = ROOT / f"firmware_{chip}/components/Applications/wifi"
            service = ROOT / f"firmware_{chip}/components/Service"
            sources = [TESTS / f"test_{target}.c"]
            if target in ("core", "runtime"):
                sources += [app / "wifi_quick_scan_core.c", service / "wifi/mac_vendor.c"]
            if target in ("proxy", "runtime"):
                sources += [app / "wifi_quick_scan.c"]
            if target == "view":
                sources += [app / "wifi_scan_view.c", service / "wifi/mac_vendor.c"]
            binary = stub / target
            vendor_flags = ["-DMAC_VENDOR_ESPRESSIF_ONLY=1"] if target == "view" else []
            subprocess.run(common + vendor_flags + ["-I", str(app / "include"), "-I", str(service / "wifi/include"),
                                     "-I", str(service / "spi_bridge/include"),
                                     *map(str, sources), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print(f"{target}: passed (ASan + UBSan)")
        service_source = (ROOT / "firmware_c5/components/Service/wifi/wifi_service.c").read_text()
        # Compile the actual scoped driver helper with failure-injecting driver shims.
        defines = service_source.split("#define HOPPER_STACK_SIZE", 1)[1].split(
            "static wifi_ap_record_t", 1)[0]
        band_function = service_source.split("static wifi_band_mode_t band_mode_from_selector", 1)[1].split(
            "esp_err_t wifi_service_set_analysis_band", 1)[0]
        capture = service_source.split("static const uint8_t HOP_CHANNELS_2G", 1)[1].split(
            "static void channel_hopper_task(void *pvParameters) {", 1)[0]
        (stub / "wifi_capture_under_test.inc").write_text(
            "#define HOPPER_STACK_SIZE" + defines +
            "static wifi_band_mode_t band_mode_from_selector" + band_function +
            "static const uint8_t HOP_CHANNELS_2G" + capture)
        binary = stub / "capture"
        subprocess.run(common + ["-Wno-unused-const-variable", str(TESTS / "test_capture.c"),
                                 "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print("capture: passed (ASan + UBSan, setup/cleanup failure injection)")
        (stub / "esp_rom_crc.h").write_text("""
#include <stdint.h>
static inline uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *data, uint32_t len) {
  crc = (uint16_t)~crc;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; bit++)
      crc = (uint16_t)((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
  }
  return (uint16_t)~crc;
}
""")
        service = ROOT / "firmware_p4/components/Service/spi_bridge"
        binary = stub / "realign"
        subprocess.run(common + ["-I", str(service), "-I", str(service / "include"),
                                 str(TESTS / "test_response_realign.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print("realign: passed (ASan + UBSan, displaced response/CRC/ambiguity checks)")


if __name__ == "__main__":
    main()
