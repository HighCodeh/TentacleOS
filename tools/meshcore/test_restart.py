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


from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def function(source, signature):
 start=source.index(signature);opening=source.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
def main():
 source=(ROOT/'firmware_p4/components/Applications/LoRa/meshcore/meshcore_phoneapi.c').read_text()
 native=function(source,'esp_err_t meshcore_phoneapi_init(void) {')
 native+='\n'+function(source,'void meshcore_phoneapi_on_disconnect(void) {')
 fixture=r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_LOGI(...) ((void)0)
#define portMAX_DELAY 0
static bool s_is_initialized,fail_alloc;
static uint32_t s_ble_pin,s_app_target_ver,s_offline_queue_len;
static void *s_outbound_cb,*s_outbound_ctx,*s_queue_mutex;
static unsigned allocations,loads,locks;
static uint32_t load_or_generate_ble_pin(void){++loads;return 123456;}
static void autoadd_load(void){}
static void *xSemaphoreCreateMutex(void){++allocations;return fail_alloc?NULL:(void *)1;}
static void xSemaphoreTake(void *p,int t){(void)t;assert(p);++locks;}
static void xSemaphoreGive(void *p){assert(p);--locks;}
/* NATIVE */
int main(void){
 fail_alloc=true;assert(meshcore_phoneapi_init()==ESP_ERR_NO_MEM && !s_is_initialized);
 fail_alloc=false;assert(meshcore_phoneapi_init()==ESP_OK && s_is_initialized);
 unsigned old_alloc=allocations,old_load=loads;
 s_outbound_cb=(void *)2;s_outbound_ctx=(void *)3;s_app_target_ver=4;s_offline_queue_len=5;
 assert(meshcore_phoneapi_init()==ESP_OK);
 assert(allocations==old_alloc && loads==old_load && s_outbound_cb==(void *)2 && s_offline_queue_len==5);
 meshcore_phoneapi_on_disconnect();assert(s_app_target_ver==0 && s_offline_queue_len==0 && locks==0);
 for(unsigned i=0;i<100;i++)assert(meshcore_phoneapi_init()==ESP_OK);
 assert(allocations==old_alloc && loads==old_load);
 puts("PASS actual MeshCore PhoneAPI allocation failure, idempotent init, disconnect reset and 100 restarts (NVS/RTOS fakes)");
}
'''
 with tempfile.TemporaryDirectory() as d:
  d=Path(d);(d/'test.c').write_text(fixture.replace('/* NATIVE */',native))
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(d/'test.c'),'-o',str(d/'test')],check=True)
  subprocess.run([str(d/'test')],check=True,timeout=10)
if __name__=='__main__':main()
