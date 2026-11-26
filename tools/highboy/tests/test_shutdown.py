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

"""Run the production charger and shutdown policy against I2C/GPIO/lock faults."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
DRIVER = ROOT / "firmware_p4/components/Drivers/bq25896"
NOTICE = (DRIVER / "bq25896.c").read_text().split("#include", 1)[0]
MOCKS = {
    "husb320.h": """#pragma once
#include "esp_err.h"
static bool test_host_mode, test_host_attached;
static inline bool husb320_host_mode(void) { return test_host_mode; }
static inline esp_err_t husb320_host_attached(bool *out) { *out=test_host_attached; return ESP_OK; }
""",
    "esp_err.h": """#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_TIMEOUT,
      ESP_ERR_NO_MEM,ESP_ERR_NOT_SUPPORTED,ESP_ERR_INVALID_RESPONSE,ESP_ERR_NOT_FOUND};
const char *esp_err_to_name(int error);
""",
    "esp_log.h": """#pragma once
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
""",
    "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "freertos/FreeRTOS.h": """#pragma once
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
typedef uint32_t TickType_t;
""",
    "freertos/semphr.h": """#pragma once
typedef void *SemaphoreHandle_t;
void *xSemaphoreCreateMutex(void);
int xSemaphoreTake(void *,uint32_t);
void xSemaphoreGive(void *);
""",
    "freertos/task.h": "void vTaskDelay(uint32_t ticks);\n",
    "driver/gpio.h": """#include "esp_err.h"
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0
int gpio_config(const gpio_config_t *);
int gpio_set_level(int pin,int level);
int gpio_get_level(int pin);
""",
    "pin_def.h": "#define GPIO_CHARGER_OTG_PIN 33\n",
    "i2c_init.h": """#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
#define I2C_ADDR_BIT_LEN_7 7
#define I2C_MASTER_FREQ_HZ 100000
void *i2c_get_bus(void);
int i2c_bus_recover(void);
int i2c_master_bus_add_device(void *,const i2c_device_config_t *,void **);
int i2c_master_transmit(void *,const uint8_t *,size_t,int);
int i2c_master_transmit_receive(void *,const uint8_t *,size_t,uint8_t *,size_t,int);
""",
}


def main():
    source = (ROOT / "firmware_p4/components/Applications/ui/components/power_policy/power_policy.c").read_text()
    start = source.index("static void begin_shutdown(")
    end = source.index("\nstatic void shutdown_confirm_cb", start)
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* SHUTDOWN_POLICY */", source[start:end])
    with tempfile.TemporaryDirectory(prefix="highboy-shutdown-") as temporary:
        work = Path(temporary)
        for name, content in MOCKS.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(NOTICE + content)
        (work / "test.c").write_text(fixture)
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                   "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                   "-fno-sanitize-recover=all", "-I" + str(work), "-I" + str(DRIVER),
                   "-I" + str(DRIVER / "include"), str(work / "test.c"), "-o", str(work / "test")]
        for board in (["-DCONFIG_HIGHBOY_BOARD_V203=1"], []):
            subprocess.run(command + board, check=True)
            subprocess.run([str(work / "test")], check=True, timeout=30)


if __name__ == "__main__":
    main()
