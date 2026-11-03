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

"""Compile the production AW8623 driver and haptic service with fault injection."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NOTICE = (ROOT / "firmware_p4/components/Drivers/aw8623/aw8623.c").read_text().split("#include")[0]
MOCKS = {
    "esp_err.h": """#pragma once
#include <stddef.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_NOT_SUPPORTED, ESP_ERR_TIMEOUT, ESP_ERR_NO_MEM, ESP_ERR_NOT_FINISHED };
const char *esp_err_to_name(esp_err_t error);
""",
    "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "esp_log.h": "#define ESP_LOGE(tag, ...) ((void)(tag))\n#define ESP_LOGW(tag, ...) ((void)(tag))\n#define ESP_LOGI(tag, ...) ((void)(tag))\n",
    "freertos/FreeRTOS.h": "#define pdMS_TO_TICKS(ms) (ms)\nint xPortInIsrContext(void);\n",
    "freertos/task.h": "void vTaskDelay(int ticks);\n",
    "i2c_init.h": "#include \"driver/i2c_master.h\"\n#define I2C_MASTER_FREQ_HZ 400000\ni2c_master_bus_handle_t i2c_get_bus(void);\n",
    "driver/i2c_master.h": """#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef void *i2c_master_dev_handle_t;
typedef void *i2c_master_bus_handle_t;
#define I2C_ADDR_BIT_LEN_7 0
typedef struct { int dev_addr_length, device_address, scl_speed_hz; } i2c_device_config_t;
esp_err_t i2c_master_bus_add_device(void *, const i2c_device_config_t *, void **);
esp_err_t i2c_master_bus_rm_device(void *);
esp_err_t i2c_master_transmit(void *, const void *, size_t, int);
esp_err_t i2c_master_transmit_receive(void *, const void *, size_t, void *, size_t, int);
""",
}

def main():
    with tempfile.TemporaryDirectory(prefix="aw8623-") as directory:
        tmp = Path(directory)
        for name, body in MOCKS.items():
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(NOTICE + body)
        driver = ROOT / "firmware_p4/components/Drivers"
        binary = tmp / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I", str(tmp), "-I", str(driver / "aw8623"),
                        "-I", str(driver / "aw8623/include"), "-I", str(driver / "pins/include"),
                        str(Path(__file__).with_suffix(".c")), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        service_mocks = {
            "freertos/FreeRTOS.h": """#pragma once
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
int xPortInIsrContext(void);
""",
            "freertos/task.h": """void vTaskDelay(int ticks);
void vTaskDelete(void *task);
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *, int, void *, int, void *, int);
""",
            "sys_prio.h": "#define SYS_PRIO_SERVICE_LO 2\n#define SYS_CORE_RADIO 0\n",
            "board_service.h": '#include "board_control.h"\nconst board_control_ops_t *board_service_controls(void);\n',
        }
        for name, body in service_mocks.items():
            (tmp / name).write_text(NOTICE + body)
        service = ROOT / "firmware_p4/components/Service/haptic"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I", str(tmp),
                        "-I", str(driver / "aw8623/include"), "-I", str(driver / "pins/include"),
                        "-I", str(service), "-I", str(service / "include"),
                        str(Path(__file__).with_name("test_haptic.c")), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
