 #include "menu_ui.h"
#include "core/lv_group.h"
#include "misc/lv_palette.h"
#include "ui_manager.h"
#include "lv_port_indev.h" // Acesso ao main_group
#include "esp_log.h" 
static const char *TAG = "UI_MENU";

// --- CONFIGURAÇÕES VISUAIS ---
#define THEME_COLOR lv_palette_main(LV_PALETTE_PURPLE) // Voltei pro Laranja HighBoy, mude se preferir
#define BG_COLOR    lv_color_black()

typedef enum {
    MENU_ID_WIFI = 0,
    MENU_ID_BLUETOOTH= 1,
    MENU_ID_INFRARED,
    MENU_ID_RADIO_RF,
    MENU_ID_BAD_USB,
    MENU_ID_MICRO_SD,
    MENU_ID_NFC,
    MENU_ID_RFID
} menu_item_id_t;

static lv_obj_t * screen_menu = NULL;

// --- CALLBACK DE NAVEGAÇÃO ---
static void menu_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn_clicked = lv_event_get_target(e);

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);

        // VOLTAR -> Vai para HOME (Agora com ESC)
        if (key == LV_KEY_ESC) {
            ESP_LOGI(TAG, "Botão ESC -> Voltar para Home");
            ui_switch_screen(SCREEN_HOME);
        }
        
        // ENTRAR -> Vai para a função selecionada
        else if (key == LV_KEY_ENTER) {
            
            intptr_t id = (intptr_t)lv_obj_get_user_data(btn_clicked);
            ESP_LOGI(TAG, "Entrando no ID: %d", (int)id);

            switch (id) {
                case MENU_ID_WIFI:
                    ui_switch_screen(SCREEN_WIFI_MENU);
                    break;
                case MENU_ID_BLUETOOTH:
                    ESP_LOGW(TAG, "Tela Bluetooth não implementada");
                    break;
                // ... adicione os outros cases aqui ...
                default:
                    break;
            }
        }
    }
}

// --- HELPER PARA CRIAR BOTÕES DE LISTA ---
static void create_list_item(lv_obj_t * parent, const char * text, const char * symbol, menu_item_id_t id)
{
    // Cria o botão
    lv_obj_t * btn = lv_btn_create(parent);
    
    // --- ESTILO DE LISTA (Largura total, altura fixa) ---
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, 45); // Altura confortável
    
    // Estilo Base
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x202020), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x404040), 0);
    lv_obj_set_style_radius(btn, 10, 0); // Borda arredondada suave
    
    // Estilo quando Focado
    lv_obj_set_style_border_color(btn, THEME_COLOR, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x404040), LV_STATE_FOCUS_KEY);

    // Layout Horizontal: [Ícone] [Espaço] [Texto]
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn, 10, 0); // Padding interno
    lv_obj_set_style_pad_gap(btn, 15, 0); // Espaço entre ícone e texto

    // Ícone (Esquerda)
    lv_obj_t * lbl_icon = lv_label_create(btn);
    lv_label_set_text(lbl_icon, symbol);
    lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_icon, lv_color_white(), 0);

    // Texto (Direita)
    lv_obj_t * lbl_text = lv_label_create(btn);
    lv_label_set_text(lbl_text, text);
    lv_obj_set_style_text_font(lbl_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_text, THEME_COLOR, 0);

    // Lógica e Grupo
    lv_obj_set_user_data(btn, (void*)(intptr_t)id);
    lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_KEY, NULL);

    // --- CORREÇÃO DO SELETOR SUMINDO ---
    // Adicionamos explicitamente APENAS o botão ao grupo.
    if (main_group) {
        lv_group_add_obj(main_group, btn);
    }
}

// --- FUNÇÃO PÚBLICA ---
void ui_menu_open(void)
{
    // 1. Limpa o Grupo de Navegação
    // Isso impede que containers antigos ou objetos fantasmas roubem o foco
    if (main_group) {
        lv_group_remove_all_objs(main_group);
    }

    if (screen_menu) {
        lv_obj_del(screen_menu);
        screen_menu = NULL;
    }

    // 2. Cria a Tela
    screen_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_menu, BG_COLOR, 0);
    
    // IMPORTANTE: Remove a tela do grupo (se o set_default estiver ativo)
    if(main_group) lv_group_remove_obj(screen_menu);

    // 3. Cria um Container FLEX (Coluna)
    lv_obj_t * list_cont = lv_obj_create(screen_menu);
    lv_obj_set_size(list_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_pad_all(list_cont, 10, 0); // Margem da lista
    
    // Remove Scrollbar lateral (opcional, deixa mais limpo)
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);

    // Define Layout de Coluna (Lista)
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(list_cont, 8, 0); // Espaço entre os botões
    
    // IMPORTANTE: Remove o container do grupo para não ser "focável"
    if(main_group) lv_group_remove_obj(list_cont);

    // 4. Cria os Itens da Lista
    create_list_item(list_cont, "WiFi",      LV_SYMBOL_WIFI,      MENU_ID_WIFI);
    create_list_item(list_cont, "Bluetooth", LV_SYMBOL_BLUETOOTH, MENU_ID_BLUETOOTH);
    create_list_item(list_cont, "Infrared",  LV_SYMBOL_EYE_OPEN,  MENU_ID_INFRARED);
    create_list_item(list_cont, "Sub-GHz",   LV_SYMBOL_AUDIO,     MENU_ID_RADIO_RF);
    create_list_item(list_cont, "BadUSB",    LV_SYMBOL_USB,       MENU_ID_BAD_USB);
    create_list_item(list_cont, "MicroSD",   LV_SYMBOL_SD_CARD,   MENU_ID_MICRO_SD);
    create_list_item(list_cont, "NFC",       LV_SYMBOL_LOOP,      MENU_ID_NFC);
    create_list_item(list_cont, "RFID",      LV_SYMBOL_BELL,      MENU_ID_RFID);

    // 5. Foca no primeiro item para garantir que o seletor apareça
    if (main_group && lv_obj_get_child_cnt(list_cont) > 0) {
        lv_obj_t * first_btn = lv_obj_get_child(list_cont, 0);
        lv_group_focus_obj(first_btn);
    }

    lv_screen_load(screen_menu);
}
