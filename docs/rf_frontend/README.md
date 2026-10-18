# Shared RF frontend

The High Boy V2 LF 125 kHz and NFC 13.56 MHz stages share one physical antenna.
`firmware_p4/components/Drivers/rf_frontend` arbitrates the active mode.

`rf_frontend_claim()` rejects idle/out-of-range modes and returns
`ESP_ERR_INVALID_STATE` if the other stage owns the antenna. Reclaiming the same
mode is idempotent. `rf_frontend_release()` only releases the matching mode;
`rf_frontend_get_mode()` returns a coherent snapshot.

A statically initialized `portMUX_TYPE` protects the mode. No lazy mutex creation,
allocation or waiting is required. Critical sections contain only mode reads and
updates; logs and hardware operations run outside them.
