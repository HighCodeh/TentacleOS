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
 * @file st25r3916_reg.h
 * @brief ST25R3916 Register Map.
 */
#ifndef ST25R3916_REG_H
#define ST25R3916_REG_H

#define REG_IO_CONF1              0x00
#define REG_IO_CONF2              0x01
#define REG_OP_CTRL               0x02
#define REG_MODE                  0x03
#define REG_BIT_RATE              0x04
#define REG_ISO14443A             0x05
#define REG_ISO14443B             0x06
#define REG_ISO14443B_FELICA      0x07
#define REG_PASSIVE_TARGET        0x08
#define REG_STREAM_MODE           0x09
#define REG_AUX_DEF               0x0A
#define REG_RX_CONF1              0x0B
#define REG_RX_CONF2              0x0C
#define REG_RX_CONF3              0x0D
#define REG_RX_CONF4              0x0E
#define REG_MASK_RX_TIMER         0x0F
#define REG_NO_RESPONSE_TIMER1    0x10
#define REG_NO_RESPONSE_TIMER2    0x11
#define REG_TIMER_EMV_CTRL        0x12
#define REG_GPT1                  0x13
#define REG_GPT2                  0x14
#define REG_PPON2                 0x15
#define REG_MASK_MAIN_INT         0x16
#define REG_MASK_TIMER_NFC_INT    0x17
#define REG_MASK_ERROR_WUP_INT    0x18
#define REG_MASK_TARGET_INT       0x19
#define REG_MAIN_INT              0x1A
#define REG_TIMER_NFC_INT         0x1B
#define REG_ERROR_INT             0x1C
#define REG_TARGET_INT            0x1D
#define REG_FIFO_STATUS1          0x1E
#define REG_FIFO_STATUS2          0x1F
#define REG_COLLISION             0x20
#define REG_PASSIVE_TARGET_STS    0x21
#define REG_NUM_TX_BYTES1         0x22
#define REG_NUM_TX_BYTES2         0x23
#define REG_BIT_RATE_DET          0x24
#define REG_AD_RESULT             0x25
#define REG_ANT_TUNE_A            0x26
#define REG_ANT_TUNE_B            0x27
#define REG_TX_DRIVER             0x28
#define REG_PT_MOD                0x29
#define REG_FIELD_THRESH_ACT      0x2A
#define REG_FIELD_THRESH_DEACT    0x2B
#define REG_REGULATOR_CTRL        0x2C
#define REG_RSSI_RESULT           0x2D
#define REG_GAIN_RED_STATE        0x2E
#define REG_CAP_SENSOR_CTRL       0x2F
#define REG_CAP_SENSOR_RESULT     0x30
#define REG_AUX_DISPLAY           0x31
#define REG_OVERSHOOT_CONF1       0x32
#define REG_OVERSHOOT_CONF2       0x33
#define REG_UNDERSHOOT_CONF1      0x34
#define REG_UNDERSHOOT_CONF2      0x35
#define REG_IC_IDENTITY           0x3F

#define OP_CTRL_EN              (1 << 7)  
#define OP_CTRL_RX_EN           (1 << 6)  
#define OP_CTRL_TX_EN           (1 << 3)  
#define OP_CTRL_FIELD_ON        0xC8      

/* REG_MODE technology bitmask (not an encoded field):
 *   bit 3 (0x08) = NFC-A    bit 4 (0x10) = NFC-B
 *   bit 5 (0x20) = NFC-F    bits4+5 (0x30) = NFC-V
 *   bit 7 (0x80) = target mode   0x88 = NFC-A target */
#define MODE_POLL_NFCA          0x08
#define MODE_POLL_NFCB          0x10
#define MODE_POLL_NFCF          0x20
#define MODE_POLL_NFCV          0x30
#define MODE_TARGET             0x80
#define MODE_TARGET_NFCA        0x88

#define PT_D_106_AC_A           0x01      
#define PT_D_212_424_1_AC_F     0x02      
#define PT_D_AP2P_AC            0x04      
#define PT_RFU                  0x00      

#define ISO14443A_ANTCL         0x01
#define ISO14443A_NO_TX_PAR     0x80      
#define ISO14443A_NO_RX_PAR     0x40      

#define IRQ_MAIN_OSC            (1 << 7)
#define IRQ_MAIN_FWL            (1 << 4)
#define IRQ_MAIN_TXE            (1 << 3)  
#define IRQ_MAIN_RXS            (1 << 5)
#define IRQ_MAIN_RXE            (1 << 2)
#define IRQ_MAIN_COL            (1 << 1)

#define IC_TYPE_MASK            0xF8
#define IC_TYPE_SHIFT           3
#define IC_REV_MASK             0x07

/* IC type observed on ST25R3916 hardware (IC_IDENTITY = 0x15 -> type = 0x02).
 * ST25R3916B returns a different type and uses a different command table. */
#define ST25R3916_IC_TYPE_EXP   0x02

#define IRQ_TGT_WU_A            (1 << 7)  
#define IRQ_TGT_WU_A_X          (1 << 6)  
#define IRQ_TGT_WU_F            (1 << 5)  
#define IRQ_TGT_RFU4            (1 << 4)
#define IRQ_TGT_OSCF            (1 << 3)  
#define IRQ_TGT_SDD_C           (1 << 2)  
#define IRQ_TGT_RFU1            (1 << 1)
#define IRQ_TGT_RFU0            (1 << 0)

#define IRQ_TGT_MASK_NONE       0xFF      
#define IRQ_TGT_MASK_ALL        0x00      

#define FIELD_THRESH_ACT_TRG    0x09      

#define FIELD_THRESH_DEACT_TRG  0x05      

#define SPI_PT_MEM_A_WRITE      0xA0      
#define SPI_PT_MEM_F_WRITE      0xA8      
#define SPI_PT_MEM_TSN_WRITE    0xAC      
#define SPI_PT_MEM_READ         0xBF      
#define SPI_PT_MEM_A_LEN        15        

#endif
