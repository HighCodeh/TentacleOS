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

#ifndef ST25R3916_REG_H
#define ST25R3916_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- Register addresses (Datasheet Table 11) --- */
#define ST25R3916_REG_IO_CONF1           0x00
#define ST25R3916_REG_IO_CONF2           0x01
#define ST25R3916_REG_OP_CTRL            0x02
#define ST25R3916_REG_MODE               0x03
#define ST25R3916_REG_BIT_RATE           0x04
#define ST25R3916_REG_ISO14443A          0x05
#define ST25R3916_REG_ISO14443B          0x06
#define ST25R3916_REG_ISO14443B_FELICA   0x07
#define ST25R3916_REG_PASSIVE_TARGET     0x08
#define ST25R3916_REG_STREAM_MODE        0x09
#define ST25R3916_REG_AUX_DEF            0x0A
#define ST25R3916_REG_RX_CONF1           0x0B
#define ST25R3916_REG_RX_CONF2           0x0C
#define ST25R3916_REG_RX_CONF3           0x0D
#define ST25R3916_REG_RX_CONF4           0x0E
#define ST25R3916_REG_MASK_RX_TIMER      0x0F
#define ST25R3916_REG_NO_RESPONSE_TIMER1 0x10
#define ST25R3916_REG_NO_RESPONSE_TIMER2 0x11
#define ST25R3916_REG_TIMER_EMV_CTRL     0x12
#define ST25R3916_REG_GPT1               0x13
#define ST25R3916_REG_GPT2               0x14
#define ST25R3916_REG_PPON2              0x15
#define ST25R3916_REG_MASK_MAIN_INT      0x16
#define ST25R3916_REG_MASK_TIMER_NFC_INT 0x17
#define ST25R3916_REG_MASK_ERROR_WUP_INT 0x18
#define ST25R3916_REG_MASK_TARGET_INT    0x19
#define ST25R3916_REG_MAIN_INT           0x1A
#define ST25R3916_REG_TIMER_NFC_INT      0x1B
#define ST25R3916_REG_ERROR_INT          0x1C
#define ST25R3916_REG_TARGET_INT         0x1D
#define ST25R3916_REG_FIFO_STATUS1       0x1E
#define ST25R3916_REG_FIFO_STATUS2       0x1F
#define ST25R3916_REG_COLLISION          0x20
#define ST25R3916_REG_PASSIVE_TARGET_STS 0x21
#define ST25R3916_REG_NUM_TX_BYTES1      0x22
#define ST25R3916_REG_NUM_TX_BYTES2      0x23
#define ST25R3916_REG_BIT_RATE_DET       0x24
#define ST25R3916_REG_AD_RESULT          0x25
#define ST25R3916_REG_ANT_TUNE_A         0x26
#define ST25R3916_REG_ANT_TUNE_B         0x27
#define ST25R3916_REG_TX_DRIVER          0x28
#define ST25R3916_REG_PT_MOD             0x29
#define ST25R3916_REG_FIELD_THRESH_ACT   0x2A
#define ST25R3916_REG_FIELD_THRESH_DEACT 0x2B
#define ST25R3916_REG_REGULATOR_CTRL     0x2C
#define ST25R3916_REG_RSSI_RESULT        0x2D
#define ST25R3916_REG_GAIN_RED_STATE     0x2E
#define ST25R3916_REG_CAP_SENSOR_CTRL    0x2F
#define ST25R3916_REG_CAP_SENSOR_RESULT  0x30
#define ST25R3916_REG_AUX_DISPLAY        0x31
#define ST25R3916_REG_OVERSHOOT_CONF1    0x32
#define ST25R3916_REG_OVERSHOOT_CONF2    0x33
#define ST25R3916_REG_UNDERSHOOT_CONF1   0x34
#define ST25R3916_REG_UNDERSHOOT_CONF2   0x35
#define ST25R3916_REG_IC_IDENTITY        0x3F

/* Space-B registers used by the NFC-A receiver configuration. */
#define ST25R3916_REG_B_EMD_SUP_CONF      0x05
#define ST25R3916_REG_B_CORR_CONF1        0x0C
#define ST25R3916_REG_B_CORR_CONF2        0x0D
#define ST25R3916_REG_B_AUX_MOD           0x28
#define ST25R3916_REG_B_RES_AM_MOD        0x2A
#define ST25R3916_REG_B_REGULATOR_DISPLAY 0x2C
#define ST25R3916_REG_B_AWS_CONF1         0x2E
#define ST25R3916_REG_B_AWS_CONF2         0x2F
#define ST25R3916_REG_B_AWS_TIME1         0x34
#define ST25R3916_REG_B_AWS_TIME3         0x36
#define ST25R3916_REG_B_AWS_TIME4         0x37

#define ST25R3916_OP_CTRL_EN        (1 << 7) /**< Enable oscillator and regulator. */
#define ST25R3916_OP_CTRL_RX_EN     (1 << 6) /**< Enable receiver. */
#define ST25R3916_OP_CTRL_TX_EN     (1 << 3) /**< Enable transmitter. */
#define ST25R3916_OP_CTRL_FIELD_ON  0xC8     /**< EN + RX_EN + TX_EN: full RF field on. */
#define ST25R3916_OP_CTRL_FIELD_OFF 0x80     /**< EN only: RF off, oscillator kept stable. */
#define ST25R3916_OP_CTRL_SHUTDOWN  0x00     /**< Oscillator, receiver, and RF field off. */

#define ST25R3916_IO_CONF2_SUP3V    (1 << 7) /**< Supply is in the 2.4 V to 3.6 V range. */
#define ST25R3916_IO_CONF2_MISO_PD2 (1 << 4) /**< MISO pull-down configuration bit 2. */
#define ST25R3916_IO_CONF2_MISO_PD1 (1 << 3) /**< MISO pull-down configuration bit 1. */
#define ST25R3916_IO_CONF2_IO_DRV   (1 << 2) /**< Strong SPI output drive. */

#define ST25R3916_TX_DRIVER_D_RES_MASK 0x0FU /**< RFO series resistance setting. */
#define ST25R3916_REGULATOR_REG_S      0x80U /**< Regulator source-select latch. */

#define ST25R3916_MODE_POLL_NFCA   0x08 /**< ISO 14443-A polling (106 kbps). */
#define ST25R3916_MODE_POLL_NFCB   0x10 /**< ISO 14443-B polling. */
#define ST25R3916_MODE_POLL_NFCF   0x18 /**< FeliCa (NFC-F) polling. */
#define ST25R3916_MODE_POLL_NFCV   0x70 /**< ISO 15693 sub-carrier stream mode. */
#define ST25R3916_MODE_TARGET      0x80 /**< Target mode flag. */
#define ST25R3916_MODE_TARGET_NFCA 0x88 /**< ISO 14443-A target mode. */
#define ST25R3916_MODE_TARGET_NFCA_LISTEN \
  0xC8 /**< Target + bitrate detection + NFC-A listen mode. */

#define ST25R3916_PT_D_106_AC_A       0x01 /**< 106 kbps, Type A. */
#define ST25R3916_PT_D_212_424_1_AC_F 0x02 /**< 212/424 kbps, FeliCa. */
#define ST25R3916_PT_D_AP2P_AC        0x04 /**< Active P2P. */
#define ST25R3916_PT_RFU              0x00

#define ST25R3916_ISO14443A_ANTCL     0x01 /**< Anti-collision loop enabled. */
#define ST25R3916_ISO14443A_NO_TX_PAR 0x80 /**< Suppress TX parity bit. */
#define ST25R3916_ISO14443A_NO_RX_PAR 0x40 /**< Ignore RX parity bit. */

#define ST25R3916_IRQ_MAIN_OSC (1 << 7) /**< Oscillator stable. */
#define ST25R3916_IRQ_MAIN_FWL (1 << 6) /**< FIFO water level reached. */
#define ST25R3916_IRQ_MAIN_TXE (1 << 3) /**< TX end: last bit transmitted. */
#define ST25R3916_IRQ_MAIN_RXS (1 << 5) /**< RX start: first bit received. */
#define ST25R3916_IRQ_MAIN_RXE (1 << 4) /**< RX end: full frame received. */
#define ST25R3916_IRQ_MAIN_COL (1 << 2) /**< Bit collision detected. */

#define ST25R3916_IC_TYPE_MASK  0xF8 /**< IC type field mask (bits [7:3]). */
#define ST25R3916_IC_TYPE_SHIFT 3    /**< Shift to extract IC type. */
#define ST25R3916_IC_REV_MASK   0x07 /**< IC revision mask (bits [2:0]). */

/* ST25R3916B IC type code in IC_IDENTITY[7:3] (e.g. rev 1 raw ID 0x31). */
#define ST25R3916_IC_TYPE_EXP 0x06

#define ST25R3916_IRQ_TGT_PPON2   (1 << 7) /**< PPON2 field-on timer expired. */
#define ST25R3916_IRQ_TGT_SL_WL   (1 << 6) /**< Passive-target slot water level. */
#define ST25R3916_IRQ_TGT_APON    (1 << 5) /**< Anticollision completed and field on. */
#define ST25R3916_IRQ_TGT_RXE_PTA (1 << 4) /**< RX end with an automatic response. */
#define ST25R3916_IRQ_TGT_WU_F    (1 << 3) /**< NFC-F passive-target active state. */
#define ST25R3916_IRQ_TGT_RFU2    (1 << 2)
#define ST25R3916_IRQ_TGT_WU_A_X  (1 << 1) /**< NFC-A passive-target Active* state. */
#define ST25R3916_IRQ_TGT_WU_A    (1 << 0) /**< NFC-A passive-target Active state. */

/* Compatibility names used by older emulators. These sources historically
 * assigned ST25R3916
 * target-register meanings to the wrong bit positions. */
#define ST25R3916_IRQ_TGT_OSCF  ST25R3916_IRQ_TGT_WU_F
#define ST25R3916_IRQ_TGT_SDD_C ST25R3916_IRQ_TGT_RFU2

#define ST25R3916_IRQ_TGT_MASK_NONE 0xFF /**< Mask all target IRQs. */
#define ST25R3916_IRQ_TGT_MASK_ALL  0x00 /**< Unmask all target IRQs. */

#define ST25R3916_PASSIVE_TARGET_FDEL_5           0x50U
#define ST25R3916_PASSIVE_TARGET_DISABLE_106_AUTO 0x01U

#define ST25R3916_FIELD_THRESH_ACT_TRG   0x09 /**< Field activation threshold (~350 mV). */
#define ST25R3916_FIELD_THRESH_DEACT_TRG 0x05 /**< Field deactivation threshold (~200 mV). */

#define ST25R3916_AUX_DISP_EFD_BIT 0x40U /**< efd_o: external field detected (bit 6). */
#define ST25R3916_AUX_DISP_OSC_OK  0x10U /**< osc_ok: crystal is stable (bit 4). */

#define ST25R3916_SPI_PT_MEM_A_WRITE   0xA0 /**< Write PT memory (NFC-A section). */
#define ST25R3916_SPI_PT_MEM_F_WRITE   0xA8 /**< Write PT memory (NFC-F section). */
#define ST25R3916_SPI_PT_MEM_TSN_WRITE 0xAC /**< Write PT memory (TSN section). */
#define ST25R3916_SPI_PT_MEM_READ      0xBF /**< Read PT memory. */
#define ST25R3916_SPI_PT_MEM_A_LEN     15   /**< NFC-A PT memory section length in bytes. */

#define ST25R3916_FIFO_STATUS2_MSB_MASK  0xC0 /**< Bits [7:6] extend the 10-bit FIFO count. */
#define ST25R3916_FIFO_TX_BYTES_LSB_MASK 0x1F /**< Bits [4:0] of nbytes for REG_NUM_TX_BYTES2. */
#define ST25R3916_FIFO_TX_BITS_MASK      0x07 /**< Bits [2:0] for partial-byte bit count. */

#define ST25R3916_SPI_INIT_DELAY_MS 5 /**< SPI stabilization delay after CMD_SET_DEFAULT. */
#define ST25R3916_OSC_POLL_MAX \
  100 /**< Max oscillator-ready poll iterations (10 ms at 100 µs each). */
#define ST25R3916_OSC_POLL_INTERVAL_US 100 /**< Poll interval for oscillator-ready flag. */
#define ST25R3916_REGULATOR_DELAY_MS   6   /**< Settling time after CMD_ADJUST_REGULATORS. */
#define ST25R3916_FIELD_ON_DELAY_MS    10  /**< RF field guard/stabilization before polling. */
#define ST25R3916_FIELD_CYCLE_DELAY_MS 5   /**< Field-off and field-on duration in power-cycle. */
#define ST25R3916_AD_MEAS_DELAY_MS \
  2 /**< ADC settling time after CMD_MEAS_AMPLITUDE / MEAS_PHASE. */
#define ST25R3916_IRQ_TX_POLL_MAX     2000 /**< Max TXE poll iterations (20 ms at 10 µs each). */
#define ST25R3916_IRQ_TX_POLL_US      10   /**< IRQ pin poll interval during TX-end wait. */
#define ST25R3916_FIFO_WAIT_POLL_US   100  /**< FIFO byte-count poll interval. */
#define ST25R3916_DRIVER_CAL_DELAY_MS 1    /**< Settling time after CMD_CALIBRATE_DRIVER. */

/* --- Diagnostic dump constants --- */
#define ST25R3916_REG_DUMP_COUNT  64 /**< Total number of readable registers. */
#define ST25R3916_REG_DUMP_STRIDE 16 /**< Registers per log line in dump output. */

#ifdef __cplusplus
}
#endif

#endif // ST25R3916_REG_H
