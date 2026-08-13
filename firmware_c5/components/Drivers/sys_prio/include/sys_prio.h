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

#ifndef SYS_PRIO_H
#define SYS_PRIO_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Central task scheduling policy for TentacleOS on the ESP32-C5 radio
// co-processor. Every xTaskCreate site draws its priority from here instead of a
// scattered magic number, so the whole policy lives in one file.
//
// The C5 is single-core (CONFIG_FREERTOS_UNICORE=y) and headless, so unlike the
// P4 there is no render band and no UI/radio core split: every task runs on the
// one core. The core macros exist only so the few pinned creates stay readable.

#define SYS_CORE_MAIN 0              // the only core on the C5
#define SYS_CORE_ANY  tskNO_AFFINITY // let the scheduler place the task

// Priority bands. Higher number is higher priority (FreeRTOS convention).
//
//   SYS_PRIO_REALTIME     deferred ISR / hard real-time (the SPI bridge to the P4)
//   SYS_PRIO_SERVICE_HI   radio services and app tasks (Wi-Fi/BLE ops, scanners, DNS, OTA)
//   SYS_PRIO_SERVICE_LO   lower-priority services (host-link logging)
//   SYS_PRIO_BACKGROUND   periodic polling, telemetry
//   SYS_PRIO_BACKGROUND_LO lowest non-idle background work
//   SYS_PRIO_MONITOR      health monitor

#define SYS_PRIO_REALTIME      10
#define SYS_PRIO_SERVICE_HI    5
#define SYS_PRIO_SERVICE_LO    4
#define SYS_PRIO_BACKGROUND    3
#define SYS_PRIO_BACKGROUND_LO 2
#define SYS_PRIO_MONITOR       1

#endif  // SYS_PRIO_H
