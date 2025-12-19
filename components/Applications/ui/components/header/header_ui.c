#include "header_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>

#define HEADER_HEIGHT 24
#define COLOR_PURPLE_MAIN    0x1A053D
#define HEADER_BORDER_COLOR  0x5E12A0

static const char *TAG = "HEADER_UI";

/* Função para carregar um .bin em ARGB8888 da PSRAM */
static lv_image_dsc_t* load_bin_to_psram(const char * path, int32_t w, int32_t h) {
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    int header_size = 12; // Header padrão LVGL 9
    long pixel_data_size = st.st_size - header_size;

    lv_image_dsc_t * dsc = (lv_image_dsc_t *)heap_caps_malloc(sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM);
    uint8_t * pixel_data = (uint8_t *)heap_caps_malloc(pixel_data_size, MALLOC_CAP_SPIRAM);

    if (!dsc || !pixel_data) {
        ESP_LOGE(TAG, "Error: Insufficient PSRAM Memory");
        if (dsc) free(dsc);
        fclose(f);
        return NULL;
    }

    fseek(f, header_size, SEEK_SET);
    fread(pixel_data, 1, pixel_data_size, f);
    fclose(f);

    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.stride = w * 4; 
    dsc->data_size = pixel_data_size;
    dsc->data = pixel_data;

    ESP_LOGI(TAG, "Loaded: %s (%dx%d)", path, w, h);
    return dsc;
}

void header_ui_create(lv_obj_t * parent)
{
    // Criar o container do Header
    lv_obj_t * header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Estilização do Header
    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_PURPLE_MAIN), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(HEADER_BORDER_COLOR), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    /* --- MENU (Esquerda) --- */
    static lv_image_dsc_t * home_menu_dsc = NULL;
    if (home_menu_dsc == NULL) {
        // Carregando o binário do menu (ajuste w=60, h=16 conforme seu arquivo real)
        home_menu_dsc = load_bin_to_psram("/assets/label/HOME_MENU.bin", 74, 22);
    }

    if (home_menu_dsc) {
        lv_obj_t * img_menu = lv_image_create(header);
        lv_image_set_src(img_menu, home_menu_dsc);
        lv_obj_align(img_menu, LV_ALIGN_LEFT_MID, 10, 0); // 10px da borda esquerda
    }

    /* --- CONTAINER DE ÍCONES (Direita) --- */
    lv_obj_t * icon_cont = lv_obj_create(header);
    lv_obj_set_size(icon_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont, LV_ALIGN_RIGHT_MID, -8, 0); // 8px da borda direita
    
    // Layout Flex para alinhar os ícones horizontalmente
    lv_obj_set_flex_flow(icon_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Remover fundo e bordas do container de ícones
    lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont, 0, 0);
    lv_obj_set_style_pad_all(icon_cont, 0, 0);
    lv_obj_set_style_pad_column(icon_cont, 8, 0); // Espaçamento de 8px entre ícones

    /* Lista de ícones binários */
    const char * bin_icons[] = {
        "/assets/icons/STORAGE_ICON.bin",
        "/assets/icons/HEADPHONE_ICON.bin",
        "/assets/icons/BLE_ICON.bin",
        "/assets/icons/BATTERY_ICON.bin"
    };

    static lv_image_dsc_t * loaded_icons[4] = {NULL};

    for (int i = 0; i < 4; i++) {
        if (loaded_icons[i] == NULL) {
            loaded_icons[i] = load_bin_to_psram(bin_icons[i], 20, 20);
        }

        if (loaded_icons[i]) {
            lv_obj_t * img = lv_image_create(icon_cont);
            lv_image_set_src(img, loaded_icons[i]);
        }
    }
}