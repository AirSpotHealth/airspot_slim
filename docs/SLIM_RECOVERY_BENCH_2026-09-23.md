# Slim recovery bench session — 23 September 2026

Target: AirSpot-81A9DC, J-Link 603003276, firmware 0.17.7.

## Baseline

User observed first hold fading out, second hold fading in and reconnecting.
This is consistent with initial state ON, followed by OFF then ON. A dark LED
is not proof of OFF. RTT auto-search did not locate the control block; explicit
address 0x20004050 from the local ELF enabled capture. Baseline capture:
`/tmp/slim-recovery-addressed-20260923.log`.

Baseline showed valid readings (1380 then 1312 ppm), battery 71–72%, Qi charging,
and BLE connected. Captured startup reset cause was 0x20 (debug), so this is not
proof of an undisturbed System OFF wake. Final qualification requires unplugged
debug signals and BLE/user observations.

## Fault experiment

An explicit extra Zephyr module under tests/hardware/sensor_fault wraps
stcc4_read. First 60 seconds after the first measurement read use real readings; later calls return -EIO and
invalidate the output. Each reboot restarts this window. This allows a valid
sample for image confirmation. It does not reproduce sleep/init errors or hangs.

Build directory: application/build_sensor_fault. Normal restoration build:
application/build_production (artifact validation passed before installation).
Neither production source nor production build configuration includes the module.
ELF disassembly verified both measurement call sites target __wrap_stcc4_read.
Eight selected host tests passed; signed artifact validation passed.

Installation backup:
`local_backups/production-603003276-20260923T111733035571Z`.
Install transcript: `/tmp/slim-sensor-fault-install.log`.

Physical fault/off/on observations are pending. Emergency gesture implementation
and qualification are pending the baseline fault sequence. Do not describe the
customer's failure as reproduced based on injected errors alone.

Initial install passed readback and preserved settings. Bench timing was corrected
to use elapsed time from the first read, since reset-time uptime was nonzero.
The host test now starts with uptime 700000 ms and verifies the relative threshold.
Corrected install transcript: `/tmp/slim-sensor-fault-corrected-install.log`.

Corrected image installation passed readback, preserved settings, and restarted.
Backup: `local_backups/production-603003276-20260923T111914885734Z`.
Capture: `/tmp/slim-sensor-fault-corrected-live-20260923.log`.
Observed first real sample at device uptime 00:14:24 (884 ppm). At 00:15:24,
`BENCH TEST: injecting STCC4 read -EIO; no valid CO2 sample` appeared.
The following generic sensor summary prints cached 884 ppm even though the
measurement is invalid; do not mistake that log line for successful recovery.
App error confirmation and manual power cycle are still pending.

## First failed wake, debugger attached

User reports fade-out followed by no response to the next hold. RTT confirms
STCC4 sleep succeeded and System OFF entry at 00:23:15.741; no subsequent boot.
Stopped RTT and preserved logs in local_backups/recovery-failure-20260923.
A brief CPU halt/register capture (then resume, no reset) found PC 0x2abf8,
which resolves to the WFE loop in nrf_regulators_system_off, explicitly labelled
"Solution for simulated System OFF in debug mode" in the pinned Nordic HAL.
BASEPRI=0x40. GPIO reads: P0 IN=0x18 (Qi active-low asserted, button released),
LATCH=0x4 (button detected), P0.02 PIN_CNF=0x20000 (input, sense high).
Thus the button reached hardware; no app wake gate ran in this captured attempt.

This is a debugger-confounded failure, not proof of sensor-caused field failure.
Nordic documentation says debug interface mode emulates System OFF and requires
normal-mode exit followed by pin reset; further qualification must remove debug
signals. https://docs.nordicsemi.com/r/bundle/ps_nrf54l15/page/debug.html-debuginterfacemode
CPU/GPIO capture scripts and transcripts are in the same backup directory.

## Repeat without debug lead

User reports the unit powered on when the programming lead was disconnected
after CTRL-AP RESET_HARD. Thus injected measurement failure plus off/on did
not reproduce an unwakeable device in that attempt. Second cycle requested.

## Recovery candidate (not yet physically qualified)

The running button queue now emits warning at 10 s, ready at 15 s, cancellation
at 18 s, and recovery only on release in [15,18) seconds. Recovery dispatch
precedes calibration checks and does not acquire sensor locks. It requests an
MCU reboot with settings preserved; it is not a power-rail reset or a separate
sensor-free boot mode. Normal startup already permits failed sensors with BLE.

System OFF wake applies the same release policy before starting sensors/BLE.
A failed/missing Qi signal only disables normal wake, not emergency wake.
Normal five-second on/off now completes on release before ten seconds; this
prevents normal shutdown consuming a longer emergency hold. At five seconds
the wake gate gives a brief white cue; emergency warning is amber flash and
ready is rapid white flash. Holding through 18 seconds cancels and requires
release then a fresh press. Battery and OTA checks remain in the normal boot.

Build outputs: application/build_recovery (no fault injection) and the existing
application/build_sensor_fault (explicit fault module if rebuilt). Do not ship
the fault image. Physical gesture qualification and restoration are pending.

Recovery candidate validation: all 75 host tests passed with the Nordic toolchain
Python (system Python lacked the existing artifact tests' cbor dependency).
Recovery firmware built and artifact checks passed. No sensor read wrapper is
linked in the normal recovery candidate. Hardware installation is pending
reconnection of the programming lead after the independent baseline test.

## Recovery candidate installed

User confirmed a second successful fault/off/on cycle with debug disconnected.
On reconnection, installed application/build_recovery; backup is
`local_backups/production-603003276-20260923T114818174886Z`.
Readback verified; settings bytes preserved. RTT capture
`/tmp/slim-recovery-candidate-live-20260923.log` confirms the new gesture banner,
successful startup and a real 1489 ppm sample with battery 83%.
Stopped RTT and issued CTRL-AP RESET_HARD before requesting debug lead removal
for physical emergency-hold testing. Candidate retains version label 0.17.7;
use the recorded artifact hash/build path, not that label, to identify this build.
Physical normal/emergency/cancellation gesture qualification remains pending.
