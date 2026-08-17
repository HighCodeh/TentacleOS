extern "C" {
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
}

#include "hle/hle_display.h"
#include <lvgl.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

static const char *TAG = "LV_PORT_DISP";

#ifndef HLE_ASSETS_DIR
#define HLE_ASSETS_DIR "/tmp/hle_assets"
#endif

#define LCD_H_RES 240
#define LCD_V_RES 320

static lv_display_t *s_disp = nullptr;
static bool s_fs_registered = false;

static std::string hle_fs_translate_path(const char *path) {
    if (path == nullptr) {
        return {};
    }

    std::string relative(path);
    if (relative.rfind("assets/", 0) == 0) {
        relative.erase(0, sizeof("assets/") - 1);
    }

    return std::string(HLE_ASSETS_DIR) + "/" + relative;
}

static void *fs_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
    (void)drv;
    const char *fopen_mode = (mode == LV_FS_MODE_WR) ? "wb" : ((mode & LV_FS_MODE_WR) ? "w+b" : "rb");
    const std::string translated = hle_fs_translate_path(path);
    return fopen(translated.c_str(), fopen_mode);
}

static lv_fs_res_t fs_close_cb(lv_fs_drv_t *drv, void *file_p) {
    (void)drv;
    return fclose(static_cast<FILE *>(file_p)) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_read_cb(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br) {
    (void)drv;
    const size_t read = fread(buf, 1, btr, static_cast<FILE *>(file_p));
    if (br != nullptr) {
        *br = static_cast<uint32_t>(read);
    }
    return (read < btr && ferror(static_cast<FILE *>(file_p))) ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek_cb(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence) {
    (void)drv;
    int origin = SEEK_SET;
    if (whence == LV_FS_SEEK_CUR) origin = SEEK_CUR;
    else if (whence == LV_FS_SEEK_END) origin = SEEK_END;
    return fseek(static_cast<FILE *>(file_p), static_cast<long>(pos), origin) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_tell_cb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p) {
    (void)drv;
    const long pos = ftell(static_cast<FILE *>(file_p));
    if (pos < 0 || pos_p == nullptr) {
        return LV_FS_RES_UNKNOWN;
    }
    *pos_p = static_cast<uint32_t>(pos);
    return LV_FS_RES_OK;
}

static void hle_lv_fs_register(void) {
    if (s_fs_registered) {
        return;
    }

    static lv_fs_drv_t s_fs_drv;
    lv_fs_drv_init(&s_fs_drv);
    s_fs_drv.letter = 'A';
    s_fs_drv.open_cb = fs_open_cb;
    s_fs_drv.close_cb = fs_close_cb;
    s_fs_drv.read_cb = fs_read_cb;
    s_fs_drv.seek_cb = fs_seek_cb;
    s_fs_drv.tell_cb = fs_tell_cb;
    lv_fs_drv_register(&s_fs_drv);
    s_fs_registered = true;
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const uint32_t stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), lv_display_get_color_format(disp));
    hle::Display::instance().draw_bitmap_strided(
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map, stride);

    lv_display_flush_ready(disp);
}

extern "C" {

void lv_port_disp_init(void);
void hle_lv_port_disp_init(void);

void lv_port_disp_init(void) {
    hle_lv_fs_register();
    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, disp_flush_cb);

    const uint32_t stride = lv_draw_buf_width_to_stride(LCD_H_RES, LV_COLOR_FORMAT_RGB565);
    size_t buf_size = stride * (LCD_V_RES / 2);
    auto *buf1 = static_cast<lv_color_t *>(malloc(buf_size));
    auto *buf2 = static_cast<lv_color_t *>(malloc(buf_size));
    lv_display_set_buffers(s_disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "HLE display initialized (%dx%d)", LCD_H_RES, LCD_V_RES);
}

void hle_lv_port_disp_init(void) { lv_port_disp_init(); }

} // extern "C"
