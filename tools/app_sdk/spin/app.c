// Misbehaving app: yields (so the watchdog is fed) but never checks should_stop,
// so appstop must force-kill it after the cooperative timeout.
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  api->log(TOS_LOG_WARN, "SPIN", "spinning forever, ignoring stop requests");
  for (;;)
    api->delay_ms(200);
  return 0;
}
