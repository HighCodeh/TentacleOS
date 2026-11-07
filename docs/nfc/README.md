<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# NFC Application

The NFC feature has two halves in the tree:

1. An ST25R3916-based NFC stack under
   `components/Applications/nfc/` (manager, scanner, reader, listener, device /
   store persistence, and a full protocol tree), talking to the
   `st25r3916` driver (`components/Drivers/st25r3916/`, "HighBoy NFC").
2. An on-device UI suite under `components/Applications/ui/screens/nfc/`.

The integrated High Boy profile drives the ST25R3916 through native read,
scan, write, emulation, protocol-laboratory and APDU services. `nfc_sim` remains
as a shared card/library representation; its name does not mean that the native
screens synthesize hardware results. Physical RF operation still requires board
acceptance testing. See [High Boy integration](../highboy/README.md).

## Overview

- **App location:** `components/Applications/nfc/`
- **UI location:** `components/Applications/ui/screens/nfc/`
- **Driver:** `components/Drivers/st25r3916/` (`highboy_nfc_*`)
- **Target IC:** ST25R3916 / ST25R3916B (SPI mode 1, clock <= 6 MHz)
- **Card/library model:** `ui/screens/nfc/nfc_sim.c` (saved library persisted in NVS)

## Architecture

```text
NFC UI -> manager / reader / listener / protocol service / APDU lab
       -> ST25R3916 core -> shared SPI bus
       -> RF frontend arbitration with native LF RFID
```

The APDU workbench provides bounded command/response history and optional SD
traces. DESFire, NFC-V, NDEF and FeliCa screens use the shared protocol laboratory.
Their presence in the UI is not a claim that every operation has passed physical
acceptance tests.

## UI Suite (`ui/screens/nfc/`)

`ui_nfc_menu_open()` (`nfc_menu_ui.c`) builds the NFC menu. Selecting an item
switches to the matching screen via the `screen_id_t` enum in `ui_manager.h`.

| Menu item | Screen | File |
|-----------|--------|------|
| READ TAGS | `SCREEN_NFC_READ` | `nfc_read_ui.c` |
| SCAN / IDENTIFY | `SCREEN_NFC_SCAN` | `nfc_scan_ui.c` |
| EMULATE | `SCREEN_NFC_EMULATE` | `nfc_emulate_ui.c` |
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
| APDU WORKBENCH | `SCREEN_NFC_APDU` | `nfc_apdu_ui.c` |

The dedicated emulate screen (`nfc_emulate_ui.c`, `SCREEN_NFC_EMULATE`) uses
saved MIFARE dumps. The legacy `SCREEN_CARD_EMU` entry opens that same native
screen rather than animating a simulated card. Read, write, scan, bank-card and
emulation screens each register their own stop handler with the screen manager,
so navigation or resource preemption stops the owning worker.
Native operations run in background services and publish results to the UI.
The `nfc_sim` model carries card metadata and saved-library entries.

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

### Card/library model (`nfc_sim`)

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

### Classic nonce and passive transmitter fixes

`nfc_poller_classic_nonce()` is a dedicated initial AUTH exchange. The command
still transmits CRC-A, but its four-byte nonce response has no CRC. Only the
CRC-error indication is ignored for that exact response; timeout, collision,
parity/framing errors, FIFO overflow and partial final bytes remain failures.
The general transceive path retains strict error checking. This matches the
special response handling in the fork's Classic poller without weakening other
protocols.

Passive transparent TX allocates its RMT channel with the configured MOSI GPIO.
ESP-IDF v6.1 rejects GPIO -1 at allocation. Allocation saves the RMT output signal
and restores MOSI to SPI while holding the shared-bus and device locks. Only the
actual passive response routes MOSI back to RMT; cleanup restores SPI.

Regression commands:

```sh
python3 tools/nfc_api/test_nonce.py
python3 tools/nfc_api/test_transparent.py
```

### Fork parity status

Reference checkout: `flipperzero-firmware-wPlugins`, commit
`13ed5ca8d82cbe6c681e27f0a5e1200a2c0f03ae`. Protocol names alone do not establish
feature parity. Full toolkit equivalence has **not** been implemented or validated.

| Fork family | TentacleOS status / remaining work |
|---|---|
| ISO14443-3A/3B/4A/4B | Native polling and ISO-DEP exist; command and recovery coverage still needs differential testing |
| Classic | Reader, writer and emulator exist; complete Mini/1K/4K Flipper `.nfc` import and complete Classic export implemented; nonce and RMT allocation regressions fixed; physical repeated authentication/emulation remains to be validated |
| Ultralight | Reader plus Type 2 target; original Ultralight, EV1 11/21 and NTAG213/215/216 Flipper replay integrated; signature, counters, tearing flags, VCSL, NTAG ASCII mirror and AUTHLIM implemented; AES saved-file authentication/CMAC/RID and Type 2 session-copy persistence implemented; I2C target emulation and full security/physical parity remain incomplete |
| Plus | Native SL3 poller and listener, First/NonFirst authentication, MAC/encrypted multi-block reads, protected RAM writes and Flipper session-copy export integrated; administrative commands, complete access-condition parity and physical interoperability remain unverified |
| DESFire | Structured application/file reader, typed v4 persistence, detail pages and saved identity emulation implemented; authenticated application emulation is outside the fork baseline; physical interoperability pending |
| FeliCa | Structured Standard/Lite reads, known-key Lite authentication, typed files and saved-profile listener; physical RF validation pending |
| ISO15693 / SLIX | Full ordinary-memory reads saved within library capacity; SLIX session/password authentication, privacy unlock, SLIX2 protection info and signature retrieval implemented; transparent IRQ/RMT ISO15693/SLIX listener and read-only saved-card/Flipper-file replay implemented; RF interoperability remains unverified |
| ST25TB | Ordinary blocks and system/OTP block saved; confirmed ordinary-block writes with UID check and read-back implemented; OTP programming is not exposed |
| NTAG4xx | 28/29-byte version identification, typed profiles, details and ISO-DEP identity replay; RF validation pending |
| Type 4 | NDEF read exists; write API and target engine are not integrated as general saved-NDEF UI workflows; typed persistence remains missing |
| EMV | PPSE, PDOL/GPO, AFL records, metadata/counters/logs, typed fork-compatible files and paged bank-card UI; RF validation pending |
| Texkom | Transparent RMT receive, TK13/TK15/TK17/MMBIT codecs, library persistence and passive transmit implemented; physical RF validation remains pending |

The [functional comparison of DESFire, FeliCa, NTAG4xx, Type 4 and EMV](FORK_COMPARISON.md)
records the audited call paths, fork scope, persistence gaps and acceptance criteria.
It also identifies valid Type 4 files rejected by generic duplicate-key validation.
The comparison is complete for those families; implementation parity is not. Host
tests cover named regressions, not this entire parity matrix.

### Classic Flipper files

The emulation browser accepts complete Classic `.nfc` files in
`/sdcard/nfc/dumps`, with arbitrary basenames. Existing `mf_*.bin` 1K dumps
remain supported. Flipper file versions 3 and 4 with Classic data format 2
support Mini, 1K and 4K, including four- and seven-byte UIDs. ATQA is converted
from the file's display order into the radio's byte order. Sector trailers use
four blocks below sector 32 and sixteen blocks thereafter.

The loader validates metadata, unique block indices, exact block sizes and
complete geometry before exposing a card to the listener. Unknown bytes (`??`),
missing/duplicate blocks, unsupported versions/types, oversized lines and
unsupported UID lengths are rejected; they are never filled with invented data.
An invalid file clears the output profile and is omitted from the emulation list.
The existing native binary format cannot preserve a seven-byte UID or ATQA/SAK;
use `.nfc` for those cards.

Saving a complete Classic 1K read with both keys known for all sectors now also
writes a companion `.nfc`, retaining the `.bin` and `.keys` outputs. The export
uses the acquired UID/ATQA/SAK and reconstructed keys. It refuses to overwrite an
existing `.nfc`; failed writes remove only the new partial file. Reads with unknown
keys retain the existing binary/key-report behavior and do not claim a complete
Flipper export. The public export API also supports complete Mini/4K profiles.
Both formats may appear in the browser when a dump has both extensions.

`python3 tools/nfc_api/test_flipper_file.py` compiles the production file module
with ASan/UBSan and checks geometry, large-sector key offsets, UID/ATQA, export
round trips, overwrite protection and malformed/incomplete files. Actual RF
emulation still requires board testing. Import does not add 1K EV1/2K support or
implement the missing listeners in the parity table.

### NFC-V transparent listener

The NFC-V/SLIX worker now receives reader modulation on the ST25R3916 IRQ pin,
following the transport architecture in the local Flipper fork. It does not use
the legacy FIFO target emulator. `hb_nfc_v_rx` timestamps GPIO edges into a
bounded ISR ring; `hb_nfc_v_codec` reconstructs fc/128 samples and decodes
1-of-4/1-of-256 requests and standalone EOF slot markers. Partial, malformed and
overflowed frames are discarded. Acquisition waits 10 ms for a new request and
bounds an active capture to 250 ms; very long 1-of-256 requests are not supported.

`hb_nfc_spi_target_v_exchange` owns the SPI bus continuously from entering
transparent mode through capture, protocol processing and the RMT reply. The
callback cannot access SPI or block. Responses include CRC and use high/low-rate
single-subcarrier waveforms, including the doubled rates for Fast Inventory Read. The nominal response deadline is 319 microseconds
after the EOF edge; a callback more than 50 microseconds late drops the response.
Routing and ownership are restored on errors. Failed RMT shutdown prevents buffer
reuse until cleanup succeeds. The worker reserves the shared NFC/LF frontend,
keeps the local RF transmitter off and resets protocol sessions on field loss.

The ISO15693 listener implements inventory masks and 16 slots, addressing,
selection, quiet/reset, system information, ordinary block reads/writes/locks,
security status, AFI/DSFID operations and deferred Option acknowledgments.
SLIX supports random challenges, authentication using supplied passwords, privacy,
password changes, protection checks/configuration, NXP system information and
stored signature responses. State changes affect session RAM; mutable file sessions
can explicitly save a new copy through the emulation screen. Unsupported commands
return protocol errors; dual subcarriers and protocol extensions are unsupported.
SLIX2 now includes the block-79 counter, EAS commands A2–A7, password lock B5,
64-bit protection BB, page-protection lock B7, authenticated virtual destroy B9
and persistent quiet BC. EAS alarms support ID masks and Option acknowledgments.
Counter reads bypass page protection; increment/preset enforce their separate
password rules. Persistent quiet survives short field interruptions; this emulator
uses a deterministic two-second expiry, not an analog retention model.
Implementation follows the [NXP SL2S2602 Rev. 4.2 datasheet](https://www.nxp.com/docs/en/data-sheet/SL2S2602.pdf),
including corrections where the fork has placeholder handlers.

Inventory Read/Fast Inventory Read A0/A1 support UID masks, 16 slots,
addressed/selected access, UID truncation, extended EAS/data/quiet options and
high/low double-rate replies. Legacy inventory-page variants remain unsupported;
multiple-block writes are rejected for SLIX. The wider family parity matrix above
remains applicable; these additions do not complete it.

Saved native NFC-V/SLIX entries now open the listener from the library. Their v1
format lacks access metadata, so replay is read-only. The emulation browser also
imports Flipper v4 `ISO15693-3` and `SLIX` `.nfc` files from `/sdcard/nfc/dumps`.
It checks UID byte order, geometry, complete memory/security data, duplicate
fields and optional password/protection/signature metadata. Missing passwords
remain unknown. `AcceptAllPasswords` is not accepted. In the emulation browser, OK starts read-only replay; RIGHT starts NFC-V with
session-only RAM writes. The status label distinguishes these modes. Closing and
reopening reloads the original file, including after virtual destroy. Credentials
and mutations are never written back to the dump.

Optional High Boy metadata fields preserve details absent from the fork format:
`EAS Enabled`, `EAS ID` (two hex bytes), `EAS Protected`, `AFI Protected`,
`Password 64`, and `Password Locks` (five 00/01 bytes, read/write/privacy/destroy/EAS).
The four EAS fields must appear together. Without them EAS is unavailable rather
than inventing the original tag's state. Booleans use `true`/`false`; duplicate,
partial and malformed metadata are rejected.

Host checks: `test_v_codec.py`, `test_v_rx.py`, `test_v_listener.py`,
`test_v_file.py` and `test_transparent.py` in `tools/nfc_api` cover edge decoding,
long quiet intervals, malformed frames, ring overflow, CRC, slot scheduling,
password/protection state, EAS masks/locks, counter authorization and wrap,
persistent-quiet expiry, virtual destroy, inventory-read response layouts,
all four waveform rates, file validation and bus cleanup with ASan/UBSan.
These do not establish physical interoperability: IRQ polarity, ISR latency,
clock drift and actual RMT frame-delay timing still need measurement on High Boy.
The 250 ms bound also limits cancellation latency during active capture.

### ST25TB ordinary-block writing

After a successful ST25TB read, RIGHT opens a hexadecimal input in the format
`BB:DDDDDDDD` (block address followed by four bytes in wire order). Submitting
this input only stages the operation. The screen shows the pending block/data;
OK writes it and BACK cancels it. The worker reselects the tag and compares all
eight UID bytes with the last read before transmitting anything destructive.

Write commands are sent once, without automatic retry. ST25TB does not ACK a
write: the expected receive timeout is followed by at least seven milliseconds
of programming time and a CRC-checked read-back. Success requires an exact byte
match. A timeout by itself never means success. After writing, rescan to save the
updated memory. A stopped write requires a rescan because cancellation cannot
undo a command already sent. Block bounds come from the detected card; the
system/OTP block `FF` is excluded from this ordinary-memory editor.

`python3 tools/nfc_api/test_st25tb_write.py` covers command bytes, staging,
one-shot execution, tag mismatch, cancellation before write, bounds, unexpected
responses, read-back mismatch and injected transport failures.
`test_st25tb_write_ui.py` checks that staging does not write, BACK cancels, OK
executes once, and a timer refresh cannot hide pending confirmation. Physical write
acceptance remains to be tested with a supported tag.

### Reader validation and additional protocol screens

The NFC protocol menu includes dedicated ST25TB, ICODE SLIX and NTAG4xx
readers. These are read-only operations using the existing exclusive NFC worker
and its cancellation/field-off lifecycle. Their current scope is:

| Screen | Implemented reading | Limits |
|---|---|---|
| ST25TB | Initiate, chip-ID select, UID, geometry, ordinary blocks and system block FF; saved in NFC library | Fails without saving if any block is missing; RIGHT stages an ordinary-block write with confirmation; OTP writing is excluded |
| ICODE SLIX | Inventory, complete memory/security capture to `.nfc`, SLIX2 protection/signature, supplied passwords by purpose, confirmed data restore with readback | Signature retained but not cryptographically verified; restore requires matching UID/geometry and does not change keys, locks or the SLIX2 counter; RF validation pending |
| NTAG4xx | NXP GetVersion identification and Type 4 NDEF reading | Protected NDEF requires credentials; successful version identification is reported even when the optional NDEF capability file is unavailable |

The local `flipperzero-firmware-wPlugins` readers were used to check wire
commands and identification. This is not feature parity with all Flipper
pollers/listeners. Texkom now uses transparent MISO capture instead of the
FIFO reader path; its UI exposes reading and passive emulation.

Reader error handling now rejects malformed or oversized frames rather than
reporting truncated success. In particular:

- Classic verifies the card authentication answer and encrypted parity. Ordinary
  block reads retry once after reselection and authentication; cached key B also
  reads blocks left unread by key A. Seven-byte UIDs use their final four bytes
  for Crypto1 initialization.
- ISO-DEP bounds chaining and WTX, checks block sequencing, rejects response
  buffer overflow, and returns failure for interrupted chains. Type 4 reads
  validate the supported capability-container layout and NDEF file bounds.
- FeliCa uses Polling request/response codes 00/01 and validates frame length,
  returned IDm, status and block counts before copying data.
- ISO15693 checks response CRC and optional system-information bounds.
- Ultralight validates read/PACK CRCs and address limits; Ultralight-C uses
  status-prefixed authentication replies and the AF continuation command.
- DESFire emits correctly sized empty native APDUs and bounds additional-frame
  responses. EMV BER-TLV traversal has a fixed nesting bound. Type 1 reads reject
  receive overflow and FIFO read failures.

Host regression checks (ASan/UBSan) are available via:

```sh
python3 tools/nfc_api/test_classic_auth.py
python3 tools/nfc_api/test_iso_dep.py
python3 tools/nfc_api/test_readers.py
python3 tools/nfc_api/test_new_readers.py
```

These check authentication faults, malformed/chained ISO-DEP responses,
FeliCa wire framing and ST25TB read sequences. They do not establish RF timing
or interoperability with physical cards. Validate each family on hardware,
including removal/re-presentation, interrupted reads, repeated reads without
reopening the screen, and returning to another protocol after cancellation.

### Texkom and persistent memory reads

The Texkom screen captures rising-edge intervals through RMT RX on the configured
SPI MISO pin. RX uses a static DMA buffer, bounded 20 ms captures and restores the
SPI routing and ST25 registers on exit. TK13/TK15 pair decoding, TK17 pulse-ratio
decoding and MMBIT use their respective checksum rules. Capture failures do not
publish a decoded card. The worker retries bounded captures and supports cancellation.

A valid eight-byte frame is saved to the NFC library. RIGHT on the read screen
emulates the last successful read; selecting a saved Texkom entry and choosing
Emulate loads and validates that entry, including after reboot. Passive transmission
uses RMT on the board's configured MOSI pin, 64-carrier-cycle high pulses and
protocol-specific low intervals. The worker restores registers and releases the
field/no-sleep resources on stop or failure. These are implementations awaiting
physical tag/reader validation, not an RF interoperability claim.

SLIX RIGHT accepts eight hexadecimal digits in wire byte order, optionally prefixed
with `r:` (read), `w:` (write), `p:` (privacy), `d:` (destroy authentication) or
`e:` (EAS/AFI). An unprefixed password retains the privacy behavior. Entries
accumulate by purpose; OK reads, UP clears all credentials, and leaving the screen
clears the working credentials. Get Random Number / Set Password authenticates
only; it does not replace keys or execute destroy. Failed authentication aborts
the operation and passwords are never logged.

Successful reads save `/sdcard/nfc/dumps/slix_<UID>_<index>.nfc`, including memory,
block security status, confirmed passwords, and SLIX2 signature/protection/lock
metadata. These files can be selected by the existing NFC-V/SLIX emulation picker.
Treat exported dumps as sensitive: they contain any supplied passwords confirmed
by the tag. Unknown passwords and unreadable EAS state are not fabricated.
A failed or cancelled read does not publish a partial dump. Legacy native v1
library entries remain importable; new SLIX reads use the richer file format.

DOWN stages restoration from a dump filename (empty selects the last successful
read). OK explicitly confirms the write; BACK cancels it. The physical tag must
match the saved UID and geometry. Enter target passwords with RIGHT before staging
if needed; credentials from the source file are not implicitly applied. Restore
skips identical blocks, checks target locks before changed blocks, writes one
block at a time and verifies it by reading back. It stops at the first failure
without replaying an uncertain write and reports verified blocks. Passwords,
AFI/DSFID, protection settings, locks and the SLIX2 counter block 79 are not written.
Interrupted restoration can leave earlier blocks changed. Host tests cover these
paths; physical RF interoperability remains to be validated on the board.

The native NFC library payload contracts added here are:

- Texkom: the validated eight-byte wire frame. TK13 and TK15 have the same stored
  frame format and use the TK13 waveform when reloaded, as in the reference fork.
- ST25TB v1: version byte, little-endian 16-bit block count, block-size byte (4),
  four-byte system/OTP block, then ordinary blocks in address order.
- ISO15693/SLIX v1: version byte, little-endian 16-bit block count, block-size byte,
  DSFID, AFI, IC reference, reserved zero byte, then ordinary blocks. Block lock
  statuses, signature and vendor-specific metadata are not part of this payload.

Saved NFC-V entries open the read-only transparent listener. Saved ST25TB entries
still show an unavailable message for emulation instead of opening a Classic dump.

The library has a 2048-byte payload limit. Geometry that exceeds that capacity,
short blocks, cancellation and any failed block prevent saving; a partial read is
never presented as a complete dump. This native format is not Flipper `.nfc`.

Behavior was compared with the pinned fork's `lib/nfc/protocols/texkom`,
`targets/f7/furi_hal/furi_hal_nfc_texkom.c`, `lib/digital_signal/presets/nfc/texkom_signal.c`
and `lib/nfc/protocols/slix`. The ESP-IDF HAL, codecs and command adapter are local
implementations; no Furi runtime dependency or vendored SDK copy was added.

Additional host checks:

```sh
python3 tools/nfc_api/test_texkom.py
python3 tools/nfc_api/test_transparent.py
python3 tools/nfc_api/test_slix.py
python3 tools/nfc_api/test_v_memory.py
```

These exercise checksum vectors, malformed pulse bounds, RX/TX cleanup faults,
password byte order and addressing, response validation and all-block persistence
failure paths. They cannot establish antenna performance or hardware timing.

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

## Shared antenna and failed shutdown

NFC and native LF RFID share a physical antenna. The [RF frontend](../rf_frontend/README.md)
arbitrates the stages using a statically initialized critical section.
An unsuccessful NFC ON or OFF transfer may leave TX active. The core keeps the
antenna reserved and marks the field uncertain until an OFF write succeeds.
Further field-on requests fail with `ESP_ERR_INVALID_STATE` while uncertain.
Oscillator and RX-gain startup failures use the same checked OFF recovery.
`st25r3916_core_deinit()` now returns `esp_err_t`; it retains the initialized
transport when OFF fails so callers can retry. The public `highboy_nfc_deinit()`
wrapper retains its initialized flag on that failure. A successful SPI transfer
is the software confirmation boundary; electrical field-off still needs hardware
validation.

## Synchronous companion discovery

`nfc_manager_poll(technology, out_card)` reuses the manager's A/B/F/V probes
without starting the scanner task. The caller initializes the driver, owns the
antenna and performs field shutdown. Its output is a native card descriptor;
setup errors propagate, discovery timeout/no-card maps to `ESP_ERR_NOT_FOUND`,
and optional activation/geometry failures retain the original discovered card.
The background manager uses the same probe implementations. The companion's
bounded [capture commands](../host_link/rf-commands.md) call this interface under
an exclusive RF reservation and a no-sleep reference.

Native apps and scripts can now use the [ABI 1.10 discovery calls](../app_sdk/README.md#native-nfcrfid-api-abi-110).
They reuse the native poll and stable RF worker, while later ABI revisions add bounded block operations and owned emulation. UI functionality is unchanged.


## Shared Classic block operations

ABI 1.17 and authenticated companion operations 04/05 expose bounded Classic 1K
Key A block access through `host_rf_nfc_block`. Native apps and JS/Lua use the
same worker and antenna/power cleanup. Writes preserve manufacturer/trailer
blocks and read back data before success. Four-byte UID/SAK 08/88 support is
explicit; discovery alone does not imply support for a different card geometry.
See [the app contract](../app_sdk/README.md#nfc-classic-block-access-abi-117)
and [companion protocol](../host_link/rf-commands.md). Owned emulation follows in ABI 1.22 below; electrical/card acceptance remains unverified.


## Owned emulation (ABI 1.22)

`nfc.emulate_start(uid, 4, &session)` copies a four-byte UID into a volatile,
zero-filled Classic 1K image with ATQA 04/00, SAK 08 and unknown keys. It exposes
UID/selection emulation, not a saved dump or a clone of card contents. It never
reads profiles or persists writes. Start/stop require radio-tx; session IDs belong
to the originating app/script task and are not reused before reboot. Binding
teardown cancels the session without retaining app buffers or callbacks.

A persistent firmware worker reserves the shared antenna for its whole lifetime,
including failed startup/shutdown. Failed OFF/deinit retains that reservation and
the no-sleep reference until cleanup succeeds. Foreign/stale IDs fail. Stop is
permitted after cooperative cancellation; timeout leaves cleanup running.

The native listener now also copies its card into firmware storage and owns its
antenna, initialization, emulation and cleanup on one persistent task. This
prevents a UI listener and app emulator from overwriting one another. The legacy
void stop waits up to three seconds; `nfc_listener_is_active()` remains true while
startup or cleanup is pending. Native target configuration/listen/start checks
all SPI/IRQ operations; foreign emulator mutations are rejected before changing
shared state. Host tests cover ownership, cancellation, retained cleanup and
fault injection at each target startup operation. Physical RF acceptance remains
unverified.

## Classic emulation transport

Classic encrypted responses and four-bit ACK/NACKs use passive transparent
modulation on MOSI. They must not use the target FIFO with `NO_TX_PAR`: ST
confirms that disabling generated parity through that flag is unsupported in
card emulation ([ST technical response](https://community.st.com/st25-nfc-rfid-tags-and-readers-54/st25r3916b-mifare-classic-emulation-issue-152252)).
The earlier FIFO length correction alone was therefore insufficient.

The local Flipper fork's `furi_hal_nfc_iso14443a_listener_tx_custom_parity`,
`iso14443_3a_signal` and Classic listener were used as behavioral references.
Highboy uses its own ESP-IDF implementation: a preallocated RMT DMA channel,
40 MHz timing with fractional carrier-period accumulation, Manchester-coded
106 kbit/s data and 847.5 kHz subcarrier bursts. Packed encrypted parity bits
are emitted unchanged. The bounded frame covers at most 162 bits (18 bytes plus
parity), with a start symbol and a low end interval. Full SPI bus ownership and
the shared display/radio lock are held while MOSI is temporarily routed to RMT;
MOSI is restored and transparent mode is exited before releasing the bus.
Failed transmitter cleanup retains the initialized driver for a cleanup retry.
No firmware-generated reader RF field is enabled for these target replies.

HALT and rejected authentication enter hardware SLEEP, allowing WUPA to restart
selection. Loss of field and recoverable RF transaction failures rearm SENSE;
only failure to rearm the hardware terminates the owning listener. Configuration
and interrupt clearing precede GOTO_SENSE, so cleanup cannot discard a newly
arriving request. RXE observed during selection is retained for the next worker
step. Cipher and pending operation state are reset between sessions.

Host checks:

- `python3 tools/nfc_api/test_emu_tx.py`: encrypted parity and cipher advancement.
- `python3 tools/nfc_api/test_transparent.py`: waveform timing, bounds, bus ownership
  and cleanup with injected transport failures (ASan/UBSan).
- `python3 tools/nfc_api/test_emu_states.py`: repeated selection, HALT and recovery.
- `python3 tools/nfc_api/test_startup.py`: startup bus-failure propagation.
- `python3 tools/nfc_api/test_plus_poller.py`: full/partial reads, all geometries,
  administrative/configuration capture, security-level classification, cancellation,
  UID checks and partial write failure without replay.
- `python3 tools/nfc_api/test_sessions.py`: worker ownership and cancellation.

Physical acceptance is still required: repeat selection/authentication/read and
remove/reintroduce the reader field without closing the emulation screen. Verify
the transparent waveform and response latency on the board if the reader still
rejects replies. `AUTH OK` records local challenge verification and transmission;
it does not confirm that the reader accepted the answer or read the full dump.

## IDF v6.1 cryptography

TF-PSA-Crypto removed DES, including 3DES, as obsolete algorithms; this was not
a header rename. Migrating to the public PSA Crypto API does not restore them.
See the upstream [migration guide](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.0.0/docs/1.0-migration-guide.md#removed-obsolete-cryptographic-mechanisms).

Legacy NFC card protocols still require DES/3DES. Substituting AES locally would
break interoperability: the card and its selected authentication protocol must
also support AES. DES/3DES is therefore retained only for legacy compatibility,
not as the choice for new cryptographic protocols.

The firmware uses the isolated Mbed TLS v3.6.6 DES/3DES module in
`protocols/common/legacy_des`, preserving its upstream license notices. This
does not bring back an older TLS stack or enable DES in the SDK configuration.
Its [provenance and maintenance instructions](../../firmware_p4/components/Applications/nfc/protocols/common/legacy_des/README.md)
document the source and local adaptations. The host regression
`python3 tools/nfc/test_legacy_des.py` checks 12 upstream DES/3DES ECB/CBC vectors;
these checks do not replace physical card interoperability tests.

NFC AES-ECB, AES-CBC and AES-CMAC now use the public PSA Crypto API through
`Service/firmware_crypto`. CBC retains the protocol's explicit IV, no-padding
semantics and output chaining IV, including in-place decryption. CMAC errors
are propagated instead of reporting a successful MAC. The active NFC path no
longer includes private SDK primitive headers. P4 uses software AES because of
the pinned SDK's incomplete PSA hardware algorithm coverage; see
[SDK compatibility](../highboy/README.md#esp-idf-v61-compatibility).

`python3 tools/crypto/test_psa.py --idf "$IDF_PATH"` validates the production
NFC AES/CMAC code and CBC chaining against independent vectors with the actual
SDK software PSA backend. DES/3DES remains covered by its separate legacy test.

## ISO-DEP target block validation

The Type 4 / DESFire target consumes RX-end interrupts and the full 10-bit FIFO
byte count. It rejects oversized and incomplete FIFO frames instead of truncating
them. CID headers are checked before access; NAD is unsupported. Chained APDUs
are bounded to 300 bytes, including the exact-capacity case. Overflow ends the
session without executing or acknowledging a truncated command.

The listener tracks the expected I-block number. Retransmitted I-blocks replay
the cached response or chaining acknowledgement without executing the APDU twice.
Chaining acknowledgements carry the next expected block number, and responses
preserve the CID flag. RATS clears transport and application state for a new
session. Responses are fragmented according to the reader's RATS frame size,
including CID overhead. R-ACK advances the response; retransmitted acknowledgements
and R-NAK replay the cached fragment without rerunning the application command.
CRC-A is checked before dispatch. Application traffic requires RATS; the default
ATS advertises 106 kbit/s, CID support and no NAD. Four- and seven-byte target UIDs
use their respective passive-target memory layouts.

Field loss, deselection and a new RATS reset transport and application sessions.
TX timeout rearms SENSE; SPI failures are exposed to the owning worker for cleanup.
The Plus listener uses this transport. NFC-V physical reception remains separate
and is not implemented by these ISO14443-A changes.

Run `python3 tools/nfc_api/test_t4t_blocks.py` for production-code regression
checks with ASan/UBSan (CID, retransmission, chain bounds and response capacity).
Physical repeated-reader interoperability still needs validation on the board.


## MIFARE Plus SL3

The native poller now uses AuthFirst `70`, AuthNonFirst `76`, continuation `72`
and status `90`. Key addresses are little-endian. Authentication verifies the
rotated reader nonce before accepting the session. Session keys use AES-derived
nonce material; secure messaging includes TI and separate read/write counters,
with odd-byte CMAC truncation. Received MACs are verified before plaintext is
returned. Authentication state is cleared after uncertain exchanges, invalid MACs
or exhausted counters; writes are not retried automatically.

The implementation was compared with the local fork identified above and NXP's
[AN12706](https://www.nxp.com/docs/en/application-note/AN12706.pdf). Host vectors
independently calculate AES/CBC/CMAC messages rather than deriving expected values
from the production implementation.

`mfp_listener_exchange()` provides callback-backed SL3 authentication, one-to-fifteen-block
plain/MAC or encrypted reads and single-block writes, optional GetVersion chaining and optional
signature responses. Key resolution must succeed; unknown keys never become zero
keys. Ordinary blocks are restricted to the authenticated sector, and callbacks
supply additional policy. A missing write callback rejects writes. Callers own
callback data until the listener worker finishes cleanup.

The emulation browser also accepts Flipper Version 4 `Mifare Plus` `.nfc` files in
`/sdcard/nfc/dumps`, with SL3/data-format version 1, 1K/2K/4K geometry and four- or
seven-byte UIDs. Duplicate, missing or out-of-range entries are rejected. Entries
containing only `??` remain unavailable; mixed known/unknown byte entries are
rejected. Captured ATS fields are validated, then replaced by this transport's
106 kbit/s/CID capabilities.

File-backed replay defaults to **read-only**. RIGHT in the Emulate browser selects
Plus RAM writes; OK stops the worker and saves a unique `.session-N.nfc` copy,
while BACK discards the session. Saving waits for worker cleanup before accessing
callback-owned memory. The original dump is never overwritten. The overlay reports
Plus running/stopped state instead of showing stale Classic statistics.

Ordinary-block access is limited to the authenticated sector and recovered key.
The containing trailer must be known with valid redundant access bits. The
ordinary data access matrix distinguishes Key A/Key B and the five-block access
groups of large sectors. Invalid or unknown access conditions deny access.
Trailer reads mask key bytes. In RAM-write mode, trailer access bytes and sector
AES keys follow their Key A/Key B write permissions. Invalid replacement access
bits and manufacturer-block writes are rejected. Known administrative keys
9000/9001/9003/9004 authenticate; configuration B000–B003 and administrative key
updates are supported in the session image, matching the fork's authenticated
replay policy. A successful authorized write can populate a
previously unknown ordinary block in the session image. This does not implement
all Plus-specific configuration/security policies.

The SL3 listener validates the MAC and every requested block before replying to
multi-block reads; it rejects zero/oversized ranges, unavailable blocks and sector
crossings. Encrypted responses use one CBC operation over the complete payload,
with one response MAC and read-counter increment per command. ISO-DEP fragments
the response to the reader's negotiated frame size.

Export supports all three geometries and preserves unknown data/keys as `??`,
card type, version, signature, and optional administrative-key/configuration
entries. Administrative keys/configuration participate in replay without bypassing
ordinary sector permissions. ATS continues to advertise this
transport's capabilities. Export failures leave the session available for retry.

The requested scope is the local fork's MIFARE Plus feature set, not every NXP
hardware command. Like that fork, the listener implements SL3 rather than SL1/SL2
or value operations. Administrative replay does not claim all silicon security
policies. RF interoperability and repeated reads still require board tests.

**NFC > MIFARE Plus** reads the selected card. RIGHT accepts a 32-hex-digit AES
candidate; UP clears it. Reads try that candidate, zero/FF keys and up to the
64-candidate limit from `/sdcard/nfc/assets/dict/mf_plus.dic` (flash fallback
`/assets/nfc/dict/mf_plus.dic`). Each sector's A/B keys and the four administrative
keys are attempted. Reads support encrypted and plain/MAC responses, preserve
unread bytes/keys as unknown, and save a unique `plus_N.nfc` in NFC dumps.
Version/signature and readable configuration blocks are captured. SL0/SL1/SL2
identification is informational only; no SL3 memory operation is attempted.
Unconfirmed cards are not classified as Plus solely because SAK is 20.

DOWN selects a dump filename for write-back (empty uses the last saved Plus read).
The confirmation screen requires OK; BACK cancels. Writes require matching UID,
skip manufacturer/trailer/unknown blocks, and use recovered sector keys. The
screen reports the number of acknowledged blocks; it does not imply every block
was writable. Default writes are encrypted; prefix the filename with `plain:`
for plain/MAC writes, including `plain:` alone for the last read. Failed writes
stop without automatic replay. Leaving the screen clears the manually entered
key. Saved dumps include recovered keys, as the fork's format does.

Validation:

- `python3 tools/nfc_api/test_plus.py`: production poller/listener with independent
  wire vectors, First/NonFirst authentication, counters, plain/encrypted exchanges,
  forged authentication reply, tampered write rejection and recovery. Requires
  Python `cryptography` and OpenSSL development files (`OPENSSL_PREFIX` overrides
  `/opt/homebrew/opt/openssl@3` on macOS).
- `python3 tools/nfc_api/test_plus_file.py`: strict dump parsing, unknown data,
  sector restrictions, access bits, RAM writes, masked trailers, 1K/2K/4K
  export/import, administrative metadata and write-failure propagation.
- `python3 tools/nfc_api/test_plus_poller.py`: full/partial reads, all geometries,
  administrative/configuration capture, security-level classification, cancellation,
  UID checks and partial write failure without replay.
- `python3 tools/nfc_api/test_sessions.py`: worker ownership, startup failure and
  cancellation for Classic and Plus.
- `python3 tools/nfc_api/test_t4t_blocks.py`: response fragmentation, CID,
  retransmissions and chained APDU bounds.

These host checks use ASan/UBSan. Successful compilation and host tests do not
establish physical reader compatibility or full toolkit parity.

### NTAG Type 2 saved-file emulation

The emulation browser accepts complete Flipper v4 `NTAG/Ultralight` profiles
whose `NTAG/Ultralight type` is NTAG210, NTAG212, NTAG213, NTAG215, NTAG216, Mifare Ultralight,
Mifare Ultralight 11 or Mifare Ultralight 21. Place files in
`/sdcard/nfc/dumps` and select them with OK. The importer validates seven-byte
UID, ATQA/SAK, geometry, unique page indices and all memory bytes. Partial reads,
unknown bytes, duplicate pages and unsupported variants are rejected. Files are
reloaded and the UID rechecked immediately before starting.

OK starts read-only replay. RIGHT enables session-only writes for Type 2 and NFC-V
profiles. The overlay identifies the selected mode; closing emulation discards
changes and never overwrites the dump. Captured password/PACK
pages do not establish knowledge of credentials; password authentication is not
enabled for imported profiles. Protected reads remain protected. Optional captured `Signature`, `Mifare version`, `Counter 0/1/2`, `Tearing 0/1/2`
and `Failed authentication attempts` fields are validated and retained. Duplicate,
malformed and overflowing metadata is rejected. EV1 profiles require the captured
version. Missing signatures or counters are not synthesized.

READ_SIG returns the captured 32-byte signature. NTAG READ_CNT exposes counter 2
only when enabled and permitted by the counter password-protection bit. A successful
READ/FAST_READ increments this counter once per field presence, saturating at
0xFFFFFF; HALT/reselection do not reset that allowance. This automatic increment
changes RAM only, even in read-only replay. The response uses the pre-increment
counter, matching the reference listener. Field loss permits another increment.

NTAG ASCII mirroring substitutes UID, counter or `UIDxCOUNTER` within configured
user-memory bounds, with byte offsets and partial-page reads. An unavailable or
protected counter disables counter-only mirroring and reduces combined mirroring
to UID. Combined data that cannot fit also falls back to UID when it fits. Mirror
substitution never changes stored bytes or exposes protected credential pages.

EV1 supports three captured 24-bit counters, INCR_CNT with overflow rejection,
CHECK_TEARING and VCSL. Explicit counter increments are rejected in read-only mode; RIGHT enables
updates to counters and tearing status in RAM.
NTAG accepts CHECK_TEARING only for counter 2. The original Ultralight profile does
not advertise EV1/NTAG-only commands.

The mutable engine enforces AUTHLIM, configuration-page locking and static/dynamic
block-lock bits. Successful password authentication resets the attempt count;
field loss does not clear a lockout. These credential operations require explicit
known credentials in the engine configuration; importing masked password bytes
never enables authentication. Writing both password and PACK pages in writable mode establishes known
credentials for that session. Updating already-known pages refreshes them.
Writing only one unknown credential does not enable authentication. Physical interoperability requires board tests.

Type 2 reception now waits for RX completion, validates FIFO status and CRC-A,
and uses a single IRQ snapshot. Startup enters SENSE; field loss clears session
state, and TX timeout rearms listening instead of leaving a stale exchange.
Transport failures stop the worker through its existing antenna ownership and
cleanup path. Configuration and startup propagate bus errors.

READ rolls over at memory/protection boundaries, and READ/FAST_READ mask secret
pages. COMPAT_WRITE consumes its sixteen-byte second frame but writes only the
first four bytes to the addressed page. NTAG AUTH0/ACCESS offsets and static/dynamic
lock addressing were corrected. The existing UL-C handler now includes the
continuation/status bytes in its 3DES exchange; UL-C replay uses the same browser.

Validation:

```sh
python3 tools/nfc_api/test_t2t_memory.py
python3 tools/nfc_api/test_t2t_file.py
python3 tools/nfc_api/test_sessions.py
```

These host tests do not validate RF timing. Repeated selection, removal and
re-presentation with a physical reader still require testing on the board.

### Ultralight-C saved-file emulation

The Type 2 file browser accepts 48-page `Mifare Ultralight C` Flipper v4 dumps.
A complete read supplies the saved 3DES key; a dump marked with 44 pages read
never enables authentication from placeholder key bytes. All 48 page records
must be present. OK starts read-only; RIGHT permits session writes. During a
mutable Type 2 session, OK stops and saves a numbered `.session-N.nfc` copy;
BACK discards the changes. The source file is never replaced. A failed save
keeps the stopped session available for an OK retry. The worker captures memory,
counters, tearing flags and authentication attempts before releasing the emulator.
Explicit `Highboy known secrets` metadata preserves partially known keys without
treating unread password bytes as valid zero credentials. Files are written through
the storage atomic-write API after reserving a unique destination.

AUTH0/AUTH1 govern access even when the key is unknown. Key pages cannot be
read over RF, including after authentication; READ wraps at page 44. The
Ultralight-C lock map covers user groups, counter, configuration and key pages.
Starting or failing authentication clears previous authentication privileges.
These rules follow the [NXP MF0ICU2 datasheet](https://www.nxp.com/docs/en/data-sheet/MF0ICU2.pdf).

UL-C counter writes initialize the 16-bit value, then increment using the low
nibble, rejecting overflow. The pending value becomes readable after RF field
loss; reselection alone does not commit it. Zero increments preserve the value.
Key writes use the physical page byte order, while the stored session image uses
the Flipper key order. A key becomes usable after reactivation or field loss,
and only once all sixteen bytes are known. Read-only replay rejects both paths.

Validation includes `test_ulc_auth.py` against the NXP 3DES exchange vector,
`test_t2t_memory.py` for key programming/counter commits/locks and
`test_sessions.py` for read-only versus writable listener sessions.

### Type 2 profile identification and read limits

NTAG210/212 imports use their own geometry and UID-only ASCII mirror. They do not
expose the EV1 counters or NTAG213/215/216 single counter. Their signature,
password protection, compatible writes and lock bytes use the corresponding map.
See the [NTAG210/212 datasheet](https://www.nxp.com/docs/en/data-sheet/NTAG210_212.pdf).

Ultralight/NTAG and Type 2 NDEF screens now derive their read limit from a validated
GET_VERSION response and reselect before reading. NTAG216 can use all 231 pages;
the previous 135-page limit truncated its data. Unknown maps use a 16-page preview, avoiding address wrap or a fabricated complete dump. The
console uses the corrected version codes and preserves successful Ultralight-C
authentication for the subsequent read. `test_t2t_reader.py` checks geometry,
partial transfers and output boundaries.

The Ultralight/NTAG screen also reads NTAG I2C 1K/2K and I2C Plus 1K/2K via
SECTOR_SELECT. It follows the fork's linear dump mapping for EEPROM and the
configuration/session-register regions, including the 492-page Plus 2K map.
Only successfully read pages count toward the result; sector restoration failure
fails the operation. A sector switch requires an ACK followed by a clean passive
ACK, never an arbitrary transport failure. The console currently previews sector
0 for these models. The command contract is specified in the
[NTAG I2C Plus datasheet](https://www.nxp.com/docs/en/data-sheet/NT3H2111_2211.pdf).
`test_t2t_i2c.py` covers every mapped page, all four geometries, partial reads,
selection failures and buffer limits; `test_nonce.py` verifies passive-ACK
rejection of receive errors and unexpected data.

### Ultralight AES saved-file emulation

The Type 2 browser also accepts complete 60-page `Mifare Ultralight AES` files
with a captured version. Emulation handles AES-128 mutual authentication with
DataProtKey or UIDRetrKey, optional session CMAC, READ/FAST_READ, user/key writes,
configuration and key locks, counter access controls, and captured 48-byte
`AES signature` replies. UIDRetrKey authentication does not grant protected
user-memory access. Invalid MACs, counter exhaustion and malformed authentication
frames clear the session. Authentication failure limits and the successful-auth
counter reduction follow the [NXP MF0AES(H)20 Rev. 3.3 datasheet](https://www.nxp.com/docs/en/data-sheet/MF0AES(H)20.pdf).

The implementation uses the project's PSA AES/CBC/CMAC helpers. Host tests check
the NXP authentication vector, independently computed session keys and command/
response MACs, replay rejection, both authentication privileges, missing keys,
authentication limits, memory locks and counter permissions.

Key pages use physical card byte order. `Highboy AES known keys` contains four
hexadecimal bytes: the little-endian 16-bit known-byte mask for DataProtKey,
followed by that for UIDRetrKey. `FF FF FF FF` means both keys are known;
`FF FF 00 00` means only DataProtKey is known. Highboy exports always include this metadata, so masked or unknown keys remain
unknown. For a complete legacy fork dump without the extension, pages 48–51 are
interpreted as its saved DataProtKey, including an all-zero key, matching that
fork's convention; UIDRetrKey remains unknown. This import convention cannot
distinguish a manually fabricated zero placeholder from a known zero key. Session copies
retain these masks and counter metadata. The UI uses the same read-only/mutable
start and save-copy controls as other Type 2 files.

Random ID selects a four-byte random identifier with the required prefix at
startup and after field loss, while retaining the real UID in the saved image.
UID and signature reads require authentication when RID is active. This RF path
still requires board validation.

WRITE_SIG updates the twelve four-byte signature blocks; LOCK_SIG supports
unlock, lock and irreversible lock within the emulated image. Both commands
respect read-only sessions and the shared authenticated CMAC path. Save-copy
preserves partial signature writes and lock state. No signature is generated.

`Highboy AES signature state` stores three hexadecimal bytes: a little-endian
12-bit known-block mask followed by lock state (0 unknown, 1 unlocked, 2 locked,
3 permanently locked). A complete signature has mask `FF 0F`. Imported files
without this extension retain an unknown lock state and reject signature
mutations; a captured signature alone does not reveal its hardware lock state.
The virtual image can explicitly specify this metadata before mutable emulation.
Tests cover block bounds, partial writes, malformed commands, read-only mode,
lock transitions, permanent-lock rejection and save/reload preservation.

The hidden OriginalityKey is not implemented. Persistence for other listener families
remains separate work. RF interoperability has not been tested by these host tests.

```sh
python3 tools/nfc_api/test_t2t_aes.py
```

### NFC-V/SLIX session copies

Mutable NFC-V/SLIX file emulation now offers **OK Save copy / BACK Discard**.
Saving stops the worker before taking its result, reserves a unique
`.session-N.nfc` filename, and writes atomically. A failed save keeps the stopped
session available for retry. The source file is never overwritten. Failed
initialization and active sessions cannot supply a result.

Copies retain memory, block/AFI/DSFID locks, known passwords and their locks,
privacy, signature, protection configuration and EAS state. Unknown passwords
remain absent. `Highboy Destroyed` retains a virtual SLIX destroy operation;
`Highboy EAS Lock` preserves the EAS lock even without captured protection-system
information. These are Highboy extensions to the Flipper v4 text format; other
readers may ignore them. Loading a copy still defaults to read-only emulation.
Host tests exercise save/load of SLIX state, maximum ISO15693 geometry and
storage failures; RF behavior still requires testing on the board.

### Ultralight AES reader and write-back

In **Ultralight / NTAG**, RIGHT accepts `0:<32 hex digits>` for plain authenticated
reads or `1:<32 hex digits>` for CMAC-protected reads. UP clears the explicit key;
leaving the screen also clears it. Without an explicit key, AES reads try zero,
FF and candidates from `/sdcard/nfc/assets/dict/mf_ul_aes.dic`, with flash fallback
`/assets/nfc/dict/mf_ul_aes.dic`, bounded to 64 candidates. Each authentication
attempt starts with a fresh RF activation. After successful authentication, an
unsuccessful plain read is retried in CMAC mode after reactivation and a new
handshake. Explicit 0/1 modes do not switch automatically.

The reader validates mutual authentication, CRC and (when enabled) every response
MAC. A complete 60-page image is saved as `aes_<real-UID>_<number>.nfc` without
overwriting existing files. The real UID comes from authenticated memory, so RID
anticollision identifiers do not replace it. The authenticated key is inserted
in reversed key-page byte order and marked known; the other key remains unknown.
This makes the captured file reusable for authenticated emulation. Dumps therefore
contain recovered keys; the log never prints them. Signature and counter capture
retain only validated metadata. Incomplete memory reads are reported as errors
and do not create a complete-looking dump.

DOWN accepts an AES dump filename (empty uses the last successful AES read).
The confirmation screen requires OK; BACK cancels. The presented target must be
Ultralight AES and must authenticate with the configured key or dictionary.
The workflow probes the target through reads before writing, then starts a fresh
session with its secure-messaging setting. It copies user pages 4–39, preserving
UID, locks, configuration and keys. A different compatible AES target is allowed.
Prefix the filename with `key:` to additionally copy the saved DataProtKey pages
48–51; the confirmation explicitly identifies key replacement and requires a
known source key. UIDRetrKey is never overwritten by this action.

Plain writes check the ACK; CMAC writes validate the standalone response MAC,
including its counter and CRC. A timeout, bad MAC/CRC, rejection or cancellation
stops further writes without retrying an uncertain operation. The result reports
acknowledged pages, including partial progress; it does not claim readback
verification. A partial key update is not rolled back. Rescan the target before
retrying a failed operation.

The emulation path includes DataProtKey/UIDRetrKey authentication, protected reads,
RAM writes, key/configuration locks, counters, signature write/lock commands,
RID and save-copy persistence described above. The implementation scope follows
the local fork, not undocumented chip internals such as OriginalityKey.

Validation: `test_t2t_aes.py` exercises independent NXP/AES/CMAC vectors, both write
response formats, forbidden-page rejection, key persistence and failed exchanges;
`test_ul_aes_workflow.py` covers dictionary selection, both modes, user-only/key-copy
ranges, cancellation and partial failure. `test_t2t_file.py` and
`test_st25tb_write_ui.py` cover file round trips and explicit write confirmation.
Physical RF interoperability still requires validation on Highboy.

### NTAG I²C dump emulation

The NFC Emulate file picker accepts complete Flipper v4 profiles named
`NTAG I2C 1K`, `NTAG I2C 2K`, `NTAG I2C Plus 1K` and
`NTAG I2C Plus 2K`. Their flattened images contain 231, 485, 236 and
492 pages respectively, matching the reader layout above. Version metadata is
required. Import/export preserves the entire image, including the second sector.

SECTOR_SELECT uses two CRC-checked frames: C2 FF receives ACK, followed by
sector/00/00/00 with a silent acknowledgement. Field loss, a new selection or
starting emulation restores sector zero. Unmapped sectors cannot access memory.
READ and FAST_READ translate RF addresses into the saved layout; session-register
aliases at sector 3 F8/F9 share their saved bytes with the primary mapping.
A valid read extending into unmapped addresses returns zeros in those positions.
GET_VERSION returns captured version bytes. Plus supports captured READ_SIG,
PWD_AUTH, secret-page masking, AUTH0/NFC_PROT and sector-one access restrictions.

The default mode is read-only. Selecting T2T RAM in the existing interface permits
user EEPROM writes, respecting captured static/dynamic locks and Plus password
protection; the session-copy action saves those changes to a new file. UID,
configuration, lock and session-register writes are rejected. This does not yet
emulate an external I²C host: SRAM mirroring, pass-through and FAST_WRITE are not
implemented. Session registers are captured values, not a live host handshake.
FAST_READ currently retains the transport's 256-byte response limit; larger
requests receive NAK. These are explicit limitations, not full NTAG I²C hardware
parity. RF validation on Highboy remains necessary.

The host memory tests cover all mapped pages, aliasing, holes, read boundaries,
password masking, sector access, user writes and locked/read-only rejection.
The file tests round-trip all four geometries under ASan/UBSan.

### Cross-family file persistence

`NFC > NFC FILES` browses `.nfc` files in `/sdcard/nfc/dumps` without turning on
RF. UP/DOWN selects a file, OK opens its identity/size summary, RIGHT exports a
new numbered copy, and BACK exits. Reopening after reboot reads the SD file;
there is no volatile-only archive index. Copies never overwrite the source.

`nfc_archive` provides a bounded, lossless document archive for Flipper NFC v4
and Highboy capture v1 **for documents accepted by its envelope validator**.
Import/export preserves their original bytes, including unknown protocol fields,
comments, ordering and line endings, without flattening records into the small
native NVS library. Type 4 files accept repeated `NDEF Data` records with validated hex chunks and a matching declared size; singleton metadata still rejects duplicates. See the
[functional comparison](FORK_COMPARISON.md#generic-type-4-tag). The envelope validator rejects embedded NUL/control
characters, missing identity/header fields, duplicate keys, invalid UID hex,
unsupported envelope versions, more than 4096 fields or more than 128 KiB.
It validates the file container, **not** every protocol-specific field or its
suitability for emulation. Existing typed importers remain responsible for that.

Physical protocol-laboratory reads now persist the data they actually obtain:

| Reader | Additional SD capture |
| --- | --- |
| DESFire | Dedicated typed v4 profile: identity/ATS, version, free memory, applications, keys, file settings and accessible contents; see DESFire workflow below |
| FeliCa | Dedicated typed v4 profile: identity, Lite block statuses or Standard systems/areas/services/public blocks |
| NDEF | NFC-A identity, ATS/parsed CC where available and complete NDEF message; oversized Type 2 messages fail instead of being silently truncated |
| NTAG4xx | Typed Flipper v4 profile with identity/ATS/PICC Version and optional Highboy CC/NDEF extensions |
| Ultralight / NTAG without AES | Identity, version when available and exactly the pages returned by the reader, including I2C data |
| ISO15693 | Existing native geometry/identity payload plus all successfully read memory |
| ST25TB | Geometry, ordinary memory and system/OTP block, alongside the existing NVS entry |
| Texkom | Validated frame and UID, alongside the existing NVS entry |

Except for the typed DESFire, FeliCa and NTAG4xx profiles, these captured records use
`Filetype: Highboy NFC capture`, version 1,
with named hex fields and an explicit capture-scope statement. They are archival
records, **not new complete Flipper protocol profiles**. Unknown or unreadable
fields are omitted rather than represented by fabricated data. AES, Plus and
SLIX retain their existing richer typed `.nfc` exporters. Imported Flipper files
are copied unchanged and remain Flipper files. Exporting a Highboy capture does
not convert it into a Flipper-compatible emulation profile.

Writes reserve a unique filename and use `storage_write_atomic`; errors propagate
to the UI. Capture/export runs on the protocol worker, and file-only operations
skip NFC initialization and field control. Failed saves do not report success.
The typed DESFire and FeliCa workflows below provide their own reader and
saved-profile emulation paths.

`tools/nfc_api/test_archive.py` runs the actual archive implementation under
ASan/UBSan, checking cross-family byte-identical round trips, unknown fields,
CRLF, malformed headers/UIDs, duplicates, save failures and no-overwrite behavior.
The existing ST25TB/NFC-V reader tests also check capture geometry and memory
length, and the protocol UI tests retain write-confirmation coverage.

The capture `CC parsed` field uses big-endian 16-bit CC length, MLe, MLc,
NDEF file ID and maximum NDEF size, followed by read/write access bytes for Type
4. For Type 2 it contains the four raw CC bytes. UID and response fields preserve
the reader's byte order. Captures do not serialize C structures or padding.
`test_capture_workflow.py` additionally checks FeliCa read/load/emulation cleanup
and propagation of storage failures.


### DESFire structured workflow

The DESFIRE screen now uses `nfc_desfire_read()` rather than a version-only probe.
It traverses application IDs, key settings/versions and file IDs/settings, reading
freely accessible standard/backup files, values and linear/cyclic records. It also
retains Transaction MAC settings and additional access-right sets. It does not
attempt authentication or protected writes, matching the reference reader scope.
Permission/authentication denials are recorded as unavailable; transport errors,
malformed responses, capacity errors and cancellation do not produce a successful
complete read. GetVersion accepts DESFire and DESFire Light family codes.

Controls:

- **OK:** read again; successful reads are automatically saved as a new
  `/sdcard/nfc/dumps/desfire_NNNN.nfc`.
- **UP/DOWN:** page through card/application metadata, key versions, file settings,
  access rights and acquired contents in hexadecimal.
- **RIGHT:** enter a DESFire `.nfc` basename from the dumps folder. Loading validates
  the typed profile and writes a new normalized copy without replacing the source.
- **LEFT:** emulate the loaded/read identity (UID, ATQA, SAK and ATS).
- **BACK:** cancel the active task or leave the screen. Leaving releases the in-memory profile.

Identity mode implements activation/ISO-DEP identity only, as the fork's DESFire
`EmulateUid` feature does. It deliberately bypasses the legacy DESFire stub and
default Type 4 NDEF responder. No application/file contents or authenticated
DESFire transactions are emulated. Hardware errors stop the listener; cancellation
stops it and releases the RF reservation and sleep inhibition.

The typed v4 codec retains the fork's `PICC`, `Application`, key-version and file
fields, ATQA display byte order, optional ATS interface/historical bytes and
optional free-memory/flags fields. Absent file contents remain absent. Additional
`Highboy ... Known` and key-known flags preserve unavailable metadata across
Highboy reloads. A partial file with unavailable settings can be saved/reopened
here but is not guaranteed to load in the fork, whose schema requires those
settings. This is separate from the generic NFC FILES archive path.

Limits are 128 applications, 32 files per application, 14 keys/access-right sets,
128 KiB of acquired contents in total, four- or seven-byte UIDs and 32 ATS bytes.
The text codec accepts documents below 1 MiB and at most 65,536 input lines. Duplicate
keys/IDs, invalid geometry/lengths and out-of-range counts are rejected. Limits
produce explicit errors instead of silent truncation. Saves use the atomic storage
writer with exclusive numbered destination reservation; SD failure is reported
while the successfully acquired profile remains available in RAM.

`test_desfire.py` exercises all six file types, optional/denied data, native fallback,
key flags, cancellation, malformed documents and typed round trips under ASan/UBSan.
`test_desfire_transport.py` covers production native/wrapped additional-frame
handling, response capacity, empty frames and mid-chain cancellation.
`test_desfire_identity.py` covers saved identity setup and injected listener
startup/transport failures with resource cleanup. The shared UI regression also
covers DESFire page/load/emulation dispatch and cancellation.

P4 compilation and host checks are required; repeated physical read/load/emulate
cycles with a reader remain board acceptance tests. Only the P4 firmware changes.

### FeliCa structured workflow

The FELICA screen reads Lite/Lite-S and Standard profiles. Standard traversal
collects systems, areas, services, key versions and public blocks; Lite stores
28 block statuses and supports authentication with a supplied 16-byte key.
Protected or unavailable contents remain unavailable. Standard encrypted
services and unknown-key recovery are not implemented by this workflow.

- OK reads and saves a new `felica_NNNN.nfc` under `/sdcard/nfc/dumps`.
- UP/DOWN browse identity, metadata and block details.
- RIGHT accepts a saved filename, or `key:` followed by 32 hexadecimal digits.
  `key:` alone clears the key. The supplied key is cleared when leaving the screen.
- LEFT emulates the loaded/read profile. BACK stops the operation; accepted
  reader writes save a new profile, preserving the original file. Storage failures
  are reported instead of claiming that changes were saved.

Import supports Flipper NFC v4 envelopes with FeliCa data formats 1–4; export
writes format 4. Profiles preserve multiple systems and repeated per-system keys.
Limits are 16 systems, 1024 area/service nodes per system, 4096 public blocks in
all systems and a 1 MiB text file. Out-of-range profiles fail explicitly.
The optional `Highboy key known` extension prevents an unavailable key from
becoming an all-zero authentication key. Successfully authenticated Lite profiles
contain the supplied key, as in the reference format; treat those dumps accordingly.

The listener handles polling and discovery, public reads/writes, Lite MACs,
access restrictions and counter replay rejection. Field loss clears authentication
and reloads memory-configuration state. SPI/target errors propagate to the worker.
Response timing bytes are widened during emulation, following the fork's software
listener approach. Host tests cover protocol/codec/crypto and UI/service cleanup;
physical interoperability and repeated field-entry behavior remain to be tested
on the board. See [the fork comparison](FORK_COMPARISON.md#felica).

### NTAG4xx workflow

The NTAG4xx screen matches the reference fork's identification and identity-only
emulation scope. GetVersion supports exactly 28 or 29 bytes, including the
optional fabrication-key ID. Models 413 DNA, 424 DNA and 424 DNA TagTamper have
specific labels; unknown versions retain their raw values without guessing a
model. UP/DOWN shows identity, nominal memory layout, hardware/software version,
batch, manufacturing date and fabrication-key metadata (not an authentication key).

OK reads and saves `ntag4xx_NNNN.nfc` in `/sdcard/nfc/dumps`. RIGHT loads a filename
from that directory and exports a new numbered copy. LEFT starts saved identity
emulation; BACK stops it. UID, ATQA, SAK and ATS are replayed. Application commands,
NDEF contents, SDM and authentication are not emulated by this feature. Generic
Type 4 NDEF emulation is a separate workflow.

Files use `Filetype: Flipper NFC device`, version 4, device `NTAG4xx`, and the
fork's `PICC Version`/ISO14443-4A fields. The importer validates UID size, ISO-DEP
SAK, ATS interface presence/length, hex lengths and duplicate recognized fields;
it accepts CRLF and comments. Import is bounded to 8 KiB. Export normalizes the
optional version tail to 29 bytes, zero-filling it when absent, as the fork does.
The optional `Highboy CC` and `Highboy NDEF` hex fields preserve public captures;
NDEF is bounded to 1024 bytes. Absent data stays absent, while an empty successfully
read NDEF remains distinguishable. NDEF read errors are displayed separately from
a successful version read. No protected-content credentials are guessed.

SD failures propagate to the screen while a successfully read profile remains in
RAM. RF reservation, sleep inhibition and target teardown follow the protocol
worker lifecycle. Tests cover typed files, reader failures, UI and lifecycle;
physical read/replay timing on the board still requires validation.


## Type 4 NDEF workflow

The NDEF screen supports Type 4A mapping 2.0 independently of NTAG4xx identity
replay. OK reads a physical tag, RIGHT loads a `.nfc` filename from
`/sdcard/nfc/dumps`, LEFT emulates the loaded/read message, DOWN stages a write,
and UP stages creation/completion of the NDEF application on DESFire. Writes and
creation require a second OK; BACK cancels. Type 2 reading remains available,
but does not create a Type 4 emulation profile.

Type 4 reads save numbered `t4t_*.nfc` profiles. The Flipper v4 codec preserves
UID/ATQA/SAK/ATS, empty NDEF and repeated 16-byte `NDEF Data` records. Optional
`Highboy CC` contains big-endian MLe, MLc, NDEF FID, full file capacity, then
read/write access bytes. Fork files without that extension default to a 2048-byte
file, expanded when needed to hold the imported message. The implementation
supports 4/7-byte UIDs and up to 32766 payload bytes (32768 including NLEN); larger files fail
explicitly. CC parsing searches bounded TLVs, including extended TLV lengths,
with a 1024-byte CC limit.

The listener handles NDEF application/file selection and READ/UPDATE BINARY,
checks access permissions and bounds, and resets selection on field/session
restart. It uses the existing ISO-DEP target fragmentation engine. Accepted
session writes are saved to a new profile when emulation stops, including on
transport failure; failed SD persistence is reported and data remains in RAM.
This models NDEF file contents, not vendor authentication, SDM or secure storage.

Physical writing checks target CC permissions and capacity, clears NLEN before
payload writes, and commits NLEN last. Completion is reported only after reading
back and comparing NLEN and the entire payload. Interrupted writes can leave an
empty/incomplete target; cancellation is checked before writing and during
verification, while the bounded payload-write primitive finishes its current
operation before cancellation is observed.

Creation follows the fork's DESFire application parameters: native AID EEEE10,
ISO DF E110/name D2760000850101, CC E103 and a 2048-byte NDEF file E104. It checks
GetVersion before mutations, does not delete applications or alter keys, and
creates missing files or resumes a matching partial application. Existing file
type, communication/access settings and size must match before proceeding. A
matching CC is preserved; a blank CC can be initialized, while a nonblank
incompatible CC is rejected. Existing NDEF contents are never cleared by this
action. Denied creation is reported; interruption can leave a partial application. Creation is not generic formatting of
NTAG, arbitrary Type 4 chips or ISO14443-B.

Host validation: `tools/nfc_api/test_t4t_profile.py` covers codec/listener,
permissions, reset, creation command sequences and failure gates, CC TLV parsing,
NLEN capacity, chunking and interrupted writes. The shared UI test covers explicit
confirmation and cancellation; archive tests cover repeated NDEF fields. Actual
RF read/write/emulation and creation still require validation on Highboy hardware.

## EMV inspector

The bank-card screen reads up to eight PPSE applications, performs SELECT,
PDOL/GPO and AFL record reads, then queries optional metadata and transaction
logs. It displays PAN, cardholder/application names, dates, AIP, country/currency,
PIN tries and counters only when obtained. `read` means the required application
flow succeeded and a PAN was obtained; `partial / failed` retains acquired data
and shows the APDU status/error. Optional fields may legitimately be unavailable.

Controls after reading: **UP/DOWN** changes the detail/log page, **LEFT** changes
application, **RIGHT** opens a filename from `/sdcard/nfc/dumps`, **OK** scans
again, and **BACK** exits. Each application is automatically saved as
`emv_NNNN.nfc`; failure to save is shown while the data remains available in RAM.
Opening a file performs no card communication. Imported and exported files use
the fork's EMV format, with Highboy extensions for availability and log records.

The worker releases the RF field and the no-sleep lock on success, cancellation
and failure. It sends SELECT, GPO, READ RECORD and GET DATA, without payment
completion, PIN verification or emulation. Metadata is not printed to serial logs.
PDOL uses the reference terminal profile, including its synthetic amount/date values.

Run `python3 tools/nfc_api/test_emv.py` for the synthetic host regression suite.
Physical card and RF validation is still required. EMV, NTAG4xx and Type 4 share the ISO-DEP text header encoder to avoid
duplicating serialization code and the generated license notice. No partition
migration is introduced.

## Persistent profile memory

The Type 4 profile (32,824 bytes) and the eight EMV application profiles
(9,184 bytes) reside in external BSS/PSRAM on P4. These task-context data models
are not DMA buffers or interrupt state. Keeping them out of internal BSS
recovers 42,008 bytes of internal RAM even when the NFC screens are closed.
The board profile requires PSRAM and enables external BSS. Transport DMA buffers,
task stacks and synchronization objects retain their existing placement.

## Discovery deadlines and target DMA

The ST25R3916 TX wait retains receive status consumed alongside TXE, so a
fast tag response remains visible to the subsequent RX wait. Clearing IRQs
before a new exchange also clears this software latch. IRQ reads propagate
SPI failures. TX, RX and FIFO waits use monotonic elapsed-time deadlines,
including SPI transfer time, rather than counting polling iterations.
Automatic discovery still cycles NFC-A/B/F/V: each technology needs its own
RF configuration. No-card probes now respect their configured time budgets.

The Classic transparent TX DMA buffer holds 832 symbols (3,328 bytes), enough
for the maximum 162-bit response, framing and DMA alignment without a
mid-frame refill. The three card serialization scratch buffers live in PSRAM;
they are task-context storage buffers, not DMA memory. The RMT DMA allocation
remains internal. `tools/nfc_api/test_irq_wait.py` covers simultaneous TX/RX
interrupts, error handoff, stale-state clearing and deadlines with slow SPI;
`test_transparent.py` checks waveform generation and allocation cleanup.

## Classic authentication latency

NFC SPI transactions use synchronous polling while retaining the shared-bus
lock and RF ownership checks. Register and FIFO transfers no longer require
an interrupt-driven task wake-up for each operation in the nonce/response
exchange. The reader does not print the nonce between receiving it and sending
the encrypted reader response. This removes avoidable latency; it does not
make an unknown sector key valid. A silent card after the reader response can
still mean a rejected key or lost RF communication.

`test_classic_auth.py` now checks the outgoing 72-bit response and its encrypted
parity with a separately initialized simulated card, then generates At from
that card state rather than copying the reader's state. It also retains invalid
At/parity and SPI-failure checks. Physical dump validation remains necessary.
