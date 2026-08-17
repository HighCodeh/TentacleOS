#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void hle_kernel_init(void);
void hle_kernel_shutdown(void);
void hle_set_button_mask(unsigned char mask);
void hle_set_storage_path(const char *path);
const char *hle_get_storage_path(void);

#ifdef __cplusplus
}
#endif
