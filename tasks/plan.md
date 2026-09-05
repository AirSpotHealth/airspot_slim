# Rev3A firmware migration plan

Assessment date: 2026-09-05. Status: initial port and hardware startup verified; full qualification pending. See docs/board/REV3A_BRINGUP.md for current commands and evidence.

## Repository baseline

- Current branch: `test/stcc4-frc-asc`, HEAD `4012c394832a6410d3bbd789646ee2170095495b`.
- Local `main`, remote-tracking `origin/main`, and live remote `main` point to the same commit. `git ls-remote --heads origin` reports no `develop` branch. This establishes the shared baseline, not proof that it works on Rev3A.
- Seventeen tracked files have staged and/or unstaged changes. These include STCC4 calibration/ASC, conditioning, sensor management, settings, logging, power gesture code, and build/RTT tooling. Untracked files include the Rev3A schematic, sensor meeting notes/source archive, release binaries, and a release RTT configuration.
- User subsequently explicitly requested discarding the pending changes. Tracked edits, the untracked release RTT configuration, and generated untracked release binaries were discarded. Schematic, plan and source reference documents were retained.
- Switched to `main`, then created `feature/rev3a-nrf54l15` in this checkout. The older LED worktree remains untouched. Calibration work from the discarded branch will not be reintroduced.

## Hardware evidence

Source: `docs/datasheets/AH-013-0001-SCH CO2 Monitor (Standard Build) PWA Schematics Rev3A.PDF`, sheets 2-9 visually inspected; all sheets text-extracted. Correction to initial assessment: service color indices already match Rev3A; RGB did not need rewiring in firmware.

Sheet 5 specifies **nRF54L15-QFAA**. Current build tooling targets `nrf54l15dk/nrf54l05/cpuapp`, and J-Link commands specify `NRF54L05_M33`.

| Signal | Current application overlay | Rev3A schematic |
| --- | --- | --- |
| Button | P2.10, active high | P0.02; sheet 8 transistor circuit indicates active high |
| Pressure interrupt | Not represented in application overlay | P2.10 |
| Flash HOLD | P1.11 | P1.06 |
| Flash WP | P1.13 | P1.07 |
| Red | service LED_RED=1, led1 P2.08 | Same |
| Green | service LED_GREEN=2, led2 P2.06 | Same |
| Blue | service LED_BLUE=0, led0 P2.09 | Same |
| I2C SCL/SDA | P0.03 / P0.04 | Same |
| SPI SCK/MOSI/MISO/CS | P2.01 / P2.02 / P2.04 / P2.05 | Same |
| Battery divider enable / ADC | P1.04 / P1.05 | Same; divider factor 0.6 verified on sheet 2 |
| UART TX/RX | P1.02 / P1.03 | Same |
| Qi status | P0.00 | Same |

Additional Rev3A controls: nPM ERR P0.01, CHG P1.08, MODE P1.09, SHPACT P2.03; Qi EN1 P1.12 and EN2 P1.13. The old flash WP hog drives a pin now connected to Qi EN2. The old firmware must not be assumed electrically compatible.

Sheet 9 explicitly records the button/pressure-interrupt swap, removal of SWO routing from the backup programming header, replacement of LED1 with 150044M155260, smaller U2/U3 BGA packages, jumper/component removals, and designator changes. It does not explicitly list an MCU change; the MCU choice is established by sheet 5.

## Architecture and scope

- Add explicit Rev3A board selection with the existing sensor/service architecture. Keep legacy targeting available for regression builds.
- Retain the pinned NCS 3.2.1 initially. Local Zephyr reports `ncs-v3.2.1` and includes nRF54L15 targets. Verify manifest/module consistency before building.
- The documented upstream target is `nrf54l15dk/nrf54l15/cpuapp`: https://docs.zephyrproject.org/latest/boards/nordic/nrf54l15dk/doc/index.html. A DK target alone is insufficient: disable inherited DK peripherals/pins and inspect final generated devicetree. Prefer an explicit custom board once boot/build behavior is established.
- Preserve BLE UUIDs, command/reply formats, history format, settings and calibration semantics. Document any intentional behavior changes separately.
- Keep OTA disabled during this port. Audit the currently inconsistent monolithic/MCUboot configuration before supporting bootloader builds. Do not expand or relocate settings simply because the new MCU has more memory.
- Review System OFF separately: it is currently disabled in Kconfig with an explanation tied to P2.10. Moving the button enables a new wake design, but does not prove safe wake behavior or power consumption.
- Address demonstrated reliability problems with reproducible tests; avoid an unrelated broad refactor.

## Ordered implementation tasks

### 1. Recover a reproducible baseline
Dependencies: none. Files: workspace setup and validation record.
Acceptance: discard pending code as explicitly requested; establish migration branch from main; build the shared committed baseline and record exact SDK/toolchain and output.
Verification: confirm branch base and tracked files restored before starting; clean build without flash; record failures rather than calling a historical commit known-good.

### 2. Implement Rev3A electrical mapping
Dependencies: 1. Files: board definition/overlay and board documentation (split into small commits if more than five files).
Acceptance: all MCU nets audited against all relevant schematic sheets; button, RGB and flash control mappings correct; no inherited DK pin conflicts or unintended outputs on power controls.
Verification: generated DTS/config inspection, compile Rev3A and legacy targets, physical pin/LED/button checks when board available.

### 3. Make build and debug tooling board-aware
Dependencies: 2. Files: `scripts/airspot`, `application/build.sh`, recovery scripts and artifact metadata, in separate increments.
Acceptance: SoC, overlay, J-Link target, artifact name and metadata agree; incompatible cached builds/artifacts cannot silently flash; debug/UART/RTT and release builds select the intended configuration.
Verification: shell syntax and argument validation; clean build matrix; inspect runners and artifacts without flashing.

### 4. Validate memory and wired recovery
Dependencies: 3. Files: partition and sysbuild/MCUboot configuration.
Acceptance: application/settings boundaries verified for nRF54L15; monolithic path reproducible; bootloader path either validated independently or explicitly unavailable.
Verification: inspect map/partition output and image addresses; wired boot/recovery on a test unit; persistence across supported updates. No production erase or provisioning during assessment.

### Checkpoint: board bring-up
Rev3A boot, correct LEDs/button, I2C identities and flash JEDEC verified on hardware before enabling deeper power changes.

### 5. Validate baseline sensor/calibration behavior
Dependencies: 1 and 4. Files: STCC4/SHT40 drivers, sensor manager, calibration service, settings, in separate changes.
Acceptance: discarded test-branch changes stay discarded; baseline behavior verified; calibration failures do not persist success; conditioning/measurement timing does not cause watchdog resets or BLE starvation.
Verification: protocol/datasheet checks; targeted tests for CRC, transport/timeouts, calibration errors and persistence; hardware sensor readings and BLE responsiveness.

### 6. Validate charging and power transitions
Dependencies: 4 and 5. Files: Qi driver, power manager, main, board configuration and Kconfig.
Acceptance: Qi receiver status distinguished from battery charger status; safe control defaults; power gestures, charge attach/detach and button wake correct; watchdog recovery remains effective.
Verification: repeated battery/charger boot and sleep/wake cycles; ADC comparison to meter; measured active/idle/off current; fault injection for GPIO/bus errors.

### 7. Prove feature compatibility and release readiness
Dependencies: all previous tasks. Files: focused tests and validation/release documentation.
Acceptance: BLE pairing/reconnect, all supported commands, live measurements, history rollover/reboot recovery, settings/calibration persistence, factory test and wired updates verified; reset/fault handling and power budget documented.
Verification: run existing validation plan plus Rev3A-specific matrix; long-duration soak and power interruption tests on real board; document measured results, unresolved failures and exact artifacts.

## Open hardware inputs

- Availability of an assembled Rev3A board, programmer and serial logs for bring-up.
- Confirmation that the assembled BOM matches the supplied standard-build schematic and whether any wires/rework exist.
- Product acceptance thresholds for current consumption and measurement accuracy, using existing specifications where available.

Initial port and tooling changes build successfully for Rev3A debug/release/RTT, with eleven artifact regression tests passing. Actual legacy NVS is at 0x74000, not the unused YAML address 0x52000. New monolithic devicetree enforces app [0, 0x74000) and storage [0x74000, 0x7d000). Production readiness remains unproven. User confirmed the probe and UART are connected to Rev3A. Existing RRAM was backed up, programming independently verified, and RTT logs confirmed live readings, flash metadata recovery and BLE advertising. Hardware-discovered watchdog-scan and STCC4 timing faults were corrected; UART diagnosis, complete app/gesture checks and production qualification remain.

## Authorized continuation: production target and Bluetooth OTA
User requested Bluetooth DFU, RTT diagnostics and one production hardware target.
1. Use shared board wiring and internal MCUboot slots; build signed images with an untracked private key. Verify generated bootloader and app configurations.
2. Enable MCUmgr BLE image/reset services, startup health confirmation and watchdog rollback. Verify accepted/rejected images and startup.
3. Replace revision-specific build names with production commands and validate signed artifacts before programming. Preserve NVS on initial migration.
4. Exercise wireless upload, test boot, confirmation and recovery on the connected board where host Bluetooth permits. Record remaining physical/mobile qualification.

OTA hardware checkpoint: valid signed trial confirmed after sensor startup. Deliberately failing 0.14.1 test firmware timed out at 120 seconds and MCUboot reverted to working 0.14.0 with readings/advertising. Settings migration/readback passed. GRTC/LFCLK handoff needed a bootloader-only wrapper and LTO disabled. Wireless transfer remains blocked by host Bluetooth powered off; asynchronous user request is pending.
