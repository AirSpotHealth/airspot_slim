# AirSpot Slim 0.16.7 — pressure startup diagnostics and recovery

Corrects pressure-driver error handling exposed by 81A9DC returning zero pressure
and failing environmental compensation at startup. Configuration errors and
measurement readiness timeouts now return errors instead of stale output bytes.
Temperature is read before pressure so PRESS_OUT_H is the last register read,
as required for BDU by the LPS22HH datasheet. Low-noise mode is configured only
after entering power-down. Readings outside the sensor's 260–1260 hPa range are
rejected without updating the last valid value.

If the first measurement fails, one bounded reset of the pressure sensor's
volatile configuration and retry is allowed. Register snapshots identify the
state before configuration and after failure. Persistent failures remain invalid;
no invented pressure is supplied to STCC4. This does not issue STCC4 reset/FRC,
change its calibration history, or modify AirSpot settings and stored readings.

The unchanged High-mode behavior reports CO2 every five seconds and updates
compensation every second. Software ASC preferences and manual corrections are
preserved. No app or BLE packet layouts changed.

This is a diagnostic candidate, not a claim that the disagreement between the
CO2 sensors is resolved. Physical findings are recorded in
`docs/SLIM_SENSOR_DIAGNOSTICS.md`.

Physical result on 81A9DC: the pressure sensor still does not become ready and
volatile reset times out. This candidate has not restored measurements. Settings
were verified byte-identical after J-Link installation; 96 stored records remain.
