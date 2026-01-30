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
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include <string.h>

// Service Includes
#include "wifi_service.h"

// Application Includes
#include "ap_scanner.h"
#include "wifi_deauther.h"
#include "beacon_spam.h"

static const char *TAG = "CMD_WIFI";

static struct {
  struct arg_end *end;
} scan_args;

static int subcmd_scan(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&scan_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, scan_args.end, "wifi scan");
    return 1;
  }

  printf("Starting Wi-Fi Scan...\n");
  wifi_service_scan();

  uint16_t count = wifi_service_get_ap_count();
  printf("Found %d networks:\n", count);
  printf("%-32s | %-17s | %s | %s | %s\n", "SSID", "BSSID", "CH", "RSSI", "WPS");
  printf("--------------------------------------------------------------------------------\n");

  for (int i = 0; i < count; i++) {
    wifi_ap_record_t *rec = wifi_service_get_ap_record(i);
    if (rec) {
      printf("%-32s | %02x:%02x:%02x:%02x:%02x:%02x | %2d | %4d | %s\n", 
             rec->ssid, 
             rec->bssid[0], rec->bssid[1], rec->bssid[2], rec->bssid[3], rec->bssid[4], rec->bssid[5],
             rec->primary, rec->rssi,
             rec->wps ? "Yes" : "No ");
    }
  }
  return 0;
}

static struct {
  struct arg_str *ssid;
  struct arg_str *password;
  struct arg_end *end;
} connect_args;

static int subcmd_connect(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&connect_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, connect_args.end, "wifi connect");
    return 1;
  }

  const char *ssid = connect_args.ssid->sval[0];
  const char *pass = (connect_args.password->count > 0) ? connect_args.password->sval[0] : NULL;

  printf("Connecting to '%s'...\n", ssid);
  esp_err_t err = wifi_service_connect_to_ap(ssid, pass);
  if (err == ESP_OK) {
    printf("Connection request sent.\n");
  } else {
    printf("Error initiating connection: %s\n", esp_err_to_name(err));
  }
  return 0;
}

static struct {
  struct arg_str *ssid;
  struct arg_str *password;
  struct arg_end *end;
} ap_args;

static int subcmd_ap(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&ap_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, ap_args.end, "wifi ap");
    return 1;
  }

  const char *ssid = ap_args.ssid->sval[0];
  const char *pass = (ap_args.password->count > 0) ? ap_args.password->sval[0] : "";

  printf("Configuring AP: SSID='%s', Pass='%s'\n", ssid, pass);
  wifi_set_ap_ssid(ssid);
  wifi_set_ap_password(pass);
  printf("AP Configuration updated.\n");
  return 0;
}

static struct {
  struct arg_lit *random;
  struct arg_lit *list;
  struct arg_lit *stop;
  struct arg_end *end;
} spam_args;

static int subcmd_spam(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&spam_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, spam_args.end, "wifi spam");
    return 1;
  }

  if (spam_args.stop->count > 0) {
    beacon_spam_stop();
    printf("Beacon spam stopped.\n");
    return 0;
  }

  if (spam_args.random->count > 0) {
    if (beacon_spam_start_random()) {
      printf("Random Beacon Spam started.\n");
    } else {
      printf("Failed to start Random Beacon Spam.\n");
    }
    return 0;
  }

  if (spam_args.list->count > 0) {
    if (beacon_spam_start_custom("/assets/config/wifi/beacon_list.json")) {
      printf("Custom List Beacon Spam started.\n");
    } else {
      printf("Failed to start Custom Beacon Spam (Check /assets/config/wifi/beacon_list.json).\n");
    }
    return 0;
  }

  printf("Usage: wifi spam -r (random) | -l (list) | -s (stop)\n");
  return 0;
}

static struct {
  struct arg_str *mac; // Target MAC
  struct arg_int *channel;
  struct arg_lit *stop;
  struct arg_end *end;
} deauth_args;

static int subcmd_deauth(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&deauth_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, deauth_args.end, "wifi deauth");
    return 1;
  }

  if (deauth_args.stop->count > 0) {
    wifi_deauther_stop();
    printf("Deauther stopped.\n");
    return 0;
  }

  if (deauth_args.mac->count == 0) {
    printf("Error: Target MAC required.\n");
    return 1;
  }

  const char *mac_str = deauth_args.mac->sval[0];
  uint8_t mac[6];
  int parsed = sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                      &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

  if (parsed != 6) {
    printf("Error: Invalid MAC format. Use XX:XX:XX:XX:XX:XX\n");
    return 1;
  }

  int channel = (deauth_args.channel->count > 0) ? deauth_args.channel->ival[0] : 1;

  wifi_ap_record_t target_ap;
  memset(&target_ap, 0, sizeof(wifi_ap_record_t));
  memcpy(target_ap.bssid, mac, 6);
  target_ap.primary = channel;

  if (wifi_deauther_start(&target_ap, DEAUTH_INVALID_AUTH, true)) {
    printf("Deauth Attack Started on %s (Ch %d)\n", mac_str, channel);
  } else {
    printf("Failed to start Deauth (Is Wi-Fi running?).\n");
  }

  return 0;
}

static int subcmd_status(int argc, char **argv) {
  printf("--- Wi-Fi Status ---\n");
  printf("Service Active: %s\n", wifi_service_is_active() ? "Yes" : "No");

  const char* conn_ssid = wifi_service_get_connected_ssid();
  printf("Connected STA:  %s\n", conn_ssid ? conn_ssid : "Disconnected");

  // Get MACs
  uint8_t mac_sta[6], mac_ap[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac_sta);
  esp_wifi_get_mac(WIFI_IF_AP, mac_ap);

  printf("MAC STA:        " MACSTR "\n", MAC2STR(mac_sta));
  printf("MAC AP:         " MACSTR "\n", MAC2STR(mac_ap));

  // Attack Status
  printf("Beacon Spam:    %s\n", beacon_spam_is_running() ? "RUNNING" : "Stopped");
  printf("Deauther:       %s\n", wifi_deauther_is_running() ? "RUNNING" : "Stopped");

  return 0;
}


static int cmd_wifi(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: wifi <command> [args]\n");
    printf("Commands:\n");
    printf("  scan              Scan networks\n");
    printf("  connect           Connect to AP (-s SSID [-p PASS])\n");
    printf("  ap                Config Hotspot (-s SSID [-p PASS])\n");
    printf("  spam              Beacon Spam (-r random | -l list | -s stop)\n");
    printf("  deauth            Deauth Attack (-t MAC [-c CH] | -s stop)\n");
    printf("  status            Show status\n");
    return 0;
  }

  const char *subcmd = argv[1];
  int sub_argc = argc - 1;
  char **sub_argv = &argv[1];

  if (strcmp(subcmd, "scan") == 0) return subcmd_scan(sub_argc, sub_argv);
  if (strcmp(subcmd, "connect") == 0) return subcmd_connect(sub_argc, sub_argv);
  if (strcmp(subcmd, "ap") == 0) return subcmd_ap(sub_argc, sub_argv);
  if (strcmp(subcmd, "spam") == 0) return subcmd_spam(sub_argc, sub_argv);
  if (strcmp(subcmd, "deauth") == 0) return subcmd_deauth(sub_argc, sub_argv);
  if (strcmp(subcmd, "status") == 0) return subcmd_status(sub_argc, sub_argv);

  printf("Unknown wifi command: %s\n", subcmd);
  return 1;
}

void register_wifi_commands(void) {
  // Initialize Arg Tables
  scan_args.end = arg_end(1);

  connect_args.ssid = arg_str1("s", "ssid", "<ssid>", "Network SSID");
  connect_args.password = arg_str0("p", "pass", "<password>", "Password");
  connect_args.end = arg_end(1);

  ap_args.ssid = arg_str1("s", "ssid", "<ssid>", "AP SSID");
  ap_args.password = arg_str0("p", "pass", "<password>", "AP Password");
  ap_args.end = arg_end(1);

  spam_args.random = arg_lit0("r", "random", "Random SSIDs");
  spam_args.list = arg_lit0("l", "list", "Use beacon_list.json");
  spam_args.stop = arg_lit0("s", "stop", "Stop spam");
  spam_args.end = arg_end(1);

  deauth_args.mac = arg_str0("t", "target", "<mac>", "Target BSSID");
  deauth_args.channel = arg_int0("c", "channel", "<ch>", "Channel");
  deauth_args.stop = arg_lit0("s", "stop", "Stop attack");
  deauth_args.end = arg_end(1);

  const esp_console_cmd_t wifi_cmd = {
    .command = "wifi",
    .help = "Wi-Fi Management & Attacks",
    .hint = "<scan|connect|ap|spam|deauth|status> ...",
    .func = &cmd_wifi,
    .argtable = NULL // No argtable for the wrapper, we handle manually
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_cmd));
}
