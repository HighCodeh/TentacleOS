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

#ifndef NFC_FELICA_H
#define NFC_FELICA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "felica.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_FELICA_SYSTEMS          16U
#define NFC_FELICA_NODES            1024U
#define NFC_FELICA_BLOCKS           4096U
#define NFC_FELICA_LITE_BLOCKS      28U
#define NFC_FELICA_FRAME            255U
#define NFC_FELICA_UNKNOWN          0xFFFFU
#define NFC_FELICA_BYTES            16U
#define NFC_FELICA_HALF             8U
#define NFC_FELICA_REG              0x0EU
#define NFC_FELICA_RC               0x80U
#define NFC_FELICA_MAC              0x81U
#define NFC_FELICA_ID               0x82U
#define NFC_FELICA_DID              0x83U
#define NFC_FELICA_SER              0x84U
#define NFC_FELICA_SYS              0x85U
#define NFC_FELICA_CKV              0x86U
#define NFC_FELICA_CK               0x87U
#define NFC_FELICA_MC               0x88U
#define NFC_FELICA_WCNT             0x90U
#define NFC_FELICA_MAC_A            0x91U
#define NFC_FELICA_STATE            0x92U
#define NFC_FELICA_CRC              0xA0U
#define NFC_FELICA_LITE_SYSTEM      0x88B4U
#define NFC_FELICA_SVC_RO           0x000BU
#define NFC_FELICA_SVC_RW           0x0009U
#define NFC_FELICA_CMD_SERVICE      0x02U
#define NFC_FELICA_CMD_RESPONSE     0x04U
#define NFC_FELICA_CMD_SEARCH       0x0AU
#define NFC_FELICA_CMD_SYSTEM       0x0CU
#define NFC_FELICA_MAX_READ         4U
#define NFC_FELICA_MAX_WRITE        2U
#define NFC_FELICA_HEADER           10U
#define NFC_FELICA_STATUS           12U
#define NFC_FELICA_PAYLOAD          13U
#define NFC_FELICA_SHIFT            8U
#define NFC_FELICA_ATTR_MASK        0x3FU
#define NFC_FELICA_ATTR_PUBLIC      0x01U
#define NFC_FELICA_ATTR_RO          0x02U
#define NFC_FELICA_SF_SERVICE_COUNT 0xA1U
#define NFC_FELICA_SF_BLOCK_COUNT   0xA2U
#define NFC_FELICA_SF_ORDER         0xA3U
#define NFC_FELICA_SF_SERVICE       0xA6U
#define NFC_FELICA_SF_MODE          0xA7U
#define NFC_FELICA_SF_BLOCK         0xA8U
#define NFC_FELICA_SF_MIXED         0xB0U
#define NFC_FELICA_SF_AUTH          0xB1U
#define NFC_FELICA_SF_MAC           0xB2U
#define NFC_FELICA_SF_WARNING       0x71U

typedef enum {
  NFC_FELICA_TYPE_UNKNOWN,
  NFC_FELICA_TYPE_STANDARD,
  NFC_FELICA_TYPE_LITE,
  NFC_FELICA_TYPE_COUNT
} nfc_felica_type_t;

typedef struct {
  uint8_t sf1, sf2, data[NFC_FELICA_BYTES];
} nfc_felica_block_t;
typedef struct {
  uint16_t code, key_version;
} nfc_felica_service_t;
typedef struct {
  uint16_t code, end, first, last, key_version;
} nfc_felica_area_t;
typedef struct {
  uint16_t service;
  uint8_t index;
  nfc_felica_block_t block;
} nfc_felica_public_t;
typedef struct {
  uint16_t code, key_version;
  size_t service_count, area_count, block_count;
  nfc_felica_service_t *services;
  nfc_felica_area_t *areas;
  nfc_felica_public_t *blocks;
} nfc_felica_system_t;
typedef struct {
  felica_tag_t tag;
  nfc_felica_type_t type;
  uint8_t blocks_total, blocks_read;
  nfc_felica_block_t lite[NFC_FELICA_LITE_BLOCKS];
  bool has_key, is_internal_auth, is_external_auth;
  size_t system_count;
  nfc_felica_system_t *systems;
} nfc_felica_card_t;
typedef bool (*nfc_felica_cancel_cb_t)(void);
typedef struct {
  const uint8_t *key; /* Optional known 16-byte Lite key; never guessed. */
  nfc_felica_cancel_cb_t should_cancel;
} nfc_felica_read_options_t;
typedef struct {
  uint8_t key[NFC_FELICA_BYTES], rc[NFC_FELICA_BYTES];
} nfc_felica_crypto_t;
typedef struct {
  nfc_felica_card_t *card; /* Borrowed until the RF worker stops. */
  nfc_felica_crypto_t crypto;
  uint8_t mc_shadow[NFC_FELICA_BYTES];
  size_t system;
  bool has_rc, is_authenticated, is_dirty;
} nfc_felica_listener_t;
typedef struct {
  uint8_t data[NFC_FELICA_FRAME];
  size_t size;
} nfc_felica_frame_t;

/* Outputs must be zero-initialized and freed before reuse. */
void nfc_felica_free(nfc_felica_card_t *card);
nfc_felica_type_t nfc_felica_type(const uint8_t *pmm);
uint8_t nfc_felica_lite_address(size_t index);
int nfc_felica_lite_index(uint16_t address);
esp_err_t nfc_felica_read(nfc_felica_card_t *out, const nfc_felica_read_options_t *options);
esp_err_t nfc_felica_load(const char *path, nfc_felica_card_t *out);
esp_err_t nfc_felica_save(const char *path, const nfc_felica_card_t *card);
size_t nfc_felica_page_count(const nfc_felica_card_t *card);
void nfc_felica_page(const nfc_felica_card_t *card, size_t page, char *out, size_t size);
esp_err_t
nfc_felica_crypto_init(nfc_felica_crypto_t *crypto, const uint8_t *key, const uint8_t *rc);
esp_err_t nfc_felica_mac_read(const nfc_felica_crypto_t *crypto,
                              const uint8_t *blocks,
                              const nfc_felica_frame_t *data,
                              uint8_t *out);
esp_err_t nfc_felica_mac_write(const nfc_felica_crypto_t *crypto,
                               const uint8_t *counter,
                               const uint8_t *data,
                               uint8_t *out);
void nfc_felica_listener_init(nfc_felica_listener_t *listener, nfc_felica_card_t *card);
void nfc_felica_listener_reset(nfc_felica_listener_t *listener);
esp_err_t nfc_felica_listener_frame(nfc_felica_listener_t *listener,
                                    const uint8_t *data,
                                    size_t size,
                                    nfc_felica_frame_t *out);

#ifdef __cplusplus
}
#endif
#endif
