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

#include "tos_app_ctx.h"

#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "host_link_dispatch.h"

// One binding per running app task, plus headroom for handoffs.
#define TOS_MAX_APP_BINDINGS 8

typedef struct {
  void *task;
  tos_app_ctx_t *ctx;
} binding_t;

static binding_t s_bindings[TOS_MAX_APP_BINDINGS];
static SemaphoreHandle_t s_lock = NULL;

static bool ensure_lock(void) {
  if (s_lock == NULL)
    s_lock = xSemaphoreCreateMutex();
  return s_lock != NULL;
}

esp_err_t tos_app_ctx_bind(void *task, tos_app_ctx_t *ctx) {
  if (task == NULL || ctx == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!ensure_lock())
    return ESP_ERR_NO_MEM;

  esp_err_t err = ESP_ERR_NO_MEM;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  int free_slot = -1;
  for (int i = 0; i < TOS_MAX_APP_BINDINGS; i++) {
    if (s_bindings[i].task == task) { // rebind
      s_bindings[i].ctx = ctx;
      err = ESP_OK;
      goto out;
    }
    if (free_slot < 0 && s_bindings[i].task == NULL)
      free_slot = i;
  }
  if (free_slot >= 0) {
    s_bindings[free_slot].task = task;
    s_bindings[free_slot].ctx = ctx;
    err = ESP_OK;
  }

out:
  xSemaphoreGive(s_lock);
  return err;
}

void tos_app_ctx_unbind(void *task) {
  if (s_lock == NULL || task == NULL)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int i = 0; i < TOS_MAX_APP_BINDINGS; i++) {
    if (s_bindings[i].task == task) {
      s_bindings[i].task = NULL;
      s_bindings[i].ctx = NULL;
      break;
    }
  }
  xSemaphoreGive(s_lock);
}

tos_app_ctx_t *tos_app_ctx_current(void) {
  if (s_lock == NULL)
    return NULL;
  void *self = xTaskGetCurrentTaskHandle();
  tos_app_ctx_t *ctx = NULL;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int i = 0; i < TOS_MAX_APP_BINDINGS; i++) {
    if (s_bindings[i].task == self) {
      ctx = s_bindings[i].ctx;
      break;
    }
  }
  xSemaphoreGive(s_lock);
  return ctx;
}

bool tos_app_cap_check(uint32_t cap) {
  tos_app_ctx_t *ctx = tos_app_ctx_current();
  if (ctx == NULL)
    return true; // trusted firmware caller
  return (ctx->granted_caps & cap) == cap;
}

void tos_app_ctx_signal_resource_lost(void *task) {
  if (s_lock == NULL || task == NULL)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (int i = 0; i < TOS_MAX_APP_BINDINGS; i++) {
    if (s_bindings[i].task == task) {
      tos_app_ctx_t *ctx = s_bindings[i].ctx;
      if (ctx != NULL && ctx->res_lost != NULL)
        *ctx->res_lost = true;
      break;
    }
  }
  xSemaphoreGive(s_lock);
}

void tos_app_ctx_clear_resource_lost(void) {
  tos_app_ctx_t *ctx = tos_app_ctx_current();
  if (ctx != NULL && ctx->res_lost != NULL)
    *ctx->res_lost = false;
}

static const char *REG_TAG = "TOS_APP_CTX";

void tos_app_regs_record_console(tos_app_regs_t *regs, const char *cmd) {
  if (regs == NULL || cmd == NULL || regs->count >= TOS_MAX_APP_REGS)
    return;
  tos_app_reg_t *r = &regs->items[regs->count++];
  r->kind = 0;
  strlcpy(r->console_cmd, cmd, sizeof(r->console_cmd));
}

void tos_app_regs_record_hostlink(tos_app_regs_t *regs, uint8_t category, uint8_t op_min) {
  if (regs == NULL || regs->count >= TOS_MAX_APP_REGS)
    return;
  tos_app_reg_t *r = &regs->items[regs->count++];
  r->kind = 1;
  r->hl_category = category;
  r->hl_op_min = op_min;
}

void tos_app_regs_teardown(tos_app_regs_t *regs) {
  if (regs == NULL)
    return;
  for (int i = 0; i < regs->count; i++) {
    tos_app_reg_t *r = &regs->items[i];
    if (r->kind == 0) {
      esp_console_cmd_deregister(r->console_cmd);
      ESP_LOGI(REG_TAG, "unregistered app console command '%s'", r->console_cmd);
    } else {
      host_link_unregister(r->hl_category, r->hl_op_min);
      ESP_LOGI(REG_TAG, "unregistered app host_link op %u/%u", r->hl_category, r->hl_op_min);
    }
  }
  regs->count = 0;
}
