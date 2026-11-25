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

# Frequency analyzer

Sub-GHz > Frequency Analyzer discovers a nearby carrier using CC1101 RSSI.
It is receive-only. Detection is a frequency estimate, not protocol decoding,
a packet counter, or calibrated RF power measurement. Short isolated bursts can
be missed during a sweep; hold or repeat the remote button while searching.

## Controls

- LEFT/RIGHT: Auto (17 common channels), 315, 433, 868, 915, or Full.
- UP/DOWN: detection threshold from -95 to -40 dBm in 1 dB steps (default -93 dBm, matching the reference).
  A more negative threshold accepts weaker signals and more noise.
- OK: pause. While paused LEFT/RIGHT selects Resume or Read RAW, OK confirms.
- BACK: return to Sub-GHz and stop the owned receiver.
- Touch: Pause/Resume and Read RAW. RAW requires a confirmed result, waits for
  analyzer cleanup, and opens the raw reader at the detected frequency. It retains
  the configured demodulation preset, which may need adjusting for that remote.

The display shows estimated frequency, RSSI, live sample history and (portrait)
the three latest distinct frequencies. Bars are sequential RSSI samples, not a
calibrated frequency-axis spectrum. The last result persists while searching;
changing search mode clears the active result but retains history.

## Search and ownership

Auto prioritizes 315, 433.92, 868.35 and 915 MHz among 17 common frequencies.
Full visits 1099 points at 250 kHz spacing across the supported 300–348,
387–464 and 779–928 MHz bands. Band selectors restrict that plan. Gaps are skipped.
The checked `cc1101_scan_rssi` driver operation chooses the RF switch path,
calibrates, enters RX, settles and reads RSSI under one SPI reservation per point.
Polling SPI transactions inside that reservation avoid repeatedly yielding to
display DMA between registers. Other CC1101 users retain the normal transaction
path. The analyzer disables automatic calibration on RX entry because each point
has already been calibrated explicitly.

The core-0 worker first uses 650 kHz bandwidth, then refines a candidate over
plus/minus 300 kHz at 20 kHz steps with approximately 58 kHz bandwidth. Each
point settles for 2 ms and takes one RSSI sample, matching the reference cadence. A fine
peak must exceed the threshold, have at least 6 dB contrast across the local
scan, and still be present on a confirmation sample. This rejects flat noise
and candidates that vanish, but does not guarantee exact carrier measurement.

The wide/fine strategy and AGC settings were checked against the official
[Flipper worker](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/main/subghz/helpers/subghz_frequency_analyzer_worker.c).
This service uses the High Boy driver, task, resource and UI contracts; hardware
and antenna behavior remain different from Flipper.

Start reserves RES_SUBGHZ before creating a task. Snapshots are copied under a
critical section; configuration changes cancel pending old-generation scans.
Pause confirms radio IDLE. Stop retains ownership until checked IDLE succeeds,
including retries after failure. Radio errors never become valid RSSI values.
A detection never blocks the worker: the view holds the signal indicator for 500 ms
while scanning continues. The UI refresh interval is 40 ms, with unchanged labels
retained, a smoothed RSSI bar and a native 24-pixel frequency font.

UI timers only read snapshots; they never access SPI. Close/delete hooks stop
only the screen-owned session, release display hold and delete the timer.

## Diagnostics

`subghz analyzer` reads the active worker snapshot without accessing the radio.
It reports completed sweeps, samples, detections, last sweep duration and last
fine-scan duration in microseconds. Sweep time includes refinement if needed.
A timing readout is not a comparison against a physical Flipper.

## Validation

`python3 tools/frequency_analyzer/test_frequency.py` tests production worker code
with mocked radio/RTOS, UBSan, band boundaries, four common carriers, off-grid
refinement, noise/transient rejection, pause/cancel, ownership and failures injected at every radio-call position in a full common-channel scan. Timing assertions check the programmed waits: 34 ms for the
17-point coarse scan and 98 ms including 31 fine samples plus confirmation;
SPI, calibration and task scheduling add hardware time. These simulated carriers do not establish physical RF performance.

`python3 tools/frequency_analyzer/test_ui.py --output /absolute/output/path`
renders the production LVGL view using the host-ui build, covering portrait,
landscape, light/dark themes, controls, RAW handoff and cleanup. Build with
`bash tools/build.sh p4`; app-only flash does not install missing microSD assets.
Hardware validation must separately confirm startup, controls, cleanup and
frequency acquisition with a known transmitter.

On unit 02018913, the pre-batching build reported a 19,773,877 us Auto cycle,
including 11,866,897 us refinement, with the live view active. This is the measured
baseline that motivated the SPI reservation change, not a Flipper measurement.
The SPI-batching build was subsequently installed on that same unit, with the
complete application verified and runtime ELF SHA-256
`0121b16d18d86c2cbc5ef62b5402662a248502bec13e5a5a4001a5958a8d6111` confirmed.
The view still advanced slowly during the observed run. User changes to the mode
and threshold prevented a controlled before/after sweep comparison, and no
known-transmitter acceptance run was completed. Therefore scan latency remains
an open issue; this laboratory feature is not yet qualified as matching Flipper
speed or carrier-acquisition performance.

These are historical observations of the integrated device build. The scoped
`lab/rf` publication is separately compiled and host-tested; it is not a claim
that the published tree was flashed or tested over RF on 25 November.
