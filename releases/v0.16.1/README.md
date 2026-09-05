# AirSpot Slim 0.16.1 — software calibration startup guard

Keeps optional AirSpot software Auto Calibration (off by default) and manual
calibration. STCC4 internal ASC remains paused, independent of the toggle.

The 0.16.0 hardware test exposed temporary low readings entering automatic
calibration after reboot. This patch excludes the first three minutes after
sensor initialization or reset from automatic observations and qualification.
Both the immediate below-420 adjustment and weekly calculation respect this guard.
Live readings and user-initiated manual calibration remain available.

Toggling Auto Calibration or changing its target cannot bypass the remaining
startup guard. When it expires, averaging begins with fresh samples. Saved toggle,
target and correction settings survive the update; no hardware forced calibration
or calibration-history erasure is performed.

This is a startup safeguard, not a claim of sensor accuracy or full sensor warm-up.
Low readings after the guard still follow the configured automatic algorithm.
The unresolved Screen/Slim comparison is recorded in [0.16.0 qualification](../v0.16.0/README.md).

## Validation

- All 29 firmware tests passed, including startup/reset guard boundaries, no
  accumulation or accelerated qualification during the guard, toggle/target
  bypass prevention, three fresh samples after expiry, and existing calibration
  behavior and failures.
- Production build and signed-image/layout validation passed.
- All 51 Flutter tests passed using this exact signed candidate ZIP.
- Physical Slim AirSpot-C58CC9 updated to 0.16.1 and self-confirmed healthy.
  Software ASC on during startup left correction zero despite readings 246–337 ppm.
  Saved-on reboot produced readings 62–342 ppm with correction still zero.
  Sensor reset retained the on preference and zero correction during guarded readings
  including 291/333 ppm. Diagnostic status was 0x0084 (ASC paused).
- Final reconnect confirmed software ASC off, correction zero, high sampling mode,
  progress zero and paused hardware status. Sydney device time was restored.
- Manual calibration during the guard passed in the production C runtime harness;
  no manual offset or hardware FRC was applied on the physical unit.
- This patch changes automatic observation eligibility only. The unresolved
  accuracy comparison from 0.16.0 remains open; no new accuracy claim is made.
  The companion app has not been installed on a physical phone.

Use `airspot_slim_v0.16.1_dfu.zip` with the existing Slim MCUboot updater.
`firmware-artifact.json` identifies the image; `SHA256SUMS` identifies these files.
