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

// Central task scheduling policy for TentacleOS (dual-core ESP32-P4).
//
// Two levers keep the UI responsive: priority bands and core affinity. Priority
// keeps hard real-time work above the renderer and the renderer above services;
// core affinity keeps the renderer off the core that radios and USB streaming
// saturate. Every xTaskCreate site picks a band and a core from this file so the
// policy lives in one place instead of 50 scattered defines.
//
// CONFIG_FREERTOS_UNICORE is not set, so both cores are available. The UI core
// runs the LVGL renderer plus everything that feeds the screen (media playback,
// capture, UI helper tasks). The radio core runs radios, host link, bridge,
// storage and background services. Pinning the two apart is what removes most of
// the jank; the priority bands settle contention within a core.

#define SYS_CORE_UI    1              // LVGL render, media, UI helper tasks
#define SYS_CORE_RADIO 0              // radios, host link, bridge, storage, monitor
#define SYS_CORE_ANY   tskNO_AFFINITY // let the scheduler place the task

// Priority bands. Higher number is higher priority (FreeRTOS convention).
//
//   SYS_PRIO_REALTIME     deferred ISR / hard real-time (radio IRQ)
//   SYS_PRIO_RENDER       LVGL renderer, pinned to SYS_CORE_UI
//   SYS_PRIO_SERVICE_HI   latency-sensitive services (host link, radio rx/tx, streaming)
//   SYS_PRIO_SERVICE_LO   regular services (media playback, capture, UI helpers)
//   SYS_PRIO_BACKGROUND   periodic polling, logging, telemetry
//   SYS_PRIO_BACKGROUND_LO lowest non-idle background work
//   SYS_PRIO_MONITOR      health monitor

#define SYS_PRIO_REALTIME      10
#define SYS_PRIO_RENDER        6
#define SYS_PRIO_SERVICE_HI    5
#define SYS_PRIO_SERVICE_LO    4
#define SYS_PRIO_BACKGROUND    3
#define SYS_PRIO_BACKGROUND_LO 2
#define SYS_PRIO_MONITOR       1

#endif  // SYS_PRIO_H
