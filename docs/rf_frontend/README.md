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
measures carrier feedback with PCNT and reads EM4100 frames with parity checks.
Console diagnostics also provide passive listening, edge capture and modulation
checks. Pin assignments remain in `Drivers/pins/include/pin_def.h`.

The RFID screen reader runs on `SYS_CORE_RADIO` at `SYS_PRIO_SERVICE_LO`.
A critical section protects cancellation, generation, running state and the
result/tag pair. It publishes a coherent result only for the current generation.
The task owns its hardware from field start through field stop. Closing the
screen invalidates its generation; cleanup may take one 500 ms read interval.
Reopening while cleanup is active reports busy, and cannot launch a second
reader. Task-creation failure permits a later retry.

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
