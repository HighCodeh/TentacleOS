#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "st7789.h" // Seu header do driver (deve expor lcd_init e handles)

// Definições de Resolução e Buffer
// Buffer de 1/10 da tela (ajuste conforme RAM disponível)
#define LVGL_BUF_PIXELS (LCD_H_RES * 40)


static lv_display_t * disp_handle = NULL;

// 1. Callback de quando o DMA do ESP32 termina de enviar (ISR Context)
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t * disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

// 2. Callback de Flush (LVGL pede para desenhar)
static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  uint32_t px_count = w * h;

  lv_draw_sw_rgb565_swap(px_map, px_count);

  // 3. Envia para o driver ESP_LCD via DMA
  esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}
void lv_port_disp_init(void)
{
    disp_handle = lv_display_create(LCD_H_RES, LCD_V_RES);
    
    // B. Define o Callback de Flush
    lv_display_set_flush_cb(disp_handle, disp_flush);

    // C. Aloca Buffers em memória DMA (Internal RAM é mais rápida, mas se faltar use SPIRAM)
    // Multiplicamos por sizeof(lv_color_t) porque o malloc pede bytes
    size_t buffer_size_bytes = LVGL_BUF_PIXELS * sizeof(lv_color_t);
    
    void *buf1 = heap_caps_malloc(buffer_size_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buffer_size_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    // Verifica se alocou
    if(buf1 == NULL || buf2 == NULL) {
        // Se falhar, tente sem MALLOC_CAP_INTERNAL ou diminua o buffer
        // ESP_LOGE("LVGL", "Falha ao alocar memória DMA para o display");
        return;
    }

    // D. Configura os Buffers no Display (Double Buffering)
    // Modo PARTIAL é o padrão (renderiza pedaços da tela)
    lv_display_set_buffers(disp_handle, buf1, buf2, buffer_size_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // E. Registra o Callback do ESP-LCD para avisar o LVGL quando o DMA acabar
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    
    // Passamos 'disp_handle' como contexto para saber quem notificar no callback
    esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, disp_handle);
}
