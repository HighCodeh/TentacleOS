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

#include "imu_motion.h"
#include "imu_orientation.h"
#include "qmi8658a.h"
#include "sys_prio.h"
#include "power_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#define MOTION_ODR      6
#define POLL_MS         4
#define WORKER_STACK    4096
#define STOP_TIMEOUT_MS 500
#define SAMPLE_GAP_US   50000
#define ZERO_SETTLE_US  1000000
#define REQ_CENTER      1u
#define REQ_ZERO        2u
static const char *TAG = "IMU3D";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static imu_motion_snapshot_t s_snapshot;
static bool s_stop;
static unsigned s_request;

static void publish(const imu_motion_snapshot_t *value) {
  portENTER_CRITICAL(&s_lock);
  s_snapshot = *value;
  portEXIT_CRITICAL(&s_lock);
}
void imu_motion_snapshot(imu_motion_snapshot_t *out) {
  portENTER_CRITICAL(&s_lock);
  *out = s_snapshot;
  portEXIT_CRITICAL(&s_lock);
}
void imu_motion_request(bool calibrate) {
  portENTER_CRITICAL(&s_lock);
  s_request |= calibrate ? REQ_ZERO : REQ_CENTER;
  portEXIT_CRITICAL(&s_lock);
}
static void worker(void *context) {
  (void)context;
  imu_motion_snapshot_t v = {.running = true, .zeroing = true};
  imu_orientation_t orientation;
  imu_zero_t zero;
  imu_orientation_reset(&orientation);
  imu_zero_reset(&zero);
  imu_orientation_matrix(&orientation, v.matrix, v.angles);
  v.error = power_manager_no_sleep_acquire();
  bool power_held = v.error == ESP_OK;
  if (power_held)
    v.error = qmi8658a_start();
  if (v.error == ESP_OK)
    v.error = qmi8658a_set_odr(MOTION_ODR);
  bool started = v.error == ESP_OK;
  int64_t previous = 0, rate_started = 0, zero_after = 0;
  unsigned rate_count = 0;
  publish(&v);
  for (;;) {
    portENTER_CRITICAL(&s_lock);
    bool stop = s_stop;
    unsigned request = s_request;
    s_request = 0;
    portEXIT_CRITICAL(&s_lock);
    if (stop)
      break;
    if (request & REQ_ZERO) {
      v.zeroing = true;
      imu_zero_reset(&zero);
      zero_after = esp_timer_get_time() + ZERO_SETTLE_US;
    }
    if (request & REQ_CENTER)
      imu_orientation_recenter(&orientation);
    qmi8658a_sample_t sample;
    esp_err_t err = started ? qmi8658a_read(&sample) : v.error;
    int64_t now = esp_timer_get_time();
    if (err == ESP_OK) {
      float dt = previous ? (now - previous) * 0.000001f : 0.009f;
      if (previous && now - previous > SAMPLE_GAP_US) {
        ++v.gaps;
        imu_zero_reset(&zero);
      }
      previous = now;
      v.sample_us = now;
      v.error = ESP_OK;
      ++v.samples;
      if (!rate_started)
        rate_started = now;
      if (++rate_count >= 100) {
        v.rate_hz = (rate_count - 1) * 1000000.0f / (now - rate_started);
        rate_count = 0;
        rate_started = 0;
      }
      /* Body convention: +X screen-right, +Y head, +Z out of display.
         U17 is on BottomLayer. The mirrored PCB footprint is documented in README. */
      v.accel[0] = sample.accel_g[0];
      v.accel[1] = -sample.accel_g[1];
      v.accel[2] = -sample.accel_g[2];
      float raw[3] = {sample.gyro_dps[0], -sample.gyro_dps[1], -sample.gyro_dps[2]};
      if (v.zeroing && now >= zero_after) {
        int result = imu_zero_sample(&zero, v.accel, raw, v.bias);
        if (result == 1) {
          v.zeroing = false;
          v.calibrated = true;
          imu_orientation_reset(&orientation);
          ESP_LOGI(TAG, "zero accepted: bias %.4f %.4f %.4f dps", v.bias[0], v.bias[1], v.bias[2]);
        }
      }
      v.zero_count = zero.count;
      for (int i = 0; i < 3; ++i)
        v.gyro[i] = raw[i] - v.bias[i];
      v.temperature = sample.temperature_c;
      if (v.calibrated)
        imu_orientation_update(&orientation, v.accel, v.gyro, dt);
      imu_orientation_matrix(&orientation, v.matrix, v.angles);
    } else if (err != ESP_ERR_NOT_FINISHED) {
      ++v.errors;
      v.error = err;
      imu_zero_reset(&zero);
      previous = 0;
    }
    publish(&v);
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
  esp_err_t stop_err = qmi8658a_stop();
  if (stop_err != ESP_OK)
    v.error = stop_err;
  if (power_held) {
    esp_err_t release = power_manager_no_sleep_release();
    if (release != ESP_OK)
      v.error = release;
  }
  v.running = false;
  publish(&v);
  vTaskDelete(NULL);
}
esp_err_t imu_motion_start(void) {
  portENTER_CRITICAL(&s_lock);
  if (s_snapshot.running) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  memset(&s_snapshot, 0, sizeof(s_snapshot));
  s_snapshot.running = true;
  s_snapshot.zeroing = true;
  s_snapshot.matrix[0] = s_snapshot.matrix[4] = s_snapshot.matrix[8] = 1;
  s_stop = false;
  s_request = 0;
  portEXIT_CRITICAL(&s_lock);
  if (xTaskCreatePinnedToCore(
          worker, "imu_motion", WORKER_STACK, NULL, SYS_PRIO_SERVICE_LO, NULL, SYS_CORE_UI) !=
      pdPASS) {
    portENTER_CRITICAL(&s_lock);
    s_snapshot.running = false;
    s_snapshot.error = ESP_ERR_NO_MEM;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGE(TAG, "Failed to allocate IMU worker");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
esp_err_t imu_motion_stop(void) {
  portENTER_CRITICAL(&s_lock);
  s_stop = true;
  portEXIT_CRITICAL(&s_lock);
  int64_t until = esp_timer_get_time() + STOP_TIMEOUT_MS * 1000;
  imu_motion_snapshot_t snap;
  do {
    imu_motion_snapshot(&snap);
    if (!snap.running)
      return snap.error;
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  } while (esp_timer_get_time() < until);
  return ESP_ERR_TIMEOUT;
}
void imu_motion_log(void) {
  imu_motion_snapshot_t v;
  imu_motion_snapshot(&v);
  ESP_LOGI(
      TAG,
      "running=%d calibrated=%d zeroing=%d n=%lu errors=%lu gaps=%lu age_ms=%lld Hz=%.2f err=0x%x",
      v.running,
      v.calibrated,
      v.zeroing,
      (unsigned long)v.samples,
      (unsigned long)v.errors,
      (unsigned long)v.gaps,
      v.sample_us ? (esp_timer_get_time() - v.sample_us) / 1000 : -1,
      v.rate_hz,
      v.error);
  ESP_LOGI(TAG,
           "angles=%.2f,%.2f,%.2f accel=%.3f,%.3f,%.3f gyro=%.3f,%.3f,%.3f bias=%.3f,%.3f,%.3f",
           v.angles[0],
           v.angles[1],
           v.angles[2],
           v.accel[0],
           v.accel[1],
           v.accel[2],
           v.gyro[0],
           v.gyro[1],
           v.gyro[2],
           v.bias[0],
           v.bias[1],
           v.bias[2]);
}
