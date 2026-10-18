// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_scan_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sys_prio.h"

#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "sd_card_init.h"
#include "storage_mkdir.h"
#include "storage_stream.h"
#include "tos_loot.h"
#include "tos_storage_paths.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "wifi_service.h"

#include "hb_spectrum.h"

static const char *TAG = "WIFI_SURVEY_UI";

#define SURVEY_MAX_APS    WIFI_SCAN_LIST_SIZE
#define RESULTS_PAGE_SIZE MENU_COMP_MAX_ITEMS
#define HISTORY_POINTS    20
#define TASK_STACK_SIZE   10240
#define SURVEY_DWELL_MIN  45
#define SURVEY_DWELL_MAX  90

typedef enum { SURVEY_CONFIG, SURVEY_RUNNING, SURVEY_RESULTS } survey_view_t;
typedef enum { MODE_SNAPSHOT, MODE_CONTINUOUS, MODE_30_SEC, MODE_2_MIN } survey_mode_t;
typedef enum { FILTER_ALL, FILTER_OPEN, FILTER_SECURE } survey_filter_t;

typedef struct {
  char ssid[33];
  uint8_t bssid[6];
  uint8_t channel;
  uint8_t second;
  int8_t rssi;
  int8_t rssi_min;
  int8_t rssi_max;
  int32_t rssi_sum;
  uint16_t samples;
  uint8_t authmode;
  uint8_t pairwise_cipher;
  uint8_t group_cipher;
  uint8_t bandwidth;
  uint8_t phy;
  uint8_t country[3];
} survey_ap_t;

static const char *const MODE_NAMES[] = {"Snapshot", "Continuous", "30 seconds", "2 minutes"};
static const char *const BAND_NAMES[] = {"Dual", "2.4 GHz", "5 GHz"};
static const char *const FILTER_NAMES[] = {"All", "Open", "Protected"};

static lv_obj_t *s_screen;
static menu_component_t s_menu;
static survey_view_t s_view;
static survey_mode_t s_mode = MODE_SNAPSHOT;
static uint8_t s_band;
static survey_filter_t s_filter;

static survey_ap_t s_aps[SURVEY_MAX_APS];
static int s_ap_count;
static int s_filtered_idx[SURVEY_MAX_APS];
static int s_filtered_count;
static int s_page;
static uint32_t s_cycles;
static uint32_t s_elapsed_ms;
static uint16_t s_visible_2g;
static uint16_t s_visible_5g;
static bool s_saved;
static char s_json_path[256];
static char s_csv_path[256];

static volatile bool s_scan_run;
static volatile bool s_worker_busy;
static uint32_t s_generation;

static lv_obj_t *s_summary_label;
static lv_obj_t *s_progress_label;
static lv_obj_t *s_strong_label;
static lv_obj_t *s_spectrum;
static hb_band_t s_spec_band;

static const char *auth_name(uint8_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-EAP";
    case WIFI_AUTH_WPA3_ENT_192:
      return "WPA3-EAP";
    case WIFI_AUTH_OWE:
      return "OWE";
    default:
      return "SECURED";
  }
}

static const char *cipher_name(uint8_t cipher) {
  switch (cipher) {
    case WIFI_CIPHER_TYPE_NONE:
      return "NONE";
    case WIFI_CIPHER_TYPE_WEP40:
    case WIFI_CIPHER_TYPE_WEP104:
      return "WEP";
    case WIFI_CIPHER_TYPE_TKIP:
      return "TKIP";
    case WIFI_CIPHER_TYPE_CCMP:
      return "CCMP";
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
      return "TKIP+CCMP";
    case WIFI_CIPHER_TYPE_GCMP:
      return "GCMP";
    case WIFI_CIPHER_TYPE_GCMP256:
      return "GCMP256";
    default:
      return "OTHER";
  }
}

static const char *bandwidth_name(uint8_t bandwidth) {
  switch ((wifi_bandwidth_t)bandwidth) {
    case WIFI_BW20:
      return "20MHz";
    case WIFI_BW40:
      return "40MHz";
    case WIFI_BW80:
      return "80MHz";
    case WIFI_BW160:
      return "160MHz";
    case WIFI_BW80_BW80:
      return "80+80MHz";
    default:
      return "?MHz";
  }
}

static uint8_t bandwidth_mhz(uint8_t bandwidth) {
  switch ((wifi_bandwidth_t)bandwidth) {
    case WIFI_BW40:
      return 40;
    case WIFI_BW80:
      return 80;
    case WIFI_BW160:
    case WIFI_BW80_BW80:
      return 160;
    default:
      return 20;
  }
}

static int signal_quality(int8_t rssi) {
  int q = (rssi + 100) * 2;
  if (q < 0)
    q = 0;
  if (q > 100)
    q = 100;
  return q;
}

static const char *band_for_channel(uint8_t channel) {
  return channel >= 15 ? "5G" : "2.4G";
}

static bool ap_matches_filter(const survey_ap_t *ap) {
  if (s_filter == FILTER_OPEN)
    return ap->authmode == WIFI_AUTH_OPEN;
  if (s_filter == FILTER_SECURE)
    return ap->authmode != WIFI_AUTH_OPEN;
  return true;
}

static int compare_rssi_desc(const void *a, const void *b) {
  const survey_ap_t *aa = (const survey_ap_t *)a;
  const survey_ap_t *bb = (const survey_ap_t *)b;
  return (int)bb->rssi - (int)aa->rssi;
}

static int find_bssid(const uint8_t bssid[6]) {
  for (int i = 0; i < s_ap_count; i++) {
    if (memcmp(s_aps[i].bssid, bssid, 6) == 0)
      return i;
  }
  return -1;
}

static void sanitize_ssid(char out[33], const uint8_t in[33]) {
  size_t i = 0;
  for (; i < 32 && in[i] != '\0'; i++) {
    uint8_t c = in[i];
    out[i] = (c >= 0x20 && c <= 0x7e) ? (char)c : '?';
  }
  out[i] = '\0';
  if (out[0] == '\0')
    strcpy(out, "[hidden]");
}

static void merge_record(const wifi_ap_record_t *rec) {
  int idx = find_bssid(rec->bssid);
  if (idx < 0) {
    if (s_ap_count < SURVEY_MAX_APS) {
      idx = s_ap_count++;
      memset(&s_aps[idx], 0, sizeof(s_aps[idx]));
      memcpy(s_aps[idx].bssid, rec->bssid, 6);
      s_aps[idx].rssi_min = rec->rssi;
      s_aps[idx].rssi_max = rec->rssi;
    } else {
      int weakest = s_ap_count - 1;
      if (rec->rssi <= s_aps[weakest].rssi)
        return;
      idx = weakest;
      memset(&s_aps[idx], 0, sizeof(s_aps[idx]));
      memcpy(s_aps[idx].bssid, rec->bssid, 6);
      s_aps[idx].rssi_min = rec->rssi;
      s_aps[idx].rssi_max = rec->rssi;
    }
  }

  survey_ap_t *ap = &s_aps[idx];
  sanitize_ssid(ap->ssid, rec->ssid);
  ap->channel = rec->primary;
  ap->second = (uint8_t)rec->second;
  ap->rssi = rec->rssi;
  if (rec->rssi < ap->rssi_min)
    ap->rssi_min = rec->rssi;
  if (rec->rssi > ap->rssi_max)
    ap->rssi_max = rec->rssi;
  ap->rssi_sum += rec->rssi;
  if (ap->samples < UINT16_MAX)
    ap->samples++;
  ap->authmode = (uint8_t)rec->authmode;
  ap->pairwise_cipher = (uint8_t)rec->pairwise_cipher;
  ap->group_cipher = (uint8_t)rec->group_cipher;
  ap->bandwidth = (uint8_t)rec->bandwidth;
  ap->phy = (uint8_t)((rec->phy_11a ? 0x01 : 0) | (rec->phy_11b ? 0x02 : 0) |
                      (rec->phy_11g ? 0x04 : 0) | (rec->phy_11n ? 0x08 : 0) |
                      (rec->phy_11ac ? 0x10 : 0) | (rec->phy_11ax ? 0x20 : 0));
  memcpy(ap->country, rec->country.cc, sizeof(ap->country));
}

static uint32_t mode_duration_ms(void) {
  if (s_mode == MODE_30_SEC)
    return 30000;
  if (s_mode == MODE_2_MIN)
    return 120000;
  return 0;
}

static void write_text(storage_stream_t stream, const char *text) {
  if (stream != NULL && text != NULL && text[0] != '\0')
    storage_stream_write(stream, text, strlen(text));
}

static void json_escape(const char *src, char *dst, size_t dst_size) {
  size_t o = 0;
  for (size_t i = 0; src[i] != '\0' && o + 2 < dst_size; i++) {
    char c = src[i];
    if (c == '"' || c == '\\')
      dst[o++] = '\\';
    dst[o++] = c;
  }
  dst[o] = '\0';
}

static bool save_survey(void) {
  s_saved = false;
  s_json_path[0] = '\0';
  s_csv_path[0] = '\0';
  if (!sd_is_mounted() || s_ap_count == 0)
    return false;
  if (storage_mkdir_recursive(TOS_PATH_WIFI_LOOT_SURVEYS) != ESP_OK)
    return false;

  tos_loot_generate_path(
      TOS_PATH_WIFI_LOOT_SURVEYS, "survey", "json", s_json_path, sizeof(s_json_path), NULL, 0);
  strncpy(s_csv_path, s_json_path, sizeof(s_csv_path) - 1);
  s_csv_path[sizeof(s_csv_path) - 1] = '\0';
  char *dot = strrchr(s_csv_path, '.');
  if (dot != NULL)
    strcpy(dot, ".csv");

  storage_stream_t json = storage_stream_open(s_json_path, "wb");
  storage_stream_t csv = storage_stream_open(s_csv_path, "wb");
  if (json == NULL || csv == NULL) {
    if (json != NULL)
      storage_stream_close(json);
    if (csv != NULL)
      storage_stream_close(csv);
    return false;
  }

  char line[384];
  snprintf(line,
           sizeof(line),
           "{\"mode\":\"%s\",\"band\":\"%s\",\"cycles\":%lu,\"elapsed_ms\":%lu,"
           "\"access_points\":[\n",
           MODE_NAMES[s_mode],
           BAND_NAMES[s_band],
           (unsigned long)s_cycles,
           (unsigned long)s_elapsed_ms);
  write_text(json, line);
  write_text(csv,
             "ssid,bssid,band,channel,rssi_last,rssi_min,rssi_max,rssi_avg,quality,auth,"
             "pairwise,group,samples,country\n");

  for (int i = 0; i < s_ap_count; i++) {
    const survey_ap_t *ap = &s_aps[i];
    char escaped[70];
    json_escape(ap->ssid, escaped, sizeof(escaped));
    int avg = ap->samples ? (int)(ap->rssi_sum / ap->samples) : ap->rssi;
    snprintf(line,
             sizeof(line),
             "%s{\"ssid\":\"%s\",\"bssid\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
             "\"band\":\"%s\",\"channel\":%u,\"rssi\":%d,\"rssi_min\":%d,"
             "\"rssi_max\":%d,\"rssi_avg\":%d,\"quality\":%d,\"auth\":\"%s\","
             "\"pairwise\":\"%s\",\"group\":\"%s\",\"samples\":%u,"
             "\"country\":\"%c%c\"}",
             i ? ",\n" : "",
             escaped,
             ap->bssid[0],
             ap->bssid[1],
             ap->bssid[2],
             ap->bssid[3],
             ap->bssid[4],
             ap->bssid[5],
             band_for_channel(ap->channel),
             ap->channel,
             ap->rssi,
             ap->rssi_min,
             ap->rssi_max,
             avg,
             signal_quality(ap->rssi),
             auth_name(ap->authmode),
             cipher_name(ap->pairwise_cipher),
             cipher_name(ap->group_cipher),
             ap->samples,
             ap->country[0] ? ap->country[0] : '-',
             ap->country[1] ? ap->country[1] : '-');
    write_text(json, line);

    // SSIDs are printable ASCII after sanitization. Replace commas to keep the
    // CSV directly importable without a heavyweight quoting path.
    char csv_ssid[33];
    strncpy(csv_ssid, ap->ssid, sizeof(csv_ssid));
    csv_ssid[sizeof(csv_ssid) - 1] = '\0';
    for (size_t j = 0; csv_ssid[j] != '\0'; j++) {
      if (csv_ssid[j] == ',')
        csv_ssid[j] = ';';
    }
    snprintf(line,
             sizeof(line),
             "%s,%02X:%02X:%02X:%02X:%02X:%02X,%s,%u,%d,%d,%d,%d,%d,%s,%s,%s,%u,%c%c\n",
             csv_ssid,
             ap->bssid[0],
             ap->bssid[1],
             ap->bssid[2],
             ap->bssid[3],
             ap->bssid[4],
             ap->bssid[5],
             band_for_channel(ap->channel),
             ap->channel,
             ap->rssi,
             ap->rssi_min,
             ap->rssi_max,
             avg,
             signal_quality(ap->rssi),
             auth_name(ap->authmode),
             cipher_name(ap->pairwise_cipher),
             cipher_name(ap->group_cipher),
             ap->samples,
             ap->country[0] ? ap->country[0] : '-',
             ap->country[1] ? ap->country[1] : '-');
    write_text(csv, line);
  }
  write_text(json, "\n]}\n");
  storage_stream_flush(json);
  storage_stream_flush(csv);
  bool ok = storage_stream_close(json) == ESP_OK && storage_stream_close(csv) == ESP_OK;
  s_saved = ok;
  return ok;
}

static void reset_screen(void) {
  if (s_screen != NULL)
    lv_obj_del(s_screen);
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  s_summary_label = NULL;
  s_progress_label = NULL;
  s_strong_label = NULL;
  s_spectrum = NULL;
  s_menu = (menu_component_t){0};
}

static void build_config(void) {
  reset_screen();
  s_menu = menu_component_create(s_screen, "WIFI SURVEY", "/assets/icons/wifi_find.bin");
  menu_component_add_selector(&s_menu, "/assets/icons/monitoring.bin", "Mode", MODE_NAMES[s_mode]);
  menu_component_add_selector(&s_menu, "/assets/icons/cell_tower.bin", "Band", BAND_NAMES[s_band]);
  menu_component_add_selector(&s_menu, "/assets/icons/tune.bin", "Filter", FILTER_NAMES[s_filter]);
  menu_component_add_item(&s_menu, "/assets/icons/wifi_find.bin", "START SURVEY");
  menu_component_set_hint(&s_menu, "LEFT/RIGHT change • OK start");
  s_view = SURVEY_CONFIG;
  ui_screen_load_owned(&s_screen, s_screen);
}

static lv_obj_t *center_label(int y, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(s_screen);
  lv_obj_set_width(label, ui_screen_w() - 16);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  return label;
}

#define SPEC_VIEW_MAX 64

// Rebuild the spectrum from the current survey cache for the selected band.
// s_aps is sorted strongest-first, so the first in-band entries are the loudest;
// only those few get an SSID label so the view stays readable.
static void refresh_spectrum(void) {
  if (s_spectrum == NULL)
    return;
  hb_spectrum_ap_t view[SPEC_VIEW_MAX];
  int n = 0;
  int labeled = 0;
  bool want_5g = (s_spec_band == HB_BAND_5);
  for (int i = 0; i < s_ap_count && n < SPEC_VIEW_MAX; i++) {
    bool ap_5g = s_aps[i].channel >= 15;
    if (ap_5g != want_5g)
      continue;
    view[n].channel = s_aps[i].channel;
    view[n].rssi = s_aps[i].rssi;
    view[n].width_mhz = bandwidth_mhz(s_aps[i].bandwidth);
    view[n].label = (labeled < 4) ? s_aps[i].ssid : NULL;
    labeled++;
    n++;
  }
  hb_spectrum_set_aps(s_spectrum, view, n);
}

static void build_running(void) {
  reset_screen();
  ui_chrome_header(s_screen, "LIVE SURVEY", "/assets/icons/wifi_find.bin");
  s_summary_label =
      center_label(UI_CHROME_HEADER_H + 3, &lv_font_montserrat_16, current_theme.border_accent);
  s_progress_label =
      center_label(UI_CHROME_HEADER_H + 27, &lv_font_montserrat_12, current_theme.text_secondary);

  // Channel-occupancy spectrum: sized to fill the gap between the header
  // readout and the footer, clamped to a sane height band.
  int spec_top = UI_CHROME_HEADER_H + 46;
  int spec_h = ui_screen_h() - spec_top - UI_CHROME_FOOTER_H - 22;
  if (spec_h < 80)
    spec_h = 80;
  if (spec_h > 130)
    spec_h = 130;
  s_spectrum = hb_spectrum_create(s_screen, ui_screen_w() - 8, spec_h);
  if (s_spectrum != NULL) {
    lv_obj_align(s_spectrum, LV_ALIGN_TOP_MID, 0, spec_top);
    s_spec_band = (s_band == 2) ? HB_BAND_5 : HB_BAND_24;
    hb_spectrum_set_band(s_spectrum, s_spec_band);
  }

  s_strong_label =
      center_label(spec_top + spec_h + 3, &lv_font_montserrat_12, current_theme.text_main);
  lv_label_set_text(s_summary_label, "Starting dual-band scan...");
  lv_label_set_text_fmt(s_progress_label, "%s • %s", MODE_NAMES[s_mode], BAND_NAMES[s_band]);
  lv_label_set_text(s_strong_label, "Waiting for first sweep");
  ui_chrome_footer(s_screen, "LEFT/RIGHT band • OK stop • BACK exit");
  s_view = SURVEY_RUNNING;
  ui_screen_load_owned(&s_screen, s_screen);
}

static void rebuild_filtered(void) {
  s_filtered_count = 0;
  for (int i = 0; i < s_ap_count; i++) {
    if (ap_matches_filter(&s_aps[i]))
      s_filtered_idx[s_filtered_count++] = i;
  }
  int pages = (s_filtered_count + RESULTS_PAGE_SIZE - 1) / RESULTS_PAGE_SIZE;
  if (pages < 1)
    pages = 1;
  if (s_page >= pages)
    s_page = pages - 1;
}

static void build_results(void) {
  rebuild_filtered();
  reset_screen();
  s_menu = menu_component_create(s_screen, "SURVEY RESULTS", "/assets/icons/wifi_find.bin");
  int first = s_page * RESULTS_PAGE_SIZE;
  int end = first + RESULTS_PAGE_SIZE;
  if (end > s_filtered_count)
    end = s_filtered_count;
  if (first >= end) {
    menu_component_add_item(&s_menu, "/assets/icons/wifi_off.bin", "No matching networks");
  } else {
    for (int i = first; i < end; i++) {
      const survey_ap_t *ap = &s_aps[s_filtered_idx[i]];
      char row[64];
      snprintf(row,
               sizeof(row),
               "%s  %d  %s/%u",
               ap->ssid,
               ap->rssi,
               band_for_channel(ap->channel),
               ap->channel);
      const char *icon = ap->rssi >= -55   ? "/assets/icons/network_wifi_3_bar.bin"
                         : ap->rssi >= -68 ? "/assets/icons/network_wifi_2_bar.bin"
                         : ap->rssi >= -80 ? "/assets/icons/network_wifi_1_bar.bin"
                                           : "/assets/icons/signal_wifi_0_bar.bin";
      menu_component_add_item(&s_menu, icon, row);
      menu_component_set_item_label_color(&s_menu,
                                          i - first,
                                          ap->authmode == WIFI_AUTH_OPEN ? lv_color_hex(0xFFC107)
                                                                         : current_theme.text_main);
    }
  }
  int pages = (s_filtered_count + RESULTS_PAGE_SIZE - 1) / RESULTS_PAGE_SIZE;
  if (pages < 1)
    pages = 1;
  char hint[96];
  snprintf(hint,
           sizeof(hint),
           "%d/%d AP • page %d/%d • %s",
           s_filtered_count,
           s_ap_count,
           s_page + 1,
           pages,
           s_saved ? "SD saved" : "no SD save");
  menu_component_set_hint(&s_menu, hint);
  s_view = SURVEY_RESULTS;
  ui_screen_load_owned(&s_screen, s_screen);
}

static void update_running_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_SCAN_MENU || s_view != SURVEY_RUNNING)
    return;
  lv_label_set_text_fmt(
      s_summary_label, "%d AP   %u 2.4G / %u 5G", s_ap_count, s_visible_2g, s_visible_5g);
  uint32_t seconds = s_elapsed_ms / 1000;
  uint32_t duration = mode_duration_ms() / 1000;
  if (duration > 0)
    lv_label_set_text_fmt(s_progress_label,
                          "Sweep %lu • %02lu:%02lu / %02lu:%02lu",
                          (unsigned long)s_cycles,
                          (unsigned long)(seconds / 60),
                          (unsigned long)(seconds % 60),
                          (unsigned long)(duration / 60),
                          (unsigned long)(duration % 60));
  else
    lv_label_set_text_fmt(s_progress_label,
                          "Sweep %lu • %02lu:%02lu • %s",
                          (unsigned long)s_cycles,
                          (unsigned long)(seconds / 60),
                          (unsigned long)(seconds % 60),
                          BAND_NAMES[s_band]);
  refresh_spectrum();
  if (s_ap_count > 0)
    lv_label_set_text_fmt(s_strong_label,
                          "Strongest: %s  %d dBm  CH %u",
                          s_aps[0].ssid,
                          s_aps[0].rssi,
                          s_aps[0].channel);
}

static void finish_survey_cb(void *unused) {
  uint32_t generation = (uint32_t)(uintptr_t)unused;
  if (generation != s_generation)
    return;
  if (ui_current_screen() != SCREEN_WIFI_SCAN_MENU)
    return;
  s_page = 0;
  build_results();
  if (s_ap_count > 0)
    ui_feedback(UI_FB_READ);
}

static void survey_worker(void *arg) {
  uint32_t generation = (uint32_t)(uintptr_t)arg;
  TickType_t started = xTaskGetTickCount();
  uint32_t target_ms = mode_duration_ms();
  bool one_shot = s_mode == MODE_SNAPSHOT;

  while (s_scan_run) {
    wifi_service_start();
    esp_err_t err = wifi_service_scan_ex(s_band, false, SURVEY_DWELL_MIN, SURVEY_DWELL_MAX);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Survey sweep failed: %s", esp_err_to_name(err));
      break;
    }

    s_visible_2g = 0;
    s_visible_5g = 0;
    uint16_t count = wifi_service_get_ap_count();
    for (uint16_t i = 0; i < count; i++) {
      wifi_ap_record_t *rec = wifi_service_get_ap_record(i);
      if (rec == NULL)
        continue;
      merge_record(rec);
      if (rec->primary >= 15)
        s_visible_5g++;
      else
        s_visible_2g++;
    }
    qsort(s_aps, s_ap_count, sizeof(s_aps[0]), compare_rssi_desc);
    s_cycles++;
    s_elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - started);
    ui_async_call(update_running_cb, NULL);

    if (one_shot || (target_ms > 0 && s_elapsed_ms >= target_ms))
      break;
    vTaskDelay(pdMS_TO_TICKS(180));
  }

  s_scan_run = false;
  s_worker_busy = false;
  save_survey();
  ui_async_call(finish_survey_cb, (void *)(uintptr_t)generation);
  vTaskDelete(NULL);
}

static void start_survey(void) {
  if (s_worker_busy)
    return;
  memset(s_aps, 0, sizeof(s_aps));
  s_ap_count = 0;
  s_cycles = 0;
  s_elapsed_ms = 0;
  s_visible_2g = 0;
  s_visible_5g = 0;
  s_saved = false;
  s_generation++;
  build_running();
  s_scan_run = true;
  s_worker_busy = true;
  if (xTaskCreatePinnedToCore(survey_worker,
                              "wifi_survey",
                              TASK_STACK_SIZE,
                              (void *)(uintptr_t)s_generation,
                              SYS_PRIO_SERVICE_LO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    s_scan_run = false;
    s_worker_busy = false;
    msgbox_open(LV_SYMBOL_WIFI, "Could not start survey task", "OK", NULL, NULL);
  }
}

static void show_ap_details(int visible_index) {
  int absolute = s_page * RESULTS_PAGE_SIZE + visible_index;
  if (absolute < 0 || absolute >= s_filtered_count)
    return;
  const survey_ap_t *ap = &s_aps[s_filtered_idx[absolute]];
  int avg = ap->samples ? (int)(ap->rssi_sum / ap->samples) : ap->rssi;
  char phy[40] = {0};
  if (ap->phy & 0x01)
    strcat(phy, "a/");
  if (ap->phy & 0x02)
    strcat(phy, "b/");
  if (ap->phy & 0x04)
    strcat(phy, "g/");
  if (ap->phy & 0x08)
    strcat(phy, "n/");
  if (ap->phy & 0x10)
    strcat(phy, "ac/");
  if (ap->phy & 0x20)
    strcat(phy, "ax/");
  size_t phy_len = strlen(phy);
  if (phy_len > 0)
    phy[phy_len - 1] = '\0';
  else
    strcpy(phy, "unknown");

  char msg[320];
  snprintf(msg,
           sizeof(msg),
           "%s\n%02X:%02X:%02X:%02X:%02X:%02X\n%s  CH %u  %s\n"
           "RSSI %d dBm  quality %d%%\navg %d  range %d..%d  n=%u\n"
           "%s  %s/%s\n802.11 %s  CC %c%c",
           ap->ssid,
           ap->bssid[0],
           ap->bssid[1],
           ap->bssid[2],
           ap->bssid[3],
           ap->bssid[4],
           ap->bssid[5],
           band_for_channel(ap->channel),
           ap->channel,
           bandwidth_name(ap->bandwidth),
           ap->rssi,
           signal_quality(ap->rssi),
           avg,
           ap->rssi_min,
           ap->rssi_max,
           ap->samples,
           auth_name(ap->authmode),
           cipher_name(ap->pairwise_cipher),
           cipher_name(ap->group_cipher),
           phy,
           ap->country[0] ? ap->country[0] : '-',
           ap->country[1] ? ap->country[1] : '-');
  msgbox_open(LV_SYMBOL_WIFI, msg, "OK", NULL, NULL);
}

static void cycle_config(int direction) {
  int sel = menu_component_get_selected(&s_menu);
  if (sel == 0) {
    s_mode = (survey_mode_t)((s_mode + 4 + direction) % 4);
    menu_component_set_selector_value(&s_menu, 0, MODE_NAMES[s_mode]);
  } else if (sel == 1) {
    s_band = (uint8_t)((s_band + 3 + direction) % 3);
    menu_component_set_selector_value(&s_menu, 1, BAND_NAMES[s_band]);
  } else if (sel == 2) {
    s_filter = (survey_filter_t)((s_filter + 3 + direction) % 3);
    menu_component_set_selector_value(&s_menu, 2, FILTER_NAMES[s_filter]);
  }
}

static void wifi_scan_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = ev->action == INPUT_ACTION_PRESS;
  const bool nav = press || ev->action == INPUT_ACTION_REPEAT;

  if (s_view == SURVEY_CONFIG) {
    if (ev->button == INPUT_BTN_DOWN && nav)
      menu_component_next(&s_menu);
    else if (ev->button == INPUT_BTN_UP && nav)
      menu_component_prev(&s_menu);
    else if (ev->button == INPUT_BTN_LEFT && press)
      cycle_config(-1);
    else if (ev->button == INPUT_BTN_RIGHT && press)
      cycle_config(1);
    else if (ev->button == INPUT_BTN_BACK && press)
      ui_switch_screen(SCREEN_WIFI_MENU);
    else if (ev->button == INPUT_BTN_OK && press) {
      if (menu_component_get_selected(&s_menu) == 3)
        start_survey();
      else
        cycle_config(1);
    }
  } else if (s_view == SURVEY_RUNNING) {
    if (ev->button == INPUT_BTN_OK && press) {
      s_scan_run = false;
      lv_label_set_text(s_progress_label, "Finishing current sweep...");
    } else if (ev->button == INPUT_BTN_LEFT && press) {
      s_spec_band = HB_BAND_24;
      if (s_spectrum != NULL)
        hb_spectrum_set_band(s_spectrum, s_spec_band);
      refresh_spectrum();
    } else if (ev->button == INPUT_BTN_RIGHT && press) {
      s_spec_band = HB_BAND_5;
      if (s_spectrum != NULL)
        hb_spectrum_set_band(s_spectrum, s_spec_band);
      refresh_spectrum();
    } else if (ev->button == INPUT_BTN_BACK && press) {
      s_scan_run = false;
      ui_switch_screen(SCREEN_WIFI_MENU);
    }
  } else {
    if (ev->button == INPUT_BTN_DOWN && nav)
      menu_component_next(&s_menu);
    else if (ev->button == INPUT_BTN_UP && nav)
      menu_component_prev(&s_menu);
    else if (ev->button == INPUT_BTN_LEFT && press && s_page > 0) {
      s_page--;
      build_results();
    } else if (ev->button == INPUT_BTN_RIGHT && press &&
               (s_page + 1) * RESULTS_PAGE_SIZE < s_filtered_count) {
      s_page++;
      build_results();
    } else if (ev->button == INPUT_BTN_BACK && press) {
      build_config();
    } else if (ev->button == INPUT_BTN_OK && press && s_filtered_count > 0) {
      show_ap_details(menu_component_get_selected(&s_menu));
    }
  }
}

void ui_wifi_scan_open(void) {
  s_scan_run = false;
  s_generation++;
  build_config();
  ui_input_set_screen_handler(wifi_scan_input, NULL);
}
