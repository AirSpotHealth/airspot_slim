# 0.16.9 — flash wake timing fix, read-only diagnostic candidate

The AT25XE161D B9 sleep command enters ultra-deep power-down when SR4.PDM=0.
The previous caller-side 50-us delay was too short. The wake helper now waits
2 ms after a successful AB command, covering the datasheet's 1200-us worst case
for short sleep intervals. On 81A9DC, SR4=01 and the same CRC-valid metadata read
as zero after 50 us but correctly after 200, 1200 and 2000 us, at both 10-ms and
600-ms sleep durations. BLE history reads now agree with the raw flash contents.

CONFIG_AIRSPOT_FORENSIC_HISTORY remains enabled: external program/erase operations
are blocked. This is NOT a production logging release. The pressure sensor still
fails startup; neither normal measurements nor logging have been restored.

All 50 tests and the signed production-wiring build passed. J-Link installation
preserved settings with readback verification. The raw metadata+first-history
sector capture is byte-identical before/after this update. Four metadata entries
pass CRC (counts 0, 32, 64, 96); the first history sector is entirely FF. The
counter does not prove 96 stored readings exist. No CO2 values were recovered.

See docs/SLIM_SENSOR_DIAGNOSTICS.md for the evidence and remaining limitations.
