// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include "driver/ledc.h"

// Pino do buzzer (configurar conforme seu hardware)
#define BUZZER_GPIO 41

#define LEDC_CHANNEL         LEDC_CHANNEL_1
#define LEDC_TIMER           LEDC_TIMER_1
#define LEDC_MODE            LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RESOLUTION LEDC_TIMER_13_BIT
#define LEDC_FREQ            4000  // Frequência PWM inicial (Hz)

// Protótipos das funções
void buzzer_save_config(void);
void buzzer_load_config(void);
void buzzer_set_volume(int volume);
void buzzer_set_enabled(bool enabled);
int buzzer_get_volume(void);
bool buzzer_is_enabled(void);

esp_err_t buzzer_init(void);
void buzzer_play_tone(uint32_t freq_hz, uint32_t duration_ms);

esp_err_t buzzer_play_sound_file(const char *sound_name);

#endif // BUZZER_H