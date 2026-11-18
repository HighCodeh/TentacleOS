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

#include "wardrive.h"
#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pin_def.h"
#include "rf_frontend.h"
#include "lf_rfid.h"
#include "resource_mgr.h"
#include "power_manager.h"
#include "spi_bridge.h"
#include "sys_prio.h"
#include "usb_msc.h"
#include "vfs_core.h"
#include "vfs_sdcard.h"

#define WARD_UART             UART_NUM_3
#define WARD_BAUD             9600
#define WARD_RING_BYTES       8192
#define WARD_QUEUE_EVENTS     32
#define WARD_READ_BYTES       512
#define WARD_STACK_BYTES      8192
#define WARD_RPC_MS           800
#define WARD_TICK_MS          50
#define WARD_POLL_MS          250
#define WARD_SCAN_TIMEOUT_MS  45000U
#define WARD_SCAN_GAP_MS      2000U
#define WARD_RETRY_MAX_MS     30000U
#define WARD_STORAGE_RETRY_MS 10000U
#define WARD_FIX_HISTORY      64
#define WARD_FIX_OFFSET_MS    1500U
#define WARD_PENDING_MAX      256
#define WARD_UNIQUE_MAX       2048
#define WARD_LINE_BYTES       1024
#define WARD_MIN_FREE_BYTES   (256U * 1024U)
#define WARD_DIR              "/sdcard/wardriving"
#define WARD_MS_SECOND        1000U
#define WARD_PUMP_BUDGET      2048U

typedef struct {
  spi_ward_record_t ap;
  wardrive_fix_t fix;
  uint32_t id, cycle, scan_start_ms, scan_end_ms;
  bool located;
} wardrive_observation_t;
static const char *TAG = "WARDRIVE";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static wardrive_status_t s_status;
static atomic_bool s_stop, s_pause;
EXT_RAM_BSS_ATTR static wardrive_observation_t s_pending[WARD_PENDING_MAX];
EXT_RAM_BSS_ATTR static uint8_t s_unique[WARD_UNIQUE_MAX][6];
static wardrive_gps_t s_gps;
static wardrive_fix_t s_history[WARD_FIX_HISTORY];
static uint16_t s_head, s_pending_count, s_unique_count;
static uint8_t s_history_next;
static uint32_t s_last_epoch, s_started_ms, s_next_storage_ms, s_part;
static int s_fd = -1;
static QueueHandle_t s_uart_queue;
static bool s_uart_installed;

static uint32_t millis(void) {
  return (uint32_t)(esp_timer_get_time() / WARD_MS_SECOND);
}
static void worker(void *arg);
static void gps_pump(void);
static void storage_flush(void);
static esp_err_t scan_cycle(uint8_t band);

esp_err_t wardrive_start(uint8_t band) {
  if (band > SPI_WIFI_BAND_5G)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  if (s_status.running) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  // Unconfirmed records survive stop/start until the next successful SD flush.
  if (s_status.buffered == 0) {
    memset(&s_status, 0, sizeof(s_status));
    s_status.session_id = esp_random();
    s_head = s_pending_count = s_unique_count = 0;
    s_part = 0;
    s_started_ms = millis();
  }
  s_status.running = true;
  s_status.stopping = s_status.paused = false;
  s_status.band = band;
  s_status.radio_error = s_status.gps_error = ESP_OK;
  atomic_store(&s_stop, false);
  atomic_store(&s_pause, false);
  portEXIT_CRITICAL(&s_lock);
  if (xTaskCreatePinnedToCore(
          worker, "wardrive", WARD_STACK_BYTES, NULL, SYS_PRIO_BACKGROUND, NULL, SYS_CORE_RADIO) ==
      pdPASS)
    return ESP_OK;
  portENTER_CRITICAL(&s_lock);
  s_status.running = false;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGE(TAG, "Worker allocation failed");
  return ESP_ERR_NO_MEM;
}
void wardrive_stop(void) {
  atomic_store(&s_stop, true);
}
void wardrive_pause(bool pause) {
  atomic_store(&s_pause, pause);
}
void wardrive_get_status(wardrive_status_t *out) {
  if (out == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_status;
  portEXIT_CRITICAL(&s_lock);
}
bool wardrive_is_running(void) {
  portENTER_CRITICAL(&s_lock);
  bool running = s_status.running;
  portEXIT_CRITICAL(&s_lock);
  return running;
}
static esp_err_t rpc(spi_id_t id, const void *payload, size_t len, void *out, size_t size) {
  spi_header_t response = {0};
  esp_err_t err = spi_bridge_send_command(id, payload, len, &response, out, size, WARD_RPC_MS);
  return err == ESP_OK && response.length != size ? ESP_ERR_INVALID_SIZE : err;
}
static void gps_pump(void) {
  if (s_uart_installed) {
    uint8_t bytes[WARD_READ_BYTES];
    size_t budget = 0;
    int count;
    while (budget < WARD_PUMP_BUDGET &&
           (count = uart_read_bytes(WARD_UART, bytes, sizeof(bytes), 0)) > 0) {
      uint32_t now = millis();
      budget += count;
      for (int i = 0; i < count; i++)
        wardrive_gps_feed(&s_gps, bytes[i], now);
    }
    uart_event_t event;
    while (xQueueReceive(s_uart_queue, &event, 0)) {
      if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
          event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR ||
          event.type == UART_BREAK) {
        portENTER_CRITICAL(&s_lock);
        s_status.uart_errors++;
        portEXIT_CRITICAL(&s_lock);
        s_gps.rmc.valid = s_gps.gga.valid = false;
        s_gps.used = 0;
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL)
          uart_flush_input(WARD_UART);
      }
    }
  }
  uint32_t now = millis();
  wardrive_fix_t fix;
  bool valid = wardrive_gps_fix(&s_gps, now, &fix);
  if (valid && (fix.epoch_ms != s_last_epoch)) {
    s_history[s_history_next] = fix;
    s_history_next = (s_history_next + 1) % WARD_FIX_HISTORY;
    s_last_epoch = fix.epoch_ms;
  }
  portENTER_CRITICAL(&s_lock);
  s_status.fix = fix;
  s_status.has_fix = valid;
  s_status.gps_online =
      s_gps.valid_frames > 0 && now - s_gps.last_frame_ms <= WARDRIVE_GPS_STALE_MS;
  s_status.satellites = s_gps.gga.satellites;
  s_status.visible = s_gps.visible;
  s_status.gps_bytes = s_gps.bytes;
  s_status.checksum_errors = s_gps.checksum_errors + s_gps.malformed;
  s_status.elapsed_s = (now - s_started_ms) / WARD_MS_SECOND;
  s_status.paused = atomic_load(&s_pause);
  s_status.stopping = atomic_load(&s_stop);
  portEXIT_CRITICAL(&s_lock);
}
static bool closest_fix(uint32_t midpoint, wardrive_fix_t *out) {
  uint32_t best = WARD_FIX_OFFSET_MS + 1;
  memset(out, 0, sizeof(*out));
  for (size_t i = 0; i < WARD_FIX_HISTORY; i++) {
    int32_t delta = (int32_t)(s_history[i].received_ms - midpoint);
    uint32_t gap = (uint32_t)(delta < 0 ? -(int64_t)delta : delta);
    if (s_history[i].valid && gap < best) {
      best = gap;
      *out = s_history[i];
    }
  }
  return best <= WARD_FIX_OFFSET_MS;
}
static void wait_until(uint32_t deadline) {
  while (!atomic_load(&s_stop) && (int32_t)(deadline - millis()) > 0) {
    gps_pump();
    portENTER_CRITICAL(&s_lock);
    s_status.retry_s = (deadline - millis() + WARD_MS_SECOND - 1) / WARD_MS_SECOND;
    portEXIT_CRITICAL(&s_lock);
    vTaskDelay(pdMS_TO_TICKS(WARD_TICK_MS));
  }
  portENTER_CRITICAL(&s_lock);
  s_status.retry_s = 0;
  portEXIT_CRITICAL(&s_lock);
}
static void storage_error(esp_err_t err) {
  if (s_fd >= 0) {
    close(s_fd);
    s_fd = -1;
  }
  s_next_storage_ms = millis() + WARD_STORAGE_RETRY_MS;
  portENTER_CRITICAL(&s_lock);
  s_status.storage_ok = false;
  s_status.storage_error = err;
  s_status.storage_errors++;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGW(TAG, "Storage: %s; pending=%u", esp_err_to_name(err), s_pending_count);
}
static esp_err_t open_segment(void) {
  if (usb_msc_get_state() != USB_MSC_IDLE)
    return ESP_ERR_INVALID_STATE;
  if (!vfs_sdcard_is_mounted()) {
    esp_err_t err = vfs_sdcard_init();
    if (err != ESP_OK)
      return err;
  }
  uint64_t free_bytes = 0;
  if (vfs_get_free_space("/sdcard", &free_bytes) != ESP_OK || free_bytes < WARD_MIN_FREE_BYTES)
    return ESP_ERR_NO_MEM;
  if (mkdir(WARD_DIR, 0755) != 0 && errno != EEXIST)
    return ESP_FAIL;
  char path[WARDRIVE_PATH_BYTES];
  for (unsigned attempt = 0; attempt < 16; attempt++) {
    snprintf(path,
             sizeof(path),
             WARD_DIR "/%08lx-%04lu.jsonl",
             (unsigned long)s_status.session_id,
             (unsigned long)s_part++);
    s_fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (s_fd >= 0)
      break;
    if (errno != EEXIST)
      return ESP_FAIL;
  }
  if (s_fd < 0)
    return ESP_FAIL;
  const char header[] = "{\"type\":\"session\",\"schema\":1,\"source\":\"High "
                        "Boy\",\"scan\":\"passive\",\"position\":\"receiver_at_scan_midpoint\"}\n";
  if (write(s_fd, header, sizeof(header) - 1) != sizeof(header) - 1 || fsync(s_fd) != 0)
    return ESP_FAIL;
  portENTER_CRITICAL(&s_lock);
  snprintf(s_status.path, sizeof(s_status.path), "%s", path);
  s_status.storage_ok = true;
  s_status.storage_error = ESP_OK;
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}
static void storage_flush(void) {
  if (s_fd < 0) {
    if ((int32_t)(s_next_storage_ms - millis()) > 0)
      return;
    esp_err_t err = open_segment();
    if (err != ESP_OK) {
      storage_error(err);
      return;
    }
  }
  uint16_t written = 0;
  while (written < s_pending_count) {
    wardrive_observation_t *o = &s_pending[(s_head + written) % WARD_PENDING_MAX];
    char hex[65], position[256], line[WARD_LINE_BYTES];
    size_t len = strnlen((char *)o->ap.ssid, 32);
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
      hex[i * 2] = digits[o->ap.ssid[i] >> 4];
      hex[i * 2 + 1] = digits[o->ap.ssid[i] & 15];
    }
    hex[len * 2] = 0;
    if (o->located)
      snprintf(position,
               sizeof(position),
               "{\"latitude\":%.7f,\"longitude\":%.7f,\"altitude_m\":%.1f,\"hdop\":%.2f,"
               "\"satellites\":%u,\"date_ddmmyy\":%06lu,\"utc_ms\":%lu,\"fix_uptime_ms\":%lu}",
               o->fix.latitude,
               o->fix.longitude,
               o->fix.altitude,
               o->fix.hdop,
               o->fix.satellites,
               (unsigned long)o->fix.date,
               (unsigned long)o->fix.epoch_ms,
               (unsigned long)o->fix.received_ms);
    else
      strcpy(position, "null");
    int length =
        snprintf(line,
                 sizeof(line),
                 "{\"type\":\"ap\",\"session\":\"%08lx\",\"id\":%lu,\"cycle\":%lu,\"bssid\":\"%02x:"
                 "%02x:%02x:%02x:%02x:%02x\",\"ssid_hex\":\"%s\",\"channel\":%u,\"rssi_dbm\":%d,"
                 "\"auth\":%u,\"scan_start_ms\":%lu,\"scan_end_ms\":%lu,\"position\":%s}\n",
                 (unsigned long)s_status.session_id,
                 (unsigned long)o->id,
                 (unsigned long)o->cycle,
                 o->ap.bssid[0],
                 o->ap.bssid[1],
                 o->ap.bssid[2],
                 o->ap.bssid[3],
                 o->ap.bssid[4],
                 o->ap.bssid[5],
                 hex,
                 o->ap.channel,
                 o->ap.rssi,
                 o->ap.authmode,
                 (unsigned long)o->scan_start_ms,
                 (unsigned long)o->scan_end_ms,
                 position);
    if (length < 0 || length >= sizeof(line) || write(s_fd, line, length) != length) {
      storage_error(ESP_FAIL);
      return;
    }
    written++;
    gps_pump();
  }
  if (fsync(s_fd) != 0) {
    storage_error(ESP_FAIL);
    return;
  }
  s_head = (s_head + written) % WARD_PENDING_MAX;
  s_pending_count -= written;
  portENTER_CRITICAL(&s_lock);
  s_status.saved += written;
  s_status.buffered = s_pending_count;
  s_status.storage_ok = true;
  s_status.storage_error = ESP_OK;
  portEXIT_CRITICAL(&s_lock);
}
static void record_ap(const spi_ward_record_t *ap, uint32_t start, uint32_t end) {
  bool known = false;
  for (size_t i = 0; i < s_unique_count; i++)
    if (!memcmp(s_unique[i], ap->bssid, 6)) {
      known = true;
      break;
    }
  if (!known && s_unique_count < WARD_UNIQUE_MAX)
    memcpy(s_unique[s_unique_count++], ap->bssid, 6);
  if (s_pending_count == WARD_PENDING_MAX) {
    s_head = (s_head + 1) % WARD_PENDING_MAX;
    s_pending_count--;
    portENTER_CRITICAL(&s_lock);
    s_status.dropped++;
    portEXIT_CRITICAL(&s_lock);
  }
  wardrive_observation_t *o = &s_pending[(s_head + s_pending_count++) % WARD_PENDING_MAX];
  memset(o, 0, sizeof(*o));
  o->ap = *ap;
  o->scan_start_ms = start;
  o->scan_end_ms = end;
  o->located = closest_fix(start + (end - start) / 2, &o->fix);
  portENTER_CRITICAL(&s_lock);
  o->id = ++s_status.observations;
  o->cycle = s_status.cycles;
  s_status.unique = s_unique_count;
  s_status.unique_full = s_unique_count == WARD_UNIQUE_MAX;
  s_status.located += o->located;
  s_status.buffered = s_pending_count;
  memmove(&s_status.rows[1], &s_status.rows[0], sizeof(s_status.rows[0]) * (WARDRIVE_UI_ROWS - 1));
  wardrive_row_t *row = &s_status.rows[0];
  memset(row, 0, sizeof(*row));
  for (size_t i = 0; i < 32 && ap->ssid[i]; i++)
    row->ssid[i] = ap->ssid[i] >= 32 && ap->ssid[i] < 127 ? ap->ssid[i] : '?';
  if (!row->ssid[0])
    strcpy(row->ssid, "Rede oculta");
  row->rssi = ap->rssi;
  row->channel = ap->channel;
  row->auth = ap->authmode;
  row->located = o->located;
  portEXIT_CRITICAL(&s_lock);
}
static esp_err_t scan_cycle(uint8_t band) {
  spi_ward_status_t status;
  uint32_t start = millis();
  esp_err_t err = rpc(SPI_ID_WIFI_WARD_SCAN, &band, sizeof(band), &status, sizeof(status));
  if (err != ESP_OK)
    return err;
  uint32_t generation = status.generation;
  portENTER_CRITICAL(&s_lock);
  s_status.scanning = true;
  portEXIT_CRITICAL(&s_lock);
  while (status.busy && millis() - start < WARD_SCAN_TIMEOUT_MS) {
    // Always drain the finite C5 scan, including while the user requests stop/pause.
    uint32_t deadline = millis() + WARD_POLL_MS;
    while ((int32_t)(deadline - millis()) > 0) {
      gps_pump();
      vTaskDelay(pdMS_TO_TICKS(WARD_TICK_MS));
    }
    err = rpc(SPI_ID_WIFI_WARD_STATUS, NULL, 0, &status, sizeof(status));
    if (err != ESP_OK)
      break;
    if (status.generation != generation) {
      err = ESP_ERR_INVALID_STATE;
      break;
    }
  }
  uint32_t end = millis();
  if (err == ESP_OK && status.busy)
    err = ESP_ERR_TIMEOUT;
  if (err == ESP_OK)
    err = status.error;
  if (err == ESP_OK && status.count > SPI_WARD_MAX_RECORDS)
    err = ESP_ERR_INVALID_SIZE;
  if (err == ESP_OK) {
    portENTER_CRITICAL(&s_lock);
    s_status.cycles++;
    if (status.total > status.count)
      s_status.dropped += status.total - status.count;
    portEXIT_CRITICAL(&s_lock);
    for (uint16_t i = 0; i < status.count; i++) {
      spi_ward_request_t request = {.generation = generation, .index = i};
      spi_ward_record_t ap;
      err = rpc(SPI_ID_WIFI_WARD_RECORD, &request, sizeof(request), &ap, sizeof(ap));
      if (err != ESP_OK)
        break;
      gps_pump();
      record_ap(&ap, start, end);
    }
  }
  portENTER_CRITICAL(&s_lock);
  s_status.scanning = false;
  portEXIT_CRITICAL(&s_lock);
  return err;
}
static void worker(void *arg) {
  (void)arg;
  res_handle_t wifi = RES_HANDLE_NONE;
  bool rf = false, has_power_hold = false, has_session = false;
  s_history_next = 0;
  s_last_epoch = UINT32_MAX;
  s_next_storage_ms = 0;
  s_fd = -1;
  memset(s_history, 0, sizeof(s_history));
  wardrive_gps_init(&s_gps);
  res_request_t request = {.id = RES_WIFI, .lane = RES_LANE_MAIN, .owner_kind = RES_OWNER_UI};
  esp_err_t err = resource_acquire(&request, &wifi);
  if (err != ESP_OK)
    goto cleanup;
  if (uart_is_driver_installed(WARD_UART) || uart_is_driver_installed(UART_NUM_2) ||
      lf_rfid_field_is_on() || usb_msc_get_state() != USB_MSC_IDLE) {
    err = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  err = rf_frontend_reserve(RF_FRONTEND_LF_125KHZ);
  if (err != ESP_OK)
    goto cleanup;
  rf = true;
  lf_rfid_field_stop();
  err = power_manager_no_sleep_acquire();
  if (err != ESP_OK)
    goto cleanup;
  has_power_hold = true;
  // Release any previous GPIO output before connecting the UART matrix.
  // GPIO11 also drives the LF load gate: the reserved LF field remains off.
  const gpio_config_t pins = {.pin_bit_mask =
                                  (1ULL << GPIO_GPS_UART_RX_PIN) | (1ULL << GPIO_GPS_UART_TX_PIN),
                              .mode = GPIO_MODE_INPUT,
                              .pull_up_en = GPIO_PULLUP_DISABLE,
                              .pull_down_en = GPIO_PULLDOWN_DISABLE,
                              .intr_type = GPIO_INTR_DISABLE};
  uart_config_t config = {.baud_rate = WARD_BAUD,
                          .data_bits = UART_DATA_8_BITS,
                          .parity = UART_PARITY_DISABLE,
                          .stop_bits = UART_STOP_BITS_1,
                          .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                          .source_clk = UART_SCLK_DEFAULT};
  err = gpio_config(&pins);
  if (err == ESP_OK)
    err = uart_param_config(WARD_UART, &config);
  if (err == ESP_OK)
    err = uart_driver_install(WARD_UART, WARD_RING_BYTES, 0, WARD_QUEUE_EVENTS, &s_uart_queue, 0);
  if (err == ESP_OK) {
    s_uart_installed = true;
    // No commands are sent, but GPS RX must see UART idle, not a permanent break.
    err = uart_set_pin(WARD_UART,
                       GPIO_GPS_UART_TX_PIN,
                       GPIO_GPS_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
  }
  portENTER_CRITICAL(&s_lock);
  s_status.gps_error = err;
  portEXIT_CRITICAL(&s_lock);
  if (err != ESP_OK)
    goto cleanup;
  has_session = true;
  uint32_t backoff = WARD_SCAN_GAP_MS;
  bool radio_ready = false;
  storage_flush();
  while (!atomic_load(&s_stop)) {
    gps_pump();
    if (atomic_load(&s_pause)) {
      storage_flush();
      wait_until(millis() + WARD_MS_SECOND);
      continue;
    }
    if (!resource_handle_valid(wifi)) {
      err = ESP_ERR_INVALID_STATE;
      break;
    }
    if (!radio_ready) {
      spi_header_t response = {0};
      err = spi_bridge_send_command(SPI_ID_WIFI_STOP, NULL, 0, &response, NULL, 0, WARD_RPC_MS);
      radio_ready = err == ESP_OK;
    }
    if (radio_ready)
      err = scan_cycle(s_status.band);
    portENTER_CRITICAL(&s_lock);
    s_status.radio_error = err;
    if (err != ESP_OK)
      s_status.scan_errors++;
    portEXIT_CRITICAL(&s_lock);
    storage_flush();
    if (err != ESP_OK) {
      backoff = backoff < WARD_RETRY_MAX_MS / 2 ? backoff * 2 : WARD_RETRY_MAX_MS;
    } else
      backoff = WARD_SCAN_GAP_MS;
    wait_until(millis() + backoff);
  }
cleanup:
  if (has_session) {
    s_next_storage_ms = 0;
    storage_flush();
  }
  if (s_fd >= 0) {
    int fd = s_fd;
    s_fd = -1;
    if (close(fd) != 0)
      storage_error(ESP_FAIL);
  }
  if (s_uart_installed) {
    uart_driver_delete(WARD_UART);
    s_uart_installed = false;
    s_uart_queue = NULL;
  }
  if (rf) {
    gpio_reset_pin(GPIO_GPS_UART_RX_PIN);
    gpio_reset_pin(GPIO_GPS_UART_TX_PIN);
    gpio_set_level(GPIO_GPS_UART_TX_PIN, 0);
    gpio_set_direction(GPIO_GPS_UART_TX_PIN, GPIO_MODE_OUTPUT);
    rf_frontend_unreserve(RF_FRONTEND_LF_125KHZ);
  }
  if (has_power_hold)
    power_manager_no_sleep_release();
  if (wifi != RES_HANDLE_NONE)
    resource_release(wifi);
  portENTER_CRITICAL(&s_lock);
  if (err != ESP_OK)
    s_status.radio_error = err;
  uint32_t saved = s_status.saved;
  uint16_t pending = s_pending_count;
  s_status.running = false;
  s_status.stopping = false;
  s_status.scanning = false;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGI(TAG, "Session ended: saved=%lu pending=%u", (unsigned long)saved, pending);
  vTaskDelete(NULL);
}
