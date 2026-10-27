// Long-running test app: blinks the LED green until the manager asks it to
// stop. Demonstrates the cooperative-stop lifecycle (api->should_stop).
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  api->log(TOS_LOG_INFO, "BLINK", "blink app started");
  int on = 0;
  while (!api->should_stop()) {
    on = !on;
    api->led_set(0, on ? 40 : 0, 0);
    api->delay_ms(300);
  }
  api->led_set(0, 0, 0);
  api->log(TOS_LOG_INFO, "BLINK", "blink app stopped");
  return 0;
}
