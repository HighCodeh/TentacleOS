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

#ifndef CAPTURE_RESULT_UI_H
#define CAPTURE_RESULT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Shared "I captured a signal — now what?" panel for the capture
 *        protocols (NFC / RFID / IR / Sub-GHz).
 *
 * Draws a result card (icon + type + key value) plus a vertical action list, in
 * the content area between the standard chrome header and footer. It is visual +
 * selection only — the calling screen drives it from its own input loop (like
 * menu_component): UP/DOWN -> prev/next, OK -> dispatch
 * capture_result_selected(). The primary action verb differs per protocol
 * ("Emulate" for NFC/RFID, "Send" for IR/Sub-GHz).
 */

/**
 * @brief Selectable actions offered on the capture-result panel.
 */
typedef enum {
  CAP_ACT_PRIMARY = 0, ///< Emulate (NFC/RFID) or Send (IR/Sub-GHz)
  CAP_ACT_SAVE,        ///< persist to the library
  CAP_ACT_AGAIN,       ///< discard current, capture a new one
  CAP_ACT_DISCARD,     ///< drop it and leave
  CAP_ACT_MAX,         ///< action count sentinel
} capture_action_t;

/**
 * @brief Configuration for a capture-result panel.
 */
typedef struct {
  lv_color_t accent;         ///< protocol accent (tints card, value, selection)
  const char *card_icon;     ///< ".bin" path for the card icon (may be NULL)
  const char *card_title;    ///< e.g. "Signal captured"
  const char *card_sub;      ///< e.g. "NEC protocol" (may be NULL)
  const char *card_value;    ///< e.g. "cmd 08 F7" — shown in accent (may be NULL)
  const char *primary_label; ///< "Emulate" / "Send"
  const char *again_label;   ///< "Read again" / "Receive again" / "Capture again"
} capture_result_cfg_t;

/**
 * @brief Runtime handle for a capture-result panel.
 */
typedef struct {
  lv_obj_t *root;                ///< root container of the panel
  lv_obj_t *rows[CAP_ACT_MAX];   ///< action row containers
  lv_obj_t *icons[CAP_ACT_MAX];  ///< action row icon labels
  lv_obj_t *labels[CAP_ACT_MAX]; ///< action row text labels
  int count;                     ///< number of active actions
  int sel;                       ///< index of the highlighted action
  bool saved;                    ///< true once the Save row is confirmed
  lv_color_t accent;             ///< protocol accent colour
} capture_result_t;

/**
 * @brief Build the decision menu (summary card + action list) on a screen that
 *        already has the standard chrome header/footer.
 *
 * Meant to be shown AFTER the calling screen has presented its own
 * captured-signal view for a moment (the card/waveform each protocol draws).
 *
 * @param parent  Screen that already carries the chrome header/footer.
 * @param cfg     Panel configuration (labels, accent, card content).
 * @return The panel handle, returned by value.
 */
capture_result_t capture_result_create(lv_obj_t *parent, const capture_result_cfg_t *cfg);

/**
 * @brief Move the highlight to the next action.
 * @param cr  Panel handle.
 */
void capture_result_next(capture_result_t *cr);

/**
 * @brief Move the highlight to the previous action.
 * @param cr  Panel handle.
 */
void capture_result_prev(capture_result_t *cr);

/**
 * @brief Get the currently highlighted action.
 * @param cr  Panel handle.
 * @return The highlighted action.
 */
capture_action_t capture_result_selected(const capture_result_t *cr);

/**
 * @brief Relabel the Save row to a "Saved" confirmation (call after persisting).
 * @param cr  Panel handle.
 */
void capture_result_mark_saved(capture_result_t *cr);

/**
 * @brief Delete the panel's objects (e.g. before starting a fresh capture).
 * @param cr  Panel handle.
 */
void capture_result_destroy(capture_result_t *cr);

#ifdef __cplusplus
}
#endif

#endif // CAPTURE_RESULT_UI_H
