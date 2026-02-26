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


#ifndef CC1101_H
#define CC1101_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Config registers (Write and Read)
#define CC1101_IOCFG2       0x00        // GDO2 output pin configuration
#define CC1101_IOCFG1       0x01        // GDO1 output pin configuration
#define CC1101_IOCFG0       0x02        // GDO0 output pin configuration
#define CC1101_FIFOTHR      0x03        // RX FIFO and TX FIFO thresholds
#define CC1101_SYNC1        0x04        // Sync word, high INT8U
#define CC1101_SYNC0        0x05        // Sync word, low INT8U
#define CC1101_PKTLEN       0x06        // Packet length
#define CC1101_PKTCTRL1     0x07        // Packet automation control
#define CC1101_PKTCTRL0     0x08        // Packet automation control
#define CC1101_ADDR         0x09        // Device address
#define CC1101_CHANNR       0x0A        // Channel number
#define CC1101_FSCTRL1      0x0B        // Frequency synthesizer control
#define CC1101_FSCTRL0      0x0C        // Frequency synthesizer control
#define CC1101_FREQ2        0x0D        // Frequency control word, high INT8U
#define CC1101_FREQ1        0x0E        // Frequency control word, middle INT8U
#define CC1101_FREQ0        0x0F        // Frequency control word, low INT8U
#define CC1101_MDMCFG4      0x10        // Modem configuration
#define CC1101_MDMCFG3      0x11        // Modem configuration
#define CC1101_MDMCFG2      0x12        // Modem configuration
#define CC1101_MDMCFG1      0x13        // Modem configuration
#define CC1101_MDMCFG0      0x14        // Modem configuration
#define CC1101_DEVIATN      0x15        // Modem deviation setting
#define CC1101_MCSM2        0x16        // Main Radio Control State Machine configuration
#define CC1101_MCSM1        0x17        // Main Radio Control State Machine configuration
#define CC1101_MCSM0        0x18        // Main Radio Control State Machine configuration
#define CC1101_FOCCFG       0x19        // Frequency Offset Compensation configuration
#define CC1101_BSCFG        0x1A        // Bit Synchronization configuration
#define CC1101_AGCCTRL2     0x1B        // AGC control
#define CC1101_AGCCTRL1     0x1C        // AGC control
#define CC1101_AGCCTRL0     0x1D        // AGC control
#define CC1101_WOREVT1      0x1E        // High INT8U Event 0 timeout
#define CC1101_WOREVT0      0x1F        // Low INT8U Event 0 timeout
#define CC1101_WORCTRL      0x20        // Wake On Radio control
#define CC1101_FREND1       0x21        // Front end RX configuration
#define CC1101_FREND0       0x22        // Front end TX configuration
#define CC1101_FSCAL3       0x23        // Frequency synthesizer calibration
#define CC1101_FSCAL2       0x24        // Frequency synthesizer calibration
#define CC1101_FSCAL1       0x25        // Frequency synthesizer calibration
#define CC1101_FSCAL0       0x26        // Frequency synthesizer calibration
#define CC1101_RCCTRL1      0x27        // RC oscillator configuration
#define CC1101_RCCTRL0      0x28        // RC oscillator configuration
#define CC1101_FSTEST       0x29        // Frequency synthesizer calibration control
#define CC1101_PTEST        0x2A        // Production test
#define CC1101_AGCTEST      0x2B        // AGC test
#define CC1101_TEST2        0x2C        // Various test settings
#define CC1101_TEST1        0x2D        // Various test settings
#define CC1101_TEST0        0x2E        // Various test settings

// Strobe commands
#define CC1101_SRES         0x30        // Reset chip.
#define CC1101_SFSTXON      0x31        // Enable and calibrate frequency synthesizer.
#define CC1101_SXOFF        0x32        // Turn off crystal oscillator.
#define CC1101_SCAL         0x33        // Calibrate frequency synthesizer and turn it off.
#define CC1101_SRX          0x34        // Enable RX.
#define CC1101_STX          0x35        // Enable TX.
#define CC1101_SIDLE        0x36        // Exit RX / TX, turn off frequency synthesizer.
#define CC1101_SAFC         0x37        // Perform AFC adjustment of the frequency synthesizer.
#define CC1101_SWOR         0x38        // Start automatic RX polling sequence (Wake-on-Radio).
#define CC1101_SPWD         0x39        // Enter power down mode when CSn goes high.
#define CC1101_SFRX         0x3A        // Flush the RX FIFO buffer.
#define CC1101_SFTX         0x3B        // Flush the TX FIFO buffer.
#define CC1101_SWORRST      0x3C        // Reset real time clock.
#define CC1101_SNOP         0x3D        // No operation.

// STATUS REGISTERS (Readonly - Requires burst bit 0x40)
#define CC1101_PARTNUM      0x30        // Part number
#define CC1101_VERSION      0x31        // Chip ID
#define CC1101_FREQEST      0x32        // Frequency offset estimate
#define CC1101_LQI          0x33        // Link quality indicator
#define CC1101_RSSI         0x34        // Received signal strength indicator
#define CC1101_MARCSTATE    0x35        // Main radio control state machine state
#define CC1101_WORTIME1     0x36        // High byte of WOR time
#define CC1101_WORTIME0     0x37        // Low byte of WOR time
#define CC1101_PKTSTATUS    0x38        // Current GDOx status and packet status
#define CC1101_VCO_VC_DAC   0x39        // Current setting from PLL calibration module
#define CC1101_TXBYTES      0x3A        // Underflow and number of bytes in TX FIFO
#define CC1101_RXBYTES      0x3B        // Overflow and number of bytes in RX FIFO

// PATABLE, TXFIFO, RXFIFO
#define CC1101_PATABLE      0x3E        // PATABLE access
#define CC1101_TXFIFO       0x3F        // TX FIFO access
#define CC1101_RXFIFO       0x3F        // RX FIFO access


void cc1101_init(void);
void cc1101_set_frequency(uint32_t freq_hz);
void cc1101_strobe(uint8_t cmd);
void cc1101_write_reg(uint8_t reg, uint8_t val);
uint8_t cc1101_read_reg(uint8_t reg);
void cc1101_write_burst(uint8_t reg, const uint8_t *buf, uint8_t len);
void cc1101_read_burst(uint8_t reg, uint8_t *buf, uint8_t len);
float cc1101_convert_rssi(uint8_t rssi_raw);
void cc1101_spectrum_task(void *pvParameters);
void cc1101_send_data(const uint8_t *data, size_t len);
void cc1101_enter_rx_mode(void);
void cc1101_enter_tx_mode(void);
void cc1101_enable_async_mode(uint32_t freq_hz); // ASK (OOK) Default
void cc1101_enable_fsk_mode(uint32_t freq_hz);   // FSK

// Preset Management
typedef enum {
    CC1101_PRESET_IDLE = 0,
    CC1101_PRESET_OOK_270KHZ = 1,
    CC1101_PRESET_OOK_650KHZ = 2,
    CC1101_PRESET_2FSK_2KHZ = 3,
    CC1101_PRESET_2FSK_47KHZ = 4,
    CC1101_PRESET_2FSK_95KHZ = 5,
    CC1101_PRESET_OOK_800KHZ = 6, 
} cc1101_preset_t;

void cc1101_set_preset(cc1101_preset_t preset, uint32_t freq_hz);
uint8_t cc1101_get_active_preset_id(void);

// New Tuning & Configuration Functions
void cc1101_set_rx_bandwidth(float khz);
void cc1101_set_data_rate(float baud);
void cc1101_set_deviation(float dev);
void cc1101_set_modulation(uint8_t modulation); // 0=2-FSK, 1=GFSK, 2=ASK/OOK, 3=4-FSK, 4=MSK
void cc1101_set_pa(int dbm);
void cc1101_set_channel(uint8_t channel);
void cc1101_set_chsp(float khz); // Channel Spacing
void cc1101_calibrate(void);
void cc1101_set_sync_mode(uint8_t mode);
void cc1101_set_fec(bool enable);
void cc1101_set_preamble(uint8_t preamble_bytes);
void cc1101_set_dc_filter_off(bool disable);
void cc1101_set_manchester(bool enable);

#endif // CC1101_H
