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
 * @file nfc_ui_common.h
 * @brief Shared visual kit for the NFC simulation screens.
 *
 * A styled header bar, a credit-card-style panel that renders a card, and an
 * expanding-ring "field" animation. Keeps Read/Saved/Write/Emulate consistent
 * and polished.
 */

#ifndef NFC_UI_COMMON_H
#define NFC_UI_COMMON_H

#include "lvgl.h"

#include "nfc_sim.h"

/**
 * @brief Accent title + underline at the top of @p parent.
 *
 * @param parent  Parent object to attach the header to.
 * @param title   Title text.
 * @return The title label object.
 */
lv_obj_t *nfc_ui_header(lv_obj_t *parent, const char *title);

/**
 * @brief A card-style panel rendering @p card. Caller aligns the returned object.
 *
 * @param parent  Parent object to attach the panel to.
 * @param card    Card to render.
 * @return The created panel object.
 */
lv_obj_t *nfc_ui_card_panel(lv_obj_t *parent, const nfc_sim_card_t *card);

/**
 * @brief The accent colour of @p card's palette (for matching rings/glow to a card).
 *
 * @param card  Card whose palette accent to return.
 * @return The accent colour.
 */
lv_color_t nfc_ui_card_color(const nfc_sim_card_t *card);

/**
 * @brief Expanding concentric-ring field animation (a "broadcasting" NFC field).
 */
typedef struct {
  lv_obj_t *ring[3];
} nfc_ui_field_t;

/**
 * @brief Create the 3 rings centered in @p parent in @p color.
 *
 * @param[out] f       Field animation state to initialize.
 * @param      parent  Parent object to attach the rings to.
 * @param      color   Ring colour.
 */
void nfc_ui_field_create(nfc_ui_field_t *f, lv_obj_t *parent, lv_color_t color);

/**
 * @brief Advance the ring animation; call each frame with elapsed-since-start ms.
 *
 * @param f           Field animation state.
 * @param elapsed_ms  Milliseconds elapsed since the animation start.
 */
void nfc_ui_field_tick(nfc_ui_field_t *f, uint32_t elapsed_ms);

/**
 * @brief Short, pleasant speaker cues for the NFC screens.
 *
 * Fire-and-forget: each call spawns a tiny worker that renders the tones via the
 * self-contained audio_i2s_play_song() (opens/closes its own I2S channel, so it
 * works even though the persistent audio task in kernel.c is disabled). Safe to
 * call from the LVGL thread - the blocking playback runs off-thread.
 */
typedef enum {
  NFC_SND_FOUND, ///< rising two-note blip - a tag was detected
  NFC_SND_SAVE,  ///< short confirm tick - a card was saved
} nfc_ui_sound_t;

/**
 * @brief Play a one-shot NFC cue on the speaker. No-op if a cue is already playing.
 *
 * @param kind  Which cue to play.
 */
void nfc_ui_play_sound(nfc_ui_sound_t kind);

#endif // NFC_UI_COMMON_H
