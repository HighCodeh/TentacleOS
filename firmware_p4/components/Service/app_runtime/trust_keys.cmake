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

# Configure a build-local trust store without modifying the development header.
option(TOS_APP_PRODUCTION "Require a non-development app-signing public key" OFF)
set(TOS_APP_TRUST_PUBLIC_KEY "" CACHE STRING "Ed25519 public key, 64 hexadecimal characters")
set(TOS_APP_HAS_CUSTOM_TRUST OFF)
set(_trust_key "${TOS_APP_TRUST_PUBLIC_KEY}")
if(_trust_key STREQUAL "")
  if(TOS_APP_PRODUCTION)
    message(FATAL_ERROR "TOS_APP_PRODUCTION requires TOS_APP_TRUST_PUBLIC_KEY")
  endif()
  return()
endif()

string(LENGTH "${_trust_key}" _trust_key_length)
if(NOT _trust_key_length EQUAL 64 OR NOT _trust_key MATCHES "^[0-9A-Fa-f]+$")
  message(FATAL_ERROR "TOS_APP_TRUST_PUBLIC_KEY must contain exactly 64 hexadecimal characters")
endif()
string(TOLOWER "${_trust_key}" _trust_key)
# This public development identity is shipped with a throwaway signing seed.
set(_development_key "8da14861ca1c57ada158421a87483f27d65a72dc40e88fd38598fc6531b7edb3")
if(TOS_APP_PRODUCTION AND _trust_key STREQUAL _development_key)
  message(FATAL_ERROR "The development app-signing key cannot be used for production")
endif()
if(_trust_key MATCHES "^0+$")
  message(FATAL_ERROR "An all-zero app-signing public key is invalid")
endif()

set(TOS_APP_TRUST_BYTES "")
foreach(_offset RANGE 0 62 2)
  string(SUBSTRING "${_trust_key}" ${_offset} 2 _byte)
  string(APPEND TOS_APP_TRUST_BYTES "0x${_byte}, ")
endforeach()
configure_file("${CMAKE_CURRENT_LIST_DIR}/tos_build_trust_keys.h.in"
               "${GENERATED_VERSION_DIR}/tos_build_trust_keys.h" @ONLY)
set(TOS_APP_HAS_CUSTOM_TRUST ON)
