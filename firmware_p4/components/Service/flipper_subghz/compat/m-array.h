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

#include "flipper_subghz_compat.h"
#define M_POD_OPLIST      0
#define ARRAY_OPLIST(...) 0
#define ARRAY_DEF(name, type, ops) \
  typedef struct {                 \
    type *items;                   \
    size_t count;                  \
  } name##_t[1];
#define M_EACH(item, array, type)                                                                 \
  (SubGhzKey *item = (array)[0].items, *item##_end = item + (array)[0].count; item != item##_end; \
   ++item)
