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

# Official DOOM (doomgeneric) as an ESP-IDF component for the HighBoy P4.
# All engine sources + doomgeneric.c + the HighBoy platform layer compile here.
file(GLOB DOOM_SRCS "*.c")

idf_component_register(
    SRCS ${DOOM_SRCS}
    INCLUDE_DIRS "include"        # public: only doom_highboy.h (launcher entry)
    PRIV_INCLUDE_DIRS "."         # private: doomgeneric internal headers (config.h, etc.)
    REQUIRES Drivers Service esp_lcd esp_timer esp_system driver
    LDFRAGMENTS "linker.lf"       # park DOOM's ~300 KB static .bss in PSRAM
)

# Render at DOOM's native internal resolution (no upscale). ALL sources must
# agree, so define it component-wide.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    DOOMGENERIC_RESX=320
    DOOMGENERIC_RESY=200
    FEATURE_SOUND=1        # enable SFX (DG_sound_module in doom_sound_highboy.c)
)

# Legacy id-Software C: silence its many warnings and optimize for framerate
# (the project default is -Og; DOOM needs -O2 to be playable).
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -w)
