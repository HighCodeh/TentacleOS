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

// Per-app memory arena. Every allocation an app makes through the ABI is charged
// against a budget and tracked, so the manager can reclaim whatever the app
// leaks when it exits. Accessed only from the owning app's task (no locking).

#ifndef TOS_ARENA_H
#define TOS_ARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct tos_arena tos_arena_t;

/** @brief Create an arena capped at @p budget bytes (excludes per-block overhead). */
tos_arena_t *tos_arena_create(size_t budget);

/** @brief Free every outstanding allocation and the arena itself. */
void tos_arena_destroy(tos_arena_t *a);

/** @brief Allocate @p n bytes; NULL if it would exceed the budget or on OOM. */
void *tos_arena_alloc(tos_arena_t *a, size_t n);

/** @brief Free a block from this arena (NULL and foreign pointers are ignored). */
void tos_arena_free(tos_arena_t *a, void *p);

/** @brief Bytes currently outstanding. */
size_t tos_arena_used(const tos_arena_t *a);

#ifdef __cplusplus
}
#endif

#endif // TOS_ARENA_H
