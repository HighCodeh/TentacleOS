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

#ifndef RFID_LAB_COMMAND_H
#define RFID_LAB_COMMAND_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RFID_LAB_LINE_BYTES 128U
#define RFID_LAB_MAX_MS 5000U
#define RFID_LAB_STATIC_MAX_MS 1000U
#define RFID_LAB_MIN_HZ 20000U
#define RFID_LAB_MAX_HZ 200000U
#define RFID_LAB_DEFAULT_HZ 125000U
#define RFID_LAB_DEFAULT_DUTY 50U
#define RFID_LAB_SWEEP_MAX_MS 100000U
#define RFID_LAB_SWEEP_MAX_STEPS 100U

typedef enum {
  RFID_LAB_HELP = 0, RFID_LAB_STATUS, RFID_LAB_PINS, RFID_LAB_INFO, RFID_LAB_OFF,
  RFID_LAB_FIELD, RFID_LAB_PULSE, RFID_LAB_GPIO, RFID_LAB_SWEEP,
  RFID_LAB_LISTEN, RFID_LAB_READ, RFID_LAB_RAW, RFID_LAB_EMULATE,
  RFID_LAB_REBOOT, RFID_LAB_ROMBOOT, RFID_LAB_COMMAND_COUNT
} rfid_lab_command_kind_t;
typedef struct {
  rfid_lab_command_kind_t kind;
  uint32_t hz;
  uint32_t end_hz;
  uint32_t step_hz;
  uint32_t duty;
  uint32_t ms;
  uint32_t pin;
  uint32_t level;
  uint32_t invert;
} rfid_lab_command_t;
bool rfid_lab_command_parse(char *line, rfid_lab_command_t *out);
#ifdef __cplusplus
}
#endif
#endif
