/**
 * @file st25r3916_cmd.h
 * @brief ST25R3916 Direct Commands.
 *
 * v5 FIX: Command codes verified against ST25R3916
 * datasheet DocID 031020 Rev 3, Table 19.
 *
 * BUGS FIXED from v4:
 *  - CMD_MEAS_AMPLITUDE was 0xD8, correct = 0xD3
 *  - CMD_CALIBRATE_DRIVER was 0xD7, correct = 0xD8
 *  - Several other measurement/timer codes off-by-one
 *  - Added missing target-mode commands
 *
 * NOTE: These are for ST25R3916 (IC type 0x02, IC_IDENTITY = 0x15).
 * ST25R3916B returns a different IC type and has a different command table.
 */
#ifndef ST25R3916_CMD_H
#define ST25R3916_CMD_H

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

#define CMD_STOP_ALL            CMD_CLEAR
#define CMD_CLEAR_FIFO          CMD_CLEAR  

#define CMD_MASK_RX_DATA        0xD0  
#define CMD_UNMASK_RX_DATA      0xD1  
#define CMD_AM_MOD_STATE_CHG    0xD2  
#define CMD_MEAS_AMPLITUDE      0xD3  
#define CMD_RESET_RX_GAIN       0xD5  
#define CMD_ADJUST_REGULATORS   0xD6  

#define CMD_CALIBRATE_DRIVER    0xD8  
#define CMD_MEAS_PHASE          0xD9  
#define CMD_CLEAR_RSSI          0xDA  
#define CMD_TRANSPARENT_MODE    0xDB  
#define CMD_CALIBRATE_C_SENSOR  0xDC  
#define CMD_MEAS_CAPACITANCE    0xDD  
#define CMD_MEAS_VDD            0xDE  

#define CMD_START_GP_TIMER      0xDF  
#define CMD_START_WU_TIMER      0xE0  
#define CMD_START_MASK_RX_TIMER 0xE1  
#define CMD_START_NRT           0xE2  
#define CMD_START_PPON2_TIMER   0xE3  

#define CMD_TEST_ACCESS         0xFC  

#define SPI_FIFO_LOAD           0x80
#define SPI_FIFO_READ           0x9F

#define SEL_CL1                 0x93
#define SEL_CL2                 0x95
#define SEL_CL3                 0x97

#endif
