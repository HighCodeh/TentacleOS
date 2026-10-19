# Remote GATT explorer

Open **Bluetooth → Detect → GATT Explorer**. A background P4 worker scans for
five seconds and lists real C5 scan results. Choose an address to connect,
discover primary services and enumerate their characteristics. The list shows
remote handles, complete UUIDs and readable properties. Select a readable
characteristic to issue an ATT read. A successful value appears as a hex prefix
(up to 16 bytes); an ATT error is reported without inventing a value.

BACK requests disconnect and returns to Detect. Leaving the screen through
another route also requests cancellation. The connection has an absolute
60-second C5 lease, including discovery and reads, so a lost P4 cannot keep it
open indefinitely. Reopen the explorer for another scan/session. Pairing,
protected-characteristic authorization, writes, notification subscriptions,
descriptor enumeration and ATT long reads are not implemented by this screen.
An error reading a protected attribute does not imply the peripheral is broken.

## C5 ownership and lifecycle

C5 runs connect, discovery, read and disconnect operations on the NimBLE event
queue. The SPI worker only enqueues requests or copies bounded snapshots under
a recursive mutex. NimBLE callbacks own the discovery state and copy results;
there is no JSON tree, storage writer or self-freeing worker stack. GATT callbacks
carry a generation to reject late results from an earlier connection.

The explorer refuses to replace an active Meshtastic, MeshCore or host-link
GATT server. While an explorer session or queued operation owns the stack, the
Bluetooth dispatcher rejects other Bluetooth commands with BUSY. Status, record,
read and stop operations remain available. Direct Bluetooth host shutdown also
refuses while the explorer is busy. Muse setup still permits GATT status and
cancellation. On port deinitialization, after the NimBLE host has stopped, the
explorer releases its event and lease resources before the NPL pool is destroyed.
A later host start reinitializes them.

Service/characteristic callbacks check connection, generation, state, error and
NULL result before copying. Only discovered readable value handles can be read.
Errors starting a procedure are retained in status; errors reading a value leave
the connection ready for another selection. Disconnect/expiry cancels pending
work, and termination is retried while shutdown remains pending.

## Bounded SPI contract

Both protocol-v8 headers reserve these Bluetooth operations:

| ID | Request | Response after status byte |
| --- | --- | --- |
| `0x0266` start | address[6], address_type u8 | none; queued only |
| `0x0276` status | empty | `spi_gatt_status_t` |
| `0x0277` record | index u8 | `spi_gatt_record_t` |
| `0x0278` read | discovered value handle u16 LE | none; queued only |
| `0x0279` stop | empty | none; queued only |

The packed status is 72 bytes: state u8, count u8, truncated flag u8, error u16,
value handle u16, value length u8, value[64]. All multi-byte fields are little
endian. States are IDLE=0, CONNECTING=1, DISCOVERING=2, READY=3, READING=4,
STOPPING=5 and ERROR=6. A read error can coexist with READY.

A record is 25 bytes: kind u8 (service=1, characteristic=2), UUID type u8
(16/32/128), properties u8, service start u16, service end u16, value handle u16,
UUID[16]. UUID bytes retain NimBLE's little-endian representation; unused bytes
are zero. Service records have no value handle. The cache holds at most 48 total
records; overflow sets the truncated flag. An ATT value longer than 64 bytes
returns its bounded prefix and an explicit size error. It does not silently
claim a complete long read. P4 validates response lengths, state, count and value
length before publishing snapshots.

P4 performs scans, SPI transfers and result polling on `SYS_CORE_RADIO` at
`SYS_PRIO_SERVICE_LO`; LVGL only copies snapshots and renders them. Its persistent
worker blocks when the screen is closed and cleanup has completed. Discovery
results are temporary and are not written to the old C5 GATT JSON loot path.

## Validation

`python3 tools/gatt/test_gatt.py` compiles the production C5 backend and P4 proxy
with ASan/UBSan and explicit NimBLE/event/transport fakes. It checks allocation
failure, cancellation before connection, lease failures/expiry, real callback
state transitions, ATT rejection, bounded values, stale generations, host
release/reinitialization and malformed SPI responses. The suite also checks the
native menu and screen routes. Paired SDK builds validate actual NimBLE API use.
Real BLE interoperability, RF behavior and cross-interface switching still need
the paired hardware under #156; these host tests do not connect to a peripheral.
