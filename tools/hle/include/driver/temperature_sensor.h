#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct temperature_sensor_obj_t *temperature_sensor_handle_t;

typedef struct {
    int range_min;
    int range_max;
} temperature_sensor_config_t;

#define TEMPERATURE_SENSOR_CONFIG_DEFAULT(min, max) {.range_min = (min), .range_max = (max)}

static inline esp_err_t temperature_sensor_install(const temperature_sensor_config_t *cfg,
                                                   temperature_sensor_handle_t *out_handle) {
    (void)cfg;
    if (out_handle) *out_handle = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t temperature_sensor_uninstall(temperature_sensor_handle_t handle) {
    (void)handle;
    return ESP_OK;
}
static inline esp_err_t temperature_sensor_enable(temperature_sensor_handle_t handle) {
    (void)handle;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t handle,
                                                       float *out_celsius) {
    (void)handle;
    (void)out_celsius;
    return ESP_ERR_NOT_SUPPORTED;
}

#ifdef __cplusplus
}
#endif
