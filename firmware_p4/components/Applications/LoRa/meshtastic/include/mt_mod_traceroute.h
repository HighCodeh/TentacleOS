// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MT_MOD_TRACEROUTE_H
#define MT_MOD_TRACEROUTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "mt_modules.h"

/**
 * @brief Initialize the TraceRouteModule.
 */
void mt_mod_traceroute_init(uint32_t node_num);

/**
 * @brief Handle an incoming RouteDiscovery.
 *
 * If we are the destination and want_response is set, replies with
 * the accumulated route plus our own node number. Otherwise forwards
 * by relying on the mesh core's flood rebroadcast.
 */
void mt_mod_traceroute_on_received(const mt_packet_meta_t *meta,
                                    const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // MT_MOD_TRACEROUTE_H
