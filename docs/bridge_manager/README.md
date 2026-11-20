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

# Bridge manager — V2.03 physical recovery

The P4 supervisor initializes the SPI bridge in polling mode and monitors C5
availability. Default SPI remains 4 MHz, mode 0, protocol v2 / transport revision 1.
CRC, response caching, FETCH sizing and DMA ownership are unchanged.

`bridge_manager_init()` initializes the bridge and board controls, checks the C5
application/protocol version and starts one monitor task. Application version
mismatch is informational; no automatic firmware update occurs.
`bridge_manager_force_update()` is the explicit C5 SPI OTA entry point.

## Detection and budget

After a ten-second boot/maintenance grace period, the monitor sends a read-only
PING every 1.5 seconds, with a 500 ms deadline. Three consecutive transport
failures are required before considering a physical reset. A successful PING
clears the consecutive-failure counter. Local allocation errors, suspension and
maintenance do not count as reasons to reset C5. A responding but incompatible
peer stays unavailable instead of entering a reset loop.

P4 permits at most **two physical reset attempts per P4 boot**, at least **30
seconds apart**. Attempts include uncertain GPIO failures. Neither a successful
probe nor a C5 reboot/epoch change replenishes this budget. This bounds external
restarts independently of C5's existing internal recovery/restart budget.
After exhaustion, read-only probes continue; a peer that later recovers can be
negotiated without another reset. Restarting P4 creates a new budget.

## Exclusive recovery sequence

1. Atomically reserve recovery against maintenance holders. Other tasks cannot
   start commands, stream fetches or diagnostic clock changes during recovery.
   Only the supervisor's PING/version/protocol and BOARD_NEGOTIATE/STATE commands
   are admitted; callbacks cannot start an operation using the supervisor's task.
2. Drain the blocking master transaction under its mutex (500 ms acquisition
   limit). Confirm a normal PHY, no legacy session and inactive CS. CS readback
   explicitly enables its input buffer without replacing the peripheral output.
   **A stuck transaction/DMA or active CS refuses reset**; buffers are not freed
   and DMA is not forcibly aborted.
3. Invalidate transport sessions/sequence and advance the P4 link generation.
   Drain stream callbacks through the existing barrier (500 ms). Old fetched
   batches and late session-start results are rejected by generation checks.
4. Invalidate the active SPI application session, release its radio grant and
   invoke its existing lost callback. No unknown command is replayed.
5. For physical reset, drive P4 GPIO1 low to release C5 BOOT through T14; wait
   10 ms. Drive GPIO0 high through T13 to assert C5 reset for 20 ms, then low to
   release it. Always attempt release after an assertion error. These are project
   timing margins; validate them against the assembled reset circuit.
6. Wait 2 seconds before the first negotiation attempt. This is a boot settling
   delay, not proof of readiness. Fresh transport HELLO, protocol check and board
   capability/state negotiation must succeed before admitting normal traffic.
   If boot takes longer, subsequent PING probes can reestablish the link without
   another physical reset.
7. Board reconciliation cancels old pending intents and reports the new epoch and
   confirmed safe outputs. Old remote handles/uploads disappear on C5 reset;
   IR and peripheral clients reject their old epoch. Other consumers receive
   bridge errors and retain their existing timeout/lost-session handling.

A failed drain or callback barrier never authorizes GPIO reset. A failed
negotiation leaves the bridge down. There is no restore of old effects, radio
operations or upload data after reconnecting.

## OTA, programming and shutdown

`spi_bridge_maintenance_begin/end()` provide nested reset exclusion while still
allowing normal SPI traffic. P4 OTA, C5 OTA, explicit reboot and power-off acquire
it before their side effects. An already running recovery rejects a new OTA or
power-off request; the caller can retry. A ten-second grace period follows the
last maintenance release. Power-off retains its hold; the restart/shutdown hook
also permanently disables new automatic recovery.

`spi_bridge_recovery_disable()` latches automatic recovery off until P4 restarts.
Explicit ROM-download requests latch it even when their acknowledgement is lost.
An uncertain C5 OTA result, or failed P4 OTA cleanup, also latches it. This keeps
a silent ROM loader/flash writer from being mistaken for a hung app. Definitive
C5 DONE/ERROR and preflight failures release the scoped maintenance hold.
Manual SPI commands/updates remain available when recovery is disabled.

External programming uses C5's independent CP2105 port. Before deliberate external
programming with P4 running, explicitly enter download mode through the firmware
so recovery is inhibited, or keep P4 off. P4 cannot observe an external programmer's
intent. No UART P4→C5 flasher or automatic bootloader entry is implemented.

## Validation

`python3 tools/spi_transport/test_supervisor.py` executes production supervisor,
exclusion-gate and C5 updater code with ASan/UBSan fault injection. Cases include
isolated/persistent failure, local memory pressure, lock/CS/barrier refusal,
maintenance nesting, owner-only negotiation, generation invalidation, reset
failure, cooldown, nonrenewable budget, incompatible peers, unknown OTA outcome,
preflight cleanup and intentional ROM entry.

The GPIO suite tests reset polarity/order/delays and every GPIO failure; the board
suite tests safe renegotiation and discarded haptic intent. Existing transport
and DMA suites cover delayed replies, bad CRC, duplicates, lost replies, restart
and ownership. P4 OTA tests check the hold before flash writes and until restart.
These tests and builds do not qualify electrical timing. Bench acceptance still
requires logic-analyzer captures under radio/BLE/IR/audio load and fault injection.
See the [migration plan](../highboy/V2_03_MIGRATION_PLAN.md),
[SPI transport](../spi_bridge/README.md) and [board controls](../board_control/README.md).

Paired ESP-IDF v6.1 build passed on 2026-11-03: P4 `0x2c8110` bytes
(`0x7ef0` free), C5 `0x1fee30` bytes (`0x11d0` free). Original partitions,
paired protocol headers and C5 memory policy are preserved. No flashing was performed.
