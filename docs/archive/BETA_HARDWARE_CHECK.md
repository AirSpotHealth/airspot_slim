# Slim attached-device verification — 6 September 2026

> Archived on 13 September 2026. Historical evidence and plans; experiment
> profiles and commands described here may have been retired. See the
> [current Slim status](../STATUS.md) for supported workflow and open findings.

The user authorized wired flashing and OTA testing after the host-only audit.
Target: nRF54L15, J-Link 603003276, BLE name AirSpot-81A9DC. Tests use a macOS
Bleak/MCUmgr client, not the Flutter iOS/Android application.

**Use the final 0.17.1 hash below.** The original 0.17.0 and intermediate 0.17.1
builds exposed real recovery defects and must not be released.

## Firmware defects found and corrected

| Priority | Hardware finding | Final correction |
|---|---|---|
| High | Upload preparation on a nonempty slot exceeded the OTA worker deadline and reset the device | Enable SDK progressive image preparation; avoid bulk RRAM flattening while retaining the ten-second worker deadline and trailer cleanup |
| High | MCU reset during conditioning or continuous sampling left the powered CO2 sensor in an incompatible state, so sensing never started | On the initial boot attempt only, wait out a possible conditioning cycle, wake, stop measurement, wait the full stop interval plus margin, then retry conditioning once; ASC verification and a live sample still determine health |

The startup retry waits 24 seconds with watchdog keepalives. The stop attempt
gets at least 1,250 ms even after an ambiguous I2C error; the driver already waits
1,200 ms on success. Conditioning is attempted once more, and a permanent failure
remains a health failure. Runtime reinitialization does not take this additional
wait, preserving the existing 35-second worker deadlines.

An initial wait-only retry did not restore idle state. A sleep/wake variant fixed
conditioning interruption but failed after one watchdog reset when entering sleep
was rejected. The final path avoids that unnecessary sleep transition and does
not issue a shared-bus general-call reset. The wake helper's expected NACK is not
considered proof of recovery: subsequent conditioning, ASC and sensor checks are.
See Sensirion's [STCC4 command reference](https://sensirion.github.io/python-i2c-stcc4/api.html)
for wake-to-idle, stop timing and conditioning behavior.

## Observed checks

- Wired installations passed chip/capacity validation, full internal-memory
  backup, readback, unchanged settings bytes at 0x15c000, and unchanged auxiliary
  memory. External history was not erased; startup recovered 96 stored records.
- Signed 0.17.0 → 0.16.9 → 0.17.0 wireless round trip passed hash/version checks,
  test boot, reboot, automatic confirmation and resumed live samples.
- Deliberate disconnect at upload byte 65,664 left the running image confirmed;
  reconnect resumed at that offset. No wired rescue was used for that interruption.
- The initial older-image upload timeout was preserved in the logs. A subsequent
  reproduction on 0.17.0 recorded OTA deadline bit 8 and WATCHDOG reset cause 16,
  identifying the occupied-slot preparation problem. Installing the corrected
  candidate initially required a settings-preserving wired flash.
- With progressive preparation, seeded 768 bytes into the secondary slot,
  disconnected, and started a different image. Its first chunk completed in
  0.0897 seconds; the complete signed upload succeeded without a reset while
  sensing continued. Later full uploads replaced complete existing images too.
- A complete repair OTA succeeded while the running intermediate firmware had
  genuinely failed sensor initialization. BLE recovery was not sensor-gated.
- Remote permanent confirmation of an inactive image was rejected (SMP error 1),
  with unchanged image state.
- Early reset of an unconfirmed image rolled back to the previously confirmed
  image. A primary-slot readback matched the intended trial's signed bytes. One
  diagnostic capture used an oversized J-Link hex length; only the matching
  image-length prefix was used. The separate six-second reset tests omit that
  capture so its duration cannot affect their timing.
- Final hash below uploaded successfully in 68.89 seconds, booted as a trial,
  was observed active/unconfirmed, then self-confirmed after live sensor checks
  at approximately 34 seconds. Subsequent BLE verification matched the final
  confirmed hash. These timings are for this host client, not phone benchmarks.

- Final image: injected an expired protocol deadline through SWD. The journal
  recorded protocol bit 4, then reset cause 16 (WATCHDOG); retained mask 5 includes
  the main deadline expiring after feeding stopped. All 16 retrieved records
  passed CRC validation. Sensing resumed after reboot with valid samples.
  This tests deadline supervision and watchdog recovery, not a physically blocked
  worker or every subsystem's stall behavior.
- Final image: reset again six seconds after restarting, interrupting conditioning.
  The bounded wake/stop/reconditioning path succeeded, settings selected the same
  five-second mode, history recovered 96 records, and valid sensor samples resumed.
  BLE subsequently reported the exact final hash active and confirmed, with no
  pending update. The device is left on that image.

## Final artifact and automated evidence

Version: 0.17.1. MCUboot image hash:
`44d7eff174e3aac57df502055524c57ee7d41b95a903b2d5853aaaa1aa9c0251`.
Signed binary SHA-256:
`a81c7096b32ccf1cdb7d05d67a7b45e37ea6902600f11a9fb6f577917b211dad`.
OTA ZIP SHA-256:
`2d9ffed6a57eb799313a31b670885388d0d21e4c3ca9c344e30cc3c751f1b357`.
The product signing key, bootloader layout and unpaired/signed-downgrade policies
are unchanged. No firmware was published and no rollout was performed.

Production application allocation is 270,908 bytes flash (41.09% of its region)
and 89,496 bytes static RAM (46.49%). These are linker allocations, not measured
stack/heap peaks. MCUboot uses 32,124 bytes flash and 19,872 bytes RAM.

62 firmware tests passed, including bounded recovery and an artifact guard
against re-enabling bulk image preparation. Production and qualification builds
passed. All five Flutter image-parser tests passed against this final signed ZIP.
Earlier full Flutter/static-analysis/Android and iOS build evidence remains in
[BETA_VALIDATION.md](BETA_VALIDATION.md).

Evidence/harnesses: `local_backups/beta-hardware-20260906/`.
Full wired backups:
`local_backups/production-000603003276-20260906T092102328447Z/` and
`local_backups/production-000603003276-20260906T093412600177Z/`.
Intermediate images are retained only for reproducibility; identify builds by
hash, not their shared local 0.17.1 version string.

## Limits of the initial macOS phase

The preceding checks are focused firmware confidence testing on one board. They do not establish
mobile-app iOS/Android behavior, actual power-loss recovery during swap, low-battery
edge behavior, settings/history fault injection, flash endurance, stack/heap peaks,
energy improvement, all sensing modes or the 72-hour soak. Only the existing
five-second sensing mode was exercised. Formal beta release gates remain unmet;
this report records narrower, concrete firmware evidence.

## iPhone follow-up — corrected target

The user subsequently identified AirSpot-C58CC9 as the intended supply-powered
board. The earlier AirSpot-81A9DC results above apply to that earlier board only.
The stopped iPhone run on 81A9DC completed one confirmed downgrade to 0.16.9
before the user corrected the target; its return update was not started.

On C58CC9, a temporary Flutter entrypoint called the actual SlimFirmwareImage
parser and SlimDfuService, with the production mcumgr_flutter native transport,
on the connected iPhone 17 Pro Max (iOS 26.6.1). No fake manager was used.
This exercised the updater service, not hosted download or the complete app UI.

| Transition | Result | End-to-end time | Transfer evidence |
|---|---|---|---|
| Confirmed 0.16.6 → final 0.17.1 | Exact target hash active/confirmed | 81.893 s | Full upload, ~44.5 s including older firmware preparation |
| 0.17.1 → signed 0.16.9 | Exact target hash active/confirmed | 67.302 s | Full upload, ~20.1 s |
| 0.16.9 → final 0.17.1 | Exact target hash active/confirmed | 41.572 s | Reused valid secondary image; not another full transfer |

Post-reset image reads temporarily timed out, then recovered through the
service's bounded verification loop. Each installation passed confirmation,
and final RTT showed resumed live samples. The board started with 64 history
records; no erase was requested. The test made no signing-key/bootloader changes.
Evidence and temporary harness: local_backups/beta-hardware-20260906/ios-c58cc9/.

Power interruption is still pending confirmation of supply-only power and manual
output control. Android is not connected. Phone interruption/cancellation tests,
hosted-download/UI flow, 20 cycles per platform and soak remain unperformed.

## Controlled supply interruption — C58CC9

Verified installed bootloader bytes against the production ELF's binary. Backed
up internal RRAM, requested a signed 0.16.9 trial, and halted at boot_write_status
(PC 0x48be, caller boot_move_sector_up) after moving one sector but before
recording completion. Captured changed RRAM (1,930 differing bytes); settings
region remained byte-identical. Cleared the breakpoint without resuming.

User switched output off; J-Link measured VTref=0.000V, then debugger exited.
Initial restore checks still read zero; subsequent check measured 3.150V. No
manual reset or flash command was issued during the recovery observation.

The swap resumed and booted 0.16.9 active/unconfirmed. LPS22HH WHO_AM_I read
failed with -5, so the trial refused confirmation. At uptime 121.606 seconds
it rebooted automatically, rolled back, and BLE verified the exact final
0.17.1 hash active/confirmed, with no pending update. This verifies recovery at
one interrupted sector-move boundary, not arbitrary mid-program brownouts.

**Open hardware finding:** pressure-sensor I2C failure persisted on the confirmed
0.17.1 rollback image. BLE remains accessible, but sensing is not healthy. A
longer cold power-off and power-path investigation are pending; do not interpret
successful rollback as resolution of this sensor failure.

Evidence: local_backups/beta-hardware-20260906/power-c58cc9/.

### Follow-up cold start

After the user reported a 30-second output-off interval and restored 4.02 V,
LPS22HH answered WHO_AM_I=0xB3 and initialization succeeded. Live pressure was
1018 hPa, with CO2/temperature/humidity samples resuming at uptime 32.430 seconds.
History recovery reported 64 records. BLE verified final hash 44d7eff174e3aac57df502055524c57ee7d41b95a903b2d5853aaaa1aa9c0251
active/confirmed, no pending image. Debug logging was stopped afterward.

The longer cold start clears the observed sensor failure, but does not establish
its root cause or prove automatic sensor recovery after arbitrary power ramps.
Keep that finding open for power-path/sensor investigation. No firmware change
or wired reflash was needed to recover the interrupted swap and final board.
