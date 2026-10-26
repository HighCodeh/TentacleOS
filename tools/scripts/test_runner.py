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

"""Run the production worker and real engines with file/task/clock device fakes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from test_engine import ROOT, ENGINE, JS, LUA, LUA_SOURCES


def main():
    with tempfile.TemporaryDirectory(prefix='highboy-script-runner-') as tmp:
        directory = Path(tmp)
        (directory / 'freertos').mkdir()
        (directory / 'esp_attr.h').write_text('#define EXT_RAM_BSS_ATTR\n')
        (directory / 'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
''')
        (directory / 'esp_log.h').write_text('''#pragma once
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
''')
        (directory / 'esp_heap_caps.h').write_text('''#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(n, caps) malloc(n)
''')
        (directory / 'resource_mgr.h').write_text('void resource_release_owner_task(void *task);\n')
        (directory / 'ui_manager.h').write_text('#define SCREEN_SCRIPTS 123\n')
        (directory / 'esp_timer.h').write_text('static int64_t esp_timer_get_time(void) { return s_now_us; }\n')
        (directory / 'freertos/FreeRTOS.h').write_text('''#pragma once
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(mutex) pthread_mutex_lock(mutex)
#define portEXIT_CRITICAL(mutex) pthread_mutex_unlock(mutex)
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
''')
        (directory / 'freertos/task.h').write_text('''#pragma once
static void *xTaskGetCurrentTaskHandle(void) { return (void *)1; }
static void test_tick(void);
static void vTaskDelay(unsigned ticks) { s_now_us += (int64_t)ticks * 1000000; test_tick(); }
static void vTaskDeleteWithCaps(void *task) { (void)task; ++s_finished_tasks; }
static int xTaskCreatePinnedToCoreWithCaps(void (*task)(void *), const char *name,
    unsigned stack, void *ctx, unsigned priority, void *handle, int core, unsigned caps) {
  (void)name; (void)ctx; (void)handle;
  assert(stack >= 16384 && priority == 2 && core == 0 && caps == 3);
  if (s_task_should_fail) return 0;
  assert(s_pending_task == NULL);
  s_pending_task = task;
  return pdPASS;
}
''')
        executable = directory / 'test'
        command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=gnu11', '-O1',
                   '-D_GNU_SOURCE', '-DLUAI_MAXCCALLS=32', '-include', 'stddef.h',
                   '-fno-math-errno', '-fno-trapping-math',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I', str(directory), '-I', str(ENGINE / 'include'),
                   '-I', str(JS), '-I', str(JS / 'generated'), '-I', str(LUA),
                   '-I', str(ROOT / 'firmware_p4/components/Applications/scripts/include'),
                   '-I', str(ROOT / 'firmware_p4/components/Applications/scripts'),
                   '-I', str(ROOT / 'firmware_p4/components/Drivers/sys_prio/include'),
                   str(ENGINE / 'script_engine.c'), str(ROOT / 'tools/scripts/tests/test_runner.c')]
        command += ['-I', str(ROOT / 'firmware_p4/components/Service/app_runtime/include'),
                    '-I', str(ROOT / 'tools/dependencies/build/managed_components/espressif__cjson/cJSON'),
                    str(ROOT / 'tools/dependencies/build/managed_components/espressif__cjson/cJSON/cJSON.c'),
                    str(ENGINE / 'script_bindings.c')]
        command += [str(JS / (name + '.c')) for name in ('mquickjs', 'dtoa', 'libm', 'cutils')]
        command += [str(LUA / (name + '.c')) for name in LUA_SOURCES]
        subprocess.run([*command, '-lm', '-pthread', '-o', str(executable)], check=True, timeout=120)
        subprocess.run([str(executable), str(directory)], check=True, timeout=30)


if __name__ == '__main__':
    main()
