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
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

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

static void print_interface_info(uint8_t iface, const char *name) {
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_WIFI_GET_IP_INFO,
      &iface, 1,
      &resp_hdr, resp_buf, 2000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    printf("%s Interface: unavailable\n", name);
    return;
  }

  spi_wifi_ip_info_t *info = (spi_wifi_ip_info_t *)&resp_buf[1];

  printf("%s Interface:\n", name);
  printf("  IP: %u.%u.%u.%u\n",
      (unsigned)(info->ip & 0xFF), (unsigned)((info->ip >> 8) & 0xFF),
      (unsigned)((info->ip >> 16) & 0xFF), (unsigned)((info->ip >> 24) & 0xFF));
  printf("  Mask: %u.%u.%u.%u\n",
      (unsigned)(info->netmask & 0xFF), (unsigned)((info->netmask >> 8) & 0xFF),
      (unsigned)((info->netmask >> 16) & 0xFF), (unsigned)((info->netmask >> 24) & 0xFF));
  printf("  GW: %u.%u.%u.%u\n",
      (unsigned)(info->gw & 0xFF), (unsigned)((info->gw >> 8) & 0xFF),
      (unsigned)((info->gw >> 16) & 0xFF), (unsigned)((info->gw >> 24) & 0xFF));
  printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
      info->mac[0], info->mac[1], info->mac[2],
      info->mac[3], info->mac[4], info->mac[5]);
}

static int cmd_ip(int argc, char **argv) {
  print_interface_info(0, "STA");
  print_interface_info(1, "AP");
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
