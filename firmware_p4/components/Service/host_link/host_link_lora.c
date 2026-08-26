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

#include "host_link_lora.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "host_link.h"
#include "lora_session.h"
#include "meshcore.h"
#include "spi_protocol.h"
#include "tos_config.h"
#include "tos_storage_paths.h"

static const char *TAG = "HOST_LORA";

#define LORA_CFG_GET_LEN 12
#define LORA_CFG_SET_LEN 11
#define LORA_NAME_MAX    24
#define LORA_DEFAULT_CR  5
#define LORA_RX_TYPE_MSG 0
#define LORA_RX_BATCH    8
#define LORA_RX_FRAME_MAX \
  (1 + 4 + 2 + 1 + 1 + sizeof(((lora_msg_t *)0)->who) + sizeof(((lora_msg_t *)0)->text))
#define LORA_POLL_MS       500
#define LORA_RX_TASK_STACK 4096
#define LORA_RX_TASK_PRIO  SYS_PRIO_BACKGROUND
#define LORA_RX_TASK_CORE  SYS_CORE_RADIO

static volatile bool s_chat_active = false;
static TaskHandle_t s_rx_task = NULL;
static uint32_t s_seq = 0;

static void put_u32(uint8_t *b, uint32_t v) {
  b[0] = (uint8_t)(v & 0xFF);
  b[1] = (uint8_t)((v >> 8) & 0xFF);
  b[2] = (uint8_t)((v >> 16) & 0xFF);
  b[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t name_hash(const char *s) {
  uint32_t h = 5381;
  for (; *s != '\0'; s++)
    h = ((h << 5) + h) + (uint8_t)(*s);
  return h;
}

static uint32_t local_node_id(void) {
  uint8_t pk[32] = {0};
  meshcore_get_pub_key(pk);
  return (uint32_t)pk[0] | ((uint32_t)pk[1] << 8) | ((uint32_t)pk[2] << 16) |
         ((uint32_t)pk[3] << 24);
}

static void lookup_node_signal(const char *who, int16_t *rssi, int8_t *snr) {
  *rssi = 0;
  *snr = 0;
  uint16_t n = lora_session_node_count();
  for (uint16_t i = 0; i < n; i++) {
    lora_node_t node;
    if (lora_session_node_get(i, &node) && strncmp(node.name, who, sizeof(node.name)) == 0) {
      *rssi = node.rssi;
      *snr = (int8_t)node.snr;
      return;
    }
  }
}

static void emit_rx(const lora_msg_t *m) {
  uint8_t frame[LORA_RX_FRAME_MAX];
  uint16_t pos = 0;
  size_t nl = strnlen(m->who, sizeof(m->who));
  size_t tl = strnlen(m->text, sizeof(m->text));

  int16_t rssi = 0;
  int8_t snr = 0;
  lookup_node_signal(m->who, &rssi, &snr);

  frame[pos++] = LORA_RX_TYPE_MSG;
  put_u32(frame + pos, name_hash(m->who));
  pos += 4;
  frame[pos++] = (uint8_t)((uint16_t)rssi & 0xFF);
  frame[pos++] = (uint8_t)(((uint16_t)rssi >> 8) & 0xFF);
  frame[pos++] = (uint8_t)snr;
  frame[pos++] = (uint8_t)nl;
  memcpy(frame + pos, m->who, nl);
  pos = (uint16_t)(pos + nl);
  memcpy(frame + pos, m->text, tl);
  pos = (uint16_t)(pos + tl);

  host_link_emit_stream(
      SPI_CMD_CAT(SPI_ID_LORACHAT_RX), SPI_CMD_OP(SPI_ID_LORACHAT_RX), frame, pos);
}

static void lora_rx_task(void *arg) {
  (void)arg;
  lora_msg_t batch[LORA_RX_BATCH];

  while (s_chat_active) {
    uint16_t n = lora_session_msg_since(&s_seq, batch, LORA_RX_BATCH);
    for (uint16_t i = 0; i < n; i++)
      if (!batch[i].outgoing)
        emit_rx(&batch[i]);
    if (n < LORA_RX_BATCH)
      vTaskDelay(pdMS_TO_TICKS(LORA_POLL_MS));
  }

  s_rx_task = NULL;
  vTaskDelete(NULL);
}

static uint8_t handle_cfg_get(uint8_t *out, uint16_t out_cap, uint16_t *out_len) {
  if (out_cap < LORA_CFG_GET_LEN)
    return SPI_STATUS_ERROR;
  put_u32(out, g_config_lora.frequency);
  out[4] = (uint8_t)g_config_lora.spreading_factor;
  put_u32(out + 5, (uint32_t)g_config_lora.bandwidth);
  out[9] = (uint8_t)(int8_t)g_config_lora.tx_power;
  out[10] = g_config_lora.sync_word;
  out[11] = g_config_lora.enabled ? 1 : 0;
  *out_len = LORA_CFG_GET_LEN;
  return SPI_STATUS_OK;
}

static uint8_t handle_cfg_set(const uint8_t *payload, uint16_t plen) {
  if (payload == NULL || plen < LORA_CFG_SET_LEN)
    return SPI_STATUS_INVALID_ARG;

  uint32_t freq = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                  ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
  uint8_t sf = payload[4];
  uint32_t bw = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) | ((uint32_t)payload[7] << 16) |
                ((uint32_t)payload[8] << 24);
  int8_t txp = (int8_t)payload[9];
  bool enabled = payload[10] != 0;

  g_config_lora.frequency = freq;
  g_config_lora.spreading_factor = sf;
  g_config_lora.bandwidth = (long)bw;
  g_config_lora.tx_power = txp;
  g_config_lora.enabled = enabled;
  tos_config_save(TOS_PATH_CONFIG_LORA, "lora");

  if (lora_session_active() == LORA_PROTO_MESHCORE) {
    esp_err_t err = meshcore_set_radio_params(freq, bw, sf, LORA_DEFAULT_CR, txp);
    if (err != ESP_OK)
      ESP_LOGW(TAG, "live retune failed: %s", esp_err_to_name(err));
  }
  return SPI_STATUS_OK;
}

static uint8_t handle_chat_start(
    const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t out_cap, uint16_t *out_len) {
  char name[LORA_NAME_MAX];
  uint16_t n = plen < sizeof(name) - 1 ? plen : (uint16_t)(sizeof(name) - 1);
  if (payload != NULL && n > 0)
    memcpy(name, payload, n);
  else
    n = 0;
  name[n] = '\0';

  esp_err_t err = lora_session_start(LORA_PROTO_MESHCORE);
  if (err == ESP_ERR_INVALID_STATE)
    return SPI_STATUS_BUSY;
  if (err != ESP_OK)
    return SPI_STATUS_ERROR;

  if (name[0] != '\0')
    meshcore_set_node_name(name);

  s_seq = 0;
  s_chat_active = true;
  if (s_rx_task == NULL && xTaskCreatePinnedToCore(lora_rx_task,
                                                   "lora_hl_rx",
                                                   LORA_RX_TASK_STACK,
                                                   NULL,
                                                   LORA_RX_TASK_PRIO,
                                                   &s_rx_task,
                                                   LORA_RX_TASK_CORE) != pdPASS) {
    s_chat_active = false;
    return SPI_STATUS_ERROR;
  }

  if (out_cap >= 4) {
    put_u32(out, local_node_id());
    *out_len = 4;
  }
  return SPI_STATUS_OK;
}

static uint8_t handle_chat_send(const uint8_t *payload, uint16_t plen) {
  if (payload == NULL || plen == 0)
    return SPI_STATUS_INVALID_ARG;
  char text[192];
  uint16_t n = plen < sizeof(text) - 1 ? plen : (uint16_t)(sizeof(text) - 1);
  memcpy(text, payload, n);
  text[n] = '\0';
  esp_err_t err = lora_session_send_text(text);
  return (err == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
}

void host_lora_stop(void) {
  s_chat_active = false;
}

uint8_t host_lora_handle(uint16_t cmd,
                         const uint8_t *payload,
                         uint16_t plen,
                         uint8_t *out_data,
                         uint16_t out_cap,
                         uint16_t *out_len) {
  if (out_len != NULL)
    *out_len = 0;

  switch (cmd) {
    case SPI_ID_LORACFG_GET:
      return handle_cfg_get(out_data, out_cap, out_len);
    case SPI_ID_LORACFG_SET:
      return handle_cfg_set(payload, plen);
    case SPI_ID_LORACHAT_START:
      return handle_chat_start(payload, plen, out_data, out_cap, out_len);
    case SPI_ID_LORACHAT_STOP:
      host_lora_stop();
      return SPI_STATUS_OK;
    case SPI_ID_LORACHAT_SEND:
      return handle_chat_send(payload, plen);
    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}
