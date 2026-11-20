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

# Shared RF frontend

The High Boy V2 LF 125 kHz and NFC 13.56 MHz stages share one physical antenna.
`firmware_p4/components/Drivers/rf_frontend` arbitrates the active mode.

`rf_frontend_claim()` rejects idle/out-of-range modes and returns
`ESP_ERR_INVALID_STATE` if the other stage owns the antenna. Reclaiming the same
mode is idempotent. `rf_frontend_release()` only releases the matching mode;
`rf_frontend_get_mode()` returns a coherent snapshot.

A statically initialized `portMUX_TYPE` protects the mode. No lazy mutex creation,
allocation or waiting is required. Critical sections contain only mode reads and
updates; logs and hardware operations run outside them. Callers release ownership
only after their stage has been disabled. An uncertain NFC shutdown retains the
reservation until a successful OFF transfer.

# Native LF RFID

`Drivers/lf_rfid` replaces the YS-RFID2 UART path in the integrated boot and RFID
read screen. It initializes LF outputs off, generates the carrier with LEDC,
measures carrier feedback with PCNT and reads EM4100 RF/64 through hardware
capture of both GPIO16/DA edges. MCPWM timestamps at 1 MHz feed an internal-RAM
512-edge ring; the worker decodes durations without busy-wait GPIO sampling.
The decoder checks Manchester transitions, header, stop, row/column parity and
three consecutive equal frames, in either polarity. Ring overflow is an error,
never a valid ID. Capture resources are released after each bounded read; failed
cleanup is retained for retry. Reading waits until 450 ms after field start before
its capture window. GPIO15 remains an input; the measured CL frequency does not
qualify tag power or antenna amplitude. Only EM4100 RF/64 is qualified by these
software vectors; RF/16, RF/32, PSK and other LF protocols are not added.

`lf_rfid_field_stop_checked()` checks both PWM stops and capture cleanup before
releasing antenna ownership. The legacy void wrapper remains for compatibility.
The lab UI uses the checked operation and keeps the worker alive on failed OFF.
During reading, GPIO2 carries the 125 kHz PWM into T4/T3/T5 and GPIO11 stays low,
leaving the T2 load off. GPIO2 is not a static reader enable. GPIO15/CL is the
carrier feedback input from U12; GPIO16/DA is the demodulated-data input.
LEDC timer 3 has separate outputs: channel 7 for GPIO2 carrier drive and channel 6
for the bounded GPIO11 load test. Only one output may have nonzero duty at a time.
The load test changes the shared timer while the carrier channel remains at zero,
then restores the nominal frequency. Idle and passive reception keep both low.
Console diagnostics also provide passive listening, edge capture and modulation
checks. The lab-only `lf_rfid_scope_drive()` reserves the idle frontend and drives
GPIO2 at 20–200 kHz, 50% duty, for 10–5000 ms, then turns both outputs off. It
refuses detected external LF carrier and retains the normal reader's 110–140 kHz
limits. Its returned frequency is the LEDC setting, not an antenna measurement.
The LF self-test requires detected carrier feedback before reporting
frequency PASS; an absent carrier leaves the frequency error field at zero and
must not be mistaken for a measurement within tolerance. The terminal workflow
and focused regression are documented in [Hardware Lab](../hardware_lab/README.md). Pin assignments remain in `Drivers/pins/include/pin_def.h`.

The RFID screen reader runs on `SYS_CORE_RADIO` at `SYS_PRIO_SERVICE_LO`.
A critical section protects cancellation, generation, running state and the
result/tag pair. It publishes a coherent result only for the current generation.
The task owns its hardware from field start through field stop. Closing the
screen invalidates its generation; cleanup may take one 500 ms read interval plus any remaining initial 450 ms field settling.
Reopening while cleanup is active reports busy, and cannot launch a second
reader. Task-creation failure permits a later retry.

## Bounded EM4100 emulation

`lf_rfid_emulate_em4100()` emits a complete RF/64 Manchester EM4100 frame
through GPIO11/T2 load modulation. It reserves the idle shared antenna, checks
external carrier feedback and keeps GPIO2 (own-field drive) low. The 64-bit
frame includes nine header bits, forty ID bits, row/column parity and a stop bit,
as specified in the [EM4200 EM4100-compatible format](https://www.emmicroelectronic.com/sites/default/files/products/datasheets/em4200_ds.pdf).

RMT runs at 1 MHz with two hardware memory blocks and a finite frame loop.
Each half-bit is 32 measured carrier cycles rounded to microseconds. The requested
100–5000 ms limit is rounded down to complete frames; no infinite loop is used.
The 256-byte payload resides in internal RAM for ISR access and remains valid
until channel shutdown. The antenna reservation protects this shared buffer.
Normal cleanup deletes RMT resources and restores GPIO11's idle load-channel
LEDC route. It must never route the reader carrier to GPIO11 after emulation.
Failed cleanup retains ownership, reports `requires_reset`, and requires reboot
before another RF operation; an in-flight buffer is never freed.

The lab console exposes this as `rfid emutest [duration_ms] [invert]`, using
synthetic ID `0102030405` and a no-sleep hold. `invert` is 0 or 1 and defaults
to 0. This does not enable the saved-tag emulation UI. Command completion proves
only waveform submission and cleanup; independent reader decoding is required
for RF acceptance. See [Hardware Lab](../hardware_lab/README.md) for the test.

Run `python3 tools/highboy/test_rf_ownership.py` for concurrent LF/HF claims,
failed NFC OFF/startup rollback/deinit, and RFID result/cancel/reopen/failure
regressions. These tests use real host mutexes and explicit hardware/scheduler
fakes. They do not certify antenna switching electrically.

## Exclusive companion reservations

`rf_frontend_reserve(mode)` atomically reserves an idle antenna for the calling
task. It refuses an active field, including one in the same mode. Until
`rf_frontend_unreserve(mode)` succeeds, claims and access from another task or
mode fail. Native claims remain idempotent within their existing unreserved mode.
The reservation survives a driver's successful field-off; unreserve is a separate
step and requires the active mode to be idle. Failed OFF therefore retains both
the stage claim and its external reservation.

`rf_frontend_can_access(mode)` is checked by LF public hardware operations and
NFC field lifecycle/SPI transfers. NFC initialization temporarily reserves the
antenna unless its caller already owns a reservation, avoiding a competing lazy
initialization. The companion uses the reservation across initialization, probe
and shutdown, and holds a no-sleep reference until cleanup succeeds. See the
[NFC/RFID commands](../host_link/rf-commands.md) for recovery and validation.

The [ABI 1.10 NFC/RFID reads](../app_sdk/README.md#native-nfcrfid-api-abi-110)
execute on the same persistent worker as companion capture. Reservations and
retained failed-OFF cleanup belong to that firmware task, so app exit does not
orphan antenna ownership. A forced app kill must quiesce the local-call gate
before deleting the caller; the worker's fixed request buffers outlive apps.

## Integrated lab tag screen

The full `lab/rf` firmware has a [switch and tag bench](../rf_chamber/passive.md)
with active NFC identity polling and EM4100 reads. The standalone UART image is
not needed for these on-device tests. `test_lf_decoder.py` and `test_lf_reader.py`
in `tools/hardware_lab` test actual decoder/capture code against explicit host
waveforms, clock variation, wrapping timestamps, task stalls, corrupted frames,
overflow and injected peripheral failures. These are software checks, not a
claim that the analogue path can read a physical tag.
