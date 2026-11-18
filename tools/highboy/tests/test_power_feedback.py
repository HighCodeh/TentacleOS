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

"""Host execution of production battery and LED services with fault injection."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[3]
NOTICE=(ROOT/'firmware_p4/components/Drivers/aw8623/aw8623.c').read_text().split('#include')[0]
MOCKS={
'esp_err.h': '#pragma once\n#include <stddef.h>\ntypedef int esp_err_t;\nenum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_TIMEOUT,ESP_ERR_NO_MEM};\nconst char *esp_err_to_name(esp_err_t);\n',
'esp_log.h':'#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGW(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n',
'esp_timer.h':'#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
'pin_def.h':'',
'sys_prio.h':'#define SYS_CORE_RADIO 0\n#define SYS_PRIO_BACKGROUND 3\n',
'freertos/FreeRTOS.h': '#pragma once\n#define pdMS_TO_TICKS(x) (x)\n#define pdTRUE 1\n#define pdPASS 1\n#define portMAX_DELAY 0xffff\ntypedef int portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED 0\n#define portENTER_CRITICAL(p) ((void)p)\n#define portEXIT_CRITICAL(p) ((void)p)\nint xPortInIsrContext(void);\n',
'freertos/semphr.h':'#pragma once\ntypedef void *SemaphoreHandle_t;\nstatic inline void *xSemaphoreCreateMutex(void){return (void*)1;}\nstatic inline int xSemaphoreTake(void *m,int t){(void)m;(void)t;return 1;}\nstatic inline void xSemaphoreGive(void *m){(void)m;}\nstatic inline void vSemaphoreDelete(void *m){(void)m;}\n',
'freertos/task.h': '#pragma once\ntypedef void *TaskHandle_t;\nvoid vTaskDelay(int);\nvoid vTaskDelete(void*);\nunsigned ulTaskNotifyTake(int,unsigned);\nvoid xTaskNotifyGive(void*);\nint xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,void*,int);\n',
'i2c_init.h':'#pragma once\n#include "esp_err.h"\n#include <stdint.h>\ntypedef void *i2c_master_dev_handle_t;\ntypedef void *i2c_master_bus_handle_t;\ntypedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;\n#define I2C_ADDR_BIT_LEN_7 0\n#define I2C_MASTER_FREQ_HZ 100000\ni2c_master_bus_handle_t i2c_get_bus(void);\nesp_err_t i2c_master_bus_add_device(void*,const i2c_device_config_t*,void**);\nesp_err_t i2c_master_transmit(void*,const void*,size_t,int);\nesp_err_t i2c_master_transmit_receive(void*,const void*,size_t,void*,size_t,int);\n'
}
with tempfile.TemporaryDirectory() as tmpdir:
 tmp=Path(tmpdir)
 for name,body in MOCKS.items():
  p=tmp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(NOTICE+body)
 inc=[tmp,ROOT/'firmware_p4/components/Drivers/bq25896',ROOT/'firmware_p4/components/Drivers/bq25896/include',ROOT/'firmware_p4/components/Drivers/led',ROOT/'firmware_p4/components/Drivers/led/include']
 for name in ['test_pack_service','test_led_feedback']:
  command=['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-DCONFIG_HIGHBOY_BOARD_V203=1']
  for p in inc:command+=['-I',str(p)]
  command += [str(Path(__file__).with_name(name+'.c')),'-o',str(tmp/'test')]
  subprocess.run(command,check=True);subprocess.run([str(tmp/'test')],check=True)
