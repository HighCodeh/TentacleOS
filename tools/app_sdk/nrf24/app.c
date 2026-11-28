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

#include "tos_api.h"

#define NRF_ABI_MINOR 24U
#define NRF_CE_PIN 7U
#define NRF_CSN_PIN 9U
#define NRF_SPI_HZ 1000000U
#define NRF_CHANNEL 76U
#define NRF_CHANNEL_MAX 83U
#define NRF_PAYLOAD_SIZE 16U
#define NRF_ADDRESS_SIZE 5U
#define NRF_POLL_MS 10U
#define NRF_TX_WAIT_MS 200U
#define NRF_BOOT_WAIT_MS 100U
#define NRF_POWER_WAIT_MS 10U
#define NRF_UI_PAD 12
#define NRF_UI_LINE_H 24
#define NRF_UI_STATUS_Y 88
#define NRF_UI_STATUS_H 56
#define NRF_UI_COUNT_Y 148
#define NRF_UI_FOOTER_H 58
#define NRF_COUNTER_DIGITS 10U
#define NRF_HEX_SHIFT 4U
#define NRF_HEX_MASK 15U
#define NRF_BYTE_BITS 8U
#define NRF_REG_CONFIG 0x00U
#define NRF_REG_EN_AA 0x01U
#define NRF_REG_EN_RXADDR 0x02U
#define NRF_REG_SETUP_AW 0x03U
#define NRF_REG_SETUP_RETR 0x04U
#define NRF_REG_RF_CH 0x05U
#define NRF_REG_RF_SETUP 0x06U
#define NRF_REG_STATUS 0x07U
#define NRF_REG_RX_ADDR_P0 0x0AU
#define NRF_REG_TX_ADDR 0x10U
#define NRF_REG_RX_PW_P0 0x11U
#define NRF_REG_DYNPD 0x1CU
#define NRF_REG_FEATURE 0x1DU
#define NRF_CMD_WRITE 0x20U
#define NRF_CMD_PAYLOAD 0xA0U
#define NRF_CMD_FLUSH_TX 0xE1U
#define NRF_CMD_FLUSH_RX 0xE2U
#define NRF_CMD_NOP 0xFFU
#define NRF_CONFIG_OFF 0x0CU
#define NRF_CONFIG_TX 0x0EU
#define NRF_STATUS_CLEAR 0x70U
#define NRF_STATUS_TX_DS 0x20U
#define NRF_STATUS_MAX_RT 0x10U
#define NRF_ADDRESS_WIDTH 3U
#define NRF_RF_1M_MINUS6DBM 0x04U
#define NRF_RETRIES 0x23U

/* Freestanding structure/string initialization may emit this compiler helper. */
void *memcpy(void *destination, const void *source, size_t length) {
  volatile uint8_t *out = destination;
  const volatile uint8_t *in = source;
  for (size_t i = 0; i < length; ++i)
    out[i] = in[i];
  return destination;
}

static const tos_api_t *s_api;
static uint32_t s_session;
static uint32_t s_packets;
static uint8_t s_channel = NRF_CHANNEL;
static bool s_has_radio;
static bool s_ack;
static char s_diagnostic[64];
static tos_ui_obj_t s_status;
static tos_ui_obj_t s_config_label;
static tos_ui_obj_t s_count_label;
static const uint8_t ADDRESS[NRF_ADDRESS_SIZE] = {'H', 'B', 'O', 'Y', '1'};

static esp_err_t command(uint8_t cmd, const uint8_t *data, uint8_t *out, size_t length) {
  uint8_t tx[NRF_PAYLOAD_SIZE + 1];
  uint8_t rx[NRF_PAYLOAD_SIZE + 1];
  if (length > NRF_PAYLOAD_SIZE)
    return ESP_ERR_INVALID_SIZE;
  tx[0] = cmd;
  for (size_t i = 0; i < length; ++i)
    tx[i + 1] = data ? data[i] : NRF_CMD_NOP;
  esp_err_t error = s_api->header->spi_transfer(s_session, tx, rx, length + 1);
  if (error == ESP_OK && out)
    for (size_t i = 0; i < length; ++i)
      out[i] = rx[i + 1];
  return error;
}

static esp_err_t write_reg(uint8_t reg, uint8_t value) {
  return command(NRF_CMD_WRITE | reg, &value, NULL, 1);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *out) {
  return command(reg, NULL, out, 1);
}

static void report(const char *message, esp_err_t error) {
  if (error != ESP_OK && s_diagnostic[0])
    message = s_diagnostic;
  s_api->ui->set_text(s_status, message);
  s_api->log(error == ESP_OK ? TOS_LOG_INFO : TOS_LOG_ERROR, "NRF24", "%s (status=%d)", message, error);
}

static void refresh_config(void) {
  char line[] = "CH 00 / 1 Mbps / -6 dBm\nACK OFF / address HBOY1";
  line[3] = '0' + s_channel / 10;
  line[4] = '0' + s_channel % 10;
  if (s_ack) {
    line[29] = 'N';
    line[30] = ' ';
  }
  s_api->ui->set_text(s_config_label, line);
}

static void hex_byte(char *out, uint8_t value) {
  static const char digits[] = "0123456789ABCDEF";
  out[0] = digits[value >> NRF_HEX_SHIFT];
  out[1] = digits[value & NRF_HEX_MASK];
}

static esp_err_t verify_register(uint8_t reg, uint8_t expected) {
  uint8_t actual = 0;
  esp_err_t error = read_reg(reg, &actual);
  if (error == ESP_OK && actual == expected)
    return ESP_OK;
  const char text[] = "REG 00: 00 != 00\nCheck 3.3V / SPI.";
  for (size_t i = 0; i < sizeof(text); ++i)
    s_diagnostic[i] = text[i];
  hex_byte(&s_diagnostic[4], reg);
  hex_byte(&s_diagnostic[8], actual);
  hex_byte(&s_diagnostic[14], expected);
  s_api->log(TOS_LOG_ERROR, "NRF24", "Register 0x%02X: expected=0x%02X read=0x%02X SPI status=%d",
             reg, expected, actual, error);
  return error != ESP_OK ? error : ESP_ERR_NOT_FOUND;
}

static esp_err_t initialize_radio(void) {
  s_diagnostic[0] = 0;
  s_has_radio = false;
  esp_err_t error = s_api->header->gpio_write(s_session, NRF_CE_PIN, false);
  if (error != ESP_OK)
    return error;
  s_api->delay_ms(NRF_BOOT_WAIT_MS);
  /* Read-back of writable registers distinguishes a responding radio from floating SPI. */
  error = write_reg(NRF_REG_CONFIG, NRF_CONFIG_OFF);
  if (error != ESP_OK)
    return error;
  error = verify_register(NRF_REG_CONFIG, NRF_CONFIG_OFF);
  if (error != ESP_OK)
    return error;
  const uint8_t registers[][2] = {
      {NRF_REG_EN_AA, s_ack ? 1 : 0}, {NRF_REG_EN_RXADDR, 1},
      {NRF_REG_SETUP_AW, NRF_ADDRESS_WIDTH}, {NRF_REG_SETUP_RETR, s_ack ? NRF_RETRIES : 0},
      {NRF_REG_RF_CH, s_channel}, {NRF_REG_RF_SETUP, NRF_RF_1M_MINUS6DBM},
      {NRF_REG_RX_PW_P0, NRF_PAYLOAD_SIZE}, {NRF_REG_DYNPD, 0}, {NRF_REG_FEATURE, 0},
      {NRF_REG_STATUS, NRF_STATUS_CLEAR},
  };
  for (size_t i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
    error = write_reg(registers[i][0], registers[i][1]);
    if (error != ESP_OK)
      return error;
    if (registers[i][0] != NRF_REG_STATUS) {
      error = verify_register(registers[i][0], registers[i][1]);
      if (error != ESP_OK)
        return error;
    }
  }
  error = command(NRF_CMD_WRITE | NRF_REG_TX_ADDR, ADDRESS, NULL, NRF_ADDRESS_SIZE);
  if (error != ESP_OK)
    return error;
  error = command(NRF_CMD_WRITE | NRF_REG_RX_ADDR_P0, ADDRESS, NULL, NRF_ADDRESS_SIZE);
  if (error != ESP_OK)
    return error;
  uint8_t address[NRF_ADDRESS_SIZE];
  error = command(NRF_REG_TX_ADDR, NULL, address, NRF_ADDRESS_SIZE);
  if (error != ESP_OK)
    return error;
  for (size_t i = 0; i < NRF_ADDRESS_SIZE; ++i)
    if (address[i] != ADDRESS[i])
      return ESP_ERR_NOT_FOUND;
  error = verify_register(NRF_REG_RF_CH, s_channel);
  if (error != ESP_OK)
    return error;
  error = command(NRF_CMD_FLUSH_TX, NULL, NULL, 0);
  if (error != ESP_OK)
    return error;
  error = command(NRF_CMD_FLUSH_RX, NULL, NULL, 0);
  if (error != ESP_OK)
    return error;
  error = write_reg(NRF_REG_CONFIG, NRF_CONFIG_TX);
  if (error != ESP_OK)
    return error;
  s_api->delay_ms(NRF_POWER_WAIT_MS);
  s_has_radio = true;
  return ESP_OK;
}

static esp_err_t send_packet(void) {
  uint8_t payload[NRF_PAYLOAD_SIZE] = {'H','i','g','h','b','o','y',' ','N','R','F','2','4',0,0,0};
  payload[NRF_PAYLOAD_SIZE - 2] = (uint8_t)(s_packets >> NRF_BYTE_BITS);
  payload[NRF_PAYLOAD_SIZE - 1] = (uint8_t)s_packets;
  esp_err_t error = write_reg(NRF_REG_STATUS, NRF_STATUS_CLEAR);
  if (error != ESP_OK)
    return error;
  error = command(NRF_CMD_FLUSH_TX, NULL, NULL, 0);
  if (error != ESP_OK)
    return error;
  error = command(NRF_CMD_PAYLOAD, payload, NULL, sizeof(payload));
  if (error != ESP_OK)
    return error;
  error = s_api->header->gpio_write(s_session, NRF_CE_PIN, true);
  if (error != ESP_OK)
    return error;
  s_api->delay_ms(NRF_POLL_MS);
  error = s_api->header->gpio_write(s_session, NRF_CE_PIN, false);
  if (error != ESP_OK)
    return error;
  for (uint32_t elapsed = 0; elapsed < NRF_TX_WAIT_MS && !s_api->should_stop(); elapsed += NRF_POLL_MS) {
    uint8_t status = 0;
    error = read_reg(NRF_REG_STATUS, &status);
    if (error != ESP_OK)
      return error;
    if (status == NRF_CMD_NOP)
      return ESP_ERR_NOT_FOUND;
    if (status & NRF_STATUS_MAX_RT) {
      command(NRF_CMD_FLUSH_TX, NULL, NULL, 0);
      return ESP_ERR_TIMEOUT;
    }
    if (status & NRF_STATUS_TX_DS) {
      ++s_packets;
      return write_reg(NRF_REG_STATUS, NRF_STATUS_TX_DS);
    }
    s_api->delay_ms(NRF_POLL_MS);
  }
  return ESP_ERR_TIMEOUT;
}

static void show_count(void) {
  char text[] = "Packets: 0000000000";
  uint32_t value = s_packets;
  for (size_t i = 0; i < NRF_COUNTER_DIGITS; ++i) {
    text[sizeof(text) - 2 - i] = '0' + value % 10;
    value /= 10;
  }
  s_api->ui->set_text(s_count_label, text);
}

static tos_ui_obj_t label(int32_t y, int32_t height, int32_t width, const char *text) {
  tos_ui_obj_t object = s_api->ui->label(TOS_UI_NONE, NRF_UI_PAD, y, text);
  if (object != TOS_UI_NONE && s_api->ui->set_size(object, width - 2 * NRF_UI_PAD, height) != ESP_OK)
    return TOS_UI_NONE;
  return object;
}

int app_main(const tos_api_t *api, int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (!api || api->abi_major != TOS_ABI_VERSION_MAJOR || api->abi_minor < NRF_ABI_MINOR || !api->header || !api->ui)
    return 1;
  s_api = api;
  if (api->ui->screen_open() != ESP_OK)
    return 1;
  int32_t width, height;
  api->ui->metrics(&width, &height);
  tos_ui_obj_t title = label(NRF_UI_PAD, NRF_UI_LINE_H, width, "NRF24 / Header test");
  s_config_label = label(NRF_UI_PAD + NRF_UI_LINE_H, NRF_UI_LINE_H * 2, width, "Initializing...");
  s_status = label(NRF_UI_STATUS_Y, NRF_UI_STATUS_H, width, "Checking SPI...");
  s_count_label = label(NRF_UI_COUNT_Y, NRF_UI_LINE_H, width, "Packets: 0");
  tos_ui_obj_t footer = label(height - NRF_UI_FOOTER_H, NRF_UI_FOOTER_H, width,
      "OK send / UP retry\nL/R channel / DOWN ACK\nBACK exit");
  if (!title || !s_config_label || !s_status || !s_count_label || !footer)
    goto cleanup;
  const tos_header_spi_config_t config = {.clock_hz = NRF_SPI_HZ, .cs_pin = NRF_CSN_PIN,
      .control_pin = NRF_CE_PIN, .mode = 0};
  esp_err_t error = api->header->spi_open(&config, &s_session);
  if (error == ESP_OK)
    error = initialize_radio();
  refresh_config();
  report(error == ESP_OK ? "Radio detected. Ready.\nOK sends 16 bytes." : "Radio unavailable.\nCheck wiring / power.", error);
  while (!api->should_stop()) {
    tos_ui_event_t event;
    while (api->ui->poll_event(&event)) {
      if (event.action != TOS_KEY_PRESS)
        continue;
      if (event.key == TOS_KEY_BACK)
        goto cleanup;
      if (event.key == TOS_KEY_OK && s_has_radio) {
        report("Sending...", ESP_OK);
        error = send_packet();
        report(error == ESP_OK ? (s_ack ? "Receiver acknowledged." : "Transmitted (no ACK).\nDelivery not verified.") :
            (error == ESP_ERR_TIMEOUT ? "No ACK / TX timeout.\nCheck receiver settings." : "SPI / radio error.\nUP to retry."), error);
        show_count();
      } else if (event.key == TOS_KEY_UP || event.key == TOS_KEY_LEFT || event.key == TOS_KEY_RIGHT || event.key == TOS_KEY_DOWN) {
        if (event.key == TOS_KEY_LEFT && s_channel > 0) --s_channel;
        if (event.key == TOS_KEY_RIGHT && s_channel < NRF_CHANNEL_MAX) ++s_channel;
        if (event.key == TOS_KEY_DOWN) s_ack = !s_ack;
        if (!s_session)
          error = api->header->spi_open(&config, &s_session);
        if (s_session)
          error = initialize_radio();
        refresh_config();
        report(error == ESP_OK ? "Radio ready. OK sends." : "Radio unavailable.\nCheck wiring / power.", error);
      }
    }
    api->delay_ms(NRF_POLL_MS);
  }
cleanup:
  if (s_session) {
    api->header->gpio_write(s_session, NRF_CE_PIN, false);
    write_reg(NRF_REG_CONFIG, NRF_CONFIG_OFF);
    api->header->close(s_session);
    s_session = 0;
  }
  api->ui->screen_close();
  return 0;
}
