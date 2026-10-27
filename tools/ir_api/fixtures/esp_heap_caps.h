#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(size,caps) malloc(size)
#define heap_caps_realloc(ptr,size,caps) realloc(ptr,size)
