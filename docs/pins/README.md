# Pins (Board GPIO Map)

`pin_def.h` is the central, header-only map of every GPIO assignment for a board. No driver should hardcode a GPIO number: all of them include this header and use the defines. The P4 and C5 firmwares each ship their own `pin_def.h` with different pin numbers and different peripheral sets, so the two are documented as separate sections below.

## Overview

- **Location:** `components/Drivers/pins/include/pin_def.h` (per firmware)
- **Header only:** no `.c` file; pure `#define`s inside an `extern "C"` guard.
- **Rule:** hardware pin numbers live here and only here.

A `-1` value marks a signal that is not wired on the board (or is driven by something other than that MCU), and `LED_COUNT` gives the addressable-LED count.

---

# P4 (ESP32-P4, HighBoy V2)

Header: `firmware_p4/components/Drivers/pins/include/pin_def.h`

## Pin Groups

### Shared SPI bus
One SPI bus is shared by the display, CC1101, NFC, IMU and LoRa.

| Signal | Define | GPIO |
| :--- | :--- | :--- |
| MOSI | `GPIO_SPI_MOSI_PIN` | 22 |
| SCLK | `GPIO_SPI_SCLK_PIN` | 21 |
| MISO | `GPIO_SPI_MISO_PIN` | 23 |

The SX1262 LoRa reuses these same lines: `GPIO_LORA_SCLK_PIN` (21), `GPIO_LORA_MOSI_PIN` (22) and `GPIO_LORA_MISO_PIN` (23) alias the shared bus, with its own `GPIO_LORA_CS_PIN` (26), `GPIO_LORA_BUSY_PIN` (4) and `GPIO_LORA_DIO1_PIN` (5). `GPIO_LORA_RESET_PIN`, `GPIO_LORA_TXEN_PIN` and `GPIO_LORA_RXEN_PIN` are `-1` (not wired).

### I2C bus
Charger, fuel gauge, LED driver and haptic share one I2C bus: `GPIO_I2C_SDA_PIN` (31), `GPIO_I2C_SCL_PIN` (30).

### Chip selects and IRQs on the shared SPI bus

| Peripheral | Signal | Define | GPIO |
| :--- | :--- | :--- | :--- |
| CC1101 | CS | `GPIO_CC1101_CS_PIN` | 20 |
| CC1101 | GDO0 | `GPIO_CC1101_GDO0_PIN` | 8 |
| CC1101 | GDO2 | `GPIO_CC1101_GDO2_PIN` | 9 |
| ST7789 | CS / DC / RST / BL | `GPIO_ST7789_CS_PIN` / `_DC_PIN` / `_RST_PIN` / `_BL_PIN` | 51 / 50 / 52 / 14 |
| IMU QMI8658 | CS | `GPIO_QMI8658A_CS_PIN` | 36 |
| NFC ST25R3916 | CS / IRQ | `GPIO_NFC_CS_PIN` / `GPIO_NFC_IRQ_PIN` | 24 / 10 |

### Sub-GHz antenna switch (PE613050)
`GPIO_RF_SW_V1_PIN` (17), `GPIO_RF_SW_V2_PIN` (18).

### Buttons (active-low)

| Button | Define | GPIO |
| :--- | :--- | :--- |
| LEFT | `GPIO_BTN_LEFT_PIN` | 35 |
| RIGHT | `GPIO_BTN_RIGHT_PIN` | 13 |
| UP | `GPIO_BTN_UP_PIN` | 3 |
| DOWN | `GPIO_BTN_DOWN_PIN` | 7 |
| OK | `GPIO_BTN_OK_PIN` | 25 |
| BACK | `GPIO_BTN_BACK_PIN` | 6 |

**BACK / LEFT are on the hardware power path.** As documented in `power_policy.c`, BACK and LEFT are wired to the charger `/QON` and the P4 `CHIP_PU` respectively, so holding **BACK + LEFT** together resets the P4 in hardware before firmware can react. Because of this the graceful power-off combo is deliberately **OK + LEFT** (OK is not on that path), letting firmware stay alive to issue the ship-mode command. Keep this in mind when picking or documenting button combos.

Note also that `GPIO_BTN_LEFT_PIN` (35) is the same pin as the P4 boot strap `GPIO_P4_BOOT_PIN` (35, "button B5"), so LEFT doubles as a boot-strapping pin.

### 125 kHz LF RFID
`GPIO_RFID_LF_CARRIER_PIN` (15), `GPIO_RFID_LF_DATA_PIN` (16), `GPIO_RFID_LF_MOD_PIN` (11), `GPIO_RFID_LF_COIL_PIN` (2).

### IR
`GPIO_IR_TX_PIN` (0), `GPIO_IR_RX_PIN` (1) (TSOP receiver + LED).

### SDMMC (4-bit SDIO)
`GPIO_SDMMC_CLK_PIN` (43), `GPIO_SDMMC_CMD_PIN` (44), `GPIO_SDMMC_D0_PIN` (39), `GPIO_SDMMC_D1_PIN` (40), `GPIO_SDMMC_D2_PIN` (41), `GPIO_SDMMC_D3_PIN` (42), card-detect `GPIO_SD_CD_PIN` (34).

### P4 to C5 bridge (SPI master)
`GPIO_BRIDGE_SCLK_PIN` (45), `GPIO_BRIDGE_MOSI_PIN` (46), `GPIO_BRIDGE_MISO_PIN` (47), `GPIO_BRIDGE_CS_PIN` (48). `GPIO_BRIDGE_IRQ_PIN` is `-1`. C5 reset/boot (`GPIO_C5_RESET_PIN`, `GPIO_C5_BOOT_PIN`) are `-1` because the CP2105, not the P4, drives them.

### Audio (I2S) and mic (PDM)
`GPIO_AUDIO_EN_PIN` (49), `GPIO_AUDIO_LRCLK_PIN` (29), `GPIO_AUDIO_BCLK_PIN` (28), `GPIO_AUDIO_DIN_PIN` (27) for the amp (commented NS4168); `GPIO_MIC_PDM_CLK_PIN` (54), `GPIO_MIC_PDM_DATA_PIN` (53) for the PDM mic (commented MSM261). See [docs/audio_i2s/README.md](../audio_i2s/README.md).

### Miscellaneous
`GPIO_HAPTIC_TRIG_PIN` (32, AW8623 trigger), `GPIO_CHARGER_CE_PIN` (33, BQ25896 OTG/CE), `GPIO_USB_MUX_SEL_PIN` (19, TS3USB221 D+/D- mux), console UART0 `GPIO_P4_UART0_RX_PIN` (37) / `GPIO_P4_UART0_TX_PIN` (38) to the CP2105 SCI, and `GPIO_LED_RGB_PIN` `-1` with `LED_COUNT` 1 (the LP5816 RGB LED is I2C-driven in V2, no data pin).

### No direct P4 to C5 UART
On the HighBoy V2 there is **no** direct P4 to C5 UART. The schematic routes the P4 UART0 (GPIO37/38) and the C5 UART0 (GPIO11/12) to two **separate** channels of the CP2105 USB bridge; they never meet. `GPIO38` is the P4's own console TX to the CP2105 (SCI), not a wire to the C5. The legacy defines `GPIO_C5_UART_TX_PIN` (38), `GPIO_C5_UART_RX_PIN` (39), `GPIO_RFID_UART_TX_PIN` (24) and `GPIO_RFID_UART_RX_PIN` (25) are kept only so unported V1 drivers still build. Consequently a "C5 OTA over UART" path cannot reach the C5 (it just writes to the P4's USB serial); the only P4 to C5 data path is the SPI bridge.

---

# C5 (ESP32-C5)

Header: `firmware_c5/components/Drivers/pins/include/pin_def.h`

The C5 carries a smaller peripheral set than the P4 and uses entirely different GPIO numbers.

## Pin Groups

### SPI bus
`GPIO_SPI_MOSI_PIN` (11), `GPIO_SPI_SCLK_PIN` (12), `GPIO_SPI_MISO_PIN` (13).

### CC1101 Sub-GHz radio
`GPIO_CC1101_CS_PIN` (3), `GPIO_CC1101_GDO0_PIN` (8), `GPIO_CC1101_GDO2_PIN` (9).

### SD card (SPI mode)
`GPIO_SD_CARD_CS_PIN` (14). Unlike the P4 (4-bit SDIO), the C5 uses SPI-mode SD.

### ST7789 display
`GPIO_ST7789_CS_PIN` (48), `GPIO_ST7789_DC_PIN` (47), `GPIO_ST7789_RST_PIN` (21), `GPIO_ST7789_BL_PIN` (38).

### Buttons

| Button | Define | GPIO |
| :--- | :--- | :--- |
| LEFT | `GPIO_BTN_LEFT_PIN` | 5 |
| BACK | `GPIO_BTN_BACK_PIN` | 7 |
| UP | `GPIO_BTN_UP_PIN` | 15 |
| DOWN | `GPIO_BTN_DOWN_PIN` | 6 |
| OK | `GPIO_BTN_OK_PIN` | 4 |
| RIGHT | `GPIO_BTN_RIGHT_PIN` | 16 |

The C5 button pins are independent GPIOs and are not tied to the charger `/QON` or `CHIP_PU` power path that the P4's BACK/LEFT are on.

### I2C bus
`GPIO_I2C_SDA_PIN` (8), `GPIO_I2C_SCL_PIN` (9). These share the same GPIO numbers as the CC1101 GDO0/GDO2 defines above.

### RGB LED
`GPIO_LED_RGB_PIN` (27), `LED_COUNT` 1 (WS2812 / SK6812). Unlike the P4, the C5 has a real addressable-LED data pin.

### P4 to C5 bridge (SPI slave)
The C5 side of the bridge is the SPI **slave**: `GPIO_BRIDGE_SCLK_PIN` (26), `GPIO_BRIDGE_MOSI_PIN` (25), `GPIO_BRIDGE_MISO_PIN` (24), `GPIO_BRIDGE_CS_PIN` (23), `GPIO_BRIDGE_IRQ_PIN` (3). As noted on the P4 side, this SPI bridge is the only data path between the two chips; there is no direct P4 to C5 UART on the V2 board.
</content>
