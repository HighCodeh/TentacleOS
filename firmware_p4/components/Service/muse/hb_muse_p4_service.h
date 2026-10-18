#ifndef FIRMWARE_P4_COMPONENTS_SERVICE_MUSE_HB_MUSE_P4_SERVICE_H
#define FIRMWARE_P4_COMPONENTS_SERVICE_MUSE_HB_MUSE_P4_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef HB_MUSE_P4_HOST_TEST
#include "native_host.h"
#else
#include "esp_err.h"
#endif
/* Native task adapter; no SDK credential ever enters P4. Only local physical
 * UI may start these operations. Starting a task is not connection readiness. */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HB_MUSE_P4_OFF,
  HB_MUSE_P4_CHAT,
  HB_MUSE_P4_SETUP,
  HB_MUSE_P4_TOKEN
} hb_muse_p4_operation_t;
typedef struct {
  bool active, ready, mic, error, confirming;
  uint8_t level, phase;
  uint64_t ticket;
  char status[64], detail[128], reply[1024];
} hb_muse_p4_view_t;
esp_err_t hb_muse_p4_start(hb_muse_p4_operation_t operation);
void hb_muse_p4_stop(void);
void hb_muse_p4_key(bool pressed); /* actual OK press/release only */
void hb_muse_p4_view(hb_muse_p4_view_t *out);
bool hb_muse_p4_is_active(void); /* includes cleanup, setup and token window */

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_P4_COMPONENTS_SERVICE_MUSE_HB_MUSE_P4_SERVICE_H
