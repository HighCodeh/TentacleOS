// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "media_thumb.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "driver/jpeg_decode.h"

static const char *TAG = "MEDIA_THUMB";

#define THUMB_MAX_DIM             2048
#define JPEG_DECODE_TIMEOUT_MS    70

bool media_thumb_decode_jpeg(const uint8_t *jpeg, size_t len, media_thumb_t *out) {
  if (jpeg == NULL || len < 4 || out == NULL)
    return false;
  memset(out, 0, sizeof(*out));

  if (jpeg[0] != 0xFF || jpeg[1] != 0xD8)
    return false;

  jpeg_decoder_handle_t jpgd = NULL;
  jpeg_decode_engine_cfg_t engine_cfg = {
      .timeout_ms = JPEG_DECODE_TIMEOUT_MS,
  };
  if (jpeg_new_decoder_engine(&engine_cfg, &jpgd) != ESP_OK) {
    ESP_LOGW(TAG, "jpeg_new_decoder_engine failed");
    return false;
  }

  bool ok = false;
  uint8_t *in_buf = NULL;
  uint8_t *out_buf = NULL;
  jpeg_decode_picture_info_t info = {0};
  size_t out_buf_size = 0;
  size_t in_buf_size = 0;
  uint32_t decoded = 0;
  jpeg_decode_cfg_t decode_cfg = {
      .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
      .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
      .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
  };
  jpeg_decode_memory_alloc_cfg_t out_mem_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
  };
  jpeg_decode_memory_alloc_cfg_t in_mem_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
  };

  in_buf = (uint8_t *)jpeg_alloc_decoder_mem(len, &in_mem_cfg, &in_buf_size);
  if (in_buf == NULL) {
    ESP_LOGW(TAG, "input mem alloc failed (%u bytes)", (unsigned)len);
    goto done;
  }
  memcpy(in_buf, jpeg, len);

  if (jpeg_decoder_get_info(in_buf, (uint32_t)len, &info) != ESP_OK) {
    ESP_LOGW(TAG, "get_info failed (progressive JPEG?)");
    goto done;
  }
  ESP_LOGI(TAG, "cover %ux%u, %u bytes", (unsigned)info.width, (unsigned)info.height, (unsigned)len);
  if (info.width == 0 || info.height == 0 || info.width > THUMB_MAX_DIM ||
      info.height > THUMB_MAX_DIM) {
    ESP_LOGW(TAG, "bad cover dims %ux%u", (unsigned)info.width, (unsigned)info.height);
    goto done;
  }

  out_buf = (uint8_t *)jpeg_alloc_decoder_mem(
      (size_t)info.width * info.height * 2, &out_mem_cfg, &out_buf_size);
  if (out_buf == NULL) {
    ESP_LOGW(TAG, "decoder mem alloc failed (%u bytes)", (unsigned)(info.width * info.height * 2));
    goto done;
  }

  if (jpeg_decoder_process(jpgd, &decode_cfg, in_buf, (uint32_t)len, out_buf, out_buf_size,
                           &decoded) != ESP_OK) {
    ESP_LOGW(TAG, "jpeg_decoder_process failed");
    goto done;
  }

  out->buf = out_buf;
  out->w = info.width;
  out->h = info.height;
  out->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  out->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  out->dsc.header.w = info.width;
  out->dsc.header.h = info.height;
  out->dsc.header.stride = (uint32_t)info.width * 2;
  out->dsc.data = out_buf;
  out->dsc.data_size = (uint32_t)info.width * info.height * 2;
  out_buf = NULL;
  ok = true;

done:
  if (in_buf != NULL)
    free(in_buf);
  if (out_buf != NULL)
    free(out_buf);
  if (jpgd != NULL)
    jpeg_del_decoder_engine(jpgd);
  return ok;
}

void media_thumb_free(media_thumb_t *t) {
  if (t == NULL)
    return;
  if (t->buf != NULL)
    free(t->buf);
  memset(t, 0, sizeof(*t));
}
