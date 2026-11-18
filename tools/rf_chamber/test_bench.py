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

"""Compile production RF parameter and CC1101 backend paths against fault-injected hardware."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
NOTICE = (ROOT / 'firmware_p4/components/Drivers/cc1101/cc1101_lease.c').read_text().split('#include')[0]
def main():
    with tempfile.TemporaryDirectory(prefix='rf-bench-test-') as tmp:
        p = Path(tmp)
        headers = {
          'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n' + '\n'.join('#define '+s+' '+str(i) for i,s in enumerate(['ESP_OK','ESP_ERR_INVALID_ARG','ESP_ERR_INVALID_STATE','ESP_ERR_TIMEOUT','ESP_ERR_NOT_FOUND','ESP_ERR_INVALID_RESPONSE','ESP_ERR_INVALID_SIZE','ESP_ERR_NO_MEM'])) + '\nconst char *esp_err_to_name(int);\n',
          'esp_log.h': '#pragma once\n#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n',
          'esp_timer.h': '#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
          'driver/gpio.h': '#pragma once\n#define GPIO_MODE_INPUT 1\n#define GPIO_MODE_OUTPUT 2\nint gpio_set_direction(int,int);\nint gpio_set_level(int,int);\nint gpio_get_level(int);\nint gpio_input_enable(int);\n',
          'pin_def.h': '#define GPIO_RF_SW_V1_PIN 17\n#define GPIO_RF_SW_V2_PIN 18\n#define GPIO_CC1101_GDO0_PIN 8\n',
          'freertos/FreeRTOS.h': '#pragma once\n#define pdMS_TO_TICKS(x) (x)\n',
          'freertos/task.h': '#pragma once\nvoid vTaskDelay(int);\n',
        }
        for name, value in headers.items():
            dest=p/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(NOTICE+value)
        cc=ROOT/'firmware_p4/components/Drivers/cc1101'
        svc=ROOT/'firmware_p4/components/Service/rf_chamber'
        command=[*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror',
                 '-fsanitize=undefined','-fno-sanitize-recover=all', '-I'+str(p),
                 '-I'+str(cc/'include'),'-I'+str(svc/'include'),str(cc/'cc1101_bench.c'),
                 str(svc/'rf_chamber_config.c'),str(Path(__file__).with_suffix('.c')),'-o',str(p/'test')]
        subprocess.run(command,check=True,timeout=60)
        subprocess.run([str(p/'test')],check=True,timeout=30)
if __name__ == '__main__':
    main()
