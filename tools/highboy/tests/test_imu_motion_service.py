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

"""Run the real IMU worker against threaded RTOS/sensor/power fakes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SERVICE = ROOT / "firmware_p4/components/Service/imu"
DRIVER = ROOT / "firmware_p4/components/Drivers/qmi8658a"
NOTICE = (SERVICE / "imu_motion.c").read_text().split('#include', 1)[0]
code = r"""
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "imu_motion.h"
#include "qmi8658a.h"
static atomic_int start_fail, read_fail, stop_fail, release_fail;
static atomic_int starts, stops, holds, samples;
static atomic_bool create_fail, power_fail, sensor_active, no_data;
static pthread_t thread;
static bool joinable;
static void (*task_fn)(void *);
static void *entry(void *unused) {(void)unused;task_fn(NULL);return NULL;}
static int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,int stack,void *arg,int priority,void *handle,int core) {
 (void)name;(void)arg;assert(stack>=4096&&priority==4&&handle==NULL&&core==1);
 if(create_fail)return 0;
 if(joinable){pthread_join(thread,NULL);joinable=false;}
 task_fn=fn;assert(!pthread_create(&thread,NULL,entry,NULL));joinable=true;return 1;
}
static void vTaskDelay(unsigned ms){usleep(ms*100);}
static void vTaskDelete(void *task){assert(!task);pthread_exit(NULL);}
static int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return ((int64_t)t.tv_sec*1000000+t.tv_nsec/1000)*10;}
static bool consume(atomic_int *n){int value=atomic_load(n);while(value>0){if(atomic_compare_exchange_weak(n,&value,value-1))return true;}return false;}
static int power_manager_no_sleep_acquire(void){if(power_fail)return ESP_ERR_INVALID_STATE;assert(atomic_fetch_add(&holds,1)==0);return 0;}
static int power_manager_no_sleep_release(void){assert(holds==1);if(consume(&release_fail))return ESP_ERR_TIMEOUT;assert(!sensor_active);holds--;return 0;}
esp_err_t qmi8658a_start(void){assert(holds==1);starts++;sensor_active=true;return consume(&start_fail)?ESP_ERR_TIMEOUT:ESP_OK;}
esp_err_t qmi8658a_set_odr(uint8_t odr){assert(holds==1&&odr==6);return ESP_OK;}
esp_err_t qmi8658a_read(qmi8658a_sample_t *out){assert(holds==1&&sensor_active);if(no_data)return ESP_ERR_NOT_FINISHED;if(consume(&read_fail))return ESP_ERR_INVALID_STATE;*out=(qmi8658a_sample_t){.accel_g={0,0,-1}};samples++;return ESP_OK;}
esp_err_t qmi8658a_stop(void){assert(holds==1);stops++;if(consume(&stop_fail))return ESP_ERR_TIMEOUT;sensor_active=false;return ESP_OK;}
#include "imu_motion.c"
static imu_motion_snapshot_t snapshot(void){imu_motion_snapshot_t v;imu_motion_snapshot(&v);return v;}
static void wait_stopped(void){for(int i=0;i<10000&&snapshot().running;i++)usleep(1000);assert(!snapshot().running);if(joinable){pthread_join(thread,NULL);joinable=false;}}
static void wait_samples(int count){for(int i=0;i<2000&&samples<count;i++)usleep(1000);assert(samples>=count);}
static void reset(void){assert(holds==0&&!sensor_active);if(joinable){pthread_join(thread,NULL);joinable=false;}start_fail=read_fail=stop_fail=release_fail=starts=stops=samples=0;create_fail=power_fail=no_data=false;}
int main(void){
 reset();create_fail=true;assert(imu_motion_start()==ESP_ERR_NO_MEM);assert(!snapshot().running);assert(imu_motion_stop()==0);
 reset();power_fail=true;assert(imu_motion_start()==0);wait_stopped();assert(!starts&&!holds&&snapshot().error==ESP_ERR_INVALID_STATE);assert(imu_motion_stop()==0);
 reset();start_fail=10;assert(imu_motion_start()==0);wait_stopped();assert(starts==START_ATTEMPTS&&!holds&&!sensor_active);assert(snapshot().error==ESP_ERR_TIMEOUT);assert(imu_motion_stop()==ESP_OK);assert(imu_motion_stop()==ESP_OK);
 reset();start_fail=1;assert(imu_motion_start()==0);wait_samples(4);assert(starts==2&&holds==1);assert(imu_motion_start()==ESP_ERR_INVALID_STATE);
 assert(imu_motion_stop()==0);wait_stopped();assert(!holds&&!sensor_active);
 reset();assert(imu_motion_start()==0);wait_samples(5);int before=starts;read_fail=READ_ERROR_LIMIT;int previous=samples;wait_samples(previous+5);assert(starts==before+1&&holds==1&&snapshot().error==ESP_OK);
 assert(imu_motion_stop()==0);wait_stopped();
 reset();assert(imu_motion_start()==0);wait_samples(3);stop_fail=100;assert(imu_motion_stop()==ESP_ERR_TIMEOUT);assert(holds==1&&sensor_active&&snapshot().running);assert(imu_motion_start()==ESP_ERR_INVALID_STATE);stop_fail=0;wait_stopped();assert(imu_motion_stop()==0&&!holds);
 reset();assert(imu_motion_start()==0);wait_samples(3);release_fail=100;assert(imu_motion_stop()==ESP_ERR_TIMEOUT);assert(holds==1&&!sensor_active&&snapshot().running);release_fail=0;wait_stopped();assert(imu_motion_stop()==0&&!holds);
 reset();start_fail=2;assert(imu_motion_start()==0);assert(imu_motion_stop()==0);wait_stopped();assert(!holds&&!sensor_active);
 reset();read_fail=1000;assert(imu_motion_start()==0);wait_stopped();assert(starts==RECOVERY_LIMIT+1&&!holds&&!sensor_active);assert(imu_motion_stop()==0);
 reset();no_data=true;assert(imu_motion_start()==0);wait_stopped();assert(starts==RECOVERY_LIMIT+1&&!holds&&snapshot().error==ESP_ERR_TIMEOUT);assert(imu_motion_stop()==0);
 puts("PASS: actual IMU worker retries startup/read errors, idempotent Stop, restart, cancellation and retained sensor/power cleanup ownership");
}
"""
with tempfile.TemporaryDirectory() as temporary:
    work = Path(temporary)
    stubs = {
        'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_TIMEOUT 0x107\n#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_NOT_FINISHED 0x10c\nstatic inline const char *esp_err_to_name(int n){(void)n;return "fake";}\n',
        'sys_prio.h': '#include <pthread.h>\ntypedef pthread_mutex_t portMUX_TYPE;\n#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER\n#define portENTER_CRITICAL(m) pthread_mutex_lock(m)\n#define portEXIT_CRITICAL(m) pthread_mutex_unlock(m)\n#define pdPASS 1\n#define pdMS_TO_TICKS(ms) (ms)\n#define SYS_PRIO_SERVICE_LO 4\n#define SYS_CORE_UI 1\n',
        'power_manager.h': '', 'esp_timer.h': '',
        'esp_log.h': '#define ESP_LOGW(tag,...) ((void)(tag))\n#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n',
    }
    for name, text in stubs.items():
        (work / name).write_text(NOTICE + text)
    (work / 'test.c').write_text(NOTICE + code)
    command = ['cc', '-std=gnu11', '-g', '-pthread', '-fsanitize=address,undefined',
               '-fno-sanitize-recover=all', '-I' + str(work), '-I' + str(SERVICE),
               '-I' + str(SERVICE / 'include'), '-I' + str(DRIVER / 'include'),
               str(work / 'test.c'), str(SERVICE / 'imu_orientation.c'), '-lm', '-o', str(work / 'test')]
    subprocess.run(command, check=True, timeout=45)
    subprocess.run([str(work / 'test')], check=True, timeout=20)
