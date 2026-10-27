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

#include "subghz_protocol_decoder.h"

#include <stdio.h>
#include <string.h>

#include "subghz_keeloq.h"

// Registry adapter for the KeeLoq/HCS301 rolling-code module. decode() demodulates
// the HCS301 frame and brute-forces the loaded manufacturer keys to recover the
// serial/button/counter; encode() replays the NEXT code (counter + 1), which is
// the rolling-code prediction attack.

#define KEELOQ_MIN_PULSES 130 // preamble + header + 64 data bits (2 pulses each)
#define KEELOQ_NAME_PREFIX "KeeLoq "

static char s_name_buf[40];

static bool protocol_keeloq_decode(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  if (count < KEELOQ_MIN_PULSES) {
    return false;
  }
  uint32_t fix, hop;
  if (!subghz_keeloq_pwm_decode(pulses, count, &fix, &hop)) {
    return false;
  }
  if (subghz_keeloq_load_mfkeys() <= 0) {
    return false;
  }
  keeloq_result_t res;
  uint64_t key = ((uint64_t)fix << 32) | hop;
  if (!subghz_keeloq_decode_key(key, &res)) {
    return false;
  }

  snprintf(s_name_buf, sizeof(s_name_buf), KEELOQ_NAME_PREFIX "%s", res.mfname);
  out_data->protocol_name = s_name_buf;
  out_data->bit_count = 64;
  out_data->serial = res.serial;
  out_data->btn = res.button;
  out_data->raw_value = res.counter;
  return true;
}

static size_t
protocol_keeloq_encode(const subghz_data_t *data, int32_t *pulses, size_t max_count) {
  const char *mfname = data->protocol_name;
  if (mfname == NULL) {
    return 0;
  }
  if (strncmp(mfname, KEELOQ_NAME_PREFIX, strlen(KEELOQ_NAME_PREFIX)) == 0) {
    mfname += strlen(KEELOQ_NAME_PREFIX);
  }
  if (subghz_keeloq_load_mfkeys() <= 0) {
    return 0;
  }

  const keeloq_mfkey_t *mf = NULL;
  for (int i = 0; i < subghz_keeloq_mfkey_count(); i++) {
    const keeloq_mfkey_t *k = subghz_keeloq_mfkey_at(i);
    if (k != NULL && strcmp(k->name, mfname) == 0) {
      mf = k;
      break;
    }
  }
  if (mf == NULL) {
    return 0;
  }

  uint16_t next = (uint16_t)(data->raw_value + 1); // rolling-code prediction
  return subghz_keeloq_encode(data->serial, data->btn, next, mf->key, mf->type, pulses, max_count);
}

subghz_protocol_t protocol_keeloq = {
    .name = "KeeLoq", .decode = protocol_keeloq_decode, .encode = protocol_keeloq_encode};
