/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */


#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "ft2build.h"
#include FT_FREETYPE_H

static const char *TAG = "example";

static void init_filesystem(void);
static void init_freetype(void);
static void load_font(void);
static void render_text(void);

#define BITMAP_WIDTH  80
#define BITMAP_HEIGHT 18

static FT_Library  s_library;
static FT_Face s_face;
static uint8_t s_bitmap[BITMAP_HEIGHT][BITMAP_WIDTH];


void app_main(void)
{
    init_filesystem();
    init_freetype();
    load_font();
    render_text();
}

static void init_filesystem(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/fonts",
        .partition_label = "fonts",
        .max_files = 1,
    };

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
}

static void init_freetype(void)
{
    FT_Error error = FT_Init_FreeType( &s_library );
    if (error) {
        ESP_LOGE(TAG, "Error initializing FreeType library: %d", error);
        abort();
    }

    ESP_LOGI(TAG, "FreeType library initialized");
}

static void load_font(void)
{
    FT_Error error = FT_New_Face( s_library,
                                  "/fonts/DejaVuSans.ttf",
                                  0,
                                  &s_face );
    if (error) {
        ESP_LOGE(TAG, "Error loading font: %d", error);
        abort();
    }

    ESP_LOGI(TAG, "Font loaded");

}

static void render_text(void)
{
    /* Configure character size. */
    const int font_size = 14;
    const int freetype_scale = 64;
    FT_Error error = FT_Set_Char_Size(s_face, 0, font_size * freetype_scale, 0, 0 );
    if (error) {
        ESP_LOGE(TAG, "Error setting font size: %d", error);
        abort();
    }

    const char *text = "FreeType";
    int num_chars = strlen(text);

    /* current drawing position */
    int x = 0;
    int y = 12;

    for (int n = 0; n < num_chars; n++) {
        ESP_LOGI(TAG, "Rendering char: '%c'", text[n]);

        /* retrieve glyph index from character code */
        FT_UInt  glyph_index = FT_Get_Char_Index( s_face, text[n] );

        /* load glyph image into the slot (erase previous one) */
        error = FT_Load_Glyph( s_face, glyph_index, FT_LOAD_DEFAULT );
        if (error) {
            ESP_LOGE(TAG, "Error loading glyph: %d", error);
            abort();
        }

        /* convert to a bitmap */
        error = FT_Render_Glyph( s_face->glyph, FT_RENDER_MODE_NORMAL );
        if (error) {
            ESP_LOGE(TAG, "Error rendering glyph: %d", error);
            abort();
        }

        /* copy the glyph bitmap into the overall bitmap */
        FT_GlyphSlot slot = s_face->glyph;
        for (int iy = 0; iy < slot->bitmap.rows; iy++) {
            for (int ix = 0; ix < slot->bitmap.width; ix++) {
                /* bounds check */
                int res_x = ix + x;
                int res_y = y + iy - slot->bitmap_top;
                if (res_x >= BITMAP_WIDTH || res_y >= BITMAP_HEIGHT) {
                    continue;
                }
                s_bitmap[res_y][res_x] = slot->bitmap.buffer[ix + iy * slot->bitmap.width];
            }
        }

        /* increment horizontal position */
        x += slot->advance.x / 64;
        if (x >= BITMAP_WIDTH) {
            break;
        }
    }

    /* output the resulting bitmap to console */
    for (int iy = 0; iy < BITMAP_HEIGHT; iy++) {
        for (int ix = 0; ix < x; ix++) {
            int val = s_bitmap[iy][ix];
            if (val > 127) {
                putchar('#');
            } else if (val > 64) {
                putchar('+');
            } else if (val > 32) {
                putchar('.');
            } else {
                putchar(' ');
            }
        }
        putchar('\n');
    }
}
