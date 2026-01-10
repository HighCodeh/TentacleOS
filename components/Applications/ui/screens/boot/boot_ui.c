#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "UI_BOOT";
#define BOOT_DOT_COUNT 5

static void anim_set_opa_cb(void * var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_set_scale_cb(void * var, int32_t v) {
    lv_obj_set_style_transform_scale((lv_obj_t *)var, (int16_t)v, 0);
}

static void anim_set_translate_y_cb(void * var, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, (int32_t)v, 0);
}

static lv_image_dsc_t* boot_load_bin_to_psram(const char * path, int32_t w, int32_t h) {
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    int header_size = 12; 
    long pixel_data_size = st.st_size - header_size;
    
    lv_image_dsc_t * dsc = (lv_image_dsc_t *)heap_caps_malloc(sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t * pixel_data = (uint8_t *)heap_caps_malloc(pixel_data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!dsc || !pixel_data) {
        if (dsc) free(dsc);
        if (pixel_data) free(pixel_data);
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
    return dsc;
}

void ui_boot_show(void) {
    lv_obj_t * boot_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_screen, lv_color_hex(0x05050A), 0);
    lv_obj_set_scrollbar_mode(boot_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_image_dsc_t * logo_dsc = boot_load_bin_to_psram("/assets/img/highcode_boot_new.bin", 240, 117);
    if(logo_dsc) {
        lv_obj_t * logo = lv_image_create(boot_screen);
        lv_image_set_src(logo, logo_dsc);
        lv_obj_align(logo, LV_ALIGN_CENTER, 0, -40);
        
        lv_anim_t a_logo;
        lv_anim_init(&a_logo);
        lv_anim_set_var(&a_logo, logo);
        lv_anim_set_values(&a_logo, 0, 255);
        lv_anim_set_duration(&a_logo, 800);
        lv_anim_set_exec_cb(&a_logo, anim_set_opa_cb);
        lv_anim_start(&a_logo);
    }

    lv_obj_t * subtitle = lv_label_create(boot_screen);
    lv_label_set_text(subtitle, "INITIALIZING KERNEL...");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x404050), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(subtitle, 1, 0);
    lv_obj_align(subtitle, LV_ALIGN_BOTTOM_MID, 0, -75);

    lv_obj_t * dots_cont = lv_obj_create(boot_screen);
    lv_obj_set_size(dots_cont, 180, 50);
    lv_obj_align(dots_cont, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_opa(dots_cont, 0, 0);
    lv_obj_set_style_border_width(dots_cont, 0, 0);
    lv_obj_set_scrollbar_mode(dots_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(dots_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_flex_flow(dots_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(dots_cont, 12, 0);

    for(int i = 0; i < BOOT_DOT_COUNT; i++) {
        lv_obj_t * dot = lv_obj_create(dots_cont);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x6b4ce6), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        
        lv_obj_set_scrollbar_mode(dot, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_style_shadow_width(dot, 15, 0);
        lv_obj_set_style_shadow_color(dot, lv_color_hex(0x6b4ce6), 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_40, 0);

        uint32_t duration = 700; 
        uint32_t delay = i * 150; 

        lv_anim_t a_opa;
        lv_anim_init(&a_opa);
        lv_anim_set_var(&a_opa, dot);
        lv_anim_set_values(&a_opa, LV_OPA_20, LV_OPA_COVER);
        lv_anim_set_duration(&a_opa, duration);
        lv_anim_set_playback_duration(&a_opa, duration);
        lv_anim_set_repeat_count(&a_opa, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_delay(&a_opa, delay);
        lv_anim_set_path_cb(&a_opa, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a_opa, anim_set_opa_cb);
        lv_anim_start(&a_opa);

        lv_anim_t a_scale;
        lv_anim_init(&a_scale);
        lv_anim_set_var(&a_scale, dot);
        lv_anim_set_values(&a_scale, 256, 380); 
        lv_anim_set_duration(&a_scale, duration);
        lv_anim_set_playback_duration(&a_scale, duration);
        lv_anim_set_repeat_count(&a_scale, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_delay(&a_scale, delay);
        lv_anim_set_path_cb(&a_scale, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a_scale, anim_set_scale_cb);
        lv_anim_start(&a_scale);

        lv_anim_t a_y;
        lv_anim_init(&a_y);
        lv_anim_set_var(&a_y, dot);
        lv_anim_set_values(&a_y, 0, -10);
        lv_anim_set_duration(&a_y, duration);
        lv_anim_set_playback_duration(&a_y, duration);
        lv_anim_set_repeat_count(&a_y, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_delay(&a_y, delay);
        lv_anim_set_path_cb(&a_y, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a_y, anim_set_translate_y_cb);
        lv_anim_start(&a_y);
    }

    lv_screen_load(boot_screen);
}