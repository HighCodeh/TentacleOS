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

"""Exercise production app context bindings, including concurrent first use."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CONTEXT = ROOT / 'firmware_p4/components/Service/app_runtime'


def main():
    with tempfile.TemporaryDirectory(prefix='highboy-script-context-') as tmp:
        directory = Path(tmp)
        (directory / 'freertos').mkdir()
        (directory / 'esp_err.h').write_text('''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NO_MEM 0x101
''')
        (directory / 'esp_log.h').write_text('#define ESP_LOGI(tag, ...) ((void)(tag))\n')
        (directory / 'esp_console.h').write_text('static inline int esp_console_cmd_deregister(const char *name) { (void)name; return 0; }\n')
        (directory / 'host_link_dispatch.h').write_text('static inline int host_link_unregister(unsigned char category, unsigned char op) { (void)category; (void)op; return 0; }\n')
        (directory / 'freertos/FreeRTOS.h').write_text('''#pragma once
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(mutex) pthread_mutex_lock(mutex)
#define portEXIT_CRITICAL(mutex) pthread_mutex_unlock(mutex)
''')
        (directory / 'freertos/task.h').write_text('extern _Thread_local void *g_test_task;\nstatic inline void *xTaskGetCurrentTaskHandle(void) { return g_test_task; }\n')
        (directory / 'test.c').write_text('''#include "tos_app_ctx.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
_Thread_local void *g_test_task;
static atomic_uint s_ready;
static atomic_uint s_cancels;
static atomic_uint s_nfc_cancels,s_lora_cancels;
void tos_api_subghz_cancel_owner(void *task) { assert(task!=NULL);atomic_fetch_add(&s_cancels,1); }
void tos_api_nfc_session_cancel_owner(void *task) { assert(task!=NULL);atomic_fetch_add(&s_nfc_cancels,1); }
void tos_api_lora_cancel_owner(void *task) { assert(task!=NULL);atomic_fetch_add(&s_lora_cancels,1); }
static atomic_bool s_exit;
static atomic_bool s_lost;
static void mark_lost(void *ctx, bool is_lost) { assert(ctx==(void *)1); atomic_store(&s_lost,is_lost); }
static void *worker(void *task) {
  g_test_task=task;
  tos_app_ctx_t context={.granted_caps=(uint32_t)(uintptr_t)task,.user=(void *)1,.set_resource_lost=mark_lost};
  assert(tos_app_ctx_current()==NULL);
  assert(tos_app_ctx_bind(task,&context)==ESP_OK);
  assert(tos_app_ctx_current()==&context);
  assert(tos_app_cap_check(context.granted_caps));
  assert(!tos_app_cap_check(256));
  atomic_fetch_add(&s_ready,1);
  while(!atomic_load(&s_exit)) {}
  tos_app_ctx_unbind(task);
  assert(tos_app_ctx_current()==NULL);
  return NULL;
}
int main(void) {
  pthread_t threads[8];
  for(uintptr_t i=0;i<8;++i) assert(pthread_create(&threads[i],NULL,worker,(void *)(i+1))==0);
  while(atomic_load(&s_ready)!=8) {}
  g_test_task=(void *)9;
  tos_app_ctx_t context={0};
  assert(tos_app_ctx_bind(g_test_task,&context)==ESP_ERR_NO_MEM);
  assert(tos_app_ctx_current()==NULL);
  tos_app_ctx_signal_resource_lost((void *)1);
  assert(atomic_load(&s_lost));
  atomic_store(&s_exit,true);
  for(unsigned i=0;i<8;++i) assert(pthread_join(threads[i],NULL)==0);
  context.user=(void *)1; context.set_resource_lost=mark_lost;
  assert(tos_app_ctx_bind(g_test_task,&context)==ESP_OK);
  assert(!tos_app_cap_check(1));
  tos_app_ctx_clear_resource_lost();
  assert(!atomic_load(&s_lost));
  tos_app_ctx_unbind(g_test_task);
  assert(tos_app_cap_check(1));
  assert(tos_app_ctx_bind(NULL,&context)==ESP_ERR_INVALID_ARG);
  assert(atomic_load(&s_cancels)==19 && atomic_load(&s_nfc_cancels)==19 && atomic_load(&s_lora_cancels)==19);
  return 0;
}
''')
        (directory / 'string_compat.h').write_text('''#include <string.h>
static inline size_t script_test_strlcpy(char *dst,const char *src,size_t size) {
  size_t length=strlen(src);
  if(size!=0) { size_t count=length<size-1 ? length : size-1; memcpy(dst,src,count); dst[count]=0; }
  return length;
}
#define strlcpy script_test_strlcpy
''')
        executable = directory / 'test'
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=gnu11', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-include', str(directory / 'string_compat.h'),
                        '-I', str(directory), '-I', str(CONTEXT / 'include'),
                        str(CONTEXT / 'tos_app_ctx.c'), str(directory / 'test.c'),
                        '-pthread', '-o', str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
    print('PASS: concurrent app/script context binding, table exhaustion, capabilities and revocation')


if __name__ == '__main__':
    main()
