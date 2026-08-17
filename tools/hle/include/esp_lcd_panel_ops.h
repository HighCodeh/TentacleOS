#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_lcd_panel_io_handle_t;
typedef void *esp_lcd_panel_handle_t;

typedef struct {
    int cs_gpio_num;
    int dc_gpio_num;
    int spi_mode;
    int pclk_hz;
    int trans_queue_depth;
    int lcd_cmd_bits;
    int lcd_param_bits;
    int flags;
} esp_lcd_panel_io_spi_config_t;

typedef struct {
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    int color_space;
    int bits_per_pixel;
    int flags;
    int rgb_ele_order;
} esp_lcd_panel_dev_config_t;

#define LCD_RGB_ELEMENT_ORDER_RGB 0

typedef enum {
    ESP_LCD_COLOR_SPACE_RGB = 0,
    ESP_LCD_COLOR_SPACE_BGR,
    ESP_LCD_COLOR_SPACE_MONOCHROME,
} esp_lcd_color_space_t;

typedef struct {
    int dummy;
} esp_lcd_panel_io_event_data_t;

typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *);

typedef struct {
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
} esp_lcd_panel_io_callbacks_t;

esp_err_t esp_lcd_new_panel_io_spi(int bus, const esp_lcd_panel_io_spi_config_t *cfg, esp_lcd_panel_io_handle_t *out_io);
esp_err_t esp_lcd_new_panel_st7789(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *cfg, esp_lcd_panel_handle_t *out_panel);

// Panel ops
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t panel, bool invert);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on);
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t panel, bool mx, bool my);
esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t panel, bool swap);
esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t panel, int x, int y);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x0, int y0, int x1, int y1, const void *data);

// IO ops
esp_err_t esp_lcd_panel_io_register_event_callbacks(esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_io_callbacks_t *cbs, void *ctx);

#ifdef __cplusplus
}
#endif
