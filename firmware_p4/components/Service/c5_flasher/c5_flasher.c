// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "c5_flasher.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_def.h"

static const char *TAG = "C5_FLASHER";

#define FLASHER_UART          UART_NUM_1
#define FLASHER_UART_BUF      2048
#define FLASH_BLOCK_SIZE      1024
#define FLASH_BLOCK_HDR_SIZE  16
#define FLASH_CHECKSUM_INIT   0xEF
#define BOOTLOADER_DELAY_MS   100
#define BOOT_RELEASE_DELAY_MS 200
#define FLASH_BEGIN_DELAY_MS  100
#define FLASH_BLOCK_DELAY_MS  20

// ESP serial protocol (SLIP framing)
#define ESP_ROM_BAUD 115200
#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

// ESP serial protocol commands
#define ESP_CMD_FLASH_BEGIN 0x02
#define ESP_CMD_FLASH_DATA  0x03
#define ESP_CMD_FLASH_END   0x04

#if C5_FIRMWARE_EMBEDDED
extern const uint8_t c5_firmware_bin_start[] asm("_binary_TentacleOS_C5_bin_start");
extern const uint8_t c5_firmware_bin_end[] asm("_binary_TentacleOS_C5_bin_end");
#endif

typedef struct {
  uint8_t direction;
  uint8_t command;
  uint16_t size;
  uint32_t checksum;
} __attribute__((packed)) c5_flasher_cmd_header_t;

static void slip_send_byte(uint8_t b);
static void send_packet(uint8_t cmd, uint8_t *payload, uint16_t len, uint32_t checksum);

esp_err_t c5_flasher_init(void) {
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << GPIO_C5_RESET_PIN) | (1ULL << GPIO_C5_BOOT_PIN),
      .pull_down_en = 0,
      .pull_up_en = 1,
  };
  gpio_config(&io_conf);
  gpio_set_level(GPIO_C5_RESET_PIN, 1);
  gpio_set_level(GPIO_C5_BOOT_PIN, 1);

  uart_config_t uart_config = {
      .baud_rate = ESP_ROM_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  uart_driver_install(FLASHER_UART, FLASHER_UART_BUF, 0, 0, NULL, 0);
  uart_param_config(FLASHER_UART, &uart_config);
  return uart_set_pin(FLASHER_UART, GPIO_C5_UART_TX_PIN, GPIO_C5_UART_RX_PIN, -1, -1);
}

void c5_flasher_enter_bootloader(void) {
  ESP_LOGI(TAG, "Entering bootloader mode");
  gpio_set_level(GPIO_C5_BOOT_PIN, 0);
  gpio_set_level(GPIO_C5_RESET_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(BOOTLOADER_DELAY_MS));
  gpio_set_level(GPIO_C5_RESET_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(BOOT_RELEASE_DELAY_MS));
  gpio_set_level(GPIO_C5_BOOT_PIN, 1);
}

void c5_flasher_reset_normal(void) {
  gpio_set_level(GPIO_C5_RESET_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(BOOTLOADER_DELAY_MS));
  gpio_set_level(GPIO_C5_RESET_PIN, 1);
  ESP_LOGI(TAG, "C5 reset completed");
}

esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size) {
  if (bin_data == NULL) {
#if C5_FIRMWARE_EMBEDDED
    bin_data = c5_firmware_bin_start;
    bin_size = c5_firmware_bin_end - c5_firmware_bin_start;
#else
    ESP_LOGE(TAG, "Embedded C5 firmware is unavailable");
    return ESP_ERR_NOT_FOUND;
#endif
  }

  if (bin_size == 0) {
    ESP_LOGE(TAG, "Invalid binary size");
    return ESP_ERR_INVALID_ARG;
  }

  c5_flasher_enter_bootloader();

  uint32_t num_blocks = (bin_size + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
  uint32_t begin_params[4] = {bin_size, num_blocks, FLASH_BLOCK_SIZE, 0x0000};
  send_packet(ESP_CMD_FLASH_BEGIN, (uint8_t *)begin_params, sizeof(begin_params), 0);
  vTaskDelay(pdMS_TO_TICKS(FLASH_BEGIN_DELAY_MS));

  for (uint32_t i = 0; i < num_blocks; i++) {
    uint32_t offset = i * FLASH_BLOCK_SIZE;
    uint32_t this_len =
        (bin_size - offset > FLASH_BLOCK_SIZE) ? FLASH_BLOCK_SIZE : bin_size - offset;

    uint8_t block_buffer[FLASH_BLOCK_SIZE + FLASH_BLOCK_HDR_SIZE];
    uint32_t *params = (uint32_t *)block_buffer;
    params[0] = this_len;
    params[1] = i;
    params[2] = 0;
    params[3] = 0;
    memcpy(block_buffer + FLASH_BLOCK_HDR_SIZE, bin_data + offset, this_len);

    uint32_t checksum = FLASH_CHECKSUM_INIT;
    for (uint32_t j = 0; j < this_len; j++) {
      checksum ^= bin_data[offset + j];
    }

    send_packet(ESP_CMD_FLASH_DATA, block_buffer, this_len + FLASH_BLOCK_HDR_SIZE, checksum);
    ESP_LOGI(TAG, "Writing block %lu/%lu", i + 1, num_blocks);
    vTaskDelay(pdMS_TO_TICKS(FLASH_BLOCK_DELAY_MS));
  }

  uint32_t end_params[1] = {0};
  send_packet(ESP_CMD_FLASH_END, (uint8_t *)end_params, sizeof(end_params), 0);

  ESP_LOGI(TAG, "Update successful");
  c5_flasher_reset_normal();
  return ESP_OK;
}

static void slip_send_byte(uint8_t b) {
  if (b == SLIP_END) {
    uint8_t esc[] = {SLIP_ESC, SLIP_ESC_END};
    uart_write_bytes(FLASHER_UART, esc, 2);
  } else if (b == SLIP_ESC) {
    uint8_t esc[] = {SLIP_ESC, SLIP_ESC_ESC};
    uart_write_bytes(FLASHER_UART, esc, 2);
  } else {
    uart_write_bytes(FLASHER_UART, &b, 1);
  }
}

static void send_packet(uint8_t cmd, uint8_t *payload, uint16_t len, uint32_t checksum) {
  uint8_t start = SLIP_END;
  c5_flasher_cmd_header_t header = {
      .direction = 0x00,
      .command = cmd,
      .size = len,
      .checksum = checksum,
  };

  uart_write_bytes(FLASHER_UART, &start, 1);

  uint8_t *h_ptr = (uint8_t *)&header;
  for (size_t i = 0; i < sizeof(header); i++) {
    slip_send_byte(h_ptr[i]);
  }
  for (uint16_t i = 0; i < len; i++) {
    slip_send_byte(payload[i]);
  }

  uart_write_bytes(FLASHER_UART, &start, 1);
}
