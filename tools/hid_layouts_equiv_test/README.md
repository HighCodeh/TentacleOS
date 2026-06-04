# HID layouts equivalence test

A host-side (no-hardware) proof that the table-driven refactor of
`firmware_p4/components/Applications/bad_usb/hid_layouts.c` is
**behavior-preserving**.

It compiles the **real, current** `hid_layouts.c` together with a **frozen copy
of the pre-refactor implementation** (`hid_layouts_reference_old.c`, captured
from `origin/main`). Both are driven with identical input while
`hid_hal_press_key()` is stubbed to record every `(keycode, modifier)` press.
The test asserts the two press logs are identical for:

- every single byte `0x01..0xFF`,
- every `0xC3 0xXX` two-byte sequence (the ABNT2 UTF-8 accent range),
- assorted other / malformed multibyte leads, and
- a set of curated real-world strings (incl. all ABNT2 accents and dead keys).

Exit code `0` = fully equivalent; `1` = at least one mismatch (printed with the
offending input bytes and both press sequences).

## Run it

Requires any C99/C11 host compiler (gcc or clang recommended).

```sh
# macOS / Linux / Git-Bash / WSL
./run_equiv.sh

# Windows PowerShell
powershell -ExecutionPolicy Bypass -File .\run_equiv.ps1
```

Expected output ends with:

```
==== HID layouts equivalence: 1048 cases, 0 mismatch(es) ====
RESULT: PASS  (new table-driven output == frozen old output)
```

## Files

| File | Role |
|------|------|
| `equiv_test.c` | Test driver + `hid_hal_press_key` logging stub + `main`. |
| `hid_layouts_reference_old.c` | Frozen pre-refactor oracle (do not edit). |
| `shims/` | Minimal stand-ins for TinyUSB / ESP-IDF / firmware headers so the real `hid_layouts.c` compiles on a host. |
| `run_equiv.sh` / `run_equiv.ps1` | Build + run. |

## Notes

- This directory lives under `tools/` (outside `components/`) on purpose: the
  BadUSB component's `CMakeLists.txt` uses `file(GLOB_RECURSE ... "bad_usb/*.c")`,
  so a `.c` placed inside the component would be swept into the firmware build.
- The shims define HID keycodes / modifier bitmasks with their real USB HID
  values. `HID_KEY_INTERNATIONAL_1` / `HID_KEY_NON_US_BACKSLASH` are left
  undefined so both translation units use their identical built-in fallbacks.
- If the legitimate pre-refactor behavior ever changes, regenerate
  `hid_layouts_reference_old.c` from history rather than editing it.
