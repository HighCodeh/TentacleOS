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

# QMI8658A driver

Implementation: `firmware_p4/components/Drivers/qmi8658a`.
The driver owns device registration, register transactions and synchronized
six-axis samples. Acquisition, calibration and presentation are separate:
[IMU service](../imu/README.md), [monitor UI](../ui/README.md#imu-motion-monitor).

V2.03 uses shared SPI3 at 1 MHz with software CS. The shared bus lock covers
identity/readback, configuration and complete samples. Preserve the qualified
cold-start CTRL1 value (0x20); do not reset the shared interface.

`qmi8658a_read()` returns `ESP_ERR_NOT_FINISHED` during the five-second warm-up
or when no fresh sample is available. It leaves caller output untouched on
failure and releases the sample latch through GZ_H even after transport errors.
Stop disables the sensor and releases its SPI device. A failed stop is retryable.

Run `python3 tools/highboy/test_imu_normal.py` for coherent sampling, injected
transaction failures, identity loss, ODR changes and cleanup. Hardware timing
still requires validation on the board.
