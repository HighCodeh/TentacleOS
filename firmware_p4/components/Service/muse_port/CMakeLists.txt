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
# Keep the final link enabled for the component LTO objects; architecture
# flags come from the same IDF 6.1 toolchain response file as compilation.
idf_build_replace_option_from_property(LINK_OPTIONS "-fno-lto" "")
idf_build_set_property(LINK_OPTIONS "-flto=2" APPEND)
idf_build_set_property(LINK_OPTIONS "-fuse-linker-plugin" APPEND)
idf_build_set_property(LINK_OPTIONS "-Os" APPEND)
