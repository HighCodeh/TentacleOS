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
 * @brief Central GPIO pin assignments for the ESP32-C5 board.
 *
 * All hardware pin numbers are defined here. No driver should
 * hardcode GPIO numbers — use these defines instead.
 */

#ifndef PIN_DEF_H
#define PIN_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

// SPI Bus
#define GPIO_SPI_MOSI_PIN 11
#define GPIO_SPI_SCLK_PIN 12
#define GPIO_SPI_MISO_PIN 13

// CC1101 Sub-GHz Radio
#define GPIO_CC1101_CS_PIN   3
#define GPIO_CC1101_GDO0_PIN 8
#define GPIO_CC1101_GDO2_PIN 9

// SD Card (SPI mode)
#define GPIO_SD_CARD_CS_PIN 14

// ST7789 Display
#define GPIO_ST7789_CS_PIN  48
#define GPIO_ST7789_DC_PIN  47
#define GPIO_ST7789_RST_PIN 21
#define GPIO_ST7789_BL_PIN  38

// Buttons
#define GPIO_BTN_LEFT_PIN  5
#define GPIO_BTN_BACK_PIN  7
#define GPIO_BTN_UP_PIN    15
#define GPIO_BTN_DOWN_PIN  6
#define GPIO_BTN_OK_PIN    4
#define GPIO_BTN_RIGHT_PIN 16

// I2C Bus
#define GPIO_I2C_SDA_PIN 8
#define GPIO_I2C_SCL_PIN 9

// RGB LED (WS2812 / SK6812)
#define GPIO_LED_RGB_PIN 27
#define LED_COUNT        1

// P4-C5 Bridge SPI (Slave)
#define GPIO_BRIDGE_SCLK_PIN 26
#define GPIO_BRIDGE_MOSI_PIN 25
#define GPIO_BRIDGE_MISO_PIN 24
#define GPIO_BRIDGE_CS_PIN   23
#define GPIO_BRIDGE_IRQ_PIN  3

#ifdef __cplusplus
}
#endif

#endif // PIN_DEF_H
