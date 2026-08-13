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
 * @file nfc_sim.h
 * @brief NFC SIMULATION model and saved-card library.
 *
 * The real ST25R3916 reader is on the shared SPI3 bus, whose MISO line is tied
 * to LCD-RST by a board jumper and cannot be read reliably, so the NFC submenu
 * runs as a faithful SIMULATION instead of touching the radio. This module is
 * the shared card model plus a small saved "library" persisted in NVS, used by
 * the Read / Saved / Write / Emulate screens. No hardware is accessed.
 */

#ifndef NFC_SIM_H
#define NFC_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define NFC_SIM_MAX_SAVED 16
#define NFC_SIM_NAME_LEN  20
#define NFC_SIM_TYPE_LEN  24

/**
 * @brief A simulated NFC card / tag record.
 */
typedef struct {
  char name[NFC_SIM_NAME_LEN];
  char type[NFC_SIM_TYPE_LEN];
  uint8_t uid[7];
  uint8_t uid_len; ///< UID length in bytes (4 or 7)
  uint16_t atqa;
  uint8_t sak;
} nfc_sim_card_t;

/**
 * @brief Load the saved library from NVS (idempotent; seeds presets on first run).
 */
void nfc_sim_init(void);

/**
 * @brief Get the number of cards currently in the saved library.
 *
 * @return Count of saved cards.
 */
int nfc_sim_saved_count(void);

/**
 * @brief Get a saved card by index.
 *
 * @param index  Zero-based index into the saved library.
 * @return Pointer to the card, or NULL if @p index is out of range.
 */
const nfc_sim_card_t *nfc_sim_saved_get(int index);

/**
 * @brief Append a card to the library and persist.
 *
 * @param card  Card to append. Caller retains ownership.
 * @return true on success, false if the library is full.
 */
bool nfc_sim_add(const nfc_sim_card_t *card);

/**
 * @brief Remove a card by index and persist.
 *
 * @param index  Zero-based index of the card to remove.
 */
void nfc_sim_remove(int index);

/**
 * @brief Synthesize a fresh "discovered" tag (random UID from a realistic template).
 *
 * @param[out] out  Destination card record.
 */
void nfc_sim_random_card(nfc_sim_card_t *out);

/**
 * @brief Number of card templates (for the editor's type cycler).
 *
 * @return Template count.
 */
int nfc_sim_template_count(void);

/**
 * @brief Build a card of a SPECIFIC template @p tmpl with a random UID (editor).
 *
 * @param      tmpl  Template index in range [0, nfc_sim_template_count()).
 * @param[out] out   Destination card record.
 */
void nfc_sim_make_card(int tmpl, nfc_sim_card_t *out);

/**
 * @brief Format a card UID as "DE:AD:BE:EF" into @p buf.
 *
 * @param      card    Card whose UID to format.
 * @param[out] buf     Destination buffer.
 * @param      buflen  Size of @p buf in bytes.
 */
void nfc_sim_format_uid(const nfc_sim_card_t *card, char *buf, int buflen);

#ifdef __cplusplus
}
#endif

#endif // NFC_SIM_H
