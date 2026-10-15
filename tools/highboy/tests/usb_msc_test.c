#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "usb_msc.h"

typedef int esp_err_t;
typedef struct { int marker; } sdmmc_card_t;
typedef void *tinyusb_msc_storage_handle_t;
typedef enum { TINYUSB_MSC_STORAGE_MOUNT_USB, TINYUSB_MSC_STORAGE_MOUNT_APP }
    tinyusb_msc_mount_point_t;
typedef struct {
  struct { unsigned auto_mount_off; } user_flags;
} tinyusb_msc_driver_config_t;
typedef struct {
  struct { sdmmc_card_t *card; } medium;
  struct { const char *base_path; bool do_not_format; } fat_fs;
  tinyusb_msc_mount_point_t mount_point;
} tinyusb_msc_storage_config_t;

#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))

static sdmmc_card_t card;
static int storage_token;
static bool card_live, storage_live, exposed, native, mounted;
static int detach_calls, install_calls, create_calls, delete_calls, restore_calls, restart_calls;
static int delay_calls, busy_deletes;
static esp_err_t detach_error, install_error, create_error, delete_error, restore_error, mux_error;
static bool exercise_reentrant_calls;

const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock"; }
void esp_restart(void) { restart_calls++; }
void vTaskDelay(uint32_t ticks) { assert(ticks > 0); delay_calls++; }
bool tud_mounted(void) { return mounted; }

esp_err_t vfs_sdcard_detach_for_msc(void **out_card) {
  detach_calls++;
  if (exercise_reentrant_calls) {
    usb_msc_enter();
    usb_msc_exit();
    assert(usb_msc_get_state() == USB_MSC_ENTERING);
  }
  if (detach_error != ESP_OK)
    return detach_error;
  assert(!card_live);
  card_live = true;
  *out_card = &card;
  return ESP_OK;
}

esp_err_t vfs_sdcard_reattach_after_msc(void *raw) {
  assert(raw == &card && card_live);
  assert(!storage_live); // Never release a card still owned by MSC.
  card_live = false;
  restore_calls++;
  return restore_error;
}

esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *cfg) {
  assert(cfg->user_flags.auto_mount_off == 1);
  install_calls++;
  return install_error;
}

esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *cfg,
                                       tinyusb_msc_storage_handle_t *handle) {
  create_calls++;
  assert(card_live && cfg->medium.card == &card);
  assert(cfg->fat_fs.do_not_format);
  assert(cfg->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB);
  if (create_error != ESP_OK)
    return create_error;
  storage_live = true;
  *handle = &storage_token;
  return ESP_OK;
}

esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle) {
  assert(handle == &storage_token && card_live && storage_live);
  assert(!exposed);
  delete_calls++;
  if (exercise_reentrant_calls) {
    usb_msc_enter();
    usb_msc_exit();
    assert(usb_msc_get_state() == USB_MSC_EXITING);
  }
  if (busy_deletes > 0) {
    busy_deletes--;
    return ESP_ERR_INVALID_STATE;
  }
  if (delete_error != ESP_OK)
    return delete_error;
  storage_live = false;
  return ESP_OK;
}

void busb_set_msc_exposed(bool value) {
  if (value)
    assert(storage_live && card_live);
  exposed = value;
}

esp_err_t usb_mux_set_native(bool value) {
  if (value && mux_error != ESP_OK)
    return mux_error;
  native = value;
  return ESP_OK;
}

#include "../../../firmware_p4/components/Service/storage_vfs/usb_msc.c"

static void reset(void) {
  atomic_store(&s_state, USB_MSC_IDLE);
  s_card = NULL;
  s_handle = NULL;
  card_live = storage_live = exposed = native = mounted = false;
  detach_calls = install_calls = create_calls = delete_calls = restore_calls = restart_calls = 0;
  delay_calls = busy_deletes = 0;
  detach_error = install_error = create_error = delete_error = restore_error = mux_error = ESP_OK;
  exercise_reentrant_calls = false;
}

static void test_normal_and_repeated_cycle(void) {
  reset();
  usb_msc_enter();
  assert(usb_msc_get_state() == USB_MSC_ACTIVE && exposed && native);
  assert(!usb_msc_host_connected());
  mounted = true;
  assert(usb_msc_host_connected());
  usb_msc_enter();
  assert(detach_calls == 1);
  usb_msc_exit();
  assert(usb_msc_get_state() == USB_MSC_IDLE && !card_live && !storage_live);
  assert(!exposed && !native && !usb_msc_host_connected());
  usb_msc_exit();
  assert(restore_calls == 1 && delete_calls == 1);
  install_error = ESP_ERR_INVALID_STATE; // Driver stays installed between cycles.
  usb_msc_enter();
  usb_msc_exit();
  assert(restore_calls == 2 && restart_calls == 0);
}

static void test_deferred_writes_drain(void) {
  reset();
  usb_msc_enter();
  busy_deletes = 3;
  usb_msc_exit();
  assert(delete_calls == 4 && delay_calls == 3 && restore_calls == 1);
  assert(usb_msc_get_state() == USB_MSC_IDLE);
}

static void test_busy_timeout_preserves_ownership(void) {
  reset();
  usb_msc_enter();
  delete_error = ESP_ERR_INVALID_STATE;
  usb_msc_exit();
  assert(delete_calls == USB_MSC_DELETE_ATTEMPTS);
  assert(card_live && storage_live && exposed && native);
  assert(s_handle == &storage_token && s_card == &card);
  assert(restore_calls == 0 && restart_calls == 0);
  assert(usb_msc_get_state() == USB_MSC_ACTIVE);
  delete_error = ESP_OK;
  usb_msc_exit();
  assert(restore_calls == 1 && usb_msc_get_state() == USB_MSC_IDLE);
}

static void test_permanent_delete_error_preserves_ownership(void) {
  reset();
  usb_msc_enter();
  delete_error = ESP_FAIL;
  usb_msc_exit();
  assert(delete_calls == 1 && delay_calls == 0 && restore_calls == 0);
  assert(card_live && storage_live && exposed && usb_msc_get_state() == USB_MSC_ACTIVE);
}

static void test_entry_failures(void) {
  reset();
  detach_error = ESP_FAIL;
  usb_msc_enter();
  assert(restart_calls == 1 && install_calls == 0);
  reset();
  install_error = ESP_ERR_NO_MEM;
  usb_msc_enter();
  assert(restore_calls == 1 && create_calls == 0 && !card_live);
  assert(usb_msc_get_state() == USB_MSC_ERROR);
  usb_msc_exit();
  assert(usb_msc_get_state() == USB_MSC_IDLE);
  reset();
  create_error = ESP_ERR_NO_MEM;
  usb_msc_enter();
  assert(restore_calls == 1 && delete_calls == 0 && !exposed);
  create_error = ESP_OK;
  usb_msc_enter(); // A new screen may retry directly from ERROR.
  assert(usb_msc_get_state() == USB_MSC_ACTIVE);
  usb_msc_exit();
}

static void test_failed_remount_recovers(void) {
  reset();
  usb_msc_enter();
  restore_error = ESP_FAIL;
  usb_msc_exit();
  assert(delete_calls == 1 && restore_calls == 1 && restart_calls == 1);
  assert(usb_msc_get_state() != USB_MSC_IDLE);
  reset();
  create_error = ESP_FAIL;
  restore_error = ESP_FAIL;
  usb_msc_enter();
  assert(restart_calls == 1 && restore_calls == 1);
}

static void test_reentrant_transition_guards(void) {
  reset();
  exercise_reentrant_calls = true;
  usb_msc_enter();
  usb_msc_exit();
  assert(detach_calls == 1 && delete_calls == 1 && restore_calls == 1);
}

static void test_mux_failure_rolls_back(void) {
  reset();
  mux_error = ESP_FAIL;
  usb_msc_enter();
  assert(delete_calls == 1 && restore_calls == 1 && !exposed && !card_live);
  assert(usb_msc_get_state() == USB_MSC_ERROR);
  reset();
  mux_error = ESP_FAIL;
  delete_error = ESP_FAIL;
  usb_msc_enter();
  assert(restore_calls == 0 && card_live && storage_live && exposed);
  assert(usb_msc_get_state() == USB_MSC_ACTIVE);
  delete_error = ESP_OK;
  usb_msc_exit();
  assert(restore_calls == 1);
}

int main(void) {
  test_normal_and_repeated_cycle();
  test_deferred_writes_drain();
  test_busy_timeout_preserves_ownership();
  test_permanent_delete_error_preserves_ownership();
  test_entry_failures();
  test_failed_remount_recovers();
  test_reentrant_transition_guards();
  test_mux_failure_rolls_back();
  puts("PASS: 8 MSC lifecycle groups (real production source, mocked hardware)");
  return 0;
}
