// The former telemetry shim returned zero for VSYS, VBUS and current. The
// silicon-backed implementation now lives beside the register transport in
// bq25896.c so a complete snapshot is captured under one driver lock.
