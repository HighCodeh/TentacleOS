#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jpeg_decoder_t *jpeg_decoder_handle_t;

typedef enum { JPEG_DECODE_OUT_FORMAT_RGB888, JPEG_DECODE_OUT_FORMAT_RGB565 } jpeg_dec_output_format_t;
typedef enum { JPEG_DEC_RGB_ELEMENT_ORDER_RGB, JPEG_DEC_RGB_ELEMENT_ORDER_BGR } jpeg_dec_rgb_element_order_t;
typedef enum { JPEG_YUV_RGB_CONV_STD_BT601, JPEG_YUV_RGB_CONV_STD_BT709 } jpeg_yuv_rgb_conv_std_t;
typedef enum { JPEG_DEC_ALLOC_INPUT_BUFFER, JPEG_DEC_ALLOC_OUTPUT_BUFFER } jpeg_dec_buffer_alloc_direction_t;

typedef struct {
    int intr_priority;
    int timeout_ms;
} jpeg_decode_engine_cfg_t;

typedef struct {
    jpeg_dec_output_format_t output_format;
    jpeg_dec_rgb_element_order_t rgb_order;
    jpeg_yuv_rgb_conv_std_t conv_std;
} jpeg_decode_cfg_t;

typedef struct {
    jpeg_dec_buffer_alloc_direction_t buffer_direction;
} jpeg_decode_memory_alloc_cfg_t;

typedef struct {
    uint32_t width;
    uint32_t height;
} jpeg_decode_picture_info_t;

// No JPEG codec on the host: every entry point fails and callers skip the image.
static inline esp_err_t jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *cfg,
                                                jpeg_decoder_handle_t *out_handle) {
    (void)cfg;
    if (out_handle) *out_handle = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t jpeg_del_decoder_engine(jpeg_decoder_handle_t handle) {
    (void)handle;
    return ESP_OK;
}
static inline esp_err_t jpeg_decoder_get_info(const uint8_t *in_buf, uint32_t inbuf_len,
                                              jpeg_decode_picture_info_t *out_info) {
    (void)in_buf;
    (void)inbuf_len;
    (void)out_info;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t jpeg_decoder_process(jpeg_decoder_handle_t handle,
                                             const jpeg_decode_cfg_t *cfg, const uint8_t *in_buf,
                                             uint32_t in_len, uint8_t *out_buf, uint32_t out_size,
                                             uint32_t *out_len) {
    (void)handle;
    (void)cfg;
    (void)in_buf;
    (void)in_len;
    (void)out_buf;
    (void)out_size;
    if (out_len) *out_len = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline void *jpeg_alloc_decoder_mem(size_t size, const jpeg_decode_memory_alloc_cfg_t *cfg,
                                           size_t *out_size) {
    (void)size;
    (void)cfg;
    if (out_size) *out_size = 0;
    return NULL;
}

#ifdef __cplusplus
}
#endif
