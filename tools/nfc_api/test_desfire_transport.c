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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define HB_NFC_ERR_INTERNAL -100
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM -1
#define HB_NFC_ERR_PROTOCOL -2
#define ESP_ERR_INVALID_STATE -3
#define MF_DESFIRE_CLA 0x90
#define MF_DESFIRE_SW_OK 0x9100
#define MF_DESFIRE_SW_MORE 0x91AF
#define MF_DESFIRE_CMD_ADDITIONAL 0xAF
#define ESP_LOGW(tag,...) ((void)(tag))
typedef int hb_nfc_err_t;
typedef struct { bool dep_ready, use_native_frames; bool (*should_cancel)(void); } mf_desfire_session_t;
static unsigned s_frame, s_mode;
static bool s_cancel;
/* CONSTANTS */
/* BUILDER */
static int mf_desfire_apdu_transceive(mf_desfire_session_t *session, const uint8_t *apdu,
                                     size_t length, uint8_t *out, size_t capacity, int timeout) {
  (void)timeout; assert(capacity >= 8);
  assert(apdu[session->use_native_frames ? 0 : 1] == (s_frame ? 0xAF : 0x60));
  assert(length == (session->use_native_frames ? 1 : 5));
  if (s_mode == 3) return 0;
  uint8_t status = s_frame ? 0 : 0xAF;
  if (s_mode == 1) status = 0xAF;
  size_t count = s_mode == 2 ? 0 : 3;
  if (session->use_native_frames) out[0] = status;
  for (size_t i = 0; i < count; i++) out[i + (session->use_native_frames ? 1 : 0)] = s_frame * 3 + i;
  if (!session->use_native_frames) { out[count] = 0x91; out[count + 1] = status; }
  s_frame++;
  if (s_mode == 4) s_cancel = true;
  return count + (session->use_native_frames ? 1 : 2);
}
static int64_t esp_timer_get_time(void) { return 0; }
static void clear_authentication(mf_desfire_session_t *session) { (void)session; }
/* PRODUCTION */
static bool cancelled(void) { return s_cancel; }
int main(void) {
  uint8_t data[16]; size_t length; uint16_t sw;
  for (unsigned native = 0; native < 2; native++) {
    mf_desfire_session_t session = {.dep_ready = true, .use_native_frames = native, .should_cancel = cancelled};
    s_frame = 0; s_mode = 0; s_cancel = false;
    assert(mf_desfire_transceive_native(&session,0x60,NULL,0,data,sizeof(data),&length,&sw) == HB_NFC_OK);
    assert(length == 6 && sw == 0x9100 && data[5] == 5 && s_frame == 2);
    s_frame = 0;
    assert(mf_desfire_transceive_native(&session,0x60,NULL,0,data,4,&length,&sw) == HB_NFC_ERR_PROTOCOL);
    assert(length == 0);
    for (s_mode = 2; s_mode < 5; s_mode++) {
      s_frame = 0; s_cancel = false;
      assert(mf_desfire_transceive_native(&session,0x60,NULL,0,data,sizeof(data),&length,&sw) != HB_NFC_OK);
      assert(length == 0);
    }
    s_cancel = false;
    assert(mf_desfire_transceive_native(&session,0x60,NULL,1,data,sizeof(data),&length,&sw) == HB_NFC_ERR_PARAM);
  }
  puts("DESFire production native/wrapped chaining, overflow, empty frames and cancellation passed");
}
