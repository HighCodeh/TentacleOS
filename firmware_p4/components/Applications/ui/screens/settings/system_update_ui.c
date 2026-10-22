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

#include "system_update_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ota_service.h"
#include "sys_prio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define UPDATE_TASK_STACK   6144
#define UPDATE_REFRESH_MS   200
#define UPDATE_MESSAGE_SIZE 80
#define UPDATE_WIDTH        212
#define UPDATE_STATUS_Y     100
#define UPDATE_BAR_Y        160
#define UPDATE_BAR_HEIGHT   14
#define UPDATE_PROGRESS_MAX 100

typedef enum {
  SYSTEM_UPDATE_CHECKING = 0,
  SYSTEM_UPDATE_MISSING,
  SYSTEM_UPDATE_READY,
  SYSTEM_UPDATE_WRITING,
  SYSTEM_UPDATE_FAILED,
  SYSTEM_UPDATE_REBOOTING,
} system_update_phase_t;

typedef struct {
  system_update_phase_t phase;
  int percent;
  char message[UPDATE_MESSAGE_SIZE];
} system_update_snapshot_t;

static const char *TAG = "P4_UPDATE_UI";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static system_update_snapshot_t s_snapshot;
static bool s_is_worker_busy;
static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_bar;
static lv_timer_t *s_timer;

static void publish(system_update_phase_t phase, int percent, const char *message) {
  portENTER_CRITICAL(&s_lock);
  s_snapshot.phase = phase;
  s_snapshot.percent = percent;
  snprintf(s_snapshot.message, sizeof(s_snapshot.message), "%s", message);
  portEXIT_CRITICAL(&s_lock);
}

static void report_progress(int percent, const char *message) {
  publish(SYSTEM_UPDATE_WRITING, percent, message);
}

static void update_worker(void *argument) {
  bool is_install = argument != NULL;
  if (is_install) {
    esp_err_t error = ota_start_update(report_progress);
    if (error != ESP_OK) {
      char message[UPDATE_MESSAGE_SIZE];
      snprintf(message, sizeof(message), "P4 update failed: %s", esp_err_to_name(error));
      publish(SYSTEM_UPDATE_FAILED, 0, message);
    } else {
      publish(SYSTEM_UPDATE_REBOOTING, UPDATE_PROGRESS_MAX, "P4 reboot requested");
    }
  } else {
    bool has_update = ota_update_available();
    publish(has_update ? SYSTEM_UPDATE_READY : SYSTEM_UPDATE_MISSING,
            0,
            has_update ? "SD image found. OK installs P4 only." : "No P4 update image on SD.");
  }
  portENTER_CRITICAL(&s_lock);
  s_is_worker_busy = false;
  portEXIT_CRITICAL(&s_lock);
  vTaskDelete(NULL);
}

static void start_worker(bool is_install) {
  portENTER_CRITICAL(&s_lock);
  if (s_is_worker_busy) {
    portEXIT_CRITICAL(&s_lock);
    return;
  }
  s_is_worker_busy = true;
  portEXIT_CRITICAL(&s_lock);
  publish(is_install ? SYSTEM_UPDATE_WRITING : SYSTEM_UPDATE_CHECKING,
          0,
          is_install ? "Preparing P4 update..." : "Checking SD image...");
  if (xTaskCreatePinnedToCore(update_worker,
                              "p4_update",
                              UPDATE_TASK_STACK,
                              is_install ? (void *)1 : NULL,
                              SYS_PRIO_SERVICE_LO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    portENTER_CRITICAL(&s_lock);
    s_is_worker_busy = false;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGE(TAG, "Cannot start P4 update worker");
    publish(SYSTEM_UPDATE_FAILED, 0, "Cannot start update worker");
  }
}

static void refresh(lv_timer_t *timer) {
  if (s_screen == NULL || lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_timer = NULL;
    return;
  }
  portENTER_CRITICAL(&s_lock);
  system_update_snapshot_t snapshot = s_snapshot;
  portEXIT_CRITICAL(&s_lock);
  lv_label_set_text(s_status, snapshot.message);
  lv_bar_set_value(s_bar, snapshot.percent, LV_ANIM_OFF);
}

static void screen_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  if (s_timer != NULL)
    lv_timer_delete(s_timer);
  s_timer = NULL;
  s_screen = s_status = s_bar = NULL;
}

static void update_input(const input_event_t *event, void *context) {
  (void)context;
  if (event->action != INPUT_ACTION_PRESS)
    return;
  portENTER_CRITICAL(&s_lock);
  system_update_phase_t phase = s_snapshot.phase;
  portEXIT_CRITICAL(&s_lock);
  if (phase == SYSTEM_UPDATE_WRITING || phase == SYSTEM_UPDATE_REBOOTING)
    return;
  if (event->button == INPUT_BTN_BACK)
    ui_switch_screen(SCREEN_DEV_MENU);
  else if (event->button == INPUT_BTN_OK && s_timer != NULL)
    start_worker(phase == SYSTEM_UPDATE_READY);
}

void ui_system_update_open(void) {
  if (s_timer != NULL)
    lv_timer_delete(s_timer);
  s_timer = NULL;
  s_screen = lv_obj_create(NULL);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  ui_chrome_header(s_screen, "P4 UPDATE", "/assets/icons/system_update.bin");
  ui_chrome_footer(s_screen, "OK Install / Recheck    BACK Exit");
  lv_obj_t *version = lv_label_create(s_screen);
  lv_label_set_text_fmt(version, "Installed: %s", ota_get_current_version());
  lv_obj_align(version, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  s_status = lv_label_create(s_screen);
  lv_obj_set_width(s_status, UPDATE_WIDTH);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, UPDATE_STATUS_Y);
  s_bar = lv_bar_create(s_screen);
  lv_obj_set_size(s_bar, UPDATE_WIDTH, UPDATE_BAR_HEIGHT);
  lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, UPDATE_BAR_Y);
  lv_bar_set_range(s_bar, 0, UPDATE_PROGRESS_MAX);
  lv_obj_t *path = lv_label_create(s_screen);
  lv_obj_set_width(path, UPDATE_WIDTH);
  lv_label_set_text(path, OTA_UPDATE_PATH);
  lv_obj_align(path, LV_ALIGN_BOTTOM_MID, 0, -UI_CHROME_FOOTER_H);
  lv_obj_add_event_cb(s_screen, screen_deleted, LV_EVENT_DELETE, NULL);
  ui_input_set_screen_handler(update_input, NULL);
  // The delete handler owns this slot across deferred screen deletion.
  ui_screen_load(s_screen);
  s_timer = lv_timer_create(refresh, UPDATE_REFRESH_MS, NULL);
  if (s_timer == NULL) {
    publish(SYSTEM_UPDATE_FAILED, 0, "Update display unavailable");
    lv_label_set_text(s_status, "Update display unavailable");
    return;
  }
  start_worker(false);
  refresh(s_timer);
}
