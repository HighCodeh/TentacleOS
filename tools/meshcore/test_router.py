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
import subprocess,tempfile
from test_restart import function
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'firmware_p4/components/Applications/LoRa/meshcore/meshcore_router.c').read_text()
native='\n'.join(function(source,s) for s in ('static void on_tx_done(void *ctx) {','static void on_timeout(void *ctx) {','esp_err_t mc_transmit_raw(const uint8_t *raw, uint16_t len) {','void mc_router_rx_process(void) {'))
fixture=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_TIMEOUT 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define MESHCORE_MAX_RAW 255
#define MC_CAD_MAX_RETRIES 1
#define MC_RELAY_DELAY_MIN_MS 0
#define MC_RELAY_DELAY_RANGE_MS 1
#define MC_TX_TIMEOUT_MS 5000
#define MC_TX_IRQ_MARGIN_MS 250
#define MC_RX_BATCH_MAX 8
#define pdMS_TO_TICKS(x) (x)
typedef struct {int unused;} meshcore_packet_view_t;
typedef struct {int id;} sx1262_packet_t;
static atomic_bool s_is_tx_done,s_is_tx_timed_out;
static unsigned now,done_at,timeout_at,rx_restarts,available,processed;
static bool running=true;
static int tx_error,rx_error;
static void on_tx_done(void *ctx);
static void on_timeout(void *ctx);
static bool meshcore_packet_parse(const uint8_t *p,unsigned n,meshcore_packet_view_t *v){(void)p;(void)n;(void)v;return false;}
static int meshcore_packet_hash(const meshcore_packet_view_t *v,uint8_t *h){(void)v;(void)h;return 0;}
static void mc_dedup_check_add(uint8_t *h){(void)h;}
static bool cad_check_clear(void){return true;}
static unsigned esp_random(void){return 0;}
static void vTaskDelay(unsigned ticks){now+=ticks;if(done_at && now>=done_at){done_at=0;on_tx_done(NULL);}if(timeout_at && now>=timeout_at){timeout_at=0;on_timeout(NULL);}}
static unsigned mc_now_ms(void){return now;}
static int sx1262_stop_rx(void){return 0;}
static int sx1262_transmit(const uint8_t *p,uint8_t n,unsigned ms){(void)p;(void)n;assert(ms==5000);return tx_error;}
static bool mc_is_running(void){return running;}
static int sx1262_receive_continuous(void){++rx_restarts;return rx_error;}
static int sx1262_get_packet(sx1262_packet_t *p){if(!available)return 1;p->id=available--;return 0;}
static void on_rx_done(const sx1262_packet_t *p,void *ctx){(void)ctx;assert(p->id>0);++processed;}
/* NATIVE */
static void reset(void){now=0;done_at=0;timeout_at=0;rx_restarts=0;tx_error=0;rx_error=0;}
int main(void){
 uint8_t payload[110]={0};
 reset();done_at=650;assert(mc_transmit_raw(payload,110)==ESP_OK && now==650 && rx_restarts==1);
 reset();done_at=2200;assert(mc_transmit_raw(payload,110)==ESP_OK && now==2200);
 reset();timeout_at=5000;assert(mc_transmit_raw(payload,110)==ESP_ERR_TIMEOUT && now==5000);
 reset();assert(mc_transmit_raw(payload,110)==ESP_ERR_TIMEOUT && now==5250);
 reset();tx_error=ESP_FAIL;assert(mc_transmit_raw(payload,110)==ESP_FAIL && now==0 && rx_restarts==1);
 reset();done_at=650;rx_error=ESP_FAIL;assert(mc_transmit_raw(payload,110)==ESP_FAIL);
 reset();assert(mc_transmit_raw(payload,256)==ESP_ERR_INVALID_SIZE && now==0);
 available=12;mc_router_rx_process();assert(processed==8 && available==4);
 running=false;mc_router_rx_process();assert(processed==8 && available==4);
 running=true;mc_router_rx_process();assert(processed==12 && available==0);
 puts("PASS actual MeshCore TX >500ms, hardware timeout, missing IRQ, failure recovery and bounded worker RX (radio/clock fakes)");
}
"""
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(fixture.replace('/* NATIVE */',native))
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(d/'test.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True,timeout=10)
