<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# QMI8658A motion sensor

- V2.03 uses the shared SPI3 bus at 1 MHz with software-controlled CS.
- Preserve the qualified cold-start CTRL1 value (0x20); avoid rewriting/resetting the interface.
- Check identity and register readback while holding the shared bus lock.
- Enable synchronized six-axis sampling; read individual registers through GZ_H.
- Return `ESP_ERR_NOT_FINISHED` during the five-second warm-up or without new data.
- Leave caller output untouched on failure; unlock the sample on transport errors.
- Stop disables the sensors and releases the SPI device; a failed stop remains retryable.

Run `python3 tools/highboy/test_imu_normal.py` for injected transaction failures,
identity loss, coherent samples, ODR changes and cleanup. Hardware timing still
requires validation on the target board.
