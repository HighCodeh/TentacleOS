#!/usr/bin/env python3
"""Exercise production RF commands/arbitration with explicit hardware and PM fakes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
COMPONENTS = ROOT / 'firmware_p4/components'


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    with tempfile.TemporaryDirectory(prefix='highboy-host-rf-') as tmp:
        directory = Path(tmp)
        (directory / 'freertos').mkdir()
        (directory / 'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_SUPPORTED 0x106
''')
        (directory / 'esp_rom_crc.h').write_text('''#include <stdint.h>
#include <stddef.h>
static inline uint16_t esp_rom_crc16_le(uint16_t crc,const uint8_t *data,uint32_t length) { (void)data; (void)length; return crc; }
''')
        (directory / 'esp_log.h').write_text('''#pragma once
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
''')
        (directory / 'freertos/FreeRTOS.h').write_text('''#pragma once
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(mutex) pthread_mutex_lock(mutex)
#define portEXIT_CRITICAL(mutex) pthread_mutex_unlock(mutex)
''')
        (directory / 'freertos/task.h').write_text('void *xTaskGetCurrentTaskHandle(void);\n')
        (directory / 'highboy_nfc.h').write_text('''#pragma once
#include "esp_err.h"
#include "highboy_nfc_types.h"
typedef struct { int pin; } highboy_nfc_config_t;
#define HIGHBOY_NFC_CONFIG_DEFAULT() ((highboy_nfc_config_t){0})
esp_err_t highboy_nfc_init(const highboy_nfc_config_t *config);
esp_err_t highboy_nfc_field_off(void);
''')
        (directory / 'power_manager.h').write_text('''#include "esp_err.h"
esp_err_t power_manager_no_sleep_acquire(void);
esp_err_t power_manager_no_sleep_release(void);
''')
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(directory)]
        for relative in ('Drivers/rf_frontend', 'Drivers/lf_rfid', 'Drivers/st25r3916',
                         'Service/spi_bridge', 'Service/host_link', 'Applications/nfc', 'Applications/nfc/protocols/mifare'):
            command += ['-I', str(COMPONENTS / relative / 'include')]
        executable = directory / 'test'
        command += [str(COMPONENTS / 'Drivers/rf_frontend/rf_frontend.c'),
                    str(COMPONENTS / 'Service/host_link/host_link_rf.c'),
                    str(ROOT / 'tools/host_link/tests/test_rf.c'), '-pthread', '-o', str(executable)]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
        core = (COMPONENTS / 'Service/host_link/host_link.c').read_text()
        routing = (ROOT / 'tools/host_link/tests/test_rf_routing.c').read_text()
        (directory / 'routing.c').write_text(routing.replace('/* PROCESS_FRAME */', function(core, 'static void process_frame(const uint8_t *frame, size_t total) {')))
        sources = [str(COMPONENTS / 'Drivers/rf_frontend/rf_frontend.c'),
                   str(COMPONENTS / 'Service/host_link/host_link_rf.c'),
                   str(ROOT / 'tools/host_link/tests/test_rf.c')]
        routing_command = [part for part in command if part not in sources]
        routing_command.insert(routing_command.index('-pthread'), str(directory / 'routing.c'))
        subprocess.run(routing_command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
        manager = (COMPONENTS / 'Applications/nfc/nfc_manager.c').read_text()
        signatures = ['static esp_err_t discovery_error(',
                      'static esp_err_t nfc_manager_probe_a(',
                      'static esp_err_t nfc_manager_probe_b(',
                      'static esp_err_t nfc_manager_probe_f(',
                      'static esp_err_t nfc_manager_probe_v(',
                      'static esp_err_t nfc_manager_probe(',
                      'esp_err_t nfc_manager_poll(']
        probe = (ROOT / 'tools/host_link/tests/test_nfc_poll.c').read_text()
        (directory / 'probe.c').write_text(probe.replace('/* NFC_POLL */',
            '\n'.join(function(manager, signature) for signature in signatures)))
        probe_command = [str(directory / 'probe.c') if part == str(directory / 'routing.c') else part
                         for part in routing_command]
        subprocess.run(probe_command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)




if __name__ == '__main__':
    main()
