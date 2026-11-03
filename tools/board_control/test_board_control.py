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

"""Compile both production board services together; inject transport and GPIO faults."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NOTICE = (ROOT / "firmware_c5/components/Drivers/pins/board_pins.c").read_text().split("#include")[0]

MOCKS = {
"esp_err.h": """#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_NO_MEM, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE,
ESP_ERR_NOT_SUPPORTED, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE, ESP_ERR_NOT_FINISHED };
""",
"esp_log.h": """#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
""",
"esp_rom_crc.h": """#include <stdint.h>
static inline uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *p, uint32_t n) {
(void)p; (void)n; return crc;
}
""",
"esp_random.h": "void esp_fill_random(void *p, size_t n);\n",
"esp_timer.h": """#pragma once
#include "esp_err.h"
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
static inline int esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h) {
(void)a; *h = (void *)1; return ESP_OK;
}
static inline int esp_timer_start_periodic(esp_timer_handle_t h, uint64_t us) {
(void)h; (void)us; return ESP_OK;
}
static inline int esp_timer_delete(esp_timer_handle_t h) { (void)h; return ESP_OK; }
""",
"freertos/FreeRTOS.h": """#pragma once
#include <assert.h>
#include "esp_err.h"
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
extern int critical, is_isr, task_fail;
#define portENTER_CRITICAL(p) do { (void)(p); assert(!critical); critical++; } while (0)
#define portEXIT_CRITICAL(p) do { (void)(p); assert(critical); critical--; } while (0)
static inline int xPortInIsrContext(void) { return is_isr; }
""",
"freertos/task.h": """#pragma once
typedef void *TaskHandle_t;
static inline int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name,
int stack, void *arg, int priority, TaskHandle_t *h, int core) {
(void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)core;
*h = (void *)1; return !task_fail;
}
static inline void xTaskNotifyGive(TaskHandle_t h) { (void)h; assert(!critical); }
static inline void ulTaskNotifyTake(int clear, int ticks) { (void)clear; (void)ticks; }
""",
"freertos/semphr.h": """#pragma once
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t h, int ticks) { (void)h; (void)ticks; return 1; }
static inline void xSemaphoreGive(SemaphoreHandle_t h) { (void)h; }
static inline void vSemaphoreDelete(SemaphoreHandle_t h) { (void)h; }
""",
"sys_prio.h": "#define SYS_PRIO_SERVICE_LO 4\n#define SYS_CORE_RADIO 0\n",
"board_pins.h": """#include "esp_err.h"
esp_err_t board_pins_set_amplifier(bool v);
esp_err_t board_pins_set_haptic_release(bool v);
esp_err_t board_pins_set_usb_enable(bool v);
bool board_pins_haptic_int_high(void);
bool board_pins_haptic_irq_pending(void);
""",
"spi_bridge.h": """#include "esp_err.h"
#include "spi_protocol.h"
esp_err_t spi_bridge_send_command(spi_id_t id, const void *data, uint16_t len,
spi_header_t *header, void *out, size_t capacity, uint32_t timeout);
""",
}


def main():
    with tempfile.TemporaryDirectory(prefix="board-control-") as temporary:
        tmp = Path(temporary)
        for name, content in MOCKS.items():
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(NOTICE + content)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        common = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                  "-g", "-I", str(tmp)]
        objects = []
        for target in ("c5", "p4"):
            components = ROOT / f"firmware_{target}/components"
            service = components / "Service/board_service"
            obj = tmp / f"{target}.o"
            cmd = compiler + common + ["-I", str(service), "-I", str(service / "include"),
                "-I", str(components / "Drivers/pins/include"),
                "-I", str(components / "Service/spi_bridge/include"),
                "-c", str(Path(__file__).with_name(f"test_{target}.c")), "-o", str(obj)]
            subprocess.run(cmd, check=True)
            objects.append(str(obj))
        binary = tmp / "test"
        subprocess.run(compiler + common + objects + ["-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print("PASS: production P4/C5 board controls, faults, epochs, lease and reconciliation")


if __name__ == "__main__":
    main()
