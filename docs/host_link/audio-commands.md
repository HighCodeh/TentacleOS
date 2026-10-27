# Audio command reference (host link)

Byte-level contract for the **audio category** (`category = 0x0A`,
`SPI_CAT_AUDIO`). P4-native (NS4168 speaker), handled on the P4, never relayed to
the C5. Read [`app-guide.md`](./app-guide.md) first. Responses are `[status u8]`.

| op | name | request | response |
|----|------|---------|----------|
| `0x01` | `AUDIO_SET_VOLUME` | `[pct u8]` (0-100) | none |
| `0x02` | `AUDIO_TONE` | `[freq_hz u16][dur_ms u16][amp_pct u8]` (amp 0-100) | none |
| `0x03` | `AUDIO_CHIME` | none | none |
| `0x04` | `AUDIO_CLICK` | none | none |

`AUDIO_TONE` returns immediately: the tone plays on a short-lived P4 task so the
link is not blocked for `dur_ms`. Volume is the digital square-law curve (the
amplifier gain is fixed); it also persists as the system volume.

Mic capture is not exposed here - the device records WAV files to
`/sdcard/recordings`, which the app reads via the `FILE_*` ops
([`app-guide.md`](./app-guide.md) §9).

Console equivalent: `audio volume <pct>` / `audio tone <freq> <ms> [amp%]` /
`audio chime` / `audio click`. Tester: `cli.py audio tone 1000 200 50`.

Source of truth: `spi_protocol.h`, P4 `Service/host_link/host_link_audio.c`.
</content>
