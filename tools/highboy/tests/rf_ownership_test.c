#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NO_MEM 5
#define ESP_ERR_NOT_FOUND 6
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define RFID_READER_STACK_BYTES 6144
#define RFID_READER_POLL_MS 500
#define LF_RFID_NOMINAL_HZ 125000
#define LF_RFID_DEFAULT_DUTY_PERMILLE 500
#define ST25R3916_REG_OP_CTRL 2
#define ST25R3916_OP_CTRL_TX_EN 8
#define ST25R3916_OP_CTRL_FIELD_ON 0xc8
#define ST25R3916_OP_CTRL_FIELD_OFF 0x80
#define ST25R3916_OP_CTRL_SHUTDOWN 0
#define ST25R3916_REG_IO_CONF2 1
#define ST25R3916_IO_CONF2_MISO_PD1 8
#define ST25R3916_IO_CONF2_MISO_PD2 16
#define ST25R3916_REG_AUX_DISPLAY 3
#define ST25R3916_AUX_DISP_OSC_OK 16
#define ST25R3916_OSC_POLL_MAX 3
#define ST25R3916_OSC_POLL_INTERVAL_US 100
#define ST25R3916_FIELD_ON_DELAY_MS 10
#define ST25R3916_CMD_RESET_RX_GAIN 1
#define HB_NFC_SPI_ADDR_MASK 0x3f
#define HB_NFC_SPI_REG_XFER_BITS 16

typedef int esp_err_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) assert(pthread_mutex_lock(lock) == 0)
#define portEXIT_CRITICAL(lock) assert(pthread_mutex_unlock(lock) == 0)

static _Thread_local unsigned s_task_identity;
static void *xTaskGetCurrentTaskHandle(void) { return &s_task_identity; }

/* FRONTEND */

typedef struct {size_t length; const void *tx_buffer;} spi_transaction_t;
static bool s_physical_field;
static bool s_fail_off, s_fail_on, s_oscillator_ready = true, s_fail_gain;
static int s_spi_deinit_calls, s_gpio_deinit_calls;
static esp_err_t transmit(spi_transaction_t *transaction) {
  const uint8_t *data = transaction->tx_buffer;
  assert(transaction->length == HB_NFC_SPI_REG_XFER_BITS);
  if (data[0] == ST25R3916_REG_OP_CTRL) {
    if ((data[1] & ST25R3916_OP_CTRL_TX_EN) != 0) {
      s_physical_field = true;
      if (s_fail_on) return ESP_ERR_TIMEOUT;
    } else {
      if (s_fail_off) return ESP_ERR_TIMEOUT;
      s_physical_field = false;
    }
  }
  return ESP_OK;
}
/* HAL */
static struct {bool is_init; bool is_field_on; bool has_field_fault;} s_drv;
static esp_err_t hb_nfc_spi_reg_read(uint8_t reg, uint8_t *out) {
  assert(reg == ST25R3916_REG_AUX_DISPLAY);
  *out = s_oscillator_ready ? ST25R3916_AUX_DISP_OSC_OK : 0;
  return ESP_OK;
}
static esp_err_t hb_nfc_spi_direct_cmd(uint8_t command) {
  assert(command == ST25R3916_CMD_RESET_RX_GAIN);
  return s_fail_gain ? ESP_FAIL : ESP_OK;
}
static esp_err_t hb_nfc_spi_reg_modify(uint8_t reg, uint8_t mask, uint8_t value) {
  (void)reg; (void)mask; (void)value; return ESP_OK;
}
static void hb_nfc_spi_deinit(void) {s_spi_deinit_calls++;}
static void hb_nfc_gpio_deinit(void) {s_gpio_deinit_calls++;}
static void hb_nfc_timer_delay_ms(uint32_t delay) {(void)delay;}
static void hb_nfc_timer_delay_us(uint32_t delay) {(void)delay;}
/* CORE */

typedef struct {uint8_t bytes[5];} lf_rfid_em4100_t;
static void (*s_pending_task)(void *);
static void *s_pending_arg;
static bool s_fail_task, s_cancel_during_read, s_restart_during_read;
static bool s_reopened_while_busy;
static int s_lf_starts, s_lf_stops, s_lf_reads;
static bool s_lf_field;
static void ui_rfid_menu_stop(void);
static void reader_start(void);
static bool reader_can_start(void);
static esp_err_t lf_rfid_init(void) {return ESP_OK;}
static esp_err_t lf_rfid_field_start(uint32_t hz, uint16_t duty) {
  assert(hz == LF_RFID_NOMINAL_HZ && duty == LF_RFID_DEFAULT_DUTY_PERMILLE);
  assert(!s_lf_field);
  s_lf_field = true; s_lf_starts++; return ESP_OK;
}
static esp_err_t lf_rfid_read_em4100(uint32_t timeout, lf_rfid_em4100_t *out) {
  assert(timeout == RFID_READER_POLL_MS && s_lf_field);
  s_lf_reads++;
  memset(out->bytes, 0x42, sizeof(out->bytes));
  if (s_cancel_during_read || s_restart_during_read) {
    ui_rfid_menu_stop();
    assert(s_lf_field); // Only the reader may stop its hardware.
    if (s_restart_during_read) {
      assert(!reader_can_start());
      reader_start();
      s_reopened_while_busy = true;
    }
  }
  return ESP_OK;
}
static void lf_rfid_field_stop(void) {
  assert(s_lf_field); s_lf_stops++; s_lf_field = false;
}
static int xTaskCreatePinnedToCore(void (*task)(void *), const char *name, uint32_t stack,
                                  void *arg, uint32_t priority, void *handle, uint32_t core) {
  assert(strcmp(name, "rfid_em4100") == 0 && stack == RFID_READER_STACK_BYTES);
  assert(priority == SYS_PRIO_SERVICE_LO && core == SYS_CORE_RADIO && handle == NULL);
  if (s_fail_task) return 0;
  assert(s_pending_task == NULL);
  s_pending_task = task; s_pending_arg = arg; return pdPASS;
}
static void vTaskDelete(void *task) {assert(task == NULL);}
/* RFID */

static pthread_mutex_t s_barrier_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_barrier_cond = PTHREAD_COND_INITIALIZER;
static unsigned s_barrier_arrivals, s_barrier_generation;
static void barrier(void) {
  assert(pthread_mutex_lock(&s_barrier_lock) == 0);
  unsigned generation = s_barrier_generation;
  if (++s_barrier_arrivals == 2) {
    s_barrier_arrivals = 0; s_barrier_generation++;
    assert(pthread_cond_broadcast(&s_barrier_cond) == 0);
  } else {
    while (generation == s_barrier_generation)
      assert(pthread_cond_wait(&s_barrier_cond, &s_barrier_lock) == 0);
  }
  assert(pthread_mutex_unlock(&s_barrier_lock) == 0);
}
typedef struct {rf_frontend_mode_t mode; bool claimed;} claim_t;
static void *claim_thread(void *arg) {
  claim_t *claim = arg;
  barrier();
  claim->claimed = rf_frontend_claim(claim->mode) == ESP_OK;
  barrier();
  if (claim->claimed) rf_frontend_release(claim->mode);
  return NULL;
}
static void test_concurrent_claims(void) {
  for (unsigned round = 0; round < 200; round++) {
    claim_t claims[] = {{RF_FRONTEND_LF_125KHZ, false}, {RF_FRONTEND_NFC_13MHZ, false}};
    pthread_t threads[2];
    for (unsigned i = 0; i < 2; i++)
      assert(pthread_create(&threads[i], NULL, claim_thread, &claims[i]) == 0);
    for (unsigned i = 0; i < 2; i++) assert(pthread_join(threads[i], NULL) == 0);
    assert(claims[0].claimed != claims[1].claimed);
    assert(rf_frontend_get_mode() == RF_FRONTEND_IDLE);
  }
  assert(rf_frontend_claim(RF_FRONTEND_COUNT) == ESP_ERR_INVALID_ARG);
  assert(rf_frontend_claim(RF_FRONTEND_NFC_13MHZ) == ESP_OK);
  rf_frontend_release(RF_FRONTEND_LF_125KHZ);
  assert(rf_frontend_get_mode() == RF_FRONTEND_NFC_13MHZ);
  rf_frontend_release(RF_FRONTEND_NFC_13MHZ);
}
static void *compete_with_reservation(void *arg) {
  (void)arg;
  assert(!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));
  assert(!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ));
  assert(rf_frontend_claim(RF_FRONTEND_NFC_13MHZ)==ESP_ERR_INVALID_STATE);
  assert(rf_frontend_claim(RF_FRONTEND_LF_125KHZ)==ESP_ERR_INVALID_STATE);
  assert(rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ)==ESP_ERR_INVALID_STATE);
  assert(rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ)==ESP_ERR_INVALID_STATE);
  assert(st25r3916_core_field_off()==ESP_ERR_INVALID_STATE);
  assert(st25r3916_core_deinit()==ESP_ERR_INVALID_STATE);
  rf_frontend_release(RF_FRONTEND_NFC_13MHZ);
  return NULL;
}
static void test_reserved_owner(void) {
  assert(rf_frontend_reserve(RF_FRONTEND_IDLE)==ESP_ERR_INVALID_ARG);
  assert(rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ)==ESP_OK);
  assert(rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));
  assert(!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ));
  s_drv.is_init=true;
  assert(st25r3916_core_field_on()==ESP_OK);
  assert(rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ)==ESP_ERR_INVALID_STATE);
  pthread_t thread;
  assert(pthread_create(&thread,NULL,compete_with_reservation,NULL)==0);
  assert(pthread_join(thread,NULL)==0);
  assert(s_physical_field && rf_frontend_get_mode()==RF_FRONTEND_NFC_13MHZ);
  s_fail_off=true;
  assert(st25r3916_core_field_off()==ESP_ERR_TIMEOUT);
  assert(rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ)==ESP_ERR_INVALID_STATE);
  s_fail_off=false;
  assert(st25r3916_core_field_off()==ESP_OK);
  assert(rf_frontend_get_mode()==RF_FRONTEND_IDLE);
  assert(rf_frontend_claim(RF_FRONTEND_LF_125KHZ)==ESP_ERR_INVALID_STATE);
  assert(rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ)==ESP_OK);
}

static void assert_retained(void) {
  assert(s_physical_field && s_drv.is_field_on && s_drv.has_field_fault);
  assert(rf_frontend_claim(RF_FRONTEND_LF_125KHZ) == ESP_ERR_INVALID_STATE);
  assert(st25r3916_core_field_on() == ESP_ERR_INVALID_STATE);
}
static void test_field_failures(void) {
  s_drv.is_init = true;
  assert(st25r3916_core_field_on() == ESP_OK);
  s_fail_off = true;
  assert(st25r3916_core_field_off() == ESP_ERR_TIMEOUT);
  assert_retained();
  assert(st25r3916_core_deinit() == ESP_ERR_TIMEOUT);
  assert(s_drv.is_init && s_spi_deinit_calls == 0 && s_gpio_deinit_calls == 0);
  s_fail_off = false;
  assert(st25r3916_core_field_off() == ESP_OK);
  assert(!s_physical_field && !s_drv.is_field_on && !s_drv.has_field_fault);
  assert(rf_frontend_get_mode() == RF_FRONTEND_IDLE);
  s_fail_on = s_fail_off = true;
  assert(st25r3916_core_field_on() == ESP_ERR_TIMEOUT);
  assert_retained();
  s_fail_on = s_fail_off = false;
  assert(st25r3916_core_field_off() == ESP_OK);
  s_oscillator_ready = false; s_fail_off = true;
  assert(st25r3916_core_field_on() == ESP_ERR_TIMEOUT);
  assert_retained();
  s_oscillator_ready = true; s_fail_off = false;
  assert(st25r3916_core_field_off() == ESP_OK);
  s_fail_gain = s_fail_off = true;
  assert(st25r3916_core_field_on() == ESP_FAIL);
  assert_retained();
  s_fail_gain = s_fail_off = false;
  assert(st25r3916_core_deinit() == ESP_OK);
  assert(!s_drv.is_init && s_spi_deinit_calls == 1 && s_gpio_deinit_calls == 1);
  assert(rf_frontend_get_mode() == RF_FRONTEND_IDLE);
}
static void run_pending(void) {
  assert(s_pending_task != NULL);
  void (*task)(void *) = s_pending_task;
  void *arg = s_pending_arg;
  s_pending_task = NULL;
  task(arg);
}
static void test_reader_lifecycle(void) {
  esp_err_t result;
  lf_rfid_em4100_t tag;
  reader_start();
  assert(!reader_can_start());
  run_pending();
  assert(reader_take_result(&result, &tag) && result == ESP_OK && tag.bytes[0] == 0x42);
  assert(!reader_take_result(&result, &tag) && reader_can_start());
  assert(s_lf_starts == 1 && s_lf_stops == 1);
  reader_start();
  ui_rfid_menu_stop();
  run_pending();
  assert(!reader_take_result(&result, &tag));
  assert(s_lf_starts == 1 && s_lf_stops == 1);
  s_cancel_during_read = true;
  reader_start(); run_pending();
  assert(!reader_take_result(&result, &tag) && reader_can_start());
  assert(s_lf_starts == 2 && s_lf_stops == 2);
  s_cancel_during_read = false; s_restart_during_read = true;
  reader_start(); run_pending();
  assert(s_reopened_while_busy);
  assert(reader_take_result(&result, &tag) && result == ESP_ERR_INVALID_STATE);
  assert(s_lf_starts == 3 && s_lf_stops == 3);
  s_restart_during_read = false;
  reader_start(); run_pending();
  assert(reader_take_result(&result, &tag) && result == ESP_OK);
  s_fail_task = true;
  reader_start();
  assert(reader_take_result(&result, &tag) && result == ESP_ERR_NO_MEM);
  assert(reader_can_start());
}
int main(void) {
  test_concurrent_claims();
  test_reserved_owner();
  test_field_failures();
  test_reader_lifecycle();
  assert(s_lf_starts == s_lf_stops && !s_lf_field && s_lf_reads == 4);
  puts("PASS: RF concurrent claims, failed OFF/rollback/deinit, RFID results/cancel/reopen/allocation failure");
  return 0;
}
