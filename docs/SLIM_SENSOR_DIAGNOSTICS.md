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

## Temporary supply-mode diagnostic (0.16.3)

Rev3A sheet 5 connects nRF P1.09 to nPM1100 MODE (NRF_NPM_Mode), with an external
pull-down. Existing firmware leaves that pin disconnected. Nordic specifies
maximum output ripple of 80 mVpp in hysteretic operation versus 10 mVpp in PWM;
STCC4 specifies maximum unloaded supply ripple of 30 mV. This is a specification
compatibility concern, not a measurement proving ripple on either assembled board.
Sources: [Nordic BUCK specification](https://docs.nordicsemi.com/r/bundle/ps_npm1100/page/chapters/core_components/buck/doc/frontpage.html)
and [STCC4 datasheet](https://sensirion.com/resource/datasheet/STCC4).

BLE command 0x35 accepts one action byte: 0 restores the disconnected GPIO and
schematic default; 1 requests forced PWM for 300 seconds; 2 queries. Response
payload is six bytes: schema 1, signed big-endian 16-bit errno, active byte,
big-endian 16-bit remaining seconds. Missing or malformed actions fail without
enabling PWM. Enabling reads back the pin; errors are returned. Repeated enable
renews the lease. A delayed work item restores the default independently of BLE,
retrying failed restoration each second. Query reports restoration errors. No
voltage-selection pins, calibration commands or persistent settings are touched.
Reboot restores the default; production startup does not enable PWM.

Nordic allows changing MODE during operation. The diagnostic waits 100 us after
asserting MODE, exceeding the specified 55 us transition. Revision-1 PMIC erratum
[5](https://docs.nordicsemi.com/r/bundle/errata_npm1100_rev1/page/err/npm1100/rev1/latest/anomaly_100_5.html)
describes a possible 200 mV transient with a >30 mA load increase in the first
55 us; the assembled PMIC revision is not identified. No supply voltage setting
changes are made by the diagnostic.

Signed production build and all 31 firmware runtime tests pass. Tests cover
lease expiry/renewal, GPIO configuration/readback failure, restoration retry,
malformed requests and BLE encoding. Physical observations are recorded below.

### Continuous / single-shot / continuous comparison on 0.16.2

Both devices were observed in mode 3, then on-demand mode 0 with one immediate
measurement every approximately 11 seconds (including command duration), then
returned to mode 3. Excluding the first three samples of each phase:

| Phase | Older Slim mean (range) | New Slim mean (range) |
| --- | --- | --- |
| Continuous before | 119.8 (91–139) ppm | 2011.5 (1975–2040) ppm |
| Single shot | 153.5 (98–180) ppm | 1946.5 (1850–2036) ppm |
| Continuous after | 41.9 (13–161) ppm | 1918.1 (1901–1941) ppm |

The large separation persisted in both modes. Mode transitions changed readings,
particularly on the older sensor, so these brief phases do not establish long-term
accuracy or prove complete thermal equilibrium. Both original settings packets
and zero corrections were verified unchanged after restoration. No calibration
commands were issued. Evidence: `mode-comparison.jsonl` and summary JSON.

### 0.16.3 installation status — physical PWM test not yet run

Both signed uploads completed and their staged image hashes matched the manifest.
The first attempts timed out on the initial upload request; retries with a longer
initial-request timeout completed. C58CC9 accepted a test-boot request and reset,
but subsequent scans and a direct-address connection could not find it. Its
running version/startup confirmation therefore remains unverified. 81A9DC accepted
the test request (0.16.3 pending, 0.16.2 still active); no reset was requested for
that device while the first device was unavailable. The user was asked to check
whether the phone had reconnected. No 0x35 enable command has been sent, so there
are no physical PWM results yet. Do not describe the regulator hypothesis as
verified. Resume by verifying the first device's startup before rebooting the
second, then run `pwm_compare.py` with before/during/automatic-expiry observations.

The user asked whether to erase everything. No erase was performed. MCU flash
and sensor-internal calibration history are separate reset domains; the existing
matched settings and zero offsets make an indiscriminate MCU erase a poor first
experiment. Preserve backups and a comparison device before any future reset of
persistent state. Sensor factory reset/FRC remains outside the authorized tests.

## User erase/reflash follow-up and Bluetooth recovery

The user subsequently erased internal firmware/settings and reflashed both boards.
The local provisioning record identifies a rebuild of 0.16.3 at git 0428bb50f036
(image hash bfaae3e8020e7334c2aa178b0c42ca25daf8a73368292ded5d09bd2c8901a15f).
This differs from the earlier packaged candidate's build hash; it is not evidence
of a calibration change. Default mode 1 / 180-second sampling replaced the prior
mode 3 setting after settings reset.

Initially only Screen appeared in scans. Neither Slim was returned by macOS's
connected-peripheral query for the NUS service. Direct connections using known
CoreBluetooth peripheral objects timed out. The user confirmed phone disconnection,
button-light responses and proximity to the Mac. J-Link RTT identified C58CC9,
confirmed 0.16.3, successful sensor/ASC-pause startup and reported advertising.
Fresh sensor logs continued. A live RAM snapshot showed advertising true,
advertising suppression false, connection null, PWM inactive and CPU not halted.
A non-erasing nrfutil reset restored observable advertisements and working BLE.
The same non-erasing restart recovered BLE on 81A9DC. This is a recovery observation,
not an established root cause for the advertising failure.

Both devices subsequently returned identical settings packets, zero corrections,
software ASC off, firmware 0.16.3 and ASC-paused status 0084. The user disconnected
J-Link and returned both devices beside Screen, off chargers, for the comparison
below. All diagnostic logger connections were stopped before that comparison.

### Physical PWM experiment after erase/reflash

Both devices ran their restored default single-shot mode (setting 1). The client
requested fresh measurements approximately every 10.8 seconds without saving a
mode change. It recorded 13 baseline samples, enabled PWM with one 300-second
lease per device, recorded 31 samples across that lease and expiry, then recorded
13 more samples. Screen was read approximately every 10 seconds throughout.
No calibration, settings writes, sensor resets or self-tests occurred in this run.
Both devices reported charging false throughout; battery readings were 70–71%
for C58CC9 and 66–67% for 81A9DC. J-Link was disconnected by the user first.

Values below are means of the final six readings in each phase. PWM uses only
readings before 295 seconds into the lease, excluding samples after expiry.

| Phase | Older Slim C58CC9 | New Slim 81A9DC | Screen | Slim-to-Slim gap |
| --- | --- | --- | --- | --- |
| Normal before | 469.7 ppm | 1876.5 ppm | 1000.5 ppm | 1406.8 ppm |
| PWM, late in lease | 545.5 ppm | 1973.7 ppm | 1012.2 ppm | 1428.2 ppm |
| Normal after expiry | 505.5 ppm | 1938.8 ppm | 1017.0 ppm | 1433.3 ppm |

Final six-reading ranges were 501–510 ppm and 1935–1942 ppm, respectively.
Final temperatures were 24.60–24.65 C and 24.58–24.65 C. PWM coincided with a small
temperature rise and CO2 shifts on both devices, but did not close the large gap.
This does not prove the supply meets the ripple specification: no oscilloscope
measurement was taken. It does not establish that every power-related error is
absent. It rejects this short regulator-mode intervention as a fix for the observed
large separation under these conditions.

Both devices acknowledged PWM with pin readback and a 300-second lease. Queries
confirmed automatic expiry to inactive/zero remaining before any explicit off
command. Final explicit off requests also succeeded. Full settings packets and
correction packets matched the initial values; final ASC diagnostics were 0084
with no error on both. No protocol/command errors occurred during the experiment.
Thus MCU/settings erase and this supply-mode change did not resolve the disparity.

Evidence: `pwm-post-erase.jsonl`, `pwm-post-erase-summary.json`, the paired
`pwm_compare_post_erase.py` script, and the RTT/BLE recovery logs in
`local_backups/slim-divergence-20260905/`.

Remaining diagnostic: compare responses at a second CO2 level without changing
calibration. A stable additive difference across levels would support a baseline
error hypothesis, but would not by itself identify its physical origin. The
[manufacturer testing guide](https://sensirion.com/media/documents/D0ABC18E/6A1D5183/CD_AN_STCC4_Testing-Guide_D1.pdf)
requires initial operation/warm-up and reference calibration before formal sensor
qualification; its FRC step was not performed because the authorized investigation
preserves sensor calibration history. Screen remains a comparison device, not a
certified reference in this experiment. Do not claim absolute accuracy or a
confirmed manufacturing/calibration root cause from these observations alone.

## High mode: one-second environmental compensation

The user requested testing 5-second reporting with 1-second temperature/humidity
and pressure input. Before this change, compensation occurred only during a full
measurement cycle. Furthermore, startup selected the saved interval but left
STCC4 in single-shot operation until a BLE power-mode command arrived. Thus a
saved setting of 3 did not by itself prove continuous sensor operation.

0.16.4 introduced one-second compensation ticks in continuous mode with 5-second
report deadlines. Compensation-only ticks do not run the CO2 callback, battery
ADC or software ASC algorithm. Environmental errors invalidate affected reports;
non-finite or failed inputs are not substituted with invented compensation data.
Startup verification now sends the measured environmental input before reading
ASC status. New read-only BLE 0x36 exposes continuous mode, report interval and
compensation success/failure counts and observed gaps. Its payload is schema 1,
continuous byte, then five big-endian uint32 values: report interval (ms), success
count, failure count, last gap (ms), maximum gap (ms).

Both devices installed and confirmed 0.16.4 via OTA. Physical counters showed
1-second updates after explicitly selecting mode 3, but a 5001 ms historical gap
exposed the missing startup hardware-mode restoration. The preliminary capture
was stopped to correct boot behavior, not used as a completed five-minute test.

0.16.5 applied the saved hardware mode at boot, including an equal configured
interval. Both boards booted it but produced no periodic reports and automatically
rolled back to confirmed 0.16.4. That candidate is withdrawn. Review of Zephyr's
work implementation found that asynchronous cancel of already-running work sets
CANCELLING; the subsequent k_work_schedule call silently ignores it. A worker
waiting for meas_mutex during the boot mode change can hit that path. The initial
host harness did not model cancellation state. Its regression scenario now does.

0.16.6 replaces cancel-plus-schedule with k_work_reschedule for cadence changes,
retaining mutex serialization. The signed production build and all 47 current
firmware tests pass, including main's actual startup block for all saved modes,
equal-interval hardware transitions and the running-worker cancellation case.
No Flutter code changed. The device's public setting and existing reading packet
layouts remain compatible; the new diagnostic command is additive.

### Completed 0.16.6 physical OTA test

Both C58CC9 and 81A9DC installed 0.16.6 over BLE and reported that image active
and confirmed after the five-minute capture. The active image digest on both was
`487201b24f9ac350612e8ceaff9ea6b58a8f308087c27b6f59247712fc3aca01`.
No power-mode command was sent after reboot: saved mode 3 restored continuous
operation automatically. Each Slim delivered 61 unsolicited CO2 notifications.

| Observed quantity | C58CC9 | 81A9DC |
| --- | --- | --- |
| Mean notification interval | 5.0003 s | 5.0001 s |
| Notification interval range | 4.951–5.041 s | 4.977–5.040 s |
| Maximum compensation gap since mode startup | 1001 ms | 1001 ms |
| Successful compensation updates at end | 316 | 316 |
| Compensation failures | 0 | 0 |
| Unavailable reading sentinels | 0 | 0 |
| Battery, not charging | 69% | 65% |

Both final settings and correction response bytes matched their initial values:
software ASC off, manual correction zero, and High mode retained. Final sensor
diagnostics reported ASC paused (status 0x0084) without an error on both. There
were no BLE command errors. The clients disconnected at completion, leaving both
devices running in High mode. No FRC, sensor factory reset, offset adjustment or
temporary PWM command was issued during this capture.

The last 12 readings averaged 23.8 ppm (19–32) on C58CC9, 1435.1 ppm
(1423–1445) on 81A9DC and 805.5 ppm (800–812) on Screen. The remaining Slim
mean difference was 1411.3 ppm. These are short post-reboot observations, not
stabilized accuracy results. Screen also fell from 882 to 800 ppm during the
capture; environmental changes and sensor settling prevent attributing changes
from earlier experiments solely to this firmware. No unavailable sentinel does
not imply a physically accurate measurement. The substantial discrepancy remains
unresolved despite verified timing and paused internal ASC.

Screen was polled for its cached value about every 5.13 seconds; this does not
verify Screen's internal sampling or compensation cadence. The 1-second
compensation and 5-second unsolicited reporting measurements above apply to Slim.

Evidence is in `local_backups/slim-divergence-20260905/`:
`high_mode_final_notifications.py`, `high-mode-final-one-second.jsonl`,
`summarize_high_mode.py` and `high-mode-final-summary.json`. These local diagnostic
captures are not included in the release archive.
