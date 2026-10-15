// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "grok_wifi.h"
#include "grok_wifi_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "session_manager.h"
#include "spi_bridge.h"
#include "storage_assets.h"
#include "sys_prio.h"
#include "wifi_service.h"

#define TX_CAPACITY     8192u
#define RX_CAPACITY     16384u
#define IO_CHUNK        512u
#define CONNECT_US      (15LL * 1000 * 1000)
#define MASTER_LEASE_US (10LL * 1000 * 1000)
#define STALL_US        (5LL * 1000 * 1000)
#define PROFILE_PATH    "config/grok.json"

enum { STOPPED, STARTING, CONNECTED, FAILED, STOPPING };
enum {
  ERR_NONE,
  ERR_WIFI,
  ERR_RADIO_BUSY,
  ERR_PROFILE,
  ERR_MEMORY,
  ERR_TLS,
  ERR_IO,
  ERR_TIMEOUT,
  ERR_BACKPRESSURE
};

static const char *TAG = "GROK_WIFI";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
  bool active, stop;
  uint8_t state, error, first_error;
  int64_t last_poll;
  uint32_t tx_bytes, rx_bytes;
  grok_wifi_ring_t tx, rx;
} s;

// Only fixed stage labels and allocator counters are logged: never the profile,
// token, certificate, audio or protocol payloads. No per-packet logging.
static void log_memory(const char *stage) {
  const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  ESP_LOGI(TAG,
           "stage=%s internal8_free=%lu largest=%lu psram8_free=%lu largest=%lu",
           stage,
           (unsigned long)heap_caps_get_free_size(internal),
           (unsigned long)heap_caps_get_largest_free_block(internal),
           (unsigned long)heap_caps_get_free_size(external),
           (unsigned long)heap_caps_get_largest_free_block(external));
}

bool grok_wifi_is_active(void) {
  portENTER_CRITICAL(&s_lock);
  bool active = s.active;
  portEXIT_CRITICAL(&s_lock);
  return active;
}

void grok_wifi_stop(void) {
  portENTER_CRITICAL(&s_lock);
  bool requested = s.active && !s.stop;
  uint8_t first_error = s.first_error;
  s.stop = true;
  s.state = s.active ? STOPPING : STOPPED;
  s.error = ERR_NONE;
  portEXIT_CRITICAL(&s_lock);
  if (requested)
    ESP_LOGI(TAG, "Stop requested by P4; first_error=%u", first_error);
}

static void fail(uint8_t error, const char *stage) {
  bool first = false;
  portENTER_CRITICAL(&s_lock);
  if (!s.stop) {
    if (s.first_error == ERR_NONE) {
      s.first_error = error;
      first = true;
    }
    s.state = FAILED;
    s.error = s.first_error;
  }
  portEXIT_CRITICAL(&s_lock);
  // Persist the first reason in the log before a following P4 STOP clears the
  // protocol status. Keep logging outside the shared SPI/worker critical lock.
  if (first) {
    ESP_LOGE(TAG, "Failed stage=%s error=%u", stage, error);
    log_memory("failed");
  }
}

static void fail_tls(esp_tls_t *tls, uint8_t error, const char *stage, ssize_t result) {
  esp_tls_error_handle_t handle = NULL;
  int tls_code = 0, verify_flags = 0;
  esp_err_t detail = esp_tls_get_error_handle(tls, &handle);
  if (detail == ESP_OK && handle)
    detail = esp_tls_get_and_clear_last_error(handle, &tls_code, &verify_flags);
  ESP_LOGE(TAG,
           "TLS failed stage=%s result=%ld esp=0x%x tls=%d verify=0x%x",
           stage,
           (long)result,
           (unsigned int)detail,
           tls_code,
           (unsigned int)verify_flags);
  fail(error, stage);
}

static bool alive(void) {
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  bool keep = !s.stop;
  bool lease = now - s.last_poll <= MASTER_LEASE_US;
  portEXIT_CRITICAL(&s_lock);
  if (!keep)
    return false;
  if (!lease) {
    fail(ERR_TIMEOUT, "p4_lease");
    return false;
  }
  if (!wifi_service_is_connected()) {
    fail(ERR_WIFI, "wifi_disconnected");
    return false;
  }
  if (wifi_service_is_busy() || spi_bridge_async_scan_busy() || session_manager_is_active()) {
    fail(ERR_RADIO_BUSY, "radio_busy");
    return false;
  }
  return true;
}

static bool would_block(ssize_t n) {
  return n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE;
}

static grok_wifi_profile_t *load_profile(void) {
  size_t size = 0, read = 0;
  if (storage_assets_get_file_size(PROFILE_PATH, &size) != ESP_OK || !size ||
      size > GROK_PROFILE_MAX) {
    fail(ERR_PROFILE, "profile_size");
    return NULL;
  }
  char *json = calloc(1, GROK_PROFILE_MAX + 1);
  grok_wifi_profile_t *profile = calloc(1, sizeof(*profile));
  if (!json || !profile) {
    free(json);
    free(profile);
    fail(ERR_MEMORY, "profile_alloc");
    return NULL;
  }
  bool ok = storage_assets_read_file(PROFILE_PATH, (uint8_t *)json, GROK_PROFILE_MAX + 1, &read) ==
                ESP_OK &&
            read == size;
  if (ok)
    ok = grok_wifi_profile_parse(json, size, profile);
  mbedtls_platform_zeroize(json, GROK_PROFILE_MAX + 1);
  free(json);
  if (!ok) {
    mbedtls_platform_zeroize(profile, sizeof(*profile));
    free(profile);
    fail(ERR_PROFILE, "profile_parse");
    return NULL;
  }
  return profile;
}

static bool send_hello(esp_tls_t *tls, const grok_wifi_profile_t *profile) {
  // Token alphabet is validated by the profile parser, so interpolation cannot
  // inject JSON. The credential is sent only after certificate verification.
  uint8_t hello[256] = {1, 0, 0};
  int n = snprintf((char *)hello + 3,
                   sizeof(hello) - 3,
                   "{\"version\":1,\"token\":\"%s\",\"rate\":16000}",
                   profile->token);
  if (n <= 0 || n >= (int)sizeof(hello) - 3) {
    fail(ERR_PROFILE, "hello_size");
    return false;
  }
  hello[1] = (uint8_t)((unsigned)n >> 8);
  hello[2] = (uint8_t)n;
  size_t total = (size_t)n + 3, offset = 0;
  int64_t deadline = esp_timer_get_time() + STALL_US;
  bool ok = true;
  while (offset < total) {
    if (!alive()) {
      ok = false;
      break;
    }
    if (esp_timer_get_time() > deadline) {
      fail(ERR_TIMEOUT, "hello_timeout");
      ok = false;
      break;
    }
    ssize_t wrote = esp_tls_conn_write(tls, hello + offset, total - offset);
    if (wrote > 0 && (size_t)wrote <= total - offset)
      offset += (size_t)wrote;
    else if (!would_block(wrote)) {
      fail_tls(tls, ERR_IO, "hello_write", wrote);
      ok = false;
      break;
    }
    if (offset < total)
      vTaskDelay(1);
  }
  mbedtls_platform_zeroize(hello, sizeof(hello));
  return ok;
}

static void voice_worker(void *unused) {
  (void)unused;
  grok_wifi_profile_t *profile = NULL;
  esp_tls_t *tls = NULL;
  uint8_t *tx = NULL, *rx = NULL;
  uint8_t tx_chunk[IO_CHUNK], rx_chunk[IO_CHUNK];
  size_t pending = 0, sent = 0;
  int64_t stalled_tx = 0, stalled_rx = 0;

  log_memory("worker_start");
  if (!alive())
    goto cleanup;
  profile = load_profile();
  if (!profile || !alive())
    goto cleanup;
  log_memory("profile_loaded");
  // These software rings are not DMA buffers. Reserve scarce internal RAM for
  // the existing TLS allocator and task stacks. Missing/fragmented PSRAM must
  // fail cleanly, never silently fall back to internal RAM.
  tx = heap_caps_calloc(1, TX_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!tx) {
    fail(ERR_MEMORY, "tx_psram_alloc");
    goto cleanup;
  }
  rx = heap_caps_calloc(1, RX_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rx) {
    fail(ERR_MEMORY, "rx_psram_alloc");
    goto cleanup;
  }
  tls = esp_tls_init();
  if (!tls) {
    fail(ERR_MEMORY, "tls_context_alloc");
    goto cleanup;
  }
  portENTER_CRITICAL(&s_lock);
  s.tx = (grok_wifi_ring_t){.data = tx, .capacity = TX_CAPACITY};
  s.rx = (grok_wifi_ring_t){.data = rx, .capacity = RX_CAPACITY};
  portEXIT_CRITICAL(&s_lock);

  // A dedicated, provisioned certificate plus exact hostname is required.
  // No bundle, insecure fallback, plaintext transport or public DNS resolution.
  // Existing sdkconfig does NOT check certificate validity dates; see README.
  esp_tls_cfg_t cfg = {
      .cacert_buf = (const unsigned char *)profile->ca_pem,
      .cacert_bytes = strlen(profile->ca_pem) + 1,
      .common_name = profile->tls_server_name,
      .skip_common_name = false,
      .non_block = true,
      .timeout_ms = 100,
      .tls_version = ESP_TLS_VER_TLS_1_2,
  };
  log_memory("tls_connect");
  int64_t deadline = esp_timer_get_time() + CONNECT_US;
  for (;;) {
    if (!alive())
      goto cleanup;
    if (esp_timer_get_time() > deadline) {
      fail(ERR_TIMEOUT, "tls_connect_timeout");
      goto cleanup;
    }
    int connected =
        esp_tls_conn_new_async(profile->host, strlen(profile->host), profile->port, &cfg, tls);
    if (connected < 0) {
      fail_tls(tls, ERR_TLS, "tls_connect", connected);
      goto cleanup;
    }
    if (connected == 1)
      break;
    vTaskDelay(1);
  }
  if (!send_hello(tls, profile))
    goto cleanup;
  // CONNECTED means the TLS transport only, NOT Grok authentication/readiness.
  // P4 must parse HELLO_OK, SESSION_START/STATE and errors from the gateway.
  portENTER_CRITICAL(&s_lock);
  if (!s.stop)
    s.state = CONNECTED;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGI(TAG, "TLS transport ready; gateway/API readiness is separate");
  log_memory("tls_ready");

  while (alive()) {
    if (!pending) {
      portENTER_CRITICAL(&s_lock);
      pending = grok_wifi_ring_peek(&s.tx, tx_chunk, sizeof(tx_chunk));
      portEXIT_CRITICAL(&s_lock);
      sent = 0;
    }
    if (pending) {
      ssize_t n = esp_tls_conn_write(tls, tx_chunk + sent, pending - sent);
      if (n > 0 && (size_t)n <= pending - sent) {
        portENTER_CRITICAL(&s_lock);
        grok_wifi_ring_drop(&s.tx, (size_t)n);
        s.tx_bytes += (uint32_t)n;
        portEXIT_CRITICAL(&s_lock);
        sent += (size_t)n;
        if (sent == pending)
          pending = 0;
        stalled_tx = 0;
      } else if (!would_block(n)) {
        fail_tls(tls, ERR_IO, "stream_write", n);
        break;
      } else if (!stalled_tx)
        stalled_tx = esp_timer_get_time();
    }
    portENTER_CRITICAL(&s_lock);
    size_t space = grok_wifi_ring_free(&s.rx);
    portEXIT_CRITICAL(&s_lock);
    if (space) {
      if (space > sizeof(rx_chunk))
        space = sizeof(rx_chunk);
      ssize_t n = esp_tls_conn_read(tls, rx_chunk, space);
      if (n > 0 && (size_t)n <= space) {
        portENTER_CRITICAL(&s_lock);
        bool accepted = grok_wifi_ring_write(&s.rx, rx_chunk, (size_t)n);
        if (accepted)
          s.rx_bytes += (uint32_t)n;
        portEXIT_CRITICAL(&s_lock);
        if (!accepted) {
          fail(ERR_BACKPRESSURE, "rx_ring_full");
          break;
        }
      } else if (!would_block(n)) {
        fail_tls(tls, ERR_IO, "stream_read", n);
        break;
      }
      stalled_rx = 0;
    } else if (!stalled_rx)
      stalled_rx = esp_timer_get_time();
    int64_t now = esp_timer_get_time();
    if ((stalled_tx && now - stalled_tx > STALL_US) ||
        (stalled_rx && now - stalled_rx > STALL_US)) {
      fail(ERR_BACKPRESSURE, "stream_stalled");
      break;
    }
    vTaskDelay(1);
  }

cleanup:
  if (tls)
    esp_tls_conn_destroy(tls);
  // Detach under the same lock used by SPI reads/writes, then erase audio and
  // credentials. No other task closes this worker-owned TLS object or memory.
  portENTER_CRITICAL(&s_lock);
  s.tx = (grok_wifi_ring_t){0};
  s.rx = (grok_wifi_ring_t){0};
  portEXIT_CRITICAL(&s_lock);
  if (profile) {
    mbedtls_platform_zeroize(profile, sizeof(*profile));
    free(profile);
  }
  if (tx) {
    mbedtls_platform_zeroize(tx, TX_CAPACITY);
    heap_caps_free(tx);
  }
  if (rx) {
    mbedtls_platform_zeroize(rx, RX_CAPACITY);
    heap_caps_free(rx);
  }
  mbedtls_platform_zeroize(tx_chunk, sizeof(tx_chunk));
  mbedtls_platform_zeroize(rx_chunk, sizeof(rx_chunk));
  portENTER_CRITICAL(&s_lock);
  if (s.stop) {
    s.state = STOPPED;
    s.error = ERR_NONE;
  } else if (s.state != FAILED) {
    s.state = FAILED;
    s.error = ERR_IO;
  }
  bool stopped = s.stop;
  uint8_t first_error = s.first_error;
  s.active = false;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGI(TAG, "Worker ended: %s first_error=%u", stopped ? "stopped" : "failed", first_error);
  log_memory("cleanup_complete");
  vTaskDelete(NULL);
}

static spi_status_t start(void) {
  // Called by the single SPI dispatcher. Reserve exclusivity before spawning,
  // even while a new worker has not yet been scheduled.
  portENTER_CRITICAL(&s_lock);
  bool active = s.active;
  portEXIT_CRITICAL(&s_lock);
  if (active)
    return SPI_STATUS_BUSY;
  portENTER_CRITICAL(&s_lock);
  s.stop = false;
  s.first_error = ERR_NONE;
  portEXIT_CRITICAL(&s_lock);
  if (!wifi_service_is_connected()) {
    fail(ERR_WIFI, "start_wifi");
    return SPI_STATUS_ERROR;
  }
  if (wifi_service_is_busy() || spi_bridge_async_scan_busy() || session_manager_is_active()) {
    fail(ERR_RADIO_BUSY, "start_radio_busy");
    return SPI_STATUS_BUSY;
  }
  portENTER_CRITICAL(&s_lock);
  memset(&s, 0, sizeof(s));
  s.active = true;
  s.state = STARTING;
  s.last_poll = esp_timer_get_time();
  portEXIT_CRITICAL(&s_lock);
  if (xTaskCreate(voice_worker, "grok_tls", 6144, NULL, SYS_PRIO_SERVICE_LO, NULL) != pdPASS) {
    portENTER_CRITICAL(&s_lock);
    s.active = false;
    portEXIT_CRITICAL(&s_lock);
    fail(ERR_MEMORY, "worker_stack_alloc");
    return SPI_STATUS_ERROR;
  }
  return SPI_STATUS_OK;
}

static void le16(uint8_t *p, uint16_t n) {
  p[0] = (uint8_t)n;
  p[1] = (uint8_t)(n >> 8);
}
static void le32(uint8_t *p, uint32_t n) {
  p[0] = (uint8_t)n;
  p[1] = (uint8_t)(n >> 8);
  p[2] = (uint8_t)(n >> 16);
  p[3] = (uint8_t)(n >> 24);
}

spi_status_t grok_wifi_execute(
    spi_id_t id, const uint8_t *payload, uint8_t len, uint8_t *response, uint8_t *response_len) {
  if (!response || !response_len || (len && !payload))
    return SPI_STATUS_INVALID_ARG;
  *response_len = 0;
  if (id != SPI_ID_GROK_TX && len)
    return SPI_STATUS_INVALID_ARG;
  if (id == SPI_ID_GROK_START)
    return start();
  if (id == SPI_ID_GROK_STOP) {
    grok_wifi_stop();
    return SPI_STATUS_OK;
  }
  if (id != SPI_ID_GROK_STATUS && id != SPI_ID_GROK_TX && id != SPI_ID_GROK_PULL)
    return SPI_STATUS_UNSUPPORTED;
  if (id == SPI_ID_GROK_TX && !len)
    return SPI_STATUS_INVALID_ARG;

  spi_status_t status = SPI_STATUS_OK;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  s.last_poll = now;
  if (id == SPI_ID_GROK_STATUS) {
    response[0] = s.state;
    response[1] = s.error;
    le16(response + 2, (uint16_t)grok_wifi_ring_free(&s.tx));
    le16(response + 4, (uint16_t)s.rx.used);
    le32(response + 6, s.tx_bytes);
    le32(response + 10, s.rx_bytes);
    *response_len = 14;
  } else if (s.state != CONNECTED || s.stop) {
    status = s.state == STARTING ? SPI_STATUS_BUSY : SPI_STATUS_ERROR;
  } else if (id == SPI_ID_GROK_TX) {
    if (!grok_wifi_ring_write(&s.tx, payload, len))
      status = SPI_STATUS_BUSY;
  } else {
    size_t n = grok_wifi_ring_peek(&s.rx, response, SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE);
    grok_wifi_ring_drop(&s.rx, n);
    *response_len = (uint8_t)n;
  }
  portEXIT_CRITICAL(&s_lock);
  return status;
}
