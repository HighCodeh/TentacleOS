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

#include "console_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "c5_flasher.h"
#include "spi_protocol.h"
#include "spi_bridge.h"
#include "spi_protocol.h"

static const char *TAG = "CMD_SYSTEM";

static int cmd_c5(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: c5 <ota|ping|info|sync|download|rom|passthrough|release>\n");
    printf("  ota <spi|uart>  push the C5 image; control on SPI, bytes over spi or uart\n");
    printf("  ping            ping the running C5 over the SPI bridge\n");
    printf("  info            read the C5 chip info over the SPI bridge\n");
    printf("  sync            re-probe and reconnect the bridge (e.g. after a C5 reboot)\n");
    printf("  download        ask the running C5 to enter ROM download mode (SPI)\n");
    printf("  rom             serial-flash a C5 already in download mode (recovery)\n");
    printf("  passthrough     bridge host esptool <-> C5 (never returns; BACK reboots)\n");
    printf("  release         tri-state the C5 UART lines for an external programmer\n");
    return 1;
  }
  if (strcmp(argv[1], "ota") == 0) {
    uint8_t transport = SPI_OTA_TRANSPORT_SPI;
    if (argc >= 3 && strcmp(argv[2], "uart") == 0) {
      transport = SPI_OTA_TRANSPORT_UART;
    } else if (argc >= 3 && strcmp(argv[2], "spi") != 0) {
      printf("usage: c5 ota <spi|uart>\n");
      return 1;
    }
    if (transport == SPI_OTA_TRANSPORT_UART) {
      c5_flasher_init(); // UART1 is only needed when the bytes travel over UART
    }
    esp_err_t r = c5_flasher_update(NULL, 0, transport);
    printf("C5 OTA (%s): %s\n", (transport == SPI_OTA_TRANSPORT_UART) ? "uart" : "spi",
           esp_err_to_name(r));
    return r == ESP_OK ? 0 : 1;
  }
  if (strcmp(argv[1], "ping") == 0) {
    esp_err_t r = c5_flasher_ping();
    printf("C5 ping: %s\n", esp_err_to_name(r));
    return r == ESP_OK ? 0 : 1;
  }
  if (strcmp(argv[1], "info") == 0) {
    esp_err_t r = c5_flasher_info();
    if (r != ESP_OK) {
      printf("C5 info: %s\n", esp_err_to_name(r));
    }
    return r == ESP_OK ? 0 : 1;
  }
  if (strcmp(argv[1], "sync") == 0) {
    esp_err_t r = c5_flasher_sync();
    printf("C5 sync: %s\n", esp_err_to_name(r));
    return r == ESP_OK ? 0 : 1;
  }
  if (strcmp(argv[1], "download") == 0) {
    esp_err_t r = c5_flasher_enter_download();
    printf("C5 enter-download: %s\n", esp_err_to_name(r));
    return r == ESP_OK ? 0 : 1;
  }
  if (strcmp(argv[1], "rom") == 0) {
    c5_flasher_init();
    esp_err_t r = c5_flasher_rom_flash();
    printf("C5 ROM flash: %s\n", esp_err_to_name(r));
    return r == ESP_OK ? 0 : 1;
  }
  if (strcmp(argv[1], "passthrough") == 0) {
    printf("Entering C5 passthrough. Reboot (or BACK) to exit.\n");
    c5_passthrough_run();
    return 0;
  }
  if (strcmp(argv[1], "release") == 0) {
    c5_flasher_release_uart();
    return 0;
  }
  printf("unknown subcommand '%s'\n", argv[1]);
  return 1;
}

static int cmd_free(int argc, char **argv) {
  printf("Internal RAM:\n");
  printf("  Free: %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  printf("  Min Free: %lu bytes\n",
         (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

  printf("SPIRAM (PSRAM):\n");
  printf("  Free: %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  printf("  Min Free: %lu bytes\n",
         (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
  return 0;
}

static int cmd_restart(int argc, char **argv) {
  printf("Restarting system...\n");
  esp_restart();
  return 0;
}

static int cmd_firstboot(int argc, char **argv) {
  (void)argc;
  (void)argv;
  nvs_handle_t h;
  if (nvs_open("tutorial", NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_key(h, "done");
    nvs_commit(h);
    nvs_close(h);
  }
  if (nvs_open("scrtips", NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_key(h, "seen");
    nvs_erase_key(h, "skip");
    nvs_commit(h);
    nvs_close(h);
  }
  printf("First-boot wizard + screen tips cleared. Restarting...\n");
  esp_restart();
  return 0;
}

static int cmd_capprep(int argc, char **argv) {
  (void)argc;
  (void)argv;
  nvs_handle_t h;
  if (nvs_open("tutorial", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, "done", 1);
    nvs_commit(h);
    nvs_close(h);
  }
  if (nvs_open("scrtips", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, "skip", 1);
    nvs_commit(h);
    nvs_close(h);
  }
  printf("Onboarding skipped (wizard + tips). Restarting clean for capture...\n");
  esp_restart();
  return 0;
}

static void print_interface_info(uint8_t iface, const char *name) {
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret =
      spi_bridge_send_command(SPI_ID_WIFI_GET_IP_INFO, &iface, 1, &resp_hdr, resp_buf, 2000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    printf("%s Interface: unavailable\n", name);
    return;
  }

  spi_wifi_ip_info_t *info = (spi_wifi_ip_info_t *)&resp_buf[1];

  printf("%s Interface:\n", name);
  printf("  IP: %u.%u.%u.%u\n",
         (unsigned)(info->ip & 0xFF),
         (unsigned)((info->ip >> 8) & 0xFF),
         (unsigned)((info->ip >> 16) & 0xFF),
         (unsigned)((info->ip >> 24) & 0xFF));
  printf("  Mask: %u.%u.%u.%u\n",
         (unsigned)(info->netmask & 0xFF),
         (unsigned)((info->netmask >> 8) & 0xFF),
         (unsigned)((info->netmask >> 16) & 0xFF),
         (unsigned)((info->netmask >> 24) & 0xFF));
  printf("  GW: %u.%u.%u.%u\n",
         (unsigned)(info->gw & 0xFF),
         (unsigned)((info->gw >> 8) & 0xFF),
         (unsigned)((info->gw >> 16) & 0xFF),
         (unsigned)((info->gw >> 24) & 0xFF));
  printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
         info->mac[0],
         info->mac[1],
         info->mac[2],
         info->mac[3],
         info->mac[4],
         info->mac[5]);
}

static int cmd_ip(int argc, char **argv) {
  print_interface_info(0, "STA");
  print_interface_info(1, "AP");
  return 0;
}

static int cmd_tasks(int argc, char **argv) {
  const size_t bytes_per_task = 40;
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
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_tasks_def));

  const esp_console_cmd_t cmd_ip_def = {
      .command = "ip",
      .help = "Show network interfaces",
      .hint = NULL,
      .func = &cmd_ip,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_ip_def));

  const esp_console_cmd_t cmd_free_def = {
      .command = "free",
      .help = "Show remaining memory",
      .hint = NULL,
      .func = &cmd_free,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_free_def));

  const esp_console_cmd_t cmd_restart_def = {
      .command = "restart",
      .help = "Reboot the Highboy",
      .hint = NULL,
      .func = &cmd_restart,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_restart_def));

  const esp_console_cmd_t cmd_firstboot_def = {
      .command = "firstboot",
      .help = "Clear the first-boot onboarding flag and restart to run it again",
      .hint = NULL,
      .func = &cmd_firstboot,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_firstboot_def));

  const esp_console_cmd_t cmd_capprep_def = {
      .command = "capprep",
      .help = "Skip onboarding (wizard + tips) and restart clean, for screen capture",
      .hint = NULL,
      .func = &cmd_capprep,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_capprep_def));

  const esp_console_cmd_t cmd_c5_def = {
      .command = "c5",
      .help = "C5 firmware update: ota | download | rom | passthrough | release",
      .hint = NULL,
      .func = &cmd_c5,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_c5_def));
}
