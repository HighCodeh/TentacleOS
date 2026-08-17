#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_event_base_t;
typedef int esp_event_loop_handle_t;
typedef int esp_event_handler_instance_t;

typedef void (*esp_event_handler_t)(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

#define ESP_EVENT_ANY_ID (-1)

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *ctx);
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t handler);
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, void *data, size_t len, unsigned int timeout);

#define WIFI_EVENT  ((esp_event_base_t)0x01)
#define IP_EVENT    ((esp_event_base_t)0x02)

#ifdef __cplusplus
}
#endif
