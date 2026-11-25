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

- LEFT/RIGHT: Auto, the 52 numeric frequencies from Radio Config, then the
  315/433/868/915 preset groups and Full. Holding repeats navigation. The selected
  carrier appears in the main readout until a signal is found, explicitly marked
  as selected rather than detected. Auto keeps its 17 priority channels.
- A selected carrier searches that channel and refines only a candidate. Band
  groups visit catalog frequencies, not every 250 kHz. Full retains the complete
  250 kHz grid for unknown/off-catalog carriers and is deliberately slower.
- UP/DOWN: detection threshold from -95 to -40 dBm in 1 dB steps (default -93 dBm, matching the reference).
  A more negative threshold accepts weaker signals and more noise.
- OK: pause. While paused LEFT/RIGHT selects Resume, Read RAW or Waterfall; OK confirms.
- BACK: return to Sub-GHz and stop the owned receiver.
- Touch: Pause/Resume, Read RAW and Waterfall. RAW requires a confirmed result, waits for
  analyzer cleanup, and opens the raw reader at the detected frequency. It retains
  the configured demodulation preset, which may need adjusting for that remote.

The display shows estimated frequency, RSSI, live sample history and (portrait)
the three latest distinct frequencies. Bars are sequential RSSI samples, not a
calibrated frequency-axis spectrum. The last result persists while searching;
changing search mode clears the active result but retains history.

## Spectrum / waterfall view

Choose Waterfall from the Finder's paused actions (OK, LEFT/RIGHT, OK), or touch
its Waterfall action. A confirmed frequency centers the sweep; otherwise the
current Radio Config frequency is used. Near a band edge, the center shifts just
enough to keep the complete span inside the supported band.

The upper plot shows the latest **complete** 40-point RSSI sweep, with a dashed
optional max-hold trace and a cursor. The lower waterfall uses exactly the same
frequency bins; newest rows enter at the top. Intensity uses an adaptive dBm scale
through a fixed dark-blue, blue, cyan, green, yellow and red palette, independent
of the interface theme. The intensity palette is centralized in the theme API;
labels, controls and spectrum traces retain the selected interface theme. This is
sequential CC1101 RSSI sampling, not I/Q reception or a calibrated spectrum analyzer.
Short transmissions between samples can be missed.

Each complete sweep estimates the noise floor from the 20th percentile minus
3 dB and the upper limit from the peak plus 1 dB. A minimum 24 dB range keeps
noise-only sweeps blue instead of exaggerating tiny variations. Limits are smoothed
between sweeps (15%, with 50% attack for rising peaks). The Auto readout shows the
current lower/upper dBm limits. All visible history is recolored against the same
scale, so equal RSSI has equal color across rows. With no new sweep, the scale
stays unchanged. This affects only waterfall contrast; measured values and the
upper spectrum's fixed -120 to -20 dBm axis are unchanged.

- UP/DOWN selects View, Center, Span, RX bandwidth, Cursor, Max hold, Pause or Read RAW.
- The Center field is selected on entry. LEFT/RIGHT jumps through the same 52
  numeric Radio Config frequencies, wrapping and crossing unsupported gaps without
  tuning inside them; 315 MHz is directly selectable. View returns to Finder.
  Span ranges from 100 kHz to 20 MHz. Near a band edge the window is fitted inside
  the band; the requested catalog frequency is retained for navigation so a wide
  span cannot trap the selector at an edge. The top C value and axis show the
  actual window center and endpoints. RX bandwidth offers approximately 58, 102, 203,
  271, 406 or 650 kHz. Bandwidth is the receive filter, not bin spacing.
- OK pauses/resumes except on Read RAW, where it opens the receiver at the cursor.
  The transition waits for checked radio cleanup and retains Radio Config modulation.
- BACK returns to Finder. Its dedicated screen close hook stops acquisition and
  frees all view buffers; deleting the screen directly has the same idempotent cleanup.

The top readout includes center (C, MHz), span (S, MHz), RX bandwidth (BW, kHz),
peak frequency/RSSI, automatic color limits and last complete sweep duration.
Cursor inspection shows the last sweep's value, including while paused. Max hold
keeps maxima since opening or the last configuration reset. Changing center, span
or bandwidth clears both history and max hold; partial old sweeps are never published.
Pause discards an incomplete sweep and confirms IDLE; resume starts a full row.

Each row now uses 40 actual measurements instead of 80, halving tuning/sample
operations while retaining exact span endpoints. This trades horizontal frequency
resolution for refresh speed; narrow the span to inspect finer detail. It does
not imply simultaneous sampling or a guaranteed hardware frame rate.

The view allocates one bounded PSRAM block (about 16 KiB): 40 x 64 raw RSSI samples and RGB565 history,
line coordinates, maxima and the copied sweep. Older rows are discarded after 64;
there is no growing log, SD write or internal-RAM fallback. The worker never accesses
this view allocation. It publishes synchronized snapshots and retains RES_SUBGHZ
until checked IDLE, so the UI can free PSRAM immediately even if cleanup retries.
A busy UI can miss completed sweeps; missing sweeps do not generate invented
rows. Each row is a sweep, not a fixed wall-clock interval; pause gaps are not plotted.
Theme changes rebuild the view through the normal screen lifecycle and clear history.
Both portrait and landscape use live content bounds. No radio calls occur in rendering.

The waterfall service tests inject startup, per-radio-call and cleanup failures,
check frequency overflow/band gaps, exact endpoints, interrupted-row rejection and
pause/resume. The LVGL tests exercise both orientations/themes, bounded history,
controls, RAW handoff, live theme rebuild, direct deletion and PSRAM/timer failures:

```sh
python3 tools/frequency_analyzer/test_waterfall.py
python3 tools/frequency_analyzer/test_waterfall_ui.py --host-build /path/to/host-ui --output /tmp/waterfall-preview
```

The expanded application requires the [16 MB P4 build profile](../highboy/README.md#16-mb-p4-build-profile).
The prototype now has 16 MB; installation details are recorded in that profile.

## Search and ownership

Auto prioritizes 315, 433.92, 868.35 and 915 MHz among 17 common frequencies.
Full visits 1099 points at 250 kHz spacing across the supported 300–348,
387–464 and 779–928 MHz bands. Band selectors instead filter the shared Radio Config catalog (20 low-band,
20 mid-band, 8 frequencies below 900 MHz and 4 above). Gaps are skipped. A single
carrier mode visits exactly one catalog entry before any necessary refinement.
The catalog is maintained in the service's `frequency_catalog.h`; Radio Config
uses the same table through its existing accessor, including its zero/Auto entry.
The checked `cc1101_scan_rssi` driver operation chooses the RF switch path,
calibrates, enters RX, settles and reads RSSI under one SPI reservation per point.
Polling SPI transactions inside that reservation avoid repeatedly yielding to
display DMA between registers. Other CC1101 users retain the normal transaction
path. Shared SPI admission makes the higher-priority display yield to pending
peripheral requests, including the analyzer’s initial register configuration. The analyzer disables automatic calibration on RX entry because each point
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

### Historical measurements from lab/rf (not this build)

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

These are historical observations recorded by the lab branch. The scoped
`lab/rf` publication is separately compiled and host-tested; it is not a claim
that the published tree was flashed or tested over RF on 25 November.

### Current dev integration

The dedicated menu view replaces the old 2 MHz spectrum screen. Existing spectrum
console/app APIs, accumulated Read decoding, the inline Radio Config dial and PTT
transmission remain available. Read RAW handoff explicitly selects raw mode;
normal Read resets that selection. No C5 or paired-protocol change is required.
The host driver tests additionally inject transaction failures into the production
RSSI sampler and verify the shared bus reservation is released on success, failure
and calibration timeout. On the 16 MB P4 prototype, the original integration reproduced an SPI lock
timeout during initial configuration, before collecting samples. After display
admission was changed to yield to pending peripheral requests, a physical run
completed 37 Auto sweeps and 1771 samples with `ESP_OK`, while the live view
refreshed. Recent sweeps were about 0.63 s including refinement; leaving the view
stopped the worker with `ESP_OK`. This verifies that contention case and cleanup,
not carrier-detection accuracy or long-term stability.
