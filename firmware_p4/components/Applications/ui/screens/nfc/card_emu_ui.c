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

#include "card_emu_ui.h"

#include "esp_log.h"

#include "nfc_emulate_ui.h"

static const char *TAG = "CARD_EMU_UI";

void ui_card_emu_open(void) {
  ESP_LOGD(TAG, "Opening native MIFARE emulation");
  ui_nfc_emulate_open();
}
