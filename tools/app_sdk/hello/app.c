// Minimal TentacleOS test app. Links against nothing in the firmware: it
// receives the host API pointer at entry and reaches the device through it.
#include "tos_api.h"

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  api->log(TOS_LOG_INFO, "APP", "hello from a real .elf running in PSRAM");
  api->led_set(0, 255, 0);
  return 42;
}
