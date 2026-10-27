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

#ifndef MESHCORE_H
#define MESHCORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

/* Radio defaults (MeshCore wire spec) */
#define MESHCORE_FREQ_HZ      915000000UL
#define MESHCORE_BW_HZ        250000UL
#define MESHCORE_SF           10
#define MESHCORE_CR           5
#define MESHCORE_PREAMBLE_LEN 8
#define MESHCORE_TX_POWER_DBM 20

/* Internal limits */
#define MESHCORE_MAX_PAYLOAD  184
#define MESHCORE_MAX_PATH     64
#define MESHCORE_MAX_RAW      255
#define MESHCORE_MAX_CONTACTS 32
#define MESHCORE_MAX_CHANNELS 8
#define MESHCORE_NAME_MAX     32
#define MESHCORE_TEXT_MAX     160

/* Sentinel for unknown path in ContactInfo.out_path_len */
#define MESHCORE_OUT_PATH_UNKNOWN 0xFF

/* Public channel index (default in slot 0) */
#define MESHCORE_PUBLIC_CHANNEL 0

/* ADVERT app_data flags */
#define MESHCORE_ADV_FLAG_HAS_LATLON 0x10
#define MESHCORE_ADV_FLAG_HAS_NAME   0x80

/**
 * @brief MeshCore wire format payload types (4 bits).
 *
 * See `INFO/Lora_Meshcore_github/.../docs/payloads.md` for the layout of each type.
 */
typedef enum {
  MC_PT_REQ = 0x00,
  MC_PT_RESPONSE = 0x01,
  MC_PT_TXT_MSG = 0x02,
  MC_PT_ACK = 0x03,
  MC_PT_ADVERT = 0x04,
  MC_PT_GRP_TXT = 0x05,
  MC_PT_GRP_DATA = 0x06,
  MC_PT_ANON_REQ = 0x07,
  MC_PT_PATH = 0x08,
  MC_PT_TRACE = 0x09,
  MC_PT_MULTIPART = 0x0A,
  MC_PT_CONTROL = 0x0B,
  MC_PT_RAW_CUSTOM = 0x0F,
} meshcore_payload_type_t;

/**
 * @brief MeshCore wire format route types (2 bits).
 */
typedef enum {
  MC_RT_TRANSPORT_FLOOD = 0x00,
  MC_RT_FLOOD = 0x01,
  MC_RT_DIRECT = 0x02,
  MC_RT_TRANSPORT_DIRECT = 0x03,
} meshcore_route_type_t;

/**
 * @brief Node types announced in ADVERT (lower 4 bits of flags).
 */
typedef enum {
  MC_ADV_TYPE_NONE = 0,
  MC_ADV_TYPE_CHAT = 1,
  MC_ADV_TYPE_REPEATER = 2,
  MC_ADV_TYPE_ROOM = 3,
  MC_ADV_TYPE_SENSOR = 4,
} meshcore_adv_type_t;

/**
 * @brief TXT_MSG message sub-type (upper 6 bits of plaintext byte 4).
 */
typedef enum {
  MC_TXT_TYPE_PLAIN = 0,
  MC_TXT_TYPE_CLI_DATA = 1,
  MC_TXT_TYPE_SIGNED_PLAIN = 2,
  MC_TXT_TYPE_RESERVED = 3,
} meshcore_txt_type_t;

/**
 * @brief Parsed view of a LoRa packet (pointers into the raw buffer).
 *
 * The `path` and `payload` pointers point into the original raw buffer.
 * The `rssi_dbm` and `snr_db` fields are filled in by the caller.
 */
typedef struct {
  uint8_t version;
  uint8_t payload_type;
  uint8_t route_type;
  uint8_t hash_size;
  uint8_t path_count;
  const uint8_t *path;
  uint16_t path_len;
  const uint8_t *payload;
  uint16_t payload_len;
  int16_t rssi_dbm;
  int8_t snr_db;
} meshcore_packet_view_t;

/**
 * @brief Node identity (Ed25519 keypair + name + type).
 */
typedef struct {
  uint8_t pub_key[32];
  uint8_t priv_key[64];
  char name[MESHCORE_NAME_MAX];
  uint8_t adv_type;
} meshcore_identity_t;

/**
 * @brief Remote contact (mirrors ContactInfo from the companion protocol).
 *
 * `out_path_len`: encoded path_len byte (`(hash_size-1)<<6 | hop_count`),
 * or MESHCORE_OUT_PATH_UNKNOWN (0xFF) if direct path is unknown.
 */
typedef struct {
  bool is_used;
  uint8_t pub_key[32];
  uint8_t type;
  uint8_t flags;
  uint8_t out_path_len;
  uint8_t out_path[MESHCORE_MAX_PATH];
  char name[MESHCORE_NAME_MAX];
  uint32_t last_advert;
  int32_t gps_lat;
  int32_t gps_lon;
  uint32_t lastmod;
} meshcore_contact_t;

/**
 * @brief Group channel (16B PSK + name).
 */
typedef struct {
  bool is_used;
  uint8_t hash;
  uint8_t secret[16];
  char name[MESHCORE_NAME_MAX];
} meshcore_channel_t;

/**
 * @brief LoRa radio parameters, persisted in NVS.
 */
typedef struct {
  uint32_t freq_hz;
  uint32_t bw_hz;
  uint8_t sf;
  uint8_t cr;
  int8_t tx_power_dbm;
  uint8_t reserved;
} meshcore_radio_prefs_t;

/**
 * @brief Callback: ADVERT received with valid signature.
 *
 * @param contact   Pointer to the stored contact (valid during callback).
 * @param rssi_dbm  Packet RSSI in dBm.
 * @param snr_db    Packet SNR in dB.
 * @param ctx       Context provided in meshcore_init.
 */
typedef void (*meshcore_on_advert_cb_t)(const meshcore_contact_t *contact,
                                        int16_t rssi_dbm,
                                        int8_t snr_db,
                                        void *ctx);

/**
 * @brief Callback: GRP_TXT decrypted on some local channel.
 *
 * @param channel_idx  Local channel slot that matched (0..MESHCORE_MAX_CHANNELS-1).
 * @param path_len     Number of hops in the packet path.
 * @param text         Null-terminated UTF-8 string.
 * @param timestamp    Sender's unix timestamp.
 * @param rssi_dbm     RSSI in dBm.
 * @param snr_db       SNR in dB.
 * @param ctx          Context.
 */
typedef void (*meshcore_on_grp_txt_cb_t)(uint8_t channel_idx,
                                         uint8_t path_len,
                                         const char *text,
                                         uint32_t timestamp,
                                         int16_t rssi_dbm,
                                         int8_t snr_db,
                                         void *ctx);

/**
 * @brief Callback: DM (TXT_MSG) decrypted addressed to this node.
 *
 * @param peer_pub_key  Sender pubkey (32B).
 * @param text          UTF-8 string.
 * @param timestamp     Sender's unix timestamp.
 * @param txt_type      Sub-type (MC_TXT_TYPE_*).
 * @param path_len      Hops traversed.
 * @param rssi_dbm      RSSI in dBm.
 * @param snr_db        SNR in dB.
 * @param ctx           Context.
 */
typedef void (*meshcore_on_direct_msg_cb_t)(const uint8_t peer_pub_key[32],
                                            const char *text,
                                            uint32_t timestamp,
                                            uint8_t txt_type,
                                            uint8_t path_len,
                                            int16_t rssi_dbm,
                                            int8_t snr_db,
                                            void *ctx);

/**
 * @brief Callback: ACK received for a DM we sent (CRC matched pending).
 *
 * @param ack_crc       ACK CRC.
 * @param peer_pub_key  Pubkey of the peer that sent the ACK.
 * @param ctx           Context.
 */
typedef void (*meshcore_on_ack_cb_t)(uint32_t ack_crc, const uint8_t peer_pub_key[32], void *ctx);

/**
 * @brief Callback: path learned for a contact (PATH inbound).
 *
 * @param contact  Pointer to the contact with freshly updated `out_path`.
 * @param ctx      Context.
 */
typedef void (*meshcore_on_path_update_cb_t)(const meshcore_contact_t *contact, void *ctx);

/**
 * @brief Set of callbacks registered in meshcore_init.
 */
typedef struct {
  meshcore_on_advert_cb_t on_advert;
  meshcore_on_grp_txt_cb_t on_grp_txt;
  meshcore_on_direct_msg_cb_t on_direct_msg;
  meshcore_on_ack_cb_t on_ack;
  meshcore_on_path_update_cb_t on_path_update;
  void *ctx;
} meshcore_callbacks_t;

/**
 * @brief Initializes the MeshCore stack. Loads prefs from NVS.
 *
 * Must be called AFTER sx1262_init and BEFORE meshcore_start.
 *
 * @param identity  Node identity. Ed25519 pub/priv already generated.
 * @param cbs       Callbacks (optional fields may be NULL).
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if identity is NULL
 */
esp_err_t meshcore_init(const meshcore_identity_t *identity, const meshcore_callbacks_t *cbs);

/**
 * @brief Starts continuous RX on the SX1262.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if already started
 */
esp_err_t meshcore_start(void);

/**
 * @brief Stops RX. Idempotent.
 */
void meshcore_stop(void);

/**
 * @brief Pumps pending SX1262 IRQ + pendings GC + drains relay queue.
 *
 * Call from the main loop every ~50ms.
 */
void meshcore_poll(void);

/**
 * @brief Sets the node name, persists in NVS, and re-emits ADVERT (with cooldown).
 *
 * @param name  Null-terminated UTF-8 string (max MESHCORE_NAME_MAX-1).
 */
void meshcore_set_node_name(const char *name);

/**
 * @brief Sets node lat/lon, persists in NVS, and re-emits ADVERT.
 *
 * @param lat_e6      Latitude * 1e6.
 * @param lon_e6      Longitude * 1e6.
 * @param has_latlon  If false, omits lat/lon from the next ADVERT.
 */
void meshcore_set_advert_latlon(int32_t lat_e6, int32_t lon_e6, bool has_latlon);

/**
 * @brief Emits ADVERT immediately (ignores cooldown). Used for CMD_SEND_SELF_ADVERT.
 *
 * @return ESP_OK on success.
 */
esp_err_t meshcore_send_advert(void);

/**
 * @brief Emits ADVERT respecting the 10s cooldown between re-emits.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if still in cooldown
 */
esp_err_t meshcore_send_advert_throttled(void);

/**
 * @brief Retrieves pointer to a channel by idx.
 *
 * @param idx  0..MESHCORE_MAX_CHANNELS-1.
 * @return Read-only pointer or NULL if idx invalid or slot empty.
 */
const meshcore_channel_t *meshcore_channel_get(uint8_t idx);

/**
 * @brief Sets or removes a channel. Persists in NVS.
 *
 * @param idx     0..MESHCORE_MAX_CHANNELS-1.
 * @param name    Name (max 32B). May be NULL.
 * @param secret  16B PSK. NULL or all-zero removes the channel.
 * @return true on success, false if idx invalid.
 */
bool meshcore_channel_set(uint8_t idx, const char *name, const uint8_t secret[16]);

/**
 * @brief Emits GRP_TXT on the specified channel (flood).
 *
 * @param channel_idx  0..MESHCORE_MAX_CHANNELS-1.
 * @param text         Null-terminated UTF-8 string.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if idx invalid or text NULL
 *   - ESP_ERR_NOT_FOUND if slot empty
 */
esp_err_t meshcore_send_grp_txt(uint8_t channel_idx, const char *text);

/**
 * @brief Returns pointer to the fixed contacts array (read-only).
 *
 * @return Array of MESHCORE_MAX_CONTACTS entries (check `is_used`).
 */
const meshcore_contact_t *meshcore_contacts_array(void);

/**
 * @brief Counts how many contacts are in use.
 *
 * @return 0..MESHCORE_MAX_CONTACTS.
 */
size_t meshcore_contacts_count(void);

/**
 * @brief Looks up a contact by full pubkey.
 *
 * @param pub_key  32B Ed25519 pubkey.
 * @return Read-only pointer or NULL.
 */
const meshcore_contact_t *meshcore_contact_find(const uint8_t pub_key[32]);

/**
 * @brief Looks up a contact by the first byte of the pubkey (1B hash).
 *
 * @param hash_byte  First byte of the pubkey.
 * @return First contact with pubkey[0] == hash_byte, or NULL.
 */
const meshcore_contact_t *meshcore_contact_find_by_hash(uint8_t hash_byte);

/**
 * @brief Adds or updates a contact. Preserves out_path if the input does not bring a new one.
 *
 * @param src  Contact to insert.
 * @return true on success, false if argument invalid.
 */
bool meshcore_contact_upsert(const meshcore_contact_t *src);

/**
 * @brief Removes a contact and persists DB.
 *
 * @param pub_key  32B contact pubkey.
 * @return true if removed, false if not found.
 */
bool meshcore_contact_remove(const uint8_t pub_key[32]);

/**
 * @brief Clears the out_path of a contact (forces next DM via flood).
 *
 * @param pub_key  32B contact pubkey.
 * @return true on success, false if not found.
 */
bool meshcore_contact_reset_path(const uint8_t pub_key[32]);

/**
 * @brief Sends an encrypted DM (X25519 ECDH + AES-128-ECB + HMAC-SHA256[2B]).
 *
 * If contact has known `out_path`, sends with ROUTE_TYPE_DIRECT.
 * Otherwise, flood. Adds entry to the pendings table.
 *
 * @param[in]  peer_pub_key      32B recipient pubkey.
 * @param[in]  text              UTF-8 plaintext (max MESHCORE_TEXT_MAX).
 * @param[in]  timestamp         Unix ts (part of plaintext + replay nonce).
 * @param[in]  attempt           0..3, retransmit attempt id.
 * @param[out] out_expected_ack  Expected CRC for correlation with push 0x82. May be NULL.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if arguments invalid
 *   - ESP_FAIL if TX fails
 */
esp_err_t meshcore_send_direct_msg(const uint8_t peer_pub_key[32],
                                   const char *text,
                                   uint32_t timestamp,
                                   uint8_t attempt,
                                   uint32_t *out_expected_ack);

/**
 * @brief Reconfigures the SX1262 at runtime. Persists in NVS.
 *
 * @param freq_hz       150 MHz to 960 MHz.
 * @param bw_hz         7810..500000 (mapped to driver BW codes).
 * @param sf            5..12.
 * @param cr            5..8 (4/5 to 4/8).
 * @param tx_power_dbm  -9 to +22.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if any parameter out of range
 *   - ESP_FAIL if driver rejects
 */
esp_err_t meshcore_set_radio_params(
    uint32_t freq_hz, uint32_t bw_hz, uint8_t sf, uint8_t cr, int8_t tx_power_dbm);

/**
 * @brief Retrieves a read-only pointer to the radio parameters in use.
 *
 * @return Pointer never NULL after meshcore_init.
 */
const meshcore_radio_prefs_t *meshcore_get_radio_prefs(void);

/**
 * @brief Sets unix time (called by CMD_SET_DEVICE_TIME).
 *
 * @param ts  Unix timestamp in seconds.
 */
void meshcore_set_unix_time(uint32_t ts);

/**
 * @brief Returns current unix time. Before first sync, uses fallback
 *        (2026-01-01 + uptime) to avoid returning 0 in signatures.
 *
 * @return Unix timestamp in seconds.
 */
uint32_t meshcore_get_unix_time(void);

/**
 * @brief Enables or disables rebroadcast of flood/direct packets.
 *
 * Default: false (pure Companion). Repeater variant: true.
 *
 * @param is_enabled  true to relay; false to silence.
 */
void meshcore_set_relay(bool is_enabled);

/**
 * @brief Returns current state of the relay flag.
 *
 * @return true if relay enabled.
 */
bool meshcore_get_relay(void);

/**
 * @brief Copies node pubkey to a buffer.
 *
 * @param[out] out  32B buffer.
 */
void meshcore_get_pub_key(uint8_t out[32]);

/**
 * @brief Copies node name to a buffer.
 *
 * @param[out] out  Output buffer.
 * @param[in]  max  Buffer size (includes terminator).
 */
void meshcore_get_name(char *out, size_t max);

/**
 * @brief Retrieves the current advert lat/lon.
 *
 * @param[out] out_lat_e6  Lat * 1e6. May be NULL.
 * @param[out] out_lon_e6  Lon * 1e6. May be NULL.
 * @param[out] out_has     true if HAS_LATLON active. May be NULL.
 */
void meshcore_get_advert_latlon(int32_t *out_lat_e6, int32_t *out_lon_e6, bool *out_has);

/**
 * @brief Returns node `adv_type` (1=CHAT, 2=REPEATER, 3=ROOM, 4=SENSOR).
 *
 * @return Value from meshcore_adv_type_t.
 */
uint8_t meshcore_get_adv_type(void);

/** Copy one entry/count under the writer lock; out may be NULL for count only. */
esp_err_t meshcore_contacts_snapshot(uint16_t index, meshcore_contact_t *out, uint16_t *out_count);

#ifdef __cplusplus
}
#endif

#endif // MESHCORE_H
