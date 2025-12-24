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

// =============================================================================
// STROBE COMMANDS (One-way commands via SPI)
// =============================================================================
#define CC1101_SRES         0x30      // Reseta o chip CC1101
#define CC1101_SFSTXON      0x31      // Calibra o sintetizador de frequência e ativa
#define CC1101_SXOFF        0x32      // Desliga o oscilador de cristal
#define CC1101_SCAL         0x33      // Calibra o sintetizador de frequência e desliga
#define CC1101_SRX          0x34      // Ativa o modo de Recepção (RX)
#define CC1101_STX          0x35      // Ativa o modo de Transmissão (TX)
#define CC1101_SIDLE        0x36      // Sai do modo RX/TX, entra em Idle
#define CC1101_SWOR         0x38      // Ativa o Wake on Radio
#define CC1101_SPWD         0x39      // Entra em modo de baixo consumo (Power down)
#define CC1101_SFRX         0x3A      // Limpa o buffer FIFO de Recebimento
#define CC1101_SFTX         0x3B      // Limpar o buffer FIFO de Transmissão
#define CC1101_SWORRST      0x3C      // Reseta o relógio de tempo real do WOR
#define CC1101_SNOP         0x3D      // Nenhuma operação

// =============================================================================
// CONFIG REGISTERS (Write and Read)
// =============================================================================
#define CC1101_IOCFG2       0x00      // Configuração do pino GDO2
#define CC1101_IOCFG1       0x01      // Configuração do pino GDO1
#define CC1101_IOCFG0       0x02      // Configuração do pino GDO0
#define CC1101_FIFOTHR      0x03      // Limiares de FIFO RX e TX
#define CC1101_SYNC1        0x04      // Palavra de sincronismo, byte alto
#define CC1101_SYNC0        0x05      // Palavra de sincronismo, byte baixo
#define CC1101_PKTLEN       0x06      // Comprimento do pacote
#define CC1101_PKTCTRL1     0x07      // Controle de automação de pacotes
#define CC1101_PKTCTRL0     0x08      // Controle de automação de pacotes
#define CC1101_ADDR         0x09      // Endereço do dispositivo
#define CC1101_CHANNR       0x0A      // Número do canal
#define CC1101_FSCTRL1      0x0B      // Controle do sintetizador de frequência
#define CC1101_FSCTRL0      0x0C      // Controle do sintetizador de frequência
#define CC1101_FREQ2        0x0D      // Palavra de controle de frequência, byte alto
#define CC1101_FREQ1        0x0E      // Palavra de controle de frequência, byte médio
#define CC1101_FREQ0        0x0F      // Palavra de controle de frequência, byte baixo
#define CC1101_MDMCFG4      0x10      // Configuração do modem
#define CC1101_MDMCFG3      0x11      // Configuração do modem
#define CC1101_MDMCFG2      0x12      // Configuração do modem
#define CC1101_MDMCFG1      0x13      // Configuração do modem
#define CC1101_MDMCFG0      0x14      // Configuração do modem
#define CC1101_DEVIATN      0x15      // Configuração de desvio do modem
#define CC1101_MCSM2        0x16      // Configuração da máquina de estados do rádio
#define CC1101_MCSM1        0x17      // Configuração da máquina de estados do rádio
#define CC1101_MCSM0        0x18      // Configuração da máquina de estados do rádio
#define CC1101_FOCCFG       0x19      // Configuração de compensação de offset de freq.
#define CC1101_BSCFG        0x1A      // Configuração de sincronização de bits
#define CC1101_AGCCTRL2     0x1B      // Controle de AGC
#define CC1101_AGCCTRL1     0x1C      // Controle de AGC
#define CC1101_AGCCTRL0     0x1D      // Controle de AGC
#define CC1101_WOREVT1      0x1E      // Byte alto de timeout do Evento 0
#define CC1101_WOREVT0      0x1F      // Byte baixo de timeout do Evento 0
#define CC1101_WORCTRL      0x20      // Controle de Wake On Radio
#define CC1101_FREND1       0x21      // Configuração de front end de recepção
#define CC1101_FREND0       0x22      // Configuração de front end de transmissão
#define CC1101_FSCAL3       0x23      // Calibração do sintetizador de frequência
#define CC1101_FSCAL2       0x24      // Calibração do sintetizador de frequência
#define CC1101_FSCAL1       0x25      // Calibração do sintetizador de frequência
#define CC1101_FSCAL0       0x26      // Calibração do sintetizador de frequência
#define CC1101_RCCTRL1      0x27      // Configuração do oscilador RC
#define CC1101_RCCTRL0      0x28      // Configuração do oscilador RC
#define CC1101_FSTEST       0x29      // Controle de teste do sintetizador
#define CC1101_PTEST        0x2A      // Teste de produção
#define CC1101_AGCTEST      0x2B      // Teste de AGC
#define CC1101_TEST2        0x2C      // Configuração de teste variada
#define CC1101_TEST1        0x2D      // Configuração de teste variada
#define CC1101_TEST0        0x2E      // Configuração de teste variada

// =============================================================================
// REGISTER STATUS (Readonly - Require burst bit 0x40)
// =============================================================================
#define CC1101_PARTNUM      0x30      // Número da parte
#define CC1101_VERSION      0x31      // Versão do chip
#define CC1101_FREQEST      0x32      // Estimativa de frequência
#define CC1101_LQI          0x33      // Indicador de qualidade de link
#define CC1101_RSSI         0x34      // Força do sinal recebido
#define CC1101_MARCSTATE    0x35      // Estado atual da máquina de rádio
#define CC1101_WORTIME1     0x36      // Tempo WOR, byte alto
#define CC1101_WORTIME0     0x37      // Tempo WOR, byte baixo
#define CC1101_PKTSTATUS    0x38      // Status do pacote e GDOs
#define CC1101_VCO_VC_DAC   0x39      // Corrente do PLL e DAC
#define CC1101_TXBYTES      0x3A      // Número de bytes na FIFO TX
#define CC1101_RXBYTES      0x3B      // Número de bytes na FIFO RX
#define CC1101_PATABLE      0x3E      // Tabela de potência de saída
#define CC1101_TXFIFO       0x3F      // Acesso à FIFO de Transmissão
#define CC1101_RXFIFO       0x3F      // Acesso à FIFO de Recepção


void cc1101_init(void);
void cc1101_set_frequency(uint32_t freq_hz);
void cc1101_strobe(uint8_t cmd);
void cc1101_write_reg(uint8_t reg, uint8_t val);
uint8_t cc1101_read_reg(uint8_t reg);
void cc1101_write_burst(uint8_t reg, const uint8_t *buf, uint8_t len);
float cc1101_convert_rssi(uint8_t rssi_raw);
void cc1101_spectrum_task(void *pvParameters);
void cc1101_send_data(const uint8_t *data, size_t len);
void cc1101_enter_receive(void);
void cc1101_enable_async_mode(uint32_t freq_hz);

#endif // CC1101_H

