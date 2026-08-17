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

#ifndef MT_MOD_TRACEROUTE_H
#define MT_MOD_TRACEROUTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "mt_modules.h"

#define MT_TRACE_MAX_HOPS 8

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
                                   const uint8_t *payload,
                                   uint16_t len);

/**
 * @brief Send a TraceRoute request toward @p to (want_response set).
 *
 * Records @p to as the pending target; the reply is captured for the UI.
 */
void mt_mod_traceroute_start(uint32_t to);

/** @brief True while a traceroute we started is awaiting its reply. */
bool mt_mod_traceroute_is_pending(void);

/**
 * @brief Fetch the most recent completed traceroute result.
 *
 * @param out_hops    Buffer of at least MT_TRACE_MAX_HOPS entries (route node nums).
 * @param out_count   Receives the number of hops written.
 * @param out_target  Receives the responding node number.
 * @return true if a result is ready (and copies it); false otherwise.
 */
bool mt_mod_traceroute_get_result(uint32_t *out_hops, int *out_count, uint32_t *out_target);

#ifdef __cplusplus
}
#endif

#endif // MT_MOD_TRACEROUTE_H
