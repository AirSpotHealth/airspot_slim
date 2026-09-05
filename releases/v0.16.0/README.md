# AirSpot Slim 0.16.0 — calibration candidate

Qualification-only OTA candidate; startup software-calibration behavior remains under review. Select
`airspot_slim_v0.16.0_dfu.zip` in the app's Slim local firmware updater. This is a
signed application update with the existing MCUboot layout and product key.
Do not erase settings or install a different bootloader. The candidate is tested over BLE on Slim AirSpot-C58CC9.

## Changes

- STCC4 internal ASC is unconditionally paused and status bit 2 is verified at
  startup/reset and for every accepted measurement. Failed commands or unexpected
  status prevent successful readings instead of silently allowing internal ASC.
- The Auto Calibration flag controls AirSpot software ASC only. The firmware no
  longer exposes a driver operation to resume the sensor's internal ASC.
- Software ASC uses the lowest three-reading average, the Screen 420 ppm low-value
  correction (except in Flight Mode), and the configured target after seven
  qualifying days. Default stays off; saved toggle, target and correction survive OTA.
- The daily check occurs once at 23:59 in device time, requires valid time and at
  least 400 readings, and does not count a calendar day twice after clock changes.
  As on Screen, insufficient samples carry forward. Reboot restarts the observation
  window. Missing days are not backfilled. 0x29 supports accelerated qualification;
  zero restores the normal schedule, and on-demand mode does not accelerate.
- Manual calibration uses raw samples; software ASC uses corrected, unscaled
  samples. Manual operations exclude ASC updates and restart its observation window.
  Signed overflow, missing samples and storage failures are reported as failures.
- Live data and history apply the shared correction exactly once. Automatic changes
  use existing history record types; signed totals remain readable through 0x25.
- Sensor reset/configuration and measurement sequences are serialized. Reset still
  clears the software correction on success, as before, but preserves the software
  auto-calibration preference. No hardware FRC or ASC-history reset is issued.
- Updated mobile code interprets Slim's reset byte as operation status, refreshes
  settings after reset/toggle, and displays calibration failures. Screen is unchanged.

## Protocol compatibility

0x11: existing success byte 0x02; 0xFF on save failure. 0x24: existing success byte
0x01; 0xFF if reset/verification fails. A short 0x0D response with byte 0xFF reports
manual/target-operation failure; existing eight-byte signed correction responses
are preserved. 0x2A returns current software ASC progress. Settings field offsets
and history layouts are unchanged. Install the companion app changes to see the
new failure responses correctly; older apps cannot reliably display these failures.

The ASC-pause command is `0x3FBC`. The testing flag is low-byte bit 2
(mask `0x0004`), per Sensirion's current embedded/Python driver documentation
(model 3.5.1). The June 2025 PDF linked in the original plan instead says low-byte
bit 6; that conflicts with the current driver and this physical Slim's `0x0084`
status. Do not use `0x0040` or `0x4000` as the testing flag.

Pinned primary reference:
https://github.com/Sensirion/embedded-i2c-stcc4/blob/4a190ae1323ccb0bd1fa86a690754d2ed8b6a545/stcc4_i2c.h#L121

Read-only Slim diagnostic command `0x33` preserves all existing command layouts.
Its six-byte payload is: schema version `1`, verification stage, signed big-endian
16-bit errno, then the CRC-checked big-endian 16-bit sensor status. Stages are
0=not attempted, 1=stop, 2=pause, 3=single shot, 4=read/status, 5=verified,
6=temperature/humidity measurement. It reports the last startup/reset verification,
not a new measurement. Normal live readings independently enforce the same status flag.

Pausing ASC does not undo any correction previously learned by the sensor.

## Automated validation

- Production build and signed-image/layout validator passed.
- All 29 firmware tests passed: sensor pause/status/CRC checks, failed reset commands,
  failed storage, calibration BLE acknowledgements, software ASC daily and accelerated
  schedules, invalid time, clock changes, raw/manual separation, signed limits and
  consistent live/history correction.
- Companion Flutter analyzer passed; all 51 Flutter tests passed; the actual candidate ZIP is checked by the
  app's firmware parser against its embedded version and image hash.
- `firmware-artifact.json` records build hashes; `SHA256SUMS` records package hashes.

## Physical qualification (2026-09-05)

Slim `AirSpot-C58CC9` was updated over BLE from 0.15.0 to this 0.16.0 image.
MCUboot reports the candidate active and confirmed after its own startup checks.
Screen `AirSpot-AF94D2` remains on 1.5.5. Both started in high sampling mode,
with Auto Calibration off and software correction zero.

- Read-only diagnostic returned stage 5, errno 0, status `0x0084`.
- Software Auto Calibration off/on preserved paused hardware status and produced
  live readings. Saved-on preference survived an MCU reboot; software day count
  restarted at zero.
- Sensor reset succeeded with the toggle both on and off, preserved that setting,
  and returned paused status `0x0084`. BLE stayed usable for recovery/diagnostics.
- The first candidates used incorrect status masks and rejected readings. They
  rolled back automatically; none was forced confirmed. The final mask follows
  the current Sensirion driver, as documented above.
- The on-at-boot test exposed low startup readings entering software ASC. It
  automatically added +85 ppm, increasing to +116 ppm before the toggle was
  disabled. A successful sensor reset restored the original zero correction.
  This was a software correction, not hardware FRC. A proposed startup observation
  guard is awaiting product clarification; it is NOT present in this image.
- Final reconnect verified software ASC off, correction zero, high sampling mode,
  progress zero and paused status. Device time was restored to Sydney local time.
  No correction was applied during the post-update comparison.
- BLE behavior was exercised directly; the updated Flutter app has not been
  installed or tested on a physical phone. Analyzer and automated tests passed.

Before updating, 12 paired readings over 59 seconds averaged 1179.3 ppm on Slim
(range 1171–1193) and 716.9 ppm on Screen (716–717): Slim was 462.4 ppm higher.
After the update and reset tests, 36 pairs over about six minutes averaged 326.4 ppm
on Slim (279–861) and 800.4 ppm on Screen (792–818): mean difference -474.0 ppm.
The final pair jumped to 861/818 ppm; the run was not stable enough to establish
an accuracy result. These readings do not show that the accuracy discrepancy is
fixed. Longer controlled stabilization/reference testing remains outstanding.

This is a side-by-side field comparison, not an accuracy calibration with known gas.
A sensor's previous learned calibration is not undone by pausing its internal ASC.

Rollback to older Slim firmware restores its hardware-ASC toggle behavior; it is
not an equivalent calibration policy.
