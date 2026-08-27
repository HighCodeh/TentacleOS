// Test app for the resource manager. Holds a WiFi monitor session (radio-rx) and
// loops until asked to stop OR until the radio is preempted by the UI/companion.
// Use it to see `resources` show wifi held by `app`, to watch a UI attack preempt
// it (resource_lost fires), and to confirm the session is reclaimed on exit.
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;

  uint32_t sid = 0;
  int e = api->wifi->deauth_detect(&sid); // radio-rx monitor session
  if (e != 0) {
    api->log(TOS_LOG_ERROR, "RADIOHOG", "start failed: %d (radio busy, or radio-rx not granted)", e);
    return 1;
  }
  api->log(TOS_LOG_INFO, "RADIOHOG", "holding wifi session 0x%x - run `resources` now", (unsigned)sid);

  while (!api->should_stop()) {
    if (api->resource_lost()) {
      api->log(TOS_LOG_WARN, "RADIOHOG", "radio preempted by UI/companion - stopping");
      break;
    }
    api->delay_ms(150); // yields and feeds the watchdog; short so stop is prompt
  }

  // No session_stop here on purpose: the manager reclaims the radio on exit, and
  // doing SPI right before the app returns opens a force-kill-mid-SPI window.
  api->log(TOS_LOG_INFO, "RADIOHOG", "exiting - manager will reclaim the radio");
  return 0;
}
