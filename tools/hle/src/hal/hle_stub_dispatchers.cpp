#include <cstdint>

extern "C" {
#include "esp_err.h"
}

// Types from spi_protocol.h (HLE shim has no C5 include path for the interactive target).
typedef uint8_t spi_id_t;
typedef uint8_t spi_status_t;
#define SPI_STATUS_OK          0x00
#define SPI_STATUS_UNSUPPORTED 0x03

extern "C" {

spi_status_t wifi_dispatcher_execute(spi_id_t id, const uint8_t *payload, uint8_t len,
                                     uint8_t *out_resp_payload, uint8_t *out_resp_len) {
    (void)id; (void)payload; (void)len; (void)out_resp_payload;
    if (out_resp_len) *out_resp_len = 0;
    return SPI_STATUS_UNSUPPORTED;
}
spi_status_t bt_dispatcher_execute(spi_id_t id, const uint8_t *payload, uint8_t len,
                                   uint8_t *out_resp_payload, uint8_t *out_resp_len) {
    (void)id; (void)payload; (void)len; (void)out_resp_payload;
    if (out_resp_len) *out_resp_len = 0;
    return SPI_STATUS_UNSUPPORTED;
}

// Meshtastic GATT/TCP stubs — NimBLE/TCP unavailable in HLE.
// Signatures match firmware_c5/.../meshtastic_gatt.h and meshtastic_tcp.h exactly.
// Transport functions come from compiled meshtastic_transport.c / meshcore_transport.c.
esp_err_t meshtastic_gatt_init(uint32_t node_num) { (void)node_num; return ESP_OK; }
void meshtastic_gatt_stop(void) {}
bool meshtastic_gatt_is_running(void) { return false; }
bool meshtastic_gatt_is_connected(void) { return false; }
bool meshtastic_gatt_is_fromnum_subscribed(void) { return false; }
bool meshtastic_gatt_is_logradio_subscribed(void) { return false; }
void meshtastic_gatt_enqueue_fromradio(const uint8_t *frame, uint16_t len) { (void)frame; (void)len; }
void meshtastic_gatt_notify_log(const uint8_t *chunk, uint16_t len) { (void)chunk; (void)len; }

esp_err_t meshtastic_tcp_init(uint32_t node_num) { (void)node_num; return ESP_ERR_NOT_SUPPORTED; }
void meshtastic_tcp_stop(void) {}
bool meshtastic_tcp_is_running(void) { return false; }
uint8_t meshtastic_tcp_get_client_count(void) { return 0; }
void meshtastic_tcp_send_fromradio(const uint8_t *frame, uint16_t len) { (void)frame; (void)len; }

// MeshCore GATT stubs — NimBLE unavailable in HLE.
// Signatures match firmware_c5/.../meshcore_gatt.h exactly.
esp_err_t meshcore_gatt_init(const char *name_prefix, uint32_t pin) {
    (void)name_prefix; (void)pin;
    return ESP_OK;
}
void meshcore_gatt_stop(void) {}
bool meshcore_gatt_is_running(void) { return false; }
bool meshcore_gatt_is_connected(void) { return false; }
bool meshcore_gatt_is_subscribed(void) { return false; }
void meshcore_gatt_notify(const uint8_t *frame, uint16_t len) { (void)frame; (void)len; }

} // extern "C"
