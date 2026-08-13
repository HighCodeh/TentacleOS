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

#ifndef IR_CONTROLLER_UI_H
#define IR_CONTROLLER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Appliance layout the remote controller renders.
 */
typedef enum {
  IR_DEV_TV,    ///< Television remote layout
  IR_DEV_SOUND, ///< Sound system remote layout
  IR_DEV_AC,    ///< Air conditioner remote layout
} ir_device_t;

/**
 * @brief Choose which appliance layout the next ui_ir_controller_open() shows.
 *
 * @param dev  Appliance layout to render on the next open.
 */
void ui_ir_controller_set_device(ir_device_t dev);

/**
 * @brief Open the infrared remote controller screen.
 */
void ui_ir_controller_open(void);

#ifdef __cplusplus
}
#endif

#endif // IR_CONTROLLER_UI_H
