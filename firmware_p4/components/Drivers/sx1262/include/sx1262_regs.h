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

#ifndef SX1262_REGS_H
#define SX1262_REGS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opcodes: Operational Modes ───────────────────────────────────── */
#define SX1262_OP_SET_SLEEP                0x84 /* Enter sleep mode (warm/cold)       */
#define SX1262_OP_SET_STANDBY              0x80 /* Enter standby (RC or XOSC)         */
#define SX1262_OP_SET_FS                   0xC1 /* Enter frequency synthesis mode      */
#define SX1262_OP_SET_TX                   0x83 /* Start transmission                  */
#define SX1262_OP_SET_RX                   0x82 /* Start reception                     */
#define SX1262_OP_STOP_TIMER_ON_PREAMBLE   0x9F /* Stop RX timer on preamble detect    */
#define SX1262_OP_SET_RX_DUTY_CYCLE        0x94 /* RX/sleep duty cycle (wake-on-radio) */
#define SX1262_OP_SET_CAD                  0xC5 /* Start channel activity detection     */
#define SX1262_OP_SET_TX_CONTINUOUS_WAVE   0xD1 /* Continuous wave (test mode)         */
#define SX1262_OP_SET_TX_INFINITE_PREAMBLE 0xD2 /* Infinite preamble (test mode)       */
#define SX1262_OP_SET_REGULATOR_MODE       0x96 /* Select LDO or DC-DC regulator       */
#define SX1262_OP_CALIBRATE                0x89 /* Calibrate RC, PLL, ADC blocks       */
#define SX1262_OP_CALIBRATE_IMAGE          0x98 /* Calibrate image rejection for freq  */
#define SX1262_OP_SET_PA_CONFIG            0x95 /* Configure power amplifier           */
#define SX1262_OP_SET_RX_TX_FALLBACK_MODE  0x93 /* State after TX/RX complete          */

/* ── Opcodes: Register and Buffer Access ─────────────────────────── */
#define SX1262_OP_WRITE_REGISTER 0x0D /* Write to internal register          */
#define SX1262_OP_READ_REGISTER  0x1D /* Read from internal register         */
#define SX1262_OP_WRITE_BUFFER   0x0E /* Write payload to data buffer        */
#define SX1262_OP_READ_BUFFER    0x1E /* Read payload from data buffer       */

/* ── Opcodes: DIO and IRQ Control ────────────────────────────────── */
#define SX1262_OP_SET_DIO_IRQ_PARAMS    0x08 /* Map IRQ flags to DIO1/2/3 pins     */
#define SX1262_OP_GET_IRQ_STATUS        0x12 /* Read pending IRQ flags              */
#define SX1262_OP_CLEAR_IRQ_STATUS      0x02 /* Clear IRQ flags                     */
#define SX1262_OP_SET_DIO2_AS_RF_SWITCH 0x9D /* DIO2 controls external RF switch    */
#define SX1262_OP_SET_DIO3_AS_TCXO      0x97 /* DIO3 powers external TCXO           */

/* ── Opcodes: RF, Modulation and Packet ──────────────────────────── */
#define SX1262_OP_SET_RF_FREQUENCY          0x86 /* Set carrier frequency in Hz         */
#define SX1262_OP_SET_PACKET_TYPE           0x8A /* Select LoRa or FSK modulation       */
#define SX1262_OP_GET_PACKET_TYPE           0x11 /* Read current modulation type         */
#define SX1262_OP_SET_TX_PARAMS             0x8E /* Set TX power and PA ramp time       */
#define SX1262_OP_SET_MODULATION_PARAMS     0x8B /* Set SF, BW, CR, LDRO               */
#define SX1262_OP_SET_PACKET_PARAMS         0x8C /* Set preamble, header, CRC, IQ       */
#define SX1262_OP_SET_CAD_PARAMS            0x88 /* Set CAD detection thresholds        */
#define SX1262_OP_SET_BUFFER_BASE_ADDR      0x8F /* Set TX/RX buffer start offsets      */
#define SX1262_OP_SET_LORA_SYMB_NUM_TIMEOUT 0xA0 /* Set symbol timeout for RX          */

/* ── Opcodes: Status ─────────────────────────────────────────────── */
#define SX1262_OP_GET_STATUS           0xC0 /* Read chip mode and command status    */
#define SX1262_OP_GET_RX_BUFFER_STATUS 0x13 /* Read RX payload length and offset   */
#define SX1262_OP_GET_PACKET_STATUS    0x14 /* Read RSSI, SNR of last packet       */
#define SX1262_OP_GET_RSSI_INST        0x15 /* Read instantaneous channel RSSI     */
#define SX1262_OP_GET_STATS            0x10 /* Read RX packet/error counters       */
#define SX1262_OP_RESET_STATS          0x00 /* Reset RX packet/error counters      */
#define SX1262_OP_GET_DEVICE_ERRORS    0x17 /* Read hardware error bitmask         */
#define SX1262_OP_CLEAR_DEVICE_ERRORS  0x07 /* Clear hardware error bitmask        */

/* ── Standby Modes ───────────────────────────────────────────────── */
#define SX1262_STDBY_RC   0x00 /* 13 MHz RC oscillator                */
#define SX1262_STDBY_XOSC 0x01 /* External crystal oscillator         */

/* ── Packet Type ─────────────────────────────────────────────────── */
#define SX1262_PACKET_TYPE_GFSK 0x00 /* FSK modulation                      */
#define SX1262_PACKET_TYPE_LORA 0x01 /* LoRa modulation                     */

/* ── Regulator Mode ──────────────────────────────────────────────── */
#define SX1262_REGULATOR_LDO  0x00 /* LDO regulator (higher consumption)  */
#define SX1262_REGULATOR_DCDC 0x01 /* DC-DC regulator (lower consumption) */

/* ── PA Ramp Time ────────────────────────────────────────────────── */
#define SX1262_PA_RAMP_10U   0x00 /* 10 us  */
#define SX1262_PA_RAMP_20U   0x01 /* 20 us  */
#define SX1262_PA_RAMP_40U   0x02 /* 40 us  */
#define SX1262_PA_RAMP_80U   0x03 /* 80 us  */
#define SX1262_PA_RAMP_200U  0x04 /* 200 us */
#define SX1262_PA_RAMP_800U  0x05 /* 800 us */
#define SX1262_PA_RAMP_1700U 0x06 /* 1.7 ms */
#define SX1262_PA_RAMP_3400U 0x07 /* 3.4 ms */

/* ── LoRa Spreading Factor ────────────────────────────────────────── */
#define SX1262_LORA_SF5  0x05 /* Fastest, shortest range  */
#define SX1262_LORA_SF6  0x06 /* Requires implicit header */
#define SX1262_LORA_SF7  0x07 /* Default for most apps   */
#define SX1262_LORA_SF8  0x08
#define SX1262_LORA_SF9  0x09
#define SX1262_LORA_SF10 0x0A
#define SX1262_LORA_SF11 0x0B /* LDRO required at BW<=125kHz */
#define SX1262_LORA_SF12 0x0C /* Slowest, longest range  */

/* ── LoRa Bandwidth (values are non-linear — use sx1262_bw_to_hz()) ── */
#define SX1262_LORA_BW_7   0x00 /* 7.81 kHz  */
#define SX1262_LORA_BW_10  0x08 /* 10.42 kHz */
#define SX1262_LORA_BW_15  0x01 /* 15.63 kHz */
#define SX1262_LORA_BW_20  0x09 /* 20.83 kHz */
#define SX1262_LORA_BW_31  0x02 /* 31.25 kHz */
#define SX1262_LORA_BW_41  0x0A /* 41.67 kHz */
#define SX1262_LORA_BW_62  0x03 /* 62.50 kHz */
#define SX1262_LORA_BW_125 0x04 /* 125 kHz   */
#define SX1262_LORA_BW_250 0x05 /* 250 kHz   */
#define SX1262_LORA_BW_500 0x06 /* 500 kHz   */

/* ── LoRa Coding Rate ────────────────────────────────────────────── */
#define SX1262_LORA_CR_4_5 0x01 /* 4/5 — least redundancy  */
#define SX1262_LORA_CR_4_6 0x02 /* 4/6                     */
#define SX1262_LORA_CR_4_7 0x03 /* 4/7                     */
#define SX1262_LORA_CR_4_8 0x04 /* 4/8 — most redundancy   */

/* ── LoRa Header Type ────────────────────────────────────────────── */
#define SX1262_LORA_HEADER_EXPLICIT 0x00 /* Header with payload len, CR, CRC */
#define SX1262_LORA_HEADER_IMPLICIT 0x01 /* No header — params must match     */

/* ── IRQ Flag Masks ──────────────────────────────────────────────── */
#define SX1262_IRQ_TX_DONE           (1U << 0) /* Transmission complete       */
#define SX1262_IRQ_RX_DONE           (1U << 1) /* Packet received             */
#define SX1262_IRQ_PREAMBLE_DETECTED (1U << 2) /* LoRa preamble detected      */
#define SX1262_IRQ_SYNC_WORD_VALID   (1U << 3) /* Sync word matched           */
#define SX1262_IRQ_HEADER_VALID      (1U << 4) /* Valid header received        */
#define SX1262_IRQ_HEADER_ERR        (1U << 5) /* Header CRC error            */
#define SX1262_IRQ_CRC_ERR           (1U << 6) /* Payload CRC error           */
#define SX1262_IRQ_CAD_DONE          (1U << 7) /* CAD scan complete           */
#define SX1262_IRQ_CAD_DETECTED      (1U << 8) /* LoRa activity detected      */
#define SX1262_IRQ_TIMEOUT           (1U << 9) /* RX or TX timeout            */
#define SX1262_IRQ_ALL               0x03FFU   /* All flags combined          */

/* ── LoRa Sync Word Registers ────────────────────────────────────── */
#define SX1262_REG_LORA_SYNC_WORD_MSB 0x0740 /* Sync word byte 0            */
#define SX1262_REG_LORA_SYNC_WORD_LSB 0x0741 /* Sync word byte 1            */
#define SX1262_LORA_SYNC_WORD_PUBLIC  0x3444 /* LoRaWAN public network      */
#define SX1262_LORA_SYNC_WORD_PRIVATE 0x1424 /* Private/Meshtastic network  */

/* ── Rx Gain Register ────────────────────────────────────────────── */
#define SX1262_REG_RX_GAIN 0x08AC /* RX gain boost control       */

/* ── Workaround Registers (DS Section 15) ────────────────────────── */
#define SX1262_REG_TX_MODULATION   0x0889 /* W1: BW500 sensitivity fix — bit 2         */
#define SX1262_REG_TX_CLAMP_CONFIG 0x08D8 /* W2: PA over-clamp fix — bits 4:1          */
#define SX1262_REG_RTC_CTRL        0x0902 /* W3: Implicit header timeout — bit 0       */
#define SX1262_REG_EVT_CLR         0x0944 /* W3: Event clear for RTC stop — bit 1      */
#define SX1262_REG_IQ_POLARITY     0x0736 /* W4: Inverted IQ polarity fix — bit 2      */

/* ── Sleep Configuration ─────────────────────────────────────────── */
#define SX1262_SLEEP_COLD_START 0x00 /* Lose all config, full re-init needed */
#define SX1262_SLEEP_WARM_START 0x04 /* Retain config, fast wakeup           */

/* ── Fallback Mode After TX/RX ───────────────────────────────────── */
#define SX1262_FALLBACK_STDBY_RC   0x20 /* Return to standby RC        */
#define SX1262_FALLBACK_STDBY_XOSC 0x30 /* Return to standby XOSC      */
#define SX1262_FALLBACK_FS         0x40 /* Return to frequency synth   */

/* ── Calibration Block Bitmask ───────────────────────────────────── */
#define SX1262_CALIBRATE_RC64K      (1U << 0) /* 64 kHz RC oscillator     */
#define SX1262_CALIBRATE_RC13M      (1U << 1) /* 13 MHz RC oscillator     */
#define SX1262_CALIBRATE_PLL        (1U << 2) /* PLL synthesizer          */
#define SX1262_CALIBRATE_ADC_PULSE  (1U << 3) /* ADC pulse               */
#define SX1262_CALIBRATE_ADC_BULK_N (1U << 4) /* ADC bulk N-path         */
#define SX1262_CALIBRATE_ADC_BULK_P (1U << 5) /* ADC bulk P-path         */
#define SX1262_CALIBRATE_IMAGE      (1U << 6) /* Image rejection          */
#define SX1262_CALIBRATE_ALL        0x7F      /* All blocks              */

/* ── Status Byte Parsing ─────────────────────────────────────────── */
#define SX1262_STATUS_CHIP_MODE_MASK   0x70 /* Bits 6:4 = chip operating mode  */
#define SX1262_STATUS_CHIP_MODE_SHIFT  4
#define SX1262_STATUS_CMD_STATUS_MASK  0x0E /* Bits 3:1 = last command status  */
#define SX1262_STATUS_CMD_STATUS_SHIFT 1

/* ── Chip Mode Values (from status byte bits 6:4) ────────────────── */
#define SX1262_CHIP_MODE_UNUSED     0x00 /* Not used                    */
#define SX1262_CHIP_MODE_STDBY_RC   0x02 /* Standby with RC oscillator  */
#define SX1262_CHIP_MODE_STDBY_XOSC 0x03 /* Standby with crystal        */
#define SX1262_CHIP_MODE_FS         0x04 /* Frequency synthesis         */
#define SX1262_CHIP_MODE_RX         0x05 /* Receiving                   */
#define SX1262_CHIP_MODE_TX         0x06 /* Transmitting                */

/* ── Timing Constants ────────────────────────────────────────────── */
#define SX1262_WAIT_BUSY_TIMEOUT_MS 100 /* Max wait for BUSY LOW       */
#define SX1262_RESET_HOLD_MS        2   /* NRESET LOW hold time        */
#define SX1262_RESET_WAIT_MS        20  /* Wait after NRESET HIGH      */

/* ── TCXO Supply Voltage ─────────────────────────────────────────── */
#define SX1262_TCXO_1V6 0x00 /* 1.6V */
#define SX1262_TCXO_1V7 0x01 /* 1.7V */
#define SX1262_TCXO_1V8 0x02 /* 1.8V */
#define SX1262_TCXO_2V2 0x03 /* 2.2V */
#define SX1262_TCXO_2V4 0x04 /* 2.4V */
#define SX1262_TCXO_2V7 0x05 /* 2.7V */
#define SX1262_TCXO_3V0 0x06 /* 3.0V */
#define SX1262_TCXO_3V3 0x07 /* 3.3V */

/* ── PA Configuration (optimal for +22 dBm on SX1262) ────────────── */
#define SX1262_PA_DUTY_CYCLE_22DBM  0x04 /* PA duty cycle for +22dBm    */
#define SX1262_PA_HP_MAX_22DBM      0x07 /* HP PA max power             */
#define SX1262_PA_DEVICE_SEL_SX1262 0x00 /* Device select: SX1262       */
#define SX1262_PA_LUT_1             0x01 /* PA lookup table 1           */

/* ── Antenna Switch Modes ────────────────────────────────────────── */
#define SX1262_ANT_OFF 0x00 /* Both TX/RX switches off     */
#define SX1262_ANT_RX  0x01 /* RX path enabled             */
#define SX1262_ANT_TX  0x02 /* TX path enabled             */

/**
 * @brief Convert LoRa BW enum (DS Table 13-48) to bandwidth in Hz.
 *
 * BW enum values are non-linear, so direct comparison is unsafe.
 * Use this function for any BW magnitude comparison.
 */
static inline uint32_t sx1262_bw_to_hz(uint8_t bw) {
  switch (bw) {
    case SX1262_LORA_BW_7:
      return 7810;
    case SX1262_LORA_BW_10:
      return 10420;
    case SX1262_LORA_BW_15:
      return 15630;
    case SX1262_LORA_BW_20:
      return 20830;
    case SX1262_LORA_BW_31:
      return 31250;
    case SX1262_LORA_BW_41:
      return 41670;
    case SX1262_LORA_BW_62:
      return 62500;
    case SX1262_LORA_BW_125:
      return 125000;
    case SX1262_LORA_BW_250:
      return 250000;
    case SX1262_LORA_BW_500:
      return 500000;
    default:
      return 0;
  }
}

/**
 * @brief Check if a BW enum value is valid (DS Table 13-48).
 */
static inline bool sx1262_bw_is_valid(uint8_t bw) {
  return sx1262_bw_to_hz(bw) != 0;
}

#ifdef __cplusplus
}
#endif

#endif // SX1262_REGS_H
