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

#include "subghz_settings.h"

static subghz_settings_t s_settings = {
    .preset = CC1101_PRESET_OOK_800KHZ,
    .freq = 0,
};

subghz_settings_t subghz_settings_get(void) {
  return s_settings;
}

void subghz_settings_set_preset(cc1101_preset_t preset) {
  s_settings.preset = preset;
}

void subghz_settings_set_freq(uint32_t freq) {
  s_settings.freq = freq;
}
