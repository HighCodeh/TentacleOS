// Em um novo arquivo, por exemplo, main/usb_stream.c
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include "st7789.h"

static const char *TAG = "USB_STREAM";

// Esta é a nossa nova função de envio
void send_framebuffer_over_usb() {
    uint16_t* fb = st7789_get_framebuffer();
    if (fb == NULL) {
        // Não faz nada se o framebuffer não estiver pronto
        return;
    }

    size_t framebuffer_size = ST7789_WIDTH * ST7789_HEIGHT * sizeof(uint16_t);
    
    // Escreve os bytes brutos do framebuffer diretamente para a saída padrão (stdout),
    // que agora é a nossa porta USB CDC.
    fwrite(fb, 1, framebuffer_size, stdout);
    fflush(stdout); // Garante que os dados sejam enviados imediatamente
}