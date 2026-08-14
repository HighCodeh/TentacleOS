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

#include "tos_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "storage_init.h"
#include "tos_storage_paths.h"

static const char *TAG = "TOS_LOG";

#define LOG_DIR       TOS_PATH_LOGS
#define LOG_FILE_FMT  LOG_DIR "/sys.%d.log"
#define LOG_MAX_FILES 5
#define LOG_MAX_SIZE  (2 * 1024 * 1024) // 2 MB per file (total max = 10 MB)
#define LOG_PATH_LEN  64

// The log hook runs in the context of whichever task called ESP_LOGx, so it may
// never touch the filesystem: doing so put a flash write (cache disabled, non-IRAM
// interrupts masked) on the hot path of radio/UI tasks. Instead it formats the
// line and appends it to a RAM buffer under a mutex; a dedicated task drains the
// buffer to flash in batches, off the logging tasks' critical paths, and is the
// only code that opens, writes, flushes, or rotates the file.
#define LOG_BUF_SIZE       2048              // RAM buffer, double-buffered on flush
#define LOG_LINE_MAX       256               // max bytes captured from one log line
#define LOG_FLUSH_MS       1000              // periodic drain interval
#define LOG_HIGH_WATER     (LOG_BUF_SIZE * 3 / 4) // early-drain threshold for bursts
#define LOG_LOCK_WAIT_MS   50                // append gives up rather than block
#define LOG_TASK_STACK     4096
#define LOG_TASK_PRIO SYS_PRIO_BACKGROUND
#define LOG_STOP_WAIT_MS   2000

static FILE *s_log_file = NULL;
static vprintf_like_t s_original_vprintf = NULL;

static SemaphoreHandle_t s_lock = NULL;      // guards s_buf, s_buf_len, s_dropped
static SemaphoreHandle_t s_stop_done = NULL; // flush task signals teardown complete
static TaskHandle_t s_flush_task = NULL;
static volatile bool s_ready = false;
static volatile bool s_stop = false;

static uint8_t s_buf[LOG_BUF_SIZE];          // accumulation buffer (append path)
static uint8_t s_scratch[LOG_BUF_SIZE];      // drain buffer (flush task only)
static size_t s_buf_len = 0;
static size_t s_dropped = 0;                 // bytes dropped while buffer was full

static void build_path(char *out_path, int index) {
  snprintf(out_path, LOG_PATH_LEN, LOG_FILE_FMT, index);
}

static long get_file_size(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    return 0;
  }
  return st.st_size;
}

/**
 * Rotate log files (flush task / init only, never the append path):
 *   sys.5.log -> deleted
 *   sys.4.log -> sys.5.log
 *   sys.3.log -> sys.4.log
 *   sys.2.log -> sys.3.log
 *   sys.1.log -> sys.2.log
 *   (new sys.1.log created)
 */
static void rotate_logs(void) {
  if (s_log_file != NULL) {
    fclose(s_log_file);
    s_log_file = NULL;
  }

  char old_path[LOG_PATH_LEN];
  char new_path[LOG_PATH_LEN];

  build_path(old_path, LOG_MAX_FILES);
  remove(old_path);

  for (int i = LOG_MAX_FILES - 1; i >= 1; i--) {
    build_path(old_path, i);
    build_path(new_path, i + 1);
    rename(old_path, new_path);
  }

  build_path(old_path, 1);
  s_log_file = fopen(old_path, "w");
}

// Move the accumulated buffer to flash. Holds the lock only for the swap so
// logging tasks never wait on flash. All file I/O and rotation happen here.
static void flush_once(void) {
  if (s_log_file == NULL) {
    return;
  }

  size_t len = 0;
  size_t dropped = 0;

  if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
    if (s_buf_len > 0) {
      memcpy(s_scratch, s_buf, s_buf_len);
      len = s_buf_len;
      s_buf_len = 0;
    }
    dropped = s_dropped;
    s_dropped = 0;
    xSemaphoreGive(s_lock);
  }

  if (len == 0 && dropped == 0) {
    return;
  }

  if (len > 0) {
    fwrite(s_scratch, 1, len, s_log_file);
  }
  if (dropped > 0) {
    fprintf(s_log_file, "\n[TOS_LOG] %u bytes dropped (buffer full)\n", (unsigned)dropped);
  }
  fflush(s_log_file);

  if (ftell(s_log_file) >= LOG_MAX_SIZE) {
    rotate_logs();
  }
}

static void flush_task(void *pvParameters) {
  (void)pvParameters;

  while (!s_stop) {
    // Wake on the periodic tick or on an early-drain notification from a burst.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LOG_FLUSH_MS));
    flush_once();
  }

  flush_once(); // final drain on teardown
  xSemaphoreGive(s_stop_done);
  vTaskDelete(NULL);
}

static int log_to_file(const char *fmt, va_list args) {
  // Always print to serial first (unbuffered, unchanged behavior).
  int ret = s_original_vprintf(fmt, args);

  if (!s_ready || !storage_is_mounted()) {
    return ret;
  }

  // This is the global vprintf hook, so it runs on whatever task is logging -
  // keep it off the caller's stack. Take the lock first, then format into a
  // shared static buffer (guarded by the same lock) instead of a stack temp.
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOG_LOCK_WAIT_MS)) != pdTRUE) {
    return ret; // contended: skip file copy, serial already has the line
  }

  // vsnprintf returns the length it would have written; clamp to what fits
  // (drops the trailing NUL and any overflow of an unusually long line).
  static char line[LOG_LINE_MAX];
  va_list ap;
  va_copy(ap, args);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    xSemaphoreGive(s_lock);
    return ret;
  }
  size_t wlen = (n >= (int)sizeof(line)) ? (sizeof(line) - 1) : (size_t)n;

  bool over_high_water = false;
  if (s_buf_len + wlen > LOG_BUF_SIZE) {
    // Drop the whole line so the file never holds a torn record.
    s_dropped += wlen;
  } else {
    memcpy(s_buf + s_buf_len, line, wlen);
    s_buf_len += wlen;
    over_high_water = (s_buf_len >= LOG_HIGH_WATER);
  }
  xSemaphoreGive(s_lock);

  if (over_high_water && s_flush_task != NULL) {
    xTaskNotifyGive(s_flush_task);
  }

  return ret;
}

esp_err_t tos_log_init(void) {
  if (s_ready) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!storage_is_mounted()) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = ESP_OK;

  // Open current log file (append to existing sys.1.log).
  char path[LOG_PATH_LEN];
  build_path(path, 1);
  s_log_file = fopen(path, "a");
  if (s_log_file == NULL) {
    ESP_LOGE(TAG, "Failed to open log file: %s", path);
    return ESP_FAIL;
  }

  // Rotate on startup if the current file is already full (single-threaded here,
  // before the hook and flush task exist).
  if (get_file_size(path) >= LOG_MAX_SIZE) {
    rotate_logs();
    if (s_log_file == NULL) {
      ESP_LOGE(TAG, "Failed to reopen log file after startup rotation");
      return ESP_FAIL;
    }
  }

  s_lock = xSemaphoreCreateMutex();
  s_stop_done = xSemaphoreCreateBinary();
  if (s_lock == NULL || s_stop_done == NULL) {
    err = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  s_buf_len = 0;
  s_dropped = 0;
  s_stop = false;

  if (xTaskCreatePinnedToCore(flush_task, "tos_log", LOG_TASK_STACK, NULL, LOG_TASK_PRIO, &s_flush_task, SYS_CORE_RADIO) !=
      pdPASS) {
    ESP_LOGE(TAG, "Failed to create log flush task");
    err = ESP_FAIL;
    goto cleanup;
  }

  // Install the hook last, once the buffer and drain task are ready to receive.
  s_ready = true;
  s_original_vprintf = esp_log_set_vprintf(log_to_file);

  ESP_LOGI(TAG,
           "Log system initialized (file: %s, %d files x %d MB, buffer %d B, flush %d ms)",
           path,
           LOG_MAX_FILES,
           LOG_MAX_SIZE / (1024 * 1024),
           LOG_BUF_SIZE,
           LOG_FLUSH_MS);

  return ESP_OK;

cleanup:
  if (s_flush_task != NULL) {
    s_flush_task = NULL;
  }
  if (s_stop_done != NULL) {
    vSemaphoreDelete(s_stop_done);
    s_stop_done = NULL;
  }
  if (s_lock != NULL) {
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
  }
  if (s_log_file != NULL) {
    fclose(s_log_file);
    s_log_file = NULL;
  }
  return err;
}

void tos_log_deinit(void) {
  // Restore the default handler first so no new lines are buffered during teardown.
  if (s_original_vprintf != NULL) {
    esp_log_set_vprintf(s_original_vprintf);
    s_original_vprintf = NULL;
  }
  s_ready = false;

  // Stop the flush task and let it drain what is left.
  if (s_flush_task != NULL) {
    s_stop = true;
    xTaskNotifyGive(s_flush_task);
    if (s_stop_done != NULL) {
      xSemaphoreTake(s_stop_done, pdMS_TO_TICKS(LOG_STOP_WAIT_MS));
    }
    s_flush_task = NULL;
  }

  if (s_log_file != NULL) {
    fclose(s_log_file);
    s_log_file = NULL;
  }
  if (s_stop_done != NULL) {
    vSemaphoreDelete(s_stop_done);
    s_stop_done = NULL;
  }
  if (s_lock != NULL) {
    vSemaphoreDelete(s_lock);
    s_lock = NULL;
  }

  ESP_LOGI(TAG, "Log system deinitialized");
}
