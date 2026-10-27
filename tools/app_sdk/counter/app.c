#include "tos_api.h"
static int s_count = 0;                    // writable global -> .bss
static int s_hist[4];                      // writable array -> .bss
static const char *const msgs[3] = {"one", "two", "three"}; // ptr array -> .data + relocs
int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc; (void)argv;
  for (int i = 0; i < 3; i++) {
    s_count++;
    s_hist[i] = s_count;
    api->log(TOS_LOG_INFO, "COUNT", msgs[i]);
  }
  api->led_set(0, 0, (uint8_t)(s_count * 20));
  return s_count + s_hist[2];
}
