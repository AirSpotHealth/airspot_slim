# Slim history erase and recovery — 14 October 2026

The history erase path reset its RAM cursor but retained the external flash
metadata sector. Boot could consequently load an old page address and record
count after a successful erase. The fix invalidates metadata before erasing
history, resets the first/current cursor and record/sequence counters, and
persists an empty checkpoint before returning success. Erase/write failures
propagate to the caller. Firmware/DFU storage is outside the erased range.
An interrupted erase is not reported as success; remaining history can be
recovered by the existing full scan and the user can retry erasure.

Normal restart and preferences reset continue to preserve history and calibration.
The candidate firmware already retries sensor recovery after 30 seconds of
failure, with bounded backoff, and requires a fresh measurement for success.
This does not guarantee recovery from the LPS22HH stuck BOOT state.

81A9DC was electronically powered off through J-Link on 14 October: TP26
measured 0 V, a button press restored 3.3 V, and LPS22HH initialized and reported
1018 hPa. The unit was healthy immediately before this test, so this establishes
the power-off/wake mechanism, not recovery from a reproduced stuck state.

Mobile guidance changes apply only to Slim. They direct customers to the
existing Restart device action and Report Issue if the fault persists, and
remove the previous suggestion to reset sensor calibration. Screen behavior is
unchanged. The 0.19.2 candidate now integrates electronic power-off into Slim Factory Reset;
see SLIM_FACTORY_RESET_POWER_OFF.md. This mechanism requires charger removal
and a physical button press to wake.

Validation: production C erase function exercised with persistent-cursor state
and injected metadata erase, history erase, and checkpoint write failures;
existing history/recovery host tests; targeted Flutter tests and static analysis.
No physical history erase was performed on the customer's devices.
