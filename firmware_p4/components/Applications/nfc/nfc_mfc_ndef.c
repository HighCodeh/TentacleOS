// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nfc_mfc_ndef.h"

#include <string.h>

#include "ndef.h"

/* MAD1 stores the application-code byte first, then the function-cluster
 * byte. The NFC Forum AID is therefore encoded as 03 E1 on the card. */
#define MAD_INFO_VERSION_1       0x01U
#define MAD_NFC_APP_CODE         0x03U
#define MAD_NFC_FUNCTION_CLUSTER 0xE1U
#define MAD_CRC_INIT             0xC7U
#define MAD_CRC_POLY             0x1DU

#define MFC_DATA_BLOCKS_PER_SECTOR 3U
#define MFC_NDEF_SECTOR_FIRST      1U
#define MFC_NDEF_SECTOR_LAST       15U
#define MFC_NDEF_CAPACITY                                                             \
  ((MFC_NDEF_SECTOR_LAST - MFC_NDEF_SECTOR_FIRST + 1U) * MFC_DATA_BLOCKS_PER_SECTOR * \
   NFC_MFC_BLOCK_SIZE)

#define NDEF_TLV_SHORT_MAX 0xFEU
#define NDEF_TLV_EXTENDED  0xFFU

static uint8_t mad_crc_update(uint8_t crc, uint8_t value) {
  crc ^= value;
  for (int bit = 0; bit < 8; bit++) {
    const bool msb = (crc & 0x80U) != 0U;
    crc <<= 1U;
    if (msb)
      crc ^= MAD_CRC_POLY;
  }
  return crc;
}

static uint8_t mad_crc_blocks(const uint8_t block1[16], const uint8_t block2[16]) {
  uint8_t crc = MAD_CRC_INIT;
  for (size_t i = 1; i < NFC_MFC_BLOCK_SIZE; i++)
    crc = mad_crc_update(crc, block1[i]);
  for (size_t i = 0; i < NFC_MFC_BLOCK_SIZE; i++)
    crc = mad_crc_update(crc, block2[i]);
  return crc;
}

static void
mad_set_aid(uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE], int sector, bool nfc) {
  uint8_t *entry;
  if (sector <= 7)
    entry = &blocks[1][sector * 2];
  else
    entry = &blocks[2][(sector - 8) * 2];
  entry[0] = nfc ? MAD_NFC_APP_CODE : 0U;
  entry[1] = nfc ? MAD_NFC_FUNCTION_CLUSTER : 0U;
}

static bool mad_sector_is_nfc(const uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE],
                              int sector) {
  const uint8_t *entry = sector <= 7 ? &blocks[1][sector * 2] : &blocks[2][(sector - 8) * 2];
  return entry[0] == MAD_NFC_APP_CODE && entry[1] == MAD_NFC_FUNCTION_CLUSTER;
}

static size_t tlv_header_size(size_t ndef_len) {
  return ndef_len <= NDEF_TLV_SHORT_MAX ? 2U : 4U;
}

static hb_nfc_err_t build_ndef(const char *text, uint8_t *ndef, size_t ndef_max, size_t *ndef_len) {
  if (text == NULL || text[0] == '\0' || strlen(text) > NFC_MFC_NDEF_TEXT_MAX)
    return HB_NFC_ERR_PARAM;
  ndef_builder_t builder;
  ndef_builder_init(&builder, ndef, ndef_max);
  hb_nfc_err_t err = ndef_builder_add_text(&builder, text, "pt", false);
  if (err != HB_NFC_OK)
    return err;
  *ndef_len = ndef_builder_len(&builder);
  return *ndef_len > 0U ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

int nfc_mfc_ndef_text_blocks_required(const char *text) {
  uint8_t ndef[MFC_NDEF_CAPACITY] = {0};
  size_t ndef_len = 0;
  if (build_ndef(text, ndef, sizeof(ndef), &ndef_len) != HB_NFC_OK)
    return 0;
  const size_t tlv_len = tlv_header_size(ndef_len) + ndef_len + 1U;
  const size_t sectors = (tlv_len + (MFC_DATA_BLOCKS_PER_SECTOR * NFC_MFC_BLOCK_SIZE) - 1U) /
                         (MFC_DATA_BLOCKS_PER_SECTOR * NFC_MFC_BLOCK_SIZE);
  if (sectors == 0U || sectors > MFC_NDEF_SECTOR_LAST)
    return 0;
  /* MAD blocks 1/2 plus all three data blocks in every allocated sector. */
  return 2 + (int)(sectors * MFC_DATA_BLOCKS_PER_SECTOR);
}

hb_nfc_err_t nfc_mfc_ndef_build_text(const char *text,
                                     uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE],
                                     bool write_mask[NFC_MFC1K_BLOCKS],
                                     int *blocks_required,
                                     size_t *ndef_len_out) {
  if (blocks == NULL || write_mask == NULL || blocks_required == NULL)
    return HB_NFC_ERR_PARAM;

  memset(blocks, 0, NFC_MFC1K_BLOCKS * NFC_MFC_BLOCK_SIZE);
  memset(write_mask, 0, NFC_MFC1K_BLOCKS * sizeof(write_mask[0]));

  /* Trailer bytes are only key-search hints for nfc_mfc_write; the writer
   * never includes trailers in its write mask. Prefer factory-default keys. */
  for (int sector = 0; sector < NFC_MFC1K_SECTORS; sector++) {
    memset(&blocks[sector * 4 + 3][0], 0xFF, NFC_MFC_KEY_SIZE);
    memset(&blocks[sector * 4 + 3][10], 0xFF, NFC_MFC_KEY_SIZE);
  }

  uint8_t ndef[MFC_NDEF_CAPACITY] = {0};
  size_t ndef_len = 0;
  hb_nfc_err_t err = build_ndef(text, ndef, sizeof(ndef), &ndef_len);
  if (err != HB_NFC_OK)
    return err;

  const size_t header_len = tlv_header_size(ndef_len);
  const size_t tlv_len = header_len + ndef_len + 1U;
  const size_t sector_bytes = MFC_DATA_BLOCKS_PER_SECTOR * NFC_MFC_BLOCK_SIZE;
  const size_t sectors = (tlv_len + sector_bytes - 1U) / sector_bytes;
  if (sectors == 0U || sectors > MFC_NDEF_SECTOR_LAST)
    return HB_NFC_ERR_PARAM;

  blocks[1][1] = MAD_INFO_VERSION_1;
  for (int sector = MFC_NDEF_SECTOR_FIRST; sector <= MFC_NDEF_SECTOR_LAST; sector++)
    mad_set_aid(blocks, sector, (size_t)sector <= sectors);
  blocks[1][0] = mad_crc_blocks(blocks[1], blocks[2]);
  write_mask[1] = true;
  write_mask[2] = true;

  uint8_t stream[MFC_NDEF_CAPACITY] = {0};
  size_t pos = 0;
  stream[pos++] = NDEF_TLV_NDEF;
  if (ndef_len <= NDEF_TLV_SHORT_MAX) {
    stream[pos++] = (uint8_t)ndef_len;
  } else {
    stream[pos++] = NDEF_TLV_EXTENDED;
    stream[pos++] = (uint8_t)(ndef_len >> 8U);
    stream[pos++] = (uint8_t)ndef_len;
  }
  memcpy(&stream[pos], ndef, ndef_len);
  pos += ndef_len;
  stream[pos++] = NDEF_TLV_TERMINATOR;

  size_t src = 0;
  for (int sector = MFC_NDEF_SECTOR_FIRST; (size_t)sector <= sectors; sector++) {
    for (int offset = 0; offset < (int)MFC_DATA_BLOCKS_PER_SECTOR; offset++) {
      const int block = sector * 4 + offset;
      memcpy(blocks[block], &stream[src], NFC_MFC_BLOCK_SIZE);
      src += NFC_MFC_BLOCK_SIZE;
      write_mask[block] = true;
    }
  }

  *blocks_required = 2 + (int)(sectors * MFC_DATA_BLOCKS_PER_SECTOR);
  if (ndef_len_out != NULL)
    *ndef_len_out = ndef_len;
  return HB_NFC_OK;
}

static hb_nfc_err_t collect_ndef_area(const uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE],
                                      const bool block_read[NFC_MFC1K_BLOCKS],
                                      uint8_t *area,
                                      size_t area_max,
                                      size_t *area_len) {
  if (block_read != NULL && (!block_read[1] || !block_read[2]))
    return HB_NFC_ERR_NOT_FOUND;
  if (blocks[1][0] != mad_crc_blocks(blocks[1], blocks[2]))
    return HB_NFC_ERR_PROTOCOL;
  if ((blocks[1][1] & 0x03U) != MAD_INFO_VERSION_1)
    return HB_NFC_ERR_PROTOCOL;

  size_t pos = 0;
  bool found = false;
  bool gap = false;
  for (int sector = MFC_NDEF_SECTOR_FIRST; sector <= MFC_NDEF_SECTOR_LAST; sector++) {
    if (!mad_sector_is_nfc(blocks, sector)) {
      if (found)
        gap = true;
      continue;
    }
    /* NFC sectors for the mandatory message must be contiguous. */
    if (gap)
      return HB_NFC_ERR_PROTOCOL;
    found = true;
    for (int offset = 0; offset < (int)MFC_DATA_BLOCKS_PER_SECTOR; offset++) {
      const int block = sector * 4 + offset;
      if (block_read != NULL && !block_read[block])
        return HB_NFC_ERR_NOT_FOUND;
      if (pos + NFC_MFC_BLOCK_SIZE > area_max)
        return HB_NFC_ERR_PROTOCOL;
      memcpy(&area[pos], blocks[block], NFC_MFC_BLOCK_SIZE);
      pos += NFC_MFC_BLOCK_SIZE;
    }
  }
  if (!found)
    return HB_NFC_ERR_NOT_FOUND;
  *area_len = pos;
  return HB_NFC_OK;
}

static hb_nfc_err_t
find_ndef_tlv(const uint8_t *area, size_t area_len, const uint8_t **ndef, size_t *ndef_len) {
  size_t pos = 0;
  while (pos < area_len) {
    const uint8_t type = area[pos++];
    if (type == NDEF_TLV_NULL)
      continue;
    if (type == NDEF_TLV_TERMINATOR)
      return HB_NFC_ERR_NOT_FOUND;
    if (pos >= area_len)
      return HB_NFC_ERR_PROTOCOL;
    size_t len = area[pos++];
    if (len == NDEF_TLV_EXTENDED) {
      if (pos + 2U > area_len)
        return HB_NFC_ERR_PROTOCOL;
      len = ((size_t)area[pos] << 8U) | area[pos + 1U];
      pos += 2U;
    }
    if (pos + len > area_len)
      return HB_NFC_ERR_PROTOCOL;
    if (type == NDEF_TLV_NDEF) {
      *ndef = &area[pos];
      *ndef_len = len;
      return len > 0U ? HB_NFC_OK : HB_NFC_ERR_NOT_FOUND;
    }
    pos += len;
  }
  return HB_NFC_ERR_NOT_FOUND;
}

hb_nfc_err_t nfc_mfc_ndef_extract_text(const uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE],
                                       const bool block_read[NFC_MFC1K_BLOCKS],
                                       char *out,
                                       size_t out_max,
                                       size_t *out_len) {
  if (blocks == NULL || out == NULL || out_max == 0U)
    return HB_NFC_ERR_PARAM;
  out[0] = '\0';
  if (out_len != NULL)
    *out_len = 0U;

  uint8_t area[MFC_NDEF_CAPACITY] = {0};
  size_t area_len = 0;
  hb_nfc_err_t err = collect_ndef_area(blocks, block_read, area, sizeof(area), &area_len);
  if (err != HB_NFC_OK)
    return err;

  const uint8_t *ndef = NULL;
  size_t ndef_len = 0;
  err = find_ndef_tlv(area, area_len, &ndef, &ndef_len);
  if (err != HB_NFC_OK)
    return err;

  ndef_parser_t parser;
  ndef_parser_init(&parser, ndef, ndef_len);
  ndef_record_t record;
  while (ndef_parse_next(&parser, &record)) {
    ndef_text_t text;
    if (!ndef_decode_text(&record, &text) || text.utf16)
      continue;
    if (text.text_len + 1U > out_max)
      return HB_NFC_ERR_PARAM;
    memcpy(out, text.text, text.text_len);
    out[text.text_len] = '\0';
    if (out_len != NULL)
      *out_len = text.text_len;
    return HB_NFC_OK;
  }
  return ndef_parser_error(&parser) == HB_NFC_OK ? HB_NFC_ERR_NOT_FOUND
                                                 : ndef_parser_error(&parser);
}
