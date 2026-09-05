# AirSpot Slim 0.16.4 — one-second compensation in high mode

> Superseded by 0.16.6: saved High mode did not activate continuous operation at boot without a BLE mode command.

High / 5-second mode now sends fresh SHT40 temperature/humidity and LPS22HH
pressure compensation to STCC4 every second while publishing sensor readings
approximately every five seconds. Compensation-only ticks do not publish CO2,
read battery ADC, or feed software auto-calibration. Single-shot modes keep their
configured sampling intervals. Work is serialized with refresh, reset and sensor
configuration; compensation failures invalidate affected reports. Startup now
sends compensation before the ASC-pause verification measurement.

Adds read-only BLE command 0x36 (schema 1): continuous flag, report interval,
successful compensation count, failed count, last gap and maximum gap. Integers
are 32-bit big-endian. This permits physical timing verification without SWD.

Internal ASC remains paused. Calibration values, history, software ASC preferences
and power-mode preferences are preserved during OTA. No forced recalibration or
sensor factory reset is performed. Regulator PWM remains off by default.

Validation: production signed-image build and all 46 current firmware tests pass,
including injected I2C failures, compensation/report cadence, manual refresh,
mode transitions, stop/reset behavior and diagnostic BLE encoding. No Flutter
changes; the app uses its existing power-mode command and reading packets.
Physical results are recorded in docs/SLIM_SENSOR_DIAGNOSTICS.md as available.
This change aligns compensation cadence; it does not establish CO2 accuracy.

Image digest: `37b56d742316ec2213611720b0619d5ae328842755d46a4ab175b7b60a13bdca`.
