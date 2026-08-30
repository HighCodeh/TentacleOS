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

#include "tos_api.h"

#include <stdlib.h>

#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led_control.h"
#include "tos_app_ctx.h"
#include "tos_arena.h"

static void api_log(tos_log_level_t level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  esp_log_writev((esp_log_level_t)level, tag, fmt, ap);
  va_end(ap);
}

static void *api_mem_alloc(size_t size) {
  tos_app_ctx_t *ctx = tos_app_ctx_current();
  if (ctx != NULL && ctx->arena != NULL)
    return tos_arena_alloc(ctx->arena, size);
  return malloc(size); // firmware caller: untracked
}

static void api_mem_free(void *ptr) {
  tos_app_ctx_t *ctx = tos_app_ctx_current();
  if (ctx != NULL && ctx->arena != NULL) {
    tos_arena_free(ctx->arena, ptr);
    return;
  }
  free(ptr);
}

static void api_delay_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

static void api_yield(void) {
  vTaskDelay(1); // one tick: yields and lets the idle task feed the TWDT
}

static int api_should_stop(void) {
  tos_app_ctx_t *ctx = tos_app_ctx_current();
  return (ctx != NULL && ctx->stop != NULL && *ctx->stop) ? 1 : 0;
}

static int api_resource_lost(void) {
  tos_app_ctx_t *ctx = tos_app_ctx_current();
  return (ctx != NULL && ctx->res_lost != NULL && *ctx->res_lost) ? 1 : 0;
}

static esp_err_t api_host_link_register(const host_link_handler_t *h) {
  if (!tos_app_cap_check(TOS_CAP_HOSTLINK))
    return ESP_ERR_NOT_SUPPORTED;
  esp_err_t e = host_link_register(h);
  if (e == ESP_OK && h != NULL) {
    tos_app_ctx_t *ctx = tos_app_ctx_current();
    if (ctx != NULL)
      tos_app_regs_record_hostlink(ctx->regs, h->category, h->op_min);
  }
  return e;
}

static esp_err_t api_host_link_unregister(uint8_t category, uint8_t op_min) {
  if (!tos_app_cap_check(TOS_CAP_HOSTLINK))
    return ESP_ERR_NOT_SUPPORTED;
  return host_link_unregister(category, op_min);
}

static esp_err_t
api_console_register(const char *command, const char *help, int (*func)(int, char **)) {
  if (!tos_app_cap_check(TOS_CAP_CONSOLE))
    return ESP_ERR_NOT_SUPPORTED;
  const esp_console_cmd_t cmd = {.command = command, .help = help, .hint = NULL, .func = func};
  esp_err_t e = esp_console_cmd_register(&cmd);
  if (e == ESP_OK) {
    tos_app_ctx_t *ctx = tos_app_ctx_current();
    if (ctx != NULL)
      tos_app_regs_record_console(ctx->regs, command);
  }
  return e;
}

static esp_err_t api_console_unregister(const char *command) {
  if (!tos_app_cap_check(TOS_CAP_CONSOLE))
    return ESP_ERR_NOT_SUPPORTED;
  return esp_console_cmd_deregister(command);
}

static esp_err_t api_led_set(uint8_t r, uint8_t g, uint8_t b) {
  if (!tos_app_cap_check(TOS_CAP_UI))
    return ESP_ERR_NOT_SUPPORTED;
  led_set_color(r, g, b);
  return ESP_OK;
}

extern const tos_wifi_api_t tos_wifi_api_impl; // tos_api_wifi.c
extern const tos_ble_api_t tos_ble_api_impl;   // tos_api_ble.c
extern const tos_ui_api_t tos_ui_api_impl;     // tos_api_ui.c
extern const tos_fs_api_t tos_fs_api_impl;     // tos_api_fs.c

static const tos_api_t s_api = {
    .abi_major = TOS_ABI_VERSION_MAJOR,
    .abi_minor = TOS_ABI_VERSION_MINOR,
    .log = api_log,
    .mem_alloc = api_mem_alloc,
    .mem_free = api_mem_free,
    .delay_ms = api_delay_ms,
    .yield = api_yield,
    .should_stop = api_should_stop,
    .resource_lost = api_resource_lost,
    .host_link_register = api_host_link_register,
    .host_link_unregister = api_host_link_unregister,
    .console_register = api_console_register,
    .console_unregister = api_console_unregister,
    .led_set = api_led_set,
    .wifi = &tos_wifi_api_impl,
    .ble = &tos_ble_api_impl,
    .ui = &tos_ui_api_impl,
    .fs = &tos_fs_api_impl,
};

const tos_api_t *tos_api_get(void) {
  return &s_api;
}
