#include "c5_flasher.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "pin_def.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "C5_FLASHER";
#define FLASHER_UART UART_NUM_1
#define FLASH_BLOCK_SIZE 1024

// Access to embedded binary symbols
#if C5_FIRMWARE_EMBEDDED
extern const uint8_t c5_firmware_bin_start[] asm("_binary_TentacleOS_C5_bin_start");
extern const uint8_t c5_firmware_bin_end[]   asm("_binary_TentacleOS_C5_bin_end");
#endif

// ESP Serial Protocol Constants
#define ESP_ROM_BAUD  115200
#define SLIP_END      0xC0
#define SLIP_ESC      0xDB
#define SLIP_ESC_END  0xDC
#define SLIP_ESC_ESC  0xDD

typedef struct {
    uint8_t direction;
    uint8_t command;
    uint16_t size;
    uint32_t checksum;
} __attribute__((packed)) esp_command_header_t;

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
    esp_command_header_t header = {
        .direction = 0x00,
        .command = cmd,
        .size = len,
        .checksum = checksum
    };

    uart_write_bytes(FLASHER_UART, &start, 1);
    
    uint8_t *h_ptr = (uint8_t *)&header;
    for (int i = 0; i < sizeof(header); i++) slip_send_byte(h_ptr[i]);
    for (int i = 0; i < len; i++) slip_send_byte(payload[i]);
    
    uart_write_bytes(FLASHER_UART, &start, 1);
}

esp_err_t c5_flasher_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << C5_RESET_PIN) | (1ULL << C5_BOOT_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1
    };
    gpio_config(&io_conf);
    gpio_set_level(C5_RESET_PIN, 1);
    gpio_set_level(C5_BOOT_PIN, 1);

    uart_config_t uart_config = {
        .baud_rate = ESP_ROM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    uart_driver_install(FLASHER_UART, 2048, 0, 0, NULL, 0);
    uart_param_config(FLASHER_UART, &uart_config);
    return uart_set_pin(FLASHER_UART, C5_UART_TX_PIN, C5_UART_RX_PIN, -1, -1);
}

void c5_flasher_enter_bootloader(void) {
    ESP_LOGI(TAG, "Syncing C5 into bootloader...");
    gpio_set_level(C5_BOOT_PIN, 0);
    gpio_set_level(C5_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(C5_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(C5_BOOT_PIN, 1);
}

void c5_flasher_reset_normal(void) {
    gpio_set_level(C5_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(C5_RESET_PIN, 1);
    ESP_LOGI(TAG, "C5 Reset completed.");
}

esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size) {
    if (!bin_data) {
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

    // 1. FLASH_BEGIN (0x02)
    uint32_t num_blocks = (bin_size + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE;
    uint32_t begin_params[4] = { bin_size, num_blocks, FLASH_BLOCK_SIZE, 0x0000 }; // Offset 0
    send_packet(0x02, (uint8_t*)begin_params, sizeof(begin_params), 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. FLASH_DATA (0x03)
    for (uint32_t i = 0; i < num_blocks; i++) {
        uint32_t offset = i * FLASH_BLOCK_SIZE;
        uint32_t this_len = (bin_size - offset > FLASH_BLOCK_SIZE) ? FLASH_BLOCK_SIZE : bin_size - offset;
        
        uint8_t block_buffer[FLASH_BLOCK_SIZE + 16];
        uint32_t *params = (uint32_t*)block_buffer;
        params[0] = this_len;
        params[1] = i;
        params[2] = 0; // Padding
        params[3] = 0; // Padding
        memcpy(block_buffer + 16, bin_data + offset, this_len);

        uint32_t checksum = 0xEF;
        for (int j = 0; j < this_len; j++) checksum ^= bin_data[offset + j];

        send_packet(0x03, block_buffer, this_len + 16, checksum);
        ESP_LOGI(TAG, "Writing block %ld/%ld...", i + 1, num_blocks);
        vTaskDelay(pdMS_TO_TICKS(20)); // Flow control
    }

    // 3. FLASH_END (0x04)
    uint32_t end_params[1] = { 0 }; // Reboot = 0
    send_packet(0x04, (uint8_t*)end_params, sizeof(end_params), 0);
    
    ESP_LOGI(TAG, "Update successful!");
    c5_flasher_reset_normal();
    return ESP_OK;
}
