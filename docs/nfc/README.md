# NFC Application

The NFC feature has two halves in the tree:

1. A complete ST25R3916-based NFC stack under
   `components/Applications/nfc/` (manager, scanner, reader, listener, device /
   store persistence, and a full protocol tree), talking to the
   `st25r3916` driver (`components/Drivers/st25r3916/`, "HighBoy NFC").
2. An on-device UI suite under `components/Applications/ui/screens/nfc/`.

**Important:** the shipping UI currently runs as a **simulation**. As documented
in `nfc_sim.h`, the ST25R3916 shares the SPI3 bus whose MISO line is tied to
LCD-RST by a board jumper and cannot be read reliably, so the NFC screens use a
faithful simulation model (`nfc_sim`) instead of driving the radio. The protocol
stack and driver are compiled and complete, but the current screens do not call
them (the only wiring is a defensive `nfc_manager_stop` close-hook registered in
`ui_manager.c`).

## Overview

- **App location:** `components/Applications/nfc/`
- **UI location:** `components/Applications/ui/screens/nfc/`
- **Driver:** `components/Drivers/st25r3916/` (`highboy_nfc_*`)
- **Target IC:** ST25R3916 / ST25R3916B (SPI mode 1, clock <= 6 MHz)
- **Simulation model:** `ui/screens/nfc/nfc_sim.c` (saved library persisted in NVS)

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│              UI suite (ui/screens/nfc/*)                 │
│  Menu + Read / Identify / Emulate / Write / Saved /      │
│  Config / per-family detail screens                      │
│  event-driven input, rotation-aware layout               │
│                    │                                     │
│                    ▼ (currently) nfc_sim model + NVS     │
│         ┌───────────────────────────┐                    │
│         │ nfc_sim: card model +     │                    │
│         │ saved library (NVS)       │                    │
│         └───────────────────────────┘                    │
└──────────────────────────────────────────────────────────┘
      · · · · · · (not wired in current UI) · · · · · · · · ·
┌──────────────────────────────────────────────────────────┐
│              NFC stack (Applications/nfc/)               │
│  manager (state machine + scan task)                     │
│  scanner · reader · listener (emulation)                 │
│  device / store (NVS card profiles + entries)            │
│  protocols/  (see table below)                           │
└───────────────────────────┬──────────────────────────────┘
                            ▼
┌──────────────────────────────────────────────────────────┐
│        st25r3916 driver (highboy_nfc, SPI2/SPI3)         │
│  core · fifo · irq · aat + HAL (gpio / spi / timer)      │
└──────────────────────────────────────────────────────────┘
```

## UI Suite (`ui/screens/nfc/`)

`ui_nfc_menu_open()` (`nfc_menu_ui.c`) builds the NFC menu. Selecting an item
switches to the matching screen via the `screen_id_t` enum in `ui_manager.h`.

| Menu item | Screen | File |
|-----------|--------|------|
| READ TAGS | `SCREEN_NFC_READ` | `nfc_read_ui.c` |
| SCAN / IDENTIFY | `SCREEN_NFC_SCAN` | `nfc_scan_ui.c` |
| EMULATE | `SCREEN_CARD_EMU` | `card_emu_ui.c` |
| WRITE | `SCREEN_NFC_WRITE` | `nfc_write_ui.c` |
| CONFIGURATIONS | `SCREEN_NFC_CONFIG` | `nfc_config_ui.c` |
| SAVED | `SCREEN_NFC_SAVED` | `nfc_saved_ui.c` |
| BANK CARD | `SCREEN_NFC_BANKCARD` | `nfc_bankcard_ui.c` |
| DESFIRE | `SCREEN_NFC_DESFIRE` | `nfc_desfire_ui.c` |
| NFC-V / 15693 | `SCREEN_NFC_ISO15693` | `nfc_iso15693_ui.c` |
| ULTRALIGHT/NTAG | `SCREEN_NFC_ULTRALIGHT` | `nfc_ultralight_ui.c` |
| NDEF | `SCREEN_NFC_NDEF` | `nfc_ndef_ui.c` |
| FELICA | `SCREEN_NFC_FELICA` | `nfc_felica_ui.c` |
| SHARE (P2P) | `SCREEN_NFC_P2P` | `nfc_p2p_ui.c` |
| KEY DICTIONARY | `SCREEN_NFC_KEYDICT` | `nfc_keydict_ui.c` |

There is also a dedicated emulate screen (`nfc_emulate_ui.c`,
`SCREEN_NFC_EMULATE`) reached from the read flow. All of these screens are driven
by the `nfc_sim` model and LVGL animations; none currently drive the ST25R3916.

### Event-driven input

Each screen registers a handler with `ui_input_set_screen_handler()` and reacts
to `input_event_t` events (`INPUT_ACTION_PRESS` / `INPUT_ACTION_REPEAT` for
`INPUT_BTN_UP/DOWN/LEFT/RIGHT/OK/BACK`). The central input pump only calls a
handler while input is unlocked and no modal overlay is up, so screens carry no
per-frame edge-detection of their own. Held UP/DOWN auto-scroll via the REPEAT
action.

### Rotation-aware layout

Screens size and place elements with the live logical dimensions from
`ui_metrics.h` (`ui_screen_w()` / `ui_screen_h()`), which follow
`lv_display_set_rotation` rather than the fixed 240 x 320 panel constants, so the
layout survives rotation (e.g. the read screen clamps its dump-list Y against
`ui_screen_h()` and the footer height).

### Shared visual kit (`nfc_ui_common`)

`nfc_ui_common.h` provides the common look used by Read / Saved / Write /
Emulate: an accent header + underline (`nfc_ui_header`), a credit-card-style
panel that renders a card (`nfc_ui_card_panel`), an expanding concentric-ring
"broadcasting field" animation (`nfc_ui_field_create` / `nfc_ui_field_tick`), and
fire-and-forget speaker cues (`nfc_ui_play_sound`: a rising blip on tag found, a
tick on save). The Identify screen animates a per-technology checklist
(NFC-A / NFC-B / NFC-F / NFC-V).

### Simulation model (`nfc_sim`)

`nfc_sim.c` is the shared card record (`nfc_sim_card_t`: name, type, UID, ATQA,
SAK) plus a saved "library" (up to `NFC_SIM_MAX_SAVED` = 16 cards) persisted in
NVS and seeded with presets on first run. It can synthesize a random discovered
tag (`nfc_sim_random_card`), build a card from a specific template
(`nfc_sim_make_card`), and format UIDs as `DE:AD:BE:EF`.

## NFC Stack (`Applications/nfc/`)

The stack is a full reader/emulator implementation that targets the ST25R3916.

### Manager (`nfc_manager`)

A scan-task state machine over the driver:

```c
typedef enum {
  NFC_MANAGER_STATE_IDLE, NFC_MANAGER_STATE_SCANNING,
  NFC_MANAGER_STATE_READING, NFC_MANAGER_STATE_EMULATING,
  NFC_MANAGER_STATE_ERROR, NFC_MANAGER_STATE_COUNT
} nfc_manager_state_t;

hb_nfc_err_t nfc_manager_start(nfc_manager_card_found_cb_t cb, void *ctx);
void nfc_manager_stop(void);
nfc_manager_state_t nfc_manager_get_state(void);
```

Hardware must be pre-initialized with `highboy_nfc_init()`. The card-found
callback delivers an `hb_nfc_card_data_t`.

### Scanner / Reader / Listener

- `nfc_scanner` (`nfc_scanner_alloc/start/stop`) polls the field and reports the
  detected protocol list (`nfc_scanner_event_t`, up to
  `NFC_SCANNER_MAX_PROTOCOLS` = 4).
- `nfc_reader` implements the concrete read/write flows:
  `mf_classic_read_full()` (dumps all sectors into the global emulation card
  `s_emu_card`), `mf_classic_write_all()` (writes data blocks back, trailers
  guarded), `mfp_probe_and_dump()` (MIFARE Plus), `mful_dump_card()`
  (Ultralight), and `t4t_dump_ndef()` (Type 4 NDEF).
- `nfc_listener` starts card emulation from generic card data
  (`nfc_listener_start`) or from a pre-loaded MIFARE Classic dump
  (`nfc_listener_start_emu`).

### Persistence (`nfc_device`, `nfc_store`)

- `nfc_device` stores MIFARE Classic card profiles (with keys) in NVS namespace
  `nfc_cards`, up to `NFC_DEVICE_MAX_PROFILES` (8), with an active-profile
  selector for emulation and legacy generic-card wrappers.
- `nfc_store` stores generic card entries (name, protocol, UID, ATQA, SAK, and a
  protocol-specific payload up to `NFC_STORE_PAYLOAD_MAX` = 2048 B) in NVS, up to
  `NFC_STORE_MAX_ENTRIES` (16), plus NTAG/Ultralight pack/unpack helpers.

### Card families / protocols (`protocols/`)

| Family | Directory | Notes |
|--------|-----------|-------|
| Common | `common/` | APDU, crypto, RF, tag, ISO-DEP TCL layer |
| ISO 14443-A | `iso14443a/` | anti-collision, ISO-DEP, poller, NDEF, T4T + T4T emulation |
| ISO 14443-B | `iso14443b/` | reader + emulation |
| ISO 15693 (NFC-V) | `iso15693/` | reader + emulation |
| FeliCa (NFC-F) | `felica/` | reader + emulation |
| EMV | `emv/` | bank card / payment applications |
| LLCP / SNEP | `llcp/` | NFC P2P link + SNEP exchange |
| MIFARE | `mifare/` | Classic (+ emu, + writer), Ultralight, Plus, DESFire (+ emu), crypto1, nested attack (`mf_nested`), `mfkey`, key cache, key dictionary loader, known cards |
| Topaz / Type 1 | `t1t/` | Type 1 tag |
| Type 2 | `t2t/` | Type 2 tag emulation |

The supported protocol identifiers are enumerated in
`highboy_nfc_protocol_t` (`highboy_nfc_types.h`): ISO14443-3A/-3B/-4A/-4B,
FeliCa, ISO15693, MIFARE Classic / Ultralight / Plus / DESFire, ST25TB, and SLIX.

### Key dictionaries (`assets/nfc/dict/`)

MIFARE key dictionaries shipped as assets and loaded by `nfc_dict_loader`:
`mf_classic_default.dic`, `mf_classic_user.dic` (Classic key A/B lists) and
`mf_ulc_default.dic` (Ultralight-C keys).

## Driver: HighBoy NFC (`st25r3916`)

`highboy_nfc.h` is the driver front end for the ST25R3916 / ST25R3916B:

```c
esp_err_t highboy_nfc_init(const highboy_nfc_config_t *config);
void      highboy_nfc_deinit(void);
esp_err_t highboy_nfc_ping(uint8_t *out_chip_id);
esp_err_t highboy_nfc_field_on(void);
esp_err_t highboy_nfc_field_off(void);
uint8_t   highboy_nfc_measure_amplitude(void);
bool      highboy_nfc_field_detected(uint8_t *out_aux_display);
```

- Configuration is a `highboy_nfc_config_t` (SPI pins, host, mode 1, clock).
  `HIGHBOY_NFC_CONFIG_DEFAULT()` provides the ESP32-P4 reference wiring.
- The driver is split into `st25r3916_core.c`, `st25r3916_fifo.c`,
  `st25r3916_irq.c`, `st25r3916_aat.c` (antenna auto-tuning) and a HAL layer
  (`hal/hb_nfc_gpio.c`, `hb_nfc_spi.c`, `hb_nfc_timer.c`).
- Capacity constants live in `highboy_nfc_types.h` (UID up to 10 bytes for
  triple cascade, ATS up to 64 bytes, 512-byte hardware FIFO).
