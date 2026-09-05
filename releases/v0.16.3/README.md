# AirSpot Slim 0.16.3 — temporary supply-mode diagnostic

Adds diagnostic BLE command 0x35 to select nPM1100 forced PWM for five minutes,
query its state, or restore automatic operation. Default power mode is unchanged.
The lease expires even after BLE disconnect; reboot restores the schematic default.
GPIO readback and command errors are reported. No calibration or NVS operation is
performed by this command. Internal STCC4 ASC remains paused.

This is an investigation candidate, not an established CO2 accuracy fix.

Validation: signed production build and all 31 firmware runtime tests passed.
The new runtime harness covers default state, malformed requests, GPIO failures,
readback, lease renewal, expiry, retry after restoration failure, and explicit off.
No Flutter code changed; the app does not invoke this diagnostic command.
Physical experiment results are recorded separately in the investigation document.

Image digest: `33bfa3ecc90703dd11d3ac6665ce0e4cf88da12fad69488c204c238d72aae616`.

See [investigation and protocol details](../../docs/SLIM_SENSOR_DIAGNOSTICS.md).
