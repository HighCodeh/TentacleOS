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
 * @brief Central GPIO pin assignments for the ESP32-P4 (HighBoy V2 board).
 *
 * All hardware pin numbers are defined here. No driver should hardcode GPIO
 * numbers - use these defines instead. Mapped from the HighBoy V2 schematic
 * (2026-07-05). See the sheet reference in each group's comment.
 */

#ifndef PIN_DEF_H
#define PIN_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shared SPI bus (display, CC1101, NFC, IMU, LoRa) - each has its own CS. */
#define GPIO_SPI_MOSI_PIN 22 // GPIO22 SI
#define GPIO_SPI_SCLK_PIN 21 // GPIO21 CLK
#define GPIO_SPI_MISO_PIN 23 // GPIO23 SO

/** @brief I2C bus (charger, fuel gauge, LED driver, haptic). */
#define GPIO_I2C_SDA_PIN 31
#define GPIO_I2C_SCL_PIN 30

/** @brief CC1101 Sub-GHz radio + PE613050 antenna switch. */
#define GPIO_CC1101_CS_PIN   20
#define GPIO_CC1101_GDO0_PIN 8
#define GPIO_CC1101_GDO2_PIN 9
#define GPIO_RF_SW_V1_PIN    17 // PE613050 control
#define GPIO_RF_SW_V2_PIN    18

/** @brief ST7789 display (SPI shared). Backlight is a PWM to the LED driver. */
#define GPIO_ST7789_CS_PIN  51
#define GPIO_ST7789_DC_PIN  50
#define GPIO_ST7789_RST_PIN 52
#define GPIO_ST7789_BL_PIN  14 // DSP_LED_CAT driver

/**
 * @brief Buttons (active-low, 10k pull-up). Physical B1..B6 = {6,25,13,7,35,3}.
 */
#define GPIO_BTN_LEFT_PIN  6  // B1 (also part of the P4 reset OR-gate)
#define GPIO_BTN_RIGHT_PIN 13 // B3
#define GPIO_BTN_UP_PIN    3  // B6
#define GPIO_BTN_DOWN_PIN  7  // B4
#define GPIO_BTN_OK_PIN    25 // B2
#define GPIO_BTN_BACK_PIN  35 // B5 (also the P4 BOOT strap)

/** @brief IMU QMI8658 (SPI shared). */
#define GPIO_IMU_CS_PIN  36
#define GPIO_IMU_INT_PIN (-1)

/** @brief NFC ST25R3916 (SPI shared). */
#define GPIO_NFC_CS_PIN  24
#define GPIO_NFC_IRQ_PIN 10

/** @brief 125 kHz LF RFID analog frontend. */
#define GPIO_RFID_LF_CARRIER_PIN 15 // 125KHz CL
#define GPIO_RFID_LF_DATA_PIN    16 // 125KHz DA
#define GPIO_RFID_LF_MOD_PIN     11 // OOK drive (T2)
#define GPIO_RFID_LF_COIL_PIN    2

/** @brief SX1262 LoRa (SPI shared). */
#define GPIO_LORA_SCLK_PIN  21 // shared
#define GPIO_LORA_MOSI_PIN  22 // shared
#define GPIO_LORA_MISO_PIN  23 // shared
#define GPIO_LORA_CS_PIN    26
#define GPIO_LORA_BUSY_PIN  4
#define GPIO_LORA_DIO1_PIN  5
#define GPIO_LORA_RESET_PIN (-1) // @todo NRESET (pin 15) not confirmed

/** @brief SDMMC (4-bit SDIO). */
#define GPIO_SDMMC_CLK_PIN 43
#define GPIO_SDMMC_CMD_PIN 44
#define GPIO_SDMMC_D0_PIN  39
#define GPIO_SDMMC_D1_PIN  40
#define GPIO_SDMMC_D2_PIN  41
#define GPIO_SDMMC_D3_PIN  42
#define GPIO_SD_CD_PIN     34

/** @brief P4-C5 bridge SPI (master). */
#define GPIO_BRIDGE_SCLK_PIN 45
#define GPIO_BRIDGE_MOSI_PIN 46
#define GPIO_BRIDGE_MISO_PIN 47
#define GPIO_BRIDGE_CS_PIN   48
#define GPIO_BRIDGE_IRQ_PIN  (-1)

/**
 * @brief C5 reset/boot are driven by the CP2105 (C5_EN / C5_GPIO28), not the P4.
 * Flash the C5 over the CP2105 (COM9). No P4-C5 UART in V2 (link is the SPI bridge).
 */
#define GPIO_C5_RESET_PIN (-1)
#define GPIO_C5_BOOT_PIN  (-1)

/** @brief Audio amplifier (I2S, NS4168). */
#define GPIO_AUDIO_EN_PIN    49 // CTRL / shutdown
#define GPIO_AUDIO_LRCLK_PIN 29
#define GPIO_AUDIO_BCLK_PIN  28
#define GPIO_AUDIO_DIN_PIN   27

/** @brief Microphone (PDM, MSM261). L/R selected by strap, no SEL GPIO. */
#define GPIO_MIC_PDM_CLK_PIN  54
#define GPIO_MIC_PDM_DATA_PIN 53

/** @brief Haptic driver (AW8623, I2C + trigger). */
#define GPIO_HAPTIC_TRIG_PIN 32

/** @brief Battery charger (BQ25896, I2C) OTG/CE control. */
#define GPIO_CHARGER_CE_PIN 33

/** @brief USB D+/D- mux (TS3USB221: 0 = CP2105 bridge, 1 = P4 native USB). */
#define GPIO_USB_MUX_SEL_PIN 19

/** @brief P4 console UART0 (to CP2105 SCI / Standard port). */
#define GPIO_P4_UART0_RX_PIN 37
#define GPIO_P4_UART0_TX_PIN 38

/** @brief P4 boot strap: GPIO35 (= button B5) LOW at reset -> download mode. */
#define GPIO_P4_BOOT_PIN 35

/** @brief RGB LED - driven by the LP5816 I2C driver in V2 (no dedicated data pin). */
#define GPIO_LED_RGB_PIN (-1)
#define LED_COUNT        1

#ifdef __cplusplus
}
#endif

#endif // PIN_DEF_H
