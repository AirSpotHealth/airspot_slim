# Slim beta validation — 6 September 2026

> Archived on 13 September 2026. Historical evidence and plans; experiment
> profiles and commands described here may have been retired. See the
> [current Slim status](../STATUS.md) for supported workflow and open findings.

Historical host validation for candidate 0.17.0, based on firmware `5d255a008c8a07f85c0b13b2935b6e462c3fab33`
and mobile `a2e6f4f9466a3dcc113c1663501214c6aff861c4`, with local hardening changes.
See [findings and physical gates](BETA_HARDENING.md). **NO-GO for beta rollout**
until those physical gates pass. No device was flashed during this original
host-only phase. The subsequently authorized attached-device tests and corrected
0.17.1 artifact are recorded in [hardware validation](BETA_HARDWARE_CHECK.md).

## Artifact identity

Production MCUboot image hash:
`0e41e5283fed8a12db745b69603f0e6eac1ca053c3e447071be36597491347c9`.
Signed binary SHA-256:
`3699e14f29893372873e0ed7e08261d83db9918ff76e76790ce883cb6783baa9`.
OTA ZIP SHA-256:
`63459860ffea621a8ea151f396796a8aef20de627cb1682f800021bc34b0b3b8`.
Public-key SHA-256 matches the preserved baseline. The full hash inventory is
`application/build_production/firmware-artifact.json` and its final local backup.
Generated signing/container bytes can differ on rebuild; verify that build's
manifest before transferring any artifact.

## Host evidence

- Production firmware build and artifact/signature/wiring verification: passed.
- Separate qualification + adaptive BLE firmware build: passed.
- Native firmware suite: 60 tests passed (18.17 seconds).
- Flutter full suite: 69 passed, one optional real-package test skipped; separately
  ran that package test against this signed 0.17.0 artifact: all five image-parser
  tests passed, including the previously skipped real-package hash check.
- Flutter static analysis: no issues.
- iOS release build with codesigning disabled: passed, Runner.app 52.1 MB.
  This is compile/link evidence, not an installable signed distribution or BLE test.
- Android release APK: passed (AGP/Kotlin compatibility fixes included).
- Git whitespace checks: passed in both repositories.

Native tests compile production C against host fault-injection shims. They cover
ADC failures/freshness, health deadline latching, OTA authorization, corrupt and
failed settings writes, diagnostic CRC/torn records/queue/write failure/retained
hints, sensor ownership/ASC behavior, connection-scoped NUS and protocol buffers,
and concurrent history request identity. Address/undefined-behavior sanitizers
are enabled in the applicable C harnesses. Artifact tests validate signed image
and pinned production configuration. Provisioning test output refers to fake
host devices; it is not evidence of physical flashing.

Android APK SHA-256:
`0a3c5e4d364ddc1ebb2b20fc4050839c9409b775fd2d3096fbe2281a419ab966`.
iOS Runner executable SHA-256:
`b5ab8ef3b1217683693aed717e6203dd15bb682805fc3dedaf7949a24ff842e3`.
These local builds used the existing `.env.local` configuration; distribution
signing and physical platform testing remain separate release gates.

## Static resource allocation

| Measurement | 0.16.9 baseline | 0.17.0 production |
|---|---:|---:|
| ELF text + data | 269,052 B | 271,280 B |
| ELF data + bss | 86,284 B | 89,556 B |

Production linker-region allocation: FLASH 271,308 / 659,306 B (41.15%),
RAM 89,496 / 192,512 B (46.49%). ELF sections and linker-region accounting differ
slightly; these are static allocations, **not measured dynamic headroom**.
Qualification image linker allocation: FLASH 274,016 B, RAM 92,728 B (48.17%).
Bootloader: FLASH 32,124 B, RAM 19,872 B; boot RAM is reused after handoff.

Actual thread stack high-water marks, heap peaks, BLE throughput/latency,
energy per workload and runtime per charge remain unmeasured. Production retains
its baseline radio/transfer parameters; only duplicate battery ADC work was
removed. The opt-in profiles enable repeatable hardware measurement.

## Remaining build maintenance and release evidence

Android compile SDK moved from 36 to 37 because the installed
permission_handler_android 14.1.0 references API 37 symbols. AGP is 9.1.1 and
Gradle is 9.3.1; target SDK remains unchanged. Compatibility basis:
[Android API/AGP table](https://developer.android.com/build/releases/about-agp)
and [AGP 9.1 requirements](https://developer.android.com/build/releases/agp-9-1-0-release-notes).
Flutter warns about future removal of legacy Kotlin Gradle plugin support;
iOS warns that device_info_plus and mcumgr_flutter still require CocoaPods.
A scoped Gradle bridge enables file_picker Kotlin compilation while built-in
Kotlin is disabled; its version 11 AGP check otherwise omits the plugin classes.
These dependency migrations need a separate compatibility qualification.

No physical update cycles, power-cut swaps, stall recovery, interrupted NVS writes,
Screen coexistence, 72-hour soak, or current/energy measurements were performed.
The host shims do not validate scheduler timing, radio behavior, flash atomicity,
watchdog register retention or bootloader power-loss recovery on the device.
