// Test app for the resource manager, BLE side. Starts the BadBLE HID keyboard,
// which holds RES_BLE. Use it to confirm the companion-over-BLE reservation:
// with `hostlink ble on`, hid_start returns BUSY and the companion survives;
// with the companion off (or on USB), it advertises and can inject a key.
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;

  int e = api->ble->hid_start();
  if (e != 0) {
    api->log(TOS_LOG_ERROR, "BADBLE", "hid_start failed: %d (BLE busy = companion holds it)", e);
    return 1;
  }
  api->log(TOS_LOG_INFO, "BADBLE", "BLE HID advertising - run `resources` (ble held by app)");

  int ticks = 0;
  while (!api->should_stop() && !api->resource_lost() && ticks < 200) {
    if (api->ble->hid_is_connected()) {
      api->log(TOS_LOG_INFO, "BADBLE", "target connected - sending 'a'");
      api->ble->hid_send_key(0, 0x04); // HID usage 0x04 = 'a', no modifier
      api->ble->hid_send_key(0, 0x00); // release
      api->delay_ms(1000);
    }
    api->delay_ms(300);
    ticks++;
  }

  if (api->resource_lost())
    api->log(TOS_LOG_WARN, "BADBLE", "BLE preempted - stopping");
  api->ble->hid_stop();
  api->log(TOS_LOG_INFO, "BADBLE", "released ble, exiting");
  return 0;
}
