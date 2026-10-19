// Native RF discovery through the shared ABI. Requests only radio-rx.
#include "tos_api.h"

#define RF_ABI_MINOR 10
#define RF_TIMEOUT_MS 500

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (api->abi_major != TOS_ABI_VERSION_MAJOR || api->abi_minor < RF_ABI_MINOR ||
      api->nfc == NULL || api->rfid == NULL || api->nfc->poll == NULL || api->rfid->read == NULL)
    return 1;
  tos_nfc_tag_t nfc;
  esp_err_t error = api->nfc->poll(&nfc, RF_TIMEOUT_MS);
  api->log(TOS_LOG_INFO, "RFREAD", "NFC status=%d", error);
  if (api->should_stop())
    return 0;
  tos_rfid_tag_t rfid;
  error = api->rfid->read(&rfid, RF_TIMEOUT_MS);
  if (error == ESP_OK)
    api->log(TOS_LOG_INFO, "RFREAD", "LF %s %s", rfid.protocol, rfid.id);
  else
    api->log(TOS_LOG_INFO, "RFREAD", "LF status=%d", error);
  return 0;
}
