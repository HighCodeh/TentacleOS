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

## Live 3D monitor

- A worker samples/calibrates motion; the LVGL screen renders the High Boy model.
- OK recenters, UP recalibrates, RIGHT switches raw data/3D, BACK exits.
- Roll/pitch use gravity correction; yaw is relative and can drift without a magnetometer.
- Show warm-up, calibration, stale samples and sensor errors explicitly.
- Keep the display awake and hold/release the power lock with the worker lifecycle.
- Load `/assets/models/highboy_imu.bin` into PSRAM; report missing/invalid files.
- The model asset is required along with the P4 application; this change does not flash it.

`tools/highboy/build_imu_mesh.py --check` verifies the lossless CAD-derived asset.
`test_imu_orientation.py` checks rotations, gravity correction and calibration.
`test_imu_motion_ui.py --output /tmp/imu-ui` renders the actual LVGL screen with
synthetic samples and checks controls, lifecycle and missing/corrupt assets.
