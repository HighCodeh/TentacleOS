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

#include "input_manager.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "pin_def.h"

static const char *TAG = "INPUT_MGR";

#define BUTTON_PRESSED_LEVEL   0
#define SAMPLE_PERIOD_MS       5    // sampler tick; independent of the render loop
#define DEBOUNCE_SAMPLES       4    // stable samples required to accept a level (~20 ms)
#define LONG_PRESS_MS          800  // held this long fires LONG_PRESS once
#define REPEAT_DELAY_MS        400  // first REPEAT after a press
#define REPEAT_PERIOD_MS       120  // subsequent REPEAT interval
#define EVENT_QUEUE_LEN        16

static const uint32_t s_button_gpio[INPUT_BTN_COUNT] = {
    [INPUT_BTN_UP] = GPIO_BTN_UP_PIN,       [INPUT_BTN_DOWN] = GPIO_BTN_DOWN_PIN,
    [INPUT_BTN_LEFT] = GPIO_BTN_LEFT_PIN,   [INPUT_BTN_RIGHT] = GPIO_BTN_RIGHT_PIN,
    [INPUT_BTN_OK] = GPIO_BTN_OK_PIN,       [INPUT_BTN_BACK] = GPIO_BTN_BACK_PIN,
};

static const char *const s_button_name[INPUT_BTN_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK",
};

typedef struct {
  bool debounced;          // accepted (debounced) held state
  bool candidate;          // raw level being counted toward a change
  uint8_t stable_count;    // consecutive samples matching candidate
  bool long_fired;         // LONG_PRESS already emitted this press
  uint32_t press_ms;       // when the current press was accepted
  uint32_t next_repeat_ms; // when the next REPEAT is due
} button_state_t;

static button_state_t s_state[INPUT_BTN_COUNT];
static volatile bool s_held[INPUT_BTN_COUNT];         // read by input_is_down (shim path)
static volatile bool s_press_latch[INPUT_BTN_COUNT];  // consumed by input_consume_press
static volatile int64_t s_sim_until_us[INPUT_BTN_COUNT];
static volatile uint32_t s_last_activity_ms;

static QueueHandle_t s_event_queue;
static esp_timer_handle_t s_sample_timer;
static bool s_started;

static inline uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline bool sim_active(input_button_t b) {
  return esp_timer_get_time() < s_sim_until_us[b];
}

static void emit(input_button_t button, input_action_t action, uint32_t t_ms) {
  s_last_activity_ms = t_ms;
  if (s_event_queue == NULL) {
    return;
  }
  input_event_t ev = {.button = button, .action = action, .timestamp_ms = t_ms};
  // Never block the sampler: drop the oldest-visible event by simply failing if
  // the consumer has fallen behind (a full queue means nobody is reading).
  if (xQueueSend(s_event_queue, &ev, 0) != pdTRUE) {
    ESP_LOGW(TAG, "event queue full; dropping %s/%d", s_button_name[button], (int)action);
  }
}

static void sample_button(input_button_t b, uint32_t t_ms) {
  bool raw = (gpio_get_level(s_button_gpio[b]) == BUTTON_PRESSED_LEVEL) || sim_active(b);
  button_state_t *st = &s_state[b];

  // Debounce: a level must hold for DEBOUNCE_SAMPLES ticks to be accepted.
  if (raw != st->debounced) {
    if (raw == st->candidate) {
      if (++st->stable_count >= DEBOUNCE_SAMPLES) {
        st->debounced = raw;
        st->stable_count = 0;
        s_held[b] = raw;

        if (raw) {
          st->long_fired = false;
          st->press_ms = t_ms;
          st->next_repeat_ms = t_ms + REPEAT_DELAY_MS;
          s_press_latch[b] = true;
          emit(b, INPUT_ACTION_PRESS, t_ms);
        } else {
          emit(b, INPUT_ACTION_RELEASE, t_ms);
        }
      }
    } else {
      st->candidate = raw;
      st->stable_count = 1;
    }
  } else {
    st->candidate = raw;
    st->stable_count = 0;
  }

  // Long-press and auto-repeat while held. Signed differences are wrap-safe.
  if (st->debounced) {
    if (!st->long_fired && (int32_t)(t_ms - st->press_ms) >= LONG_PRESS_MS) {
      st->long_fired = true;
      emit(b, INPUT_ACTION_LONG_PRESS, t_ms);
    }
    if ((int32_t)(t_ms - st->next_repeat_ms) >= 0) {
      st->next_repeat_ms = t_ms + REPEAT_PERIOD_MS;
      emit(b, INPUT_ACTION_REPEAT, t_ms);
    }
  }
}

static void sample_timer_cb(void *arg) {
  (void)arg;
  uint32_t t_ms = now_ms();
  for (input_button_t b = 0; b < INPUT_BTN_COUNT; b++) {
    sample_button(b, t_ms);
  }
}

esp_err_t input_configure_wake_source(void) {
  esp_err_t err = esp_sleep_enable_gpio_wakeup();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_sleep_enable_gpio_wakeup: %s", esp_err_to_name(err));
    return err;
  }
  for (input_button_t b = 0; b < INPUT_BTN_COUNT; b++) {
    esp_err_t e = gpio_wakeup_enable(s_button_gpio[b], GPIO_INTR_LOW_LEVEL);
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "gpio_wakeup_enable(%s): %s", s_button_name[b], esp_err_to_name(e));
      err = e;
    }
  }
  return err;
}

esp_err_t input_manager_init(void) {
  if (s_started) {
    return ESP_OK;
  }

  uint64_t pin_mask = 0;
  for (input_button_t b = 0; b < INPUT_BTN_COUNT; b++) {
    pin_mask |= (1ULL << s_button_gpio[b]);
  }
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = pin_mask,
      .pull_down_en = 0,
      .pull_up_en = 1,
  };
  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "gpio_config: %s", esp_err_to_name(err));
    return err;
  }

  // Seed the debounced state from the current level so a button already held at
  // boot does not synthesize a spurious PRESS. A boot-held button is treated as
  // pressed at boot (press_ms = now), so long-press/repeat time from here rather
  // than firing immediately - this is what a boot key-combo would rely on.
  uint32_t t_ms = now_ms();
  for (input_button_t b = 0; b < INPUT_BTN_COUNT; b++) {
    bool level = (gpio_get_level(s_button_gpio[b]) == BUTTON_PRESSED_LEVEL);
    s_state[b] = (button_state_t){
        .debounced = level,
        .candidate = level,
        .press_ms = t_ms,
        .next_repeat_ms = t_ms + REPEAT_DELAY_MS,
    };
    s_held[b] = level;
  }

  s_event_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(input_event_t));
  if (s_event_queue == NULL) {
    ESP_LOGE(TAG, "failed to create event queue");
    return ESP_ERR_NO_MEM;
  }

  const esp_timer_create_args_t targs = {
      .callback = sample_timer_cb,
      .name = "input_sample",
  };
  err = esp_timer_create(&targs, &s_sample_timer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_timer_create: %s", esp_err_to_name(err));
    vQueueDelete(s_event_queue);
    s_event_queue = NULL;
    return err;
  }
  err = esp_timer_start_periodic(s_sample_timer, (uint64_t)SAMPLE_PERIOD_MS * 1000);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_timer_start_periodic: %s", esp_err_to_name(err));
    return err;
  }

  input_configure_wake_source();

  s_last_activity_ms = t_ms;
  s_started = true;
  ESP_LOGI(TAG, "Input manager started (6 buttons, %d ms sampling)", SAMPLE_PERIOD_MS);
  return ESP_OK;
}

bool input_get_event(input_event_t *out_event, uint32_t timeout_ms) {
  if (out_event == NULL || s_event_queue == NULL) {
    return false;
  }
  return xQueueReceive(s_event_queue, out_event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool input_is_down(input_button_t button) {
  return (button < INPUT_BTN_COUNT) && s_held[button];
}

bool input_consume_press(input_button_t button) {
  if (button >= INPUT_BTN_COUNT) {
    return false;
  }
  return __atomic_exchange_n(&s_press_latch[button], false, __ATOMIC_RELAXED);
}

uint32_t input_last_activity_ms(void) {
  return s_last_activity_ms;
}

void input_sim_press(input_button_t button, uint32_t ms) {
  if (button >= INPUT_BTN_COUNT) {
    return;
  }
  s_sim_until_us[button] = esp_timer_get_time() + (int64_t)ms * 1000;
}
