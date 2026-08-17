// WiFi UI screen stubs — backends excluded (Applications/wifi/ needs lwIP/esp_wifi).
// Keep in sync with:
//   - firmware_p4/components/Applications/ui/ui_manager.c switch cases
//   - tools/hle/CMakeLists.txt exclusions for ui/screens/wifi and connect_wifi

extern "C" {

void ui_connect_wifi_open(void) {}
void ui_wifi_ap_list_open(void) {}
void ui_wifi_attack_menu_open(void) {}
void ui_wifi_auth_flood_open(void) {}
void ui_wifi_beacon_spam_open(void) {}
void ui_wifi_beacon_spam_simple_open(void) {}
void ui_wifi_deauth_attack_open(void) {}
void ui_wifi_deauth_open(void) {}
void ui_wifi_evil_twin_open(void) {}
void ui_wifi_menu_open(void) {}
void ui_wifi_packets_menu_open(void) {}
void ui_wifi_probe_flood_open(void) {}
void ui_wifi_probe_open(void) {}
void ui_wifi_scan_ap_open(void) {}
void ui_wifi_scan_menu_open(void) {}
void ui_wifi_scan_monitor_open(void) {}
void ui_wifi_scan_open(void) {}
void ui_wifi_scan_probe_open(void) {}
void ui_wifi_scan_stations_open(void) {}
void ui_wifi_scan_target_open(void) {}
void ui_wifi_sniffer_attack_open(void) {}
void ui_wifi_sniffer_handshake_open(void) {}
void ui_wifi_sniffer_raw_open(void) {}

} // extern "C"
