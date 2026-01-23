#ifndef ASSETS_MANAGER_H
#define ASSETS_MANAGER_H

#include "lvgl.h"
#include <stdbool.h>

void assets_manager_init(void);

lv_image_dsc_t * assets_get(const char * path);

void assets_manager_free_all(void);

#endif // ASSETS_MANAGER_H