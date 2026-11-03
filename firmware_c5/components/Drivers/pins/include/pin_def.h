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

/** @brief High Boy V2.03: P4-C5 bridge, polling only. */
#define GPIO_BRIDGE_SCLK_PIN 26
#define GPIO_BRIDGE_MOSI_PIN 25
#define GPIO_BRIDGE_MISO_PIN 24
#define GPIO_BRIDGE_CS_PIN   23

/** @brief Local controls; peripheral data/I2C remain on P4. */
#define GPIO_AUDIO_EN_PIN   0
#define GPIO_HAPTIC_INT_PIN 1
#define GPIO_USB_MUX_OE_PIN 4
#define GPIO_HAPTIC_RST_PIN 5
#define GPIO_IR_TX_PIN      6
#define GPIO_IR_RX_PIN      10

#ifdef __cplusplus
}
#endif

#endif // PIN_DEF_H
