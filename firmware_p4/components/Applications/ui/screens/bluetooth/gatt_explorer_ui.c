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

#include "gatt_explorer_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bluetooth_service.h"
#include "gatt_explorer.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "sys_prio.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define GATT_UI_POLL_MS       500
#define GATT_UI_SCAN_MS       5000
#define GATT_UI_STACK         6144
#define GATT_UI_TARGET_MAX    30
#define GATT_UI_NAME_MAX      32
#define GATT_UI_LABEL_MAX     96
#define GATT_UI_VALUE_PREVIEW 16
#define GATT_UI_ICON          "/assets/icons/bluetooth_searching.bin"

typedef enum {
  GATT_UI_JOB_NONE,
  GATT_UI_JOB_SCAN,
  GATT_UI_JOB_CONNECT,
  GATT_UI_JOB_READ
} gatt_ui_job_t;
typedef struct {
  uint8_t addr[6];
  uint8_t addr_type;
  char name[GATT_UI_NAME_MAX];
} gatt_ui_target_t;

static const char *TAG = "GATT_UI";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_worker;
static lv_obj_t *s_screen;
static menu_component_t s_menu;
static lv_timer_t *s_timer;
static bool s_is_open, s_has_target, s_is_scanning;
static gatt_ui_job_t s_job;
static uint8_t s_target_count, s_selected_target;
static uint16_t s_read_handle;
static uint32_t s_revision, s_rendered_revision;
static esp_err_t s_error;
static spi_gatt_status_t s_status;
EXT_RAM_BSS_ATTR static gatt_ui_target_t s_targets[GATT_UI_TARGET_MAX];
EXT_RAM_BSS_ATTR static spi_gatt_record_t s_records[SPI_GATT_RECORD_MAX];

static void worker(void *arg);
static void refresh(lv_timer_t *timer);
static void on_input(const input_event_t *event, void *context);
static void build(void);

void ui_gatt_explorer_open(void) {
  if (s_timer != NULL) {
    lv_timer_del(s_timer);
    s_timer = NULL;
  }
  if (s_screen != NULL)
    lv_obj_del(s_screen);
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  portENTER_CRITICAL(&s_lock);
  s_is_open = true;
  s_has_target = false;
  s_job = GATT_UI_JOB_SCAN;
  s_is_scanning = true;
  s_target_count = 0;
  s_error = ESP_OK;
  ++s_revision;
  portEXIT_CRITICAL(&s_lock);
  if (s_worker == NULL &&
      xTaskCreatePinnedToCore(
          worker, "gatt_ui", GATT_UI_STACK, NULL, SYS_PRIO_SERVICE_LO, &s_worker, SYS_CORE_RADIO) !=
          pdPASS) {
    ESP_LOGE(TAG, "Cannot create GATT UI worker");
    portENTER_CRITICAL(&s_lock);
    s_error = ESP_ERR_NO_MEM;
    s_is_scanning = false;
    portEXIT_CRITICAL(&s_lock);
  }
  build();
  ui_input_set_screen_handler(on_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_timer = lv_timer_create(refresh, GATT_UI_POLL_MS, NULL);
  if (s_worker != NULL)
    xTaskNotifyGive(s_worker);
}

static void build(void) {
  lv_obj_clean(s_screen);
  s_menu = menu_component_create(s_screen, "REMOTE GATT", GATT_UI_ICON);
  portENTER_CRITICAL(&s_lock);
  bool has_target = s_has_target, is_scanning = s_is_scanning;
  uint8_t target_count = s_target_count;
  esp_err_t error = s_error;
  spi_gatt_status_t status = s_status;
  s_rendered_revision = s_revision;
  portEXIT_CRITICAL(&s_lock);
  char label[GATT_UI_LABEL_MAX];
  if (error != ESP_OK) {
    menu_component_add_section(&s_menu, esp_err_to_name(error));
  }
  if (!has_target) {
    menu_component_add_item(&s_menu, GATT_UI_ICON, is_scanning ? "Scanning..." : "Scan again");
    for (uint8_t index = 0; index < target_count; ++index) {
      gatt_ui_target_t target;
      portENTER_CRITICAL(&s_lock);
      target = s_targets[index];
      portEXIT_CRITICAL(&s_lock);
      snprintf(label,
               sizeof(label),
               "%s %02x:%02x:%02x:%02x:%02x:%02x",
               target.name,
               target.addr[5],
               target.addr[4],
               target.addr[3],
               target.addr[2],
               target.addr[1],
               target.addr[0]);
      menu_component_add_item(&s_menu, GATT_UI_ICON, label);
    }
  } else {
    static const char *const STATES[SPI_GATT_STATE_COUNT] = {
        "Disconnected", "Connecting", "Discovering", "Ready", "Reading", "Disconnecting", "Failed"};
    snprintf(label,
             sizeof(label),
             "%s%s%s",
             STATES[status.state],
             status.error != 0 ? " / remote error" : "",
             status.is_truncated ? " / list full" : "");
    menu_component_add_section(&s_menu, label);
    if (status.value_length != 0) {
      size_t offset = 0;
      uint8_t count =
          status.value_length < GATT_UI_VALUE_PREVIEW ? status.value_length : GATT_UI_VALUE_PREVIEW;
      for (uint8_t index = 0; index < count; ++index)
        offset += snprintf(label + offset, sizeof(label) - offset, "%02x ", status.value[index]);
      menu_component_add_section(&s_menu, label);
    }
    for (uint8_t index = 0; index < status.count; ++index) {
      spi_gatt_record_t record;
      portENTER_CRITICAL(&s_lock);
      record = s_records[index];
      portEXIT_CRITICAL(&s_lock);
      char uuid[SPI_GATT_UUID_MAX * 2 + 1];
      uint8_t uuid_length = record.uuid_type == 16   ? 2
                            : record.uuid_type == 32 ? 4
                                                     : SPI_GATT_UUID_MAX;
      for (uint8_t byte = 0; byte < uuid_length; ++byte)
        snprintf(
            uuid + byte * 2, sizeof(uuid) - byte * 2, "%02x", record.uuid[uuid_length - byte - 1]);
      snprintf(label,
               sizeof(label),
               "%s h%04x %s %s",
               record.kind == SPI_GATT_SERVICE ? "Service" : "Char",
               record.kind == SPI_GATT_SERVICE ? record.start_handle : record.value_handle,
               uuid,
               (record.properties & SPI_GATT_PROPERTY_READ) != 0 ? "Readable" : "");
      menu_component_add_item(&s_menu, GATT_UI_ICON, label);
    }
  }
  menu_component_set_hint(&s_menu, "OK scan/connect/read   BACK disconnect");
}

static void refresh(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    portENTER_CRITICAL(&s_lock);
    s_is_open = false;
    portEXIT_CRITICAL(&s_lock);
    if (s_worker != NULL)
      xTaskNotifyGive(s_worker);
    s_timer = NULL;
    lv_timer_del(timer);
    return;
  }
  portENTER_CRITICAL(&s_lock);
  bool has_changed = s_revision != s_rendered_revision;
  portEXIT_CRITICAL(&s_lock);
  if (has_changed) {
    int selected = menu_component_get_selected(&s_menu);
    build();
    menu_component_select(&s_menu, selected);
  }
}

static void on_input(const input_event_t *event, void *context) {
  (void)context;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  bool can_navigate = is_press || event->action == INPUT_ACTION_REPEAT;
  if (event->button == INPUT_BTN_DOWN && can_navigate)
    menu_component_next(&s_menu);
  else if (event->button == INPUT_BTN_UP && can_navigate)
    menu_component_prev(&s_menu);
  else if (event->button == INPUT_BTN_BACK && is_press) {
    portENTER_CRITICAL(&s_lock);
    s_is_open = false;
    portEXIT_CRITICAL(&s_lock);
    if (s_worker != NULL)
      xTaskNotifyGive(s_worker);
    ui_switch_screen(SCREEN_BLE_DETECT_MENU);
  } else if (event->button == INPUT_BTN_OK && is_press && s_worker != NULL) {
    int selected = menu_component_get_selected(&s_menu);
    portENTER_CRITICAL(&s_lock);
    if (s_job == GATT_UI_JOB_NONE && !s_has_target && !s_is_scanning) {
      if (selected == 0) {
        s_job = GATT_UI_JOB_SCAN;
        s_is_scanning = true;
        ++s_revision;
      } else if (selected > 0 && selected <= s_target_count) {
        s_selected_target = selected - 1;
        s_job = GATT_UI_JOB_CONNECT;
      }
    } else if (s_job == GATT_UI_JOB_NONE && s_has_target && s_status.state == SPI_GATT_READY &&
               selected >= 0 && selected < s_status.count &&
               s_records[selected].kind == SPI_GATT_CHARACTERISTIC &&
               (s_records[selected].properties & SPI_GATT_PROPERTY_READ) != 0) {
      s_read_handle = s_records[selected].value_handle;
      s_job = GATT_UI_JOB_READ;
    }
    portEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(s_worker);
  }
}

static void worker(void *arg) {
  (void)arg;
  bool has_client = false;
  for (;;) {
    portENTER_CRITICAL(&s_lock);
    bool is_open = s_is_open;
    gatt_ui_job_t job = s_job;
    s_job = GATT_UI_JOB_NONE;
    gatt_ui_target_t target = s_targets[s_selected_target];
    uint16_t handle = s_read_handle;
    portEXIT_CRITICAL(&s_lock);
    esp_err_t error = ESP_OK;
    if (!is_open && has_client) {
      error = gatt_explorer_stop();
    } else if (is_open && job == GATT_UI_JOB_SCAN && has_client) {
      error = gatt_explorer_stop();
      portENTER_CRITICAL(&s_lock);
      s_job = GATT_UI_JOB_SCAN;
      portEXIT_CRITICAL(&s_lock);
    } else if (is_open && job == GATT_UI_JOB_SCAN && !has_client) {
      error = bluetooth_service_init();
      if (error == ESP_OK)
        error = bluetooth_service_start();
      if (error == ESP_OK) {
        bluetooth_service_scan(GATT_UI_SCAN_MS);
        uint16_t count = bluetooth_service_get_scan_count();
        if (count > GATT_UI_TARGET_MAX)
          count = GATT_UI_TARGET_MAX;
        for (uint16_t index = 0; index < count; ++index) {
          bluetooth_service_scan_result_t *source = bluetooth_service_get_scan_result(index);
          if (source == NULL) {
            count = index;
            break;
          }
          gatt_ui_target_t item = {.addr_type = source->addr_type};
          memcpy(item.addr, source->addr, sizeof(item.addr));
          memcpy(item.name, source->name, sizeof(item.name));
          item.name[sizeof(item.name) - 1] = '\0';
          portENTER_CRITICAL(&s_lock);
          s_targets[index] = item;
          portEXIT_CRITICAL(&s_lock);
        }
        portENTER_CRITICAL(&s_lock);
        s_target_count = count;
        portEXIT_CRITICAL(&s_lock);
      }
      portENTER_CRITICAL(&s_lock);
      s_is_scanning = false;
      ++s_revision;
      portEXIT_CRITICAL(&s_lock);
    } else if (is_open && job == GATT_UI_JOB_CONNECT && !has_client) {
      has_client = gatt_explorer_start(target.addr, target.addr_type);
      error = has_client ? ESP_OK : ESP_FAIL;
      portENTER_CRITICAL(&s_lock);
      s_has_target = true;
      memset(&s_status, 0, sizeof(s_status));
      ++s_revision;
      portEXIT_CRITICAL(&s_lock);
    } else if (is_open && job == GATT_UI_JOB_READ && has_client) {
      error = gatt_explorer_read(handle);
    }
    if (has_client) {
      spi_gatt_status_t status;
      error = gatt_explorer_get_status(&status);
      if (error == ESP_OK) {
        for (uint8_t index = 0; index < status.count; ++index) {
          spi_gatt_record_t record;
          error = gatt_explorer_get_record(index, &record);
          if (error != ESP_OK)
            break;
          portENTER_CRITICAL(&s_lock);
          s_records[index] = record;
          portEXIT_CRITICAL(&s_lock);
        }
        portENTER_CRITICAL(&s_lock);
        if (memcmp(&status, &s_status, sizeof(status)) != 0) {
          s_status = status;
          ++s_revision;
        }
        portEXIT_CRITICAL(&s_lock);
        if (status.state == SPI_GATT_IDLE || status.state == SPI_GATT_ERROR)
          has_client = false;
      }
    }
    portENTER_CRITICAL(&s_lock);
    if (s_error != error) {
      s_error = error;
      ++s_revision;
    }
    bool needs_poll = s_is_open || has_client;
    portEXIT_CRITICAL(&s_lock);
    ulTaskNotifyTake(pdTRUE, needs_poll ? pdMS_TO_TICKS(GATT_UI_POLL_MS) : portMAX_DELAY);
  }
}
