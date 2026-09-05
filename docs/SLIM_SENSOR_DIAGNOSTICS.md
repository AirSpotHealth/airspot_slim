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
