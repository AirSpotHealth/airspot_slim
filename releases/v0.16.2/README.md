# AirSpot Slim 0.16.2 — sensor diagnostic candidate

Adds BLE command 0x34 to run STCC4's non-calibrating self-test under the sensor
measurement lock and restore the prior measurement mode. It preserves settings,
manual corrections and sensor calibration history; internal ASC remains paused.
Self-test 0010 is correctly recognized as a pass for host-connected SHT40.

This candidate does not fix or establish CO2 accuracy. The large disagreement
between two side-by-side Slims remains under investigation.

Validation: production signed-image build and all 30 firmware tests passed.
The runtime harness covers command errors, stopped/restored measurement mode,
manual-calibration exclusion, restoration failure, calibration preservation and
BLE response encoding. Both physical Slims (C58CC9 and 81A9DC) booted and confirmed 0.16.2, returned self-test
0010 with no command error, preserved settings/corrections and resumed readings.
No Flutter code changed; Flutter checks were not rerun for this diagnostic-only
command, which the app does not invoke.

Image digest: `251751bea766eebf08a16f446f0734e5df88cdae7c1e6df227c84c6c2062c0ea`.

See [investigation and protocol details](../../docs/SLIM_SENSOR_DIAGNOSTICS.md).
