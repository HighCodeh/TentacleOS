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

# Actual portable controller and unmodified official Muse pixel renderer.
idf_component_register(SRCS
  "p4_app/hb_muse_app.c" "core/hb_muse_core.c"
  "transport/hb_muse_wire.c" "transport/hb_muse_wire_client.c"
  "official/muse_pixel.c"
  INCLUDE_DIRS "." "p4_app" "core" "transport" "official")
# Only these newly introduced modules contain LTO IR. Fat objects retain normal
# target code for inspection. Existing TentacleOS objects remain unchanged.
# Two LTO workers bound memory use; no fast-math or renderer source changes.
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -flto=2 -ffat-lto-objects)
# -Os here applies only when GCC compiles the new Muse LTO IR at final link;
# already compiled non-LTO games, drivers and other apps are not reoptimized.
# IDF 5.5 appends -fno-lto globally. GCC 14's driver suppresses the linker
# plugin whenever that option remains present, even before a later -flto=2;
# a fat object then silently falls back to its unchanged non-LTO code.
# Remove only this final-link veto using IDF's supported property API.
idf_build_replace_option_from_property(LINK_OPTIONS "-fno-lto" "")
idf_build_set_property(LINK_OPTIONS "-flto=2" APPEND)
idf_build_set_property(LINK_OPTIONS "-fuse-linker-plugin" APPEND)
idf_build_set_property(LINK_OPTIONS "-Os" APPEND)
# The existing P4 project appends this architecture to ALL source compiles,
# while IDF's default link still advertises xesploop_xespv. GCC 14 LTO emits
# invalid per-function .option arch when those two extension names differ.
# Match the already-selected source architecture for just the LTO final pass;
# ABI and every existing non-LTO machine-code object remain unchanged.
idf_build_set_property(LINK_OPTIONS "-march=rv32imafc_zicsr_zifencei_xesppie" APPEND)
