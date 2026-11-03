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

#define board_service_init c5_init
#include "board_service.c"

int test_c5_init(void) { return c5_init(); }
int test_c5_rpc(spi_id_t id, const void *data, uint16_t len, void *out, uint8_t *out_len) {
  board_service_reply_t reply;
  int status = board_service_dispatch(id, data, len, &reply);
  memcpy(out, reply.data, reply.length);
  *out_len = reply.length;
  return status;
}
void test_c5_expire(void) { watchdog(NULL); }
void test_c5_reboot(void) {
  s_client = 0;
  s_deadline = 0;
  s_epoch = new_epoch();
  safe_state();
}
