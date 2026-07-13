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

/**
 * @file pin_def.h
 * @brief Central GPIO pin assignments for the ESP32-P4 board.
 *
 * All hardware pin numbers are defined here. No driver should
 * hardcode GPIO numbers — use these defines instead.
 */

#ifndef PIN_DEF_H
#define PIN_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

// SPI Bus (shared: display, radio, SD card)
#define GPIO_SPI_MOSI_PIN 22
#define GPIO_SPI_SCLK_PIN 21
#define GPIO_SPI_MISO_PIN 23

// CC1101 Sub-GHz Radio
#define GPIO_CC1101_CS_PIN   20
#define GPIO_CC1101_GDO0_PIN 8
#define GPIO_CC1101_GDO2_PIN 9

// SDMMC (4-bit SDIO)
#define GPIO_SDMMC_CLK_PIN 43
#define GPIO_SDMMC_CMD_PIN 44
#define GPIO_SDMMC_D0_PIN  32
#define GPIO_SDMMC_D1_PIN  40
#define GPIO_SDMMC_D2_PIN  41
#define GPIO_SDMMC_D3_PIN  42

// ST7789 Display
#define GPIO_ST7789_CS_PIN  34
#define GPIO_ST7789_DC_PIN  35
#define GPIO_ST7789_RST_PIN 36
#define GPIO_ST7789_BL_PIN  14

// Buttons
#define GPIO_BTN_LEFT_PIN  6
#define GPIO_BTN_BACK_PIN  54
#define GPIO_BTN_UP_PIN    3
#define GPIO_BTN_DOWN_PIN  7
#define GPIO_BTN_OK_PIN    29
#define GPIO_BTN_RIGHT_PIN 13

// I2C Bus
#define GPIO_I2C_SDA_PIN 31
#define GPIO_I2C_SCL_PIN 30

// RGB LED (WS2812 / SK6812)
#define GPIO_LED_RGB_PIN 45
#define LED_COUNT        1

// P4-C5 Bridge SPI (Master)
#define GPIO_BRIDGE_SCLK_PIN 45
#define GPIO_BRIDGE_MOSI_PIN 46
#define GPIO_BRIDGE_MISO_PIN 47
#define GPIO_BRIDGE_CS_PIN   48
#define GPIO_BRIDGE_IRQ_PIN  (-1)

// C5 Control & Update (UART + Boot)
#define GPIO_C5_UART_TX_PIN 38
#define GPIO_C5_UART_RX_PIN 39
#define GPIO_C5_RESET_PIN   (-1)
#define GPIO_C5_BOOT_PIN    (-1)

// SX1262 LoRa (SPI3_HOST, separate from C5 bridge)
#define GPIO_LORA_SCLK_PIN  18
#define GPIO_LORA_MOSI_PIN  19
#define GPIO_LORA_MISO_PIN  14
#define GPIO_LORA_CS_PIN    26
#define GPIO_LORA_RESET_PIN (-1)
#define GPIO_LORA_BUSY_PIN  4
#define GPIO_LORA_DIO1_PIN  5
#define GPIO_LORA_TXEN_PIN  (-1)
#define GPIO_LORA_RXEN_PIN  (-1)

// YS-RFID2 125kHz RFID Reader (UART)
// TODO: placeholder pins — definir com base no schematic do board
#define GPIO_RFID_UART_TX_PIN 24
#define GPIO_RFID_UART_RX_PIN 25

#ifdef __cplusplus
}
#endif

#endif // PIN_DEF_H
