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

"""Run production confirmation, USB mux and Type-C policy against injected faults."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from test_board_control import MOCKS, NOTICE, ROOT


def main():
    with tempfile.TemporaryDirectory(prefix="board-consumers-") as temporary:
        tmp = Path(temporary)
        mocks = dict(MOCKS)
        mocks["esp_log.h"] += "#define ESP_LOGI(tag, ...) ((void)(tag))\n"
        mocks["freertos/task.h"] += "void vTaskDelay(int ticks);\n"
        mocks["freertos/semphr.h"] = """#pragma once
extern int locked;
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t h, int ticks) {
(void)h; (void)ticks; if (locked) return 0; locked=1; return 1;
}
static inline void xSemaphoreGive(SemaphoreHandle_t h) { (void)h; assert(locked); locked=0; }
static inline void vSemaphoreDelete(SemaphoreHandle_t h) { (void)h; }
"""
        mocks["tusb_desc.h"] = """#include "board_control.h"
esp_err_t busb_init(void);
bool usb_mux_is_native(void);
"""
        mocks["driver/gpio.h"] = """#include "esp_err.h"
esp_err_t gpio_set_level(int pin, uint32_t level);
"""
        mocks["driver/i2c_master.h"] = """#pragma once
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct { int dev_addr_length, device_address, scl_speed_hz; } i2c_device_config_t;
#define I2C_ADDR_BIT_LEN_7 7
int i2c_master_bus_add_device(void *, const i2c_device_config_t *, void **);
int i2c_master_bus_rm_device(void *);
int i2c_master_transmit_receive(void *, const uint8_t *, size_t, uint8_t *, size_t, int);
int i2c_master_transmit(void *, const uint8_t *, size_t, int);
"""
        mocks["i2c_init.h"] = """#include "driver/i2c_master.h"
#define I2C_MASTER_FREQ_HZ 100000
i2c_master_bus_handle_t i2c_get_bus(void);
"""
        mocks["esp_sleep.h"] = "int esp_sleep_enable_gpio_wakeup(void);\n"
        mocks["freertos/queue.h"] = """#include "esp_err.h"
typedef void *QueueHandle_t;
void *xQueueCreate(int count, size_t size);
int xQueueSend(void *q, const void *item, int ticks);
int xQueueReceive(void *q, void *item, unsigned ticks);
void vQueueDelete(void *q);
"""
        mocks["driver/gpio.h"] += """typedef struct { uint64_t pin_bit_mask; int mode, intr_type, pull_down_en, pull_up_en; } gpio_config_t;
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_LOW_LEVEL 1
#define GPIO_MODE_INPUT 2
int gpio_config(const gpio_config_t *config);
int gpio_get_level(int pin);
int gpio_wakeup_enable(int pin, int mode);
"""
        policy = (ROOT / "firmware_p4/components/Applications/ui/components/power_policy/power_policy.c").read_text()
        start = policy.index("static void check_manual_shutdown_gestures(")
        end = policy.index("\nstatic void tick_cb", start)
        (tmp / "gestures.inc").write_text(NOTICE + policy[start:end])
        for name, content in mocks.items():
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(NOTICE + content)
        drivers = ROOT / "firmware_p4/components/Drivers"
        for name in ("consumers", "husb320", "buttons"):
            command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(tmp),
                "-I", str(drivers / "pins/include"), "-I", str(drivers / "input_manager/include"), "-I", str(drivers / "husb320/include"),
                str(Path(__file__).with_name(f"test_{name}.c")), "-o", str(tmp / "test")]
            subprocess.run(command, check=True)
            subprocess.run([str(tmp / "test")], check=True)
            print(f"PASS: production {name} sequencing, errors and recovery")


if __name__ == "__main__":
    main()
