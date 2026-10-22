#!/usr/bin/env python3
"""Compile the production LoRa ABI with explicit IRQ/mesh/HAL fixtures."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / 'firmware_p4/components/Service'
DRIVER = ROOT / 'firmware_p4/components/Drivers/sx1262/include'


def main():
    with tempfile.TemporaryDirectory(prefix='tos-lora-session-') as tmp:
        directory = Path(tmp)
        (directory / 'freertos').mkdir()
        headers = {
            'esp_err.h': '''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
''',
            'esp_log.h': '#define ESP_LOGE(tag, ...) ((void)(tag))\n',
            'freertos/FreeRTOS.h': '''#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
typedef pthread_mutex_t portMUX_TYPE;
typedef uint32_t TickType_t;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(m) pthread_mutex_lock(m)
#define portEXIT_CRITICAL(m) pthread_mutex_unlock(m)
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
''',
            'freertos/task.h': '''#pragma once
#include "FreeRTOS.h"
typedef struct test_task *TaskHandle_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *,unsigned);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,uint32_t);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t);
''',
            'sys_prio.h': '#define SYS_PRIO_SERVICE_LO 4\n#define SYS_CORE_RADIO 0\n',
            'power_manager.h': '#include "esp_err.h"\nesp_err_t power_manager_no_sleep_acquire(void);\nesp_err_t power_manager_no_sleep_release(void);\n',
            'lora_session.h': '''#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum { LORA_PROTO_NONE, LORA_PROTO_MESHTASTIC, LORA_PROTO_MESHCORE } lora_proto_t;
esp_err_t lora_session_radio_reserve(void *);
esp_err_t lora_session_radio_release(void *);
esp_err_t lora_session_start_owned(lora_proto_t,void *,bool *);
esp_err_t lora_session_stop_owned(lora_proto_t,void *);
''',
        }
        for name, content in headers.items():
            (directory / name).write_text(content)
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(directory), '-I', str(SERVICE / 'app_runtime/include'),
                   '-I', str(SERVICE / 'app_runtime'), '-I', str(SERVICE / 'host_link/include'),
                   '-I', str(SERVICE / 'spi_bridge/include'), '-I', str(DRIVER),
                   str(SERVICE / 'app_runtime/tos_api_lora.c'),
                   str(ROOT / 'tools/lora_api/test_sessions.c'), '-o', str(directory / 'test')]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(directory / 'test')], check=True, timeout=30)


if __name__ == '__main__':
    main()
