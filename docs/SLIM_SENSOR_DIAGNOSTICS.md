# Slim sensor diagnostics

## Calibration correction investigation, 4 October 2026

The supplied Engine (AirSpot-E368FD, 0.17.7) exports show manual calibration
corrections of -30195, -31102 and -31610 ppm. Manual calibration saves
`target - average raw CO2`; the 426 ppm target and -31610 ppm correction imply
a raw average of 32036 ppm. From 12:32:16 to 14:16:16 the export contains 106
consecutive 1157 ppm samples, exactly `32767 - 31610`. This strongly supports
raw signed-output saturation masked by the host correction. It is not proof of
cached BLE samples. Sensor reset on 3 October exposed roughly 30000 ppm output
before another large correction was applied.

An arithmetic audit checked calibration input before host correction, signed
32-bit subtraction, bounded unsigned 32-bit sample accumulation, signed 16-bit
storage and one-time output correction. At 11:04/11:05 Engine changed from
1404 to 496 ppm while the correction changed from -30195 to -31102; the implied
raw values are 31599 and 31598 ppm. At 12:05/12:06 it changed from 947 to 443 ppm
with corrections -31102 and -31610; the implied raw values are 32049 and 32053.
These transitions support one additive correction, not double application.
The completion reply carries the correction delta; the correction query and
history carry the saved total. No overflow or double-correction cause was
reproduced in this audit. The inferred raw values are not direct I2C captures
from Engine, and its exact installed binary has not been obtained.

The Engine was reported mounted on a phone and heated in sunlight. Thermal
gradients, environmental compensation and sensor-internal calibration state
remain possible physical causes; the CO2-only exports cannot distinguish them.
The affected device is with the user's boss and unavailable for probe testing.

The connected J-Link board is AirSpot-81A9DC. Its installed application was
matched byte-for-byte to `application/build_recovery/application/zephyr/airspot_slim.hex`
over 270988 bytes; the local production image does not match. Two read-only RAM
snapshots showed raw CO2 1030 then 1036 ppm, correction zero, software ASC off,
status 0x0084, valid samples and timestamps advancing by 60000 ms. SHT40 and
STCC4 compensation feedback matched (approximately 23.1 C / 54.7% RH), with
1016 hPa pressure. This board did not reproduce the Engine failure. Buffered
RTT messages are historical and must not be presented as current observations.
No target settings, firmware or sensor calibration commands were changed.

The source fix rejects a manual-calibration session if any raw sample exceeds
5000 ppm, the upper end of the STCC4's specified accuracy range. The finish
function also rejects an out-of-range average. Rejection preserves the previous
correction, emits the existing calibration failure response and does not record
a successful new correction. Normal live CO2 readings above 5000 ppm remain
available; this is a calibration-input policy, not a live-value clamp. Existing
large saved corrections are not silently cleared. This prevents calibration
from concealing abnormal output; it does not establish or fix its physical cause.

Regression coverage replays raw means of 31602, 32036 and 32767 ppm, checks the
5000/5001 ppm boundary, rejects a spike whose average falls below the limit, and
verifies a subsequent clean session succeeds. All 75 host tests pass with the
Nordic SDK Python runtime, and the signed candidate builds and passes the
production image/layout validator. The candidate has not been installed or
qualified on the affected hardware. Diagnostic evidence and the isolated build
are under `/private/tmp/slim-jlink-20261004/`.

Sources: [STCC4 specifications](https://sensirion.com/products/catalog/STCC4),
[reference signed CO2 decoder](https://sensirion.github.io/python-i2c-stcc4/_modules/sensirion_i2c_stcc4/commands.html),
and [thermal design guidance](https://sensirion.com/resource/user_guide/stcc4/design-in).

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

## 2026-09-06: overnight follow-up blocked by pressure startup failure

Only Screen and subsequently 81A9DC were reachable. C58CC9 remained absent.
Screen read 491 ppm in the first capture and 527–528 ppm later; no valid paired
12-hour CO2 result was obtained. 81A9DC returned the 0xffff unavailable sentinel,
not an actual high CO2 value. Its cached measurement battery packet was zero
because no measurement had completed; direct battery-driver values were 3928 mV
at the captured initial boot, 4048 mV later, and 4123 mV at a normal restart.
The user confirmed removing it from the charger, explaining the later charging
flag. This is not evidence that it ran out of battery overnight.

J-Link read-only RAM inspection on 0.16.6 found diagnostic stage 6 / -ERANGE,
one compensation failure, zero successes and no valid CO2 sample. SHT40 cached
values were 27.73175 C / 28.34806% RH; LPS22HH cached pressure was zero and its
internal temperature was 0.1 C. The earlier log recorded a debug-related reset
(Zephyr RESET_DEBUG, 0x20), so uninterrupted overnight operation is unverified.
A deliberately requested normal MCU system reset also reproduced the failure.
No factory reset, settings reset, STCC4 reset or FRC command was sent.

The pressure driver had concrete defects: ignored trigger/configuration errors,
continued to read output after readiness timed out, and read pressure before
temperature despite the LPS22HH BDU requirement that PRESS_OUT_H be last.
Candidate 0.16.7 adds honest error propagation, the documented output read order,
range checking, register snapshots, and one bounded first-sample recovery through
pressure-sensor volatile reset/reconfiguration. Low-noise configuration is made
in power-down mode. There is no change to STCC4 calibration or settings/history.
The signed production build and 48 tests passed, including a register-level
runtime harness for readiness failures, stale-output rejection, reset/configuration
errors and bounded recovery. These tests establish driver behavior, not physical
sensor health.

OTA writes are rejected by the existing health gate when startup has failed.
The connected 81A9DC was therefore installed through the existing J-Link
preserve-settings path. Full RRAM backup and post-install readback are in
`local_backups/production-603003276-20260906T043544754645Z/`. The settings region
0x15c000–0x165000 compares byte-for-byte identical before/after. The separate
external flash was not erased; both boots recovered the same 96 records and
metadata sequence 4. Candidate 0.16.7 remains diagnostic: installation does not
mean startup recovered or that MCUboot health checks passed.

Physical 0.16.7 trace: WHO_AM_I reads B3 and all diagnostic register reads return
success. Before configuration CTRL_REG1/CTRL_REG2 are 00/10; after requested
02/12 writes they still read 00/10, and STATUS stays 00. The first measurement
returns -ETIMEDOUT (Zephyr -116). The bounded volatile-reset attempt also times
out. Thus the fault persists below the CO2 compensation algorithm: the pressure
sensor/control interface does not reach a usable measurement state. A correct
chip ID alone does not demonstrate a healthy sensor. These observations do not
separate a stuck powered state, power integrity, bus behavior and permanent
hardware damage, nor do they explain the previous CO2 disagreement.

The next diagnostic is a physical power removal (battery, charger and J-Link),
then restart. No data erase is required. User has been asked whether the battery
connector is accessible. Captures/scripts are saved under
`local_backups/slim-overnight-20260906/`, including the original RAM inspection,
0.16.6 restart log, 0.16.7 register trace and installation verification.

### Raw history verification supersedes the earlier record-count inference

The user declined battery disconnection and asked for persisted readings instead.
Downloading half-pages 0–7 with 0.16.7 returned zero payloads despite the current
half-page pointer being 6. The earlier statement that all 96 records were intact
was explicitly corrected: only the metadata count had been verified.

0.16.8 is a read-only forensic candidate. CONFIG_AIRSPOT_FORENSIC_HISTORY blocks
external-flash program/erase commands in the driver's write-enable function;
a runtime harness proves none of the mutation entry points reaches SPI. At boot
it dumps 0x64000–0x65fff in 32-byte reads and compares 128-byte read prefixes.
All 256 reads succeeded and all 64 prefix comparisons matched. Metadata contains
four CRC-valid entries: counts 0, 32, 64, 96 and timestamps 0, 9626, 19227, 28827.
These are uptime-scale values without a calendar date and cannot establish the
exact overnight stop time or prove battery depletion. The entire first history
sector (4096 bytes) is FF. No CO2 values were recovered from it.

The flash datasheet (AT25XE161D Rev M sections 4.9, 6.34 and 7.5) identifies a
wake timing mismatch: B9 selects UDPD when SR4.PDM=0. Resume is up to 200 us,
and up to 1200 us after short sleeps. The previous 50-us caller delays were not
sufficient. Candidate 0.16.9 moves a 2000-us wait into spi_flash_power_up itself,
and retains the external-flash mutation block. Its read-only physical probe
returned SR4=01 (PDM=0). Both 10-ms and 600-ms sleep experiments gave:

| Delay after AB wake command | Metadata first four bytes |
| --- | --- |
| 50 us | 00000000 (incorrect) |
| 200 us | 55aa55aa (correct) |
| 1200 us | 55aa55aa (correct) |
| 2000 us | 55aa55aa (correct) |

This directly reproduces the history retrieval defect. The same early-access
bug can also affect record programming; the old driver checks SPI transfer
success without verifying write-enable acceptance or programmed bytes. That is
a plausible explanation for advancing counters with blank history, but this
read-only experiment does not prove the original write/erase chronology.
No destructive write test was used to fill that evidence gap.

After the wake fix, all BLE history pages correctly returned FF (with protocol
reserved bytes normalized to zero), matching the blank raw sector. The 8192-byte
metadata+history dump before/after the diagnostic update compares identical,
SHA256 164ce06ffbacb86a07d4a5943c0cfff7c45f40190bedc0326b9fe1de602f7058.
Settings readback remained identical. 0.16.9 was installed through J-Link with
backup in local_backups/production-603003276-20260906T045240082187Z/.
The production-wiring signed build and 50 tests passed.

The pressure sensor still shows a separate startup fault: CTRL_REG2 read 14 on
later boots (SWRESET stuck), and it does not produce measurements. Nothing in the
flash timing experiment establishes the cause of the earlier CO2 disagreement.
0.16.8/0.16.9 are diagnostic builds with logging disabled; they must not be
represented as completed production releases or recovered measurement devices.

### Non-erasing power-isolation option identified

Rev 3A schematic sheet 7 connects U6 (LPS22HH) VDD/VDDIO directly to +3V0,
with C25/C42 decoupling; there is no independent MCU-controlled sensor switch.
A measurement at C25/C42 would check local supply delivery, which J-Link VTref
cannot establish. The user was asked whether a meter is available.

Further inspection of sheet 3 found nPM1100 SHPACT connected to NRF_NPM_SHPACT;
sheet 5 maps that net to P2.03 / TP41. Sheet 8 connects the button to SHPHLD.
Nordic's nPM1100 specification permits entering ship mode with SHPACT high for
at least 200 ms while VBUS is absent and SHPHLD high, then exiting through a
button press pulling SHPHLD low for at least 200 ms (or applying VBUS). This can
isolate battery power electronically without unplugging the battery connector.
It is different from MCU System OFF, which does not remove the sensor supply.

Read-only J-Link preflight found P2.03 configured as input (PIN_CNF=2), output
bit 3 low, and direction bit 3 clear. The diagnostic flash remains write-protected.
No ship-mode GPIO write has been made yet: user coordination is pending so they
can perform the physical wake step. J-Link may also need disconnecting briefly
after power-off to eliminate possible back-power through debug signals.

## Four-device comparison, 4 October 2026

The user placed F1D7C1, 2325D4, 81A9DC and Screen AF94D2 together in a box.
Read-only BLE queries and simultaneous passive notifications collected 36
samples per device across approximately three minutes. All four report mode 3,
zero software correction and software auto-calibration disabled. Each stream's
median sensor timestamp interval is five seconds. Slim ASC diagnostics report
stage 5, errno 0 and status 0x0084.

| Device | Firmware | Mean ppm | Range ppm | Reported temperature C | Reported RH % |
| --- | --- | --- | --- | --- | --- |
| Screen AF94D2 | 1.5.5 | 684.5 | 683–687 | Not captured | Not captured |
| Slim 81A9DC | 0.17.7 | 1063.0 | 1055–1076 | 23.11–23.22 | 55.89–56.06 |
| Slim 2325D4 | 0.17.1 | 1658.9 | 1632–1680 | 22.93–22.98 | 56.26–56.34 |
| Slim F1D7C1 | 0.17.1 | 1793.3 | 1742–1842 | 23.36–23.40 | 55.56–55.73 |

Slim pressure queries returned 1015–1016 hPa. F1D7C1 fell from 1838 to 1742 ppm;
this is not a fully settled accuracy qualification. The disagreement exists
with zero host correction, and the changing five-second samples exclude frozen
output in this capture. Matching reported environmental inputs do not establish
matching physical temperature at the gas sensing element. Screen is a comparison
unit, not an independently calibrated reference. Firmware version alone cannot
identify the cause without obtaining and comparing the installed binaries.

After the passive capture, non-calibrating self-tests passed on 81A9DC and
2325D4 (errno 0, flags 0x0010). F1D7C1 returned errno -5 and unavailable flags
0xffff; this is a failed diagnostic transaction, not a decoded supply/memory
fault. Its periodic stream subsequently stopped, requiring recovery checks.
2325D4 briefly returned 813 ppm after measurement restart, then returned toward
1500–1560 ppm. That transient is not a successful accuracy fix or evidence that
calibration changed. No manual calibration, FRC, factory erase or firmware
installation was performed during the comparison.

Evidence: `/private/tmp/slim-jlink-20261004/box-state.jsonl`,
`box-monitor.jsonl`, `box-summary.json`, `box-selftest.jsonl`, `box-post.jsonl`
and `f1-recover.jsonl`. Raw logs include independently advancing device clocks;
use host capture time for comparisons, not their absolute wall-clock alignment.

F1D7C1 recovery query confirmed an invalid `ffff` reading with timestamp zero
following its failed self-test. A controller-only command 0x27 rebooted the MCU;
no sensor-reset, calibration or erase command was sent. After startup, BLE
queries confirmed firmware 0.17.1, saved mode 3, correction zero and valid
1569 ppm output (device uptime timestamp 66 seconds). Thus the failed self-test
also exposed an operational recovery problem distinct from the preexisting CO2
disagreement. Initial reconnect was too early during boot; the subsequent scan
and connection succeeded. Follow-up evidence: `f1-check.jsonl` and
`f1-periodic.jsonl`. Absolute device clock was unset by reboot, so host timestamps
are necessary for the follow-up comparison.

## Settled outdoor follow-up, 5 October 2026

All four devices were reconnected after the user reported extended outdoor
exposure in an open box. A simultaneous passive two-minute capture collected
24 samples per device, with a median five-second timestamp interval. Settings
queries confirmed mode 3, correction zero, and Slim ASC status 0x0084 / errno 0.
No calibration, self-test, reset, firmware or settings writes were issued.

| Device | Mean ppm | Range ppm | Temperature C | RH % |
| --- | --- | --- | --- | --- |
| AirSpot-AF94D2 | 470.7 | 469–476 | Not captured | Not captured |
| AirSpot-81A9DC | 777.8 | 766–793 | 18.22–18.36 | 71.91–72.29 |
| AirSpot-F1D7C1 | 675.5 | 671–681 | 18.29–18.42 | 71.99–72.39 |
| AirSpot-2325D4 | 658.2 | 646–668 | 18.49–18.58 | 70.99–71.15 |

The earlier 30-minute recording did capture 2325D4: the second discovery
attempt succeeded. Final five-minute means were Screen 476.8, 81A9DC 793.5,
F1D7C1 1017.0 and 2325D4 919.2 ppm. The fresh follow-up shows additional
recovery of F1D7C1 and 2325D4 without changing their zero corrections. Thus the
large initial disagreement was time/environment dependent, not explained by
different saved additive corrections alone. Time, outdoor concentration,
thermal equilibration and internal sensor state remain confounded; this does
not prove which mechanism caused the recovery. Remaining deviations from Screen
are approximately +187, +205 and +307 ppm. Screen accuracy has not been verified.
The two 0.17.1 devices are now closer to each other than to 81A9DC (0.17.7);
version correlation alone is not evidence of a firmware regression.

Evidence under `/private/tmp/slim-jlink-20261004/`: `outdoor-monitor.jsonl`,
`outdoor-2325.jsonl`, `outdoor-settled-state.jsonl`,
`outdoor-settled-monitor.jsonl`, and `outdoor-settled-summary.json`.

## Shared Slim over-reading audit, 5 October 2026

The user identifies Screen AF94D2 as the comparison reference and prioritizes the
shared Slim positive bias over the additional unit-to-unit spread. The source
audit finds a shared calibration-policy risk, not a proved physical root cause:
`pause_stcc4_asc()` enables testing mode at startup and verifies status bit 2.
The STCC4 internal ASC remains paused regardless of the software ASC setting.
App manual calibration accumulates raw samples and saves `target - raw_mean`
as a host correction. `sensor_manager_perform_frc()` exists, but no application
caller invokes it, including factory-test and manual-calibration paths. There
is no driver disable-testing-mode path. This policy can retain a sensor-internal
baseline error; it does not by itself explain the direction or magnitude of the
observed positive bias. It may be intentional to avoid competing calibration
algorithms, so it must not be changed blindly.

Verified against the STCC4 datasheet: host RH input uses `(RH+6)*65535/125`,
temperature uses `(T+45)*65535/175`, and pressure uses hPa*100/2. SHT40 decoding
uses the corresponding correct output formulas. CO2 is already signed integer
ppm, decoded big endian and sent with one host correction. The known 81A9DC
installed image has startup conditioning enabled. Source high mode schedules
compensation at one-second intervals and reporting at five-second intervals;
the actual running cadence and received compensation must still be measured.

Sensirion specifies initial operation up to 12 hours followed by fresh-air
exposure, and warm-up up to one hour after long idle periods. Testing mode
pauses ASC state updates. The software ASC three-minute guard is not a
substitute for those sensor requirements; it also cannot explain current
software-ASC-disabled, zero-correction positive values as a host adjustment.
The design-in guide requires SHT4x and STCC4 to experience the same local
conditions, which matching reported temperatures between devices do not prove.

A read-only J-Link attempt found the probe but VTref 0.000 V; no target RAM
was read and no reset/write was issued. The user was asked to reconnect 81A9DC
with Screen beside it. Prepared read-only RAM capture commands are at
`/private/tmp/slim-jlink-20261004/shared-overread/read-feedback.jlink`; these use
addresses from the previously byte-verified 0.17.7 image, so identity/image must
be rechecked before using them on a newly connected unit. They compare host
SHT40 readings against temperature/RH echoed by STCC4, raw CO2, sample timestamps
and actual measurement-mode flags. A match verifies delivery of inputs, not
physical temperature equality at the gas sensing element or CO2 accuracy.

Primary sources: [STCC4 datasheet](https://sensirion.com/resource/datasheet/STCC4),
sections 1.1.2–1.1.4, 3.4.13–3.4.15 and 3.5;
[design-in guidance](https://sensirion.com/resource/user_guide/stcc4/design-in),
sections 1 and 3. No calibration-policy or firmware changes were made in this audit.

## Native ASC OTA experiment — 2026-10-05

User authorized an internal STCC4 ASC firmware experiment on 2325D4,
F1D7C1 and 81A9DC, without host offset calibration. Added an opt-in native
ASC policy, disable-testing command 0x3F3D, effective zero host correction,
and disabled host software/manual calibration while preserving saved settings.
All 79 host tests pass and signed artifact/layout validation passes.

Physical 2325D4 trial 0.17.8 failed stage 4, errno -13, status 0x0004 and
was confirmed to roll back to 0.17.1. Revised 0.17.9 drains old measurement
buffers and retries fresh verification shots, but physical 2325D4 still
returns stage 4, errno -13, status 0x0004. The image was not remotely confirmed.
Both candidates are withdrawn; F1D7C1 and 81A9DC were not updated.

Do not infer that ASC is active or that a baseline was saved from the app's
ASC setting alone: it reports the requested policy. Sensor verification
failed and must be resolved with physical command/response inspection.
The current Sensirion driver documents testing status byte 10 value 4;
the June 2025 datasheet describes the second most significant bit instead.
The device's original diagnostic status was 0x0084 and candidate status
0x0004. This discrepancy is recorded, not used to weaken the validity guard.

Once activation is verified, mode 3 is preferred for initial baseline
learning: continuous 1-second sensing, 5-second reporting. Datasheet first
ASC save is after 1 hour continuous / 360 shots, subsequent saves at maximum
2 hours continuous / 720 shots. Neither native baseline nor successful NVM
save is exposed by this firmware. Accuracy and persistence require later
matched-condition comparisons and an actual sensor power cycle.

0.17.9 rollback verified over SMP: 2325D4 active confirmed 0.17.1
(hash 44d7eff174e3aac57df502055524c57ee7d41b95a903b2d5853aaaa1aa9c0251);
0.17.9 inactive, not pending and not confirmed. See the withdrawn release
folder for installation, failed sensor verification and rollback evidence.

## J-Link identifies the ASC-checker defect — 2026-10-05

Connected unit identified as 2325D4 from RAM BLE name; VTref 3.154 V,
prior confirmed firmware 0.17.1. Debug-buffer readback reproduces the
0.17.9 rejection: disable-testing 0x3F3D accepted, fresh single-shot
status remains 0x0004. Continuous verification also returns 0x0004.

0.17.10 continuous verification could exceed the 5-second startup watchdog
in its three-attempt path; replaced by bounded completed-step keepalives
in 0.17.11, covered by a watchdog timing regression. 0.17.11 still rejected
the same status. Both candidates are withdrawn, with no multi-unit rollout.

0.17.12 fixes the production testing-mode mask to 0x0080 and adds a paired
startup check: enable-testing must produce a freshly measured set flag,
then disable-testing must clear it on fresh measurements. Unknown status
mappings fail rather than accepting a coincidentally clear native value.
On physical 2325D4, paused single-shot status is 0x0084, then native
continuous status is 0x0004. Thus 0x0004 stays present in both states and
the previous checker confused it with the testing flag. This is a proven
ASC verification defect, not proof of the cause of all CO2 over-reading.

2325D4 0.17.12 startup and fresh samples passed and MCUboot confirmation
was independently verified over SMP. Effective settings: mode 3, ASC on,
target 400, host correction zero. Four live CO2 samples 458/457/464/457 ppm
at 5-second cadence. Image hash:
9e27b01c182366cdc782bc3229d80859510bd59adbc4f0093ce987562acb4d65.
All 79 host tests and signed artifact/production layout validation pass.

Native ASC activation is now verified on 2325D4. Learned-baseline save,
long-term accuracy and post-power-cycle persistence remain to be tested.

Rollout completed: F1D7C1 and 81A9DC also independently verified active
confirmed 0.17.12 with the same image hash, native status 0x0004, mode 3,
effective correction zero and ASC on/target400. Their startup paired checks
are required for confirmation, so the observed production mapping is verified
on all three. F1 live samples: 402/402/406/397; 81: 441/412/410/414 ppm.
Screen AF94D2 was not flashed. Values were collected at different times
and are not yet a simultaneous accuracy comparison.

Post-update simultaneous one-minute capture: 12 values/device, Screen 612.8
ppm (612–613), 2325D4 485.7 (476–495), 81A9DC 406.8 (399–420), F1D7C1 393.0
(391–397). User explicitly confirms all four are together indoors. Thus this
is a startup comparison, not fresh-air baseline learning. Slims currently
under-read the Screen; shared accuracy issue is not declared fixed. All three
native ASC activations/firmware confirmations are verified, but settling,
fresh-air learning and post-power-cycle baseline persistence remain. User
advised shaded ventilated fresh air, uninterrupted mode 3 at least 2 hours,
no manual calibration or resets during that experiment.

## Native ASC follow-up — 2026-10-05 morning (Sydney)

Read-only BLE state queries and passive captures. All three Slims report
0.17.12, mode 3, native ASC requested/verified stage 5 errno 0 status 0x0004,
effective host correction zero and target400. Battery reports: 2325D4 69%,
F1D7C1 68%, 81A9DC 86%. Uptime timestamps approximately 28,400–28,900 seconds
indicate about eight hours of MCU operation since OTA, with no reboot observed.
This is consistent with normal sensor ASC save intervals, but actual NVM
saving/persistence is not directly exposed or verified.

Initial outdoor Slim means over 24 fresh samples: 2325D4 434.8 ppm, 81A9DC
428.5 ppm, F1D7C1 454.0 ppm (25.5 ppm spread). Screen was unreachable because
its battery had run out; user put it on charge. Its first 8 reads averaged
537.9 ppm. The Screen's location relative to outdoor Slims was not verified
then, so those reads are not used as an outdoor same-air reference.

User then explicitly moved all four together indoors. Passive transition
and five-minute captures show all responding to the higher indoor CO2.
Final-minute means: Screen 622 ppm; 2325D4 544.2;
81A9DC 518.1; F1D7C1 542.2 ppm.
See followup-2026-10-05/indoor-summary.json for ranges and gaps.
The large shared Slim over-reading is absent in this check, and Slims are
much closer to each other. A common indoor under-reading relative to the
Screen remains; accuracy is not declared fixed. Reference had recently
restarted and charged, and Slim temperatures changed after relocation.
No calibration, power-mode change, MCU reboot or sensor reset was sent.
Screen power-on/charging was done by the user. Comparisons use host receive
timestamps; Slim packets carry uptime and Screen packets carry its RTC.

## 2026-10-05 — App-controlled native ASC candidate 0.18.0

User requested an app/firmware native ASC setting with manual calibration hidden
when enabled. Implemented distinct BLE command 0x38 (read/set) with sensor-verified
state, signed operation error and production testing flag; the old software ASC
command 0x11 is not relabelled as native ASC. Firmware supports runtime enabling
and pausing, verifies fresh measurements before publishing/persisting policy,
restores hardware on storage failure, and invalidates the pipeline if restoration
fails. A shared calibration lease prevents concurrent manual and native changes.
Duplicate verified requests do not interrupt measurement/learning.

ON bypasses saved host offsets, disables host software learning, and rejects
manual target/start and FRC. OFF after ON pauses ASC while retaining the sensor
baseline and bypassing historical host offsets. Only successful deliberate manual
calibration commits a new host correction. The app hides manual target/start and
reset while ON, unconfirmed, loading, disconnected or failed. Firmware capability
is gated at 0.18.0; fixed experimental 0.17.12 remains locked against manual use.
The app refreshes native state on reconnect and after manual completion. Screen
calibration behavior is unchanged. No optimistic native preference is stored.

Saved native policy uses tagged reserved bytes in the same 49-byte CRC snapshot.
The packaged native experiment build defaults ON for the three existing 0.17.12
units until a policy is explicitly saved; an ordinary build without the overlay
defaults OFF/manual. Stored host offsets and sensor calibration history are
preserved by the toggle. Native NVM baseline/event readout remains unavailable;
this verifies learning policy, not calibration completion.

Validation: 80 firmware host tests (including runtime sensor/write rollback,
unknown flag mapping, watchdog bounds, manual/FRC rejection, persisted mode,
manual correction transition, malformed BLE requests), 19 Flutter protocol/widget
tests, clean changed-file static analysis, signed image/production layout checks.
Signed candidate and exact patch against pre-feature dirty files are in
releases/v0.18.0-native-asc-controls. Not installed/published during this request;
physical toggle/persistence checks and app installation remain pending. No BLE
settings, calibration, reset or firmware mutations were issued during this feature
implementation. Existing live devices remain on their previous firmware/settings.

## 2026-10-05 — Authorized 0.18.0 OTA rollout and hardware control test

User authorized updating all four Slims while running the modified phone app.
2325D4, F1D7C1, 81A9DC and BE56FF now report 0.18.0 and the exact active,
self-confirmed signed image hash
974053a2550cb986e0bf659408c18b7b4ce69d35c1223e412f41b2274ec210c8.
Native 0x38 state is verified ON on all four, effective host offset is zero,
and each provided fresh CO2 samples. First three retain five-second reporting;
BE56FF retains one-minute reporting. Screen AF94D2 was not updated.

BE56FF's +165 ppm previous correction was recorded before OTA and is preserved
in storage, bypassed in native mode. Initial scan missed it; retry uploaded
the complete signed image. Reset disconnected before its SMP reply, followed
by successful new-version, hash, confirmation and sensor-state verification.

2325D4 physical test: verified ON, wrote OFF, verified paused sensor status,
restarted the MCU, confirmed OFF survived, restored ON and verified sensor
status. No manual calibration, FRC or history erase was sent. This proves
policy persistence across an MCU restart, not sensor baseline save/completion.
Phone app installation and live UI are user-managed and not independently
verified in this rollout. No firmware server publication was performed.
Evidence: releases/v0.18.0-native-asc-controls/rollout-2026-10-05/.

## 2026-10-05 15:05–15:09 Sydney — Outdoor follow-up after 0.18.0

User confirmed all four Slims and Screen remained outdoors together in shade.
Read-only BLE capture found Screen AF94D2 and Slims 2325D4, 81A9DC; a second
scan reached F1D7C1. All three reached Slims report 0.18.0, five-second mode,
verified native ASC ON (mode 2, status 0x0004, no operation error), and effective
host correction zero. Fresh packets arrived throughout approximately two-minute
captures. No calibration, reset, firmware update or setting change was sent.

Screen mean 428.3 ppm; 2325D4 mean 410.4 ppm; 81A9DC mean 420.1 ppm.
F1D7C1's exact mean/range is recorded in the linked summary. The captures partly
overlap; device packet timestamps are RTC-like after phone synchronization, so
comparisons use host receive times. This is an encouraging outdoor agreement
check, not proof of baseline adjustment/save or indoor accuracy. BE56FF was
unreachable on repeated scans and direct lookup; its current result is unknown.
Evidence: releases/v0.18.0-native-asc-controls/outdoor-followup-2026-10-05/.

## 2026-10-05 16:18–16:24 Sydney — Settled indoor check and ASC pause

User requested another indoor check after settling. Three reachable Slims
remained native ASC ON: user confirmed they had not changed any settings.
Read-only approximately two-minute captures gave Screen AF94D2 mean 598.9 ppm
(range 593–604), 81A9DC 579.7 (572–591), F1D7C1 576.9 (559–586),
2325D4 539.0 (525–556). Captures partly overlap. Slims report 0.18.0,
five-second reporting and effective host correction zero. Temperatures near
25.2–25.3 C do not establish a compensation fault. Large shared over-reading
is absent in this run; 2325D4 remains approximately 60 ppm below the Screen.

For the ongoing indoor baseline-hold experiment, native ASC was then paused
sequentially on 81A9DC, F1D7C1 and 2325D4. Each readback verified mode 1,
operation error zero and sensor status 0x0084 (testing/paused flag set),
with effective host offset still zero. Short fresh samples were captured after
pausing. No manual calibration, FRC, reset or history erase was issued.
The main numerical comparison precedes pausing; it is not described as an
ASC-OFF controlled comparison. BE56FF was intermittently detected but either
missing from scans or disconnected during connection; no pause verified and
no new valid measurement. Its current state is unknown.
Evidence: releases/v0.18.0-native-asc-controls/indoor-settled-2026-10-05/.

### Later ASC-OFF capture, approximately 16:25–16:26 Sydney

After pausing and allowing the immediate restart transient to pass, a new
one-minute capture verified all three reachable Slims remained native mode 1
with sensor status 0x0084, host offset zero, five-second reporting and no
operation error. Exact readings are in summary.json/later_asc_off_samples.
F1D7C1 remained substantially lower than its previous ASC-ON capture (roughly
504 vs 577 ppm), while Screen declined only roughly 599 to 585 ppm. This
change is observed but not explained; different time windows and sensor restart
effects prevent a causal conclusion about baseline handling. Do not declare
the shared accuracy issue fixed or assume ASC OFF had no measurement effect.
BE56FF remains unreachable and no policy change was verified on it.

### 16:28–16:29 Sydney — BE56FF retry succeeded

User requested another retry. BE56FF connected and reported 0.18.0, native
ASC ON, one-minute reporting, effective host correction zero, battery 84%.
Native ASC was paused and verified mode 1, errno zero, status 0x0084; host
correction remained zero. One fresh subsequent sample was 618 ppm, 25.89 C,
53.16% RH. One-minute reporting was preserved. This single later sample is
not directly compared with the earlier one-minute Screen/other Slim averages.
All four Slims now have verified native ASC OFF. No manual/FRC/reset was sent.
Evidence: indoor-settled-2026-10-05/be-retry.jsonl and summary.json.

### BE56FF five-second mode change — abnormal low raw readings

User explicitly requested five-second reporting on BE56FF. Command 0x05 mode 3
was sent; an old asynchronous 0x08 state notification caused the first helper
to assert before fresh readback. Reconnection showed mode 3 already applied.
Delayed fresh readback and sensor timestamp increments confirmed five-second
reporting. Native ASC remained verified OFF, mode 1/status 0x0084.

After the change, raw sensor packets gave 175, 170, 159, 158 ppm at five-second
intervals. These are abnormal for the indoor comparison and do not validate
accuracy; the helper passed only mode/policy/cadence checks. The previous
one-minute sample was 618 ppm. Cause of the mode-associated drop is not proven.
No offset, manual calibration, FRC or reset was applied. Final mode remains
five seconds, as requested; all four Slims now use five-second mode and ASC OFF.
Evidence: indoor-settled-2026-10-05/be-five-second-verified.jsonl.

## 2026-10-05 23:10 Sydney — Later status check

User requested current readings. F1D7C1 and BE56FF connected, both report
0.18.0, five-second mode, verified native ASC OFF (mode 1/status 0x0084),
effective host correction zero. F1D7C1 provided valid fresh measurements; exact
mean/range is in late-indoor-check-2026-10-05/summary.json. BE56FF repeatedly
sent ffaa010a44000000ffff00000000f6: fault byte 0x44 SENSOR_FAULT_CO2,
CO2 0xffff invalid sentinel, no valid RHT. This is an error frame, not
65,535 ppm or a zero-temperature/humidity observation. Battery reports 80%.
The specific driver error and cause are not exposed in this packet; ASC status
query success does not prove the current CO2 pipeline is healthy.

Screen AF94D2, 2325D4 and 81A9DC were not found on initial or follow-up scan.
User was asked to confirm same location and disconnect/move devices nearby;
no response during capture. No calibration, recovery, reset, power-mode change
or firmware update was sent. Current all-device agreement cannot be assessed.

### 23:15–23:16 Sydney — Remaining units reached on retry

A 40-second scan and fresh BLE connections reached Screen AF94D2, 81A9DC
and 2325D4. One-minute averages: Screen 534.9 ppm (531–539), 81A9DC 504.9
(496–513), 2325D4 404.6 (400–412), 12 packets each. Both Slims retain verified
native ASC OFF, five-second mode and effective host correction zero. All three
provide valid fresh measurements. Agreement remains incomplete: 2325D4 is
about 130 ppm below the contemporaneous Screen. Earlier F1D7C1 and BE56FF
captures were in a separate time window. No setting changes or recovery sent.
Evidence: late-indoor-check-2026-10-05/others-retry.jsonl and summary.json.

## 2026-10-05 — User-requested BE56FF sensor reset/offset clear attempt

User requested setting calibration offset to zero and a sensor reset. Effective
software offset already read zero because native-paused mode bypasses stored
corrections. Command 0x24 returned failure (ffaa2401ffcd). Error packets 0x44
continued. The command combines sensor recovery, stored correction clear,
and a fresh measurement; its failure reply does not reveal which stage failed.
The saved software offset clear cannot therefore be independently confirmed.

Non-calibrating 0x34 self-test returned errno 0, flags 0x0010, a successful
STCC4 self-test per datasheet section 3.4.12. Bit 4 indicates the SHT4x is not
connected through the dedicated STCC4 controller interface, consistent with
our host-supplied compensation path. This does not validate CO2 accuracy or
exclude a bad signed reading or integration/compensation issue.

A bounded controller-only 0x27 restart and one 0x24 retry also returned failure.
Approximately 90 seconds of post-reset observation did not recover valid CO2.
Final settings read five-second mode, native ASC OFF, effective offset zero.
No sensor factory reset, FRC or native ASC-history erase was sent. Native
calibration history remains; MCU restart is not a verified sensor power cycle.
Evidence: releases/v0.18.0-native-asc-controls/be56ff-reset-2026-10-05/.
Source: https://sensirion.com/resource/datasheet/STCC4.
