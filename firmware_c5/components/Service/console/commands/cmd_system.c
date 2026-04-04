// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "console_service.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"

static int cmd_free(int argc, char **argv) {
  printf("Internal RAM:\n");
  printf("  Free: %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  printf("  Min Free: %lu bytes\n", (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

  printf("SPIRAM (PSRAM):\n");
  printf("  Free: %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  printf("  Min Free: %lu bytes\n", (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
  return 0;
}

static int cmd_restart(int argc, char **argv) {
  printf("Restarting system...\n");
  esp_restart();
  return 0;
}

static int cmd_ip(int argc, char **argv) {
  esp_netif_t *netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_t *netif_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

  if (netif_sta) {
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif_sta, &ip_info);
    printf("STA Interface:\n");
    printf("  IP: " IPSTR "\n", IP2STR(&ip_info.ip));
    printf("  Mask: " IPSTR "\n", IP2STR(&ip_info.netmask));
    printf("  GW: " IPSTR "\n", IP2STR(&ip_info.gw));

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    printf("  MAC: " MACSTR "\n", MAC2STR(mac));
  }

  if (netif_ap) {
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif_ap, &ip_info);
    printf("AP Interface:\n");
    printf("  IP: " IPSTR "\n", IP2STR(&ip_info.ip));
    printf("  Mask: " IPSTR "\n", IP2STR(&ip_info.netmask));
    printf("  GW: " IPSTR "\n", IP2STR(&ip_info.gw));

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    printf("  MAC: " MACSTR "\n", MAC2STR(mac));
  }
  return 0;
}

static int cmd_tasks(int argc, char **argv) {
  const size_t bytes_per_task = 40; /* See vTaskList description */
  char *task_list_buffer = malloc(uxTaskGetNumberOfTasks() * bytes_per_task);

  if (task_list_buffer == NULL) {
    printf("Error: Failed to allocate memory for task list.\n");
    return 1;
  }

  printf("Task Name       State   Prio    Stack   Num\n");
  printf("-------------------------------------------\n");
  vTaskList(task_list_buffer);
  printf("%s", task_list_buffer);
  printf("-------------------------------------------\n");

  free(task_list_buffer);
  return 0;
}

void register_system_commands(void) {
  const esp_console_cmd_t cmd_tasks_def = {
    .command = "tasks",
    .help = "List running FreeRTOS tasks",
    .hint = NULL,
    .func = &cmd_tasks,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_tasks_def));

  const esp_console_cmd_t cmd_ip_def = {    .command = "ip",
    .help = "Show network interfaces",
    .hint = NULL,
    .func = &cmd_ip,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_ip_def));

  const esp_console_cmd_t cmd_free_def = {    .command = "free",
    .help = "Show remaining memory",
    .hint = NULL,
    .func = &cmd_free,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_free_def));

  const esp_console_cmd_t cmd_restart_def = {
    .command = "restart",
    .help = "Reboot the Highboy",
    .hint = NULL,
    .func = &cmd_restart,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_restart_def));
}
