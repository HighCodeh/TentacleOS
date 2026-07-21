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

#ifndef UI_FEEDBACK_H
#define UI_FEEDBACK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI audio + haptic feedback cue identifiers.
 *
 * Sound plays on navigation and on every function read/write/emulate;
 * vibration is added only on function results (read/write/emulate), never on
 * plain button presses. Cues run on a short-lived worker task so UI callers
 * never block, and overlapping cues are dropped while one is still playing.
 */
typedef enum {
  UI_FB_NAV = 0, ///< Menu item changed: short tick, no vibration.
  UI_FB_SELECT,  ///< Open/confirm: soft blip, no vibration.
  UI_FB_READ,    ///< Function READ succeeded: rising tone + vibration.
  UI_FB_WRITE,   ///< Function WRITE/SAVE succeeded: two-tone + vibration.
  UI_FB_EMULATE, ///< Function is EMULATING: pulse + vibration.
  UI_FB_BOOT,    ///< Startup chime, no vibration.
  UI_FB_COUNT    ///< Sentinel; number of cue kinds.
} ui_feedback_kind_t;

/**
 * @brief Initialize the feedback subsystem.
 *
 * Idempotent; call once at UI init.
 */
void ui_feedback_init(void);

/**
 * @brief Fire a feedback cue.
 *
 * Non-blocking and safe to call from any UI callback. The cue is dropped if
 * another cue is already playing or if @p kind is out of range.
 *
 * @param kind  Cue to play.
 */
void ui_feedback(ui_feedback_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif // UI_FEEDBACK_H
