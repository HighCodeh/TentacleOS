# LED command reference (host link)

Byte-level contract for the **RGB status-LED category** (`category = 0x0B`,
`SPI_CAT_LED`). P4-native (LP5816), handled on the P4, never relayed to the C5.
Read [`app-guide.md`](./app-guide.md) first. Responses are `[status u8]`.

| op | name | request | response |
|----|------|---------|----------|
| `0x01` | `LED_SET_COLOR` | `[r u8][g u8][b u8]` | none |
| `0x02` | `LED_CLEAR` | none | none |
| `0x03` | `LED_BLINK` | `[r u8][g u8][b u8][dur_ms u16]` | none |
| `0x04` | `LED_SIGNAL` | `[which u8]` (`0` info, `1` warning, `2` error) | none |

The LED is shared with firmware status cues (BadUSB, scans, errors), so an
app-driven color can be overwritten by a subsequent system signal.

Console equivalent: `led color <r> <g> <b>` / `led clear` / `led blink <r> <g> <b> <ms>`
/ `led signal <info|warn|error>`. Tester: `cli.py led color 0 255 0`.

Source of truth: `spi_protocol.h`, P4 `Service/host_link/host_link_led.c`.
</content>
