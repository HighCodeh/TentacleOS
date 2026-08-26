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

#include "tos_app_mgr.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "led_control.h"
#include "sys_prio.h"
#include "tos_app_ctx.h"
#include "tos_arena.h"
#include "tos_elf.h"
#include "tos_grants.h"
#include "tos_hb.h"

static const char *TAG = "TOS_APP_MGR";

#define APP_TASK_STACK   8192
#define APP_ARENA_BUDGET (1024 * 1024) // 1 MB per app

typedef struct {
  volatile bool active;
  volatile bool stop;
  volatile bool cleaning;
  bool has_img;
  TaskHandle_t task;
  const tos_api_t *api;
  uint8_t *hb_copy;
  const uint8_t *elf;
  size_t elf_len;
  uint32_t caps;
  tos_arena_t *arena;
  tos_elf_image_t img;
  tos_app_regs_t regs;
  char name[TOS_APP_NAME_CAP];
} app_slot_t;

static app_slot_t s_apps[TOS_APP_MAX];
static SemaphoreHandle_t s_mtx = NULL;

static bool ensure_mtx(void) {
  if (s_mtx == NULL)
    s_mtx = xSemaphoreCreateMutex();
  return s_mtx != NULL;
}

// Exactly one of {natural exit, force-kill} tears a slot down. The winner sets
// its cleaning flag; the loser must not touch it.
static bool claim_cleanup(app_slot_t *a) {
  bool won = false;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (!a->cleaning) {
    a->cleaning = true;
    won = true;
  }
  xSemaphoreGive(s_mtx);
  return won;
}

// Reverts everything the app touched. Runs once per app, from the app task
// (natural exit) or the killer (force-kill).
static void finish_slot(app_slot_t *a) {
  tos_app_ctx_unbind(a->task); // covers force-kill, which never unbinds itself
  tos_app_regs_teardown(&a->regs);
  if (a->caps & TOS_CAP_UI)
    led_clear(); // only if this app could have driven the LED
  size_t leaked = tos_arena_used(a->arena);
  if (leaked > 0)
    ESP_LOGW(TAG, "app '%s' leaked %u bytes; reclaiming", a->name, (unsigned)leaked);
  tos_arena_destroy(a->arena);
  a->arena = NULL;
  if (a->has_img) {
    tos_elf_unload(&a->img);
    a->has_img = false;
  }
  heap_caps_free(a->hb_copy);
  a->hb_copy = NULL;
  a->task = NULL;
  a->cleaning = false;
  a->active = false; // last: frees the slot
}

static void app_task(void *arg) {
  app_slot_t *a = (app_slot_t *)arg;
  tos_app_ctx_t ctx = {.granted_caps = a->caps,
                       .id = a->name,
                       .stop = &a->stop,
                       .arena = a->arena,
                       .regs = &a->regs};
  tos_app_ctx_bind(xTaskGetCurrentTaskHandle(), &ctx);

  esp_err_t e = tos_elf_load(a->elf, a->elf_len, &a->img);
  if (e == ESP_OK) {
    a->has_img = true;
    int ret = tos_elf_call(&a->img, a->api);
    ESP_LOGI(TAG, "app '%s' exited (ret=%d)", a->name, ret);
  } else {
    ESP_LOGE(TAG, "app '%s' load failed: %s", a->name, esp_err_to_name(e));
  }

  if (claim_cleanup(a))
    finish_slot(a);
  else
    vTaskSuspend(NULL); // the killer claimed us and will delete this task
  vTaskDelete(NULL);
}

esp_err_t tos_app_mgr_start(const uint8_t *hb, size_t len, const tos_api_t *api) {
  if (hb == NULL || api == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!ensure_mtx())
    return ESP_ERR_NO_MEM;

  tos_hb_t meta;
  esp_err_t e = tos_hb_open(hb, len, &meta); // verifies the signature
  if (e != ESP_OK)
    return e;
  if (meta.abi_major != TOS_ABI_VERSION_MAJOR)
    return ESP_ERR_INVALID_VERSION;

  // Reserve a free slot (name set now so a concurrent list sees it).
  app_slot_t *a = NULL;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  for (int i = 0; i < TOS_APP_MAX; i++) {
    if (!s_apps[i].active) {
      a = &s_apps[i];
      a->active = true;
      a->stop = false;
      a->cleaning = false;
      a->has_img = false;
      a->arena = NULL;
      a->hb_copy = NULL;
      a->regs.count = 0;
      strlcpy(a->name, meta.name, sizeof(a->name));
      break;
    }
  }
  xSemaphoreGive(s_mtx);
  if (a == NULL)
    return ESP_ERR_INVALID_STATE; // all app slots in use

  uint8_t *copy = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (copy == NULL) {
    a->active = false;
    return ESP_ERR_NO_MEM;
  }
  memcpy(copy, hb, len);
  tos_hb_open(copy, len, &meta);

  tos_arena_t *arena = tos_arena_create(APP_ARENA_BUDGET);
  if (arena == NULL) {
    heap_caps_free(copy);
    a->active = false;
    return ESP_ERR_NO_MEM;
  }

  a->api = api;
  a->hb_copy = copy;
  a->elf = meta.elf;
  a->elf_len = meta.elf_len;
  a->caps = meta.caps & tos_grants_get(a->name); // only what the user approved
  a->arena = arena;

  BaseType_t ok = xTaskCreatePinnedToCore(
      app_task, a->name, APP_TASK_STACK, a, SYS_PRIO_BACKGROUND, &a->task, SYS_CORE_RADIO);
  if (ok != pdPASS) {
    heap_caps_free(copy);
    tos_arena_destroy(arena);
    a->hb_copy = NULL;
    a->arena = NULL;
    a->active = false;
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "started app '%s' (caps=0x%lx)", a->name, (unsigned long)a->caps);
  return ESP_OK;
}

esp_err_t tos_app_mgr_stop(const char *name, uint32_t timeout_ms) {
  if (name == NULL || s_mtx == NULL)
    return ESP_ERR_INVALID_ARG;

  app_slot_t *a = NULL;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  for (int i = 0; i < TOS_APP_MAX; i++) {
    if (s_apps[i].active && strcmp(s_apps[i].name, name) == 0) {
      a = &s_apps[i];
      break;
    }
  }
  xSemaphoreGive(s_mtx);
  if (a == NULL)
    return ESP_ERR_NOT_FOUND;

  a->stop = true; // cooperative: the app polls api->should_stop and returns
  uint32_t waited = 0;
  while (a->active && waited < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(20));
    waited += 20;
  }
  if (!a->active)
    return ESP_OK;

  if (claim_cleanup(a)) {
    ESP_LOGW(TAG, "force-killing unresponsive app '%s'", a->name);
    if (a->task != NULL)
      vTaskDelete(a->task);
    finish_slot(a);
    return ESP_OK;
  }
  waited = 0;
  while (a->active && waited < 500) {
    vTaskDelay(pdMS_TO_TICKS(20));
    waited += 20;
  }
  return ESP_OK;
}

int tos_app_mgr_list(char names[][TOS_APP_NAME_CAP], int max) {
  int n = 0;
  if (s_mtx == NULL)
    return 0;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  for (int i = 0; i < TOS_APP_MAX; i++) {
    if (s_apps[i].active) {
      if (n < max)
        strlcpy(names[n], s_apps[i].name, TOS_APP_NAME_CAP);
      n++;
    }
  }
  xSemaphoreGive(s_mtx);
  return n;
}
