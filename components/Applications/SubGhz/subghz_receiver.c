#include "subghz_receiver.h"
#include "cc1101.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "pin_def.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "SUBGHZ_RX";

#define RMT_RX_GPIO       8  // GDO0 (GPIO_SDA_PIN)
#define RMT_RESOLUTION_HZ 1000000 // 1MHz -> 1us per tick

// Configuration
#define RX_BUFFER_SIZE    2048 // Number of symbols
#define MIN_PULSE_NS      3000 // Hardware Filter limit (~3us)
#define SOFTWARE_FILTER_US 50  // Requested 50us filter (Software)
#define MAX_PULSE_NS      10000000 // 10ms idle timeout

static rmt_channel_handle_t rx_channel = NULL;
static QueueHandle_t rx_queue = NULL;
static TaskHandle_t rx_task_handle = NULL;
static volatile bool s_is_running = false;
static rmt_symbol_word_t raw_symbols[RX_BUFFER_SIZE];

static bool subghz_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
  BaseType_t high_task_wakeup = pdFALSE;
  QueueHandle_t queue = (QueueHandle_t)user_data;
  xQueueSendFromISR(queue, edata, &high_task_wakeup);
  return high_task_wakeup == pdTRUE;
}

static void subghz_rx_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting RMT RX Task (New Driver)");

  // 1. Configure RMT RX Channel
  rmt_rx_channel_config_t rx_channel_cfg = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = RMT_RESOLUTION_HZ,
    .mem_block_symbols = 64, // Default internal buffer
    .gpio_num = RMT_RX_GPIO,
  };

  ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &rx_channel));

  // 2. Register Callbacks
  rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
  rmt_rx_event_callbacks_t cbs = {
    .on_recv_done = subghz_rx_done_callback,
  };
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, rx_queue));

  // 3. Enable Channel
  ESP_ERROR_CHECK(rmt_enable(rx_channel));

  // 4. Configure CC1101 (Async Mode)
  cc1101_enable_async_mode(433920000);

  // 5. Start Receiving Loop
  s_is_running = true;
  ESP_LOGI(TAG, "Waiting for signals...");

  rmt_receive_config_t receive_config = {
    .signal_range_min_ns = MIN_PULSE_NS, // Hardware Filter
    .signal_range_max_ns = MAX_PULSE_NS, // Idle Timeout
  };

  rmt_rx_done_event_data_t rx_data;

  // Start first reception
  ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config));

  while (s_is_running) {
    if (xQueueReceive(rx_queue, &rx_data, pdMS_TO_TICKS(100)) == pdPASS) {

      if (rx_data.num_symbols > 0) {
        bool has_printed_tag = false;

        for (size_t i = 0; i < rx_data.num_symbols; i++) {
          rmt_symbol_word_t *sym = &rx_data.received_symbols[i];

          // Pulse 0 + Software Filter
          if (sym->duration0 >= SOFTWARE_FILTER_US) {
            if (!has_printed_tag) { printf("RAW: "); has_printed_tag = true; }
            printf("%d ", sym->level0 ? (int)sym->duration0 : -(int)sym->duration0);
          }

          // Pulse 1 + Software Filter
          if (sym->duration1 >= SOFTWARE_FILTER_US) {
            if (!has_printed_tag) { printf("RAW: "); has_printed_tag = true; }
            printf("%d ", sym->level1 ? (int)sym->duration1 : -(int)sym->duration1);
          }
        }

        if (has_printed_tag) printf("\n");
      }

      // Continue receiving
      if (s_is_running) {
        ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config));
      }
    }
  }

  // Cleanup
  rmt_disable(rx_channel);
  rmt_del_channel(rx_channel);
  vQueueDelete(rx_queue);
  rx_channel = NULL;
  rx_queue = NULL;
  vTaskDelete(NULL);
}

void subghz_receiver_start(void) {
  if (s_is_running) return;
  xTaskCreatePinnedToCore(subghz_rx_task, "subghz_rx", 4096, NULL, 5, &rx_task_handle, 1);
}

void subghz_receiver_stop(void) {
  s_is_running = false;
}

bool subghz_receiver_is_running(void) {
  return s_is_running;
}
