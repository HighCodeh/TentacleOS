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

#include "console_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"

#include "audio_i2s.h"

static int cmd_audio(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: audio <volume|tone|chime|click>\n");
    printf("  volume <pct>                 set output volume 0-100\n");
    printf("  tone <freq_hz> <ms> [amp%%]   play a tone (amp 0-100, default 50)\n");
    printf("  chime                        play the UI chime\n");
    printf("  click                        play the UI click\n");
    return 1;
  }

  if (strcmp(argv[1], "volume") == 0) {
    if (argc < 3) {
      printf("usage: audio volume <pct>\n");
      return 1;
    }
    audio_i2s_set_volume((uint8_t)strtoul(argv[2], NULL, 0));
    return 0;
  }

  if (strcmp(argv[1], "tone") == 0) {
    if (argc < 4) {
      printf("usage: audio tone <freq_hz> <ms> [amp%%]\n");
      return 1;
    }
    float freq = (float)strtoul(argv[2], NULL, 0);
    int dur = (int)strtoul(argv[3], NULL, 0);
    float amp = (argc >= 5) ? strtoul(argv[4], NULL, 0) / 100.0f : 0.5f;
    if (amp > 1.0f)
      amp = 1.0f;
    esp_err_t r = audio_i2s_play_tone(freq, dur, amp);
    if (r != ESP_OK) {
      printf("audio: tone failed: %s\n", esp_err_to_name(r));
      return 1;
    }
    return 0;
  }

  if (strcmp(argv[1], "chime") == 0) {
    audio_play_chime();
    return 0;
  }

  if (strcmp(argv[1], "click") == 0) {
    audio_click();
    return 0;
  }

  printf("audio: unknown subcommand '%s'\n", argv[1]);
  return 1;
}

void register_audio_commands(void) {
  const esp_console_cmd_t cmd_audio_def = {
      .command = "audio",
      .help = "Audio: audio volume | tone | chime | click",
      .hint = NULL,
      .func = &cmd_audio,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_console_cmd_register(&cmd_audio_def));
}
