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


#include <assert.h>
#include <string.h>
#include <stdio.h>
#undef memcpy
#define memcpy nrf_test_memcpy
#include "app.c"
#undef memcpy

static uint8_t registers[32][5];
static uint8_t completion;
static bool missing;
static bool payload_written;
static bool ce;
static char config_text[96];
static esp_err_t fake_transfer(uint32_t session, const uint8_t *tx, uint8_t *rx, size_t len) {
  assert(session == 1);
  memset(rx, 0, len);
  uint8_t cmd = tx[0];
  uint8_t reg = cmd & 31;
  if (missing) { memset(rx, 255, len); return ESP_OK; }
  if (cmd < 32) {
    for(size_t i=1;i<len;i++) rx[i]=registers[reg][i-1];
  } else if ((cmd & 0xe0) == 0x20) {
    for(size_t i=1;i<len;i++) {
      if(reg==NRF_REG_STATUS) registers[reg][i-1] &= ~tx[i];
      else registers[reg][i-1]=tx[i];
    }
  } else if(cmd==NRF_CMD_PAYLOAD) { assert(len==17); payload_written=true; }
  return ESP_OK;
}
static esp_err_t fake_gpio(uint32_t session, uint8_t pin, bool value) {
  assert(session==1 && pin==NRF_CE_PIN);
  ce=value;
  if(value) { assert(payload_written); registers[NRF_REG_STATUS][0]=completion; }
  return ESP_OK;
}
static void fake_delay(uint32_t delay) { (void)delay; }
static void fake_log(tos_log_level_t level, const char *tag, const char *format, ...) {
  (void)level; (void)tag; (void)format;
}
static int fake_stop(void) { return false; }
static esp_err_t fake_text(tos_ui_obj_t obj, const char *text) {
  (void)obj;
  snprintf(config_text,sizeof(config_text),"%s",text);
  return ESP_OK;
}
int main(void) {
  const tos_header_api_t header={.spi_transfer=fake_transfer,.gpio_write=fake_gpio};
  const tos_ui_api_t ui={.set_text=fake_text};
  const tos_api_t api={.header=&header,.ui=&ui,.log=fake_log,.delay_ms=fake_delay,.should_stop=fake_stop};
  s_api=&api; s_session=1;
  missing=true;
  assert(initialize_radio()==ESP_ERR_NOT_FOUND && !s_has_radio && !ce);
  assert(strstr(s_diagnostic,"REG 00: FF != 0C")!=NULL);
  missing=false;
  assert(initialize_radio()==ESP_OK && s_has_radio && !ce && !payload_written);
  assert(registers[NRF_REG_RF_CH][0]==76);
  assert(registers[NRF_REG_RF_SETUP][0]==4);
  assert(registers[NRF_REG_EN_AA][0]==0);
  completion=NRF_STATUS_TX_DS;
  assert(send_packet()==ESP_OK && s_packets==1 && !ce);
  s_ack=true;
  assert(initialize_radio()==ESP_OK);
  assert(registers[NRF_REG_EN_AA][0]==1);
  refresh_config(); assert(strstr(config_text,"ACK ON ")!=NULL);
  completion=NRF_STATUS_MAX_RT;
  assert(send_packet()==ESP_ERR_TIMEOUT && s_packets==1 && !ce);
  completion=0;
  assert(send_packet()==ESP_ERR_TIMEOUT && s_packets==1 && !ce);
  completion=NRF_STATUS_TX_DS;
  assert(send_packet()==ESP_OK && s_packets==2);
  s_ack=false; refresh_config(); assert(strstr(config_text,"ACK OFF")!=NULL);
  puts("NRF24 host checks passed: detection, register setup, TX, ACK, timeout, labels");
}
