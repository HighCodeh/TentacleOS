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

# IMU motion service

Implementation: `firmware_p4/components/Service/imu`. The service depends on the
[QMI8658A driver](../qmi8658a/README.md) and power manager, never on LVGL or UI
headers. `imu_orientation` contains orientation and calibration mathematics;
`imu_motion` owns the sampling worker, synchronized snapshots and control requests.

`imu_motion_start()` initializes shared state before creating a pinned worker
using `SYS_PRIO_SERVICE_LO` / `SYS_CORE_UI`, as a display-feeding task. Only the
worker accesses the sensor. Consumers copy snapshots through
`imu_motion_snapshot()`; they must not access the private snapshot directly.
Center/calibration requests and stop flags use the same critical section.

The worker holds a no-sleep lease, samples/calibrates motion, and releases sensor
and lease on exit. Stop requests graceful shutdown with a bounded wait and
returns any error. Roll/pitch use gravity correction; yaw is relative and can
drift without a magnetometer. Calibration rejects moving or invalid windows.

The [monitor UI](../ui/README.md#imu-motion-monitor) owns display objects, timer
and model buffers, not the sensor worker's sample storage.

Run `python3 tools/highboy/test_imu_orientation.py` for rotations, recentering,
gravity correction and calibration. `test_imu_motion_ui.py` checks UI lifecycle
with simulated service snapshots; it does not validate physical SPI or RF timing.
