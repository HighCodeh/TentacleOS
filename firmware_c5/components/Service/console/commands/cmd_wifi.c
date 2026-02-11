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
#include "wifi_sniffer.h"
#include "probe_monitor.h"
#include "client_scanner.h"
#include "target_scanner.h"
#include "signal_monitor.h"
#include "deauther_detector.h"
#include "evil_twin.h"
#include "port_scan.h"

static const char *TAG = "CMD_WIFI";


// --- SCAN ---
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

// --- CONNECT ---
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

// --- AP CONFIG ---
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

// --- CONFIG ---
static struct {
  struct arg_int *enabled;
  struct arg_str *ip;
  struct arg_int *max_conn;
  struct arg_end *end;
} config_args;

static int subcmd_config(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&config_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, config_args.end, "wifi config");
    return 1;
  }

  if (config_args.enabled->count > 0) {
    bool en = (config_args.enabled->ival[0] != 0);
    printf("Setting Wi-Fi Enabled: %s\n", en ? "True" : "False");
    wifi_set_wifi_enabled(en);
  }

  if (config_args.ip->count > 0) {
    const char *ip = config_args.ip->sval[0];
    printf("Setting AP IP: %s\n", ip);
    wifi_set_ap_ip(ip);
  }

  if (config_args.max_conn->count > 0) {
    int max = config_args.max_conn->ival[0];
    printf("Setting Max Connections: %d\n", max);
    wifi_set_ap_max_conn((uint8_t)max);
  }

  return 0;
}

// --- SPAM ---
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

// --- DEAUTH ---
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

// --- SNIFFER ---
static struct {
  struct arg_str *type; // beacon, probe, pwn (eapol/pmkid), raw
  struct arg_int *channel;
  struct arg_str *file; // Save to file
  struct arg_lit *verbose; // Print to console
  struct arg_lit *stop;
  struct arg_end *end;
} sniff_args;

static int subcmd_sniff(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&sniff_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, sniff_args.end, "wifi sniff");
    return 1;
  }

  if (sniff_args.stop->count > 0) {
    wifi_sniffer_stop();
    printf("Sniffer stopped.\n");
    return 0;
  }

  sniff_type_t type = SNIFF_TYPE_RAW;
  if (sniff_args.type->count > 0) {
    const char *t = sniff_args.type->sval[0];
    if (strcmp(t, "beacon") == 0) type = SNIFF_TYPE_BEACON;
    else if (strcmp(t, "probe") == 0) type = SNIFF_TYPE_PROBE;
    else if (strcmp(t, "pwn") == 0) type = SNIFF_TYPE_EAPOL;
  }

  uint8_t ch = (sniff_args.channel->count > 0) ? (uint8_t)sniff_args.channel->ival[0] : 0; // 0 = Hopping

  wifi_sniffer_set_verbose(sniff_args.verbose->count > 0);

  if (sniff_args.file->count > 0) {
    // Start with streaming to SD if file provided
    if (wifi_sniffer_start_stream_sd(type, ch, sniff_args.file->sval[0])) {
      printf("Sniffer started (streaming to %s)\n", sniff_args.file->sval[0]);
    } else {
      printf("Failed to start sniffer stream.\n");
    }
  } else {
    // Normal sniff to RAM
    if (wifi_sniffer_start(type, ch)) {
      printf("Sniffer started (RAM buffer).\n");
    } else {
      printf("Failed to start sniffer.\n");
    }
  }
  return 0;
}

// --- PROBE MONITOR ---
static struct {
  struct arg_lit *start;
  struct arg_lit *stop;
  struct arg_end *end;
} probe_args;

static int subcmd_probe(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&probe_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, probe_args.end, "wifi probe");
    return 1;
  }

  if (probe_args.stop->count > 0) {
    probe_monitor_stop();
    printf("Probe monitor stopped.\n");
    return 0;
  }

  if (probe_monitor_start()) {
    printf("Probe monitor started. Use 'wifi status' to see results count.\n");
  } else {
    printf("Failed to start probe monitor.\n");
  }
  return 0;
}

// --- CLIENT SCAN ---
static struct {
  struct arg_lit *start;
  struct arg_lit *stop;
  struct arg_end *end;
} clients_args;

static int subcmd_clients(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&clients_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, clients_args.end, "wifi clients");
    return 1;
  }

  if (clients_args.stop->count > 0) {
    // client scanner stops automatically usually, but we can force it by stopping global sniffer
    wifi_service_promiscuous_stop(); 
    printf("Client scanner stopped.\n");
    return 0;
  }

  if (client_scanner_start()) {
    printf("Client scanner started (15s duration).\n");
  } else {
    printf("Failed to start client scanner.\n");
  }
  return 0;
}

// --- TARGET SCAN ---
static struct {
  struct arg_str *mac;
  struct arg_int *channel;
  struct arg_lit *stop;
  struct arg_end *end;
} target_args;

static int subcmd_target(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&target_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, target_args.end, "wifi target");
    return 1;
  }

  if (target_args.stop->count > 0) {
    // No explicit stop function in public API except destroying task?
    // Usually stops itself. Force stop sniff.
    wifi_service_promiscuous_stop();
    printf("Target scanner stopped.\n");
    return 0;
  }

  if (target_args.mac->count == 0 || target_args.channel->count == 0) {
    printf("Error: Target MAC and Channel required.\n");
    return 1;
  }

  const char *mac_str = target_args.mac->sval[0];
  uint8_t mac[6];
  sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
  uint8_t ch = (uint8_t)target_args.channel->ival[0];

  if (target_scanner_start(mac, ch)) {
    printf("Target scanner started for %s on Ch %d.\n", mac_str, ch);
  } else {
    printf("Failed to start target scanner.\n");
  }
  return 0;
}

// --- EVIL TWIN ---
static struct {
  struct arg_str *ssid;
  struct arg_lit *stop;
  struct arg_end *end;
} evil_args;

static int subcmd_evil(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&evil_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, evil_args.end, "wifi evil");
    return 1;
  }

  if (evil_args.stop->count > 0) {
    evil_twin_stop_attack();
    printf("Evil Twin stopped.\n");
    return 0;
  }

  if (evil_args.ssid->count > 0) {
    const char *ssid = evil_args.ssid->sval[0];
    evil_twin_start_attack(ssid);
    printf("Evil Twin started with SSID: %s\n", ssid);
    return 0;
  }

  printf("Usage: wifi evil -ssid <ssid> | --stop (stop)\n");
  return 0;
}

// --- PORT SCAN ---
static struct {
  struct arg_str *ip;
  struct arg_int *min;
  struct arg_int *max;
  struct arg_end *end;
} port_args;

static int subcmd_portscan(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&port_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, port_args.end, "wifi portscan");
    return 1;
  }

  if (port_args.ip->count == 0) {
    printf("Error: IP required.\n");
    return 1;
  }

  const char *ip = port_args.ip->sval[0];
  int min = (port_args.min->count > 0) ? port_args.min->ival[0] : 1;
  int max = (port_args.max->count > 0) ? port_args.max->ival[0] : 1024;

  printf("Starting Port Scan on %s (%d-%d). This block the console...\n", ip, min, max);

  // Allocate results on heap to avoid stack overflow
  scan_result_t *results = malloc(sizeof(scan_result_t) * 20);
  if (!results) {
    printf("Memory error.\n");
    return 1;
  }

  int count = port_scan_target_range(ip, min, max, results, 20);

  printf("Scan finished. Found %d open ports:\n", count);
  for(int i=0; i<count; i++) {
    printf("  %d/%s: %s\n", results[i].port, (results[i].protocol == PROTO_TCP) ? "TCP" : "UDP", results[i].banner);
  }
  free(results);
  return 0;
}

// --- STATUS ---
static int subcmd_status(int argc, char **argv) {
  printf("--- Wi-Fi Status ---\n");
  printf("Service Active: %s\n", wifi_service_is_active() ? "Yes" : "No");

  const char* conn_ssid = wifi_service_get_connected_ssid();
  printf("Connected STA:  %s\n", conn_ssid ? conn_ssid : "Disconnected");

  uint8_t mac_sta[6], mac_ap[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac_sta);
  esp_wifi_get_mac(WIFI_IF_AP, mac_ap);

  printf("MAC STA:        " MACSTR "\n", MAC2STR(mac_sta));
  printf("MAC AP:         " MACSTR "\n", MAC2STR(mac_ap));

  printf("--- Applications ---\n");
  printf("Beacon Spam:    %s\n", beacon_spam_is_running() ? "RUNNING" : "Stopped");
  printf("Deauther:       %s\n", wifi_deauther_is_running() ? "RUNNING" : "Stopped");
  // pcap_buffer is private, using packet count as proxy for activity or just generic status
  printf("Sniffer Pkts:   %lu\n", wifi_sniffer_get_packet_count());
  printf("Deauth Det:     %lu events detected\n", deauther_detector_get_count());

  return 0;
}

// =============================================================================
// MAIN WIFI COMMAND DISPATCHER
// =============================================================================

static int cmd_wifi(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: wifi <command> [options]\n\n");
    printf("Commands:\n");
    printf("  scan       Scan networks\n");
    printf("  connect    Connect to AP\n");
    printf("             -s <ssid> -p <pass>\n");
    printf("  ap         Config Hotspot\n");
    printf("             -s <ssid> -p <pass>\n");
    printf("  config     Advanced Config\n");
    printf("             -e <0/1> (Enable) | -i <ip> | -m <max_conn>\n");
    printf("  spam       Beacon Spam Attack\n");
    printf("             -r (Random) | -l (List) | -s (Stop)\n");
    printf("  deauth     Deauth Attack\n");
    printf("             -t <mac> [-c <ch>] | -s (Stop)\n");
    printf("  sniff      Packet Sniffer\n");
    printf("             -t <type> -c <ch> -f <file> -v (Verbose) | -s (Stop)\n");
    printf("  probe      Probe Request Monitor\n");
    printf("             start | -s (Stop)\n");
    printf("  clients    Scan Connected Clients\n");
    printf("             start | -s (Stop)\n");
    printf("  target     Target Scan\n");
    printf("             -t <mac> -c <ch> | -s (Stop)\n");
    printf("  evil       Evil Twin Attack\n");
    printf("             -s <ssid> | -s (Stop)\n");
    printf("  portscan   Port Scanner\n");
    printf("             -i <ip> [-min <port>] [-max <port>]\n");
    printf("  status     Show current status\n");
    return 0;
  }
  const char *subcmd = argv[1];
  int sub_argc = argc - 1;
  char **sub_argv = &argv[1];

  if (strcmp(subcmd, "scan") == 0) return subcmd_scan(sub_argc, sub_argv);
  if (strcmp(subcmd, "connect") == 0) return subcmd_connect(sub_argc, sub_argv);
  if (strcmp(subcmd, "ap") == 0) return subcmd_ap(sub_argc, sub_argv);
  if (strcmp(subcmd, "config") == 0) return subcmd_config(sub_argc, sub_argv);
  if (strcmp(subcmd, "spam") == 0) return subcmd_spam(sub_argc, sub_argv);
  if (strcmp(subcmd, "deauth") == 0) return subcmd_deauth(sub_argc, sub_argv);
  if (strcmp(subcmd, "status") == 0) return subcmd_status(sub_argc, sub_argv);
  // New Commands
  if (strcmp(subcmd, "sniff") == 0) return subcmd_sniff(sub_argc, sub_argv);
  if (strcmp(subcmd, "probe") == 0) return subcmd_probe(sub_argc, sub_argv);
  if (strcmp(subcmd, "clients") == 0) return subcmd_clients(sub_argc, sub_argv);
  if (strcmp(subcmd, "target") == 0) return subcmd_target(sub_argc, sub_argv);
  if (strcmp(subcmd, "evil") == 0) return subcmd_evil(sub_argc, sub_argv);
  if (strcmp(subcmd, "portscan") == 0) return subcmd_portscan(sub_argc, sub_argv);

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

  config_args.enabled = arg_int0("e", "enabled", "<0/1>", "Enable/Disable Wi-Fi");
  config_args.ip = arg_str0("i", "ip", "<ip>", "Static IP");
  config_args.max_conn = arg_int0("m", "max", "<val>", "Max Connections");
  config_args.end = arg_end(1);

  spam_args.random = arg_lit0("r", "random", "Random SSIDs");
  spam_args.list = arg_lit0("l", "list", "Use beacon_list.json");
  spam_args.stop = arg_lit0("s", "stop", "Stop spam");
  spam_args.end = arg_end(1);

  deauth_args.mac = arg_str0("t", "target", "<mac>", "Target BSSID");
  deauth_args.channel = arg_int0("c", "channel", "<ch>", "Channel");
  deauth_args.stop = arg_lit0("s", "stop", "Stop attack");
  deauth_args.end = arg_end(1);

  sniff_args.type = arg_str0("t", "type", "<beacon|probe|pwn|raw>", "Sniff Type");
  sniff_args.channel = arg_int0("c", "channel", "<ch>", "Channel (0=Hop)");
  sniff_args.file = arg_str0("f", "file", "<path>", "Save to .pcap");
  sniff_args.verbose = arg_lit0("v", "verbose", "Print packets");
  sniff_args.stop = arg_lit0("s", "stop", "Stop sniffer");
  sniff_args.end = arg_end(1);

  probe_args.start = arg_lit0(NULL, "start", "Start monitor");
  probe_args.stop = arg_lit0("s", "stop", "Stop monitor");
  probe_args.end = arg_end(1);

  clients_args.start = arg_lit0(NULL, "start", "Start scan");
  clients_args.stop = arg_lit0("s", "stop", "Stop scan");
  clients_args.end = arg_end(1);

  target_args.mac = arg_str0("t", "target", "<mac>", "BSSID");
  target_args.channel = arg_int0("c", "channel", "<ch>", "Channel");
  target_args.stop = arg_lit0("s", "stop", "Stop scan");
  target_args.end = arg_end(1);

  evil_args.ssid = arg_str0("s", "ssid", "<ssid>", "Fake AP Name");
  evil_args.stop = arg_lit0(NULL, "stop", "Stop attack"); // arg_lit0 name fixed to match flag
  evil_args.end = arg_end(1);

  port_args.ip = arg_str1("i", "ip", "<ip>", "Target IP");
  port_args.min = arg_int0(NULL, "min", "<port>", "Start Port");
  port_args.max = arg_int0(NULL, "max", "<port>", "End Port");
  port_args.end = arg_end(1);

  const esp_console_cmd_t wifi_cmd = {
    .command = "wifi",
    .help = "Wi-Fi Management & Attacks",
    .hint = "<scan|connect|ap|spam|deauth|sniff|evil|...> ...",
    .func = &cmd_wifi,
    .argtable = NULL 
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_cmd));
}
