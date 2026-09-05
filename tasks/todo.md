# Production board migration checklist

See `plan.md` for dependencies, acceptance criteria and verification.

- [x] Identify current branch, shared commit and live remote branches.
- [x] Inventory pending work without changing index or tracked source.
- [x] Inspect Rev3A MCU, UI and revision-history sheets.
- [x] Compare key nets with existing firmware overlay/tooling.
- [x] Save concrete migration plan.
- [x] User approved implementation and requested discarding test-branch changes.
- [x] Task 1: Discard requested edits, branch from main and build committed baseline.
- [x] Task 2: Review schematic and implement Rev3A overlay; hardware checks pending.
- [x] Task 3: Add board-specific build profiles, J-Link target and artifact checks.
- [x] Task 4 (build): Verify actual partitions, linker boundary and complete HEX/BIN images.
- [x] Task 4 (hardware): Verify wired programming and settings persistence.
- [x] Checkpoint: Program/verify Rev3A, capture RTT startup, confirm sensors/flash and BLE advertising.
- [x] Fix blank-flash watchdog reset and STCC4 command delays found on hardware.
- [x] Verify subsequent metadata recovery and live CO2 sample.
- [ ] Confirm mobile app commands, button gestures and charging behavior.
- [ ] Task 5: Verify main-branch sensor/calibration behavior; do not restore discarded changes.
- [ ] Task 6: Verify charging, power transitions and wake.
- [ ] Task 7: Run compatibility matrix, fault tests and hardware soak.
- [ ] Record release evidence and unresolved limitations.

## Production OTA
- [x] Build MCUboot and signed RTT application using shared production wiring
- [x] Implement startup health confirmation and update protections
- [x] Validate artifacts and preserve settings during wired migration
- [x] Verify valid trial update and failed-startup rollback on hardware (staged over J-Link)
- [x] Exercise wireless upload, trial boot and confirmation: 0.14.0 to 0.14.1 passed over BLE
- [ ] Qualify mobile updater, initial discovery/reconnect reliability and interrupted transfers
- [x] Update production commands/docs and record qualification limits

## Production audit execution
- [x] Preserve baseline signed image and configurations; baseline 14 checks pass
- [x] Remove proven unused targets/profiles/backups; preserve old_firmware and inputs
- [x] Fix short settings reads and scalar/name/DND persistence publication
- [x] Harden NUS connection lifetime, command framing and DND bounds
- [x] Preserve battery lockout on invalid samples and exclude them from calibration
- [x] Serialize mode changes with reads and sleep STCC4 on shutdown
- [x] Consolidate mobile soft-off with the existing button path
- [x] Gate driver logging; retain RTT
- [x] Add native fault tests and battery-event report tests (22 host checks pass)
- [ ] Complete remaining high-severity findings in PRODUCTION_AUDIT.md
- [ ] Final candidate hardware mode/reconnect/OTA qualification
- [ ] Capture actual phone app workload and qualify its updater
- [ ] Full charge-to-cutoff at 5 s, 60 s, 180 s sequentially
- [ ] Later profiler measurements and before/after energy comparison
