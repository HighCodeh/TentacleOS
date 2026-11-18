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

#include "rf_chamber.h"
#include "rf_passive.h"
#include <limits.h>
#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cc1101_bench.h"
#include "cc1101_lease.h"
#include "led_control.h"
#include "lora_session.h"
#include "power_manager.h"
#include "sx1262.h"
#include "sx1262_airtime.h"
#include "sx1262_cmd.h"
#include "sx1262_radio.h"
#include "sx1262_regs.h"
#include "sys_prio.h"
#define WORKER_STACK_BYTES   6144U
#define POLL_MS              50U
#define US_PER_MS            1000LL
#define SX_PACKET_TIMEOUT_MS 6000U
#define PREAMBLE_SYMBOLS     12U
#define SX_CHIP_MODE_MASK    7U
#define SX_CHIP_MODE_SHIFT   4U
#define SX_CHIP_RX           5U
#define SX_CHIP_TX           6U
static const char *TAG = "RF_CHAMBER";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rf_chamber_status_t s_status;
static _Atomic(TaskHandle_t) s_task;
static atomic_bool s_stop;
static atomic_uint s_tx_done;
static atomic_uint s_timeout;
static atomic_uint s_irq_errors;
static bool s_has_radio;
static bool s_has_power;
static bool s_has_hal;
static bool s_is_sx_initialized;
static sx1262_config_t s_sx;
static void worker(void *arg);
static esp_err_t prepare(const rf_chamber_config_t *config);
static esp_err_t cleanup(rf_chamber_radio_t radio);
static esp_err_t run(rf_chamber_status_t *status);
static esp_err_t sample(rf_chamber_status_t *status);
static void publish(const rf_chamber_status_t *status);
static void tx_done(void *ctx);
static void timeout(void *ctx);
static void irq_error(int error, void *ctx);
static void
receive(rf_chamber_status_t *status, const uint8_t *data, uint8_t length, bool has_error);

esp_err_t rf_chamber_start(const rf_chamber_config_t *config) {
  if (rf_passive_is_boot())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = rf_chamber_validate(config);
  if (error != ESP_OK)
    return error;
  portENTER_CRITICAL(&s_lock);
  bool can_start = s_status.phase == RF_CHAMBER_IDLE;
  if (can_start) {
    uint32_t id = s_status.run_id + 1;
    memset(&s_status, 0, sizeof(s_status));
    s_status.config = *config;
    s_status.phase = RF_CHAMBER_STARTING;
    s_status.run_id = id;
    s_status.rssi_min_half_dbm = INT16_MAX;
    s_status.rssi_max_half_dbm = INT16_MIN;
    atomic_store(&s_stop, false);
  }
  portEXIT_CRITICAL(&s_lock);
  if (!can_start)
    return ESP_ERR_INVALID_STATE;
  if (atomic_load(&s_task) == NULL) {
    TaskHandle_t task = NULL;
    if (xTaskCreatePinnedToCore(worker,
                                "rf_chamber",
                                WORKER_STACK_BYTES,
                                NULL,
                                SYS_PRIO_SERVICE_LO,
                                &task,
                                SYS_CORE_RADIO) != pdPASS) {
      portENTER_CRITICAL(&s_lock);
      s_status.phase = RF_CHAMBER_IDLE;
      s_status.error = ESP_ERR_NO_MEM;
      portEXIT_CRITICAL(&s_lock);
      ESP_LOGE(TAG, "worker allocation failed");
      return ESP_ERR_NO_MEM;
    }
    atomic_store(&s_task, task);
  }
  xTaskNotifyGive(atomic_load(&s_task));
  return ESP_OK;
}
void rf_chamber_stop(void) {
  atomic_store(&s_stop, true);
  portENTER_CRITICAL(&s_lock);
  TaskHandle_t task = atomic_load(&s_task);
  bool is_held = s_status.phase == RF_CHAMBER_HELD;
  portEXIT_CRITICAL(&s_lock);
  if (task != NULL && is_held)
    xTaskNotifyGive(task);
}
void rf_chamber_snapshot(rf_chamber_status_t *out_status) {
  if (out_status == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out_status = s_status;
  portEXIT_CRITICAL(&s_lock);
}

static void worker(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    rf_chamber_status_t status;
    rf_chamber_snapshot(&status);
    if (status.phase == RF_CHAMBER_STARTING) {
      atomic_store(&s_tx_done, 0);
      atomic_store(&s_timeout, 0);
      atomic_store(&s_irq_errors, 0);
      status.error = prepare(&status.config);
      if (status.error == ESP_OK && !atomic_load(&s_stop)) {
        status.phase = RF_CHAMBER_RUNNING;
        publish(&status);
        ESP_LOGI(TAG,
                 "RUN id=%lu radio=%u mode=%u hz=%lu nominal_dbm=%d path=%u sf=%u bw=%u cr=%u "
                 "duration_ms=%lu",
                 (unsigned long)status.run_id,
                 status.config.radio,
                 status.config.mode,
                 (unsigned long)status.config.frequency_hz,
                 status.config.power_dbm,
                 status.config.path,
                 status.config.sf,
                 status.config.bandwidth_khz,
                 status.config.cr,
                 (unsigned long)status.config.duration_ms);
        led_signal_info();
        status.error = run(&status);
      }
    }
    status.stopped_by_user = atomic_load(&s_stop);
    status.phase = RF_CHAMBER_STOPPING;
    publish(&status);
    status.cleanup_error = cleanup(status.config.radio);
    status.phase = status.cleanup_error == ESP_OK ? RF_CHAMBER_IDLE : RF_CHAMBER_HELD;
    publish(&status);
    if (status.error != ESP_OK || status.cleanup_error != ESP_OK) {
      ESP_LOGE(TAG,
               "END id=%lu operation=%s cleanup=%s phase=%u",
               (unsigned long)status.run_id,
               esp_err_to_name(status.error),
               esp_err_to_name(status.cleanup_error),
               status.phase);
      led_signal_error();
    } else {
      ESP_LOGI(TAG,
               "END id=%lu checked_off=1 tx_done=%lu rx_test=%lu elapsed_ms=%lu",
               (unsigned long)status.run_id,
               (unsigned long)status.tx_done,
               (unsigned long)status.rx_test,
               (unsigned long)status.elapsed_ms);
      led_signal_info();
    }
  }
}
static esp_err_t prepare(const rf_chamber_config_t *config) {
  esp_err_t error =
      config->radio == RF_CHAMBER_CC ? cc1101_lease_acquire() : lora_session_radio_reserve(&s_sx);
  if (error != ESP_OK)
    return error;
  s_has_radio = true;
  error = power_manager_no_sleep_acquire();
  if (error != ESP_OK)
    return error;
  s_has_power = true;
  if (config->radio == RF_CHAMBER_CC) {
    cc1101_bench_config_t cc = {.frequency_hz = config->frequency_hz,
                                .power_dbm = config->power_dbm,
                                .path = config->path,
                                .is_cw = config->mode == RF_CHAMBER_CW};
    return cc1101_bench_prepare(&cc);
  }
  memset(&s_sx, 0, sizeof(s_sx));
  error = sx1262_hal_create(&s_sx.hal);
  if (error != ESP_OK)
    return error;
  s_has_hal = true;
  s_sx.frequency_hz = config->frequency_hz;
  s_sx.tx_power_dbm = config->power_dbm;
  s_sx.sf = config->sf;
  s_sx.bw = config->bandwidth_khz == 125   ? SX1262_LORA_BW_125
            : config->bandwidth_khz == 250 ? SX1262_LORA_BW_250
                                           : SX1262_LORA_BW_500;
  s_sx.cr = config->cr;
  s_sx.preamble_len = PREAMBLE_SYMBOLS;
  s_sx.is_crc_on = true;
  error = sx1262_init(&s_sx);
  if (error != ESP_OK)
    return error;
  s_is_sx_initialized = true;
  sx1262_callbacks_t callbacks = {
      .on_tx_done = tx_done, .on_timeout = timeout, .on_error = irq_error};
  error = sx1262_set_callbacks(&callbacks);
  if (error == ESP_OK)
    error = sx1262_start();
  if (error == ESP_OK)
    error = sx1262_radio_reset_stats();
  return error;
}
static esp_err_t cleanup(rf_chamber_radio_t radio) {
  if (!s_has_radio)
    return ESP_OK;
  esp_err_t error = ESP_OK;
  if (radio == RF_CHAMBER_CC) {
    (void)cc1101_lease_acquire();
    error = cc1101_bench_restore();
    if (error != ESP_OK) {
      cc1101_lease_pause();
      return error;
    }
  } else if (s_has_hal) {
    if (s_is_sx_initialized) {
      error = sx1262_radio_standby_checked();
      if (error != ESP_OK)
        return error;
    }
    error = sx1262_deinit();
    if (error != ESP_OK)
      return error;
    s_has_hal = false;
    s_is_sx_initialized = false;
  }
  if (s_has_power) {
    error = power_manager_no_sleep_release();
    if (error != ESP_OK)
      return error;
    s_has_power = false;
  }
  error = radio == RF_CHAMBER_CC ? cc1101_lease_release() : lora_session_radio_release(&s_sx);
  if (error == ESP_OK)
    s_has_radio = false;
  return error;
}
static esp_err_t run(rf_chamber_status_t *status) {
  const rf_chamber_config_t *config = &status->config;
  bool is_cc = config->radio == RF_CHAMBER_CC;
  esp_err_t error = ESP_OK;
  int64_t start = esp_timer_get_time();
  int64_t end = start + config->duration_ms * US_PER_MS;
  int64_t next_tx = start;
  int64_t tx_started = 0;
  uint32_t sequence = 0;
  bool is_pending = false;
  if (config->mode == RF_CHAMBER_CW)
    error = is_cc ? cc1101_bench_enter(true) : sx1262_radio_lab_cw_start();
  else if (config->mode == RF_CHAMBER_RX)
    error = is_cc ? cc1101_bench_enter(false) : sx1262_receive_continuous();
  while (error == ESP_OK && !atomic_load(&s_stop) && esp_timer_get_time() < end) {
    int64_t now = esp_timer_get_time();
    if (!is_cc) {
      status->tx_done = atomic_load(&s_tx_done);
      status->tx_timeout = atomic_load(&s_timeout);
      if (config->mode == RF_CHAMBER_TX && status->tx_timeout != 0) {
        error = ESP_ERR_TIMEOUT;
        break;
      }
      if (is_pending && status->tx_done >= status->tx_submitted)
        is_pending = false;
      if (is_pending && now - tx_started > SX_PACKET_TIMEOUT_MS * US_PER_MS) {
        error = ESP_ERR_TIMEOUT;
        break;
      }
    }
    if (config->mode == RF_CHAMBER_TX && !is_pending && now >= next_tx) {
      if (!is_cc && sx1262_airtime_remaining_ms(&s_sx) != 0) {
        ++status->duty_deferred;
        next_tx = now + config->interval_ms * US_PER_MS;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        continue;
      }
      uint8_t payload[RF_CHAMBER_PAYLOAD_SIZE];
      rf_chamber_payload(sequence, payload);
      error = is_cc ? cc1101_bench_packet(payload)
                    : sx1262_transmit(payload, sizeof(payload), SX_PACKET_TIMEOUT_MS);
      if (error == ESP_OK) {
        ++status->tx_submitted;
        ++sequence;
        if (is_cc)
          ++status->tx_done;
        else {
          is_pending = true;
          tx_started = now;
        }
      }
      next_tx = now + config->interval_ms * US_PER_MS;
    }
    if (error == ESP_OK)
      error = sample(status);
    status->elapsed_ms = (esp_timer_get_time() - start) / US_PER_MS;
    publish(status);
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
  status->elapsed_ms = (esp_timer_get_time() - start) / US_PER_MS;
  if (!is_cc) {
    status->tx_done = atomic_load(&s_tx_done);
    status->tx_timeout = atomic_load(&s_timeout);
  }
  /* End-of-window TX may be truncated; it stays submitted, never counted done. */
  return error;
}
static esp_err_t sample(rf_chamber_status_t *status) {
  int16_t rssi_half = 0;
  esp_err_t error;
  if (status->config.radio == RF_CHAMBER_CC) {
    cc1101_bench_sample_t cc;
    error = cc1101_bench_sample(&cc);
    if (error != ESP_OK)
      return error;
    status->radio_status = cc.state;
    status->pa_code = cc.pa;
    status->frequency_error_raw = cc.frequency_error;
    rssi_half = cc.rssi_half_dbm;
    if (status->config.mode == RF_CHAMBER_CW && cc.state != 19)
      return ESP_ERR_INVALID_RESPONSE;
    if (cc.has_packet && status->config.mode == RF_CHAMBER_RX) {
      receive(status, cc.payload, sizeof(cc.payload), cc.has_crc_error);
      error = cc1101_bench_enter(false);
    }
  } else {
    error = sx1262_get_status(&status->radio_status);
    if (error == ESP_OK)
      error = sx1262_get_device_errors(&status->device_errors);
    if (error != ESP_OK)
      return error;
    if (status->device_errors != 0)
      return ESP_ERR_INVALID_RESPONSE;
    if (status->config.mode == RF_CHAMBER_CW &&
        ((status->radio_status >> SX_CHIP_MODE_SHIFT) & SX_CHIP_MODE_MASK) != SX_CHIP_TX)
      return ESP_ERR_INVALID_RESPONSE;
    if (status->config.mode == RF_CHAMBER_RX) {
      int16_t rssi = 0;
      error = sx1262_get_rssi_inst(&rssi);
      if (error != ESP_OK)
        return error;
      rssi_half = rssi * 2;
      sx1262_packet_t packet;
      while (sx1262_get_packet(&packet) == ESP_OK) {
        status->last_snr_db = packet.snr_pkt_db;
        receive(status, packet.buf, packet.len, packet.has_crc_error || packet.has_header_error);
        ESP_LOGI(TAG,
                 "RX id=%lu test=%lu rssi=%d snr=%d crc_error=%u header_error=%u",
                 (unsigned long)status->run_id,
                 (unsigned long)status->rx_test,
                 packet.rssi_pkt_dbm,
                 packet.snr_pkt_db,
                 packet.has_crc_error,
                 packet.has_header_error);
      }
      status->rx_error += atomic_exchange(&s_irq_errors, 0);
      if (((status->radio_status >> SX_CHIP_MODE_SHIFT) & SX_CHIP_MODE_MASK) != SX_CHIP_RX)
        return ESP_ERR_INVALID_RESPONSE;
    }
  }
  if (error == ESP_OK && status->config.mode == RF_CHAMBER_RX) {
    ++status->samples;
    status->rssi_sum_half_dbm += rssi_half;
    if (rssi_half < status->rssi_min_half_dbm)
      status->rssi_min_half_dbm = rssi_half;
    if (rssi_half > status->rssi_max_half_dbm)
      status->rssi_max_half_dbm = rssi_half;
  }
  return error;
}
static void
receive(rf_chamber_status_t *status, const uint8_t *data, uint8_t length, bool has_error) {
  uint32_t sequence;
  if (has_error) {
    ++status->rx_error;
  } else if (!rf_chamber_parse_payload(data, length, &sequence)) {
    ++status->rx_other;
  } else {
    if (status->rx_test != 0) {
      if (sequence == status->sequence_last)
        ++status->sequence_repeats;
      if (sequence > status->sequence_last && sequence - status->sequence_last > 1)
        status->sequence_gaps += sequence - status->sequence_last - 1;
    }
    status->sequence_last = sequence;
    ++status->rx_test;
  }
}
static void publish(const rf_chamber_status_t *status) {
  portENTER_CRITICAL(&s_lock);
  s_status = *status;
  portEXIT_CRITICAL(&s_lock);
}
static void tx_done(void *ctx) {
  (void)ctx;
  atomic_fetch_add(&s_tx_done, 1);
}
static void timeout(void *ctx) {
  (void)ctx;
  atomic_fetch_add(&s_timeout, 1);
}
static void irq_error(int error, void *ctx) {
  (void)error;
  (void)ctx;
  atomic_fetch_add(&s_irq_errors, 1);
}

static atomic_bool s_mesh_stopping;
static atomic_int s_mesh_stop_error;
static void stop_mesh_task(void *arg) {
  lora_proto_t protocol = (lora_proto_t)(uintptr_t)arg;
  esp_err_t error = lora_session_stop(protocol);
  atomic_store(&s_mesh_stop_error, error);
  atomic_store(&s_mesh_stopping, false);
  vTaskDelete(NULL);
}
esp_err_t rf_chamber_mesh_stop_async(void) {
  bool expected = false;
  if (!atomic_compare_exchange_strong(&s_mesh_stopping, &expected, true))
    return ESP_ERR_INVALID_STATE;
  lora_proto_t protocol = lora_session_active();
  atomic_store(&s_mesh_stop_error, ESP_OK);
  if (protocol == LORA_PROTO_NONE) {
    atomic_store(&s_mesh_stopping, false);
    return ESP_OK;
  }
  if (xTaskCreatePinnedToCore(stop_mesh_task,
                              "lab_mesh_stop",
                              WORKER_STACK_BYTES,
                              (void *)(uintptr_t)protocol,
                              SYS_PRIO_SERVICE_LO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    atomic_store(&s_mesh_stop_error, ESP_ERR_NO_MEM);
    atomic_store(&s_mesh_stopping, false);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
esp_err_t rf_chamber_mesh_stop_status(bool *out_busy) {
  if (out_busy != NULL)
    *out_busy = atomic_load(&s_mesh_stopping);
  return atomic_load(&s_mesh_stop_error);
}
