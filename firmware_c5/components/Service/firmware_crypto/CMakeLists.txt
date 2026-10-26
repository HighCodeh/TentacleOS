# Copyright (c) 2025 HIGH CODE LLC
# SPDX-License-Identifier: GPL-3.0-or-later
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

set(CRYPTO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../common/crypto")
idf_component_register(SRCS "${CRYPTO_DIR}/firmware_crypto.c"
                       INCLUDE_DIRS "${CRYPTO_DIR}" REQUIRES mbedtls)
