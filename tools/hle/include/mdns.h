#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mdns_init(void);
esp_err_t mdns_free(void);
esp_err_t mdns_hostname_set(const char *hostname);
esp_err_t mdns_instance_name_set(const char *name);
esp_err_t mdns_service_add(const char *instance, const char *service, const char *proto, uint16_t port, const void *txt, size_t txt_len);
esp_err_t mdns_service_txt_set(const char *service, const char *proto, char **txt, uint8_t txt_count);

#ifdef __cplusplus
}
#endif
