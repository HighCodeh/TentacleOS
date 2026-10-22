#ifndef SUBGHZ_TEST_PLATFORM_H
#define SUBGHZ_TEST_PLATFORM_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "esp_err.h"
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define SYS_PRIO_BACKGROUND_LO 2
#define SYS_PRIO_SERVICE_HI 5
#define SYS_PRIO_SERVICE_LO 4
#define portMAX_DELAY UINT32_MAX
#define SYS_CORE_RADIO 0
#define portMUX_TYPE pthread_mutex_t
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) assert(pthread_mutex_lock(lock)==0)
#define portEXIT_CRITICAL(lock) assert(pthread_mutex_unlock(lock)==0)
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_8BIT 8
#define GPIO_CC1101_GDO0_PIN 8
#define RMT_CLK_SRC_DEFAULT 0
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *rmt_channel_handle_t;
typedef void *rmt_encoder_handle_t;
typedef struct { uint32_t duration0, duration1; bool level0, level1; } rmt_symbol_word_t;
typedef struct { rmt_symbol_word_t *received_symbols; size_t num_symbols; } rmt_rx_done_event_data_t;
typedef struct { int clk_src, resolution_hz, mem_block_symbols, gpio_num;
  struct { bool invert_in, with_dma; } flags; } rmt_rx_channel_config_t;
typedef struct { int clk_src, resolution_hz, mem_block_symbols, gpio_num, trans_queue_depth; } rmt_tx_channel_config_t;
typedef struct { bool (*on_recv_done)(rmt_channel_handle_t, const rmt_rx_done_event_data_t *, void *); } rmt_rx_event_callbacks_t;
typedef struct { uint32_t signal_range_min_ns, signal_range_max_ns; } rmt_receive_config_t;
typedef struct { int unused; } rmt_copy_encoder_config_t;
typedef struct { int loop_count; } rmt_transmit_config_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void);
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t, const rmt_rx_event_callbacks_t *, void *);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_disable(rmt_channel_handle_t);
esp_err_t rmt_del_channel(rmt_channel_handle_t);
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *, rmt_encoder_handle_t *);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t);
esp_err_t rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void *, size_t, const rmt_transmit_config_t *);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int);
esp_err_t rmt_receive(rmt_channel_handle_t, void *, size_t, const rmt_receive_config_t *);
QueueHandle_t xQueueCreate(unsigned int, size_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
BaseType_t xQueueSendFromISR(QueueHandle_t, const void *, BaseType_t *);
void vQueueDelete(QueueHandle_t);
void *heap_caps_aligned_alloc(size_t, size_t, unsigned int);
void *heap_caps_malloc(size_t, unsigned int);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
void vSemaphoreDelete(SemaphoreHandle_t);
BaseType_t xQueueSend(QueueHandle_t,const void *,TickType_t);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
void xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, int, TaskHandle_t *, int);
void vTaskDelete(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
void xTaskNotifyGive(TaskHandle_t);
void vTaskDelay(TickType_t);
void ets_delay_us(uint32_t);
int64_t esp_timer_get_time(void);
#endif
