// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tos_api_lora.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host_link_local_rf.h"
#include "lora_session.h"
#include "power_manager.h"
#include "sx1262.h"
#include "sx1262_hal.h"
#include "sx1262_regs.h"
#include "sys_prio.h"
#include "tos_app_ctx.h"

#define LORA_LOCK_MS          3000
#define LORA_RETRY_MS         10
#define LORA_STACK_BYTES      4096
#define LORA_TX_WAIT_MS       60000
#define LORA_FREQ_MIN_HZ      150000000
#define LORA_FREQ_MAX_HZ      960000000
#define LORA_POWER_MIN_DBM    (-9)
#define LORA_POWER_MAX_DBM    22
#define LORA_PREAMBLE_SYMBOLS 12
#define LORA_CR_OFFSET        4
#define LORA_CR_MIN           5
#define LORA_CR_MAX           8
#define LORA_RX_RING_SIZE     8

static const char *TAG = "TOS_LORA";

typedef struct {
  void *owner;
  uint32_t id;
  bool is_active;
  bool is_stopping;
} tos_api_lora_session_t;

typedef struct {
  const uint8_t *data;
  uint8_t len;
  bool has_terminal_error;
} tos_api_lora_tx_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static tos_api_lora_session_t s_session;
static TaskHandle_t s_cleanup_task;
static uint32_t s_sequence;
static sx1262_config_t s_config;
static lora_proto_t s_proto;
static bool s_has_radio;
static bool s_has_power;
static bool s_has_hal;
static bool s_has_mesh;
static atomic_bool s_is_receiving;
static atomic_bool s_is_tx_done;
static atomic_int s_async_error;

static void cleanup_task(void *arg);
static esp_err_t cleanup_worker(void *arg);
static esp_err_t configure_worker(void *arg);
static esp_err_t send_worker(void *arg);
static esp_err_t rx_worker(void *arg);
static esp_err_t mesh_worker(void *arg);
static esp_err_t acquire(uint32_t caps, bool can_stop);
static esp_err_t create_session(void);
static bool is_owner(void);
static void fail_session(void);
static bool valid_config(const tos_lora_cfg_t *config);
static void on_rx_done(const sx1262_packet_t *packet, void *ctx);
static void on_tx_done(void *ctx);
static void on_timeout(void *ctx);

esp_err_t tos_api_lora_configure(const tos_lora_cfg_t *config) {
  if (!valid_config(config))
    return ESP_ERR_INVALID_ARG;
  uint32_t caps = tos_app_cap_check(TOS_CAP_RADIO_RX) ? TOS_CAP_RADIO_RX : TOS_CAP_RADIO_TX;
  esp_err_t error = acquire(caps, false);
  if (error != ESP_OK)
    return error;
  if (is_owner() && !s_has_mesh) {
    if (atomic_load(&s_is_receiving)) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
  } else {
    error = create_session();
    if (error != ESP_OK)
      goto cleanup;
  }
  tos_lora_cfg_t copy = *config;
  error = host_local_rf_call(configure_worker, &copy);
  if (error != ESP_OK)
    fail_session();
  else
    tos_app_ctx_clear_resource_lost();
cleanup:
  host_local_rf_unlock();
  return error;
}

esp_err_t tos_api_lora_send(const uint8_t *data, uint8_t len) {
  if (data == NULL || len == 0)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = acquire(TOS_CAP_RADIO_TX, false);
  if (error != ESP_OK)
    return error;
  if (!is_owner() || s_has_mesh) {
    error = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  tos_api_lora_tx_t request = {.data = data, .len = len};
  error = host_local_rf_call(send_worker, &request);
  if (request.has_terminal_error)
    fail_session();
cleanup:
  host_local_rf_unlock();
  return error;
}

esp_err_t tos_api_lora_rx_start(uint32_t *out_session) {
  if (out_session == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_session = 0;
  esp_err_t error = acquire(TOS_CAP_RADIO_RX, false);
  if (error != ESP_OK)
    return error;
  if (!is_owner() || s_has_mesh) {
    error = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  error = host_local_rf_call(rx_worker, NULL);
  if (error == ESP_OK) {
    portENTER_CRITICAL(&s_lock);
    *out_session = s_session.id;
    portEXIT_CRITICAL(&s_lock);
  } else {
    fail_session();
  }
cleanup:
  host_local_rf_unlock();
  return error;
}

int tos_api_lora_rx_poll(tos_lora_packet_t *out) {
  if (out == NULL)
    return -ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  esp_err_t error = acquire(TOS_CAP_RADIO_RX, false);
  if (error != ESP_OK)
    return error > ESP_OK ? -error : error;
  int count = 0;
  if (!is_owner() || s_has_mesh || !atomic_load(&s_is_receiving)) {
    error = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  error = atomic_load(&s_async_error);
  if (error != ESP_OK) {
    fail_session();
    goto cleanup;
  }
  sx1262_packet_t packet;
  for (uint32_t index = 0; index < LORA_RX_RING_SIZE; ++index) {
    error = sx1262_get_packet(&packet);
    if (error == ESP_ERR_NOT_FOUND) {
      error = ESP_OK;
      break;
    }
    if (error != ESP_OK)
      break;
    if (packet.len == 0 || packet.has_crc_error || packet.has_header_error)
      continue;
    out->len = packet.len;
    out->rssi_dbm = packet.rssi_pkt_dbm;
    out->snr_db = packet.snr_pkt_db;
    memcpy(out->data, packet.buf, packet.len);
    count = 1;
    break;
  }
cleanup:
  host_local_rf_unlock();
  return error == ESP_OK ? count : (error > ESP_OK ? -error : error);
}

esp_err_t tos_api_lora_mesh_start(uint8_t proto, uint32_t *out_session) {
  if (out_session == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_session = 0;
  if (proto > TOS_LORA_MESH_MESHCORE)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = acquire(TOS_CAP_RADIO_TX, false);
  if (error != ESP_OK)
    return error;
  error = create_session();
  if (error != ESP_OK)
    goto cleanup;
  s_proto = proto == TOS_LORA_MESH_MESHTASTIC ? LORA_PROTO_MESHTASTIC : LORA_PROTO_MESHCORE;
  error = host_local_rf_call(mesh_worker, NULL);
  if (error != ESP_OK) {
    fail_session();
  } else {
    portENTER_CRITICAL(&s_lock);
    *out_session = s_session.id;
    portEXIT_CRITICAL(&s_lock);
    tos_app_ctx_clear_resource_lost();
  }
cleanup:
  host_local_rf_unlock();
  return error;
}

esp_err_t tos_api_lora_session_stop(uint32_t session) {
  uint32_t caps = tos_app_cap_check(TOS_CAP_RADIO_RX) ? TOS_CAP_RADIO_RX : TOS_CAP_RADIO_TX;
  esp_err_t error = acquire(caps, true);
  if (error != ESP_OK)
    return error;
  portENTER_CRITICAL(&s_lock);
  bool is_matching = s_session.is_active && s_session.owner == xTaskGetCurrentTaskHandle() &&
                     (session == s_session.id || (session == 0 && !s_has_mesh));
  if (is_matching)
    s_session.is_stopping = true;
  portEXIT_CRITICAL(&s_lock);
  error = is_matching ? host_local_rf_call(cleanup_worker, NULL) : ESP_ERR_INVALID_STATE;
  if (is_matching && error != ESP_OK)
    xTaskNotifyGive(s_cleanup_task);
  host_local_rf_unlock();
  return error;
}

void tos_api_lora_cancel_owner(void *task) {
  TaskHandle_t worker = NULL;
  portENTER_CRITICAL(&s_lock);
  if (task != NULL && s_session.owner == task && s_session.is_active) {
    s_session.owner = NULL;
    s_session.is_stopping = true;
    worker = s_cleanup_task;
  }
  portEXIT_CRITICAL(&s_lock);
  if (worker != NULL)
    xTaskNotifyGive(worker);
}

static esp_err_t acquire(uint32_t caps, bool can_stop) {
  if (!tos_app_cap_check(caps))
    return ESP_ERR_NOT_SUPPORTED;
  if (!host_local_rf_is_ready() || (!can_stop && tos_api_get()->should_stop()))
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(LORA_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  if (!can_stop && tos_api_get()->should_stop()) {
    host_local_rf_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

static bool is_owner(void) {
  portENTER_CRITICAL(&s_lock);
  bool result = s_session.is_active && !s_session.is_stopping &&
                s_session.owner == xTaskGetCurrentTaskHandle();
  portEXIT_CRITICAL(&s_lock);
  return result;
}

static esp_err_t create_session(void) {
  portENTER_CRITICAL(&s_lock);
  bool can_start = !s_session.is_active && s_sequence != UINT32_MAX;
  portEXIT_CRITICAL(&s_lock);
  if (!can_start)
    return ESP_ERR_INVALID_STATE;
  if (s_cleanup_task == NULL) {
    TaskHandle_t task = NULL;
    if (xTaskCreatePinnedToCore(cleanup_task,
                                "lora_app",
                                LORA_STACK_BYTES,
                                NULL,
                                SYS_PRIO_SERVICE_LO,
                                &task,
                                SYS_CORE_RADIO) != pdPASS) {
      ESP_LOGE(TAG, "Cannot allocate LoRa cleanup task");
      return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_lock);
    s_cleanup_task = task;
    portEXIT_CRITICAL(&s_lock);
  }
  portENTER_CRITICAL(&s_lock);
  s_session = (tos_api_lora_session_t){
      .owner = xTaskGetCurrentTaskHandle(), .id = ++s_sequence, .is_active = true};
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

static void fail_session(void) {
  portENTER_CRITICAL(&s_lock);
  s_session.is_stopping = true;
  tos_app_ctx_signal_resource_lost(s_session.owner);
  portEXIT_CRITICAL(&s_lock);
  xTaskNotifyGive(s_cleanup_task);
}

static bool valid_config(const tos_lora_cfg_t *config) {
  return config != NULL && config->freq_hz >= LORA_FREQ_MIN_HZ &&
         config->freq_hz <= LORA_FREQ_MAX_HZ && config->sf >= SX1262_LORA_SF5 &&
         config->sf <= SX1262_LORA_SF12 && config->sf != SX1262_LORA_SF6 &&
         sx1262_bw_is_valid(config->bw) &&
         !(config->sf == SX1262_LORA_SF5 && config->bw == SX1262_LORA_BW_500) &&
         config->cr >= LORA_CR_MIN && config->cr <= LORA_CR_MAX &&
         config->tx_power_dbm >= LORA_POWER_MIN_DBM && config->tx_power_dbm <= LORA_POWER_MAX_DBM;
}

static esp_err_t configure_worker(void *arg) {
  const tos_lora_cfg_t *config = arg;
  if (!s_has_radio) {
    esp_err_t error = lora_session_radio_reserve(&s_config);
    if (error != ESP_OK)
      return error;
    s_has_radio = true;
    error = power_manager_no_sleep_acquire();
    if (error != ESP_OK)
      return error;
    s_has_power = true;
    error = sx1262_hal_create(&s_config.hal);
    if (error != ESP_OK)
      return error;
    s_has_hal = true;
  }
  s_config.frequency_hz = config->freq_hz;
  s_config.sf = config->sf;
  s_config.bw = config->bw;
  s_config.cr = config->cr - LORA_CR_OFFSET;
  s_config.tx_power_dbm = config->tx_power_dbm;
  s_config.preamble_len = LORA_PREAMBLE_SYMBOLS;
  s_config.is_crc_on = true;
  esp_err_t error;
  if (sx1262_is_running())
    return sx1262_config_lora(&s_config);
  error = sx1262_init(&s_config);
  if (error != ESP_OK)
    return error;
  sx1262_callbacks_t callbacks = {
      .on_rx_done = on_rx_done, .on_tx_done = on_tx_done, .on_timeout = on_timeout};
  error = sx1262_set_callbacks(&callbacks);
  if (error != ESP_OK)
    return error;
  atomic_store(&s_async_error, ESP_OK);
  return sx1262_start();
}

static esp_err_t rx_worker(void *arg) {
  (void)arg;
  atomic_store(&s_is_receiving, true);
  esp_err_t error = sx1262_receive_continuous();
  if (error != ESP_OK)
    atomic_store(&s_is_receiving, false);
  return error;
}

static esp_err_t send_worker(void *arg) {
  tos_api_lora_tx_t *request = arg;
  bool was_receiving = atomic_exchange(&s_is_receiving, false);
  atomic_store(&s_is_tx_done, false);
  atomic_store(&s_async_error, ESP_OK);
  esp_err_t error = sx1262_transmit(request->data, request->len, LORA_TX_WAIT_MS);
  if (error == ESP_OK) {
    TickType_t started = xTaskGetTickCount();
    while (!atomic_load(&s_is_tx_done) && atomic_load(&s_async_error) == ESP_OK) {
      if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(LORA_TX_WAIT_MS)) {
        error = ESP_ERR_TIMEOUT;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(LORA_RETRY_MS));
    }
    if (error == ESP_OK)
      error = atomic_load(&s_async_error);
    request->has_terminal_error = error != ESP_OK;
  }
  if (was_receiving && !request->has_terminal_error) {
    esp_err_t rx_error = rx_worker(NULL);
    if (rx_error != ESP_OK) {
      error = rx_error;
      request->has_terminal_error = true;
    }
  }
  return error;
}

static esp_err_t mesh_worker(void *arg) {
  (void)arg;
  return lora_session_start_owned(s_proto, &s_session, &s_has_mesh);
}

static esp_err_t cleanup_worker(void *arg) {
  (void)arg;
  atomic_store(&s_is_receiving, false);
  if (s_has_mesh) {
    esp_err_t error = lora_session_stop_owned(s_proto, &s_session);
    if (error != ESP_OK && error != ESP_ERR_NOT_FOUND)
      return error;
    s_has_mesh = false;
  }
  if (s_has_hal) {
    esp_err_t error = sx1262_deinit();
    if (error != ESP_OK)
      return error;
    s_has_hal = false;
  }
  if (s_has_power) {
    esp_err_t error = power_manager_no_sleep_release();
    if (error != ESP_OK)
      return error;
    s_has_power = false;
  }
  if (s_has_radio) {
    esp_err_t error = lora_session_radio_release(&s_config);
    if (error != ESP_OK)
      return error;
    s_has_radio = false;
  }
  memset(&s_config, 0, sizeof(s_config));
  portENTER_CRITICAL(&s_lock);
  memset(&s_session, 0, sizeof(s_session));
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

static void cleanup_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      if (!host_local_rf_lock(LORA_RETRY_MS)) {
        vTaskDelay(pdMS_TO_TICKS(LORA_RETRY_MS));
        continue;
      }
      portENTER_CRITICAL(&s_lock);
      bool is_stopping = s_session.is_stopping;
      portEXIT_CRITICAL(&s_lock);
      esp_err_t error = is_stopping ? host_local_rf_call(cleanup_worker, NULL) : ESP_OK;
      host_local_rf_unlock();
      if (error == ESP_OK)
        break;
      vTaskDelay(pdMS_TO_TICKS(LORA_RETRY_MS));
    }
  }
}

static void on_rx_done(const sx1262_packet_t *packet, void *ctx) {
  (void)packet;
  (void)ctx;
  if (atomic_load(&s_is_receiving)) {
    esp_err_t error = sx1262_receive_continuous();
    if (error != ESP_OK)
      atomic_store(&s_async_error, error);
  }
}

static void on_tx_done(void *ctx) {
  (void)ctx;
  atomic_store(&s_is_tx_done, true);
}

static void on_timeout(void *ctx) {
  (void)ctx;
  atomic_store(&s_async_error, ESP_ERR_TIMEOUT);
}
