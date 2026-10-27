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

#include "script_runner.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sys_prio.h"
#include "script_bindings.h"
#include "tos_app_ctx.h"
#include "resource_mgr.h"
#include "ui_manager.h"

#define SCRIPT_TASK_STACK_BYTES 16384
#define SCRIPT_POLL_MS          10
#define MICROSECONDS_PER_MS     1000
#define SCRIPT_PATH_MAX         (sizeof(SCRIPT_RUNNER_DIRECTORY) + SCRIPT_RUNNER_NAME_MAX)

static const char *TAG = "SCRIPT_RUNNER";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
EXT_RAM_BSS_ATTR static script_runner_snapshot_t s_snapshot;
static bool s_is_stop_requested;
static bool s_is_resource_lost;
static bool s_has_display;
static uint32_t s_generation;
static int64_t s_started_ms;
static size_t s_output_length;

static int64_t clock_ms(void *ctx) {
  (void)ctx;
  return esp_timer_get_time() / MICROSECONDS_PER_MS;
}

static bool poll(void *ctx) {
  (void)ctx;
  portENTER_CRITICAL(&s_mux);
  bool is_stopped = s_is_stop_requested || s_is_resource_lost ||
                    clock_ms(NULL) - s_started_ms >= SCRIPT_RUNNER_TIMEOUT_MS;
  portEXIT_CRITICAL(&s_mux);
  if (!is_stopped)
    vTaskDelay(1);
  return is_stopped;
}

static void sleep_ms(void *ctx, uint32_t milliseconds) {
  int64_t deadline = clock_ms(NULL) + milliseconds;
  while (clock_ms(NULL) < deadline && !poll(ctx))
    vTaskDelay(pdMS_TO_TICKS(SCRIPT_POLL_MS));
}

static void output(void *ctx, const char *text, size_t length) {
  (void)ctx;
  portENTER_CRITICAL(&s_mux);
  size_t available = SCRIPT_RUNNER_OUTPUT_MAX - 1 - s_output_length;
  size_t count = length < available ? length : available;
  // Keep the first bounded transcript; later output cannot grow the heap.
  memcpy(s_snapshot.output + s_output_length, text, count);
  s_output_length += count;
  s_snapshot.output[s_output_length] = '\0';
  portEXIT_CRITICAL(&s_mux);
}

static void *allocate(size_t length) {
  return heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static bool stop_requested(void *ctx) {
  (void)ctx;
  portENTER_CRITICAL(&s_mux);
  bool is_stopped =
      s_is_stop_requested || clock_ms(NULL) - s_started_ms >= SCRIPT_RUNNER_TIMEOUT_MS;
  portEXIT_CRITICAL(&s_mux);
  return is_stopped;
}

static bool resource_lost(void *ctx) {
  (void)ctx;
  portENTER_CRITICAL(&s_mux);
  bool is_lost = s_is_resource_lost;
  portEXIT_CRITICAL(&s_mux);
  return is_lost;
}

static void set_resource_lost(void *ctx, bool is_lost) {
  (void)ctx;
  portENTER_CRITICAL(&s_mux);
  s_is_resource_lost = is_lost;
  portEXIT_CRITICAL(&s_mux);
}

static void set_display(bool is_open) {
  portENTER_CRITICAL(&s_mux);
  s_has_display = is_open;
  portEXIT_CRITICAL(&s_mux);
}

static void run_script(void *ctx) {
  (void)ctx;
  char path[SCRIPT_PATH_MAX];
  char name[SCRIPT_RUNNER_NAME_MAX];
  portENTER_CRITICAL(&s_mux);
  memcpy(name, s_snapshot.name, sizeof(name));
  portEXIT_CRITICAL(&s_mux);
  snprintf(path, sizeof(path), "%s/%s", SCRIPT_RUNNER_DIRECTORY, name);
  script_bindings_t bindings = {.api = tos_api_get(), .display = set_display};
  tos_app_ctx_t app_ctx = {.id = name,
                           .poll_stop = stop_requested,
                           .poll_resource_lost = resource_lost,
                           .set_resource_lost = set_resource_lost,
                           .ui_return_screen = SCREEN_SCRIPTS};
  bool is_bound = false;
  const script_engine_host_t host = {.bindings = &bindings,
                                     .output = output,
                                     .poll = poll,
                                     .sleep = sleep_ms,
                                     .clock = clock_ms,
                                     .alloc = allocate};
  script_engine_request_t request = {.name = name, .host = &host};
  request.language = strcmp(strrchr(name, '.'), ".lua") == 0 ? SCRIPT_ENGINE_LUA : SCRIPT_ENGINE_JS;
  script_engine_result_t result = SCRIPT_ENGINE_ERROR;
  FILE *file = NULL;
  char *source = NULL;
  file = fopen(path, "rb");
  if (file == NULL) {
    snprintf(request.error, sizeof(request.error), "Cannot open script");
    goto cleanup;
  }
  if (fseek(file, 0, SEEK_END) != 0)
    goto read_error;
  long length = ftell(file);
  if (length <= 0 || length > SCRIPT_ENGINE_SOURCE_MAX) {
    snprintf(request.error, sizeof(request.error), "Script must contain 1..65536 bytes");
    goto cleanup;
  }
  rewind(file);
  source = allocate((size_t)length + 1);
  if (source == NULL) {
    ESP_LOGE(TAG, "Failed to allocate script source");
    result = SCRIPT_ENGINE_NO_MEMORY;
    goto cleanup;
  }
  if (fread(source, 1, (size_t)length, file) != (size_t)length || ferror(file))
    goto read_error;
  source[length] = '\0';
  fclose(file);
  file = NULL;
  request.source = source;
  request.length = (size_t)length;
  if (script_bindings_requested_caps(source, &bindings.caps) != ESP_OK) {
    snprintf(request.error, sizeof(request.error), "Invalid script capabilities");
    goto cleanup;
  }
  if (bindings.caps != 0) {
    portENTER_CRITICAL(&s_mux);
    s_snapshot.requested_caps = bindings.caps;
    s_snapshot.is_waiting_consent = true;
    portEXIT_CRITICAL(&s_mux);
    bool is_waiting = true;
    while (is_waiting && !poll(NULL)) {
      portENTER_CRITICAL(&s_mux);
      is_waiting = s_snapshot.is_waiting_consent;
      portEXIT_CRITICAL(&s_mux);
      vTaskDelay(pdMS_TO_TICKS(SCRIPT_POLL_MS));
    }
    if (poll(NULL)) {
      result = SCRIPT_ENGINE_STOPPED;
      goto cleanup;
    }
  }
  app_ctx.granted_caps = bindings.caps;
  if (tos_app_ctx_bind(xTaskGetCurrentTaskHandle(), &app_ctx) != ESP_OK) {
    snprintf(request.error, sizeof(request.error), "Cannot bind script permissions");
    goto cleanup;
  }
  is_bound = true;
  result = script_engine_run(&request);
  goto cleanup;
read_error:
  snprintf(request.error, sizeof(request.error), "Cannot read complete script");
cleanup:
  script_bindings_close(&bindings);
  if (is_bound) {
    resource_release_owner_task(xTaskGetCurrentTaskHandle());
    if (bindings.caps & TOS_CAP_UI) {
      bindings.api->ui->screen_close();
      bindings.api->led_set(0, 0, 0);
    }
    tos_app_ctx_unbind(xTaskGetCurrentTaskHandle());
  }
  set_display(false);
  if (file != NULL)
    fclose(file);
  free(source);
  portENTER_CRITICAL(&s_mux);
  s_snapshot.result = result;
  memcpy(s_snapshot.error, request.error, sizeof(s_snapshot.error));
  s_snapshot.is_running = false;
  s_snapshot.is_waiting_consent = false;
  portEXIT_CRITICAL(&s_mux);
  ESP_LOGI(TAG, "Script %s finished (%d)", name, result);
  vTaskDeleteWithCaps(NULL);
}

bool script_runner_name_is_valid(const char *name) {
  if (name == NULL)
    return false;
  size_t length = strnlen(name, SCRIPT_RUNNER_NAME_MAX);
  if (length == 0 || length >= SCRIPT_RUNNER_NAME_MAX || name[0] == '.')
    return false;
  for (size_t i = 0; i < length; ++i) {
    unsigned char value = (unsigned char)name[i];
    if (!isalnum(value) && value != '_' && value != '-' && value != '.')
      return false;
  }
  const char *extension = strrchr(name, '.');
  return extension != NULL && (strcmp(extension, ".js") == 0 || strcmp(extension, ".lua") == 0);
}

esp_err_t script_runner_start(const char *name) {
  if (!script_runner_name_is_valid(name))
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_mux);
  if (s_snapshot.is_running) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  memset(&s_snapshot, 0, sizeof(s_snapshot));
  memcpy(s_snapshot.name, name, strlen(name) + 1);
  s_snapshot.is_running = true;
  s_is_stop_requested = false;
  s_is_resource_lost = false;
  s_has_display = false;
  if (++s_generation == 0)
    ++s_generation;
  s_snapshot.generation = s_generation;
  s_output_length = 0;
  s_started_ms = clock_ms(NULL);
  portEXIT_CRITICAL(&s_mux);
  if (xTaskCreatePinnedToCoreWithCaps(run_script,
                                      "script",
                                      SCRIPT_TASK_STACK_BYTES,
                                      NULL,
                                      SYS_PRIO_BACKGROUND_LO,
                                      NULL,
                                      SYS_CORE_RADIO,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create script worker");
    portENTER_CRITICAL(&s_mux);
    s_snapshot.is_running = false;
    s_snapshot.result = SCRIPT_ENGINE_NO_MEMORY;
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t script_runner_authorize(uint32_t generation, bool is_allowed) {
  portENTER_CRITICAL(&s_mux);
  if (!s_snapshot.is_running || !s_snapshot.is_waiting_consent ||
      s_snapshot.generation != generation) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  s_snapshot.is_waiting_consent = false;
  if (!is_allowed)
    s_is_stop_requested = true;
  else
    s_started_ms = clock_ms(NULL);
  portEXIT_CRITICAL(&s_mux);
  return ESP_OK;
}

bool script_runner_has_display(void) {
  portENTER_CRITICAL(&s_mux);
  bool has_display = s_has_display;
  portEXIT_CRITICAL(&s_mux);
  return has_display;
}

void script_runner_stop(void) {
  portENTER_CRITICAL(&s_mux);
  s_is_stop_requested = true;
  portEXIT_CRITICAL(&s_mux);
}

void script_runner_snapshot(script_runner_snapshot_t *out_snapshot) {
  if (out_snapshot == NULL)
    return;
  portENTER_CRITICAL(&s_mux);
  *out_snapshot = s_snapshot;
  portEXIT_CRITICAL(&s_mux);
}
