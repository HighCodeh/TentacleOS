# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

# Leaf arbitration layer. Depends only on FreeRTOS + esp_common on purpose so
# both Service and Applications can require it without a dependency cycle.
idf_component_register(SRCS "resource_mgr.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos esp_common)
