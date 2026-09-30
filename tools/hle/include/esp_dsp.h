#pragma once

#include "esp_err.h"

#ifndef CONFIG_DSP_MAX_FFT_SIZE
#define CONFIG_DSP_MAX_FFT_SIZE 4096
#endif

// esp-dsp is not built for the host. Init reports that, so the spectrum screen
// skips its FFT path and draws an empty spectrum.
static inline esp_err_t dsps_fft2r_init_fc32(float *fft_table_buff, int table_size) {
    (void)fft_table_buff;
    (void)table_size;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t dsps_fft2r_fc32(float *data, int N) {
    (void)data;
    (void)N;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t dsps_bit_rev_fc32(float *data, int N) {
    (void)data;
    (void)N;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t dsps_cplx2reC_fc32(float *data, int N) {
    (void)data;
    (void)N;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline void dsps_wind_hann_f32(float *window, int len) {
    for (int i = 0; i < len; i++) window[i] = 1.0f;
}
