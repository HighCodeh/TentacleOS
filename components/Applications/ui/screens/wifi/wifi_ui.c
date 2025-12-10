#include "wifi_ui.h"
#include "misc/lv_palette.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "esp_log.h"

static const char *TAG = "UI_WIFI";

// --- CONFIGURAÇÕES VISUAIS (Mesmas do Menu Principal) ---
#define THEME_COLOR lv_palette_main(LV_PALETTE_DEEP_PURPLE)
#define BG_COLOR    lv_color_black()

// --- ESTRUTURA DE DADOS ---
// Adaptada do seu struct para facilitar o loop de criação
typedef struct {
    const char * label;
    const char * symbol; // Substituindo seu 'icon' por Symbol por enquanto
    int screen_id_target; // Qual tela abrir (ou ID da ação)
} wifi_menu_item_t;

// Enum para identificar as ações internamente
typedef enum {
    WIFI_OP_SCAN = 0,
    WIFI_OP_ANALYZER,
    WIFI_OP_TRAFFIC,
    WIFI_OP_ATTACK,
    WIFI_OP_PORT_SCAN,
    WIFI_OP_EVIL_TWIN
} wifi_op_t;

// Sua lista de opções mapeada para o LVGL
static const wifi_menu_item_t wifi_items[] = {
    { "Scan Redes",       LV_SYMBOL_WIFI,      WIFI_OP_SCAN },
    { "Analisar Redes",   LV_SYMBOL_LIST,      WIFI_OP_ANALYZER },
    { "Analisar Trafego", LV_SYMBOL_SHUFFLE,   WIFI_OP_TRAFFIC }, // Shuffle parece gráfico
    { "Port Scan",        LV_SYMBOL_WIFI,      WIFI_OP_PORT_SCAN},
    { "Atacar Alvo",      LV_SYMBOL_WARNING,   WIFI_OP_ATTACK },  // Warning para perigo
    { "Evil Twin",        LV_SYMBOL_REFRESH,   WIFI_OP_EVIL_TWIN }, // Refresh para 'clone'
};

static lv_obj_t * screen_wifi = NULL;

// --- CALLBACK DE NAVEGAÇÃO ---
static void wifi_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn_clicked = lv_event_get_target(e);

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);

        // VOLTAR -> Vai para o MENU PRINCIPAL
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            ESP_LOGI(TAG, "Voltando para Menu Principal");
            ui_switch_screen(SCREEN_MENU);
        }
        
        // ENTRAR -> Executa a ação
        else if (key == LV_KEY_RIGHT || key == LV_KEY_ENTER) {
            
            intptr_t op_id = (intptr_t)lv_obj_get_user_data(btn_clicked);
            ESP_LOGI(TAG, "Opção WiFi Selecionada: %d", (int)op_id);

            // AQUI VOCÊ CHAMA SUAS FUNÇÕES REAIS OU ABRE AS TELAS
            switch (op_id) {
                case WIFI_OP_SCAN:
                    // wifi_action_scan(); // Sua função backend
                    ui_switch_screen(SCREEN_WIFI_SCAN); // Abre a tela de scan
                    break;

                case WIFI_OP_ANALYZER:
                    // wifi_action_analyze();
                    // ui_switch_screen(SCREEN_WIFI_ANALYZER);
                    break;

                case WIFI_OP_TRAFFIC:
                    // show_traffic_analyzer();
                    break;

                case WIFI_OP_ATTACK:
                    // wifi_action_attack();
                    // ui_switch_screen(SCREEN_WIFI_ATTACK);
                    break;

                case WIFI_OP_EVIL_TWIN:
                     // wifi_action_evil_twin();
                     break;

                default:
                    break;
            }
        }
    }
}

// --- HELPER PARA CRIAR BOTÃO (Reutilizado do Menu Principal) ---
static void create_wifi_item(lv_obj_t * parent, const char * text, const char * symbol, int id)
{
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, 45);
    
    // Estilos
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x151515), 0); // Um pouco mais escuro que o menu principal
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    
    // Estilo Focado
    lv_obj_set_style_border_color(btn, THEME_COLOR, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x303030), LV_STATE_FOCUS_KEY);

    // Layout
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn, 10, 0);
    lv_obj_set_style_pad_gap(btn, 15, 0);

    // Ícone
    lv_obj_t * lbl_icon = lv_label_create(btn);
    lv_label_set_text(lbl_icon, symbol);
    lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_icon, lv_color_white(), 0);

    // Texto
    lv_obj_t * lbl_text = lv_label_create(btn);
    lv_label_set_text(lbl_text, text);
    lv_obj_set_style_text_font(lbl_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_text, THEME_COLOR, 0);

    // Dados e Eventos
    lv_obj_set_user_data(btn, (void*)(intptr_t)id);
    lv_obj_add_event_cb(btn, wifi_event_cb, LV_EVENT_KEY, NULL);

    // Adiciona ao grupo para navegação funcionar
    if (main_group) {
        lv_group_add_obj(main_group, btn);
    }
}

// --- FUNÇÃO PÚBLICA ---
void ui_wifi_menu(void)
{
    // 1. Limpa o Grupo
    if (main_group) {
        lv_group_remove_all_objs(main_group);
    }

    if (screen_wifi) {
        lv_obj_del(screen_wifi);
        screen_wifi = NULL;
    }

    // 2. Cria Tela
    screen_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_wifi, BG_COLOR, 0);
    if(main_group) lv_group_remove_obj(screen_wifi); // Remove tela do foco

    // 3. Título do Menu (Header Simples)
    lv_obj_t * title = lv_label_create(screen_wifi);
    lv_label_set_text(title, "Wi-Fi Attacks");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // 4. Container da Lista
    lv_obj_t * list_cont = lv_obj_create(screen_wifi);
    lv_obj_set_size(list_cont, LV_PCT(100), 200); // Altura fixa ou resto da tela
    lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 35); // Abaixo do título
    lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_pad_all(list_cont, 10, 0);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(list_cont, 8, 0);
    
    if(main_group) lv_group_remove_obj(list_cont);

    // 5. Gera os botões via Loop (Baseado no seu array)
    int num_items = sizeof(wifi_items) / sizeof(wifi_items[0]);
    
    for(int i = 0; i < num_items; i++) {
        create_wifi_item(list_cont, wifi_items[i].label, wifi_items[i].symbol, wifi_items[i].screen_id_target);
    }

    // 6. Foca no primeiro item
    if (main_group && lv_obj_get_child_cnt(list_cont) > 0) {
        lv_obj_t * first_btn = lv_obj_get_child(list_cont, 0);
        lv_group_focus_obj(first_btn);
    }

    lv_screen_load(screen_wifi);
}
