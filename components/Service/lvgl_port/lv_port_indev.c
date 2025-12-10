#include "core/lv_group.h"
#include "pin_def.h"
#include "buttons_gpio.h" 
#include "esp_log.h"
#include "lv_port_indev.h"
#include "lvgl.h"

static void keypad_read(lv_indev_t * indev, lv_indev_data_t * data);
static uint32_t keypad_get_key(void);

lv_indev_t * indev_keypad = NULL;
lv_group_t * main_group = NULL;

void lv_port_indev_init(void)
{
    // --- LVGL v9: Criação do Input Device ---
    
    // 1. Cria o dispositivo de entrada (não precisa mais de lv_indev_drv_t)
    indev_keypad = lv_indev_create();

    // 2. Define o tipo como KEYPAD (Teclado/Botões físicos)
    lv_indev_set_type(indev_keypad, LV_INDEV_TYPE_KEYPAD);

    // 3. Define a função de leitura (callback)
    lv_indev_set_read_cb(indev_keypad, keypad_read);

    // Opcional: Se você tiver um "Group" (grupo de navegação), você associaria aqui depois
    // lv_indev_set_group(indev_keypad, group);
    // 2. Cria o Grupo de Navegação
    main_group = lv_group_create();
    
    // 3. Define como padrão (qualquer widget criado depois disso entra no grupo auto)
    lv_group_set_default(main_group);

    // 4. Associa o Teclado físico ao Grupo Lógico
    lv_indev_set_group(indev_keypad, main_group);
}

// Callback chamado periodicamente pelo LVGL para ler o estado dos botões
static void keypad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    static uint32_t last_key = 0;

    // Obtém a tecla pressionada do hardware (sua função de GPIO)
    uint32_t act_key = keypad_get_key();

    if(act_key != 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        last_key = act_key;
        ESP_LOGI("INDEV", "Tecla Pressionada: %d", (int)act_key);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    data->key = last_key;
}

// Função auxiliar para mapear seus botões físicos para teclas do LVGL
static uint32_t keypad_get_key(void)
{
    // AQUI entra a lógica dos seus botões do HighBoy
    // Exemplo (supondo que suas funções de botão retornem true se pressionado):
    
  //navegation
  if(up_button_is_down()) return LV_KEY_PREV;
  if(down_button_is_down()) return LV_KEY_NEXT;

  if(ok_button_is_down()) return LV_KEY_ENTER;
  if(back_button_is_down()) return LV_KEY_ESC;
  if(left_button_is_down()) return LV_KEY_LEFT;
  if(right_button_is_down()) return LV_KEY_RIGHT;

  return 0; // Nenhuma tecla pressionada
}
