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

#include "tos_arena.h"

#include "esp_heap_caps.h"

// Each block carries a small header so free/reclaim can find its size and links.
typedef struct node {
  struct node *next;
  struct node *prev;
  size_t size;
} node_t;

struct tos_arena {
  node_t *head;
  size_t used;
  size_t budget;
};

tos_arena_t *tos_arena_create(size_t budget) {
  tos_arena_t *a = heap_caps_malloc(sizeof(*a), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (a == NULL)
    return NULL;
  a->head = NULL;
  a->used = 0;
  a->budget = budget;
  return a;
}

void *tos_arena_alloc(tos_arena_t *a, size_t n) {
  if (a == NULL || n == 0)
    return NULL;
  if (a->used + n > a->budget)
    return NULL;
  node_t *node = heap_caps_malloc(sizeof(node_t) + n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (node == NULL)
    return NULL;
  node->size = n;
  node->prev = NULL;
  node->next = a->head;
  if (a->head != NULL)
    a->head->prev = node;
  a->head = node;
  a->used += n;
  return node + 1;
}

void tos_arena_free(tos_arena_t *a, void *p) {
  if (a == NULL || p == NULL)
    return;
  node_t *node = (node_t *)p - 1;
  if (node->prev != NULL)
    node->prev->next = node->next;
  else
    a->head = node->next;
  if (node->next != NULL)
    node->next->prev = node->prev;
  a->used -= node->size;
  heap_caps_free(node);
}

size_t tos_arena_used(const tos_arena_t *a) {
  return a != NULL ? a->used : 0;
}

void tos_arena_destroy(tos_arena_t *a) {
  if (a == NULL)
    return;
  node_t *n = a->head;
  while (n != NULL) {
    node_t *next = n->next;
    heap_caps_free(n);
    n = next;
  }
  heap_caps_free(a);
}
