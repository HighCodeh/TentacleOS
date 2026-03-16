// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "port_scan.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PORT_SCANNER";

#define PORT_SCAN_TIMEOUT_MS 20000

static int fetch_scan_results(scan_result_t *results, int max_results) {
    spi_header_t resp_hdr;
    uint8_t resp_buf[SPI_MAX_PAYLOAD];

    // Fetch count
    uint16_t index = SPI_DATA_INDEX_COUNT;
    esp_err_t ret = spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
        (const uint8_t *)&index, sizeof(index),
        &resp_hdr, resp_buf, 5000);

    if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
        ESP_LOGE(TAG, "Failed to fetch result count");
        return 0;
    }

    uint16_t total = resp_buf[1] | (resp_buf[2] << 8);
    if (total > max_results) total = max_results;

    ESP_LOGI(TAG, "Fetching %d results from C5", total);

    int count = 0;
    for (uint16_t i = 0; i < total; i++) {
        ret = spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
            (const uint8_t *)&i, sizeof(i),
            &resp_hdr, resp_buf, 2000);

        if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
            ESP_LOGW(TAG, "Failed to fetch result %d", i);
            continue;
        }

        spi_port_scan_result_t *rec = (spi_port_scan_result_t *)&resp_buf[1];
        strncpy(results[count].ip_str, rec->ip_str, 16);
        results[count].port = rec->port;
        results[count].protocol = (rec->protocol == 0) ? PROTO_TCP : PROTO_UDP;
        results[count].status = (rec->status == 0) ? STATUS_OPEN : STATUS_OPEN_FILTERED;
        strncpy(results[count].banner, rec->banner, MAX_BANNER_LEN);
        count++;
    }

    return count;
}

int port_scan_target_range(const char *target_ip, int start_port, int end_port,
                           scan_result_t *results, int max_results) {
    spi_port_scan_range_req_t req = {0};
    strncpy(req.ip, target_ip, sizeof(req.ip) - 1);
    req.start_port = start_port;
    req.end_port = end_port;
    req.max_results = max_results;

    spi_header_t resp_hdr;
    uint8_t resp_buf[SPI_MAX_PAYLOAD];

    esp_err_t ret = spi_bridge_send_command(SPI_ID_WIFI_PORT_SCAN_TARGET_RANGE,
        (const uint8_t *)&req, sizeof(req),
        &resp_hdr, resp_buf, PORT_SCAN_TIMEOUT_MS);

    if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
        ESP_LOGE(TAG, "Target range scan failed");
        return 0;
    }

    return fetch_scan_results(results, max_results);
}

int port_scan_target_list(const char *target_ip, const int *port_list, int list_size,
                          scan_result_t *results, int max_results) {
    // Payload: ip[16] + max_results(2) + count(2) + ports(2*N)
    uint8_t payload[SPI_MAX_PAYLOAD];
    memset(payload, 0, sizeof(payload));

    size_t offset = 0;
    strncpy((char *)payload, target_ip, 16);
    offset += 16;

    uint16_t max_res = max_results;
    memcpy(&payload[offset], &max_res, 2);
    offset += 2;

    uint16_t count = list_size;
    memcpy(&payload[offset], &count, 2);
    offset += 2;

    // Cap port count to fit in payload
    int max_ports = (SPI_MAX_PAYLOAD - offset) / 2;
    if (list_size > max_ports) list_size = max_ports;

    for (int i = 0; i < list_size; i++) {
        uint16_t p = port_list[i];
        memcpy(&payload[offset], &p, 2);
        offset += 2;
    }

    spi_header_t resp_hdr;
    uint8_t resp_buf[SPI_MAX_PAYLOAD];

    esp_err_t ret = spi_bridge_send_command(SPI_ID_WIFI_PORT_SCAN_TARGET_LIST,
        payload, offset,
        &resp_hdr, resp_buf, PORT_SCAN_TIMEOUT_MS);

    if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
        ESP_LOGE(TAG, "Target list scan failed");
        return 0;
    }

    return fetch_scan_results(results, max_results);
}

int port_scan_network_range_using_port_range(const char *start_ip, const char *end_ip,
                                             int start_port, int end_port,
                                             scan_result_t *results, int max_results) {
    spi_port_scan_network_req_t req = {0};
    strncpy(req.start_ip, start_ip, sizeof(req.start_ip) - 1);
    strncpy(req.end_ip, end_ip, sizeof(req.end_ip) - 1);
    req.start_port = start_port;
    req.end_port = end_port;
    req.max_results = max_results;
    req.scan_type = 0;

    spi_header_t resp_hdr;
    uint8_t resp_buf[SPI_MAX_PAYLOAD];

    esp_err_t ret = spi_bridge_send_command(SPI_ID_WIFI_PORT_SCAN_NETWORK,
        (const uint8_t *)&req, sizeof(req),
        &resp_hdr, resp_buf, PORT_SCAN_TIMEOUT_MS);

    if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
        ESP_LOGE(TAG, "Network range scan failed");
        return 0;
    }

    return fetch_scan_results(results, max_results);
}

int port_scan_network_range_using_port_list(const char *start_ip, const char *end_ip,
                                            const int *port_list, int list_size,
                                            scan_result_t *results, int max_results) {
    // Payload: start_ip[16] + end_ip[16] + max_results(2) + count(2) + ports(2*N)
    uint8_t payload[SPI_MAX_PAYLOAD];
    memset(payload, 0, sizeof(payload));

    size_t offset = 0;
    strncpy((char *)payload, start_ip, 16);
    offset += 16;
    strncpy((char *)&payload[offset], end_ip, 16);
    offset += 16;

    uint16_t max_res = max_results;
    memcpy(&payload[offset], &max_res, 2);
    offset += 2;

    uint16_t count = list_size;
    memcpy(&payload[offset], &count, 2);
    offset += 2;

    int max_ports = (SPI_MAX_PAYLOAD - offset) / 2;
    if (list_size > max_ports) list_size = max_ports;

    for (int i = 0; i < list_size; i++) {
        uint16_t p = port_list[i];
        memcpy(&payload[offset], &p, 2);
        offset += 2;
    }

    spi_header_t resp_hdr;
    uint8_t resp_buf[SPI_MAX_PAYLOAD];

    esp_err_t ret = spi_bridge_send_command(SPI_ID_WIFI_PORT_SCAN_NETWORK,
        payload, offset,
        &resp_hdr, resp_buf, PORT_SCAN_TIMEOUT_MS);

    if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
        ESP_LOGE(TAG, "Network list scan failed");
        return 0;
    }

    return fetch_scan_results(results, max_results);
}

int port_scan_cidr_using_port_range(const char *base_ip, int cidr,
                                    int start_port, int end_port,
                                    scan_result_t *results, int max_results) {
    spi_port_scan_cidr_req_t req = {0};
    strncpy(req.base_ip, base_ip, sizeof(req.base_ip) - 1);
    req.cidr = cidr;
    req.scan_type = 0;
    req.start_port = start_port;
    req.end_port = end_port;
    req.max_results = max_results;

    spi_header_t resp_hdr;
    uint8_t resp_buf[SPI_MAX_PAYLOAD];

    esp_err_t ret = spi_bridge_send_command(SPI_ID_WIFI_PORT_SCAN_CIDR,
        (const uint8_t *)&req, sizeof(req),
        &resp_hdr, resp_buf, PORT_SCAN_TIMEOUT_MS);

    if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
        ESP_LOGE(TAG, "CIDR range scan failed");
        return 0;
    }

    return fetch_scan_results(results, max_results);
}

int port_scan_cidr_using_port_list(const char *base_ip, int cidr,
                                   const int *port_list, int list_size,
                                   scan_result_t *results, int max_results) {
    // Payload: base_ip[16] + cidr(1) + max_results(2) + count(2) + ports(2*N)
    uint8_t payload[SPI_MAX_PAYLOAD];
    memset(payload, 0, sizeof(payload));

    size_t offset = 0;
    strncpy((char *)payload, base_ip, 16);
    offset += 16;

    payload[offset++] = (uint8_t)cidr;

    uint16_t max_res = max_results;
    memcpy(&payload[offset], &max_res, 2);
    offset += 2;

    uint16_t count = list_size;
    memcpy(&payload[offset], &count, 2);
    offset += 2;

    int max_ports = (SPI_MAX_PAYLOAD - offset) / 2;
    if (list_size > max_ports) list_size = max_ports;

    for (int i = 0; i < list_size; i++) {
        uint16_t p = port_list[i];
        memcpy(&payload[offset], &p, 2);
        offset += 2;
    }

    spi_header_t resp_hdr;
    uint8_t resp_buf[SPI_MAX_PAYLOAD];

    esp_err_t ret = spi_bridge_send_command(SPI_ID_WIFI_PORT_SCAN_CIDR,
        payload, offset,
        &resp_hdr, resp_buf, PORT_SCAN_TIMEOUT_MS);

    if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
        ESP_LOGE(TAG, "CIDR list scan failed");
        return 0;
    }

    return fetch_scan_results(results, max_results);
}
