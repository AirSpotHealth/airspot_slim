# Slim sensor diagnostics

Firmware 0.16.2 adds a non-calibrating STCC4 self-test for investigation of the
large disagreement between AirSpot-81A9DC and AirSpot-C58CC9. This is a diagnostic
candidate, not an accuracy fix. The existing measurement algorithm is unchanged.

Send NUS command `0x34`. Its response payload is five bytes:

| Bytes | Meaning |
| --- | --- |
| 0 | Schema version, 1 |
| 1–2 | Signed command/restoration errno, big-endian; 0 on success |
| 3–4 | Raw STCC4 self-test flags, big-endian; FFFF if unavailable |

Inspect flags only when errno is zero. Sensirion documents 0000 and 0010 as
successful tests. Bit 0 indicates supply voltage outside specifications;
bits 5–6 indicate memory errors. Bit 4 means the SHT sensor is not on the STCC4's
own controller bus, which is expected for Slim's host-connected SHT40.
A pass does not establish CO2 accuracy or rule out transient supply ripple.

The command serializes with measurement operations, rejects execution during
manual calibration, stops measurement for the test and restores its prior mode.
It never issues forced recalibration, enables internal ASC, writes settings or
erases sensor history. It restarts the software ASC observation window and
invalidates the cached reading until the next successful sample. If restoring
continuous mode fails, measurements remain unavailable until sensor recovery.

## Investigation, 5 September 2026

Both devices ran 0.16.1, with software ASC off, manual correction zero, Flight
Mode off, high sampling mode and verified internal ASC-paused status 0084.
The new board reported 66% battery; the other reported 69%, both not charging.
Both reported 1003 hPa. Their SHT40 readings were approximately 25.9 C / 45.6% RH
and 26.3 C / 44.2% RH respectively.

A three-minute experiment requested immediate measurements once per second,
thereby updating environmental compensation once per second without changing
saved settings. The new board went from 3279 to 3021 ppm; the other from 188 to
177 ppm. This did not resolve the discrepancy. The production continuous-mode
compensation update interval is five seconds; Sensirion specifies one second.

A read-only RAM inspection of the wired board found STCC4 compensation feedback
25.836 C / 45.532% RH, matching SHT40 25.817 C / 45.526% RH. Its sensor status was
0084. J-Link reported VTref 3.156 V; this is not a measurement of supply ripple
at STCC4. The schematic specifies a regulated 3.0 V sensor rail and explicitly
says the programmer must not supply 3.3 V. A cable-disconnected comparison was
requested; do not assume a change occurred without confirmation.

Both boards were subsequently updated over Bluetooth to the signed 0.16.2
candidate. Both returned command errno 0 and self-test flags 0010, the expected
pass for the external SHT40 topology. Neither reported supply-range or memory
faults. Device settings and zero correction were identical before and after the
self-test, and valid measurements resumed with ASC paused. A pass cannot rule out
rail ripple, thermal effects or baseline/calibration errors.

The root cause is not yet established. In particular, identical host firmware
and zero host corrections do not establish identical sensor-internal calibration
history. The STCC4 datasheet specifies initial operation up to 12 hours in
continuous mode followed by fresh-air exposure, and up to one hour of warm-up
after long idle periods. The three-minute software ASC guard does not replace
those requirements. No ASC activation or FRC was performed in this investigation.

Evidence is under ignored `local_backups/slim-divergence-20260905/`.

Sources:
- [Sensirion STCC4 datasheet](https://sensirion.com/resource/datasheet/STCC4), sections 1.1 and 3.4.12.
- [Sensirion design-in guidelines](https://sensirion.com/resource/user_guide/stcc4/design-in), sections 3–4.
- [Sensirion testing guidelines](https://sensirion.com/media/documents/D0ABC18E/6A1D5183/CD_AN_STCC4_Testing-Guide_D1.pdf).
- Board schematic: `docs/datasheets/AH-013-0001-SCH CO2 Monitor (Standard Build) PWA Schematics Rev3A.PDF`, sheets 5–7.

## Cable-disconnected follow-up

The user confirmed removal of the new Slim's programming cable. After an initial
Bluetooth-unavailable period, the new Slim was reachable without reconnecting
the programmer. Over 37 samples / three minutes it remained at 2383–2408 ppm,
with 66% battery and charging false. Correction stayed zero and ASC status 0084.
Thus removing the programmer did not resolve the high reading.

A nearby Screen comparison (12 readings) averaged 658.9 ppm; the older Slim
averaged 75.7 ppm in that preceding window. These are comparison readings, not a
certified reference measurement.

The first connection to the older Slim reported charging false / 69%. By the
start of its subsequent 37-sample observation it reported charging true / 78%,
ending at charging true / 80%. That observation was confounded by charging. Its
reported temperature rose to 28.14 C while the new Slim ended at 25.34 C.
Do not label this a matched battery-only or thermally stable comparison. A
follow-up requires both devices off chargers and untouched in matching enclosure
conditions. No calibration or firmware settings changed during this follow-up.

## Enclosed-box follow-up after user-reported >20 minutes

Read-only BLE capture collected 13 readings per device over roughly one minute:

| Device | Mean CO2 | CO2 range | Temperature | RH | Battery | Charging |
| --- | --- | --- | --- | --- | --- | --- |
| New Slim 81A9DC | 2096.9 ppm | 2087–2108 | 24.50–24.54 C | 48.00–48.07% | 66% | No |
| Older Slim C58CC9 | 153.7 ppm | 150–160 | 24.48–24.52 C | 47.55–47.62% | 71% | No |
| Screen AF94D2 | 645.7 ppm | 643–648 | Not read | Not read | 92% | No |

Both Slims retained firmware 0.16.2, zero correction, software ASC off,
Flight Mode off, high sampling mode and ASC-paused diagnostic status 0084.
No measurement resets, settings writes, self-tests or calibration commands were
issued during this capture. All BLE connections were closed afterward.

The discrepancy persists with matching reported temperature, similar humidity,
and both Slims off chargers. Battery percentage, programmer attachment and the
earlier charging-temperature difference are not sufficient explanations.
This does not measure sensor-rail ripple or the temperature gradient between
SHT40 and STCC4, and does not distinguish sensor-internal baseline/calibration
state from a physical sensor/board integration issue. Neither the prior self-test
passes nor paused-ASC status establish accuracy. Screen is a comparison device,
not a certified reference. No calibration adjustment is justified from this
single test environment alone.

Evidence: `box-settled.jsonl` and `box-settled-summary.json` in the investigation
backup directory.
