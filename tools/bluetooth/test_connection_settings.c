// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "connection_settings.h"

#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define pdPASS 1
#define TOS_PATH_CONFIG_WIFI "wifi"
#define TOS_PATH_CONFIG_BLE "ble"
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))

tos_config_wifi_t g_config_wifi;
tos_config_ble_t g_config_ble;
static bool allocation_failure, task_failure;
static int save_error, wifi_error, stop_error, start_error, config_error, adv_error;
static unsigned saves, stops, starts, advertisements, signals;
static void (*worker)(void *);
static void *request_data;
static void *heap_caps_calloc(size_t count, size_t size, int caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return allocation_failure ? NULL : calloc(count, size);
}
static int xTaskCreatePinnedToCoreWithCaps(void (*fn)(void *), const char *name, int stack,
                                          void *arg, int priority, void *handle, int core, int caps) {
  assert(priority == SYS_PRIO_SERVICE_LO && core == SYS_CORE_RADIO && caps == MALLOC_CAP_SPIRAM);
  if (task_failure) return 0;
  assert(worker == NULL);
  worker = fn;
  request_data = arg;
  return 1;
}
static void vTaskDeleteWithCaps(void *task) { assert(task == NULL); }
static int wifi_service_save_ap_config(const char *name, const char *password, unsigned clients,
                                       const char *ip, bool enabled) {
  assert(strlen(name) <= 32 && strlen(password) <= 63 && clients >= 1 && clients <= 8);
  return wifi_error;
}
static int bluetooth_service_deinit(void) { ++stops; return stop_error; }
static int bluetooth_service_start(void) { ++starts; return start_error; }
static int bluetooth_service_save_announce_config(const char *name, unsigned clients) {
  assert(strlen(name) <= 29 && clients >= 1 && clients <= 3);
  return config_error;
}
static int bluetooth_service_start_advertising(void) { ++advertisements; return adv_error; }
int tos_config_save(const char *path, const char *module) { ++saves; return save_error; }
static void led_signal_info(void) { ++signals; }
static void led_signal_error(void) { ++signals; }
/* PRODUCTION */

static connection_settings_status_t finish(void) {
  assert(worker != NULL);
  worker(request_data);
  worker = NULL;
  request_data = NULL;
  connection_settings_status_t status;
  connection_settings_get_status(&status);
  assert(!status.is_busy && status.has_result);
  return status;
}

int main(void) {
  tos_config_wifi_t wifi = {.enabled=true, .ap={.ssid="Highboy", .password="12345678", .max_conn=4, .ip_addr="192.168.4.1"}};
  tos_config_ble_t ble = {.enabled=true, .advertising=true, .name="Highboy", .max_conn=3};
  assert(connection_settings_validate_wifi(NULL) == ESP_ERR_INVALID_ARG);
  strcpy(wifi.ap.password,"short");
  assert(connection_settings_request_wifi(&wifi) == ESP_ERR_INVALID_ARG);
  strcpy(wifi.ap.password,"12345678");
  wifi.ap.max_conn=9;
  assert(connection_settings_request_wifi(&wifi) == ESP_ERR_INVALID_ARG);
  wifi.ap.max_conn=4;
  allocation_failure=true;
  assert(connection_settings_request_wifi(&wifi) == ESP_ERR_NO_MEM && !saves);
  allocation_failure=false; task_failure=true;
  assert(connection_settings_request_wifi(&wifi) == ESP_ERR_NO_MEM && !saves);
  task_failure=false;
  assert(connection_settings_request_wifi(&wifi) == ESP_OK);
  assert(connection_settings_request_ble(&ble) == ESP_ERR_INVALID_STATE);
  assert(!g_config_wifi.enabled && !saves);
  wifi_error=ESP_FAIL;
  assert(finish().error == ESP_FAIL && !g_config_wifi.enabled && !saves);
  wifi_error=ESP_OK;
  assert(connection_settings_request_wifi(&wifi) == ESP_OK);
  assert(finish().error == ESP_OK && g_config_wifi.enabled && saves==1);
  wifi.enabled=false; save_error=ESP_FAIL;
  assert(connection_settings_request_wifi(&wifi) == ESP_OK);
  connection_settings_status_t status=finish();
  assert(status.error == ESP_FAIL && status.is_applied && !g_config_wifi.enabled);
  save_error=0;
  ble.max_conn=4;
  assert(connection_settings_request_ble(&ble) == ESP_ERR_INVALID_ARG);
  ble.max_conn=3; stop_error=ESP_FAIL;
  unsigned previous=saves;
  assert(connection_settings_request_ble(&ble) == ESP_OK);
  assert(finish().error == ESP_FAIL && starts==0 && saves==previous);
  stop_error=0; start_error=ESP_FAIL;
  assert(connection_settings_request_ble(&ble) == ESP_OK);
  assert(finish().error == ESP_FAIL && !g_config_ble.enabled && saves==previous);
  start_error=0; adv_error=ESP_FAIL;
  assert(connection_settings_request_ble(&ble) == ESP_OK);
  assert(finish().error == ESP_FAIL && saves==previous);
  adv_error=0;
  assert(connection_settings_request_ble(&ble) == ESP_OK);
  assert(finish().error == ESP_OK && g_config_ble.enabled && saves==previous+1);
  unsigned advertised=advertisements;
  ble.advertising=false;
  assert(connection_settings_request_ble(&ble) == ESP_OK);
  assert(finish().error == ESP_OK && advertisements==advertised);
  unsigned started=starts;
  ble.enabled=false;
  assert(connection_settings_request_ble(&ble) == ESP_OK);
  assert(finish().error == ESP_OK && starts==started && !g_config_ble.enabled);
  puts("PASS: settings validation, PSRAM/task failures, overlap, radio failures, save failure, visibility and on/off persistence");
}
