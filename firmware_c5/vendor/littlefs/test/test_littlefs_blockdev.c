// Tests for LittleFS mounted via esp_blockdev handle
#include "test_littlefs_common.h"
#include "esp_partition.h"
#include "esp_blockdev.h"
#include "spi_flash_mmap.h"
#include "esp_flash.h"
#include <stdlib.h>
#include <string.h>

static esp_partition_t get_test_data_static_partition(void);
static void            test_setup_bdl(const esp_partition_t* partition, bool read_only);
static void            test_teardown_bdl(void);

static esp_blockdev_handle_t s_bdl_handle = NULL;

/* Mock BDL for geometry/flag validation tests. */
#define MOCK_BDL_TEST_READ_SIZE   16
#define MOCK_BDL_TEST_WRITE_SIZE  16
#define MOCK_BDL_TEST_ERASE_SIZE  512
#define MOCK_BDL_TEST_BLOCK_COUNT 32
#define MOCK_BDL_TEST_DISK_SIZE   (MOCK_BDL_TEST_ERASE_SIZE * MOCK_BDL_TEST_BLOCK_COUNT)

typedef struct {
    uint8_t *storage;
    size_t disk_size;
    size_t erase_size;
    bool strict_erase_alignment;
    size_t erase_calls;
    size_t last_erase_len;
} mock_bdl_ctx_t;

typedef struct {
    bool read_only;
    bool encrypted;
    bool erase_before_write;
    bool and_type_write;
    bool default_val_after_erase;
    size_t read_size;
    size_t write_size;
    size_t erase_size;
    size_t recommended_read_size;
    size_t recommended_write_size;
    size_t recommended_erase_size;
    size_t disk_size;
    bool strict_erase_alignment;
} mock_bdl_params_t;

static uint8_t s_mock_bdl_storage[MOCK_BDL_TEST_DISK_SIZE];

static esp_err_t mock_bdl_read(esp_blockdev_handle_t dev_handle, uint8_t *dst_buf,
                               size_t dst_buf_size, uint64_t src_addr, size_t data_read_len)
{
    mock_bdl_ctx_t *ctx = (mock_bdl_ctx_t *)dev_handle->ctx;
    if (!ctx || !dst_buf || data_read_len > dst_buf_size || src_addr + data_read_len > ctx->disk_size) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(dst_buf, &ctx->storage[src_addr], data_read_len);
    return ESP_OK;
}

static esp_err_t mock_bdl_write(esp_blockdev_handle_t dev_handle, const uint8_t *src_buf,
                                uint64_t dst_addr, size_t data_write_len)
{
    mock_bdl_ctx_t *ctx = (mock_bdl_ctx_t *)dev_handle->ctx;
    if (!ctx || !src_buf || dst_addr + data_write_len > ctx->disk_size) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dev_handle->geometry.write_size == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if ((dst_addr % dev_handle->geometry.write_size) || (data_write_len % dev_handle->geometry.write_size)) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Enforce flash-like behavior for this mock: bytes must be erased before programming. */
    for (size_t i = 0; i < data_write_len; i++) {
        if (ctx->storage[dst_addr + i] != 0xFF) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    memcpy(&ctx->storage[dst_addr], src_buf, data_write_len);
    return ESP_OK;
}

static esp_err_t mock_bdl_erase(esp_blockdev_handle_t dev_handle, uint64_t start_addr, size_t erase_len)
{
    mock_bdl_ctx_t *ctx = (mock_bdl_ctx_t *)dev_handle->ctx;
    if (!ctx || start_addr + erase_len > ctx->disk_size) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->strict_erase_alignment && ctx->erase_size > 0 &&
            ((start_addr % ctx->erase_size) || (erase_len % ctx->erase_size))) {
        return ESP_ERR_INVALID_SIZE;
    }

    ctx->erase_calls++;
    ctx->last_erase_len = erase_len;
    memset(&ctx->storage[start_addr], 0xFF, erase_len);
    return ESP_OK;
}

static esp_err_t mock_bdl_sync(esp_blockdev_handle_t dev_handle)
{
    (void)dev_handle;
    return ESP_OK;
}

static esp_err_t mock_bdl_release(esp_blockdev_handle_t dev_handle)
{
    if (!dev_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dev_handle->ctx) {
        free(dev_handle->ctx);
        dev_handle->ctx = NULL;
    }
    dev_handle->ops = NULL;
    return ESP_OK;
}

static const esp_blockdev_ops_t s_mock_bdl_ops = {
    .read = mock_bdl_read,
    .write = mock_bdl_write,
    .erase = mock_bdl_erase,
    .sync = mock_bdl_sync,
    .ioctl = NULL,
    .release = mock_bdl_release,
};

static mock_bdl_params_t mock_bdl_default_params(void)
{
    return (mock_bdl_params_t){
        .read_only = false,
        .encrypted = false,
        .erase_before_write = true,
        .and_type_write = true,
        .default_val_after_erase = true,
        .read_size = MOCK_BDL_TEST_READ_SIZE,
        .write_size = MOCK_BDL_TEST_WRITE_SIZE,
        .erase_size = MOCK_BDL_TEST_ERASE_SIZE,
        .recommended_read_size = 0,
        .recommended_write_size = 0,
        .recommended_erase_size = 0,
        .disk_size = MOCK_BDL_TEST_DISK_SIZE,
        .strict_erase_alignment = true,
    };
}

static esp_err_t mock_bdl_create_custom(esp_blockdev_handle_t *out, const mock_bdl_params_t *params, bool reset_media)
{
    if (!out || !params || params->disk_size == 0 || params->disk_size > sizeof(s_mock_bdl_storage)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (reset_media) {
        memset(s_mock_bdl_storage, 0xFF, params->disk_size);
    }

    esp_blockdev_handle_t dev = calloc(1, sizeof(*dev));
    mock_bdl_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!dev || !ctx) {
        free(ctx);
        free(dev);
        return ESP_ERR_NO_MEM;
    }

    ctx->storage = s_mock_bdl_storage;
    ctx->disk_size = params->disk_size;
    ctx->erase_size = params->erase_size;
    ctx->strict_erase_alignment = params->strict_erase_alignment;

    dev->ctx = ctx;
    dev->device_flags.read_only = params->read_only;
    dev->device_flags.encrypted = params->encrypted;
    dev->device_flags.erase_before_write = params->erase_before_write;
    dev->device_flags.and_type_write = params->and_type_write;
    dev->device_flags.default_val_after_erase = params->default_val_after_erase;
    dev->geometry.disk_size = params->disk_size;
    dev->geometry.read_size = params->read_size;
    dev->geometry.write_size = params->write_size;
    dev->geometry.erase_size = params->erase_size;
    dev->geometry.recommended_read_size = params->recommended_read_size;
    dev->geometry.recommended_write_size = params->recommended_write_size;
    dev->geometry.recommended_erase_size = params->recommended_erase_size;
    dev->ops = &s_mock_bdl_ops;

    *out = dev;
    return ESP_OK;
}

static void mock_bdl_destroy_handle(esp_blockdev_handle_t handle)
{
    if (!handle) {
        return;
    }
    if (handle->ops && handle->ops->release) {
        TEST_ESP_OK(handle->ops->release(handle));
    }
    free(handle);
}

static esp_err_t try_register_mock_bdl(const mock_bdl_params_t *params,
                                       bool mount_read_only,
                                       bool format_if_mount_failed,
                                       bool reset_media,
                                       esp_blockdev_handle_t *out_handle)
{
    esp_blockdev_handle_t handle = NULL;
    esp_err_t err = mock_bdl_create_custom(&handle, params, reset_media);
    if (err != ESP_OK) {
        return err;
    }

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = littlefs_base_path,
        .partition_label = NULL,
        .partition = NULL,
        .blockdev = handle,
        .dont_mount = false,
        .read_only = mount_read_only,
        .format_if_mount_failed = format_if_mount_failed,
    };

    err = esp_vfs_littlefs_register(&conf);
    if (err == ESP_OK) {
        if (out_handle) {
            *out_handle = handle;
        } else {
            TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(handle));
            mock_bdl_destroy_handle(handle);
        }
        return err;
    }

    mock_bdl_destroy_handle(handle);
    if (out_handle) {
        *out_handle = NULL;
    }
    return err;
}

static void assert_mock_bdl_register_result(const mock_bdl_params_t *params,
                                            bool mount_read_only,
                                            esp_err_t expected)
{
    TEST_ASSERT_EQUAL(expected,
        try_register_mock_bdl(params, mount_read_only, true, true, NULL));
}

static esp_partition_t get_test_data_static_partition(void)
{
    /* Construct a partition descriptor for the embedded testfs binary */
    extern const uint8_t partition_blob_start[] asm("_binary_testfs_bin_start");
    extern const uint8_t partition_blob_end[] asm("_binary_testfs_bin_end");

    return (esp_partition_t){
        .flash_chip = esp_flash_default_chip,
        .type       = ESP_PARTITION_TYPE_DATA,
        .subtype    = ESP_PARTITION_SUBTYPE_DATA_FAT,
        .address    = spi_flash_cache2phys(partition_blob_start),
        .size       = ((uintptr_t)partition_blob_end) - ((uintptr_t)partition_blob_start),
        .erase_size = SPI_FLASH_SEC_SIZE, /* match flash sector & embedded LittleFS image block size */
        .label      = "",
        .encrypted  = false,
        .readonly   = false,
    };
}

static void test_setup_bdl(const esp_partition_t* partition, bool read_only)
{
    TEST_ESP_OK(esp_partition_ptr_get_blockdev(partition, &s_bdl_handle));

    const esp_vfs_littlefs_conf_t conf = {
        .base_path  = littlefs_base_path,
        .partition_label = NULL,
        .partition  = NULL,
        .blockdev   = s_bdl_handle,
        .dont_mount = false,
        .read_only  = read_only,
    };

    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    TEST_ASSERT_TRUE(esp_littlefs_blockdev_mounted(s_bdl_handle));
    printf("BDL test setup complete.\n");
}

static void test_teardown_bdl(void)
{
    TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(s_bdl_handle));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    s_bdl_handle = NULL; // released by unregister
    printf("BDL test teardown complete.\n");
}

static const esp_partition_t* get_rw_partition(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,
        littlefs_test_partition_label);
    if (!part) {
        part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_ANY,
            littlefs_test_partition_label);
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(part, "littlefs test partition not found");
    return part;
}

static void test_setup_bdl_rw(void)
{
    const esp_partition_t *part = get_rw_partition();
    /* Format via partition API to ensure clean media before blockdev mount */
    TEST_ESP_OK(esp_littlefs_format_partition(part));

    TEST_ESP_OK(esp_partition_ptr_get_blockdev(part, &s_bdl_handle));

    const esp_vfs_littlefs_conf_t conf = {
        .base_path  = littlefs_base_path,
        .partition_label = NULL,
        .partition  = NULL,
        .blockdev   = s_bdl_handle,
        .dont_mount = false,
        .read_only  = false,
        .format_if_mount_failed = true,
    };

    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    TEST_ASSERT_TRUE(esp_littlefs_blockdev_mounted(s_bdl_handle));
    printf("BDL RW setup complete.\n");
}

static void test_teardown_bdl_rw(void)
{
    TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(s_bdl_handle));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    s_bdl_handle = NULL;
    printf("BDL RW teardown complete.\n");
}

TEST_CASE("bdl can initialize LittleFS in erased partition", "[littlefs_bdl]")
{
    const esp_partition_t *part = get_rw_partition();
    TEST_ESP_OK(esp_littlefs_format_partition(part));
    test_setup_bdl_rw();

    size_t total = 0, used = 0;
    TEST_ESP_OK(esp_littlefs_blockdev_info(s_bdl_handle, &total, &used));
    TEST_ASSERT_GREATER_THAN(0, total);

    test_teardown_bdl_rw();
}

TEST_CASE("bdl can read file", "[littlefs_bdl]")
{
    const esp_partition_t partition = get_test_data_static_partition();
    test_setup_bdl(&partition, true);

    test_littlefs_read_file_with_content(littlefs_base_path "/test1.txt", "test1");
    test_littlefs_read_file_with_content(littlefs_base_path "/test2.txt", "test2");
    test_littlefs_read_file_with_content(littlefs_base_path "/pangram.txt", "The quick brown fox jumps over the lazy dog");

    test_teardown_bdl();
}

TEST_CASE("bdl cannot create file when mounted read-only", "[littlefs_bdl]")
{
    const esp_partition_t partition = get_test_data_static_partition();
    test_setup_bdl(&partition, true);

    FILE* f = fopen(littlefs_base_path "/new_file.txt", "wb");
    TEST_ASSERT_NULL(f);

    test_teardown_bdl();
}

TEST_CASE("bdl cannot delete existing file when mounted read-only", "[littlefs_bdl]")
{
    const esp_partition_t partition = get_test_data_static_partition();
    test_setup_bdl(&partition, true);

    TEST_ASSERT_EQUAL(-1, unlink(littlefs_base_path "/test1.txt"));
    test_littlefs_read_file_with_content(littlefs_base_path "/test1.txt", "test1");

    test_teardown_bdl();
}

TEST_CASE("bdl can create and read file", "[littlefs_bdl]")
{
    test_setup_bdl_rw();

    const char *fn = littlefs_base_path "/hello.txt";
    test_littlefs_create_file_with_text(fn, littlefs_test_hello_str);
    test_littlefs_read_file(fn);

    test_teardown_bdl_rw();
}

TEST_CASE("bdl cannot write existing file when mounted read-only", "[littlefs_bdl]")
{
    const esp_partition_t partition = get_test_data_static_partition();
    test_setup_bdl(&partition, true);

    FILE* f = fopen(littlefs_base_path "/test1.txt", "wb");
    TEST_ASSERT_NULL(f);
    test_littlefs_read_file_with_content(littlefs_base_path "/test1.txt", "test1");

    test_teardown_bdl();
}

TEST_CASE("bdl can format mounted partition", "[littlefs_bdl]")
{
    const esp_partition_t *part = get_rw_partition();
    /* start clean */
    TEST_ESP_OK(esp_littlefs_format_partition(part));
    test_setup_bdl_rw();

    /* write a file then format via BDL path */
    const char *fn = littlefs_base_path "/hello.txt";
    test_littlefs_create_file_with_text(fn, littlefs_test_hello_str);

    /* unmount then format underlying partition */
    test_teardown_bdl_rw();
    TEST_ESP_OK(esp_littlefs_format_partition(part));

    /* mount again and ensure file is gone */
    test_setup_bdl_rw();
    FILE *f = fopen(fn, "r");
    TEST_ASSERT_NULL(f);

    test_teardown_bdl_rw();
}

TEST_CASE("bdl flag compatibility validation", "[littlefs_bdl_geom]")
{
    mock_bdl_params_t p = mock_bdl_default_params();
    p.encrypted = true;
    assert_mock_bdl_register_result(&p, false, ESP_ERR_NOT_SUPPORTED);

    p = mock_bdl_default_params();
    p.default_val_after_erase = false;
    assert_mock_bdl_register_result(&p, false, ESP_ERR_NOT_SUPPORTED);

    p = mock_bdl_default_params();
    p.and_type_write = false;
    assert_mock_bdl_register_result(&p, false, ESP_OK);

    p = mock_bdl_default_params();
    p.read_only = true;
    assert_mock_bdl_register_result(&p, false, ESP_ERR_INVALID_ARG);
}

TEST_CASE("bdl basic geometry rejection matrix", "[littlefs_bdl_geom]")
{
    mock_bdl_params_t p = mock_bdl_default_params();
    p.read_size = 0;
    assert_mock_bdl_register_result(&p, false, ESP_ERR_INVALID_ARG);

    p = mock_bdl_default_params();
    p.erase_before_write = true;
    p.erase_size = 0;
    assert_mock_bdl_register_result(&p, false, ESP_ERR_INVALID_ARG);

    p = mock_bdl_default_params();
    p.erase_before_write = false;
    p.and_type_write = false;
    p.write_size = 0;
    p.strict_erase_alignment = false;
    assert_mock_bdl_register_result(&p, false, ESP_ERR_INVALID_ARG);

    p = mock_bdl_default_params();
    p.erase_before_write = false;
    p.and_type_write = false;
    p.read_size = 16;
    p.write_size = 48; /* lcm = 48 */
    p.disk_size = (48 * 10) - 1;
    p.strict_erase_alignment = false;
    assert_mock_bdl_register_result(&p, false, ESP_ERR_INVALID_ARG);
}

TEST_CASE("bdl classic sector size uses recommended erase size", "[littlefs_bdl_geom]")
{
    mock_bdl_params_t p = mock_bdl_default_params();
    p.erase_before_write = true;
    p.read_size = 16;
    p.write_size = 16;
    p.erase_size = 512;
    p.recommended_erase_size = 1024;
    p.disk_size = 1024 * 8;

    esp_blockdev_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, try_register_mock_bdl(&p, false, true, true, &handle));
    TEST_ASSERT_NOT_NULL(handle);

    mock_bdl_ctx_t *ctx = (mock_bdl_ctx_t *)handle->ctx;
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_GREATER_THAN(0, ctx->erase_calls);
    TEST_ASSERT_EQUAL(1024, ctx->last_erase_len);

    TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(handle));
    mock_bdl_destroy_handle(handle);
}

TEST_CASE("bdl logical sector size uses lcm(read,write)", "[littlefs_bdl_geom]")
{
    mock_bdl_params_t p = mock_bdl_default_params();
    p.erase_before_write = false;
    p.and_type_write = false;
    p.read_size = 64;
    p.write_size = 96; /* lcm = 192 */
    p.erase_size = 4096; /* ignored for logical mode sizing */
    p.disk_size = 192 * 20;
    p.strict_erase_alignment = false;

    esp_blockdev_handle_t handle = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, try_register_mock_bdl(&p, false, true, true, &handle));
    TEST_ASSERT_NOT_NULL(handle);

    mock_bdl_ctx_t *ctx = (mock_bdl_ctx_t *)handle->ctx;
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_GREATER_THAN(0, ctx->erase_calls);
    TEST_ASSERT_EQUAL(192, ctx->last_erase_len);

    TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(handle));
    mock_bdl_destroy_handle(handle);
}

TEST_CASE("duplicate blockdev registration keeps existing mount intact", "[littlefs_bdl_geom]")
{
    mock_bdl_params_t p = mock_bdl_default_params();
    esp_blockdev_handle_t handle = NULL;
    TEST_ESP_OK(mock_bdl_create_custom(&handle, &p, true));
    TEST_ASSERT_NOT_NULL(handle);

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = littlefs_base_path,
        .partition_label = NULL,
        .partition = NULL,
        .blockdev = handle,
        .dont_mount = false,
        .read_only = false,
        .format_if_mount_failed = true,
    };

    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    TEST_ASSERT_TRUE(esp_littlefs_blockdev_mounted(handle));

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, esp_vfs_littlefs_register(&conf));
    TEST_ASSERT_TRUE(esp_littlefs_blockdev_mounted(handle));
    TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(handle));

    mock_bdl_destroy_handle(handle);
}

TEST_CASE("format blockdev does not consume caller handle", "[littlefs_bdl_geom]")
{
    mock_bdl_params_t p = mock_bdl_default_params();
    esp_blockdev_handle_t handle = NULL;
    TEST_ESP_OK(mock_bdl_create_custom(&handle, &p, true));
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ESP_OK(esp_littlefs_format_blockdev(handle));
    TEST_ASSERT_NOT_NULL(handle->ops);

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = littlefs_base_path,
        .partition_label = NULL,
        .partition = NULL,
        .blockdev = handle,
        .dont_mount = false,
        .read_only = false,
        .format_if_mount_failed = true,
    };

    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(handle));

    mock_bdl_destroy_handle(handle);
}
