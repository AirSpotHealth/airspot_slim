# Slim pressure and low-voltage validation — 27 October 2026

Target: wired Slim D, nRF54L15, J-Link 603003276. Its own RTT logs and BLE
verification identify the advertised name as **AirSpot-C58CC9** (the earlier
D30E3C label was inferred incorrectly from a chip identifier).
The battery was disconnected. User initially controlled a bench supply, then
connected Power Profiler Kit II D29683E23A07 in Source Meter mode. Tests used
0.22.5 with AirSpot debug logging; kernel/library logging remained at error level.
These current captures include diagnostic logging and SWD and are not battery-life estimates.

## Observations before the fix

- Bench supply 4.2 V: device ADC approximately 4.147 V, pressure about 1011.7 hPa.
- Bench supply 3.6 V: 73 fresh pressure readings, 1011.71–1011.83 hPa, no errors;
  unfiltered ADC approximately 3.531 V.
- Initial bench 3.4 V attempt: logs stopped and J-Link VTref was 0 V. No pressure
  fault was captured; this attempt cannot establish a sensor failure.
- PPK2 3.4 V: fresh pressure readings around 1011.8 hPa at ADC 3.30 V / 1%.
- PPK2 3.3 V: clean low-battery lockout, ADC approximately 3.195 V.
- PPK2 cold start at 3.4 V: boot ADC 3.302 V, LPS22HH initialized and produced
  fresh readings without a pressure recovery attempt.
- PPK2 4.2 → 3.0 → 4.2 V: commanded dip from 14:38:10.390 to 14:38:15.500
  Australia/Sydney. Pressure continued successfully. At uptime 1:47.312 the ADC
  recorded 2.926 V while the display EMA remained 3.738 V. The device did not
  enter lockout during that dip.

The pressure failure was **not reproduced** on this device. Supply-setting/ADC
mismatch alone does not distinguish cable drop from ADC calibration; the 3 V
sensor rail was not measured with a scope.

## Confirmed protection defect and fix

`battery_monitor_poll` used a single one-minute deadline for both low-voltage
protection and battery reporting. Its voltage API already rejects a falling
supply hidden by EMA, but the monitor could miss an entire dip between polls.
0.22.6 separates the deadlines: protection polls once per second, sharing the
existing ADC cache with a five-second maximum sample age; routine BLE battery
reports remain once per minute, with immediate charger/subscription updates.
This is not a sub-millisecond brownout detector and does not prove a cure for
the reported pressure fault. ADC failures still produce no false healthy/empty
cell decision, and notification retries do not delay the protection deadline.

Regression test injects 2.926 V between minute reports and requires lockout
without an extra BLE battery notification.

## Local evidence

- `local_backups/pressure-0225-voltage-sweep.ppk2`: original PPK2 capture.
- `/private/tmp/slim-pressure-focused.rtt.log`: initial bench supply measurements.
- `/private/tmp/slim-pressure-ppk.rtt.log`: PPK2 3.4 V and 3.3 V lockout.
- `/private/tmp/slim-pressure-ppk-cold3400.rtt.log`: cold startup and 3.0 V dip.
- `local_backups/pressure-test-20261027.json`: test-session state.

## Hardware verification of 0.22.6

At 4.2 V, pressure readings resumed normally after installation. The repeat
3.0 V dip was commanded at 14:41:51.030 and restored to 4.2 V at 14:41:55.565.
At uptime 5:26.333 the ADC recorded 2.990 V (EMA still 3.877 V). Lockout was
logged at 5:26.486, about 153 ms later. STCC4 measurements were stopped at
5:27.704 and the sensor manager confirmed shutdown at 5:27.705. This validates
protection on this particular dip, not a guaranteed 153 ms detection latency
for arbitrary transients. Shared voltage samples can be up to five seconds old.
The expected lockout remains latched without a charger; restoring bench voltage
alone does not simulate the charger signal.

- Repeat RTT: `/private/tmp/slim-pressure-0226.rtt.log`.
- Repeat current capture: `local_backups/pressure-0226-protection-repeat.ppk2`.
- The new regression fails against the previous source and passes with the fix.
- Production build and all 123 tests passed.


RTT and test logs are also preserved in `local_backups/pressure-validation-20261027/`.

Final installation: 0.22.6 production, settings preserved and full image readback
verified. PPK2 left at 4.2 V with output enabled and capture stopped. Two live
RAM snapshots after startup confirmed valid CO2 and pressure, zero sensor fault,
and advancing measurement timestamps (117167 → 119167 ms). Pressure was
1011.79 → 1011.80 hPa, CO2 was 477 → 485 ppm, and battery ADC was 4099 mV.
The snapshots and J-Link output are preserved with the other local evidence.

## Battery-current comparison on production 0.22.6

PPK2 source at 3.7 V, battery disconnected, 100 kHz capture for 180 seconds per
mode. Both modes report every ten seconds and update compensation every second.
BLE mode acknowledgements were verified, then the central disconnected before
capture. The device advertised normally. No J-Link session was active; a cold
power cycle cleared debugger state before these measurements. Initial startup
and mode-transition periods were excluded. The first-hour white indicator was
active in both captures (2595 seconds remaining before single-shot capture).

| Mode | Average current | Peak current | Integrated charge | 75 mAh estimate | Assumed 60 mAh usable estimate |
| --- | ---: | ---: | ---: | ---: | ---: |
| Continuous | 0.97382 mA | 17.81 mA | 175.29 mC | 77.0 h / 3.21 days | 61.6 h / 2.57 days |
| Single shot | 0.29244 mA | 17.38 mA | 52.64 mC | 256.5 h / 10.69 days | 205.2 h / 8.55 days |

Runtime calculation: capacity in mAh divided by average current in mA.
Single-shot used 69.97% less current, giving a calculated 3.33× runtime at the
same usable capacity. The Rev3A schematic specifies a nominal 75 mAh cell;
60 mAh is an illustrative usable-capacity assumption, not a measured capacity.
These are fixed-voltage, short-capture estimates, not observed full-discharge
lifetimes. Actual capacity, cutoff, voltage-dependent load, temperature and
Bluetooth/LED workload affect endurance. They are not a qualified customer claim.

Raw PPK2 files, mode acknowledgements and machine-readable results are in
`local_backups/battery-comparison-20261027/`. Both captures contain 180 seconds
at 100,000 samples/second. Original continuous mode was restored and verified;
PPK2 was returned to 4.2 V, output enabled, sampling stopped.
