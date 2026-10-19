# JavaScript and Lua scripts

Developer → Scripts runs plain `.js` and `.lua` source files from
`/sdcard/scripts`. Developer → Apps remains the separate launcher for signed
native `.hb` applications, including apps that provide their own UI. Script files
do not need packaging, signing or compilation on a computer.

Copy the [JavaScript](../../tools/scripts/examples/hello.js) and
[Lua](../../tools/scripts/examples/hello.lua) examples into `/sdcard/scripts`, then
open Scripts, select a file and press OK. Output and real execution errors appear
on screen. OK stops a running script; BACK returns to the list and requests stop.
Leaving the Scripts screen also requests stop. A second script cannot start until
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

No radio, HID, arbitrary filesystem, shell or custom UI bindings are introduced
by this runtime. `.hb` apps retain their existing ABI and capability model.

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
Applications and Service code now use `-Os` and LTO. LVGL uses the same size
policy with its draw/rasterizer sources explicitly retaining `-O2`. Drivers,
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
bash tools/highboy/test.command
```

The host tests execute both real engines with ASan/UBSan. They cover functions,
collections, output, syntax/runtime failures, infinite loops, protected-call
cancellation, allocation failure, memory limits and recovery. Worker tests use
the production file/worker code and real engines, with task, clock and filesystem
fakes, to verify SD loading, busy ownership, task-creation failure, stop, timeout
and transcript bounds. No device or SD card is accessed.

The generated 32/64-bit JavaScript ROM and atom tables are checked by rebuilding
with the host C compiler. When intentionally changing bindings, run
`python3 tools/scripts/generate_js_stdlib.py` and review both generated variants.
