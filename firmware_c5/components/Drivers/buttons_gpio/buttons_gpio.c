#include "driver/gpio.h"
#include "buttons_gpio.h"
#include "pin_def.h"

#define BUTTON_PRESSED_LEVEL    0

static button_t buttons[] = {
    { BTN_UP,    true, false },
    { BTN_DOWN,  true, false },
    { BTN_LEFT,  true, false },
    { BTN_RIGHT, true, false },
    { BTN_OK,    true, false },
    { BTN_BACK,  true, false },
};

#define NUM_BUTTONS (sizeof(buttons)/sizeof(buttons[0]))



static bool get_raw_level(uint32_t gpio)
{
    return gpio_get_level(gpio) == BUTTON_PRESSED_LEVEL;
}

bool up_button_pressed(void)    { return buttons[0].pressed_flag && (__atomic_test_and_set(&buttons[0].pressed_flag, __ATOMIC_RELAXED), false); }
bool down_button_pressed(void)  { return buttons[1].pressed_flag && (__atomic_test_and_set(&buttons[1].pressed_flag, __ATOMIC_RELAXED), false); }
bool left_button_pressed(void)  { return buttons[2].pressed_flag && (__atomic_test_and_set(&buttons[2].pressed_flag, __ATOMIC_RELAXED), false); }
bool right_button_pressed(void) { return buttons[3].pressed_flag && (__atomic_test_and_set(&buttons[3].pressed_flag, __ATOMIC_RELAXED), false); }
bool ok_button_pressed(void)    { return buttons[4].pressed_flag && (__atomic_test_and_set(&buttons[4].pressed_flag, __ATOMIC_RELAXED), false); }
bool back_button_pressed(void)  { return buttons[5].pressed_flag && (__atomic_test_and_set(&buttons[5].pressed_flag, __ATOMIC_RELAXED), false); }

bool up_button_is_down(void)    { return get_raw_level(BTN_UP);    }
bool down_button_is_down(void)  { return get_raw_level(BTN_DOWN);  }
bool left_button_is_down(void)  { return get_raw_level(BTN_LEFT);  }
bool right_button_is_down(void) { return get_raw_level(BTN_RIGHT); }
bool ok_button_is_down(void)    { return get_raw_level(BTN_OK);    }
bool back_button_is_down(void)  { return get_raw_level(BTN_BACK);  }

void buttons_task(void)
{
    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool current = get_raw_level(buttons[i].gpio);

        if (buttons[i].last_state == true && current == false) {
            buttons[i].pressed_flag = true;  
        }

        buttons[i].last_state = current;
    }
}
void buttons_init(void)
{
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = ((1ULL << BTN_UP)    | (1ULL << BTN_DOWN)  |
                         (1ULL << BTN_LEFT)  | (1ULL << BTN_RIGHT) |
                         (1ULL << BTN_OK)    | (1ULL << BTN_BACK)),
        .pull_down_en = 0,
        .pull_up_en   = 1,
    };
    gpio_config(&io_conf);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        buttons[i].last_state = get_raw_level(buttons[i].gpio);
        buttons[i].pressed_flag = false;
    }
}
