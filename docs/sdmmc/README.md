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

# SDMMC checked multi-sector writes

P4 uses the ESP-IDF v6.1 SDMMC component with a narrow write-error patch.
The upstream Apache-2.0 notices are preserved. SDK version and all 27 component
inputs, the patch and patched outputs are pinned by SHA-256 in
`firmware_p4/components/Drivers/sdmmc/patch_manifest.json`.

## Reason and behavior

Upstream `sdmmc_write_sectors_dma()` overwrites the original failed CMD25 result
with the result of the ACMD22 written-block-count query. A successful query can
therefore report success after a failed or partial write. The patch keeps the
original error even if the diagnostic query succeeds, reports a full count, or
fails itself. It also propagates the initial CMD13 status-query failure after
an otherwise successful write. Existing DMA alignment, busy waits and timeouts
remain unchanged. There is no blind retry or acknowledgement of partial writes.
The override applies to all P4 SDMMC consumers, including FatFS; C5 is unchanged.

USB MSC now submits each aligned buffer as one `sdmmc_write_sectors()` call:
up to 64 sectors for the 32 KiB endpoint buffer. The SDK may split unsuitable
buffers using its existing DMA path. USB completion remains synchronous; any
failure latches the LUN unavailable and reports a SCSI medium error. Buffer
size, memory placement, filesystem ownership and eject behavior are unchanged.

## Build integration and updates

`tools/cmake/highboy_sdmmc.cmake` runs `tools/sdmmc/prepare.py` before component
resolution. It generates `<build>/components/Drivers/sdmmc`, registering that
component via `EXTRA_COMPONENT_DIRS`. The installed SDK and managed dependencies
are never modified. Unrecognized SDK inputs or patch changes stop configuration.
Do not edit generated files. Edit the tracked `write_errors.patch`, review changes
against the SDK, and update source/patch/output hashes together when upgrading.

The integration follows the existing FatFS generation approach: isolated patch
application, path validation, marked output directories and stable timestamps
when content is unchanged. No downloads are performed.

## Validation

```sh
python3 tools/sdmmc/test_sdmmc.py --idf "$IDF_PATH"
node --test tools/highboy/tests/test_usb_msc.mjs
```

The host harness compiles the actual generated write function and checks both
SD and SPI modes, single/multiple sectors, every partial count through 64 blocks,
failed recovery queries, status-query errors and busy timeout. It reproduces the
failure against the original SDK code. Generator checks reject changed source,
version and patch hashes, preserve timestamps and repair disposable output.
MSC tests verify one 64-sector write call per 32 KiB callback, error latching,
bounds, ownership, flush and eject. These are host tests, not physical card tests.

Before this change, a 32 MiB Mac USB/exFAT file test measured 9.76 MB/s read and
0.59 MB/s write with F_NOCACHE and fsync/F_FULLFSYNC. Post-change throughput and
physical-card compatibility must be measured after installing this build; no
speedup is assumed from host tests alone.
