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

"""Run the production NMEA parser with ASan/UBSan and known coordinates."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "firmware_p4/components/Service/console"
TEST = r"""
#include "gps_nmea.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int parse(const char *payload, gps_nmea_fix_t *fix) {
 char line[GPS_NMEA_LINE_SIZE]; unsigned sum=0;
 for(const char *p=payload;*p;p++) sum^=(unsigned char)*p;
 snprintf(line,sizeof(line),"$%s*%02X",payload,sum);
 return gps_nmea_parse(line,fix);
}
int main(void) {
 gps_nmea_fix_t fix;
 assert(parse("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", &fix)==1);
 assert(fabs(fix.latitude-48.1173)<0.000001);
 assert(fabs(fix.longitude-11.5166666667)<0.000001);
 assert(fix.satellites==8 && !strcmp(fix.altitude,"545.4"));
 assert(parse("GNGGA,123519,4807.038,S,01131.000,W,2,08,0.9,545.4,M,46.9,M,,", &fix)==1);
 assert(fix.latitude<0 && fix.longitude<0);
 assert(parse("GPGGA,123519,,,,,0,00,99.99,,,,,,", &fix)==0);
 assert(fix.latitude==0 && fix.longitude==0);
 assert(parse("GPGGA,123519,4867.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", &fix)==-1);
 assert(parse("GPGGA,123519,4807.038,X,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", &fix)==-1);
 assert(parse("GPGGA,123519,nan,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", &fix)==-1);
 assert(parse("GPGGA,123519,9100.0,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", &fix)==-1);
 assert(parse("GPGGA,123519,4807.038,N,01131.000,E,6,08,0.9,545.4,M,46.9,M,,", &fix)==0);
 assert(parse("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,101126,,,A", &fix)==-1);
 char bad[]="$GPGGA,123519*ZZ"; assert(gps_nmea_parse(bad,&fix)==-1);
 char wrong[]="$GPGGA,123519*00"; assert(gps_nmea_parse(wrong,&fix)==-1);
 char empty[]=""; assert(gps_nmea_parse(empty,&fix)==-1);
 char short_line[]="$*"; assert(gps_nmea_parse(short_line,&fix)==-1);
 assert(gps_nmea_parse(NULL,&fix)==-1);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="hb-gps-") as temp:
    test = Path(temp) / "test.c"
    notice = (SOURCE / "gps_nmea.c").read_text().split('#include', 1)[0]
    test.write_text(notice + TEST)
    binary = Path(temp) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(SOURCE / "include"),
                    str(SOURCE / "gps_nmea.c"), str(test), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: NMEA checksum, coordinates, hemispheres, no-fix, malformed and estimated fixes")

COMMAND_TEST = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define UART_NUM_1 1
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
#define GPIO_EXT_GPS_RX_PIN 12
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(tag, ...) do { (void)(tag); printf(__VA_ARGS__); } while(0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
#define ESP_ERROR_CHECK_WITHOUT_ABORT(err) assert((err)==ESP_OK)
typedef int esp_err_t;
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
typedef struct { const char *command,*help; int (*func)(int,char **); } esp_console_cmd_t;
static int64_t clock_us;
static const char *stream;
static bool installed;
static int installs,deletes,resets,fail_stage;
static const char *esp_err_to_name(int err) { return err ? "FAIL":"OK"; }
static int64_t esp_timer_get_time(void) { return clock_us; }
static bool uart_is_driver_installed(int port) { assert(port==1); return installed; }
static int uart_driver_install(int port,int rx,int tx,int q,void *queue,int flags) {
 (void)port; (void)rx; (void)tx; (void)q; (void)queue; (void)flags;
 if(fail_stage==1) return ESP_FAIL;
 installed=true; installs++; return ESP_OK;
}
static int uart_param_config(int port,const uart_config_t *cfg) {
 assert(port==1 && cfg->baud_rate==9600); return fail_stage==2 ? ESP_FAIL:ESP_OK;
}
static int uart_set_pin(int port,int tx,int rx,int rts,int cts) {
 assert(port==1 && tx==-1 && rx==12 && rts==-1 && cts==-1);
 return fail_stage==3 ? ESP_FAIL:ESP_OK;
}
static int uart_flush_input(int port) { (void)port; return ESP_OK; }
static int uart_read_bytes(int port,uint8_t *out,int size,int ticks) {
 assert(port==1 && size==1); clock_us+=ticks*1000;
 if(fail_stage==4) return -1;
 if(!stream || !*stream) return 0;
 *out=(uint8_t)*stream++; return 1;
}
static int uart_driver_delete(int port) { assert(port==1); installed=false; deletes++; return ESP_OK; }
static int gpio_reset_pin(int pin) { assert(pin==12); resets++; return ESP_OK; }
static int esp_console_cmd_register(const esp_console_cmd_t *cmd) { assert(!strcmp(cmd->command,"gps")); return ESP_OK; }
#include "cmd_gps.c"
int main(void) {
 char *geo[]={"gps","--geo"};
 stream="$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
 assert(cmd_gps(2,geo)==0 && !installed && installs==1 && deletes==1);
 stream=""; assert(cmd_gps(2,geo)==1 && !installed);
 installed=true; int old_deletes=deletes;
 assert(cmd_gps(2,geo)==1 && installed && deletes==old_deletes);
 installed=false;
 for(fail_stage=1;fail_stage<=4;fail_stage++) {
  assert(cmd_gps(2,geo)==1 && !installed && !atomic_load(&s_is_busy));
 }
 fail_stage=0;
 char *invalid[]={"gps","--baud","0"}; int old_installs=installs;
 assert(cmd_gps(3,invalid)==1 && installs==old_installs);
 assert(deletes==installs && resets==deletes);
 register_gps_commands();
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="hb-gps-uart-") as temp:
    work = Path(temp)
    notice = (SOURCE / "gps_nmea.c").read_text().split('#include', 1)[0]
    for name in ("console_service.h", "driver/gpio.h", "driver/uart.h", "esp_console.h", "esp_log.h", "esp_timer.h", "pin_def.h"):
        header = work / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(notice)
    (work / "test.c").write_text(notice + COMMAND_TEST)
    binary = work / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(work), "-I", str(SOURCE / "include"),
                    "-I", str(SOURCE / "commands"), str(SOURCE / "gps_nmea.c"),
                    str(work / "test.c"), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, stdout=subprocess.DEVNULL)
print("PASS: GPS UART pin routing, busy-port rejection, timeout and error cleanup")
