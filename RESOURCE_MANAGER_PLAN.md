# Resource Manager (`resource_mgr`) - Design

Arbitration layer for the shared radios and peripherals, so that apps, the
on-device UI, and the companion (host_link) never drive the same hardware into
conflicting states, while still allowing the concurrency the hardware genuinely
supports (e.g. companion-over-BLE running at the same time as a BAD BLE HID app).

Status: Phase 1 and Phase 2 complete and building. The manager plus every radio
with a real consumer (WiFi, BLE, SubGHz, IR, USB HID) is wired, including the BLE
app ABI (`tos_api_ble.c`, ABI 1.3) and the companion-over-BLE reservation.
Companion piece to `APP_PLATFORM_PLAN.md`.

**Correction vs the first draft of this doc:** BLE is NOT multiplexed into
connection/advertising/scanner lanes on this hardware. The C5 `bt_dispatcher`
serialises the whole NimBLE host: starting a BT attack tears down every GATT
server, including the companion's BLE channel. So `RES_BLE` is a single exclusive
lane, and the companion is protected by priority, not by a reserved slot. The
table and worked example below reflect that reality.

## The problem

Every radio op funnels from the P4 to the C5 over the SPI bridge, or drives a
P4-local peripheral (SubGHz/CC1101, IR/RMT, USB HID, NFC). There is now more than
one consumer competing for that hardware:

- **apps** (via the ABI: `api->wifi->...`, future `api->ble->...`),
- the **on-device UI** attack screens,
- the **companion** over host_link (BLE or USB),
- the **firmware core** itself (connectivity for OTA, etc.).

A single "one owner per radio" mutex is wrong: it would refuse the legitimate
case where the companion is connected over BLE and the user launches a BAD BLE
app. The two can coexist because the BLE controller multiplexes roles. The model
must capture *what kind of concurrency each resource allows*, not treat every
radio as exclusive.

## Principles

1. **Per-resource concurrency semantics, not a global mutex.** Some resources are
   exclusive (one operation at a time); BLE is multiplexed into slots.
2. **The control channel is privileged.** The companion link reserves what it
   needs; an app can never starve it.
3. **Refuse or preempt by priority, and always say why.** No silent blocking. A
   denied acquire returns a typed error with a reason; a preempted owner is
   notified.
4. **Enforcement, not cooperation.** Preemption actually stops the loser's radio
   session; it does not rely on a (possibly buggy or hostile) app behaving.
5. **Reclaim on death.** An app that crashes or is force-killed, or a companion
   that disconnects, releases everything it held automatically.
6. **Passive library, no task.** The manager is a mutex-protected table. It owns
   no FreeRTOS task and never blocks under its lock. Acquire fails fast; queuing
   is a later addition.

## Resource model: lanes

Each resource has one or more **lanes**. A lane has a capacity (how many can run
at once) and a reservation (slots held for privileged owners). This one shape
covers both exclusive and multiplexed resources:

- An **exclusive** resource = one lane, capacity 1.
- **BLE** = three lanes: connections, advertising sets, scanner.

```
Resource     Lane        Capacity   Notes
---------    --------    --------   -----------------------------
RES_WIFI     (single)    1          one attack/mode at a time on the C5 WiFi radio
RES_BLE      (single)    1          the C5 serialises the whole NimBLE host
RES_SUBGHZ   (single)    1          CC1101, P4-local
RES_IR       (single)    1          RMT, P4-local
RES_NFC      (single)    1          reader stack not wired yet
RES_HID_USB  (single)    1          BadUSB
RES_LORA     (single)    1
```

Every resource is a single exclusive lane. BLE was originally drafted as three
lanes (connection/advertising/scanner) on the assumption NimBLE would multiplex;
the C5 `bt_dispatcher` does not: an attack command tears down the mesh and host
GATT servers (`bt_ensure_service_ready`), so only one BLE owner exists at a time.

WiFi and BLE are separate controllers (they share the 2.4 GHz front end via the
coexistence scheduler, but arbitration treats them independently): a WiFi attack
and a BLE companion link coexist without contending in this model.

## Owner and priority model

```c
typedef enum {
  RES_OWNER_SYSTEM = 0,  // firmware core: connectivity, OTA. Highest.
  RES_OWNER_COMPANION,   // the host_link control channel
  RES_OWNER_UI,          // on-device attack screen (user at the device)
  RES_OWNER_APP,         // a loaded app
} res_owner_kind_t;
```

Default preemption priority (tunable):

```
SYSTEM     = 30
COMPANION  = 20
UI         = 20
APP        = 10
```

Rule: a request preempts a holder only if `req.prio > holder.prio`, the request
opted into preemption, and the resource lane is preemptible. Equal priority never
preempts (first-come wins; the later request gets BUSY). SYSTEM use is rare and
deliberate (OTA), so its ability to preempt an app mid-attack is acceptable and
logged.

An owner is identified by kind plus, for apps, the app's `TaskHandle_t`, which
ties resource ownership to the existing `tos_app_ctx` lifecycle. That is what
makes crash-reclaim free: the app-context teardown already runs on app exit; it
just also calls `resource_release_owner(task)`.

## Public API

New component `firmware_p4/components/Service/resource_mgr/`.

```c
// include/resource_mgr.h

typedef enum {
  RES_WIFI = 0, RES_BLE, RES_SUBGHZ, RES_IR, RES_NFC, RES_HID_USB, RES_LORA,
  RES_COUNT
} res_id_t;

// Lane indices are per-resource. Exclusive resources use lane 0.
enum { RES_BLE_LANE_CONN = 0, RES_BLE_LANE_ADV = 1, RES_BLE_LANE_SCAN = 2 };

typedef uint32_t res_handle_t;      // opaque grant token; 0 = invalid
#define RES_HANDLE_NONE 0u

typedef struct {
  res_id_t          id;
  uint8_t           lane;           // lane within the resource (0 for exclusive)
  res_owner_kind_t  owner;
  uint8_t           priority;       // 0 -> default for the owner kind
  bool              allow_preempt;  // may preempt a lower-priority holder
  void            (*on_revoke)(void *user); // called if this grant is preempted
  void             *user;
} res_request_t;

esp_err_t     resource_mgr_init(void);

// Acquire one slot. On success writes a handle and returns ESP_OK.
//   exclusive lane held by >= priority           -> ESP_ERR_INVALID_STATE (BUSY)
//   slotted lane with no free (non-reserved) slot -> ESP_ERR_NO_MEM
//   preemptible lower-priority holder             -> preempts, then ESP_OK
esp_err_t     resource_acquire(const res_request_t *req, res_handle_t *out_handle);
esp_err_t     resource_release(res_handle_t handle);

bool          resource_available(res_id_t id, uint8_t lane);

// Bulk reclaim (app exit / companion disconnect). Fires on_revoke for each.
void          resource_release_owner_task(TaskHandle_t task);      // apps
void          resource_release_owner_kind(res_owner_kind_t kind);  // companion/UI

// Introspection for a `resources` console command and a UI status row.
typedef struct {
  res_id_t id; uint8_t lane; uint8_t capacity; uint8_t used; uint8_t reserved;
  res_owner_kind_t holders[RES_MAX_SLOTS_PER_LANE];
} res_status_t;
int           resource_snapshot(res_status_t *out, int max);
```

## Internal structures

```c
// resource_mgr.c

typedef struct {
  const char *name;      // "", "conn", "adv", "scan"
  uint8_t     capacity;  // total slots (read from Kconfig for BLE)
  uint8_t     reserved;  // slots only privileged owners may take (companion conn)
} res_lane_desc_t;

typedef struct {
  res_id_t         id;
  const char      *name;
  bool             preemptible;   // exclusive radios: yes; you can steal them
  uint8_t          lane_count;
  res_lane_desc_t  lanes[RES_MAX_LANES];
} res_desc_t;

typedef struct {                  // one live grant
  bool             active;
  uint32_t         token;         // matches the handle; guards stale releases
  res_id_t         id;
  uint8_t          lane;
  res_owner_kind_t owner_kind;
  TaskHandle_t     owner_task;    // for apps; NULL otherwise
  uint8_t          priority;
  void           (*on_revoke)(void *user);
  void            *user;
} res_grant_t;

static const res_desc_t s_desc[RES_COUNT];        // static table above
static res_grant_t      s_grants[RES_MAX_GRANTS]; // small fixed pool
static SemaphoreHandle_t s_lock;                  // guards s_grants only
```

The descriptor table is static/const. Only the grants array is mutable, under
`s_lock`. The lock is held for microseconds and never across an SPI call.

## Algorithms

### acquire

```
lock
  desc  = s_desc[req.id]; lane = desc.lanes[req.lane]
  prio  = req.priority ? req.priority : default_for(req.owner_kind)
  used  = count active grants on (id, lane)
  free  = lane.capacity - used
  // reserved slots are only for privileged (companion) owners
  usable_free = free - (owner_is_privileged(req.owner_kind) ? 0 : lane.reserved_remaining)

  if usable_free > 0:
      g = alloc_grant(); fill(g, req, prio); handle = make_handle(g)
      unlock; return ESP_OK

  if desc.preemptible and req.allow_preempt:
      victim = lowest-priority active grant on (id, lane) with prio < req.prio
      if victim:
          capture victim {id, lane, owner_task, session-hint via on_revoke/user}
          mark victim inactive
          g = reuse victim slot; fill(g, req, prio); handle = make_handle(g)
          unlock
          fire victim.on_revoke(victim.user)   // outside the lock
          return ESP_OK
  unlock
  return (lane.capacity == 1) ? ESP_ERR_INVALID_STATE : ESP_ERR_NO_MEM
```

`on_revoke` runs outside the lock and must be quick (set a flag, post a queue
item). It is where the ABI layer sends the C5 `SESSION_STOP` for the preempted
session, guaranteeing the radio is actually freed even if the app ignores it.

### release / reclaim

`resource_release(handle)` validates token, marks inactive. `release_owner_*`
walks the grants, fires `on_revoke` for each match, marks them inactive. Both are
idempotent.

## Where it sits

```
   apps            UI screens        companion (host_link)      firmware core
  (ABI wrappers)  (attack UIs)       (dispatch handlers)        (connectivity)
        \              |                    |                        /
         \             |                    |                       /
          v            v                    v                      v
          +----------------- resource_mgr (acquire/release) -----------------+
          |   arbitration only: decides WHO may drive the hardware now       |
          +------------------------------|----------------------------------+
                                         | (once granted)
                     +-------------------+--------------------+
                     v                                        v
              spi_bridge -> C5 radios                 P4-local peripherals
              (WiFi, BLE)                             (SubGHz, IR, NFC, USB HID)
```

The manager is a **mediator every radio consumer consults before acting**. It is
not a transport wrapper: the SPI bridge, wifi_service, and the C5 firmware are
untouched. Each consumer acquires, then uses the hardware exactly as it does
today, then releases.

## Integration points

| Consumer | Change |
| --- | --- |
| `tos_api_wifi.c` | Acquire `RES_WIFI` (owner APP, task from ctx) before a session op; store `{handle, session}`; release on `session_stop` / teardown. On BUSY return `ESP_ERR_INVALID_STATE` to the app. |
| `tos_api_ble.c` (new) | Acquire the right BLE lane per op: HID/BAD BLE -> `adv` (+`conn` when target connects); spam -> `adv`; scan -> `scan`. |
| `tos_app_ctx.c` | Add `resource_release_owner_task(task)` to the existing app teardown path. Free crash-reclaim. |
| `tos_api.h` | Add lifecycle `int (*resource_lost)(void);` (sibling of `should_stop`) so a graceful app can react to preemption; minor-bump the ABI. Add the `const tos_ble_api_t *ble;` pointer when BLE lands. |
| host_link BLE server | On companion connect acquire `{RES_BLE, conn, COMPANION}` (reserved slot). On disconnect `resource_release_owner_kind(RES_OWNER_COMPANION)`. |
| UI attack screens | Acquire as `RES_OWNER_UI` before starting; release on exit. |
| wifi_service (connectivity) | Acquire `RES_WIFI` as `RES_OWNER_SYSTEM` for OTA/connectivity. |
| console | New `resources` command over `resource_snapshot` (observe/report, aligned with sys_monitor philosophy). |
| `sys_prio.h` | No change. The manager has no task. |

## Worked example: companion over BLE + a BAD BLE app

The hardware does not let these coexist, and the manager makes that a clean
refusal instead of the companion channel dropping underneath the user.

1. The companion transport is BLE. `host_link_ble_start` acquires
   `{RES_BLE, COMPANION}` (priority 20). The companion GATT is the single BLE
   owner.
2. User launches a BAD BLE (HID) app. `api->ble->hid_start()` tries to acquire
   `{RES_BLE, APP}` (priority 10). The lane is held by the companion at higher
   priority, so acquire returns BUSY, the ABI returns `ESP_ERR_INVALID_STATE`,
   and **the C5 `BT_HID_INIT` is never sent** - so `bt_ensure_service_ready`
   never tears down the companion GATT. The control channel survives.
3. If the companion transport is USB-CDC instead, nothing holds `RES_BLE`, so the
   same `hid_start()` acquires it and the BAD BLE runs freely.
4. Reverse order: an app already running a BLE attack (holds `{RES_BLE, APP}`),
   then the user switches the companion to BLE. `host_link_ble_start` acquires as
   COMPANION with preemption: it takes the lane, the app's `on_revoke` fires and
   stops the app's C5 session, and the companion comes up. The control channel
   wins.

WiFi attack while the companion is on BLE: different controller, no contention.
Two WiFi (or two BLE) attacks at once: capacity 1, the second gets BUSY.

## Threading and safety

- One mutex guards the grants array. Held for microseconds; never across an SPI
  bridge call or an `on_revoke`.
- Preemption captures what to stop under the lock, then stops it after unlocking.
- Fixed-size grant pool (no allocation on the hot path).
- All acquires are non-blocking in v1: success, BUSY, or NO_MEM. No queue, no
  wait, so no deadlock and no priority inversion.

## Phasing

1. **Exclusive leases + all real-consumer radios. DONE.** The generic manager
   (all resources modeled, exclusive + slotted lanes, BUSY + priority + enforced
   preemption + owner reclaim) plus every exclusive radio that has a live consumer:
   - **WiFi.** App path (`tos_api_wifi.c`): every session op holds `RES_WIFI` as
     `RES_OWNER_APP` for the session's life; `on_revoke` stops the C5 session;
     one-shot TX does a momentary acquire; two apps or an app-vs-UI clash returns
     BUSY / yields correctly. UI path (`spi_session.c`, the single choke point for
     all engine sessions): acquires `RES_WIFI` as `RES_OWNER_UI`, preempting a
     background app; releases on stop / loss. App teardown
     (`tos_app_mgr.c finish_slot`): `resource_release_owner_task` stops any radio a
     dead/killed app left running (also closes the pre-existing leak where an app's
     C5 session outlived the app).
   - **SubGHz** (`subghz_transmitter.c` TX, `subghz_receiver.c` RX): acquire
     `RES_SUBGHZ` at the TX/RX gates. TX and RX are already stop-before-start, so
     they never both hold it; a cross-tenant clash returns BUSY, which brute/replay
     and the UI already handle.
   - **IR** (`ir.c`): acquire `RES_IR` at `ir_tx_init`/`ir_rx_init`, release at the
     matching deinit and the shared `fail` label.
   - **USB HID / BadUSB** (`bad_usb.c`): acquire `RES_HID_USB` at `bad_usb_init`,
     release at `bad_usb_deinit`.
   - `resources` console command for live introspection.

   **Deferred on purpose (documented, not silent):**
   - **NFC / RFID.** The reader stacks (`st25r3916`, `ys_rfid2`) are not wired to
     any UI/console/companion path today (the NFC UI is simulated; hardware is
     touched only by diagnostics and boot-time init). `RES_NFC` is modeled and
     ready; wire it when the reader gets a real consumer. Arbitrating it now would
     guard nothing.
   - **`wifi_service` connectivity** (scan / connect / STA / hotspot). These share
     the one radio with the attacks but are a different, OS-critical mode (OTA and
     the companion link live here), and they are reached by app, UI, console, and
     system through the same four functions. Forcing them under the single
     exclusive `RES_WIFI` lane would create false conflicts (a UI flow that scans
     while an attack runs, OTA blocked by an app) and needs per-caller owner
     inference. The WiFi *attack* surface, which is the actual contended resource,
     is already fully arbitrated for both app and UI.
2. **BLE. DONE.** `RES_BLE` is a single exclusive lane (matching the C5's
   serialised NimBLE host). The BLE app ABI (`tos_api_ble.c`, `tos_ble_api_t`,
   ABI 1.3) exposes scan, connect/MAC/adv, the session attacks (sniffer, spam,
   flood, skimmer, tracker) and the BadBLE HID keyboard, each holding `RES_BLE`;
   `on_revoke` stops the C5 activity; app teardown reclaims it. `host_link_ble`
   holds `{RES_BLE, COMPANION}` while the companion uses the BLE transport, so an
   app attack gets BUSY instead of tearing down the companion channel. Also fixed:
   `spi_session` is generic (BLE attacks and the companion stream flow through it
   too), so it now picks `RES_WIFI` vs `RES_BLE` by op category.
   Not yet wired: the on-device UI BLE screens as `RES_OWNER_UI` (the P4 BLE
   engines under `Applications/bluetooth/` and `bluetooth_service` sniffer already
   go through `spi_session`, so those inherit arbitration; the non-session UI ops
   do not yet).
3. **Preemption notification + policy. DONE (re-scoped).**
   - `resource_lost()` in the ABI (`tos_api_t`, ABI 1.4): nonzero once a radio the
     app held was preempted; the revoke path sets a per-app flag
     (`tos_app_ctx_signal_resource_lost`), a successful acquire clears it. Sibling
     of `should_stop`. Documented in the SDK guide.
   - **Companion TX rate-limit: dropped, not needed.** It assumed the multiplex
     model. With BLE exclusive, an app that would contend with the companion gets
     BUSY instead of coexisting; WiFi and BLE are separate controllers whose RF
     time-slicing is handled by the hardware coexistence scheduler. A software
     rate-limit on top would only hurt attack throughput for no arbitration gain.
   - **User-facing status: the `resources` console command** covers introspection.
     A graphical header glyph is deferred: it needs a new icon asset and header
     plumbing for modest value over the console.
4. **Bounded-wait acquire. DONE.** `resource_acquire_timeout(req, timeout_ms,
   &h)` retries until the resource frees or the timeout elapses, for a caller that
   wants to wait rather than fail-fast. The default `resource_acquire` stays
   fail-fast (the right default for a security device); no caller waits today, the
   API is there for when one needs to.

## Mid-start preemption. RESOLVED.

`spi_session_start` acquires the radio before the C5 start round-trip. If a
higher-priority owner takes the radio during that round-trip, the grant is gone
by the time the session id comes back. `spi_session_start` now re-checks
`resource_handle_valid(new_handle)` after the start returns and, if the grant was
preempted, stops the just-started C5 session and returns invalid instead of
leaving an orphan the manager no longer tracks. This closes the window before
`wifi_service` connectivity is ever wired as `RES_OWNER_SYSTEM`.

## Remaining loose ends (each blocked on a real prerequisite)

- **`wifi_service` / `bluetooth_service` connectivity arbitration** (scan/connect
  as owner-inferred). This means moving acquisition down to the service layer with
  owner inference (APP via `tos_app_ctx`, else UI/system) and de-duping against the
  ABI wrappers, on OTA-critical and UI paths. It should be validated on hardware,
  not landed blind. The *attack* surface, the actual contended resource, is already
  arbitrated for app and UI.
- **On-device UI BLE non-session ops** (scan/adv/mac) as `RES_OWNER_UI`. Same
  owner-inference shape as above (the session ops already inherit arbitration via
  `spi_session`).
- **NFC / RFID.** No consumer wires the reader today (the NFC UI is simulated;
  hardware is touched only by diagnostics and boot). `RES_NFC` is modeled; wiring
  it now would arbitrate nothing.
- **Graphical radio-busy indicator** in the header. Needs a new icon asset; the
  `resources` console command already exposes the state.

## Files

New:
- `firmware_p4/components/Service/resource_mgr/include/resource_mgr.h`
- `firmware_p4/components/Service/resource_mgr/resource_mgr.c`
- `firmware_p4/components/Service/resource_mgr/CMakeLists.txt`

Touched (per the integration table): `tos_api_wifi.c`, `tos_api_ble.c` (new),
`tos_app_ctx.c`, `include/tos_api.h`, the host_link BLE server, the UI attack
screens, `wifi_service.c`, a new console command.

No change to `spi_protocol.h`, the SPI bridge, or the C5 firmware: arbitration is
entirely a P4-side concern in front of the existing transport.
