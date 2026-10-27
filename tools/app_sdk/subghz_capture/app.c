// One bounded capture through the shared native ABI; requests only radio-rx.
#include "tos_api.h"

#define SUBGHZ_ABI_MINOR 13
#define SUBGHZ_FREQ_HZ 433920000
#define SUBGHZ_PRESET_OOK800 6
#define SUBGHZ_TIMEOUT_MS 500

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (api->abi_major != TOS_ABI_VERSION_MAJOR || api->abi_minor < SUBGHZ_ABI_MINOR ||
      api->subghz == NULL || api->subghz->receive == NULL)
    return 1;
  if (api->should_stop())
    return 0;
  tos_subghz_signal_t signal;
  esp_err_t error = api->subghz->receive(&signal, SUBGHZ_FREQ_HZ, SUBGHZ_PRESET_OOK800,
                                       SUBGHZ_TIMEOUT_MS);
  if (error == ESP_OK)
    api->log(TOS_LOG_INFO, "SUBGHZ", "%s decoded=%u value=%lu", signal.protocol,
             (unsigned)signal.decoded, (unsigned long)signal.value);
  else
    api->log(TOS_LOG_INFO, "SUBGHZ", "capture status=%d", error);
  return 0;
}
