#include "wifi_sniffer.h"
#include "spi_bridge.h"
#include "storage_stream.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_sniffer";
static sniffer_stats_t cached_stats;
static wifi_sniffer_cb_t stream_cb = NULL;
static storage_stream_t capture_stream = NULL;

bool wifi_sniffer_stop_capture(void);

static void wifi_sniffer_stream_cb(spi_id_t id, const uint8_t *payload, uint8_t len) {
    (void)id;
    if (!payload || len < 3) return;
    const spi_wifi_sniffer_frame_t *frame = (const spi_wifi_sniffer_frame_t *)payload;
    uint16_t data_len = frame->len;
    if (data_len > (len - 3)) data_len = (len - 3);

    if (capture_stream) {
        storage_stream_write(capture_stream, frame->data, data_len);
    }

    if (stream_cb) {
        stream_cb(frame->data, data_len, frame->rssi, frame->channel);
    }
}

static void update_stats(void) {
    spi_header_t resp;
    uint16_t magic_stats = SPI_DATA_INDEX_STATS;
    spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_stats, 2, &resp, (uint8_t*)&cached_stats, 1000);
}

bool wifi_sniffer_start(sniff_type_t type, uint8_t channel) {
    uint8_t payload[2];
    payload[0] = (uint8_t)type;
    payload[1] = channel;
    memset(&cached_stats, 0, sizeof(cached_stats));
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_SNIFFER, payload, 2, NULL, NULL, 2000) == ESP_OK);
}

bool wifi_sniffer_start_stream(sniff_type_t type, uint8_t channel, wifi_sniffer_cb_t cb) {
    if (!cb) {
        return wifi_sniffer_start(type, channel);
    }
    stream_cb = cb;
    spi_bridge_register_stream_cb(SPI_ID_WIFI_APP_SNIFFER, wifi_sniffer_stream_cb);
    if (!wifi_sniffer_start(type, channel)) {
        spi_bridge_unregister_stream_cb(SPI_ID_WIFI_APP_SNIFFER);
        stream_cb = NULL;
        return false;
    }
    return true;
}

void wifi_sniffer_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
    spi_bridge_unregister_stream_cb(SPI_ID_WIFI_APP_SNIFFER);
    wifi_sniffer_stop_capture();
    stream_cb = NULL;
}

uint32_t wifi_sniffer_get_packet_count(void) {
    update_stats();
    return cached_stats.packets;
}

uint32_t wifi_sniffer_get_deauth_count(void) {
    // Stats are updated in packet_count call, usually UI calls these together
    return cached_stats.deauths;
}

uint32_t wifi_sniffer_get_buffer_usage(void) {
    return cached_stats.buffer_usage;
}

bool wifi_sniffer_handshake_captured(void) {
    return cached_stats.handshake_captured;
}

bool wifi_sniffer_pmkid_captured(void) {
    return cached_stats.pmkid_captured;
}

bool wifi_sniffer_start_capture(const char *path) {
    if (!path) return false;
    if (capture_stream) {
        storage_stream_close(capture_stream);
        capture_stream = NULL;
    }
    capture_stream = storage_stream_open(path, "wb");
    if (!capture_stream) {
        ESP_LOGE(TAG, "Failed to open capture file: %s", path);
        return false;
    }
    ESP_LOGI(TAG, "Capture started: %s", path);
    return true;
}

bool wifi_sniffer_stop_capture(void) {
    if (!capture_stream) return false;
    storage_stream_flush(capture_stream);
    storage_stream_close(capture_stream);
    capture_stream = NULL;
    ESP_LOGI(TAG, "Capture stopped");
    return true;
}

size_t wifi_sniffer_get_capture_size(void) {
    return storage_stream_bytes_written(capture_stream);
}

void wifi_sniffer_free_buffer(void) {
    spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_FREE_BUFFER, NULL, 0, NULL, NULL, 2000);
    cached_stats.buffer_usage = 0;
}

void wifi_sniffer_set_snaplen(uint16_t len) {
    uint8_t payload[2];
    memcpy(payload, &len, sizeof(len));
    spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_SET_SNAPLEN, payload, sizeof(payload), NULL, NULL, 2000);
}

void wifi_sniffer_set_verbose(bool verbose) {
    uint8_t payload = verbose ? 1 : 0;
    spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_SET_VERBOSE, &payload, 1, NULL, NULL, 2000);
}


void wifi_sniffer_clear_pmkid(void) {
    spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_CLEAR_PMKID, NULL, 0, NULL, NULL, 2000);
    cached_stats.pmkid_captured = false;
}

void wifi_sniffer_get_pmkid_bssid(uint8_t out_bssid[6]) {
    if (!out_bssid) return;
    spi_header_t resp;
    uint8_t payload[6] = {0};
    if (spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_GET_PMKID_BSSID, NULL, 0, &resp, payload, 1000) == ESP_OK) {
        memcpy(out_bssid, payload, 6);
    } else {
        memset(out_bssid, 0, 6);
    }
}

void wifi_sniffer_clear_handshake(void) {
    spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_CLEAR_HANDSHAKE, NULL, 0, NULL, NULL, 2000);
    cached_stats.handshake_captured = false;
}

void wifi_sniffer_get_handshake_bssid(uint8_t out_bssid[6]) {
    if (!out_bssid) return;
    spi_header_t resp;
    uint8_t payload[6] = {0};
    if (spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_GET_HANDSHAKE_BSSID, NULL, 0, &resp, payload, 1000) == ESP_OK) {
        memcpy(out_bssid, payload, 6);
    } else {
        memset(out_bssid, 0, 6);
    }
}
