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
/**
 * @file iso_dep.c
 * @brief Bounded ISO-DEP reader exchanges; no partial APDU is reported as complete.
 */
#include "iso_dep.h"

#include <string.h>

#include "esp_timer.h"

#include "nfc_poller.h"
#include "iso14443a.h"
#include "hb_nfc_timer.h"

#define ISO_DEP_FRAME_BYTES        256U
#define ISO_DEP_BUFFER_BYTES       260U
#define ISO_DEP_CRC_BYTES          2U
#define ISO_DEP_DEFAULT_FWI        4U
#define ISO_DEP_TIMEOUT_MAX_MS     5000
#define ISO_DEP_EXCHANGE_BUDGET_US 15000000
#define ISO_DEP_WTX_LIMIT          8U
#define ISO_DEP_RETRY_LIMIT        2U
#define ISO_DEP_BLOCK_LIMIT        128U
#define ISO_DEP_GET_RESPONSE_LIMIT 8U

/* The frontend has one active reader session, serialized by its owner. */
static uint8_t s_pcd_block_num;

static uint16_t iso_dep_frame_size(uint8_t code) {
  static const uint16_t sizes[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
  return code < sizeof(sizes) / sizeof(sizes[0]) ? sizes[code] : 0;
}

static int iso_dep_fwt_ms(uint8_t fwi) {
  if (fwi > 14U)
    return 0;
  return (int)((302U * (1U << fwi) + 999U) / 1000U) + 1;
}

static int iso_dep_strip_crc(uint8_t *data, int length) {
  /* The poller exposes the FIFO including CRC-A. Never accept a CRC-less
   * response or infer that an invalid trailer was APDU data. */
  return length >= 3 && iso14443a_check_crc(data, (size_t)length) ? length - 2 : 0;
}

static size_t iso_dep_header(uint8_t *frame, uint8_t pcb, uint8_t cid) {
  frame[0] = pcb | (cid ? 0x08U : 0U);
  if (cid)
    frame[1] = cid;
  return cid ? 2U : 1U;
}

/* NAD is not negotiated by this API. Accept optional CID=0, but never put CID
 * or NAD bytes into an APDU payload. */
static size_t iso_dep_rx_header(const uint8_t *frame, int length, uint8_t cid) {
  if (length < 1 || (frame[0] & 0x04U))
    return 0;
  if (frame[0] & 0x08U)
    return length >= 2 && frame[1] == cid ? 2U : 0U;
  return cid ? 0U : 1U;
}

hb_nfc_err_t iso_dep_rats(uint8_t fsdi, uint8_t cid, nfc_iso_dep_data_t *dep) {
  if (dep == NULL || fsdi > 8U || cid > 14U)
    return HB_NFC_ERR_PARAM;
  memset(dep, 0, sizeof(*dep));
  const uint8_t command[] = {0xE0, (uint8_t)((fsdi << 4U) | cid)};
  uint8_t response[NFC_ATS_MAX_LEN + ISO_DEP_CRC_BYTES] = {0};
  int length =
      nfc_poller_transceive(command, sizeof(command), true, response, sizeof(response), 1, 30);
  length = iso_dep_strip_crc(response, length);
  if (length < 1 || length > NFC_ATS_MAX_LEN || response[0] != length)
    return HB_NFC_ERR_PROTOCOL;
  uint16_t fsc = 32;
  uint8_t fwi = ISO_DEP_DEFAULT_FWI, sfgi = 0, tc = 0;
  if (length > 1) {
    const uint8_t t0 = response[1];
    fsc = iso_dep_frame_size(t0 & 0x0FU);
    if (!fsc || (t0 & 0x80U))
      return HB_NFC_ERR_PROTOCOL;
    int pos = 2;
    if (t0 & 0x10U) {
      if (pos >= length)
        return HB_NFC_ERR_PROTOCOL;
      pos++;
    }
    if (t0 & 0x20U) {
      if (pos >= length)
        return HB_NFC_ERR_PROTOCOL;
      fwi = response[pos] >> 4U;
      sfgi = response[pos++] & 0x0FU;
      if (fwi > 14U || sfgi > 14U)
        return HB_NFC_ERR_PROTOCOL;
    }
    if (t0 & 0x40U) {
      if (pos >= length)
        return HB_NFC_ERR_PROTOCOL;
      tc = response[pos];
    }
  }
  if (cid && !(tc & 0x02U))
    return HB_NFC_ERR_PROTOCOL;
  memcpy(dep->ats, response, (size_t)length);
  dep->ats_len = (uint16_t)length;
  dep->fsc = fsc;
  dep->fsd = iso_dep_frame_size(fsdi);
  dep->fwi = fwi;
  dep->cid = cid;
  s_pcd_block_num = 0;
  if (sfgi)
    hb_nfc_timer_delay_ms((302U * (1U << sfgi) + 999U) / 1000U + 1U);
  return HB_NFC_OK;
}

hb_nfc_err_t iso_dep_pps(uint8_t cid, uint8_t dsi, uint8_t dri) {
  if (cid > 14U || dsi > 3U || dri > 3U)
    return HB_NFC_ERR_PARAM;
  /* Higher rates require changing both radio and link timing after PPS. */
  return dsi == 0 && dri == 0 ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

/* WTX applies to the next wait only. The exchange deadline and counter bound
 * even a responsive peer that keeps extending the wait indefinitely. */
static int iso_dep_exchange(const nfc_iso_dep_data_t *dep,
                            const uint8_t *tx,
                            size_t tx_length,
                            uint8_t *rx,
                            int base_timeout,
                            unsigned *wtx_count,
                            int64_t deadline) {
  uint8_t wtx[3];
  int wait_ms = base_timeout;
  for (;;) {
    int64_t remaining = (deadline - esp_timer_get_time()) / 1000;
    if (remaining <= 0)
      return -1;
    if (wait_ms > ISO_DEP_TIMEOUT_MAX_MS)
      wait_ms = ISO_DEP_TIMEOUT_MAX_MS;
    if (wait_ms > remaining)
      wait_ms = (int)remaining;
    int length = nfc_poller_transceive(tx, tx_length, true, rx, ISO_DEP_BUFFER_BYTES, 1, wait_ms);
    length = iso_dep_strip_crc(rx, length);
    if (length <= 0)
      return 0;
    size_t hdr = iso_dep_rx_header(rx, length, dep->cid);
    if (!hdr || (size_t)length + ISO_DEP_CRC_BYTES > dep->fsd)
      return -1;
    if ((rx[0] & (uint8_t)~0x08U) != 0xF2U)
      return length;
    if (length != (int)hdr + 1 || ++*wtx_count > ISO_DEP_WTX_LIMIT)
      return -1;
    uint8_t multiplier = rx[hdr] & 0x3FU;
    if ((rx[hdr] & 0xC0U) || !multiplier || multiplier > 59U)
      return -1;
    tx_length = iso_dep_header(wtx, 0xF2U, dep->cid);
    wtx[tx_length++] = multiplier;
    tx = wtx;
    wait_ms = base_timeout > ISO_DEP_TIMEOUT_MAX_MS / multiplier ? ISO_DEP_TIMEOUT_MAX_MS
                                                                 : base_timeout * multiplier;
  }
}

int iso_dep_transceive(const nfc_iso_dep_data_t *dep,
                       const uint8_t *tx,
                       size_t tx_len,
                       uint8_t *rx,
                       size_t rx_max,
                       int timeout_ms) {
  if (!dep || !tx || !rx || !tx_len || !rx_max || dep->fsc < 16 || dep->fsc > ISO_DEP_FRAME_BYTES ||
      dep->fsd < 16 || dep->fsd > ISO_DEP_FRAME_BYTES || dep->cid > 14U || dep->fwi > 14U ||
      timeout_ms > ISO_DEP_TIMEOUT_MAX_MS)
    return 0;
  const size_t hdr = dep->cid ? 2U : 1U;
  const size_t max_inf = dep->fsc - ISO_DEP_CRC_BYTES - hdr;
  int base_timeout = timeout_ms > 0 ? timeout_ms : iso_dep_fwt_ms(dep->fwi);
  const int64_t deadline = esp_timer_get_time() + ISO_DEP_EXCHANGE_BUDGET_US;
  uint8_t frame[ISO_DEP_FRAME_BYTES], response[ISO_DEP_BUFFER_BYTES], control[2];
  uint8_t block = s_pcd_block_num;
  unsigned wtx_count = 0, blocks = 0;
  size_t offset = 0, output = 0;
  int length = 0;
  while (offset < tx_len) {
    if (++blocks > ISO_DEP_BLOCK_LIMIT)
      return 0;
    size_t chunk = tx_len - offset;
    bool chain = chunk > max_inf;
    if (chain)
      chunk = max_inf;
    iso_dep_header(frame, (uint8_t)(0x02U | block | (chain ? 0x10U : 0U)), dep->cid);
    memcpy(frame + hdr, tx + offset, chunk);
    unsigned retry = 0;
    for (;;) {
      length =
          iso_dep_exchange(dep, frame, hdr + chunk, response, base_timeout, &wtx_count, deadline);
      if (!length) {
        size_t count = iso_dep_header(control, (uint8_t)(0xB2U | block), dep->cid);
        for (unsigned recovery = 0; !length && recovery < ISO_DEP_RETRY_LIMIT; recovery++)
          length =
              iso_dep_exchange(dep, control, count, response, base_timeout, &wtx_count, deadline);
      }
      if (length <= 0)
        return 0;
      uint8_t pcb = response[0] & (uint8_t)~0x08U;
      if ((pcb & 0xFEU) == 0xA2U) {
        if (length != (int)iso_dep_rx_header(response, length, dep->cid))
          return 0;
        if ((pcb & 1U) != block && retry++ < ISO_DEP_RETRY_LIMIT)
          continue;
        if (!chain || (pcb & 1U) != block)
          return 0;
        block ^= 1U;
      } else if (chain)
        return 0;
      break;
    }
    offset += chunk;
  }
  unsigned duplicates = 0;
  for (;;) {
    if (++blocks > ISO_DEP_BLOCK_LIMIT || length <= 0)
      return 0;
    uint8_t pcb = response[0];
    size_t response_hdr = iso_dep_rx_header(response, length, dep->cid);
    if (!response_hdr || (pcb & 0xE6U) != 0x02U)
      return 0;
    if ((pcb & 1U) == block) {
      size_t count = (size_t)length - response_hdr;
      if (count > rx_max - output)
        return 0;
      memcpy(rx + output, response + response_hdr, count);
      output += count;
      block ^= 1U;
      duplicates = 0;
      if (!(pcb & 0x10U)) {
        s_pcd_block_num = block;
        return (int)output;
      }
    } else if (!output || ++duplicates > ISO_DEP_RETRY_LIMIT)
      return 0;
    size_t count = iso_dep_header(control, (uint8_t)(0xA2U | block), dep->cid);
    length = 0;
    for (unsigned retry = 0; !length && retry <= ISO_DEP_RETRY_LIMIT; retry++)
      length = iso_dep_exchange(dep, control, count, response, base_timeout, &wtx_count, deadline);
  }
}

int iso_dep_apdu_transceive(const nfc_iso_dep_data_t *dep,
                            const uint8_t *apdu,
                            size_t apdu_len,
                            uint8_t *rx,
                            size_t rx_max,
                            int timeout_ms) {
  if (!apdu || !rx || !apdu_len || apdu_len > ISO_DEP_BUFFER_BYTES || rx_max < 2)
    return 0;
  uint8_t command[ISO_DEP_BUFFER_BYTES];
  memcpy(command, apdu, apdu_len);
  int length = iso_dep_transceive(dep, command, apdu_len, rx, rx_max, timeout_ms);
  if (length < 2)
    return 0;
  if (rx[length - 2] == 0x6CU) {
    /* Only Case 2S/4S has a trailing Le. Never overwrite Case 3 command data. */
    bool has_le =
        apdu_len == 5 || (apdu_len >= 7 && apdu[4] != 0 && apdu_len == (size_t)apdu[4] + 6U);
    if (!has_le)
      return length;
    command[apdu_len - 1] = rx[length - 1];
    length = iso_dep_transceive(dep, command, apdu_len, rx, rx_max, timeout_ms);
    if (length < 2)
      return 0;
  }
  size_t output = (size_t)length - 2U;
  uint8_t sw1 = rx[output], sw2 = rx[output + 1U];
  for (unsigned more = 0; sw1 == 0x61U; more++) {
    if (more >= ISO_DEP_GET_RESPONSE_LIMIT)
      return 0;
    const uint8_t get_response[] = {apdu[0], 0xC0, 0, 0, sw2};
    uint8_t response[ISO_DEP_BUFFER_BYTES];
    length = iso_dep_transceive(
        dep, get_response, sizeof(get_response), response, sizeof(response), timeout_ms);
    if (length < 2 || (size_t)length > rx_max - output)
      return 0;
    size_t count = (size_t)length - 2U;
    memcpy(rx + output, response, count);
    output += count;
    sw1 = response[count];
    sw2 = response[count + 1U];
  }
  rx[output++] = sw1;
  rx[output++] = sw2;
  return (int)output;
}
