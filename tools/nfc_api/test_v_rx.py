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

"""Run production IRQ capture with simulated edge timing and injected failures."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'firmware_p4/components/Drivers/st25r3916/hal'
with tempfile.TemporaryDirectory(prefix='nfc-v-rx-') as tmp:
    p=Path(tmp)
    for name in ['driver','freertos','hal','soc']:
        (p/name).mkdir()
    stubs={
      'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_TIMEOUT 3\n#define ESP_ERR_INVALID_SIZE 4\n#define ESP_ERR_INVALID_RESPONSE 5\nconst char *esp_err_to_name(int);\n',
      'driver/gpio.h': '#pragma once\n#include "esp_err.h"\ntypedef int gpio_num_t;\n#define GPIO_NUM_NC -1\n#define GPIO_IS_VALID_GPIO(p) ((p)>=0 && (p)<55)\n#define GPIO_INTR_ANYEDGE 3\nint gpio_install_isr_service(int);\nint gpio_set_intr_type(int,int);\nint gpio_isr_handler_add(int,void (*)(void *),void *);\nint gpio_isr_handler_remove(int);\nint gpio_intr_enable(int);\nint gpio_intr_disable(int);\n',
      'esp_attr.h':'#define DRAM_ATTR\n#define IRAM_ATTR\n',
      'esp_log.h':'#define ESP_LOGE(tag,...) ((void)(tag))\n',
      'esp_rom_sys.h':'#include <stdint.h>\nvoid esp_rom_delay_us(uint32_t);\n',
      'esp_timer.h':'#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
      'freertos/FreeRTOS.h':'#pragma once\ntypedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(p) ((void)(p))\n#define portEXIT_CRITICAL(p) ((void)(p))\n#define portENTER_CRITICAL_ISR(p) ((void)(p))\n#define portEXIT_CRITICAL_ISR(p) ((void)(p))\n',
      'freertos/task.h':'',
      'hal/gpio_ll.h':'int gpio_ll_get_level(void *, int);\n',
      'soc/gpio_struct.h':'extern int GPIO;\n',
    }
    for name,text in stubs.items(): (p/name).write_text(text)
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all', '-I'+str(p),
                    '-I'+str(BASE/'include'),str(BASE/'hb_nfc_v_rx.c'),str(BASE/'hb_nfc_v_codec.c'),
                    str(Path(__file__).with_suffix('.c')),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
