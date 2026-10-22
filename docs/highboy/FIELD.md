# Field and consumer validation

This checkout currently produces paired **app-only development candidates**.
The physical P4 was erased for a separate test; runtime assets/settings and an
approved full restore package have not been established. There is no supported
consumer installation procedure for this candidate yet. Use the
[deployment boundary](README.md#flash-and-storage-boundary) before any restoration;
the generic upstream flashing commands do not apply here.

## Information to collect

Record board revision, P4/C5 identities, installed partition maps, candidate
commit and application hashes, asset-image provenance, SD state and test date.
For a failure, record the exact command or UI action, expected behavior, observed
behavior and relevant logs. This is the information missing from the original
“bash bug” in #158; a description on another PC has not been recovered.

Build evidence is in [TESTS.md](TESTS.md). The [integration matrix](MATRIX.md)
records which UI, native-app and companion paths actually exist. It must be
filled with observed results on the approved restored device; host fakes do not
supply physical results.

## Once an approved running device is available

| Area | Exercise | Record |
| --- | --- | --- |
| NFC | Known A/B/F/V tags; UI identification, `.hb`/script discovery and companion READ | Tag technology, consistent UID, absence behavior, errors and confirmed field shutdown |
| LF RFID | Known EM4100/EM4102 tag; UI, `rfread` and companion capture | Identifier consistency, sampling timeout, cancellation and field shutdown |
| RF contention | Start one interface, request access from another, then stop/disconnect/reopen | BUSY/error behavior, retained cleanup, successful recovery and no stranded owner |
| Wi-Fi | Scan, authorized connection/disconnection and bounded sessions through each implemented interface | Actual results, session cleanup, USB/BLE control latency and measured bytes/time |
| BLE | Scan/connect, companion attach/detach and HID mouse | Actual peer/device behavior, connection ownership and reconnect behavior |
| IR / Sub-GHz | Known receive/send fixtures and saved captures using existing UI/companion paths | Decoder/raw agreement, actual transmission and teardown; app matrix stays pending |
| LoRa | Each available mesh, failed sends and shared airtime pacing | Real packets, measured airtime, errors and retry behavior; MQTT broker/device acceptance remains pending; KISS is unsupported on this profile |
| Power | Battery diagnostics, external power, USB suspend and peripheral holds | Measured current and wake behavior; the current default keeps PM/light sleep disabled |
| Recovery | Normal boot, safe-mode entry and reviewed fault injection | Reset reason, crash/coredump availability, boot-loop handling and successful recovery |

The existing companion tester lists commands offline:

```sh
python3 tools/host_link/cli.py list
```

When using an approved running device, its authenticated NFC/RFID command
contract and current CLI examples are in [RF commands](../host_link/rf-commands.md).
Pairing uses handshake v2: fresh X25519, PSK-authenticated transcript proofs and
matching-code confirmation on the device before commands are accepted. Use the
updated clients; #151 is implemented. The `rfread` app and [JavaScript/Lua calls](../scripts/README.md#nfcrfid-discovery-bindings)
use the same receive capability and native backend. Do not interpret unavailable
functions as connected/working features. MQTT and remote GATT have native software
paths but still require real broker/peer acceptance; RNode/KISS is unsupported on
this profile. ABI 1.13 adds bounded app Sub-GHz capture/raw transmission through the shared
persistent RF worker. ABI 1.14 adds copied text submission to an already active
native LoRa mesh, and ABI 1.15 adds a bounded 80-bin Sub-GHz RSSI sweep.
Asynchronous/file app sessions remain unavailable. Check capabilities, selected frequency/preset
and cleanup results when exercising the implemented paths.

For #157, record `free`, `stack` and `tasks` at idle and after each exercised
feature, including repeated start/stop cycles. Retain minimum internal-heap and
stack-watermark readings alongside UI/radio latency and bytes/time measurements.
The [build-size checkpoint](SIZE.md) is static evidence and does not replace
these runtime measurements.

## Production handoff still required

A release needs its real app-signing public identity, key custody/recovery,
reviewed Secure Boot/Flash Encryption provisioning for both chips, approved
asset/boot restoration, paired transport results and performance/internal-RAM
measurements. The development trust key, a successful host suite or a fitting
application image does not satisfy those items. No eFuse provisioning, device
access or factory-reset action was performed for this work.


## Candidate integrity before an installed-device update

Build/package with `python3 tools/highboy/package.py` and verify its output using
`python3 tools/highboy/verify_package.py <candidate-directory>`. The package is
app-only and records the working tree's dirty state; it does not restore assets,
select production identities, provision security or certify a release. The P4
Update screen now performs native OTA from the actual SD file, replacing the old
mock version/progress/reboot. Matching installed boot/partition/assets and actual
rollback acceptance remain prerequisites. See the [candidate procedure](README.md#verifiable-app-only-candidates).


## UART console report clarified — issue 158

The user identified the original “bash bug” as UART line-editing corruption
while holding Backspace. It is no longer an unidentified shell-script report.
The actual pinned SDK editor reproduces this: its under-30-ms paste path
inserts DEL/control bytes as text and skips normal middle insertion. The P4
build selects a hash-pinned generated correction, without modifying ESP-IDF.
See [console editing and host regression](../console/README.md#editing-uart-input).

Forty fast/slow actual-editor host cases pass using explicit UART/time/output
fixtures and ASan/UBSan. This establishes software behavior, not physical
terminal/adapter/log-interference acceptance. The firmware provides an ESP-IDF
command console, not Bash/zsh syntax. Consumer restoration, production identity
and physical release requirements remain in [the release procedure](RELEASE.md);
closing the reported console defect does not satisfy those separate gates.
