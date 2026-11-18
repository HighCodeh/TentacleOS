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

#ifndef LORA_RNODE_UI_H
#define LORA_RNODE_UI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Select the menu parent before navigation; opening never starts RF. */
void ui_rf_lab_from_subghz(bool from_subghz);
/** Open bounded CC1101/SX1262 laboratory controls and live results. */
void ui_lora_rnode_open(void);
void ui_lora_rnode_close(void);

#ifdef __cplusplus
}
#endif

#endif // LORA_RNODE_UI_H
