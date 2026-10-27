#!/usr/bin/env python3
"""Exercise production NFC app and listener lifecycles with real host threads."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / 'firmware_p4/components/Service'
NFC = ROOT / 'firmware_p4/components/Applications/nfc'
DRIVERS = ROOT / 'firmware_p4/components/Drivers'


def main():
    with tempfile.TemporaryDirectory(prefix='tos-nfc-session-') as tmp:
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
            'esp_log.h': '''#pragma once
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
#define EXT_RAM_BSS_ATTR
''',
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
void taskYIELD(void);
''',
            'sys_prio.h': '#define SYS_PRIO_SERVICE_HI 5\n#define SYS_CORE_RADIO 0\n',
            'highboy_nfc.h': '''#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef struct { int unused; } highboy_nfc_config_t;
#define HIGHBOY_NFC_CONFIG_DEFAULT() {0}
esp_err_t highboy_nfc_init(const highboy_nfc_config_t *);
esp_err_t highboy_nfc_field_off(void);
esp_err_t highboy_nfc_ping(void *);
void highboy_nfc_deinit(void);
bool highboy_nfc_is_initialized(void);
''',
            'mf_classic_emu.h': '''#pragma once
#include <stdint.h>
typedef int hb_nfc_err_t;
#define HB_NFC_OK 0
#define HB_NFC_ERR_INTERNAL -1
#define HB_NFC_ERR_PARAM -2
#define MF_CLASSIC_1K 1
#define MFC_EMU_STATE_ERROR 9
#define HB_PROTO_MF_CLASSIC 7
typedef struct { uint8_t uid[10],uid_len,atqa[2],sak; } nfc_iso14443a_data_t;
typedef struct { int protocol; nfc_iso14443a_data_t iso14443a; } hb_nfc_card_data_t;
typedef struct { uint8_t uid[10]; unsigned char data[4096]; } mfc_emu_card_data_t;
void mfc_emu_card_data_init(mfc_emu_card_data_t *,const nfc_iso14443a_data_t *,int);
hb_nfc_err_t mfc_emu_init(const mfc_emu_card_data_t *);
hb_nfc_err_t mfc_emu_configure_target(void);
hb_nfc_err_t mfc_emu_start(void);
int mfc_emu_run_step(void);
void mfc_emu_stop(void);
''',
            'mf_classic.h': '#pragma once\nstatic inline int mf_classic_get_type(unsigned sak) { (void)sak; return 1; }\n',
            'nfc_device.h': '''#pragma once
#include "mf_classic_emu.h"
static inline int nfc_device_get_active(void) { return -1; }
static inline int nfc_device_load(int i,mfc_emu_card_data_t *c) { (void)i;(void)c;return -2; }
''',
            'power_manager.h': '''#pragma once
#include "esp_err.h"
esp_err_t power_manager_no_sleep_acquire(void);
esp_err_t power_manager_no_sleep_release(void);
''',
        }
        for name in ('highboy_nfc_error.h', 'highboy_nfc_compat.h', 'highboy_nfc_types.h'):
            headers[name] = '#pragma once\n#include "mf_classic_emu.h"\n'
        for name, content in headers.items():
            (directory / name).write_text(content)
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(directory), '-I', str(SERVICE / 'app_runtime/include'),
                   '-I', str(SERVICE / 'app_runtime'), '-I', str(SERVICE / 'host_link/include'),
                   '-I', str(SERVICE / 'spi_bridge/include'), '-I', str(NFC / 'include'),
                   '-I', str(DRIVERS / 'rf_frontend/include'),
                   str(SERVICE / 'app_runtime/tos_api_nfc_session.c'), str(NFC / 'nfc_listener.c'),
                   str(DRIVERS / 'rf_frontend/rf_frontend.c'),
                   str(ROOT / 'tools/nfc_api/test_sessions.c'), '-o', str(directory / 'test')]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(directory / 'test')], check=True, timeout=40)


if __name__ == '__main__':
    main()
