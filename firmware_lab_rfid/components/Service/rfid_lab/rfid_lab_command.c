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

#include "rfid_lab_command.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "pin_def.h"

#define MAX_ARGS 6U
#define KHZ_TO_HZ 1000U
#define DEFAULT_PULSE_HZ 1000U
#define DEFAULT_CAPTURE_MS 1000U
#define DEFAULT_STATIC_MS 100U
#define MIN_DURATION_MS 20U
#define MIN_DUTY 10U
#define MAX_DUTY 90U
#define MAX_PULSE_HZ 10000U
#define DEFAULT_STEP_HZ 10000U

static bool number(const char *text, uint32_t *out) {
  if (text == NULL || text[0] < '0' || text[0] > '9') return false;
  errno = 0;
  char *end;
  unsigned long value = strtoul(text, &end, 10);
  if (errno != 0 || *end != '\0' || value > UINT32_MAX) return false;
  *out = value;
  return true;
}

static bool optional(char **args, size_t count, size_t index, uint32_t *out) {
  return count <= index || number(args[index], out);
}

bool rfid_lab_command_parse(char *line, rfid_lab_command_t *out) {
  if (line == NULL || out == NULL) return false;
  char *args[MAX_ARGS];
  size_t count = 0;
  char *save;
  char *token = strtok_r(line, " \t\r\n", &save);
  while (token != NULL) {
    if (count == MAX_ARGS) return false;
    args[count++] = token;
    token = strtok_r(NULL, " \t\r\n", &save);
  }
  if (count == 0) return false;
  *out = (rfid_lab_command_t){.hz = RFID_LAB_DEFAULT_HZ,
    .duty = RFID_LAB_DEFAULT_DUTY, .ms = RFID_LAB_MAX_MS};
  const char *name = args[0];
  if (!strcmp(name, "help") || !strcmp(name, "ajuda") || !strcmp(name, "?")) out->kind = RFID_LAB_HELP;
  else if (!strcmp(name, "status")) out->kind = RFID_LAB_STATUS;
  else if (!strcmp(name, "pins")) out->kind = RFID_LAB_PINS;
  else if (!strcmp(name, "info")) out->kind = RFID_LAB_INFO;
  else if (!strcmp(name, "off") || !strcmp(name, "parar")) out->kind = RFID_LAB_OFF;
  else if (!strcmp(name, "reboot")) out->kind = RFID_LAB_REBOOT;
  else if (!strcmp(name, "romboot")) {
    out->kind = RFID_LAB_ROMBOOT;
    return count == 2U && !strcmp(args[1], "ARMED");
  } else if (!strcmp(name, "field") || !strcmp(name, "campo")) {
    out->kind = RFID_LAB_FIELD;
    uint32_t khz = RFID_LAB_DEFAULT_HZ / KHZ_TO_HZ;
    if (count > 4U || !optional(args, count, 1, &khz) || khz < RFID_LAB_MIN_HZ / KHZ_TO_HZ ||
        khz > RFID_LAB_MAX_HZ / KHZ_TO_HZ || !optional(args, count, 2, &out->duty) ||
        !optional(args, count, 3, &out->ms)) return false;
    out->hz = khz * KHZ_TO_HZ;
  } else if (!strcmp(name, "pulse") || !strcmp(name, "pulso")) {
    out->kind = RFID_LAB_PULSE;
    out->hz = DEFAULT_PULSE_HZ;
    out->ms = DEFAULT_CAPTURE_MS;
    if (count > 4U || !optional(args, count, 1, &out->hz) || out->hz == 0 || out->hz > MAX_PULSE_HZ ||
        !optional(args, count, 2, &out->duty) || !optional(args, count, 3, &out->ms)) return false;
  } else if (!strcmp(name, "gpio")) {
    out->kind = RFID_LAB_GPIO;
    out->ms = DEFAULT_STATIC_MS;
    if (count < 3U || count > 4U || !number(args[1], &out->pin) ||
        (out->pin != GPIO_RFID_LF_COIL_PIN && out->pin != GPIO_RFID_LF_MOD_PIN) ||
        !number(args[2], &out->level) || out->level > 1 ||
        !optional(args, count, 3, &out->ms) || out->ms > RFID_LAB_STATIC_MAX_MS) return false;
  } else if (!strcmp(name, "sweep") || !strcmp(name, "varrer")) {
    out->kind = RFID_LAB_SWEEP;
    uint32_t start = RFID_LAB_MIN_HZ / KHZ_TO_HZ;
    uint32_t end = RFID_LAB_MAX_HZ / KHZ_TO_HZ;
    uint32_t step = DEFAULT_STEP_HZ / KHZ_TO_HZ;
    if (count != 1 && count != 5U) return false;
    if (!optional(args, count, 1, &start) || !optional(args, count, 2, &end) ||
        !optional(args, count, 3, &step) || !optional(args, count, 4, &out->ms) ||
        start < RFID_LAB_MIN_HZ / KHZ_TO_HZ || end > RFID_LAB_MAX_HZ / KHZ_TO_HZ ||
        end < start || step == 0 || step > RFID_LAB_MAX_HZ / KHZ_TO_HZ ||
        (end - start) % step != 0) return false;
    uint32_t steps = (end - start) / step + 1U;
    if (steps > RFID_LAB_SWEEP_MAX_STEPS || (uint64_t)steps * out->ms > RFID_LAB_SWEEP_MAX_MS) return false;
    out->hz = start * KHZ_TO_HZ;
    out->end_hz = end * KHZ_TO_HZ;
    out->step_hz = step * KHZ_TO_HZ;
  } else if (!strcmp(name, "listen") || !strcmp(name, "read") || !strcmp(name, "ler") ||
             !strcmp(name, "raw") || !strcmp(name, "emulate")) {
    out->kind = !strcmp(name, "listen") ? RFID_LAB_LISTEN :
      !strcmp(name, "raw") ? RFID_LAB_RAW :
      !strcmp(name, "emulate") ? RFID_LAB_EMULATE : RFID_LAB_READ;
    if (out->kind == RFID_LAB_LISTEN || out->kind == RFID_LAB_RAW) out->ms = DEFAULT_CAPTURE_MS;
    if (count > (out->kind == RFID_LAB_EMULATE ? 3U : 2U) ||
        !optional(args, count, 1, &out->ms) || !optional(args, count, 2, &out->invert) ||
        out->invert > 1) return false;
  } else return false;
  if (out->kind <= RFID_LAB_OFF || out->kind == RFID_LAB_REBOOT) return count == 1;
  return out->ms >= MIN_DURATION_MS && out->ms <= RFID_LAB_MAX_MS &&
    out->duty >= MIN_DUTY && out->duty <= MAX_DUTY;
}
