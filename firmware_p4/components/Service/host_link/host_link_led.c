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

#include "host_link_led.h"

#include "led_control.h"
#include "spi_protocol.h"

uint8_t host_led_handle(uint16_t cmd,
                        const uint8_t *payload,
                        uint16_t plen,
                        uint8_t *out,
                        uint16_t out_cap,
                        uint16_t *out_len) {
  (void)out;
  (void)out_cap;
  *out_len = 0;

  switch (cmd) {
    case SPI_ID_LED_SET_COLOR:
      if (plen < 3)
        return SPI_STATUS_INVALID_ARG;
      led_set_color(payload[0], payload[1], payload[2]);
      return SPI_STATUS_OK;

    case SPI_ID_LED_CLEAR:
      led_clear();
      return SPI_STATUS_OK;

    case SPI_ID_LED_BLINK: {
      if (plen < 5)
        return SPI_STATUS_INVALID_ARG;
      uint16_t dur = (uint16_t)(payload[3] | (payload[4] << 8));
      led_blink(payload[0], payload[1], payload[2], dur);
      return SPI_STATUS_OK;
    }

    case SPI_ID_LED_SIGNAL:
      if (plen < 1)
        return SPI_STATUS_INVALID_ARG;
      if (payload[0] == 0)
        led_signal_info();
      else if (payload[0] == 1)
        led_signal_warning();
      else
        led_signal_error();
      return SPI_STATUS_OK;

    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}
