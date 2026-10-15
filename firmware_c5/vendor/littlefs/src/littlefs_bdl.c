/**
 * @file littlefs_bdl.c
 * @brief Maps the ESP-IDF Block Device Layer (BDL) <-> littlefs HAL
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_blockdev.h"
#include "littlefs/lfs.h"
#include "littlefs_api.h"

#ifdef CONFIG_LITTLEFS_WDT_RESET
#include "esp_task_wdt.h"
#endif

/* Convert esp_err_t to the nearest littlefs error code */
static int esp_err_to_lfs(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return LFS_ERR_OK;
    case ESP_ERR_NO_MEM:
        return LFS_ERR_NOMEM;
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_SIZE:
    case ESP_ERR_INVALID_STATE:
        return LFS_ERR_INVAL;
    case ESP_ERR_NOT_FOUND:
    case ESP_ERR_NOT_SUPPORTED:
        return LFS_ERR_IO;
    default:
        return LFS_ERR_IO;
    }
}

static bool validate_handle(const esp_blockdev_handle_t dev, const char *op)
{
    if (!dev) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL %s called with null device handle", op);
        return false;
    }

    if (!dev->ops) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL %s called with missing ops table", op);
        return false;
    }

    return true;
}

int littlefs_bdl_read(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, void *buffer, lfs_size_t size)
{
    esp_littlefs_t *efs = (esp_littlefs_t *)c->context;
    esp_blockdev_handle_t dev = efs ? efs->bdl_handle : NULL;
    if (!validate_handle(dev, "read") || !dev->ops->read) {
        return LFS_ERR_IO;
    }

    /* Enforce LittleFS contract: read confined to a block */
    if (off + size > c->block_size) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL read exceeds block boundary: off=0x%08x size=0x%08x block_size=0x%08x",
                 (unsigned)off, (unsigned)size, (unsigned)c->block_size);
        return LFS_ERR_INVAL;
    }

    const uint64_t addr = ((uint64_t)block * c->block_size) + off;

#ifdef CONFIG_LITTLEFS_WDT_RESET
    esp_task_wdt_reset();
#endif

    if (dev->geometry.disk_size && addr + size > dev->geometry.disk_size) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL read out of range: addr=0x%016" PRIx64 ", size=0x%08x (disk_size=0x%016" PRIx64 ")",
                 addr, (unsigned)size, dev->geometry.disk_size);
        return LFS_ERR_IO;
    }

    /* dst_buf_size == data_read_len because LittleFS always provides an exact-sized buffer */
    esp_err_t err = dev->ops->read(dev, (uint8_t *)buffer, size, addr, size);
    if (err != ESP_OK) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL read failed: addr=0x%016" PRIx64 ", size=0x%08x, err=0x%x",
                 addr, (unsigned)size, err);
    }
    return esp_err_to_lfs(err);
}

int littlefs_bdl_write(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, const void *buffer, lfs_size_t size)
{
    esp_littlefs_t *efs = (esp_littlefs_t *)c->context;
    esp_blockdev_handle_t dev = efs ? efs->bdl_handle : NULL;
    if (!validate_handle(dev, "write") || !dev->ops->write) {
        return LFS_ERR_IO;
    }

    if ((efs && efs->read_only) || dev->device_flags.read_only) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL write rejected: device is read-only");
        return LFS_ERR_IO;
    }

    if (off + size > c->block_size) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL write exceeds block boundary: off=0x%08x size=0x%08x block_size=0x%08x",
                 (unsigned)off, (unsigned)size, (unsigned)c->block_size);
        return LFS_ERR_INVAL;
    }

    const uint64_t addr = ((uint64_t)block * c->block_size) + off;

#ifdef CONFIG_LITTLEFS_WDT_RESET
    esp_task_wdt_reset();
#endif

    if (dev->geometry.disk_size && addr + size > dev->geometry.disk_size) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL write out of range: addr=0x%016" PRIx64 ", size=0x%08x (disk_size=0x%016" PRIx64 ")",
                 addr, (unsigned)size, dev->geometry.disk_size);
        return LFS_ERR_IO;
    }

    esp_err_t err = dev->ops->write(dev, (const uint8_t *)buffer, addr, size);
    if (err != ESP_OK) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL write failed: addr=0x%016" PRIx64 ", size=0x%08x, err=0x%x",
                 addr, (unsigned)size, err);
    }
    return esp_err_to_lfs(err);
}

int littlefs_bdl_erase(const struct lfs_config *c, lfs_block_t block)
{
    esp_littlefs_t *efs = (esp_littlefs_t *)c->context;
    esp_blockdev_handle_t dev = efs ? efs->bdl_handle : NULL;
    if (!validate_handle(dev, "erase")) {
        return LFS_ERR_IO;
    }

    if ((efs && efs->read_only) || dev->device_flags.read_only) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL erase not supported (read-only)");
        return LFS_ERR_IO;
    }

    const uint64_t addr = (uint64_t)block * c->block_size;
    const size_t erase_len = c->block_size;
    const bool logical = efs && efs->bdl_logical_block_mode;

    /*
     * Logical BDL mode (erase_before_write=0): LittleFS block_size may be smaller than geometry.erase_size.
     * Skip alignment to geometry.erase_size; erase operation is still required for LittleFS compatibility.
     */
    if (!dev->ops->erase) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL erase not supported (missing erase op)");
        return LFS_ERR_IO;
    }

    if (!logical && dev->geometry.erase_size == 0) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL erase not supported (erase_size=0)");
        return LFS_ERR_IO;
    }

#ifdef CONFIG_LITTLEFS_WDT_RESET
    esp_task_wdt_reset();
#endif

    /* Classic (erase_before_write=1): logical blocks align to geometry.erase_size. */
    if (!logical && dev->geometry.erase_size &&
        ((addr % dev->geometry.erase_size) || (erase_len % dev->geometry.erase_size))) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL erase misaligned: addr=0x%016" PRIx64 ", len=0x%08x, erase_size=0x%08x",
                 addr, (unsigned)erase_len, (unsigned)dev->geometry.erase_size);
        return LFS_ERR_INVAL;
    }

    if (dev->geometry.disk_size && addr + erase_len > dev->geometry.disk_size) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL erase out of range: addr=0x%016" PRIx64 ", len=0x%08x (disk_size=0x%016" PRIx64 ")",
                 addr, (unsigned)erase_len, dev->geometry.disk_size);
        return LFS_ERR_IO;
    }

    esp_err_t err = dev->ops->erase(dev, addr, erase_len);
    if (err != ESP_OK) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL erase failed: addr=0x%016" PRIx64 ", len=0x%08x, err=0x%x",
                 addr, (unsigned)erase_len, err);
    }
    return esp_err_to_lfs(err);
}

int littlefs_bdl_sync(const struct lfs_config *c)
{
    esp_littlefs_t *efs = (esp_littlefs_t *)c->context;
    esp_blockdev_handle_t dev = efs ? efs->bdl_handle : NULL;
    if (!validate_handle(dev, "sync")) {
        return LFS_ERR_IO;
    }

    if (!dev->ops->sync) {
        return LFS_ERR_OK; /* Nothing to do */
    }

#ifdef CONFIG_LITTLEFS_WDT_RESET
    esp_task_wdt_reset();
#endif

    esp_err_t err = dev->ops->sync(dev);
    if (err != ESP_OK) {
        ESP_LOGE(ESP_LITTLEFS_TAG, "BDL sync failed: err=0x%x", err);
    }
    return esp_err_to_lfs(err);
}
