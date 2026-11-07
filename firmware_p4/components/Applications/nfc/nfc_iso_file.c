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

#include "nfc_iso_file.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "esp_log.h"

#define HEX_SHIFT      4U
#define HEX_ALPHA      10U
#define UID_SHORT      4U
#define UID_LONG       7U
#define ISO_DEP_SAK    0x20U
#define ATS_INTERFACE  0x10U
#define ATS_INTERFACES 3U
#define ATS_RESERVED   0x80U
#define ATS_FSCI_MASK  0x0FU
#define ATS_MAX_FSCI   8U

static const char *TAG = "NFC_ISO_FILE";

static int digit(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  value = (char)tolower((unsigned char)value);
  return value >= 'a' && value <= 'f' ? value - 'a' + HEX_ALPHA : -1;
}

bool nfc_iso_file_hex(const char *text, uint8_t *out, size_t capacity, size_t *out_size) {
  if (text == NULL)
    return false;
  size_t used = 0;
  while (*text) {
    while (isspace((unsigned char)*text))
      text++;
    if (!*text)
      break;
    if (used == capacity || !text[1])
      return false;
    int high = digit(text[0]), low = digit(text[1]);
    if (high < 0 || low < 0)
      return false;
    out[used++] = (uint8_t)((high << HEX_SHIFT) | low);
    text += 2;
    if (*text && !isspace((unsigned char)*text))
      return false;
  }
  *out_size = used;
  return true;
}

bool nfc_iso_file_valid_identity(const t4t_emu_identity_t *identity) {
  if ((identity->uid_len != UID_SHORT && identity->uid_len != UID_LONG) ||
      !(identity->sak & ISO_DEP_SAK) || !identity->ats_len ||
      identity->ats_len > T4T_EMU_ATS_CAPACITY || identity->ats[0] != identity->ats_len)
    return false;
  if (identity->ats_len == 1)
    return true;
  uint8_t t0 = identity->ats[1];
  if ((t0 & ATS_RESERVED) || (t0 & ATS_FSCI_MASK) > ATS_MAX_FSCI)
    return false;
  size_t minimum = 2;
  for (size_t i = 0; i < ATS_INTERFACES; i++)
    if (t0 & (ATS_INTERFACE << i))
      minimum++;
  return identity->ats_len >= minimum;
}

bool nfc_iso_file_parse_identity(char *const *values, t4t_emu_identity_t *identity) {
  size_t size = 0;
  if (!nfc_iso_file_hex(values[0], identity->uid, sizeof(identity->uid), &size))
    return false;
  identity->uid_len = size;
  uint8_t atqa[2];
  if (!nfc_iso_file_hex(values[1], atqa, sizeof(atqa), &size) || size != sizeof(atqa))
    return false;
  identity->atqa[0] = atqa[1];
  identity->atqa[1] = atqa[0];
  if (!nfc_iso_file_hex(values[2], &identity->sak, 1, &size) || size != 1)
    return false;
  size_t pos = 1;
  if (values[3] != NULL) {
    if (!nfc_iso_file_hex(values[3], &identity->ats[pos++], 1, &size) || size != 1)
      return false;
    for (size_t i = 0; i < ATS_INTERFACES; i++) {
      bool has_interface = (identity->ats[1] & (ATS_INTERFACE << i)) != 0;
      if (has_interface != (values[4 + i] != NULL))
        return false;
      if (has_interface &&
          (!nfc_iso_file_hex(values[4 + i], &identity->ats[pos++], 1, &size) || size != 1))
        return false;
    }
    if (values[7] != NULL) {
      if (!nfc_iso_file_hex(values[7], identity->ats + pos, sizeof(identity->ats) - pos, &size))
        return false;
      pos += size;
    }
  } else if (values[4] || values[5] || values[6] || values[7])
    return false;
  identity->ats_len = pos;
  identity->ats[0] = pos;
  if (!nfc_iso_file_valid_identity(identity))
    return false;
  return true;
}

bool nfc_iso_file_emit(
    char **cursor, size_t *remaining, const char *key, const uint8_t *data, size_t size) {
  int count = snprintf(*cursor, *remaining, "%s:", key);
  if (count < 0 || (size_t)count >= *remaining)
    return false;
  *cursor += count;
  *remaining -= count;
  for (size_t i = 0; i < size; i++) {
    count = snprintf(*cursor, *remaining, " %02X", data[i]);
    if (count < 0 || (size_t)count >= *remaining)
      return false;
    *cursor += count;
    *remaining -= count;
  }
  if (*remaining <= 1)
    return false;
  *(*cursor)++ = '\n';
  **cursor = 0;
  (*remaining)--;
  return true;
}

bool nfc_iso_file_begin(char **cursor,
                        size_t *remaining,
                        const char *device,
                        const t4t_emu_identity_t *identity) {
  if (!cursor || !*cursor || !remaining || !device || !identity) {
    ESP_LOGE(TAG, "Missing file header input");
    return false;
  }
  int count = snprintf(*cursor,
                       *remaining,
                       "# Copyright (c) 2025 HIGH CODE LLC\n#\n"
                       "# TentacleOS is free software: you can redistribute it and/or modify\n"
                       "# it under the terms of the GNU General Public License as published by\n"
                       "# the Free Software Foundation, either version 3 of the License, or\n"
                       "# (at your option) any later version.\n#\n"
                       "# TentacleOS is distributed in the hope that it will be useful,\n"
                       "# but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                       "# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
                       "# GNU General Public License for more details.\n#\n"
                       "# You should have received a copy of the GNU General Public License\n"
                       "# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.\n"
                       "Filetype: Flipper NFC device\nVersion: 4\nDevice type: %s\n",
                       device);
  if (count < 0 || (size_t)count >= *remaining)
    return false;
  *cursor += count;
  *remaining -= count;
  static const char *const interface_names[] = {"TA(1)", "TB(1)", "TC(1)"};
  uint8_t atqa[] = {identity->atqa[1], identity->atqa[0]};
#define EMIT(key, data, size)                                   \
  do {                                                          \
    if (!nfc_iso_file_emit(cursor, remaining, key, data, size)) \
      return false;                                             \
  } while (0)
  EMIT("UID", identity->uid, identity->uid_len);
  EMIT("ATQA", atqa, sizeof(atqa));
  EMIT("SAK", &identity->sak, 1);
  if (identity->ats_len > 1) {
    size_t pos = 1;
    EMIT("T0", &identity->ats[pos++], 1);
    for (size_t i = 0; i < ATS_INTERFACES; i++) {
      if (identity->ats[1] & (ATS_INTERFACE << i))
        EMIT(interface_names[i], &identity->ats[pos++], 1);
    }
    if (pos < identity->ats_len)
      EMIT("T1...Tk", identity->ats + pos, identity->ats_len - pos);
  }
#undef EMIT
  return true;
}
