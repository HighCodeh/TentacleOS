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

#ifndef FLY_LINK_H
#define FLY_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLY_LINK_VERSION              1U
#define FLY_LINK_MODE_TEST            0U
#define FLY_LINK_MODE_VISUAL_3D       1U
#define FLY_LINK_FRAME_CMD            0x0F04U // P4 local: no C5 protocol change
#define FLY_LINK_FRAME_WIDTH          240U
#define FLY_LINK_FRAME_HEIGHT         200U
#define FLY_LINK_FRAME_PIXELS         (FLY_LINK_FRAME_WIDTH * FLY_LINK_FRAME_HEIGHT)
#define FLY_LINK_FRAME_BYTES          (FLY_LINK_FRAME_PIXELS * 2U)
#define FLY_LINK_FRAME_HEADER_SIZE    20U
#define FLY_LINK_FRAME_RESPONSE_SIZE  20U
#define FLY_LINK_FRAME_PAYLOAD_MAX    4000U
#define FLY_LINK_FRAME_CHUNK_PIXELS   4096U
#define FLY_LINK_CONFIG_SIZE          16U
#define FLY_LINK_SNAPSHOT_SIZE        64U
#define FLY_LINK_STOP_SIZE            8U
#define FLY_LINK_RESPONSE_HEADER_SIZE 16U
#define FLY_LINK_EVENT_SIZE           12U
#define FLY_LINK_EVENT_BATCH          4U
#define FLY_LINK_RESPONSE_MAX         64U
#define FLY_LINK_QUEUE_SIZE           16U
#define FLY_LINK_STIMULUS_MAX         4U
#define FLY_LINK_ACTIVITY_MAX         4U
#define FLY_LINK_COORD_MAX            UINT16_MAX
#define FLY_LINK_ACTIVITY_SCALE       10000U
#define FLY_LINK_SPEED_SCALE          1000U
#define FLY_LINK_HEADING_MAX          36000U
#define FLY_LINK_STALE_MS             1000U
#define FLY_LINK_FLAG_PAUSED          1U

typedef enum {
  FLY_LINK_ACTION_SWEET = 1,
  FLY_LINK_ACTION_TOUCH,
  FLY_LINK_ACTION_PAUSE,
  FLY_LINK_ACTION_RESET,
  FLY_LINK_ACTION_CAMERA,
  FLY_LINK_ACTION_SCROLL,
  FLY_LINK_ACTION_COUNT
} fly_link_action_t;

typedef struct {
  uint16_t x;
  uint16_t y;
  uint8_t kind;
  uint8_t strength;
} fly_link_stimulus_t;

typedef struct {
  uint32_t session;
  uint32_t sequence;
  uint32_t sim_time_ms;
  uint16_t x;
  uint16_t y;
  uint16_t heading;
  uint16_t activity[FLY_LINK_ACTIVITY_MAX];
  uint16_t speed_milli;
  uint8_t animation;
  uint8_t stimulus_count;
  uint8_t activity_count;
  uint8_t mode;
  bool is_paused;
  fly_link_stimulus_t stimuli[FLY_LINK_STIMULUS_MAX];
} fly_link_snapshot_t;

typedef struct {
  fly_link_snapshot_t snapshot;
  uint32_t generation;
  uint32_t age_ms;
  uint32_t frame_sequence;
  uint32_t frame_age_ms;
  uint8_t mode;
  uint8_t queued_events;
  bool is_open;
  bool is_connected;
  bool has_snapshot;
  bool is_stale;
  bool has_overflow;
  bool has_frame;
  bool is_frame_stale;
  bool frame_error;
} fly_link_view_t;

void fly_link_init(void);
esp_err_t fly_link_open(void);
void fly_link_close(void);
void fly_link_transport_lost(void);
esp_err_t fly_link_get_view(fly_link_view_t *out_view);
// Copy one complete frame into UI-owned storage. Never returns a live pointer
// into worker storage; generation and sequence must match the view snapshot.
esp_err_t
fly_link_frame_copy(uint32_t generation, uint32_t sequence, uint8_t *out, size_t capacity);
esp_err_t fly_link_enqueue(
    fly_link_action_t action, uint16_t x, uint16_t y, uint16_t value, uint32_t generation);
uint8_t fly_link_handle(uint16_t cmd,
                        const uint8_t *payload,
                        uint16_t plen,
                        uint8_t *out,
                        uint16_t out_cap,
                        uint16_t *out_len);

#ifdef __cplusplus
}
#endif
#endif
