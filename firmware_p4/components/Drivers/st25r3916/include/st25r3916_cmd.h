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

#ifndef ST25R3916_CMD_H
#define ST25R3916_CMD_H

/* ── Core Commands ── */
#define CMD_SET_DEFAULT         0xC0 
#define CMD_CLEAR               0xC2  
#define CMD_TX_WITH_CRC         0xC4  
#define CMD_TX_WO_CRC           0xC5  
#define CMD_TX_REQA             0xC6 
#define CMD_TX_WUPA             0xC7 
#define CMD_NFC_INITIAL_RF_COL  0xC8 
#define CMD_NFC_RESPONSE_RF_COL 0xC9 
#define CMD_NFC_RESPONSE_RF_N   0xCA  
#define CMD_GOTO_SENSE          0xCD 
#define CMD_GOTO_SLEEP          0xCE  

/* Legacy aliases */
#define CMD_STOP_ALL            CMD_CLEAR
#define CMD_CLEAR_FIFO          CMD_CLEAR

/* ── Data/Modulation Commands ── */
#define CMD_MASK_RX_DATA        0xD0
#define CMD_UNMASK_RX_DATA      0xD1
#define CMD_AM_MOD_STATE_CHG    0xD2
#define CMD_MEAS_AMPLITUDE      0xD3
#define CMD_RESET_RX_GAIN       0xD5
#define CMD_ADJUST_REGULATORS   0xD6

/* ── Calibration/Measurement ── */
#define CMD_CALIBRATE_DRIVER    0xD8
#define CMD_MEAS_PHASE          0xD9
#define CMD_CLEAR_RSSI          0xDA
#define CMD_TRANSPARENT_MODE    0xDB
#define CMD_CALIBRATE_C_SENSOR  0xDC
#define CMD_MEAS_CAPACITANCE    0xDD
#define CMD_MEAS_VDD            0xDE

/* ── Timer Commands (0xDF-0xE3) ── */
#define CMD_START_GP_TIMER      0xDF
#define CMD_START_WU_TIMER      0xE0
#define CMD_START_MASK_RX_TIMER 0xE1
#define CMD_START_NRT           0xE2
#define CMD_START_PPON2_TIMER   0xE3

/* ── Special Commands ── */
#define CMD_TEST_ACCESS         0xFC

/* ── SPI FIFO Prefixes ── */
#define SPI_FIFO_LOAD           0x80
#define SPI_FIFO_READ           0x9F

/* ── Anti-collision SELECT commands (ISO14443-3) ── */
#define SEL_CL1                 0x93
#define SEL_CL2                 0x95
#define SEL_CL3                 0x97

#endif
