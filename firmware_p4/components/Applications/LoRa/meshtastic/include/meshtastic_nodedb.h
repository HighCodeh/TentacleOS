// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#ifndef MESHTASTIC_NODEDB_H
#define MESHTASTIC_NODEDB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#define MT_NODEDB_MAX_NODES            150
#define MT_NODEDB_LONG_NAME_LEN        32
#define MT_NODEDB_SHORT_NAME_LEN       8
#define MT_NODEDB_ID_LEN               16
#define MT_NODEDB_PUBKEY_LEN           32
#define MT_NODEDB_NEXTHOP_MAX_FAILURES 2

/**
 * @brief In-memory entry for a known node (peer).
 *
 * Campos correspondem ao NodeInfo/User protos do Meshtastic. Somente
 * num, long_name e short_name sao obrigatorios; o resto e populado
 * conforme chegam updates.
 */
typedef struct {
  uint32_t num;              /**< Node number (from field) */
  uint32_t last_heard;       /**< Unix seconds (approx) */
  char id[MT_NODEDB_ID_LEN]; /**< "!xxxxxxxx" */
  char long_name[MT_NODEDB_LONG_NAME_LEN];
  char short_name[MT_NODEDB_SHORT_NAME_LEN];
  uint8_t hw_model;
  uint8_t role;
  float snr;
  int16_t rssi;
  uint8_t hops_away;
  bool is_favorite;
  bool is_ignored;
  bool is_muted;
  bool via_mqtt;
  bool in_use;
  uint8_t public_key[MT_NODEDB_PUBKEY_LEN]; /**< X25519 pubkey (all zeros if unknown) */
  bool has_public_key;
  uint8_t next_hop;          /**< Last octet of relay that got us here (0 = unknown) */
  uint8_t next_hop_failures; /**< Count retx failures on current next_hop; fallback to flood at 2 */
} mt_node_entry_t;

/**
 * @brief Initialize the NodeDB. Loads persisted entries from NVS.
 *
 * Deve ser chamado uma vez no boot, depois do mt_nvs_init.
 */
esp_err_t mt_nodedb_init(void);

/**
 * @brief Insere ou atualiza um no a partir de um User proto.
 *
 * Se o node nao existe ainda, aloca slot. Se ja existe, faz merge
 * (mantem flags sticky como is_favorite/is_ignored/is_muted).
 *
 * @param num         Node number.
 * @param user_bytes  Proto User serializado.
 * @param user_len    Bytes.
 * @param snr         SNR do ultimo pacote.
 * @param rssi        RSSI do ultimo pacote.
 * @param hops_away   Hops ate este no (0 = vizinho direto).
 * @return true se houve mudanca (novo no ou campos alterados).
 */
bool mt_nodedb_upsert_from_user(uint32_t num,
                                const uint8_t *user_bytes,
                                uint16_t user_len,
                                float snr,
                                int16_t rssi,
                                uint8_t hops_away);

/**
 * @brief Busca entrada por node number.
 * @return Ponteiro pra entrada (read-only) ou NULL se nao existe.
 */
const mt_node_entry_t *mt_nodedb_get(uint32_t num);

/**
 * @brief Busca entrada por indice (0..count-1).
 *
 * Usado pelo PhoneAPI no STATE_SEND_OTHER_NODEINFOS.
 * @return Ponteiro ou NULL se fora do range.
 */
const mt_node_entry_t *mt_nodedb_get_by_index(uint16_t idx);

/**
 * @brief Numero de entradas ativas.
 */
uint16_t mt_nodedb_count(void);

/**
 * @brief Marca/desmarca um no como favorito. Persiste em NVS.
 * @return true se o no existe e foi atualizado.
 */
bool mt_nodedb_set_favorite(uint32_t num, bool fav);

/**
 * @brief Marca/desmarca como ignorado. Persiste em NVS.
 */
bool mt_nodedb_set_ignored(uint32_t num, bool ign);

/**
 * @brief Alterna estado muted. Persiste em NVS.
 */
bool mt_nodedb_toggle_muted(uint32_t num);

/**
 * @brief Remove um no por node number. Persiste em NVS.
 */
bool mt_nodedb_remove(uint32_t num);

/**
 * @brief Learn a next-hop for a peer.
 *
 * Called when we hear a packet from `num` that was relayed by `relay_byte`
 * (last octet of relayer's node_num). Updates nodedb[num].next_hop so future
 * unicasts to `num` can be directed.
 *
 * Resets next_hop_failures to 0.
 */
void mt_nodedb_learn_next_hop(uint32_t num, uint8_t relay_byte);

/**
 * @brief Get next_hop byte for unicast routing. Returns 0 if unknown.
 */
uint8_t mt_nodedb_get_next_hop(uint32_t num);

/**
 * @brief Increment retx failure counter for this peer. When it reaches
 * MT_NODEDB_NEXTHOP_MAX_FAILURES, next_hop is cleared (fallback to flood).
 */
void mt_nodedb_record_next_hop_failure(uint32_t num);

/**
 * @brief Forca persistencia imediata em NVS.
 *
 * Em condicoes normais os mutadores acima ja persistem; usar apenas
 * se precisar gravar pending state (ex: shutdown).
 */
esp_err_t mt_nodedb_save(void);

/** Copy one entry/count under the writer lock; out may be NULL for count only. */
esp_err_t mt_nodedb_snapshot(uint16_t index, mt_node_entry_t *out, uint16_t *out_count);

#ifdef __cplusplus
}
#endif

#endif // MESHTASTIC_NODEDB_H
