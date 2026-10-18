/* Six physical buttons -> active-low DMG joypad. Pure, host-testable mapping. */
#ifndef HIGHBOY_GB_INPUT_MAPPING_H
#define HIGHBOY_GB_INPUT_MAPPING_H
#include <stdbool.h>
#include <stdint.h>

enum {
  GB_PHYS_UP = 1u << 0,
  GB_PHYS_DOWN = 1u << 1,
  GB_PHYS_LEFT = 1u << 2,
  GB_PHYS_RIGHT = 1u << 3,
  GB_PHYS_OK = 1u << 4,
  GB_PHYS_BACK = 1u << 5,
  GB_PHYS_DPAD = 0x0f,
  GB_PHYS_AB = GB_PHYS_OK | GB_PHYS_BACK,
  GB_INPUT_EXIT_HOLD_MS = 2000
};

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool wait_release;
  bool exit_tracking;
  uint8_t shortcut;
  uint32_t exit_started_ms;
} highboy_gb_input_state_t;

static inline void highboy_gb_input_reset(highboy_gb_input_state_t *s) {
  *s = (highboy_gb_input_state_t){.wait_release = true};
}

static inline uint8_t highboy_gb_input_update(highboy_gb_input_state_t *s,
                                              uint8_t physical,
                                              uint32_t now_ms,
                                              bool *exit_requested) {
  *exit_requested = false;
  if (s->wait_release) {
    if (physical != 0)
      return 0xff;
    s->wait_release = false;
  }

  uint8_t dpad = physical & GB_PHYS_DPAD;
  bool both = (physical & GB_PHYS_AB) == GB_PHYS_AB;
  uint8_t shortcut = 0;
  if (both && dpad == GB_PHYS_RIGHT)
    shortcut = 0x08; /* START */
  else if (both && dpad == GB_PHYS_LEFT)
    shortcut = 0x04; /* SELECT */

  if (s->shortcut && shortcut != s->shortcut) {
    /* A partial chord release must not become A/B or a directional press. */
    s->shortcut = 0;
    s->exit_tracking = false;
    s->wait_release = physical != 0;
    return 0xff;
  }
  if (shortcut) {
    s->shortcut = shortcut;
    s->exit_tracking = false;
    return (uint8_t)~shortcut;
  }

  if (both && dpad == 0) {
    if (!s->exit_tracking) {
      s->exit_tracking = true;
      s->exit_started_ms = now_ms;
    } else if ((uint32_t)(now_ms - s->exit_started_ms) >= GB_INPUT_EXIT_HOLD_MS) {
      *exit_requested = true;
      return 0xff;
    }
  } else {
    s->exit_tracking = false;
  }

  uint8_t pressed = 0;
  if (physical & GB_PHYS_OK)
    pressed |= 0x01; /* A */
  if (physical & GB_PHYS_BACK)
    pressed |= 0x02; /* B */
  /* Landscape: physical UP/DOWN are game LEFT/RIGHT. Opposites cancel. */
  if ((dpad & (GB_PHYS_UP | GB_PHYS_DOWN)) == GB_PHYS_UP)
    pressed |= 0x20;
  if ((dpad & (GB_PHYS_UP | GB_PHYS_DOWN)) == GB_PHYS_DOWN)
    pressed |= 0x10;
  if ((dpad & (GB_PHYS_LEFT | GB_PHYS_RIGHT)) == GB_PHYS_RIGHT)
    pressed |= 0x40;
  if ((dpad & (GB_PHYS_LEFT | GB_PHYS_RIGHT)) == GB_PHYS_LEFT)
    pressed |= 0x80;
  return (uint8_t)~pressed;
}
#ifdef __cplusplus
}
#endif

#endif
