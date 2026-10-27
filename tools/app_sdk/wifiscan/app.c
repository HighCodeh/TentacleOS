// Example: use the WiFi subsystem of the ABI. Scans and logs the access points.
// Requests only radio-rx (discovery), so it cannot transmit.
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  api->log(TOS_LOG_INFO, "WIFISCAN", "scanning...");
  if (api->wifi->scan() != 0) {
    api->log(TOS_LOG_ERROR, "WIFISCAN", "scan failed (radio-rx granted?)");
    return 1;
  }
  int n = api->wifi->ap_count();
  api->log(TOS_LOG_INFO, "WIFISCAN", "found %d access points:", n);
  for (int i = 0; i < n && i < 20; i++) {
    tos_wifi_ap_t ap;
    if (api->wifi->ap_get(i, &ap) == 0)
      api->log(TOS_LOG_INFO, "WIFISCAN", "  ch%-2u %ddBm  %s", ap.channel, ap.rssi, ap.ssid);
  }
  return n;
}
