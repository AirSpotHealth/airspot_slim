# Slim beta hardening execution

> Archived on 13 September 2026. Historical evidence and plans; experiment
> profiles and commands described here may have been retired. See the
> [current Slim status](../STATUS.md) for supported workflow and open findings.

Approved scope: audit and harden Slim firmware/mobile OTA, watchdog, persistent
fault records, measured performance, and beta evidence. Flashing was subsequently
authorized for the attached device; publishing remains excluded.
Preserve pre-existing provisioning/tooling changes and historical task records.

- [x] Preserve baseline revisions, diff, signed package, manifests and configs.
- [x] Revalidate earlier audit against current 0.16.9 source.
- [x] Implement recovery upload authorization independent of sensor health.
- [x] Serialize fresh battery authorization; reject stale/failed ADC reads.
- [x] Keep trial confirmation device-controlled and rollback deadline bounded.
- [x] Preserve installed/rollback/health-failure distinctions in mobile updater.
- [x] Add worker-progress watchdog supervision and retained failure evidence.
- [x] Add bounded diagnostic journal and validated mobile diagnostic export.
- [x] Make settings transactions atomic, protect migration across trial rollback.
- [x] Serialize factory sensor access; report/restore failed mode changes.
- [x] Isolate partial/queued commands and replies by BLE session.
- [x] Prevent concurrent history requests from relabeling an in-flight read.
- [x] Retain production BLE defaults; add explicit adaptive/measurement profiles.
- [x] Restore production history recording; retain opt-in forensic mode.
- [x] Add firmware/native and mobile regression checks.
- [ ] Physical iOS and Android OTA/interruption qualification (20 cycles each).
- [ ] Power-interrupted MCUboot swaps and comprehensive watchdog stall tests.
- [x] Attached-device early-reset rollback and protocol deadline/watchdog recovery.
- [ ] Actual stack/heap peaks under maximum combined workload.
- [ ] Phone latency/throughput and battery/energy A/B measurements.
- [ ] 72-hour soak with logs and retained settings/history evidence.

See ../docs/BETA_HARDENING.md for findings, interfaces and evidence limits.

## Authorized attached-device follow-up

- [x] Back up and flash the attached nRF54L15, preserving settings; verify readback.
- [x] Exercise signed OTA round trip and interrupted-upload resume over macOS BLE.
- [x] Verify remote inactive-image confirmation rejection and early-reset rollback.
- [x] Diagnose persisted watchdog/OTA and sensor startup faults on hardware.
- [x] Correct bulk slot preparation and interrupted conditioning in 0.17.1.
- [x] Pass 62 host firmware tests and real-package Flutter parsing.
- [x] Finish corrected-image occupied-slot, conditioning and watchdog checks.

See ../docs/BETA_HARDWARE_CHECK.md for exact evidence and limitations.
