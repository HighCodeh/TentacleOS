// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "host_link_sec.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

#include "host_link.h"

static const char *TAG = "HOST_LINK_SEC";

#define HL_NVS_NAMESPACE "hostlink"
#define HL_NVS_PSK_KEY   "psk"

#define HL_MAC_SIZE 16 // truncated HMAC length on the wire (HOST_LINK_MAC_SIZE)

// HKDF info labels: distinct per direction so a captured frame can't be
// reflected back on the other key.
static const char HL_INFO_A2D[] = "tos-host-a2d";
static const char HL_INFO_D2A[] = "tos-host-d2a";

static uint8_t s_psk[HOST_LINK_PSK_SIZE];
static bool s_psk_loaded = false;

static bool s_authed = false;
static uint8_t s_k_a2d[HOST_LINK_KEY_SIZE];
static uint8_t s_k_d2a[HOST_LINK_KEY_SIZE];
static uint32_t s_rx_counter = 0;
static bool s_rx_valid = false;

static const mbedtls_md_info_t *md_sha256(void) {
  return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
}

// HMAC-SHA256 truncated to HL_MAC_SIZE.
static void hmac_trunc(
    const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out_mac) {
  uint8_t full[32];
  mbedtls_md_hmac(md_sha256(), key, key_len, data, data_len, full);
  memcpy(out_mac, full, HL_MAC_SIZE);
}

// Constant-time equality to avoid leaking MAC mismatch position via timing.
static bool ct_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++)
    diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

static esp_err_t persist_psk(void) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(HL_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = nvs_set_blob(h, HL_NVS_PSK_KEY, s_psk, sizeof(s_psk));
  if (err == ESP_OK)
    err = nvs_commit(h);
  nvs_close(h);
  return err;
}

esp_err_t host_link_sec_init(void) {
  host_link_sec_reset();

  nvs_handle_t h;
  esp_err_t err = nvs_open(HL_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    return err;
  }

  size_t len = sizeof(s_psk);
  err = nvs_get_blob(h, HL_NVS_PSK_KEY, s_psk, &len);
  nvs_close(h);

  if (err == ESP_OK && len == sizeof(s_psk)) {
    s_psk_loaded = true;
    ESP_LOGI(TAG, "PSK loaded from NVS");
    return ESP_OK;
  }

  // First boot (or wrong size): mint a fresh PSK from the hardware RNG.
  esp_fill_random(s_psk, sizeof(s_psk));
  s_psk_loaded = true;
  err = persist_psk();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to persist PSK: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGW(TAG, "Generated a new pairing PSK (provision it to the app)");
  return ESP_OK;
}

bool host_link_sec_is_authenticated(void) {
  return s_authed;
}

void host_link_sec_reset(void) {
  s_authed = false;
  s_rx_valid = false;
  s_rx_counter = 0;
  mbedtls_platform_zeroize(s_k_a2d, sizeof(s_k_a2d));
  mbedtls_platform_zeroize(s_k_d2a, sizeof(s_k_d2a));
}

esp_err_t host_link_sec_handle_hello(
    const uint8_t *payload, uint16_t plen, uint8_t *ack_out, size_t ack_cap, size_t *out_len) {
  if (!s_psk_loaded)
    return ESP_ERR_INVALID_STATE;
  // HELLO payload = [host_ver u8][client_nonce[16]]
  if (payload == NULL || plen < 1 + HOST_LINK_NONCE_SIZE)
    return ESP_ERR_INVALID_ARG;

  uint8_t host_ver = payload[0];
  const uint8_t *client_nonce = payload + 1;

  uint8_t server_nonce[HOST_LINK_NONCE_SIZE];
  esp_fill_random(server_nonce, sizeof(server_nonce));

  // salt = client_nonce || server_nonce (shared transcript material).
  uint8_t salt[HOST_LINK_NONCE_SIZE * 2];
  memcpy(salt, client_nonce, HOST_LINK_NONCE_SIZE);
  memcpy(salt + HOST_LINK_NONCE_SIZE, server_nonce, HOST_LINK_NONCE_SIZE);

  int rc = mbedtls_hkdf(md_sha256(),
                        salt,
                        sizeof(salt),
                        s_psk,
                        sizeof(s_psk),
                        (const uint8_t *)HL_INFO_A2D,
                        sizeof(HL_INFO_A2D) - 1,
                        s_k_a2d,
                        sizeof(s_k_a2d));
  if (rc == 0)
    rc = mbedtls_hkdf(md_sha256(),
                      salt,
                      sizeof(salt),
                      s_psk,
                      sizeof(s_psk),
                      (const uint8_t *)HL_INFO_D2A,
                      sizeof(HL_INFO_D2A) - 1,
                      s_k_d2a,
                      sizeof(s_k_d2a));
  if (rc != 0) {
    ESP_LOGE(TAG, "HKDF failed: -0x%04x", -rc);
    host_link_sec_reset();
    return ESP_FAIL;
  }

  // mac_psk = HMAC(PSK, client_nonce || server_nonce) — proves PSK possession.
  uint8_t mac_psk[HL_MAC_SIZE];
  hmac_trunc(s_psk, sizeof(s_psk), salt, sizeof(salt), mac_psk);

  uint8_t device_id[HOST_LINK_DEVICE_ID_SIZE];
  esp_read_mac(device_id, ESP_MAC_BASE);

  // HELLO_ACK payload = [host_ver][server_nonce[16]][device_id[6]][mac_psk[16]]
  size_t need = 1 + HOST_LINK_NONCE_SIZE + HOST_LINK_DEVICE_ID_SIZE + HL_MAC_SIZE;
  if (ack_cap < need)
    return ESP_ERR_INVALID_SIZE;

  size_t off = 0;
  ack_out[off++] = host_ver;
  memcpy(ack_out + off, server_nonce, HOST_LINK_NONCE_SIZE);
  off += HOST_LINK_NONCE_SIZE;
  memcpy(ack_out + off, device_id, HOST_LINK_DEVICE_ID_SIZE);
  off += HOST_LINK_DEVICE_ID_SIZE;
  memcpy(ack_out + off, mac_psk, HL_MAC_SIZE);
  off += HL_MAC_SIZE;
  *out_len = off;

  // Keys are live; counters reset (inbound baseline set by the first authed frame).
  s_rx_valid = false;
  s_rx_counter = 0;
  s_authed = true;
  ESP_LOGI(TAG, "Handshake complete; session authenticated");
  return ESP_OK;
}

bool host_link_sec_verify_inbound(const uint8_t *span,
                                  size_t span_len,
                                  const uint8_t *mac,
                                  uint32_t counter) {
  if (!s_authed)
    return false;

  uint8_t expect[HL_MAC_SIZE];
  hmac_trunc(s_k_a2d, sizeof(s_k_a2d), span, span_len, expect);
  if (!ct_equal(expect, mac, HL_MAC_SIZE))
    return false;

  // Replay protection: strictly-increasing counter after the first authed frame.
  if (s_rx_valid && counter <= s_rx_counter)
    return false;

  s_rx_counter = counter;
  s_rx_valid = true;
  return true;
}

void host_link_sec_sign_outbound(const uint8_t *span, size_t span_len, uint8_t *out_mac) {
  hmac_trunc(s_k_d2a, sizeof(s_k_d2a), span, span_len, out_mac);
}

esp_err_t host_link_sec_get_psk_hex(char *out, size_t out_cap) {
  if (!s_psk_loaded)
    return ESP_ERR_INVALID_STATE;
  if (out_cap < HOST_LINK_PSK_HEX_SIZE)
    return ESP_ERR_INVALID_SIZE;
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(s_psk); i++) {
    out[i * 2] = hex[s_psk[i] >> 4];
    out[i * 2 + 1] = hex[s_psk[i] & 0x0F];
  }
  out[sizeof(s_psk) * 2] = '\0';
  return ESP_OK;
}

esp_err_t host_link_sec_regenerate_psk(void) {
  esp_fill_random(s_psk, sizeof(s_psk));
  s_psk_loaded = true;
  host_link_sec_reset();
  esp_err_t err = persist_psk();
  if (err == ESP_OK)
    ESP_LOGW(TAG, "PSK regenerated; existing pairings invalidated");
  return err;
}
