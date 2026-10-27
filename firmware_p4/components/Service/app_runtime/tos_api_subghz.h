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

#ifndef TOS_API_SUBGHZ_H
#define TOS_API_SUBGHZ_H

#include "tos_api.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tos_api_subghz_receive(tos_subghz_signal_t *out,
                                 uint32_t frequency,
                                 uint8_t preset,
                                 uint32_t timeout_ms);
esp_err_t tos_api_subghz_tx_raw(const int32_t *timings, int count, uint32_t frequency);
esp_err_t
tos_api_subghz_spectrum_capture(tos_subghz_spectrum_t *out, uint32_t center, uint32_t span);
int tos_api_subghz_get_raw(int32_t *out, int max);
esp_err_t tos_api_subghz_replay(const char *path);
esp_err_t tos_api_subghz_rx_start(uint32_t frequency, uint8_t preset, uint32_t *out_session);
esp_err_t tos_api_subghz_spectrum_start(uint32_t center, uint32_t span, uint32_t *out_session);
int tos_api_subghz_session_poll(tos_subghz_signal_t *out, bool *out_has_session);
int tos_api_subghz_spectrum_poll(uint32_t *out_start, uint32_t *out_step, float *out_dbm, int max);
esp_err_t tos_api_subghz_set_frequency(uint32_t frequency);
esp_err_t tos_api_subghz_session_stop(uint32_t session);
// Nonblocking binding teardown: detaches the owner; firmware worker finishes hardware cleanup.
void tos_api_subghz_cancel_owner(void *task);

#ifdef __cplusplus
}
#endif

#endif
