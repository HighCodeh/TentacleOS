# JavaScript and Lua scripts

Developer → Scripts runs plain `.js` and `.lua` source files from
`/sdcard/scripts`. Developer → Apps remains the separate launcher for signed
native `.hb` applications, including apps that provide their own UI. Script files
do not need packaging, signing or compilation on a computer.

Copy the [JavaScript](../../tools/scripts/examples/hello.js) and
[Lua](../../tools/scripts/examples/hello.lua) examples into `/sdcard/scripts`, then
open Scripts, select a file and press OK. Output and real execution errors appear
on screen. OK stops a running script; BACK returns to the list and requests stop.
Leaving the Scripts screen also requests stop, except when the script takes over
the display through its ABI. Navigating away from that display revokes its lease
and requests stop. Closing the script display returns to Scripts. A second script cannot start until
the previous worker has released its interpreter and source buffer.

The browser lists up to 16 files in that directory. Names are limited to 63
characters using letters, numbers, `_`, `-` and `.`, with a lower-case `.js` or
`.lua` extension; names beginning with `.` and subdirectory paths are rejected.
An empty or unavailable directory displays instructions rather than sample runs.

## Languages and functions

JavaScript uses [MicroQuickJS](https://github.com/bellard/mquickjs), fixed at
`6d4d7eb6b99011d5aab6e88a9b99e8fd4d86c6e9`. It implements the upstream strict
ES5-oriented subset, with functions, arrays, objects, JSON, Math and typed arrays.
It is not Node.js or a browser: there is no DOM, module loader or timer event loop.
See the upstream README for syntax and compatibility limits.

Lua uses the official [Lua 5.4.9](https://www.lua.org/manual/5.4/) sources. Base,
table, string, math and UTF-8 libraries are available. `io`, `os`, `debug`,
`package`, coroutines, dynamic loading, `dofile`, `loadfile`, `load` and
`collectgarbage` are not exposed. Only text source is accepted.

Both languages provide:

| Function | Behavior |
| --- | --- |
| `print(...)` | Write values to the script transcript. JavaScript also provides `console.log(...)`. |
| `sleep(ms)` | Yield for an integer duration from 0 to 30000 milliseconds, checking cancellation during the wait. |
| `millis()` | Monotonic device uptime in milliseconds. JavaScript `Date.now()` uses wall-clock time separately. |

## Shared firmware ABI

Both languages expose `tos.call(method, ...)`. Calls go through the same const
`tos_api_t` returned by `tos_api_get()` for native `.hb` apps, with a task-bound
`tos_app_ctx_t`. The existing ABI checks capabilities and owns radio/display
leases; scripts do not call drivers or SPI operations directly. A failure to bind
that context prevents execution. The script adapter additionally validates
argument types, ranges and capability grants before invoking a function pointer.
The adapter consumes native ABI 1.11; new namespace pointers are appended while
older field offsets stay compatible.

Scripts request capabilities on their **first line**, using a strict hexadecimal
mask without trailing spaces:

```js
// @caps 0x08
tos.call("wifi.scan");
var ap = tos.call("wifi.ap_get", 0);
print(ap.ssid, ap.rssi);
```

```lua
-- @caps 0x08
tos.call("wifi.scan")
local ap = tos.call("wifi.ap_get", 0)
print(ap.ssid, ap.rssi)
```

| Mask | Permission |
| --- | --- |
| `0x01` | Read SD files |
| `0x04` | Transmit / control radios |
| `0x08` | Receive / scan radios |
| `0x10` | Send BLE HID keys |
| `0x20` | Control display and LED |

Runnable [JavaScript](../../tools/scripts/examples/wifi_scan.js) and
[Lua](../../tools/scripts/examples/wifi_scan.lua) scan examples are provided.

Combine bits (for example `0x09` for SD reads and radio reception). No header
means zero permissions. Unsupported bits, including filesystem writes and C
callback registration, reject the run. Requested permissions require Allow/Deny
confirmation **on every run**, initially selecting Deny. The source is loaded
once and retained while confirmation is pending; approval applies to those exact
bytes, not to a later SD read. Generation IDs reject stale dialog responses.
Waiting for consent has a 30-second deadline; approval starts a fresh 30-second
execution deadline. Stop or denial while waiting executes no script code.

Available methods retain their ABI names, using a dot for namespaces:

| Namespace | Methods |
| --- | --- |
| Lifecycle | `version`, `log`, `delay_ms`, `yield`, `should_stop`, `resource_lost`, `led_set` |
| Wi-Fi | `scan`, `ap_count`, `ap_get`, `is_connected`, `connected_ssid`, `connect`, `disconnect`, `sniffer_start`, `deauth_detect`, `probe_monitor`, `signal_monitor`, `deauth`, `deauth_broadcast`, `flood`, `beacon_spam`, `evil_twin`, `session_stop`, `channel_hop` |
| BLE | `scan`, `device_count`, `device_get`, `connect`, `disconnect`, `set_random_mac`, `adv_start`, `adv_stop`, `sniffer_start`, `spam`, `flood`, `skimmer_detect`, `tracker_detect`, `session_stop`, `hid_start`, `hid_stop`, `hid_is_connected`, `hid_send_key` |
| Filesystem | `list`, `size`, `open`, `read`, `seek`, `close` |
| UI | `screen_open`, `screen_close`, `metrics`, `label`, `rect`, `bar`, `set_text`, `set_value`, `set_pos`, `set_size`, `set_text_color`, `set_bg_color`, `set_hidden`, `del`, `poll_event`, `canvas_end` |

Use `wifi.*`, `ble.*`, `fs.*` and `ui.*` for namespace names. See
[`tos_api.h`](../../firmware_p4/components/Service/app_runtime/include/tos_api.h)
for parameter order. The script representations differ where C uses pointers:

- `version()` returns `{major, minor}`. Stop/connection predicates return booleans.
  `log(text)` uses the fixed SCRIPT tag and treats text as data, not a format string.
- MAC addresses are strings such as `"00:11:22:33:44:55"`, including in returned
  AP/device records. `ap_get(index)` and `device_get(index)` return records with
  the ABI field names. Indices start at zero in both languages.
- Successful `esp_err_t` calls return zero; failures throw a language exception
  with the ESP error code. Session-start methods return their session ID instead
  of using an output pointer. Pass that ID to the namespace's `session_stop`.
- `beacon_spam(null)` / `beacon_spam(nil)` selects the ABI's random-list mode.
- `fs.list(path, limit)` returns an array/list of directory records (limit 1–8).
  `fs.open(path)` returns an opaque integer token. At most four files can be open.
  `fs.read(token, count)` returns up to 128 byte values, preserving embedded NUL;
  an empty array/table means EOF. `fs.seek(token, offset)` uses absolute offsets
  through INT32_MAX. Closed tokens cannot be reused. Paths remain restricted by
  the ABI to `/sdcard`, and the interface is read-only.
- `ui.metrics()` returns `{width, height}`. `ui.poll_event()` returns `{key, action}`
  or null/nil when empty. Widgets use the existing opaque handles and root handle
  zero. The native UI backend continues to validate ownership and stale handles.
- Strings are limited to 256 UTF-8 bytes and cannot contain embedded NUL. Calls
  accept up to seven scalar arguments; integers must be finite and in the ABI
  parameter's range. Lua arrays returned by the adapter start at one.

Raw memory pointers, native C callbacks (`batch`, console/host-link registration),
image blobs and direct-panel canvas begin/blit remain native `.hb` facilities.
NFC, RFID, Sub-GHz, infrared and LoRa contracts currently have no published pointer
in `tos_api_t`, so scripts cannot reach them through this bridge yet. There are no
shell bindings. This is capability enforcement for firmware calls, not process or
hardware memory isolation for native apps.

The worker closes owned files, revokes remaining radio leases, closes its UI and
clears its LED before unbinding the context. Cleanup runs after success, error,
cancellation, timeout and interpreter allocation failure. A subsequent run starts
only after that cleanup completes.

## Execution limits and ownership

One worker runs at `SYS_PRIO_BACKGROUND_LO` on `SYS_CORE_RADIO`. It uses a
16 KiB PSRAM stack and deletes itself through `vTaskDeleteWithCaps()`.
Rendering stays on the UI core. The worker owns the source and interpreter;
UI callbacks read a synchronized snapshot and never touch VM state.

- Source: 1–65536 bytes, with embedded NUL rejected.
- Interpreter heap: 512 KiB. MicroQuickJS receives one fixed arena; Lua's custom
  allocator accounts for allocation headers and transient reallocation storage.
  Shrinking retains existing capacity so it cannot fail under memory pressure.
- Transcript: the first 4095 bytes, with a terminating NUL. Further output is
  discarded without allocating more memory.
- Runtime: 30 seconds, checked by VM hooks and interruptible sleep. Long native
  library calls can delay a cancellation check; this is cooperative execution.
- Lua C-call nesting: 32. Its instruction hook becomes more frequent after stop
  so protected calls cannot keep an ordinary VM loop running indefinitely.

The baseline OTA slots are preserved. To accommodate both interpreters, P4
Applications, Service, interpreters and cJSON marshalling use `-Os` and LTO.
The final link uses one LTO optimization unit for these components. LVGL uses
the same size policy with its draw/rasterizer sources explicitly retaining `-O2`. Drivers,
standalone Game Boy/DOOM components, pinned SDKs, C5 memory settings and paired
SPI v8 remain unchanged. This changes the optimization policy for P4 application
and service code: responsiveness and peripheral throughput still need device
acceptance; host tests cannot establish those measurements.

## Sources, generation and tests

Interpreter sources and licenses are included under `firmware_p4/vendor` for
offline builds. Their provenance records pin source hashes. MicroQuickJS has one
recorded portability patch to encode negative tagged integers without an
undefined signed shift. Its sources force-include standard `<stddef.h>` to use
standard `offsetof`; only vendor format/dangling-pointer warning classes are
demoted. First-party interpreter code retains the project's warning policy.

The native adapter is the nested `Service/script_engine` component. The worker
is in `Applications/scripts`, and the screen is in `ui/screens/dev`.

```sh
python3 tools/scripts/generate_js_stdlib.py --check
python3 tools/scripts/test_engine.py
python3 tools/scripts/test_runner.py
python3 tools/scripts/test_context.py
python3 tools/scripts/test_ui.py
bash tools/highboy/test.command
```

The host tests execute both real engines with ASan/UBSan. They cover functions,
collections, output, syntax/runtime failures, infinite loops, protected-call
cancellation, allocation failure, memory limits and recovery. Worker tests use
the production file/worker code and real engines, with task, clock and filesystem
fakes, to verify SD loading, busy ownership, task-creation failure, stop, timeout
and transcript bounds. ABI tests exercise the real JS/Lua marshalling, permission
denial, strict arguments, output records, binary reads and file cleanup. Context
tests exercise concurrent first binding, capacity exhaustion and resource-loss
callbacks against the production context implementation. UI callback tests cover
ABI handoff, asynchronous deletion, navigation cancellation and consent cleanup. No device or SD card is
accessed. The marshalling uses bounded cJSON trees outside the 512 KiB VM budget
(up to eight directory records or 128 byte nodes per call); trees are freed even
when conversion into Lua fails under memory pressure.

The generated 32/64-bit JavaScript ROM and atom tables are checked by rebuilding
with the host C compiler. When intentionally changing bindings, run
`python3 tools/scripts/generate_js_stdlib.py` and review both generated variants.

The [validated build record](../highboy/TESTS.md#shared-script-abi-build-record)
contains the paired application sizes, hashes and acceptance limits.

## NFC/RFID discovery bindings

ABI 1.10 adds `nfc.poll(timeout_ms)` and `rfid.read(timeout_ms)` to `tos.call` in
both languages. They require `radio-rx` (`@caps 0x08`) and use the same native
[RF API](../app_sdk/README.md#native-nfcrfid-api-abi-110) and stable firmware worker
as signed apps. `nfc.poll` returns `{protocol, uid_len, uid, atqa, sak}`, with UID
and ATQA as byte arrays. `rfid.read` returns `{id, protocol, bit_count, raw_value,
card_number, facility_code}`; the 40-bit raw identifier is exact in both language
number representations. Missing tags, invalid timeouts, contention, cancellation
and hardware errors raise the usual ABI error. NFC writes/emulation are not
script bindings. A firmware table older than 1.10 is rejected before reading the
new appended namespace pointers.

## Infrared bindings

ABI 1.11 adds `tos.call("ir.receive", timeout_ms)` and
`tos.call("ir.send", protocol, address, command, repeat)`. Receive returns a
record with those four fields. The bounds and ownership are shared with the
[native IR API](../app_sdk/README.md#native-ir-api-abi-111): decoded codes only,
RX timeout 1–1000 ms, explicit RX/TX approval and persistent-worker cleanup.
An older host table is rejected before its appended IR pointer is accessed.

## Radio observation bindings

ABI 1.12 adds `tos.call("subghz.rx_poll")` and `tos.call("lora.mesh_poll")`.
Both require `radio-rx` (`@caps 0x08`) and observe existing native activity;
they do not activate a radio. Sub-GHz returns
`{protocol, decoded, freq_hz, value, serial, bit_count, btn}`; LoRa chat returns
`{outgoing, who, text}`. Empty polls return JavaScript `null` or Lua `nil`.
Cursors are independent for each app/run; retained history and overwritten
backlog follow the [native observation contract](../app_sdk/README.md#native-radio-observations-abi-112).
Older ABI tables are rejected before their appended pointers are accessed.

## Bounded Sub-GHz bindings

ABI 1.13 adds `tos.call("subghz.receive", frequency_hz, preset, timeout_ms)`
with RX permission and `tos.call("subghz.tx_raw", frequency_hz, timings_csv)`
with TX permission. Capture returns a copied signal record; TX returns zero
only after completion. Both use the shared persistent-worker backend and
[bounded native contract](../app_sdk/README.md#bounded-sub-ghz-api-abi-113).
For example, `"350,-700,350"` represents three microsecond durations. Script
CSV input is limited to 128 pulses and the normal string limit; invalid or empty
items are rejected before a native call. No persistent session is created.


## Queued LoRa mesh text binding

ABI 1.14 adds `tos.call("lora.mesh_send_text", "hello")` in JavaScript and Lua,
requiring TX approval. Zero means acceptance into the same native four-entry
queue used by `.hb`, the native UI and companion chat. Messages are 1..159 bytes;
the native mesh must already be active. Full/busy queue, inactive session,
capability denial and older ABI versions fail the script call. Acceptance is
independent of script lifetime and does not confirm airtime or delivery. No
startup, protocol switch, raw PHY or shutdown binding is published. See the
[ABI contract](../app_sdk/README.md#queued-lora-mesh-text-abi-114).


## Bounded Sub-GHz spectrum binding

ABI 1.15 adds `tos.call("subghz.spectrum_capture", 433920000, 1000000)` with RX
approval. It returns `{start_hz, step_hz, dbm}` with exactly 80 RSSI numbers;
JavaScript indexes `dbm` from 0 and Lua from 1. The native
[range and sampling contract](../app_sdk/README.md#bounded-sub-ghz-spectrum-abi-115)
applies. Older ABI versions, missing permissions, busy radio and SPI/cleanup
errors fail the call without a partial record. This reads one sweep and creates
no persistent script session. RF acceptance still requires hardware.


## Complete IR bindings (ABI 1.16)

JavaScript and Lua now expose the same raw/file/AC ABI adapters:

| Call | Approval | Result |
| --- | --- | --- |
| `tos.call("ir.get_last_raw", max)` | radio-rx | signed timing array; max 1..128 |
| `tos.call("ir.send_raw", 38000, "350,-700,350")` | radio-tx | zero after native completion |
| `tos.call("ir.send_file", "/sdcard/one.ir")` | radio-tx + fs-read | zero after single-signal replay |
| `tos.call("ir.send_ac", 5, true, 2, 23, 3)` | radio-tx | HAIER, on, HEAT, 23 C, high fan |

Raw CSV uses the existing bounded signed-timing parser (at most 128 timings,
subject to script string size). Empty/trailing items, overflow and old/missing
ABI entries are rejected. See the [native IR limits](../app_sdk/README.md#complete-ir-adapters-abi-116)
for carrier, durations, file selection and retained cleanup.


## NFC block bindings (ABI 1.17)

`tos.call("nfc.read_block", 4, "FFFFFFFFFFFF")` returns 16 byte values, requiring
radio-rx. `tos.call("nfc.write_block", 4, "FFFFFFFFFFFF",
"CACACACACACACACACACACACACACACACA")` performs a verified data-block write with
radio-tx. Keys/data use exactly 12/32 hex characters, case-insensitive, without
separators. Bad input, old/missing tables and capability denial fail before the
native call. The [bounded Classic 1K contract](../app_sdk/README.md#nfc-classic-block-access-abi-117)
applies equally to JavaScript, Lua, native apps and companion commands.


## Sub-GHz raw snapshot binding (ABI 1.18)

`tos.call("subghz.get_raw", max)` returns a signed microsecond timing array in
JavaScript and Lua. RX approval is required; max is 1..128 in scripts. Empty
history returns an empty array. A capture larger than max fails without returning
a truncated signal. Call bounded `subghz.receive` first; this does not read the
native UI receiver's history. Older/missing ABI entries fail before dispatch.
See the [native snapshot contract](../app_sdk/README.md#sub-ghz-raw-snapshot-abi-118).


## Sub-GHz file replay binding (ABI 1.19)

`tos.call("subghz.replay", "/sdcard/capture.sub")` uses the shared native replay
adapter in JavaScript and Lua. Both TX and filesystem-read approvals are required.
Zero indicates native transmit completion. Missing/old ABI, malformed input,
unsupported encoder, radio contention and cleanup errors fail the script call.
The [native file and pulse limits](../app_sdk/README.md#sub-ghz-file-replay-abi-119)
apply; the script does not create an asynchronous session.


## LoRa node observation bindings (ABI 1.20)

`tos.call("lora.mesh_node_count")` returns a number;
`tos.call("lora.mesh_node_get", 0)` returns `{name, rssi_dbm, snr_db}`. Both require
RX approval and a running native mesh. The node index is zero-based in **both**
JavaScript and Lua. Missing records, inactive mesh, old/missing ABI and backend
lock errors fail the call. Names/metrics follow the
[copied-node ABI contract](../app_sdk/README.md#copied-lora-node-observations-abi-120).
The list may change between count and get; these calls do not start a session.


## Sub-GHz session bindings (ABI 1.21)

JavaScript and Lua share the owned session API, requiring RX approval:

```text
tos.call("subghz.rx_start", 433920000, 6)         -> session ID
tos.call("subghz.rx_poll")                       -> signal or null/nil
tos.call("subghz.set_frequency", 315000000)      -> zero
tos.call("subghz.session_stop", session)         -> zero
tos.call("subghz.spectrum_start", 433920000, 1000000) -> session ID
tos.call("subghz.spectrum_poll")                 -> {start_hz, step_hz, dbm} or null/nil
```

`dbm` contains exactly 80 values (JS from index 0, Lua from 1). Empty windows
return no new signal; errors fail the script call. Version/pointer guards reject
older firmware before dispatch. The session belongs to the script runner task,
and normal completion, cancellation or deadline unbinds that task and requests
firmware cleanup. No hardware callback references script memory after exit.

These are windowed background captures, with gaps and coalesced latest results.
Startup waits for readiness and stop waits for cleanup; timeouts may leave
firmware-owned cleanup pending. See the
[ABI limits](../app_sdk/README.md#owned-sub-ghz-sessions-abi-121).
