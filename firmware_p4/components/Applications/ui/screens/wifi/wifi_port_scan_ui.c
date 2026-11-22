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

#include "wifi_port_scan_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"

#include "keyboard_ui.h"
#include "led_control.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "port_scanner.h"
#include "port_scanner_store.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define REFRESH_MS         200
#define RESULTS_PER_PAGE   PORT_SCANNER_PAGE_SIZE
#define ROW_TEXT_LENGTH    72
#define DETAIL_TEXT_LENGTH 128
#define RUN_MARGIN         8
#define RUN_META_HEIGHT    80
#define RUN_ROW_HEIGHT     38
#define RUN_ROWS_MAX       4
#define RUN_PHASE_Y        0
#define RUN_IP_Y           16
#define RUN_PROGRESS_Y     40
#define RUN_STORAGE_Y      56
#define RUN_BAR_Y          74
#define RUN_BAR_HEIGHT     4
#define RUN_ROW_PAD        4
#define RUN_BANNER_Y       22
#define RUN_BAR_MAX        1000

static const char *TAG = "WIFI_PORT_SCAN_UI";
static const char *SCAN_ICON = "/assets/icons/wifi_find.bin";
static const char *const TARGET_NAMES[] = {
    "Gateway", "Single IP", "Local network", "IP range", "CIDR"};
static const char *const TARGET_DEFAULTS[] = {
    "", "192.168.1.1", "", "192.168.1.1-192.168.1.254", "192.168.1.0/24"};
static const char *const PORT_NAMES[] = {"Common", "Custom", "All 1-65535"};
static const char *const STATE_NAMES[] = {
    "Ready", "Scanning", "Stopping", "Complete", "Cancelled", "Failed"};

typedef enum {
  WIFI_PORT_SCAN_TARGET,
  WIFI_PORT_SCAN_ADDRESS,
  WIFI_PORT_SCAN_PORTS,
  WIFI_PORT_SCAN_LIST,
  WIFI_PORT_SCAN_PROTOCOL,
  WIFI_PORT_SCAN_PING,
  WIFI_PORT_SCAN_START,
  WIFI_PORT_SCAN_RESULTS,
  WIFI_PORT_SCAN_SAVED,
  WIFI_PORT_SCAN_COUNT
} wifi_port_scan_row_t;

static const char *PROTOCOL_NAMES[] = {"TCP", "UDP", "TCP + UDP"};

static lv_obj_t *s_screen;
static lv_timer_t *s_timer;
static EXT_RAM_BSS_ATTR menu_component_t s_menu;
static port_scanner_config_t s_config = {.target_mode = PORT_SCANNER_GATEWAY,
                                         .port_mode = PORT_SCANNER_COMMON_PORTS,
                                         .ports = "22,80,443,8000-8100"};
static bool s_is_open;
static bool s_show_results;
static bool s_show_files;
static bool s_show_hosts;
static bool s_is_deleting;
static bool s_is_delete_confirm;
static bool s_is_previous;
static uint32_t s_cursor;
static uint32_t s_hosts_cursor;
static uint32_t s_hosts_page;
static port_scanner_host_t s_host;
static char s_archive_label[PORT_SCANNER_TARGET_LENGTH];
static bool s_use_sd_results;
static bool s_is_loading;
static bool s_is_page_deferred;
static bool s_return_to_files;
static uint32_t s_page;
static uint32_t s_files_page;
static char s_archive_path[PORT_SCANNER_PATH_LENGTH];
static EXT_RAM_BSS_ATTR port_scanner_page_t s_sd_page;
static uint16_t s_page_count;
static uint16_t s_visible_results;
static lv_obj_t *s_start_label;
static bool s_is_execution;
static bool s_is_following_latest;
static bool s_is_stop_pressed;
static uint16_t s_run_first;
static uint16_t s_run_selected;
static uint16_t s_run_count;
static uint16_t s_run_rows;
static lv_obj_t *s_run_phase;
static lv_obj_t *s_run_ip;
static lv_obj_t *s_run_progress;
static lv_obj_t *s_run_storage;
static lv_obj_t *s_run_bar;
static lv_obj_t *s_run_footer;
static lv_obj_t *s_run_empty;
static lv_obj_t *s_run_items[RUN_ROWS_MAX];
static lv_obj_t *s_run_titles[RUN_ROWS_MAX];
static lv_obj_t *s_run_banners[RUN_ROWS_MAX];
static EXT_RAM_BSS_ATTR port_scan_result_t s_run_results[RUN_ROWS_MAX];
static ui_theme_t s_run_theme;
static int32_t s_run_width;
static int32_t s_run_height;

static void build_screen(void);
static void refresh(lv_timer_t *timer);
static void input(const input_event_t *event, void *context);
static void deleted(lv_event_t *event);
static void submitted(const char *text, void *context);
static void request_page(void);
static void delete_confirmed(bool is_confirmed);
static bool build_execution(lv_obj_t *root);
static void refresh_execution(const port_scanner_snapshot_t *status);
static void show_detail(const port_scan_result_t *result);

void ui_wifi_port_scan_close(void) {
  s_is_open = false;
  s_is_loading = false;
  s_is_page_deferred = false;
  if (s_is_delete_confirm) {
    s_is_delete_confirm = false;
    msgbox_close();
  }
  port_scanner_close_page();
  port_scanner_stop();
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (keyboard_is_open())
    keyboard_close();
}

void ui_wifi_port_scan_open(void) {
  s_is_open = true;
  port_scanner_snapshot_t status;
  port_scanner_get_snapshot(&status);
  s_is_execution = status.is_busy;
  s_is_following_latest = true;
  s_is_stop_pressed = false;
  s_run_first = 0;
  s_run_selected = 0;
  s_show_results = false;
  s_show_files = false;
  s_show_hosts = false;
  s_is_deleting = false;
  s_is_previous = false;
  s_cursor = 0;
  s_use_sd_results = false;
  s_is_loading = false;
  s_return_to_files = false;
  s_page = 0;
  build_screen();
  if (!s_is_open)
    return;
  if (s_timer == NULL)
    s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
  if (s_timer == NULL) {
    ESP_LOGE(TAG, "Refresh timer allocation failed");
    led_signal_error();
    port_scanner_stop();
    if (s_is_execution)
      ui_chrome_footer_set_text(s_run_footer, "Unavailable; BACK to retry");
    else
      menu_component_set_hint(&s_menu, "Unavailable; BACK to retry");
  }
}

static void deleted(lv_event_t *event) {
  if (s_screen == lv_event_get_target(event)) {
    s_screen = NULL;
    ui_wifi_port_scan_close();
  }
}

static void build_screen(void) {
  lv_obj_t *root = ui_chrome_screen_create();
  if (root == NULL) {
    ESP_LOGE(TAG, "Screen allocation failed");
    led_signal_error();
    ui_wifi_port_scan_close();
    ui_input_set_screen_handler(input, NULL);
    return;
  }
  if (s_is_execution) {
    if (!build_execution(root)) {
      lv_obj_delete(root);
      ESP_LOGE(TAG, "Execution view allocation failed");
      led_signal_error();
      ui_wifi_port_scan_close();
      ui_input_set_screen_handler(input, NULL);
      return;
    }
    lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, NULL);
    ui_input_set_screen_handler(input, NULL);
    ui_screen_load_owned(&s_screen, root);
    refresh(NULL);
    return;
  }
  const char *title = s_show_files       ? "SAVED NETWORKS"
                      : s_show_hosts     ? s_archive_label[0] ? s_archive_label : "FOUND IPs"
                      : s_use_sd_results ? s_host.ip
                      : s_show_results   ? "PORT RESULTS"
                                         : "PORT SCAN";
  s_menu = menu_component_create(root, title, SCAN_ICON);
  s_start_label = NULL;
  if (s_show_files || s_use_sd_results) {
    s_page_count = !s_is_loading && s_sd_page.error == ESP_OK ? s_sd_page.count : 0;
    for (uint16_t i = 0; i < s_page_count; ++i) {
      char row[ROW_TEXT_LENGTH];
      if (s_show_files)
        snprintf(row, sizeof(row), "%s", s_sd_page.labels[i]);
      else if (s_show_hosts)
        snprintf(row,
                 sizeof(row),
                 "%s  (%lu)",
                 s_sd_page.hosts[i].ip,
                 (unsigned long)s_sd_page.hosts[i].count);
      else
        snprintf(row,
                 sizeof(row),
                 "%s:%d/%s%s",
                 s_sd_page.results[i].ip_str,
                 s_sd_page.results[i].port,
                 s_sd_page.results[i].protocol == PORT_SCAN_PROTO_TCP ? "TCP" : "UDP",
                 s_sd_page.results[i].status == PORT_SCAN_STATUS_OPEN_FILTERED ? " ?"
                 : s_sd_page.results[i].status == PORT_SCAN_STATUS_FILTERED    ? " F"
                                                                               : "");
      menu_component_add_item(&s_menu, NULL, row);
    }
    if (s_page_count == 0) {
      const char *message = s_is_deleting               ? "Deleting scan..."
                            : s_is_loading              ? "Loading from SD..."
                            : s_sd_page.error != ESP_OK ? "SD read failed; BACK to retry"
                            : s_show_files              ? "No saved scans"
                                                        : "No results";
      menu_component_add_item(&s_menu, NULL, message);
    }
    if (s_show_hosts && !s_is_loading)
      menu_component_add_item(&s_menu, NULL, "Delete scan");
    char hint[ROW_TEXT_LENGTH];
    uint32_t pages = s_sd_page.total / RESULTS_PER_PAGE + (s_sd_page.total % RESULTS_PER_PAGE != 0);
    if (s_show_hosts)
      snprintf(hint, sizeof(hint), "L/R page %lu   OK ports   BACK", (unsigned long)s_page + 1);
    else
      snprintf(hint,
               sizeof(hint),
               "L/R %lu/%lu   OK open   BACK",
               (unsigned long)s_page + 1,
               (unsigned long)(pages != 0 ? pages : 1));
    menu_component_set_hint(&s_menu,
                            s_is_loading           ? "Loading... BACK cancel"
                            : s_sd_page.is_partial ? "Partial | L/R pages | BACK"
                                                   : hint);
  } else if (s_show_results) {
    port_scanner_snapshot_t snapshot;
    port_scanner_get_snapshot(&snapshot);
    s_visible_results = snapshot.stored;
    s_page_count = 0;
    for (uint16_t i = s_page * RESULTS_PER_PAGE;
         i < snapshot.stored && s_page_count < RESULTS_PER_PAGE;
         ++i) {
      port_scan_result_t result;
      if (!port_scanner_get_result(i, &result))
        break;
      char row[ROW_TEXT_LENGTH];
      snprintf(row,
               sizeof(row),
               "%s:%d/%s%s",
               result.ip_str,
               result.port,
               result.protocol == PORT_SCAN_PROTO_TCP ? "TCP" : "UDP",
               result.status == PORT_SCAN_STATUS_OPEN_FILTERED ? " ?"
               : result.status == PORT_SCAN_STATUS_FILTERED    ? " F"
                                                               : "");
      menu_component_add_item(&s_menu, NULL, row);
      ++s_page_count;
    }
    if (s_page_count == 0)
      menu_component_add_item(&s_menu, NULL, "No results yet");
    menu_component_set_hint(&s_menu, "L/R pages   Live preview   BACK");
  } else {
    menu_component_add_selector(&s_menu, NULL, "Target", "");
    menu_component_add_selector(&s_menu, NULL, "Address", "");
    menu_component_add_selector(&s_menu, NULL, "Ports", "");
    menu_component_add_selector(&s_menu, NULL, "List / range", "");
    menu_component_add_selector(&s_menu, NULL, "Protocol", "");
    menu_component_add_selector(&s_menu, NULL, "Ping first", "");
    lv_obj_t *start = menu_component_add_item(&s_menu, NULL, "Scan");
    s_start_label = start != NULL ? lv_obj_get_child(start, 0) : NULL;
    menu_component_add_item(&s_menu, NULL, "Results");
    menu_component_add_item(&s_menu, NULL, "Saved scans");
  }
  lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, NULL);
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, root);
  refresh(NULL);
}

static void refresh(lv_timer_t *timer) {
  (void)timer;
  if (!s_is_open || s_screen == NULL)
    return;
  port_scanner_snapshot_t status;
  port_scanner_get_snapshot(&status);
  if (s_is_execution) {
    refresh_execution(&status);
    return;
  }
  if (s_show_files || s_use_sd_results) {
    if (s_is_loading) {
      port_scanner_get_page(&s_sd_page);
      if (!s_sd_page.is_busy) {
        if (s_is_page_deferred) {
          request_page();
        } else {
          s_is_loading = false;
          if (s_is_deleting) {
            s_is_deleting = false;
            if (s_sd_page.error == ESP_OK) {
              s_show_hosts = false;
              s_show_results = false;
              s_use_sd_results = false;
              s_show_files = true;
              s_page = s_files_page;
            } else {
              notify(NOTIFY_WARNING, "Could not delete scan");
            }
            request_page();
          } else if (s_show_files && s_sd_page.error == ESP_OK && s_page > 0 &&
                     (uint64_t)s_page * RESULTS_PER_PAGE >= s_sd_page.total) {
            s_page = s_sd_page.total == 0 ? 0 : (s_sd_page.total - 1) / RESULTS_PER_PAGE;
            request_page();
          } else {
            s_cursor = s_sd_page.cursor;
            s_is_previous = false;
            build_screen();
          }
        }
      }
    }
    return;
  }
  if (s_show_results) {
    if (!status.is_busy && status.archive_path[0] != '\0' && s_timer != NULL) {
      memcpy(s_archive_path, status.archive_path, sizeof(s_archive_path));
      s_use_sd_results = true;
      s_show_hosts = true;
      s_cursor = 0;
      s_page = 0;
      s_archive_label[0] = '\0';
      request_page();
      return;
    }
    if (status.stored != s_visible_results) {
      int selected = menu_component_get_selected(&s_menu);
      build_screen();
      if (s_is_open)
        menu_component_select(&s_menu, selected);
    }
    return;
  }
  menu_component_set_selector_value(
      &s_menu, WIFI_PORT_SCAN_TARGET, TARGET_NAMES[s_config.target_mode]);
  menu_component_set_selector_value(&s_menu,
                                    WIFI_PORT_SCAN_ADDRESS,
                                    s_config.target_mode == PORT_SCANNER_GATEWAY ||
                                            s_config.target_mode == PORT_SCANNER_LOCAL
                                        ? "Automatic"
                                        : s_config.target);
  menu_component_set_selector_value(&s_menu, WIFI_PORT_SCAN_PORTS, PORT_NAMES[s_config.port_mode]);
  menu_component_set_selector_value(&s_menu,
                                    WIFI_PORT_SCAN_LIST,
                                    s_config.port_mode == PORT_SCANNER_CUSTOM_PORTS ? s_config.ports
                                                                                    : "Preset");
  menu_component_set_selector_value(
      &s_menu, WIFI_PORT_SCAN_PROTOCOL, PROTOCOL_NAMES[s_config.protocol_mode]);
  menu_component_set_selector_value(
      &s_menu, WIFI_PORT_SCAN_PING, s_config.is_ping_enabled ? "On" : "Off");
  if (s_start_label != NULL)
    lv_label_set_text(s_start_label, status.is_busy ? "Stopping..." : "Scan");
  bool is_ping_row = menu_component_get_selected(&s_menu) == WIFI_PORT_SCAN_PING;
  menu_component_set_hint(&s_menu,
                          is_ping_row && s_config.is_ping_enabled
                              ? "No ping reply: skip host"
                              : "OK edit/start   L/R change   BACK");
}

static void request_page(void) {
  memset(&s_sd_page, 0, sizeof(s_sd_page));
  port_scanner_browse_request_t request = {.kind = s_show_files   ? PORT_SCANNER_BROWSE_NETWORKS
                                                   : s_show_hosts ? PORT_SCANNER_BROWSE_HOSTS
                                                                  : PORT_SCANNER_BROWSE_PORTS,
                                           .number = s_page,
                                           .cursor = s_cursor,
                                           .is_previous = s_is_previous,
                                           .host = s_host};
  if (!s_show_files)
    memcpy(request.path, s_archive_path, sizeof(request.path));
  esp_err_t error = port_scanner_browse(&request);
  s_is_loading = error == ESP_OK;
  s_is_page_deferred = false;
  if (error == ESP_ERR_INVALID_STATE) {
    port_scanner_get_page(&s_sd_page);
    s_is_page_deferred = s_sd_page.is_busy;
    s_is_loading = s_is_page_deferred;
  }
  s_sd_page.error = error;
  if (error != ESP_OK && !s_is_page_deferred) {
    ESP_LOGW(TAG, "Archive unavailable: %s", esp_err_to_name(error));
    led_signal_warning();
  }
  build_screen();
}

static void submitted(const char *text, void *context) {
  if (!s_is_open || text == NULL)
    return;
  wifi_port_scan_row_t row = (wifi_port_scan_row_t)(intptr_t)context;
  port_scanner_config_t draft = s_config;
  if (row == WIFI_PORT_SCAN_ADDRESS)
    snprintf(draft.target, sizeof(draft.target), "%s", text);
  else
    snprintf(draft.ports, sizeof(draft.ports), "%s", text);
  if (port_scanner_validate(&draft) != ESP_OK) {
    ESP_LOGW(TAG, "Invalid address or ports");
    led_signal_warning();
    notify(NOTIFY_WARNING, "Invalid address or ports");
    return;
  }
  s_config = draft;
  refresh(NULL);
}

static void input(const input_event_t *event, void *context) {
  (void)context;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  if (event->button == INPUT_BTN_BACK && is_press) {
    if (s_is_execution) {
      port_scanner_snapshot_t status;
      port_scanner_get_snapshot(&status);
      if (status.is_busy && !s_is_stop_pressed && status.state != PORT_SCANNER_STOPPING) {
        s_is_stop_pressed = true;
        port_scanner_stop();
        refresh(NULL);
      } else {
        s_is_execution = false;
        build_screen();
      }
    } else if (s_show_results || s_show_files) {
      port_scanner_close_page();
      s_is_loading = false;
      s_is_deleting = false;
      s_is_previous = false;
      if (s_use_sd_results && !s_show_hosts) {
        s_show_hosts = true;
        s_cursor = s_hosts_cursor;
        s_page = s_hosts_page;
        request_page();
      } else if (s_use_sd_results && s_return_to_files) {
        s_show_results = false;
        s_show_hosts = false;
        s_use_sd_results = false;
        s_show_files = true;
        s_page = s_files_page;
        request_page();
      } else {
        s_show_results = false;
        s_show_hosts = false;
        s_show_files = false;
        s_use_sd_results = false;
        build_screen();
      }
    } else {
      port_scanner_snapshot_t status;
      port_scanner_get_snapshot(&status);
      if (status.is_busy && status.state != PORT_SCANNER_STOPPING)
        port_scanner_stop();
      else
        ui_switch_screen(SCREEN_WIFI_MENU);
    }
    return;
  }
  if (!s_is_open || s_screen == NULL)
    return;
  if (s_is_execution) {
    port_scanner_snapshot_t status;
    port_scanner_get_snapshot(&status);
    if (is_press && event->button == INPUT_BTN_OK && s_run_selected < s_run_count) {
      show_detail(&s_run_results[s_run_selected]);
    } else if (is_press || event->action == INPUT_ACTION_REPEAT) {
      if (event->button == INPUT_BTN_UP) {
        s_is_following_latest = false;
        if (s_run_selected > 0)
          --s_run_selected;
        else if (s_run_first > 0)
          --s_run_first;
      } else if (event->button == INPUT_BTN_DOWN) {
        if (s_run_selected + 1 < s_run_count)
          ++s_run_selected;
        else if (s_run_first + s_run_count < status.stored)
          ++s_run_first;
        s_is_following_latest = s_run_first + s_run_selected + 1 >= status.stored;
      }
      refresh(NULL);
    }
    return;
  }
  if (is_press || event->action == INPUT_ACTION_REPEAT) {
    if (event->button == INPUT_BTN_UP)
      menu_component_prev(&s_menu);
    else if (event->button == INPUT_BTN_DOWN)
      menu_component_next(&s_menu);
  }
  bool is_ok = event->button == INPUT_BTN_OK;
  bool is_change = is_ok || event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT;
  if (!is_press || !is_change)
    return;
  int selected = menu_component_get_selected(&s_menu);
  if (s_show_files || s_use_sd_results) {
    if (s_is_loading)
      return;
    if (!is_ok) {
      if (event->button == INPUT_BTN_LEFT && s_page > 0) {
        --s_page;
        s_is_previous = s_show_hosts;
      } else if (event->button == INPUT_BTN_RIGHT && s_sd_page.has_next) {
        ++s_page;
        s_cursor = s_sd_page.next_cursor;
        s_is_previous = false;
      } else {
        return;
      }
      request_page();
    } else if (selected < s_page_count) {
      if (s_show_files) {
        snprintf(s_archive_path,
                 sizeof(s_archive_path),
                 PORT_SCANNER_STORE_DIR "/%s",
                 s_sd_page.files[selected]);
        memcpy(s_archive_label, s_sd_page.labels[selected], sizeof(s_archive_label));
        s_files_page = s_page;
        s_show_files = false;
        s_show_results = true;
        s_show_hosts = true;
        s_use_sd_results = true;
        s_return_to_files = true;
        s_page = 0;
        s_cursor = 0;
        s_is_previous = false;
        request_page();
      } else if (s_show_hosts) {
        s_host = s_sd_page.hosts[selected];
        s_hosts_page = s_page;
        s_hosts_cursor = s_cursor;
        s_show_hosts = false;
        s_page = 0;
        request_page();
      } else {
        show_detail(&s_sd_page.results[selected]);
      }
    } else if (s_show_hosts && selected == (s_page_count != 0 ? s_page_count : 1)) {
      s_is_delete_confirm = true;
      msgbox_open_ex(LV_SYMBOL_TRASH,
                     "Delete this scan and all its results?",
                     "Delete",
                     "Cancel",
                     delete_confirmed,
                     false);
    }
    return;
  }
  if (s_show_results) {
    if (is_ok && selected < s_page_count) {
      port_scan_result_t result;
      if (port_scanner_get_result(s_page * RESULTS_PER_PAGE + selected, &result))
        show_detail(&result);
    } else if (!is_ok) {
      if (event->button == INPUT_BTN_LEFT && s_page > 0)
        --s_page;
      else if (event->button == INPUT_BTN_RIGHT &&
               (s_page + 1) * RESULTS_PER_PAGE < s_visible_results)
        ++s_page;
      else
        return;
      build_screen();
    }
    return;
  }
  port_scanner_snapshot_t status;
  port_scanner_get_snapshot(&status);
  if (selected == WIFI_PORT_SCAN_RESULTS && is_ok) {
    s_page = 0;
    s_show_results = true;
    s_return_to_files = false;
    if (!status.is_busy && status.archive_path[0] != '\0' && s_timer != NULL) {
      memcpy(s_archive_path, status.archive_path, sizeof(s_archive_path));
      s_use_sd_results = true;
      s_show_hosts = true;
      s_cursor = 0;
      s_is_previous = false;
      s_archive_label[0] = '\0';
      request_page();
    } else {
      build_screen();
    }
  } else if (selected == WIFI_PORT_SCAN_SAVED && is_ok && s_timer != NULL) {
    if (status.is_busy) {
      ESP_LOGW(TAG, "Wait for scan completion before browsing SD");
      led_signal_warning();
      notify(NOTIFY_WARNING, "Stop scan before browsing saved scans");
    } else {
      s_show_files = true;
      s_page = 0;
      request_page();
    }
  } else if (selected == WIFI_PORT_SCAN_START && is_ok && s_timer != NULL && !status.is_busy) {
    s_is_execution = true;
    s_is_following_latest = true;
    s_is_stop_pressed = false;
    s_run_first = 0;
    s_run_selected = 0;
    build_screen();
    if (!s_is_open)
      return;
    esp_err_t error = port_scanner_start(&s_config);
    if (error != ESP_OK) {
      ESP_LOGW(TAG, "Cannot start scan: %s", esp_err_to_name(error));
      led_signal_warning();
      s_is_execution = false;
      build_screen();
      notify(NOTIFY_WARNING, "Scan unavailable; check settings");
    }
  } else if (!status.is_busy) {
    int direction = event->button == INPUT_BTN_LEFT ? -1 : 1;
    if (selected == WIFI_PORT_SCAN_PROTOCOL) {
      s_config.protocol_mode = (s_config.protocol_mode + direction + PORT_SCANNER_PROTOCOL_COUNT) %
                               PORT_SCANNER_PROTOCOL_COUNT;
    } else if (selected == WIFI_PORT_SCAN_PING) {
      s_config.is_ping_enabled = !s_config.is_ping_enabled;
    } else if (selected == WIFI_PORT_SCAN_TARGET) {
      s_config.target_mode = (s_config.target_mode + direction + PORT_SCANNER_TARGET_COUNT) %
                             PORT_SCANNER_TARGET_COUNT;
      snprintf(
          s_config.target, sizeof(s_config.target), "%s", TARGET_DEFAULTS[s_config.target_mode]);
    } else if (selected == WIFI_PORT_SCAN_PORTS) {
      s_config.port_mode =
          (s_config.port_mode + direction + PORT_SCANNER_PORTS_COUNT) % PORT_SCANNER_PORTS_COUNT;
    } else if (is_ok && ((selected == WIFI_PORT_SCAN_ADDRESS &&
                          s_config.target_mode != PORT_SCANNER_GATEWAY &&
                          s_config.target_mode != PORT_SCANNER_LOCAL) ||
                         (selected == WIFI_PORT_SCAN_LIST &&
                          s_config.port_mode == PORT_SCANNER_CUSTOM_PORTS))) {
      bool is_address = selected == WIFI_PORT_SCAN_ADDRESS;
      keyboard_config_t editor = {
          .title = !is_address                                  ? "Ports: 22,80,8000-8100"
                   : s_config.target_mode == PORT_SCANNER_RANGE ? "First IP-last IP"
                   : s_config.target_mode == PORT_SCANNER_CIDR  ? "Network/prefix: /16, /24"
                                                                : "Target IPv4",
          .initial = is_address ? s_config.target : s_config.ports,
          .min_chars = 1,
          .max_chars = is_address ? sizeof(s_config.target) - 1 : sizeof(s_config.ports) - 1,
          .mode = KEYBOARD_TEXT,
      };
      keyboard_open_config(&editor, submitted, (void *)(intptr_t)selected);
    }
  }
  refresh(NULL);
}

static void delete_confirmed(bool is_confirmed) {
  s_is_delete_confirm = false;
  if (!is_confirmed || !s_is_open || !s_show_hosts || s_is_loading)
    return;
  port_scanner_browse_request_t request = {.kind = PORT_SCANNER_BROWSE_DELETE};
  memcpy(request.path, s_archive_path, sizeof(request.path));
  esp_err_t error = port_scanner_browse(&request);
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "Cannot delete scan: %s", esp_err_to_name(error));
    led_signal_warning();
    notify(NOTIFY_WARNING, "Scan busy; try again");
    return;
  }
  s_is_deleting = true;
  s_is_loading = true;
  s_is_page_deferred = false;
  build_screen();
}

static void show_detail(const port_scan_result_t *result) {
  char detail[DETAIL_TEXT_LENGTH];
  snprintf(detail,
           sizeof(detail),
           "%s:%d/%s\n%s\n%s",
           result->ip_str,
           result->port,
           result->protocol == PORT_SCAN_PROTO_TCP ? "TCP" : "UDP",
           result->status == PORT_SCAN_STATUS_OPEN       ? "OPEN"
           : result->status == PORT_SCAN_STATUS_FILTERED ? "FILTERED"
                                                         : "OPEN|FILTERED",
           result->banner[0] != '\0'                          ? result->banner
           : result->status == PORT_SCAN_STATUS_OPEN_FILTERED ? "No UDP reply; open not confirmed"
           : result->status == PORT_SCAN_STATUS_FILTERED      ? "ICMP destination unreachable"
                                                              : "No banner");
  msgbox_open(LV_SYMBOL_WIFI, detail, "OK", NULL, NULL);
}

static lv_obj_t *run_label(lv_obj_t *parent, int32_t y, int32_t width, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  if (label == NULL)
    return NULL;
  lv_obj_set_pos(label, 0, y);
  lv_obj_set_size(label, width, lv_font_get_line_height(font));
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, current_theme.text_main, 0);
  lv_label_set_text(label, "");
  return label;
}

static bool build_execution(lv_obj_t *root) {
  s_run_width = ui_screen_w();
  s_run_height = ui_screen_h();
  s_run_theme = current_theme;
  lv_obj_t *header = ui_chrome_header(root, "PORT SCAN", SCAN_ICON);
  s_run_footer = ui_chrome_footer(root, "BACK stop   UP/DOWN results");
  lv_obj_t *body = lv_obj_create(root);
  if (header == NULL || s_run_footer == NULL || body == NULL)
    return false;
  lv_obj_remove_style_all(body);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  int32_t width = s_run_width - RUN_MARGIN * 2;
  int32_t height = s_run_height - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H - RUN_MARGIN * 2;
  lv_obj_set_pos(body, RUN_MARGIN, UI_CHROME_HEADER_H + RUN_MARGIN);
  lv_obj_set_size(body, width, height);
  s_run_phase = run_label(body, RUN_PHASE_Y, width, &lv_font_montserrat_12);
  s_run_ip = run_label(body, RUN_IP_Y, width, &lv_font_montserrat_16);
  s_run_progress = run_label(body, RUN_PROGRESS_Y, width, &lv_font_montserrat_12);
  s_run_storage = run_label(body, RUN_STORAGE_Y, width, &lv_font_montserrat_12);
  s_run_bar = lv_bar_create(body);
  if (s_run_phase == NULL || s_run_ip == NULL || s_run_progress == NULL || s_run_storage == NULL ||
      s_run_bar == NULL)
    return false;
  lv_obj_set_style_text_color(s_run_phase, current_theme.border_accent, 0);
  lv_obj_set_style_text_color(s_run_storage, current_theme.text_secondary, 0);
  lv_obj_set_pos(s_run_bar, 0, RUN_BAR_Y);
  lv_obj_set_size(s_run_bar, width, RUN_BAR_HEIGHT);
  lv_obj_set_style_bg_color(s_run_bar, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_run_bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_bar_set_range(s_run_bar, 0, RUN_BAR_MAX);
  int32_t available = height - RUN_META_HEIGHT;
  s_run_rows = available / RUN_ROW_HEIGHT;
  if (s_run_rows > RUN_ROWS_MAX)
    s_run_rows = RUN_ROWS_MAX;
  if (s_run_rows == 0)
    return false;
  s_run_empty = run_label(body, RUN_META_HEIGHT + RUN_MARGIN, width, &lv_font_montserrat_14);
  if (s_run_empty == NULL)
    return false;
  lv_obj_set_style_text_color(s_run_empty, current_theme.text_secondary, 0);
  for (uint16_t i = 0; i < s_run_rows; ++i) {
    lv_obj_t *row = lv_obj_create(body);
    s_run_items[i] = row;
    if (row == NULL)
      return false;
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(row, 0, RUN_META_HEIGHT + i * RUN_ROW_HEIGHT);
    lv_obj_set_size(row, width, RUN_ROW_HEIGHT);
    lv_obj_set_style_radius(row, RUN_ROW_PAD, 0);
    lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, current_theme.border_inactive, 0);
    s_run_titles[i] = run_label(row, RUN_ROW_PAD, width - RUN_ROW_PAD * 2, &lv_font_montserrat_14);
    s_run_banners[i] =
        run_label(row, RUN_BANNER_Y, width - RUN_ROW_PAD * 2, &lv_font_montserrat_12);
    if (s_run_titles[i] == NULL || s_run_banners[i] == NULL)
      return false;
    lv_obj_set_x(s_run_titles[i], RUN_ROW_PAD);
    lv_obj_set_x(s_run_banners[i], RUN_ROW_PAD);
    lv_obj_set_style_text_color(s_run_banners[i], current_theme.text_secondary, 0);
  }
  return true;
}

static void refresh_execution(const port_scanner_snapshot_t *status) {
  if (s_run_width != ui_screen_w() || s_run_height != ui_screen_h() ||
      memcmp(&current_theme, &s_run_theme, sizeof(current_theme)) != 0) {
    build_screen();
    return;
  }
  char text[ROW_TEXT_LENGTH];
  bool is_discovery = status->is_ping_enabled && !status->is_discovery_complete;
  uint32_t hosts_total = status->is_ping_enabled ? status->hosts_responded : status->hosts_total;
  uint32_t hosts_done = status->hosts_done - status->hosts_skipped;
  bool is_udp = status->active_protocol == PORT_SCAN_PROTO_UDP;
  const char *phase = STATE_NAMES[status->state];
  if (status->state == PORT_SCANNER_RUNNING) {
    if (is_discovery)
      phase = "1/2 DISCOVERY";
    else if (status->is_ping_enabled)
      phase = is_udp ? "2/2 UDP SCAN" : "2/2 TCP SCAN";
    else if (status->current_ip[0] != '\0')
      phase = is_udp ? "UDP SCAN" : "TCP SCAN";
    else
      phase = "PREPARING";
  }
  if (is_discovery)
    snprintf(text, sizeof(text), "%s", phase);
  else
    snprintf(text,
             sizeof(text),
             "%s   %lu/%lu hosts",
             phase,
             (unsigned long)hosts_done,
             (unsigned long)hosts_total);
  lv_label_set_text(s_run_phase, text);
  lv_label_set_text(s_run_ip,
                    status->current_ip[0] ? status->current_ip
                    : status->is_busy     ? "Starting scan..."
                    : is_discovery        ? "Discovery stopped"
                                          : "No host scanned");
  if (is_discovery)
    snprintf(text,
             sizeof(text),
             "%lu/%lu IPs   %lu up",
             (unsigned long)status->hosts_probed,
             (unsigned long)status->hosts_total,
             (unsigned long)status->hosts_responded);
  else
    snprintf(text,
             sizeof(text),
             "%lu/%lu ports   %lu open",
             (unsigned long)status->ports_done,
             (unsigned long)status->ports_total,
             (unsigned long)status->found);
  lv_label_set_text(s_run_progress, text);
  if (status->error != ESP_OK)
    snprintf(text, sizeof(text), "%s", esp_err_to_name(status->error));
  else if (status->storage_error != ESP_OK)
    snprintf(text,
             sizeof(text),
             "%s",
             status->archive_path[0] ? "SD write failed" : "No SD: recent 128 only");
  else if (status->is_probing)
    snprintf(text,
             sizeof(text),
             "PING x%u   %lu skipped",
             status->probe_count,
             (unsigned long)status->hosts_skipped);
  else if (status->uncertain != 0 || status->filtered != 0)
    snprintf(text,
             sizeof(text),
             "%lu uncertain   %lu filtered",
             (unsigned long)status->uncertain,
             (unsigned long)status->filtered);
  else
    snprintf(text,
             sizeof(text),
             "%lu saved   %lu skipped",
             (unsigned long)status->saved,
             (unsigned long)status->hosts_skipped);
  lv_label_set_text(s_run_storage, text);
  uint64_t total = is_discovery ? status->hosts_total : (uint64_t)hosts_total * status->ports_total;
  uint64_t done = is_discovery ? status->hosts_probed : (uint64_t)hosts_done * status->ports_total;
  if (!is_discovery && hosts_done < hosts_total)
    done += status->ports_done;
  uint32_t progress = total == 0 ? (status->state == PORT_SCANNER_DONE ? RUN_BAR_MAX : 0)
                                 : done * RUN_BAR_MAX / total;
  lv_bar_set_value(s_run_bar, progress, LV_ANIM_OFF);
  if (s_is_following_latest) {
    s_run_first = status->stored > s_run_rows ? status->stored - s_run_rows : 0;
    s_run_selected = status->stored != 0 ? (status->stored - s_run_first - 1) : 0;
  }
  if (s_run_selected >= s_run_rows)
    s_run_selected = s_run_rows - 1;
  s_run_count = 0;
  for (uint16_t i = 0; i < s_run_rows; ++i) {
    if (port_scanner_get_result(s_run_first + i, &s_run_results[i])) {
      ++s_run_count;
      lv_obj_remove_flag(s_run_items[i], LV_OBJ_FLAG_HIDDEN);
      snprintf(text,
               sizeof(text),
               "%s:%d/%s",
               s_run_results[i].ip_str,
               s_run_results[i].port,
               s_run_results[i].protocol == PORT_SCAN_PROTO_TCP ? "TCP" : "UDP");
      lv_label_set_text(s_run_titles[i], text);
      const char *banner = s_run_results[i].banner;
      if (s_run_results[i].status == PORT_SCAN_STATUS_OPEN_FILTERED)
        banner = "OPEN|FILTERED: no UDP reply";
      else if (s_run_results[i].status == PORT_SCAN_STATUS_FILTERED)
        banner = "FILTERED: ICMP unreachable";
      else if (banner[0] == '\0')
        banner = s_run_results[i].protocol == PORT_SCAN_PROTO_UDP ? "UDP OPEN: reply received"
                                                                  : "TCP OPEN: no banner";
      lv_label_set_text(s_run_banners[i], banner);
      lv_obj_set_style_bg_opa(
          s_run_items[i], i == s_run_selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    } else {
      lv_obj_add_flag(s_run_items[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (s_run_count == 0) {
    lv_obj_remove_flag(s_run_empty, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(
        s_run_empty,
        is_discovery ? (status->is_busy ? "Finding hosts before ports" : "Port scan not started")
        : status->is_ping_enabled && status->is_discovery_complete && hosts_total == 0
            ? "No hosts replied to ping"
        : status->is_busy ? "Waiting for open ports..."
                          : "No open ports found");
  } else {
    lv_obj_add_flag(s_run_empty, LV_OBJ_FLAG_HIDDEN);
  }
  ui_chrome_footer_set_text(s_run_footer,
                            status->is_busy && !s_is_stop_pressed &&
                                    status->state != PORT_SCANNER_STOPPING
                                ? "BACK stop   UP/DOWN results"
                                : "BACK setup   OK details");
}
