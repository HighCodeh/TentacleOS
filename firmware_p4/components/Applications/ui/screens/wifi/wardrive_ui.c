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

#include "wardrive_ui.h"
#include <stdio.h>
#include "esp_log.h"
#include "lvgl.h"
#include "lvgl_glue.h"
#include "ui_manager.h"
#include "wardrive.h"
#include "ui_theme.h"
#include "ui_semantic.h"
#include "led_control.h"
#define WARD_UI_REFRESH_MS   250
#define WARD_BG              lv_color_to_u32(current_theme.screen_base)
#define WARD_PANEL           lv_color_to_u32(current_theme.bg_secondary)
#define WARD_TEXT            lv_color_to_u32(current_theme.text_main)
#define WARD_DIM             lv_color_to_u32(current_theme.text_secondary)
#define WARD_CYAN            lv_color_to_u32(current_theme.protocol_wifi)
#define WARD_GREEN           UI_COL_SUCCESS
#define WARD_ORANGE          UI_COL_WARNING
#define WARD_RED             UI_COL_DANGER
#define WARD_COLUMNS         3
#define WARD_TEXT_BYTES      256
#define WARD_POSITION_HEIGHT 60
static const char *TAG = "WARD_UI";
static lv_obj_t *s_screen, *s_badge, *s_metric[WARD_COLUMNS], *s_rows[WARDRIVE_UI_ROWS];
static lv_obj_t *s_gps, *s_position, *s_card, *s_footer, *s_diag;
static lv_timer_t *s_timer;
static bool s_was_landscape, s_details, s_leave;
static uint8_t s_band;
static void refresh(lv_timer_t *timer);
static void input(const input_event_t *event, void *context);
static lv_obj_t *label(lv_obj_t *parent,
                       const char *text,
                       int x,
                       int y,
                       int w,
                       uint32_t color,
                       const lv_font_t *font) {
  lv_obj_t *o = lv_label_create(parent);
  lv_label_set_text(o, text);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_width(o, w);
  lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(o, font, 0);
  return o;
}
static lv_obj_t *panel(int x, int y, int w, int h) {
  lv_obj_t *o = lv_obj_create(s_screen);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(WARD_PANEL), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, 10, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
void wardrive_ui_open(void) {
  (void)TAG;
  s_was_landscape = lvgl_glue_is_landscape();
  if (!s_was_landscape)
    lvgl_glue_toggle_rotation();
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  s_screen = lv_obj_create(NULL);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(WARD_BG), 0);
  s_details = false;
  s_leave = false;
  label(s_screen, "WARDRIVE", 10, 9, 130, WARD_TEXT, &lv_font_montserrat_16);
  s_badge = label(s_screen, "PRONTO", 165, 11, 145, WARD_CYAN, &lv_font_montserrat_12);
  lv_obj_set_style_text_align(s_badge, LV_TEXT_ALIGN_RIGHT, 0);
  static const char *names[] = {"REDES", "SALVOS", "SATELITES"};
  for (int i = 0; i < WARD_COLUMNS; i++) {
    lv_obj_t *p = panel(10 + i * 102, 36, 96, 52);
    label(p, names[i], 8, 6, 82, WARD_DIM, &lv_font_montserrat_12);
    s_metric[i] =
        label(p, "0", 8, 22, 82, i == 2 ? WARD_ORANGE : WARD_CYAN, &lv_font_montserrat_16);
  }
  lv_obj_t *net = panel(10, 96, 190, 114);
  label(net, "REDES PROXIMAS", 8, 6, 174, WARD_DIM, &lv_font_montserrat_12);
  for (int i = 0; i < WARDRIVE_UI_ROWS; i++)
    s_rows[i] = label(net, "--", 8, 23 + i * 22, 174, WARD_TEXT, &lv_font_montserrat_12);
  lv_obj_t *gps = panel(208, 96, 102, 114);
  s_gps = label(gps, "GPS", 8, 6, 88, WARD_ORANGE, &lv_font_montserrat_12);
  s_position = label(gps, "Aguardando\nrecepcao", 8, 27, 88, WARD_DIM, &lv_font_montserrat_12);
  lv_obj_set_height(s_position, WARD_POSITION_HEIGHT);
  lv_label_set_long_mode(s_position, LV_LABEL_LONG_WRAP);
  s_card = label(gps, "SD: aguardando", 8, 88, 88, WARD_DIM, &lv_font_montserrat_12);
  s_diag = label(s_screen, "", 10, 96, 300, WARD_TEXT, &lv_font_montserrat_12);
  lv_obj_set_height(s_diag, 114);
  lv_obj_set_style_bg_color(s_diag, lv_color_hex(WARD_BG), 0);
  lv_obj_set_style_bg_opa(s_diag, LV_OPA_COVER, 0);
  lv_label_set_long_mode(s_diag, LV_LABEL_LONG_WRAP);
  lv_obj_add_flag(s_diag, LV_OBJ_FLAG_HIDDEN);
  s_footer = label(s_screen,
                   "OK iniciar  |  > detalhes  |  BACK sair",
                   10,
                   221,
                   300,
                   WARD_DIM,
                   &lv_font_montserrat_12);
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_timer = lv_timer_create(refresh, WARD_UI_REFRESH_MS, NULL);
  if (s_timer == NULL) {
    ESP_LOGE(TAG, "Refresh timer allocation failed");
    led_signal_error();
    lv_label_set_text(s_badge, "SEM MEMORIA");
  }
  refresh(NULL);
}
void wardrive_ui_stop(void) {
  wardrive_stop();
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (!s_was_landscape && lvgl_glue_is_landscape())
    lvgl_glue_toggle_rotation();
}
static void refresh(lv_timer_t *timer) {
  (void)timer;
  wardrive_status_t st;
  wardrive_get_status(&st);
  if (s_leave && !st.running) {
    ui_switch_screen(SCREEN_WIFI_MENU);
    return;
  }
  const char *state = st.stopping      ? "FINALIZANDO"
                      : !st.running    ? (st.radio_error ? "ERRO"
                                          : st.buffered  ? "PENDENTE"
                                                         : "PRONTO")
                      : st.paused      ? "PAUSADO"
                      : st.radio_error ? "WIFI: RETRY"
                      : st.scanning    ? "COLETANDO"
                                       : "GRAVANDO";
  lv_label_set_text_fmt(s_badge,
                        "%s %02lu:%02lu",
                        state,
                        (unsigned long)(st.elapsed_s / 60),
                        (unsigned long)(st.elapsed_s % 60));
  lv_obj_set_style_text_color(s_badge, lv_color_hex(st.radio_error ? WARD_RED : WARD_CYAN), 0);
  lv_label_set_text_fmt(s_metric[0], "%lu%s", (unsigned long)st.unique, st.unique_full ? "+" : "");
  lv_label_set_text_fmt(s_metric[1], "%lu", (unsigned long)st.saved);
  lv_label_set_text_fmt(s_metric[2], "%u / %u", st.satellites, st.visible);
  for (int i = 0; i < WARDRIVE_UI_ROWS; i++) {
    const wardrive_row_t *r = &st.rows[i];
    if (r->ssid[0])
      lv_label_set_text_fmt(s_rows[i], "%d  %s", r->rssi, r->ssid);
    else
      lv_label_set_text(s_rows[i], i == 0 ? "Aguardando varredura" : "--");
  }
  lv_label_set_text(s_gps,
                    st.has_fix      ? "GPS FIX"
                    : st.gps_online ? "BUSCANDO"
                    : st.gps_bytes  ? "GPS: SINAL?"
                                    : "SEM GPS");
  lv_obj_set_style_text_color(s_gps, lv_color_hex(st.has_fix ? WARD_GREEN : WARD_ORANGE), 0);
  if (st.has_fix) {
    char position[WARD_TEXT_BYTES];
    // The compact LVGL formatter omits floating-point support in this build.
    snprintf(position,
             sizeof(position),
             "%.5f\n%.5f\nHDOP %.1f\n%.1f km/h",
             st.fix.latitude,
             st.fix.longitude,
             st.fix.hdop,
             st.fix.speed_kmh);
    lv_label_set_text(s_position, position);
  } else
    lv_label_set_text(s_position,
                      st.storage_ok ? "Sem posicao\nWi-Fi salvo\nno microSD"
                      : st.buffered ? "Sem posicao\nDados na RAM\nNao desligue"
                                    : "Aguardando\nsatelites");
  lv_label_set_text(s_card, st.storage_ok ? "SD: OK" : st.storage_error ? "SD: ERRO" : "SD: --");
  lv_obj_set_style_text_color(s_card, lv_color_hex(st.storage_ok ? WARD_GREEN : WARD_ORANGE), 0);
  uint8_t band = st.running ? st.band : s_band;
  char text[WARD_TEXT_BYTES];
  snprintf(text,
           sizeof(text),
           "Wi-Fi %s  |  %lu ciclos  |  %lu erros\nGPS %lu bytes  |  UART %lu  |  NMEA %lu\nGPS "
           "valido: %lu  |  Fila: %lu  |  Perdas: %lu\nSD %s  |  Retry %lus\n%s",
           band == 1   ? "2.4 GHz"
           : band == 2 ? "5 GHz"
                       : "2.4 + 5 GHz",
           (unsigned long)st.cycles,
           (unsigned long)st.scan_errors,
           (unsigned long)st.gps_bytes,
           (unsigned long)st.uart_errors,
           (unsigned long)st.checksum_errors,
           (unsigned long)st.located,
           (unsigned long)st.buffered,
           (unsigned long)st.dropped,
           st.storage_ok ? "OK" : esp_err_to_name(st.storage_error),
           (unsigned long)st.retry_s,
           st.path);
  lv_label_set_text(s_diag, text);
  if (s_details)
    lv_obj_remove_flag(s_diag, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(s_diag, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_footer,
                    st.stopping  ? "Salvando e fechando a sessao..."
                    : st.running ? (st.paused ? "OK retomar  > info  BACK salvar"
                                              : "OK pausa  > info  BACK salvar")
                                 : "OK iniciar  ^ banda  > info  BACK sair");
}
static void input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action != INPUT_ACTION_PRESS)
    return;
  wardrive_status_t st;
  wardrive_get_status(&st);
  if (ev->button == INPUT_BTN_BACK) {
    if (st.running) {
      s_leave = true;
      wardrive_stop();
    } else
      ui_switch_screen(SCREEN_WIFI_MENU);
    return;
  }
  if (ev->button == INPUT_BTN_RIGHT || ev->button == INPUT_BTN_LEFT)
    s_details = !s_details;
  if (ev->button == INPUT_BTN_UP && !st.running) {
    s_band = (s_band + 1) % 3;
    s_details = true;
  }
  if (ev->button == INPUT_BTN_OK) {
    if (st.running)
      wardrive_pause(!st.paused);
    else {
      if (s_timer == NULL)
        return;
      esp_err_t err = wardrive_start(s_band);
      if (err != ESP_OK)
        lv_label_set_text(s_badge, esp_err_to_name(err));
    }
  }
  refresh(NULL);
}
