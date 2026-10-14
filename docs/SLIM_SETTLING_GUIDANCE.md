# Slim calibration recovery and low-power qualification

Candidate firmware: 0.19.0. BE56FF is the user-authorized comparison unit;
the other physical units retain their existing firmware. Physical observations
and image hashes are recorded in the workspace's
`diagnostics/slim-2026-10-13/` directory.

## Recovery policy

Ordinary MCU restart, sensor restart and reset preferences preserve calibration.
Only the explicitly confirmed calibration erase starts the recovery workflow.
Legacy full factory reset remains destructive for protocol compatibility.

The app shows device-reported progress on the reading and settings screens:
12 hours of healthy continuous native-ASC operation in ordinary indoor air,
followed by a user-started 10-minute fresh-air exposure and acknowledgement.
The UI's five-second reporting mode uses continuous STCC4 operation internally,
with fresh host compensation every second. Slow/on-demand shots do not earn
this continuous settling time. Phone disconnection does not stop device progress.
The persistent fresh-air prompt appears on reconnect; this implementation does
not send OS/background push notifications while disconnected.

The device saves erase intent before issuing the destructive sensor command.
Interrupted erasure remains visibly uncertain. Reset time is seconds since 2000
or zero when the device clock is unknown; generation disambiguates resets.
Successful erasure resets manual correction and enables native ASC/continuous
operation. Flash errors are reported, not presented as successful recovery.

Conservative product policy: a reboot, invalid sample, mode change, disabled
native ASC or gap longer than 10 seconds restarts unfinished continuous settling.
Completed settling survives reboot; interrupted exposure returns to the fresh-air
step. Sensirion does not state that every short interruption demands a new full
12 hours: this policy avoids certifying accumulated interrupted runtime as its
documented continuous initial-operation procedure. It can be refined only with
validated manufacturer guidance. The record is not internal ASC history.

Opening a window is useful only when outside air actually ventilates the room.
The app cannot verify 400 ppm from its own recovering sensor. Acknowledgement
does not certify accuracy, and an abnormal sensor still needs reference testing.

## Sources and measurement modes

- [STCC4 datasheet](https://sensirion.com/resource/datasheet/STCC4), sections
  1.1.3–1.1.4 and 3.4.6–3.4.9: initial continuous operation, long-idle warm-up,
  recommended wake/shot/read/sleep sequence, ASC interval 5–600 seconds and
  retention of compensation/ASC during sleep.
- [Design-in guide](https://sensirion.com/media/documents/4B786C73/68E5EE93/CD_AN_STCC4_Design-In_Guide_D1_1.pdf),
  section 3: SHT4x temperature/humidity input every second; pressure supplied
  according to local conditions. The one-second language does not explicitly
  distinguish single-shot operation; confirm per-shot host compensation with
  Sensirion before describing it as expressly approved.
- [Testing guide](https://sensirion.com/media/documents/D0ABC18E/6A1D5183/CD_AN_STCC4_Testing-Guide_D1.pdf):
  ventilation, stable conditions and comparison against a calibrated reference.

## Sleep comparison build

Production remains idle between single shots pending physical qualification.
Add `-Dapplication_EXTRA_CONF_FILE=single_shot_sleep.conf` to a separate sysbuild
directory to enable `CONFIG_AIRSPOT_STCC4_SINGLE_SHOT_SLEEP`. Do not reuse the
production build directory for this comparison.

The candidate sleeps after successful single shots, wakes before compensation
or other host commands, waits 5 ms after the wake byte, refreshes SHT40/LPS22HH
compensation, then measures/reads again. A wake-byte NACK is expected; subsequent
command failures propagate. Sleep does not issue a continuous-stop command after
each shot. Failed measurements stay awake for diagnosis. Continuous mode stays
awake. FRC in the sleep build requires continuous mode, since the single-shot FRC
procedure requires preceding shots to end in idle without sleep.

Sleep retains ASC state; it is not a factory reset or rail power cycle. Datasheet
typical STCC4 current is 55 µA idle versus 1 µA asleep. This is not a measured
whole-device battery-life improvement. Long on-demand gaps are separate: after
over three hours without measurements, conditioning and warm-up must be considered.
The existing firmware does not yet present a dedicated on-demand warm-up UI.
The datasheet's single-shot accuracy specification is stated at a 10-second
sampling interval. The recommended 5–600-second ASC range is not an explicit
accuracy guarantee for every interval in that range.

## Longer physical qualification procedure (not yet performed)

Use the user's AirSpot Screen as a comparison device. It is not a traceable
reference unless calibrated. Exclude currently faulting/30,000-ppm Slims from
claims about normal sleep accuracy; investigate them separately.

1. Place a healthy Slim and Screen together with free ventilation, away from
   breath, sun, vents and heat. Record device IDs, versions, ASC settings and
   Screen calibration history if known. Do not erase calibration for this test.
2. Record a continuous baseline, then idle single shots at 60 and 180 seconds.
   Compare using actual sample timestamps, not repeated BLE reads of old samples.
3. Install the comparison image on that same Slim while preserving settings and
   repeat with sleep enabled. Cross over back to idle to distinguish environmental
   drift from firmware effects. Prefer multiple Slims and repeated blocks.
4. Collect at least 30 new shots per stable block (30 minutes at 60 seconds;
   90 minutes at 180 seconds). Repeat battery-only and charging. Include a gentle
   room ventilation transition; never breathe directly onto either sensor.
5. Record raw/corrected CO2, status, sequence, T/RH/pressure, battery and charger,
   missing/fault samples, and Screen values. Report paired bias/spread, first-shot
   discontinuity and response lag separately. Measure current with suitable
   hardware; a battery percentage is not a microamp measurement.
6. Exercise continuous↔slow transitions, on-demand wake, self-test/settings/reset
   commands after sleep, failures and recovery. Test long-idle warm-up separately.

Promote sleep only after reviewing the physical results. Host tests establish
command ordering and state/error handling; they cannot establish CO2 accuracy,
thermal behaviour, ASC convergence or measured battery life.
