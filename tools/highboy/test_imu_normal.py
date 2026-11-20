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
root=Path(__file__).resolve().parents[2]
s=(root/'firmware_p4/components/Drivers/qmi8658a/qmi8658a.c').read_text()
code=s[s.index('#define QMI_REG_WHO_AM_I'):]
fake=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
typedef int esp_err_t;
typedef void* spi_device_handle_t;
typedef struct {int length,flags;uint8_t tx_data[4],rx_data[4];} spi_transaction_t;
typedef struct {int cs_pin,clock_speed_hz,mode,queue_size;} spi_device_config_t;
typedef struct {float temperature_c,accel_g[3],gyro_dps[3];uint8_t status;} qmi8658a_sample_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_NOT_FINISHED 5
#define ESP_ERR_INVALID_ARG 6
#define CONFIG_HIGHBOY_BOARD_V203 1
#define SPI_TRANS_USE_TXDATA 1
#define SPI_TRANS_USE_RXDATA 2
#define SPI_DEVICE_IMU 0
#define SPI3_HOST 3
#define GPIO_QMI8658A_CS_PIN 36
#define SIG_GPIO_OUT_IDX 1
#define GPIO_MODE_OUTPUT 1
#define pdMS_TO_TICKS(x) (x)
static const char* TAG="test";
static void log_ignore(const char*t,const char*f,...){(void)t;(void)f;}
#define ESP_LOGI log_ignore
static uint8_t regs[128];static int calls,fail_at,locked,added,unlocks,writes,bad_write,lock_reads;
static bool no_sample,never_lock,lose_identity;static int64_t now;
static int cs_level=1;
static void esp_rom_gpio_connect_out_signal(int p,int s,bool a,bool b){assert(p==36&&s==1&&!a&&!b);}
static int gpio_set_level(int p,int v){assert(p==36);cs_level=v;return 0;}
static int gpio_set_direction(int p,int m){assert(p==36&&m==1);return 0;}
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(int ms){now+=ms*1000;}
static void esp_rom_delay_us(int us){now+=us;}
static bool spi_bus_lock_take(int ms){(void)ms;assert(!locked);locked=1;return true;}
static void spi_bus_lock_give(void){assert(locked&&cs_level==1);locked=0;}
static int spi_add_device(int h,int i,const spi_device_config_t*c){assert(locked&&h==3&&i==0&&c->mode==0&&c->cs_pin==-1);added=1;return 0;}
static void*spi_get_handle(int i){assert(i==0);return added?(void*)1:NULL;}
static int spi_remove_device(int i){assert(locked&&i==0);added=0;return 0;}
static int spi_device_transmit(void*d,spi_transaction_t*t){
assert(locked&&added&&d==(void*)1&&t->length==16&&cs_level==0);
if(++calls==fail_at)return ESP_ERR_TIMEOUT;
int r=t->tx_data[0]&127;
if(t->tx_data[0]&128){
 if(r==0x2d)t->rx_data[1]=no_sample?0:((++lock_reads>1&&!never_lock)?3:1);
 else t->rx_data[1]=regs[r];
 if(r==0x40){unlocks++;lock_reads=0;}
}else{
 assert(r==3||r==4||r==6||r==8);writes++;regs[r]=t->tx_data[1];
 if(bad_write){regs[r]^=16;bad_write=0;}
 if(lose_identity)regs[0]=0x3e;
}
return 0;}
'''
tests=r'''
static void reset(void){
memset(regs,0,sizeof(regs));regs[0]=5;regs[1]=0x7c;regs[2]=0x20;
regs[0x34]=25;regs[0x38]=16;regs[0x3b]=64;
calls=fail_at=locked=added=unlocks=writes=bad_write=lock_reads=0;
no_sample=never_lock=lose_identity=false;now=0;s_ready=false;s_spi=NULL;s_odr=6;
}
int main(void){
reset();assert(qmi8658a_start()==0&&regs[8]==0x83);int init_calls=calls;
qmi8658a_sample_t sample,sentinel;memset(&sentinel,0xa5,sizeof sentinel);sample=sentinel;
assert(qmi8658a_read(&sample)==ESP_ERR_NOT_FINISHED&&!memcmp(&sample,&sentinel,sizeof sample));
now+=QMI_SETTLE_US;assert(qmi8658a_read(&sample)==0&&sample.status==3);
assert(sample.temperature_c==25&&sample.accel_g[1]==1&&sample.gyro_dps[0]==1&&!locked);
int read_calls=calls-init_calls;assert(qmi8658a_stop()==0&&!added&&!s_ready&&regs[8]==0);
for(int i=1;i<=init_calls;++i){reset();fail_at=i;assert(qmi8658a_start()!=0&&!s_ready&&!locked);assert(!added);}
for(int i=1;i<=read_calls;++i){reset();assert(qmi8658a_start()==0);now+=QMI_SETTLE_US;fail_at=calls+i;sample=sentinel;
assert(qmi8658a_read(&sample)!=0&&!memcmp(&sample,&sentinel,sizeof sample)&&!locked);assert(unlocks>=2);}
reset();assert(qmi8658a_start()==0);now+=QMI_SETTLE_US;no_sample=true;sample=sentinel;
assert(qmi8658a_read(&sample)==ESP_ERR_NOT_FINISHED&&!memcmp(&sample,&sentinel,sizeof sample));
no_sample=false;never_lock=true;assert(qmi8658a_read(&sample)==ESP_ERR_TIMEOUT&&unlocks>=2&&!locked);
reset();bad_write=1;assert(qmi8658a_start()==ESP_ERR_INVALID_RESPONSE&&!added&&!s_ready);
reset();lose_identity=true;assert(qmi8658a_start()==ESP_ERR_INVALID_RESPONSE&&writes==1&&!added);
reset();regs[2]=0x40;assert(qmi8658a_start()==ESP_ERR_INVALID_RESPONSE&&!writes&&!added);
reset();assert(qmi8658a_start()==0);for(int odr=5;odr<=8;odr++){assert(qmi8658a_set_odr(odr)==0);assert(regs[3]==(0x20|odr)&&regs[4]==(0x50|odr)&&regs[8]==0x83);}
assert(qmi8658a_set_odr(9)==ESP_ERR_INVALID_ARG);assert(qmi8658a_stop()==0);
reset();assert(qmi8658a_start()==0);fail_at=calls+1;assert(qmi8658a_stop()!=0&&added);assert(qmi8658a_stop()==0&&!added);
return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'imu')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-x','c','-','-o',exe],input=fake+code+tests,text=True,check=True)
 subprocess.run([exe],check=True)
print('PASS: production IMU path; locked coherent samples, startup/no-data gates, all transaction faults, identity loss, ODR changes, stop failure/retry, unchanged caller output on errors.')
