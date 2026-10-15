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

#ifndef ST25R3916_CMD_H
#define ST25R3916_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#define ST25R3916_CMD_SET_DEFAULT    0xC1 /**< Set all registers to default. */
#define ST25R3916_CMD_STOP_ALL       0xC2 /**< Stop activity, clear FIFO/IRQ. */
#define ST25R3916_CMD_TX_WITH_CRC    0xC4 /**< Transmit FIFO with CRC. */
#define ST25R3916_CMD_TX_WO_CRC      0xC5 /**< Transmit FIFO without CRC. */
#define ST25R3916_CMD_TX_REQA        0xC6 /**< Transmit ISO 14443-A REQA. */
#define ST25R3916_CMD_TX_WUPA        0xC7 /**< Transmit ISO 14443-A WUPA. */
#define ST25R3916_CMD_NFC_INIT_COL   0xC8 /**< Initial RF collision avoidance. */
#define ST25R3916_CMD_NFC_RESP_COL   0xC9 /**< Response RF collision avoidance. */
#define ST25R3916_CMD_NFC_RESP_COL_N 0xCA /**< Response RF collision avoidance (n). */
#define ST25R3916_CMD_GOTO_SENSE     0xCD /**< Enter sense state. */
#define ST25R3916_CMD_GOTO_SLEEP     0xCE /**< Enter sleep state. */

#define ST25R3916_CMD_MASK_RX_DATA      0xD0 /**< Mask RX data. */
#define ST25R3916_CMD_UNMASK_RX_DATA    0xD1 /**< Unmask RX data. */
#define ST25R3916_CMD_MEAS_AMPLITUDE    0xD3 /**< Trigger ADC amplitude measurement. */
#define ST25R3916_CMD_RESET_RX_GAIN     0xD5 /**< Reset RX gain. */
#define ST25R3916_CMD_ADJUST_REGULATORS 0xD6 /**< Calibrate internal regulators. */
#define ST25R3916_CMD_CALIBRATE_DRIVER  0xD8 /**< Calibrate TX driver. */
#define ST25R3916_CMD_MEAS_PHASE        0xD9 /**< Trigger ADC phase measurement. */
#define ST25R3916_CMD_CLEAR_RSSI        0xDA /**< Clear RSSI measurement. */
#define ST25R3916_CMD_CLEAR_FIFO        0xDB /**< Clear FIFO and FIFO status. */
#define ST25R3916_CMD_TRANSPARENT_MODE  0xDC /**< Enter transparent (bit-stream) mode. */
#define ST25R3916_CMD_MEAS_VDD          0xDF /**< Trigger VDD measurement. */

#define ST25R3916_CMD_START_GP_TIMER      0xE0 /**< Start general-purpose timer. */
#define ST25R3916_CMD_START_WU_TIMER      0xE1 /**< Start wake-up timer. */
#define ST25R3916_CMD_START_MASK_RX_TIMER 0xE2 /**< Start mask-receive timer. */
#define ST25R3916_CMD_START_NRT           0xE3 /**< Start no-response timer. */

#define ST25R3916_SPI_FIFO_LOAD 0x80 /**< SPI Operation: Write to FIFO. */
#define ST25R3916_SPI_FIFO_READ 0x9F /**< SPI Operation: Read from FIFO. */
#define ST25R3916_SPI_CMD_MODE  0xC0 /**< SPI Operation: Direct Command mode. */
#define ST25R3916_SPI_SPACE_B   0xFB /**< Prefix for register space B access. */

#define ST25R3916_SEL_CL1 0x93 /**< SELECT Cascade Level 1. */
#define ST25R3916_SEL_CL2 0x95 /**< SELECT Cascade Level 2. */
#define ST25R3916_SEL_CL3 0x97 /**< SELECT Cascade Level 3. */

#ifdef __cplusplus
}
#endif

#endif // ST25R3916_CMD_H
