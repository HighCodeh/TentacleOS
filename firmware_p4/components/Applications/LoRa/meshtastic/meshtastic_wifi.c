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

#include "meshtastic_wifi.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "meshtastic_mesh.h"
#include "meshtastic_phoneapi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "MESH_WIFI";


#define MT_START1       0x94
#define MT_START2       0xC3
#define MT_HEADER_LEN   4
#define MT_PORT         4403


#define MT_CONFIG_NONCE     69420
#define MT_NODE_INFO_NONCE  69421


static uint32_t s_node_num = 0;
static int      s_server_fd = -1;
static int      s_client_fd = -1;
static bool     s_is_connected = false;
static uint8_t  s_handshake_stage = 0;
static uint32_t s_packet_id = 100;
static uint32_t s_last_telemetry_ms = 0;
#define TELEMETRY_INTERVAL_MS  10000


static uint16_t encode_varint(uint8_t *buf, uint64_t val)
{
    uint16_t pos = 0;
    do {
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val) byte |= 0x80;
        buf[pos++] = byte;
    } while (val);
    return pos;
}

static uint16_t encode_field_varint(uint8_t *buf, uint8_t field_num, uint64_t val)
{
    uint16_t pos = 0;
    buf[pos++] = (field_num << 3) | 0;
    pos += encode_varint(&buf[pos], val);
    return pos;
}

static uint16_t encode_field_bytes(uint8_t *buf, uint8_t field_num,
                                   const uint8_t *data, uint16_t len)
{
    uint16_t pos = 0;
    buf[pos++] = (field_num << 3) | 2;
    pos += encode_varint(&buf[pos], len);
    memcpy(&buf[pos], data, len);
    pos += len;
    return pos;
}

static uint16_t build_my_info(uint8_t *buf, uint32_t radio_id, uint32_t node_num)
{
    uint8_t my_info[64];
    uint16_t mi_len = 0;
    mi_len += encode_field_varint(&my_info[mi_len], 1, node_num);
    mi_len += encode_field_varint(&my_info[mi_len], 8, 1);
    mi_len += encode_field_varint(&my_info[mi_len], 11, 30200);


    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    mi_len += encode_field_bytes(&my_info[mi_len], 12, mac, 6);

    const char *env = "highboy-s3";
    mi_len += encode_field_bytes(&my_info[mi_len], 13,
                                 (const uint8_t *)env, (uint16_t)strlen(env));
    mi_len += encode_field_varint(&my_info[mi_len], 15, 1);

    uint16_t pos = 0;
    pos += encode_field_varint(&buf[pos], 1, radio_id);
    pos += encode_field_bytes(&buf[pos], 3, my_info, mi_len);
    return pos;
}


static uint16_t build_node_info(uint8_t *buf, uint32_t radio_id, uint32_t node_num)
{

    uint8_t user[80];
    uint16_t u_len = 0;

    char id_str[12];
    snprintf(id_str, sizeof(id_str), "!%08lx", (unsigned long)node_num);
    u_len += encode_field_bytes(&user[u_len], 1, (const uint8_t *)id_str, (uint16_t)strlen(id_str));

    const char *long_name = "Highboy S3";
    u_len += encode_field_bytes(&user[u_len], 2, (const uint8_t *)long_name, (uint16_t)strlen(long_name));

    const char *short_name = "HBS3";
    u_len += encode_field_bytes(&user[u_len], 3, (const uint8_t *)short_name, (uint16_t)strlen(short_name));

    u_len += encode_field_varint(&user[u_len], 5, 255);
    u_len += encode_field_varint(&user[u_len], 7, 0);


    uint8_t node_info[128];
    uint16_t ni_len = 0;
    ni_len += encode_field_varint(&node_info[ni_len], 1, node_num);
    ni_len += encode_field_bytes(&node_info[ni_len], 2, user, u_len);


    uint32_t uptime = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    uint32_t last_heard = 1776192000UL + uptime;
    node_info[ni_len++] = (5 << 3) | 5;
    memcpy(&node_info[ni_len], &last_heard, 4);
    ni_len += 4;


    uint16_t pos = 0;
    pos += encode_field_varint(&buf[pos], 1, radio_id);
    pos += encode_field_bytes(&buf[pos], 4, node_info, ni_len);
    return pos;
}


static uint16_t build_metadata(uint8_t *buf, uint32_t radio_id)
{
    uint8_t meta[64];
    uint16_t m_len = 0;

    const char *fw_ver = "2.6.0.0";
    m_len += encode_field_bytes(&meta[m_len], 1, (const uint8_t *)fw_ver, (uint16_t)strlen(fw_ver));
    m_len += encode_field_varint(&meta[m_len], 4, 255);
    m_len += encode_field_varint(&meta[m_len], 11, 1);

    uint16_t pos = 0;
    pos += encode_field_varint(&buf[pos], 1, radio_id);
    pos += encode_field_bytes(&buf[pos], 13, meta, m_len);
    return pos;
}


static uint16_t encode_field_fixed32(uint8_t *buf, uint8_t field_num, uint32_t val)
{
    uint16_t pos = 0;
    buf[pos++] = (field_num << 3) | 5;
    memcpy(&buf[pos], &val, 4);
    pos += 4;
    return pos;
}

static uint16_t encode_field_float(uint8_t *buf, uint8_t field_num, float val)
{
    uint16_t pos = 0;
    buf[pos++] = (field_num << 3) | 5;
    memcpy(&buf[pos], &val, 4);
    pos += 4;
    return pos;
}


static uint16_t build_telemetry_packet(uint8_t *buf, uint32_t radio_id,
                                        uint32_t node_num, uint32_t pkt_id,
                                        float temp_c, float humidity_pct, float pressure_hpa)
{

    uint8_t env[32];
    uint16_t env_len = 0;
    env_len += encode_field_float(&env[env_len], 1, temp_c);
    env_len += encode_field_float(&env[env_len], 2, humidity_pct);
    env_len += encode_field_float(&env[env_len], 3, pressure_hpa);


    uint8_t telem[48];
    uint16_t t_len = 0;
    uint32_t uptime_sec = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    uint32_t fake_unix = 1776192000UL + uptime_sec;
    t_len += encode_field_varint(&telem[t_len], 1, fake_unix);
    t_len += encode_field_bytes(&telem[t_len], 3, env, env_len);


    uint8_t data[64];
    uint16_t d_len = 0;
    d_len += encode_field_varint(&data[d_len], 1, 67);
    d_len += encode_field_bytes(&data[d_len], 2, telem, t_len);


    uint8_t pkt[128];
    uint16_t p_len = 0;
    p_len += encode_field_fixed32(&pkt[p_len], 1, node_num);
    p_len += encode_field_fixed32(&pkt[p_len], 2, node_num);
    p_len += encode_field_bytes(&pkt[p_len], 4, data, d_len);
    p_len += encode_field_fixed32(&pkt[p_len], 6, pkt_id);


    uint16_t pos = 0;
    pos += encode_field_varint(&buf[pos], 1, radio_id);
    pos += encode_field_bytes(&buf[pos], 2, pkt, p_len);
    return pos;
}

static uint16_t build_config_complete(uint8_t *buf, uint32_t radio_id, uint32_t config_id)
{
    uint16_t pos = 0;
    pos += encode_field_varint(&buf[pos], 1, radio_id);
    pos += encode_field_varint(&buf[pos], 7, config_id);
    return pos;
}


static uint32_t decode_varint(const uint8_t *buf, uint16_t len, uint16_t *out_bytes_read)
{
    uint32_t val = 0;
    uint16_t shift = 0;
    uint16_t i = 0;
    while (i < len && i < 5) {
        val |= (uint32_t)(buf[i] & 0x7F) << shift;
        if (!(buf[i] & 0x80)) {
            i++;
            break;
        }
        shift += 7;
        i++;
    }
    if (out_bytes_read) *out_bytes_read = i;
    return val;
}


static void send_framed(int fd, const uint8_t *pb_data, uint16_t pb_len)
{
    uint8_t header[MT_HEADER_LEN];
    header[0] = MT_START1;
    header[1] = MT_START2;
    header[2] = (uint8_t)(pb_len >> 8);
    header[3] = (uint8_t)(pb_len & 0xFF);

    send(fd, header, MT_HEADER_LEN, 0);
    send(fd, pb_data, pb_len, 0);
    ESP_LOGI(TAG, "Sent %d bytes (framed)", pb_len);
}


static esp_err_t start_wifi_ap(uint32_t node_num)
{
    esp_err_t ret;

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) return ret;

    char ssid[32];
    snprintf(ssid, sizeof(ssid), "Highboy_%04x", (uint16_t)(node_num & 0xFFFF));

    wifi_config_t wifi_config = {
        .ap = {
            .channel = 1,
            .max_connection = 2,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(ssid);

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_start();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "WiFi AP started - SSID: %s, IP: 192.168.4.1, port: %d", ssid, MT_PORT);

    if (mdns_init() == ESP_OK) {
        char hostname[32];
        snprintf(hostname, sizeof(hostname), "highboy-%04lx",
                 (unsigned long)(node_num & 0xFFFF));
        mdns_hostname_set(hostname);
        mdns_instance_name_set(ssid);
        mdns_service_add(NULL, "_meshtastic", "_tcp", MT_PORT, NULL, 0);
        ESP_LOGI(TAG, "mDNS: %s._meshtastic._tcp.local:%d", hostname, MT_PORT);
    } else {
        ESP_LOGW(TAG, "mDNS init failed (continuing without service discovery)");
    }

    return ESP_OK;
}


static esp_err_t start_tcp_server(void)
{
    s_server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_server_fd < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        return ESP_FAIL;
    }

    int opt = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(MT_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        close(s_server_fd);
        return ESP_FAIL;
    }

    if (listen(s_server_fd, 1) < 0) {
        ESP_LOGE(TAG, "listen() failed: %d", errno);
        close(s_server_fd);
        return ESP_FAIL;
    }


    int flags = fcntl(s_server_fd, F_GETFL, 0);
    fcntl(s_server_fd, F_SETFL, flags | O_NONBLOCK);

    ESP_LOGI(TAG, "TCP server listening on port %d", MT_PORT);
    return ESP_OK;
}


esp_err_t meshtastic_wifi_init(uint32_t node_num)
{
    s_node_num = node_num;

    esp_err_t ret = start_wifi_ap(node_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi AP failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = start_tcp_server();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCP server failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

void meshtastic_wifi_poll(void)
{

    if (s_client_fd < 0) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        s_client_fd = accept(s_server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (s_client_fd >= 0) {
            s_is_connected = true;
            s_handshake_stage = 0;
            ESP_LOGI(TAG, "Client connected de %s",
                     inet_ntoa(client_addr.sin_addr));


            int flags = fcntl(s_client_fd, F_GETFL, 0);
            fcntl(s_client_fd, F_SETFL, flags | O_NONBLOCK);
        }
        return;
    }


    uint8_t rx_buf[512];
    int rx_len = recv(s_client_fd, rx_buf, sizeof(rx_buf), 0);

    if (rx_len > 0) {
        ESP_LOGI(TAG, "Recebidos %d bytes:", rx_len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buf, rx_len, ESP_LOG_INFO);


        for (int i = 0; i < rx_len - 1; i++) {
            if (rx_buf[i] == MT_START1 && rx_buf[i + 1] == MT_START2 && i + 3 < rx_len) {
                uint16_t pb_len = ((uint16_t)rx_buf[i + 2] << 8) | rx_buf[i + 3];
                uint8_t *pb_data = &rx_buf[i + MT_HEADER_LEN];

                ESP_LOGI(TAG, "Protobuf packet: %d bytes", pb_len);


                phoneapi_on_toradio(pb_data, pb_len);
                break;
            }
        }
    }


    if (s_is_connected && s_client_fd >= 0) {
        uint8_t frame[512];
        uint16_t flen;
        while ((flen = phoneapi_poll_fromradio(frame, sizeof(frame))) > 0) {
            send_framed(s_client_fd, frame, flen);
            vTaskDelay(1);
        }
    }


    if (rx_len == 0) {

        ESP_LOGI(TAG, "Client disconnected");
        close(s_client_fd);
        s_client_fd = -1;
        s_is_connected = false;
    }

}

bool meshtastic_wifi_is_connected(void)
{
    return s_is_connected;
}

int meshtastic_wifi_get_client_fd(void)
{
    return s_client_fd;
}
