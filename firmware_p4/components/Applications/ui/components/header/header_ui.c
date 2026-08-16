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

#include "header_ui.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "battery_service.h"
#include "bluetooth_service.h"
#include "bq25896.h"
#include "tos_config.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "pin_def.h"
#include "sys_time.h"
#include "ui_feedback.h"
#include "ui_theme.h"
#include "vfs_config.h"
#include "vfs_core.h"
#include "vfs_sdcard.h"
#include "wifi_service.h"

#define HEADER_HEIGHT ((LCD_V_RES * 9) / 100)

#define HEADER_ACTIVE_TINT_HEX 0x00E676
#define SD_CD_PRESENT_LEVEL    0

#define STATUS_POLL_MS       1000
#define WIFI_ANIM_MS         800
#define BATTERY_CHARGE_ANIM_MS 350

#define SD_MOUNT_RETRIES        3
#define SD_MOUNT_RETRY_DELAY_MS 150
#define SD_TASK_STACK           6144 // vfs mount/unmount logs via the deep console path
#define SD_CD_DEBOUNCE_MS       150  // settle the card-detect switch after an edge

static lv_obj_t *bt_img_ref = NULL;
static lv_obj_t *card_img_ref = NULL;
static bool s_ble_active = false;
static lv_timer_t *header_poll_timer = NULL;
static lv_obj_t *s_lbl_time = NULL;

static bool s_bt_tint_last = false;
static bool s_card_shown_last = false;
static bool s_wifi_shown_last = false;
static bool s_wifi_connected_last = false;

static void header_sync_wifi_icon(void);
static bool s_cd_configured = false;
static bool s_sd_mounted = false;
static int s_sd_used_pct = 0;
static TaskHandle_t s_cd_task = NULL;       // debounces the CD ISR + (un)mounts
static bool s_sd_present_committed = false;  // last debounced CD state acted on
static volatile bool s_sd_remount_req = false;
static char s_sd_name[24];
static char s_sd_size[16];
static char s_sd_free[16];
static char s_sd_fmt[12];

static void apply_active_tint(lv_obj_t *obj, bool active) {
  if (!obj || !lv_obj_is_valid(obj))
    return;
  lv_obj_set_style_text_color(
      obj, active ? lv_color_hex(HEADER_ACTIVE_TINT_HEX) : current_theme.text_main, 0);
  lv_obj_set_style_text_opa(obj, active ? LV_OPA_COVER : LV_OPA_50, 0);
}

static void header_sync_ble_icon(void) {
  bool running = bluetooth_service_is_running_cached();
  bool shown = g_config_ble.enabled || running;
  if (shown == s_ble_active && running == s_bt_tint_last)
    return;
  s_ble_active = shown;
  s_bt_tint_last = running;
  if (!bt_img_ref || !lv_obj_is_valid(bt_img_ref))
    return;
  if (!shown) {
    lv_obj_add_flag(bt_img_ref, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(bt_img_ref, LV_OBJ_FLAG_HIDDEN);
  apply_active_tint(bt_img_ref, running);
}

static void sd_cd_isr(void *arg);
static void sd_cd_task(void *arg);

static void sd_cd_ensure_configured(void) {
  if (s_cd_configured)
    return;

  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << GPIO_SD_CD_PIN,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_ANYEDGE, // interrupt-driven hotplug (item 20)
  };
  gpio_config(&cfg);

  // The debounce + (un)mount worker. The ISR only notifies it; blocking work
  // (vfs mount/statvfs) runs here and UI updates hop to the LVGL thread via
  // lv_async_call. Boot state is handled by the task's first pass.
  if (s_cd_task == NULL) {
    xTaskCreatePinnedToCore(sd_cd_task, "sd_cd", SD_TASK_STACK, NULL, SYS_PRIO_SERVICE_LO, &s_cd_task,
                            SYS_CORE_RADIO);
  }

  gpio_isr_handler_add(GPIO_SD_CD_PIN, sd_cd_isr, NULL);

  s_cd_configured = true;
}

static bool sd_card_present(void) {
  return gpio_get_level(GPIO_SD_CD_PIN) == SD_CD_PRESENT_LEVEL;
}

static void set_card_icon_shown(bool shown) {
  if (!card_img_ref || !lv_obj_is_valid(card_img_ref))
    return;
  if (shown) {
    lv_obj_remove_flag(card_img_ref, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(card_img_ref, LV_OBJ_FLAG_HIDDEN);
  }
}

static void fmt_bytes(char *out, size_t n, uint64_t bytes) {
  const uint64_t gb = 1024ULL * 1024 * 1024;
  const uint64_t mb = 1024ULL * 1024;
  if (bytes >= gb) {
    uint64_t t = (bytes * 10) / gb;
    snprintf(out, n, "%llu.%llu GB", (unsigned long long)(t / 10), (unsigned long long)(t % 10));
  } else if (bytes >= mb) {
    snprintf(out, n, "%llu MB", (unsigned long long)(bytes / mb));
  } else {
    snprintf(out, n, "%llu KB", (unsigned long long)(bytes / 1024));
  }
}

// --- LVGL-thread UI updates (posted from the CD task via lv_async_call) ---

static void sd_apply_mounted(void *arg) {
  bool boot = (bool)(intptr_t)arg;
  s_sd_mounted = true;
  set_card_icon_shown(true);
  s_card_shown_last = true;
  if (!boot) {
    ui_feedback(UI_FB_SD_CONNECT);
    msgbox_open_sd_info(s_sd_name, s_sd_size, s_sd_free, s_sd_fmt);
  }
}

static void sd_apply_removed(void *arg) {
  (void)arg;
  bool was = s_sd_mounted;
  s_sd_mounted = false;
  s_sd_used_pct = 0;
  set_card_icon_shown(false);
  s_card_shown_last = false;
  if (was) {
    ui_feedback(UI_FB_SD_DISCONNECT);
    notify(NOTIFY_WARNING, "SD card removed");
  }
}

void header_ui_sd_eject(void) {
  if (vfs_sdcard_is_mounted())
    vfs_sdcard_deinit();
  s_sd_mounted = false;
  s_sd_used_pct = 0;
  set_card_icon_shown(false);
  s_card_shown_last = false;
}

// Mount (with retries) and gather the info strings. Runs on the CD task.
static bool sd_try_mount(void) {
  bool ok = vfs_sdcard_is_mounted();
  for (int i = 0; !ok && i < SD_MOUNT_RETRIES; i++) {
    if (!sd_card_present()) {
      break;
    }
    ok = (vfs_sdcard_init() == ESP_OK);
    if (!ok) {
      vTaskDelay(pdMS_TO_TICKS(SD_MOUNT_RETRY_DELAY_MS));
    }
  }
  if (!ok) {
    return false;
  }
  if (!vfs_sdcard_get_name(s_sd_name, sizeof(s_sd_name))) {
    s_sd_name[0] = '\0';
  }
  vfs_statvfs_t st;
  if (vfs_statvfs(VFS_MOUNT_POINT, &st) == ESP_OK) {
    fmt_bytes(s_sd_size, sizeof(s_sd_size), st.total_bytes);
    fmt_bytes(s_sd_free, sizeof(s_sd_free), st.free_bytes);
    s_sd_used_pct = (st.total_bytes > 0) ? (int)((st.used_bytes * 100) / st.total_bytes) : 0;
  } else {
    snprintf(s_sd_size, sizeof(s_sd_size), "-");
    snprintf(s_sd_free, sizeof(s_sd_free), "-");
    s_sd_used_pct = 0;
  }
  snprintf(s_sd_fmt, sizeof(s_sd_fmt), "FAT32");
  return true;
}

static void IRAM_ATTR sd_cd_isr(void *arg) {
  (void)arg;
  BaseType_t hpw = pdFALSE;
  if (s_cd_task != NULL) {
    vTaskNotifyGiveFromISR(s_cd_task, &hpw);
  }
  portYIELD_FROM_ISR(hpw);
}

static void sd_cd_task(void *arg) {
  (void)arg;
  bool boot = true;
  for (;;) {
    if (!boot) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);       // wait for a CD edge
      vTaskDelay(pdMS_TO_TICKS(SD_CD_DEBOUNCE_MS));  // let the switch settle
      ulTaskNotifyTake(pdTRUE, 0);                   // drain bounces during settle
    }

    bool force = s_sd_remount_req;
    s_sd_remount_req = false;

    bool present = sd_card_present();
    if (!boot && !force && present == s_sd_present_committed) {
      continue;  // spurious edge, no real change
    }

    if (force && vfs_sdcard_is_mounted()) {
      vfs_sdcard_deinit();  // drop the unhealthy mount so it is rebuilt fresh
    }
    s_sd_present_committed = present;

    if (present) {
      if (sd_try_mount()) {
        lv_async_call(sd_apply_mounted, (void *)(intptr_t)(boot || force));
      } else if (vfs_sdcard_is_mounted()) {
        vfs_sdcard_deinit();
      }
    } else if (!boot) {
      // Real removal (not a card-less boot): unmount and tell the UI.
      if (vfs_sdcard_is_mounted()) {
        vfs_sdcard_deinit();
      }
      lv_async_call(sd_apply_removed, NULL);
    }

    boot = false;
  }
}

void header_ui_request_sd_remount(void) {
  s_sd_remount_req = true;
  if (s_cd_task != NULL)
    xTaskNotifyGive(s_cd_task);
}

static void battery_apply(void);

static void header_set_clock_label(lv_obj_t *lbl) {
  if (lbl == NULL) {
    return;
  }
  char buf[8];
  if (!sys_time_format(buf, sizeof(buf), "%H:%M")) {
    snprintf(buf, sizeof(buf), "--:--");
  }
  const char *cur = lv_label_get_text(lbl);
  if (cur != NULL && strcmp(cur, buf) == 0) {
    return;
  }
  lv_label_set_text(lbl, buf);
}

static void header_time_del_cb(lv_event_t *e) {
  if (lv_event_get_target(e) == s_lbl_time) {
    s_lbl_time = NULL;
  }
}

static void header_poll_cb(lv_timer_t *timer) {
  (void)timer;
  header_sync_wifi_icon();
  header_sync_ble_icon();
  battery_apply();
  if (s_lbl_time != NULL && lv_obj_is_valid(s_lbl_time)) {
    header_set_clock_label(s_lbl_time);
  }
}

bool header_ui_sd_usage(int *out_used_pct) {
  if (out_used_pct != NULL) {
    *out_used_pct = s_sd_used_pct;
  }
  return s_sd_mounted;
}

static lv_font_t *inter_font = NULL;

static lv_obj_t *wifi_img = NULL;
static lv_image_dsc_t *wifi_dscs[4] = {NULL};
static int wifi_frame = 0;
static int wifi_dir = 1;
static lv_timer_t *wifi_anim_timer = NULL;

static const char *wifi_paths[4] = {
    "/assets/icons/wifi_icon_0.bin",
    "/assets/icons/wifi_icon_1.bin",
    "/assets/icons/wifi_icon_2.bin",
    "/assets/icons/wifi_icon_3.bin",
};

static lv_obj_t *battery_cont = NULL;
static lv_obj_t *battery_img = NULL;
static lv_obj_t *power_img = NULL;
static lv_image_dsc_t *battery_dscs[4] = {NULL};
static int battery_frame = 0;
static lv_timer_t *battery_charge_timer = NULL;

// Last-shown battery state, so an on-battery (not charging) header only repaints
// when the level/low/charging/present actually changes instead of every tick.
static int s_batt_soc_idx_last = -1;
static bool s_batt_low_last = false;
static bool s_batt_charging_last = false;
static bool s_batt_present_last = false;

static const char *battery_paths[4] = {
    "/assets/icons/battery_1.bin",
    "/assets/icons/battery_2.bin",
    "/assets/icons/battery_3.bin",
    "/assets/icons/battery_4.bin",
};

// Static WiFi icon reflecting the real (cached) state: full when connected, a
// mid bar when on but not connected, empty when off. Never animates here.
static void update_wifi_icon_static(void) {
  if (!wifi_img || !lv_obj_is_valid(wifi_img)) {
    return;
  }
  if (!s_wifi_shown_last) {
    lv_obj_add_flag(wifi_img, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(wifi_img, LV_OBJ_FLAG_HIDDEN);
  int frame = s_wifi_connected_last ? 3 : 2;
  if (wifi_dscs[frame]) {
    lv_image_set_src(wifi_img, wifi_dscs[frame]);
  }
  lv_obj_set_style_opa(wifi_img, s_wifi_connected_last ? LV_OPA_COVER : LV_OPA_50, 0);
}

// Called from the status timer: refresh the icon only when the WiFi state
// changed, and never while a connection animation is running (it would fight it).
// wifi_service_is_active/is_connected are cheap cached reads on the P4, so this
// is not an SPI transaction. Replaces the old 2 Hz timer that wrote variables
// nobody read.
static void header_sync_wifi_icon(void) {
  bool shown = g_config_wifi.enabled || wifi_service_is_active();
  bool connected = wifi_service_is_connected();
  if (shown == s_wifi_shown_last && connected == s_wifi_connected_last) {
    return;
  }
  s_wifi_shown_last = shown;
  s_wifi_connected_last = connected;
  if (wifi_anim_timer == NULL) {
    update_wifi_icon_static();
  }
}

static void wifi_anim_timer_cb(lv_timer_t *timer) {
  if (!wifi_img || !lv_obj_is_valid(wifi_img)) {
    lv_timer_delete(timer);
    wifi_anim_timer = NULL;
    wifi_img = NULL;
    return;
  }

  wifi_frame += wifi_dir;
  if (wifi_frame >= 3) {
    wifi_frame = 3;
    wifi_dir = -1;
  }
  if (wifi_frame <= 0) {
    wifi_frame = 0;
    wifi_dir = 1;
  }

  if (wifi_dscs[wifi_frame]) {
    lv_image_set_src(wifi_img, wifi_dscs[wifi_frame]);
  }
}

void header_ui_set_wifi_connecting(bool connecting) {
  if (connecting) {
    if (wifi_anim_timer == NULL && wifi_img && lv_obj_is_valid(wifi_img)) {
      wifi_frame = 0;
      wifi_dir = 1;
      wifi_anim_timer = lv_timer_create(wifi_anim_timer_cb, WIFI_ANIM_MS, NULL);
    }
  } else {
    if (wifi_anim_timer != NULL) {
      lv_timer_delete(wifi_anim_timer);
      wifi_anim_timer = NULL;
    }
    update_wifi_icon_static();  // settle on the real state
  }
}

static void battery_charge_anim_cb(lv_timer_t *timer);

static void battery_apply(void) {
  if (!battery_cont || !lv_obj_is_valid(battery_cont))
    return;

  battery_snapshot_t bs;
  if (!battery_service_get(&bs))
    return;

  // No charger on I2C: drop the whole cell so the flex row leaves no empty slot.
  if (!bs.present) {
    if (s_batt_present_last) {
      lv_obj_add_flag(battery_cont, LV_OBJ_FLAG_HIDDEN);
      s_batt_present_last = false;
    }
    return;
  }
  if (!s_batt_present_last) {
    lv_obj_remove_flag(battery_cont, LV_OBJ_FLAG_HIDDEN);
    s_batt_present_last = true;
  }

  if (!battery_img || !lv_obj_is_valid(battery_img))
    return;

  int soc_idx;
  if (bs.soc < 20)
    soc_idx = 0;
  else if (bs.soc < 45)
    soc_idx = 1;
  else if (bs.soc < 75)
    soc_idx = 2;
  else
    soc_idx = 3;

  bool charging_changed = (bs.charging != s_batt_charging_last);

  if (bs.charging) {
    if (charging_changed)
      lv_obj_set_style_image_recolor_opa(battery_img, LV_OPA_TRANSP, 0);
    if (battery_charge_timer == NULL)
      battery_charge_timer = lv_timer_create(battery_charge_anim_cb, BATTERY_CHARGE_ANIM_MS, NULL);
  } else {
    if (battery_charge_timer != NULL) {
      lv_timer_delete(battery_charge_timer);
      battery_charge_timer = NULL;
    }
    // On battery: static frame. Only write when the shown level/low changed (or
    // we just stopped charging), so an idle header stops forcing redraws.
    if (charging_changed || soc_idx != s_batt_soc_idx_last) {
      if (battery_dscs[soc_idx])
        lv_image_set_src(battery_img, battery_dscs[soc_idx]);
    }
    if (charging_changed || bs.low != s_batt_low_last) {
      if (bs.low) {
        lv_obj_set_style_image_recolor(battery_img, lv_color_hex(0xE53935), 0);
        lv_obj_set_style_image_recolor_opa(battery_img, LV_OPA_70, 0);
      } else {
        lv_obj_set_style_image_recolor_opa(battery_img, LV_OPA_TRANSP, 0);
      }
    }
  }

  // Bolt shows ONLY while actually charging (standard status-bar behavior) — no
  // bolt when merely plugged-and-idle, charge-done, or on battery. Toggle only on
  // the charging transition.
  if (charging_changed && power_img && lv_obj_is_valid(power_img)) {
    if (bs.charging)
      lv_obj_remove_flag(power_img, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(power_img, LV_OBJ_FLAG_HIDDEN);
  }

  s_batt_soc_idx_last = soc_idx;
  s_batt_low_last = bs.low;
  s_batt_charging_last = bs.charging;
}

static void battery_charge_anim_cb(lv_timer_t *timer) {
  if (!battery_img || !lv_obj_is_valid(battery_img)) {
    lv_timer_delete(timer);
    battery_charge_timer = NULL;
    return;
  }
  battery_frame = (battery_frame + 1) & 3;
  if (battery_dscs[battery_frame])
    lv_image_set_src(battery_img, battery_dscs[battery_frame]);
}

// Safety net: when a status cluster is deleted (its screen/overlay is freed), null
// any global pointer that still belongs to it so the singleton timers never touch
// freed memory. A newer header may have already rebound the globals to a different
// container — then the parent check fails and we correctly leave them intact.
static void header_status_del_cb(lv_event_t *e) {
  lv_obj_t *cont = lv_event_get_target(e);
  if (wifi_img && lv_obj_get_parent(wifi_img) == cont)
    wifi_img = NULL;
  if (bt_img_ref && lv_obj_get_parent(bt_img_ref) == cont)
    bt_img_ref = NULL;
  if (card_img_ref && lv_obj_get_parent(card_img_ref) == cont)
    card_img_ref = NULL;
  if (battery_cont && lv_obj_get_parent(battery_cont) == cont) {
    battery_cont = NULL;
    battery_img = NULL;
    power_img = NULL;
  }
}

void header_ui_attach_status(lv_obj_t *parent, int y_offset) {
  lv_obj_t *icon_cont = lv_obj_create(parent);
  lv_obj_set_size(icon_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(icon_cont, LV_ALIGN_RIGHT_MID, -6, y_offset);
  lv_obj_set_flex_flow(icon_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      icon_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(icon_cont, 10, 0);
  lv_obj_set_style_pad_all(icon_cont, 0, 0);
  lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(icon_cont, 0, 0);
  lv_obj_add_event_cb(icon_cont, header_status_del_cb, LV_EVENT_DELETE, NULL);

  for (int i = 0; i < 4; i++) {
    if (!wifi_dscs[i])
      wifi_dscs[i] = assets_get(wifi_paths[i]);
  }

  wifi_img = lv_image_create(icon_cont);
  if (wifi_dscs[0])
    lv_image_set_src(wifi_img, wifi_dscs[0]);

  lv_obj_t *bt_img = lv_label_create(icon_cont);
  lv_label_set_text(bt_img, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_font(bt_img, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(bt_img, current_theme.text_main, 0);
  bt_img_ref = bt_img;

  lv_obj_t *card_img = lv_label_create(icon_cont);
  lv_label_set_text(card_img, LV_SYMBOL_SD_CARD);
  lv_obj_set_style_text_font(card_img, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(card_img, current_theme.text_main, 0);
  card_img_ref = card_img;

  sd_cd_ensure_configured();
  set_card_icon_shown(s_sd_mounted);
  s_card_shown_last = s_sd_mounted;
  s_bt_tint_last = bluetooth_service_is_running_cached();
  s_ble_active = g_config_ble.enabled || s_bt_tint_last;
  if (s_ble_active) {
    lv_obj_remove_flag(bt_img_ref, LV_OBJ_FLAG_HIDDEN);
    apply_active_tint(bt_img_ref, s_bt_tint_last);
  } else {
    lv_obj_add_flag(bt_img_ref, LV_OBJ_FLAG_HIDDEN);
  }

  if (header_poll_timer == NULL) {
    header_poll_timer = lv_timer_create(header_poll_cb, STATUS_POLL_MS, NULL);
  }

  for (int i = 0; i < 4; i++) {
    if (!battery_dscs[i])
      battery_dscs[i] = assets_get(battery_paths[i]);
  }

  lv_obj_t *bat_cont = lv_obj_create(icon_cont);
  lv_obj_set_size(bat_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(bat_cont, 0, 0);
  lv_obj_set_style_bg_opa(bat_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bat_cont, 0, 0);
  battery_cont = bat_cont;
  // Start hidden so it doesn't flash before the first battery poll; the timer
  // reveals it only when the charger actually answers on I2C.
  lv_obj_add_flag(bat_cont, LV_OBJ_FLAG_HIDDEN);

  battery_img = lv_image_create(bat_cont);
  if (battery_dscs[2])
    lv_image_set_src(battery_img, battery_dscs[2]);
  lv_obj_center(battery_img);

  power_img = lv_label_create(bat_cont);
  lv_label_set_text(power_img, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_font(power_img, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(power_img, lv_color_white(), 0);
  lv_obj_center(power_img);
  lv_obj_add_flag(power_img, LV_OBJ_FLAG_HIDDEN);

  s_wifi_shown_last = g_config_wifi.enabled || wifi_service_is_active();
  s_wifi_connected_last = wifi_service_is_connected();
  update_wifi_icon_static();

  s_batt_present_last = false;
  s_batt_soc_idx_last = -1;
  s_batt_low_last = false;
  s_batt_charging_last = false;
  battery_apply();
}

void header_ui_create(lv_obj_t *parent) {
  lv_obj_t *header = lv_obj_create(parent);
  lv_obj_set_size(header, lv_pct(100), HEADER_HEIGHT + 12);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, -12);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_style_radius(header, 12, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);

  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(header, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_NONE, 0);

  if (!inter_font) {
    inter_font = lv_binfont_create("A:assets/fonts/Inter.bin");
  }

  s_lbl_time = lv_label_create(header);
  lv_obj_add_event_cb(s_lbl_time, header_time_del_cb, LV_EVENT_DELETE, NULL);
  lv_obj_set_style_text_color(s_lbl_time, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_lbl_time, inter_font ? inter_font : &lv_font_montserrat_12, 0);
  lv_obj_align(s_lbl_time, LV_ALIGN_LEFT_MID, 6, 6);
  header_set_clock_label(s_lbl_time);

  // Home/menu full header: the status cluster is drawn 6px lower to sit on the
  // bar's visual center (the bar is created with a -12 top inset).
  header_ui_attach_status(header, 6);
}

// Static status snapshot: paints the icons at the CURRENT state, but binds NO
// globals and registers NO timers. For transient overlays / temp screens drawn
// over a live screen — they must not rebind the dynamic header (which would dangle
// the globals when the overlay is freed and freeze the screen underneath). It just
// doesn't animate, which is fine for a brief overlay.
void header_ui_attach_status_snapshot(lv_obj_t *parent, int y_offset) {
  lv_obj_t *icon_cont = lv_obj_create(parent);
  lv_obj_set_size(icon_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(icon_cont, LV_ALIGN_RIGHT_MID, -6, y_offset);
  lv_obj_set_flex_flow(icon_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      icon_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(icon_cont, 10, 0);
  lv_obj_set_style_pad_all(icon_cont, 0, 0);
  lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(icon_cont, 0, 0);

  for (int i = 0; i < 4; i++) {
    if (!wifi_dscs[i])
      wifi_dscs[i] = assets_get(wifi_paths[i]);
  }
  bool wifi_on = g_config_wifi.enabled || wifi_service_is_active();
  bool wifi_conn = wifi_service_is_connected();
  lv_obj_t *w = lv_image_create(icon_cont);
  if (wifi_dscs[wifi_conn ? 3 : 2])
    lv_image_set_src(w, wifi_dscs[wifi_conn ? 3 : 2]);
  lv_obj_set_style_opa(w, wifi_conn ? LV_OPA_COVER : LV_OPA_50, 0);
  if (!wifi_on)
    lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *bt = lv_label_create(icon_cont);
  lv_label_set_text(bt, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_font(bt, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(bt, current_theme.text_main, 0);
  bool ble_run = bluetooth_service_is_running_cached();
  if (g_config_ble.enabled || ble_run)
    apply_active_tint(bt, ble_run);
  else
    lv_obj_add_flag(bt, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *sd = lv_label_create(icon_cont);
  lv_label_set_text(sd, LV_SYMBOL_SD_CARD);
  lv_obj_set_style_text_font(sd, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(sd, current_theme.text_main, 0);
  if (!s_sd_mounted)
    lv_obj_add_flag(sd, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < 4; i++) {
    if (!battery_dscs[i])
      battery_dscs[i] = assets_get(battery_paths[i]);
  }
  battery_snapshot_t bs;
  bool present = battery_service_get(&bs) && bs.present;

  lv_obj_t *bcont = lv_obj_create(icon_cont);
  lv_obj_set_size(bcont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(bcont, 0, 0);
  lv_obj_set_style_bg_opa(bcont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bcont, 0, 0);
  if (!present)
    lv_obj_add_flag(bcont, LV_OBJ_FLAG_HIDDEN);

  int soc_idx = present ? (bs.soc < 20 ? 0 : bs.soc < 45 ? 1 : bs.soc < 75 ? 2 : 3) : 2;
  lv_obj_t *bimg = lv_image_create(bcont);
  if (battery_dscs[soc_idx])
    lv_image_set_src(bimg, battery_dscs[soc_idx]);
  lv_obj_center(bimg);
  if (present && bs.low && !bs.charging) {
    lv_obj_set_style_image_recolor(bimg, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_image_recolor_opa(bimg, LV_OPA_70, 0);
  }

  lv_obj_t *pimg = lv_label_create(bcont);
  lv_label_set_text(pimg, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_font(pimg, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(pimg, lv_color_white(), 0);
  lv_obj_center(pimg);
  if (!(present && bs.charging))
    lv_obj_add_flag(pimg, LV_OBJ_FLAG_HIDDEN);
}

void header_ui_create_snapshot(lv_obj_t *parent) {
  lv_obj_t *header = lv_obj_create(parent);
  lv_obj_set_size(header, lv_pct(100), HEADER_HEIGHT + 12);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, -12);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(header, 12, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(header, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_NONE, 0);

  if (!inter_font) {
    inter_font = lv_binfont_create("A:assets/fonts/Inter.bin");
  }
  lv_obj_t *lbl_time = lv_label_create(header);
  lv_obj_set_style_text_color(lbl_time, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl_time, inter_font ? inter_font : &lv_font_montserrat_12, 0);
  lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 6, 6);
  header_set_clock_label(lbl_time);

  header_ui_attach_status_snapshot(header, 6);
}
