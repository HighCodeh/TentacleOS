#ifndef TEST_MQTT_CLIENT_H
#define TEST_MQTT_CLIENT_H
#include <stdbool.h>
#include "esp_err.h"
typedef void *esp_mqtt_client_handle_t;
typedef const char *esp_event_base_t;
typedef struct {
  struct {
    struct { const char *uri; } address;
    struct { esp_err_t (*crt_bundle_attach)(void *); } verification;
  } broker;
  struct { const char *username; struct { const char *password; } authentication; } credentials;
  struct { int timeout_ms, reconnect_timeout_ms; } network;
  struct { int keepalive; } session;
  struct { int priority, stack_size; } task;
  struct { int size; } buffer;
} esp_mqtt_client_config_t;
typedef struct {
  esp_mqtt_client_handle_t client;
  int msg_id;
  char *data;
  int data_len;
  int total_data_len;
  int current_data_offset;
} esp_mqtt_event_t;
typedef esp_mqtt_event_t *esp_mqtt_event_handle_t;
enum { MQTT_EVENT_ANY=-1, MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_ERROR,
       MQTT_EVENT_SUBSCRIBED, MQTT_EVENT_DATA };
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config);
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, int event,
                                        void (*callback)(void *, esp_event_base_t, int32_t, void *), void *arg);
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client);
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic,
                            const char *data, int length, int qos, int retain);
#endif
