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

#ifndef BOARD_SERVICE_H
#define BOARD_SERVICE_H

#include "esp_err.h"
#include "spi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t length;
  uint8_t data[sizeof(spi_board_caps_t)];
} board_service_reply_t;

esp_err_t board_service_init(void);
spi_status_t board_service_dispatch(spi_id_t id,
                                    const uint8_t *data,
                                    uint8_t length,
                                    board_service_reply_t *out_reply);

#ifdef __cplusplus
}
#endif
#endif
