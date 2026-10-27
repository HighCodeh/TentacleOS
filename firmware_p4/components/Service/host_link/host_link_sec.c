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

#include "firmware_crypto.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sodium.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

#include "host_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "HOST_LINK_SEC";

#define HL_NVS_NAMESPACE "hostlink"
#define HL_NVS_PSK_KEY   "psk"

#define HL_PAIR_WINDOW_MS    60000
#define HL_PAIR_PENDING_MS   30000
#define HL_US_PER_MS         1000
#define HL_CODE_MODULUS      1000000
#define HL_CODE_BITS         8
#define HL_PUBLIC_OFFSET     (1 + HOST_LINK_NONCE_SIZE)
#define HL_HELLO_PREFIX_SIZE (HL_PUBLIC_OFFSET + HOST_LINK_ECDH_SIZE)
#define HL_ACK_PREFIX_SIZE   (HL_PUBLIC_OFFSET + HOST_LINK_ECDH_SIZE + HOST_LINK_DEVICE_ID_SIZE)
#define HL_TRANSCRIPT_SIZE   (HL_HELLO_PREFIX_SIZE + HL_ACK_PREFIX_SIZE)
#define HL_CONTEXT_MAX       160
#define HL_FINISH_COUNTER    1
#define HL_MAC_SIZE          16 // truncated HMAC length on the wire (HOST_LINK_MAC_SIZE)

// HKDF info labels: distinct per direction so a captured frame can't be
// reflected back on the other key.
static const char HL_INFO_A2D[] = "tos-host-v2-a2d";
static const char HL_INFO_D2A[] = "tos-host-v2-d2a";

static const char HL_CLIENT_PROOF[] = "tos-host-v2-client";
static const char HL_SERVER_PROOF[] = "tos-host-v2-server";
static const char HL_INFO_CODE[] = "tos-host-v2-code";
static int64_t s_window_deadline;
static int64_t s_pending_deadline;
static uint32_t s_next_token;
static host_link_pairing_t s_pairing;

_Static_assert(HOST_LINK_ECDH_SIZE == crypto_scalarmult_curve25519_BYTES,
               "X25519 wire size mismatch");

static uint8_t s_psk[HOST_LINK_PSK_SIZE];
static bool s_psk_loaded = false;
static SemaphoreHandle_t s_crypto_lock = NULL;

static bool s_authed = false;
static uint8_t s_k_a2d[HOST_LINK_KEY_SIZE];
static uint8_t s_k_d2a[HOST_LINK_KEY_SIZE];
static uint32_t s_rx_counter = 0;
static bool s_rx_valid = false;

// HMAC-SHA256 truncated to HL_MAC_SIZE.
static bool hmac_trunc(
    const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out_mac) {
  uint8_t full[32] = {0};
  int rc = firmware_crypto_mac((firmware_crypto_span_t){key, key_len},
                               (firmware_crypto_span_t){data, data_len},
                               PSA_ALG_HMAC(PSA_ALG_SHA_256),
                               full);
  if (rc == 0)
    memcpy(out_mac, full, HL_MAC_SIZE);
  else
    memset(out_mac, 0, HL_MAC_SIZE);
  mbedtls_platform_zeroize(full, sizeof(full));
  return rc == 0;
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

static esp_err_t sec_init_locked(void) {
  host_link_sec_reset();
  s_window_deadline = 0;
  if (sodium_init() < 0) {
    ESP_LOGE(TAG, "Cannot initialize pairing cryptography");
    return ESP_FAIL;
  }

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

static void sec_reset_locked(void) {
  __atomic_store_n(&s_authed, false, __ATOMIC_RELEASE);
  memset(&s_pairing, 0, sizeof(s_pairing));
  s_pending_deadline = 0;
  s_rx_valid = false;
  s_rx_counter = 0;
  mbedtls_platform_zeroize(s_k_a2d, sizeof(s_k_a2d));
  mbedtls_platform_zeroize(s_k_d2a, sizeof(s_k_d2a));
}

static bool
transcript_proof(const char *label, const uint8_t *transcript, size_t length, uint8_t *out) {
  uint8_t context[HL_CONTEXT_MAX];
  size_t prefix = strlen(label);
  if (prefix > sizeof(context) || length > sizeof(context) - prefix)
    return false;
  memcpy(context, label, prefix);
  memcpy(context + prefix, transcript, length);
  return hmac_trunc(s_psk, sizeof(s_psk), context, prefix + length, out);
}

static int
derive_key(const uint8_t *secret, const uint8_t *transcript, const char *label, uint8_t *out) {
  uint8_t info[HL_CONTEXT_MAX];
  size_t prefix = strlen(label);
  if (prefix > sizeof(info) - HL_TRANSCRIPT_SIZE)
    return -1;
  memcpy(info, label, prefix);
  memcpy(info + prefix, transcript, HL_TRANSCRIPT_SIZE);
  firmware_crypto_hkdf_t params = {.secret = {secret, HOST_LINK_ECDH_SIZE},
                                   .salt = {s_psk, sizeof(s_psk)},
                                   .info = {info, prefix + HL_TRANSCRIPT_SIZE}};
  return firmware_crypto_hkdf(&params, out, HOST_LINK_KEY_SIZE);
}

static void expire_pairing(void) {
  if (s_pairing.token != 0 && esp_timer_get_time() >= s_pending_deadline)
    sec_reset_locked();
}

static esp_err_t sec_hello_locked(
    const uint8_t *payload, uint16_t plen, uint8_t *ack_out, size_t ack_cap, size_t *out_len) {
  if (out_len != NULL)
    *out_len = 0;
  if (!s_psk_loaded || esp_timer_get_time() >= s_window_deadline)
    return ESP_ERR_INVALID_STATE;
  if (payload == NULL || plen != HOST_LINK_HELLO_SIZE || payload[0] != HOST_LINK_PAIR_VERSION ||
      ack_out == NULL || out_len == NULL)
    return ESP_ERR_INVALID_ARG;
  if (ack_cap < HOST_LINK_HELLO_ACK_SIZE)
    return ESP_ERR_INVALID_SIZE;
  sec_reset_locked();
  uint8_t proof[HL_MAC_SIZE];
  if (!transcript_proof(HL_CLIENT_PROOF, payload, HL_HELLO_PREFIX_SIZE, proof) ||
      !ct_equal(proof, payload + HL_HELLO_PREFIX_SIZE, sizeof(proof)))
    return ESP_ERR_INVALID_CRC;
  esp_err_t error = ESP_FAIL;
  uint8_t private_key[HOST_LINK_ECDH_SIZE] = {0};
  uint8_t secret[HOST_LINK_ECDH_SIZE] = {0};
  uint8_t code_key[HOST_LINK_KEY_SIZE] = {0};
  uint8_t transcript[HL_TRANSCRIPT_SIZE];
  uint8_t ack[HOST_LINK_HELLO_ACK_SIZE] = {HOST_LINK_PAIR_VERSION};
  esp_fill_random(private_key, sizeof(private_key));
  esp_fill_random(ack + 1, HOST_LINK_NONCE_SIZE);
  if (crypto_scalarmult_curve25519_base(ack + HL_PUBLIC_OFFSET, private_key) != 0 ||
      crypto_scalarmult_curve25519(secret, private_key, payload + HL_PUBLIC_OFFSET) != 0)
    goto cleanup;
  if (esp_read_mac(ack + HL_PUBLIC_OFFSET + HOST_LINK_ECDH_SIZE, ESP_MAC_BASE) != ESP_OK)
    goto cleanup;
  memcpy(transcript, payload, HL_HELLO_PREFIX_SIZE);
  memcpy(transcript + HL_HELLO_PREFIX_SIZE, ack, HL_ACK_PREFIX_SIZE);
  if (!transcript_proof(
          HL_SERVER_PROOF, transcript, sizeof(transcript), ack + HL_ACK_PREFIX_SIZE) ||
      derive_key(secret, transcript, HL_INFO_A2D, s_k_a2d) != 0 ||
      derive_key(secret, transcript, HL_INFO_D2A, s_k_d2a) != 0 ||
      derive_key(secret, transcript, HL_INFO_CODE, code_key) != 0)
    goto cleanup;
  uint32_t code = 0;
  for (uint32_t index = 0; index < sizeof(code); ++index)
    code |= (uint32_t)code_key[index] << (index * HL_CODE_BITS);
  if (++s_next_token == 0)
    ++s_next_token;
  s_pairing = (host_link_pairing_t){.token = s_next_token, .code = code % HL_CODE_MODULUS};
  s_pending_deadline = esp_timer_get_time() + (int64_t)HL_PAIR_PENDING_MS * HL_US_PER_MS;
  if (s_pending_deadline > s_window_deadline)
    s_pending_deadline = s_window_deadline;
  memcpy(ack_out, ack, sizeof(ack));
  *out_len = sizeof(ack);
  error = ESP_OK;
cleanup:
  mbedtls_platform_zeroize(private_key, sizeof(private_key));
  mbedtls_platform_zeroize(secret, sizeof(secret));
  mbedtls_platform_zeroize(code_key, sizeof(code_key));
  if (error != ESP_OK)
    sec_reset_locked();
  return error;
}

static bool
sec_verify_locked(const uint8_t *span, size_t span_len, const uint8_t *mac, uint32_t counter) {
  if (!s_authed)
    return false;

  uint8_t expect[HL_MAC_SIZE];
  if (span == NULL || mac == NULL || !hmac_trunc(s_k_a2d, sizeof(s_k_a2d), span, span_len, expect))
    return false;
  if (!ct_equal(expect, mac, HL_MAC_SIZE))
    return false;

  // Replay protection: strictly-increasing counter after the first authed frame.
  if (s_rx_valid && counter <= s_rx_counter)
    return false;

  s_rx_counter = counter;
  s_rx_valid = true;
  return true;
}

static bool sec_sign_locked(const uint8_t *span, size_t span_len, uint8_t *out_mac) {
  if (!s_authed || span == NULL || out_mac == NULL)
    return false;
  return hmac_trunc(s_k_d2a, sizeof(s_k_d2a), span, span_len, out_mac);
}

static esp_err_t sec_get_psk_locked(char *out, size_t out_cap) {
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

static esp_err_t sec_regenerate_locked(void) {
  uint8_t previous[HOST_LINK_PSK_SIZE];
  memcpy(previous, s_psk, sizeof(previous));
  esp_fill_random(s_psk, sizeof(s_psk));
  s_psk_loaded = true;
  host_link_sec_reset();
  esp_err_t err = persist_psk();
  if (err != ESP_OK)
    memcpy(s_psk, previous, sizeof(s_psk));
  mbedtls_platform_zeroize(previous, sizeof(previous));
  if (err == ESP_OK)
    ESP_LOGW(TAG, "PSK regenerated; existing pairings invalidated");
  return err;
}

// Security API serializes the entire key lifecycle, including console/UI PSK
// access. Recursive solely for helpers calling reset; it never takes TX or
// performs teardown. Authentication status is an atomic snapshot for log hooks
// and UI, not authorization (verify/sign take the lock and recheck).
esp_err_t host_link_sec_init(void) {
  if (s_crypto_lock == NULL) {
    s_crypto_lock = xSemaphoreCreateRecursiveMutex();
    if (s_crypto_lock == NULL) {
      ESP_LOGE(TAG, "Cannot allocate security lock");
      return ESP_ERR_NO_MEM;
    }
  }
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  esp_err_t err = sec_init_locked();
  xSemaphoreGiveRecursive(s_crypto_lock);
  return err;
}

void host_link_sec_pairing_window(bool is_open) {
  if (s_crypto_lock == NULL)
    return;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  s_window_deadline =
      is_open ? esp_timer_get_time() + (int64_t)HL_PAIR_WINDOW_MS * HL_US_PER_MS : 0;
  if (s_pairing.token != 0)
    sec_reset_locked();
  xSemaphoreGiveRecursive(s_crypto_lock);
}

bool host_link_sec_pairing_get(host_link_pairing_t *out) {
  if (out == NULL)
    return false;
  memset(out, 0, sizeof(*out));
  if (s_crypto_lock == NULL)
    return false;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  expire_pairing();
  *out = s_pairing;
  out->can_pair = esp_timer_get_time() < s_window_deadline;
  xSemaphoreGiveRecursive(s_crypto_lock);
  return out->token != 0;
}

esp_err_t host_link_sec_pairing_confirm(uint32_t token, bool is_approved) {
  if (s_crypto_lock == NULL)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  expire_pairing();
  esp_err_t error = ESP_ERR_INVALID_STATE;
  if (token != 0 && token == s_pairing.token) {
    if (is_approved)
      s_pairing.is_approved = true;
    else
      sec_reset_locked();
    error = ESP_OK;
  }
  xSemaphoreGiveRecursive(s_crypto_lock);
  return error;
}

bool host_link_sec_finish(const uint8_t *span,
                          size_t length,
                          const uint8_t *mac,
                          uint32_t counter) {
  if (s_crypto_lock == NULL || span == NULL || mac == NULL || counter != HL_FINISH_COUNTER)
    return false;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  expire_pairing();
  uint8_t expected[HL_MAC_SIZE];
  bool can_finish = (s_authed || (s_pairing.token != 0 && s_pairing.is_approved)) &&
                    hmac_trunc(s_k_a2d, sizeof(s_k_a2d), span, length, expected) &&
                    ct_equal(expected, mac, sizeof(expected));
  if (can_finish && !s_authed) {
    memset(&s_pairing, 0, sizeof(s_pairing));
    s_pending_deadline = 0;
    s_rx_counter = counter;
    s_rx_valid = true;
    __atomic_store_n(&s_authed, true, __ATOMIC_RELEASE);
  }
  xSemaphoreGiveRecursive(s_crypto_lock);
  return can_finish;
}

bool host_link_sec_is_authenticated(void) {
  return __atomic_load_n(&s_authed, __ATOMIC_ACQUIRE);
}

void host_link_sec_reset(void) {
  if (s_crypto_lock == NULL)
    return;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  sec_reset_locked();
  xSemaphoreGiveRecursive(s_crypto_lock);
}

esp_err_t host_link_sec_handle_hello(
    const uint8_t *payload, uint16_t plen, uint8_t *ack_out, size_t ack_cap, size_t *out_len) {
  if (s_crypto_lock == NULL)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  esp_err_t err = sec_hello_locked(payload, plen, ack_out, ack_cap, out_len);
  xSemaphoreGiveRecursive(s_crypto_lock);
  return err;
}

bool host_link_sec_verify_inbound(const uint8_t *span,
                                  size_t span_len,
                                  const uint8_t *mac,
                                  uint32_t counter) {
  if (s_crypto_lock == NULL)
    return false;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  bool ok = sec_verify_locked(span, span_len, mac, counter);
  xSemaphoreGiveRecursive(s_crypto_lock);
  return ok;
}

bool host_link_sec_sign_outbound(const uint8_t *span, size_t span_len, uint8_t *out_mac) {
  if (s_crypto_lock == NULL)
    return false;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  bool ok = sec_sign_locked(span, span_len, out_mac);
  xSemaphoreGiveRecursive(s_crypto_lock);
  return ok;
}

esp_err_t host_link_sec_get_psk_hex(char *out, size_t out_cap) {
  if (s_crypto_lock == NULL || out == NULL)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  esp_err_t err = sec_get_psk_locked(out, out_cap);
  xSemaphoreGiveRecursive(s_crypto_lock);
  return err;
}

esp_err_t host_link_sec_regenerate_psk(void) {
  if (s_crypto_lock == NULL)
    return ESP_ERR_INVALID_STATE;
  // Invalidate outbound work BEFORE waiting on crypto; callbacks remain free
  // to invalidate further. Worker will reset/teardown before the next HELLO.
  host_link_reset_rx();
  xSemaphoreTakeRecursive(s_crypto_lock, portMAX_DELAY);
  esp_err_t err = sec_regenerate_locked();
  xSemaphoreGiveRecursive(s_crypto_lock);
  // A HELLO may have entered while regeneration waited for crypto. Invalidate
  // that whole interval too, so it cannot publish an ACK using the old PSK.
  host_link_reset_rx();
  return err;
}
