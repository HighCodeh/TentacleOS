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

# Game Boy (DMG) emulator — Peanut-GB core + HighBoy platform layer.
file(GLOB GB_SRCS "*.c")   # gb_highboy.c + minigb_apu.c
idf_component_register(
    SRCS ${GB_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."       # peanut_gb.h (single-header core)
    REQUIRES Drivers Service lvgl esp_lcd esp_timer esp_system driver
)

# Sound: enable Peanut-GB audio hooks (gb_highboy.c) and the minigb_apu 16-bit
# sample format (both gb_highboy.c and minigb_apu.c must see the format macro).
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ENABLE_SOUND=1
    MINIGB_APU_AUDIO_FORMAT_S16SYS
)

# Peanut-GB is performance-critical and warns a lot; optimize + silence.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -w)
