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

#include "tos_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "storage_init.h"
#include "tos_storage_paths.h"

static const char *TAG = "TOS_LOG";

#define LOG_DIR        TOS_PATH_LOGS
#define LOG_FILE_FMT   LOG_DIR "/sys.%d.log"
#define LOG_MAX_FILES  5
#define LOG_MAX_SIZE   (2 * 1024 * 1024) // 2 MB per file (total max = 10 MB)
#define LOG_PATH_LEN   64

static FILE *s_log_file = NULL;
static vprintf_like_t s_original_vprintf = NULL;

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
 * Rotate log files:
 *   sys.5.log -> deleted
 *   sys.4.log -> sys.5.log
 *   sys.3.log -> sys.4.log
 *   sys.2.log -> sys.3.log
 *   sys.1.log -> sys.2.log
 *   (new sys.1.log created)
 */
static void rotate_logs(void) {
  // Close current file before rotating
  if (s_log_file != NULL) {
    fclose(s_log_file);
    s_log_file = NULL;
  }

  char old_path[LOG_PATH_LEN];
  char new_path[LOG_PATH_LEN];

  // Delete the oldest file
  build_path(old_path, LOG_MAX_FILES);
  remove(old_path);

  // Shift files: N -> N+1
  for (int i = LOG_MAX_FILES - 1; i >= 1; i--) {
    build_path(old_path, i);
    build_path(new_path, i + 1);
    rename(old_path, new_path);
  }

  // Open fresh sys.1.log
  build_path(old_path, 1);
  s_log_file = fopen(old_path, "w");
}

static int log_to_file(const char *fmt, va_list args) {
  // Always print to serial
  int ret = s_original_vprintf(fmt, args);

  if (s_log_file == NULL || !storage_is_mounted()) {
    return ret;
  }

  // Check if rotation is needed
  long size = ftell(s_log_file);
  if (size >= LOG_MAX_SIZE) {
    rotate_logs();
    if (s_log_file == NULL) {
      return ret;
    }
  }

  // Write to file
  va_list args_copy;
  va_copy(args_copy, args);
  vfprintf(s_log_file, fmt, args_copy);
  va_end(args_copy);
  fflush(s_log_file);

  return ret;
}

esp_err_t tos_log_init(void) {
  if (s_log_file != NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!storage_is_mounted()) {
    return ESP_ERR_INVALID_STATE;
  }

  // Open current log file (append to existing sys.1.log)
  char path[LOG_PATH_LEN];
  build_path(path, 1);
  s_log_file = fopen(path, "a");
  if (s_log_file == NULL) {
    ESP_LOGE(TAG, "Failed to open log file: %s", path);
    return ESP_FAIL;
  }

  // Check if rotation is needed on startup
  if (get_file_size(path) >= LOG_MAX_SIZE) {
    rotate_logs();
  }

  // Redirect all ESP_LOGx output through our handler
  s_original_vprintf = esp_log_set_vprintf(log_to_file);

  ESP_LOGI(TAG, "Log system initialized (file: %s, max: %d files x %d MB)",
           path, LOG_MAX_FILES, LOG_MAX_SIZE / (1024 * 1024));

  return ESP_OK;
}

void tos_log_deinit(void) {
  if (s_original_vprintf != NULL) {
    esp_log_set_vprintf(s_original_vprintf);
    s_original_vprintf = NULL;
  }

  if (s_log_file != NULL) {
    fclose(s_log_file);
    s_log_file = NULL;
  }

  ESP_LOGI(TAG, "Log system deinitialized");
}
