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
/**
 * [FIX-v3] Replaced spi_device_queue_trans + spi_device_get_trans_result
 *  with spi_device_transmit (blocking, atomic).
 *
 *  Problem with the previous approach (queue + get with timeout):
 *
 *  1. queue_trans() enqueues DMA and starts on the SPI bus
 *  2. get_trans_result(10ms) occasionally times out
 *  3. drain(portMAX_DELAY) saves from crash, BUT the transaction already
 *     happened on the bus: the chip read REG_TARGET_INT and CLEARED
 *     the IRQ register in hardware.
 *  4. We drop the WU_A / SDD_C values silently; software never sees
 *     the phone even though the chip is working.
 *
 *  spi_device_transmit() fixes this completely:
 *  - Blocking with portMAX_DELAY internally never loses data.
 *  - No timeout window between queue and get.
 *  - Eliminates "spi wait fail" warnings.
 *  - Simpler code, no intermediate states.
 */
#include "hb_nfc_spi.h"

#include <string.h>
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_err.h"

static const char* TAG = "hb_spi";
static spi_device_handle_t s_spi = NULL;
static bool s_init = false;

static hb_nfc_err_t hb_spi_transmit(spi_transaction_t* t)
{
    if (!s_spi || !t) return HB_NFC_ERR_SPI_XFER;

    esp_err_t ret = spi_device_transmit(s_spi, t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi transmit fail: %s", esp_err_to_name(ret));
        return HB_NFC_ERR_SPI_XFER;
    }

    return HB_NFC_OK;
}

hb_nfc_err_t hb_spi_init(int spi_host, int mosi, int miso, int sclk,
                           int cs, int mode, uint32_t clock_hz)
{
    if (s_init) return HB_NFC_OK;

    spi_bus_config_t bus = {
        .mosi_io_num   = mosi,
        .miso_io_num   = miso,
        .sclk_io_num   = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t ret = spi_bus_initialize(spi_host, &bus, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bus init fail: %s", esp_err_to_name(ret));
        return HB_NFC_ERR_SPI_INIT;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz   = clock_hz,
        .mode             = mode,
        .spics_io_num     = cs,
        .queue_size       = 1,
        .cs_ena_pretrans  = 1,
        .cs_ena_posttrans = 1,
    };
    ret = spi_bus_add_device(spi_host, &dev, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add device fail: %s", esp_err_to_name(ret));
        spi_bus_free(spi_host);
        return HB_NFC_ERR_SPI_INIT;
    }

    s_init = true;
    ESP_LOGI(TAG, "SPI OK: mode=%d clk=%lu cs=%d", mode, (unsigned long)clock_hz, cs);
    return HB_NFC_OK;
}

void hb_spi_deinit(void)
{
    if (!s_init) return;
    if (s_spi) {
        spi_bus_remove_device(s_spi);
        s_spi = NULL;
    }
    s_init = false;
}

hb_nfc_err_t hb_spi_reg_read(uint8_t addr, uint8_t* value)
{
    uint8_t tx[2] = { (uint8_t)(0x40 | (addr & 0x3F)), 0x00 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    hb_nfc_err_t err = hb_spi_transmit(&t);
    if (err != HB_NFC_OK) {
        *value = 0;
        return err;
    }
    *value = rx[1];
    return HB_NFC_OK;
}

hb_nfc_err_t hb_spi_reg_write(uint8_t addr, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(addr & 0x3F), value };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
    };
    return hb_spi_transmit(&t);
}

hb_nfc_err_t hb_spi_reg_modify(uint8_t addr, uint8_t mask, uint8_t value)
{
    uint8_t cur;
    hb_nfc_err_t err = hb_spi_reg_read(addr, &cur);
    if (err != HB_NFC_OK) return err;
    uint8_t nv = (cur & (uint8_t)~mask) | (value & mask);
    return hb_spi_reg_write(addr, nv);
}

hb_nfc_err_t hb_spi_fifo_load(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > 512) return HB_NFC_ERR_PARAM;

    /* Static buffer: ST25R3916 FIFO is 512 bytes. Not re-entrant, but NFC
     * is single-threaded and DMA requires a stable (non-stack) pointer. */
    static uint8_t tx[1 + 512];
    tx[0] = 0x80;
    memcpy(&tx[1], data, len);

    spi_transaction_t t = {
        .length    = (uint32_t)((len + 1) * 8),
        .tx_buffer = tx,
    };
    return hb_spi_transmit(&t);
}

hb_nfc_err_t hb_spi_fifo_read(uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > 512) return HB_NFC_ERR_PARAM;

    static uint8_t tx[1 + 512];
    static uint8_t rx[1 + 512];
    memset(tx, 0, len + 1);
    tx[0] = 0x9F;

    spi_transaction_t t = {
        .length    = (uint32_t)((len + 1) * 8),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    hb_nfc_err_t err = hb_spi_transmit(&t);
    if (err != HB_NFC_OK) return err;
    memcpy(data, &rx[1], len);
    return HB_NFC_OK;
}

hb_nfc_err_t hb_spi_direct_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    return hb_spi_transmit(&t);
}

/**
 * PT Memory write ST25R3916 SPI protocol:
 *  TX: [prefix_byte] [data0] [data1] ... [dataN]
 *
 * Prefix bytes:
 *  0xA0 = PT_MEM_A (NFC-A: ATQA/UID/SAK, 15 bytes max)
 *  0xA8 = PT_MEM_F (NFC-F: SC + SENSF_RES, 19 bytes total)
 *  0xAC = PT_MEM_TSN (12 bytes max)
 */
hb_nfc_err_t hb_spi_pt_mem_write(uint8_t prefix, const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > 19) return HB_NFC_ERR_PARAM;

    uint8_t tx[1 + 19];
    tx[0] = prefix;
    memcpy(&tx[1], data, len);

    spi_transaction_t t = {
        .length    = (uint32_t)((len + 1) * 8),
        .tx_buffer = tx,
    };
    return hb_spi_transmit(&t);
}

/**
 * PT Memory read ST25R3916 SPI protocol:
 *  TX: [0xBF] [0x00] [0x00] ... (len + 2 bytes total)
 *  RX: [xx] [xx] [d0] ... (data starts at rx[2])
 *
 * The ST25R3916 inserts two bytes before the real data on MISO:
 * a command byte and a turnaround byte.
 */
hb_nfc_err_t hb_spi_pt_mem_read(uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > 19) return HB_NFC_ERR_PARAM;

    uint8_t tx[2 + 19] = { 0 };
    uint8_t rx[2 + 19] = { 0 };
    tx[0] = 0xBF;

    spi_transaction_t t = {
        .length    = (uint32_t)((len + 2) * 8),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    hb_nfc_err_t err = hb_spi_transmit(&t);
    if (err != HB_NFC_OK) return err;
    memcpy(data, &rx[2], len);
    return HB_NFC_OK;
}

/**
 * Raw SPI transfer for arbitrary length transactions.
 */
hb_nfc_err_t hb_spi_raw_xfer(const uint8_t* tx, uint8_t* rx, size_t len)
{
    if (len == 0 || len > 64) return HB_NFC_ERR_PARAM;

    spi_transaction_t t = {
        .length    = (uint32_t)(len * 8),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return hb_spi_transmit(&t);
}

#include "hb_nfc_gpio.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char* TAG_GPIO = "hb_gpio";
static int s_pin_irq = -1;

hb_nfc_err_t hb_gpio_init(int pin_irq)
{
    s_pin_irq = pin_irq;

    gpio_config_t cfg = {
        .pin_bit_mask   = 1ULL << pin_irq,
        .mode           = GPIO_MODE_INPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_GPIO, "IRQ pin %d config fail", pin_irq);
        return HB_NFC_ERR_GPIO;
    }

    ESP_LOGI(TAG_GPIO, "IRQ pin %d OK, level=%d", pin_irq, gpio_get_level(pin_irq));
    return HB_NFC_OK;
}

void hb_gpio_deinit(void)
{
    if (s_pin_irq >= 0) {
        gpio_reset_pin(s_pin_irq);
        s_pin_irq = -1;
    }
}

int hb_gpio_irq_level(void)
{
    if (s_pin_irq < 0) return 0;
    return gpio_get_level(s_pin_irq);
}

bool hb_gpio_irq_wait(uint32_t timeout_ms)
{
    for (uint32_t i = 0; i < timeout_ms; i++) {
        if (gpio_get_level(s_pin_irq)) return true;
        esp_rom_delay_us(1000);
    }
    return false;
}

#include "hb_nfc_timer.h"
#include "esp_rom_sys.h"
#include "freertos/task.h"

void hb_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

void hb_delay_ms(uint32_t ms)
{
    /* Yield to the scheduler for multi-ms delays instead of busy-spinning.
     * Must be called from a FreeRTOS task context (not ISR). */
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}
