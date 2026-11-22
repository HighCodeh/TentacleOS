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

"""Exercise actual C5 radio initialization with retained HT40 and injected SDK failures."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = (ROOT / "firmware_c5/components/Service/wifi/wifi_service.c").read_text()
NOTICE = SOURCE.split("#include", 1)[0]


def function(name):
    match = re.search(r"^(?:static )?(?:void|esp_err_t) " + name + r"\([^;]*?\) \{", SOURCE, re.M)
    assert match, name
    start = match.start()
    level = 0
    for index in range(SOURCE.index("{", start), len(SOURCE)):
        level += (SOURCE[index] == "{") - (SOURCE[index] == "}")
        if level == 0:
            return SOURCE[start:index + 1] + "\n"
    raise AssertionError(name)


SHIM = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
typedef int wifi_interface_t;
typedef struct {int ghz_2g,ghz_5g;} wifi_bandwidths_t, wifi_protocols_t;
typedef struct {int unused;} wifi_init_config_t, esp_netif_t;
typedef struct {struct {uint32_t addr;} ip,gw,netmask;} esp_netif_ip_info_t;
typedef struct {struct {char ssid[33], password[64];uint8_t ssid_len,channel,max_connection;int beacon_interval,authmode;} ap;} wifi_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define WIFI_BW20 1
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_PROTOCOL_11A 16
#define WIFI_PROTOCOL_11AC 32
#define WIFI_PROTOCOL_11AX 64
#define WIFI_MODE_APSTA 3
#define WIFI_BAND_MODE_AUTO 3
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA_WPA2_PSK 3
#define WIFI_PS_MIN_MODEM 1
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(call) assert((call)==ESP_OK)
#define SSID_MAX_LEN 32
#define PASSWORD_MAX_LEN 64
#define IP_ADDR_MAX_LEN 16
#define DEFAULT_MAX_CONN 4
#define DEFAULT_AP_CHANNEL 1
#define DEFAULT_BEACON_INTERVAL 100
#define WIFI_EVENT 1
#define IP_EVENT 2
#define ESP_EVENT_ANY_ID -1
#define IP4_ADDR(...) ((void)0)
static bool s_is_active,s_is_connected;
static void *s_mutex;
static bool enabled,initialized,started;
static int mode,configure_calls,fail_config_call,starts,band_calls,stops;
static int start_error,band_error,stop_error;
static wifi_bandwidths_t bandwidths[2];
static wifi_protocols_t protocols[2];
static esp_netif_t netif;
static int nvs_flash_init(void){return ESP_OK;}
static int esp_netif_init(void){return ESP_OK;}
static int esp_event_loop_create_default(void){return ESP_OK;}
static esp_netif_t *esp_netif_create_default_wifi_ap(void){return &netif;}
static esp_netif_t *esp_netif_create_default_wifi_sta(void){return &netif;}
static int esp_wifi_init(const wifi_init_config_t *cfg){(void)cfg;initialized=true;return ESP_OK;}
#define esp_event_handler_instance_register(...) (ESP_OK)
static int esp_wifi_set_mode(int value){assert(initialized);mode=value;return ESP_OK;}
static int esp_wifi_set_config(int ifx,const wifi_config_t *cfg){assert(ifx==WIFI_IF_AP && cfg->ap.channel==1);return ESP_OK;}
static int esp_netif_dhcps_stop(esp_netif_t *n){assert(n);return ESP_OK;}
static int esp_netif_dhcps_start(esp_netif_t *n){assert(n);return ESP_OK;}
static int esp_netif_str_to_ip4(const char *s,void *out){assert(s&&out);return ESP_OK;}
static int esp_netif_set_ip_info(esp_netif_t *n,const esp_netif_ip_info_t *i){assert(n&&i);return ESP_OK;}
static int esp_wifi_set_ps(int value){assert(value==WIFI_PS_MIN_MODEM);return ESP_OK;}
static void *xSemaphoreCreateMutex(void){return &netif;}
static int init_known_network_writer(void){return ESP_OK;}
static void load_ap_config(char *ssid,char *password,uint8_t *clients,char *ip,bool *out_enabled){
 (void)ssid;(void)password;(void)clients;(void)ip;*out_enabled=enabled;
}
static int esp_wifi_set_bandwidths(int ifx,const wifi_bandwidths_t *bw){
 assert(initialized && !started && ifx>=0 && ifx<2);
 assert(bw->ghz_2g==WIFI_BW20 && bw->ghz_5g==WIFI_BW20);
 if(++configure_calls==fail_config_call)return ESP_ERR_INVALID_STATE;
 bandwidths[ifx]=*bw;return ESP_OK;
}
static int esp_wifi_set_protocols(int ifx,const wifi_protocols_t *p){
 assert(initialized && !started && bandwidths[ifx].ghz_2g==WIFI_BW20 && bandwidths[ifx].ghz_5g==WIFI_BW20);
 assert(p->ghz_2g==(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_11AX));
 assert(p->ghz_5g==(WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_11AC|WIFI_PROTOCOL_11AX));
 if(++configure_calls==fail_config_call)return ESP_ERR_INVALID_STATE;
 protocols[ifx]=*p;return ESP_OK;
}
static int esp_wifi_start(void){
 assert(initialized && mode==WIFI_MODE_APSTA);
 for(int i=0;i<2;i++)assert(bandwidths[i].ghz_2g==WIFI_BW20 && bandwidths[i].ghz_5g==WIFI_BW20 &&
  (protocols[i].ghz_2g&WIFI_PROTOCOL_11AX) && (protocols[i].ghz_5g&WIFI_PROTOCOL_11AX));
 ++starts;if(start_error)return start_error;started=true;return ESP_OK;
}
static int esp_wifi_set_band_mode(int value){assert(started && value==WIFI_BAND_MODE_AUTO);++band_calls;return band_error;}
static int esp_wifi_stop(void){++stops;if(stop_error)return stop_error;started=false;return ESP_OK;}
static esp_err_t configure_radio(void);
static esp_err_t start_radio(void);
"""
TEST = r"""
static void reset(bool boot_enabled){
 enabled=boot_enabled;initialized=started=s_is_active=s_is_connected=false;s_mutex=NULL;
 mode=configure_calls=fail_config_call=starts=band_calls=stops=0;start_error=band_error=stop_error=0;
 for(int i=0;i<2;i++){bandwidths[i]=(wifi_bandwidths_t){2,2};protocols[i]=(wifi_protocols_t){WIFI_PROTOCOL_11N,WIFI_PROTOCOL_11N};}
}
int main(void){
 reset(true);wifi_service_init();assert(started && s_is_active && starts==1 && band_calls==1 && configure_calls==4);
 wifi_service_start();assert(starts==1 && band_calls==1);
 reset(false);wifi_service_init();assert(!started && !s_is_active && starts==0 && band_calls==0 && configure_calls==4);
 wifi_service_start();assert(started && s_is_active && starts==1 && band_calls==1);
 assert(esp_wifi_stop()==ESP_OK);s_is_active=false;
 wifi_service_start();assert(started && s_is_active && starts==2 && band_calls==2);
 for(int fail=1;fail<=4;fail++){
  reset(false);initialized=true;fail_config_call=fail;
  assert(configure_radio()==ESP_ERR_INVALID_STATE && configure_calls==fail && starts==0 && band_calls==0);
 }
 reset(false);wifi_service_init();start_error=ESP_ERR_INVALID_STATE;wifi_service_start();
 assert(!s_is_active && !started && band_calls==0 && stops==0);
 start_error=0;band_error=ESP_ERR_INVALID_STATE;wifi_service_start();
 assert(!s_is_active && !started && band_calls==1 && stops==1);
 band_error=0;wifi_service_start();assert(s_is_active && started && band_calls==2);
 reset(false);wifi_service_init();band_error=stop_error=ESP_ERR_INVALID_STATE;wifi_service_start();
 assert(s_is_active && started && stops==1);
 puts("PASS: C5 actual init/start: 20 MHz before AX/start, dual-band after start, disabled boot, restart, configuration and cleanup failures");
}
"""
production = "\n".join(function(name) for name in
                       ("configure_radio", "start_radio", "wifi_service_init", "wifi_service_start"))
with tempfile.TemporaryDirectory(prefix="hb-wifi-startup-") as temp:
    path = Path(temp)
    (path / "test.c").write_text(NOTICE + SHIM + production + TEST)
    subprocess.run(["cc", "-std=c11", "-fsanitize=address,undefined", "-g",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)
