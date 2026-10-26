# Slim 0.21.0: continuous sensing, recovery and battery qualification

Status: engineering candidate, not installed or physically qualified.
Assignment: https://app.basecamp.com/5949526/buckets/49366756/card_tables/cards/10440435332

## Operating contract

The user selected continuous CO2 acquisition with temperature/humidity and pressure compensation every second in all modes. Now publishes on request; 3 min, 1 min and 5 sec select reporting and history-writing intervals. Slower reporting does not put the STCC4 to sleep. Firmware 0.21.0 defaults implement this contract; `stcc4_operating_policy.conf` also makes the intended configuration explicit.

Native sensor calibration is used without host CO2 offsets. Starting the guided recovery enables native ASC without erasing calibration or readings. A previously saved native-ASC pause remains until recovery is started. Manual FRC is rejected in this profile. The app exposes recovery and removes the old fixed battery-life claims for Slim firmware >=0.21.0. Do not distribute another profile under that version unless it implements the same contract.

## Battery estimate — assumptions, not a measured claim

The repository schematic specifies a nominal 75 mAh cell. Usable capacity depends on age, temperature, load and cutoff. Sensirion specifies approximately 0.95 mA typical continuous STCC4 current; this is sensor-supply current, not total battery current. Regulator conversion and the MCU, radio, other sensors, LEDs and flash must be included.

For a **hypothetical 60 mAh usable capacity**, battery-side average-current scenarios are:

| Average battery current | Calculated runtime |
|---|---:|
| 1.0 mA | 60 h |
| 1.2 mA | 50 h |
| 1.5 mA | 40 h |
| 2.0 mA | 30 h |
| 3.0 mA | 20 h |

Thus 30–50 hours is a planning scenario if the complete device averages 1.2–2.0 mA. It is not yet a prediction supported by current measurements. Runtime = usable battery mAh / measured average battery mA. The old multi-day low-mode claims do not apply to this continuous-sensing policy.

Source: https://sensirion.com/resource/datasheet/STCC4 (D1, June 2025). Confirm against the exact fitted part/revision before qualification.

## Power work completed

| Area | Implementation or audit finding |
|---|---|
| Sensor cadence | One-second worker accounts for I2C execution time; continuous sensing retained as requested |
| CO2 reports/history | Only at selected reporting interval or explicit Now request |
| Battery ADC | Acquisition path reuses voltage for five seconds instead of reading every second; existing battery safety/OTA freshness checks retained |
| Battery BLE service | Only changed percentages invoke the stack notifier; repeated values no longer request radio traffic |
| Bluetooth connection | Requests latency 4 with existing 30–50 ms interval and 4 s timeout; central may decline; physical compatibility qualification required |
| Logging | Application/default log level reduced to errors; routine sensor summary is debug-only. PRINTK remains enabled because immediate logging requires it |
| Temperature/pressure | Existing conversion waits yield CPU; precision unchanged pending accuracy evaluation |
| External flash | Existing power-down between operations retained; no new per-second history writes |
| Recovery progress | Existing five-minute persistent checkpoints, not per-second writes |
| OTA health | Every acquisition contributes to trial-image health even in Now/slow reporting modes |

No percentage energy saving is claimed from these code changes. Sensor conversion power remains dominant; sleep/single-shot policies would change the user's required sensing behavior.

## Customer instructions

1. If readings seem wrong, connect the updated app and choose **Check my readings → Start 12-hour check**. This enables sensor auto-calibration and preserves calibration history and saved readings.
2. Keep Slim powered for 12 uninterrupted hours in ordinary indoor air, away from heat and direct sun. Charge if needed. Every reporting mode continues sensing on this firmware. The device tracks progress; the phone need not stay connected. A restart, invalid sensor operation or interruption restarts the continuous interval.
3. When prompted, detach Slim from the phone and charger and expose it to fresh outside air for 10 minutes, sheltered from rain/direct sun and away from breath, traffic and exhaust. A thoroughly ventilated room can be used, but an open window does not itself establish fresh-air conditions.
4. Start the fresh-air step in the app and confirm only after the exposure. Slim cannot identify fresh air automatically. Completion does not certify accuracy or force a particular ppm value. If results remain suspect, compare with a trusted reference or contact support.

The 12-hour policy follows the user's instruction. The 10-minute fresh-air step comes from the assignment's existing guidance; it is not a manufacturer-approved forced-recalibration reference or stability criterion.

## Required hardware qualification before customer release

Use at least three Slims. Record serial, firmware hash, battery age/capacity, ambient conditions and test setup.

- Measure battery-side current without charger or debugger attached, at representative battery voltages, in every mode. Include disconnected advertising, connected idle, Now requests, history sync, LEDs/alerts and reconnects. Capture peaks and integrate charge over long enough to include five-minute checkpoints.
- Verify negotiated Bluetooth latency on iOS/Android, reconnect, history transfer and signed OTA. Confirm trial-image health in Now and 3-minute modes.
- Run full discharge tests through the actual low-battery cutoff; calculate usable capacity and runtime. Compare old/new firmware on the same hardware and workload before quoting savings.
- Run the full 12 hours plus fresh-air step; test reboot, sensor failure, unplugging/recharging and phone disconnection. Confirm ordinary recovery does not erase history or perform FRC.
- Compare against a suitable reference in changing CO2, attached to a phone, while charging and after thermal stabilization. Define acceptance tolerances before judging results.
- Record factory preparation per serial. Do not claim all shipped devices are prepared until that process is actually operating.

Host tests and successful compilation do not substitute for these measurements. Keep the Basecamp card open pending qualification.

## Build and storage maintenance

Use one incremental build directory per actively tested configuration. `source scripts/airspot` then `airspot build`; use `--clean` only when a pristine rebuild is required. `AIRSPOT_BUILD_DIR` can select another explicit directory. `AIRSPOT_SIGNING_KEY` selects the existing product key; never copy it into an artifact package.

The launcher now enables ccache for both app and MCUboot, uses a shared workspace-local `.cache/ccache`, limits it to 256 MB by default, and checks compiler content. `CCACHE_DIR` and `CCACHE_MAXSIZE` can override the location/limit. Inspect with `ccache --show-stats` after sourcing the environment. Compiler caching accelerates builds; it does not reduce device current. Avoid unsafe cache options or caching fresh temperature/pressure values beyond the required one-second cadence.

Sixteen obsolete original-checkout build directories were selected for reversible cleanup. Their full contents are preserved in `local_backups/build-cleanup-2026-10-27/obsolete-builds.tar.gz`, with a manifest. Production build, releases, device snapshots, the historical backup directory, dependencies and worktrees are retained. The SDK and archived firmware are not redundant source files; removing them would need a separate dependency/history audit.

## Verification recorded 27 October 2026

- 116 host firmware tests passed using the Nordic Python toolchain, including real-C harnesses for all-mode acquisition, compensation failures, battery sampling/notifications, recovery persistence and signed-artifact checks.
- Signed nRF54L15 application/MCUboot build passed the production artifact validator, both incremental and pristine with compiler caching enabled. The repeat build recorded 347 cache hits; the cumulative cache was 11.4 MB with a 256 MB cap. This does not establish a build-time speedup percentage.
- 13 focused Flutter tests passed; analysis of all seven changed app/test files reported no issues. UI was widget-tested, not exercised on a physical phone.
- Obsolete build cleanup removed 947,131,528 bytes of original files, preserved a 221,377,378-byte recovery archive, and reclaimed 725,754,150 bytes net before the new compiler cache. Archive contents were fully read back before deletion.
- No device was flashed; provisioning messages in the host tests come from temporary fixtures. No release was published and the Basecamp card remains open.

## 0.21.1 startup correction

The 0.21.1 candidate supersedes 0.21.0 for testing. Sensirion datasheet §1.1.4 describes an initial bypass output of 390 ppm for 20 seconds in continuous mode (two single-shot measurements). It can restart during the initial learning period or after factory reset. Because the host cannot establish the sensor's saved ASC history with certainty, this build conservatively withholds readings until more than 20 seconds after every successful continuous-mode start. It never filters a reading solely because its value is 390 ppm.

Compensation and acquisition continue during bypass. Reports carry startup status rather than a valid CO2 value; those samples cannot confirm OTA health or trigger a sensor-recovery loop. Actual compensation and sensor failures remain faults. Periodic modes publish the first valid post-bypass sample promptly; Now remains request-driven. Double-press LED readout uses white for unavailable samples, never a default green zone.

Conditioning remains enabled **conditionally** after long idle or unknown idle history, as in this candidate's evaluation baseline. The 22-second conditioning command, the 20-second bypass guard and the 12-hour recovery procedure are separate operations. Passing the short guard is not evidence of full warm-up or accuracy.

The updated app shows a neutral “Preparing sensor…” notice on device cards and the current-reading view, with unavailable values in neutral colour. Real errors retain recovery/report controls. The notice clears when a valid report arrives; in Now mode request a fresh reading to update it.

Read-only Nordic probe scan found zero supported devices. No firmware was installed. Battery/current, physical OTA/Bluetooth, 12-hour endurance and reference-meter accuracy qualification remain outstanding.
