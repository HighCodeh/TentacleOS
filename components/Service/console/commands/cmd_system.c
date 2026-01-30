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

void register_system_commands(void) {
  const esp_console_cmd_t cmd_ip_def = {
    .command = "ip",
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
