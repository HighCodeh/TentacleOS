/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "esp_heap_caps.h"
#include "sdkconfig.h"

/*
 * Where large, non-DMA buffers and task stacks go: PSRAM when the board has
 * it, internal RAM otherwise (the buffers shrink to match; see MUSE_LOW_MEM).
 */
#if CONFIG_SPIRAM
#define MUSE_BIG_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define MUSE_LOW_MEM 0
#else
#define MUSE_BIG_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define MUSE_LOW_MEM 1
#endif

/* Core for audio work: the second core when there is one. */
#if CONFIG_FREERTOS_UNICORE
#define MUSE_AUDIO_CORE 0
#else
#define MUSE_AUDIO_CORE 1
#endif

/*
 * LVGL's task on boards that start it themselves: on the audio core, just
 * below the audio task, which leaves the first core to Wi-Fi, the network and
 * the reply's decoding. Left unpinned, it stays on whichever core it first
 * touched the FPU on, usually the first.
 */
#define MUSE_UI_CORE MUSE_AUDIO_CORE
#define MUSE_UI_PRIORITY 5
