# TentacleOS - Auditoria da Camada Operacional

Levantamento do que falta para o sistema ser robusto do ponto de vista de **sistema operacional**,
não de features. Escopo: gerenciamento de energia, sono, brilho, entrada, recuperação de falhas,
concorrência, memória, tempo, contenção de barramento e ciclo de vida.

Explicitamente **fora de escopo**: correção funcional de features (leitura NFC, TX SubGhz, LoRa, BLE).
Onde uma feature aparece aqui, é como evidência de um problema estrutural, não como bug de feature.

**Alvo auditado**: `firmware_p4` e `firmware_c5`, branch `dev-v1`. Os itens 1 a 40 descrevem o P4. A
**seção C5** ao final audita o coprocessador, que compartilha o mesmo `Core` e vários `Service` do P4.

**Correção de uma versão anterior deste documento**: uma versão anterior afirmou que o `firmware_c5`
"não tem `main/` nem `Core/`, logo não é executável". **Isso estava errado**, foi resultado de um erro
de caminho relativo na verificação. O C5 tem `main/main.c` que chama `kernel_init()`, tem
`components/Core/` completo (`kernel.c` + `sys_monitor.c`), e é um sistema executável completo com
WiFi, Bluetooth NimBLE, DNS, HTTP, ESP-NOW, Meshtastic e MeshCore. Ver a seção C5.

Sobre o estado atual do bridge: `firmware_p4/components/Core/kernel.c:91-97` desativa o bridge e marca
`spi_bridge_set_alive(false)` porque o C5 da placa de teste não responde. Consequência que atravessa a
auditoria do P4: enquanto o bridge estiver morto, o P4 não tem rádio próprio e **toda conectividade do
produto depende de um coprocessador desligado** (ver item 27). O C5 em si, quando bootado, funciona.

**Nota sobre PSRAM**: itens ligados a ausência de PSRAM foram omitidos por decisão de produto. A V1
está com PSRAM desativada de propósito e o protótipo a caminho já tem PSRAM funcional. Onde a PSRAM
muda o cálculo de um item, está anotado no próprio item.

Os números dos itens são **identificadores estáveis**. Use-os em commits e issues.

---

## Aviso: duas recomendações desta auditoria se atropelam

Antes de qualquer coisa, a armadilha mais perigosa do documento.

O item 2 recomenda ligar `CONFIG_ESP_TASK_WDT_PANIC=y` para que travamento de task vire reboot em vez
de congelamento permanente. **Se isso for ligado antes de consertar o item 30, o aparelho vai
reiniciar no meio da escrita da partição de OTA** e ficar com uma imagem parcial.

Ordem obrigatória: **item 30 antes do item 2.** Está refletido no roadmap.

---

## Sumário por severidade

| # | Item | Sev | Área |
|---|---|---|---|
| 1 | Rollback infinito de OTA por dependência do C5 | Crítico | Boot / Update |
| 2 | `ui_acquire()` espera infinito, deadlock silencioso | Crítico | Concorrência |
| 3 | `tos_log` com race na rotação e flush por linha | Crítico | Logging / Timing |
| 30 | Loop de escrita do OTA sem yield, incompatível com WDT panic | Crítico | Update / WDT |
| 4 | Sem `esp_reset_reason` e sem detecção de boot-loop | Alto | Recuperação |
| 5 | `ESP_ERROR_CHECK` nos caminhos de init | Alto | Boot |
| 6 | `sys_monitor` mata tasks e escreve na UI sem lock | Alto | Recuperação |
| 7 | `ui_task` zumbi, recuperação protege a task errada | Alto | Recuperação |
| 8 | Retornos de init descartados no `kernel_init` | Alto | Boot |
| 9 | Gerenciamento de energia inexistente | Alto | Energia |
| 10 | Timeout de tela e auto-dim declarados mas não implementados | Alto | Energia / UX |
| 11 | Entrada sem camada de eventos, sem rastreio de atividade | Alto | Entrada |
| 27 | Barramento SPI3 compartilhado entre display e três rádios | Alto | Contenção |
| 28 | Timers do header forçam redraw a 2 Hz para sempre | Alto | Energia |
| 31 | Nenhum desligamento gracioso, 5 `esp_restart()` sem flush | Alto | Persistência |
| 38 | Binário a 93% do slot de OTA, teto iminente | Alto | Flash / Update |
| 39 | Partição de assets 100% cheia, zero folga | Alto | Flash / Storage |
| 12 | Anarquia de prioridades, 18 tasks acima do renderizador | Médio-Alto | Escalonamento |
| 13 | Sem contrato de ciclo de vida de aplicação | Médio-Alto | Arquitetura |
| 14 | Telas nunca são liberadas ao navegar | Médio | Memória |
| 15 | Sem política de heap e fragmentação | Médio | Memória |
| 16 | Sem fonte de tempo, tudo datado em 1970 | Médio | Tempo |
| 17 | APIs de recuperação existem e nunca são chamadas | Médio | Recuperação |
| 18 | Contador de boots no lugar errado | Médio | Observabilidade |
| 19 | Escrita de config não atômica | Médio | Persistência |
| 20 | Sem hotplug de SD | Médio | Storage |
| 21 | Sem guarda térmica | Médio | Energia |
| 22 | Coredump desativado, zero forense de campo | Médio | Observabilidade |
| 23 | Sem factory reset nem modo seguro | Médio | Recuperação |
| 29 | Header mente sobre bateria, wifi e hora | Médio | UX operacional |
| 41 | Estado de USB não alimenta o gerenciamento de energia | Médio | Energia |
| 32 | `assets_manager` sem limite nem eviction | Médio | Memória |
| 40 | Orçamento de stacks sem controle, 16 KB desperdiçados | Médio | Memória |
| 33 | Três drivers órfãos, sem dono de inicialização | Médio | Arquitetura |
| 34 | `qmi8658a_configure` sequestra o barramento do display | Médio | Contenção |
| 35 | I2C no driver legado, sem recuperação de bus travado | Médio | Drivers |
| 36 | Credenciais em texto claro, sem flash encryption | Médio | Segurança |
| 24 | `gpio_install_isr_service` duplicado, ordem frágil | Baixo | Drivers |
| 25 | `input_lock_until` estoura em 49,7 dias | Baixo | Entrada |
| 26 | Delay de panic em 0 segundo | Baixo | Observabilidade |
| 37 | Log em nível INFO amplifica o custo do item 3 | Baixo | Logging |

## Índice temático

**Boot e atualização**: 1, 5, 8, 30, 38
**Recuperação de falhas**: 2, 4, 6, 7, 17, 23
**Energia e sono**: 9, 10, 21, 28, 41
**Entrada**: 11, 25
**Escalonamento e contenção**: 12, 27, 34, 35
**Arquitetura e ciclo de vida**: 13, 33
**Memória**: 14, 15, 32, 40
**Persistência e tempo**: 16, 19, 31
**Observabilidade**: 18, 22, 26, 29, 37
**Storage**: 20, 39
**Drivers**: 24, 35
**Segurança**: 36
**Orçamento de flash e RAM**: 38, 39, 40

Itens que impõem **limite duro** e não apenas degradação: 38 e 39. Ler primeiro se o plano
envolve adicionar código ou assets.

---

# Boot e atualização

## 1. Rollback infinito de OTA por dependência do C5

**Severidade: Crítico. Perde firmware.**

`main/main.c:8-11`:

```c
void app_main(void) {
  ota_post_boot_check();
  kernel_init();
}
```

`components/Service/ota/ota_service.c:256-266`:

```c
if (ret == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
  ret = bridge_manager_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "C5 sync failed — rollback will occur on next reboot");
    return ESP_FAIL;
  }
  esp_ota_mark_app_valid_cancel_rollback();
}
```

Com `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (`sdkconfig.defaults`), toda imagem nova sobe em
`PENDING_VERIFY` e só é confirmada se `bridge_manager_init()` retornar OK. Mas o C5 está desativado
de propósito (`kernel.c:96-97`), então a validação nunca passa e **o aparelho volta ao firmware
anterior no reboot seguinte, indefinidamente**.

Dois agravantes de ordem:

- `ota_post_boot_check()` roda **antes** de `spi_init()` (`kernel.c:72`), então o bridge não teria
  barramento nem se o C5 estivesse vivo.
- `sync_version_to_assets()` (`ota_service.c:52-77`, chamado em `:271`) roda antes de
  `storage_assets_init()` (`kernel.c:77`). O `storage_assets_load_file` retorna NULL e a função sai
  silenciosamente. O `firmware.json` nunca é atualizado.

**Correção**: validar a imagem com critérios locais e obrigatórios (LittleFS montado e passando
`storage_health_check` do item 17, painel inicializado, LVGL respondendo por N segundos), nunca com
periférico opcional. Mover a checagem para depois do `kernel_init`. Se quiser manter o sync do C5, ele
deve ser um passo *opcional* que loga aviso e não bloqueia a validação.

## 5. `ESP_ERROR_CHECK` nos caminhos de init

**Severidade: Alto. Converte falha transitória de hardware em boot-loop.**

`ESP_ERROR_CHECK` chama `abort()`. Distribuição atual:

| Arquivo | Ocorrências |
|---|---|
| `Drivers/st7789/st7789.c` | 9 |
| `Applications/SubGhz/subghz_receiver.c` | 6 |
| `Service/console/console_service.c` | 5 |
| `Service/console/commands/cmd_system.c` | 5 |
| `Service/console/commands/cmd_fs.c` | 4 |
| `Applications/SubGhz/subghz_transmitter.c` | 4 |
| `Core/kernel.c` | 2 |
| outros (`cmd_wifi`, `cmd_hostlink`, `cmd_badusb`, `c5_flasher`) | 1 cada |

As 9 do `st7789.c` são as mais graves: num handheld com display em conector FFC, um contato marginal
no init do painel vira `abort()` no boot. Somado ao item 1 (rollback ativo) e ao item 4 (sem detecção
de loop), o resultado é exatamente "reinicia sozinho e às vezes volta versão".

**Correção**: nos caminhos de init, propagar `esp_err_t` e decidir. Falha de display é fatal mas
merece retry e mensagem; falha de CC1101 ou RFID deve apenas desabilitar a feature e seguir o boot.

## 8. Retornos de init descartados no `kernel_init`

**Severidade: Alto. Falha de subsistema fica invisível.**

`components/Core/kernel.c:71-119`: **todas** as chamadas de init descartam o retorno.

```c
spi_init();
init_i2c();              // void, ver item 35
storage_init();          // retorna esp_err_t, descartado
storage_assets_init();   // retorna esp_err_t, descartado
tos_config_load_all();
tos_log_init();
led_rgb_init();
bq25896_init();          // retorna esp_err_t, descartado
cc1101_init();
buttons_init();
ys_rfid2_init(NULL);
st7789_init();
lvgl_glue_init();        // retorna esp_err_t, descartado
...
```

O caso mais grave é `storage_init()`. Se a montagem da LittleFS falhar, o boot continua como se nada
tivesse acontecido: `tos_config_load_all()` cai em defaults silenciosamente, `tos_log_init()` nunca
escreve nada, `storage_assets_init()` não acha ícone nenhum, e o usuário vê uma UI sem ícones e sem
configuração salva, sem nenhuma indicação do motivo.

Também não há **supervisão de fase**: se um init bloquear (dispositivo I2C sem ACK, SD sem responder),
o boot para com tela preta, sem timeout e sem fallback. Não há tela de boot progressivo que mostre em
qual etapa parou.

**Correção**: `kernel_init` retorna `esp_err_t` e agrega um mapa de subsistemas
(`{nome, obrigatório, resultado}`). Subsistema obrigatório que falha aborta para o modo degradado do
item 4; opcional que falha marca a feature indisponível e segue. Expor esse mapa na tela de about e
via `host_link` para diagnóstico.

## 30. Loop de escrita do OTA sem yield, incompatível com WDT panic

**Severidade: Crítico. Bloqueia a correção do item 2.**

`components/Service/ota/ota_service.c:169-196`:

```c
while (bytes_written < file_size) {
  size_t to_read = ...;
  size_t bytes_read = fread(buffer, 1, to_read, f);
  ...
  ret = esp_ota_write(ota_handle, buffer, bytes_read);
  ...
  bytes_written += bytes_read;
  // progresso via callback
}
```

**Nenhum `vTaskDelay`, nenhum `esp_task_wdt_reset`, nenhum yield.** É um laço apertado sobre uma
imagem de até 3 MB (`partitions.csv`: `ota_0` e `ota_1` de 3M cada) fazendo `fread` da LittleFS e
`esp_ota_write` na flash. As duas operações desabilitam a cache.

Com `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` e `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y` +
`CPU1=y`, esse laço mata de fome a idle task do core onde roda e o Task Watchdog dispara.

Hoje isso passa despercebido porque `CONFIG_ESP_TASK_WDT_PANIC is not set`: o TWDT só imprime
warning. **No momento em que o item 2 ligar `TASK_WDT_PANIC=y`, este laço vai reiniciar o aparelho no
meio da gravação da partição de OTA.**

O mesmo padrão de risco existe em `c5_flasher.c:214` (laço de `uart_write_bytes` sobre a imagem do
C5), embora ali o `uart_write_bytes` bloqueante ceda a CPU naturalmente.

**Correção**: `vTaskDelay(pdMS_TO_TICKS(1))` a cada chunk, ou explicitamente inscrever a task no TWDT
e chamar `esp_task_wdt_reset()` por iteração. Fazer **antes** de mexer em `TASK_WDT_PANIC`.

---

# Recuperação de falhas

## 2. `ui_acquire()` espera infinito, deadlock silencioso

**Severidade: Crítico. Trava permanente sem recuperação.**

`components/Applications/ui/ui_manager.c:317-319`:

```c
bool ui_acquire(void) {
  return lvgl_glue_lock(-1);
}
```

`-1` chega em `lvgl_port_lock` como `portMAX_DELAY`. Qualquer task que pegue o lock e não devolva
congela **permanentemente** todas as outras que tocam a UI. Não há timeout, não há log, não há reset.

O que fecha o cerco: **nada no projeto está inscrito no Task Watchdog**. `esp_task_wdt_add` não
aparece em nenhum arquivo, e `CONFIG_ESP_TASK_WDT_PANIC is not set`. Então uma task travada só imprime
warning para sempre e a tela fica congelada até o usuário deixar a bateria acabar. Não há botão de
desligar (item 9c).

Existem dezenas de callsites de `ui_acquire()` espalhados pelas telas.

**Correção**:
- `ui_acquire()` com timeout finito (1000 ms) e retorno tratado em todos os callsites.
- Inscrever a task do LVGL no TWDT via `esp_task_wdt_add`.
- Ligar `CONFIG_ESP_TASK_WDT_PANIC=y`, **depois** de resolver o item 30.

## 4. Sem `esp_reset_reason` e sem detecção de boot-loop

**Severidade: Alto. É a causa direta do sintoma "reinicia sozinho".**

`esp_reset_reason()` não é chamado em nenhum lugar do projeto. Não existe registro de "o boot anterior
foi panic", nem contador de boots anormais consecutivos, nem modo degradado.

Consequência: um panic causado por hardware marginal (conector FFC do display, cartão SD ruim,
periférico I2C sem ACK) leva a um loop de boot que se repete para sempre, sem nenhum rastro e sem
nenhuma tentativa de subir de forma reduzida.

**Correção**: no primeiro passo do `app_main`, ler `esp_reset_reason()`. Se for `ESP_RST_PANIC` /
`ESP_RST_TASK_WDT` / `ESP_RST_INT_WDT` / `ESP_RST_BROWNOUT`, incrementar um contador. Zerar depois de
N segundos de boot estável (mesmo gatilho que valida o OTA no item 1). Ao atingir 3 boots anormais
consecutivos, subir em **modo degradado**: sem rádios, sem assets de SD, UI mínima com a razão do
reset na tela e opção de factory reset (item 23).

Detalhe de implementação que evita desgaste de NVS: `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=y`
já está ligado, então o contador pode viver em `RTC_NOINIT_ATTR`, que sobrevive a reset mas não a
power cycle. Isso é exatamente a semântica desejada para detectar boot-loop, e não escreve em flash.
Persistir em NVS só o resumo (última razão de reset, total de panics).

Este é o item de melhor custo-benefício de toda a lista.

## 6. `sys_monitor` mata tasks e escreve na UI sem lock

**Severidade: Alto. A recuperação causa dano pior que a falha.**

`components/Core/sys_monitor.c:42-78`:

```c
if (watermark >= CRITICAL_STACK_THRESHOLD) continue;   // limite = 256 bytes

if (strcmp(tasks[i].pcTaskName, "UI Task") == 0) {
  vTaskDelete(tasks[i].xHandle);
  ui_hard_restart();
  continue;
}

ui_switch_screen(SCREEN_HOME);            // linha 63: SEM ui_acquire()
...
safeguard_alert("SYSTEM PROTECTED", msg_buf);
if (tasks[i].xHandle != xTaskGetCurrentTaskHandle()) {
  vTaskDelete(tasks[i].xHandle);          // linha 75
}
```

Três problemas:

1. **`vTaskDelete` arbitrário**: matar uma task no meio de uma transação I2C ou SPI vaza o mutex
   interno do barramento. O driver fica travado em definitivo e nenhuma outra task consegue usar aquele
   periférico até reboot. Isso transforma "stack apertada" em "periférico morto". Pior no SPI3, que é
   compartilhado com o display (item 27): matar uma task de rádio pode levar o display com ela.
2. **`ui_switch_screen()` na linha 63 roda sem o lock do LVGL.** Só o `safeguard_alert`
   (`kernel.c:130-137`) pega `ui_acquire()`. Race direto com a task de render.
3. **Limiar de 256 bytes é muito baixo para agir destruindo.** Uma task pode ficar abaixo disso
   transitoriamente e voltar.

**Correção**: o monitor deve **observar e reportar**, não matar. Logar, avisar na UI (com lock), e
escalar para `esp_restart()` controlado (com o shutdown gracioso do item 31) se a condição persistir
por N ciclos. Se algum caso realmente exigir matar uma task, o dono do subsistema precisa expor um
`*_abort()` que libere seus recursos.

## 7. `ui_task` zumbi, recuperação protege a task errada

**Severidade: Alto. Todo o caminho de recuperação de UI é código morto.**

`components/Applications/ui/ui_manager.c:132-159`:

```c
static void ui_task(void *pvParameter) {
  // splash de boot, abre a home
  ...
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(UI_IDLE_LOOP_MS));
  }
}
```

Depois do boot, a `"UI Task"` **não faz absolutamente nada**. Todo o trabalho real de UI acontece na
task do `esp_lvgl_port` (prioridade 4, `lvgl_glue.c:19`) e nos `lv_timer` de cada tela.

Mas o `sys_monitor` tem tratamento especial exatamente para o nome `"UI Task"`
(`sys_monitor.c:56-60`). Como a task só dorme, o watermark dela nunca cai. **A lógica de "UI recovery"
vigia uma task que não pode falhar, enquanto a task que realmente pode travar (a do LVGL) o monitor
não conhece por nome.**

Agravante: `ui_hard_restart()` (`ui_manager.c:124-130`) cria uma segunda task com o mesmo nome sem
deletar a anterior. É pública. Se for chamada de qualquer outro lugar além do `sys_monitor` (que
deleta antes na linha 58), você fica com duas `"UI Task"` disputando o lock do LVGL.

**Correção**: escolher um dos dois caminhos. Ou dar função real à `ui_task` (dono do laço de eventos
de entrada do item 11), ou deletá-la e apontar toda a supervisão para a task do LVGL. Em ambos os
casos, remover o casamento por string de nome de task.

## 17. APIs de recuperação existem e nunca são chamadas

**Severidade: Médio. Trabalho já feito, desperdiçado.**

`components/Service/storage_api/storage_init.c` implementa:

- `storage_health_check()` (`:95-122`): faz write e read de teste e retorna erro se o filesystem
  estiver ruim;
- `storage_remount()` (`:83-93`): desmonta e remonta.

**Nenhuma das duas é chamada em lugar algum do projeto.** Verificado por grep em `components/` e
`main/`.

O mesmo padrão vale para as 16 funções `*_deinit` do item 13 e para `assets_manager_free_all()` do
item 32. Há um padrão recorrente no projeto: **a API de recuperação é escrita e nunca é ligada.**

**Correção**: `storage_health_check()` deve rodar no boot (e virar critério de validação do OTA do
item 1) e periodicamente no `sys_monitor`. Falha dispara `storage_remount()` e, se persistir, modo
degradado do item 4.

## 23. Sem factory reset nem modo seguro

**Severidade: Médio.**

Não existe factory reset de sistema. Os únicos `nvs_erase_all` do projeto são de escopo de feature:
`meshtastic_nvs.c:114` (`mt_nvs_factory_reset`), `mf_key_dict.c:70`, `meshcore_nvs.c:32`.
`kernel.c:66` faz `nvs_flash_erase()` mas só como recuperação de NVS corrompida no init.

Não existe modo seguro. As únicas coisas parecidas com recuperação são:

- o `is_recovery` do `ui_task` (`ui_manager.c:134-147`), que como visto no item 7 é código morto;
- `ACTION_REBOOT_P4` no settings (`settings_ui.c:400-404`), que é só um reboot voluntário;
- o `c5_rom_flasher.c`, que é recuperação do C5, não do P4.

Consequência: se a config na LittleFS ficar inconsistente (item 19) ou um tema quebrado travar a UI, o
usuário não tem caminho de saída. Não há combinação de botões no boot que suba mínimo.

**Correção**: combinação de teclas no boot (ex. segurar OK + BACK durante o splash, o que depende do
long-press do item 11) que entre num modo seguro: sem tema custom, sem assets de SD, sem rádios, com
opções de "resetar config", "resetar tudo", "ver último crash" (item 22) e "ver mapa de boot"
(item 8). Esse modo é o mesmo alvo do modo degradado do item 4. Vale implementar uma vez.

---

# Energia e sono

## 9. Gerenciamento de energia inexistente

**Severidade: Alto. É a lacuna funcional mais larga da camada operacional.**

Nada de `esp_pm` no projeto. Grep vazio para: `esp_pm_configure`, `esp_pm_lock`, `esp_sleep`,
`gpio_wakeup`, `rtc_gpio`, `light_sleep`, `deep_sleep`.

### 9a. CPU sempre no máximo

`CONFIG_PM_ENABLE is not set`. Sem tickless idle. `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360`, o máximo do
P4, travado inclusive com o aparelho parado num menu.

### 9b. Nenhum modo de sono e nenhuma máquina de estados

Não existe o conceito de estado de energia. Falta a máquina:
`ativo -> dim -> tela off -> light sleep -> ship mode`, com transições por inatividade e por bateria,
e com wake por GPIO de botão.

Atenção: mesmo que `PM_ENABLE` e tickless idle sejam ligados hoje, **o item 28 impede a CPU de
chegar a idle**. A ordem importa.

### 9c. O desligar não desliga

`components/Drivers/bq25896/bq25896_ext.c` (57 linhas) é mock declarado no próprio header
(`bq25896.h`, comentário antes de `bq25896_get_charge_enable`):

- `bq25896_power_off()` (ship mode via BATFET_DIS): no-op.
- `bq25896_set_charge_enable()`: no-op.
- VSYS/VBUS em mV, correntes, registradores crus e fault: valores inventados.

`power_ui.c:173` chama `bq25896_power_off()` e nada acontece. **O aparelho não tem desligar de
verdade.** Isso interage com o item 2: numa UI travada, o usuário não tem nem como forçar desligar.

São reais apenas: tensão de bateria, percentual, status de carga e status de VBUS
(`bq25896.c:121-160`).

### 9d. Percentual de bateria sem filtro

`bq25896_get_battery_percentage()` (`bq25896.c:144-152`) é lookup de tensão puro. Sem filtro, sem
compensação de carga, sem histerese. Quando o CC1101 ou o SX1262 transmite, a tensão cai sob carga e
o percentual pula para baixo, depois volta.

Hoje isso é academicamente invisível porque o indicador de bateria da UI nunca atualiza (item 29).

### 9e. Nenhuma política de bateria baixa

Nada avisa em 15%. Nada força dim em 10%. Nada bloqueia TX de rádio em 5%. Nada faz shutdown gracioso
perto do corte. Nada persiste estado antes de morrer (item 31).

**Correção**: um serviço `power_manager` em `components/Core`, dono de:

- estado de energia e transições, alimentado pelo `last_activity` do item 11;
- `esp_pm_configure` com faixa de frequência, e `esp_pm_lock` do tipo `NO_LIGHT_SLEEP` adquirido pelos
  donos de recurso (rádio ativo, USB conectado, SD em uso, áudio tocando), liberado pelo `close` do
  item 13;
- curva de SoC filtrada com compensação por corrente e histerese;
- thresholds de bateria e as ações associadas;
- ship mode real (desmockar o BQ25896).

## 10. Timeout de tela e auto-dim declarados mas não implementados

**Severidade: Alto. Metade já está escrita e não está ligada em nada.**

### 10a. Os campos de config existem e ninguém lê

`components/Service/storage_api/include/tos_config.h:42-48`:

```c
typedef struct {
  int brightness;
  int rotation;
  char theme[32];
  int auto_lock_seconds;
  bool auto_dim;
} tos_config_screen_t;
```

Defaults em `tos_config.c:33-34` (`auto_lock_seconds = 300`, `auto_dim = true`), carregados em
`:139-141`, salvos em `:198-199`. **Nenhum consumidor no projeto lê `auto_lock_seconds` ou
`auto_dim`.** Verificado por grep em todo `components/` e `main/`.

### 10b. A tela de Display é 100% mock

`components/Applications/ui/screens/settings/display_settings_ui.c`:

- `cycle_selector()` (`:58-68`) só troca o texto do label. Nada é aplicado.
- `s_timeout_idx` volta para 1 a cada abertura da tela (`:144`), então o valor escolhido é descartado.
- A linha de Brightness (`:153`) usa `menu_component_add_intensity`, e **nenhum caminho chama
  `lcd_set_brightness()`**.
- Auto-dim e Invert são toggles decorativos. A prova está no próprio código, `:96`:
  `ESP_LOGI(TAG, "mock toggle row %d -> %d", ...)`.
- O `notify(NOTIFY_SAVED, "Display settings saved")` em `:125` mente: nada é salvo.

### 10c. O que já funciona

O PWM de backlight é real: `st7789.c:69-88` configura LEDC, `lcd_set_brightness()` (`:157-170`)
aplica duty e persiste em `FLASH_CONFIG_SCREEN`, `lcd_get_brightness()` (`:172`) lê de volta, e
`st7789_init()` (`:253-256`) restaura no boot.

Ou seja: **falta só ligar os fios**, mais três coisas ausentes:

- rampa de fade em vez de degrau na mudança de brilho;
- nível de "dim" separado do nível escolhido pelo usuário, para poder restaurar exatamente;
- comando de sleep do painel (`sleep-in` / `display_off` do ST7789) para cortar o painel de verdade e
  não só o backlight.

## 28. Timers do header forçam redraw a 2 Hz para sempre

**Severidade: Alto. É o bloqueador real de light sleep.**

O header cria três `lv_timer` que **nunca morrem** e rodam independentemente de qualquer mudança de
estado. `components/Applications/ui/components/header/header_ui.c:31-33`:

```c
#define STATUS_TINT_POLL_MS 500
#define WIFI_STATUS_POLL_MS 500
#define WIFI_ANIM_MS        800
```

### 28a. `status_tint_timer` invalida objetos a cada 500 ms

`:55-59`:

```c
static void status_tint_timer_cb(lv_timer_t *timer) {
  (void)timer;
  apply_active_tint(card_img_ref, sd_is_mounted());
  apply_active_tint(bt_img_ref, s_ble_active);
}
```

`apply_active_tint` (`:40-49`) escreve `lv_obj_set_style_image_recolor` e
`lv_obj_set_style_image_recolor_opa` **sem comparar com o valor anterior**.

Isso não é inofensivo. Verificado na fonte do LVGL 9 em
`managed_components/lvgl__lvgl/src/core/lv_obj_style.c:242-250`:

```c
void lv_obj_refresh_style(lv_obj_t * obj, lv_part_t part, lv_style_prop_t prop)
{
    if(!style_refr) return;
    lv_obj_invalidate(obj);      // linha 250, incondicional, sem diff de valor
```

Ou seja: **duas invalidações forçadas por segundo, para sempre**, mesmo com o aparelho parado na home
sem ninguém tocando. A task do LVGL acorda, calcula áreas sujas, e emite transação SPI.

Além disso, é diferente dos outros dois: `status_tint_timer` **não tem checagem de validade que o
delete**. Os outros dois se autodestroem quando o objeto morre. Esse é criado uma vez
(`:229-231`, guardado por `if (status_tint_timer == NULL)`) e vive até o reboot.

### 28b. `wifi_anim_timer` anima o ícone para sempre

`:110-132` faz ping-pong de frame 0 -> 3 -> 0 a cada 800 ms via `lv_image_set_src`, **sem nenhuma
condição sobre o estado real do WiFi**. O ícone de WiFi está permanentemente animando, conectado ou
não (ver item 29).

### 28c. `header_wifi_status_timer_cb` é 2 Hz de nada

`:95-108`:

```c
bool current_active = wifi_service_is_active();
bool current_connected = wifi_service_is_connected();

if (current_active != header_wifi_enabled || current_connected != header_wifi_connected) {
  header_wifi_enabled = current_active;
  header_wifi_connected = current_connected;
}
```

O callback lê o estado, atribui a duas variáveis estáticas e **nada mais**. Nenhum objeto é atualizado
com base nelas. É 2 Hz de trabalho puro descartado, para sempre.

Nota atenuante: as duas chamadas são baratas hoje porque `wifi_service_*` faz
`spi_bridge_send_command`, que retorna imediatamente em `spi_bridge.c:225-227` quando
`!s_bridge_alive`, sem logar. Quando o C5 voltar a funcionar, isso passa a ser **duas transações SPI
por segundo no barramento SPI2, para sempre, para atualizar variáveis que ninguém lê.**

### Impacto conjunto

Enquanto esses três timers existirem, ligar `CONFIG_PM_ENABLE` e tickless idle **não vai produzir
economia nenhuma**: a CPU não chega a idle por tempo suficiente, e a task do LVGL nunca fica quieta.
Esse item é pré-requisito técnico do item 9b, não um detalhe de polimento.

**Correção**:
- `status_tint_timer`: trocar polling por notificação. `sd_is_mounted()` muda em evento de montagem
  (item 20), `s_ble_active` já tem setter (`header_ui_set_ble_active`, `:51-53`). No mínimo, comparar
  com o último valor e só escrever o estilo quando mudar.
- `wifi_anim_timer`: criar somente durante conexão em andamento, destruir ao concluir.
- `header_wifi_status_timer_cb`: ou fazer atualizar o ícone de fato, ou deletar.
- Regra geral para o projeto: **nenhum `lv_timer` periódico deve existir sem uma condição de parada.**

## 21. Sem guarda térmica

**Severidade: Médio.**

Nenhum uso de `temperature_sensor` no projeto. Grep vazio.

Um P4 a 360 MHz fixos, mais backlight, mais rádio transmitindo, dentro de uma caixa fechada de mão,
sem throttle e sem limite. Não há leitura, não há log, não há redução de clock, não há corte de TX.

Também não há proteção térmica de carga: o BQ25896 tem registro de status térmico e o `power_ui.c:183`
até lê `bq25896_reg_raw(0x10)`, mas o valor é mockado e nada age sobre ele.

**Correção**: ler o sensor interno periodicamente no `sys_monitor`. Thresholds ligados ao
`power_manager` do item 9: reduzir frequência de CPU, reduzir brilho, bloquear TX, e em último caso
shutdown. Desmockar o status térmico do charger para limitar corrente de carga quando quente.

## 41. Estado de USB não alimenta o gerenciamento de energia

**Severidade: Médio. Entrada obrigatória do `power_manager` do item 9.**

Conectar e desconectar o cabo USB é um dos eventos de energia mais importantes de um handheld: muda a
fonte de alimentação, muda o estado de carga, e o host pode pedir que o dispositivo entre em suspend.
Hoje **nenhum desses eventos chega a lugar nenhum de decisão de energia**, e não é por falta de sinal:
existem dois sinais independentes de presença de USB no sistema, e os dois são ignorados para fins de
energia.

### 41a. O charger já sabe, e ninguém escuta para energia

`bq25896_get_vbus_status()` (`bq25896.c:130-136`) retorna o estado real do VBUS:
`USB_HOST`, `ADAPTER_PORT`, `OTG` ou `UNKNOWN`. Esse dado é lido, mas só para **exibição** na tela de
power (`power_ui.c:62-86`, que formata o nome do VBUS). Não há nenhum consumidor que use "VBUS presente"
como decisão: nada segura o aparelho acordado enquanto plugado, nada muda a política de brilho ou de
sono quando a alimentação externa entra ou sai.

### 41b. TinyUSB não reporta mount, suspend nem resume

Grep por `tud_mount`, `tud_umount`, `tud_suspend`, `tud_resume` em todo o P4: **nenhum tratado.** O
único uso de estado de USB é `tud_mounted()` no Bad USB (`bad_usb.c:82`, espera de enumeração) e
`tud_cdc_n_connected()` no host link (`host_link_cdc.c:76`, guarda de escrita). O ciclo de vida do
barramento USB em si (enumerou, host suspendeu, host acordou) não é observado.

Isso tem duas consequências de energia:

- **Suspend do host ignorado**: pela spec USB, quando o host suspende, o dispositivo bus-powered deve
  cair para consumo mínimo. Sem tratar `tud_suspend`/`tud_resume`, o aparelho continua a plena carga
  com o host dormindo.
- **Conexão de USB não segura o sono**: quando o `power_manager` do item 9 existir e começar a fazer
  light sleep por inatividade, ele pode adormecer o aparelho **no meio de uma sessão de host link ou de
  um OTA** se ninguém disser "há USB ativo, não durma". O evento de mount do USB é justamente o sinal
  que deveria adquirir um `esp_pm_lock` do tipo `NO_LIGHT_SLEEP`.

### 41c. O descritor declara bus-powered com bateria embarcada

`tusb_desc.c:232-233`: `.self_powered = false` e `.vbus_monitor_io = -1`. O dispositivo se declara
alimentado pelo barramento e não monitora VBUS pela stack USB. Num aparelho a bateria com charger
próprio, o correto costuma ser `self_powered = true` com monitor de VBUS, para o comportamento de
suspend e o relato de consumo ao host ficarem corretos. Não é urgente, mas é incoerente com o hardware
e afeta como o host trata o dispositivo em suspend.

**Correção**: o `power_manager` do item 9 precisa de uma entrada de estado de USB, alimentada por duas
fontes:

- o VBUS do BQ25896 (41a) como verdade de "há alimentação externa", já disponível, só falta rotear;
- callbacks `tud_mount`/`tud_umount`/`tud_suspend`/`tud_resume` (41b) para o ciclo do barramento.

Com isso: USB conectado adquire o lock `NO_LIGHT_SLEEP` e libera ao desconectar; suspend do host baixa o
consumo; e a política de brilho/sono pode diferir entre "na bateria" e "plugado". Reavaliar
`self_powered` (41c) junto. Este item não tem entregável próprio antes do item 9 existir: ele define
**o que** o power manager precisa consumir de USB, e deve ser implementado junto com ele.

---

# Entrada

## 11. Entrada sem camada de eventos, sem rastreio de atividade

**Severidade: Alto. Bloqueia todo o item 9.**

`components/Drivers/buttons_gpio/buttons_gpio.c` tem 128 linhas e lê GPIO cru dentro de cada função de
leitura (`:40`, `:44`, `:55`):

```c
return gpio_get_level(gpio) == BUTTON_PRESSED_LEVEL;
```

Não há: debounce temporal, long-press, auto-repeat, fila de eventos, ISR, nem fonte de wake.
`buttons_init()` (`:112-122`) só faz `gpio_config`.

Cada tela reimplementa detecção de borda com statics próprios. Exemplo em
`display_settings_ui.c:50-56`:

```c
static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_right_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;
```

e a comparação manual em `:86-134`. Esse padrão está replicado em cerca de 30 telas, cada uma com seu
próprio `lv_timer` de polling.

Consequências operacionais:

- **Não existe um único ponto que saiba "o usuário interagiu".** Um timer de inatividade não tem onde
  se plugar sem editar as 30 telas. É exatamente por isso que o item 10 nunca foi implementado.
- Não existe long-press, então não há "segurar power para desligar" nem combinação de boot para modo
  seguro (item 23).
- Sem debounce por tempo, o comportamento depende do período do `lv_timer` de cada tela, que varia.
- Sem fonte de wake configurada, nem light sleep nem deep sleep podem ser acordados por botão.
- Os ~30 timers de polling são mais uma fonte de "nunca fica idle", junto com o item 28.

**Correção**: um `input_manager` em `Core`, com:

- amostragem única (timer periódico ou ISR + debounce por `esp_timer`);
- fila de eventos `{botão, PRESS | RELEASE | LONG_PRESS | REPEAT, timestamp}`;
- `input_last_activity_ms()` consultado pelo `power_manager`;
- registro do GPIO como wake source;
- API de consumo que as telas usam em vez de pollar GPIO.

A migração das 30 telas é o trabalho mais volumoso da lista, mas é pré-requisito de tudo em energia.
É também a oportunidade de eliminar 30 `lv_timer` periódicos.

## 25. `input_lock_until` estoura em 49,7 dias

**Severidade: Baixo.**

`ui_manager.c:167-169` e `:307`:

```c
bool ui_input_is_locked(void) {
  return (lv_tick_get() < input_lock_until);
}
...
input_lock_until = lv_tick_get() + INPUT_LOCK_MS;
```

`lv_tick_get()` retorna `uint32_t` em ms e dá a volta em ~49,7 dias de uptime. Na volta, a comparação
direta `<` inverte e o lock de entrada pode ficar preso ou ser ignorado.

Improvável hoje, mas um aparelho que ganhe modos de sono (item 9) pode facilmente acumular esse
uptime.

**Correção**: `(int32_t)(lv_tick_get() - input_lock_until) < 0`, padrão de comparação tick-safe.

---

# Escalonamento e contenção

## 12. Anarquia de prioridades, 18 tasks acima do renderizador

**Severidade: Médio-Alto. Causa mais provável dos travamentos que não são crash.**

A task do LVGL está em **prioridade 4** (`lvgl_glue.c:19`) com
`task_affinity = LVGL_PORT_TASK_CORE_ANY` (`:21`, valor `-1`).

Inventário de prioridades declaradas (constantes de prioridade de *task*, ignorando os `MESH_PRIO_*`
que são prioridade de *mensagem* de mesh, não de task):

| Prio | Tasks |
|---|---|
| 10 | `SX1262_IRQ_TASK_PRIO` |
| 6 | `C5_PASSTHROUGH_TASK_PRIO`, `nfc_emu` (hardcoded, `nfc_listener.c:46`) |
| 5 | `TX_TASK_PRIORITY`, `RX_TASK_PRIORITY`, `SPI_STREAM_TASK_PRIO`, `SCANNER_TASK_PRIORITY`, `RFID_TASK_PRIORITY`, `HOST_LINK_CDC_TASK_PRIO`, `HEARTBEAT_PRIO`, `HAPTIC_TASK_PRIORITY`, `CONSOLE_TASK_PRIO`, `C5_FLASH_TASK_PRIO`, `C5_ROM_FLASH_TASK_PRIO`, `BRIDGE_TASK_PRIO`, `BRIDGE_NOTIFY_TASK_PRIO`, `BLE_TASK_PRIO`, `nfc_mgr` (hardcoded, `nfc_manager.c:114`) |
| 4 | **LVGL**, `WIFI_TASK_PRIORITY`, `STREAM_WD_TASK_PRIO`, `SS_TASK_PRIO`, `SPK_TASK_PRIORITY`, `SPECTRUM_TASK_PRIORITY`, `SND_TASK_PRIORITY`, `NFC_SND_TASK_PRIORITY`, `MT_POLL_TASK_PRIO`, `MC_POLL_TASK_PRIO`, `MIC_TASK_PRIORITY`, `HOST_LOG_TASK_PRIO`, `FB_TASK_PRIORITY`, `BLE_SCAN_TASK_PRIO`, `c5_link_mon` (hardcoded 4), 3x `TASK_PRIORITY` genérico em telas de wifi |
| 3 | `mesh_tx` (`tskIDLE_PRIORITY + 3`) |
| 1 | `MONITOR_PRIORITY` (sys_monitor) |

**18 tasks estão acima da prioridade do renderizador.** Com afinidade livre, o escalonador pode colocar
a task de render no mesmo core de um scan de NFC (prio 6) ou de um stream SPI (prio 5). Resultado: a
UI engasga durante qualquer atividade de rádio ou USB. Esse é o perfil da sensação de travamento sem
crash, e agrava o item 27.

Há ainda **44 sítios de `xTaskCreate`** no projeto, cada um escolhendo prioridade e stack por conta.
Não existe política central, e três telas de wifi usam um `TASK_PRIORITY` genérico com o mesmo nome em
arquivos diferentes.

**Correção**: um `include/sys_prio.h` no `Core` com a política única, e aplicá-la nos 44 sítios:

| Faixa | Uso |
|---|---|
| 9-10 | ISR deferido / tempo real duro (IRQ de rádio) |
| 6-7 | Render (LVGL), fixado no core 1 |
| 4-5 | Serviços (host_link, bridge, storage) no core 0 |
| 2-3 | Background (poll de mesh, telemetria) |
| 1 | Monitor |

`CONFIG_FREERTOS_UNICORE is not set`, então há dois cores disponíveis. Fixar a UI num core e os rádios
no outro resolve a maior parte do engasgo sem mexer em prioridade.

## 27. Barramento SPI3 compartilhado entre display e três rádios

**Severidade: Alto. Contenção estrutural, não ajustável por prioridade.**

Mapa real dos barramentos:

| Host | Dispositivos |
|---|---|
| **SPI3_HOST** | ST7789 display (`st7789.c:239`), CC1101 SubGhz (`cc1101.c:598`), QMI8658A IMU (`qmi8658a.c:99`), SX1262 LoRa (`sx1262_hal.c:40`) |
| SPI2_HOST | Bridge C5 (`spi_bridge_phy.c:54,66`), ST25R3916 NFC (`highboy_nfc.h:56`) |

O display divide barramento com **três rádios e o IMU**. Números do display
(`st7789.h:34,41-42` e `lvgl_glue.c:20`):

- 240x320 RGB565 = 153.600 bytes por quadro cheio;
- `LCD_PIXEL_CLOCK_HZ = 20 MHz`;
- buffers de 20 linhas, ou seja 16 faixas de 9.600 bytes por quadro;
- **~61 ms de ocupação de barramento por quadro cheio**, ignorando overhead.

Ou seja: o display já consome bastante do SPI3 sozinho. Qualquer transação de rádio se intercala
entre as 16 faixas de um flush. O driver SPI do IDF serializa por barramento, então não há corrupção,
mas há **latência inserida no meio do desenho**, que aparece como tearing ou congelamento parcial
durante uso de rádio.

Isso não se resolve com prioridade de task (item 12): o barramento é um recurso serializado. Prioridade
alta num rádio só faz ele ganhar o barramento mais rápido, atrasando o display mais.

Piora com o item 34 (`spi_device_acquire_bus` no IMU) e com o item 3b (escritas em flash mascarando
interrupções, o que atrasa a conclusão de DMA).

**Correção**, em ordem de custo:

1. Curto prazo: reduzir a superfície redesenhada. O item 28 elimina os redraws desnecessários, o item
   14 reduz objetos vivos. Menos flush = menos contenção.
2. Médio prazo: subir o `LCD_PIXEL_CLOCK_HZ` se o painel e o layout aceitarem (ST7789 costuma tolerar
   40-80 MHz), o que reduz proporcionalmente a ocupação por quadro. Testar com cuidado: a placa V1.x
   já usa clock de flash conservador por causa de hardware marginal
   (`sdkconfig.defaults`: `CONFIG_ESPTOOLPY_FLASHFREQ_20M`), então o mesmo cuidado vale aqui.
3. Longo prazo, quando a PSRAM entrar: buffer de quadro cheio permite um único flush por quadro em vez
   de 16, reduzindo drasticamente o número de janelas em que um rádio pode se intercalar.
4. Se o revisionamento de placa permitir: mover o IMU para I2C ou para o SPI2, tirando um contendor do
   barramento do display.

Documentar o mapa de barramento explicitamente em `pin_def.h` para que ninguém adicione um quinto
dispositivo no SPI3 sem perceber o custo.

## 34. `qmi8658a_configure` sequestra o barramento do display

**Severidade: Médio. Hoje latente, será grave quando o IMU for ligado.**

`components/Drivers/qmi8658a/qmi8658a.c:136-155`:

```c
bool acq =
    (spi_device_acquire_bus(s_spi, pdMS_TO_TICKS(QMI8658A_BUS_ACQUIRE_TIMEOUT_MS)) == ESP_OK);
if (!acq) {
  ESP_LOGW(TAG, "configure: acquire_bus timeout — proceeding unlocked");
}
ret = write_verify(QMI8658A_REG_CTRL1, QMI8658A_CTRL1_VALUE);
if (ret == ESP_OK) ret = write_verify(QMI8658A_REG_CTRL2, QMI8658A_CTRL2_VALUE);
if (ret == ESP_OK) ret = write_verify(QMI8658A_REG_CTRL3, QMI8658A_CTRL3_VALUE);
if (ret == ESP_OK) ret = write_verify(QMI8658A_REG_CTRL7, QMI8658A_CTRL7_VALUE);
if (acq) spi_device_release_bus(s_spi);
```

`spi_device_acquire_bus()` no SPI3 bloqueia **todos** os outros dispositivos daquele barramento,
incluindo o display, pela duração da posse. E `QMI8658A_BUS_ACQUIRE_TIMEOUT_MS = 5000` (`:53`).

Dentro da janela de posse, cada `write_verify` faz um `ESP_LOGI` com 4 argumentos (`:126-133`). Com o
`tos_log` fazendo flush em flash por linha (item 3b), isso significa: **barramento do display retido
exclusivamente enquanto quatro escritas em flash acontecem**, cada uma desabilitando a cache. O display
congela pelo tempo somado.

Atenuante atual: `qmi8658a_init` e `qmi8658a_configure` têm **zero callers** no projeto (item 33), ou
seja o IMU nunca é inicializado. O código está latente.

**Correção**: remover os `ESP_LOGI` de dentro da janela de posse (acumular e logar depois), reduzir o
timeout para algo compatível com um frame de display (50 ms), e reavaliar se `acquire_bus` é necessário
para 4 escritas de registro. Se não for, tirar. O comentário "proceeding unlocked" em `:141` sugere que
não é obrigatório.

## 35. I2C no driver legado, sem recuperação de bus travado

**Severidade: Médio.**

`components/Drivers/i2c_init/i2c_init.c:27-50`:

```c
void init_i2c(void) {
  i2c_config_t conf = { ... .master.clk_speed = I2C_MASTER_FREQ_HZ };  // 400 kHz
  esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
  if (ret != ESP_OK) { ESP_LOGE(...); return; }
  ret = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
  if (ret != ESP_OK) { ESP_LOGE(...); return; }
}
```

Quatro problemas:

1. **Driver legado**: usa `driver/i2c.h` (`i2c_param_config` + `i2c_driver_install`), depreciado no
   IDF 5.x e removido no 6.x. Dívida de migração para `driver/i2c_master.h`.
2. **`void` de retorno**: falha de configuração ou de instalação só imprime e retorna. O `kernel.c:73`
   não tem como saber (item 8). Todo consumidor I2C (BQ25896, DRV2605L) descobre sozinho, em runtime,
   um dispositivo por vez.
3. **Sem recuperação de bus travado**: se um escravo I2C segurar SDA em nível baixo (clássico em
   handheld com reset parcial ou glitch de alimentação), o barramento fica morto até power cycle. Não
   há rotina de recovery (pulsar SCL 9 vezes com SDA liberado + STOP). O BQ25896 fica ilegível, então
   o sistema perde a leitura de bateria sem nenhum diagnóstico.
4. **400 kHz sem margem declarada**: aceitável, mas com traços longos até charger e haptic num
   handheld, vale medir. Não há configuração de timeout de barramento.

**Correção**: migrar para `driver/i2c_master.h`, `init_i2c` retorna `esp_err_t`, adicionar rotina de
recuperação de bus chamada quando N leituras consecutivas falharem, e expor a saúde do barramento
no `sys_monitor`.

---

# Arquitetura e ciclo de vida

## 13. Sem contrato de ciclo de vida de aplicação

**Severidade: Médio-Alto. Recursos ficam ligados depois de sair do app.**

`ui_manager.c:171-173` define `ui_open_fn_t` e `screen_open_fn()`: um mapa de tela para função de
**abrir**. Não existe função de fechar, suspender ou retomar. Grep por `screen_close_fn`, `on_exit`:
nada.

Consequência: cada tela é responsável por limpar tudo sozinha, e o framework não garante nada. Os
`stop` já escritos ficam órfãos:

- `subghz_receiver_stop()` (`subghz_receiver.h:53`, implementado em `subghz_receiver.c:348`):
  **zero callers**. `subghz_read_ui.c:531` e `:566` fazem `ui_switch_screen()` no back sem parar nada.
- `nfc_manager_stop()` (`nfc_manager.c:118`): **zero callers a partir da UI**.
- `speaker_ui.c:542` faz `ui_switch_screen(SCREEN_SETTINGS)` no back sem sinalizar parada para a task
  `spk_play` (prio 4) que ela criou em `:472`.

Existem **16 funções `*_deinit` públicas** declaradas em `Drivers` e `Service` e nenhum caminho central
que as chame.

Hoje o dano é limitado porque várias telas de rádio ainda são mock (o `subghz_read_ui` não chama o
receiver de fato). Mas a lacuna é estrutural: **quando os rádios forem ligados de verdade, nada no
framework garante que eles desliguem ao sair da tela.** Rádio em RX esquecido é a forma mais rápida de
matar a bateria e a mais difícil de diagnosticar, porque não há sintoma visível. E no SPI3, um rádio em
polling contínuo também come a banda do display (item 27).

**Correção**: estender o mapa de telas para `{open, close, suspend, resume}` e fazer `ui_switch_screen()`
chamar `close` da tela que sai antes do `open` da que entra. Toda tela que liga hardware ou cria task
registra o `close` correspondente. Casar com os `esp_pm_lock` do item 9 (o `close` libera o lock) e com
a liberação de tela do item 14.

## 33. Três drivers órfãos, sem dono de inicialização

**Severidade: Médio.**

Grep por callers de `_init` fora do próprio componente:

| Driver | Callers de init | Situação |
|---|---|---|
| `drv2605l` (haptic) | **nenhum** | nunca inicializado |
| `qmi8658a` (IMU) | **nenhum** | nunca inicializado (ver item 34) |
| `audio_i2s` | **nenhum** | nunca inicializado |
| `sx1262` (LoRa) | `meshtastic_app.c:89`, `rnode.c:173` | init preguiçoso a partir de apps |

Os três primeiros são código morto compilado: ocupam flash, aparecem no `CMakeLists`, e um leitor do
`kernel.c` conclui erradamente que o hardware está pronto. Note que existe uma tela de haptic
(`haptic_ui.c`, que cria task em `:241`) e uma de áudio (`speaker_ui.c`, `micrec_ui.c`, `spectrum_ui.c`)
que presumivelmente dependem desses drivers.

O quarto é pior arquiteturalmente: o `sx1262` é inicializado por **duas** aplicações diferentes
(Meshtastic e RNode), nenhuma das duas dona exclusiva. Sem o contrato do item 13, se o usuário
navegar de Meshtastic para RNode, ninguém garante quem faz `sx1262_deinit()` e quem reconfigura o
rádio. O `sx1262` também cria uma task de IRQ em prioridade 10 (`sx1262.c:372`), a mais alta do
sistema (item 12).

**Correção**: um registro central de dispositivos no `kernel_init` que declare explicitamente o estado
de cada periférico (`presente / inicializado / ausente / falhou`), com init preguiçoso mediado por esse
registro em vez de chamado direto pela aplicação. Isso resolve também a questão de "quem faz deinit"
do item 13 e alimenta o mapa de subsistemas do item 8.

Curto prazo, sem refactor: ou ligar os três drivers no `kernel_init`, ou removê-los do build até que
as telas correspondentes os usem. Deixar como está mascara o estado real do produto.

---

# Memória

## 14. Telas nunca são liberadas ao navegar

**Severidade: Médio. Ocupação permanente de heap.**

`ui_manager.c:300-315`:

```c
void ui_switch_screen(screen_id_t new_screen) {
  ...
  if (ui_acquire()) {
    clear_current_screen();
    open_fn();
    current_screen_id = new_screen;
    ui_release();
  }
}
```

e `:161-165`:

```c
static void clear_current_screen(void) {
  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
  }
}
```

`clear_current_screen()` apenas limpa o grupo de entrada. **Não deleta o objeto da tela que sai.** E
`ui_screen_load()` (`:325-327`) é `lv_screen_load()` puro, sem a variante com auto-delete.

As telas se protegem individualmente na reabertura, como em `display_settings_ui.c:138-141`:

```c
if (s_screen != NULL) {
  lv_obj_del(s_screen);
  s_screen = NULL;
}
```

Então o crescimento é **limitado ao número de telas distintas visitadas**, não infinito. Mas o efeito é
que toda tela que o usuário visitou fica residente em heap para sempre, com seus objetos, labels e
descritores de imagem. Com cerca de 30 telas, isso é uma ocupação permanente significativa.

Efeito secundário: os `lv_timer` das telas se autodestroem checando `lv_screen_active() != s_screen` no
começo do callback (padrão em `display_settings_ui.c:71-75`). Isso significa que o timer da tela que
saiu ainda dispara pelo menos uma vez depois da troca.

**Correção**: liberar a tela que sai no `close` do item 13, ou usar
`lv_screen_load_anim(..., auto_del = true)`. Se decidir manter telas em cache por performance, que
seja explícito: cache LRU com limite e liberação sob pressão de memória (item 15).

## 15. Sem política de heap e fragmentação

**Severidade: Médio.**

- `sys_monitor` **só loga** heap, e apenas em modo verbose (`sys_monitor.c:88-98`). Nunca age. Sem
  threshold, sem aviso, sem liberação de cache. E é iniciado com verbose desligado
  (`kernel.c:110`: `sys_monitor_start(false)`), então na prática nem loga.
- Nenhum `heap_caps_register_failed_alloc_callback`. Falha de alocação só cai no
  `vApplicationMallocFailedHook` (`kernel.c:144-146`), que imprime uma linha e retorna, sem contexto de
  quem pediu nem de quanto.
- O número que importa para UI em LVGL não é o total livre, é o **maior bloco livre contíguo**. Abrir e
  fechar telas aloca e libera objetos de tamanhos variados. O único lugar do projeto que consulta
  `heap_caps_get_largest_free_block()` é `micrec_ui.c:95`.

O sintoma esperado é "depois de uns 20 minutos navegando, uma tela não abre ou o aparelho reinicia".
A PSRAM do protótipo alivia a pressão, mas **não remove a necessidade da política**: fragmentação em
pool grande só demora mais para doer, e os itens 14 e 32 garantem que a ocupação só cresce.

**Correção**: `sys_monitor` passa a acompanhar total livre, mínimo histórico
(`esp_get_minimum_free_heap_size`) e maior bloco contíguo, por caps. Thresholds com ação: aviso na UI,
liberação de cache de assets (item 32), e como último recurso `esp_restart()` controlado (item 31) com
o motivo gravado. Registrar o callback de falha de alocação.

## 32. `assets_manager` sem limite nem eviction

**Severidade: Médio.**

`components/Applications/ui/assets_manager.c` (250 linhas) tem um design razoável: decoder LVGL
customizado que lê `.bin` da LittleFS, integrado ao cache de imagens do LVGL
(`lv_image_decoder_add_to_cache`, `:133`), com `CONFIG_LV_CACHE_DEF_SIZE=262144`.

Os problemas são de contorno:

1. **Lista de nós sem limite**: `s_assets_head` (`:45`) é lista simplesmente ligada, e `assets_get()`
   (`:196`) adiciona um nó por caminho único. Nunca há eviction.
2. **`assets_manager_free_all()` (`:233`) nunca é chamada.** Mesmo padrão dos itens 17 e 13: caminho de
   liberação escrito e não ligado. Não existe forma de liberar assets sob pressão de memória.
3. **Busca linear**: `find_node_by_path()` (`:48`) e `find_node_by_dsc()` (`:56`) são O(n), chamadas
   para cada imagem em cada construção de tela. Com o header sozinho pedindo 4 ícones de wifi + 4 de
   bateria + bluetooth + card + power (`header_ui.c:203-238`), isso é dezenas de varreduras por
   navegação.
4. **`fopen` no caminho de decode** (`:104`): quando o cache do LVGL evicta uma imagem, o próximo
   desenho relê da LittleFS. Combinado com o redraw forçado do item 28 e as faixas de 20 linhas do
   item 27, há risco de thrash de cache que vira leitura de flash durante o desenho.

**Correção**: dar limite à lista com política LRU, ligar `assets_manager_free_all()` (ou uma versão
parcial) na política de memória do item 15, e trocar a busca linear por hash do caminho. O ponto 4
melhora automaticamente com a PSRAM (cache maior) e com o item 28 (menos redraw).

---

# Persistência e tempo

## 16. Sem fonte de tempo, tudo datado em 1970

**Severidade: Médio.**

Nenhum `settimeofday`, nenhum SNTP, nenhum RTC inicializado no boot. O único consumidor de tempo de
parede é `components/Service/storage_api/tos_loot.c:30-36`:

```c
time_t now = time(NULL);
localtime_r(&now, &tm);
strftime(buf, size, "%Y-%m-%d", &tm);
```

Sem sincronização, `time(NULL)` retorna o epoch. **Todo arquivo salvo, toda linha de log e toda entrada
de loot ficam datados em 1970-01-01.**

O header confirma o sintoma de forma literal: `header_ui.c` cria o label de hora com
`lv_label_set_text(lbl_time, "12:00")` e nunca atualiza (item 29).

Consequências operacionais:

- impossível ordenar eventos entre boots;
- nomes de arquivo baseados em data colidem entre sessões;
- a rotação de log (item 3) não tem noção de idade, só de tamanho;
- diagnóstico remoto de um relato de usuário fica sem linha do tempo.

**Correção**: receber a hora do host no pairing do `host_link` e persistir em NVS, mais um contador
monotônico de boot como fallback quando nunca houve sync. Se a placa tiver cristal de RTC, usar.

## 19. Escrita de config não atômica

**Severidade: Médio. Queda de energia corrompe configuração.**

Padrão em `st7789.c:147` e `tos_config.c:107`:

```c
FILE *f = fopen(path, "w");
```

`"w"` trunca o arquivo imediatamente. Se houver reset, brownout ou remoção de energia entre o
truncamento e o flush, o arquivo fica vazio ou pela metade. Na próxima leitura o parse de JSON falha e
a configuração volta ao default sem aviso.

Isso é agravado por dois lados: tudo o que escreve config está no caminho de UI (mudar brilho, tema ou
rotação grava na hora), e não há shutdown gracioso (item 31), então um reboot pelo menu pode pegar uma
escrita no meio.

Contexto de partição (`partitions.csv`): a config vive na LittleFS de 1920K em `0x620000`, a mesma
partição `assets` que o OTA de assets substitui. O `tos_config.c` usa defaults por campo
(`json_get_int(root, "campo", default)`), o que faz config parcial degradar em vez de quebrar, mas não
há campo de versão nem migração explícita de schema.

**Correção**: escrever em `<path>.tmp`, `fflush` + `fsync`, `rename()` para o destino final. Rename em
LittleFS é atômico. Adicionar `schema_version` nos arquivos de config e uma função de migração em
`tos_config_load_all`.

## 31. Nenhum desligamento gracioso, 5 `esp_restart()` sem flush

**Severidade: Alto. Perde log e corrompe config.**

Grep por `esp_register_shutdown_handler`: **nenhuma ocorrência no projeto.** O IDF oferece esse hook
exatamente para isso, e ele roda em `esp_restart()`.

Os cinco pontos que reiniciam o aparelho:

| Local | Contexto |
|---|---|
| `settings_ui.c:405` | reboot voluntário pelo menu |
| `cmd_system.c:115` | comando de console |
| `ota_service.c:228` | após concluir OTA |
| `rnode_commands.c:331` | reset pedido pelo host |
| `c5_passthrough.c:103` | saída do modo passthrough |

Nenhum deles faz:

- `tos_log_deinit()` ou flush final do log (declarado em `tos_log.h:49`, existe, não é chamado aqui);
- `fsync` ou fechamento de arquivos de config em aberto;
- `storage_deinit()` / desmontagem da LittleFS ou do SD;
- parada dos rádios ou dos streams de `host_link`.

O único cuidado presente é um `vTaskDelay(REBOOT_DELAY_MS)` em `settings_ui.c:404`, que é uma
espera cega e não uma sincronização.

Consequências: **a cauda do log é perdida exatamente no reboot que o usuário provocou** (justo quando
você mais quer o log), e uma escrita de config em andamento (item 19) pode ser truncada.

Isso também bloqueia a correção do item 6: hoje não existe um `esp_restart()` "controlado" para o
`sys_monitor` escalar para, porque nenhum restart é controlado.

**Correção**: registrar um handler via `esp_register_shutdown_handler()` no `kernel_init` que faça, em
ordem: parar streams do `host_link`, desligar rádios (via o registro do item 33), flush e fechamento do
log, `fsync` + desmontagem do storage. Criar uma função `sys_reboot(reason)` que grave o motivo (item
4) e chame `esp_restart()`, e trocar os cinco callsites por ela. O mesmo handler serve para o shutdown
por bateria baixa (item 9e) e para o ship mode (item 9c).

---

# Observabilidade

## 18. Contador de boots no lugar errado

**Severidade: Médio.**

Existe um contador em NVS, mas está numa tela de easter egg e não conta boots.
`components/Applications/ui/screens/octobit/octobit_status_ui.c:104-122`:

```c
static void read_boots_once(void) {
  if (s_boots_read) return;
  s_boots_read = true;
  nvs_handle_t h;
  if (nvs_open("octobit", NVS_READWRITE, &h) == ESP_OK) {
    uint32_t v = 0;
    nvs_get_u32(h, "boots", &v);
    v++;
    nvs_set_u32(h, "boots", v);
    ...
```

Ele incrementa na **primeira abertura da tela Octobit na sessão**, não no boot. O número exibido como
"boots" é na verdade "quantas vezes essa tela foi aberta".

Além de o dado estar errado, o bloco de construção está no lugar errado: contador de boots é
infraestrutura do item 4 e deveria viver num namespace NVS de sistema, incrementado no `app_main`,
junto com a razão do último reset e o contador de boots anormais consecutivos.

**Correção**: mover para um `sys_stats` em `Core`, incrementar no `app_main`, e a tela Octobit passa a
ler de lá.

## 22. Coredump desativado, zero forense de campo

**Severidade: Médio.**

`sdkconfig`: `CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y`. E `partitions.csv` não tem partição de coredump.

Resultado: um crash em campo não deixa **nenhum** dado. Backtrace, registradores, stacks das tasks,
tudo perdido. Um relato de usuário do tipo "reiniciou sozinho" é hoje impossível de investigar.

**Correção**: adicionar partição `coredump` (64K basta) e ligar `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`.
Ler no boot: se houver coredump, marcar, expor na tela de about e permitir dump via `host_link` para o
companion app.

**Nota de layout, corrigida**: uma versão anterior deste documento sugeriu abrir espaço reduzindo
`assets` de 1920K para 1856K. **Isso não é possível**: o item 39 mostra que `assets.bin` ocupa
exatamente 100% da partição. Não há um único byte livre.

Onde a partição de coredump pode sair, em ordem de preferência:

1. **Reduzir `ota_0` e `ota_1`** de 3M para 2944K cada, liberando 128K. Depende do item 38: com o
   binário a 93,1% do slot, cortar 64K por slot deixa a folga em 4,9%. Apertado, mas viável se o item
   38 for atacado primeiro.
2. **Depois de resolver o item 39**, cortar da `assets`.
3. **Coredump para UART** (`CONFIG_ESP_COREDUMP_ENABLE_TO_UART`), sem partição. Só funciona com o
   aparelho plugado no companion, então não serve para crash em campo. Meia solução.
4. **Coredump para o cartão SD** via handler customizado. Não é suportado nativamente pelo IDF e exige
   escrever no caminho de panic, o que é frágil por definição.

Ordem prática: item 38 primeiro, aí a opção 1 fica confortável.

## 26. Delay de panic em 0 segundo

**Severidade: Baixo. Mas atrapalha o diagnóstico de tudo o resto.**

`sdkconfig`: `CONFIG_ESP_SYSTEM_PANIC_REBOOT_DELAY_SECONDS=0` com
`CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y`.

O texto do panic é impresso e o chip reinicia imediatamente. Quem estiver com o serial aberto vê o
backtrace passar voando junto com o boot novo.

**Correção**: 2 segundos. Custo zero, torna o debug em campo viável.

## 29. Header mente sobre bateria, wifi e hora

**Severidade: Médio. O usuário não tem indicador confiável de nada.**

O header é a única superfície de status permanente do sistema, e os três indicadores que importam
operacionalmente estão quebrados.

### 29a. Bateria: código morto explícito

`header_ui.c:261-264`:

```c
(void)battery_anim_timer_cb;
(void)battery_anim_timer;
(void)battery_frame;
(void)battery_dir;
```

Os quatro casts existem para silenciar avisos de "não usado". O `battery_anim_timer` **nunca é
criado**. O ícone de bateria é setado uma vez em `:246-247`:

```c
battery_img = lv_image_create(bat_cont);
if (battery_dscs[2])
  lv_image_set_src(battery_img, battery_dscs[2]);
```

Sempre o frame **2 de 4**, fixo. **O ícone de bateria do sistema é uma imagem estática que nunca
reflete a carga real.** O `power_img` (`:251-255`) é criado e imediatamente escondido com
`LV_OBJ_FLAG_HIDDEN`, e nada o mostra.

Ou seja: o item 9d (percentual sem filtro) hoje é inobservável, porque o percentual nunca chega à tela.
E o usuário não tem aviso nenhum de bateria baixa (item 9e).

### 29b. WiFi: anima sempre, independente do estado

Como detalhado no item 28b, o `wifi_anim_timer` anima o ícone de WiFi indefinidamente sem consultar
estado. Com o C5 morto, não há WiFi nenhum, e o ícone anima como se estivesse conectando. O
`header_wifi_status_timer_cb` lê o estado real de 500 em 500 ms e joga fora (item 28c).

### 29c. Hora: literal hardcoded

`header_ui.c` cria o label com `lv_label_set_text(lbl_time, "12:00")` e nunca atualiza. É coerente com
o item 16 (não há fonte de tempo), mas exibir "12:00" fixo é pior que não exibir nada, porque parece
funcional.

**Correção**: os três indicadores passam a ser alimentados por eventos. Bateria pelo `power_manager`
do item 9 (com o percentual filtrado). WiFi pelo estado real, com animação só durante transição. Hora
pela fonte do item 16, ou oculta enquanto não houver sync. Enquanto qualquer um deles não tiver dado
real, **esconder é melhor que mostrar valor falso**: um indicador que mente treina o usuário a ignorar
o header inteiro.

## 37. Log em nível INFO amplifica o custo do item 3

**Severidade: Baixo.**

`sdkconfig`: `CONFIG_LOG_DEFAULT_LEVEL_INFO=y`.

Isoladamente é uma escolha razoável para desenvolvimento. Combinado com o item 3b (cada linha de log
vira uma escrita em flash com `fflush`, mascarando interrupções não-IRAM), significa que **o custo do
item 3 é pago continuamente em produção**, não só em situações excepcionais.

Amostras de volume no caminho quente: `st7789_init` loga, `storage_assets_print_info()`
(`kernel.c:78`) loga, `qmi8658a` loga dentro de posse de barramento (item 34), o callback de progresso
do OTA gera mensagem por ponto percentual (`ota_service.c:190-195`).

**Correção**: depois de resolver o item 3 (buffer + flush por lote), o nível INFO deixa de ser um
problema. Antes disso, considerar `CONFIG_LOG_DEFAULT_LEVEL_WARN` para builds de release, com INFO
mantido nos módulos em desenvolvimento via `esp_log_level_set()` por tag.

---

# Storage

## 20. Sem hotplug de SD

**Severidade: Médio.**

`sd_card_init.c:45-50` configura o slot SDMMC com CLK, CMD e D0-D3. **Não há pino de card-detect** em
`components/Drivers/pins/include/pin_def.h:42-45` e nada monitora presença do cartão.

O `sd_is_mounted()` é checado de forma disciplinada em todas as escritas (`sd_card_write.c` tem 10
checagens, `sd_card_info.c` e `sd_card_dir.c` também), então tirar o cartão não causa crash imediato:
as escritas simplesmente falham.

Mas:

- tirar o cartão durante uma escrita corrompe o arquivo em andamento;
- não há remontagem, então inserir de volta não recupera nada;
- o usuário precisa **reiniciar o aparelho** para trocar de cartão;
- o ícone de card no header depende de `sd_is_mounted()` sendo pollado a 2 Hz para sempre (item 28a),
  que é justamente o polling que um evento de montagem eliminaria.

Num handheld com slot exposto isso vai acontecer todos os dias.

**Correção**: se a placa tiver o pino de detect, usar com ISR + debounce para montar e desmontar,
emitindo evento que o header consome (resolve o item 28a de graça). Se não tiver, fazer polling leve de
saúde (`storage_health_check` do item 17) e oferecer montar/desmontar manual na UI, além de avisar
antes de operações longas de escrita.

---

# Drivers

## 24. `gpio_install_isr_service` duplicado, ordem frágil

**Severidade: Baixo.**

Duas chamadas de `gpio_install_isr_service(0)`:

- `components/Drivers/spi_bridge_phy/spi_bridge_phy.c:81`
- `components/Drivers/tusb_desc/tusb_desc.c:203`

A segunda recebe `ESP_ERR_INVALID_STATE`. O comentário em `tusb_desc.c:198` reconhece isso e diz que
`buttons_init` instala antes, o que **não é verdade**: `buttons_gpio.c:112-122` só faz `gpio_config`,
sem ISR.

Na prática, quem instala é o `spi_bridge_phy`, que hoje pode nunca rodar porque o bridge está desativado
(`kernel.c:96`). Então a ordem de quem instala o serviço de ISR de GPIO depende de qual subsistema
calhou de inicializar primeiro.

Isso vai importar de verdade quando o item 11 registrar ISR nos botões e o item 20 no card-detect.

**Correção**: instalar o serviço uma única vez, explicitamente no `kernel_init`, antes de qualquer
driver, e tratar `ESP_ERR_INVALID_STATE` como benigno nos drivers. Atualizar o comentário enganoso em
`tusb_desc.c:198`.

---

# Segurança

## 36. Credenciais em texto claro, sem flash encryption

**Severidade: Médio. Decisão de produto, mas hoje é implícita.**

`sdkconfig`:

```
# CONFIG_SECURE_FLASH_ENC_ENABLED is not set
# CONFIG_SECURE_BOOT is not set
# CONFIG_NVS_ENCRYPTION is not set
```

E `tos_config.h:52` e `:61` guardam `char password[65]` para AP e STA, persistidos como **JSON em texto
claro** na LittleFS (`FLASH_CONFIG_WIFI_AP`, `FLASH_CONFIG_WIFI_STA`).

Consequência: um dump da flash (que qualquer um com acesso físico e um cabo consegue, já que não há
secure boot nem encryption) entrega as credenciais de WiFi do usuário. Adicionalmente, sem secure boot
qualquer um pode gravar firmware arbitrário no aparelho.

Para uma ferramenta de pentest isso pode ser aceitável e até desejável (facilita desenvolvimento e
recuperação). O problema não é a escolha, é ela ser **implícita e não documentada**. Um usuário que
guarda a senha do WiFi de um cliente no aparelho tem direito de saber.

**Correção**: decidir explicitamente e documentar. Se ficar sem encryption, dizer isso no README e na
tela de about, e não persistir senha por padrão (pedir a cada conexão, ou oferecer "salvar senha" como
opt-in). Se for proteger, mover segredos para NVS com `CONFIG_NVS_ENCRYPTION` e avaliar
`CONFIG_SECURE_FLASH_ENC_ENABLED`, ciente de que encryption dificulta o fluxo de debug e recuperação
que hoje é usado no desenvolvimento.

---

# Orçamento de flash e RAM

Esta seção é diferente das outras: não são defeitos de código, são **limites físicos já quase
atingidos**. Medidos nos artefatos reais em `firmware_p4/build/` (build de 22/07/2026).

## 38. Binário a 93% do slot de OTA, teto iminente

**Severidade: Alto. Limite duro, não degradação.**

| Artefato | Tamanho | Slot | Ocupação |
|---|---|---|---|
| `build/TentacleOS_P4.bin` | 2.928.576 bytes | `ota_0` / `ota_1` = 3.145.728 | **93,1%** |

Folga: **217.152 bytes, ou 212 KB.**

Quando o binário passar de 3 MB, `esp_ota_begin`/`esp_ota_write` falham e **o OTA para de funcionar
por completo**. O aparelho continua bootando o que já tem, mas perde a capacidade de ser atualizado
remotamente. Recuperação só por cabo.

212 KB é pouco para o que este documento propõe. Estimativa grosseira do custo do roadmap:
`power_manager`, `input_manager`, registro de dispositivos, modo seguro, guarda térmica, mais o
`sys_prio.h` e a migração de I2C. Somado a features de produto que virão, o teto é alcançável neste
ciclo de desenvolvimento.

Agravante de interação: o item 22 (coredump) precisa de espaço em flash, e o item 39 mostra que não há
de onde tirar sem mexer nos slots de app.

**Correção**, em ordem de retorno sobre esforço:

1. **Medir antes de agir**: `idf.py size-components` e `size-files` mostram quem ocupa o quê. Suspeitas
   a verificar: o item 33 (três drivers órfãos compilados e nunca usados) e as duas pilhas de mesh
   (Meshtastic + MeshCore, ambas com protobuf).
2. **Remover os drivers órfãos do build** (item 33). Ganho imediato e zero risco funcional, já que
   nunca são inicializados.
3. **`CONFIG_COMPILER_OPTIMIZATION_SIZE`** (`-Os`) se hoje estiver em `-O2`. Ganho típico de 5-15% em
   projeto deste tamanho.
4. **Nível de log de release** (item 37): strings de log são bytes de flash. `LOG_DEFAULT_LEVEL_WARN`
   permite ao linker descartar as strings de INFO.
5. **Reavaliar o layout de partições** como um todo: dois slots de 3M mais 1920K de assets em 8 MB de
   flash é um orçamento apertado. Se as duas pilhas de mesh são mutuamente exclusivas na prática, vale
   considerar se ambas precisam estar na mesma imagem.

Adicionar ao CI uma checagem que falhe o build se o binário passar de, digamos, 90% do slot. Um teto
descoberto por acidente durante um OTA em campo é muito pior que um build vermelho.

## 39. Partição de assets 100% cheia, zero folga

**Severidade: Alto. Limite duro.**

| Artefato | Tamanho | Partição | Ocupação |
|---|---|---|---|
| `build/assets.bin` | 1.966.080 bytes | `assets` = 1920K = 1.966.080 | **100,0%** |

Não é "quase cheia". É **exatamente o tamanho da partição, zero bytes livres**.

Consequências operacionais:

- Nenhum ícone, fonte ou tema novo entra sem remover outro.
- O item 19 (escrita atômica de config via `<path>.tmp` + `rename`) **precisa de espaço temporário
  para o arquivo `.tmp`**. Numa LittleFS a 100%, a escrita atômica não tem para onde ir. A correção
  proposta no item 19 depende deste item.
- Os arquivos de config do sistema vivem nesta mesma partição (`FLASH_CONFIG_*` em
  `tos_flash_paths.h:25-28`). Isso significa que **salvar uma configuração pode falhar por falta de
  espaço**, e como o item 8 mostra que ninguém checa retorno, a falha é silenciosa.
- A rotação de log do `tos_log` (5 arquivos de 2 MB, `tos_log.c:32`) não cabe nesta partição de forma
  alguma. Isso indica que o log está indo para o SD, não para a flash. Verificar: se em algum cenário
  o log tentar a flash, ele enche a partição instantaneamente.
- LittleFS a 100% também não tem espaço para wear leveling nem para as operações de garbage collection,
  o que degrada a vida útil da flash e a performance de escrita.

**Correção**: auditar o conteúdo real de `assets/` e separar o que é imutável (ícones, fontes) do que é
mutável (config, temas do usuário). Duas partições distintas resolvem os dois problemas de uma vez:

- `assets` somente-leitura, dimensionada com folga de 10% e atualizada só por OTA de assets;
- `userdata` para config, temas e estado, dimensionada para caber `.tmp` de escrita atômica.

Enquanto isso não acontecer, o item 19 não é implementável e salvar config é uma operação de resultado
incerto.

## 40. Orçamento de stacks sem controle, 16 KB desperdiçados

**Severidade: Médio.**

Inventário das constantes de stack declaradas: **186.112 bytes (182 KB) em 34 declarações**. Nem todas
as tasks vivem simultaneamente, mas as de serviço vivem, e sem PSRAM isso sai todo da RAM interna.

Observações concretas:

| Constante | Valor | Nota |
|---|---|---|
| `UI_TASK_STACK_SIZE` | 16.384 (`4096 * 4`) | Para a task zumbi do item 7, que só faz `vTaskDelay`. **16 KB desperdiçados.** |
| `SPI_STREAM_TASK_STACK` | 16.384 | O maior do sistema |
| `MC_POLL_TASK_STACK` | 12.288 | MeshCore |
| `HAPTIC_TASK_STACK_SIZE` | 2.816 | O mais apertado |
| `AUDIO_TASK_STACK_SIZE`, `HEARTBEAT_STACK_SIZE`, `STREAM_WD_TASK_STK` | 3.072 | Apertados |
| maioria | 4.096 | Valor por convenção, não por medição |

Dois problemas de método:

1. **Nada é medido.** O `sys_monitor` já coleta `usStackHighWaterMark` (`sys_monitor.c:44`) e usa esse
   dado apenas para o comportamento destrutivo do item 6. Ele nunca reporta o consumo real por task,
   que é exatamente o dado necessário para dimensionar corretamente. As tasks de 2816 e 3072 bytes
   têm margem de 8-9% contra o `CRITICAL_STACK_THRESHOLD` de 256 bytes: pode ser folgado ou estar à
   beira do estouro, e ninguém sabe qual.
2. **Sem política central**, mesmo problema do item 12: 34 constantes escolhidas independentemente,
   com três `TASK_STACK_SIZE` homônimos em arquivos diferentes.

**Correção**: adicionar ao `sys_monitor` um relatório periódico (ou comando de console) de
`{task, stack alocada, watermark mínimo, folga %}`, rodar as telas todas, e redimensionar com base no
dado. Recuperar os 16 KB da `ui_task` ao resolver o item 7. Consolidar as constantes no mesmo
`sys_prio.h` do item 12, com faixas por classe de task.

Ganho esperado: dezenas de KB de RAM interna, que hoje é o recurso escasso. A PSRAM do protótipo não
ajuda aqui, porque stacks de FreeRTOS ficam em RAM interna.

---

# O que já está bem resolvido

Vale registrar, porque não é pouco e não deve ser mexido:

- **Watchdogs de hardware ativos**: `CONFIG_ESP_INT_WDT=y` com 300 ms e checagem no CPU1;
  `CONFIG_ESP_TASK_WDT_EN=y` com 5 s e checagem das idle tasks dos dois cores. Falta inscrever as tasks
  certas e ligar o panic (itens 2 e 30).
- **Brownout no nível máximo**: `CONFIG_BROWNOUT_DET_LVL=7`.
- **Dois cores disponíveis**: `CONFIG_FREERTOS_UNICORE is not set`, e `CONFIG_FREERTOS_HZ=1000` dá
  granularidade de 1 ms para o escalonador.
- **Hooks de FreeRTOS implementados**: `vApplicationStackOverflowHook` e `vApplicationMallocFailedHook`
  (`kernel.c:139-146`). Precisam de mais contexto, mas existem.
- **Rotação de log correta em conceito**: `tos_log.c` faz 5 arquivos de 2 MB com shift (`:59-83`),
  inclusive checagem na inicialização (`:130-133`). O problema é a falta de mutex e o flush por linha
  (item 3).
- **Guards de storage disciplinados**: todo caminho de escrita em SD checa `sd_is_mounted()` antes, e
  há um `storage_health_check` pronto (só falta chamar, item 17).
- **Defaults por campo na config**: `tos_config.c` usa `json_get_int(root, "campo", default)`, então
  config parcial ou corrompida degrada em vez de quebrar.
- **Curto-circuito limpo do bridge morto**: `spi_bridge.c:225-227` retorna `ESP_ERR_INVALID_STATE`
  imediatamente quando `!s_bridge_alive`, **sem logar**. Se logasse, o item 28c seria uma escrita em
  flash a cada 500 ms para sempre. Foi feito certo.
- **Ciclo de vida de USB CDC tratado**: `host_link_cdc.c:58-73` reage a mudança de DTR e a sessão é
  reivindicada na abertura da porta, não na inicialização. Desconexão é detectada.
- **Screen share aloca uma vez por sessão**: `lvgl_screen_share.c:101-125` aloca o buffer de snapshot na
  entrada da task e libera na saída, não por frame. Sem churn de heap.
- **Decoder de assets integrado ao cache do LVGL**: `assets_manager.c:86-146` registra decoder próprio e
  usa `lv_image_decoder_add_to_cache`, em vez de manter cópias próprias. Design correto; falta só
  limite e eviction (item 32).
- **Backlight PWM completo e persistente**: LEDC configurado, brilho salvo e restaurado no boot
  (`st7789.c:69-88`, `:157-170`, `:253-256`). Só não está ligado na UI (item 10).
- **Bateria real onde importa**: tensão, percentual, status de carga e status de VBUS são leituras
  verdadeiras do BQ25896 (`bq25896.c:121-160`). O que é mock é a camada de diagnóstico e controle.
- **OTA com rollback no bootloader e verificação de estado**: a estrutura está certa
  (`ota_service.c:244-273`), o critério de validação é que está errado (item 1).
- **OTA valida o tamanho antes de apagar a partição**: `ota_service.c:131-136` compara `file_size`
  contra `update_partition->size` e aborta **antes** do `esp_ota_begin` (`:151`). Isso significa que o
  teto do item 38, quando for atingido, falha de forma limpa com mensagem, e não deixa a partição
  meio apagada. Feito certo.
- **NFC e bridge em barramento separado**: SPI2 isolado do display evita que essas duas cargas
  compitam com o render (item 27).

---

# Roadmap

## Bloco 1: estabilidade

Mudanças pequenas e em grande parte independentes. Objetivo: parar de perder firmware e parar de
reiniciar sem explicação.

| Ordem | Item | Por que aqui |
|---|---|---|
| 1.1 | **30** | Yield no laço de OTA. **Pré-requisito obrigatório do 1.5.** |
| 1.2 | **1** | Validação de OTA com critério local, movida para depois do `kernel_init` |
| 1.3 | **4** | `esp_reset_reason` + contador em `RTC_NOINIT_ATTR` + modo degradado |
| 1.4 | **3** | Mutex e flush por lote no `tos_log`, rotação fora do caminho de escrita |
| 1.5 | **2** | `ui_acquire()` com timeout, LVGL no TWDT, `TASK_WDT_PANIC=y` |
| 1.6 | **31** | `esp_register_shutdown_handler` + `sys_reboot(reason)` nos 5 callsites |
| 1.7 | **5** | Tirar `ESP_ERROR_CHECK` do init, começando pelas 9 do `st7789.c` |
| 1.8 | **8** | `kernel_init` retorna erro e monta o mapa de subsistemas |
| 1.9 | **6** | `sys_monitor` para de matar tasks e respeita o lock da UI |
| 1.10 | **7** | Resolver a `ui_task` zumbi e apontar a supervisão para a task certa |
| 1.11 | **17** | Ligar `storage_health_check` no boot e no monitor |
| 1.12 | **26** | Delay de panic em 2 s |
| 1.13 | **37** | Nível de log de release, depois do 1.4. Também ganha flash para o 1.14 |
| 1.14 | **38** | Medir com `idf.py size-components`, remover órfãos do 33, `-Os`, teto no CI |
| 1.15 | **39** | Separar `assets` (read-only) de `userdata`. **Pré-requisito do item 19.** |

## Bloco 2: base arquitetural

Refactor de verdade. Objetivo: criar as fundações que a camada de energia exige.

| Ordem | Item | Por que aqui |
|---|---|---|
| 2.1 | **28** | Matar os timers sempre-ativos. **Pré-requisito de qualquer economia de energia.** |
| 2.2 | **12** | `sys_prio.h` com política de prioridade e afinidade, aplicada nos 44 sítios |
| 2.3 | **11** | `input_manager` com fila, debounce, long-press e `last_activity` |
| 2.4 | **13** | Contrato de ciclo de vida de tela (`open`/`close`/`suspend`/`resume`) |
| 2.5 | **14** | Liberar a tela que sai, no `close` |
| 2.6 | **33** | Registro central de dispositivos, resolve o "quem faz deinit" |
| 2.7 | **9** + **41** | `power_manager` com máquina de estados, `esp_pm` e locks de recurso; USB (VBUS + mount/suspend) como entrada de estado |
| 2.8 | **10** | Ligar brilho, timeout e auto-dim, consumindo os campos que já existem |
| 2.9 | **29** | Header alimentado por eventos reais, esconder o que não tiver dado |
| 2.10 | **27** | Reduzir contenção no SPI3: menos redraw, avaliar clock do painel |

## Bloco 3: observabilidade e polimento

Objetivo: poder diagnosticar um problema relatado por usuário.

| Ordem | Item |
|---|---|
| 3.1 | **22** Partição de coredump + coredump para flash + exposição via `host_link`. Depende do 38 |
| 3.2 | **15** Monitor de heap com maior bloco livre, thresholds e ação |
| 3.2b | **40** Relatório de watermark por task, redimensionar stacks, recuperar os 16 KB da `ui_task` |
| 3.3 | **32** Limite e eviction no `assets_manager`, busca por hash |
| 3.4 | **16** Fonte de tempo via `host_link` + NVS |
| 3.5 | **18** Mover o contador de boots para `sys_stats` |
| 3.6 | **19** Escrita atômica de config + `schema_version` |
| 3.7 | **23** Modo seguro por combinação de teclas no boot |
| 3.8 | **21** Guarda térmica |
| 3.9 | **20** Hotplug de SD, emitindo evento para o header |
| 3.10 | **35** Migrar I2C para o driver novo + recuperação de bus travado |
| 3.11 | **34** Tirar logs de dentro da posse de barramento do IMU |
| 3.12 | **24** Instalação única do serviço de ISR de GPIO |
| 3.13 | **25** Comparação tick-safe do `input_lock_until` |
| 3.14 | **36** Decidir e documentar a postura de segurança de credenciais |

## Dependências entre itens

Ordenações que não podem ser invertidas:

- **30 antes de 2**: ligar WDT panic sem yield no OTA reinicia o aparelho no meio da gravação.
- **28 antes de 9**: com redraw forçado a 2 Hz, `PM_ENABLE` e tickless idle não economizam nada.
- **11 antes de 10**: timeout de tela precisa de `last_activity`.
- **11 antes de 23**: modo seguro por botão precisa de long-press.
- **13 antes de 9**: os `esp_pm_lock` precisam de donos de recurso que os liberem.
- **3 antes de 37**: só faz sentido discutir nível de log depois do buffer.
- **31 antes de 6**: o `sys_monitor` precisa de um `esp_restart()` controlado para escalar para.
- **9 antes de 21**: a guarda térmica precisa de alguém que aja sobre o throttle.
- **20 alimenta 28a**: o evento de montagem elimina o polling de `sd_is_mounted()`.
- **17 alimenta 1**: `storage_health_check` é bom critério de validação do OTA.
- **4 e 23 convergem** no mesmo modo degradado. Implementar uma vez.
- **33 alimenta 8**: o registro de dispositivos é o mapa de subsistemas.
- **39 antes de 19**: escrita atômica precisa de espaço para o `.tmp`, e a partição está a 100%.
- **38 antes de 22**: coredump precisa de flash, e o item 39 fecha a alternativa de cortar dos assets.
- **33 alimenta 38**: remover os três drivers órfãos do build é ganho direto de flash.
- **37 alimenta 38**: nível de log menor deixa o linker descartar strings.
- **7 alimenta 40**: matar a `ui_task` zumbi recupera 16 KB de RAM interna.

---

# Limitações desta auditoria

O que está aqui foi verificado. O que **não** está aqui não foi declarado sadio, foi apenas não
auditado. Registro os pontos cegos para que ninguém trate este documento como prova de cobertura.

## Método

**Tudo é análise estática.** Nada foi compilado, executado ou medido em hardware nesta auditoria, com
a única exceção do item 38/39/40, que leem os artefatos já presentes em `build/` (de 22/07/2026, podem
estar defasados em relação ao código atual).

Consequência direta: não há **nenhum dado de runtime**. Não sei o tempo de boot real, não sei os
watermarks reais de stack (item 40), não sei a ocupação real de heap ao longo do uso (item 15), não sei
quanto tempo um flush de log realmente leva (item 3b), e não sei se a contenção do SPI3 (item 27) é
visível a olho nu ou apenas teórica. Todas as estimativas de impacto são derivadas de leitura de código
e de aritmética, não de medição.

A primeira coisa a fazer com este documento é **medir**, para ordenar o roadmap por dado e não por
suspeita.

## Áreas não auditadas

| Área | Situação |
|---|---|
| **`firmware_c5` a fundo** | Auditado no nível de Core, boot, storage, energia e ciclo de vida (ver seção C5). **Não** auditado: os protocolos de aplicação do C5 (`http_server`, `dns_server`, `esp_now`, `meshcore`, `meshtastic`, o stack NimBLE) quanto a robustez a entrada malformada. |
| **Protocolo do `host_link`** | Auditei o ciclo de vida de USB CDC (DTR). Não auditei: resync em quadro corrompido, timeouts, backpressure, comportamento com quadro malformado vindo do host. É superfície de estabilidade **e** de ataque. |
| **Protocolo do `spi_bridge`** | **Auditado** (seção "Protocolo SPI entre P4 e C5", SPI-1 a SPI-6). Não auditados: os transportes de aplicação por cima do bridge (`bt_dispatcher`, `meshtastic_transport`, `meshcore_transport`, `session_manager`) quanto a robustez de entrada. O `wifi_dispatcher` foi visto por amostragem. |
| **TinyUSB / Bad USB** | Descritor composto HID+CDC não auditado quanto a enumeração e conformidade. O lado de **energia** (suspend/resume e VBUS alimentando o power manager) foi auditado, ver item 41. |
| **Conflito console vs UART do C5** | Existe um teardown do REPL (`console_service.c:74`) para liberar a UART. Não auditei as condições de corrida desse handoff. |
| **Configuração fina do LVGL** | Olhei buffers e cache. Não olhei `LV_DISP_DEF_REFR_PERIOD` vs `timer_period_ms`, alinhamento de `LV_MEM`, nem a interação entre período de refresh e as faixas de 20 linhas. |
| **Orçamento de IRAM e alocação de interrupções** | Não verificado. Relevante para o item 3b, se a correção envolver mover ISRs para IRAM. |
| **LittleFS a fundo** | `LOOKAHEAD_SIZE=128`, `CACHE_SIZE=512`, `READ/WRITE_SIZE=128`. Não avaliei se são adequados, nem o comportamento de wear leveling, que o item 39 (100% cheia) torna suspeito. |
| **Auditoria sistemática de retorno de erro** | Encontrei casos por acaso (`init_i2c` void, retornos do kernel, `wifi_service` void). Não contei quantas funções públicas que podem falhar retornam `void`. O `CODING_STANDARDS.md` exige `esp_err_t`, então provavelmente há mais violações. |
| **Guardrails de CI** | Não existe nada que impeça alguém de adicionar um 45º `xTaskCreate` com prioridade arbitrária, ou de estourar o teto do item 38. Todos os itens de política deste documento (12, 40) precisam de verificação automatizada para não regredirem. |
| **`Teste/`, `temp/`, `firmware_p4/temp/`** | Diretórios na raiz e no firmware que não olhei. Podem ser artefatos obsoletos, podem ter código relevante. |

## O que consideraria a próxima rodada

Em ordem de valor:

1. **Instrumentar e medir.** Ligar o `sys_monitor` em verbose, adicionar relatório de watermark
   (item 40) e de heap (item 15), rodar todas as telas, e trazer o dado. Isso reordena o roadmap.
2. **Auditar `host_link` e `spi_bridge` como protocolo**, com foco em quadro malformado. É o caminho
   por onde um host externo fala com o aparelho.
3. **Auditar os protocolos de aplicação do C5** (`http_server`, `dns_server`, `meshtastic_tcp`, NimBLE)
   quanto a robustez a entrada malformada. A camada operacional do C5 está coberta na seção abaixo, mas
   como o C5 é a face de rede do produto, os parsers dele são superfície de ataque.
4. **Guardrails de CI** para os limites duros (38, 39, mais o C5-4) e para as políticas (12, 40).

---

# Auditoria do firmware_c5

O C5 é um sistema executável completo. `main/main.c` chama `kernel_init()`, e o C5 é a **face de rede
do produto**: WiFi (STA + AP + sniffing com channel hopping), Bluetooth NimBLE, DNS, HTTP com captive
portal, ESP-NOW, Meshtastic e MeshCore. O P4 fala com ele pelo `spi_bridge` e recebe os logs de volta
por SPI (`c5_log`).

Arquitetura de compartilhamento: o C5 e o P4 **compartilham o mesmo `Core`** (kernel + sys_monitor,
com variações) e vários `Service` idênticos por cópia (`storage_api`, `storage_vfs`, `storage_assets`,
`ota`, `wifi`, `host_link`, `bq25896`, `buttons_gpio`). Isso tem uma consequência dupla: **o C5 herda a
maioria dos defeitos estruturais do P4, e qualquer correção de item compartilhado precisa ser aplicada
ou portada nos dois lados.** Hoje são duas cópias que divergiram, não um componente comum.

## Itens do P4 que o C5 também tem

Verificados presentes no código do C5, com os mesmos file:line quando o arquivo é cópia:

| Item P4 | Estado no C5 | Evidência |
|---|---|---|
| **3** tos_log sem mutex + flush por linha | Presume-se igual (mesmo `storage_api`); o C5 tem ainda o `c5_log` que tee para SPI | mesmo componente copiado |
| **4** sem `esp_reset_reason` / boot-loop | **Presente.** `kernel.c` não lê reset reason, `main.c` não conta boots | `firmware_c5/.../kernel.c:44-70` |
| **5** `ESP_ERROR_CHECK` no init | **Presente.** `kernel.c:47,50` | `firmware_c5/.../kernel.c` |
| **6** sys_monitor mata tasks | **Pior no C5.** Ver C5-1 abaixo | `firmware_c5/.../sys_monitor.c:67-77` |
| **8** retornos de init descartados | **Presente e mais grave.** `init_i2c`, `storage_init`, `storage_assets_init`, `bq25896_init`, `wifi_service_init` todos com retorno ignorado | `firmware_c5/.../kernel.c:52-67` |
| **9** sem `esp_pm`, sem sono | **Presente.** `PM_ENABLE is not set`. Ver C5-2, que é pior num nó de rádio | `firmware_c5/sdkconfig` |
| **15** sem política de heap | **Presente.** mesmo `sys_monitor` que só loga em verbose | `firmware_c5/.../sys_monitor.c:46-56` |
| **16** sem fonte de tempo | **Presente**, e mais sentido: um nó de rede sem hora não consegue validar TLS nem datar mesh | grep vazio |
| **19** escrita de config não atômica | Presume-se igual (mesmo `storage_api`) | mesmo componente |
| **22** coredump desativado | **Presente.** `ESP_COREDUMP_ENABLE_TO_NONE=y`, sem partição | `firmware_c5/sdkconfig`, `partitions.csv` |
| **26** delay de panic 0 s | **Presente.** `PANIC_REBOOT_DELAY_SECONDS=0` | `firmware_c5/sdkconfig` |
| **30** loop de OTA sem yield | **Presente.** O `ota_task` do C5 tem `while (remaining > 0) { ...esp_ota_write... }` sem `vTaskDelay` nem WDT feed | `firmware_c5/.../ota_service.c` loop de recepção UART |
| **31** sem shutdown gracioso | **Presente.** Nenhum `esp_register_shutdown_handler` | grep vazio |
| **33** drivers órfãos | **Parcial.** `led_rgb_init()` está comentado no `kernel.c:58` | `firmware_c5/.../kernel.c:58` |
| **37** log INFO amplifica item 3 | **Presente.** `LOG_DEFAULT_LEVEL_INFO=y` | `firmware_c5/sdkconfig` |
| **40** stacks sem controle | **Presente.** Mesmo padrão, agravado pelo alloc estático do hopper (C5-6) | inventário de tasks |

Itens que **não** se aplicam ao C5: todos os de UI (2, 7, 10, 11, 12 parcial, 13, 14, 28, 29), de
display (10, 27, 34) e de entrada (25), porque o C5 é headless. O item 1 (rollback) não se aplica
porque `BOOTLOADER_APP_ROLLBACK is not set` no C5 (o que traz o problema oposto, C5-5).

## O que já está bem resolvido no C5

- **Unicore explícito e correto**: `CONFIG_FREERTOS_UNICORE=y` + `SINGLE_CORE_MODE=y`, com comentário
  explicando por quê. O C5 é single-core de verdade.
- **Console em USB-JTAG, UART0 livre para OTA**: decisão deliberada e documentada em
  `sdkconfig.defaults`. O receptor de OTA do C5 usa a UART0 sem conflito com o console.
- **SPI slave em modo POLL**: `kernel.c:60-61` roda o slave em POLL porque a placa V2 não tem trace de
  IRQ do bridge, com comentário. Coerente com o lado P4.
- **Channel hopper com stack estática e teardown real**: `wifi_service.c:556-578` aloca TCB e stack
  estáticos e tem `wifi_service_stop_channel_hopping()` (`:373`) que de fato para a task. Melhor
  disciplina de ciclo de vida que a média do P4.
- **Watchdogs, brownout nível 7**: iguais ao P4, ativos.
- **WiFi modem sleep configurado**: `ESP_WIFI_SLP_*` presente (ver C5-2, que é sobre não ativá-lo).

## Achados exclusivos do C5

### C5-1. `sys_monitor` mata tasks sem nenhuma proteção

**Severidade: Alto.**

`firmware_c5/components/Core/sys_monitor.c:64-78`:

```c
for (uint32_t i = 0; i < task_count; i++) {
  uint32_t watermark = pxTaskStatusArray[i].usStackHighWaterMark;
  if (watermark < CRITICAL_STACK_THRESHOLD) {
    ESP_LOGE(TAG, "!!! SECURITY ALERT !!! Task [%s] ... TERMINATING TASK.", ...);
    if (pxTaskStatusArray[i].xHandle != xTaskGetCurrentTaskHandle()) {
      vTaskDelete(pxTaskStatusArray[i].xHandle);
    }
  }
}
```

É a mesma patologia do item 6 do P4, mas **sem nem a tentativa de recuperação** que o P4 tem. O C5
simplesmente deleta qualquer task com menos de 256 bytes de folga de stack. Num nó de rede, as tasks
candidatas a estourar stack são justamente as de TLS, HTTP e mesh (parsers com buffers profundos). Matar
a task de WiFi no meio de uma transação **vaza o lock interno do stack de rede e trava a conectividade
até reboot**, sem nenhuma sinalização ao P4 além de os logs pararem.

Pior que o P4: o C5 é o lado que o usuário não vê nunca. Um travamento aqui aparece como "o WiFi do
aparelho parou de funcionar" sem tela, sem alerta, sem nada.

**Correção**: mesma do item 6. Observar e reportar (via `c5_log` para o P4), nunca matar. Se precisar
agir, reiniciar o C5 inteiro de forma controlada e avisar o P4 pelo bridge.

### C5-2. Nó de rádio sem qualquer power management, com modem sleep disponível e não usado

**Severidade: Alto.**

O C5 é o maior consumidor de energia do produto: mantém WiFi e/ou BLE ativos. E:

- `CONFIG_PM_ENABLE is not set`, CPU travada em 240 MHz;
- nenhum `esp_wifi_set_ps()` no código (grep vazio), então o WiFi **nunca entra em modem sleep** mesmo
  parado, apesar de o `sdkconfig` ter todo o bloco `ESP_WIFI_SLP_*` configurado. A infraestrutura está
  lá, a chamada que a ativa não.

Num handheld a bateria, ter o rádio 2.4 GHz em RX contínuo sem power save é dos maiores drenos
possíveis. E como o C5 não tem noção de estado de energia (não sabe se o P4 está dormindo, se a tela
está apagada, se o usuário guardou o aparelho), ele fica a plena carga o tempo todo.

**Correção**: `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` no mínimo, e idealmente um protocolo pelo bridge em
que o P4 informa o estado de energia ao C5, para o C5 baixar rádio ou dormir junto. Isto casa com o
`power_manager` do item 9: o power manager do produto precisa abranger os dois chips, não só o P4.

### C5-3. `kernel_init` monta storage e assets que não existem na tabela de partições

**Severidade: Alto. Boot gasta tempo e loga erro em toda inicialização.**

`firmware_c5/components/Core/kernel.c:52-56`:

```c
init_i2c();
storage_init();            // monta LittleFS no label "storage"
storage_assets_init();     // monta LittleFS no label "assets"
storage_assets_print_info();
```

`storage_init()` (via `vfs_config.h:46`) espera uma partição de label **`storage`**.
`storage_assets_init()` (`storage_assets.c:30`) espera label **`assets`**.

Mas `firmware_c5/partitions.csv` tem **apenas**:

```
nvs, otadata, phy_init, ota_0 (2M), ota_1 (2M)
```

**Nenhuma partição `storage`, nenhuma `assets`.** As duas montagens vão falhar com `ESP_ERR_NOT_FOUND`.
Pior: `storage_assets_init()` usa `.format_if_mount_failed = true` (`storage_assets.c:107`), então tenta
formatar uma partição que não existe, o que também falha, depois de gastar tempo tentando.

E pelo item 8 (retornos descartados), o `kernel.c` não percebe. O boot segue, `storage_assets_print_info()`
loga "partition has no files" e o C5 opera sem storage. Se qualquer serviço do C5 (captive portal HTML,
config de wifi salva, scripts) tentar ler da flash, falha silenciosamente.

Isto pode ser intencional (o C5 talvez não devesse ter storage próprio, os dados vivem no P4), mas
então **as duas chamadas em `kernel_init` são código que só produz erro em todo boot.** Ou a tabela de
partições está errada (falta a partição), ou o kernel está errado (não deveria montar). As duas
hipóteses são bugs; só muda qual arquivo se conserta.

**Correção**: decidir a intenção. Se o C5 precisa de storage, adicionar as partições à `partitions.csv`
(há espaço: 8 MB de flash, só 4 MB usados pelos dois slots de OTA). Se não precisa, remover
`storage_init()` e `storage_assets_init()` do `kernel_init` do C5. De qualquer forma, tratar o retorno.

### C5-4. Slots de OTA de 2M com binário a 75%, e sem partição de dados

**Severidade: Médio.**

`firmware_c5/build/TentacleOS_C5.bin` = 1.568.304 bytes. Slots `ota_0`/`ota_1` = 2 MB = 2.097.152.
Ocupação: **74,8%**, folga de ~504 KB. Mais confortável que o P4 (item 38), mas o C5 carrega NimBLE +
WiFi + lwIP + Meshtastic/MeshCore, que crescem rápido.

O ponto real é o layout: `nvs (24K) + otadata (8K) + phy (4K) + ota_0 (2M) + ota_1 (2M)` = ~4 MB dos
8 MB de flash. **Quase metade da flash está sem uso e sem partição declarada**, enquanto o kernel tenta
montar `storage` e `assets` inexistentes (C5-3). O espaço para resolver o C5-3 existe de sobra; só não
foi alocado.

**Correção**: redesenhar `partitions.csv` do C5 para incluir as partições de dados que o `kernel_init`
já assume existirem, usando os ~4 MB livres. Adicionar teto de tamanho no CI, como no item 38.

### C5-5. OTA sem rollback: imagem ruim tijola o C5

**Severidade: Médio. É o espelho invertido do item 1.**

`firmware_c5/sdkconfig`: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set`. E o C5 não chama
`ota_post_boot_check()` no boot (o `main.c` só chama `kernel_init`), nem
`esp_ota_mark_app_valid_cancel_rollback()` em lugar nenhum.

O C5 recebe firmware pelo P4 via UART (`ota_service.c`, receptor UART0). Sem rollback, se o P4 empurrar
uma imagem que boota mas está quebrada (por exemplo, trava o WiFi, ou não responde mais ao bridge),
**o C5 fica preso nessa imagem sem caminho automático de volta.** Como o C5 é headless e a única via de
recuperação é o próprio P4 pelo bridge, um C5 que não fala mais com o bridge só se recupera pelo
`c5_rom_flasher` (o caminho de recuperação por ROM que existe no P4, `c5_flasher.c`).

Note a assimetria com o P4: o P4 tem rollback ligado e o critério de validação errado (item 1, reverte
sempre). O C5 tem rollback desligado (nunca reverte, mesmo quando deveria). **Os dois extremos errados
do mesmo mecanismo, um em cada chip.**

**Correção**: ligar `BOOTLOADER_APP_ROLLBACK_ENABLE` no C5, e definir um critério de validação local
que faça sentido para um nó de rede: "o bridge respondeu ao P4 dentro de N segundos" é um bom sinal de
vida, porque é exatamente a função do C5. Se o P4 não confirmar o C5 pelo bridge, rollback. Isso fecha
o ciclo com o item 1: o P4 valida o próprio boot, e o C5 valida o dele pela saúde do bridge.

### C5-6. `channel_hopper` aloca stack estática que nunca é liberada no stop

**Severidade: Baixo.**

`wifi_service.c:556-578` cria o hopper com `xTaskCreateStatic`, alocando `s_hopper_task_stack` e
`s_hopper_task_tcb` via `heap_caps_malloc`. O `wifi_service_stop_channel_hopping()` (`:373`) para a
task, mas (pelo trecho lido) os buffers estáticos alocados no heap não são liberados no stop, só
reaproveitados no próximo start pela guarda `if (s_hopper_task_stack == NULL)`.

Não é vazamento crescente (é reusado), mas é memória presa pela vida do processo mesmo quando o sniffing
está desligado. Num chip sem PSRAM e sem folga, cada bloco preso conta.

**Correção**: liberar no stop, ou documentar que é cache intencional. Menor prioridade dos achados do
C5.

## Roadmap do C5

O C5 entra no mesmo faseamento, com a regra de que **itens de componente compartilhado devem ser
corrigidos uma vez e portados**, não consertados em uma cópia só.

**Bloco 1 (estabilidade), específico do C5:**
- **C5-3**: resolver o mismatch de partição. É o mais barato e elimina erro em todo boot.
- **C5-1**: `sys_monitor` para de matar tasks (portar a correção do item 6).
- **C5-5**: ligar rollback com validação pela saúde do bridge.
- **30, 4, 8, 31**: portar as correções do P4 (mesmo código).

**Bloco 2 (arquitetural), específico do C5:**
- **C5-2**: `esp_wifi_set_ps` + protocolo de estado de energia pelo bridge. Parte do `power_manager`
  do item 9, que precisa ser um serviço que abrange os dois chips.
- **C5-4**: redesenho da tabela de partições.
- **Unificar os componentes compartilhados** entre P4 e C5 num lugar comum (`common/`), para acabar com
  as cópias divergentes. Este é o item estrutural de fundo: enquanto forem cópias, todo conserto é
  feito duas vezes ou esquecido em um lado.

**Bloco 3:**
- **C5-6**, mais os itens compartilhados de observabilidade (15, 22, 40).
- Auditoria dos parsers de rede do C5 (HTTP, DNS, mesh, NimBLE), que este documento não cobriu.

---

# Protocolo SPI entre P4 e C5

Auditado a pedido. Este é o canal por onde **toda** a comunicação entre os dois chips passa: o P4
(master) manda comandos, o C5 (slave) responde e também emite streams. O protocolo está definido em
`spi_bridge/include/spi_protocol.h`, o master em `firmware_p4/.../spi_bridge/spi_bridge.c`, o slave em
`firmware_c5/.../spi_bridge/spi_bridge.c`.

Formato do quadro: cabeçalho de **5 bytes** (`spi_header_t`: `sync, type, category, op, length`)
seguido de até `SPI_MAX_PAYLOAD` de payload. `SPI_SYNC_BYTE = 0xAA`. Tamanho de quadro fixo
`SPI_FRAME_SIZE` (arredondado para múltiplo de 4 por causa do DMA), e um quadro maior de 2048 bytes
(`SPI_STREAM_FRAME_SIZE`) só para o stream em lote.

Antes dos defeitos, o que está **certo**: o master valida `sync`, `type`, casamento de command id, e
rejeita resposta atrasada de comando que expirou (`spi_bridge.c:287-296`, com comentário claro sobre
resync). O slave rejeita quadro com `sync`/`type` errado e re-arma o RX (`spi_bridge.c:257-261`). O
`wifi_dispatcher` valida `len` antes de cada `memcpy` (`wifi_dispatcher.c:135,160,177,187,202,208,282`).
O tamanho fixo de quadro dá um enquadramento simples e previsível. Não é um protocolo ingênuo. Os
problemas são de robustez a erro e de disciplina de manutenção, não de design grosseiro.

## SPI-1. Zero verificação de integridade num barramento inter-chip

**Severidade: Alto.**

Grep por `crc`, `checksum`, `fletcher`, `parity`, `hash` em ambos os `spi_bridge/`: **nada.** O quadro
não tem campo de integridade nenhum. A única defesa contra corrupção é o byte de sync `0xAA` e a
checagem de `type`.

Consequência: **qualquer bit invertido no barramento é aceito silenciosamente** desde que não caia no
byte de sync. Um bit-flip no `length`, no `op`, ou em qualquer byte de payload passa como dado válido.
Num link SPI entre dois chips, com traços de PCB, conector, e o barramento sendo o mesmo que sofre a
contenção do item 27 e o mascaramento de interrupção do item 3b, corrupção não é hipotética.

O modo de falha é o pior possível: não trava, não loga, **entrega dado errado como se fosse certo**. Um
`op` corrompido vira outro comando; um `length` corrompido muda quantos bytes o receptor interpreta; um
payload corrompido de um `SPI_ID_WIFI_CONNECT` conecta na rede errada ou com credencial truncada.

**Correção**: adicionar um campo de checksum ao quadro. CRC-16 sobre header+payload é barato (tabela de
512 bytes, ou o periférico de CRC do ESP). O receptor descarta e pede reenvio em mismatch. Isso exige
mexer no `spi_header_t`, que por sua vez cai no SPI-2 (as duas cópias precisam mudar juntas).

## SPI-2. Protocolo definido por cópia, e as duas cópias já divergiram

**Severidade: Alto. Dívida ativa que já produziu diferença.**

`spi_protocol.h` existe em duas cópias, uma em cada firmware, e **elas não são idênticas.** `diff`
entre as duas mostra que o lado P4 tem, e o C5 não tem:

- toda a categoria `SPI_CAT_SCREEN` (screen sharing, 4 ops + 2 structs). Tudo bem, é P4-nativo e nunca
  vai ao C5. Mas está no header compartilhado.
- sete ops de WiFi novas: `SPI_ID_WIFI_PORT_SCAN_*` (0x49-0x4D), `SPI_ID_WIFI_GET_MAC` (0x4E),
  `SPI_ID_WIFI_GET_IP_INFO` (0x4F).
- os structs `spi_wifi_ip_info_t`, `spi_port_scan_range_req_t`, `spi_port_scan_network_req_t`,
  `spi_port_scan_cidr_req_t`, `spi_port_scan_result_t`.

Ou seja: o P4 sabe pedir `SPI_ID_WIFI_GET_MAC`, mas **o C5 desta árvore não conhece esse op** e vai
responder `SPI_STATUS_UNSUPPORTED` (ou pior, se o número colidir com outro op no futuro). O contrato de
fio dos dois chips diverge por edição manual de dois arquivos que deveriam ser um.

Isso é a mesma doença de fundo do C5 (código compartilhado por cópia), mas aqui é **crítica**: um
protocolo de fio cuja definição diverge entre as duas pontas é uma fonte permanente de bug de
interpretação. O dia em que um `op` for reusado em um lado e não no outro, os dois chips vão
interpretar os mesmos bytes como comandos diferentes, sem nenhum erro visível (não há checksum, SPI-1,
que pegasse o descasamento).

**Correção**: `spi_protocol.h` tem que ser **um único arquivo**, em `common/`, incluído pelos dois
builds. É a correção mais importante desta seção, e destrava o SPI-1 (não dá para adicionar checksum a
um header que existe em duas versões).

## SPI-3. `length` é `uint8_t` mas `SPI_MAX_PAYLOAD` é 256

**Severidade: Médio.**

`spi_protocol.h:27`: `#define SPI_MAX_PAYLOAD 256`. Mas `spi_header_t.length` é `uint8_t` (`:305`),
que vai só até 255. Um payload de exatamente 256 bytes **não é representável** no campo de comprimento:
`(uint8_t)256 == 0`.

Efeitos em cascata:

- Os buffers dimensionados como `SPI_MAX_PAYLOAD` (por exemplo `resp_payload[SPI_MAX_PAYLOAD]` no slave,
  `firmware_c5/.../spi_bridge.c:265`) têm 1 byte a mais do que o protocolo consegue endereçar.
- As checagens `resp->length > SPI_MAX_PAYLOAD` no master (`spi_bridge.c:298`) e
  `header->length > SPI_MAX_PAYLOAD` no slave (`spi_bridge.c:258`) são **código morto**: um `uint8_t`
  nunca excede 256. As duas linhas parecem validar o comprimento e não validam nada.
- A API de envio usa `uint8_t len` (`spi_bridge.h:117-119`), então o cap real é 255, não 256. A
  constante mente sobre o limite.

**Correção**: alinhar. Ou `SPI_MAX_PAYLOAD = 255` (e as checagens viram `>= 255` ou `> 254` onde faz
sentido), ou o campo `length` vira `uint16_t` se 256+ for necessário. De qualquer forma, remover as
duas checagens mortas ou torná-las reais. Item pequeno mas é uma armadilha para quem confia nas
constantes.

## SPI-4. Handlers inline do slave leem payload sem checar o comprimento anunciado

**Severidade: Médio. Processa comando truncado como válido.**

Enquanto o `wifi_dispatcher` valida `len` antes de cada cópia, os handlers **inline** dentro do
`spi_bridge.c` do slave não validam. Exemplos:

`firmware_c5/.../spi_bridge.c:303-304` (SYSTEM_DATA):

```c
uint16_t index;
memcpy(&index, cmd_payload, sizeof(index));   // lê 2 bytes sem checar header->length
```

`:355-356` e `:364-365` (SESSION heartbeat e stop):

```c
spi_heartbeat_req_t req = {0};
memcpy(&req, cmd_payload, sizeof(req));        // lê sizeof(req) sem checar header->length
```

Se o master mandar um quadro com `length` menor que o struct esperado (por truncamento, por bug, ou por
corrupção do SPI-1), o slave lê `sizeof(req)` bytes de `cmd_payload` mesmo assim.

O dano é **contido, não é out-of-bounds**: `rx_buf` é `SPI_FRAME_SIZE` e é zerado antes de cada
transferência (`:243,251,259`), então a leitura excedente pega zeros ou dados residuais do próprio
buffer, não memória fora dele. Não há crash nem vazamento de memória adjacente. Mas o slave **processa
um comando malformado como se fosse legítimo**: um heartbeat truncado vira um `session_id` = lixo, um
SYSTEM_DATA truncado lê um `index` = lixo. Sem o checksum do SPI-1, não há como o slave saber que o
quadro chegou torto.

**Correção**: cada handler inline checa `header->length >= sizeof(a_struct)` antes do `memcpy`,
retornando `SPI_STATUS_INVALID_ARG` caso contrário. É o mesmo padrão que o `wifi_dispatcher` já segue;
falta portar para os handlers inline.

## SPI-5. `send_command` copia para `out_payload` sem parâmetro de capacidade

**Severidade: Médio. Contrato de API perigoso.**

`firmware_p4/.../spi_bridge.h:117-122`:

```c
esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *payload, uint8_t len,
                                  spi_header_t *out_header, uint8_t *out_payload,
                                  uint32_t timeout_ms);
```

`out_payload` não vem acompanhado de um tamanho. O master, ao receber a resposta, faz
(`spi_bridge.c:318-321`):

```c
data_len = (uint8_t)(resp->length - SPI_RESP_STATUS_SIZE);   // até 254
...
memcpy(out_payload, rx_buf + ..., data_len);
```

Ou seja, **o master escreve no buffer do chamador uma quantidade de bytes decidida pelo `length` que o
slave anunciou**, até 254 bytes, sem saber o tamanho real de `out_payload`. Se o chamador passou um
buffer menor que o esperado, ou se o slave (com bug, ou com o `length` corrompido pelo SPI-1) anuncia
mais bytes do que o chamador previu, o master estoura o buffer do chamador. Isso é um overflow no lado
P4 disparado por dado vindo do C5.

Hoje é contido pela confiança mútua (os dois firmwares são do mesmo autor e os call sites dimensionam
certo), mas é frágil por construção: um `length` corrompido é suficiente, e não há checksum que o pegue.

**Correção**: adicionar `size_t out_capacity` à assinatura e truncar `data_len` a ela, exatamente como o
slave já faz com `resp_len` na direção contrária (`spi_bridge.c:396`, que clampa a
`SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE`). É uma mudança que toca todos os call sites de
`send_command`, mas é mecânica.

## SPI-6. Resync em POLL depende só do byte de sync, sem confirmação

**Severidade: Baixo, sobe para Médio se o SPI-1 não for feito.**

Em modo POLL (o modo atual, porque a placa V2 não tem trace de IRQ, `kernel.c:60-61` no C5), o master
re-clocka o barramento até ver um quadro "válido" (`spi_bridge.c:126-130`):

```c
bool armed = (resp->sync == SPI_SYNC_BYTE) &&
             (resp->type == SPI_TYPE_RESP || resp->type == SPI_TYPE_STREAM);
if (armed && (!match_cmd || spi_header_cmd(resp) == expect_cmd)) return ESP_OK;
```

Enquanto o slave está ocupado e sem TX armado, o master lê "junk" e tenta de novo. O critério de
"achei um quadro" é: primeiro byte `0xAA` e segundo byte `0x02` ou `0x03`. São **16 bits de
reconhecimento** num fluxo de bytes arbitrários. A probabilidade de lixo casual passar por esse portão
é baixa mas não nula (~1 em 32 mil por posição), e sem checksum (SPI-1) um falso positivo é aceito como
quadro real e o `length` de lixo é usado.

O `match_cmd` ajuda no caminho de comando (exige o command id certo), mas o caminho de stream
(`match_cmd == false` para streams) aceita qualquer coisa com sync + type de stream.

**Correção**: o checksum do SPI-1 resolve isto de graça, porque um quadro de lixo não passa no CRC. Sem
o checksum, ao menos exigir mais bits de confirmação (validar `category`/`op` contra a lista de ops
conhecidos, e `length` dentro do range válido) antes de aceitar o quadro.

## Roadmap do protocolo SPI

Ordenado por dependência:

1. **SPI-2** (unificar `spi_protocol.h` em `common/`) primeiro, porque tudo que mexe no header depende
   de existir um só header.
2. **SPI-1** (checksum no quadro) em seguida. Resolve SPI-6 de graça e é a maior melhoria de robustez.
3. **SPI-4** e **SPI-5** (validar comprimento no slave e no master) juntos, são o mesmo tema de "não
   confiar no comprimento anunciado".
4. **SPI-3** (alinhar `SPI_MAX_PAYLOAD` com o campo `length`) a qualquer momento, é isolado.

Nota de escopo: não auditei os transportes de aplicação por cima do bridge (`bt_dispatcher`,
`meshtastic_transport`, `meshcore_transport`, `session_manager`) quanto a robustez de entrada. O
`wifi_dispatcher` foi olhado por amostragem e valida comprimento; os outros não foram verificados e
ficam como ponto cego, junto com os parsers de rede do C5.

## A observação estrutural mais importante do C5

O produto tem **dois sistemas operacionais que compartilham código por cópia manual**. `Core`,
`storage_api`, `wifi`, `ota`, `bq25896`, `host_link` existem nos dois firmwares como arquivos
duplicados que já divergiram (o `sys_monitor` do C5 é uma versão anterior e pior do que o do P4; o
`wifi_service` é completamente diferente, real no C5 e proxy no P4).

Isso significa que **a maior parte deste documento tem incidência dupla**, e que a dívida cresce a cada
correção feita em um lado só. A recomendação de fundo, que vale mais que qualquer item individual, é
extrair o `Core` e os `Service` genuinamente comuns para `common/` e fazer os dois firmwares
consumirem a mesma fonte. Sem isso, "consertar o item 6" vira "consertar o item 6 no P4 e lembrar de
portar para o C5-1", e o segundo passo vai ser esquecido.

## Leitura resumida

O bloco 1 é o que separa "protótipo que funciona na bancada" de "aparelho que o usuário confia".
O bloco 2 é o que separa disso um sistema operacional.
O bloco 3 é o que permite manter o sistema depois que ele estiver na mão de outras pessoas.

Dois itens do bloco 2 são pré-requisitos técnicos e não melhorias: **28** (sem ele não existe economia
de energia possível) e **11** (sem ele não existe timeout de tela possível). Se houver pouco tempo para
o bloco 2, esses dois são os que destravam o resto.
