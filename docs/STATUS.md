# AirSpot Slim status

Updated 13 September 2026. Target: production nRF54L15 Slim, pinned NCS v3.2.1.
The local source reports 0.17.1; this cleanup is not a published release or a
hardware-validated replacement for the previously tested 0.17.1 image.

## Current firmware

The production configuration provides CO2, temperature/humidity and pressure
sensing, external-flash history, existing AirSpot BLE commands, persistent
settings, software ASC, Qi/button/LED handling, signed MCUboot OTA and rollback,
worker watchdog supervision, factory tests, and persistent fault reporting.
Use [the workspace commands](../README.md) to build and verify it. Memory figures
are in [the memory report](../application/MEMORY_USAGE.md).

## Experiment cleanup

Removed qualification stack/heap reporting, adaptive BLE tuning, forensic flash
capture and write blocking, regulator PWM control, and compensation timing
telemetry. Removed their build options, hooks and dedicated experiment tests.
Diagnostic command IDs 0x35 and 0x36 remain reserved and use the existing unknown
command behavior; they must not be reassigned to another operation.
Production BLE parameters, compensation cadence, flash wake recovery, calibration,
self-tests, fault reporting, settings format and OTA layout remain unchanged.

Automated host regression tests remain under `tests/`. The older qualification
plans and results are [archived](archive/README.md); they are not an active work
queue. No benchmarking, Mesh implementation, device flashing or rollout is part
of this cleanup. Mesh remains disabled as before.

## Known limitations

The [last hardware record](archive/BETA_HARDWARE_CHECK.md) documents signed OTA,
rollback, watchdog recovery and focused iPhone checks. A pressure-sensor failure
after supply interruption cleared following a longer cold start, but its cause
and automatic recovery remain unresolved; see the [investigation](PRESSURE_RECOVERY_INVESTIGATION.md).
Comprehensive Android/phone interruption coverage, all-mode soak, runtime stack/heap
peaks and battery measurements remain unproven. Archiving the plans does not
mark those checks as passed.

## Cleanup verification

The pinned-toolchain production build and signed artifact/wiring checks passed.
All 60 remaining host tests passed (62 before retirement; two tests existed solely
for the removed supply and forensic experiments). Compensation cadence, flash
wake timing, OTA recovery and production fault handling retain regression coverage.
Whitespace checks and current documentation entry-point links passed. No device
was flashed and no firmware was published.
