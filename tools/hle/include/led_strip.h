#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/rmt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int strip_gpio_num;
    int led_count;
    int max_leds;
} led_strip_config_t;

typedef struct {
    int clk_src;
    int resolution_hz;
    struct {
        bool with_dma;
    } flags;
} led_strip_rmt_config_t;

typedef void *led_strip_handle_t;

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *strip_cfg,
                                    const led_strip_rmt_config_t *rmt_cfg,
                                    led_strip_handle_t *handle);
esp_err_t led_strip_set_pixel(led_strip_handle_t handle, uint32_t index, uint32_t r, uint32_t g, uint32_t b);
esp_err_t led_strip_refresh(led_strip_handle_t handle);
esp_err_t led_strip_clear(led_strip_handle_t handle);
esp_err_t led_strip_del(led_strip_handle_t handle);

#ifdef __cplusplus
}
#endif
