# Bridge Manager - P4

The **P4-side lifecycle / link manager for the P4<->C5 SPI bridge**. It brings the
bridge up in the right handshake mode, checks that the two chips agree on the wire
contract, keeps the link status honest while the C5 comes and goes, and is the
entry point that triggers a C5 firmware update.

- Location: `firmware_p4/components/Service/bridge_manager/`
- Transport it drives: [`../spi_bridge/README.md`](../spi_bridge/README.md)
- Update path it triggers: [`../c5_flasher/README.md`](../c5_flasher/README.md)

## API

```c
esp_err_t bridge_manager_init(void);         // bring up the bridge + run the boot checks
esp_err_t bridge_manager_force_update(void); // push a C5 app OTA (SD image, SPI transport)
```

## What `bridge_manager_init()` does

1. **Bridge init (POLL mode).** The HighBoy V2 PCB has no bridge IRQ trace
   (`GPIO_BRIDGE_IRQ_PIN == -1`), so the master polls the bus instead of waiting
   on an IRQ: `spi_bridge_master_init_mode(SPI_BRIDGE_MODE_POLL)`. The C5 slave
   must be initialized in the matching mode.
2. **App-version check.** Reads the C5 firmware version (`SPI_ID_SYSTEM_VERSION`)
   and compares it against the P4's expected `FIRMWARE_VERSION` (generated from
   `common/metadata/version_info.txt`, currently **1.4.0**). This is
   **detection-only**: a mismatch just logs "update available" and a silent C5
   marks the bridge down - the P4 never auto-flashes. The user updates explicitly
   with the `c5` console command.
3. **Proto-version match check (SPI-2).** `check_c5_protocol()` reads the C5's
   `SPI_ID_SYSTEM_PROTO_VERSION` and compares it against `SPI_PROTOCOL_VERSION`
   (**2**). This is the second integrity layer, distinct from the per-frame CRC
   (SPI-1): a drifted `spi_protocol.h` leaves the bytes intact but makes the two
   ends assign them different meaning, which the CRC cannot catch. On mismatch
   (or an older C5 that answers `UNSUPPORTED`) it logs loudly and calls
   `led_signal_error()`, but keeps the bridge up - detection, not enforcement.
4. **Starts the C5 link monitor task** (below).

## C5 link monitor

A background task (`c5_link_monitor`), pinned to `SYS_CORE_RADIO` at
`SYS_PRIO_SERVICE_LO`, that re-detects a C5 which booted late or rebooted after an
OTA:

- Idles while `spi_bridge_is_alive()` - it never pokes the bus once the link is up.
- While the bridge is dead, every ~1.5 s it optimistically probes
  `SPI_ID_SYSTEM_VERSION` (500 ms timeout). On success it marks the bridge alive,
  calls `led_signal_info()` to clear the degraded indicator, and re-runs
  `check_c5_protocol()`. On failure it drops back to dead and retries later.

## C5-link LED signalling

The manager reflects bridge health on the status LED via `led_control`:

- `led_signal_error()` on a proto-version mismatch / unavailable (headers out of
  sync - reflash both).
- `led_signal_info()` when the link is (re)established, clearing the warning state.

## OTA triggering

`bridge_manager_force_update()` runs `c5_flasher_init()` then
`c5_flasher_update(NULL, 0, SPI_OTA_TRANSPORT_SPI)` - i.e. the C5 app OTA streamed
from the SD image over the SPI transport (control always on the bridge). See the
c5_flasher doc for the OTA flow.

## Safe reads (out_capacity clamp)

Every `spi_bridge_send_command()` call passes an explicit `out_capacity` equal to
the receiving buffer's `sizeof`, so a slave that announces (or a corrupted length
that inflates) more bytes than the buffer holds is clamped and cannot overflow it.
