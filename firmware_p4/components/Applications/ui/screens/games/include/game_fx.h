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
 * @file game_fx.h
 * @brief Shared sound and haptic cues for the mini-games.
 *
 * Each cue plays a short melody on the speaker (off-thread, self-contained I2S)
 * and fires a DRV2605L haptic effect. Safe to call from the LVGL/game-loop
 * thread.
 */

#ifndef GAME_FX_H
#define GAME_FX_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sound and haptic cue kinds for the mini-games.
 */
typedef enum {
  GFX_FLAP,   ///< Wing flap / move
  GFX_SCORE,  ///< Point gained
  GFX_EAT,    ///< Snake ate food
  GFX_BOUNCE, ///< Ball bounce
  GFX_CRASH,  ///< Collision / game over
  GFX_START,  ///< Round start
} game_fx_t;

/**
 * @brief Play the sound and haptic effect for a cue.
 *
 * @param kind  The cue to play.
 */
void game_fx(game_fx_t kind);

#ifdef __cplusplus
}
#endif

#endif // GAME_FX_H
