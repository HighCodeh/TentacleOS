/* Explicit deterministic RTOS lock boundary used only in host tests. */
#ifndef HB_MUSE_SETUP_HOST_H
#define HB_MUSE_SETUP_HOST_H
typedef struct { unsigned held; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
void hb_setup_test_enter(portMUX_TYPE *lock);
void hb_setup_test_exit(portMUX_TYPE *lock);
void hb_setup_test_delay(unsigned milliseconds);
#define portENTER_CRITICAL(lock) hb_setup_test_enter(lock)
#define portEXIT_CRITICAL(lock) hb_setup_test_exit(lock)
#endif
