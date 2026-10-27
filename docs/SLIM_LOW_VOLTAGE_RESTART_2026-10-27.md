# Slim X low-voltage investigation — 0.22.8

## Fixed

STCC4 startup now wakes the sensor even when the MCU has lost its RAM sleep
flag. Low-battery shutdown deliberately sleeps STCC4, but an MCU-only reset
does not remove sensor power. Previously the first stop/configuration command
could fail with `-EIO`; runtime recovery eventually recovered on the observed
boot, but initial startup failed. Initialization now uses the existing wake
command and wake delay before configuration. No calibration reset is involved.

## Hardware verification

Target: Slim X / AirSpot-81A9DC, J-Link 603003276, PPK2 D29683E23A07.

- On 0.22.7, 4.2 V → 3.0 V for approximately 4.2 seconds → 4.2 V
  triggered low-battery lockout and STCC4 sleep. An MCU-only reset then
  reproduced the STCC4 startup error while pressure initialization succeeded.
- On diagnostic 0.22.8, the same sequence (3.0 V for 4.87 seconds) triggered
  lockout at an ADC reading of 2903 mV. After restoring 4.2 V and resetting
  only the MCU, STCC4 woke before configuration and startup succeeded.
  After the normal startup discard period, fresh CO2 readings of 548–558 ppm
  and pressure of 1011 hPa were recorded. No startup failure occurred.
- The driver regression test fails before the fix and passes after it.
  All 125 firmware tests pass. Signed production and diagnostic builds verify
  as 0.22.8. Diagnostic logging is not enabled in the production image.

Production image hash:
`03f0a1d6ae03174a85c7a3a3b29da0eb0cc2efe5dc30e388adc9eb92b146b446`.

## Pressure fault remains unproven

The initial board state had an LPS22HH configuration readback mismatch
(diagnostic stage 10, `-EIO`), followed by reset timeout and CTRL_REG2=0x14.
WHO_AM_I still read 0xB3. Increasing supply to 4.2 V and MCU resets did not
clear it. Removing board power for approximately 9.2 seconds did; J-Link
reported VTref=0 during that power cut. Fresh pressure measurements resumed.

Subsequent controlled steps through 3.6, 3.4, 3.0, 2.9, 2.8 and 2.7 V,
plus a short power cut, did not reproduce that pressure configuration fault.
These tests do not establish low voltage as its cause or establish a firmware
fix for it. The same board's earlier pressure failure is recorded in
`SLIM_SENSOR_DIAGNOSTICS.md`. LPS22HH is directly powered from +3V0; an MCU
reset cannot independently power-cycle it. Capturing the local sensor rail
and bus during another occurrence is still needed to distinguish power,
hardware and sensor-state causes. PPK input voltage is not a measurement of
the sensor's local VDD/VDDIO transient behavior.

Raw local evidence is retained under
`local_backups/slim-x-startup-20261027/`, including before/after RTT logs,
the original retained diagnostics and PPK2 captures. J-Link and debug logging
were attached during these tests; these captures are not battery-life figures.
