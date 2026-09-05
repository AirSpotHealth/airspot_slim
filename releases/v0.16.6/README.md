# AirSpot Slim 0.16.6 — one-second compensation with reliable high-mode startup

High mode restores STCC4 continuous operation from saved settings on boot, sends
fresh temperature/humidity/pressure compensation every second, and publishes CO2
approximately every five seconds. It no longer cancels a potentially running
sensor worker when changing cadence; rescheduling replaces its deadline. This
avoids Zephyr's CANCELLING state suppressing the subsequent schedule operation.

Supersedes incomplete 0.16.4 and withdrawn 0.16.5 diagnostic candidates.
Read-only BLE command 0x36 returns schema 1, continuous flag, then five uint32
big-endian fields: report interval, successful compensation count, failed count,
latest gap and maximum gap. Intervals are milliseconds; counts wrap modulo 2^32.

Compensation-only ticks do not publish CO2, read battery ADC or feed software ASC.
Environmental input/write failures invalidate affected reports. Slow modes retain
their configured measurement intervals. Startup sends valid environmental input
before verifying the internal ASC pause.

OTA preserves settings, targets and manual corrections. Hardware ASC stays paused;
no forced recalibration, sensor factory reset or offset adjustment is performed.
PWM remains off by default. This corrects operation, not a claimed accuracy fix.

Validation: signed production build and all 47 current firmware tests pass.
Tests include the real main startup block, equal-interval hardware-mode changes,
and an already-running worker during mode change, modeling Zephyr's cancellation
semantics in addition to measurement/compensation cadence and I2C failures.
Physical OTA and cadence results are recorded in the investigation document.
No Flutter code changed; existing mode commands and measurement packets remain.

Image digest: `487201b24f9ac350612e8ceaff9ea6b58a8f308087c27b6f59247712fc3aca01`.
