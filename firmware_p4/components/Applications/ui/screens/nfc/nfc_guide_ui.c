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

#include "nfc_guide_ui.h"

#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "ui_chrome.h"
#include "ui_metrics.h"

#define GUIDE_PATH         "/assets/animations/nfc_scan.gif"
#define GUIDE_LIMIT        (512U * 1024U)
#define GUIDE_STACK        4096
#define GUIDE_WIDTH        160
#define GUIDE_HEIGHT       148
#define GUIDE_STATUS_SPACE 38
#define GUIDE_MAX_SCALE    256

static const char *TAG = "NFC_GUIDE_UI";
static atomic_int s_load_state;
static lv_image_dsc_t s_descriptor;
static lv_obj_t *s_image;

static void load_task(void *context) {
  (void)context;
  uint8_t *data = NULL;
  FILE *file = fopen(GUIDE_PATH, "rb");
  long bytes = 0;
  bool ok = file != NULL && fseek(file, 0, SEEK_END) == 0 && (bytes = ftell(file)) >= 13 &&
            bytes <= GUIDE_LIMIT && fseek(file, 0, SEEK_SET) == 0;
  if (ok) {
    data = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ok = data != NULL && fread(data, 1, bytes, file) == (size_t)bytes;
  }
  if (file != NULL && fclose(file) != 0)
    ok = false;
  if (ok)
    ok = (memcmp(data, "GIF89a", 6) == 0 || memcmp(data, "GIF87a", 6) == 0) &&
         (data[6] | data[7] << 8) == GUIDE_WIDTH && (data[8] | data[9] << 8) == GUIDE_HEIGHT;
  if (ok) {
    s_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_descriptor.header.cf = LV_COLOR_FORMAT_RAW;
    s_descriptor.data = data;
    s_descriptor.data_size = bytes;
    ESP_LOGI(TAG, "NFC guidance loaded: %ld bytes", bytes);
  } else {
    heap_caps_free(data);
    ESP_LOGW(TAG, "Guidance unavailable; using NFC field illustration");
  }
  atomic_store(&s_load_state, ok ? 2 : -1);
  vTaskDeleteWithCaps(NULL);
}

void nfc_guide_prepare(void) {
  int expected = 0;
  if (!atomic_compare_exchange_strong(&s_load_state, &expected, 1))
    return;
  // Flash reads temporarily disable the cache; the loader stack must stay internal.
  if (xTaskCreatePinnedToCoreWithCaps(load_task,
                                      "nfc_art",
                                      GUIDE_STACK,
                                      NULL,
                                      SYS_PRIO_SERVICE_LO,
                                      NULL,
                                      SYS_CORE_UI,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGE(TAG, "Cannot start guidance loader");
    atomic_store(&s_load_state, -1);
  }
}

static void image_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_image)
    s_image = NULL;
}

bool nfc_guide_show(lv_obj_t *parent) {
  if (s_image != NULL)
    return true;
  if (atomic_load(&s_load_state) != 2)
    return false;
  s_image = lv_gif_create(parent);
  lv_gif_set_color_format(s_image, LV_COLOR_FORMAT_ARGB8888);
  lv_gif_set_src(s_image, &s_descriptor);
  if (!lv_gif_is_loaded(s_image)) {
    nfc_guide_close();
    atomic_store(&s_load_state, -1);
    return false;
  }
  lv_obj_add_event_cb(s_image, image_deleted, LV_EVENT_DELETE, NULL);
  int height = ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H - GUIDE_STATUS_SPACE;
  int scale = LV_MIN(GUIDE_MAX_SCALE, height * 256 / GUIDE_HEIGHT);
  lv_image_set_scale(s_image, scale);
  lv_obj_align(s_image,
               LV_ALIGN_TOP_MID,
               0,
               UI_CHROME_HEADER_H + GUIDE_STATUS_SPACE + (height - GUIDE_HEIGHT) / 2);
  return true;
}

void nfc_guide_close(void) {
  if (s_image != NULL)
    lv_obj_delete(s_image);
  s_image = NULL;
}
