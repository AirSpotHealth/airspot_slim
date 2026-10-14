# Slim history repair — 16 September 2026

Device: AirSpot-81A9DC, J-Link 603003276. Installed repair: 0.17.3.

The reported symptom was two records on the phone graph. The installed image
was 0.16.9 with forensic external-flash write blocking enabled, verified from
the image header, backed-up image strings and RTT. A fresh BLE download found
seven retained CO2 entries in half-page 6, with uptime timestamps 352–2152;
pages 0–5 were blank. These entries have no reliable calendar dates. The older
metadata count of 96 is not evidence of 96 recoverable readings.

Two additional implementation defects affected normal logging:

- Slim sampled the latest cached value on a separate five-minute timer. Screen's
  committed `scd4x/co2.c` logs each accepted CO2 measurement. Slim now queues each
  valid measurement with its acquisition-time timestamp and corrected value,
  and the main thread writes it once. The 32-entry queue keeps flash access off
  the sensor workqueue. Invalid readings and idle on-demand periods add nothing;
  a full queue or write failure is logged explicitly.
- Flash values are stored big-endian. The history sender failed to decode that
  storage representation before sending the documented little-endian BLE value.
  The Slim app consequently interpreted 491 ppm as 60161. The sender now decodes
  before serialization; existing stored bytes require no migration.

## Verification

- Signed production build and artifact/wiring checks passed; 61 host tests pass.
- Regression tests failed before the fixes and pass afterward: each valid
  callback is queued; queued values/timestamps survive later cache or calibration
  changes; an empty queue never re-saves a cached sample; history frames preserve
  values, timestamps, padding, page identity and checksum.
- On 0.17.3, four BLE-downloaded records had timestamps 1789595882, 1789595887,
  1789595892 and 1789595897, with values 1758, 1756, 1784 and 1788 ppm. Each
  matched its live BLE sample exactly. This verifies five-second saving in the
  device's existing real-time mode. Other modes were covered by callback/queue
  tests; their full physical timing cycles were not exercised in this session.
- All 13 entries captured before a software restart remained readable afterward.
  The fresh boot log confirmed SOFTWARE reset, successful startup and recovery
  of page 3 with 19 entries (additional samples were saved before the restart).
- Settings were preserved byte-for-byte during installation. External history
  was not erased. The device clock was synced using the app's local-time wire
  convention. Missing historical readings and absent calendar dates were not
  reconstructed.
- Phone UI verification remains with the user after reconnection. A local Flutter
  test attempt was blocked by the host's Dart kernel/SDK mismatch; the configured
  Puro wrapper also failed with an incompatible executable architecture. No
  mobile source or environment changes were made.

Evidence: `local_backups/slim-history-20260916/`. Original full internal-memory
backup: `local_backups/production-000603003276-20260916T114626580113Z/`.
Final installation and readback:
`local_backups/production-000603003276-20260916T115639846413Z/`.

## Screen-style event history — 0.17.4

The same external circular history now receives measurements and diagnostic
records through one ordered queue. Screen's numeric IDs and 16-bit payloads are
preserved; MCU/sensor-specific error details are described below.

| ID | Event and payload | Trigger |
|---|---|---|
| 0 | Corrected CO2 ppm | Every valid acquired sample |
| 1 | Battery voltage in mV (0 if voltage read failed) | Runtime low-battery lockout |
| 3 | Sensor read error, value 1 | First invalid sample of each failure episode |
| 4 | Screen reset reason code | Each application boot; watchdog/lockup/software/wake/pin mapped to 2/4/3/5/1 |
| 5 | Sensor user reset, value 1 | Successful sensor reset command |
| 6 | Signed new correction, two's complement | Successful manual or automatic correction |
| 7 | ASC enabled, 0/1 | Successfully saved ASC setting |
| 9 | Calibration target | Manual-start group or completed ASC cycle |
| 10 | Old unsynced seconds, low 16 bits; timestamp is newly synced time | First phone clock sync per boot |
| 11 | Signed ASC adjustment | Successful automatic adjustment |
| 12 | ASC lowest reading | Completed ASC cycle |
| 13 | Signed previous correction | Manual-start group or completed ASC cycle |
| 14 | Manual calibration started, value 1 | Accepted start after any necessary power-mode change succeeds |
| 15 | New target ppm | Successfully saved calibration target |
| 16 | DFU start, value 1 | MCUboot image manager starts an upload |
| 17 | DFU failure, value 0 | Image manager reports stopped/failed upload, first OTA rejection per boot, or recovered rollback request |
| 19 | Flight setting, 0/1 | Successfully saved flight-mode command |
| 21 | Startup SDK error, low 16-bit errno magnitude | First startup diagnostic per boot |
| 22 | Fault reason | Recovered Zephyr fatal reason, or watchdog worker bitmask |

Manual start queues IDs **14, 13, 9** as one group; ASC completion queues
**9, 12, 13, 6** as one group. Calibration failure does not falsely emit a
successful correction. The full subsystem/error and reset bitmask remain in
the existing NVS diagnostic journal. Its storage failures are not mirrored back
into the failing history queue, avoiding recursion.

The queue holds 64 entries and reserves eight for events when measurements are
backlogged. Groups are accepted entirely or rejected. A failed flash write keeps
its original entry at the head for retry. Overflow is reported through RTT and
the diagnostic journal. Power-off/lockout drains after stopping measurements;
explicit history erase removes old queued entries only when erase succeeds.

Fatal handling writes only a validated marker to `.noinit` RAM and reboots; it
never attempts SPI/NVS or takes locks from the fatal handler. A warm boot queues
that marker and clears it after a successful flash write. Retention is best
effort: host tests verify validation and retry, not physical crash survivability
through every MCUboot/reset path. Watchdog evidence also uses the pre-existing
retained NVS-journal path and can appear before and after its reset.

Limits that are intentionally not described as exact Screen parity:

- STCC4 has no Screen/SCD4x stabilization retry (18) or adjustable scale (20).
  Legacy IDs 2 and 8 remain defined but have no active equivalent event hook in
  this implementation. No fabricated records are emitted for unsupported actions.
- SDK and fatal values are Zephyr/Slim details, not Nordic Screen SDK fault IDs.
  Sensor error 1 identifies an invalid measurement; it is not a per-I2C errno log.
- An interrupted upload only emits failure when MCUboot reports it; a silent
  phone disconnect is not automatically classified as a failed upload.
- Early low-battery boot lockout occurs before flash initialization and does not
  attempt a flash write at unsafe voltage. Power removal can lose queued events.
- First-sync elapsed time wraps after 65535 seconds, as on Screen. The marker
  supplies recovery evidence; it does not itself reconstruct dates in the app.
  Clock/DST behavior is unchanged by this patch.

Mobile source now recognizes IDs 21/22, includes them in CSV ordering, and treats
only correction/adjustment values as signed. This also preserves unsigned
first-sync values above 33000 seconds. These app changes require an app build;
the already-installed phone app has not been replaced in this session.

Validation: 66 firmware host tests passed, including queue ordering/reserve/wrap,
retry after flash failure, atomic calibration groups, retained-fault validation,
erase boundaries, protocol failures and diagnostic/DFU events. The production
signed build and wiring/artifact check passed. Ten mobile protocol tests passed
using the installed stable Dart SDK to run Flutter tools directly. The broader
history integration test could not initialize the missing macOS Isar native
library (`IsarNotReadyError`); no toolchain/library installation was performed.

0.17.4 installation backup and verified readback:
`local_backups/production-000603003276-20260916T122645082382Z/`.
Settings bytes were preserved from 0x15c000; external history was not erased.

Running-device evidence after installation: RTT confirms successful 0.17.4
startup, recovery of 397 records and five-second measurements. A read-only
J-Link RAM snapshot found an empty pending queue, 436 in the internal total
counter, and completed events `(type 4, value 3, timestamp 1)` and
`(type 10, value 44, timestamp 1789597695)`. The current page contained CO2
1806/1809/1815 ppm at timestamps 1789597853/58/63. Queue removal occurs only
after the flash write returns success. This is running-state evidence, not an
independent external-flash readback. See `running-history-0.17.4.json`.

The phone automatically reconnected and supplied first sync. Independent BLE
readback and reversible flight-mode verification are pending its disconnection;
`verify_events.py` is ready. No flight setting was changed during the failed
scan. J-Link logging has been stopped and the device remains running.

## Slim hardware scope correction — 0.17.5

The 0.17.4 flight-mode hook was inappropriate for Slim and is removed. Also
removed the existing inactive buzzer/vibrator/DND runtime fields, setters,
getters, persistence loading and schedule helpers. Software ASC no longer reads
a flight flag. LPS22HH-to-STCC4 pressure compensation remains automatic.

The shared packet slots are reserved zeros, and the eight obsolete schema-1
snapshot bytes are skipped while decoding and zeroed while encoding. Supported
settings retain their original byte offsets and CRC format. The old individual
NVS keys are ignored; no destructive migration is required. Screen firmware and
its app controls are unchanged. Flight record ID 19 remains reserved in the
shared enum; Slim emits no new records of that type.

Tests cover snapshots with old enabled flags, ignored legacy keys, supported
settings retention, disabled wire fields, unsupported commands producing no
history, and calibration with no flight gate. The mobile capability tests
confirm Slim has no screen, sound, vibration, DND or flight controls.

0.17.5 validation: 67 firmware host tests and 11 mobile protocol/capability tests
passed. Production signed build and wiring checks passed. J-Link installation
readback verified and settings bytes preserved from 0x15c000. Backup:
`local_backups/production-000603003276-20260916T123646925405Z/`.

Live 0.17.5 startup completed successfully, restored the saved high-power mode,
and resumed five-second measurements with valid pressure readings (1026 hPa).
RTT evidence: `local_backups/slim-history-20260916/rtt-0.17.5.txt`.
The temporary J-Link logger was stopped; the device remains running.
