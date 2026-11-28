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

#ifndef SUBGHZ_RX_FAKES_H
#define SUBGHZ_RX_FAKES_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portTICK_PERIOD_MS 1
#define SYS_PRIO_SERVICE_HI 5
#define SYS_CORE_RADIO 0
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_8BIT 8
#define RMT_CLK_SRC_DEFAULT 0
#define GPIO_CC1101_GDO0_PIN 8
#define CC1101_SIDLE 0x36
#define CC1101_SRX 0x34
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef void *rmt_channel_handle_t;
typedef struct { uint32_t duration0 : 15; uint32_t level0 : 1;
  uint32_t duration1 : 15; uint32_t level1 : 1; } rmt_symbol_word_t;
_Static_assert(sizeof(rmt_symbol_word_t) == 4, "RMT symbols must match hardware width");
typedef struct { rmt_symbol_word_t *received_symbols; size_t num_symbols; struct { bool is_last; } flags; } rmt_rx_done_event_data_t;
typedef struct { int clk_src, resolution_hz, mem_block_symbols, gpio_num;
  struct { bool invert_in, with_dma; } flags; } rmt_rx_channel_config_t;
typedef struct { bool (*on_recv_done)(rmt_channel_handle_t, const rmt_rx_done_event_data_t *, void *); } rmt_rx_event_callbacks_t;
typedef struct { uint32_t signal_range_min_ns, signal_range_max_ns; struct { bool en_partial_rx; } flags; } rmt_receive_config_t;
#include "cc1101.h"
typedef uint32_t res_handle_t;
#define RES_HANDLE_NONE 0
#define RES_SUBGHZ 2
#define RES_LANE_MAIN 0
#define RES_OWNER_UI 2
typedef struct { int id, lane, owner_kind; void *owner_task; bool allow_preempt; } res_request_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
void xSemaphoreGive(SemaphoreHandle_t);
QueueHandle_t xQueueCreate(unsigned int, size_t);
void vQueueDelete(QueueHandle_t);
unsigned uxQueueMessagesWaiting(QueueHandle_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
BaseType_t xQueueSendFromISR(QueueHandle_t, const void *, BaseType_t *);
BaseType_t xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, int, TaskHandle_t *, int);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(TickType_t);
TickType_t xTaskGetTickCount(void);
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t, const rmt_rx_event_callbacks_t *, void *);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_disable(rmt_channel_handle_t);
esp_err_t rmt_del_channel(rmt_channel_handle_t);
esp_err_t rmt_receive(rmt_channel_handle_t, void *, size_t, const rmt_receive_config_t *);
void *heap_caps_aligned_alloc(size_t, size_t, unsigned int);
esp_err_t cc1101_strobe(uint8_t);
esp_err_t cc1101_set_frequency(uint32_t);
esp_err_t cc1101_set_preset(cc1101_preset_t, uint32_t);
esp_err_t cc1101_init(void);
esp_err_t cc1101_enter_rx_mode(void);
void *heap_caps_malloc(size_t, unsigned int);
void subghz_tx_stop(void);
esp_err_t resource_acquire(const res_request_t *, res_handle_t *);
esp_err_t resource_release(res_handle_t);
void led_signal_info(void);
void led_signal_warning(void);
void led_signal_error(void);
#include "subghz_types.h"
bool subghz_protocol_registry_decode_all(const int32_t *, size_t, subghz_data_t *);
esp_err_t subghz_storage_save_capture(const char *, const char *);
int subghz_storage_read(const char *, char *, size_t);
esp_err_t subghz_storage_save_raw(const char *, const int32_t *, size_t, uint32_t);
esp_err_t subghz_storage_save_decoded(const char *, const subghz_data_t *, uint32_t, uint32_t);
#endif
