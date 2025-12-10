#include "home_ui.h"
#include "core/lv_group.h"
#include "misc/lv_palette.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

static const char *TAG = "UI_HOME";

// --- CONFIGURAÇÕES VISUAIS ---
#define THEME_COLOR lv_palette_main(LV_PALETTE_PURPLE) 
#define BG_COLOR    lv_color_black()

// --- VARIÁVEIS GLOBAIS DA TELA ---
static lv_obj_t * screen_home = NULL;
static lv_obj_t * label_time = NULL;
static lv_timer_t * timer_clock = NULL;

// --- CALLBACKS ---

// Atualiza o relógio
static void update_clock_cb(lv_timer_t * timer)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char time_str[8];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    
    if (label_time && lv_obj_is_valid(label_time)) {
        lv_label_set_text(label_time, time_str);
    }
}

static void home_event_cb(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if(code == LV_EVENT_KEY) {
    uint32_t key = lv_event_get_key(e);

    if(key == LV_KEY_LEFT) {
      ui_switch_screen(SCREEN_MENU);
    }
  }
}

// --- CONSTUÇÃO DA UI ---

static void create_status_bar(lv_obj_t * parent)
{
    // Container do Header
    lv_obj_t * header = lv_obj_create(parent);
    lv_obj_set_size(header, 224, 30);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 5);
    
    lv_obj_set_style_bg_color(header, THEME_COLOR, 0);
    lv_obj_set_style_radius(header, 8, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(header, 10, 0);
    lv_obj_set_style_pad_right(header, 10, 0);

    // 1. Relógio (Texto)
    label_time = lv_label_create(header);
    lv_label_set_text(label_time, "00:00");
    lv_obj_set_style_text_color(label_time, lv_color_black(), 0);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_14, 0);

    // 2. Container de Ícones
    lv_obj_t * icons_cont = lv_obj_create(header);
    lv_obj_set_size(icons_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(icons_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icons_cont, 0, 0);
    lv_obj_set_flex_flow(icons_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(icons_cont, 8, 0);

    // Ícone SD (Usando Símbolo)
    lv_obj_t * lbl_sd = lv_label_create(icons_cont);
    lv_label_set_text(lbl_sd, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_color(lbl_sd, lv_color_black(), 0);

    // Ícone Bateria (Usando Símbolo)
    lv_obj_t * lbl_bat = lv_label_create(icons_cont);
    lv_label_set_text(lbl_bat, LV_SYMBOL_BATTERY_3);
    lv_obj_set_style_text_color(lbl_bat, lv_color_black(), 0);

    // Ícone Wi-Fi (Usando Símbolo)
    lv_obj_t * lbl_wifi = lv_label_create(icons_cont);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_black(), 0);
}

static void create_main_content(lv_obj_t * parent)
{
    // Container Principal
    lv_obj_t * content = lv_obj_create(parent);
    lv_obj_set_size(content, 240, 210);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 15);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);

    // Placeholder do Polvo (Texto Grande)
    lv_obj_t * logo = lv_label_create(content);
    lv_label_set_text(logo, "HIGH\nBOY");
    lv_obj_set_style_text_align(logo, LV_TEXT_ALIGN_CENTER, 0);
    
    // Usa uma fonte grande (verifique se montserrat_24 ou 28 está habilitada no lv_conf.h)
    // Se não tiver, use lv_font_montserrat_14 mesmo.
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_14, 0); 
    
    lv_obj_set_style_text_color(logo, THEME_COLOR, 0);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, 0);
    
    // Subtítulo
    lv_obj_t * sub = lv_label_create(content);
    lv_label_set_text(sub, "Cyber Security Tool");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 30);
}

// --- FUNÇÃO PÚBLICA ---

void ui_home_open(void)
{
    // Limpeza se a tela já existir
    if (screen_home) {
        lv_obj_del(screen_home);
        screen_home = NULL;
    }

    // Cria tela base
    screen_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_home, BG_COLOR, 0);
    lv_obj_remove_flag(screen_home, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(screen_home);
    create_main_content(screen_home);

    // Configura Botões Físicos
    lv_obj_add_event_cb(screen_home, home_event_cb, LV_EVENT_KEY, NULL);
    
    if (main_group) {
        lv_group_add_obj(main_group, screen_home);
        lv_group_focus_obj(screen_home);
    }

    lv_screen_load(screen_home);

    // Timer do Relógio
    if(timer_clock) lv_timer_del(timer_clock);
    timer_clock = lv_timer_create(update_clock_cb, 1000, NULL);
    update_clock_cb(NULL);
}
