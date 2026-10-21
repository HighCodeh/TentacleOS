// Queue text on the native mesh selected on the device before launching this app.
#include "tos_api.h"

#define LORA_TX_ABI_MINOR 14

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (api->abi_major != TOS_ABI_VERSION_MAJOR || api->abi_minor < LORA_TX_ABI_MINOR ||
      api->lora == NULL || api->lora->mesh_send_text == NULL)
    return 1;
  if (api->should_stop())
    return 0;
  esp_err_t error = api->lora->mesh_send_text("Hello from High Boy");
  api->log(TOS_LOG_INFO, "LORA", "queue acceptance status=%d", error);
  return error == ESP_OK ? 0 : 1;
}
