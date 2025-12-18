#include "header_ui.h"

#define HEADER_HEIGHT 24

#define COLOR_PURPLE_MAIN   0x1A053D
#define HEADER_BORDER_COLOR 0x5E12A0

void header_ui_create(lv_obj_t * parent)
{
    lv_obj_t * header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* Background */
    lv_obj_set_style_bg_color(
        header,
        lv_color_hex(COLOR_PURPLE_MAIN),
        0
    );

    /* Bottom border */
    lv_obj_set_style_border_side(
        header,
        LV_BORDER_SIDE_BOTTOM,
        0
    );
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_border_color(
        header,
        lv_color_hex(HEADER_BORDER_COLOR),
        0
    );

    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 2, 0);

    /* MENU (esquerda) - imagem */
    lv_obj_t * img_menu = lv_img_create(header);
    lv_img_set_src(img_menu, "A:/label/HOME_MENU.png");
    lv_obj_align(img_menu, LV_ALIGN_LEFT_MID, 8, 0);

    /* Container dos ícones (direita) */
    lv_obj_t * icon_cont = lv_obj_create(header);
    lv_obj_set_size(icon_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_flex_flow(icon_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(icon_cont, 0, 0);
    lv_obj_set_style_pad_column(icon_cont, 6, 0);
    lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont, 0, 0);

    const char * icons[] = {
        "A:/icons/BATTERY_ICON.png",
        "A:/icons/BLE_ICON.png",
        "A:/icons/HEADPHONE_ICON.png",
        "A:/icons/STORAGE_ICON.png"
    };

    for(int i = 0; i < 4; i++) {
        lv_obj_t * img = lv_img_create(icon_cont);
        lv_img_set_src(img, icons[i]);
    }
}
